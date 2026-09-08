#!/usr/bin/env python3
"""Export Pure3D level render meshes to glTF 2.0 for lightmap baking.

The exporter is deliberately non-destructive. It reads one or more P3D files,
writes a glTF plus binary buffer, and writes a manifest with stable source IDs.
Those IDs are intended to be carried through the Blender lightmap workflow.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterator

MESH = 0x00010000
PRIMGROUP = 0x00010002
POSITIONLIST = 0x00010005
NORMALLIST = 0x00010006
UVLIST = 0x00010007
COLOURLIST = 0x00010008
INDEXLIST = 0x0001000A
SHADER = 0x00011000
SHADER_TEXTURE_PARAM = 0x00011002
SHADER_INT_PARAM = 0x00011003
SHADER_FLOAT_PARAM = 0x00011004
SHADER_COLOUR_PARAM = 0x00011005
TEXTURE = 0x00019000
IMAGE = 0x00019001
IMAGE_DATA = 0x00019002

BASE_TEXTURE = int.from_bytes(b"TEX\0", "little")
DIFFUSE = int.from_bytes(b"DIFF", "little")
ALPHA_TEST = int.from_bytes(b"ATST", "little")
ALPHA_THRESHOLD = int.from_bytes(b"ACTH", "little")
# Core glTF 2.0 supports PNG and JPEG images. Pure3D format 1 is PNG.
IMAGE_EXTENSIONS = {1: ".png"}

TRIANGLES = 0
TRIANGLE_STRIP = 1


class P3DError(RuntimeError):
    pass


@dataclass(frozen=True)
class Chunk:
    offset: int
    chunk_id: int
    data_end: int
    end: int


@dataclass
class Primitive:
    source_file: str
    mesh_name: str
    mesh_ordinal: int
    primitive_ordinal: int
    shader: str
    primitive_type: int
    vertex_format: int
    declared_vertex_count: int
    declared_index_count: int
    positions: list[tuple[float, float, float]] = field(default_factory=list)
    normals: list[tuple[float, float, float]] = field(default_factory=list)
    uv: dict[int, list[tuple[float, float]]] = field(default_factory=dict)
    colours: list[tuple[float, float, float, float]] = field(default_factory=list)
    indices: list[int] = field(default_factory=list)


@dataclass
class ShaderInfo:
    name: str
    shader_type: str
    translucent: bool
    base_texture: str | None = None
    diffuse: tuple[float, float, float, float] = (1.0, 1.0, 1.0, 1.0)
    alpha_test: bool = False
    alpha_cutoff: float = 0.5


@dataclass
class TextureInfo:
    name: str
    image_name: str
    image_format: int
    data: bytes
    alpha_depth: int


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def pstring(data: bytes, offset: int, end: int) -> tuple[str, int]:
    if offset >= end:
        raise P3DError("truncated PString")
    size = data[offset]
    start = offset + 1
    finish = start + size
    if finish > end:
        raise P3DError("PString extends beyond chunk data")
    return data[start:finish].rstrip(b"\0").decode("latin-1", "replace"), finish


def child_chunks(data: bytes, begin: int, end: int) -> Iterator[Chunk]:
    cursor = begin
    while cursor < end:
        if cursor + 12 > end:
            raise P3DError(f"truncated chunk header at 0x{cursor:x}")
        chunk_id, data_length, total_length = struct.unpack_from("<III", data, cursor)
        if data_length < 12 or total_length < data_length or cursor + total_length > end:
            raise P3DError(f"invalid chunk at 0x{cursor:x}")
        yield Chunk(cursor, chunk_id, cursor + data_length, cursor + total_length)
        cursor += total_length


def floats(data: bytes, offset: int, count: int) -> tuple[float, ...]:
    return struct.unpack_from(f"<{count}f", data, offset)


def vector_list(data: bytes, chunk: Chunk, width: int) -> list[tuple[float, ...]]:
    payload = chunk.offset + 12
    if payload + 4 > chunk.data_end:
        raise P3DError("truncated vector-list count")
    count = u32(data, payload)
    start = payload + 4
    required = start + count * width * 4
    if required > chunk.data_end:
        raise P3DError("truncated vector list")
    raw = floats(data, start, count * width)
    return [tuple(raw[i:i + width]) for i in range(0, len(raw), width)]


def colour_rgba(value: int) -> tuple[float, float, float, float]:
    return ((value >> 16 & 255) / 255.0, (value >> 8 & 255) / 255.0,
            (value & 255) / 255.0, (value >> 24 & 255) / 255.0)


def p3d_uv_to_gltf(uv: tuple[float, float]) -> tuple[float, float]:
    # Pure3D's PC assets use the DirectX top-left image origin, while glTF UVs
    # use a bottom-left origin. Without this conversion Blender shows textures
    # vertically mirrored.
    return uv[0], 1.0 - uv[1]


def scan_resources(path: Path) -> tuple[dict[str, ShaderInfo], dict[str, TextureInfo]]:
    """Read material definitions and the highest-resolution embedded images."""
    data = path.read_bytes()
    shaders: dict[str, ShaderInfo] = {}
    textures: dict[str, TextureInfo] = {}

    def walk(begin: int, end: int) -> None:
        for chunk in child_chunks(data, begin, end):
            payload = chunk.offset + 12
            if chunk.chunk_id == SHADER:
                cursor = payload
                name, cursor = pstring(data, cursor, chunk.data_end)
                if cursor + 4 > chunk.data_end:
                    continue
                cursor += 4  # version
                shader_type, cursor = pstring(data, cursor, chunk.data_end)
                if cursor + 16 > chunk.data_end:
                    continue
                translucent = u32(data, cursor) != 0
                info = ShaderInfo(name, shader_type, translucent)
                for child in child_chunks(data, chunk.data_end, chunk.end):
                    cp = child.offset + 12
                    if child.chunk_id == SHADER_TEXTURE_PARAM and cp + 4 <= child.data_end:
                        parameter = u32(data, cp)
                        texture_name, _ = pstring(data, cp + 4, child.data_end)
                        if parameter == BASE_TEXTURE:
                            info.base_texture = texture_name
                    elif child.chunk_id == SHADER_COLOUR_PARAM and cp + 8 <= child.data_end:
                        parameter, value = struct.unpack_from("<II", data, cp)
                        if parameter == DIFFUSE:
                            info.diffuse = colour_rgba(value)
                    elif child.chunk_id == SHADER_INT_PARAM and cp + 8 <= child.data_end:
                        parameter, value = struct.unpack_from("<II", data, cp)
                        if parameter == ALPHA_TEST:
                            info.alpha_test = value != 0
                    elif child.chunk_id == SHADER_FLOAT_PARAM and cp + 8 <= child.data_end:
                        parameter = u32(data, cp)
                        if parameter == ALPHA_THRESHOLD:
                            info.alpha_cutoff = max(0.0, min(1.0, struct.unpack_from("<f", data, cp + 4)[0]))
                shaders[name] = info
            elif chunk.chunk_id == TEXTURE:
                texture_name, texture_cursor = pstring(data, payload, chunk.data_end)
                # version, width, height, bpp, alphaDepth
                alpha_depth = (u32(data, texture_cursor + 16)
                               if texture_cursor + 20 <= chunk.data_end else 0)
                # The first IMAGE child is mip 0 (the highest-resolution image).
                for image in child_chunks(data, chunk.data_end, chunk.end):
                    if image.chunk_id != IMAGE:
                        continue
                    ip = image.offset + 12
                    image_name, cursor = pstring(data, ip, image.data_end)
                    if cursor + 28 > image.data_end:
                        break
                    image_format = u32(data, cursor + 24)
                    for image_data in child_chunks(data, image.data_end, image.end):
                        if image_data.chunk_id != IMAGE_DATA:
                            continue
                        dp = image_data.offset + 12
                        if dp + 4 > image_data.data_end:
                            continue
                        size = u32(data, dp)
                        start = dp + 4
                        if start + size <= image_data.data_end:
                            textures[texture_name] = TextureInfo(
                                texture_name, image_name, image_format,
                                data[start:start + size], alpha_depth)
                        break
                    break
            elif chunk.data_end < chunk.end:
                walk(chunk.data_end, chunk.end)

    walk(0, len(data))
    return shaders, textures


def parse_primitive(data: bytes, chunk: Chunk, source: str, mesh_name: str,
                    mesh_ordinal: int, primitive_ordinal: int) -> Primitive:
    cursor = chunk.offset + 12
    if cursor + 4 > chunk.data_end:
        raise P3DError("truncated primitive header")
    _version = u32(data, cursor)
    cursor += 4
    shader, cursor = pstring(data, cursor, chunk.data_end)
    if cursor + 20 > chunk.data_end:
        raise P3DError("truncated primitive header fields")
    prim_type, vertex_format, vertex_count, index_count, _matrix_count = struct.unpack_from(
        "<IIIII", data, cursor)
    primitive = Primitive(source, mesh_name, mesh_ordinal, primitive_ordinal,
                          shader, prim_type, vertex_format, vertex_count, index_count)

    for child in child_chunks(data, chunk.data_end, chunk.end):
        payload = child.offset + 12
        if child.chunk_id == POSITIONLIST:
            primitive.positions = vector_list(data, child, 3)
        elif child.chunk_id == NORMALLIST:
            primitive.normals = vector_list(data, child, 3)
        elif child.chunk_id == UVLIST:
            if payload + 8 > child.data_end:
                raise P3DError("truncated UV-list header")
            count, channel = struct.unpack_from("<II", data, payload)
            required = payload + 8 + count * 8
            if required > child.data_end:
                raise P3DError("truncated UV list")
            raw = floats(data, payload + 8, count * 2)
            primitive.uv[channel] = [tuple(raw[i:i + 2]) for i in range(0, len(raw), 2)]
        elif child.chunk_id == COLOURLIST:
            if payload + 4 > child.data_end:
                raise P3DError("truncated colour-list header")
            count = u32(data, payload)
            if payload + 4 + count * 4 > child.data_end:
                raise P3DError("truncated colour list")
            values = struct.unpack_from(f"<{count}I", data, payload + 4)
            # pddiColour is stored as ARGB. glTF vertex colours are linear RGBA.
            primitive.colours = [colour_rgba(c) for c in values]
        elif child.chunk_id == INDEXLIST:
            if payload + 4 > child.data_end:
                raise P3DError("truncated index-list header")
            count = u32(data, payload)
            if payload + 4 + count * 4 > child.data_end:
                raise P3DError("truncated index list")
            primitive.indices = list(struct.unpack_from(f"<{count}I", data, payload + 4))
    return primitive


def parse_file(path: Path, root: Path) -> list[Primitive]:
    data = path.read_bytes()
    source = path.relative_to(root).as_posix() if path.is_relative_to(root) else path.name
    result: list[Primitive] = []
    mesh_ordinal = 0

    def walk(begin: int, end: int) -> None:
        nonlocal mesh_ordinal
        for chunk in child_chunks(data, begin, end):
            if chunk.chunk_id == MESH:
                cursor = chunk.offset + 12
                name, cursor = pstring(data, cursor, chunk.data_end)
                if cursor + 8 > chunk.data_end:
                    raise P3DError(f"truncated mesh header in {path}")
                _version, _group_count = struct.unpack_from("<II", data, cursor)
                this_mesh = mesh_ordinal
                mesh_ordinal += 1
                primitive_ordinal = 0
                for child in child_chunks(data, chunk.data_end, chunk.end):
                    if child.chunk_id == PRIMGROUP:
                        result.append(parse_primitive(data, child, source, name,
                                                      this_mesh, primitive_ordinal))
                        primitive_ordinal += 1
                    elif child.data_end < child.end:
                        walk(child.data_end, child.end)
            elif chunk.data_end < chunk.end:
                walk(chunk.data_end, chunk.end)

    walk(0, len(data))
    digest = hashlib.sha256(data).hexdigest()
    for primitive in result:
        setattr(primitive, "source_sha256", digest)
    return result


def triangle_indices(primitive: Primitive) -> list[int]:
    source = primitive.indices or list(range(len(primitive.positions)))
    if primitive.primitive_type == TRIANGLES:
        return source[:len(source) // 3 * 3]
    if primitive.primitive_type == TRIANGLE_STRIP:
        output: list[int] = []
        for i in range(len(source) - 2):
            a, b, c = source[i:i + 3]
            if i & 1:
                a, b = b, a
            if a != b and b != c and a != c:
                output.extend((a, b, c))
        return output
    return []


class GltfBuilder:
    def __init__(self) -> None:
        self.binary = bytearray()
        self.buffer_views: list[dict] = []
        self.accessors: list[dict] = []

    def accessor(self, values: list[tuple] | list[int], component_type: int,
                 kind: str, target: int, minimum=None, maximum=None) -> int:
        while len(self.binary) % 4:
            self.binary.append(0)
        offset = len(self.binary)
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[kind]
        code = {5123: "H", 5125: "I", 5126: "f"}[component_type]
        flat = values if kind == "SCALAR" else [v for row in values for v in row]
        self.binary.extend(struct.pack(f"<{len(flat)}{code}", *flat))
        view = len(self.buffer_views)
        self.buffer_views.append({"buffer": 0, "byteOffset": offset,
                                  "byteLength": len(self.binary) - offset, "target": target})
        accessor = {"bufferView": view, "componentType": component_type,
                    "count": len(values), "type": kind}
        if minimum is not None:
            accessor["min"] = minimum
        if maximum is not None:
            accessor["max"] = maximum
        index = len(self.accessors)
        self.accessors.append(accessor)
        return index


def safe_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_") or "mesh"


def find_blender(explicit: Path | None) -> Path | None:
    if explicit:
        return explicit.resolve()
    command = shutil.which("blender")
    if command:
        return Path(command)
    program_files = Path(r"C:\Program Files\Blender Foundation")
    candidates = sorted(program_files.glob("Blender */blender.exe"), reverse=True)
    return candidates[0] if candidates else None


def generate_lightmap_uv(blender: Path, gltf: Path, resolution: int,
                         padding: int, angle: float, save_blend: bool) -> Path:
    script = Path(__file__).with_name("blender_generate_lightmap_uv.py").resolve()
    sidecar = gltf.with_suffix(".lightmap-uv.json")
    command = [str(blender), "--background", "--python-exit-code", "2",
               "--python", str(script), "--",
               "--input", str(gltf.resolve()), "--output", str(sidecar.resolve()),
               "--resolution", str(resolution), "--padding", str(padding),
               "--angle", str(angle)]
    if save_blend:
        command.extend(("--blend", str(gltf.with_suffix(".lightmap.blend").resolve())))
    subprocess.run(command, check=True)
    return sidecar


def quaternion_from_z(direction: tuple[float, float, float]) -> list[float]:
    """Quaternion rotating +Z onto direction (glTF directional light shines along -Z)."""
    x, y, z = direction
    length = math.sqrt(x * x + y * y + z * z)
    x, y, z = x / length, y / length, z / length
    dot = z
    if dot < -0.999999:
        return [1.0, 0.0, 0.0, 0.0]
    # cross((0, 0, 1), direction) = (-y, x, 0)
    qx, qy, qz, qw = -y, x, 0.0, 1.0 + dot
    qlen = math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw)
    return [qx / qlen, qy / qlen, qz / qlen, qw / qlen]


def export(primitives: list[Primitive], output: Path, embed: bool,
           shaders: dict[str, ShaderInfo], textures: dict[str, TextureInfo],
           sun_direction: tuple[float, float, float], sun_intensity: float,
           common_only: bool) -> dict:
    builder = GltfBuilder()
    materials: list[dict] = []
    material_by_shader: dict[str, int] = {}
    images: list[dict] = []
    gltf_textures: list[dict] = []
    texture_by_name: dict[str, int] = {}
    meshes: list[dict] = []
    nodes: list[dict] = []
    manifest_entries: list[dict] = []
    skipped: list[dict] = []

    for primitive in primitives:
        shader_info = shaders.get(primitive.shader)
        base_texture = shader_info.base_texture if shader_info else None
        if ("treeshadow_m" in primitive.shader.casefold() or
                (base_texture and "treeshadow_m" in base_texture.casefold())):
            skipped.append({"source": primitive.source_file, "mesh": primitive.mesh_name,
                            "primitive": primitive.primitive_ordinal,
                            "reason": "material or base texture is treeshadow_m",
                            "texture": base_texture})
            continue
        if common_only and "common" not in primitive.mesh_name.casefold():
            skipped.append({"source": primitive.source_file, "mesh": primitive.mesh_name,
                            "primitive": primitive.primitive_ordinal,
                            "reason": "mesh name does not contain 'common'"})
            continue
        indices = triangle_indices(primitive)
        reason = None
        if not primitive.positions:
            reason = "no POSITIONLIST"
        elif not indices:
            reason = f"unsupported/empty primitive type {primitive.primitive_type}"
        elif max(indices, default=-1) >= len(primitive.positions):
            reason = "index outside POSITIONLIST"
        if reason:
            skipped.append({"source": primitive.source_file, "mesh": primitive.mesh_name,
                            "primitive": primitive.primitive_ordinal, "reason": reason})
            continue

        attrs: dict[str, int] = {}
        mins = [min(v[i] for v in primitive.positions) for i in range(3)]
        maxs = [max(v[i] for v in primitive.positions) for i in range(3)]
        attrs["POSITION"] = builder.accessor(primitive.positions, 5126, "VEC3", 34962, mins, maxs)
        # Blender may remove unused vertices during glTF import. Preserve the
        # Pure3D vertex number explicitly for lossless corner-UV round trips.
        attrs["_P3D_VERTEX_ID"] = builder.accessor(
            list(range(len(primitive.positions))), 5126, "SCALAR", 34962)
        if len(primitive.normals) == len(primitive.positions):
            attrs["NORMAL"] = builder.accessor(primitive.normals, 5126, "VEC3", 34962)
        if len(primitive.uv.get(0, [])) == len(primitive.positions):
            attrs["TEXCOORD_0"] = builder.accessor(
                [p3d_uv_to_gltf(uv) for uv in primitive.uv[0]], 5126, "VEC2", 34962)
        if len(primitive.colours) == len(primitive.positions):
            attrs["COLOR_0"] = builder.accessor(primitive.colours, 5126, "VEC4", 34962)

        index_type = 5123 if max(indices) <= 65535 else 5125
        index_accessor = builder.accessor(indices, index_type, "SCALAR", 34963)
        material = material_by_shader.get(primitive.shader)
        if material is None:
            material = len(materials)
            material_by_shader[primitive.shader] = material
            factor = list(shader_info.diffuse) if shader_info else [1.0, 1.0, 1.0, 1.0]
            pbr = {"baseColorFactor": factor, "metallicFactor": 0.0, "roughnessFactor": 1.0}
            material_json = {"name": primitive.shader, "pbrMetallicRoughness": pbr,
                             "extras": {"p3dShaderType": shader_info.shader_type if shader_info else "unknown"}}
            texture_name = shader_info.base_texture if shader_info else None
            texture_info = textures.get(texture_name) if texture_name else None
            if texture_info and texture_info.image_format in IMAGE_EXTENSIONS:
                gltf_texture = texture_by_name.get(texture_name)
                if gltf_texture is None:
                    image_dir = output.parent / f"{output.stem}_textures"
                    image_dir.mkdir(parents=True, exist_ok=True)
                    # A short hash prevents case-insensitive and sanitization collisions.
                    name_hash = hashlib.sha1(texture_name.encode("latin-1", "replace")).hexdigest()[:8]
                    image_file = (safe_name(texture_name) + "_" + name_hash
                                  + IMAGE_EXTENSIONS[texture_info.image_format])
                    image_path = image_dir / image_file
                    image_path.write_bytes(texture_info.data)
                    image_index = len(images)
                    images.append({"name": texture_name,
                                   "uri": f"{image_dir.name}/{image_file}"})
                    gltf_texture = len(gltf_textures)
                    gltf_textures.append({"name": texture_name, "source": image_index, "sampler": 0})
                    texture_by_name[texture_name] = gltf_texture
                pbr["baseColorTexture"] = {"index": gltf_texture, "texCoord": 0}
            if shader_info and shader_info.alpha_test and texture_info and texture_info.alpha_depth:
                material_json["alphaMode"] = "MASK"
                material_json["alphaCutoff"] = shader_info.alpha_cutoff
                material_json["doubleSided"] = True
            elif shader_info and shader_info.translucent and texture_info and texture_info.alpha_depth:
                material_json["alphaMode"] = "BLEND"
                material_json["doubleSided"] = True
            materials.append(material_json)

        stable_id = (f"{primitive.source_file}::mesh[{primitive.mesh_ordinal}]"
                     f"::{primitive.mesh_name}::primitive[{primitive.primitive_ordinal}]")
        gltf_primitive = {"attributes": attrs, "indices": index_accessor,
                          "material": material, "mode": 4,
                          "extras": {"p3dStableId": stable_id,
                                     "p3dShader": primitive.shader}}
        mesh_index = len(meshes)
        display_name = safe_name(f"{Path(primitive.source_file).stem}__{primitive.mesh_name}__p{primitive.primitive_ordinal}")
        meshes.append({"name": display_name, "primitives": [gltf_primitive],
                       "extras": {"p3dStableId": stable_id}})
        nodes.append({"name": display_name, "mesh": mesh_index})
        manifest_entries.append({
            "stableId": stable_id,
            "source": primitive.source_file,
            "sourceSha256": getattr(primitive, "source_sha256"),
            "meshName": primitive.mesh_name,
            "meshOrdinal": primitive.mesh_ordinal,
            "primitiveOrdinal": primitive.primitive_ordinal,
            "shader": primitive.shader,
            "primitiveType": primitive.primitive_type,
            "vertexFormat": primitive.vertex_format,
            "sourceVertexCount": primitive.declared_vertex_count,
            "sourceIndexCount": primitive.declared_index_count,
            "exportTriangleCount": len(indices) // 3,
            "gltfMesh": mesh_index,
        })

    binary_name = output.with_suffix(".bin").name
    if embed:
        uri = "data:application/octet-stream;base64," + base64.b64encode(builder.binary).decode("ascii")
    else:
        uri = binary_name
        output.with_suffix(".bin").write_bytes(builder.binary)
    length = math.sqrt(sum(v * v for v in sun_direction))
    toward_sun = tuple(v / length for v in sun_direction)
    sun_node = len(nodes)
    nodes.append({"name": "SHAR_PCVR_Sun", "rotation": quaternion_from_z(toward_sun),
                  "extensions": {"KHR_lights_punctual": {"light": 0}},
                  "extras": {"towardSunWorld": list(toward_sun),
                             "lightTravelDirectionWorld": [-v for v in toward_sun]}})
    gltf = {
        "asset": {"version": "2.0", "generator": "SHAR PCVR Pure3D lightmap exporter"},
        "scene": 0,
        "scenes": [{"name": "P3D lightmap bake", "nodes": list(range(len(nodes)))}],
        "nodes": nodes, "meshes": meshes, "materials": materials,
        "buffers": [{"byteLength": len(builder.binary), "uri": uri}],
        "bufferViews": builder.buffer_views, "accessors": builder.accessors,
        "extensionsUsed": ["KHR_lights_punctual"],
        "extensions": {"KHR_lights_punctual": {"lights": [{
            "name": "SHAR_PCVR_Sun", "type": "directional", "color": [1.0, 0.93, 0.82],
            "intensity": sun_intensity,
            "extras": {"source": "WorldRenderLayer.cpp enhanced world sun"}
        }]}}
    }
    if images:
        gltf["images"] = images
        gltf["textures"] = gltf_textures
        gltf["samplers"] = [{"magFilter": 9729, "minFilter": 9987,
                             "wrapS": 10497, "wrapT": 10497}]
    output.write_text(json.dumps(gltf, indent=2), encoding="utf-8")
    missing_textures = sorted({info.base_texture for info in shaders.values()
                               if info.base_texture and info.base_texture not in textures})
    return {"formatVersion": 1, "gltf": output.name, "primitives": manifest_entries,
            "skipped": skipped, "exportedTextures": sorted(texture_by_name),
            "missingTextures": missing_textures, "sunTowardWorld": list(toward_sun),
            "sunNode": sun_node}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sources", nargs="+", type=Path, help="P3D files or directories")
    parser.add_argument("--output", "-o", required=True, type=Path, help="Output .gltf path")
    parser.add_argument("--root", type=Path, help="Root used for stable relative source paths")
    parser.add_argument("--texture-source", action="append", type=Path, default=[],
                        help="Additional P3D texture/shader package (repeatable)")
    parser.add_argument("--no-auto-terra", action="store_true",
                        help="Do not automatically load L<n>_TERRA.p3d beside region files")
    parser.add_argument("--embed", action="store_true", help="Embed binary data into the glTF")
    parser.add_argument("--sun-direction", nargs=3, type=float, metavar=("X", "Y", "Z"),
                        default=(0.45, 1.0, -0.30),
                        help="World-space direction from surfaces toward the sun")
    parser.add_argument("--sun-intensity", type=float, default=3000.0,
                        help="glTF directional-light intensity in lux")
    parser.add_argument("--all-models", action="store_true",
                        help="Export every render mesh instead of static 'common' meshes only")
    parser.add_argument("--generate-uv1", action="store_true",
                        help="Run Blender headlessly and generate automatic lightmap UV")
    parser.add_argument("--blender", type=Path, help="Path to blender.exe")
    parser.add_argument("--lightmap-resolution", type=int, default=8192)
    parser.add_argument("--lightmap-padding", type=int, default=8)
    parser.add_argument("--lightmap-angle", type=float, default=66.0)
    parser.add_argument("--save-blend", action="store_true")
    args = parser.parse_args()
    output = args.output.with_suffix(".gltf")
    output.parent.mkdir(parents=True, exist_ok=True)
    files: list[Path] = []
    for source in args.sources:
        files.extend(sorted(source.rglob("*.p3d")) if source.is_dir() else [source])
    files = list(dict.fromkeys(path.resolve() for path in files))
    if not files:
        parser.error("no P3D files found")
    root = args.root.resolve() if args.root else Path(os.path.commonpath(files))
    if root.is_file():
        root = root.parent

    primitives: list[Primitive] = []
    failures = []
    for path in files:
        try:
            parsed = parse_file(path, root)
            primitives.extend(parsed)
            print(f"{path}: {len(parsed)} primitive group(s)")
        except (OSError, P3DError, struct.error) as error:
            failures.append(f"{path}: {error}")
            print(f"error: {failures[-1]}", file=sys.stderr)

    resource_files = list(files)
    resource_files.extend(path.resolve() for path in args.texture_source)
    if not args.no_auto_terra:
        for path in files:
            match = re.match(r"(?i)l(\d+)[rz]", path.stem)
            if match:
                terra = path.parent / f"L{match.group(1)}_TERRA.p3d"
                if terra.exists():
                    resource_files.insert(0, terra.resolve())
    resource_files = list(dict.fromkeys(resource_files))
    shaders: dict[str, ShaderInfo] = {}
    textures: dict[str, TextureInfo] = {}
    for resource_path in resource_files:
        try:
            found_shaders, found_textures = scan_resources(resource_path)
            shaders.update(found_shaders)
            textures.update(found_textures)
            if found_shaders or found_textures:
                print(f"resources {resource_path}: {len(found_shaders)} shader(s), "
                      f"{len(found_textures)} texture(s)")
        except (OSError, P3DError, struct.error) as error:
            failures.append(f"resource {resource_path}: {error}")
    manifest = export(primitives, output, args.embed, shaders, textures,
                      tuple(args.sun_direction), args.sun_intensity,
                      common_only=not args.all_models)
    manifest["failedFiles"] = failures
    manifest_path = output.with_suffix(".lightmap-manifest.json")
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Exported {len(manifest['primitives'])} primitive(s), skipped {len(manifest['skipped'])}")
    print(f"glTF: {output}\nManifest: {manifest_path}")
    if args.generate_uv1:
        blender = find_blender(args.blender)
        if not blender or not blender.is_file():
            print("error: Blender was not found; install it or pass --blender path\\blender.exe",
                  file=sys.stderr)
            return 2
        try:
            sidecar = generate_lightmap_uv(blender, output, args.lightmap_resolution,
                                           args.lightmap_padding, args.lightmap_angle,
                                           args.save_blend)
            print(f"UV1 sidecar: {sidecar}")
        except subprocess.CalledProcessError as error:
            print(f"error: Blender UV generation failed with exit code {error.returncode}",
                  file=sys.stderr)
            return error.returncode or 2
    return 0 if manifest["primitives"] and not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())

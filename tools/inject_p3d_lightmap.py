#!/usr/bin/env python3
"""Inject baked UV1 and an external lightmap reference into a Pure3D level."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

from export_p3d_level_gltf import (
    COLOURLIST, INDEXLIST, MESH, NORMALLIST, POSITIONLIST, PRIMGROUP,
    SHADER, SHADER_TEXTURE_PARAM, TEXTURE, UVLIST, child_chunks, pstring, u32,
)

IMAGE = 0x00019001
IMAGE_FILENAME = 0x00019003
LIGHTMAP_PARAM = int.from_bytes(b"LMAP", "little")


def ps(value: str) -> bytes:
    encoded = value.encode("latin-1") + b"\0"
    if len(encoded) > 255:
        raise ValueError(f"Pure3D string is too long: {value}")
    return bytes((len(encoded),)) + encoded


def chunk(chunk_id: int, payload: bytes, children: bytes = b"") -> bytes:
    data_length = 12 + len(payload)
    return struct.pack("<III", chunk_id, data_length, data_length + len(children)) + payload + children


def parse_stable_id(value: str) -> tuple[int, int]:
    match = re.search(r"::mesh\[(\d+)]::.*::primitive\[(\d+)]$", value)
    if not match:
        raise ValueError(f"Invalid stableId: {value}")
    return int(match.group(1)), int(match.group(2))


def rewrite_vector_chunk(data: bytes, item, source_vertices: list[int], width: int) -> bytes:
    payload = item.offset + 12
    count = u32(data, payload)
    values = data[payload + 4:item.data_end]
    stride = width * 4
    if len(values) < count * stride:
        raise ValueError("Truncated vertex list")
    output = b"".join(values[index * stride:(index + 1) * stride] for index in source_vertices)
    return chunk(item.chunk_id, struct.pack("<I", len(source_vertices)) + output)


def rewrite_colour_chunk(data: bytes, item, source_vertices: list[int]) -> bytes:
    return rewrite_vector_chunk(data, item, source_vertices, 1)


def rewrite_uv_chunk(data: bytes, item, source_vertices: list[int]) -> bytes:
    payload = item.offset + 12
    count, channel = struct.unpack_from("<II", data, payload)
    values = data[payload + 8:item.data_end]
    if len(values) < count * 8:
        raise ValueError("Truncated UV list")
    output = b"".join(values[index * 8:(index + 1) * 8] for index in source_vertices)
    return chunk(UVLIST, struct.pack("<II", len(source_vertices), channel) + output)


def rewrite_primitive(data: bytes, item, entry: dict, shader_suffix: str) -> bytes:
    cursor = item.offset + 12
    version = u32(data, cursor)
    cursor += 4
    shader, cursor = pstring(data, cursor, item.data_end)
    prim_type, vertex_format, _vertices, _indices, matrix_count = struct.unpack_from("<IIIII", data, cursor)
    triangles = entry["triangles"]
    source_vertices = []
    uv1 = []
    new_indices = []
    split_vertices = {}
    for triangle in triangles:
        for vertex, uv in zip(triangle["vertices"], triangle["uv"]):
            converted_uv = (float(uv[0]), 1.0 - float(uv[1]))
            # Split only at real lightmap seams. Reusing identical
            # (source vertex, UV1) pairs avoids the previous 3x corner-vertex
            # expansion and substantially reduces streaming upload cost.
            key = (int(vertex), struct.pack("<ff", *converted_uv))
            new_index = split_vertices.get(key)
            if new_index is None:
                new_index = len(source_vertices)
                split_vertices[key] = new_index
                source_vertices.append(int(vertex))
                uv1.append(converted_uv)
            new_indices.append(new_index)
    if len(source_vertices) > 65535:
        raise ValueError("Primitive exceeds Pure3D 16-bit vertex-index limit after UV splitting")
    new_shader = shader + shader_suffix
    header = (struct.pack("<I", version) + ps(new_shader) +
              struct.pack("<IIIII", 0, (vertex_format & ~0xF) | 2,
                          len(source_vertices), len(new_indices), matrix_count))
    output_children = []
    for child in child_chunks(data, item.data_end, item.end):
        if child.chunk_id == POSITIONLIST:
            output_children.append(rewrite_vector_chunk(data, child, source_vertices, 3))
        elif child.chunk_id == NORMALLIST:
            output_children.append(rewrite_vector_chunk(data, child, source_vertices, 3))
        elif child.chunk_id == COLOURLIST:
            output_children.append(rewrite_colour_chunk(data, child, source_vertices))
        elif child.chunk_id == UVLIST:
            output_children.append(rewrite_uv_chunk(data, child, source_vertices))
        elif child.chunk_id == INDEXLIST:
            indices = b"".join(struct.pack("<I", index) for index in new_indices)
            output_children.append(chunk(INDEXLIST, struct.pack("<I", len(new_indices)) + indices))
        else:
            output_children.append(data[child.offset:child.end])
    uv_payload = struct.pack("<II", len(uv1), 1) + b"".join(struct.pack("<ff", *uv) for uv in uv1)
    output_children.append(chunk(UVLIST, uv_payload))
    return chunk(PRIMGROUP, header, b"".join(output_children))


def clone_shader(data: bytes, item, new_name: str, lightmap_name: str) -> bytes:
    cursor = item.offset + 12
    _old_name, cursor = pstring(data, cursor, item.data_end)
    version = u32(data, cursor)
    cursor += 4
    _old_type, cursor = pstring(data, cursor, item.data_end)
    rest = data[cursor:item.data_end]
    header = ps(new_name) + struct.pack("<I", version) + ps("lightmap") + rest
    children = data[item.data_end:item.end]
    children += chunk(SHADER_TEXTURE_PARAM, struct.pack("<I", LIGHTMAP_PARAM) + ps(lightmap_name))
    return chunk(SHADER, header, children)


def external_texture(name: str, filename: str, width: int, height: int) -> bytes:
    image_name = name + Path(filename.replace("\\", "/")).suffix
    texture_header = (ps(name) + struct.pack("<IIIIIIIII", 14000, width, height, 32,
                                             0, 1, 0, 0, 0))
    image_header = ps(image_name) + struct.pack("<IIIIIII", 14000, width, height,
                                                32, 0, 0, 1)
    image = chunk(IMAGE, image_header, chunk(IMAGE_FILENAME, ps(filename)))
    return chunk(TEXTURE, texture_header, image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--uv", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--texture-name", default="pcvr_l1r1_lightmap")
    parser.add_argument("--texture-file", default="art\\lightmaps\\l1r1_lightmap.png")
    parser.add_argument("--width", type=int, default=8192)
    parser.add_argument("--height", type=int, default=8192)
    parser.add_argument("--geometry-only", action="store_true",
                        help="Inject UV1/topology only, without lightmap shaders or texture")
    args = parser.parse_args()
    data = args.source.read_bytes()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    expected_hashes = {entry["sourceSha256"] for entry in manifest["primitives"]}
    digest = hashlib.sha256(data).hexdigest()
    if expected_hashes != {digest}:
        raise ValueError("Source P3D does not match the export manifest")
    uv_data = json.loads(args.uv.read_text(encoding="utf-8"))
    targets = {parse_stable_id(entry["stableId"]): entry for entry in uv_data["meshes"]}
    shader_suffix = "_pcvr_lm"
    used_shaders: set[str] = set()
    shader_chunks: dict[str, object] = {}
    mesh_ordinal = 0

    def contains_mesh(item) -> bool:
        if item.chunk_id == MESH:
            return True
        if item.data_end >= item.end:
            return False
        return any(contains_mesh(child) for child in child_chunks(data, item.data_end, item.end))

    def rebuild(begin: int, end: int, in_root: bool = False) -> bytes:
        nonlocal mesh_ordinal
        result = []
        first_mesh_index = None
        for item in child_chunks(data, begin, end):
            if in_root and first_mesh_index is None and contains_mesh(item):
                first_mesh_index = len(result)
            if item.chunk_id == SHADER:
                name, _ = pstring(data, item.offset + 12, item.data_end)
                shader_chunks[name] = item
            if item.chunk_id == MESH:
                current_mesh = mesh_ordinal
                mesh_ordinal += 1
                primitive_ordinal = 0
                own = data[item.offset + 12:item.data_end]
                children = []
                for child in child_chunks(data, item.data_end, item.end):
                    key = (current_mesh, primitive_ordinal)
                    if child.chunk_id == PRIMGROUP:
                        if key in targets:
                            cursor = child.offset + 16
                            shader, _ = pstring(data, cursor, child.data_end)
                            if not args.geometry_only:
                                used_shaders.add(shader)
                            children.append(rewrite_primitive(
                                data, child, targets[key], "" if args.geometry_only else shader_suffix))
                        else:
                            children.append(data[child.offset:child.end])
                        primitive_ordinal += 1
                    else:
                        children.append(data[child.offset:child.end])
                result.append(chunk(MESH, own, b"".join(children)))
            elif item.data_end < item.end:
                own = data[item.offset + 12:item.data_end]
                result.append(chunk(item.chunk_id, own, rebuild(item.data_end, item.end)))
            else:
                result.append(data[item.offset:item.end])
        if in_root:
            if args.geometry_only:
                return b"".join(result)
            missing = sorted(used_shaders - shader_chunks.keys())
            if missing:
                raise ValueError("Shader chunks not found: " + ", ".join(missing))
            resources = [external_texture(args.texture_name, args.texture_file,
                                          args.width, args.height)]
            resources.extend(clone_shader(data, shader_chunks[name], name + shader_suffix,
                                          args.texture_name)
                             for name in sorted(used_shaders))
            insert_at = first_mesh_index if first_mesh_index is not None else len(result)
            result[insert_at:insert_at] = resources
        return b"".join(result)

    roots = list(child_chunks(data, 0, len(data)))
    if len(roots) != 1:
        raise ValueError("Expected one Pure3D root chunk")
    root = roots[0]
    rebuilt = chunk(root.chunk_id, data[root.offset + 12:root.data_end],
                    rebuild(root.data_end, root.end, in_root=True))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(rebuilt)
    print(f"Patched {len(targets)} primitive(s), cloned {len(used_shaders)} shader(s): {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

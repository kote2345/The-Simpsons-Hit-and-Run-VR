#!/usr/bin/env python3
"""Blender-side automatic UV1 generator for SHAR lightmap glTF scenes.

Run through Blender, not regular Python:
  blender --background --python blender_generate_lightmap_uv.py -- [options]
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import bpy
import bmesh


def operator_kwargs(operator, values: dict) -> dict:
    """Keep the script compatible with multiple Blender operator revisions."""
    supported = {item.identifier for item in operator.get_rna_type().properties}
    return {key: value for key, value in values.items() if key in supported}


def arguments() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path,
                        help="Output corner-UV sidecar JSON")
    parser.add_argument("--resolution", type=int, default=8192)
    parser.add_argument("--padding", type=int, default=8, help="Atlas padding in pixels")
    parser.add_argument("--angle", type=float, default=66.0,
                        help="Smart Project seam angle in degrees")
    parser.add_argument("--blend", type=Path, help="Optionally save the prepared .blend")
    return parser.parse_args(argv)


def import_gltf(path: Path) -> list[bpy.types.Object]:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(path.resolve()), import_pack_images=False)
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not meshes:
        raise RuntimeError(f"No mesh objects were imported from {path}")
    return meshes


def generate_uv(meshes: list[bpy.types.Object], resolution: int,
                padding: int, angle_degrees: float) -> dict[str, list[list[tuple]]]:
    if resolution <= 0 or padding < 0 or padding * 2 >= resolution:
        raise ValueError("Invalid atlas resolution/padding")
    bpy.ops.object.mode_set(mode="OBJECT") if bpy.context.object else None
    bpy.ops.object.select_all(action="DESELECT")
    seam_angle = math.radians(angle_degrees)
    source_faces: dict[str, list[list[tuple]]] = {}
    for obj in meshes:
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        # Keep UV0 untouched. Smart Project writes only into the active UV layer.
        existing = obj.data.uv_layers.get("LightmapUV")
        uv_layer = existing or obj.data.uv_layers.new(name="LightmapUV")
        obj.data.uv_layers.active = uv_layer
        uv_layer.active_render = False
        source_attribute = (obj.data.attributes.get("_P3D_VERTEX_ID") or
                            obj.data.attributes.get("P3D_VERTEX_ID"))
        if not source_attribute or source_attribute.domain != "POINT":
            raise RuntimeError(f"{obj.name} lost the _P3D_VERTEX_ID attribute")
        source_vertex = [int(round(item.value)) for item in source_attribute.data]
        source_faces[obj.name] = [[
            (tuple(obj.data.vertices[obj.data.loops[index].vertex_index].co),
             source_vertex[obj.data.loops[index].vertex_index])
            for index in polygon.loop_indices
        ] for polygon in obj.data.polygons]
        face_attribute = obj.data.attributes.get("_P3D_FACE_ID")
        if not face_attribute:
            face_attribute = obj.data.attributes.new("_P3D_FACE_ID", "INT", "FACE")
        for polygon, item in zip(obj.data.polygons, face_attribute.data):
            item.value = polygon.index
        # Build connected charts. Smart Project on already triangulated game
        # terrain tends to turn nearly every triangle into a tiny island. P3D
        # also duplicates many geometrically shared vertices, so weld only the
        # Blender bake copy before detecting chart boundaries.
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1.0e-5)
        for edge in bm.edges:
            edge.seam = (not edge.is_manifold or
                         edge.calc_face_angle(0.0) > seam_angle)
        bm.to_mesh(obj.data)
        bm.free()

    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    margin = padding / resolution
    unwrap = {
        "method": "ANGLE_BASED",
        "margin": margin,
        "correct_aspect": True,
        "use_subsurf_data": False,
    }
    result = bpy.ops.uv.unwrap(**operator_kwargs(bpy.ops.uv.unwrap, unwrap))
    if "FINISHED" not in result:
        raise RuntimeError(f"UV Unwrap failed: {result}")
    pack = {
        "rotate": True,
        "rotate_method": "ANY",
        "scale": True,
        "margin": margin,
        "margin_method": "FRACTION",
        "shape_method": "AABB",
    }
    result = bpy.ops.uv.pack_islands(**operator_kwargs(bpy.ops.uv.pack_islands, pack))
    if "FINISHED" not in result:
        raise RuntimeError(f"Pack Islands failed: {result}")
    bpy.ops.object.mode_set(mode="OBJECT")
    return source_faces


def stable_id(obj: bpy.types.Object) -> str | None:
    value = obj.get("p3dStableId")
    if value:
        return str(value)
    value = obj.data.get("p3dStableId")
    return str(value) if value else None


def write_sidecar(meshes: list[bpy.types.Object], output: Path,
                  resolution: int, padding: int,
                  source_faces: dict[str, list[list[tuple]]]) -> None:
    entries = []
    missing_ids = []
    for obj in meshes:
        source_id = stable_id(obj)
        if not source_id:
            missing_ids.append(obj.name)
            continue
        mesh = obj.data
        mesh.calc_loop_triangles()
        layer = mesh.uv_layers.get("LightmapUV")
        if not layer:
            raise RuntimeError(f"{obj.name} has no LightmapUV")
        triangles = []
        face_attribute = mesh.attributes.get("_P3D_FACE_ID")
        if not face_attribute or face_attribute.domain != "FACE":
            raise RuntimeError(f"{obj.name} lost the _P3D_FACE_ID attribute")
        for triangle in mesh.loop_triangles:
            loops = list(triangle.loops)
            original_face = int(face_attribute.data[triangle.polygon_index].value)
            original = source_faces[obj.name][original_face]
            source_ids = []
            for index in loops:
                coordinate = mesh.vertices[mesh.loops[index].vertex_index].co
                match = min(original, key=lambda item: sum(
                    (coordinate[axis] - item[0][axis]) ** 2 for axis in range(3)))
                source_ids.append(match[1])
            triangles.append({
                "vertices": source_ids,
                "uv": [[layer.data[index].uv.x, layer.data[index].uv.y] for index in loops],
            })
        entries.append({"stableId": source_id, "object": obj.name,
                        "triangleCount": len(triangles), "triangles": triangles})
    if missing_ids:
        raise RuntimeError("Imported objects lost p3dStableId: " + ", ".join(missing_ids[:8]))
    payload = {
        "format": "shar-pcvr-lightmap-uv",
        "version": 1,
        "atlas": {"width": resolution, "height": resolution, "paddingPixels": padding,
                  "uvOrigin": "bottom-left"},
        "meshes": entries,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(payload, separators=(",", ":")), encoding="utf-8")


def prepare_bake(meshes: list[bpy.types.Object], resolution: int,
                 padding: int, output: Path) -> bpy.types.Image:
    """Create one shared bake target while keeping base textures on UV0."""
    image = bpy.data.images.get("SHAR_Lightmap")
    if not image:
        image = bpy.data.images.new("SHAR_Lightmap", width=resolution, height=resolution,
                                    alpha=False, float_buffer=True)
    image.generated_color = (0.0, 0.0, 0.0, 1.0)
    image.file_format = "OPEN_EXR"
    image.filepath_raw = str(output.with_suffix(".lightmap.exr").resolve())

    materials = {material for obj in meshes for material in obj.data.materials if material}
    for material in materials:
        material.use_nodes = True
        nodes = material.node_tree.nodes
        links = material.node_tree.links
        uv0 = nodes.get("SHAR_BaseUV0") or nodes.new("ShaderNodeUVMap")
        uv0.name = "SHAR_BaseUV0"
        uv0.label = "Original P3D UV0"
        uv0.uv_map = "UVMap"
        target = nodes.get("SHAR_LightmapBakeTarget") or nodes.new("ShaderNodeTexImage")
        target.name = "SHAR_LightmapBakeTarget"
        target.label = "Shared Lightmap Bake Target"
        target.image = image
        for node in nodes:
            node.select = False
            if node.type == "TEX_IMAGE" and node != target and not node.inputs["Vector"].is_linked:
                links.new(uv0.outputs["UV"], node.inputs["Vector"])
        target.select = True
        nodes.active = target

    for obj in meshes:
        lightmap_uv = obj.data.uv_layers.get("LightmapUV")
        if lightmap_uv:
            obj.data.uv_layers.active = lightmap_uv
            lightmap_uv.active_render = True
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    scene = bpy.context.scene
    # Texture baking requires Cycles; setting may fail on builds compiled without it.
    try:
        scene.render.engine = "CYCLES"
        scene.cycles.samples = 64
    except Exception:
        pass
    scene.render.bake.margin = padding
    scene.render.bake.use_pass_direct = False
    scene.render.bake.use_pass_indirect = True
    scene.render.bake.use_pass_color = False
    return image


def main() -> int:
    args = arguments()
    meshes = import_gltf(args.input)
    source_faces = generate_uv(meshes, args.resolution, args.padding, args.angle)
    write_sidecar(meshes, args.output, args.resolution, args.padding, source_faces)
    prepare_bake(meshes, args.resolution, args.padding, args.output)
    if args.blend:
        args.blend.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(args.blend.resolve()))
    print(f"SHAR lightmap UV: {len(meshes)} meshes -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

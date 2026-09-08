# PCVR lightmap feasibility

## Conclusion

A baked lightmap pipeline is feasible for the PCVR renderer. The Pure3D runtime
already has most of the low-level rendering pieces: up to four UV channels,
lightmap shader parameters, two lightmap material modes in Vulkan, and secondary
UV attributes in the Vulkan vertex format.

The shipped game data does not contain the data needed to use those pieces
directly. A recursive scan of the installed `art` directory found 19,866 mesh
primitive groups, all with UV channel 0 and none with UV channel 1 or higher.
No `LMAP` shader parameter or embedded memory-vertex lightmap data was found.
Consequently, adding a lightmap texture alone is insufficient: static level
geometry must be unwrapped into a second UV channel and connected to an atlas.

The scan can be reproduced with:

```powershell
python tools/inspect_p3d_lightmap.py "D:\games\The Simpsons Hit & Run\art"
```

## Recommended pipeline

Use non-destructive sidecar lightmap data rather than rewriting every original
P3D chunk initially:

1. Export the render meshes of a complete level to glTF, retaining stable IDs
   for the source P3D file, mesh and primitive group.
2. Import the scene into Blender, generate a non-overlapping UV1 unwrap, bake
   static indirect diffuse/sky lighting, dilate atlas borders, and export the
   UV1 data plus per-region lightmap atlases.
3. Keep the original P3D files unchanged. At load time, find the matching
   sidecar, split vertices at UV seams, install UV1, and bind the region atlas.
4. Decode and apply the lightmap as indirect diffuse irradiance inside the PBR
   lighting calculation. Do not multiply the final shaded colour by the
   lightmap, because that incorrectly darkens direct specular and other light.
5. Retain real-time sun/CSM for direct lighting and volumetrics. Exclude direct
   sun from the bake so moving shadow cascades do not double-light the scene.

The entire level should be present during baking so illumination agrees across
streaming boundaries, while output atlases should be divided by streamed region
(`l?r?`/`l?z?`) to fit the existing lifetime and memory model.

## Required implementation work

- A P3D-to-glTF static-level exporter with a stable primitive manifest.
- A Blender import/unwrap/bake/export script or add-on.
- A compact sidecar format containing corner UV1 data, atlas assignment and a
  source-geometry fingerprint.
- Runtime loading and vertex splitting for UV seams.
- Texture loading and material association for each streamed region.
- PBR integration as baked indirect irradiance, preferably with an HDR-capable
  encoding (RGBM is a practical first version).
- Validation for alpha-tested foliage, atlas padding/mips, coordinate systems,
  region seams and VR memory/performance.

Static architecture and terrain are suitable lightmap receivers. Moving cars,
characters and props cannot use the mesh lightmap directly; a later irradiance
probe volume can give them lighting consistent with the baked world.

## Main format constraint

Pure3D stores UV values per vertex, while a generated lightmap unwrap creates
per-corner seams. The importer must duplicate seam vertices and update all
parallel vertex streams and indices consistently. This is why a simple OBJ
round trip or blind insertion of a second UV list would corrupt or mis-map some
meshes.

## Suggested first milestone

Implement one Level 1 streamed region end to end: export, Blender bake, sidecar
load, UV-seam splitting, atlas binding and physically correct PBR use. Once that
is visually and structurally verified, make the exporter and baker batch all
regions and levels.

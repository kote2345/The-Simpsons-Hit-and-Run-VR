# Pure3D level exporter

`tools/export_p3d_level_gltf.py` exports the render geometry in one or more
Pure3D files to glTF 2.0. The glTF is intended for Blender lightmap unwrapping
and baking; the accompanying manifest retains the source identity needed by the
future UV1 importer.

By default, only mesh names containing `common` (case-insensitive) are exported
as the static lightmap set. Use `--all-models` for a diagnostic export of every
render mesh. Primitive groups using the `treeshadow_m` material or base texture
are excluded from both modes because they are projected tree-shadow cards, not
receivers.

## Export one streamed region

```powershell
python tools/export_p3d_level_gltf.py `
  "D:\games\The Simpsons Hit & Run\art\l1r1.p3d" `
  --root "D:\games\The Simpsons Hit & Run\art" `
  --output build\lightmap-export\l1r1.gltf
```

For region names such as `l1r1.p3d`, the exporter automatically reads the
adjacent `L1_TERRA.p3d` texture package. An additional package can be supplied
with a repeatable `--texture-source path.p3d` option.

This creates:

- `l1r1.gltf`: scene, meshes, material names and vertex attributes;
- `l1r1.bin`: binary vertex and index data;
- `l1r1.lightmap-manifest.json`: stable P3D source mapping and hashes.
- `l1r1_textures/`: extracted base-colour images referenced by glTF materials.

Use `--embed` to put the binary buffer inside the `.gltf` file. Directories are
accepted as inputs and are searched recursively for P3D files.

## Current scope

The first version exports static triangle and triangle-strip render meshes,
positions, normals, UV0 and vertex colours. Degenerate strip triangles are
removed. Lines, points, skinning, collision, locators and gameplay chunks are
not exported because they are not lightmap receivers.

Shader names, diffuse colours, translucency and embedded PNG base
textures are retained. Formats unsupported by glTF are reported in the
manifest and are not silently assigned.

Texture alpha is preserved for baking. Pure3D alpha-test materials become glTF
`MASK` materials with their original cutoff, while genuinely translucent
materials become `BLEND`. Cycles can therefore pass sunlight through foliage,
fences and other transparent texels instead of baking the whole polygon as an
opaque shadow caster.

The scene contains a `KHR_lights_punctual` directional light named
`SHAR_PCVR_Sun`. Its default world-space direction toward the sun is the PCVR
renderer value `(0.45, 1.0, -0.30)`, normalized. Override it with
`--sun-direction X Y Z`. Its default intensity is `3000`, and it can be changed
using `--sun-intensity`.

Do not rename exported objects manually. Their `p3dStableId` metadata connects
the baked UV data to its original primitive group.

## Automatic UV1

With Blender installed, export and generate a packed lightmap UV atlas in one
command:

```powershell
python tools/export_p3d_level_gltf.py `
  "D:\games\The Simpsons Hit & Run\art\l1r1.p3d" `
  --output build\lightmap-export\l1r1.gltf `
  --generate-uv1 `
  --lightmap-resolution 8192 `
  --lightmap-padding 8 `
  --save-blend
```

If Blender is not on `PATH`, add `--blender "C:\path\to\blender.exe"`.
The operation runs Blender without a UI, creates connected charts using hard
surface angles, unwraps them into a `LightmapUV` layer, packs all selected level
meshes into one atlas, and writes
`l1r1.lightmap-uv.json`. The sidecar stores UV per triangle corner so seams do
not lose information. `--save-blend` also creates a prepared scene that can be
opened for inspection.

Atlas padding is specified in pixels and converted using the requested atlas
resolution. These two values must also be used during the future bake step.

The saved Blender scene is already prepared with one shared floating-point
image named `SHAR_Lightmap`. Every material has an active, unconnected
`SHAR_LightmapBakeTarget` image node. Original textures are explicitly kept on
`UVMap`, while `LightmapUV` is the active render UV. Cycles defaults to a
64-sample indirect-only diffuse bake with the matching pixel margin.

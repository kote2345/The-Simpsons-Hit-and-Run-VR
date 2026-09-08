# PCVR lighting modernization

## Standard BRDF update

The two PCVR PBR geometry paths now share `pbr_brdf.glsl`. Sunlight, legacy
point lights and vehicle rear lights use the same metallic/roughness BRDF:
GGX normal distribution, height-correlated Smith visibility, Schlick Fresnel,
and energy-aware Lambert diffuse. The sun additionally uses its 0.53-degree
angular diameter to bound smooth highlights. The old direct-sun denominator
floor flattened those highlights and made their response inconsistent with
punctual lights.

Split-sum environment lighting now applies an approximate multiple-scattering
energy compensation derived from the existing analytical DFG. Its result is
bounded so a white environment cannot return more than white. Diffuse ambient
is reduced by the reflected energy instead of being added independently on top.
Direct lights remain single-scattering: applying a view-only DFG multiplier to
each light failed a GPU white-furnace test with up to 1.88x energy.

Dark reflection captures are now treated as valid occluded environments rather
than replaced by the bright analytical sky. Full-geometry static reflections
derive their UV from the final mapped world normal, matching the lit path and
dynamic cubemap path. This makes normal maps affect reflections consistently.

`tools/test-pbr-brdf.bat <Vulkan SDK>` compiles the production shared BRDF into
a compute shader, executes 64 roughness/view combinations on Vulkan, checks
finite output, back-facing rejection, bounded white-furnace response, and unity
white-environment reflection. Current single-scattering furnace range is
0.30691..1.00011. Both production variants also pass `spirv-val`.

## Implemented lighting foundation

The lit and full PBR fragment paths now evaluate enabled legacy point lights
using metallic/roughness GGX, correlated Smith visibility and Schlick Fresnel.
Normal maps and geometric specular antialiasing feed this calculation. Positions
from vkdevice are world-space; the fragment position and view direction are
transformed through reflectionViewToWorld before light evaluation. The enhanced
CSM sun remains the directional source, avoiding a second legacy sun contribution.
Lighting is accumulated before the existing display transform.

Draw constant pbrMapControl.z enables this on Windows only. The uniform layout
and descriptor layout are unchanged. Android leaves the component zero.
This supports active PDDI point lights (up to eight). Windows PBR world materials
receive these even without the legacy lit flag; HUD and other material models
retain their previous selection. This does not create streetlights or replace
PDDI's draw-scoped light selection with a scene-wide registry.
Point lights currently have no shadow visibility and can illuminate through walls.

PCVR rear vehicle lamps also evaluate this BRDF, including normal maps and
metallic/roughness response, instead of adding a material-independent colour.
Their existing cone, range and near fade remain; the additional legacy lamp
contribution is disabled only when the enhanced PCVR PBR path evaluates them.
Quest keeps its previous lamp contribution. Both PBR variants share the local
BRDF in shaders/pbr_punctual.glsl, included at shader compilation time.

## PCVR filtered sun shadows

Both PBR variants use pbr_shadows.glsl. Windows defaults to a nine-fetch weighted
3x3 PCF kernel on the existing hardware comparison samplers; weights sum to one
so fully lit and fully shadowed regions keep their previous energy. The kernel
is fixed in shadow texture coordinates and contains no per-frame randomization.
Smoothstep blends the existing 20-24 and 50-56 metre transition regions without
the previous near-cascade max operation. Invalid coverage falls back to an
available cascade rather than blending a valid shadow with an invalid sample.

Set SRR2_PCVR_SHADOW_FILTER_RADIUS before launching the game: default 1.25
shadow texels, range 0-2.5. Zero restores both the original one-fetch sampling
and original cascade transitions. Settings are read once per process; invalid
or non-finite values use the default. Android leaves this uniform zero.
This is fixed-radius PCF, not contact-hardening PCSS or ray-traced shadows.
It costs nine comparison fetches per selected cascade (18 during blending),
versus one/two previously; headset GPU timings and visual validation are pending.

Validate thin poles, shadow contact at building bases, and slow camera movement
through cascade transitions. Compare radius 0 and 1.25 from the same position;
check both eyes for edge instability. Filtering currently applies to PBR only.

Validation: both GLSL shaders compiled and the PCVR RelWithDebInfo executable
linked successfully. Headset appearance, stereo stability and GPU time remain
unverified. Check a lit object with a moving point light, camera rotation,
roughness/metallic maps and a zero-light control scene before visual signoff.
Also check static PBR road geometry while braking, metallic vehicle panels,
and lamp mode Off. Lamp brightness needs visual calibration in the headset.
Regenerate and validate embedded shaders with tools/compile-pbr-shaders.ps1
using -VulkanSdk with the installed SDK directory, then run
tools/build-pcvr-incremental.bat against the configured build tree.

## HDR and screen-space GI

Windows world draws now target RGBA16F attachments. PBR fragment specialization
constant 1 disables its per-material display transform; legacy/Phong/toon
fragments decode their final colour into the linear HDR attachment. Blending
and fog compose before a dedicated full-screen resolve, separately per eye.
The resolve applies exposure and the existing ACES approximation once, encoding
sRGB only for UNORM outputs (sRGB attachments perform their own encoding).
HUD draws remain in the same linear target and the complete eye resolves once
at EndPddiEye. Pure3D may interleave UI and world materials, so resolving at the
first HUD draw could restart a cleared HDR target later and black out gameplay
after cinematic camera transitions.
Cubemap captures and explicit offscreen texture passes retain their LDR path.
This is internal HDR rendering, not an HDR10/PQ output mode for the headset.

The resolve also evaluates approximate one-bounce screen-space GI: six fixed
hemisphere rays, eight distance steps, radius 2.5 world units. It reconstructs
positions using each eye's projection, estimates normals from depth with edge
selection, rejects sky and occluders, and gathers coloured scene radiance.
GI is added before exposure and tone mapping. The source is not the resolve
output, so there is no recursive light feedback between frames or eyes.

Limitations: no offscreen geometry, no multi-bounce propagation, no temporal
denoising. A bounded receiver reflectance estimate substitutes for an albedo
G-buffer; glossy highlights and transparency can contaminate the sampled
radiance. Geometry absent from depth cannot occlude GI. Edge fading reduces
screen-border popping but does not solve missing offscreen information.
This complements the existing ambient model rather than replacing it with a
physically complete irradiance solution. Do not equate this with Lumen/DDGI.

Environment settings (read once, restart the process to change):

| Variable | Default | Meaning |
| --- | --- | --- |
| SRR2_PCVR_HDR | 1 | 0 restores the LDR path and disables SSGI |
| SRR2_PCVR_GI | 0.35 | SSGI strength, 0 disables tracing, maximum 2 |
| SRR2_PCVR_EXPOSURE | 1 | Linear exposure multiplier, range 0-16 |

RGBA16F support is checked before allocation. HDR depth is sampled and stored
even for mono eyes; explicit barriers surround the resolve. Targets/descriptors
are cached by output image, size and layer, and released at context shutdown
after device idle. This increases desktop render-target memory and bandwidth;
Android never routes into this path and retains zero HDR specialization.

Validation: all changed shaders pass glslang and SPIR-V validation, and the
PCVR RelWithDebInfo build links. The headless Vulkan test executes the actual
resolve shaders and checks HDR radiance above 1, stereo layer isolation, sky
rejection, and red colour transfer onto a grey panel in a concave test scene.
Perspective reconstruction and immediate response to removing the coloured
source are checked too. It passed with Khronos validation enabled and zero
validation errors.
This does not validate the entire game's frame lifecycle or headset performance.
Game appearance, transparency, HUD transitions and GPU frame budget still need
an in-headset run; no claim of a measured VR framerate is made.

Regenerate all affected shader headers with tools/compile-hdr-shaders.ps1
-VulkanSdk <SDK directory>. Run tools/test-hdr-resolve.bat <SDK directory>
for the GPU check, then tools/build-pcvr-incremental.bat for the game build.

## Remaining renderer work

1. Add an albedo/normal G-buffer and an opaque-only radiance source to improve
   SSGI material accuracy. Measure GPU cost and add half-resolution tracing
   with edge-aware upsampling and independent per-eye temporal histories.
2. Introduce a scene-wide dynamic-light registry and spatial light selection so
   static world surfaces receive nearby lights independently of PDDI draw state.
   Add explicit intensity, range and spotlight cones, then local shadow caching.
3. Add a world-space irradiance probe volume with visibility data and bounded
   updates per frame. A capture/injection pass must gather direct lighting and
   geometry depth; probe interpolation must reject leaking across walls. Keep
   history shared between eyes and invalidate it on level changes/teleports.
   Existing reflection cubemap mipmaps are not diffuse GI or visibility data.
4. Add indirect-light occlusion using depth and normals, with separate histories
   for the two eyes, disocclusion handling and GPU timing instrumentation.
5. Replace generic reflection mipmaps with roughness-aware environment filtering;
   connect reflection captures and irradiance updates to dynamic-light changes.

The next world-space GI implementation must demonstrate indirect colour transfer
after moving a light, dark interiors without wall leakage, stable stereo and
measured frame cost. Select probe density and update budget using the target PC
GPU and headset refresh rate.

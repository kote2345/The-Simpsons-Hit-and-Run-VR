# PCVR volumetric shadow correction

The froxel compute pass evaluates CSM visibility throughout the medium and
integrates scattering and transmittance from front to back. Geometry depth is
used only by the native-resolution HDR resolve to terminate each pixel's ray.

Current morning-haze tuning (supersedes the historical values below): extinction
0.00065, ground-density amplitude 0.40, scattering factor 0.8 + 0.45 * HG(0.55),
sun scattering tint (1, 0.88, 0.72). The previous morning preset (extinction
0.0022, ground amplitude 0.90, phase 1.6 + 0.85 * HG) remained too foggy in
headset testing. This reduces both optical thickness and source intensity,
yielding about six times less added light in typical open air, retaining a broad
shadowed base and forward shafts. Integration remains 128 slices x 4 substeps.
Shader validation and PCVR build pass; artistic balance needs headset review.

After the sparse haze preset was accepted, source intensity was increased from
1.65 to 2.15 (about 30%) for more visible volumetric light/shadow contrast.
Extinction, density, angular response and integration resolution are unchanged.

Corrections:

- Follow-up root cause: `rmt::Matrix::Invert()` is affine-only (see
  `libs/radmath/radmath/matrix.cpp`). Both froxel generation and HDR depth
  reconstruction passed perspective matrices to it. The resulting homogeneous
  W was constant, the screen rays were parallel, and surface distance was
  incorrect. `projection_math.h` now performs full 4x4 inversion with pivoting;
  compute and resolve receive the same inverse for each eye. The first patch
  did not fix this; its build and interpolation checks did not cover projective
  reconstruction. `tools/test-volumetric-projection.bat` covers asymmetric
  stereo projections, both depth directions, round trips to 120 units, ray
  divergence and singular input. Maximum measured round-trip error: 0.001102
  world units (float inverse).

- CSM read/write image barriers include compute shader consumers and both early
  and late depth writes. Previously only fragment consumers were synchronized.
- Capture the centre camera with the shadow receiver state, independent of the
  last enhanced material drawn. The same camera converts reconstructed positions
  and receiver matrices between centre-view and world coordinates.
- Sample the cumulative volume directly in HDR resolve. The former half-size
  depth termination followed by a nonlinear-depth bilateral blur mixed different
  ray lengths at geometry edges. Its intermediate draw is no longer submitted.
- Use exponential segment spacing (128 segments, 120 world units), an implicit
  zero-scattering/unit-transmittance boundary, and Beer-Lambert interpolation
  inside the selected segment. The old first segment was about 1.47 units and
  was applied in full even to a surface at the near plane.
- Oblique shaft banding follow-up: evaluate four ordered lighting/density
  subsegments per stored slice instead of extruding a single midpoint shadow
  comparison over a whole slice. Together with 128 stored slices this gives
  512 integration samples per column, versus the previous 64. This increases
  compute sampling work eightfold and volume storage twofold; headset GPU cost
  and remaining aliasing need measurement. Fixed substeps avoid temporal noise.
- Density uses a fixed world-height datum instead of following headset height.
  Extinction is reduced from 0.012 to 0.004. Following headset feedback, scattering
  uses an artistic isotropic base plus a forward lobe: 2 + 0.75 * HG(g=0.55),
  with normalization absorbed into light intensity. This yields factors 7.74
  toward the sun, 2.35 sideways and 2.14 away. The intermediate isotropic-only
  setting (factor 1) weakened the original forward shafts too much; this restores
  their original peak (7.65) while raising sideways scattering. The original
  HG setting made side scattering about 16x dimmer than forward scattering.
  CSM visibility still multiplies all scattering; no unshadowed fog light is
  added. These are artistic defaults, not correctness requirements. Background
  contrast and the length of a shadow traversed by a ray still affect visibility.

References:

- [Wronski, SIGGRAPH 2014](https://bartwronski.com/wp-content/uploads/2014/08/bwronski_volumetric_fog_siggraph2014.pdf),
  especially slides 23–28: shadowed volume lighting, exponential distribution,
  cumulative integration, native-resolution depth lookup instead of 2D upsampling.
- [Unity Adam volumetric lighting](https://github.com/Unity-Technologies/VolumetricLighting):
  frustum-aligned volume lighting and shadow injection.

Validation: GLSL compilation and spirv-val (Vulkan 1.0), PCVR RelWithDebInfo
build, and numerical comparison of segment interpolation to homogeneous-medium
Beer-Lambert transport at zero, near, intermediate and maximum distances.
Headset appearance and GPU timing have not been verified. Check under roofs and
beside buildings, rotating and translating the head in both eyes; shadow shafts
should remain in world space, with no halos copied from foreground silhouettes.
Finite froxel resolution still limits thin shafts; no temporal reprojection is
implemented. Existing intermediate image allocation remains for now, although
the intermediate rendering pass is bypassed.

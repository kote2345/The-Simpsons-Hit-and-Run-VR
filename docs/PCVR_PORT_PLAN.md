# PCVR parity plan

The Quest implementation is the behavioural reference. Platform code should
only create the OpenXR instance/session, provide native graphics bindings and
query platform extensions. Camera, input mapping, UI placement and gameplay
behaviour belong to shared code compiled by both Quest and PCVR.

## Current parity gaps

## Shared HUD consolidation (complete for Vulkan)

1. Remove the provisional desktop-only spatial layout.
2. Move Quest Vulkan HUD state and resource ownership into one shared module.
3. Move radar, all 19 mission slots, objective, timer, coin, action prompt,
   pause and iris composition without changing the Quest layout algorithm.
4. Feed that module the same per-frame eye, hand, camera and vehicle context
   from both platform backends.
5. Remove platform copies of HUD capture/composition entry points.
6. Validate resource lifetime across level/frontend transitions.
7. Build PCVR and Quest Vulkan Release.

1. **Shared OpenXR core** — in progress
   - [x] Shared loader and instance-extension negotiation.
   - [x] Shared pose math, recentering, eye and culling-camera composition.
   - [x] Shared Vulkan full-page HUD compositor.
   - [x] Shared character hand meshes and materials.
   - [x] Move frame/session transitions and Vulkan frame ordering into shared orchestration.
   - [ ] Keep only Android EGL/activity and Win32 Vulkan/window setup in backends.

2. **Input parity** — in progress
   - [x] Quest Touch buttons, sticks, triggers, grips and grip poses on PCVR.
   - [x] Feed PCVR actions through the console-style Win32 controller path.
   - [x] Shared menu-axis arbitration and room-scale movement.
   - [x] Share raw action normalization, deadzones, look suppression and menu-axis arbitration.
   - [x] Share semantic-name adapters and neutral/focus recovery.
   - [x] Share thumbstick deadzone and menu-axis arbitration.
   - [x] Add Index, Vive and WMR interaction-profile bindings (Touch Pro uses the compatible Touch profile).
   - [x] Add focus-loss/action-state recovery and clear stale virtual inputs.
   - [x] Add OpenXR haptics through the common controller rumble surface.

3. **Stereo world and cameras** — in progress
   - [x] Per-eye OpenXR FOV and tracked camera transforms.
   - [x] Stable gameplay/culling camera separate from HMD rotation.
   - [x] Use the shared centre-eye camera for PCVR visibility culling.
   - [x] First-person and SuperCam PCVR hooks use the VR path.
   - [x] Port the Android VR smoothing used by authored animated cameras.
   - [x] Audit follow/conversation/animated camera VR conditions.
   - [x] Use common recenter, room-scale, head-height and head-forward calculations.

4. **Frontend, FMV and transitions** — in progress
   - [x] World-locked stereo frontend plane.
   - [x] World-locked FMV plane, decoder-rate timing and skip input.
   - [x] Clear FMV state when returning to frontend/gameplay.
   - [x] Share frontend Pure3D registration, fixed menu pose and page-transition rules.
   - [x] Share iris blackout and pause coin composition through the consolidated HUD.
   - [ ] Validate every language/license/boot/menu/loading transition.

5. **Spatial HUD** — consolidated for Vulkan
   - [x] Capture the authored HUD once and composite it stereoscopically.
   - [x] Apply the tracked per-eye camera to embedded HUD Pure3D objects on PCVR.
   - [x] Move the exact Quest radar capture/mask into shared Vulkan HUD code.
   - [x] Move all 19 exact Quest mission HUD slots and layout state into shared code.
   - [x] Share coin/action/objective/timer/message placement and pause variants.
   - [x] Enable `IsSpatialHudEnabled()` on PCVR only through the consolidated path.

6. **Tracked hands and vehicles** — in progress
   - [x] Shared character-specific hand geometry for normal tracked controllers.
   - [x] Shared visibility rules for cutscenes and third-person vehicles.
   - [x] Move wheel/yoke geometry, grabbing and hand attachment into shared code.
   - [x] Share steering-wheel input, comfort mode and vehicle camera behaviour.

7. **Vulkan features and lifecycle** — pending parity audit
   - [ ] Replace queue-wide waits with per-frame fences/semaphores.
   - [ ] Recover cleanly from session loss and `VK_ERROR_DEVICE_LOST`.
   - [ ] Port or capability-gate Quest CSM, materials, GTAO, lights and reflections.
   - [x] Share render scale and refresh-rate validation/persistence through backend callbacks.

8. **Regression gates**
   - [x] PCVR Vulkan build.
   - [x] Quest Vulkan debug and release builds.
   - [ ] Desktop non-VR build.
   - [ ] Runtime smoke matrix: Quest Link/Air Link, SteamVR and Virtual Desktop.
   - [ ] Boot, frontend, FMV, on-foot, vehicle, pause and mission HUD checklist.

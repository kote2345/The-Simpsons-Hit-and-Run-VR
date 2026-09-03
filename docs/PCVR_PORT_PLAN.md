# PCVR port plan

The PCVR build is an additional OpenXR target. The standalone Quest build
remains the production baseline and must keep building after every stage.

## Build contract

- Quest: `SRR2_ENABLE_OPENXR=ON` by default and Android lifecycle integration.
- Desktop: `SRR2_ENABLE_OPENXR=OFF` by default, preserving the non-VR game.
- PCVR: configure desktop with `-DSRR2_ENABLE_OPENXR=ON` and Vulkan.
- OpenXR runtime loading is platform-specific and never statically tied to a
  particular headset vendor.

## Milestones

1. **Platform foundation (in progress)**
   - Separate OpenXR enablement from `RAD_ANDROID`.
   - Load `openxr_loader.dll` on Windows and `libopenxr_loader.so` on Quest.
   - Compile shared Vulkan/OpenXR code under `SRR2_OPENXR`; reserve
     `SRR2_OPENXR_PLATFORM_ANDROID/WIN32` for actual platform differences.
   - Add a desktop OpenXR lifecycle entry point without changing Quest startup.
2. **Desktop Vulkan session**
   - [x] Move Android loader/instance chaining into a platform backend.
   - [x] Request optional Meta extensions only when the active runtime supports them.
   - [x] Reuse `XR_KHR_vulkan_enable2` device selection.
   - [x] Create the Windows OpenXR instance without Android instance structures.
   - [x] Create a Win32 Vulkan session and local reference space.
   - [x] Handle session READY/STOPPING events and frame timing.
   - [x] Create a two-layer desktop stereo swapchain and submit projection layers.
   - [ ] Replace the bootstrap clear with Pure3D eye rendering.
   - Share swapchain, view, frame timing, and action code with Quest.
3. **Renderer integration**
   - [x] Enable the Vulkan PDDI backend on Windows.
   - [x] Use the vendored SDL2 revision for reproducible Win64 VR builds.
   - [x] Define Win64 OpenXR, ZLIB/libpng, OpenAL, and FFmpeg dependencies
     in a vcpkg manifest and provide `build-pcvr.bat`.
   - [x] Expose the acquired desktop swapchain image to Vulkan PDDI.
   - [x] Render separate Pure3D passes with OpenXR eye transforms and FOV.
   - Create the SDL desktop window as a mirror/diagnostic surface.
   - Keep Quest multiview and mobile extensions behind capability checks.
4. **Input and UX**
   - Validate Oculus Touch, Index, Vive, and WMR interaction profiles.
   - Keep keyboard/gamepad available for menus and debugging.
   - Store PCVR settings independently from the Quest installation.
5. **Compatibility and packaging**
   - Test Meta Quest Link/Air Link, SteamVR, and Virtual Desktop runtimes.
   - Add a Windows packaging target with the OpenXR loader dependency.
   - Run Quest release and PCVR smoke tests before each merge.

## Regression gates

- Quest release APK builds successfully.
- Quest OpenXR initialization and Vulkan rendering remain unchanged.
- Desktop non-VR still configures with `SRR2_ENABLE_OPENXR=OFF`.
- PCVR gracefully reports a missing runtime instead of crashing.

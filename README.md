  ## Development
  The entire port was created using AI-assisted development, including implementation, debugging, VR rendering work, performance optimization, and platform adaptation.
  
  # The Simpsons: Hit & Run VR for Meta Quest 3

  A standalone VR port of **The Simpsons: Hit & Run** for Meta Quest 3.

  This project is based on [Carlox33/The-Simpsons-Hit-and-Run-Android](https://github.com/Carlox33/The-Simpsons-Hit-and-Run-Android) and adapts the Android version for standalone VR hardware.

  ## Features

  - OpenXR support for Meta Quest 3.
  - Vulkan rendering through OpenXR.
  - Single-pass stereo/multiview rendering.
  - Stereoscopic game world and HUD.
  - VR-compatible menus and settings.
  - VR controller support.
  - Room-scale movement and recentering.
  - Seated mode.
  - Smooth and snap turning.
  - VR steering-wheel vehicle control.
  - Adjustable refresh rate and render scale.
  - Cascaded shadow maps.
  - Enhanced materials and lighting.

  ## Installation

  You must provide your own legal copy of the PC version of **The Simpsons: Hit & Run**. Original game files are not included with this project.

  1. Create a “SimpsonsHitRun” folder on your Quest in the root memory folder (Quest\Internal shared storage\SimpsonsHitRun)
  2. Copy all the files from the PC version of the game (without mods, the original version) into folder SimpsonsHitRun
  3. Install the apk and play

  ## Project Status

  The port is actively developed. Core gameplay, VR rendering, menus, HUD, and controller support have been adapted for Quest 3. PCVR version in plans.

  ## Vulkan renderer

  Vulkan is the production renderer for the VR port and permanently replaces
  the previous OpenGL ES renderer. It provides the shared graphics backend for
  standalone Quest builds and the planned PCVR version.

  Build the Quest version with:

  ```powershell
  .\android-project\gradlew.bat -p android-project assembleDebug
  ```

  Vulkan is selected by default; no renderer option is required.

  ## Based On

  This project is based on:

  [Carlox33/The-Simpsons-Hit-and-Run-Android](https://github.com/Carlox33/The-Simpsons-Hit-and-Run-Android)

  Special thanks to the original Android port authors and everyone contributing to the community effort to bring the game to modern platforms.

  ## Legal Notice

  **The Simpsons: Hit & Run** is the intellectual property of its respective rights holders. This project does not include original game assets and does not distribute pirated copies of the game.

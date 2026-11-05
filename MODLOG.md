# VR iris rendering repair ? 2026-10-09

Requirement: retain original vehicle/interior activation, animation speed,
CLOSED/OPEN events and gameplay timing. Only replace the circular mask with
uniform full-eye black opacity.

The first repair incorrectly used an independent 0.8-second fade and gated
GUI events on its endpoints. The user reported slow entry and persistent
blackness after exit. Those timers and event gates have been removed.

CGuiScreenIrisWipe registers the original IrisController with the VR HUD.
Rendering reads its frame: frame 0 = transparent, midpoint = fully black,
last frame = transparent. The GUI alone drives its speed and frame ranges.
OPEN completion, screen destruction and mission-loading handoff clear the
registration. The HUD retains a reference while it reads the controller.

Desktop renders a black full-eye HUD quad after HDR resolve when the optional
OpenXR color-scale extension is unavailable, in dual-pass and multiview.
Quest retains compositor scaling to avoid foveation artifacts; GLES reads
the same original controller. No save/profile changes were made.

Validation: PCVR and Android Vulkan release builds; git diff --check.
GetIn/GetOut and interior event listeners were inspected and left unchanged.
Visual verification in a headset remains pending.

Artifacts: build/pcvr/code/SimpsonsVR.exe and
android-project/app/build/outputs/apk/release/app-release.apk.

Follow-up: separate closing/opening explicitly. The original clock can advance
past the midpoint while loading; this must not open the cover. Only InitOutro
selects opening. Accelerate visual opacity over the first eighth of each phase,
then hold its endpoint. DoNotOpenOnNextOutro now preserves black. Original
event timing remains unchanged. Regression check exercises the production alpha
function with closing-clock overshoot, opening start, completion and reset.

Latest tuning: user requested 250 ms for each visual fade. GUI InitIntro/
InitOutro still exclusively activate the phases; closing holds fully black
until opening. Visual fade is now linear in elapsed time, without retaining
the controller. OPEN completion no longer truncates the 250 ms fade. Production
alpha regression checks cover start, midpoint, 250 ms endpoints and long hold.
Both release builds passed. Headset verification remains pending.

VR haptics: player-occupied car collision uses normalized physics force,
square-root amplitude and force-dependent 60?240 ms duration. Suppresses weaker
contacts within 100 ms to preserve the strongest impact. Tracked-hand attacks
vibrate only the hand that hits, with no pulse on misses. Button attacks also
require a hit. Shared hand API implemented on Quest and desktop OpenXR.
Both release builds and production collision-policy regression check passed;
physical controller feedback has not been verified in a headset.

IK animation mask: private render pose uses rest-pose locals for torso,
shoulders, arms and head; only hip-to-foot descendant locals come from animation.
Rebuild by hierarchy depth to handle parents ordered after children. Keep
gameplay root placement/yaw; remove HMD pelvis translation, chest lean and leg
IK correction. Arms solve from neutral geometry to tracked hands; head tracking
and grip fingers remain. Source animation and gameplay pose are untouched.
PCVR and Quest release builds passed. Production mask helper regression covers
static upper body, animated legs, unsorted parent indices and source immutability.
Visual verification in headset remains pending.

2026-10-10: doubled OpenXR vibration durations on both backends, including
existing steering/interact feedback. Pickups emit a 140 ms pulse through actual
coin collection, Collectible (cards/wrenches), and mission collectible hooks;
duplicates within 100 ms are suppressed. IK now neutralizes animated root too,
updates selected object matrices, and derives heading rotation from neutral
root axes. Gameplay placement remains in Character::Display. Both builds passed.
Regression checks cover pickup deduplication and upper-body invariance when
animated root/spine change, with animated leg locals and unordered hierarchy.
Headset verification remains pending.

User requested rollback of IK changes. Manually removed animation-mask helper
and restored pre-mask root orientation, pelvis/chest tracking and animated-foot
leg IK from the prior edits in this conversation. No git restore/reset used;
haptics, pickups, fade and unrelated changes retained.

IK mask revision after rollback: SelectLegAnimation preserves the existing
root anchor and original pelvis/head alignment, chest tracking and leg IK.
It replaces local animation with rest-pose transforms everywhere except the
thigh descendant branches, then runs the original arm solver. Failed arm
solves retain neutral arms. This avoids the prior bind-root coordinate change
and preserves vehicle attachment. Both release builds passed. Mask helper
regression verifies torso/arm animation removal, animated legs, source
immutability, reordered hierarchy and preservation of gameplay root movement.
Headset visual validation is still pending.

Confirmed actual no-op cause: all five playable P3D skeletons have
Motion_Root.parentIndex=0 (self), whereas SelectLegAnimation assumed -1.
Depth traversal hit count on every bone, so none were masked. Read the full
Character::Display -> CharacterRenderable -> CompositeDrawable -> PolySkin ->
Vulkan skin-upload path; poseReady preserves the final pose. Quest log confirms
LEG_ANIMATION_ONLY_V1 runs and also shows tracking fallback. Fixed root
termination in mask traversal only. Tests use real five-character parent
arrays and verify neutral non-leg locals, animated leg locals and immutable
source. Actual tPose ResetToRestPose sets root parent pointer NULL despite the
skeleton self-parent. No further root/anchor/solver changes.

VR eye position: move FirstPersonCam position and target 10 cm along its forward axis, after saving the original vehicle/body anchor. On-foot IK subtracts the same offset from its neck target so the torso does not follow the camera shift; seated IK already uses the unchanged seat anchor. Both builds and diff checks passed; visual headset verification remains pending.

Body IK: on-foot root yaw now follows the tracked HMD heading, with residual head rotation excluding that applied yaw. Seated leg branches are restored from original animation relative to the final seated render root after torso offsets, including authored pelvis transforms; leg solving/ankle alignment runs on foot only. PCVR and Android release builds passed. Headset verification pending.

Body yaw pivot fix: standing render root rotates around the eye point, with neck target subtracting the HMD-yawed forward eye offset. Seated chest yaw follows HMD relative to the seat camera around the eye point; original leg matrices remain restored and excluded from IK. Residual head orientation removes already-applied yaw in both modes. PCVR and Quest release builds passed; headset check pending.

PCVR right-eye mirror: present OpenXR array layer 1 to the existing SDL Win32 window via GPU image blit before XR image release. Enable desktop Vulkan surface/swapchain extensions and XR transfer-source usage, restore source layout, preserve aspect ratio, handle resize/minimize and release mirror resources before Vulkan teardown. Nonblocking monitor acquisition and immediate/mailbox presentation. Standalone real GPU test with red left/green right confirms green window at normal size, resize and minimize/restore. PCVR and Quest release builds pass; full headset/game validation pending.

PCVR logging: GUI subsystem removes the console window. Redirect stdout/stderr and native Windows standard handles to SimpsonsVR.log beside the executable before SDL/OpenXR startup. Unbuffered output, new log each launch, Unicode executable paths and arbitrary working directory supported. Standalone GUI test verifies no console and interleaved stdout/stderr/native output; PE header confirms GUI subsystem. PCVR and Quest release builds pass.

Main-menu disk: inspected original camset.p3d chunks. Scenegraph drawable pCylinder2fShape references a static mesh with 118 positions, XY bounds about -1.03..1.05 and Z 1.207..1.250, and sole material iris_pure3dSimpleShader1. Separate animated skins pCylinder2f2Shape/pCylinderShape1 share that material. Detach only the verified static drawable when initializing the registered VR main-menu scene; preserve the GUI iris and animated skins. Runtime logs confirm whether the exact node/material match was reached; visual verification remains pending.

Homer costume hands: audited homer_m and all six h_*_m model files. Skeletons homer/h_donut/h_evil/h_fat/h_scuzzy/h_stcrobe/h_undrwr share elbow/wrist/thumb-base indices 20/21/24 (left), 27/28/31 (right), and two-joint thumbs. All skins use version 3 (the GPU repair path). Use a shared verified skeleton UID family for both existing weight repair and distal-thumb IK behavior; actor identity is insufficient for costume skins.

Follow-up per-mesh audit: identical skeleton topology did not imply identical hands. With nonzero weights only, thumb/elbow mixed vertices L/R are homer 1/1, donut 1/1, scuzzy 1/1; underwear instead has elbow-only distal thumb-web vertices 2/3 (including a duplicate face in a different primitive group). Its duplicate has no wrist palette entry, so append that entry when repairing, leaving existing indices/weights intact. Select underwear thumb-web geometry beyond the wrist bind plane, not forearm blends. Fat/evil meshes have compact low-detail hands with no weighted distal middle-finger joint. Disabling their grip was rejected in headset testing. Restore grip with shorter curl angles and retain articulation of the weighted distal thumb. Numerical source-mesh sweep passed for all 14 hands: nonzero motion, unchanged neutral vertices/wrist transforms, finite and continuous intermediate poses. Both builds passed and APK installed. Visual verification of the revised compact-hand curl remains pending.

Original vehicle transparency: removed the VR primitive-group override that forced translucent vehicle shader alpha to 64 and then reset it to 255. Vehicle materials now retain their authored alpha; existing driver/interior hiding is unchanged. Both builds passed and APK installed.

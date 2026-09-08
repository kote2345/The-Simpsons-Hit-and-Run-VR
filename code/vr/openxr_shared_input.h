#ifndef OPENXR_SHARED_INPUT_H
#define OPENXR_SHARED_INPUT_H

#include <radmath/radmath.hpp>

namespace SharOpenXR
{
enum VrActionId
{
    VR_ACTION_MOVE_X,VR_ACTION_MOVE_Y,VR_ACTION_LOOK_X,VR_ACTION_LOOK_Y,
    VR_ACTION_SELECT,VR_ACTION_BACK,VR_ACTION_ATTACK,VR_ACTION_USE,VR_ACTION_MENU,
    VR_ACTION_LEFT_TRIGGER,VR_ACTION_RIGHT_TRIGGER,VR_ACTION_LEFT_GRIP,
    VR_ACTION_RIGHT_GRIP,VR_ACTION_LEFT_STICK_CLICK,VR_ACTION_RIGHT_STICK_CLICK,
    VR_ACTION_COUNT
};
enum VrActionKind { VR_ACTION_BOOLEAN,VR_ACTION_FLOAT };
struct VrActionSpec { const char* name;const char* localizedName;VrActionKind kind; };
struct VrBindingSpec { VrActionId action;const char* path; };
const VrActionSpec* GetVrActionSpecs();
const VrBindingSpec* GetQuestTouchBindingSpecs(bool xOpensMenu,unsigned& count);

struct ThumbstickAxes { float x,y; };
struct VrInputFrame
{
    ThumbstickAxes move;
    ThumbstickAxes look;
    float select,back,attack,use,menu;
    float leftTrigger,rightTrigger,leftGrip,rightGrip;
    float leftStickClick,rightStickClick;
};
typedef void (*VrInputBindingSink)(void* context,const char* consoleInput,float value);
struct VrConsoleAdapterState { float backButton,attackButton;VrConsoleAdapterState():backButton(0),attackButton(0){} };
void AdaptVrConsoleInput(const char* semantic,float value,bool desktopAliases,
    bool horizontalMenuDominant,bool verticalMenuDominant,
    VrConsoleAdapterState* state,VrInputBindingSink sink,void* context);

// Emit the console controller used by the Quest build. Runtime backends only
// read XrActions; gameplay meaning and button combinations live here.
void EmitQuestControllerBindings(const VrInputFrame& input,
                                 float effectiveLeftTrigger,
                                 float effectiveRightTrigger,
                                 bool leftGripButton,
                                 bool rightGripButton,
                                 bool leftThumbButton,
                                 bool rightThumbButton,
                                 VrInputBindingSink sink,
                                 void* context);
void EmitNeutralVrController(VrInputBindingSink sink,void* context);
void ResetVrInputSemantics();

// Platform-neutral VR gameplay interaction helpers. Both Quest and PCVR
// provide controller tracking through the common OpenXR facade; game code
// should use these instead of reaching into either runtime backend.
bool IsSharedVrGameplayEnabled();
float GetHandGripValue(unsigned hand);
bool GetHandWorldPosition(unsigned hand,rmt::Vector* outPosition);
bool IsPhysicalInteractPulse();
ThumbstickAxes ApplyVrThumbstickDeadzone(float x,float y);
VrInputFrame NormalizeVrInputFrame(const VrInputFrame& raw,
                                   bool suppressLookStick);
VrInputFrame ProcessVrInputFrame(const VrInputFrame& raw,bool suppressLookStick);
// Complete platform-neutral input frame: gameplay gating, vehicle overrides,
// controller emission and FMV skip edge handling.
void SubmitVrInputFrame(const VrInputFrame& raw,VrInputBindingSink sink,void* context);
void UpdateVrMenuAxisLock(const ThumbstickAxes& move,const ThumbstickAxes& look,
                          unsigned& lock,unsigned& neutralFrames,
                          bool& horizontal,bool& vertical);
}
#endif

#include <vr/openxr_shared_input.h>
#include <vr/openxr_shared_state.h>
#include <vr/openxr_shared_vehicle.h>
#include <presentation/presentation.h>
#include <presentation/fmvplayer/fmvplayer.h>
#include <worldsim/character/charactermanager.h>
#include <worldsim/character/character.h>
#include <worldsim/redbrick/vehicle.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace SharOpenXR
{
namespace
{
const VrActionSpec ActionSpecs[VR_ACTION_COUNT]={
    {"move_x","Move horizontal",VR_ACTION_FLOAT},{"move_y","Move vertical",VR_ACTION_FLOAT},
    {"look_x","Look horizontal",VR_ACTION_FLOAT},{"look_y","Look vertical",VR_ACTION_FLOAT},
    {"select","Select or jump",VR_ACTION_BOOLEAN},{"back","Back or sprint",VR_ACTION_BOOLEAN},
    {"attack","Attack or handbrake",VR_ACTION_BOOLEAN},{"use","Action or exit vehicle",VR_ACTION_BOOLEAN},
    {"menu","Game menu",VR_ACTION_BOOLEAN},{"left_trigger","Left trigger",VR_ACTION_FLOAT},
    {"right_trigger","Right trigger",VR_ACTION_FLOAT},{"left_grip","Left grip",VR_ACTION_FLOAT},
    {"right_grip","Right grip",VR_ACTION_FLOAT},{"left_stick_click","Left stick click",VR_ACTION_BOOLEAN},
    {"right_stick_click","Right stick click",VR_ACTION_BOOLEAN}};
const VrBindingSpec QuestTouchBindings[]={
    {VR_ACTION_MOVE_X,"/user/hand/left/input/thumbstick/x"},{VR_ACTION_MOVE_Y,"/user/hand/left/input/thumbstick/y"},
    {VR_ACTION_LOOK_X,"/user/hand/right/input/thumbstick/x"},{VR_ACTION_LOOK_Y,"/user/hand/right/input/thumbstick/y"},
    {VR_ACTION_SELECT,"/user/hand/right/input/a/click"},{VR_ACTION_BACK,"/user/hand/right/input/b/click"},
    {VR_ACTION_ATTACK,"/user/hand/left/input/x/click"},{VR_ACTION_USE,"/user/hand/left/input/y/click"},
    {VR_ACTION_MENU,"/user/hand/left/input/menu/click"},{VR_ACTION_LEFT_TRIGGER,"/user/hand/left/input/trigger/value"},
    {VR_ACTION_RIGHT_TRIGGER,"/user/hand/right/input/trigger/value"},{VR_ACTION_LEFT_GRIP,"/user/hand/left/input/squeeze/value"},
    {VR_ACTION_RIGHT_GRIP,"/user/hand/right/input/squeeze/value"},{VR_ACTION_LEFT_STICK_CLICK,"/user/hand/left/input/thumbstick/click"},
    {VR_ACTION_RIGHT_STICK_CLICK,"/user/hand/right/input/thumbstick/click"}};
const VrBindingSpec QuestTouchBindingsMenuOnX[]={
    {VR_ACTION_MOVE_X,"/user/hand/left/input/thumbstick/x"},{VR_ACTION_MOVE_Y,"/user/hand/left/input/thumbstick/y"},
    {VR_ACTION_LOOK_X,"/user/hand/right/input/thumbstick/x"},{VR_ACTION_LOOK_Y,"/user/hand/right/input/thumbstick/y"},
    {VR_ACTION_SELECT,"/user/hand/right/input/a/click"},{VR_ACTION_BACK,"/user/hand/right/input/b/click"},
    {VR_ACTION_MENU,"/user/hand/left/input/x/click"},{VR_ACTION_USE,"/user/hand/left/input/y/click"},
    {VR_ACTION_ATTACK,"/user/hand/left/input/menu/click"},{VR_ACTION_LEFT_TRIGGER,"/user/hand/left/input/trigger/value"},
    {VR_ACTION_RIGHT_TRIGGER,"/user/hand/right/input/trigger/value"},{VR_ACTION_LEFT_GRIP,"/user/hand/left/input/squeeze/value"},
    {VR_ACTION_RIGHT_GRIP,"/user/hand/right/input/squeeze/value"},{VR_ACTION_LEFT_STICK_CLICK,"/user/hand/left/input/thumbstick/click"},
    {VR_ACTION_RIGHT_STICK_CLICK,"/user/hand/right/input/thumbstick/click"}};
}

const VrActionSpec* GetVrActionSpecs(){return ActionSpecs;}
const VrBindingSpec* GetQuestTouchBindingSpecs(bool xOpensMenu,unsigned& count)
{
    count=static_cast<unsigned>(sizeof(QuestTouchBindings)/sizeof(QuestTouchBindings[0]));
    return xOpensMenu?QuestTouchBindingsMenuOnX:QuestTouchBindings;
}

void AdaptVrConsoleInput(const char* name,float value,bool desktop,
    bool horizontal,bool vertical,VrConsoleAdapterState* state,
    VrInputBindingSink sink,void* context)
{
    if(!name||!sink)return;
    const auto set=[&](const char* target,float v){sink(context,target,v);};
    if(!desktop){set(name,value);return;}
    if(!state)return;
    if(!std::strcmp(name,"LeftStickX")){set("MoveRight",std::max(0.0f,value));set("MoveLeft",std::max(0.0f,-value));set("SteerRight",std::max(0.0f,value));set("SteerLeft",std::max(0.0f,-value));set("VrLeftStickX",value);}
    else if(!std::strcmp(name,"LeftStickY")){set("MoveUp",std::max(0.0f,value));set("MoveDown",std::max(0.0f,-value));set("VrLeftStickY",value);}
    else if(!std::strcmp(name,"RightStickX")){set("CameraRight",std::max(0.0f,value));set("CameraLeft",std::max(0.0f,-value));set("CameraCarRight",std::max(0.0f,value));set("CameraCarLeft",std::max(0.0f,-value));set("VrRightStickX",value);}
    else if(!std::strcmp(name,"RightStickY")){set("CameraMoveIn",std::max(0.0f,value));set("CameraMoveOut",std::max(0.0f,-value));set("VrRightStickY",value);}
    else if(!std::strcmp(name,"A"))set("feSelect",value);
    else if(!std::strcmp(name,"B")){state->backButton=value;set("feBack",value);set("Jump",value);}
    else if(!std::strcmp(name,"X")){state->attackButton=value;set("feFunction1",value);set("Attack",value);}
    else if(!std::strcmp(name,"Y")){set("feFunction2",value);set("DoAction",value);set("GetOutCar",value);}
    else if(!std::strcmp(name,"Start"))set("feStart",value);
    else if(!std::strcmp(name,"LeftTrigger")){set("Reverse",value);set("CameraZoom",value);}
    else if(!std::strcmp(name,"RightTrigger"))set("Accelerate",value);
    else if(!std::strcmp(name,"White"))set("HandBrake",std::max(state->backButton,value));
    else if(!std::strcmp(name,"LeftThumb")){set("Sprint",value);set("Horn",std::max(state->attackButton,value));}
    else if(!std::strcmp(name,"RightThumb"))set("ResetCar",value);
}

void EmitQuestControllerBindings(const VrInputFrame& input,
    float effectiveLeftTrigger,float effectiveRightTrigger,
    bool leftGripButton,bool rightGripButton,bool leftThumbButton,
    bool rightThumbButton,VrInputBindingSink sink,void* context)
{
    if(!sink) return;
    const auto set=[&](const char* name,float value){sink(context,name,value);};
    set("LeftStickX",input.move.x);set("LeftStickY",input.move.y);
    set("RightStickX",input.look.x);set("RightStickY",input.look.y);
    set("A",input.select);set("B",input.back);set("X",input.attack);
    set("Y",(input.select>0.0f||input.use>0.0f)?1.0f:0.0f);
    set("Start",input.menu);
    set("LeftTrigger",effectiveLeftTrigger);
    set("RightTrigger",effectiveRightTrigger);
    set("Black",leftGripButton?1.0f:0.0f);
    set("White",rightGripButton?1.0f:0.0f);
    set("LeftThumb",leftThumbButton?1.0f:0.0f);
    set("RightThumb",rightThumbButton?1.0f:0.0f);
}

void EmitNeutralVrController(VrInputBindingSink sink,void* context)
{
    const VrInputFrame neutral={};
    EmitQuestControllerBindings(neutral,0.0f,0.0f,false,false,false,false,sink,context);
}

void ResetVrInputSemantics()
{
    SharedVrState& state=GetSharedVrState();
    state.gripValue[0]=state.gripValue[1]=0.0f;
    state.stickClick[0]=state.stickClick[1]=false;
    state.menuAxisLock=state.menuAxisNeutralFrames=0;
    state.menuHorizontalInputDominant=state.menuVerticalInputDominant=false;
}

ThumbstickAxes ApplyVrThumbstickDeadzone(float x,float y)
{
    const float deadzone=0.30f;
    const float length=std::sqrt(x*x+y*y);
    if(length<=deadzone) return ThumbstickAxes{0.0f,0.0f};
    const float scale=std::min(1.0f,(length-deadzone)/(1.0f-deadzone))/length;
    return ThumbstickAxes{x*scale,y*scale};
}

VrInputFrame NormalizeVrInputFrame(const VrInputFrame& raw,
                                   bool suppressLookStick)
{
    VrInputFrame out=raw;
    out.move=ApplyVrThumbstickDeadzone(raw.move.x,raw.move.y);
    out.look=suppressLookStick?ThumbstickAxes{0.0f,0.0f}:
                                 ApplyVrThumbstickDeadzone(raw.look.x,raw.look.y);
    const auto clamp=[](float value){return std::max(0.0f,std::min(1.0f,value));};
    out.select=clamp(out.select);out.back=clamp(out.back);
    out.attack=clamp(out.attack);out.use=clamp(out.use);out.menu=clamp(out.menu);
    out.leftTrigger=clamp(out.leftTrigger);out.rightTrigger=clamp(out.rightTrigger);
    out.leftGrip=clamp(out.leftGrip);out.rightGrip=clamp(out.rightGrip);
    out.leftStickClick=clamp(out.leftStickClick);
    out.rightStickClick=clamp(out.rightStickClick);
    return out;
}

VrInputFrame ProcessVrInputFrame(const VrInputFrame& raw,bool suppressLookStick)
{
    const VrInputFrame input=NormalizeVrInputFrame(raw,suppressLookStick);
    SharedVrState& state=GetSharedVrState();
    UpdateVrMenuAxisLock(input.move,input.look,state.menuAxisLock,
        state.menuAxisNeutralFrames,state.menuHorizontalInputDominant,
        state.menuVerticalInputDominant);
    return input;
}

void SubmitVrInputFrame(const VrInputFrame& raw,VrInputBindingSink sink,void* context)
{
    CharacterManager* characters=GetCharacterManager();
    Character* player=characters?characters->GetCharacter(0):NULL;
    Vehicle* vehicle=(player&&player->IsInCar())?player->GetTargetVehicle():NULL;
    SharedVrState& state=GetSharedVrState();
    const bool suppressLook=state.vrModeEnabled&&vehicle&&state.vehicleControlMode!=2;
    const VrInputFrame input=ProcessVrInputFrame(raw,suppressLook);
    state.gripValue[0]=input.leftGrip;state.gripValue[1]=input.rightGrip;
    state.stickClick[0]=input.leftStickClick>0.5f;
    state.stickClick[1]=input.rightStickClick>0.5f;
    float leftTrigger,rightTrigger;bool leftGrip,rightGrip,leftThumb,rightThumb;
    GetVrVehicleInputOverrides(IsVrYokeVehicle(vehicle?vehicle->GetName():NULL),
        input.leftTrigger,input.rightTrigger,input.leftGrip,input.rightGrip,
        state.stickClick[0],state.stickClick[1],&leftTrigger,&rightTrigger,
        &leftGrip,&rightGrip,&leftThumb,&rightThumb);
    EmitQuestControllerBindings(input,leftTrigger,rightTrigger,leftGrip,rightGrip,
        leftThumb,rightThumb,sink,context);

    static bool skipWasDown=false;
    const bool skipDown=input.select>0.5f||input.back>0.5f||input.attack>0.5f||
        input.use>0.5f||input.menu>0.5f;
    if(skipDown&&!skipWasDown)
    {
        PresentationManager* presentation=GetPresentationManager();
        FMVPlayer* movie=presentation?presentation->GetFMVPlayer():NULL;
        if(movie&&movie->IsDecoderPlaying()&&movie->GetElapsedTime()>0.125f)movie->Abort();
    }
    skipWasDown=skipDown;
}

void UpdateVrMenuAxisLock(const ThumbstickAxes& move,const ThumbstickAxes& look,
    unsigned& lock,unsigned& neutralFrames,bool& horizontal,bool& vertical)
{
    const float x=std::max(std::fabs(move.x),std::fabs(look.x));
    const float y=std::max(std::fabs(move.y),std::fabs(look.y));
    const bool active=std::max(x,y)>0.22f;
    if(lock==0&&active){lock=x>=y?1u:2u;neutralFrames=0;}
    else if(!active){if(++neutralFrames>=6){lock=0;neutralFrames=0;}}
    else neutralFrames=0;
    horizontal=lock==1;vertical=lock==2;
}
}

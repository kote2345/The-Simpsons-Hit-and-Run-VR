#include <vr/openxr_shared_input.h>
#include <vr/openxr_shared_state.h>
#include <vr/openxr_shared_vehicle.h>
#include <vr/openxr_shared_hands.h>
#include <vr/openxrmanager.h>
#include <ai/actionbuttonhandler.h>
#include <worldsim/character/charactercontroller.h>
#include <SDL.h>
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


struct PhysicalInteractState
{
    rmt::Vector previousHandPosition[2];
    bool previousHandValid[2];
    float cooldownSeconds;
    int pulseFrames;
    Uint32 previousUpdateTicks;
    Uint32 previousCooldownTicks;

    PhysicalInteractState()
        : cooldownSeconds(0.0f),pulseFrames(0),previousUpdateTicks(0),
          previousCooldownTicks(0)
    {
        previousHandValid[0]=previousHandValid[1]=false;
    }
};

PhysicalInteractState PhysicalInteract;

void ResetPhysicalInteractTracking()
{
    PhysicalInteract.previousHandValid[0]=false;
    PhysicalInteract.previousHandValid[1]=false;
    PhysicalInteract.cooldownSeconds=0.0f;
    PhysicalInteract.pulseFrames=0;
    PhysicalInteract.previousUpdateTicks=0;
    PhysicalInteract.previousCooldownTicks=0;
}

void CachePhysicalHandSamples()
{
    for(unsigned hand=0;hand<2;++hand)
    {
        rmt::Matrix pose;
        if(!GetControllerLocalPose(hand,&pose))
        {
            PhysicalInteract.previousHandValid[hand]=false;
            continue;
        }
        PhysicalInteract.previousHandPosition[hand]=pose.Row(3);
        PhysicalInteract.previousHandValid[hand]=true;
    }
}

void UpdatePhysicalPushInteract()
{
    // Same tuning as the previous Quest-only implementation: deliberate but
    // forgiving enough that a normal forward button/door push is reliable.
    const float pushMinSpeed=0.80f;
    const float pushForwardDot=0.28f;
    const float pushCooldownSeconds=0.40f;
    const int pushPulseFrames=2;

    const Uint32 now=SDL_GetTicks();
    if(PhysicalInteract.cooldownSeconds>0.0f)
    {
        float dt=PhysicalInteract.previousCooldownTicks==0?0.016f:
            (now-PhysicalInteract.previousCooldownTicks)*0.001f;
        PhysicalInteract.previousCooldownTicks=now;
        if(dt<0.0f||dt>0.1f)dt=0.016f;
        PhysicalInteract.cooldownSeconds=std::max(0.0f,
            PhysicalInteract.cooldownSeconds-dt);
    }
    else
    {
        PhysicalInteract.previousCooldownTicks=now;
    }

    SharedVrState& state=GetSharedVrState();
    CharacterManager* characters=GetCharacterManager();
    Character* player=characters?characters->GetCharacter(0):NULL;
    if(!state.vrModeEnabled||!player||player->IsInCar()||
       !player->GetController()||!player->GetController()->IsActive())
    {
        PhysicalInteract.previousHandValid[0]=false;
        PhysicalInteract.previousHandValid[1]=false;
        return;
    }

    // Never synthesize DoAction from a random hand movement. The game's
    // active ButtonHandler decides whether this interaction type is eligible.
    ActionButton::ButtonHandler* handler=player->GetActionButtonHandler();
    if(!handler||!handler->AllowPhysicalInteract())
    {
        CachePhysicalHandSamples();
        return;
    }

    float dt=PhysicalInteract.previousUpdateTicks==0?0.016f:
        (now-PhysicalInteract.previousUpdateTicks)*0.001f;
    PhysicalInteract.previousUpdateTicks=now;
    if(dt<0.001f||dt>0.1f)dt=0.016f;

    rmt::Vector headForward(0.0f,0.0f,1.0f);
    const bool haveHead=GetHeadForward(&headForward);

    for(unsigned hand=0;hand<2;++hand)
    {
        // A hand attached to the physical wheel/yoke is not an interaction
        // gesture. This mirrors the previous Quest implementation.
        if(state.wheelGrabbed[hand])
        {
            PhysicalInteract.previousHandValid[hand]=false;
            continue;
        }

        rmt::Matrix pose;
        if(!GetControllerLocalPose(hand,&pose))
        {
            PhysicalInteract.previousHandValid[hand]=false;
            continue;
        }
        const rmt::Vector position=pose.Row(3);
        if(!PhysicalInteract.previousHandValid[hand])
        {
            PhysicalInteract.previousHandPosition[hand]=position;
            PhysicalInteract.previousHandValid[hand]=true;
            continue;
        }

        rmt::Vector delta=position;
        delta.Sub(PhysicalInteract.previousHandPosition[hand]);
        PhysicalInteract.previousHandPosition[hand]=position;
        const float speed=delta.Magnitude()/dt;
        if(speed<pushMinSpeed||PhysicalInteract.cooldownSeconds>0.0f||
           PhysicalInteract.pulseFrames>0)
            continue;

        rmt::Vector velocityDirection=delta;
        if(velocityDirection.NormalizeSafe()<0.0001f)continue;

        rmt::Vector handForward;
        pose.RotateVector(rmt::Vector(0.0f,0.0f,1.0f),&handForward);
        const float handLength=handForward.NormalizeSafe();

        float bestAlignment=-1.0f;
        if(handLength>0.0001f)
        {
            const float alignment=velocityDirection.x*handForward.x+
                velocityDirection.y*handForward.y+
                velocityDirection.z*handForward.z;
            bestAlignment=std::max(bestAlignment,alignment);
        }
        if(haveHead)
        {
            const float alignment=velocityDirection.x*headForward.x+
                velocityDirection.y*headForward.y+
                velocityDirection.z*headForward.z;
            bestAlignment=std::max(bestAlignment,alignment);

            // Also accept a mostly-horizontal outward shove so small wrist
            // pitch differences do not make door/button pushes unreliable.
            rmt::Vector horizontal=velocityDirection;
            horizontal.y=0.0f;
            if(horizontal.NormalizeSafe()>0.35f)
            {
                rmt::Vector headHorizontal=headForward;
                headHorizontal.y=0.0f;
                if(headHorizontal.NormalizeSafe()>0.0001f)
                {
                    const float horizontalAlignment=
                        horizontal.x*headHorizontal.x+
                        horizontal.z*headHorizontal.z;
                    bestAlignment=std::max(bestAlignment,horizontalAlignment);
                }
            }
        }

        if(bestAlignment<pushForwardDot)continue;

        PhysicalInteract.pulseFrames=pushPulseFrames;
        PhysicalInteract.cooldownSeconds=pushCooldownSeconds;
        // The common haptic API currently addresses both controllers. The
        // interaction itself remains hand-specific; this keeps feedback on
        // both runtimes without putting OpenXR calls back into either backend.
        ApplyControllerHaptics(0.75f,45u);
        SDL_Log("OpenXR shared: physical interact hand=%u speed=%.2f align=%.2f",
                hand,speed,bestAlignment);
        break;
    }
}
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
    if(!std::strcmp(name,"LeftStickX")){set("MoveRight",std::max(0.0f,value));set("MoveLeft",std::max(0.0f,-value));set("SteerRight",std::max(0.0f,value));set("SteerLeft",std::max(0.0f,-value));set("feMouseRight",value);}
    else if(!std::strcmp(name,"LeftStickY")){set("MoveUp",std::max(0.0f,value));set("MoveDown",std::max(0.0f,-value));set("feMouseUp",value);}
    else if(!std::strcmp(name,"RightStickX")){set("CameraRight",std::max(0.0f,value));set("CameraLeft",std::max(0.0f,-value));set("CameraCarRight",std::max(0.0f,value));set("CameraCarLeft",std::max(0.0f,-value));set("feMouseLeft",value);}
    else if(!std::strcmp(name,"RightStickY")){set("CameraMoveIn",std::max(0.0f,value));set("CameraMoveOut",std::max(0.0f,-value));set("feMouseDown",value);}
    else if(!std::strcmp(name,"A"))set("feSelect",value);
    else if(!std::strcmp(name,"B"))
    {
        state->backButton=value;set("feBack",value);
        if(GetSharedVrState().vrModeEnabled)set("Jump",value);
        else set("Sprint",value);
    }
    else if(!std::strcmp(name,"X")){state->attackButton=value;set("feFunction1",value);set("Attack",value);}
    else if(!std::strcmp(name,"Y")){set("feFunction2",value);set("DoAction",value);set("GetOutCar",value);}
    else if(!std::strcmp(name,"Start"))set("feStart",value);
    else if(!std::strcmp(name,"LeftTrigger")){set("Reverse",value);set("CameraZoom",value);}
    else if(!std::strcmp(name,"RightTrigger"))
    {
        set("Accelerate",value);
        // Quest exposes RightTrigger directly and CharacterMappable binds it
        // to Jump in Original mode. Win32 uses backend aliases instead.
        if(!GetSharedVrState().vrModeEnabled)set("Jump",value);
    }
    else if(!std::strcmp(name,"Black")){}
    else if(!std::strcmp(name,"White"))
    {
        // GUI grip edges are dispatched directly by the desktop OpenXR
        // runtime; retain only the gameplay alias here.
        set("HandBrake",std::max(state->backButton,value));
    }
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
    ResetPhysicalInteractTracking();
    ResetRenderedHandWorldPositions();
    SharedVrState& state=GetSharedVrState();
    state.gripValue[0]=state.gripValue[1]=0.0f;
    state.stickClick[0]=state.stickClick[1]=false;
    state.menuAxisLock=state.menuAxisNeutralFrames=0;
    state.menuHorizontalInputDominant=state.menuVerticalInputDominant=false;
}

bool IsSharedVrGameplayEnabled()
{
    return GetSharedVrState().vrModeEnabled;
}

float GetHandGripValue(unsigned hand)
{
    return hand<2?GetSharedVrState().gripValue[hand]:0.0f;
}

bool GetHandWorldPosition(unsigned hand,rmt::Vector* outPosition)
{
    return GetRenderedHandWorldPosition(hand,outPosition);
}

bool IsPhysicalInteractPulse()
{
    return PhysicalInteract.pulseFrames>0;
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
    UpdatePhysicalPushInteract();
    VrInputFrame gameplayInput=input;
    // Original mode follows the gamepad layout: right grip is the on-foot
    // attack button. In VR mode the grip remains reserved for grabbing.
    if(!state.vrModeEnabled)
        gameplayInput.attack=std::max(gameplayInput.attack,input.rightGrip);
    const bool physicalPush=PhysicalInteract.pulseFrames>0;
    if(physicalPush)
    {
        --PhysicalInteract.pulseFrames;
        gameplayInput.select=1.0f;
        gameplayInput.use=1.0f;
    }
    float leftTrigger,rightTrigger;bool leftGrip,rightGrip,leftThumb,rightThumb;
    GetVrVehicleInputOverrides(IsVrYokeVehicle(vehicle?vehicle->GetName():NULL),
        input.leftTrigger,input.rightTrigger,input.leftGrip,input.rightGrip,
        state.stickClick[0],state.stickClick[1],&leftTrigger,&rightTrigger,
        &leftGrip,&rightGrip,&leftThumb,&rightThumb);
    EmitQuestControllerBindings(gameplayInput,leftTrigger,rightTrigger,leftGrip,rightGrip,
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

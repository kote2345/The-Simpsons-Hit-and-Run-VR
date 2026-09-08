#include <vr/vr_body_ik.h>
#if defined(SRR2_OPENXR_PLATFORM_WIN32) && defined(SRR2_VR_RENDERER_VULKAN)
#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <vr/openxr_desktop_runtime.h>
#include <vr/openxrmanager.h>
#include <vr/openxr_shared_render.h>
#include <vr/openxr_shared_frame.h>
#include <vr/openxr_shared_graphics.h>
#include <vr/openxr_shared_hud.h>
#include <vr/openxr_shared_hands.h>
#include <vr/openxr_shared_input.h>
#include <vr/openxr_shared_menu.h>
#include <vr/openxr_shared_settings.h>
#include <vr/openxr_shared_state.h>
#include <vr/openxr_shared_tracking.h>
#include <vr/openxr_shared_vehicle.h>
#include <vr/openxr_platform_loader.h>
#include <vr/openxr_platform_instance.h>
#include <vr/vulkan/openxr_vulkan_context.h>
#include <p3d/camera.hpp>
#include <input/inputmanager.h>
#include <presentation/presentation.h>
#include <presentation/gui/guisystem.h>
#include <presentation/fmvplayer/fmvplayer.h>
#include <worldsim/character/charactermanager.h>
#include <worldsim/character/character.h>
#include <worldsim/character/charactercontroller.h>
#include <worldsim/redbrick/vehicle.h>
#include <camera/supercam.h>
#include <camera/supercamcentral.h>
#include <camera/supercammanager.h>
#include <SDL.h>
#include <cmath>
#include <cstring>
#include <vector>
namespace SharOpenXR { namespace Desktop { namespace {
void* loader=NULL; PFN_xrGetInstanceProcAddr getProc=NULL;
XrInstance instance=XR_NULL_HANDLE; XrSystemId system=XR_NULL_SYSTEM_ID;
XrSession session=XR_NULL_HANDLE; XrSpace space=XR_NULL_HANDLE;
XrSwapchain swapchain=XR_NULL_HANDLE; int32_t eyeWidth=0,eyeHeight=0;
uint32_t recommendedEyeWidth=0,recommendedEyeHeight=0;
VkFormat swapchainFormat=VK_FORMAT_R8G8B8A8_UNORM;
std::vector<XrSwapchainImageVulkanKHR> images;
XrSessionState sessionState=XR_SESSION_STATE_UNKNOWN; bool running=false;
XrFrameState currentFrame={XR_TYPE_FRAME_STATE};XrView currentViews[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
SharedFrameState sharedFrame;
SharedVulkanRenderSequence renderSequence;
XrViewStateFlags currentViewFlags=0;
uint32_t currentImage=0,currentEye=0;bool frameActive=false,imageAcquired=false,eyeActive=false;
bool worldRendering=false,embeddedHudRendering=false;
bool multiviewRendering=false,multiviewTargetActive=false;
rmt::Matrix multiviewProjection[2],multiviewAdjustment[2];
bool originValid=false,usingStageSpace=false;
bool colorScaleBiasEnabled=false;
SharedSystemRecenterState systemRecenter;
rmt::Matrix cullingBaseCamera;
bool cullingBaseValid=false;
XrPosef origin={{0.0f,0.0f,0.0f,1.0f},{0.0f,0.0f,0.0f}};
uint64_t frameSerial=0;
PFN_xrDestroySpace destroySpace=NULL; PFN_xrDestroySession destroySession=NULL; PFN_xrDestroyInstance destroyInstance=NULL;
PFN_xrPollEvent pollEvent=NULL;PFN_xrBeginSession beginSession=NULL;PFN_xrEndSession endSession=NULL;
PFN_xrWaitFrame waitFrame=NULL;PFN_xrBeginFrame beginFrame=NULL;PFN_xrEndFrame endFrame=NULL;
PFN_xrLocateViews locateViews=NULL;PFN_xrCreateSwapchain createSwapchain=NULL;PFN_xrDestroySwapchain destroySwapchain=NULL;
PFN_xrEnumerateViewConfigurationViews enumerateViews=NULL;PFN_xrEnumerateSwapchainFormats enumerateFormats=NULL;PFN_xrEnumerateSwapchainImages enumerateImages=NULL;
PFN_xrAcquireSwapchainImage acquireImage=NULL;PFN_xrWaitSwapchainImage waitImage=NULL;PFN_xrReleaseSwapchainImage releaseImage=NULL;
SharedFrameApi FrameApi(){SharedFrameApi api={waitFrame,beginFrame,locateViews,
 acquireImage,waitImage,releaseImage,endFrame};return api;}
XrActionSet inputActionSet=XR_NULL_HANDLE;
XrAction moveXAction=XR_NULL_HANDLE,moveYAction=XR_NULL_HANDLE;
XrAction lookXAction=XR_NULL_HANDLE,lookYAction=XR_NULL_HANDLE;
XrAction selectAction=XR_NULL_HANDLE,backAction=XR_NULL_HANDLE;
XrAction attackAction=XR_NULL_HANDLE,useAction=XR_NULL_HANDLE,menuAction=XR_NULL_HANDLE;
XrAction leftTriggerAction=XR_NULL_HANDLE,rightTriggerAction=XR_NULL_HANDLE;
XrAction leftGripAction=XR_NULL_HANDLE,rightGripAction=XR_NULL_HANDLE;
XrAction leftStickClickAction=XR_NULL_HANDLE,rightStickClickAction=XR_NULL_HANDLE;
XrAction handPoseAction=XR_NULL_HANDLE;
XrAction hapticAction=XR_NULL_HANDLE;
XrPath handPaths[2]={XR_NULL_PATH,XR_NULL_PATH};
XrSpace handSpaces[2]={XR_NULL_HANDLE,XR_NULL_HANDLE};
XrPosef handPoses[2]={{{0,0,0,1},{0,0,0}},{{0,0,0,1},{0,0,0}}};
bool handPoseValid[2]={false,false};
unsigned& menuAxisLock=GetSharedVrState().menuAxisLock;
unsigned& menuAxisNeutralFrames=GetSharedVrState().menuAxisNeutralFrames;
bool& menuHorizontalInputDominant=GetSharedVrState().menuHorizontalInputDominant;
bool& menuVerticalInputDominant=GetSharedVrState().menuVerticalInputDominant;
bool& vrBaseHeadingValid=GetSharedVrState().vrBaseHeadingValid;
bool& roomscaleMovementSuspended=GetSharedVrState().roomscaleMovementSuspended;
PFN_xrStringToPath stringToPath=NULL;PFN_xrCreateActionSet createActionSet=NULL;
PFN_xrDestroyActionSet destroyActionSet=NULL;PFN_xrCreateAction createAction=NULL;
PFN_xrSuggestInteractionProfileBindings suggestBindings=NULL;
PFN_xrAttachSessionActionSets attachActionSets=NULL;PFN_xrSyncActions syncActions=NULL;
PFN_xrGetActionStateBoolean getBoolean=NULL;PFN_xrGetActionStateFloat getFloat=NULL;
PFN_xrCreateActionSpace createActionSpace=NULL;PFN_xrLocateSpace locateSpace=NULL;
PFN_xrApplyHapticFeedback applyHapticFeedback=NULL;
PFN_xrRequestDisplayRefreshRateFB requestDisplayRefreshRate=NULL;
bool Load(XrInstance i,const char* n,PFN_xrVoidFunction* f){return XR_SUCCEEDED(getProc(i,n,f))&&*f;}
VrConsoleAdapterState consoleAdapterState;
bool heightResetGestureHeld=false;
bool developerLeftGripHeld=false,developerRightGripHeld=false;
}
void ShutdownRuntime();
static void SetDesktopConsoleInput(void* context,const char* name,float value);

static bool IsDesktopHeightResetGesture()
{
 if(!originValid||!handPoseValid[1]||
    !(currentViewFlags&XR_VIEW_STATE_POSITION_VALID_BIT))return false;
 const XrVector3f head={
  (currentViews[0].pose.position.x+currentViews[1].pose.position.x)*0.5f,
  (currentViews[0].pose.position.y+currentViews[1].pose.position.y)*0.5f,
  (currentViews[0].pose.position.z+currentViews[1].pose.position.z)*0.5f};
 const XrVector3f& hand=handPoses[1].position;
 const float dx=hand.x-head.x,dy=hand.y-head.y,dz=hand.z-head.z;
 return dx*dx+dy*dy+dz*dz<=0.30f*0.30f;
}

static bool ResetDesktopHeightFromCurrentPose()
{
 if(!RecenterSharedTracking(currentViews,currentViewFlags,&origin))return false;
 originValid=true;
 GetSharedMoviePanel().InvalidateAnchor();
 GetSharedVrMenu().InvalidateAnchor();
 SDL_Log("PCVR: height recentered by right-hand gesture at %.3f m",
         origin.position.y);
 return true;
}

static bool FillSharedHudRuntime(SharedHudRuntime* runtime)
{
 if(!runtime)return false;
 runtime->activeEye=eyeActive?currentEye+1:0;
 runtime->multiviewImageAcquired=imageAcquired;
 runtime->embeddedHudRendering=embeddedHudRendering;
 runtime->cullingBaseValid=cullingBaseValid;
 runtime->vrModeEnabled=GetSharedVrState().vrModeEnabled;
 runtime->origin=origin;runtime->cullingBaseCamera=cullingBaseCamera;
 runtime->activeWheelCentre=GetSharedVrState().activeWheelCentre;
 for(unsigned i=0;i<2;++i){runtime->views[i]=currentViews[i];
  runtime->eyeWidth[i]=eyeWidth;runtime->eyeHeight[i]=eyeHeight;
  runtime->handPoses[i]=handPoses[i];runtime->handPoseValid[i]=handPoseValid[i];}
 runtime->renderImage=(currentImage<images.size())?images[currentImage].image:VK_NULL_HANDLE;
 runtime->renderFormat=swapchainFormat;
 return true;
}

static bool RecreateDesktopSwapchain(void*,float scale)
{
 if(!session||!createSwapchain||!destroySwapchain||!enumerateImages||
    !recommendedEyeWidth||!recommendedEyeHeight)return false;
 vkQueueWaitIdle(GetVulkanContext().GetQueue());
 if(swapchain!=XR_NULL_HANDLE)destroySwapchain(swapchain);
 swapchain=XR_NULL_HANDLE;images.clear();
 eyeWidth=std::max(1,static_cast<int32_t>(recommendedEyeWidth*scale));
 eyeHeight=std::max(1,static_cast<int32_t>(recommendedEyeHeight*scale));
 XrSwapchainCreateInfo info={XR_TYPE_SWAPCHAIN_CREATE_INFO};
 info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|
     XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
 info.format=swapchainFormat;info.sampleCount=1;info.width=eyeWidth;
 info.height=eyeHeight;info.faceCount=1;info.arraySize=2;info.mipCount=1;
 if(XR_FAILED(createSwapchain(session,&info,&swapchain)))return false;
 uint32_t count=0;if(XR_FAILED(enumerateImages(swapchain,0,&count,NULL)))return false;
 images.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
 return XR_SUCCEEDED(enumerateImages(swapchain,count,&count,
     reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())));
}

static bool CreateInputActions()
{
 XrActionSetCreateInfo setInfo={XR_TYPE_ACTION_SET_CREATE_INFO};
 std::strcpy(setInfo.actionSetName,"gameplay");
 std::strcpy(setInfo.localizedActionSetName,"Gameplay");
 if(XR_FAILED(createActionSet(instance,&setInfo,&inputActionSet)))return false;
 stringToPath(instance,"/user/hand/left",&handPaths[0]);
 stringToPath(instance,"/user/hand/right",&handPaths[1]);
 auto create=[](const char* name,const char* localized,XrActionType type,XrAction* action,
                bool bothHands=false){
  XrActionCreateInfo info={XR_TYPE_ACTION_CREATE_INFO};info.actionType=type;
  std::strncpy(info.actionName,name,XR_MAX_ACTION_NAME_SIZE-1);
  std::strncpy(info.localizedActionName,localized,XR_MAX_LOCALIZED_ACTION_NAME_SIZE-1);
  if(bothHands){info.countSubactionPaths=2;info.subactionPaths=handPaths;}
  return XR_SUCCEEDED(createAction(inputActionSet,&info,action));};
 XrAction* actionSlots[VR_ACTION_COUNT]={&moveXAction,&moveYAction,&lookXAction,&lookYAction,
  &selectAction,&backAction,&attackAction,&useAction,&menuAction,&leftTriggerAction,
  &rightTriggerAction,&leftGripAction,&rightGripAction,&leftStickClickAction,&rightStickClickAction};
 const VrActionSpec* specs=GetVrActionSpecs();
 for(unsigned i=0;i<VR_ACTION_COUNT;++i)
  if(!create(specs[i].name,specs[i].localizedName,
             specs[i].kind==VR_ACTION_BOOLEAN?XR_ACTION_TYPE_BOOLEAN_INPUT:XR_ACTION_TYPE_FLOAT_INPUT,
             actionSlots[i]))return false;
 if(!create("hand_pose","Tracked hand pose",XR_ACTION_TYPE_POSE_INPUT,&handPoseAction,true)||
    !create("haptic","Controller vibration",XR_ACTION_TYPE_VIBRATION_OUTPUT,&hapticAction,true))return false;
 auto path=[](const char* text){XrPath result=XR_NULL_PATH;stringToPath(instance,text,&result);return result;};
 unsigned commonBindingCount=0;
 const VrBindingSpec* commonBindings=GetQuestTouchBindingSpecs(true,commonBindingCount);
 XrActionSuggestedBinding bindings[VR_ACTION_COUNT+4];
 for(unsigned i=0;i<commonBindingCount;++i)
  bindings[i]=XrActionSuggestedBinding{*actionSlots[commonBindings[i].action],path(commonBindings[i].path)};
 bindings[commonBindingCount++]=XrActionSuggestedBinding{handPoseAction,path("/user/hand/left/input/grip/pose")};
 bindings[commonBindingCount++]=XrActionSuggestedBinding{handPoseAction,path("/user/hand/right/input/grip/pose")};
 bindings[commonBindingCount++]=XrActionSuggestedBinding{hapticAction,path("/user/hand/left/output/haptic")};
 bindings[commonBindingCount++]=XrActionSuggestedBinding{hapticAction,path("/user/hand/right/output/haptic")};
 auto suggest=[&](const char* profile,XrActionSuggestedBinding* values,uint32_t count,const char* label){XrInteractionProfileSuggestedBinding info={XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};info.interactionProfile=path(profile);info.countSuggestedBindings=count;info.suggestedBindings=values;const XrResult r=suggestBindings(instance,&info);if(XR_FAILED(r))SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"PCVR: %s bindings unavailable (%d)",label,static_cast<int>(r));return XR_SUCCEEDED(r);};
 const XrResult result=suggest("/interaction_profiles/oculus/touch_controller",bindings,commonBindingCount,"Touch")?XR_SUCCESS:XR_ERROR_PATH_UNSUPPORTED;
 if(XR_FAILED(result)){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: Touch bindings failed (%d)",static_cast<int>(result));return false;}
 SDL_Log("PCVR: Quest Touch controller bindings created");return true;
}

static void ResetVirtualController()
{
 InputManager* manager=InputManager::GetInstance();
 UserController* controller=manager?manager->GetController(0):NULL;
 if(!controller)return;
 EmitNeutralVrController(SetDesktopConsoleInput,controller);
 ResetVrInputSemantics();
 consoleAdapterState=VrConsoleAdapterState();
 heightResetGestureHeld=false;
 developerLeftGripHeld=developerRightGripHeld=false;
 controller->ClearVirtualInputs();
 controller->SetVirtualInputAvailable(false);
}

static void SetDesktopConsoleInput(void* context,const char* name,float value)
{
 UserController* controller=static_cast<UserController*>(context);
 const auto raw=[](void* c,const char* target,float v){UserController* u=static_cast<UserController*>(c);const int index=u->GetIdByName(target);if(index>=0)u->SetVirtualInputValue(static_cast<unsigned>(index),v);};
 AdaptVrConsoleInput(name,value,true,menuHorizontalInputDominant,
                     menuVerticalInputDominant,&consoleAdapterState,raw,controller);
}

static void SyncInputActions()
{
 if(!running||inputActionSet==XR_NULL_HANDLE)return;
 XrActiveActionSet active={inputActionSet,XR_NULL_PATH};
 XrActionsSyncInfo sync={XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&active;
 const XrResult syncResult=syncActions(session,&sync);
 if(XR_FAILED(syncResult)){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR input: xrSyncActions failed (%d)",static_cast<int>(syncResult));ResetVirtualController();return;}
 bool anyActionActive=false;
 auto value=[&anyActionActive](XrAction action){XrActionStateGetInfo get={XR_TYPE_ACTION_STATE_GET_INFO};get.action=action;XrActionStateFloat state={XR_TYPE_ACTION_STATE_FLOAT};const bool ok=XR_SUCCEEDED(getFloat(session,&get,&state))&&state.isActive;anyActionActive|=ok;return ok?state.currentState:0.0f;};
 auto pressed=[&anyActionActive](XrAction action){XrActionStateGetInfo get={XR_TYPE_ACTION_STATE_GET_INFO};get.action=action;XrActionStateBoolean state={XR_TYPE_ACTION_STATE_BOOLEAN};const bool ok=XR_SUCCEEDED(getBoolean(session,&get,&state))&&state.isActive;anyActionActive|=ok;return ok&&state.currentState;};
 InputManager* manager=InputManager::GetInstance();UserController* controller=manager?manager->GetController(0):NULL;
 if(!controller)return;
 // The shared binder emits every console control on every XR frame, including
 // zeroes. UserController keeps changed releases pending until game update.
 controller->SetVirtualInputAvailable(true);
 const bool rightStickDown=pressed(rightStickClickAction);
 const bool heightResetGesture=rightStickDown&&IsDesktopHeightResetGesture();
 if(heightResetGesture&&!heightResetGestureHeld)
     ResetDesktopHeightFromCurrentPose();
 heightResetGestureHeld=rightStickDown;
 VrInputFrame raw={{value(moveXAction),value(moveYAction)},
                   {value(lookXAction),value(lookYAction)},
                   pressed(selectAction)?1.0f:0.0f,
                   pressed(backAction)?1.0f:0.0f,
                   pressed(attackAction)?1.0f:0.0f,
                   pressed(useAction)?1.0f:0.0f,
                   pressed(menuAction)?1.0f:0.0f,
                   value(leftTriggerAction),value(rightTriggerAction),
                   value(leftGripAction),value(rightGripAction),
                   pressed(leftStickClickAction)?1.0f:0.0f,
                   rightStickDown&&!heightResetGesture?1.0f:0.0f};
 SubmitVrInputFrame(raw,SetDesktopConsoleInput,controller);
 // Quest's controller backend exposes Black/White as native GUI buttons.
 // Win32 does not, and routing synthetic frontend inputs through its physical
 // mappable table is not reliable across frontend/ingame registrations.
 // Deliver the identical final GUI messages directly on the grip press edge.
 const bool leftDeveloperGrip=raw.leftGrip>=0.65f;
 const bool rightDeveloperGrip=raw.rightGrip>=0.65f;
 if(IsDeveloperMenusEnabled())
 {
  if(leftDeveloperGrip&&!developerLeftGripHeld)
   GetGuiSystem()->HandleMessage(GUI_MSG_CONTROLLER_L1,0,0);
  if(rightDeveloperGrip&&!developerRightGripHeld)
   GetGuiSystem()->HandleMessage(GUI_MSG_CONTROLLER_R1,0,0);
 }
 developerLeftGripHeld=leftDeveloperGrip;
 developerRightGripHeld=rightDeveloperGrip;
 static uint32_t inputLogCounter=0;
 if((++inputLogCounter%300)==1)
  SDL_Log("PCVR input: active=%d sticks=%.2f %.2f / %.2f %.2f buttons=%.0f%.0f%.0f%.0f start=%.0f triggers=%.2f %.2f grips=%.2f %.2f dev=%d ids=%d,%d,%d,%d,%d",
          anyActionActive?1:0,raw.move.x,raw.move.y,raw.look.x,raw.look.y,
          raw.select,raw.back,raw.attack,raw.use,raw.menu,raw.leftTrigger,raw.rightTrigger,
          raw.leftGrip,raw.rightGrip,IsDeveloperMenusEnabled()?1:0,
          controller->GetIdByName("feSelect"),controller->GetIdByName("MoveUp"),controller->GetIdByName("feStart"),
          controller->GetIdByName("feL1"),controller->GetIdByName("feR1"));
}

bool InitializeRuntime(){
 SetSharedHudRuntimeProvider(FillSharedHudRuntime);
 if(instance!=XR_NULL_HANDLE)return true; LoadVrSettings();GetSharedVrMenu().Reset();GetSharedMoviePanel().End();loader=Platform::OpenLoader();
 if(!loader){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: %s",Platform::GetLoaderError());return false;}
 getProc=reinterpret_cast<PFN_xrGetInstanceProcAddr>(Platform::GetLoaderSymbol(loader,"xrGetInstanceProcAddr"));
 if(!getProc||!Platform::InitializeOpenXRLoader(getProc)){ShutdownRuntime();return false;}
 PFN_xrEnumerateInstanceExtensionProperties enumerate=NULL; PFN_xrCreateInstance create=NULL;
 if(!Load(XR_NULL_HANDLE,"xrEnumerateInstanceExtensionProperties",reinterpret_cast<PFN_xrVoidFunction*>(&enumerate))||!Load(XR_NULL_HANDLE,"xrCreateInstance",reinterpret_cast<PFN_xrVoidFunction*>(&create))){ShutdownRuntime();return false;}
 uint32_t count=0;if(XR_FAILED(enumerate(NULL,0,&count,NULL))){ShutdownRuntime();return false;}
 std::vector<XrExtensionProperties> props(count,{XR_TYPE_EXTENSION_PROPERTIES});if(XR_FAILED(enumerate(NULL,count,&count,props.data()))){ShutdownRuntime();return false;}
 bool vulkan=false,refreshRateExtension=false;for(size_t i=0;i<props.size();++i){vulkan|=std::strcmp(props[i].extensionName,XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME)==0;refreshRateExtension|=std::strcmp(props[i].extensionName,XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)==0;colorScaleBiasEnabled|=std::strcmp(props[i].extensionName,XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME)==0;}
 if(!vulkan){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: XR_KHR_vulkan_enable2 unavailable");ShutdownRuntime();return false;}
 std::vector<const char*> extensions;Platform::AppendRequiredInstanceExtensions(extensions);extensions.push_back(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);if(refreshRateExtension)extensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);if(colorScaleBiasEnabled)extensions.push_back(XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME);
 XrInstanceCreateInfo ci={XR_TYPE_INSTANCE_CREATE_INFO};if(!Platform::PrepareInstanceCreateInfo(&ci)){ShutdownRuntime();return false;}
 std::strncpy(ci.applicationInfo.applicationName,"The Simpsons Hit & Run PCVR",XR_MAX_APPLICATION_NAME_SIZE-1);std::strncpy(ci.applicationInfo.engineName,"Pure3D",XR_MAX_ENGINE_NAME_SIZE-1);
 // SteamVR still exposes an OpenXR 1.0 runtime on a number of supported
 // installations.  Request the 1.0 baseline instead of the header version
 // (currently 1.1), otherwise xrCreateInstance may reject the application.
 ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);ci.enabledExtensionCount=static_cast<uint32_t>(extensions.size());ci.enabledExtensionNames=extensions.data();
 const XrResult createResult=create(&ci,&instance);
 if(XR_FAILED(createResult)){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: xrCreateInstance failed (%d)",static_cast<int>(createResult));ShutdownRuntime();return false;}
 PFN_xrGetSystem getSystem=NULL;PFN_xrCreateSession createSession=NULL;PFN_xrCreateReferenceSpace createSpace=NULL;
 if(!Load(instance,"xrDestroyInstance",reinterpret_cast<PFN_xrVoidFunction*>(&destroyInstance))||!Load(instance,"xrGetSystem",reinterpret_cast<PFN_xrVoidFunction*>(&getSystem))||!Load(instance,"xrCreateSession",reinterpret_cast<PFN_xrVoidFunction*>(&createSession))||!Load(instance,"xrDestroySession",reinterpret_cast<PFN_xrVoidFunction*>(&destroySession))||!Load(instance,"xrCreateReferenceSpace",reinterpret_cast<PFN_xrVoidFunction*>(&createSpace))||!Load(instance,"xrDestroySpace",reinterpret_cast<PFN_xrVoidFunction*>(&destroySpace))||!Load(instance,"xrPollEvent",reinterpret_cast<PFN_xrVoidFunction*>(&pollEvent))||!Load(instance,"xrBeginSession",reinterpret_cast<PFN_xrVoidFunction*>(&beginSession))||!Load(instance,"xrEndSession",reinterpret_cast<PFN_xrVoidFunction*>(&endSession))||!Load(instance,"xrWaitFrame",reinterpret_cast<PFN_xrVoidFunction*>(&waitFrame))||!Load(instance,"xrBeginFrame",reinterpret_cast<PFN_xrVoidFunction*>(&beginFrame))||!Load(instance,"xrEndFrame",reinterpret_cast<PFN_xrVoidFunction*>(&endFrame))||!Load(instance,"xrLocateViews",reinterpret_cast<PFN_xrVoidFunction*>(&locateViews))||!Load(instance,"xrEnumerateViewConfigurationViews",reinterpret_cast<PFN_xrVoidFunction*>(&enumerateViews))||!Load(instance,"xrEnumerateSwapchainFormats",reinterpret_cast<PFN_xrVoidFunction*>(&enumerateFormats))||!Load(instance,"xrCreateSwapchain",reinterpret_cast<PFN_xrVoidFunction*>(&createSwapchain))||!Load(instance,"xrDestroySwapchain",reinterpret_cast<PFN_xrVoidFunction*>(&destroySwapchain))||!Load(instance,"xrEnumerateSwapchainImages",reinterpret_cast<PFN_xrVoidFunction*>(&enumerateImages))||!Load(instance,"xrAcquireSwapchainImage",reinterpret_cast<PFN_xrVoidFunction*>(&acquireImage))||!Load(instance,"xrWaitSwapchainImage",reinterpret_cast<PFN_xrVoidFunction*>(&waitImage))||!Load(instance,"xrReleaseSwapchainImage",reinterpret_cast<PFN_xrVoidFunction*>(&releaseImage))||!Load(instance,"xrStringToPath",reinterpret_cast<PFN_xrVoidFunction*>(&stringToPath))||!Load(instance,"xrCreateActionSet",reinterpret_cast<PFN_xrVoidFunction*>(&createActionSet))||!Load(instance,"xrDestroyActionSet",reinterpret_cast<PFN_xrVoidFunction*>(&destroyActionSet))||!Load(instance,"xrCreateAction",reinterpret_cast<PFN_xrVoidFunction*>(&createAction))||!Load(instance,"xrSuggestInteractionProfileBindings",reinterpret_cast<PFN_xrVoidFunction*>(&suggestBindings))||!Load(instance,"xrAttachSessionActionSets",reinterpret_cast<PFN_xrVoidFunction*>(&attachActionSets))||!Load(instance,"xrSyncActions",reinterpret_cast<PFN_xrVoidFunction*>(&syncActions))||!Load(instance,"xrGetActionStateBoolean",reinterpret_cast<PFN_xrVoidFunction*>(&getBoolean))||!Load(instance,"xrGetActionStateFloat",reinterpret_cast<PFN_xrVoidFunction*>(&getFloat))||!Load(instance,"xrCreateActionSpace",reinterpret_cast<PFN_xrVoidFunction*>(&createActionSpace))||!Load(instance,"xrLocateSpace",reinterpret_cast<PFN_xrVoidFunction*>(&locateSpace))||!Load(instance,"xrApplyHapticFeedback",reinterpret_cast<PFN_xrVoidFunction*>(&applyHapticFeedback))){ShutdownRuntime();return false;}
 if(refreshRateExtension)Load(instance,"xrRequestDisplayRefreshRateFB",reinterpret_cast<PFN_xrVoidFunction*>(&requestDisplayRefreshRate));
 if(!CreateInputActions()){ShutdownRuntime();return false;}
 XrSystemGetInfo si={XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
 if(XR_FAILED(getSystem(instance,&si,&system))||!GetVulkanContext().Initialize(instance,system,getProc)){ShutdownRuntime();return false;}
 XrGraphicsBindingVulkan2KHR binding={XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};binding.instance=GetVulkanContext().GetInstance();binding.physicalDevice=GetVulkanContext().GetPhysicalDevice();binding.device=GetVulkanContext().GetDevice();binding.queueFamilyIndex=GetVulkanContext().GetQueueFamilyIndex();
 XrSessionCreateInfo sci={XR_TYPE_SESSION_CREATE_INFO};sci.next=&binding;sci.systemId=system;if(XR_FAILED(createSession(instance,&sci,&session))){ShutdownRuntime();return false;}if(requestDisplayRefreshRate)requestDisplayRefreshRate(session,GetSharedVrState().refreshRate);
 XrSessionActionSetsAttachInfo attach={XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&inputActionSet;
 if(XR_FAILED(attachActionSets(session,&attach))){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: controller action attach failed");ShutdownRuntime();return false;}
 XrActionSpaceCreateInfo actionSpace={XR_TYPE_ACTION_SPACE_CREATE_INFO};actionSpace.poseInActionSpace.orientation.w=1.0f;
 actionSpace.action=handPoseAction;actionSpace.subactionPath=handPaths[0];
 if(XR_FAILED(createActionSpace(session,&actionSpace,&handSpaces[0]))){ShutdownRuntime();return false;}
 actionSpace.subactionPath=handPaths[1];
 if(XR_FAILED(createActionSpace(session,&actionSpace,&handSpaces[1]))){ShutdownRuntime();return false;}
 XrReferenceSpaceCreateInfo ri={XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
 ri.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_STAGE;ri.poseInReferenceSpace.orientation.w=1.0f;
 if(XR_SUCCEEDED(createSpace(session,&ri,&space)))usingStageSpace=true;
 else {ri.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;if(XR_FAILED(createSpace(session,&ri,&space))){ShutdownRuntime();return false;}usingStageSpace=false;}
 uint32_t viewCount=0;enumerateViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&viewCount,NULL);
 std::vector<XrViewConfigurationView> views(viewCount,{XR_TYPE_VIEW_CONFIGURATION_VIEW});
 if(viewCount!=2||XR_FAILED(enumerateViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,viewCount,&viewCount,views.data()))){ShutdownRuntime();return false;}
 uint32_t formatCount=0;enumerateFormats(session,0,&formatCount,NULL);std::vector<int64_t> formats(formatCount);enumerateFormats(session,formatCount,&formatCount,formats.data());
 // Match Quest's colour contract: request an sRGB swapchain, then render
 // display-ready legacy colours through its compatible UNORM view.
 int64_t format=ChooseSharedVulkanSwapchainFormat(formats.data(),formatCount);
 swapchainFormat=static_cast<VkFormat>(format);
 recommendedEyeWidth=views[0].recommendedImageRectWidth;recommendedEyeHeight=views[0].recommendedImageRectHeight;
 eyeWidth=std::max(1,static_cast<int32_t>(recommendedEyeWidth*GetSharedVrState().renderScale));eyeHeight=std::max(1,static_cast<int32_t>(recommendedEyeHeight*GetSharedVrState().renderScale));GetSharedVrState().appliedRenderScale=GetSharedVrState().renderScale;GetSharedVrState().renderScalePending=false;
 XrSwapchainCreateInfo swapInfo={XR_TYPE_SWAPCHAIN_CREATE_INFO};swapInfo.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;swapInfo.format=format;swapInfo.sampleCount=1;swapInfo.width=eyeWidth;swapInfo.height=eyeHeight;swapInfo.faceCount=1;swapInfo.arraySize=2;swapInfo.mipCount=1;
 if(XR_FAILED(createSwapchain(session,&swapInfo,&swapchain))){ShutdownRuntime();return false;}
 uint32_t imageCount=0;enumerateImages(swapchain,0,&imageCount,NULL);images.resize(imageCount,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
 if(XR_FAILED(enumerateImages(swapchain,imageCount,&imageCount,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())))){ShutdownRuntime();return false;}
 SDL_Log("PCVR: OpenXR Vulkan session ready (%dx%d, format=%d, images=%u)",eyeWidth,eyeHeight,static_cast<int>(swapchainFormat),imageCount);return true;}
void ShutdownRuntime(){GetSharedVrMenu().Reset();ResetVirtualController();running=false;ShutdownSharedHud();if(swapchain!=XR_NULL_HANDLE&&destroySwapchain)destroySwapchain(swapchain);swapchain=XR_NULL_HANDLE;images.clear();for(unsigned hand=0;hand<2;++hand){if(handSpaces[hand]!=XR_NULL_HANDLE&&destroySpace)destroySpace(handSpaces[hand]);handSpaces[hand]=XR_NULL_HANDLE;handPoseValid[hand]=false;}if(space!=XR_NULL_HANDLE&&destroySpace)destroySpace(space);space=XR_NULL_HANDLE;if(session!=XR_NULL_HANDLE&&destroySession)destroySession(session);session=XR_NULL_HANDLE;if(inputActionSet!=XR_NULL_HANDLE&&destroyActionSet)destroyActionSet(inputActionSet);inputActionSet=XR_NULL_HANDLE;GetVulkanContext().Shutdown();if(instance!=XR_NULL_HANDLE&&destroyInstance)destroyInstance(instance);instance=XR_NULL_HANDLE;if(loader)Platform::CloseLoader(loader);loader=NULL;getProc=NULL;system=XR_NULL_SYSTEM_ID;}
bool IsRuntimeReady(){return session!=XR_NULL_HANDLE;}
bool BeginFrame(){
 SharedHudBeginFrame();
 cullingBaseValid=false;
 if(!IsRuntimeReady())return false;XrEventDataBuffer event={XR_TYPE_EVENT_DATA_BUFFER};
 while(pollEvent(instance,&event)==XR_SUCCESS){
  const SharedSessionApi api={beginSession,endSession,[](void*){ResetVirtualController();},NULL};
  if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){const XrEventDataSessionStateChanged* changed=reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);SDL_Log("PCVR: OpenXR session state %d",static_cast<int>(changed->state));}
  else if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)SDL_Log("PCVR: system recenter pending");
  const XrResult result=HandleSharedRuntimeEvent(event,api,session,&running,
      &systemRecenter,&sessionState);
  if(XR_FAILED(result))SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: runtime event failed (%d)",static_cast<int>(result));
  event={XR_TYPE_EVENT_DATA_BUFFER};}
 if(!running){ResetVirtualController();return false;}SyncInputActions();
 if(!ApplyPendingSharedRenderScale(RecreateDesktopSwapchain,NULL)){
  SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: failed to recreate scaled swapchain");return false;}
 if(!BeginSharedFrame(FrameApi(),session,space,&sharedFrame)){
  SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: shared OpenXR frame begin failed");
  ResetVirtualController();return false;}
 currentFrame=sharedFrame.frame;currentViews[0]=sharedFrame.views[0];
 currentViews[1]=sharedFrame.views[1];currentViewFlags=sharedFrame.viewState.viewStateFlags;
 frameActive=sharedFrame.begun;
 if(!sharedFrame.shouldRender){currentFrame.shouldRender=XR_FALSE;EndFrame();return false;}
  if(!SharedRender::HasValidViewTracking(currentViewFlags))
 {
  // Do not reuse stale head poses while the runtime is reacquiring tracking.
  // Submit an empty frame and resume normally once both pose components are valid.
  currentFrame.shouldRender=XR_FALSE;sharedFrame.shouldRender=false;EndFrame();return false;
  }
  if(UpdateSharedSystemRecenter(&systemRecenter,currentFrame.predictedDisplayTime,
      currentViews,currentViewFlags,GetSharedVrState().seatedMode,&origin,&originValid))
  { GetSharedMoviePanel().InvalidateAnchor();GetSharedVrMenu().InvalidateAnchor(); }
  LocateSharedHandPoses(locateSpace,handSpaces,space,currentFrame.predictedDisplayTime,
      originValid,handPoses,handPoseValid);
 if(!originValid && (currentViewFlags&XR_VIEW_STATE_ORIENTATION_VALID_BIT)){
  origin=SharedRender::CentreYawAnchor(currentViews[0].pose,currentViews[1].pose);
  originValid=true;GetSharedVrMenu().InvalidateAnchor();
  SDL_Log("PCVR: tracking origin captured at %.3f %.3f %.3f",origin.position.x,origin.position.y,origin.position.z);}
  const auto haptic=[](void*,unsigned,float amplitude,unsigned durationMs)
  { ApplyControllerHaptics(amplitude,durationMs); };
  UpdateTrackedVrVehicle(originValid,origin,handPoses,handPoseValid,haptic,NULL);
 if(!AcquireSharedFrameImage(FrameApi(),swapchain,&sharedFrame)){
  SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: shared swapchain acquire/wait failed");
  EndFrame();return false;}
 currentImage=sharedFrame.imageIndex;imageAcquired=sharedFrame.imageAcquired;
 ++frameSerial;
 if((frameSerial%120u)==1u)
     SDL_Log("PCVR frame: serial=%llu image=%u predicted=%lld render=%d",
         static_cast<unsigned long long>(frameSerial),currentImage,
         static_cast<long long>(currentFrame.predictedDisplayTime),
         currentFrame.shouldRender?1:0);
 return true;}
bool BeginEye(unsigned eye){if(!frameActive||!imageAcquired||eye>1)return false;currentEye=eye;if(!GetVulkanContext().BeginPddiEye())return false;
 // An OpenXR swapchain release transfers the image back to the runtime. Its
 // previous contents and Vulkan layout are not application-owned state on a
 // later acquire. Discard each layer from UNDEFINED every frame instead of
 // assuming COLOR_ATTACHMENT_OPTIMAL survives the compositor handoff.
 eyeActive=GetVulkanContext().ClearImageInPddiEye(images[currentImage].image,true,eye,
                                                  eyeWidth,eyeHeight);if(!eyeActive)GetVulkanContext().EndPddiEye();return eyeActive;}
static void PresentSharedHud(void*,unsigned eye)
{
 currentEye=eye;
 if(cullingBaseValid)DrawSharedGameplayHud();
}
void EndEye(unsigned){if(eyeActive){if(cullingBaseValid)DrawSharedGameplayHud();GetVulkanContext().EndPddiEye();}eyeActive=false;}
void EndFrame(){if(!frameActive)return;if(eyeActive)EndEye(currentEye);if(imageAcquired){
 // xrReleaseSwapchainImage transfers ownership back to the compositor.  Both
 // per-eye command buffers must have completed before that handoff; merely
 // submitting them to the Vulkan queue is not sufficient synchronization.
 const VkResult queueResult=vkQueueWaitIdle(GetVulkanContext().GetQueue());
 if(queueResult!=VK_SUCCESS)
     SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                  "PCVR: vkQueueWaitIdle before swapchain release failed (%d)",
                  static_cast<int>(queueResult));
 if(!ReleaseSharedFrameImage(FrameApi(),swapchain,&sharedFrame))
     SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: shared swapchain release failed");
 imageAcquired=sharedFrame.imageAcquired;}
 sharedFrame.frame=currentFrame;sharedFrame.views[0]=currentViews[0];
 sharedFrame.views[1]=currentViews[1];
 const int widths[2]={eyeWidth,eyeWidth},heights[2]={eyeHeight,eyeHeight};
 const XrResult endResult=EndSharedFrame(FrameApi(),session,space,swapchain,
     widths,heights,&sharedFrame,UpdateSharedHudIrisAlpha(),colorScaleBiasEnabled);
 if(XR_FAILED(endResult))SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: shared xrEndFrame failed (%d)",static_cast<int>(endResult));else if((frameSerial%120u)==1u)SDL_Log("PCVR submit: serial=%llu image=%u end=XR_SUCCESS",static_cast<unsigned long long>(frameSerial),currentImage);frameActive=false;}
} }

namespace SharOpenXR
{
// The game/render loop talks only to the platform-neutral OpenXR surface.
// Keep the desktop namespace as the backend implementation detail, exactly as
// the Android backend is hidden behind these same entry points.
bool BeginFrame() { return Desktop::BeginFrame(); }
bool BeginEye(unsigned eye) { BeginBodyIKEye(); return Desktop::BeginEye(eye); }
void EndEye(unsigned eye) { Desktop::EndEye(eye); }
void EndFrame() { Desktop::EndFrame(); }
// Compatibility surface used by the shared Vulkan PDDI while the desktop
// compositor is being connected to the full gameplay OpenXR manager.
bool GetActiveVulkanEyeTarget(VulkanEyeTarget* target) { if(!target)return false;if(GetSharedHudCaptureTarget(target))return true;if(!Desktop::imageAcquired||!Desktop::eyeActive)return false;return BuildSharedVulkanEyeTarget(Desktop::images[Desktop::currentImage].image,Desktop::swapchainFormat,Desktop::eyeWidth,Desktop::eyeHeight,Desktop::currentEye,Desktop::multiviewRendering,Desktop::multiviewTargetActive,target); }
bool GetActiveProjection(rmt::Matrix* projection,int* width,int* height) { if(!Desktop::eyeActive||GetSharedMoviePanel().IsRendering())return false;const int widths[2]={Desktop::eyeWidth,Desktop::eyeWidth},heights[2]={Desktop::eyeHeight,Desktop::eyeHeight};return GetSharedEyeProjection(Desktop::currentViews,widths,heights,Desktop::currentEye,Desktop::worldRendering,Desktop::embeddedHudRendering,0.1f,8000.0f,projection,width,height); }
bool GetActiveViewport(int* width,int* height) { const int widths[2]={Desktop::eyeWidth,Desktop::eyeWidth},heights[2]={Desktop::eyeHeight,Desktop::eyeHeight};return Desktop::eyeActive&&GetSharedEyeViewport(widths,heights,Desktop::currentEye,width,height); }
unsigned GetEyeCount() { return Desktop::frameActive&&Desktop::currentFrame.shouldRender?2u:0u; }
bool GetActiveUiHorizontalOffset(float* offset) { return Desktop::eyeActive&&GetSharedUiHorizontalOffset(Desktop::currentViews,Desktop::currentEye,Desktop::worldRendering,4.0f,offset); }
void SetWorldRendering(bool enabled) { Desktop::worldRendering=enabled; }
void SetMovieRendering(bool enabled) { GetSharedMoviePanel().SetRendering(enabled,Desktop::eyeActive,Desktop::currentViews); }
void BeginMoviePlane() { GetSharedMoviePanel().Begin();GetVulkanContext().HideStartupSplash(); }
void EndMoviePlane() { GetSharedMoviePanel().End(); }
bool GetActiveMovieProjection(rmt::Matrix* out,int* width,int* height) { if(!Desktop::eyeActive)return false;return GetSharedMoviePanel().GetProjection(true,Desktop::currentViews[Desktop::currentEye],Desktop::eyeWidth,Desktop::eyeHeight,out,width,height); }
bool GetActiveFrontendProjection(rmt::Matrix* out,int* width,int* height) { if(!Desktop::eyeActive)return false;return GetSharedVrMenu().GetProjection(true,Desktop::currentViews[Desktop::currentEye],Desktop::eyeWidth,Desktop::eyeHeight,out,width,height); }
bool GetLatestCullingCamera(rmt::Matrix* out) { if(!out||!Desktop::originValid||!Desktop::cullingBaseValid)return false;SharedRender::ComposeTrackedCentreCamera(Desktop::origin,Desktop::currentViews[0].pose,Desktop::currentViews[1].pose,Desktop::cullingBaseCamera,out);return true; }
bool GetActiveCullingCamera(rmt::Matrix* out) { return Desktop::eyeActive&&GetLatestCullingCamera(out); }
bool GetGameplayCamera(rmt::Matrix* out) { if(!out||!Desktop::cullingBaseValid)return false;*out=Desktop::cullingBaseCamera;return true; }
bool GetMultiviewMatrices(rmt::Matrix* p,rmt::Matrix* a) { if(!p||!a||
    !Desktop::multiviewRendering||!Desktop::multiviewTargetActive||
    !Desktop::worldRendering)return false;p[0]=Desktop::multiviewProjection[0];
    p[1]=Desktop::multiviewProjection[1];a[0]=Desktop::multiviewAdjustment[0];
    a[1]=Desktop::multiviewAdjustment[1];return true; }
void SetMultiviewTargetActive(bool active) { Desktop::multiviewTargetActive=
    Desktop::multiviewRendering&&active; }
bool IsMultiviewAvailable()
{
 // The Vulkan context queries VkPhysicalDeviceMultiviewFeatures and only
 // advertises this after enabling it on the OpenXR-created logical device.
 // Desktop must use the same capability-driven path as Quest; a dual-pass
 // fallback is for GPUs that genuinely do not expose the feature.
 return GetVulkanContext().IsMultiviewSupported();
}
bool IsMultiviewRendering() { return Desktop::multiviewRendering&&Desktop::multiviewTargetActive; }
bool BeginMultiview()
{
 BeginBodyIKEye();
 if(!Desktop::frameActive||!Desktop::imageAcquired||!IsMultiviewAvailable())return false;
 Desktop::currentEye=0;
 if(!BeginSharedVulkanMultiview(GetVulkanContext(),
     Desktop::images[Desktop::currentImage].image,true,
     Desktop::eyeWidth,Desktop::eyeHeight,&Desktop::renderSequence))return false;
 Desktop::eyeActive=true;Desktop::multiviewRendering=true;
 Desktop::multiviewTargetActive=true;Desktop::cullingBaseValid=false;return true;
}
bool PrepareMultiviewCamera(tCamera* base)
{
 if(!base||!Desktop::multiviewRendering||!Desktop::originValid)return false;
 Desktop::cullingBaseCamera=base->GetCameraToWorldMatrix();Desktop::cullingBaseValid=true;
 rmt::Matrix centre;return BuildSharedMultiviewCameras(Desktop::origin,
     Desktop::currentViews,Desktop::cullingBaseCamera,Desktop::multiviewProjection,
     Desktop::multiviewAdjustment,&centre);
}
bool BeginMultiviewGuiEye(unsigned eye)
{
 if(!Desktop::multiviewRendering||eye>1)return false;
 if(!BeginSharedVulkanGuiEye(GetVulkanContext(),&Desktop::renderSequence,eye,
     Desktop::PresentSharedHud,NULL))return false;
 Desktop::currentEye=eye;Desktop::multiviewTargetActive=false;
 Desktop::worldRendering=false;return true;
}
void EndMultiview()
{
 if(!Desktop::multiviewRendering)return;
 const bool submitted=EndSharedVulkanMultiview(GetVulkanContext(),
     &Desktop::renderSequence,Desktop::PresentSharedHud,NULL);
 if(!submitted)SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
     "PCVR: shared multiview submit failed");
 Desktop::eyeActive=false;
 Desktop::multiviewRendering=false;Desktop::multiviewTargetActive=false;
 Desktop::worldRendering=false;
}
bool GetEyeCamera(unsigned eye,tCamera* base,rmt::Matrix* out) { if(eye>1||!base||!out||!Desktop::originValid)return false;if(Desktop::worldRendering&&!Desktop::cullingBaseValid){Desktop::cullingBaseCamera=base->GetCameraToWorldMatrix();Desktop::cullingBaseValid=true;}SharedRender::ComposeTrackedCamera(Desktop::origin,Desktop::currentViews[eye].pose,base->GetCameraToWorldMatrix(),out);return true; }
bool GetActiveEyeCamera(tCamera* base,rmt::Matrix* out) { return Desktop::eyeActive&&GetEyeCamera(Desktop::currentEye,base,out); }
void SetEmbeddedHudRendering(bool enabled) { Desktop::embeddedHudRendering=enabled; }
bool IsEmbeddedHudRendering() { return Desktop::embeddedHudRendering; }
bool IsMovieRendering() { return GetSharedMoviePanel().IsRendering(); }
void SetFrontendPlaneActive(bool active) { GetSharedVrMenu().SetActive(active); }
void SetFrontendPlaneRendering(bool rendering) { GetSharedVrMenu().SetRendering(rendering,Desktop::eyeActive,Desktop::currentViews); }
bool IsFrontendPlaneRendering() { return GetSharedVrMenu().IsRendering(); }
void SetEnhancedUiConvergence(bool enabled) { GetSharedVrState().enhancedUiConvergence=enabled; }
bool IsRightEyeRendering() { return Desktop::eyeActive&&Desktop::currentEye==1; }
// The first shared Vulkan stage composites the complete authored HUD as one
// stereoscopic plane. Per-widget wrist/world placement is enabled only after
// the corresponding shared capture slots have been migrated as well.
bool IsSpatialHudEnabled() { return IsSharedSpatialHudEnabled(); }
bool HasEnhancedUiConvergence() { return GetSharedVrState().enhancedUiConvergence; }
bool AreCustomMaterialsEnabled();
int GetEnhancedMaterialModel();
int GetReflectionMode();
namespace { rmt::Vector& desktopVrBaseHeading=GetSharedVrState().vrBaseHeading; }
int GetPbrDebugMode() { return GetSharedVrState().pbrDebugMode; }
void SetPbrDebugMode(int mode) { SetSharedPbrDebugMode(mode); }
void SetGiIndirectOnly(bool enabled) { SetSharedGiIndirectOnly(enabled); }
bool IsGiIndirectOnly() { return GetSharedVrState().giIndirectOnly; }
void SetVolumetricLightEnabled(bool enabled) { SetSharedVolumetricLightEnabled(enabled); }
bool IsVolumetricLightEnabled() { return GetSharedVrState().volumetricLightEnabled; }
void SetHdrEnabled(bool enabled) { SetSharedHdrEnabled(enabled); }
bool IsHdrEnabled() { return GetSharedVrState().hdrEnabled; }
void SetVrModeEnabled(bool enabled) { SetSharedVrModeEnabled(enabled); }
bool IsVrModeEnabled() { return GetSharedVrState().vrModeEnabled; }
void SetDeveloperMenusEnabled(bool enabled) { SetSharedDeveloperMenusEnabled(enabled); }
bool IsDeveloperMenusEnabled() { return GetSharedVrState().developerMenusEnabled; }
void SetCsmEnabled(bool enabled) { SetSharedCsmEnabled(enabled); }
bool IsCsmEnabled() { return GetSharedVrState().csmEnabled; }
void SetEnhancedMaterialsEnabled(bool enabled) { SetSharedEnhancedMaterialsEnabled(enabled); }
bool IsEnhancedMaterialsEnabled() { return GetSharedVrState().enhancedMaterialsEnabled; }
void SetCustomMaterialsEnabled(bool enabled) { SetSharedCustomMaterialsEnabled(enabled); }
void SetEnhancedMaterialModel(int model) { SetSharedEnhancedMaterialModel(model); }
int GetEnhancedMaterialModel() { const SharedVrState& s=GetSharedVrState();return s.enhancedMaterialsEnabled?std::max(1,s.enhancedMaterialModel):0; }
bool AreCustomMaterialsEnabled() { return GetSharedVrState().customMaterialsEnabled; }
void SetGtaoEnabled(bool enabled) { SetSharedGtaoEnabled(enabled); }
bool IsGtaoEnabled() { const SharedVrState& s=GetSharedVrState();return s.gtaoEnabled&&!s.vrModeEnabled; }
void SetVehicleLightMode(int mode) { SetSharedVehicleLightMode(mode); }
int GetVehicleLightMode() { return GetSharedVrState().vehicleLightMode; }
void SetReflectionMode(int mode) { SetSharedReflectionMode(mode); }
int GetReflectionMode() { return GetSharedVrState().reflectionMode; }
void SetVrSteeringWheelEnabled(bool enabled) { SetVehicleControlMode(enabled?1:0); }
bool IsVrSteeringWheelEnabled() { return GetSharedVrState().vehicleControlMode==1; }
void SetRenderScale(float scale) { const auto queue=[](void*,float){return true;};SetSharedRenderScale(scale,queue,NULL); }
float GetRenderScale() { return GetSharedVrState().renderScale; }
void SetRefreshRate(float hz) { const auto apply=[](void*,float value){return !Desktop::session||!Desktop::requestDisplayRefreshRate||XR_SUCCEEDED(Desktop::requestDisplayRefreshRate(Desktop::session,value));};SetSharedRefreshRate(hz,apply,NULL); }
float GetRefreshRate() { return GetSharedVrState().refreshRate; }
void ApplyGtao() {}
void SetSeatedMode(bool enabled) { SetSharedSeatedMode(enabled); }
bool IsSeatedMode() { return GetSharedVrState().seatedMode; }
void SetSnapTurnEnabled(bool enabled) { SetSharedSnapTurnEnabled(enabled); }
bool IsSnapTurnEnabled() { return GetSharedVrState().snapTurnEnabled; }
void SetSmoothTurnSpeed(float value) { SetSharedSmoothTurnSpeed(value); }
float GetSmoothTurnSpeed() { return GetSharedVrState().smoothTurnSpeed; }
void SetSnapTurnAngle(float value) { SetSharedSnapTurnAngle(value); }
float GetSnapTurnAngle() { return GetSharedVrState().snapTurnAngle; }
void SetVehicleControlMode(int mode) { SetSharedVehicleControlMode(mode); }
int GetVehicleControlMode() { return GetSharedVrState().vehicleControlMode; }
bool IsThirdPersonVehicleMode() { return GetSharedVrState().vehicleControlMode==2; }
bool GetVrSteeringWheelValue(float* value) { Character* player=GetCharacterManager()?GetCharacterManager()->GetCharacter(0):NULL;Vehicle* vehicle=(player&&player->IsInCar())?player->GetTargetVehicle():NULL;return GetVrVehicleSteering(value,IsVrYokeVehicle(vehicle?vehicle->GetName():NULL)); }
void ApplyControllerHaptics(float amplitude,unsigned durationMs)
{
 if(!Desktop::running||Desktop::hapticAction==XR_NULL_HANDLE||
    !Desktop::applyHapticFeedback||amplitude<=0.0f||durationMs==0)return;
 XrHapticVibration vibration={XR_TYPE_HAPTIC_VIBRATION};
 vibration.amplitude=std::max(0.0f,std::min(1.0f,amplitude));
 vibration.duration=static_cast<XrDuration>(durationMs)*1000000;
 vibration.frequency=XR_FREQUENCY_UNSPECIFIED;
 for(unsigned hand=0;hand<2;++hand)
 {
  XrHapticActionInfo info={XR_TYPE_HAPTIC_ACTION_INFO};
  info.action=Desktop::hapticAction;
  info.subactionPath=Desktop::handPaths[hand];
  Desktop::applyHapticFeedback(Desktop::session,&info,
      reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
 }
}
void SetVehicleComfortEnabled(bool enabled) { SetSharedVehicleComfortEnabled(enabled); }
bool IsVehicleComfortEnabled() { return GetSharedVrState().vehicleComfortEnabled; }
bool IsHorizontalMenuInputDominant() { return Desktop::menuHorizontalInputDominant; }
bool IsVerticalMenuInputDominant() { return Desktop::menuVerticalInputDominant; }
void SetVrBaseHeading(const rmt::Vector& heading) { SharedVrState& s=GetSharedVrState();s.vrBaseHeading=heading;s.vrBaseHeading.y=0.0f;s.vrBaseHeadingValid=s.vrBaseHeading.NormalizeSafe()>0.0001f; }
bool ConsumeRoomscaleMovement(rmt::Vector* delta) { Character* p=GetCharacterManager()?GetCharacterManager()->GetCharacter(0):NULL;return ConsumeSharedRoomscale(Desktop::currentViews,Desktop::currentViewFlags,&Desktop::origin,Desktop::originValid,p&&p->IsInCar(),delta); }
bool GetPhysicalHeadHeight(float* height) { Character* p=GetCharacterManager()?GetCharacterManager()->GetCharacter(0):NULL;const bool child=p&&(p->GetUID()==tEntity::MakeUID("bart")||p->GetUID()==tEntity::MakeUID("lisa"));return GetSharedPhysicalHeadHeight(Desktop::origin,Desktop::originValid,Desktop::usingStageSpace,GetSharedVrState().seatedMode,child,height); }
bool RecenterVrPose() { if(!RecenterSharedTracking(Desktop::currentViews,Desktop::currentViewFlags,&Desktop::origin))return false;Desktop::originValid=true;GetSharedVrMenu().InvalidateAnchor();return true; }
bool GetHeadForward(rmt::Vector* forward) { return GetSharedHeadForward(Desktop::currentViews,Desktop::origin,Desktop::originValid,forward); }
bool GetControllerLocalPose(unsigned hand,rmt::Matrix* out) { return SharedRender::ComposeControllerLocalPose(Desktop::origin,Desktop::originValid,Desktop::handPoses,Desktop::handPoseValid,hand,out); }
bool GetControllerWorldPose(unsigned hand,tCamera* base,rmt::Matrix* out) { if(!base)return false;return SharedRender::ComposeControllerWorldPose(Desktop::origin,Desktop::originValid,Desktop::handPoses,Desktop::handPoseValid,hand,base->GetCameraToWorldMatrix(),out); }
void RenderControllerHands(tCamera*)
{
 if(!GetSharedVrState().vrModeEnabled||!Desktop::cullingBaseValid)return;
 Character* player=GetCharacterManager()?GetCharacterManager()->GetCharacter(0):NULL;
 if(!player||!player->GetController()||!player->GetController()->IsActive()||
    (player->IsInCar()&&IsThirdPersonVehicleMode()))return;
 SuperCamCentral* central=GetSuperCamManager()?GetSuperCamManager()->GetSCC(0):NULL;
 SuperCam* camera=central?central->GetActiveSuperCam():NULL;
 if(camera&&(camera->GetType()==SuperCam::ANIMATED_CAM||
    camera->GetType()==SuperCam::RELATIVE_ANIMATED_CAM||
    camera->GetType()==SuperCam::CONVERSATION_CAM))return;
 Vehicle* vehicle=player->IsInCar()?player->GetTargetVehicle():NULL;
 const bool wheel=vehicle&&GetSharedVrState().vehicleControlMode==1;
 const bool yoke=IsVrYokeVehicle(vehicle?vehicle->GetName():NULL);
 rmt::Matrix localHands[2];bool localValid[2]={false,false};
 for(unsigned hand=0;hand<2;++hand)localValid[hand]=GetControllerLocalPose(hand,&localHands[hand]);
 if(wheel){RenderVrVehicleControls(Desktop::cullingBaseCamera,yoke,localHands,localValid);return;}
 rmt::Matrix poses[2];bool valid[2]={false,false};
 for(unsigned hand=0;hand<2;++hand)
 {
  if(localValid[hand]){poses[hand].Mult(localHands[hand],Desktop::cullingBaseCamera);valid[hand]=true;}
 }
 RenderTrackedHandMeshes(poses,valid);
}
void RecordPddiDraw(unsigned,unsigned,bool,double) {}
void RecordPddiMaterial(bool,double) {}
void RecordPddiUpload(unsigned,double) {}
void RecordRenderSection(unsigned,double) {}
}
#endif

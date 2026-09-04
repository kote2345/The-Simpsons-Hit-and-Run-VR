#if defined(SRR2_OPENXR_PLATFORM_WIN32) && defined(SRR2_VR_RENDERER_VULKAN)
#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <vr/openxr_desktop_runtime.h>
#include <vr/openxrmanager.h>
#include <vr/openxr_platform_loader.h>
#include <vr/openxr_platform_instance.h>
#include <vr/vulkan/openxr_vulkan_context.h>
#include <p3d/camera.hpp>
#include <SDL.h>
#include <cmath>
#include <cstring>
#include <vector>
namespace SharOpenXR { namespace Desktop { namespace {
void* loader=NULL; PFN_xrGetInstanceProcAddr getProc=NULL;
XrInstance instance=XR_NULL_HANDLE; XrSystemId system=XR_NULL_SYSTEM_ID;
XrSession session=XR_NULL_HANDLE; XrSpace space=XR_NULL_HANDLE;
XrSwapchain swapchain=XR_NULL_HANDLE; int32_t eyeWidth=0,eyeHeight=0;
VkFormat swapchainFormat=VK_FORMAT_R8G8B8A8_UNORM;
std::vector<XrSwapchainImageVulkanKHR> images;
XrSessionState sessionState=XR_SESSION_STATE_UNKNOWN; bool running=false;
XrFrameState currentFrame={XR_TYPE_FRAME_STATE};XrView currentViews[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
uint32_t currentImage=0,currentEye=0;bool frameActive=false,imageAcquired=false,eyeActive=false;
bool worldRendering=false,movieRendering=false;
uint64_t frameSerial=0;
PFN_xrDestroySpace destroySpace=NULL; PFN_xrDestroySession destroySession=NULL; PFN_xrDestroyInstance destroyInstance=NULL;
PFN_xrPollEvent pollEvent=NULL;PFN_xrBeginSession beginSession=NULL;PFN_xrEndSession endSession=NULL;
PFN_xrWaitFrame waitFrame=NULL;PFN_xrBeginFrame beginFrame=NULL;PFN_xrEndFrame endFrame=NULL;
PFN_xrLocateViews locateViews=NULL;PFN_xrCreateSwapchain createSwapchain=NULL;PFN_xrDestroySwapchain destroySwapchain=NULL;
PFN_xrEnumerateViewConfigurationViews enumerateViews=NULL;PFN_xrEnumerateSwapchainFormats enumerateFormats=NULL;PFN_xrEnumerateSwapchainImages enumerateImages=NULL;
PFN_xrAcquireSwapchainImage acquireImage=NULL;PFN_xrWaitSwapchainImage waitImage=NULL;PFN_xrReleaseSwapchainImage releaseImage=NULL;
bool Load(XrInstance i,const char* n,PFN_xrVoidFunction* f){return XR_SUCCEEDED(getProc(i,n,f))&&*f;}
}
void ShutdownRuntime();
bool InitializeRuntime(){
 if(instance!=XR_NULL_HANDLE)return true; loader=Platform::OpenLoader();
 if(!loader){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: %s",Platform::GetLoaderError());return false;}
 getProc=reinterpret_cast<PFN_xrGetInstanceProcAddr>(Platform::GetLoaderSymbol(loader,"xrGetInstanceProcAddr"));
 if(!getProc||!Platform::InitializeOpenXRLoader(getProc)){ShutdownRuntime();return false;}
 PFN_xrEnumerateInstanceExtensionProperties enumerate=NULL; PFN_xrCreateInstance create=NULL;
 if(!Load(XR_NULL_HANDLE,"xrEnumerateInstanceExtensionProperties",reinterpret_cast<PFN_xrVoidFunction*>(&enumerate))||!Load(XR_NULL_HANDLE,"xrCreateInstance",reinterpret_cast<PFN_xrVoidFunction*>(&create))){ShutdownRuntime();return false;}
 uint32_t count=0;if(XR_FAILED(enumerate(NULL,0,&count,NULL))){ShutdownRuntime();return false;}
 std::vector<XrExtensionProperties> props(count,{XR_TYPE_EXTENSION_PROPERTIES});if(XR_FAILED(enumerate(NULL,count,&count,props.data()))){ShutdownRuntime();return false;}
 bool vulkan=false;for(size_t i=0;i<props.size();++i)vulkan|=std::strcmp(props[i].extensionName,XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME)==0;
 if(!vulkan){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: XR_KHR_vulkan_enable2 unavailable");ShutdownRuntime();return false;}
 std::vector<const char*> extensions;Platform::AppendRequiredInstanceExtensions(extensions);extensions.push_back(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
 XrInstanceCreateInfo ci={XR_TYPE_INSTANCE_CREATE_INFO};if(!Platform::PrepareInstanceCreateInfo(&ci)){ShutdownRuntime();return false;}
 std::strncpy(ci.applicationInfo.applicationName,"The Simpsons Hit & Run PCVR",XR_MAX_APPLICATION_NAME_SIZE-1);std::strncpy(ci.applicationInfo.engineName,"Pure3D",XR_MAX_ENGINE_NAME_SIZE-1);
 // SteamVR still exposes an OpenXR 1.0 runtime on a number of supported
 // installations.  Request the 1.0 baseline instead of the header version
 // (currently 1.1), otherwise xrCreateInstance may reject the application.
 ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);ci.enabledExtensionCount=static_cast<uint32_t>(extensions.size());ci.enabledExtensionNames=extensions.data();
 const XrResult createResult=create(&ci,&instance);
 if(XR_FAILED(createResult)){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: xrCreateInstance failed (%d)",static_cast<int>(createResult));ShutdownRuntime();return false;}
 PFN_xrGetSystem getSystem=NULL;PFN_xrCreateSession createSession=NULL;PFN_xrCreateReferenceSpace createSpace=NULL;
 if(!Load(instance,"xrDestroyInstance",reinterpret_cast<PFN_xrVoidFunction*>(&destroyInstance))||!Load(instance,"xrGetSystem",reinterpret_cast<PFN_xrVoidFunction*>(&getSystem))||!Load(instance,"xrCreateSession",reinterpret_cast<PFN_xrVoidFunction*>(&createSession))||!Load(instance,"xrDestroySession",reinterpret_cast<PFN_xrVoidFunction*>(&destroySession))||!Load(instance,"xrCreateReferenceSpace",reinterpret_cast<PFN_xrVoidFunction*>(&createSpace))||!Load(instance,"xrDestroySpace",reinterpret_cast<PFN_xrVoidFunction*>(&destroySpace))||!Load(instance,"xrPollEvent",reinterpret_cast<PFN_xrVoidFunction*>(&pollEvent))||!Load(instance,"xrBeginSession",reinterpret_cast<PFN_xrVoidFunction*>(&beginSession))||!Load(instance,"xrEndSession",reinterpret_cast<PFN_xrVoidFunction*>(&endSession))||!Load(instance,"xrWaitFrame",reinterpret_cast<PFN_xrVoidFunction*>(&waitFrame))||!Load(instance,"xrBeginFrame",reinterpret_cast<PFN_xrVoidFunction*>(&beginFrame))||!Load(instance,"xrEndFrame",reinterpret_cast<PFN_xrVoidFunction*>(&endFrame))||!Load(instance,"xrLocateViews",reinterpret_cast<PFN_xrVoidFunction*>(&locateViews))||!Load(instance,"xrEnumerateViewConfigurationViews",reinterpret_cast<PFN_xrVoidFunction*>(&enumerateViews))||!Load(instance,"xrEnumerateSwapchainFormats",reinterpret_cast<PFN_xrVoidFunction*>(&enumerateFormats))||!Load(instance,"xrCreateSwapchain",reinterpret_cast<PFN_xrVoidFunction*>(&createSwapchain))||!Load(instance,"xrDestroySwapchain",reinterpret_cast<PFN_xrVoidFunction*>(&destroySwapchain))||!Load(instance,"xrEnumerateSwapchainImages",reinterpret_cast<PFN_xrVoidFunction*>(&enumerateImages))||!Load(instance,"xrAcquireSwapchainImage",reinterpret_cast<PFN_xrVoidFunction*>(&acquireImage))||!Load(instance,"xrWaitSwapchainImage",reinterpret_cast<PFN_xrVoidFunction*>(&waitImage))||!Load(instance,"xrReleaseSwapchainImage",reinterpret_cast<PFN_xrVoidFunction*>(&releaseImage))){ShutdownRuntime();return false;}
 XrSystemGetInfo si={XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
 if(XR_FAILED(getSystem(instance,&si,&system))||!GetVulkanContext().Initialize(instance,system,getProc)){ShutdownRuntime();return false;}
 XrGraphicsBindingVulkan2KHR binding={XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};binding.instance=GetVulkanContext().GetInstance();binding.physicalDevice=GetVulkanContext().GetPhysicalDevice();binding.device=GetVulkanContext().GetDevice();binding.queueFamilyIndex=GetVulkanContext().GetQueueFamilyIndex();
 XrSessionCreateInfo sci={XR_TYPE_SESSION_CREATE_INFO};sci.next=&binding;sci.systemId=system;if(XR_FAILED(createSession(instance,&sci,&session))){ShutdownRuntime();return false;}
 XrReferenceSpaceCreateInfo ri={XR_TYPE_REFERENCE_SPACE_CREATE_INFO};ri.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;ri.poseInReferenceSpace.orientation.w=1.0f;if(XR_FAILED(createSpace(session,&ri,&space))){ShutdownRuntime();return false;}
 uint32_t viewCount=0;enumerateViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&viewCount,NULL);
 std::vector<XrViewConfigurationView> views(viewCount,{XR_TYPE_VIEW_CONFIGURATION_VIEW});
 if(viewCount!=2||XR_FAILED(enumerateViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,viewCount,&viewCount,views.data()))){ShutdownRuntime();return false;}
 uint32_t formatCount=0;enumerateFormats(session,0,&formatCount,NULL);std::vector<int64_t> formats(formatCount);enumerateFormats(session,formatCount,&formatCount,formats.data());
 // Render directly through an image view whose format exactly matches the
 // OpenXR swapchain.  Reinterpreting an SRGB swapchain image as UNORM requires
 // XR_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT plus a compatible format list; the
 // desktop swapchain has neither and some runtimes consequently retain only
 // the first submitted image. Prefer a linear format advertised by runtime.
 int64_t format=formats.empty()?VK_FORMAT_R8G8B8A8_UNORM:formats[0];
 for(size_t i=0;i<formats.size();++i)
     if(formats[i]==VK_FORMAT_R8G8B8A8_UNORM){format=formats[i];break;}
 for(size_t i=0;i<formats.size() && format!=VK_FORMAT_R8G8B8A8_UNORM;++i)
     if(formats[i]==VK_FORMAT_B8G8R8A8_UNORM){format=formats[i];break;}
 swapchainFormat=static_cast<VkFormat>(format);
 eyeWidth=static_cast<int32_t>(views[0].recommendedImageRectWidth);eyeHeight=static_cast<int32_t>(views[0].recommendedImageRectHeight);
 XrSwapchainCreateInfo swapInfo={XR_TYPE_SWAPCHAIN_CREATE_INFO};swapInfo.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;swapInfo.format=format;swapInfo.sampleCount=1;swapInfo.width=eyeWidth;swapInfo.height=eyeHeight;swapInfo.faceCount=1;swapInfo.arraySize=2;swapInfo.mipCount=1;
 if(XR_FAILED(createSwapchain(session,&swapInfo,&swapchain))){ShutdownRuntime();return false;}
 uint32_t imageCount=0;enumerateImages(swapchain,0,&imageCount,NULL);images.resize(imageCount,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
 if(XR_FAILED(enumerateImages(swapchain,imageCount,&imageCount,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())))){ShutdownRuntime();return false;}
 char* basePath=SDL_GetBasePath();
 std::string splashPath=basePath?basePath:"";
 if(basePath) SDL_free(basePath);
 splashPath+="startup.png";
 if(!GetVulkanContext().LoadStartupSplash(splashPath.c_str(),swapchainFormat))
     SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                  "PCVR: failed to load startup splash %s",splashPath.c_str());
 else SDL_Log("PCVR: startup splash ready");
 SDL_Log("PCVR: OpenXR Vulkan session ready (%dx%d, format=%d, images=%u)",eyeWidth,eyeHeight,static_cast<int>(swapchainFormat),imageCount);return true;}
void ShutdownRuntime(){running=false;if(swapchain!=XR_NULL_HANDLE&&destroySwapchain)destroySwapchain(swapchain);swapchain=XR_NULL_HANDLE;images.clear();if(space!=XR_NULL_HANDLE&&destroySpace)destroySpace(space);space=XR_NULL_HANDLE;if(session!=XR_NULL_HANDLE&&destroySession)destroySession(session);session=XR_NULL_HANDLE;GetVulkanContext().Shutdown();if(instance!=XR_NULL_HANDLE&&destroyInstance)destroyInstance(instance);instance=XR_NULL_HANDLE;if(loader)Platform::CloseLoader(loader);loader=NULL;getProc=NULL;system=XR_NULL_SYSTEM_ID;}
bool IsRuntimeReady(){return session!=XR_NULL_HANDLE;}
bool BeginFrame(){
 if(!IsRuntimeReady())return false;XrEventDataBuffer event={XR_TYPE_EVENT_DATA_BUFFER};
 while(pollEvent(instance,&event)==XR_SUCCESS){if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){XrEventDataSessionStateChanged* changed=reinterpret_cast<XrEventDataSessionStateChanged*>(&event);sessionState=changed->state;SDL_Log("PCVR: OpenXR session state %d",static_cast<int>(sessionState));if(sessionState==XR_SESSION_STATE_READY){XrSessionBeginInfo begin={XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;const XrResult result=beginSession(session,&begin);running=XR_SUCCEEDED(result);if(XR_FAILED(result))SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: xrBeginSession failed (%d)",static_cast<int>(result));}else if(sessionState==XR_SESSION_STATE_STOPPING){const XrResult result=endSession(session);if(XR_FAILED(result))SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: xrEndSession failed (%d)",static_cast<int>(result));running=false;}else if(sessionState==XR_SESSION_STATE_EXITING||sessionState==XR_SESSION_STATE_LOSS_PENDING)running=false;}event={XR_TYPE_EVENT_DATA_BUFFER};}
 if(!running)return false;XrFrameWaitInfo wait={XR_TYPE_FRAME_WAIT_INFO};currentFrame={XR_TYPE_FRAME_STATE};XrResult result=waitFrame(session,&wait,&currentFrame);if(XR_FAILED(result)){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: xrWaitFrame failed (%d)",static_cast<int>(result));return false;}XrFrameBeginInfo begin={XR_TYPE_FRAME_BEGIN_INFO};result=beginFrame(session,&begin);if(XR_FAILED(result)){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: xrBeginFrame failed (%d)",static_cast<int>(result));return false;}frameActive=true;
 XrViewState viewState={XR_TYPE_VIEW_STATE};uint32_t viewCount=0;XrViewLocateInfo locate={XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;locate.displayTime=currentFrame.predictedDisplayTime;locate.space=space;
 result=locateViews(session,&locate,&viewState,2,&viewCount,currentViews);if(!currentFrame.shouldRender||XR_FAILED(result)||viewCount!=2){if(XR_FAILED(result))SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: xrLocateViews failed (%d)",static_cast<int>(result));EndFrame();return false;}
 XrSwapchainImageAcquireInfo acquire={XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};result=acquireImage(swapchain,&acquire,&currentImage);if(XR_FAILED(result)){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: xrAcquireSwapchainImage failed (%d)",static_cast<int>(result));EndFrame();return false;}
 // Ownership starts at acquire, not at wait.  If wait fails EndFrame still has
 // to release this image or the finite OpenXR swapchain will be exhausted.
 imageAcquired=true;
 XrSwapchainImageWaitInfo imageWait={XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};imageWait.timeout=XR_INFINITE_DURATION;result=waitImage(swapchain,&imageWait);if(XR_FAILED(result)){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: xrWaitSwapchainImage failed (%d)",static_cast<int>(result));EndFrame();return false;}
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
void EndEye(unsigned){if(eyeActive)GetVulkanContext().EndPddiEye();eyeActive=false;}
void EndFrame(){if(!frameActive)return;if(eyeActive)EndEye(currentEye);if(imageAcquired){
 // xrReleaseSwapchainImage transfers ownership back to the compositor.  Both
 // per-eye command buffers must have completed before that handoff; merely
 // submitting them to the Vulkan queue is not sufficient synchronization.
 const VkResult queueResult=vkQueueWaitIdle(GetVulkanContext().GetQueue());
 if(queueResult!=VK_SUCCESS)
     SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                  "PCVR: vkQueueWaitIdle before swapchain release failed (%d)",
                  static_cast<int>(queueResult));
 XrSwapchainImageReleaseInfo release={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};const XrResult releaseResult=releaseImage(swapchain,&release);if(XR_FAILED(releaseResult))SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: xrReleaseSwapchainImage failed (%d)",static_cast<int>(releaseResult));imageAcquired=false;}
 XrCompositionLayerProjectionView projectionViews[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};for(uint32_t i=0;i<2;++i){projectionViews[i].pose=currentViews[i].pose;projectionViews[i].fov=currentViews[i].fov;projectionViews[i].subImage.swapchain=swapchain;projectionViews[i].subImage.imageRect.extent.width=eyeWidth;projectionViews[i].subImage.imageRect.extent.height=eyeHeight;projectionViews[i].subImage.imageArrayIndex=i;}
 XrCompositionLayerProjection layer={XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=space;layer.viewCount=2;layer.views=projectionViews;const XrCompositionLayerBaseHeader* layers[]={reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};XrFrameEndInfo end={XR_TYPE_FRAME_END_INFO};end.displayTime=currentFrame.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=currentFrame.shouldRender?1:0;end.layers=currentFrame.shouldRender?layers:NULL;const XrResult endResult=endFrame(session,&end);if(XR_FAILED(endResult))SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR: xrEndFrame failed (%d)",static_cast<int>(endResult));else if((frameSerial%120u)==1u)SDL_Log("PCVR submit: serial=%llu image=%u end=XR_SUCCESS",static_cast<unsigned long long>(frameSerial),currentImage);frameActive=false;}
} }

namespace SharOpenXR
{
// Compatibility surface used by the shared Vulkan PDDI while the desktop
// compositor is being connected to the full gameplay OpenXR manager.
bool GetActiveVulkanEyeTarget(VulkanEyeTarget* target) { if(!target||!Desktop::imageAcquired||!Desktop::eyeActive)return false;target->image=Desktop::images[Desktop::currentImage].image;target->format=Desktop::swapchainFormat;target->width=Desktop::eyeWidth;target->height=Desktop::eyeHeight;target->arrayLayer=Desktop::currentEye;target->firstUse=false;return true; }
bool GetActiveProjection(rmt::Matrix* projection,int* width,int* height) { if(!projection||!Desktop::eyeActive||!Desktop::worldRendering||Desktop::movieRendering)return false;const XrFovf& fov=Desktop::currentViews[Desktop::currentEye].fov;const float l=std::tan(fov.angleLeft),r=std::tan(fov.angleRight),b=std::tan(fov.angleDown),t=std::tan(fov.angleUp),n=0.1f,f=1000.0f;projection->Identity();projection->Row4(0).Set(2.0f/(r-l),0,0,0);projection->Row4(1).Set(0,2.0f/(t-b),0,0);projection->Row4(2).Set(-(r+l)/(r-l),-(t+b)/(t-b),(f+n)/(f-n),1);projection->Row4(3).Set(0,0,(-2.0f*f*n)/(f-n),0);if(width)*width=Desktop::eyeWidth;if(height)*height=Desktop::eyeHeight;return true; }
bool GetActiveViewport(int* width,int* height) { if(!Desktop::eyeActive)return false;if(width)*width=Desktop::eyeWidth;if(height)*height=Desktop::eyeHeight;return true; }
bool GetActiveUiHorizontalOffset(float*) { return false; }
void SetWorldRendering(bool enabled) { Desktop::worldRendering=enabled; }
void SetMovieRendering(bool enabled) { Desktop::movieRendering=enabled; }
bool GetActiveRadarProjection(rmt::Matrix*,int*,int*) { return false; }
bool GetActiveMovieProjection(rmt::Matrix*,int*,int*) { return false; }
bool GetActiveFrontendProjection(rmt::Matrix*,int*,int*) { return false; }
bool GetLatestCullingCamera(rmt::Matrix*) { return false; }
bool GetMultiviewMatrices(rmt::Matrix*,rmt::Matrix*) { return false; }
void SetMultiviewTargetActive(bool) {}
bool GetEyeCamera(unsigned eye,tCamera* base,rmt::Matrix* out) { if(eye>1||!base||!out)return false;const XrPosef& pose=Desktop::currentViews[eye].pose;rmt::Quaternion q(pose.orientation.w,-pose.orientation.x,-pose.orientation.y,pose.orientation.z);rmt::Matrix local;local.Identity();local.FillRotation(q);local.Row(3).Set(pose.position.x,pose.position.y,-pose.position.z);out->Mult(local,base->GetCameraToWorldMatrix());return true; }
bool IsEmbeddedHudRendering() { return false; }
bool IsRadarRendering() { return false; }
bool IsMovieRendering() { return Desktop::movieRendering; }
bool IsFrontendPlaneRendering() { return false; }
bool IsRightEyeRendering() { return Desktop::eyeActive&&Desktop::currentEye==1; }
bool IsSpatialHudEnabled() { return false; }
bool HasEnhancedUiConvergence() { return false; }
bool AreCustomMaterialsEnabled() { return true; }
int GetEnhancedMaterialModel() { return 1; }
int GetReflectionMode() { return 0; }
namespace
{
bool desktopVrModeEnabled=true;
bool desktopDeveloperMenusEnabled=false;
bool desktopSeatedMode=true;
bool desktopSnapTurnEnabled=true;
float desktopSmoothTurnSpeed=90.0f;
float desktopSnapTurnAngle=30.0f;
int desktopVehicleControlMode=0;
bool desktopVehicleComfortEnabled=false;
int desktopPbrDebugMode=0;
}
int GetPbrDebugMode() { return desktopPbrDebugMode; }
void SetPbrDebugMode(int mode) { desktopPbrDebugMode=mode; }
void SetVrModeEnabled(bool enabled) { desktopVrModeEnabled=enabled; }
bool IsVrModeEnabled() { return desktopVrModeEnabled; }
void SetDeveloperMenusEnabled(bool enabled) { desktopDeveloperMenusEnabled=enabled; }
bool IsDeveloperMenusEnabled() { return desktopDeveloperMenusEnabled; }
void SetSeatedMode(bool enabled) { desktopSeatedMode=enabled; }
bool IsSeatedMode() { return desktopSeatedMode; }
void SetSnapTurnEnabled(bool enabled) { desktopSnapTurnEnabled=enabled; }
bool IsSnapTurnEnabled() { return desktopSnapTurnEnabled; }
void SetSmoothTurnSpeed(float value) { desktopSmoothTurnSpeed=value; }
float GetSmoothTurnSpeed() { return desktopSmoothTurnSpeed; }
void SetSnapTurnAngle(float value) { desktopSnapTurnAngle=value; }
float GetSnapTurnAngle() { return desktopSnapTurnAngle; }
void SetVehicleControlMode(int mode) { desktopVehicleControlMode=mode; }
int GetVehicleControlMode() { return desktopVehicleControlMode; }
void SetVehicleComfortEnabled(bool enabled) { desktopVehicleComfortEnabled=enabled; }
bool IsVehicleComfortEnabled() { return desktopVehicleComfortEnabled; }
bool IsHorizontalMenuInputDominant() { return false; }
bool IsVerticalMenuInputDominant() { return false; }
void RecordPddiDraw(unsigned,unsigned,bool,double) {}
void RecordPddiUpload(unsigned,double) {}
}
#endif

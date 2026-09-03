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
#include <SDL.h>
#include <cstring>
#include <vector>
namespace SharOpenXR { namespace Desktop { namespace {
void* loader=NULL; PFN_xrGetInstanceProcAddr getProc=NULL;
XrInstance instance=XR_NULL_HANDLE; XrSystemId system=XR_NULL_SYSTEM_ID;
XrSession session=XR_NULL_HANDLE; XrSpace space=XR_NULL_HANDLE;
XrSwapchain swapchain=XR_NULL_HANDLE; int32_t eyeWidth=0,eyeHeight=0;
std::vector<XrSwapchainImageVulkanKHR> images; std::vector<unsigned char> initialized;
XrSessionState sessionState=XR_SESSION_STATE_UNKNOWN; bool running=false;
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
 ci.applicationInfo.apiVersion=XR_CURRENT_API_VERSION;ci.enabledExtensionCount=static_cast<uint32_t>(extensions.size());ci.enabledExtensionNames=extensions.data();
 if(XR_FAILED(create(&ci,&instance))){ShutdownRuntime();return false;}
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
 int64_t format=formats.empty()?VK_FORMAT_R8G8B8A8_UNORM:formats[0];for(size_t i=0;i<formats.size();++i)if(formats[i]==VK_FORMAT_R8G8B8A8_SRGB){format=formats[i];break;}
 eyeWidth=static_cast<int32_t>(views[0].recommendedImageRectWidth);eyeHeight=static_cast<int32_t>(views[0].recommendedImageRectHeight);
 XrSwapchainCreateInfo swapInfo={XR_TYPE_SWAPCHAIN_CREATE_INFO};swapInfo.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;swapInfo.format=format;swapInfo.sampleCount=1;swapInfo.width=eyeWidth;swapInfo.height=eyeHeight;swapInfo.faceCount=1;swapInfo.arraySize=2;swapInfo.mipCount=1;
 if(XR_FAILED(createSwapchain(session,&swapInfo,&swapchain))){ShutdownRuntime();return false;}
 uint32_t imageCount=0;enumerateImages(swapchain,0,&imageCount,NULL);images.resize(imageCount,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});initialized.assign(imageCount,0);
 if(XR_FAILED(enumerateImages(swapchain,imageCount,&imageCount,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())))){ShutdownRuntime();return false;}
 SDL_Log("PCVR: OpenXR Vulkan session ready (%dx%d)",eyeWidth,eyeHeight);return true;}
void ShutdownRuntime(){running=false;if(swapchain!=XR_NULL_HANDLE&&destroySwapchain)destroySwapchain(swapchain);swapchain=XR_NULL_HANDLE;images.clear();initialized.clear();if(space!=XR_NULL_HANDLE&&destroySpace)destroySpace(space);space=XR_NULL_HANDLE;if(session!=XR_NULL_HANDLE&&destroySession)destroySession(session);session=XR_NULL_HANDLE;GetVulkanContext().Shutdown();if(instance!=XR_NULL_HANDLE&&destroyInstance)destroyInstance(instance);instance=XR_NULL_HANDLE;if(loader)Platform::CloseLoader(loader);loader=NULL;getProc=NULL;system=XR_NULL_SYSTEM_ID;}
bool IsRuntimeReady(){return session!=XR_NULL_HANDLE;}
void PumpCompositor(){
 if(!IsRuntimeReady())return;XrEventDataBuffer event={XR_TYPE_EVENT_DATA_BUFFER};
 while(pollEvent(instance,&event)==XR_SUCCESS){if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){XrEventDataSessionStateChanged* changed=reinterpret_cast<XrEventDataSessionStateChanged*>(&event);sessionState=changed->state;if(sessionState==XR_SESSION_STATE_READY){XrSessionBeginInfo begin={XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;running=XR_SUCCEEDED(beginSession(session,&begin));}else if(sessionState==XR_SESSION_STATE_STOPPING){endSession(session);running=false;}}event={XR_TYPE_EVENT_DATA_BUFFER};}
 if(!running)return;XrFrameWaitInfo wait={XR_TYPE_FRAME_WAIT_INFO};XrFrameState state={XR_TYPE_FRAME_STATE};if(XR_FAILED(waitFrame(session,&wait,&state)))return;XrFrameBeginInfo begin={XR_TYPE_FRAME_BEGIN_INFO};if(XR_FAILED(beginFrame(session,&begin)))return;
 XrView xrViews[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};XrViewState viewState={XR_TYPE_VIEW_STATE};uint32_t viewCount=0;XrViewLocateInfo locate={XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;locate.displayTime=state.predictedDisplayTime;locate.space=space;
 bool submit=state.shouldRender&&XR_SUCCEEDED(locateViews(session,&locate,&viewState,2,&viewCount,xrViews))&&viewCount==2;uint32_t index=0;
 if(submit){XrSwapchainImageAcquireInfo acquire={XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};submit=XR_SUCCEEDED(acquireImage(swapchain,&acquire,&index));}
 if(submit){XrSwapchainImageWaitInfo imageWait={XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};imageWait.timeout=XR_INFINITE_DURATION;submit=XR_SUCCEEDED(waitImage(swapchain,&imageWait));}
 if(submit){submit=GetVulkanContext().ClearImage(images[index].image,!initialized[index]);initialized[index]=1;XrSwapchainImageReleaseInfo release={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};releaseImage(swapchain,&release);}
 XrCompositionLayerProjectionView projectionViews[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};for(uint32_t i=0;i<2;++i){projectionViews[i].pose=xrViews[i].pose;projectionViews[i].fov=xrViews[i].fov;projectionViews[i].subImage.swapchain=swapchain;projectionViews[i].subImage.imageRect.extent.width=eyeWidth;projectionViews[i].subImage.imageRect.extent.height=eyeHeight;projectionViews[i].subImage.imageArrayIndex=i;}
 XrCompositionLayerProjection layer={XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=space;layer.viewCount=2;layer.views=projectionViews;const XrCompositionLayerBaseHeader* layers[]={reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
 XrFrameEndInfo end={XR_TYPE_FRAME_END_INFO};end.displayTime=state.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=submit?1:0;end.layers=submit?layers:NULL;endFrame(session,&end);}
} }

namespace SharOpenXR
{
// Compatibility surface used by the shared Vulkan PDDI while the desktop
// compositor is being connected to the full gameplay OpenXR manager.
bool GetActiveVulkanEyeTarget(VulkanEyeTarget*) { return false; }
bool GetActiveProjection(rmt::Matrix*,int*,int*) { return false; }
bool GetActiveViewport(int*,int*) { return false; }
bool GetActiveUiHorizontalOffset(float*) { return false; }
bool GetActiveRadarProjection(rmt::Matrix*,int*,int*) { return false; }
bool GetActiveMovieProjection(rmt::Matrix*,int*,int*) { return false; }
bool GetActiveFrontendProjection(rmt::Matrix*,int*,int*) { return false; }
bool GetLatestCullingCamera(rmt::Matrix*) { return false; }
bool IsEmbeddedHudRendering() { return false; }
bool IsRadarRendering() { return false; }
bool IsMovieRendering() { return false; }
bool IsFrontendPlaneRendering() { return false; }
bool IsRightEyeRendering() { return false; }
bool IsSpatialHudEnabled() { return false; }
bool HasEnhancedUiConvergence() { return false; }
bool AreCustomMaterialsEnabled() { return true; }
int GetEnhancedMaterialModel() { return 1; }
int GetReflectionMode() { return 0; }
int GetPbrDebugMode() { return 0; }
void RecordPddiDraw(unsigned,unsigned,bool,double) {}
void RecordPddiUpload(unsigned,double) {}
}
#endif

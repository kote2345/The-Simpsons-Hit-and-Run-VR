#if defined(SRR2_OPENXR_PLATFORM_WIN32) && defined(SRR2_VR_RENDERER_VULKAN)
#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <vr/openxr_desktop_runtime.h>
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
PFN_xrDestroySpace destroySpace=NULL; PFN_xrDestroySession destroySession=NULL; PFN_xrDestroyInstance destroyInstance=NULL;
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
 if(!Load(instance,"xrDestroyInstance",reinterpret_cast<PFN_xrVoidFunction*>(&destroyInstance))||!Load(instance,"xrGetSystem",reinterpret_cast<PFN_xrVoidFunction*>(&getSystem))||!Load(instance,"xrCreateSession",reinterpret_cast<PFN_xrVoidFunction*>(&createSession))||!Load(instance,"xrDestroySession",reinterpret_cast<PFN_xrVoidFunction*>(&destroySession))||!Load(instance,"xrCreateReferenceSpace",reinterpret_cast<PFN_xrVoidFunction*>(&createSpace))||!Load(instance,"xrDestroySpace",reinterpret_cast<PFN_xrVoidFunction*>(&destroySpace))){ShutdownRuntime();return false;}
 XrSystemGetInfo si={XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
 if(XR_FAILED(getSystem(instance,&si,&system))||!GetVulkanContext().Initialize(instance,system,getProc)){ShutdownRuntime();return false;}
 XrGraphicsBindingVulkan2KHR binding={XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};binding.instance=GetVulkanContext().GetInstance();binding.physicalDevice=GetVulkanContext().GetPhysicalDevice();binding.device=GetVulkanContext().GetDevice();binding.queueFamilyIndex=GetVulkanContext().GetQueueFamilyIndex();
 XrSessionCreateInfo sci={XR_TYPE_SESSION_CREATE_INFO};sci.next=&binding;sci.systemId=system;if(XR_FAILED(createSession(instance,&sci,&session))){ShutdownRuntime();return false;}
 XrReferenceSpaceCreateInfo ri={XR_TYPE_REFERENCE_SPACE_CREATE_INFO};ri.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;ri.poseInReferenceSpace.orientation.w=1.0f;if(XR_FAILED(createSpace(session,&ri,&space))){ShutdownRuntime();return false;}
 SDL_Log("PCVR: OpenXR Vulkan session ready");return true;}
void ShutdownRuntime(){if(space!=XR_NULL_HANDLE&&destroySpace)destroySpace(space);space=XR_NULL_HANDLE;if(session!=XR_NULL_HANDLE&&destroySession)destroySession(session);session=XR_NULL_HANDLE;GetVulkanContext().Shutdown();if(instance!=XR_NULL_HANDLE&&destroyInstance)destroyInstance(instance);instance=XR_NULL_HANDLE;if(loader)Platform::CloseLoader(loader);loader=NULL;getProc=NULL;system=XR_NULL_SYSTEM_ID;}
bool IsRuntimeReady(){return session!=XR_NULL_HANDLE;}
} }
#endif

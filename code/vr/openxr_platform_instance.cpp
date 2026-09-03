#if defined(SRR2_OPENXR)

#if defined(SRR2_OPENXR_PLATFORM_ANDROID)
#define XR_USE_PLATFORM_ANDROID
#include <jni.h>
#include <SDL.h>
#include <SDL_system.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#else
#include <openxr/openxr.h>
#endif

#include <vr/openxr_platform_instance.h>

namespace SharOpenXR
{
namespace Platform
{
#if defined(SRR2_OPENXR_PLATFORM_ANDROID)
namespace
{
    XrInstanceCreateInfoAndroidKHR gAndroidCreateInfo = {
        XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
}

bool InitializeOpenXRLoader(PFN_xrGetInstanceProcAddr getProc)
{
    PFN_xrInitializeLoaderKHR initializeLoader=NULL;
    getProc(XR_NULL_HANDLE,"xrInitializeLoaderKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&initializeLoader));
    if(!initializeLoader) return true;

    JNIEnv* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity=static_cast<jobject>(SDL_AndroidGetActivity());
    JavaVM* vm=NULL;
    if(!env || !activity || env->GetJavaVM(&vm)!=JNI_OK) return false;

    XrLoaderInitInfoAndroidKHR loaderInfo={XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loaderInfo.applicationVM=vm;
    loaderInfo.applicationContext=activity;
    return XR_SUCCEEDED(initializeLoader(
        reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo)));
}

void AppendRequiredInstanceExtensions(std::vector<const char*>& extensions)
{
    extensions.push_back(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
}

bool PrepareInstanceCreateInfo(XrInstanceCreateInfo* createInfo)
{
    if(!createInfo) return false;
    JNIEnv* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    JavaVM* vm=NULL;
    if(!env || env->GetJavaVM(&vm)!=JNI_OK) return false;
    gAndroidCreateInfo.applicationVM=vm;
    gAndroidCreateInfo.applicationActivity=
        static_cast<jobject>(SDL_AndroidGetActivity());
    if(!gAndroidCreateInfo.applicationActivity) return false;
    createInfo->next=&gAndroidCreateInfo;
    return true;
}
#else
bool InitializeOpenXRLoader(PFN_xrGetInstanceProcAddr)
{
    return true;
}

void AppendRequiredInstanceExtensions(std::vector<const char*>&)
{
}

bool PrepareInstanceCreateInfo(XrInstanceCreateInfo* createInfo)
{
    if(!createInfo) return false;
    createInfo->next=NULL;
    return true;
}
#endif
}
}

#endif

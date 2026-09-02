#if defined(RAD_ANDROID) && defined(SRR2_VR_RENDERER_VULKAN)

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_VULKAN
#include <jni.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <vr/vulkan/openxr_vulkan_context.h>
#include <vr/vulkan/material_pipeline.h>
#include <vr/vulkan/material_uniforms.h>
#include <vr/openxrmanager.h>
#include <SDL.h>
#include <SDL_system.h>

#include <cstring>
#include <cstdio>
#include <chrono>
#include <vector>

namespace SharOpenXR
{
VulkanContext& GetVulkanContext()
{
    static VulkanContext context;
    return context;
}

namespace
{
#define VKXR_ERROR(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, \
                                     "OpenXR Vulkan: " __VA_ARGS__)

template <typename T>
bool LoadXr(PFN_xrGetInstanceProcAddr getProc, XrInstance instance,
            const char* name, T* function)
{
    return XR_SUCCEEDED(getProc(instance, name,
        reinterpret_cast<PFN_xrVoidFunction*>(function))) && *function;
}
}

VulkanContext::VulkanContext()
    : mInstance(VK_NULL_HANDLE),
      mPhysicalDevice(VK_NULL_HANDLE),
      mDevice(VK_NULL_HANDLE),
      mMultiviewSupported(false),
      mFragmentDensityMapSupported(false),
      mFragmentDensityMap(VK_NULL_HANDLE),
      mFragmentDensityMapWidth(0),mFragmentDensityMapHeight(0),
      mSamplerAnisotropyEnabled(false),
      mMaxSamplerAnisotropy(1.0f),
      mQueue(VK_NULL_HANDLE),
      mQueueFamilyIndex(0),
      mCommandPool(VK_NULL_HANDLE),
      mCommandBuffer(VK_NULL_HANDLE),
      mUtilityCommandBuffer(VK_NULL_HANDLE),
      mUploadCommandBuffer(VK_NULL_HANDLE),
      mFence(VK_NULL_HANDLE),
      mFrameArenaIndex(FrameArenaCount-1),
      mActiveFrameArena(NULL),
      mTimestampQueryPool(VK_NULL_HANDLE),
      mTimestampPeriod(0.0f),
      mLastGpuMilliseconds(0.0),
      mLastFenceWaitMilliseconds(0.0),
      mPipelineCache(VK_NULL_HANDLE),
      mPendingUploadBytes(0),
      mTextureUploadBuffer(VK_NULL_HANDLE),
      mTextureUploadMemory(VK_NULL_HANDLE),
      mTextureUploadMapped(NULL),
      mTextureUploadSize(48u*1024u*1024u),
      mTextureUploadBudget(8u*1024u*1024u),
      mTextureUploadOffset(0),
      mTextureUploadEnd(0),
      mTransientVertexBuffer(VK_NULL_HANDLE),
      mTransientVertexMemory(VK_NULL_HANDLE),
      mTransientVertexMapped(NULL),
      mTransientVertexSize(16u*1024u*1024u),
      mTransientVertexOffset(0),
      mTransientVertexEnd(0),
      mTextureSetLayout(VK_NULL_HANDLE),
      mDrawSetLayout(VK_NULL_HANDLE),
      mShadowSetLayout(VK_NULL_HANDLE),
      mTextureDescriptorPool(VK_NULL_HANDLE),
      mDrawDescriptorSet(VK_NULL_HANDLE),
      mShadowDescriptorSet(VK_NULL_HANDLE),
      mDrawUniformBuffer(VK_NULL_HANDLE),
      mDrawUniformMemory(VK_NULL_HANDLE),
      mDrawUniformMapped(NULL),
      // GPU-skinned draws carry a 25-matrix palette. Keep enough per-frame
      // arena space for dense city scenes without dropping late draw calls.
      mDrawUniformSize(16u*1024u*1024u),
      mDrawUniformOffset(0),
      mDrawUniformEnd(0),
      mDrawUniformAlignment(256),
      mFallbackImage(VK_NULL_HANDLE),
      mFallbackMemory(VK_NULL_HANDLE),
      mFallbackView(VK_NULL_HANDLE),
      mFallbackSampler(VK_NULL_HANDLE),
      mFallbackDescriptorSet(VK_NULL_HANDLE),
      mFallbackPbrImage(VK_NULL_HANDLE),
      mFallbackPbrMemory(VK_NULL_HANDLE),
      mFallbackPbrView(VK_NULL_HANDLE),
      mFallbackPbrSampler(VK_NULL_HANDLE),
      mFallbackPbrDescriptorSet(VK_NULL_HANDLE),
      mPddiEyeActive(false),
      mPddiRenderPassActive(false),
      mColourClearMask(0),
      mLastDrawStateIndex(static_cast<size_t>(-1)),
      mDynamicStateValid(false),
      mBoundDepthBias(0.0f),mBoundStencilReference(0),
      mBoundStencilCompareMask(0),mBoundStencilWriteMask(0),
      mBoundPipeline(VK_NULL_HANDLE),
      mBoundVertexBuffer(VK_NULL_HANDLE),
      mBoundVertexOffset(0),
      mBoundIndexBuffer(VK_NULL_HANDLE),
      mEyeSerial(0),
      mShaderVariantFrames(0),
      mPipelinePrewarm(false),
      mPrewarmedStateGroups(0),
      mShadowPass(false),
      mShadowCascadeIndex(0),
      mShadowReceiverEnabled(false),
      mVehicleCubeImage(VK_NULL_HANDLE),
      mVehicleCubeMemory(VK_NULL_HANDLE),
      mVehicleCubeView(VK_NULL_HANDLE),
      mVehicleCubeSampler(VK_NULL_HANDLE),
      mVehicleCubeDescriptor(VK_NULL_HANDLE),
      mVehicleCubeCapture(false),
      mVehicleCubeReady(false),
      mVehicleCubeFace(0),
      mVehicleCubeCompletedMask(0)
{
    std::memset(mBoundTextureSets,0,sizeof(mBoundTextureSets));
    std::memset(mFrameArenas,0,sizeof(mFrameArenas));
    std::memset(mShaderVariantDraws,0,sizeof(mShaderVariantDraws));
    std::memset(mShadowCascades,0,sizeof(mShadowCascades));
    std::memset(mShadowReceiverMatrices,0,sizeof(mShadowReceiverMatrices));
}

VulkanContext::~VulkanContext()
{
    Shutdown();
}

bool VulkanContext::Initialize(XrInstance xrInstance, XrSystemId systemId,
                               PFN_xrGetInstanceProcAddr getProc)
{
    if(IsInitialized()) return true;

    PFN_xrGetVulkanGraphicsRequirements2KHR getRequirements = NULL;
    PFN_xrCreateVulkanInstanceKHR createInstance = NULL;
    PFN_xrGetVulkanGraphicsDevice2KHR getGraphicsDevice = NULL;
    PFN_xrCreateVulkanDeviceKHR createDevice = NULL;
    if(!LoadXr(getProc,xrInstance,"xrGetVulkanGraphicsRequirements2KHR",&getRequirements) ||
       !LoadXr(getProc,xrInstance,"xrCreateVulkanInstanceKHR",&createInstance) ||
       !LoadXr(getProc,xrInstance,"xrGetVulkanGraphicsDevice2KHR",&getGraphicsDevice) ||
       !LoadXr(getProc,xrInstance,"xrCreateVulkanDeviceKHR",&createDevice))
    {
        VKXR_ERROR("XR_KHR_vulkan_enable2 entry points unavailable");
        return false;
    }

    XrGraphicsRequirementsVulkan2KHR requirements = {
        XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR };
    if(XR_FAILED(getRequirements(xrInstance,systemId,&requirements)))
    {
        VKXR_ERROR("xrGetVulkanGraphicsRequirements2KHR failed");
        return false;
    }

    XrVersion selectedXrVersion=XR_MAKE_VERSION(1,1,0);
    if(selectedXrVersion<requirements.minApiVersionSupported)
        selectedXrVersion=requirements.minApiVersionSupported;
    if(selectedXrVersion>requirements.maxApiVersionSupported)
        selectedXrVersion=requirements.maxApiVersionSupported;
    const uint32_t selectedVkVersion=VK_MAKE_API_VERSION(0,
        static_cast<uint32_t>(XR_VERSION_MAJOR(selectedXrVersion)),
        static_cast<uint32_t>(XR_VERSION_MINOR(selectedXrVersion)),
        static_cast<uint32_t>(XR_VERSION_PATCH(selectedXrVersion)));
    VkApplicationInfo applicationInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    applicationInfo.pApplicationName = "The Simpsons Hit & Run VR";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(0,1,0);
    applicationInfo.pEngineName = "Pure3D";
    applicationInfo.engineVersion = VK_MAKE_VERSION(1,0,0);
    applicationInfo.apiVersion = selectedVkVersion;

    VkInstanceCreateInfo instanceInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instanceInfo.pApplicationInfo = &applicationInfo;
    XrVulkanInstanceCreateInfoKHR xrInstanceInfo = {
        XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR };
    xrInstanceInfo.systemId = systemId;
    xrInstanceInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xrInstanceInfo.vulkanCreateInfo = &instanceInfo;
    xrInstanceInfo.vulkanAllocator = NULL;

    VkResult vkResult = VK_SUCCESS;
    XrResult xrResult = createInstance(xrInstance,&xrInstanceInfo,
                                       &mInstance,&vkResult);
    if(XR_FAILED(xrResult) || vkResult != VK_SUCCESS)
    {
        VKXR_ERROR("xrCreateVulkanInstanceKHR failed (XR %d, Vk %d)",
                   static_cast<int>(xrResult),static_cast<int>(vkResult));
        Shutdown(); return false;
    }

    XrVulkanGraphicsDeviceGetInfoKHR deviceInfo = {
        XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
    deviceInfo.systemId = systemId;
    deviceInfo.vulkanInstance = mInstance;
    if(XR_FAILED(getGraphicsDevice(xrInstance,&deviceInfo,&mPhysicalDevice)))
    {
        VKXR_ERROR("xrGetVulkanGraphicsDevice2KHR failed");
        Shutdown(); return false;
    }

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(mPhysicalDevice,&familyCount,NULL);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(mPhysicalDevice,&familyCount,
                                              families.data());
    bool familyFound = false;
    for(uint32_t i=0;i<familyCount;++i)
    {
        if(families[i].queueCount && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            mQueueFamilyIndex=i; familyFound=true; break;
        }
    }
    if(!familyFound)
    {
        VKXR_ERROR("no Vulkan graphics queue family");
        Shutdown(); return false;
    }

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueInfo.queueFamilyIndex=mQueueFamilyIndex;
    queueInfo.queueCount=1;
    queueInfo.pQueuePriorities=&queuePriority;
    VkDeviceCreateInfo logicalDeviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    logicalDeviceInfo.queueCreateInfoCount=1;
    logicalDeviceInfo.pQueueCreateInfos=&queueInfo;
    VkPhysicalDeviceMultiviewFeatures supportedMultiview={
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES };
    VkPhysicalDeviceFragmentDensityMapFeaturesEXT supportedDensity={
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT };
    supportedMultiview.pNext=&supportedDensity;
    VkPhysicalDeviceFeatures2 supportedFeatures2={
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    supportedFeatures2.pNext=&supportedMultiview;
    PFN_vkGetPhysicalDeviceFeatures2 getPhysicalDeviceFeatures2=
        reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            vkGetInstanceProcAddr(mInstance,"vkGetPhysicalDeviceFeatures2"));
    if(getPhysicalDeviceFeatures2)
        getPhysicalDeviceFeatures2(mPhysicalDevice,&supportedFeatures2);
    else
        vkGetPhysicalDeviceFeatures(mPhysicalDevice,&supportedFeatures2.features);
    const VkPhysicalDeviceFeatures& supportedFeatures=supportedFeatures2.features;
    VkPhysicalDeviceFeatures enabledFeatures={};
    if(supportedFeatures.textureCompressionBC)
        enabledFeatures.textureCompressionBC=VK_TRUE;
    if(supportedFeatures.samplerAnisotropy)
    {
        enabledFeatures.samplerAnisotropy=VK_TRUE;
        mSamplerAnisotropyEnabled=true;
        VkPhysicalDeviceProperties samplerProperties={};
        vkGetPhysicalDeviceProperties(mPhysicalDevice,&samplerProperties);
        mMaxSamplerAnisotropy=std::min(8.0f,samplerProperties.limits.maxSamplerAnisotropy);
    }
    VkPhysicalDeviceMultiviewFeatures enabledMultiview={
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES };
    VkPhysicalDeviceFragmentDensityMapFeaturesEXT enabledDensity={
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT };
    if(supportedMultiview.multiview)
    {
        enabledMultiview.multiview=VK_TRUE;
        mMultiviewSupported=true;
    }
    if(supportedDensity.fragmentDensityMap &&
       supportedDensity.fragmentDensityMapNonSubsampledImages)
    {
        enabledDensity.fragmentDensityMap=VK_TRUE;
        enabledDensity.fragmentDensityMapNonSubsampledImages=VK_TRUE;
        mFragmentDensityMapSupported=true;
    }
    enabledMultiview.pNext=mFragmentDensityMapSupported?&enabledDensity:NULL;
    logicalDeviceInfo.pNext=&enabledMultiview;
    const char* densityExtension=VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME;
    if(mFragmentDensityMapSupported)
    {
        logicalDeviceInfo.enabledExtensionCount=1;
        logicalDeviceInfo.ppEnabledExtensionNames=&densityExtension;
    }
    logicalDeviceInfo.pEnabledFeatures=&enabledFeatures;
    XrVulkanDeviceCreateInfoKHR xrDeviceInfo = {
        XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR };
    xrDeviceInfo.systemId=systemId;
    xrDeviceInfo.pfnGetInstanceProcAddr=vkGetInstanceProcAddr;
    xrDeviceInfo.vulkanPhysicalDevice=mPhysicalDevice;
    xrDeviceInfo.vulkanCreateInfo=&logicalDeviceInfo;
    xrDeviceInfo.vulkanAllocator=NULL;
    xrResult=createDevice(xrInstance,&xrDeviceInfo,&mDevice,&vkResult);
    if(XR_FAILED(xrResult) || vkResult!=VK_SUCCESS)
    {
        VKXR_ERROR("xrCreateVulkanDeviceKHR failed (XR %d, Vk %d)",
                   static_cast<int>(xrResult),static_cast<int>(vkResult));
        Shutdown(); return false;
    }

    vkGetDeviceQueue(mDevice,mQueueFamilyIndex,0,&mQueue);
    VkCommandPoolCreateInfo poolInfo={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex=mQueueFamilyIndex;
    if(vkCreateCommandPool(mDevice,&poolInfo,NULL,&mCommandPool)!=VK_SUCCESS)
    {
        VKXR_ERROR("vkCreateCommandPool failed"); Shutdown(); return false;
    }
    VkCommandBufferAllocateInfo commandInfo={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool=mCommandPool;
    commandInfo.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount=1;
    if(vkAllocateCommandBuffers(mDevice,&commandInfo,&mUtilityCommandBuffer)!=VK_SUCCESS ||
       vkAllocateCommandBuffers(mDevice,&commandInfo,&mUploadCommandBuffer)!=VK_SUCCESS)
    {
        VKXR_ERROR("vkAllocateCommandBuffers failed"); Shutdown(); return false;
    }
    VkFenceCreateInfo fenceInfo={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if(vkCreateFence(mDevice,&fenceInfo,NULL,&mFence)!=VK_SUCCESS)
    {
        VKXR_ERROR("vkCreateFence failed"); Shutdown(); return false;
    }
    for(uint32_t i=0;i<FrameArenaCount;++i)
    {
        if(vkAllocateCommandBuffers(mDevice,&commandInfo,
                                    &mFrameArenas[i].commandBuffer)!=VK_SUCCESS ||
           vkCreateFence(mDevice,&fenceInfo,NULL,&mFrameArenas[i].fence)!=VK_SUCCESS)
        {
            VKXR_ERROR("frame arena creation failed"); Shutdown(); return false;
        }
    }
    VkPhysicalDeviceProperties timestampProperties={};
    vkGetPhysicalDeviceProperties(mPhysicalDevice,&timestampProperties);
    if(timestampProperties.limits.timestampComputeAndGraphics)
    {
        VkQueryPoolCreateInfo queryInfo={VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        queryInfo.queryType=VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount=2;
        bool timestampsReady=true;
        for(uint32_t i=0;i<FrameArenaCount;++i)
            timestampsReady=timestampsReady &&
                vkCreateQueryPool(mDevice,&queryInfo,NULL,
                                  &mFrameArenas[i].timestampQueryPool)==VK_SUCCESS;
        if(timestampsReady) mTimestampPeriod=timestampProperties.limits.timestampPeriod;
    }
    std::vector<unsigned char> pipelineCacheData;
    char* prefPath=SDL_GetPrefPath("LucasArts","The Simpsons Hit & Run VR");
    if(prefPath)
    {
        mPipelineCachePath=std::string(prefPath)+"vulkan_pipeline_cache.bin";
        SDL_free(prefPath);
        SDL_RWops* cacheFile=SDL_RWFromFile(mPipelineCachePath.c_str(),"rb");
        if(cacheFile)
        {
            const Sint64 cacheSize=SDL_RWsize(cacheFile);
            if(cacheSize>0 && cacheSize<64*1024*1024)
            {
                pipelineCacheData.resize(static_cast<size_t>(cacheSize));
                if(SDL_RWread(cacheFile,pipelineCacheData.data(),1,
                              pipelineCacheData.size())!=pipelineCacheData.size())
                    pipelineCacheData.clear();
            }
            SDL_RWclose(cacheFile);
        }
    }
    VkPipelineCacheCreateInfo pipelineCacheInfo={
        VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    pipelineCacheInfo.initialDataSize=pipelineCacheData.size();
    pipelineCacheInfo.pInitialData=pipelineCacheData.empty()?NULL:pipelineCacheData.data();
    VkResult pipelineCacheResult=vkCreatePipelineCache(
        mDevice,&pipelineCacheInfo,NULL,&mPipelineCache);
    if(pipelineCacheResult!=VK_SUCCESS && !pipelineCacheData.empty())
    {
        pipelineCacheInfo.initialDataSize=0;
        pipelineCacheInfo.pInitialData=NULL;
        pipelineCacheResult=vkCreatePipelineCache(
            mDevice,&pipelineCacheInfo,NULL,&mPipelineCache);
    }
    if(pipelineCacheResult!=VK_SUCCESS)
    {
        VKXR_ERROR("vkCreatePipelineCache failed"); Shutdown(); return false;
    }
    VkDescriptorSetLayoutBinding textureBinding={};
    textureBinding.binding=0; textureBinding.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureBinding.descriptorCount=1; textureBinding.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount=1; layoutInfo.pBindings=&textureBinding;
    if(vkCreateDescriptorSetLayout(mDevice,&layoutInfo,NULL,&mTextureSetLayout)!=VK_SUCCESS)
    { VKXR_ERROR("texture descriptor layout creation failed"); Shutdown(); return false; }
    VkDescriptorSetLayoutBinding drawBinding={};
    drawBinding.binding=0; drawBinding.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    drawBinding.descriptorCount=1;
    drawBinding.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutInfo.bindingCount=1; layoutInfo.pBindings=&drawBinding;
    if(vkCreateDescriptorSetLayout(mDevice,&layoutInfo,NULL,&mDrawSetLayout)!=VK_SUCCESS)
    { VKXR_ERROR("draw descriptor layout creation failed"); Shutdown(); return false; }
    // Keep the environment cube in this low, global descriptor set. Adreno
    // devices used by Quest return zeroes for the old set-7 sampler even when
    // the descriptor itself is valid.
    VkDescriptorSetLayoutBinding shadowBindings[4]={};
    for(uint32_t binding=0;binding<4;++binding)
    {
        shadowBindings[binding].binding=binding;
        shadowBindings[binding].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadowBindings[binding].descriptorCount=1;
        shadowBindings[binding].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    layoutInfo.bindingCount=4; layoutInfo.pBindings=shadowBindings;
    if(vkCreateDescriptorSetLayout(mDevice,&layoutInfo,NULL,&mShadowSetLayout)!=VK_SUCCESS)
    { VKXR_ERROR("shadow descriptor layout creation failed"); Shutdown(); return false; }
    VkDescriptorPoolSize poolSizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,8192},
                                       {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1}};
    VkDescriptorPoolCreateInfo poolCreate={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolCreate.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolCreate.maxSets=8193; poolCreate.poolSizeCount=2; poolCreate.pPoolSizes=poolSizes;
    if(vkCreateDescriptorPool(mDevice,&poolCreate,NULL,&mTextureDescriptorPool)!=VK_SUCCESS)
    { VKXR_ERROR("texture descriptor pool creation failed"); Shutdown(); return false; }
    VkPhysicalDeviceProperties properties={};
    vkGetPhysicalDeviceProperties(mPhysicalDevice,&properties);
    mDrawUniformAlignment=std::max<VkDeviceSize>(16,
        properties.limits.minUniformBufferOffsetAlignment);
    if(!CreateBuffer(mDrawUniformSize,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
       &mDrawUniformBuffer,&mDrawUniformMemory) ||
       vkMapMemory(mDevice,mDrawUniformMemory,0,mDrawUniformSize,0,&mDrawUniformMapped)!=VK_SUCCESS)
    { VKXR_ERROR("dynamic draw uniform ring creation failed"); Shutdown(); return false; }
    VkDescriptorSetAllocateInfo drawAllocate={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    drawAllocate.descriptorPool=mTextureDescriptorPool; drawAllocate.descriptorSetCount=1;
    drawAllocate.pSetLayouts=&mDrawSetLayout;
    if(vkAllocateDescriptorSets(mDevice,&drawAllocate,&mDrawDescriptorSet)!=VK_SUCCESS)
    { VKXR_ERROR("draw descriptor allocation failed"); Shutdown(); return false; }
    drawAllocate.pSetLayouts=&mShadowSetLayout;
    if(vkAllocateDescriptorSets(mDevice,&drawAllocate,&mShadowDescriptorSet)!=VK_SUCCESS)
    { VKXR_ERROR("shadow descriptor allocation failed"); Shutdown(); return false; }
    VkDescriptorBufferInfo drawBufferInfo={mDrawUniformBuffer,0,2816};
    VkWriteDescriptorSet drawWrite={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    drawWrite.dstSet=mDrawDescriptorSet; drawWrite.dstBinding=0; drawWrite.descriptorCount=1;
    drawWrite.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    drawWrite.pBufferInfo=&drawBufferInfo;
    vkUpdateDescriptorSets(mDevice,1,&drawWrite,0,NULL);
    if(!CreateTexture2D(1,1,1,&mFallbackImage,&mFallbackMemory,&mFallbackView,
                        &mFallbackSampler,&mFallbackDescriptorSet))
    { VKXR_ERROR("fallback texture creation failed"); Shutdown(); return false; }
    const uint32_t white=0xffffffffu;
    if(!UploadTextureMip(mFallbackImage,1,1,0,&white,sizeof(white)))
    { VKXR_ERROR("fallback texture upload failed"); Shutdown(); return false; }
    if(!CreateTexture2D(1,1,1,&mFallbackPbrImage,&mFallbackPbrMemory,
       &mFallbackPbrView,&mFallbackPbrSampler,&mFallbackPbrDescriptorSet,
       VK_FORMAT_R8G8B8A8_UNORM))
    { VKXR_ERROR("fallback PBR texture creation failed"); Shutdown(); return false; }
    // RGBA bytes: flat normal XY, 0.45 roughness, dielectric metallic.
    const uint32_t fallbackPbr=0x00738080u;
    if(!UploadTextureMip(mFallbackPbrImage,1,1,0,&fallbackPbr,sizeof(fallbackPbr)))
    { VKXR_ERROR("fallback PBR texture upload failed"); Shutdown(); return false; }
    if(!FlushTextureUploads())
    { VKXR_ERROR("fallback texture transfer failed"); Shutdown(); return false; }
    if(!CreateBuffer(mTransientVertexSize,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
       &mTransientVertexBuffer,&mTransientVertexMemory) ||
       vkMapMemory(mDevice,mTransientVertexMemory,0,mTransientVertexSize,0,
                   &mTransientVertexMapped)!=VK_SUCCESS)
    { VKXR_ERROR("persistent transient vertex ring creation failed"); Shutdown(); return false; }
    if(!CreateBuffer(mTextureUploadSize,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
       &mTextureUploadBuffer,&mTextureUploadMemory) ||
       vkMapMemory(mDevice,mTextureUploadMemory,0,mTextureUploadSize,0,
                   &mTextureUploadMapped)!=VK_SUCCESS)
    { VKXR_ERROR("persistent texture upload ring creation failed"); Shutdown(); return false; }
    SDL_Log("OpenXR Vulkan: initialized API %u.%u.%u, queue family %u, multiview=%d",
        VK_VERSION_MAJOR(applicationInfo.apiVersion),
        VK_VERSION_MINOR(applicationInfo.apiVersion),
        VK_VERSION_PATCH(applicationInfo.apiVersion),mQueueFamilyIndex,
        mMultiviewSupported?1:0);
    return true;
}

bool VulkanContext::ClearImage(VkImage image,bool firstUse)
{
    if(!IsInitialized() || !image) return false;
    mCommandBuffer=mUtilityCommandBuffer;
    vkResetFences(mDevice,1,&mFence);
    vkResetCommandBuffer(mCommandBuffer,0);
    VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if(vkBeginCommandBuffer(mCommandBuffer,&begin)!=VK_SUCCESS) return false;

    VkImageMemoryBarrier toTransfer={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.srcAccessMask=firstUse?0:VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toTransfer.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout=firstUse?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toTransfer.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image=image;
    toTransfer.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount=1;
    toTransfer.subresourceRange.layerCount=2;
    vkCmdPipelineBarrier(mCommandBuffer,
        firstUse?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&toTransfer);
    const VkClearColorValue colour={{0.0f,0.0f,0.0f,1.0f}};
    vkCmdClearColorImage(mCommandBuffer,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &colour,1,&toTransfer.subresourceRange);

    VkImageMemoryBarrier toPresent=toTransfer;
    toPresent.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,NULL,0,NULL,1,&toPresent);
    if(vkEndCommandBuffer(mCommandBuffer)!=VK_SUCCESS) return false;
    VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount=1; submit.pCommandBuffers=&mCommandBuffer;
    if(vkQueueSubmit(mQueue,1,&submit,mFence)!=VK_SUCCESS) return false;
    return vkWaitForFences(mDevice,1,&mFence,VK_TRUE,XR_INFINITE_DURATION)==VK_SUCCESS;
}

bool VulkanContext::ClearImageInPddiEye(VkImage image,bool firstUse,uint32_t layer)
{
    if(!mPddiEyeActive || !image || mPddiRenderPassActive || layer>2) return false;
    VkImageMemoryBarrier toTransfer={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.srcAccessMask=firstUse?0:VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toTransfer.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout=firstUse?VK_IMAGE_LAYOUT_UNDEFINED:
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toTransfer.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image=image;
    toTransfer.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount=1;
    toTransfer.subresourceRange.baseArrayLayer=layer<2?layer:0;
    toTransfer.subresourceRange.layerCount=layer<2?1:2;
    vkCmdPipelineBarrier(mCommandBuffer,
        firstUse?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&toTransfer);

    // Clear now rather than postponing the operation until the first draw.
    // Loading transitions can legitimately submit an eye with no geometry;
    // a deferred render-pass clear would then never run and the compositor
    // would expose the previous contents of that swapchain layer as trails.
    const VkClearColorValue black={{0.0f,0.0f,0.0f,1.0f}};
    vkCmdClearColorImage(mCommandBuffer,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &black,1,&toTransfer.subresourceRange);

    VkImageMemoryBarrier toColour=toTransfer;
    toColour.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    toColour.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toColour.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toColour.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,NULL,0,NULL,1,&toColour);
    mColourClearMask=0;
    return true;
}

bool VulkanContext::DrawSmokeTriangle(VkImage image,VkFormat format,
                                      uint32_t width,uint32_t height,
                                      uint32_t arrayLayer)
{
    // The initial smoke triangle has served its purpose. Real PDDI geometry
    // now owns the graphics pipeline and the acquired eye target.
    return image && format!=VK_FORMAT_UNDEFINED && width && height && arrayLayer<2;
}

bool VulkanContext::BeginPddiEye()
{
    if(!IsInitialized() || mPddiEyeActive) return false;
    mFrameArenaIndex=(mFrameArenaIndex+1)%FrameArenaCount;
    mActiveFrameArena=&mFrameArenas[mFrameArenaIndex];
    if(mActiveFrameArena->submitted)
    {
        const std::chrono::steady_clock::time_point fenceStart=std::chrono::steady_clock::now();
        if(vkWaitForFences(mDevice,1,&mActiveFrameArena->fence,VK_TRUE,
                           XR_INFINITE_DURATION)!=VK_SUCCESS)
            return false;
        mLastFenceWaitMilliseconds=
            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-
                                                     fenceStart).count();
        if(mActiveFrameArena->timestampQueryPool)
        {
            uint64_t timestamps[2]={};
            if(vkGetQueryPoolResults(mDevice,mActiveFrameArena->timestampQueryPool,
                 0,2,sizeof(timestamps),
                 timestamps,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT)==VK_SUCCESS &&
               timestamps[1]>=timestamps[0])
                mLastGpuMilliseconds=static_cast<double>(timestamps[1]-timestamps[0])*
                                     mTimestampPeriod/1000000.0;
        }
        mActiveFrameArena->submitted=false;
        for(uint32_t cascadeIndex=0;cascadeIndex<3;++cascadeIndex)
        {
            ShadowCascade& cascade=mShadowCascades[cascadeIndex];
            if(cascade.readbackArena!=static_cast<int>(mFrameArenaIndex) ||
               !cascade.readbackMapped || cascade.readbackLogged) continue;
            const size_t pixels=static_cast<size_t>(cascade.size)*cascade.size;
            const uint32_t* values=static_cast<const uint32_t*>(cascade.readbackMapped);
            uint32_t minimum=0xffffffffu,maximum=0;
            size_t clearLow=0,clearHigh=0,nonZero=0;
            for(size_t i=0;i<pixels;++i)
            {
                const uint32_t value=values[i];
                minimum=std::min(minimum,value); maximum=std::max(maximum,value);
                if((value&0x00ffffffu)==0x00ffffffu) ++clearLow;
                if((value&0xffffff00u)==0xffffff00u) ++clearHigh;
                if(value) ++nonZero;
            }
            SDL_Log("Vulkan CSM depth c%u: min=%08x max=%08x clearLow=%.2f%% clearHigh=%.2f%% nonzero=%.2f%%",
                    cascadeIndex,minimum,maximum,100.0*clearLow/pixels,
                    100.0*clearHigh/pixels,100.0*nonZero/pixels);
            const char* externalPath=SDL_AndroidGetExternalStoragePath();
            if(externalPath)
            {
                const std::string path=std::string(externalPath)+"/csm_depth_"+
                                       std::to_string(cascadeIndex)+".pgm";
                if(FILE* file=std::fopen(path.c_str(),"wb"))
                {
                    std::fprintf(file,"P5\n%u %u\n255\n",cascade.size,cascade.size);
                    std::vector<unsigned char> grayscale(pixels);
                    for(size_t i=0;i<pixels;++i)
                        grayscale[i]=static_cast<unsigned char>((values[i]&0x00ffffffu)>>16);
                    std::fwrite(grayscale.data(),1,grayscale.size(),file);
                    std::fclose(file);
                    SDL_Log("Vulkan CSM depth c%u saved: %s",cascadeIndex,path.c_str());
                }
            }
            cascade.readbackLogged=true; cascade.readbackArena=-1;
        }
    }
    else mLastFenceWaitMilliseconds=0.0;
    bool allArenasComplete=true;
    for(uint32_t i=0;i<FrameArenaCount;++i)
        if(mFrameArenas[i].submitted &&
           vkGetFenceStatus(mDevice,mFrameArenas[i].fence)!=VK_SUCCESS)
            allArenasComplete=false;
    if(allArenasComplete) ReleaseDeferredResources();
    mCommandBuffer=mActiveFrameArena->commandBuffer;
    mTimestampQueryPool=mActiveFrameArena->timestampQueryPool;
    vkResetFences(mDevice,1,&mActiveFrameArena->fence);
    vkResetCommandBuffer(mCommandBuffer,0);
    VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if(vkBeginCommandBuffer(mCommandBuffer,&begin)!=VK_SUCCESS) return false;
    if(mTimestampQueryPool)
    {
        vkCmdResetQueryPool(mCommandBuffer,mTimestampQueryPool,0,2);
        vkCmdWriteTimestamp(mCommandBuffer,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            mTimestampQueryPool,0);
    }
    mPddiEyeActive=true;
    mPddiRenderPassActive=false;
    const VkDeviceSize uploadSegment=mTextureUploadSize/FrameArenaCount;
    mTextureUploadOffset=uploadSegment*mFrameArenaIndex;
    mTextureUploadEnd=mFrameArenaIndex==FrameArenaCount-1?mTextureUploadSize:
                      uploadSegment*(mFrameArenaIndex+1);
    if(!RecordPendingTextureUploads()) return false;
    mColourClearMask=0;
    const VkDeviceSize transientSegment=mTransientVertexSize/FrameArenaCount;
    mTransientVertexOffset=transientSegment*mFrameArenaIndex;
    mTransientVertexEnd=mFrameArenaIndex==FrameArenaCount-1?mTransientVertexSize:
                        transientSegment*(mFrameArenaIndex+1);
    const VkDeviceSize uniformSegment=mDrawUniformSize/FrameArenaCount;
    mDrawUniformOffset=uniformSegment*mFrameArenaIndex;
    mDrawUniformEnd=mFrameArenaIndex==FrameArenaCount-1?mDrawUniformSize:
                    uniformSegment*(mFrameArenaIndex+1);
    mBoundPipeline=VK_NULL_HANDLE;
    mBoundVertexBuffer=VK_NULL_HANDLE;
    mBoundVertexOffset=0;
    mBoundIndexBuffer=VK_NULL_HANDLE;
    mDynamicStateValid=false;
    std::memset(mBoundTextureSets,0,sizeof(mBoundTextureSets));
    ++mEyeSerial;
    return true;
}

void VulkanContext::SetFragmentDensityMap(VkImage image,uint32_t width,uint32_t height)
{
    mFragmentDensityMap=mFragmentDensityMapSupported?image:VK_NULL_HANDLE;
    mFragmentDensityMapWidth=width;
    mFragmentDensityMapHeight=height;
}

void VulkanContext::ReleaseDeferredResources()
{
    for(VkPipeline value:mDeferredPipelines) vkDestroyPipeline(mDevice,value,NULL);
    for(VkPipelineLayout value:mDeferredPipelineLayouts) vkDestroyPipelineLayout(mDevice,value,NULL);
    for(VkShaderModule value:mDeferredShaderModules) vkDestroyShaderModule(mDevice,value,NULL);
    for(VkFramebuffer value:mDeferredFramebuffers) vkDestroyFramebuffer(mDevice,value,NULL);
    for(VkRenderPass value:mDeferredRenderPasses) vkDestroyRenderPass(mDevice,value,NULL);
    for(VkImageView value:mDeferredViews) vkDestroyImageView(mDevice,value,NULL);
    for(VkBuffer value:mDeferredBuffers) vkDestroyBuffer(mDevice,value,NULL);
    for(VkDeviceMemory value:mDeferredBufferMemory) vkFreeMemory(mDevice,value,NULL);
    mDeferredPipelines.clear(); mDeferredPipelineLayouts.clear();
    mDeferredShaderModules.clear(); mDeferredFramebuffers.clear();
    mDeferredRenderPasses.clear(); mDeferredViews.clear();
    mDeferredBuffers.clear(); mDeferredBufferMemory.clear();
}

bool VulkanContext::EndPddiEye()
{
    if(!mPddiEyeActive) return false;
    if(mPddiRenderPassActive)
    {
        vkCmdEndRenderPass(mCommandBuffer);
        mPddiRenderPassActive=false;
    }
    if(mTimestampQueryPool)
        vkCmdWriteTimestamp(mCommandBuffer,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            mTimestampQueryPool,1);
    mPddiEyeActive=false;
    if(vkEndCommandBuffer(mCommandBuffer)!=VK_SUCCESS)
    { ReleaseDeferredResources(); return false; }
    VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount=1; submit.pCommandBuffers=&mCommandBuffer;
    const bool submitted=vkQueueSubmit(mQueue,1,&submit,mActiveFrameArena->fence)==VK_SUCCESS;
    mActiveFrameArena->submitted=submitted;
    mActiveFrameArena=NULL;
    if(++mShaderVariantFrames>=120)
    {
        SDL_Log("Vulkan shader draws/120f: full=%u unlit=%u lit=%u compact=%u hud=%u",
                mShaderVariantDraws[0],mShaderVariantDraws[1],
                mShaderVariantDraws[2],mShaderVariantDraws[3],
                mShaderVariantDraws[4]);
        std::memset(mShaderVariantDraws,0,sizeof(mShaderVariantDraws));
        mShaderVariantFrames=0;
    }
    return submitted;
}

void VulkanContext::EndActiveRenderPass()
{
    if(mPddiEyeActive && mPddiRenderPassActive)
    {
        vkCmdEndRenderPass(mCommandBuffer);
        mPddiRenderPassActive=false;
    }
    // An offscreen shadow/cubemap pass changes dynamic state and bindings on
    // the same command buffer. Vulkan does not restore them when a render
    // pass ends, so force the next eye draw to emit the complete state again.
    mDynamicStateValid=false;
    mBoundPipeline=VK_NULL_HANDLE;
    mBoundVertexBuffer=mBoundIndexBuffer=VK_NULL_HANDLE;
    std::memset(mBoundTextureSets,0,sizeof(mBoundTextureSets));
}

void VulkanContext::ClearPddiBuffers(uint32_t bufferMask)
{
    if(!mPddiEyeActive || !bufferMask) return;
    EndActiveRenderPass();
    // DrawPddiGeometry lazily begins the next render pass. Advancing the
    // serial makes that pass use its depth/stencil-clear variant, which is
    // required when the GUI view clears depth after the 3D world. The base
    // PDDI implementation was a no-op, leaving every HUD fragment occluded.
    if(bufferMask&(2u|4u)) ++mEyeSerial;
    if(bufferMask&1u) mColourClearMask=0x3u;
}

void VulkanContext::ClearActiveColourBlack(uint32_t width,uint32_t height)
{
    if(!mPddiEyeActive || !mPddiRenderPassActive || !width || !height) return;
    VkClearAttachment attachment={};
    attachment.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    attachment.colorAttachment=0;
    attachment.clearValue.color.float32[0]=0.0f;
    attachment.clearValue.color.float32[1]=0.0f;
    attachment.clearValue.color.float32[2]=0.0f;
    attachment.clearValue.color.float32[3]=1.0f;
    VkClearRect rect={};
    rect.rect.extent.width=width;
    rect.rect.extent.height=height;
    rect.baseArrayLayer=0;
    rect.layerCount=1;
    vkCmdClearAttachments(mCommandBuffer,1,&attachment,1,&rect);
}

bool VulkanContext::BeginOffscreenTarget(VkImage image,bool initialized)
{
    if(!mPddiEyeActive || !image) return false;
    if(mPddiRenderPassActive)
    {
        vkCmdEndRenderPass(mCommandBuffer);
        mPddiRenderPassActive=false;
    }
    VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask=initialized?VK_ACCESS_SHADER_READ_BIT:0;
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout=initialized?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                                  VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=image; barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount=1; barrier.subresourceRange.layerCount=1;
    vkCmdPipelineBarrier(mCommandBuffer,initialized?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,
        0,NULL,0,NULL,1,&barrier);
    VkClearColorValue clear={};
    vkCmdClearColorImage(mCommandBuffer,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &clear,1,&barrier.subresourceRange);
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,NULL,0,NULL,1,&barrier);
    return true;
}

bool VulkanContext::EndOffscreenTarget(VkImage image)
{
    if(!mPddiEyeActive || !image) return false;
    if(mPddiRenderPassActive)
    {
        vkCmdEndRenderPass(mCommandBuffer);
        mPddiRenderPassActive=false;
    }
    VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=image; barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount=1; barrier.subresourceRange.layerCount=1;
    vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier);
    return true;
}

bool VulkanContext::BeginVehicleCubeMapFace(uint32_t face)
{
    if(!mPddiEyeActive || mVehicleCubeCapture || face>=6) return false;
    EndActiveRenderPass();
    const uint32_t size=128;
    const uint32_t mipLevels=8;
    if(!mVehicleCubeImage)
    {
        VkImageCreateInfo ii={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.flags=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        ii.imageType=VK_IMAGE_TYPE_2D; ii.format=VK_FORMAT_R8G8B8A8_UNORM;
        ii.extent={size,size,1}; ii.mipLevels=mipLevels; ii.arrayLayers=6;
        ii.samples=VK_SAMPLE_COUNT_1_BIT; ii.tiling=VK_IMAGE_TILING_OPTIMAL;
        ii.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ii.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        if(vkCreateImage(mDevice,&ii,NULL,&mVehicleCubeImage)!=VK_SUCCESS) return false;
        VkMemoryRequirements req={}; vkGetImageMemoryRequirements(mDevice,mVehicleCubeImage,&req);
        uint32_t type=0;
        if(!FindMemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&type)) return false;
        VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize=req.size; ai.memoryTypeIndex=type;
        if(vkAllocateMemory(mDevice,&ai,NULL,&mVehicleCubeMemory)!=VK_SUCCESS ||
           vkBindImageMemory(mDevice,mVehicleCubeImage,mVehicleCubeMemory,0)!=VK_SUCCESS) return false;
        VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=mVehicleCubeImage; vi.viewType=VK_IMAGE_VIEW_TYPE_CUBE; vi.format=ii.format;
        vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount=mipLevels; vi.subresourceRange.layerCount=6;
        if(vkCreateImageView(mDevice,&vi,NULL,&mVehicleCubeView)!=VK_SUCCESS ||
           !CreateTextureSamplerDescriptor(mVehicleCubeView,mipLevels,1,4,
                &mVehicleCubeSampler,&mVehicleCubeDescriptor)) return false;
        VkDescriptorImageInfo cubeInfo={mVehicleCubeSampler,mVehicleCubeView,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet cubeWrite={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        cubeWrite.dstSet=mShadowDescriptorSet;
        cubeWrite.dstBinding=3;
        cubeWrite.descriptorCount=1;
        cubeWrite.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cubeWrite.pImageInfo=&cubeInfo;
        vkUpdateDescriptorSets(mDevice,1,&cubeWrite,0,NULL);
        SDL_Log("Vulkan vehicle cubemap: allocated %ux%u incremental probe",size,size);
    }
    VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    const bool initialized=(mVehicleCubeCompletedMask&(1u<<face))!=0;
    barrier.srcAccessMask=initialized?VK_ACCESS_SHADER_READ_BIT:0;
    barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout=initialized?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=mVehicleCubeImage; barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer=face; barrier.subresourceRange.layerCount=1;
    barrier.subresourceRange.levelCount=1;
    vkCmdPipelineBarrier(mCommandBuffer,initialized?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,0,NULL,0,NULL,1,&barrier);
    // Match GLES: a cubemap face is always a mono target even while its
    // capture is issued from inside the OpenXR multiview world pass.
    SharOpenXR::SetMultiviewTargetActive(false);
    mVehicleCubeCapture=true; mVehicleCubeFace=face;
    mColourClearMask|=1u<<face;
    return true;
}

void VulkanContext::EndVehicleCubeMapFace(uint32_t face)
{
    if(!mVehicleCubeCapture || face!=mVehicleCubeFace) return;
    EndActiveRenderPass();
    const bool initialized=(mVehicleCubeCompletedMask&(1u<<face))!=0;
    VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=mVehicleCubeImage; barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel=0; barrier.subresourceRange.levelCount=1;
    barrier.subresourceRange.baseArrayLayer=face; barrier.subresourceRange.layerCount=1;
    vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
    int sourceSize=128;
    for(uint32_t level=1;level<8;++level)
    {
        barrier.srcAccessMask=initialized?VK_ACCESS_SHADER_READ_BIT:0;
        barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout=initialized?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                                      VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.subresourceRange.baseMipLevel=level;
        vkCmdPipelineBarrier(mCommandBuffer,initialized?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,0,NULL,0,NULL,1,&barrier);
        const int destinationSize=std::max(1,sourceSize/2);
        VkImageBlit blit={};
        blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level-1,face,1};
        blit.srcOffsets[1]={sourceSize,sourceSize,1};
        blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,face,1};
        blit.dstOffsets[1]={destinationSize,destinationSize,1};
        vkCmdBlitImage(mCommandBuffer,mVehicleCubeImage,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            mVehicleCubeImage,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_LINEAR);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        sourceSize=destinationSize;
    }
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.subresourceRange.baseMipLevel=0; barrier.subresourceRange.levelCount=8;
    vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier);
    mVehicleCubeCompletedMask|=1u<<face;
    mVehicleCubeReady=mVehicleCubeCompletedMask==0x3fu;
    mVehicleCubeCapture=false;
    SharOpenXR::SetMultiviewTargetActive(true);
}

bool VulkanContext::GetVehicleCubeMapTarget(VulkanEyeTarget* target) const
{
    if(!target || !mVehicleCubeCapture || !mVehicleCubeImage) return false;
    target->image=mVehicleCubeImage; target->format=VK_FORMAT_R8G8B8A8_UNORM;
    target->width=target->height=128; target->arrayLayer=mVehicleCubeFace;
    return true;
}

bool VulkanContext::BeginShadowCascade(uint32_t cascadeIndex,uint32_t size)
{
    if(!mPddiEyeActive || cascadeIndex>=3 || !size || mShadowPass) return false;
    EndActiveRenderPass();
    ShadowCascade& cascade=mShadowCascades[cascadeIndex];
    if(!cascade.image)
    {
        VkImageCreateInfo ii={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType=VK_IMAGE_TYPE_2D; ii.format=VK_FORMAT_D24_UNORM_S8_UINT;
        ii.extent={size,size,1}; ii.mipLevels=1; ii.arrayLayers=1;
        ii.samples=VK_SAMPLE_COUNT_1_BIT; ii.tiling=VK_IMAGE_TILING_OPTIMAL;
        ii.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ii.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        if(vkCreateImage(mDevice,&ii,NULL,&cascade.image)!=VK_SUCCESS) return false;
        VkMemoryRequirements req={}; vkGetImageMemoryRequirements(mDevice,cascade.image,&req);
        uint32_t type=0;
        if(!FindMemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&type)) return false;
        VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize=req.size; ai.memoryTypeIndex=type;
        if(vkAllocateMemory(mDevice,&ai,NULL,&cascade.memory)!=VK_SUCCESS ||
           vkBindImageMemory(mDevice,cascade.image,cascade.memory,0)!=VK_SUCCESS) return false;
        VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=cascade.image; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=ii.format;
        vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
        vi.subresourceRange.levelCount=vi.subresourceRange.layerCount=1;
        if(vkCreateImageView(mDevice,&vi,NULL,&cascade.view)!=VK_SUCCESS) return false;
        VkSamplerCreateInfo si={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter=si.minFilter=VK_FILTER_LINEAR;
        si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.compareEnable=VK_TRUE; si.compareOp=VK_COMPARE_OP_GREATER;
        si.minLod=si.maxLod=0.0f;
        if(vkCreateSampler(mDevice,&si,NULL,&cascade.sampler)!=VK_SUCCESS) return false;
        VkDescriptorImageInfo di={cascade.sampler,cascade.view,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet=mShadowDescriptorSet; write.dstBinding=cascadeIndex; write.descriptorCount=1;
        write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo=&di;
        vkUpdateDescriptorSets(mDevice,1,&write,0,NULL);
        VkAttachmentDescription attachment={}; attachment.format=ii.format;
        attachment.samples=VK_SAMPLE_COUNT_1_BIT; attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE; attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachment.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference depth={0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub={}; sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; sub.pDepthStencilAttachment=&depth;
        VkRenderPassCreateInfo ri={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        ri.attachmentCount=1; ri.pAttachments=&attachment; ri.subpassCount=1; ri.pSubpasses=&sub;
        if(vkCreateRenderPass(mDevice,&ri,NULL,&cascade.renderPass)!=VK_SUCCESS) return false;
        VkFramebufferCreateInfo fi={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass=cascade.renderPass; fi.attachmentCount=1; fi.pAttachments=&cascade.view;
        fi.width=fi.height=size; fi.layers=1;
        if(vkCreateFramebuffer(mDevice,&fi,NULL,&cascade.framebuffer)!=VK_SUCCESS) return false;
        cascade.size=size;
        SDL_Log("Vulkan CSM: cascade %u ready (%ux%u depth-only hardware PCF)",
                cascadeIndex,size,size);
    }
    VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask=cascade.initialized?VK_ACCESS_SHADER_READ_BIT:0;
    barrier.dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout=cascade.initialized?VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=cascade.image; barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.levelCount=barrier.subresourceRange.layerCount=1;
    vkCmdPipelineBarrier(mCommandBuffer,cascade.initialized?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,0,0,NULL,0,NULL,1,&barrier);
    VkClearValue clear={}; clear.depthStencil={1.0f,0};
    VkRenderPassBeginInfo begin={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass=cascade.renderPass; begin.framebuffer=cascade.framebuffer;
    begin.renderArea.extent={size,size}; begin.clearValueCount=1; begin.pClearValues=&clear;
    vkCmdBeginRenderPass(mCommandBuffer,&begin,VK_SUBPASS_CONTENTS_INLINE);
    mShadowPass=true; mShadowCascadeIndex=cascadeIndex;
    mShadowDrawCalls[cascadeIndex]=0;
    mShadowTriangles[cascadeIndex]=0;
    mBoundPipeline=VK_NULL_HANDLE; std::memset(mBoundTextureSets,0,sizeof(mBoundTextureSets));
    mBoundVertexBuffer=mBoundIndexBuffer=VK_NULL_HANDLE;
    mDynamicStateValid=false;
    return true;
}

bool VulkanContext::EndShadowCascade(uint32_t cascadeIndex)
{
    if(!mShadowPass || cascadeIndex!=mShadowCascadeIndex) return false;
    vkCmdEndRenderPass(mCommandBuffer);
    ShadowCascade& cascade=mShadowCascades[cascadeIndex];
    VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=cascade.image; barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.levelCount=barrier.subresourceRange.layerCount=1;
    vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,0,NULL,0,NULL,1,&barrier);
    cascade.initialized=true; mShadowPass=false;
    static uint32_t diagnosticPasses[3]={0,0,0};
    if(diagnosticPasses[cascadeIndex]++<8)
        SDL_Log("Vulkan CSM content c%u: draws=%u triangles=%llu",
                cascadeIndex,mShadowDrawCalls[cascadeIndex],
                static_cast<unsigned long long>(mShadowTriangles[cascadeIndex]));
    mBoundPipeline=VK_NULL_HANDLE; std::memset(mBoundTextureSets,0,sizeof(mBoundTextureSets));
    mBoundVertexBuffer=mBoundIndexBuffer=VK_NULL_HANDLE;
    mDynamicStateValid=false;
    return true;
}

void VulkanContext::SetShadowReceiverState(bool enabled,const float matrices[48])
{
    mShadowReceiverEnabled=enabled && mShadowCascades[0].initialized &&
        mShadowCascades[1].initialized && mShadowCascades[2].initialized;
    if(matrices) std::memcpy(mShadowReceiverMatrices,matrices,sizeof(mShadowReceiverMatrices));
}

bool VulkanContext::DrawShadowGeometry(VkBuffer vertexBuffer,VkBuffer indexBuffer,
    VkDeviceSize vertexOffset,uint32_t vertexCount,uint32_t indexCount,
    VkPrimitiveTopology topology,const float* projection,const float* modelview,
    VkDescriptorSet textureSet,const VulkanMaterialState& material)
{
    if(!mShadowPass || !vertexBuffer || !vertexCount) return false;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkPipelineLayout shadowLayout=VK_NULL_HANDLE;
    if(!mShadowPipeline.GetOrCreate(mDevice,mPipelineCache,
        mShadowCascades[mShadowCascadeIndex].renderPass,mTextureSetLayout,
        mDrawSetLayout,mShadowSetLayout,topology,&pipeline,&shadowLayout)) return false;
    if(mBoundPipeline!=pipeline) { vkCmdBindPipeline(mCommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline); mBoundPipeline=pipeline; }
    const float size=static_cast<float>(mShadowCascades[mShadowCascadeIndex].size);
    // Pure3D supplies OpenGL-oriented clip coordinates. Match the eye pass
    // with a negative-height viewport; receiver shaders explicitly undo this
    // framebuffer Y flip when converting light clip coordinates to UV.
    VkViewport viewport={0,size,size,-size,0,1};
    VkRect2D scissor={{0,0},{(uint32_t)size,(uint32_t)size}};
    vkCmdSetViewport(mCommandBuffer,0,1,&viewport); vkCmdSetScissor(mCommandBuffer,0,1,&scissor);
    VkDescriptorSet diffuse=textureSet?textureSet:mFallbackDescriptorSet;
    vkCmdBindDescriptorSets(mCommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,shadowLayout,0,1,&diffuse,0,NULL);
    float constants[704]={}; constants[20]=material.alphaTest?material.alphaRef:-1.0f;
    constants[184]=static_cast<float>(material.skinMatrixCount);
    std::memcpy(constants+185,material.enhancedSunDirection,sizeof(float)*3);
    for(unsigned i=0;i<material.skinMatrixCount && i<VulkanMaterialState::MaxSkinMatrices;++i)
        std::memcpy(constants+188+i*16,material.skinMatrices[i],sizeof(float)*16);
    const VkDeviceSize drawOffset=(mDrawUniformOffset+mDrawUniformAlignment-1)&~(mDrawUniformAlignment-1);
    if(drawOffset+sizeof(constants)>mDrawUniformEnd) return false;
    std::memcpy((unsigned char*)mDrawUniformMapped+drawOffset,constants,sizeof(constants)); mDrawUniformOffset=drawOffset+sizeof(constants);
    const uint32_t dynamicOffset=(uint32_t)drawOffset;
    vkCmdBindDescriptorSets(mCommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,shadowLayout,1,1,&mDrawDescriptorSet,1,&dynamicOffset);
    float mvp[16]={};
    for(unsigned c=0;c<4;++c) for(unsigned r=0;r<4;++r) for(unsigned k=0;k<4;++k)
        mvp[c*4+r]+=projection[k*4+r]*modelview[c*4+k];
    vkCmdPushConstants(mCommandBuffer,shadowLayout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(mvp),mvp);
    vkCmdBindVertexBuffers(mCommandBuffer,0,1,&vertexBuffer,&vertexOffset);
    if(indexBuffer&&indexCount) { vkCmdBindIndexBuffer(mCommandBuffer,indexBuffer,0,VK_INDEX_TYPE_UINT16); vkCmdDrawIndexed(mCommandBuffer,indexCount,1,0,0,0); }
    else vkCmdDraw(mCommandBuffer,vertexCount,1,0,0);
    ++mShadowDrawCalls[mShadowCascadeIndex];
    const uint32_t primitiveVertices=indexBuffer&&indexCount?indexCount:vertexCount;
    if(topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        mShadowTriangles[mShadowCascadeIndex]+=primitiveVertices/3;
    else if(topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP && primitiveVertices>=3)
        mShadowTriangles[mShadowCascadeIndex]+=primitiveVertices-2;
    return true;
}

bool VulkanContext::DrawPddiGeometry(VkImage image,VkFormat format,
                                     uint32_t width,uint32_t height,
                                     uint32_t arrayLayer,VkBuffer vertexBuffer,
                                     VkBuffer indexBuffer,VkDeviceSize vertexOffset,uint32_t vertexCount,
                                     uint32_t indexCount,VkPrimitiveTopology topology,
                                     const float* projection,const float* modelview,
                                     VkDescriptorSet textureSet,VkDescriptorSet reflectionSet,
                                     VkDescriptorSet topTextureSet,VkDescriptorSet lightMapSet,
                                     const VulkanMaterialState& material)
{
    if(mShadowPass)
        return DrawShadowGeometry(vertexBuffer,indexBuffer,vertexOffset,vertexCount,indexCount,
            topology,projection,modelview,textureSet,material);
    if(!IsInitialized() || !image || !width || !height || !vertexBuffer ||
       !vertexCount || !projection || !modelview) return false;
    // Eye targets use layer 2 as the historical "render both views" marker.
    // Cubemap +Y is also physical layer 2, but it must remain a normal mono
    // attachment or Vulkan writes layers 0/1 and leaves +Y as clear sky.
    const bool vehicleCubeTarget=mVehicleCubeCapture && image==mVehicleCubeImage;
    const bool multiview=!vehicleCubeTarget && arrayLayer==2 && mMultiviewSupported;
    const bool foveated=multiview && mFragmentDensityMapSupported &&
                         mFragmentDensityMap!=VK_NULL_HANDLE;
    CachedDepthTarget* depthTarget=NULL;
    for(CachedDepthTarget& target:mDepthTargetCache)
        if(target.colourImage==image && target.arrayLayer==arrayLayer &&
           target.width==width && target.height==height)
        { depthTarget=&target; break; }
    if(!depthTarget)
    {
        CachedDepthTarget target={}; target.colourImage=image;
        target.arrayLayer=arrayLayer; target.width=width; target.height=height;
        VkImageCreateInfo di={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        di.imageType=VK_IMAGE_TYPE_2D; di.format=VK_FORMAT_D24_UNORM_S8_UINT;
        di.extent={width,height,1}; di.mipLevels=1; di.arrayLayers=multiview?2u:1u;
        di.samples=VK_SAMPLE_COUNT_1_BIT; di.tiling=VK_IMAGE_TILING_OPTIMAL;
        di.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if(!multiview) di.usage|=VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        di.sharingMode=VK_SHARING_MODE_EXCLUSIVE; di.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        if(vkCreateImage(mDevice,&di,NULL,&target.image)!=VK_SUCCESS) return false;
        VkMemoryRequirements requirements={};
        vkGetImageMemoryRequirements(mDevice,target.image,&requirements);
        uint32_t type=0;
        const VkMemoryPropertyFlags preferredMemory=multiview?
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT:VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
        if(!FindMemoryType(requirements.memoryTypeBits,preferredMemory,&type) &&
           !FindMemoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&type))
        { vkDestroyImage(mDevice,target.image,NULL); return false; }
        VkMemoryAllocateInfo allocation={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize=requirements.size; allocation.memoryTypeIndex=type;
        if(vkAllocateMemory(mDevice,&allocation,NULL,&target.memory)!=VK_SUCCESS ||
           vkBindImageMemory(mDevice,target.image,target.memory,0)!=VK_SUCCESS)
        {
            if(target.memory) vkFreeMemory(mDevice,target.memory,NULL);
            vkDestroyImage(mDevice,target.image,NULL); return false;
        }
        VkImageViewCreateInfo dvi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        dvi.image=target.image; dvi.viewType=multiview?VK_IMAGE_VIEW_TYPE_2D_ARRAY:
                                                    VK_IMAGE_VIEW_TYPE_2D;
        dvi.format=VK_FORMAT_D24_UNORM_S8_UINT;
        dvi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
        dvi.subresourceRange.levelCount=1;
        dvi.subresourceRange.layerCount=multiview?2u:1u;
        if(vkCreateImageView(mDevice,&dvi,NULL,&target.view)!=VK_SUCCESS)
        { vkFreeMemory(mDevice,target.memory,NULL); vkDestroyImage(mDevice,target.image,NULL); return false; }
        mDepthTargetCache.push_back(target);
        depthTarget=&mDepthTargetCache.back();
    }
    VkImageView view=VK_NULL_HANDLE,densityView=VK_NULL_HANDLE;
    VkRenderPass renderPass=VK_NULL_HANDLE;
    VkRenderPass clearRenderPass=VK_NULL_HANDLE;
    VkRenderPass depthClearRenderPass=VK_NULL_HANDLE;
    VkFramebuffer framebuffer=VK_NULL_HANDLE;
    VkShaderModule vertexModule=VK_NULL_HANDLE,fragmentModule=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE; VkPipeline pipeline=VK_NULL_HANDLE;
    bool success=false,cached=false;
    // Match GLES program selection by active material features. Variant 1 is
    // the minimal unlit path, variant 2 keeps lighting/fog but drops all
    // layer/reflection varyings and samplers, and variant 0 is the full path.
    const MaterialPipelineSelection materialPipeline=
        SelectMaterialPipeline(material,mShadowReceiverEnabled);
    const uint8_t shaderVariant=materialPipeline.shaderVariant;
    const uint8_t materialModel=materialPipeline.materialModel;
    const bool enhancedShading=materialPipeline.technology!=MaterialTechnology::Legacy &&
                               material.enhancedMaterialProfile!=0;
    ++mShaderVariantDraws[shaderVariant];
    const VkCullModeFlags effectiveCull=material.twoSided?VK_CULL_MODE_NONE:material.cullMode;
    uint64_t stateHash=1469598103934665603ull;
    const auto hashStateValue=[&](uint64_t value)
    {
        stateHash^=value;
        stateHash*=1099511628211ull;
    };
    hashStateValue(reinterpret_cast<uintptr_t>(image));
    hashStateValue(reinterpret_cast<uintptr_t>(foveated?mFragmentDensityMap:VK_NULL_HANDLE));
    hashStateValue(static_cast<uint64_t>(format));
    hashStateValue(width); hashStateValue(height); hashStateValue(arrayLayer);
    hashStateValue(static_cast<uint64_t>(topology));
    hashStateValue(material.blendMode); hashStateValue(effectiveCull);
    hashStateValue(material.colourWriteMask);
    hashStateValue(material.depthTest); hashStateValue(material.depthWrite);
    hashStateValue(shaderVariant); hashStateValue(materialModel);
    hashStateValue(material.alphaTest); hashStateValue(material.depthBias!=0.0f);
    hashStateValue(material.depthCompare); hashStateValue(material.stencilTest);
    hashStateValue(material.stencilCompare); hashStateValue(material.stencilFail);
    hashStateValue(material.stencilDepthFail); hashStateValue(material.stencilPass);

    const auto stateMatches=[&](const CachedDrawState& state)
    {
        return state.image==image &&
           state.densityImage==(foveated?mFragmentDensityMap:VK_NULL_HANDLE) &&
           state.format==format && state.width==width &&
           state.height==height && state.arrayLayer==arrayLayer &&
           state.topology==topology && state.blendMode==material.blendMode &&
           state.cullMode==effectiveCull &&
           state.colourWriteMask==material.colourWriteMask &&
           state.depthTest==material.depthTest && state.depthWrite==material.depthWrite &&
           state.shaderVariant==shaderVariant &&
           state.materialModel==materialModel &&
           state.alphaTest==material.alphaTest &&
           state.depthBiasEnabled==(material.depthBias!=0.0f) &&
           state.depthCompare==material.depthCompare &&
           state.stencilTest==material.stencilTest &&
           state.stencilCompare==material.stencilCompare &&
           state.stencilFail==material.stencilFail &&
           state.stencilDepthFail==material.stencilDepthFail &&
           state.stencilPass==material.stencilPass;
    };
    const auto selectState=[&](const CachedDrawState& state,size_t index)
    {
        view=state.view; densityView=state.densityView;
        renderPass=state.renderPass; clearRenderPass=state.clearRenderPass;
        depthClearRenderPass=state.depthClearRenderPass;
        framebuffer=state.framebuffer; vertexModule=state.vertexModule;
        fragmentModule=state.fragmentModule; layout=state.layout;
        pipeline=state.pipeline; cached=true; mLastDrawStateIndex=index;
    };
    if(mLastDrawStateIndex<mDrawStateCache.size() &&
       stateMatches(mDrawStateCache[mLastDrawStateIndex]))
        selectState(mDrawStateCache[mLastDrawStateIndex],mLastDrawStateIndex);
    else
    {
        const auto found=mDrawStateLookup.find(stateHash);
        if(found!=mDrawStateLookup.end() && found->second<mDrawStateCache.size() &&
           stateMatches(mDrawStateCache[found->second]))
            selectState(mDrawStateCache[found->second],found->second);
    }
    // A 64-bit collision is extraordinarily unlikely, but preserve exact
    // correctness and repair the lookup if one is ever encountered.
    if(!cached)
        for(size_t i=0;i<mDrawStateCache.size();++i)
        {
            const CachedDrawState& state=mDrawStateCache[i];
            if(stateMatches(state))
            {
                selectState(state,i);
                mDrawStateLookup[stateHash]=i;
                break;
            }
        }

    do {
    if(!cached)
    {
    VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image=image; vi.viewType=multiview?VK_IMAGE_VIEW_TYPE_2D_ARRAY:
                                       VK_IMAGE_VIEW_TYPE_2D; vi.format=format;
    vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount=1;
    vi.subresourceRange.baseArrayLayer=multiview?0u:arrayLayer;
    vi.subresourceRange.layerCount=multiview?2u:1u;
    if(vkCreateImageView(mDevice,&vi,NULL,&view)!=VK_SUCCESS) break;
    if(foveated)
    {
        VkImageViewCreateInfo densityVi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        densityVi.image=mFragmentDensityMap;
        densityVi.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        densityVi.format=VK_FORMAT_R8G8_UNORM;
        densityVi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        densityVi.subresourceRange.levelCount=1;
        densityVi.subresourceRange.layerCount=2;
        if(vkCreateImageView(mDevice,&densityVi,NULL,&densityView)!=VK_SUCCESS) break;
    }
    VkAttachmentDescription attachments[3]={}; attachments[0].format=format;
    attachments[0].samples=VK_SAMPLE_COUNT_1_BIT; attachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].format=VK_FORMAT_D24_UNORM_S8_UINT;
    attachments[1].samples=VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    // Per-eye GUI depth is never consumed after that layer finishes. Keep it
    // tile-local on Adreno; only the multiview world target may need to
    // survive an offscreen render-pass interruption.
    attachments[1].storeOp=multiview?VK_ATTACHMENT_STORE_OP_STORE:
                                          VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].stencilStoreOp=multiview?VK_ATTACHMENT_STORE_OP_STORE:
                                                 VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if(foveated)
    {
        attachments[2].format=VK_FORMAT_R8G8_UNORM;
        attachments[2].samples=VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].initialLayout=VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;
        attachments[2].finalLayout=VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;
    }
    VkAttachmentReference colourRef={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef={1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass={}; subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount=1; subpass.pColorAttachments=&colourRef;
    subpass.pDepthStencilAttachment=&depthRef;
    VkRenderPassCreateInfo rpi={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount=foveated?3u:2u; rpi.pAttachments=attachments;
    rpi.subpassCount=1; rpi.pSubpasses=&subpass;
    const uint32_t viewMask=0x3,correlationMask=0x3;
    VkRenderPassMultiviewCreateInfo multiviewInfo={
        VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO };
    VkAttachmentReference densityRef={2,VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT};
    VkRenderPassFragmentDensityMapCreateInfoEXT densityInfo={
        VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT };
    densityInfo.fragmentDensityMapAttachment=densityRef;
    if(multiview)
    {
        multiviewInfo.subpassCount=1;
        multiviewInfo.pViewMasks=&viewMask;
        multiviewInfo.correlationMaskCount=1;
        multiviewInfo.pCorrelationMasks=&correlationMask;
        multiviewInfo.pNext=foveated?&densityInfo:NULL;
        rpi.pNext=&multiviewInfo;
    }
    else if(foveated) rpi.pNext=&densityInfo;
    if(vkCreateRenderPass(mDevice,&rpi,NULL,&renderPass)!=VK_SUCCESS) break;
    VkAttachmentDescription clearAttachments[3]={attachments[0],attachments[1],attachments[2]};
    clearAttachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    clearAttachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    clearAttachments[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    clearAttachments[1].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkRenderPassCreateInfo clearRpi=rpi;
    clearRpi.pAttachments=clearAttachments;
    if(vkCreateRenderPass(mDevice,&clearRpi,NULL,&clearRenderPass)!=VK_SUCCESS) break;
    VkAttachmentDescription depthClearAttachments[3]={attachments[0],attachments[1],attachments[2]};
    depthClearAttachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthClearAttachments[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthClearAttachments[1].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkRenderPassCreateInfo depthClearRpi=rpi;
    depthClearRpi.pAttachments=depthClearAttachments;
    if(vkCreateRenderPass(mDevice,&depthClearRpi,NULL,&depthClearRenderPass)!=VK_SUCCESS) break;
    VkFramebufferCreateInfo fi={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    VkImageView framebufferAttachments[3]={view,depthTarget->view,densityView};
    fi.renderPass=renderPass; fi.attachmentCount=foveated?3u:2u;
    fi.pAttachments=framebufferAttachments;
    fi.width=width; fi.height=height; fi.layers=1;
    if(vkCreateFramebuffer(mDevice,&fi,NULL,&framebuffer)!=VK_SUCCESS) break;
    VkShaderModuleCreateInfo smi={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    const ShaderBinary vertexShader=GetMaterialVertexShader(materialPipeline,multiview);
    smi.codeSize=vertexShader.bytes; smi.pCode=vertexShader.words;
    if(vkCreateShaderModule(mDevice,&smi,NULL,&vertexModule)!=VK_SUCCESS) break;
    const ShaderBinary fragmentShader=GetMaterialFragmentShader(materialPipeline);
    smi.codeSize=fragmentShader.bytes; smi.pCode=fragmentShader.words;
    if(vkCreateShaderModule(mDevice,&smi,NULL,&fragmentModule)!=VK_SUCCESS) break;
    VkPipelineShaderStageCreateInfo stages[2]={};
    stages[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=vertexModule; stages[0].pName="main";
    stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=fragmentModule; stages[1].pName="main";
    const VkBool32 alphaTestSpecialization=material.alphaTest?VK_TRUE:VK_FALSE;
    const VkSpecializationMapEntry alphaTestEntry={0,0,sizeof(alphaTestSpecialization)};
    const VkSpecializationInfo alphaTestInfo={1,&alphaTestEntry,
        sizeof(alphaTestSpecialization),&alphaTestSpecialization};
    stages[1].pSpecializationInfo=&alphaTestInfo;
    VkVertexInputBindingDescription binding={0,80,VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[8]={
        {0,0,VK_FORMAT_R32G32B32_SFLOAT,0},
        {1,0,VK_FORMAT_R32G32B32_SFLOAT,12},
        {2,0,VK_FORMAT_R32G32_SFLOAT,24},
        {3,0,VK_FORMAT_R8G8B8A8_UNORM,32},
        {4,0,VK_FORMAT_R32G32_SFLOAT,36},
        {5,0,VK_FORMAT_R32G32_SFLOAT,44},
        {6,0,VK_FORMAT_R32G32B32_SFLOAT,52},
        {7,0,VK_FORMAT_R32G32B32A32_UINT,64}
    };
    VkPipelineVertexInputStateCreateInfo vertexInput={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount=1; vertexInput.pVertexBindingDescriptions=&binding;
    vertexInput.vertexAttributeDescriptionCount=8; vertexInput.pVertexAttributeDescriptions=attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology=topology;
    // Eye images need the OpenGL-to-Vulkan Y correction. Cubemap face camera
    // up-vectors already follow the original GLES cubemap convention, so a
    // second viewport flip would store every face upside down.
    VkViewport viewport=vehicleCubeTarget?
        VkViewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1}:
        VkViewport{0,static_cast<float>(height),static_cast<float>(width),
                   -static_cast<float>(height),0,1};
    VkRect2D scissor={{0,0},{width,height}};
    VkPipelineViewportStateCreateInfo viewportState={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount=1; viewportState.pViewports=&viewport;
    viewportState.scissorCount=1; viewportState.pScissors=&scissor;
    VkPipelineRasterizationStateCreateInfo raster={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode=VK_POLYGON_MODE_FILL;
    raster.cullMode=effectiveCull;
    raster.depthBiasEnable=material.depthBias!=0.0f?VK_TRUE:VK_FALSE;
    // Negative viewport height reverses winding. Cubemap faces use a positive
    // height and therefore retain the original counter-clockwise convention.
    raster.frontFace=vehicleCubeTarget?VK_FRONT_FACE_COUNTER_CLOCKWISE:
                                         VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthState={VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthState.depthTestEnable=material.depthTest?VK_TRUE:VK_FALSE;
    depthState.depthWriteEnable=material.depthWrite?VK_TRUE:VK_FALSE;
    depthState.depthCompareOp=material.depthCompare;
    depthState.stencilTestEnable=material.stencilTest?VK_TRUE:VK_FALSE;
    VkStencilOpState stencilState={};
    stencilState.failOp=material.stencilFail;
    stencilState.depthFailOp=material.stencilDepthFail;
    stencilState.passOp=material.stencilPass;
    stencilState.compareOp=material.stencilCompare;
    stencilState.compareMask=material.stencilCompareMask;
    stencilState.writeMask=material.stencilWriteMask;
    stencilState.reference=material.stencilReference;
    depthState.front=depthState.back=stencilState;
    VkPipelineColorBlendAttachmentState ba={};
    ba.colorWriteMask=material.colourWriteMask;
    if(material.blendMode!=0)
    {
        ba.blendEnable=VK_TRUE;
        ba.colorBlendOp=ba.alphaBlendOp=VK_BLEND_OP_ADD;
        switch(material.blendMode)
        {
            case 1: ba.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
                    ba.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
            case 2: ba.srcColorBlendFactor=VK_BLEND_FACTOR_ONE;
                    ba.dstColorBlendFactor=VK_BLEND_FACTOR_ONE; break;
            case 3: ba.colorBlendOp=VK_BLEND_OP_REVERSE_SUBTRACT;
                    ba.srcColorBlendFactor=VK_BLEND_FACTOR_ONE;
                    ba.dstColorBlendFactor=VK_BLEND_FACTOR_ONE; break;
            case 4: ba.srcColorBlendFactor=VK_BLEND_FACTOR_DST_COLOR;
                    ba.dstColorBlendFactor=VK_BLEND_FACTOR_ZERO; break;
            case 5: ba.srcColorBlendFactor=VK_BLEND_FACTOR_DST_COLOR;
                    ba.dstColorBlendFactor=VK_BLEND_FACTOR_SRC_COLOR; break;
            case 6: ba.srcColorBlendFactor=VK_BLEND_FACTOR_ONE;
                    ba.dstColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA; break;
            case 7: ba.colorBlendOp=VK_BLEND_OP_REVERSE_SUBTRACT;
                    ba.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
                    ba.dstColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA; break;
            case 8: ba.srcColorBlendFactor=VK_BLEND_FACTOR_DST_ALPHA;
                    ba.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA; break;
            default: ba.blendEnable=VK_FALSE; break;
        }
        // GLES uses glBlendFunc/glBlendEquation, not the Separate variants:
        // alpha follows the exact same operation and factors as RGB.
        ba.srcAlphaBlendFactor=ba.srcColorBlendFactor;
        ba.dstAlphaBlendFactor=ba.dstColorBlendFactor;
        ba.alphaBlendOp=ba.colorBlendOp;
    }
    VkPipelineColorBlendStateCreateInfo blend={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount=1; blend.pAttachments=&ba;
    const VkDynamicState dynamicStates[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_DEPTH_BIAS,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK};
    VkPipelineDynamicStateCreateInfo dynamic={VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount=6; dynamic.pDynamicStates=dynamicStates;
    VkPipelineLayoutCreateInfo pli={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkDescriptorSetLayout drawLayouts[8]={mTextureSetLayout,mDrawSetLayout,mTextureSetLayout,
        mTextureSetLayout,mTextureSetLayout,mShadowSetLayout,mTextureSetLayout,mTextureSetLayout};
    pli.setLayoutCount=8; pli.pSetLayouts=drawLayouts;
    VkPushConstantRange transformRange={VK_SHADER_STAGE_VERTEX_BIT,0,128};
    pli.pushConstantRangeCount=1; pli.pPushConstantRanges=&transformRange;
    if(vkCreatePipelineLayout(mDevice,&pli,NULL,&layout)!=VK_SUCCESS) break;
    VkGraphicsPipelineCreateInfo pi={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.stageCount=2; pi.pStages=stages; pi.pVertexInputState=&vertexInput;
    pi.pInputAssemblyState=&assembly; pi.pViewportState=&viewportState;
    pi.pRasterizationState=&raster; pi.pMultisampleState=&ms;
    pi.pDepthStencilState=&depthState; pi.pColorBlendState=&blend;
    pi.pDynamicState=&dynamic;
    pi.layout=layout; pi.renderPass=renderPass;
    if(vkCreateGraphicsPipelines(mDevice,mPipelineCache,1,&pi,NULL,&pipeline)!=VK_SUCCESS) break;
    }
    if(mPipelinePrewarm) { success=true; break; }
    const bool batched=mPddiEyeActive;
    if(!batched)
    {
        vkResetFences(mDevice,1,&mFence); vkResetCommandBuffer(mCommandBuffer,0);
        VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if(vkBeginCommandBuffer(mCommandBuffer,&begin)!=VK_SUCCESS) break;
    }
    const uint32_t drawLayerMask=multiview?0x3u:(1u<<arrayLayer);
    const bool colourNeedsClear=(mColourClearMask&drawLayerMask)!=0;
    const bool depthNeedsClear=depthTarget->clearedSerial!=mEyeSerial;
    if(!batched || !mPddiRenderPassActive)
    {
        VkRenderPassBeginInfo rb={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        VkClearValue clearValues[2]={};
        clearValues[0].color={{0.0f,0.0f,0.0f,1.0f}};
        clearValues[1].depthStencil={1.0f,0};
        rb.renderPass=colourNeedsClear?clearRenderPass:
                      depthNeedsClear?depthClearRenderPass:renderPass;
        rb.framebuffer=framebuffer; rb.renderArea.extent={width,height};
        if(colourNeedsClear || depthNeedsClear)
        {
            rb.clearValueCount=2; rb.pClearValues=clearValues;
        }
        if(depthNeedsClear || colourNeedsClear)
        {
            depthTarget->initialized=true;
            depthTarget->clearedSerial=mEyeSerial;
        }
        if(colourNeedsClear)
        {
            mColourClearMask&=~drawLayerMask;
        }
        vkCmdBeginRenderPass(mCommandBuffer,&rb,VK_SUBPASS_CONTENTS_INLINE);
        if(batched) mPddiRenderPassActive=true;
    }
    if(mBoundPipeline!=pipeline)
    {
        vkCmdBindPipeline(mCommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        mBoundPipeline=pipeline;
    }
    VkViewport drawViewport={};
    drawViewport.x=material.viewportLeft*width;
    const float viewportBottom=material.viewportTop+material.viewportHeight;
    drawViewport.y=vehicleCubeTarget?material.viewportTop*height:
                                       viewportBottom*height;
    drawViewport.width=std::max(1.0f,material.viewportWidth*width);
    drawViewport.height=(vehicleCubeTarget?1.0f:-1.0f)*
        std::max(1.0f,material.viewportHeight*height);
    drawViewport.minDepth=0.0f; drawViewport.maxDepth=1.0f;
    VkRect2D drawScissor={};
    // Pure3D's scissor is expressed in the logical display resolution, while
    // OpenXR swapchain images use the headset's native per-eye resolution.
    // Scale both the origin and extent or an otherwise full-screen game view
    // is clipped into a small rectangle in the upper-left corner.
    const float scissorScaleX=static_cast<float>(width)/
        static_cast<float>(std::max(1u,material.scissorSurfaceWidth));
    const float scissorScaleY=static_cast<float>(height)/
        static_cast<float>(std::max(1u,material.scissorSurfaceHeight));
    drawScissor.offset.x=std::max(0,static_cast<int32_t>(material.scissorX*scissorScaleX));
    drawScissor.offset.y=std::max(0,static_cast<int32_t>(material.scissorY*scissorScaleY));
    const uint32_t sx=static_cast<uint32_t>(drawScissor.offset.x);
    const uint32_t sy=static_cast<uint32_t>(drawScissor.offset.y);
    const uint32_t scaledScissorWidth=std::max(1u,static_cast<uint32_t>(
        material.scissorWidth*scissorScaleX+0.5f));
    const uint32_t scaledScissorHeight=std::max(1u,static_cast<uint32_t>(
        material.scissorHeight*scissorScaleY+0.5f));
    drawScissor.extent.width=sx<width?std::min(width-sx,scaledScissorWidth):0;
    drawScissor.extent.height=sy<height?std::min(height-sy,scaledScissorHeight):0;
    if(!mDynamicStateValid || std::memcmp(&mBoundViewport,&drawViewport,
                                          sizeof(drawViewport))!=0)
    {
        vkCmdSetViewport(mCommandBuffer,0,1,&drawViewport);
        mBoundViewport=drawViewport;
    }
    if(!mDynamicStateValid || std::memcmp(&mBoundScissor,&drawScissor,
                                          sizeof(drawScissor))!=0)
    {
        vkCmdSetScissor(mCommandBuffer,0,1,&drawScissor);
        mBoundScissor=drawScissor;
    }
    if(!mDynamicStateValid || mBoundDepthBias!=material.depthBias)
    {
        vkCmdSetDepthBias(mCommandBuffer,material.depthBias,0.0f,material.depthBias);
        mBoundDepthBias=material.depthBias;
    }
    if(!mDynamicStateValid || mBoundStencilReference!=material.stencilReference)
    {
        vkCmdSetStencilReference(mCommandBuffer,VK_STENCIL_FACE_FRONT_AND_BACK,
                                 material.stencilReference);
        mBoundStencilReference=material.stencilReference;
    }
    if(!mDynamicStateValid || mBoundStencilCompareMask!=material.stencilCompareMask)
    {
        vkCmdSetStencilCompareMask(mCommandBuffer,VK_STENCIL_FACE_FRONT_AND_BACK,
                                   material.stencilCompareMask);
        mBoundStencilCompareMask=material.stencilCompareMask;
    }
    if(!mDynamicStateValid || mBoundStencilWriteMask!=material.stencilWriteMask)
    {
        vkCmdSetStencilWriteMask(mCommandBuffer,VK_STENCIL_FACE_FRONT_AND_BACK,
                                 material.stencilWriteMask);
        mBoundStencilWriteMask=material.stencilWriteMask;
    }
    mDynamicStateValid=true;
    const VkDescriptorSet selectedTexture=textureSet?textureSet:mFallbackDescriptorSet;
    if(mBoundTextureSets[0]!=selectedTexture)
    {
        vkCmdBindDescriptorSets(mCommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,
                                0,1,&selectedTexture,0,NULL);
        mBoundTextureSets[0]=selectedTexture;
    }
    // Full materials and base-lit PBR both consume the selected environment.
    // Other base-lit draws still avoid this descriptor bind.
    if(materialPipeline.usesEnvironment)
    {
        const VkDescriptorSet selectedReflection=reflectionSet?reflectionSet:mFallbackDescriptorSet;
        if(mBoundTextureSets[1]!=selectedReflection)
        {
            vkCmdBindDescriptorSets(mCommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,
                                    2,1,&selectedReflection,0,NULL);
            mBoundTextureSets[1]=selectedReflection;
        }
    }
    // Only the full shader statically references layered/light-map sets 3..4.
    if(materialPipeline.usesLayers)
    {
        const VkDescriptorSet selectedTop=topTextureSet?topTextureSet:mFallbackDescriptorSet;
        if(mBoundTextureSets[2]!=selectedTop)
        {
            vkCmdBindDescriptorSets(mCommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,
                                    3,1,&selectedTop,0,NULL);
            mBoundTextureSets[2]=selectedTop;
        }
        const VkDescriptorSet selectedLightMap=lightMapSet?lightMapSet:mFallbackDescriptorSet;
        if(mBoundTextureSets[3]!=selectedLightMap)
        {
            vkCmdBindDescriptorSets(mCommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,
                                    4,1,&selectedLightMap,0,NULL);
            mBoundTextureSets[3]=selectedLightMap;
        }
    }
    if(materialPipeline.usesShadows && mBoundTextureSets[4]!=mShadowDescriptorSet)
    {
        vkCmdBindDescriptorSets(mCommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,
            5,1,&mShadowDescriptorSet,0,NULL);
        mBoundTextureSets[4]=mShadowDescriptorSet;
    }
    const VkDescriptorSet selectedPbr=material.pbrTextureSet?
        material.pbrTextureSet:mFallbackPbrDescriptorSet;
    if(material.pbrMapFlags && material.pbrDebugMode)
    {
        static VkDescriptorSet loggedPbr=VK_NULL_HANDLE;
        if(loggedPbr!=material.pbrTextureSet)
        {
            SDL_Log("Vulkan PBR draw descriptors: custom=%p fallback=%p selected=%p flags=0x%x debug=%u",
                reinterpret_cast<void*>(material.pbrTextureSet),
                reinterpret_cast<void*>(mFallbackPbrDescriptorSet),
                reinterpret_cast<void*>(selectedPbr),material.pbrMapFlags,
                material.pbrDebugMode);
            loggedPbr=material.pbrTextureSet;
        }
    }
    // Material maps use set 6. Set 7 returned zeroes on the target Adreno even
    // for the one-pixel fallback texture, so reserve the last slot for the
    // optional cube probe and keep essential material data on the proven slot.
    if(materialPipeline.usesPbrMaps &&
       (material.enhancedMaterialModel==2 || mBoundTextureSets[5]!=selectedPbr))
    {
        vkCmdBindDescriptorSets(mCommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,
            6,1,&selectedPbr,0,NULL);
        mBoundTextureSets[5]=selectedPbr;
    }
    const VkDescriptorSet selectedVehicleCube=mVehicleCubeDescriptor?
        mVehicleCubeDescriptor:mFallbackDescriptorSet;
    if(materialPipeline.usesCubeMap && mBoundTextureSets[6]!=selectedVehicleCube)
    {
        vkCmdBindDescriptorSets(mCommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,
            7,1,&selectedVehicleCube,0,NULL);
        mBoundTextureSets[6]=selectedVehicleCube;
    }
    if(mBoundVertexBuffer!=vertexBuffer || mBoundVertexOffset!=vertexOffset)
    {
        vkCmdBindVertexBuffers(mCommandBuffer,0,1,&vertexBuffer,&vertexOffset);
        mBoundVertexBuffer=vertexBuffer; mBoundVertexOffset=vertexOffset;
    }
    float transformConstants[32]={};
    for(unsigned column=0;column<4;++column)
        for(unsigned row=0;row<4;++row)
            for(unsigned k=0;k<4;++k)
                transformConstants[column*4+row]+=projection[k*4+row]*modelview[column*4+k];
    std::memcpy(transformConstants+16,transformConstants,sizeof(float)*16);
    rmt::Matrix normalMatrix;
    normalMatrix.Identity();
    if(material.lit || material.reflectionEnabled || enhancedShading)
    {
        rmt::Matrix modelViewMatrix;
        std::memcpy(modelViewMatrix.m[0],modelview,sizeof(float)*16);
        normalMatrix.Invert(modelViewMatrix);
        normalMatrix.Transpose();
    }
    float constants[FullMaterialUniformFloatCount];
    const MaterialUniformInput uniformInput={material,modelview,normalMatrix.m[0],
        mShadowReceiverMatrices,mShadowReceiverEnabled,
        format==VK_FORMAT_R8G8B8A8_SRGB || format==VK_FORMAT_B8G8R8A8_SRGB,
        mVehicleCubeReady && !mVehicleCubeCapture &&
            SharOpenXR::GetReflectionMode()==2};
    const size_t materialUniformSize=
        PackMaterialUniforms(materialPipeline,uniformInput,constants);
    if(multiview)
    {
        rmt::Matrix stereoProjection[2],viewAdjustment[2];
        if(SharOpenXR::GetMultiviewMatrices(stereoProjection,viewAdjustment))
        {
            const auto multiply=[](const float* a,const float* b,float* out)
            {
                for(unsigned column=0;column<4;++column)
                    for(unsigned row=0;row<4;++row)
                    {
                        out[column*4+row]=0.0f;
                        for(unsigned k=0;k<4;++k)
                            out[column*4+row]+=a[k*4+row]*b[column*4+k];
                    }
            };
            float adjusted[16];
            multiply(viewAdjustment[0].m[0],modelview,adjusted);
            multiply(stereoProjection[0].m[0],adjusted,transformConstants);
            multiply(viewAdjustment[1].m[0],modelview,adjusted);
            multiply(stereoProjection[1].m[0],adjusted,transformConstants+16);
        }
    }
    const VkDeviceSize drawOffset=(mDrawUniformOffset+mDrawUniformAlignment-1)&
                                  ~(mDrawUniformAlignment-1);
    if(drawOffset+materialUniformSize>mDrawUniformEnd) break;
    std::memcpy(static_cast<unsigned char*>(mDrawUniformMapped)+drawOffset,
                constants,materialUniformSize);
    mDrawUniformOffset=drawOffset+materialUniformSize;
    const uint32_t dynamicOffset=static_cast<uint32_t>(drawOffset);
    vkCmdBindDescriptorSets(mCommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,
                            1,1,&mDrawDescriptorSet,1,&dynamicOffset);
    vkCmdPushConstants(mCommandBuffer,layout,VK_SHADER_STAGE_VERTEX_BIT,0,
                       sizeof(transformConstants),transformConstants);
    if(indexBuffer && indexCount)
    {
        if(mBoundIndexBuffer!=indexBuffer)
        {
            vkCmdBindIndexBuffer(mCommandBuffer,indexBuffer,0,VK_INDEX_TYPE_UINT16);
            mBoundIndexBuffer=indexBuffer;
        }
        vkCmdDrawIndexed(mCommandBuffer,indexCount,1,0,0,0);
    }
    else vkCmdDraw(mCommandBuffer,vertexCount,1,0,0);
    // Every opaque NPR surface gets a real inverted-hull silhouette. Draw only
    // back faces of a screen-space-expanded copy after the base surface; depth
    // testing leaves the guaranteed outer contour visible. Alpha-blended
    // geometry stays single-pass because an expanded hull around billboard
    // quads would expose their rectangular mesh rather than the texture shape.
    if(batched && !vehicleCubeTarget && material.enhancedMaterialModel==3 &&
       material.enhancedMaterialProfile!=0 &&
       material.blendMode==0)
    {
        VulkanMaterialState outline=material;
        outline.enhancedMaterialModel=4;
        outline.twoSided=false;
        outline.cullMode=VK_CULL_MODE_FRONT_BIT;
        outline.reflectionEnabled=false;
        DrawPddiGeometry(image,format,width,height,arrayLayer,vertexBuffer,indexBuffer,
            vertexOffset,vertexCount,indexCount,topology,projection,modelview,
            textureSet,reflectionSet,topTextureSet,lightMapSet,outline);
    }
    if(!batched)
    {
        vkCmdEndRenderPass(mCommandBuffer);
        if(vkEndCommandBuffer(mCommandBuffer)!=VK_SUCCESS) break;
        VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount=1; submit.pCommandBuffers=&mCommandBuffer;
        if(vkQueueSubmit(mQueue,1,&submit,mFence)!=VK_SUCCESS) break;
        if(vkWaitForFences(mDevice,1,&mFence,VK_TRUE,XR_INFINITE_DURATION)!=VK_SUCCESS) break;
    }
    success=true;
    } while(false);
    if(success && !cached)
    {
        CachedDrawState state={};
        state.image=image;
        state.densityImage=foveated?mFragmentDensityMap:VK_NULL_HANDLE;
        state.format=format; state.width=width;
        state.height=height; state.arrayLayer=arrayLayer; state.topology=topology;
        state.blendMode=material.blendMode;
        state.cullMode=effectiveCull;
        state.colourWriteMask=material.colourWriteMask;
        state.depthTest=material.depthTest; state.depthWrite=material.depthWrite;
        state.shaderVariant=shaderVariant;
        state.materialModel=materialModel;
        state.alphaTest=material.alphaTest;
        state.depthBiasEnabled=material.depthBias!=0.0f;
        state.depthCompare=material.depthCompare;
        state.stencilTest=material.stencilTest;
        state.stencilCompare=material.stencilCompare;
        state.stencilFail=material.stencilFail;
        state.stencilDepthFail=material.stencilDepthFail;
        state.stencilPass=material.stencilPass;
        state.view=view; state.densityView=densityView;
        state.renderPass=renderPass; state.clearRenderPass=clearRenderPass;
        state.depthClearRenderPass=depthClearRenderPass;
        state.framebuffer=framebuffer;
        state.vertexModule=vertexModule; state.fragmentModule=fragmentModule;
        state.layout=layout; state.pipeline=pipeline;
        mDrawStateCache.push_back(state);
        mLastDrawStateIndex=mDrawStateCache.size()-1;
        mDrawStateLookup[stateHash]=mLastDrawStateIndex;
        // A newly encountered render-state group commonly appears first with
        // only one material class. Compile its two sibling shader variants
        // while the initial/loading scenes are warming the cache, so entering
        // a streamed district does not call vkCreateGraphicsPipelines on the
        // render-critical frame.
        if(!mPipelinePrewarm && mPrewarmedStateGroups<12)
        {
            ++mPrewarmedStateGroups;
            mPipelinePrewarm=true;
            VulkanMaterialState sibling=material;
            sibling.materialMode=0; sibling.reflectionEnabled=false;
            sibling.fogEnabled=false; sibling.lit=false;
            DrawPddiGeometry(image,format,width,height,arrayLayer,vertexBuffer,indexBuffer,
                vertexOffset,vertexCount,indexCount,topology,projection,modelview,
                textureSet,reflectionSet,topTextureSet,lightMapSet,sibling);
            sibling.lit=true;
            DrawPddiGeometry(image,format,width,height,arrayLayer,vertexBuffer,indexBuffer,
                vertexOffset,vertexCount,indexCount,topology,projection,modelview,
                textureSet,reflectionSet,topTextureSet,lightMapSet,sibling);
            sibling.materialMode=1;
            DrawPddiGeometry(image,format,width,height,arrayLayer,vertexBuffer,indexBuffer,
                vertexOffset,vertexCount,indexCount,topology,projection,modelview,
                textureSet,reflectionSet,topTextureSet,lightMapSet,sibling);
            mPipelinePrewarm=false;
        }
    }
    else if(!cached)
    {
        if(pipeline) vkDestroyPipeline(mDevice,pipeline,NULL);
        if(layout) vkDestroyPipelineLayout(mDevice,layout,NULL);
        if(fragmentModule) vkDestroyShaderModule(mDevice,fragmentModule,NULL);
        if(vertexModule) vkDestroyShaderModule(mDevice,vertexModule,NULL);
        if(framebuffer) vkDestroyFramebuffer(mDevice,framebuffer,NULL);
        if(depthClearRenderPass) vkDestroyRenderPass(mDevice,depthClearRenderPass,NULL);
        if(clearRenderPass) vkDestroyRenderPass(mDevice,clearRenderPass,NULL);
        if(renderPass) vkDestroyRenderPass(mDevice,renderPass,NULL);
        if(densityView) vkDestroyImageView(mDevice,densityView,NULL);
        if(view) vkDestroyImageView(mDevice,view,NULL);
    }
    return success;
}

bool VulkanContext::FindMemoryType(uint32_t typeBits,
                                   VkMemoryPropertyFlags properties,
                                   uint32_t* typeIndex) const
{
    if(!mPhysicalDevice || !typeIndex) return false;
    VkPhysicalDeviceMemoryProperties memoryProperties={};
    vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice,&memoryProperties);
    for(uint32_t i=0;i<memoryProperties.memoryTypeCount;++i)
    {
        if((typeBits&(1u<<i)) &&
           (memoryProperties.memoryTypes[i].propertyFlags&properties)==properties)
        {
            *typeIndex=i;
            return true;
        }
    }
    return false;
}

bool VulkanContext::CreateTexture2D(uint32_t width,uint32_t height,
                                    uint32_t mipLevels,VkImage* image,
                                    VkDeviceMemory* memory,VkImageView* view,
                                    VkSampler* sampler,VkDescriptorSet* descriptorSet,
                                    VkFormat format,uint32_t filterMode)
{
    if(!IsInitialized() || !width || !height || !mipLevels || !image ||
       !memory || !view || !sampler || !descriptorSet) return false;
    *image=VK_NULL_HANDLE; *memory=VK_NULL_HANDLE;
    *view=VK_NULL_HANDLE; *sampler=VK_NULL_HANDLE;
    *descriptorSet=VK_NULL_HANDLE;
    VkImageCreateInfo ii={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType=VK_IMAGE_TYPE_2D; ii.format=format;
    ii.extent={width,height,1}; ii.mipLevels=mipLevels; ii.arrayLayers=1;
    ii.samples=VK_SAMPLE_COUNT_1_BIT; ii.tiling=VK_IMAGE_TILING_OPTIMAL;
    ii.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    if(format==VK_FORMAT_B8G8R8A8_UNORM || format==VK_FORMAT_R8G8B8A8_UNORM)
        ii.usage|=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ii.sharingMode=VK_SHARING_MODE_EXCLUSIVE; ii.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    if(vkCreateImage(mDevice,&ii,NULL,image)!=VK_SUCCESS) return false;
    VkMemoryRequirements requirements={};
    vkGetImageMemoryRequirements(mDevice,*image,&requirements);
    uint32_t type=0;
    if(!FindMemoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&type))
    { vkDestroyImage(mDevice,*image,NULL); *image=VK_NULL_HANDLE; return false; }
    VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize=requirements.size; ai.memoryTypeIndex=type;
    if(vkAllocateMemory(mDevice,&ai,NULL,memory)!=VK_SUCCESS ||
       vkBindImageMemory(mDevice,*image,*memory,0)!=VK_SUCCESS)
    { DestroyTexture(*image,*memory,*view,*sampler,*descriptorSet); *image=VK_NULL_HANDLE; *memory=VK_NULL_HANDLE; return false; }
    VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image=*image; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=ii.format;
    vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount=mipLevels; vi.subresourceRange.layerCount=1;
    if(vkCreateImageView(mDevice,&vi,NULL,view)!=VK_SUCCESS)
    { DestroyTexture(*image,*memory,*view,*sampler,*descriptorSet); return false; }
    if(!CreateTextureSamplerDescriptor(*view,mipLevels,1,filterMode,sampler,descriptorSet))
    { DestroyTexture(*image,*memory,*view,*sampler,*descriptorSet); return false; }
    return true;
}

bool VulkanContext::CreateTexture2DArray(uint32_t width,uint32_t height,uint32_t layers,
                                         VkImage* image,VkDeviceMemory* memory,
                                         VkImageView* view,VkSampler* sampler,
                                         VkDescriptorSet* descriptorSet)
{
    if(!IsInitialized() || !width || !height || !layers) return false;
    *image=VK_NULL_HANDLE; *memory=VK_NULL_HANDLE; *view=VK_NULL_HANDLE;
    *sampler=VK_NULL_HANDLE; *descriptorSet=VK_NULL_HANDLE;
    VkImageCreateInfo ii={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType=VK_IMAGE_TYPE_2D; ii.format=VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent={width,height,1}; ii.mipLevels=1; ii.arrayLayers=layers;
    ii.samples=VK_SAMPLE_COUNT_1_BIT; ii.tiling=VK_IMAGE_TILING_OPTIMAL;
    ii.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    if(vkCreateImage(mDevice,&ii,NULL,image)!=VK_SUCCESS) return false;
    VkMemoryRequirements requirements={}; vkGetImageMemoryRequirements(mDevice,*image,&requirements);
    uint32_t type=0;
    if(!FindMemoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&type))
    { vkDestroyImage(mDevice,*image,NULL); *image=VK_NULL_HANDLE; return false; }
    VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize=requirements.size; ai.memoryTypeIndex=type;
    if(vkAllocateMemory(mDevice,&ai,NULL,memory)!=VK_SUCCESS ||
       vkBindImageMemory(mDevice,*image,*memory,0)!=VK_SUCCESS)
    { DestroyTexture(*image,*memory,*view,*sampler,*descriptorSet); return false; }
    VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image=*image; vi.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY; vi.format=ii.format;
    vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount=1; vi.subresourceRange.layerCount=layers;
    if(vkCreateImageView(mDevice,&vi,NULL,view)!=VK_SUCCESS ||
       !CreateTextureSamplerDescriptor(*view,1,0,1,sampler,descriptorSet))
    { DestroyTexture(*image,*memory,*view,*sampler,*descriptorSet); return false; }
    return true;
}

bool VulkanContext::SupportsSampledTextureFormat(VkFormat format) const
{
    if(!mPhysicalDevice) return false;
    VkFormatProperties properties={};
    vkGetPhysicalDeviceFormatProperties(mPhysicalDevice,format,&properties);
    const VkFormatFeatureFlags required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|
                                        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (properties.optimalTilingFeatures&required)==required;
}

bool VulkanContext::CreateTextureSamplerDescriptor(VkImageView view,uint32_t mipLevels,
                                                    uint32_t uvMode,uint32_t filterMode,
                                                    VkSampler* sampler,VkDescriptorSet* descriptorSet)
{
    if(!mDevice || !view || !sampler || !descriptorSet) return false;
    *sampler=VK_NULL_HANDLE; *descriptorSet=VK_NULL_HANDLE;
    const bool useMips=filterMode>=2 && mipLevels>1;
    const bool linearMag=filterMode==1 || filterMode==3 || filterMode==4;
    // GLES falls back to bilinear minification when mip filtering is requested
    // for a texture that has no mip chain.
    const bool linearMin=useMips?(filterMode==3 || filterMode==4):(filterMode!=0);
    VkSamplerCreateInfo si={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter=linearMag?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
    si.minFilter=linearMin?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
    si.mipmapMode=filterMode==4?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST;
    const VkSamplerAddressMode address=uvMode==0?VK_SAMPLER_ADDRESS_MODE_REPEAT:
                                                   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeU=si.addressModeV=si.addressModeW=address;
    si.maxLod=useMips?static_cast<float>(mipLevels-1):0.0f;
    si.anisotropyEnable=mSamplerAnisotropyEnabled && useMips?VK_TRUE:VK_FALSE;
    si.maxAnisotropy=si.anisotropyEnable?mMaxSamplerAnisotropy:1.0f;
    if(vkCreateSampler(mDevice,&si,NULL,sampler)!=VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool=mTextureDescriptorPool; dai.descriptorSetCount=1;
    dai.pSetLayouts=&mTextureSetLayout;
    if(vkAllocateDescriptorSets(mDevice,&dai,descriptorSet)!=VK_SUCCESS)
    { vkDestroySampler(mDevice,*sampler,NULL); *sampler=VK_NULL_HANDLE; return false; }
    VkDescriptorImageInfo descriptorImage={};
    descriptorImage.sampler=*sampler; descriptorImage.imageView=view;
    descriptorImage.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet=*descriptorSet; write.dstBinding=0; write.descriptorCount=1;
    write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo=&descriptorImage;
    vkUpdateDescriptorSets(mDevice,1,&write,0,NULL);
    return true;
}

void VulkanContext::DestroyTextureSamplerDescriptor(VkSampler sampler,VkDescriptorSet descriptorSet)
{
    if(!mDevice) return;
    if(descriptorSet && mTextureDescriptorPool)
        vkFreeDescriptorSets(mDevice,mTextureDescriptorPool,1,&descriptorSet);
    if(sampler) vkDestroySampler(mDevice,sampler,NULL);
}

bool VulkanContext::UploadTextureMip(VkImage image,uint32_t width,uint32_t height,
                                     uint32_t mipLevel,const void* data,
                                     VkDeviceSize size)
{
    if(!image || !width || !height || !data || !size) return false;
    PendingTextureUpload upload={};
    upload.image=image; upload.width=width; upload.height=height;
    upload.mipLevel=mipLevel; upload.arrayLayer=0;
    upload.data.resize(static_cast<size_t>(size));
    std::memcpy(upload.data.data(),data,static_cast<size_t>(size));
    mPendingTextureUploads.push_back(std::move(upload));
    mPendingUploadBytes+=size;
    return true;
}

bool VulkanContext::UploadTextureLayer(VkImage image,uint32_t width,uint32_t height,
                                       uint32_t layer,const void* data,VkDeviceSize size)
{
    if(!UploadTextureMip(image,width,height,0,data,size)) return false;
    mPendingTextureUploads.back().arrayLayer=layer;
    return true;
}

bool VulkanContext::HasPendingTextureUploads(VkImage image) const
{
    for(const PendingTextureUpload& upload:mPendingTextureUploads)
        if(upload.image==image) return true;
    return false;
}

bool VulkanContext::PrepareTextureForSampling(VkImage image)
{
    if(!image || !HasPendingTextureUploads(image)) return true;
    if(!mPddiEyeActive) return false;
    // A movie frame can be decoded after BeginPddiEye has already drained the
    // upload queue. Close the current colour pass and append that late upload
    // to the same eye command buffer before binding its descriptor. Otherwise
    // VulkanTexture deliberately returns no descriptor and the renderer uses
    // its opaque white fallback for this eye.
    EndActiveRenderPass();
    return RecordPendingTextureUploads() && !HasPendingTextureUploads(image);
}

bool VulkanContext::FlushTextureUploads()
{
    if(mPendingTextureUploads.empty()) return true;
    // Initialization/shutdown-only drain. Runtime uploads are recorded into
    // frame command buffers by RecordPendingTextureUploads and never wait.
    vkResetCommandBuffer(mUploadCommandBuffer,0);
    VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if(vkBeginCommandBuffer(mUploadCommandBuffer,&begin)!=VK_SUCCESS) return false;
    std::vector<VkBuffer> stagingBuffers;
    std::vector<VkDeviceMemory> stagingMemory;
    while(!mPendingTextureUploads.empty())
    {
        PendingTextureUpload& upload=mPendingTextureUploads.front();
        VkBuffer staging=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE;
        if(!CreateBuffer(upload.data.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
           &staging,&memory) ||
           !UploadMemory(memory,0,upload.data.data(),upload.data.size())) return false;
        VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        const bool initialized=upload.mipLevel==0 && upload.arrayLayer==0 &&
            mUploadedTextureImages.count(upload.image)!=0;
        barrier.srcAccessMask=initialized?VK_ACCESS_SHADER_READ_BIT:0;
        barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout=initialized?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                                      VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=upload.image;
        barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel=upload.mipLevel;
        barrier.subresourceRange.baseArrayLayer=upload.arrayLayer;
        barrier.subresourceRange.levelCount=barrier.subresourceRange.layerCount=1;
        vkCmdPipelineBarrier(mUploadCommandBuffer,initialized?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        VkBufferImageCopy copy={};
        copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel=upload.mipLevel;
        copy.imageSubresource.baseArrayLayer=upload.arrayLayer;
        copy.imageSubresource.layerCount=1;
        copy.imageExtent={upload.width,upload.height,1};
        vkCmdCopyBufferToImage(mUploadCommandBuffer,staging,upload.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(mUploadCommandBuffer,VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier);
        stagingBuffers.push_back(staging); stagingMemory.push_back(memory);
        mPendingUploadBytes-=upload.data.size();
        if(upload.mipLevel==0 && upload.arrayLayer==0)
            mUploadedTextureImages.insert(upload.image);
        mPendingTextureUploads.pop_front();
    }
    const bool recorded=vkEndCommandBuffer(mUploadCommandBuffer)==VK_SUCCESS;
    vkResetFences(mDevice,1,&mFence);
    VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount=1; submit.pCommandBuffers=&mUploadCommandBuffer;
    const bool complete=recorded && vkQueueSubmit(mQueue,1,&submit,mFence)==VK_SUCCESS &&
        vkWaitForFences(mDevice,1,&mFence,VK_TRUE,XR_INFINITE_DURATION)==VK_SUCCESS;
    for(size_t i=0;i<stagingBuffers.size();++i)
        DestroyBuffer(stagingBuffers[i],stagingMemory[i]);
    return complete;
}

bool VulkanContext::RecordPendingTextureUploads()
{
    if(!mPddiEyeActive || !mTextureUploadMapped || mPendingTextureUploads.empty())
        return true;
    VkDeviceSize cursor=mTextureUploadOffset,recordedBytes=0;
    while(!mPendingTextureUploads.empty())
    {
        PendingTextureUpload& upload=mPendingTextureUploads.front();
        const VkDeviceSize size=upload.data.size();
        const VkDeviceSize aligned=(cursor+255u)&~VkDeviceSize(255u);
        if(aligned+size>mTextureUploadEnd) break;
        if(recordedBytes && recordedBytes+size>mTextureUploadBudget) break;
        std::memcpy(static_cast<unsigned char*>(mTextureUploadMapped)+aligned,
                    upload.data.data(),static_cast<size_t>(size));
        VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        const bool initialized=upload.mipLevel==0 && upload.arrayLayer==0 &&
            mUploadedTextureImages.count(upload.image)!=0;
        barrier.srcAccessMask=initialized?VK_ACCESS_SHADER_READ_BIT:0;
        barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout=initialized?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                                      VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=upload.image;
        barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel=upload.mipLevel;
        barrier.subresourceRange.baseArrayLayer=upload.arrayLayer;
        barrier.subresourceRange.levelCount=barrier.subresourceRange.layerCount=1;
        vkCmdPipelineBarrier(mCommandBuffer,initialized?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        VkBufferImageCopy copy={}; copy.bufferOffset=aligned;
        copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel=upload.mipLevel;
        copy.imageSubresource.baseArrayLayer=upload.arrayLayer;
        copy.imageSubresource.layerCount=1;
        copy.imageExtent={upload.width,upload.height,1};
        vkCmdCopyBufferToImage(mCommandBuffer,mTextureUploadBuffer,upload.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier);
        cursor=aligned+size; recordedBytes+=size;
        mPendingUploadBytes-=size;
        if(upload.mipLevel==0 && upload.arrayLayer==0)
            mUploadedTextureImages.insert(upload.image);
        mPendingTextureUploads.pop_front();
    }
    if(recordedBytes)
    {
        mTextureUploadOffset=cursor;
        SharOpenXR::RecordPddiUpload(static_cast<unsigned>(recordedBytes),0.0);
    }
    return true;
}

void VulkanContext::DestroyTexture(VkImage image,VkDeviceMemory memory,
                                   VkImageView view,VkSampler sampler,
                                   VkDescriptorSet descriptorSet)
{
    if(!mDevice) return;
    mUploadedTextureImages.erase(image);
    for(auto it=mPendingTextureUploads.begin();it!=mPendingTextureUploads.end();)
    {
        if(it->image==image)
        { mPendingUploadBytes-=it->data.size(); it=mPendingTextureUploads.erase(it); }
        else ++it;
    }
    if(descriptorSet && mTextureDescriptorPool)
        vkFreeDescriptorSets(mDevice,mTextureDescriptorPool,1,&descriptorSet);
    if(sampler) vkDestroySampler(mDevice,sampler,NULL);
    if(view) vkDestroyImageView(mDevice,view,NULL);
    if(image) vkDestroyImage(mDevice,image,NULL);
    if(memory) vkFreeMemory(mDevice,memory,NULL);
}

bool VulkanContext::CreateBuffer(VkDeviceSize size,VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties,
                                 VkBuffer* buffer,VkDeviceMemory* memory)
{
    if(!IsInitialized() || !size || !buffer || !memory) return false;
    *buffer=VK_NULL_HANDLE; *memory=VK_NULL_HANDLE;
    VkBufferCreateInfo bufferInfo={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size=size; bufferInfo.usage=usage;
    bufferInfo.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    if(vkCreateBuffer(mDevice,&bufferInfo,NULL,buffer)!=VK_SUCCESS) return false;
    VkMemoryRequirements requirements={};
    vkGetBufferMemoryRequirements(mDevice,*buffer,&requirements);
    uint32_t memoryType=0;
    if(!FindMemoryType(requirements.memoryTypeBits,properties,&memoryType))
    {
        vkDestroyBuffer(mDevice,*buffer,NULL); *buffer=VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo allocation={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize=requirements.size;
    allocation.memoryTypeIndex=memoryType;
    if(vkAllocateMemory(mDevice,&allocation,NULL,memory)!=VK_SUCCESS)
    {
        vkDestroyBuffer(mDevice,*buffer,NULL); *buffer=VK_NULL_HANDLE;
        return false;
    }
    if(vkBindBufferMemory(mDevice,*buffer,*memory,0)!=VK_SUCCESS)
    {
        vkFreeMemory(mDevice,*memory,NULL); vkDestroyBuffer(mDevice,*buffer,NULL);
        *memory=VK_NULL_HANDLE; *buffer=VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VulkanContext::CreateStaticBuffer(const void* data,VkDeviceSize size,
                                       VkBufferUsageFlags usage,VkBuffer* buffer,
                                       VkDeviceMemory* memory)
{
    if(!data || !size || mPddiEyeActive) return false;
    VkBuffer staging=VK_NULL_HANDLE; VkDeviceMemory stagingMemory=VK_NULL_HANDLE;
    if(!CreateBuffer(size,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
         &staging,&stagingMemory) ||
       !UploadMemory(stagingMemory,0,data,size) ||
       !CreateBuffer(size,usage|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,buffer,memory))
    {
        DestroyBuffer(staging,stagingMemory);
        return false;
    }
    vkResetFences(mDevice,1,&mFence);
    vkResetCommandBuffer(mUploadCommandBuffer,0);
    VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    bool success=vkBeginCommandBuffer(mUploadCommandBuffer,&begin)==VK_SUCCESS;
    if(success)
    {
        VkBufferCopy copy={0,0,size};
        vkCmdCopyBuffer(mUploadCommandBuffer,staging,*buffer,1,&copy);
        VkBufferMemoryBarrier ready={VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        ready.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        ready.dstAccessMask=(usage&VK_BUFFER_USAGE_INDEX_BUFFER_BIT)?
            VK_ACCESS_INDEX_READ_BIT:VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        ready.srcQueueFamilyIndex=ready.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        ready.buffer=*buffer; ready.size=size;
        vkCmdPipelineBarrier(mUploadCommandBuffer,VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,0,NULL,1,&ready,0,NULL);
        success=vkEndCommandBuffer(mUploadCommandBuffer)==VK_SUCCESS;
    }
    VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount=1; submit.pCommandBuffers=&mUploadCommandBuffer;
    success=success && vkQueueSubmit(mQueue,1,&submit,mFence)==VK_SUCCESS &&
            vkWaitForFences(mDevice,1,&mFence,VK_TRUE,XR_INFINITE_DURATION)==VK_SUCCESS;
    DestroyBuffer(staging,stagingMemory);
    if(!success)
    {
        DestroyBuffer(*buffer,*memory); *buffer=VK_NULL_HANDLE; *memory=VK_NULL_HANDLE;
    }
    return success;
}

void VulkanContext::DestroyBuffer(VkBuffer buffer,VkDeviceMemory memory)
{
    if(!mDevice) return;
    if(mPddiEyeActive)
    {
        if(buffer) mDeferredBuffers.push_back(buffer);
        if(memory) mDeferredBufferMemory.push_back(memory);
        return;
    }
    if(buffer) vkDestroyBuffer(mDevice,buffer,NULL);
    if(memory) vkFreeMemory(mDevice,memory,NULL);
}

bool VulkanContext::UploadMemory(VkDeviceMemory memory,VkDeviceSize offset,
                                 const void* data,VkDeviceSize size)
{
    if(!mDevice || !memory || !data || !size) return false;
    void* mapped=NULL;
    if(vkMapMemory(mDevice,memory,offset,size,0,&mapped)!=VK_SUCCESS) return false;
    std::memcpy(mapped,data,static_cast<size_t>(size));
    vkUnmapMemory(mDevice,memory);
    return true;
}

bool VulkanContext::UploadTransientVertices(const void* data,VkDeviceSize size,
                                            VkBuffer* buffer,VkDeviceSize* offset)
{
    if(!data || !size || !buffer || !offset || !mTransientVertexMapped) return false;
    const VkDeviceSize aligned=(mTransientVertexOffset+15u)&~VkDeviceSize(15u);
    if(aligned+size>mTransientVertexEnd) return false;
    std::memcpy(static_cast<unsigned char*>(mTransientVertexMapped)+aligned,data,size);
    *buffer=mTransientVertexBuffer; *offset=aligned;
    mTransientVertexOffset=aligned+size;
    return true;
}

void VulkanContext::Shutdown()
{
    if(mDevice)
    {
        vkDeviceWaitIdle(mDevice);
        mPendingTextureUploads.clear();
        mPendingUploadBytes=0;
        if(mTextureUploadMapped) vkUnmapMemory(mDevice,mTextureUploadMemory);
        mTextureUploadMapped=NULL;
        if(mTextureUploadBuffer) vkDestroyBuffer(mDevice,mTextureUploadBuffer,NULL);
        if(mTextureUploadMemory) vkFreeMemory(mDevice,mTextureUploadMemory,NULL);
        mTextureUploadBuffer=VK_NULL_HANDLE; mTextureUploadMemory=VK_NULL_HANDLE;
        if(mTransientVertexMapped) vkUnmapMemory(mDevice,mTransientVertexMemory);
        mTransientVertexMapped=NULL;
        if(mTransientVertexBuffer) vkDestroyBuffer(mDevice,mTransientVertexBuffer,NULL);
        if(mTransientVertexMemory) vkFreeMemory(mDevice,mTransientVertexMemory,NULL);
        mTransientVertexBuffer=VK_NULL_HANDLE; mTransientVertexMemory=VK_NULL_HANDLE;
        if(mDrawUniformMapped) vkUnmapMemory(mDevice,mDrawUniformMemory);
        mDrawUniformMapped=NULL;
        if(mDrawUniformBuffer) vkDestroyBuffer(mDevice,mDrawUniformBuffer,NULL);
        if(mDrawUniformMemory) vkFreeMemory(mDevice,mDrawUniformMemory,NULL);
        mDrawUniformBuffer=VK_NULL_HANDLE; mDrawUniformMemory=VK_NULL_HANDLE;
        ReleaseDeferredResources();
        DestroyTexture(mFallbackImage,mFallbackMemory,mFallbackView,
                       mFallbackSampler,mFallbackDescriptorSet);
        mFallbackImage=VK_NULL_HANDLE; mFallbackMemory=VK_NULL_HANDLE;
        mFallbackView=VK_NULL_HANDLE; mFallbackSampler=VK_NULL_HANDLE;
        mFallbackDescriptorSet=VK_NULL_HANDLE;
        DestroyTexture(mFallbackPbrImage,mFallbackPbrMemory,mFallbackPbrView,
                       mFallbackPbrSampler,mFallbackPbrDescriptorSet);
        mFallbackPbrImage=VK_NULL_HANDLE; mFallbackPbrMemory=VK_NULL_HANDLE;
        mFallbackPbrView=VK_NULL_HANDLE; mFallbackPbrSampler=VK_NULL_HANDLE;
        mFallbackPbrDescriptorSet=VK_NULL_HANDLE;
        DestroyTexture(mVehicleCubeImage,mVehicleCubeMemory,mVehicleCubeView,
                       mVehicleCubeSampler,mVehicleCubeDescriptor);
        mVehicleCubeImage=VK_NULL_HANDLE; mVehicleCubeMemory=VK_NULL_HANDLE;
        mVehicleCubeView=VK_NULL_HANDLE; mVehicleCubeSampler=VK_NULL_HANDLE;
        mVehicleCubeDescriptor=VK_NULL_HANDLE;
        for(const CachedDrawState& state:mDrawStateCache)
        {
            if(state.pipeline) vkDestroyPipeline(mDevice,state.pipeline,NULL);
            if(state.layout) vkDestroyPipelineLayout(mDevice,state.layout,NULL);
            if(state.fragmentModule) vkDestroyShaderModule(mDevice,state.fragmentModule,NULL);
            if(state.vertexModule) vkDestroyShaderModule(mDevice,state.vertexModule,NULL);
            if(state.framebuffer) vkDestroyFramebuffer(mDevice,state.framebuffer,NULL);
            if(state.depthClearRenderPass) vkDestroyRenderPass(mDevice,state.depthClearRenderPass,NULL);
            if(state.clearRenderPass) vkDestroyRenderPass(mDevice,state.clearRenderPass,NULL);
            if(state.renderPass) vkDestroyRenderPass(mDevice,state.renderPass,NULL);
            if(state.densityView) vkDestroyImageView(mDevice,state.densityView,NULL);
            if(state.view) vkDestroyImageView(mDevice,state.view,NULL);
        }
        mDrawStateCache.clear();
        mDrawStateLookup.clear();
        mLastDrawStateIndex=static_cast<size_t>(-1);
        for(const CachedDepthTarget& target:mDepthTargetCache)
        {
            if(target.view) vkDestroyImageView(mDevice,target.view,NULL);
            if(target.image) vkDestroyImage(mDevice,target.image,NULL);
            if(target.memory) vkFreeMemory(mDevice,target.memory,NULL);
        }
        mDepthTargetCache.clear();
        mShadowPipeline.Destroy(mDevice);
        for(ShadowCascade& cascade:mShadowCascades)
        {
            if(cascade.framebuffer) vkDestroyFramebuffer(mDevice,cascade.framebuffer,NULL);
            if(cascade.renderPass) vkDestroyRenderPass(mDevice,cascade.renderPass,NULL);
            if(cascade.sampler) vkDestroySampler(mDevice,cascade.sampler,NULL);
            if(cascade.readbackMapped) vkUnmapMemory(mDevice,cascade.readbackMemory);
            if(cascade.readbackBuffer) vkDestroyBuffer(mDevice,cascade.readbackBuffer,NULL);
            if(cascade.readbackMemory) vkFreeMemory(mDevice,cascade.readbackMemory,NULL);
            if(cascade.view) vkDestroyImageView(mDevice,cascade.view,NULL);
            if(cascade.image) vkDestroyImage(mDevice,cascade.image,NULL);
            if(cascade.memory) vkFreeMemory(mDevice,cascade.memory,NULL);
            std::memset(&cascade,0,sizeof(cascade));
        }
        if(mTextureDescriptorPool) vkDestroyDescriptorPool(mDevice,mTextureDescriptorPool,NULL);
        mShadowDescriptorSet=VK_NULL_HANDLE;
        if(mPipelineCache && !mPipelineCachePath.empty())
        {
            size_t cacheSize=0;
            if(vkGetPipelineCacheData(mDevice,mPipelineCache,&cacheSize,NULL)==VK_SUCCESS &&
               cacheSize>0 && cacheSize<64u*1024u*1024u)
            {
                std::vector<unsigned char> cacheData(cacheSize);
                if(vkGetPipelineCacheData(mDevice,mPipelineCache,&cacheSize,
                                          cacheData.data())==VK_SUCCESS)
                {
                    SDL_RWops* cacheFile=SDL_RWFromFile(mPipelineCachePath.c_str(),"wb");
                    if(cacheFile)
                    {
                        SDL_RWwrite(cacheFile,cacheData.data(),1,cacheSize);
                        SDL_RWclose(cacheFile);
                    }
                }
            }
        }
        if(mPipelineCache) vkDestroyPipelineCache(mDevice,mPipelineCache,NULL);
        if(mShadowSetLayout) vkDestroyDescriptorSetLayout(mDevice,mShadowSetLayout,NULL);
        if(mDrawSetLayout) vkDestroyDescriptorSetLayout(mDevice,mDrawSetLayout,NULL);
        if(mTextureSetLayout) vkDestroyDescriptorSetLayout(mDevice,mTextureSetLayout,NULL);
        for(uint32_t i=0;i<FrameArenaCount;++i)
        {
            if(mFrameArenas[i].timestampQueryPool)
                vkDestroyQueryPool(mDevice,mFrameArenas[i].timestampQueryPool,NULL);
            if(mFrameArenas[i].fence) vkDestroyFence(mDevice,mFrameArenas[i].fence,NULL);
        }
        if(mFence) vkDestroyFence(mDevice,mFence,NULL);
        if(mCommandPool) vkDestroyCommandPool(mDevice,mCommandPool,NULL);
        vkDestroyDevice(mDevice,NULL);
    }
    if(mInstance) vkDestroyInstance(mInstance,NULL);
    mQueue=VK_NULL_HANDLE;
    mDevice=VK_NULL_HANDLE;
    mTimestampQueryPool=VK_NULL_HANDLE;
    std::memset(mFrameArenas,0,sizeof(mFrameArenas));
    mActiveFrameArena=NULL;
    mLastGpuMilliseconds=0.0;
    mLastFenceWaitMilliseconds=0.0;
    mPhysicalDevice=VK_NULL_HANDLE;
    mInstance=VK_NULL_HANDLE;
    mQueueFamilyIndex=0;
    mFence=VK_NULL_HANDLE;
    mTextureDescriptorPool=VK_NULL_HANDLE;
    mTextureSetLayout=VK_NULL_HANDLE;
    mDrawSetLayout=VK_NULL_HANDLE;
    mShadowSetLayout=VK_NULL_HANDLE;
    mDrawDescriptorSet=VK_NULL_HANDLE;
    mShadowDescriptorSet=VK_NULL_HANDLE;
    mCommandBuffer=VK_NULL_HANDLE;
    mUploadCommandBuffer=VK_NULL_HANDLE;
    mPipelineCache=VK_NULL_HANDLE;
    mPipelineCachePath.clear();
    mCommandPool=VK_NULL_HANDLE;
}
}

#endif

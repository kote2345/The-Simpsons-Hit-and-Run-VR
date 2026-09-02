#ifndef SHAR_OPENXR_VULKAN_CONTEXT_H
#define SHAR_OPENXR_VULKAN_CONTEXT_H

#if defined(RAD_ANDROID) && defined(SRR2_VR_RENDERER_VULKAN)

#include <openxr/openxr.h>
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <cstddef>
#include <vr/vulkan/shadow_pipeline.h>
#include <vr/vulkan/material_state.h>

namespace SharOpenXR
{
struct VulkanEyeTarget;

class VulkanContext
{
public:
    VulkanContext();
    ~VulkanContext();

    // OpenXR selects the physical device and creates the Vulkan instance and
    // logical device. This guarantees that the same code works with both the
    // Quest runtime and a future desktop OpenXR runtime.
    bool Initialize(XrInstance xrInstance, XrSystemId systemId,
                    PFN_xrGetInstanceProcAddr getInstanceProcAddr);
    void Shutdown();

    bool IsInitialized() const { return mDevice != VK_NULL_HANDLE; }
    bool IsMultiviewSupported() const { return mMultiviewSupported; }
    double GetLastGpuMilliseconds() const { return mLastGpuMilliseconds; }
    double GetLastFenceWaitMilliseconds() const { return mLastFenceWaitMilliseconds; }
    VkInstance GetInstance() const { return mInstance; }
    VkPhysicalDevice GetPhysicalDevice() const { return mPhysicalDevice; }
    VkDevice GetDevice() const { return mDevice; }
    VkQueue GetQueue() const { return mQueue; }
    uint32_t GetQueueFamilyIndex() const { return mQueueFamilyIndex; }
    bool ClearImage(VkImage image, bool firstUse);
    // layer is 0/1 for a conventional stereo pass and 2 for multiview.
    bool ClearImageInPddiEye(VkImage image, bool firstUse, uint32_t layer);
    bool BeginPddiEye();
    bool EndPddiEye();
    void ClearPddiBuffers(uint32_t bufferMask);
    void ClearActiveColourBlack(uint32_t width,uint32_t height);
    void SetFragmentDensityMap(VkImage image,uint32_t width,uint32_t height);
    void EndActiveRenderPass();
    bool DrawSmokeTriangle(VkImage image, VkFormat format, uint32_t width,
                           uint32_t height, uint32_t arrayLayer);
    bool DrawPddiGeometry(VkImage image, VkFormat format, uint32_t width,
                          uint32_t height, uint32_t arrayLayer,
                          VkBuffer vertexBuffer, VkBuffer indexBuffer,
                          VkDeviceSize vertexOffset,
                          uint32_t vertexCount, uint32_t indexCount,
                          VkPrimitiveTopology topology,
                          const float* projection, const float* modelview,
                          VkDescriptorSet textureSet, VkDescriptorSet reflectionSet,
                          VkDescriptorSet topTextureSet, VkDescriptorSet lightMapSet,
                          const VulkanMaterialState& material);
    bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties, VkBuffer* buffer,
                      VkDeviceMemory* memory);
    bool CreateStaticBuffer(const void* data, VkDeviceSize size,
                            VkBufferUsageFlags usage, VkBuffer* buffer,
                            VkDeviceMemory* memory);
    void DestroyBuffer(VkBuffer buffer, VkDeviceMemory memory);
    bool UploadMemory(VkDeviceMemory memory, VkDeviceSize offset,
                      const void* data, VkDeviceSize size);
    bool UploadTransientVertices(const void* data, VkDeviceSize size,
                                 VkBuffer* buffer, VkDeviceSize* offset);
    bool CreateTexture2D(uint32_t width, uint32_t height, uint32_t mipLevels,
                         VkImage* image, VkDeviceMemory* memory,
                         VkImageView* view, VkSampler* sampler,
                         VkDescriptorSet* descriptorSet,
                         VkFormat format=VK_FORMAT_B8G8R8A8_UNORM,
                         uint32_t filterMode=1);
    bool CreateTexture2DArray(uint32_t width, uint32_t height, uint32_t layers,
                         VkImage* image, VkDeviceMemory* memory,
                         VkImageView* view, VkSampler* sampler,
                         VkDescriptorSet* descriptorSet);
    bool SupportsSampledTextureFormat(VkFormat format) const;
    bool UploadTextureMip(VkImage image, uint32_t width, uint32_t height,
                          uint32_t mipLevel, const void* data,
                          VkDeviceSize size);
    bool UploadTextureLayer(VkImage image, uint32_t width, uint32_t height,
                            uint32_t layer, const void* data, VkDeviceSize size);
    bool FlushTextureUploads();
    bool HasPendingTextureUploads(VkImage image) const;
    bool PrepareTextureForSampling(VkImage image);
    void DestroyTexture(VkImage image, VkDeviceMemory memory,
                        VkImageView view, VkSampler sampler,
                        VkDescriptorSet descriptorSet);
    bool CreateTextureSamplerDescriptor(VkImageView view, uint32_t mipLevels,
                                        uint32_t uvMode, uint32_t filterMode,
                                        VkSampler* sampler,
                                        VkDescriptorSet* descriptorSet);
    void DestroyTextureSamplerDescriptor(VkSampler sampler,
                                         VkDescriptorSet descriptorSet);
    bool BeginOffscreenTarget(VkImage image, bool initialized);
    bool EndOffscreenTarget(VkImage image);
    bool BeginVehicleCubeMapFace(uint32_t face);
    void EndVehicleCubeMapFace(uint32_t face);
    bool GetVehicleCubeMapTarget(VulkanEyeTarget* target) const;
    bool HasDynamicVehicleCubeMap() const { return mVehicleCubeReady; }
    VkDescriptorSet GetDynamicVehicleCubeMapSet() const
    { return mVehicleCubeDescriptor; }
    bool BeginShadowCascade(uint32_t cascade,uint32_t size);
    bool EndShadowCascade(uint32_t cascade);
    void SetShadowReceiverState(bool enabled,const float matrices[48]);

private:
    enum { FrameArenaCount=3 };
    struct FrameArena
    {
        VkCommandBuffer commandBuffer;
        VkFence fence;
        VkQueryPool timestampQueryPool;
        bool submitted;
    };
    struct CachedDrawState
    {
        VkImage image;
        VkImage densityImage;
        VkFormat format;
        uint32_t width, height, arrayLayer;
        VkPrimitiveTopology topology;
        uint32_t blendMode;
        VkCullModeFlags cullMode;
        VkColorComponentFlags colourWriteMask;
        bool depthTest, depthWrite, depthBiasEnabled;
        uint8_t shaderVariant;
        uint8_t materialModel;
        bool alphaTest;
        VkCompareOp depthCompare;
        bool stencilTest;
        VkCompareOp stencilCompare;
        VkStencilOp stencilFail, stencilDepthFail, stencilPass;
        VkImageView view,densityView;
        VkRenderPass renderPass, clearRenderPass, depthClearRenderPass;
        VkFramebuffer framebuffer;
        VkShaderModule vertexModule, fragmentModule;
        VkPipelineLayout layout;
        VkPipeline pipeline;
    };
    struct CachedDepthTarget
    {
        VkImage colourImage;
        uint32_t arrayLayer, width, height;
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
        bool initialized;
        uint64_t clearedSerial;
    };
    struct ShadowCascade
    {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
        VkSampler sampler;
        VkRenderPass renderPass;
        VkFramebuffer framebuffer;
        VkBuffer readbackBuffer;
        VkDeviceMemory readbackMemory;
        void* readbackMapped;
        int readbackArena;
        bool readbackLogged;
        uint32_t size;
        bool initialized;
    };
    VulkanContext(const VulkanContext&);
    VulkanContext& operator=(const VulkanContext&);

    VkInstance mInstance;
    VkPhysicalDevice mPhysicalDevice;
    VkDevice mDevice;
    bool mMultiviewSupported;
    bool mFragmentDensityMapSupported;
    VkImage mFragmentDensityMap;
    uint32_t mFragmentDensityMapWidth,mFragmentDensityMapHeight;
    bool mSamplerAnisotropyEnabled;
    float mMaxSamplerAnisotropy;
    VkQueue mQueue;
    uint32_t mQueueFamilyIndex;
    VkCommandPool mCommandPool;
    VkCommandBuffer mCommandBuffer;
    VkCommandBuffer mUtilityCommandBuffer;
    VkCommandBuffer mUploadCommandBuffer;
    VkFence mFence;
    FrameArena mFrameArenas[FrameArenaCount];
    uint32_t mFrameArenaIndex;
    FrameArena* mActiveFrameArena;
    VkQueryPool mTimestampQueryPool;
    float mTimestampPeriod;
    double mLastGpuMilliseconds;
    double mLastFenceWaitMilliseconds;
    VkPipelineCache mPipelineCache;
    std::string mPipelineCachePath;
    struct PendingTextureUpload
    {
        VkImage image;
        uint32_t width,height,mipLevel,arrayLayer;
        std::vector<unsigned char> data;
    };
    std::deque<PendingTextureUpload> mPendingTextureUploads;
    std::unordered_set<VkImage> mUploadedTextureImages;
    VkDeviceSize mPendingUploadBytes;
    VkBuffer mTextureUploadBuffer;
    VkDeviceMemory mTextureUploadMemory;
    void* mTextureUploadMapped;
    VkDeviceSize mTextureUploadSize;
    VkDeviceSize mTextureUploadBudget;
    VkDeviceSize mTextureUploadOffset;
    VkDeviceSize mTextureUploadEnd;
    VkBuffer mTransientVertexBuffer;
    VkDeviceMemory mTransientVertexMemory;
    void* mTransientVertexMapped;
    VkDeviceSize mTransientVertexSize;
    VkDeviceSize mTransientVertexOffset;
    VkDeviceSize mTransientVertexEnd;
    VkDescriptorSetLayout mTextureSetLayout;
    VkDescriptorSetLayout mDrawSetLayout;
    VkDescriptorSetLayout mShadowSetLayout;
    VkDescriptorPool mTextureDescriptorPool;
    VkDescriptorSet mDrawDescriptorSet;
    VkDescriptorSet mShadowDescriptorSet;
    VkBuffer mDrawUniformBuffer;
    VkDeviceMemory mDrawUniformMemory;
    void* mDrawUniformMapped;
    VkDeviceSize mDrawUniformSize;
    VkDeviceSize mDrawUniformOffset;
    VkDeviceSize mDrawUniformEnd;
    VkDeviceSize mDrawUniformAlignment;
    VkImage mFallbackImage;
    VkDeviceMemory mFallbackMemory;
    VkImageView mFallbackView;
    VkSampler mFallbackSampler;
    VkDescriptorSet mFallbackDescriptorSet;
    VkImage mFallbackPbrImage;
    VkDeviceMemory mFallbackPbrMemory;
    VkImageView mFallbackPbrView;
    VkSampler mFallbackPbrSampler;
    VkDescriptorSet mFallbackPbrDescriptorSet;
    bool mPddiEyeActive;
    bool mPddiRenderPassActive;
    uint32_t mColourClearMask;
    std::vector<VkImageView> mDeferredViews;
    std::vector<VkRenderPass> mDeferredRenderPasses;
    std::vector<VkFramebuffer> mDeferredFramebuffers;
    std::vector<VkShaderModule> mDeferredShaderModules;
    std::vector<VkPipelineLayout> mDeferredPipelineLayouts;
    std::vector<VkPipeline> mDeferredPipelines;
    std::vector<VkBuffer> mDeferredBuffers;
    std::vector<VkDeviceMemory> mDeferredBufferMemory;
    std::vector<CachedDrawState> mDrawStateCache;
    std::unordered_map<uint64_t,size_t> mDrawStateLookup;
    size_t mLastDrawStateIndex;
    VkPipeline mBoundPipeline;
    VkDescriptorSet mBoundTextureSets[8];
    VkBuffer mBoundVertexBuffer;
    VkDeviceSize mBoundVertexOffset;
    VkBuffer mBoundIndexBuffer;
    bool mDynamicStateValid;
    VkViewport mBoundViewport;
    VkRect2D mBoundScissor;
    float mBoundDepthBias;
    uint32_t mBoundStencilReference;
    uint32_t mBoundStencilCompareMask;
    uint32_t mBoundStencilWriteMask;
    std::vector<CachedDepthTarget> mDepthTargetCache;
    uint64_t mEyeSerial;
    uint32_t mShaderVariantDraws[5];
    uint32_t mShaderVariantFrames;
    bool mPipelinePrewarm;
    uint32_t mPrewarmedStateGroups;
    ShadowCascade mShadowCascades[3];
    bool mShadowPass;
    uint32_t mShadowCascadeIndex;
    bool mShadowReceiverEnabled;
    float mShadowReceiverMatrices[48];
    VkImage mVehicleCubeImage;
    VkDeviceMemory mVehicleCubeMemory;
    VkImageView mVehicleCubeView;
    VkSampler mVehicleCubeSampler;
    VkDescriptorSet mVehicleCubeDescriptor;
    bool mVehicleCubeCapture,mVehicleCubeReady;
    uint32_t mVehicleCubeFace,mVehicleCubeCompletedMask;
    ShadowPipeline mShadowPipeline;
    uint32_t mShadowDrawCalls[3]={0,0,0};
    uint64_t mShadowTriangles[3]={0,0,0};
    bool DrawShadowGeometry(VkBuffer vertexBuffer,VkBuffer indexBuffer,
                            VkDeviceSize vertexOffset,uint32_t vertexCount,
                            uint32_t indexCount,VkPrimitiveTopology topology,
                            const float* projection,const float* modelview,
                            VkDescriptorSet textureSet,const VulkanMaterialState& material);
    void ReleaseDeferredResources();
    bool RecordPendingTextureUploads();
    bool FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties,
                        uint32_t* typeIndex) const;
};

// Shared by the OpenXR compositor and the Vulkan PDDI driver. OpenXR owns the
// selected physical/logical device so every renderer layer uses exactly the
// runtime-compatible GPU and queue.
VulkanContext& GetVulkanContext();
}

#endif
#endif

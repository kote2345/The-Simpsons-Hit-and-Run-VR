#ifndef SHAR_VULKAN_SHADOW_PIPELINE_H
#define SHAR_VULKAN_SHADOW_PIPELINE_H
#include <vulkan/vulkan.h>
namespace SharOpenXR {
class ShadowPipeline {
public:
    ShadowPipeline();
    bool GetOrCreate(VkDevice device,VkPipelineCache cache,VkRenderPass renderPass,
                     VkDescriptorSetLayout textureLayout,VkDescriptorSetLayout drawLayout,
                     VkDescriptorSetLayout shadowLayout,VkPrimitiveTopology topology,
                     VkPipeline* pipeline,VkPipelineLayout* layout);
    void Destroy(VkDevice device);
private:
    VkPipelineLayout mLayout;
    VkPipeline mPipelines[5];
    VkShaderModule mVertexModule;
    VkShaderModule mFragmentModule;
};
}
#endif

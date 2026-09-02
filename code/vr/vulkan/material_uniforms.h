#ifndef SHAR_VULKAN_MATERIAL_UNIFORMS_H
#define SHAR_VULKAN_MATERIAL_UNIFORMS_H
#include <cstddef>
#include <vr/vulkan/material_pipeline.h>
namespace SharOpenXR {
struct VulkanMaterialState;
struct MaterialUniformInput {
    const VulkanMaterialState& material;
    const float* modelview;
    const float* normalMatrix;
    const float* shadowMatrices;
    bool shadowReceiverEnabled;
    bool sourceIsSrgb;
    bool cubeMapReady;
};
enum { FullMaterialUniformFloatCount=704, CompactMaterialUniformFloatCount=8 };
size_t GetMaterialUniformSize(const MaterialPipelineSelection& selection);
size_t PackMaterialUniforms(const MaterialPipelineSelection& selection,
                            const MaterialUniformInput& input,float* output);
void PackMaterialUniformBase(const MaterialUniformInput& input,float* output);
void PackLegacyMaterialUniforms(const MaterialUniformInput& input,float* output);
void PackPhongMaterialUniforms(const MaterialUniformInput& input,float* output);
void PackPbrMaterialUniforms(const MaterialUniformInput& input,float* output);
void PackToonMaterialUniforms(const MaterialUniformInput& input,float* output);
}
#endif

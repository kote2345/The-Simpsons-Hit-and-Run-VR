#include <vr/vulkan/material_pipeline.h>
#include <vr/vulkan/material_uniforms.h>
#include <vr/vulkan/material_state.h>
#include "../../../libs/pure3d/pddi/vulkan/shaders/lit_pbr_frag_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/smoke_pbr_frag_spv.h"
namespace SharOpenXR {
void PackPbrMaterialUniforms(const MaterialUniformInput& input,float* output) {
    PackMaterialUniformBase(input,output);
    output[680]=static_cast<float>(input.material.pbrMapFlags);
    output[681]=static_cast<float>(input.material.pbrDebugMode);
}
ShaderBinary GetPbrFragmentShader(GeometryProgram geometry) {
    return geometry==GeometryProgram::Full?ShaderBinary{smoke_pbr_frag_spv,sizeof(smoke_pbr_frag_spv)}:
                                           ShaderBinary{lit_pbr_frag_spv,sizeof(lit_pbr_frag_spv)};
} }

#include <vr/vulkan/material_pipeline.h>
#include <vr/vulkan/material_uniforms.h>
#include "../../../libs/pure3d/pddi/vulkan/shaders/lit_phong_frag_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/smoke_phong_frag_spv.h"
namespace SharOpenXR {
void PackPhongMaterialUniforms(const MaterialUniformInput& input,float* output) {
    PackMaterialUniformBase(input,output);
}
ShaderBinary GetPhongFragmentShader(GeometryProgram geometry) {
    return geometry==GeometryProgram::Full?ShaderBinary{smoke_phong_frag_spv,sizeof(smoke_phong_frag_spv)}:
                                           ShaderBinary{lit_phong_frag_spv,sizeof(lit_phong_frag_spv)};
} }

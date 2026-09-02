#include <vr/vulkan/material_pipeline.h>
#include <vr/vulkan/material_uniforms.h>
#include "../../../libs/pure3d/pddi/vulkan/shaders/compact_frag_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/simple_frag_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/lit_legacy_frag_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/smoke_legacy_frag_spv.h"
namespace SharOpenXR {
void PackLegacyMaterialUniforms(const MaterialUniformInput& input,float* output) {
    PackMaterialUniformBase(input,output);
}
ShaderBinary GetLegacyFragmentShader(GeometryProgram geometry) {
    if(geometry==GeometryProgram::Compact)return {compact_frag_spv,sizeof(compact_frag_spv)};
    if(geometry==GeometryProgram::Unlit)return {simple_frag_spv,sizeof(simple_frag_spv)};
    if(geometry==GeometryProgram::Lit)return {lit_legacy_frag_spv,sizeof(lit_legacy_frag_spv)};
    return {smoke_legacy_frag_spv,sizeof(smoke_legacy_frag_spv)};
} }

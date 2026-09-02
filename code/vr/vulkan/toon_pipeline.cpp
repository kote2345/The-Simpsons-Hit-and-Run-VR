#include <vr/vulkan/material_pipeline.h>
#include <vr/vulkan/material_uniforms.h>
#include "../../../libs/pure3d/pddi/vulkan/shaders/lit_toon_frag_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/lit_outline_frag_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/smoke_toon_frag_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/smoke_outline_frag_spv.h"
namespace SharOpenXR {
void PackToonMaterialUniforms(const MaterialUniformInput& input,float* output) {
    PackMaterialUniformBase(input,output);
}
ShaderBinary GetToonFragmentShader(GeometryProgram geometry,bool outline) {
    if(geometry==GeometryProgram::Full)
        return outline?ShaderBinary{smoke_outline_frag_spv,sizeof(smoke_outline_frag_spv)}:
                       ShaderBinary{smoke_toon_frag_spv,sizeof(smoke_toon_frag_spv)};
    return outline?ShaderBinary{lit_outline_frag_spv,sizeof(lit_outline_frag_spv)}:
                   ShaderBinary{lit_toon_frag_spv,sizeof(lit_toon_frag_spv)};
} }

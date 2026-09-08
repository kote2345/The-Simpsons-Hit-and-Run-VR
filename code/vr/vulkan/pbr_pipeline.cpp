#include <vr/vulkan/material_pipeline.h>
#include <vr/vulkan/material_uniforms.h>
#include <vr/vulkan/material_state.h>
#include <cstdlib>
#include <cmath>
// Both PBR variants fully occlude direct solar diffuse/specular through CSM.
#include "../../../libs/pure3d/pddi/vulkan/shaders/lit_pbr_frag_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/smoke_pbr_frag_spv.h"
namespace SharOpenXR {
#if defined(_WIN32)
namespace {
float PbrShadowFilterRadius() {
    // Read once rather than querying the environment for every world draw.
    // Zero restores the previous single-fetch path for visual/GPU comparisons.
    static const float radius=[]() {
        const char* setting=std::getenv("SRR2_PCVR_SHADOW_FILTER_RADIUS");
        if(!setting) return 1.25f;
        char* end=nullptr;
        const float value=std::strtof(setting,&end);
        if(end==setting || *end!='\0' || !std::isfinite(value)) return 1.25f;
        return value<0.0f?0.0f:(value>2.5f?2.5f:value);
    }();
    return radius;
}
}
#endif
void PackPbrMaterialUniforms(const MaterialUniformInput& input,float* output) {
    PackMaterialUniformBase(input,output);
    output[680]=static_cast<float>(input.material.pbrMapFlags);
    output[681]=static_cast<float>(input.material.pbrDebugMode);
#if defined(_WIN32)
    output[682]=1.0f; // PCVR punctual GGX lighting; Quest retains its current budget.
    output[683]=PbrShadowFilterRadius();
#endif
}
ShaderBinary GetPbrFragmentShader(GeometryProgram geometry) {
    return geometry==GeometryProgram::Full?ShaderBinary{smoke_pbr_frag_spv,sizeof(smoke_pbr_frag_spv)}:
                                           ShaderBinary{lit_pbr_frag_spv,sizeof(lit_pbr_frag_spv)};
} }

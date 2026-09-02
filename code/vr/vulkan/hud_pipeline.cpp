#include <vr/vulkan/material_pipeline.h>
#include "../../../libs/pure3d/pddi/vulkan/shaders/hud_vert_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/hud_vert_single_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/hud_frag_spv.h"
namespace SharOpenXR {
ShaderBinary GetHudVertexShader(bool multiview) {
    return multiview?ShaderBinary{hud_vert_spv,sizeof(hud_vert_spv)}:
                     ShaderBinary{hud_vert_single_spv,sizeof(hud_vert_single_spv)};
}
ShaderBinary GetHudFragmentShader() { return {hud_frag_spv,sizeof(hud_frag_spv)}; }
}

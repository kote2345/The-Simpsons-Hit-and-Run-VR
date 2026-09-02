#include <vr/vulkan/material_pipeline.h>
#include <vr/vulkan/material_state.h>
#include <algorithm>

#include "../../../libs/pure3d/pddi/vulkan/shaders/smoke_vert_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/smoke_vert_single_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/lit_vert_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/lit_vert_single_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/simple_vert_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/simple_vert_single_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/compact_vert_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/compact_vert_single_spv.h"

namespace SharOpenXR
{
MaterialPipelineSelection SelectMaterialPipeline(const VulkanMaterialState& material,
                                                  bool shadowReceiverEnabled)
{
    MaterialPipelineSelection result={};
    if(material.hudPass)
    {
        result.technology=MaterialTechnology::Legacy;
        result.geometry=GeometryProgram::Hud;
        result.shaderVariant=4;
        result.materialModel=0;
        return result;
    }
    const unsigned model=std::min(4u,material.enhancedMaterialModel);
    result.technology=model==1?MaterialTechnology::Phong:
        model==2?MaterialTechnology::Pbr:
        model==3?MaterialTechnology::Toon:
        model==4?MaterialTechnology::Outline:MaterialTechnology::Legacy;
    const bool full=material.materialMode!=0 || material.reflectionEnabled;
    const bool enhanced=model!=0 && material.enhancedMaterialProfile!=0 &&
        (model>=2 || material.lit || material.enhancedMaterialProfile>1);
    // Dynamic vehicle lights are independent of the selected material model.
    // Route otherwise-unlit static geometry through the lit vertex/fragment
    // interface so it receives the lamp positions and surface normal.
    const bool dynamicLights=material.vehicleRearLightMode!=0 &&
                             material.vehicleRearLightCount!=0;
    const bool compact=!full && !material.lit && !material.fogEnabled &&
        !enhanced && !dynamicLights && material.skinMatrixCount==0 &&
        !shadowReceiverEnabled;
    result.geometry=compact?GeometryProgram::Compact:
        full?GeometryProgram::Full:
        // A CSM receiver must use the same Lit vertex/fragment interface in
        // every material technology. Previously Legacy and most Phong world
        // geometry fell through to Simple while PBR/Toon used Lit, so the
        // supposedly shared shadow feature followed different shader paths.
        (!shadowReceiverEnabled&&!material.lit&&!material.fogEnabled&&
         !enhanced&&!dynamicLights)?GeometryProgram::Unlit:
                                                        GeometryProgram::Lit;
    result.shaderVariant=result.geometry==GeometryProgram::Full?0:
        result.geometry==GeometryProgram::Unlit?1:
        result.geometry==GeometryProgram::Lit?2:3;
    result.materialModel=(result.geometry==GeometryProgram::Unlit ||
                          result.geometry==GeometryProgram::Compact)?0:
                         static_cast<uint8_t>(model);
    result.usesEnvironment=result.geometry==GeometryProgram::Full ||
        (result.geometry==GeometryProgram::Lit && result.technology==MaterialTechnology::Pbr);
    result.usesLayers=result.geometry==GeometryProgram::Full;
    result.usesShadows=result.geometry!=GeometryProgram::Compact;
    result.usesPbrMaps=result.technology==MaterialTechnology::Pbr &&
                       result.geometry!=GeometryProgram::Compact;
    result.usesCubeMap=result.geometry!=GeometryProgram::Compact &&
                       result.geometry!=GeometryProgram::Unlit;
    return result;
}

ShaderBinary GetMaterialVertexShader(const MaterialPipelineSelection& selection,
                                     bool multiview)
{
    if(selection.geometry==GeometryProgram::Hud) return GetHudVertexShader(multiview);
    switch(selection.geometry)
    {
        case GeometryProgram::Compact:
            return multiview?ShaderBinary{compact_vert_spv,sizeof(compact_vert_spv)}:
                             ShaderBinary{compact_vert_single_spv,sizeof(compact_vert_single_spv)};
        case GeometryProgram::Unlit:
            return multiview?ShaderBinary{simple_vert_spv,sizeof(simple_vert_spv)}:
                             ShaderBinary{simple_vert_single_spv,sizeof(simple_vert_single_spv)};
        case GeometryProgram::Lit:
            return multiview?ShaderBinary{lit_vert_spv,sizeof(lit_vert_spv)}:
                             ShaderBinary{lit_vert_single_spv,sizeof(lit_vert_single_spv)};
        default:
            return multiview?ShaderBinary{smoke_vert_spv,sizeof(smoke_vert_spv)}:
                             ShaderBinary{smoke_vert_single_spv,sizeof(smoke_vert_single_spv)};
    }
}

ShaderBinary GetMaterialFragmentShader(const MaterialPipelineSelection& selection)
{
    if(selection.geometry==GeometryProgram::Hud) return GetHudFragmentShader();
    if(selection.geometry==GeometryProgram::Compact ||
       selection.geometry==GeometryProgram::Unlit)
        return GetLegacyFragmentShader(selection.geometry);
    switch(selection.technology)
    {
        case MaterialTechnology::Phong:return GetPhongFragmentShader(selection.geometry);
        case MaterialTechnology::Pbr:return GetPbrFragmentShader(selection.geometry);
        case MaterialTechnology::Toon:return GetToonFragmentShader(selection.geometry,false);
        case MaterialTechnology::Outline:return GetToonFragmentShader(selection.geometry,true);
        default:return GetLegacyFragmentShader(selection.geometry);
    }
}
}

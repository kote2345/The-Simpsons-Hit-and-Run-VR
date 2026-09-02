#include <vr/vulkan/material_uniforms.h>
#include <vr/vulkan/material_state.h>
#include <cstring>
namespace SharOpenXR {
void PackMaterialUniformBase(const MaterialUniformInput& input,float* output) {
    const VulkanMaterialState& m=input.material;
    std::memset(output,0,sizeof(float)*FullMaterialUniformFloatCount);
    std::memcpy(output,input.modelview,sizeof(float)*16);
    std::memcpy(output+16,m.colour,sizeof(float)*4);
    output[20]=m.alphaTest?m.alphaRef:-1.0f;
    std::memcpy(output+21,&m.alphaCompare,sizeof(m.alphaCompare));
    std::memcpy(output+24,m.ambientTerm,sizeof(float)*4);
    std::memcpy(output+28,m.specular,sizeof(float)*4);
    std::memcpy(output+32,m.fogColour,sizeof(float)*4);
    output[36]=m.fogEnabled?1.0f:0.0f; output[37]=m.fogStart; output[38]=m.fogEnd;
    std::memcpy(output+40,m.environmentBlend,sizeof(float)*4);
    output[44]=m.reflectionEnabled?1.0f:0.0f;
    output[45]=static_cast<float>(m.materialMode);
    output[46]=static_cast<float>(m.textureBlendMode);
    output[47]=m.twoLayerColourByVertex?1.0f:0.0f;
    output[48]=input.sourceIsSrgb?1.0f:0.0f;
    output[49]=static_cast<float>(m.enhancedMaterialModel);
    output[50]=static_cast<float>(m.enhancedMaterialProfile);
    output[52]=m.shininess; output[53]=m.lit?1.0f:0.0f;
    for(unsigned i=0;i<8;++i) {
        std::memcpy(output+56+i*12,m.lightPosition[i],sizeof(float)*4);
        std::memcpy(output+60+i*12,m.lightColour[i],sizeof(float)*4);
        std::memcpy(output+64+i*12,m.lightAttenuation[i],sizeof(float)*4);
    }
    std::memcpy(output+152,m.reflectionViewToWorld,sizeof(float)*16);
    std::memcpy(output+168,input.normalMatrix,sizeof(float)*16);
    output[184]=static_cast<float>(m.skinMatrixCount);
    std::memcpy(output+185,m.enhancedSunDirection,sizeof(float)*3);
    for(unsigned i=0;i<m.skinMatrixCount && i<VulkanMaterialState::MaxSkinMatrices;++i)
        std::memcpy(output+188+i*16,m.skinMatrices[i],sizeof(float)*16);
    if(input.shadowReceiverEnabled && input.shadowMatrices)
        std::memcpy(output+588,input.shadowMatrices,sizeof(float)*48);
    output[636]=input.shadowReceiverEnabled?1.0f:0.0f;
    output[637]=20.0f; output[638]=50.0f; output[639]=0.00018f;
    for(unsigned i=0;i<4;++i) {
        std::memcpy(output+640+i*4,m.vehicleRearLightPositions[i],sizeof(float)*4);
        std::memcpy(output+656+i*4,m.vehicleRearLightDirections[i],sizeof(float)*4);
    }
    std::memcpy(output+672,m.vehicleRearLightColour,sizeof(float)*3);
    output[675]=static_cast<float>(m.vehicleRearLightMode);
    output[676]=static_cast<float>(m.vehicleRearLightCount);
    output[677]=input.cubeMapReady?1.0f:0.0f;
}
size_t GetMaterialUniformSize(const MaterialPipelineSelection& s) {
    return sizeof(float)*((s.geometry==GeometryProgram::Compact || s.geometry==GeometryProgram::Hud)?
        CompactMaterialUniformFloatCount:FullMaterialUniformFloatCount);
}
size_t PackMaterialUniforms(const MaterialPipelineSelection& s,
                            const MaterialUniformInput& input,float* output) {
    if(s.geometry==GeometryProgram::Compact || s.geometry==GeometryProgram::Hud) {
        const VulkanMaterialState& m=input.material;
        const float values[8]={m.ambientTerm[0],m.ambientTerm[1],m.ambientTerm[2],
            m.colour[3],m.alphaTest?m.alphaRef:-1.0f,
            static_cast<float>(m.alphaCompare),0.0f,0.0f};
        std::memcpy(output,values,sizeof(values)); return sizeof(values);
    }
    switch(s.technology) {
        case MaterialTechnology::Phong:PackPhongMaterialUniforms(input,output); break;
        case MaterialTechnology::Pbr:PackPbrMaterialUniforms(input,output); break;
        case MaterialTechnology::Toon:
        case MaterialTechnology::Outline:PackToonMaterialUniforms(input,output); break;
        default:PackLegacyMaterialUniforms(input,output); break;
    }
    return sizeof(float)*FullMaterialUniformFloatCount;
}
}

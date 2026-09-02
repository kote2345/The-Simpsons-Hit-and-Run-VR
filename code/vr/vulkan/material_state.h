#ifndef SHAR_VULKAN_MATERIAL_STATE_H
#define SHAR_VULKAN_MATERIAL_STATE_H
#include <vulkan/vulkan.h>
#include <cstdint>
namespace SharOpenXR {
struct VulkanMaterialState {
    enum { MaxSkinMatrices=25 };
    enum ShaderClass : uint32_t { ShaderSimple,ShaderLayered,ShaderLightMap,
        ShaderLayeredLightMap,ShaderEnvironment,ShaderShadow,ShaderOther };
    uint32_t shaderClass; bool hudPass; uint32_t blendMode; bool twoSided;
    bool alphaTest; float alphaRef; uint32_t alphaCompare;
    float colour[4],ambientTerm[4],specular[4]; float shininess;
    float lightPosition[8][4],lightColour[8][4],lightAttenuation[8][4];
    bool lit; VkCullModeFlags cullMode; VkColorComponentFlags colourWriteMask;
    bool depthTest,depthWrite; VkCompareOp depthCompare;
    int32_t scissorX,scissorY; uint32_t scissorWidth,scissorHeight;
    uint32_t scissorSurfaceWidth,scissorSurfaceHeight;
    float viewportLeft,viewportTop,viewportWidth,viewportHeight,depthBias;
    bool stencilTest; VkCompareOp stencilCompare;
    VkStencilOp stencilFail,stencilDepthFail,stencilPass;
    uint32_t stencilReference,stencilCompareMask,stencilWriteMask;
    bool fogEnabled; float fogColour[4],fogStart,fogEnd;
    bool reflectionEnabled; float environmentBlend[4],reflectionViewToWorld[16];
    uint32_t materialMode,enhancedMaterialProfile,enhancedMaterialModel;
    float enhancedSunDirection[3];
    uint32_t vehicleRearLightMode,vehicleRearLightCount;
    float vehicleRearLightPositions[4][4],vehicleRearLightDirections[4][4];
    float vehicleRearLightColour[3];
    uint32_t textureBlendMode; bool twoLayerColourByVertex;
    VkDescriptorSet pbrTextureSet; uint32_t pbrMapFlags,pbrDebugMode;
    uint32_t skinMatrixCount; float skinMatrices[MaxSkinMatrices][16];
};
}
#endif

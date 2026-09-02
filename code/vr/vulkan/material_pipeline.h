#ifndef SHAR_VULKAN_MATERIAL_PIPELINE_H
#define SHAR_VULKAN_MATERIAL_PIPELINE_H

#include <cstddef>
#include <cstdint>

namespace SharOpenXR
{
struct VulkanMaterialState;

enum class MaterialTechnology : uint8_t
{
    Legacy,
    Phong,
    Pbr,
    Toon,
    Outline
};

enum class GeometryProgram : uint8_t
{
    Full,
    Unlit,
    Lit,
    Compact,
    Hud
};

struct MaterialPipelineSelection
{
    MaterialTechnology technology;
    GeometryProgram geometry;
    uint8_t shaderVariant;
    uint8_t materialModel;
    bool usesEnvironment;
    bool usesLayers;
    bool usesShadows;
    bool usesPbrMaps;
    bool usesCubeMap;
};

struct ShaderBinary
{
    const uint32_t* words;
    size_t bytes;
};

MaterialPipelineSelection SelectMaterialPipeline(const VulkanMaterialState& material,
                                                  bool shadowReceiverEnabled);
ShaderBinary GetMaterialVertexShader(const MaterialPipelineSelection& selection,
                                     bool multiview);
ShaderBinary GetMaterialFragmentShader(const MaterialPipelineSelection& selection);

ShaderBinary GetLegacyFragmentShader(GeometryProgram geometry);
ShaderBinary GetPhongFragmentShader(GeometryProgram geometry);
ShaderBinary GetPbrFragmentShader(GeometryProgram geometry);
ShaderBinary GetToonFragmentShader(GeometryProgram geometry,bool outline);
ShaderBinary GetHudVertexShader(bool multiview);
ShaderBinary GetHudFragmentShader();
}

#endif

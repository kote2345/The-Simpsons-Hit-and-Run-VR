#include <vr/vulkan/shadow_pipeline.h>
#include <cstring>
#include "../../../libs/pure3d/pddi/vulkan/shaders/shadow_vert_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/shadow_frag_spv.h"
namespace SharOpenXR {
ShadowPipeline::ShadowPipeline():mLayout(VK_NULL_HANDLE),mVertexModule(VK_NULL_HANDLE),
    mFragmentModule(VK_NULL_HANDLE) { std::memset(mPipelines,0,sizeof(mPipelines)); }
bool ShadowPipeline::GetOrCreate(VkDevice device,VkPipelineCache cache,VkRenderPass renderPass,
    VkDescriptorSetLayout textureLayout,VkDescriptorSetLayout drawLayout,
    VkDescriptorSetLayout shadowLayout,VkPrimitiveTopology topology,
    VkPipeline* pipeline,VkPipelineLayout* layout) {
    const uint32_t index=topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP?1u:
        topology==VK_PRIMITIVE_TOPOLOGY_LINE_LIST?2u:
        topology==VK_PRIMITIVE_TOPOLOGY_LINE_STRIP?3u:
        topology==VK_PRIMITIVE_TOPOLOGY_POINT_LIST?4u:0u;
    VkShaderModuleCreateInfo sm={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    if(!mVertexModule) { sm.codeSize=sizeof(shadow_vert_spv); sm.pCode=shadow_vert_spv;
        if(vkCreateShaderModule(device,&sm,nullptr,&mVertexModule)!=VK_SUCCESS) return false; }
    if(!mFragmentModule) { sm.codeSize=sizeof(shadow_frag_spv); sm.pCode=shadow_frag_spv;
        if(vkCreateShaderModule(device,&sm,nullptr,&mFragmentModule)!=VK_SUCCESS) return false; }
    if(!mLayout) {
        VkDescriptorSetLayout layouts[6]={textureLayout,drawLayout,textureLayout,textureLayout,
            textureLayout,shadowLayout};
        VkPushConstantRange range={VK_SHADER_STAGE_VERTEX_BIT,0,64};
        VkPipelineLayoutCreateInfo info={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        info.setLayoutCount=6; info.pSetLayouts=layouts;
        info.pushConstantRangeCount=1; info.pPushConstantRanges=&range;
        if(vkCreatePipelineLayout(device,&info,nullptr,&mLayout)!=VK_SUCCESS) return false;
    }
    if(!mPipelines[index]) {
        VkPipelineShaderStageCreateInfo stages[2]={{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=mVertexModule; stages[0].pName="main";
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=mFragmentModule; stages[1].pName="main";
        VkVertexInputBindingDescription binding={0,80,VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[4]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},
            {2,0,VK_FORMAT_R32G32_SFLOAT,24},{6,0,VK_FORMAT_R32G32B32_SFLOAT,52},
            {7,0,VK_FORMAT_R32G32B32A32_UINT,64}};
        VkPipelineVertexInputStateCreateInfo vi={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount=1; vi.pVertexBindingDescriptions=&binding;
        vi.vertexAttributeDescriptionCount=4; vi.pVertexAttributeDescriptions=attrs;
        VkPipelineInputAssemblyStateCreateInfo ia={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology=topology;
        VkPipelineViewportStateCreateInfo vp={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount=vp.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo rs={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode=VK_POLYGON_MODE_FILL; rs.cullMode=VK_CULL_MODE_NONE; rs.frontFace=VK_FRONT_FACE_CLOCKWISE;
        rs.depthBiasEnable=VK_TRUE; rs.depthBiasConstantFactor=4.0f; rs.depthBiasSlopeFactor=2.0f; rs.lineWidth=1.0f;
        VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds={VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable=ds.depthWriteEnable=VK_TRUE; ds.depthCompareOp=VK_COMPARE_OP_LESS;
        VkPipelineColorBlendStateCreateInfo cb={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        const VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dy={VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dy.dynamicStateCount=2; dy.pDynamicStates=states;
        VkGraphicsPipelineCreateInfo pi={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pi.stageCount=2; pi.pStages=stages; pi.pVertexInputState=&vi; pi.pInputAssemblyState=&ia;
        pi.pViewportState=&vp; pi.pRasterizationState=&rs; pi.pMultisampleState=&ms;
        pi.pDepthStencilState=&ds; pi.pColorBlendState=&cb; pi.pDynamicState=&dy;
        pi.layout=mLayout; pi.renderPass=renderPass;
        if(vkCreateGraphicsPipelines(device,cache,1,&pi,nullptr,&mPipelines[index])!=VK_SUCCESS) return false;
    }
    *pipeline=mPipelines[index]; *layout=mLayout; return true;
}
void ShadowPipeline::Destroy(VkDevice device) {
    for(VkPipeline& pipeline:mPipelines) { if(pipeline) vkDestroyPipeline(device,pipeline,nullptr); pipeline=VK_NULL_HANDLE; }
    if(mLayout) vkDestroyPipelineLayout(device,mLayout,nullptr);
    if(mVertexModule) vkDestroyShaderModule(device,mVertexModule,nullptr);
    if(mFragmentModule) vkDestroyShaderModule(device,mFragmentModule,nullptr);
    mLayout=VK_NULL_HANDLE; mVertexModule=mFragmentModule=VK_NULL_HANDLE;
}
}

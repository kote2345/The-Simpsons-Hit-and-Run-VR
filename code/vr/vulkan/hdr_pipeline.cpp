#include <vr/vulkan/openxr_vulkan_context.h>
#include "projection_math.h"
#if defined(SRR2_OPENXR) && defined(SRR2_VR_RENDERER_VULKAN)
#include <vr/openxrmanager.h>
#include <radmath/radmath.hpp>
#include <SDL.h>
#include <cstring>
#include <cmath>
#include "../../../libs/pure3d/pddi/vulkan/shaders/hdr_resolve_vert_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/hdr_resolve_frag_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/volumetric_light_frag_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/volumetric_froxel_comp_spv.h"
#include "../../../libs/pure3d/pddi/vulkan/shaders/hdr_exposure_comp_spv.h"
// HDR resolve, procedural CSM fog and bilateral volume upscale share one descriptor set.
namespace SharOpenXR {
namespace {
constexpr uint32_t kFroxelPixelSize=12;
constexpr uint32_t kFroxelSliceCount=96;
}
bool VulkanContext::RouteHdrTarget(VkImage& image,VkFormat& format,uint32_t width,
                                   uint32_t height,uint32_t layer,const float* projection) {
#if !defined(SRR2_OPENXR_PLATFORM_WIN32)
    return true;
#else
    const bool enabled=IsHdrEnabled();
    if(!enabled || !mPddiEyeActive || mPipelinePrewarm || mLdrOffscreenTargets.count(image) ||
       mHdrResolvedOutputs.count(image)) return true;
    for(const auto& t:mHdrTargets) if(t.colour==image) return true;
    HdrTarget* target=nullptr;
    for(auto& t:mHdrTargets)
        if(t.output==image && t.outputFormat==format && t.layer==layer &&
           t.width==width && t.height==height) { target=&t; break; }
    if(!target) {
        VkFormatProperties properties={};
        vkGetPhysicalDeviceFormatProperties(mPhysicalDevice,VK_FORMAT_R16G16B16A16_SFLOAT,&properties);
        const VkFormatFeatureFlags required=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        if((properties.optimalTilingFeatures&required)!=required) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR HDR: RGBA16F attachments unsupported");
            return false;
        }
        mHdrTargets.emplace_back(); target=&mHdrTargets.back();
        auto& t=*target; t.output=image; t.outputFormat=format;
        t.width=width; t.height=height; t.layer=layer; t.layers=layer==2?2:1;
        VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType=VK_IMAGE_TYPE_2D; ci.format=VK_FORMAT_R16G16B16A16_SFLOAT;
        ci.extent={width,height,1}; ci.mipLevels=1; ci.arrayLayers=layer==2?2:layer+1;
        ci.samples=VK_SAMPLE_COUNT_1_BIT; ci.tiling=VK_IMAGE_TILING_OPTIMAL;
        ci.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if(vkCreateImage(mDevice,&ci,nullptr,&t.colour)!=VK_SUCCESS) return false;
        VkMemoryRequirements req={}; vkGetImageMemoryRequirements(mDevice,t.colour,&req);
        uint32_t type=0;
        if(!FindMemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&type)) return false;
        VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize=req.size; ai.memoryTypeIndex=type;
        if(vkAllocateMemory(mDevice,&ai,nullptr,&t.memory)!=VK_SUCCESS ||
           vkBindImageMemory(mDevice,t.colour,t.memory,0)!=VK_SUCCESS) return false;
        t.volumeWidth=(width+1)/2; t.volumeHeight=(height+1)/2;
        ci.extent={t.volumeWidth,t.volumeHeight,1};
        ci.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        if(vkCreateImage(mDevice,&ci,nullptr,&t.volume)!=VK_SUCCESS) return false;
        vkGetImageMemoryRequirements(mDevice,t.volume,&req);
        if(!FindMemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&type)) return false;
        ai.allocationSize=req.size; ai.memoryTypeIndex=type;
        if(vkAllocateMemory(mDevice,&ai,nullptr,&t.volumeMemory)!=VK_SUCCESS ||
           vkBindImageMemory(mDevice,t.volume,t.volumeMemory,0)!=VK_SUCCESS) return false;
        // Camera-frustum-aligned 3D grid stored as a 2D array.  A 12-pixel XY
        // footprint is a good VR compromise: resolve filtering hides the lower
        // resolution while substantially reducing CSM/noise work and memory.
        // per eye. Lighting is injected once per froxel, then integrated by
        // the half-resolution composite pass.
        // A 1/12 grid aliases nearby foliage into eye-dependent slabs.  The
        // desktop path has enough bandwidth for 1/8 resolution, which keeps
        // alpha-tested canopy shadows spatially coherent in stereo.
        t.froxelWidth=(width+kFroxelPixelSize-1)/kFroxelPixelSize;
        t.froxelHeight=(height+kFroxelPixelSize-1)/kFroxelPixelSize;
        ci.extent={t.froxelWidth,t.froxelHeight,1};
        ci.arrayLayers=kFroxelSliceCount*t.layers;
        ci.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        if(vkCreateImage(mDevice,&ci,nullptr,&t.froxel)!=VK_SUCCESS) return false;
        vkGetImageMemoryRequirements(mDevice,t.froxel,&req);
        if(!FindMemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&type)) return false;
        ai.allocationSize=req.size; ai.memoryTypeIndex=type;
        if(vkAllocateMemory(mDevice,&ai,nullptr,&t.froxelMemory)!=VK_SUCCESS ||
           vkBindImageMemory(mDevice,t.froxel,t.froxelMemory,0)!=VK_SUCCESS) return false;
        VkBufferCreateInfo bi={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size=sizeof(float)*80; bi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if(vkCreateBuffer(mDevice,&bi,nullptr,&t.meterBuffer)!=VK_SUCCESS) return false;
        vkGetBufferMemoryRequirements(mDevice,t.meterBuffer,&req);
        if(!FindMemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&type)) return false;
        ai.allocationSize=req.size; ai.memoryTypeIndex=type;
        if(vkAllocateMemory(mDevice,&ai,nullptr,&t.meterMemory)!=VK_SUCCESS ||
           vkBindBufferMemory(mDevice,t.meterBuffer,t.meterMemory,0)!=VK_SUCCESS ||
           vkMapMemory(mDevice,t.meterMemory,0,bi.size,0,&t.meterMapped)!=VK_SUCCESS) return false;
        float* exposure=static_cast<float*>(t.meterMapped);
        std::memset(exposure,0,sizeof(float)*80);
        exposure[0]=exposure[4]=0.60f;
        VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=t.colour; vi.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY; vi.format=ci.format;
        vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,t.layers==2?0:layer,t.layers};
        if(vkCreateImageView(mDevice,&vi,nullptr,&t.colourView)!=VK_SUCCESS) return false;
        vi.image=t.volume;
        if(vkCreateImageView(mDevice,&vi,nullptr,&t.volumeView)!=VK_SUCCESS) return false;
        vi.image=t.froxel; vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,
                                                kFroxelSliceCount*t.layers};
        if(vkCreateImageView(mDevice,&vi,nullptr,&t.froxelView)!=VK_SUCCESS) return false;
        t.allocated=true;
        SDL_Log("PCVR HDR: RGBA16F %ux%u layers=%u, froxel volume=%ux%ux%u",width,height,
                t.layers,t.froxelWidth,t.froxelHeight,kFroxelSliceCount*t.layers);
    }
    auto& t=*target;
    if(!t.allocated || t.failed) return false;
    for(uint32_t eye=0;eye<t.layers;++eye) std::memcpy(t.projection[eye],projection,64);
    if(t.layers==2) {
        rmt::Matrix projections[2],adjustments[2];
        if(GetMultiviewMatrices(projections,adjustments)) {
            // Match the vertex path's projection * eyeAdjustment transform.
            // Inverting projection alone reconstructed an eye-local ray while
            // CSM matrices consume centre-view positions, making volumetric
            // occluders slide with the headset and producing a central hole.
            for(int eye=0;eye<2;++eye) {
                float* output=t.projection[eye];
                const float* a=projections[eye].m[0];
                const float* b=adjustments[eye].m[0];
                for(unsigned column=0;column<4;++column)
                    for(unsigned row=0;row<4;++row) {
                        output[column*4+row]=0.0f;
                        for(unsigned k=0;k<4;++k)
                            output[column*4+row]+=a[k*4+row]*b[column*4+k];
                    }
            }
        }
    }
    if(!t.active) {
        EndActiveRenderPass();
        VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask=t.initialized?VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT:0;
        barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout=t.initialized?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=t.colour; barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,t.layers==2?0:layer,t.layers};
        vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        t.active=t.initialized=true;
        mColourClearMask|=t.layers==2?3u:(1u<<layer);
    }
    image=t.colour; format=VK_FORMAT_R16G16B16A16_SFLOAT;
    return true;
#endif
}
bool VulkanContext::CreateHdrResolve(HdrTarget& t,VkImage depth) {
    VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image=depth; vi.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY; vi.format=VK_FORMAT_D24_UNORM_S8_UINT;
    vi.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,t.layers};
    if(vkCreateImageView(mDevice,&vi,nullptr,&t.depthView)!=VK_SUCCESS) return false;
    VkSamplerCreateInfo si={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter=si.minFilter=VK_FILTER_NEAREST;
    si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if(vkCreateSampler(mDevice,&si,nullptr,&t.sampler)!=VK_SUCCESS) return false;
    si.magFilter=si.minFilter=VK_FILTER_LINEAR;
    if(vkCreateSampler(mDevice,&si,nullptr,&t.volumeSampler)!=VK_SUCCESS) return false;
    if(vkCreateSampler(mDevice,&si,nullptr,&t.froxelSampler)!=VK_SUCCESS) return false;
    si.magFilter=si.minFilter=VK_FILTER_NEAREST;
    si.compareEnable=VK_TRUE; si.compareOp=VK_COMPARE_OP_GREATER;
    if(vkCreateSampler(mDevice,&si,nullptr,&t.fallbackShadowSampler)!=VK_SUCCESS) return false;
    VkDescriptorSetLayoutBinding bindings[9]={};
    for(uint32_t i=0;i<2;++i) {
        bindings[i].binding=i; bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount=1; bindings[i].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[0].stageFlags|=VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].stageFlags|=VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].binding=2; bindings[2].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount=1;
    bindings[2].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[3].binding=3; bindings[3].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount=1; bindings[3].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    for(uint32_t i=4;i<7;++i) {
        bindings[i].binding=i; bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount=1; bindings[i].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT;
    }
    bindings[7].binding=7; bindings[7].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[7].descriptorCount=1; bindings[7].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[8].binding=8; bindings[8].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[8].descriptorCount=1; bindings[8].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo li={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount=9; li.pBindings=bindings;
    if(vkCreateDescriptorSetLayout(mDevice,&li,nullptr,&t.setLayout)!=VK_SUCCESS) return false;
    VkDescriptorPoolSize sizes[3]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,7},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
    VkDescriptorPoolCreateInfo dpi={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets=1; dpi.poolSizeCount=3; dpi.pPoolSizes=sizes;
    if(vkCreateDescriptorPool(mDevice,&dpi,nullptr,&t.pool)!=VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dai={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool=t.pool; dai.descriptorSetCount=1; dai.pSetLayouts=&t.setLayout;
    if(vkAllocateDescriptorSets(mDevice,&dai,&t.descriptor)!=VK_SUCCESS) return false;
    VkDescriptorImageInfo images[2]={{t.sampler,t.colourView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {t.sampler,t.depthView,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet writes[2]={};
    for(uint32_t i=0;i<2;++i) {
        writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet=t.descriptor;
        writes[i].dstBinding=i; writes[i].descriptorCount=1;
        writes[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[i].pImageInfo=&images[i];
    }
    vkUpdateDescriptorSets(mDevice,2,writes,0,nullptr);
    VkDescriptorImageInfo volumeImage={t.volumeSampler,t.volumeView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet volumeWrite={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    volumeWrite.dstSet=t.descriptor; volumeWrite.dstBinding=3; volumeWrite.descriptorCount=1;
    volumeWrite.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    volumeWrite.pImageInfo=&volumeImage;
    vkUpdateDescriptorSets(mDevice,1,&volumeWrite,0,nullptr);
    VkDescriptorImageInfo froxelSample={t.froxelSampler,t.froxelView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo froxelStorage={VK_NULL_HANDLE,t.froxelView,VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet froxelWrites[2]={};
    froxelWrites[0].sType=froxelWrites[1].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    froxelWrites[0].dstSet=froxelWrites[1].dstSet=t.descriptor;
    froxelWrites[0].dstBinding=7; froxelWrites[0].descriptorCount=1;
    froxelWrites[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    froxelWrites[0].pImageInfo=&froxelSample;
    froxelWrites[1].dstBinding=8; froxelWrites[1].descriptorCount=1;
    froxelWrites[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    froxelWrites[1].pImageInfo=&froxelStorage;
    vkUpdateDescriptorSets(mDevice,2,froxelWrites,0,nullptr);
    VkDescriptorImageInfo shadowImages[3]={};
    VkWriteDescriptorSet shadowWrites[3]={};
    t.volumeShadowsBound=true;
    for(uint32_t i=0;i<3;++i) {
        const bool ready=mShadowCascades[i].view && mShadowCascades[i].sampler;
        t.volumeShadowsBound=t.volumeShadowsBound && ready;
        shadowImages[i]={ready?mShadowCascades[i].sampler:t.fallbackShadowSampler,
                         ready?mShadowCascades[i].view:t.depthView,
                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        shadowWrites[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadowWrites[i].dstSet=t.descriptor; shadowWrites[i].dstBinding=4+i;
        shadowWrites[i].descriptorCount=1;
        shadowWrites[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadowWrites[i].pImageInfo=&shadowImages[i];
    }
    vkUpdateDescriptorSets(mDevice,3,shadowWrites,0,nullptr);
    VkDescriptorBufferInfo exposureBuffer={t.meterBuffer,0,sizeof(float)*80};
    VkWriteDescriptorSet exposureWrite={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    exposureWrite.dstSet=t.descriptor; exposureWrite.dstBinding=2;
    exposureWrite.descriptorCount=1; exposureWrite.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    exposureWrite.pBufferInfo=&exposureBuffer;
    vkUpdateDescriptorSets(mDevice,1,&exposureWrite,0,nullptr);
    VkAttachmentDescription attachment={}; attachment.format=t.outputFormat;
    attachment.samples=VK_SAMPLE_COUNT_1_BIT; attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass={}; subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount=1; subpass.pColorAttachments=&ref;
    VkSubpassDependency dependency={}; dependency.srcSubpass=VK_SUBPASS_EXTERNAL; dependency.dstSubpass=0;
    dependency.srcStageMask=dependency.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo ri={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ri.attachmentCount=1; ri.pAttachments=&attachment; ri.subpassCount=1; ri.pSubpasses=&subpass;
    ri.dependencyCount=1; ri.pDependencies=&dependency;
    if(vkCreateRenderPass(mDevice,&ri,nullptr,&t.pass)!=VK_SUCCESS) return false;
    attachment.format=VK_FORMAT_R16G16B16A16_SFLOAT;
    attachment.initialLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if(vkCreateRenderPass(mDevice,&ri,nullptr,&t.volumePass)!=VK_SUCCESS) return false;
    for(uint32_t eye=0;eye<t.layers;++eye) {
        vi.image=t.output; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=t.outputFormat;
        vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,t.layers==2?eye:t.layer,1};
        if(vkCreateImageView(mDevice,&vi,nullptr,&t.outputViews[eye])!=VK_SUCCESS) return false;
        VkFramebufferCreateInfo fi={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass=t.pass; fi.attachmentCount=1; fi.pAttachments=&t.outputViews[eye];
        fi.width=t.width; fi.height=t.height; fi.layers=1;
        if(vkCreateFramebuffer(mDevice,&fi,nullptr,&t.framebuffers[eye])!=VK_SUCCESS) return false;
        vi.image=t.volume; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=VK_FORMAT_R16G16B16A16_SFLOAT;
        vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,t.layers==2?eye:t.layer,1};
        VkImageView volumeEye=VK_NULL_HANDLE;
        if(vkCreateImageView(mDevice,&vi,nullptr,&volumeEye)!=VK_SUCCESS) return false;
        t.outputViews[eye+2]=volumeEye;
        fi.renderPass=t.volumePass; fi.pAttachments=&t.outputViews[eye+2];
        fi.width=t.volumeWidth; fi.height=t.volumeHeight;
        if(vkCreateFramebuffer(mDevice,&fi,nullptr,&t.volumeFramebuffers[eye])!=VK_SUCCESS) return false;
    }
    VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,128};
    VkPipelineLayoutCreateInfo pli={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount=1; pli.pSetLayouts=&t.setLayout; pli.pushConstantRangeCount=1; pli.pPushConstantRanges=&push;
    if(vkCreatePipelineLayout(mDevice,&pli,nullptr,&t.layout)!=VK_SUCCESS) return false;
    VkShaderModuleCreateInfo mi={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    VkPipelineLayoutCreateInfo exposureLayoutInfo={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    exposureLayoutInfo.setLayoutCount=1; exposureLayoutInfo.pSetLayouts=&t.setLayout;
    if(vkCreatePipelineLayout(mDevice,&exposureLayoutInfo,nullptr,&t.exposureLayout)!=VK_SUCCESS) return false;
    VkShaderModule exposureModule=VK_NULL_HANDLE;
    mi.codeSize=sizeof(hdr_exposure_comp_spv); mi.pCode=hdr_exposure_comp_spv;
    if(vkCreateShaderModule(mDevice,&mi,nullptr,&exposureModule)!=VK_SUCCESS) return false;
    VkComputePipelineCreateInfo exposureInfo={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    exposureInfo.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    exposureInfo.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
    exposureInfo.stage.module=exposureModule; exposureInfo.stage.pName="main";
    exposureInfo.layout=t.exposureLayout;
    VkResult exposureResult=vkCreateComputePipelines(mDevice,VK_NULL_HANDLE,1,
        &exposureInfo,nullptr,&t.exposurePipeline);
    vkDestroyShaderModule(mDevice,exposureModule,nullptr);
    if(exposureResult!=VK_SUCCESS) return false;
    VkPushConstantRange froxelPush={VK_SHADER_STAGE_COMPUTE_BIT,0,68};
    VkPipelineLayoutCreateInfo froxelLayoutInfo={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    froxelLayoutInfo.setLayoutCount=1; froxelLayoutInfo.pSetLayouts=&t.setLayout;
    froxelLayoutInfo.pushConstantRangeCount=1; froxelLayoutInfo.pPushConstantRanges=&froxelPush;
    if(vkCreatePipelineLayout(mDevice,&froxelLayoutInfo,nullptr,&t.froxelLayout)!=VK_SUCCESS) return false;
    VkShaderModule froxelModule=VK_NULL_HANDLE;
    mi.codeSize=sizeof(volumetric_froxel_comp_spv); mi.pCode=volumetric_froxel_comp_spv;
    if(vkCreateShaderModule(mDevice,&mi,nullptr,&froxelModule)!=VK_SUCCESS) return false;
    VkComputePipelineCreateInfo froxelInfo={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    froxelInfo.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    froxelInfo.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
    froxelInfo.stage.module=froxelModule; froxelInfo.stage.pName="main";
    froxelInfo.layout=t.froxelLayout;
    VkResult froxelResult=vkCreateComputePipelines(mDevice,VK_NULL_HANDLE,1,&froxelInfo,
        nullptr,&t.froxelPipeline);
    vkDestroyShaderModule(mDevice,froxelModule,nullptr);
    if(froxelResult!=VK_SUCCESS) return false;
    VkShaderModule modules[2]={};
    mi.codeSize=sizeof(hdr_resolve_vert_spv); mi.pCode=hdr_resolve_vert_spv;
    if(vkCreateShaderModule(mDevice,&mi,nullptr,&modules[0])!=VK_SUCCESS) return false;
    mi.codeSize=sizeof(hdr_resolve_frag_spv); mi.pCode=hdr_resolve_frag_spv;
    if(vkCreateShaderModule(mDevice,&mi,nullptr,&modules[1])!=VK_SUCCESS) {
        vkDestroyShaderModule(mDevice,modules[0],nullptr); return false;
    }
    struct Settings { VkBool32 srgb; VkBool32 volumeOnly; };
    const Settings settings={t.outputFormat==VK_FORMAT_R8G8B8A8_SRGB ||
        t.outputFormat==VK_FORMAT_B8G8R8A8_SRGB,
        SharOpenXR::IsGiIndirectOnly()?VK_TRUE:VK_FALSE};
    t.volumeOnly=settings.volumeOnly!=VK_FALSE;
    VkSpecializationMapEntry entries[2]={{0,0,4},{3,4,4}};
    VkSpecializationInfo specialization={2,entries,sizeof(settings),&settings};
    VkPipelineShaderStageCreateInfo stages[2]={};
    for(int i=0;i<2;++i) {
        stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module=modules[i]; stages[i].pName="main";
    }
    stages[1].pSpecializationInfo=&specialization;
    VkPipelineVertexInputStateCreateInfo vertex={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport={0,0,float(t.width),float(t.height),0,1};
    VkRect2D scissor={{0,0},{t.width,t.height}};
    VkPipelineViewportStateCreateInfo vp={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount=vp.scissorCount=1; vp.pViewports=&viewport; vp.pScissors=&scissor;
    VkPipelineRasterizationStateCreateInfo raster={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode=VK_POLYGON_MODE_FILL; raster.cullMode=VK_CULL_MODE_NONE; raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend={}; blend.colorWriteMask=0xf;
    VkPipelineColorBlendStateCreateInfo cb={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount=1; cb.pAttachments=&blend;
    VkGraphicsPipelineCreateInfo pi={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.stageCount=2; pi.pStages=stages; pi.pVertexInputState=&vertex; pi.pInputAssemblyState=&assembly;
    pi.pViewportState=&vp; pi.pRasterizationState=&raster; pi.pMultisampleState=&ms;
    pi.pColorBlendState=&cb; pi.layout=t.layout; pi.renderPass=t.pass;
    VkResult result=vkCreateGraphicsPipelines(mDevice,VK_NULL_HANDLE,1,&pi,nullptr,&t.pipeline);
    if(result==VK_SUCCESS) {
        mi.codeSize=sizeof(volumetric_light_frag_spv); mi.pCode=volumetric_light_frag_spv;
        VkShaderModule volumeModule=VK_NULL_HANDLE;
        if(vkCreateShaderModule(mDevice,&mi,nullptr,&volumeModule)==VK_SUCCESS) {
            stages[1].module=volumeModule; stages[1].pSpecializationInfo=nullptr;
            VkViewport volumeViewport={0,0,float(t.volumeWidth),float(t.volumeHeight),0,1};
            VkRect2D volumeScissor={{0,0},{t.volumeWidth,t.volumeHeight}};
            vp.pViewports=&volumeViewport; vp.pScissors=&volumeScissor;
            pi.renderPass=t.volumePass;
            result=vkCreateGraphicsPipelines(mDevice,VK_NULL_HANDLE,1,&pi,nullptr,&t.volumePipeline);
            vkDestroyShaderModule(mDevice,volumeModule,nullptr);
        } else result=VK_ERROR_INITIALIZATION_FAILED;
    }
    for(auto module:modules) vkDestroyShaderModule(mDevice,module,nullptr);
    return result==VK_SUCCESS;
}
bool VulkanContext::ResolveHdrTargets() {
    for(auto& t:mHdrTargets) {
        if(!t.active) continue;
        EndActiveRenderPass();
        CachedDepthTarget* depth=nullptr;
        for(auto& d:mDepthTargetCache) if(d.colourImage==t.colour && d.arrayLayer==t.layer) { depth=&d; break; }
        if(!depth || t.failed) return false;
        const bool volumeOnly=SharOpenXR::IsGiIndirectOnly();
        if(t.pipeline && t.volumeOnly!=volumeOnly) {
            // The old pipeline can still be referenced by any of the three
            // submitted frame arenas.  Destroying it directly from a menu
            // callback races the GPU and eventually causes DEVICE_LOST.
            // Retired session objects are released after vkDeviceWaitIdle().
            mDeferredPipelines.push_back(t.pipeline);
            t.pipeline=VK_NULL_HANDLE;
        }
        if(!t.pipeline) {
            t.failed=!CreateHdrResolve(t,depth->image);
            if(t.failed) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR HDR resolve creation failed"); return false; }
        }
        if(!t.volumeShadowsBound && mShadowCascades[0].view && mShadowCascades[1].view &&
           mShadowCascades[2].view) {
            // HDR targets can be created before the first complete CSM set.
            // Upgrade the fallback descriptors once, after retiring any
            // earlier frame that might still reference this descriptor set.
            if(vkDeviceWaitIdle(mDevice)==VK_SUCCESS) {
                VkDescriptorImageInfo images[3]={};
                VkWriteDescriptorSet writes[3]={};
                for(uint32_t i=0;i<3;++i) {
                    images[i]={mShadowCascades[i].sampler,mShadowCascades[i].view,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
                    writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[i].dstSet=t.descriptor; writes[i].dstBinding=4+i;
                    writes[i].descriptorCount=1;
                    writes[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writes[i].pImageInfo=&images[i];
                }
                vkUpdateDescriptorSets(mDevice,3,writes,0,nullptr);
                t.volumeShadowsBound=true;
                SDL_Log("PCVR volumetric fog: CSM shadow descriptors activated");
            }
        }
        float inverseProjections[2][16]={};
        for(uint32_t eye=0;eye<t.layers;++eye) {
            if(!InvertProjection(t.projection[eye],inverseProjections[eye])) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR volume: invalid eye %u projection",eye);
                return false;
            }
        }
        float* exposure=static_cast<float*>(t.meterMapped);
        const bool volumetricEnabled=SharOpenXR::IsVolumetricLightEnabled();
        // value[14].y is outside the exposure compute shader's two-eye state
        // and is consumed only by the HDR resolve shader.
        exposure[57]=volumetricEnabled?1.0f:0.0f;
        for(uint32_t eye=0;eye<t.layers;++eye)
            std::memcpy(exposure+eye*4+1,mVolumetricSunDirection,sizeof(float)*3);
        // Surface shaders receive centre-view -> shadow clip matrices because
        // their vertices are already in view space.  Froxels are reconstructed
        // in world space; applying those receiver matrices directly makes the
        // shadow field rotate a second time with the headset (most visibly as
        // surrounding silhouettes moving through the sky while looking down).
        // Convert all cascades to absolute world -> shadow clip matrices.
        rmt::Matrix volumeCamera,worldToVolume;
        std::memcpy(volumeCamera.m[0],mVolumetricViewToWorld,sizeof(float)*16);
        worldToVolume.InvertOrtho(volumeCamera);
        for(uint32_t cascade=0;cascade<3;++cascade) {
            rmt::Matrix receiver,worldToShadow;
            std::memcpy(receiver.m[0],mShadowReceiverMatrices+cascade*16,
                        sizeof(float)*16);
            worldToShadow.Mult(worldToVolume,receiver);
            std::memcpy(exposure+8+cascade*16,worldToShadow.m[0],sizeof(float)*16);
        }
        // SetShadowReceiverState is intentionally disabled again after world
        // geometry.  The volume pass runs later, so retain whether its own
        // descriptors actually reference the completed CSM images.
        exposure[56]=t.volumeShadowsBound?1.0f:0.0f;
        std::memcpy(exposure+64,mVolumetricViewToWorld,sizeof(float)*16);
        VkImageMemoryBarrier barriers[2]={};
        for(auto& b:barriers) { b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,t.layers}; }
        barriers[0].image=t.colour; barriers[0].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[0].subresourceRange.baseArrayLayer=t.layers==2?0:t.layer;
        barriers[0].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        barriers[0].oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[0].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[1].image=depth->image; barriers[1].srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barriers[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        barriers[1].oldLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barriers[1].newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barriers[1].subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
        vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,0,nullptr,0,nullptr,2,barriers);
        VkBufferMemoryBarrier exposureInputBarrier={VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        exposureInputBarrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT|
            VK_ACCESS_SHADER_WRITE_BIT;
        exposureInputBarrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        exposureInputBarrier.srcQueueFamilyIndex=exposureInputBarrier.dstQueueFamilyIndex=
            VK_QUEUE_FAMILY_IGNORED;
        exposureInputBarrier.buffer=t.meterBuffer; exposureInputBarrier.size=sizeof(float)*80;
        vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_HOST_BIT|
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,1,&exposureInputBarrier,0,nullptr);
        vkCmdBindPipeline(mCommandBuffer,VK_PIPELINE_BIND_POINT_COMPUTE,t.exposurePipeline);
        vkCmdBindDescriptorSets(mCommandBuffer,VK_PIPELINE_BIND_POINT_COMPUTE,
            t.exposureLayout,0,1,&t.descriptor,0,nullptr);
        vkCmdDispatch(mCommandBuffer,t.layers,1,1);
        VkBufferMemoryBarrier exposureBarrier={VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        exposureBarrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
        exposureBarrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        exposureBarrier.srcQueueFamilyIndex=exposureBarrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        exposureBarrier.buffer=t.meterBuffer; exposureBarrier.size=sizeof(float)*80;
        vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,0,nullptr,1,&exposureBarrier,0,nullptr);
        if(volumetricEnabled) {
        VkImageMemoryBarrier froxelBarrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        froxelBarrier.srcQueueFamilyIndex=froxelBarrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        froxelBarrier.image=t.froxel;
        froxelBarrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,
                                        kFroxelSliceCount*t.layers};
        froxelBarrier.oldLayout=t.froxelInitialized?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                                                    VK_IMAGE_LAYOUT_UNDEFINED;
        froxelBarrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        froxelBarrier.srcAccessMask=t.froxelInitialized?VK_ACCESS_SHADER_READ_BIT:0;
        froxelBarrier.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(mCommandBuffer,t.froxelInitialized?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,0,nullptr,0,nullptr,1,&froxelBarrier);
        vkCmdBindPipeline(mCommandBuffer,VK_PIPELINE_BIND_POINT_COMPUTE,t.froxelPipeline);
        vkCmdBindDescriptorSets(mCommandBuffer,VK_PIPELINE_BIND_POINT_COMPUTE,t.froxelLayout,
            0,1,&t.descriptor,0,nullptr);
        struct FroxelConstants { float inverseProjection[16]; uint32_t eye; } froxelConstants={};
        for(uint32_t eye=0;eye<t.layers;++eye) {
            std::memcpy(froxelConstants.inverseProjection,inverseProjections[eye],64);
            froxelConstants.eye=eye;
            vkCmdPushConstants(mCommandBuffer,t.froxelLayout,VK_SHADER_STAGE_COMPUTE_BIT,
                0,sizeof(froxelConstants),&froxelConstants);
            vkCmdDispatch(mCommandBuffer,(t.froxelWidth+7)/8,(t.froxelHeight+7)/8,1);
        }
        froxelBarrier.oldLayout=VK_IMAGE_LAYOUT_GENERAL;
        froxelBarrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        froxelBarrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
        froxelBarrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&froxelBarrier);
        t.froxelInitialized=true;
        }
        // Resolve samples the cumulative froxel field at native scene depth.
        // No depth-terminated half-resolution image or bilateral blur is needed.
        for(uint32_t eye=0;eye<t.layers;++eye) {
            VkRenderPassBeginInfo begin={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            begin.renderPass=t.pass; begin.framebuffer=t.framebuffers[eye]; begin.renderArea.extent={t.width,t.height};
            vkCmdBeginRenderPass(mCommandBuffer,&begin,VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(mCommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,t.pipeline);
            vkCmdBindDescriptorSets(mCommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,t.layout,0,1,&t.descriptor,0,nullptr);
            float constants[32]; std::memcpy(constants,t.projection[eye],64);
            std::memcpy(constants+16,inverseProjections[eye],64);
            vkCmdPushConstants(mCommandBuffer,t.layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,128,constants);
            vkCmdDraw(mCommandBuffer,3,1,0,eye);
            vkCmdEndRenderPass(mCommandBuffer);
        }
        for(auto& b:barriers) {
            VkImageLayout old=b.oldLayout; b.oldLayout=b.newLayout; b.newLayout=old;
            b.dstAccessMask=b.srcAccessMask; b.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;
        }
        vkCmdPipelineBarrier(mCommandBuffer,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,0,0,nullptr,0,nullptr,2,barriers);
        t.active=false;
        mHdrResolvedOutputs.insert(t.output);
        EndActiveRenderPass();
    }
    return true;
}
void VulkanContext::DestroyHdrTargets() {
    // Called only after device idle and destruction of cached world framebuffers.
    for(auto& t:mHdrTargets) {
        if(t.pipeline) vkDestroyPipeline(mDevice,t.pipeline,nullptr);
        if(t.volumePipeline) vkDestroyPipeline(mDevice,t.volumePipeline,nullptr);
        if(t.exposurePipeline) vkDestroyPipeline(mDevice,t.exposurePipeline,nullptr);
        if(t.froxelPipeline) vkDestroyPipeline(mDevice,t.froxelPipeline,nullptr);
        if(t.layout) vkDestroyPipelineLayout(mDevice,t.layout,nullptr);
        if(t.exposureLayout) vkDestroyPipelineLayout(mDevice,t.exposureLayout,nullptr);
        if(t.froxelLayout) vkDestroyPipelineLayout(mDevice,t.froxelLayout,nullptr);
        for(auto f:t.framebuffers) if(f) vkDestroyFramebuffer(mDevice,f,nullptr);
        for(auto f:t.volumeFramebuffers) if(f) vkDestroyFramebuffer(mDevice,f,nullptr);
        if(t.pass) vkDestroyRenderPass(mDevice,t.pass,nullptr);
        if(t.volumePass) vkDestroyRenderPass(mDevice,t.volumePass,nullptr);
        if(t.pool) vkDestroyDescriptorPool(mDevice,t.pool,nullptr);
        if(t.setLayout) vkDestroyDescriptorSetLayout(mDevice,t.setLayout,nullptr);
        if(t.sampler) vkDestroySampler(mDevice,t.sampler,nullptr);
        if(t.volumeSampler) vkDestroySampler(mDevice,t.volumeSampler,nullptr);
        if(t.froxelSampler) vkDestroySampler(mDevice,t.froxelSampler,nullptr);
        if(t.fallbackShadowSampler) vkDestroySampler(mDevice,t.fallbackShadowSampler,nullptr);
        for(auto v:t.outputViews) if(v) vkDestroyImageView(mDevice,v,nullptr);
        if(t.depthView) vkDestroyImageView(mDevice,t.depthView,nullptr);
        if(t.colourView) vkDestroyImageView(mDevice,t.colourView,nullptr);
        if(t.volumeView) vkDestroyImageView(mDevice,t.volumeView,nullptr);
        if(t.froxelView) vkDestroyImageView(mDevice,t.froxelView,nullptr);
        if(t.colour) vkDestroyImage(mDevice,t.colour,nullptr);
        if(t.volume) vkDestroyImage(mDevice,t.volume,nullptr);
        if(t.froxel) vkDestroyImage(mDevice,t.froxel,nullptr);
        if(t.meterMapped) vkUnmapMemory(mDevice,t.meterMemory);
        if(t.meterBuffer) vkDestroyBuffer(mDevice,t.meterBuffer,nullptr);
        if(t.meterMemory) vkFreeMemory(mDevice,t.meterMemory,nullptr);
        if(t.memory) vkFreeMemory(mDevice,t.memory,nullptr);
        if(t.volumeMemory) vkFreeMemory(mDevice,t.volumeMemory,nullptr);
        if(t.froxelMemory) vkFreeMemory(mDevice,t.froxelMemory,nullptr);
    }
    mHdrTargets.clear();
    mLdrOffscreenTargets.clear();
    mHdrResolvedOutputs.clear();
}
}
#endif

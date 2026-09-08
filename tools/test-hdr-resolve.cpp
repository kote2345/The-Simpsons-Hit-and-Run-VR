// Headless Vulkan integration check for the actual HDR/GI resolve shaders.
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include "../libs/pure3d/pddi/vulkan/shaders/hdr_resolve_vert_spv.h"
#include "../libs/pure3d/pddi/vulkan/shaders/hdr_resolve_frag_spv.h"
#define CHECK(call) do { VkResult r=(call); if(r!=VK_SUCCESS) { std::fprintf(stderr,"%s failed: %d\n",#call,int(r)); std::exit(1); } } while(0)
static unsigned validationErrors=0;
static VKAPI_ATTR VkBool32 VKAPI_CALL ValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* message,void*) {
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++validationErrors; std::fprintf(stderr,"Validation: %s\n",message->pMessage);
    }
    return VK_FALSE;
}
int main() {
    VkInstance instance; VkInstanceCreateInfo ii={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    uint32_t layerCount=0; CHECK(vkEnumerateInstanceLayerProperties(&layerCount,nullptr));
    std::vector<VkLayerProperties> availableLayers(layerCount);
    CHECK(vkEnumerateInstanceLayerProperties(&layerCount,availableLayers.data()));
    bool validation=false;
    for(const auto& layer:availableLayers) if(!std::strcmp(layer.layerName,"VK_LAYER_KHRONOS_validation")) validation=true;
    const char* layerName="VK_LAYER_KHRONOS_validation";
    const char* extensionName=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkDebugUtilsMessengerCreateInfoEXT debug={VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback=ValidationMessage;
    if(validation) {
        ii.enabledLayerCount=1; ii.ppEnabledLayerNames=&layerName;
        ii.enabledExtensionCount=1; ii.ppEnabledExtensionNames=&extensionName; ii.pNext=&debug;
    }
    CHECK(vkCreateInstance(&ii,nullptr,&instance));
    VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;
    if(validation) {
        auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"));
        CHECK(create(instance,&debug,nullptr,&messenger));
    }
    uint32_t count=0; CHECK(vkEnumeratePhysicalDevices(instance,&count,nullptr));
    if(!count) return 2;
    std::vector<VkPhysicalDevice> devices(count); CHECK(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
    VkPhysicalDevice gpu=devices[0];
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,nullptr);
    std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,families.data());
    uint32_t family=0; while(family<count && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) ++family;
    if(family==count) return 2;
    float priority=1; VkDeviceQueueCreateInfo qi={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex=family; qi.queueCount=1; qi.pQueuePriorities=&priority;
    VkDeviceCreateInfo di={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount=1; di.pQueueCreateInfos=&qi;
    VkDevice device; CHECK(vkCreateDevice(gpu,&di,nullptr,&device));
    VkQueue queue; vkGetDeviceQueue(device,family,0,&queue);
    VkPhysicalDeviceMemoryProperties memory; vkGetPhysicalDeviceMemoryProperties(gpu,&memory);
    auto memoryType=[&](uint32_t bits,VkMemoryPropertyFlags flags) {
        for(uint32_t i=0;i<memory.memoryTypeCount;++i)
            if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags) return i;
        std::exit(2); return 0u;
    };
    const uint32_t width=64,height=64,layers=2,pixels=width*height*layers;
    struct Image { VkImage image; VkDeviceMemory memory; VkImageView view; };
    auto createImage=[&](VkFormat format,VkImageUsageFlags usage) {
        Image image={}; VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType=VK_IMAGE_TYPE_2D; ci.format=format; ci.extent={width,height,1};
        ci.mipLevels=1; ci.arrayLayers=layers; ci.samples=VK_SAMPLE_COUNT_1_BIT;
        ci.tiling=VK_IMAGE_TILING_OPTIMAL; ci.usage=usage;
        CHECK(vkCreateImage(device,&ci,nullptr,&image.image));
        VkMemoryRequirements req; vkGetImageMemoryRequirements(device,image.image,&req);
        VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=req.size;
        ai.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CHECK(vkAllocateMemory(device,&ai,nullptr,&image.memory)); CHECK(vkBindImageMemory(device,image.image,image.memory,0));
        VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=image.image; vi.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY; vi.format=format;
        vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,layers}; CHECK(vkCreateImageView(device,&vi,nullptr,&image.view));
        return image;
    };
    Image colour=createImage(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
    Image depth=createImage(VK_FORMAT_R32_SFLOAT,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
    Image volume=createImage(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
    Image output=createImage(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    VkBuffer buffer; VkDeviceMemory bufferMemory; VkBufferCreateInfo bi={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size=(pixels*9+8)*sizeof(float); bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|
        VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    CHECK(vkCreateBuffer(device,&bi,nullptr,&buffer)); VkMemoryRequirements req; vkGetBufferMemoryRequirements(device,buffer,&req);
    VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=req.size;
    ai.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CHECK(vkAllocateMemory(device,&ai,nullptr,&bufferMemory)); CHECK(vkBindBufferMemory(device,buffer,bufferMemory,0));
    void* mapped; CHECK(vkMapMemory(device,bufferMemory,0,VK_WHOLE_SIZE,0,&mapped)); float* data=static_cast<float*>(mapped);
    VkSampler sampler; VkSamplerCreateInfo si={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    CHECK(vkCreateSampler(device,&si,nullptr,&sampler));
    const VkDeviceSize exposureOffset=pixels*9*sizeof(float);
    data[pixels*9]=data[pixels*9+4]=1.0f;
    VkDescriptorSetLayoutBinding bindings[4]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
        {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
        {3,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo sli={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; sli.bindingCount=4; sli.pBindings=bindings;
    VkDescriptorSetLayout setLayout; CHECK(vkCreateDescriptorSetLayout(device,&sli,nullptr,&setLayout));
    VkDescriptorPoolSize poolSizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,3},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
    VkDescriptorPoolCreateInfo dpi={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.maxSets=1; dpi.poolSizeCount=2; dpi.pPoolSizes=poolSizes;
    VkDescriptorPool pool; CHECK(vkCreateDescriptorPool(device,&dpi,nullptr,&pool));
    VkDescriptorSetAllocateInfo sai={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; sai.descriptorPool=pool; sai.descriptorSetCount=1; sai.pSetLayouts=&setLayout;
    VkDescriptorSet set; CHECK(vkAllocateDescriptorSets(device,&sai,&set));
    VkDescriptorImageInfo images[2]={{sampler,colour.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,depth.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet writes[2]={}; for(int i=0;i<2;++i) {
        writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet=set; writes[i].dstBinding=i;
        writes[i].descriptorCount=1; writes[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[i].pImageInfo=&images[i];
    } vkUpdateDescriptorSets(device,2,writes,0,nullptr);
    VkDescriptorImageInfo volumeImage={sampler,volume.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet volumeWrite={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; volumeWrite.dstSet=set;
    volumeWrite.dstBinding=3; volumeWrite.descriptorCount=1;
    volumeWrite.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; volumeWrite.pImageInfo=&volumeImage;
    vkUpdateDescriptorSets(device,1,&volumeWrite,0,nullptr);
    VkDescriptorBufferInfo exposureInfo={buffer,exposureOffset,sizeof(float)*8};
    VkWriteDescriptorSet exposureWrite={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; exposureWrite.dstSet=set;
    exposureWrite.dstBinding=2; exposureWrite.descriptorCount=1;
    exposureWrite.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; exposureWrite.pBufferInfo=&exposureInfo;
    vkUpdateDescriptorSets(device,1,&exposureWrite,0,nullptr);
    VkAttachmentDescription attachment={}; attachment.format=VK_FORMAT_R32G32B32A32_SFLOAT; attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}; VkSubpassDescription subpass={};
    subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount=1; subpass.pColorAttachments=&ref;
    VkSubpassDependency dependency={}; dependency.srcSubpass=0; dependency.dstSubpass=VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dependency.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependency.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; dependency.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo ri={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; ri.attachmentCount=1; ri.pAttachments=&attachment;
    ri.subpassCount=1; ri.pSubpasses=&subpass; ri.dependencyCount=1; ri.pDependencies=&dependency;
    VkRenderPass pass; CHECK(vkCreateRenderPass(device,&ri,nullptr,&pass));
    VkImageView views[2]; VkFramebuffer framebuffers[2]; for(uint32_t eye=0;eye<2;++eye) {
        VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image=output.image; vi.viewType=VK_IMAGE_VIEW_TYPE_2D;
        vi.format=attachment.format; vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,eye,1}; CHECK(vkCreateImageView(device,&vi,nullptr,&views[eye]));
        VkFramebufferCreateInfo fi={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fi.renderPass=pass; fi.attachmentCount=1; fi.pAttachments=&views[eye];
        fi.width=width; fi.height=height; fi.layers=1; CHECK(vkCreateFramebuffer(device,&fi,nullptr,&framebuffers[eye]));
    }
    VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,128}; VkPipelineLayoutCreateInfo pli={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount=1; pli.pSetLayouts=&setLayout; pli.pushConstantRangeCount=1; pli.pPushConstantRanges=&push;
    VkPipelineLayout layout; CHECK(vkCreatePipelineLayout(device,&pli,nullptr,&layout));
    VkShaderModule modules[2]; VkShaderModuleCreateInfo mi={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mi.codeSize=sizeof(hdr_resolve_vert_spv); mi.pCode=hdr_resolve_vert_spv; CHECK(vkCreateShaderModule(device,&mi,nullptr,&modules[0]));
    mi.codeSize=sizeof(hdr_resolve_frag_spv); mi.pCode=hdr_resolve_frag_spv; CHECK(vkCreateShaderModule(device,&mi,nullptr,&modules[1]));
    VkPipelineShaderStageCreateInfo stages[2]={}; for(int i=0;i<2;++i) {
        stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module=modules[i]; stages[i].pName="main";
    }
    VkPipelineVertexInputStateCreateInfo vertex={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport={0,0,float(width),float(height),0,1}; VkRect2D scissor={{0,0},{width,height}};
    VkPipelineViewportStateCreateInfo vp={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount=vp.scissorCount=1; vp.pViewports=&viewport; vp.pScissors=&scissor;
    VkPipelineRasterizationStateCreateInfo raster={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.polygonMode=VK_POLYGON_MODE_FILL; raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend={}; blend.colorWriteMask=15; VkPipelineColorBlendStateCreateInfo cb={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount=1; cb.pAttachments=&blend;
    VkGraphicsPipelineCreateInfo pi={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; pi.stageCount=2; pi.pStages=stages; pi.pVertexInputState=&vertex; pi.pInputAssemblyState=&assembly;
    pi.pViewportState=&vp; pi.pRasterizationState=&raster; pi.pMultisampleState=&ms; pi.pColorBlendState=&cb; pi.layout=layout; pi.renderPass=pass;
    struct Settings { VkBool32 srgb; float gi,exposure; } settings={VK_TRUE,0,1};
    VkSpecializationMapEntry entries[3]={{0,0,4},{1,4,4},{2,8,4}}; VkSpecializationInfo spec={3,entries,sizeof(settings),&settings}; stages[1].pSpecializationInfo=&spec;
    VkPipeline pipelines[2]; CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&pipelines[0]));
    settings.gi=1; CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&pipelines[1]));
    VkCommandPoolCreateInfo cpi={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.queueFamilyIndex=family; cpi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool commandPool; CHECK(vkCreateCommandPool(device,&cpi,nullptr,&commandPool));
    VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool=commandPool; cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount=1;
    VkCommandBuffer command; CHECK(vkAllocateCommandBuffers(device,&cai,&command));
    bool perspective=false;
    auto render=[&](int gi) {
        CHECK(vkResetCommandBuffer(command,0)); VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CHECK(vkBeginCommandBuffer(command,&begin));
        VkImageMemoryBarrier volumeBarrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        volumeBarrier.image=volume.image; volumeBarrier.srcQueueFamilyIndex=volumeBarrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        volumeBarrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2}; volumeBarrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        volumeBarrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; volumeBarrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&volumeBarrier);
        VkClearColorValue clear={}; clear.float32[3]=1.0f;
        vkCmdClearColorImage(command,volume.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&volumeBarrier.subresourceRange);
        volumeBarrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; volumeBarrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        volumeBarrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; volumeBarrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&volumeBarrier);
        for(int i=0;i<2;++i) {
            VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.image=i?depth.image:colour.image;
            b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};
            b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
            VkBufferImageCopy copy={}; copy.bufferOffset=i?pixels*4*sizeof(float):0; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,2}; copy.imageExtent={width,height,1};
            vkCmdCopyBufferToImage(command,buffer,b.image,b.newLayout,1,&copy);
            b.oldLayout=b.newLayout; b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
        }
        float matrices[32]={}; for(int i=0;i<4;++i) matrices[i*5]=matrices[16+i*5]=1;
        if(perspective) {
            const float a=100.1f/99.9f,b=-20.0f/99.9f;
            matrices[10]=a; matrices[11]=1; matrices[14]=b; matrices[15]=0;
            matrices[26]=0; matrices[27]=1/b; matrices[30]=1; matrices[31]=-a/b;
        }
        for(uint32_t eye=0;eye<2;++eye) {
            VkRenderPassBeginInfo rb={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; rb.renderPass=pass; rb.framebuffer=framebuffers[eye]; rb.renderArea.extent={width,height};
            vkCmdBeginRenderPass(command,&rb,VK_SUBPASS_CONTENTS_INLINE); vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines[gi]);
            vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,nullptr);
            vkCmdPushConstants(command,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,128,matrices); vkCmdDraw(command,3,1,0,eye); vkCmdEndRenderPass(command);
        }
        VkBufferImageCopy copy={}; copy.bufferOffset=pixels*5*sizeof(float); copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,2}; copy.imageExtent={width,height,1};
        vkCmdCopyImageToBuffer(command,output.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
        VkMemoryBarrier barrier={VK_STRUCTURE_TYPE_MEMORY_BARRIER}; barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        CHECK(vkEndCommandBuffer(command)); VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&command;
        CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)); CHECK(vkQueueWaitIdle(queue));
        return std::vector<float>(data+pixels*5,data+pixels*9);
    };
    auto tone=[](float x) { return (x*(2.51f*x+0.03f))/(x*(2.43f*x+0.59f)+0.14f); };
    for(uint32_t i=0;i<pixels;++i) {
        bool right=i>=width*height; data[i*4]=right?0.1f:4.0f; data[i*4+1]=right?4.0f:0.1f; data[i*4+2]=0.1f; data[i*4+3]=1;
        data[pixels*4+i]=1;
    }
    auto result=render(0);
    for(uint32_t i=0;i<pixels;++i) for(int c=0;c<3;++c)
        if(std::abs(result[i*4+c]-tone(data[i*4+c]))>0.0002f) { std::fprintf(stderr,"HDR/stereo mismatch\n"); return 1; }
    std::printf("HDR >1 tone mapping, zero-volume composite and stereo isolation: PASS\n");
    CHECK(vkDeviceWaitIdle(device));
    vkDestroyCommandPool(device,commandPool,nullptr);
    for(auto p:pipelines) vkDestroyPipeline(device,p,nullptr);
    for(auto m:modules) vkDestroyShaderModule(device,m,nullptr);
    vkDestroyPipelineLayout(device,layout,nullptr);
    for(auto f:framebuffers) vkDestroyFramebuffer(device,f,nullptr);
    for(auto v:views) vkDestroyImageView(device,v,nullptr);
    vkDestroyRenderPass(device,pass,nullptr); vkDestroyDescriptorPool(device,pool,nullptr);
    vkDestroyDescriptorSetLayout(device,setLayout,nullptr); vkDestroySampler(device,sampler,nullptr);
    vkUnmapMemory(device,bufferMemory); vkDestroyBuffer(device,buffer,nullptr); vkFreeMemory(device,bufferMemory,nullptr);
    for(auto image:{colour,depth,volume,output}) { vkDestroyImageView(device,image.view,nullptr); vkDestroyImage(device,image.image,nullptr); vkFreeMemory(device,image.memory,nullptr); }
    vkDestroyDevice(device,nullptr);
    if(messenger) {
        auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"));
        destroy(instance,messenger,nullptr);
    }
    vkDestroyInstance(instance,nullptr);
    std::printf("Vulkan validation: %s, errors=%u\n",validation?"enabled":"unavailable",validationErrors);
    return validationErrors==0?0:1;
}

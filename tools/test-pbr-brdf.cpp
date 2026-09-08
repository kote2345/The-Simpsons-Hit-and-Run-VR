#include <vulkan/vulkan.h>
#include <vector>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#define CHECK(x) do { VkResult r=(x); if(r!=VK_SUCCESS) { std::fprintf(stderr,"%s: %d\n",#x,r); std::exit(1); } } while(0)
int main() {
    VkInstance instance; VkInstanceCreateInfo ii={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    CHECK(vkCreateInstance(&ii,nullptr,&instance));
    uint32_t count=0; CHECK(vkEnumeratePhysicalDevices(instance,&count,nullptr));
    if(!count) return 1;
    std::vector<VkPhysicalDevice> devices(count); CHECK(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
    VkPhysicalDevice gpu=devices[0];
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,nullptr);
    std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,families.data());
    uint32_t family=0; while(family<count && !(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT)) ++family;
    if(family==count) return 1;
    float priority=1; VkDeviceQueueCreateInfo qi={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex=family; qi.queueCount=1; qi.pQueuePriorities=&priority;
    VkDeviceCreateInfo di={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount=1; di.pQueueCreateInfos=&qi;
    VkDevice device; CHECK(vkCreateDevice(gpu,&di,nullptr,&device));
    VkQueue queue; vkGetDeviceQueue(device,family,0,&queue);
    VkBufferCreateInfo bi={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size=64*4*sizeof(float); bi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBuffer buffer; CHECK(vkCreateBuffer(device,&bi,nullptr,&buffer));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device,buffer,&req);
    VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(gpu,&props);
    uint32_t type=0; const auto flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while(type<props.memoryTypeCount && (!(req.memoryTypeBits&(1u<<type)) || (props.memoryTypes[type].propertyFlags&flags)!=flags)) ++type;
    if(type==props.memoryTypeCount) return 1;
    VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=req.size; ai.memoryTypeIndex=type;
    VkDeviceMemory memory; CHECK(vkAllocateMemory(device,&ai,nullptr,&memory)); CHECK(vkBindBufferMemory(device,buffer,memory,0));
    VkDescriptorSetLayoutBinding binding={0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo li={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; li.bindingCount=1; li.pBindings=&binding;
    VkDescriptorSetLayout setLayout; CHECK(vkCreateDescriptorSetLayout(device,&li,nullptr,&setLayout));
    VkDescriptorPoolSize poolSize={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
    VkDescriptorPoolCreateInfo dpi={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.maxSets=1; dpi.poolSizeCount=1; dpi.pPoolSizes=&poolSize;
    VkDescriptorPool pool; CHECK(vkCreateDescriptorPool(device,&dpi,nullptr,&pool));
    VkDescriptorSetAllocateInfo sai={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; sai.descriptorPool=pool; sai.descriptorSetCount=1; sai.pSetLayouts=&setLayout;
    VkDescriptorSet set; CHECK(vkAllocateDescriptorSets(device,&sai,&set));
    VkDescriptorBufferInfo dbi={buffer,0,bi.size}; VkWriteDescriptorSet write={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet=set; write.descriptorCount=1; write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo=&dbi;
    vkUpdateDescriptorSets(device,1,&write,0,nullptr);
    VkPipelineLayoutCreateInfo pli={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.setLayoutCount=1; pli.pSetLayouts=&setLayout;
    VkPipelineLayout layout; CHECK(vkCreatePipelineLayout(device,&pli,nullptr,&layout));
    std::ifstream file("build/pcvr/test-pbr-brdf.spv",std::ios::binary|std::ios::ate);
    if(!file) return 1;
    size_t bytes=static_cast<size_t>(file.tellg()); std::vector<uint32_t> code(bytes/4);
    file.seekg(0); file.read(reinterpret_cast<char*>(code.data()),bytes); if(!file) return 1;
    VkShaderModuleCreateInfo mi={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; mi.codeSize=bytes; mi.pCode=code.data();
    VkShaderModule module; CHECK(vkCreateShaderModule(device,&mi,nullptr,&module));
    VkComputePipelineCreateInfo pi={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; pi.layout=layout;
    pi.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; pi.stage.module=module; pi.stage.pName="main";
    VkPipeline pipeline; CHECK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline));
    VkCommandPoolCreateInfo cpi={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.queueFamilyIndex=family;
    VkCommandPool commands; CHECK(vkCreateCommandPool(device,&cpi,nullptr,&commands));
    VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool=commands; cai.commandBufferCount=1;
    VkCommandBuffer command; CHECK(vkAllocateCommandBuffers(device,&cai,&command));
    VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CHECK(vkBeginCommandBuffer(command,&begin));
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);
    vkCmdDispatch(command,1,1,1);
    VkMemoryBarrier barrier={VK_STRUCTURE_TYPE_MEMORY_BARRIER}; barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    CHECK(vkEndCommandBuffer(command));
    VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&command;
    CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)); CHECK(vkQueueWaitIdle(queue));
    void* mapped; CHECK(vkMapMemory(device,memory,0,bi.size,0,&mapped)); const float* values=static_cast<float*>(mapped);
    bool pass=true; float minimum=10,maximum=0;
    for(int i=0;i<64;++i) {
        const float* v=values+4*i;
        for(int j=0;j<4;++j) if(!std::isfinite(v[j])) pass=false;
        minimum=std::fmin(minimum,v[0]); maximum=std::fmax(maximum,v[0]);
        // Single-scattering GGX may lose energy at high roughness, but cannot
        // create it. IBL compensation restores a white conductor to unity.
        if(v[0]<0.20f || v[0]>1.02f || std::fabs(v[1]-1)>0.001f || v[2]!=0 || v[3]<0) {
            std::printf("FAIL case %d: furnace=%f IBL=%f backlight=%f highlight=%f\n",i,v[0],v[1],v[2],v[3]); pass=false;
        }
    }
    std::printf("%s: 64 GPU BRDF cases; white furnace range %.5f..%.5f\n",pass?"PASS":"FAIL",minimum,maximum);
    vkUnmapMemory(device,memory); vkDestroyCommandPool(device,commands,nullptr);
    vkDestroyPipeline(device,pipeline,nullptr); vkDestroyShaderModule(device,module,nullptr);
    vkDestroyPipelineLayout(device,layout,nullptr); vkDestroyDescriptorPool(device,pool,nullptr);
    vkDestroyDescriptorSetLayout(device,setLayout,nullptr); vkDestroyBuffer(device,buffer,nullptr);
    vkFreeMemory(device,memory,nullptr); vkDestroyDevice(device,nullptr); vkDestroyInstance(instance,nullptr);
    return pass?0:1;
}

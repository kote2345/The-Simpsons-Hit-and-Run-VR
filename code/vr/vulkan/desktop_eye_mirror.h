#ifndef SHAR_DESKTOP_EYE_MIRROR_H
#define SHAR_DESKTOP_EYE_MIRROR_H

// Desktop-only GPU presentation of OpenXR array layer 1 (the right eye).
#include <SDL_syswm.h>
#include <vulkan/vulkan_win32.h>
#include <algorithm>
#include <limits>
#include <vector>

namespace SharOpenXR
{
class DesktopEyeMirror
{
    VkSurfaceKHR surface=VK_NULL_HANDLE;
    VkSwapchainKHR swapchain=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE;
    VkCommandBuffer command=VK_NULL_HANDLE;
    VkSemaphore acquired=VK_NULL_HANDLE;
    std::vector<VkImage> images;
    VkExtent2D extent={};
    int windowWidth=0,windowHeight=0;
    bool failed=false;

    bool Check(VkResult result,const char* operation)
    {
        if(result==VK_SUCCESS)return true;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR mirror: %s failed (%d)",operation,int(result));
        failed=true;
        return false;
    }
    void DropSwapchain()
    {
        VulkanContext& context=GetVulkanContext();
        vkQueueWaitIdle(context.GetQueue());
        if(swapchain)vkDestroySwapchainKHR(context.GetDevice(),swapchain,NULL);
        swapchain=VK_NULL_HANDLE;
        images.clear();
    }
    bool Prepare(SDL_Window* window,int width,int height,VkFormat sourceFormat)
    {
        VulkanContext& context=GetVulkanContext();
        const VkDevice device=context.GetDevice();
        if(!surface)
        {
            SDL_SysWMinfo wm={};SDL_VERSION(&wm.version);
            if(!SDL_GetWindowWMInfo(window,&wm)||wm.subsystem!=SDL_SYSWM_WINDOWS)
            {SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR mirror: window handle unavailable");failed=true;return false;}
            const auto createSurface=reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
                vkGetInstanceProcAddr(context.GetInstance(),"vkCreateWin32SurfaceKHR"));
            if(!createSurface){failed=true;return false;}
            VkWin32SurfaceCreateInfoKHR info={VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
            info.hinstance=wm.info.win.hinstance;info.hwnd=wm.info.win.window;
            if(!Check(createSurface(context.GetInstance(),&info,NULL,&surface),"create surface"))return false;
            VkBool32 supported=VK_FALSE;
            if(!Check(vkGetPhysicalDeviceSurfaceSupportKHR(context.GetPhysicalDevice(),
                context.GetQueueFamilyIndex(),surface,&supported),"surface support"))return false;
            if(!supported){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR mirror: VR queue cannot present to this monitor");failed=true;return false;}
            VkCommandPoolCreateInfo poolInfo={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            poolInfo.queueFamilyIndex=context.GetQueueFamilyIndex();
            poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            if(!Check(vkCreateCommandPool(device,&poolInfo,NULL,&pool),"command pool"))return false;
            VkCommandBufferAllocateInfo allocate={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocate.commandPool=pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
            if(!Check(vkAllocateCommandBuffers(device,&allocate,&command),"command buffer"))return false;
            VkSemaphoreCreateInfo semaphoreInfo={VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            if(!Check(vkCreateSemaphore(device,&semaphoreInfo,NULL,&acquired),"acquire semaphore"))return false;
        }
        if(swapchain&&width==windowWidth&&height==windowHeight)return true;
        DropSwapchain();
        VkSurfaceCapabilitiesKHR caps={};
        if(!Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.GetPhysicalDevice(),surface,&caps),"surface capabilities"))return false;
        uint32_t count=0;
        if(!Check(vkGetPhysicalDeviceSurfaceFormatsKHR(context.GetPhysicalDevice(),surface,&count,NULL),"surface formats"))return false;
        std::vector<VkSurfaceFormatKHR> formats(count);
        if(!count||!Check(vkGetPhysicalDeviceSurfaceFormatsKHR(context.GetPhysicalDevice(),surface,&count,formats.data()),"surface formats"))return false;
        VkSurfaceFormatKHR format=formats[0];
        const bool sourceSrgb=sourceFormat==VK_FORMAT_R8G8B8A8_SRGB||sourceFormat==VK_FORMAT_B8G8R8A8_SRGB;
        for(const auto& candidate:formats)
        {
            const bool candidateSrgb=candidate.format==VK_FORMAT_R8G8B8A8_SRGB||candidate.format==VK_FORMAT_B8G8R8A8_SRGB;
            if(candidate.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR&&candidateSrgb==sourceSrgb)
                format=candidate;
        }
        for(const auto& candidate:formats)
            if(candidate.format==sourceFormat){format=candidate;break;}
        if(format.format==VK_FORMAT_UNDEFINED)format.format=sourceFormat;
        VkFormatProperties src={},dst={};
        vkGetPhysicalDeviceFormatProperties(context.GetPhysicalDevice(),sourceFormat,&src);
        vkGetPhysicalDeviceFormatProperties(context.GetPhysicalDevice(),format.format,&dst);
        if(!(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT)||
           !(src.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)||
           !(dst.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))
        {SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"PCVR mirror: image blit unsupported");failed=true;return false;}
        extent=caps.currentExtent;
        if(extent.width==std::numeric_limits<uint32_t>::max())
        {
            extent.width=std::max(caps.minImageExtent.width,std::min(caps.maxImageExtent.width,uint32_t(width)));
            extent.height=std::max(caps.minImageExtent.height,std::min(caps.maxImageExtent.height,uint32_t(height)));
        }
        if(!extent.width||!extent.height)return false;
        if(!Check(vkGetPhysicalDeviceSurfacePresentModesKHR(context.GetPhysicalDevice(),surface,&count,NULL),"present modes"))return false;
        std::vector<VkPresentModeKHR> modes(count);
        if(!Check(vkGetPhysicalDeviceSurfacePresentModesKHR(context.GetPhysicalDevice(),surface,&count,modes.data()),"present modes"))return false;
        VkPresentModeKHR mode=VK_PRESENT_MODE_FIFO_KHR;
        for(auto candidate:modes)if(candidate==VK_PRESENT_MODE_MAILBOX_KHR)mode=candidate;
        for(auto candidate:modes)if(candidate==VK_PRESENT_MODE_IMMEDIATE_KHR)mode=candidate;
        VkSwapchainCreateInfoKHR info={VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface=surface;info.minImageCount=caps.minImageCount+1;
        if(caps.maxImageCount)info.minImageCount=std::min(info.minImageCount,caps.maxImageCount);
        info.imageFormat=format.format;info.imageColorSpace=format.colorSpace;info.imageExtent=extent;
        info.imageArrayLayers=1;info.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;info.preTransform=caps.currentTransform;
        for(auto alpha:{VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                       VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if(caps.supportedCompositeAlpha&alpha){info.compositeAlpha=alpha;break;}
        info.presentMode=mode;info.clipped=VK_TRUE;
        if(!Check(vkCreateSwapchainKHR(device,&info,NULL,&swapchain),"create swapchain"))return false;
        if(!Check(vkGetSwapchainImagesKHR(device,swapchain,&count,NULL),"swapchain images"))return false;
        images.resize(count);
        if(!Check(vkGetSwapchainImagesKHR(device,swapchain,&count,images.data()),"swapchain images"))return false;
        windowWidth=width;windowHeight=height;
        SDL_Log("PCVR mirror: right eye, window %ux%u",extent.width,extent.height);
        return true;
    }
public:
    void Shutdown()
    {
        VulkanContext& context=GetVulkanContext();
        if(context.GetDevice())
        {
            DropSwapchain();
            if(acquired)vkDestroySemaphore(context.GetDevice(),acquired,NULL);
            if(pool)vkDestroyCommandPool(context.GetDevice(),pool,NULL);
        }
        if(surface&&context.GetInstance())vkDestroySurfaceKHR(context.GetInstance(),surface,NULL);
        surface=VK_NULL_HANDLE;pool=VK_NULL_HANDLE;command=VK_NULL_HANDLE;acquired=VK_NULL_HANDLE;
        failed=false;windowWidth=windowHeight=0;
    }
    void Present(SDL_Window* window,VkImage source,VkFormat sourceFormat,int eyeWidth,int eyeHeight)
    {
        if(failed)return;
        if(!window||(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED))return;
        int width=0,height=0;SDL_GetWindowSize(window,&width,&height);
        if(width<=0||height<=0||!Prepare(window,width,height,sourceFormat))return;
        VulkanContext& context=GetVulkanContext();
        uint32_t index=0;
        const VkResult acquire=vkAcquireNextImageKHR(context.GetDevice(),swapchain,0,acquired,VK_NULL_HANDLE,&index);
        if(acquire==VK_NOT_READY||acquire==VK_TIMEOUT)return;
        if(acquire==VK_ERROR_OUT_OF_DATE_KHR){DropSwapchain();return;}
        if(acquire!=VK_SUBOPTIMAL_KHR&&!Check(acquire,"acquire image"))return;
        if(!Check(vkResetCommandBuffer(command,0),"reset command buffer"))return;
        VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if(!Check(vkBeginCommandBuffer(command,&begin),"begin command buffer"))return;
        VkImageMemoryBarrier barriers[2]={};
        for(auto& barrier:barriers)
        {
            barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount=barrier.subresourceRange.layerCount=1;
        }
        barriers[0].image=source;barriers[0].subresourceRange.baseArrayLayer=1;
        barriers[0].oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[0].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        barriers[1].image=images[index];barriers[1].oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,0,NULL,0,NULL,2,barriers);
        VkClearColorValue black={};
        vkCmdClearColorImage(command,images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&black,1,&barriers[1].subresourceRange);
        // Order the background clear before the blit writes the same image.
        VkImageMemoryBarrier clearBarrier=barriers[1];
        clearBarrier.oldLayout=clearBarrier.newLayout;
        clearBarrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&clearBarrier);
        const float scale=std::min(float(extent.width)/eyeWidth,float(extent.height)/eyeHeight);
        const int drawWidth=std::max(1,int(eyeWidth*scale)),drawHeight=std::max(1,int(eyeHeight*scale));
        const int x=(int(extent.width)-drawWidth)/2,y=(int(extent.height)-drawHeight)/2;
        VkImageBlit blit={};blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,1,1};
        blit.srcOffsets[1]={eyeWidth,eyeHeight,1};blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        blit.dstOffsets[0]={x,y,0};blit.dstOffsets[1]={x+drawWidth,y+drawHeight,1};
        VkFormatProperties properties={};vkGetPhysicalDeviceFormatProperties(context.GetPhysicalDevice(),sourceFormat,&properties);
        const VkFilter filter=(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
        vkCmdBlitImage(command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,filter);
        barriers[0].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[0].newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barriers[0].dstAccessMask=0;
        barriers[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barriers[1].newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barriers[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barriers[1].dstAccessMask=0;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,2,barriers);
        if(!Check(vkEndCommandBuffer(command),"end command buffer"))return;
        VkPipelineStageFlags waitStage=VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=1;
        submit.pWaitSemaphores=&acquired;submit.pWaitDstStageMask=&waitStage;
        submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        if(!Check(vkQueueSubmit(context.GetQueue(),1,&submit,VK_NULL_HANDLE),"submit mirror"))return;
        // Complete the copy while the application still owns the XR image.
        // This also consumes the acquire semaphore before its next reuse.
        if(!Check(vkQueueWaitIdle(context.GetQueue()),"finish mirror"))return;
        VkPresentInfoKHR present={VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.swapchainCount=1;present.pSwapchains=&swapchain;present.pImageIndices=&index;
        const VkResult result=vkQueuePresentKHR(context.GetQueue(),&present);
        if(result==VK_ERROR_OUT_OF_DATE_KHR||result==VK_SUBOPTIMAL_KHR)DropSwapchain();
        else Check(result,"present mirror");
    }
};
}
#endif

#ifndef SHAR_OPENXR_SHARED_GRAPHICS_H
#define SHAR_OPENXR_SHARED_GRAPHICS_H

#if defined(SRR2_OPENXR)
#include <openxr/openxr.h>
#include <radmath/radmath.hpp>
#if defined(SRR2_VR_RENDERER_VULKAN)
#include <vulkan/vulkan.h>
#endif
namespace SharOpenXR
{
#if defined(SRR2_VR_RENDERER_VULKAN)
struct VulkanEyeTarget
{
    VkImage image; VkFormat format;
    uint32_t width,height,arrayLayer;
    bool firstUse;
};
VkFormat NormalizeVulkanRenderFormat(VkFormat format);
VkFormat ChooseSharedVulkanSwapchainFormat(const int64_t* formats,
                                           uint32_t count);
bool BuildSharedVulkanEyeTarget(VkImage image,VkFormat format,
    uint32_t width,uint32_t height,unsigned activeEye,
    bool multiviewRendering,bool multiviewTargetActive,
    VulkanEyeTarget* target);
#endif
bool GetSharedEyeProjection(const XrView views[2],const int widths[2],
    const int heights[2],unsigned activeEye,bool worldRendering,
    bool embeddedHudRendering,float nearPlane,float farPlane,
    rmt::Matrix* projection,int* width,int* height);
bool GetSharedEyeViewport(const int widths[2],const int heights[2],
    unsigned activeEye,int* width,int* height);
bool GetSharedUiHorizontalOffset(const XrView views[2],unsigned activeEye,
    bool worldRendering,float planeDistance,float* offset);
}
#endif
#endif

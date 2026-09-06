#if defined(SRR2_OPENXR)
#include <vr/openxr_shared_graphics.h>
#include <vr/openxr_shared_render.h>
#include <cmath>
namespace SharOpenXR
{
#if defined(SRR2_VR_RENDERER_VULKAN)
VkFormat NormalizeVulkanRenderFormat(VkFormat format)
{
    if(format==VK_FORMAT_R8G8B8A8_SRGB)return VK_FORMAT_R8G8B8A8_UNORM;
    if(format==VK_FORMAT_B8G8R8A8_SRGB)return VK_FORMAT_B8G8R8A8_UNORM;
    return format;
}
VkFormat ChooseSharedVulkanSwapchainFormat(const int64_t* formats,uint32_t count)
{
    const VkFormat preferred[]={VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_SRGB,VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM};
    for(VkFormat candidate:preferred)
        for(uint32_t i=0;formats&&i<count;++i)
            if(formats[i]==candidate)return candidate;
    return count&&formats?static_cast<VkFormat>(formats[0]):VK_FORMAT_R8G8B8A8_UNORM;
}
bool BuildSharedVulkanEyeTarget(VkImage image,VkFormat format,
    uint32_t width,uint32_t height,unsigned activeEye,
    bool multiviewRendering,bool multiviewTargetActive,VulkanEyeTarget* target)
{
    if(!target||image==VK_NULL_HANDLE||activeEye>=2||!width||!height)return false;
    target->image=image;target->format=NormalizeVulkanRenderFormat(format);
    target->width=width;target->height=height;
    target->arrayLayer=multiviewRendering&&multiviewTargetActive?2u:activeEye;
    target->firstUse=false;return true;
}
#endif
bool GetSharedEyeProjection(const XrView views[2],const int widths[2],
    const int heights[2],unsigned activeEye,bool worldRendering,
    bool embeddedHudRendering,float nearPlane,float farPlane,
    rmt::Matrix* projection,int* width,int* height)
{
    if(!views||!widths||!heights||!projection||activeEye>=2||
       !worldRendering||embeddedHudRendering)return false;
    SharedRender::MakeProjection(views[activeEye].fov,nearPlane,farPlane,projection);
    if(width)*width=widths[activeEye];if(height)*height=heights[activeEye];
    return true;
}
bool GetSharedEyeViewport(const int widths[2],const int heights[2],
    unsigned activeEye,int* width,int* height)
{
    if(!widths||!heights||activeEye>=2)return false;
    if(width)*width=widths[activeEye];if(height)*height=heights[activeEye];
    return true;
}
bool GetSharedUiHorizontalOffset(const XrView views[2],unsigned activeEye,
    bool worldRendering,float planeDistance,float* offset)
{
    if(!views||!offset||activeEye>=2||worldRendering||planeDistance<=0.0f)return false;
    const XrFovf& fov=views[activeEye].fov;
    const float left=std::tan(fov.angleLeft),right=std::tan(fov.angleRight);
    const XrVector3f centre={
        (views[0].pose.position.x+views[1].pose.position.x)*0.5f,
        (views[0].pose.position.y+views[1].pose.position.y)*0.5f,
        (views[0].pose.position.z+views[1].pose.position.z)*0.5f};
    const XrVector3f delta={views[activeEye].pose.position.x-centre.x,
        views[activeEye].pose.position.y-centre.y,
        views[activeEye].pose.position.z-centre.z};
    const XrVector3f localEye=SharedRender::Rotate(
        SharedRender::Conjugate(views[activeEye].pose.orientation),delta);
    const float tangentX=-localEye.x/planeDistance;
    *offset=0.48f*(2.0f*tangentX-(right+left))/(right-left);
    return true;
}
}
#endif

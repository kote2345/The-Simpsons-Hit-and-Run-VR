#ifndef SHAR_OPENXR_SHARED_FRAME_H
#define SHAR_OPENXR_SHARED_FRAME_H

#if defined(SRR2_OPENXR) && defined(SRR2_VR_RENDERER_VULKAN)
#include <openxr/openxr.h>
#include <vulkan/vulkan.h>
#include <radmath/radmath.hpp>

namespace SharOpenXR
{
class VulkanContext;
struct SharedSystemRecenterState;

struct SharedFrameApi
{
    PFN_xrWaitFrame waitFrame;
    PFN_xrBeginFrame beginFrame;
    PFN_xrLocateViews locateViews;
    PFN_xrAcquireSwapchainImage acquireImage;
    PFN_xrWaitSwapchainImage waitImage;
    PFN_xrReleaseSwapchainImage releaseImage;
    PFN_xrEndFrame endFrame;
};

struct SharedFrameState
{
    XrFrameState frame;
    XrViewState viewState;
    XrView views[2];
    uint32_t imageIndex;
    bool begun,shouldRender,imageAcquired;
    SharedFrameState();
};

typedef void (*SharedSessionFocusCallback)(void* userData);
struct SharedSessionApi
{
    PFN_xrBeginSession beginSession;
    PFN_xrEndSession endSession;
    SharedSessionFocusCallback lostFocus;
    void* userData;
};
XrResult ApplySharedSessionState(const SharedSessionApi& api,XrSession session,
    XrSessionState state,bool* running,bool* exitRequested=0);
XrResult HandleSharedRuntimeEvent(const XrEventDataBuffer& event,
    const SharedSessionApi& api,XrSession session,bool* running,
    SharedSystemRecenterState* recenter,XrSessionState* sessionState,
    bool* exitRequested=0);

// One Vulkan render-frame state machine used by every OpenXR backend.
// Backends only provide the acquired image and the HUD callback; the ordering
// of world multiview, the two mono GUI layers, HUD presentation and command
// buffer submission lives here.
typedef void (*SharedVulkanPresentCallback)(void* userData,unsigned eye);
struct SharedVulkanRenderSequence
{
    enum Phase { Idle, World, GuiLeft, GuiRight };
    Phase phase;
    SharedVulkanRenderSequence():phase(Idle){}
};
bool BeginSharedVulkanMultiview(VulkanContext& context,VkImage image,
    bool firstUse,uint32_t width,uint32_t height,
    SharedVulkanRenderSequence* sequence);
bool BeginSharedVulkanGuiEye(VulkanContext& context,
    SharedVulkanRenderSequence* sequence,unsigned eye,
    SharedVulkanPresentCallback present,void* userData);
bool EndSharedVulkanMultiview(VulkanContext& context,
    SharedVulkanRenderSequence* sequence,
    SharedVulkanPresentCallback present,void* userData);

bool BeginSharedFrame(const SharedFrameApi& api,XrSession session,
                      XrSpace space,SharedFrameState* state);
bool AcquireSharedFrameImage(const SharedFrameApi& api,XrSwapchain swapchain,
                             SharedFrameState* state);
bool BeginSharedVulkanTarget(VulkanContext& context,VkImage image,bool firstUse,
                             uint32_t layer,uint32_t width,uint32_t height);
bool ReleaseSharedFrameImage(const SharedFrameApi& api,XrSwapchain swapchain,
                             SharedFrameState* state);
XrResult EndSharedFrame(const SharedFrameApi& api,XrSession session,
                        XrSpace space,XrSwapchain swapchain,
                        const int widths[2],const int heights[2],
                        SharedFrameState* state,float irisBlackoutAlpha=0.0f,
                        bool colorScaleBiasEnabled=false);
bool BuildSharedMultiviewCameras(const XrPosef& origin,const XrView views[2],
    const rmt::Matrix& baseCamera,rmt::Matrix projections[2],
    rmt::Matrix adjustments[2],rmt::Matrix* centreCamera);
}
#endif
#endif

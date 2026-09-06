#if defined(SRR2_OPENXR) && defined(SRR2_VR_RENDERER_VULKAN)
#include <vr/openxr_shared_frame.h>
#include <vr/openxr_shared_render.h>
#include <vr/openxr_shared_tracking.h>
#include <vr/vulkan/openxr_vulkan_context.h>
#include <algorithm>

namespace SharOpenXR
{
SharedFrameState::SharedFrameState():frame{XR_TYPE_FRAME_STATE},
    viewState{XR_TYPE_VIEW_STATE},imageIndex(0),begun(false),
    shouldRender(false),imageAcquired(false)
{
    views[0]=XrView{XR_TYPE_VIEW};views[1]=XrView{XR_TYPE_VIEW};
}

XrResult ApplySharedSessionState(const SharedSessionApi& api,XrSession session,
    XrSessionState state,bool* running,bool* exitRequested)
{
    if(!running)return XR_ERROR_VALIDATION_FAILURE;
    const SharedRender::SessionOperation operation=SharedRender::GetSessionOperation(state);
    if(operation==SharedRender::SESSION_BEGIN)
    {
        if(!api.beginSession)return XR_ERROR_FUNCTION_UNSUPPORTED;
        XrSessionBeginInfo begin={XR_TYPE_SESSION_BEGIN_INFO};
        begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        const XrResult result=api.beginSession(session,&begin);
        *running=XR_SUCCEEDED(result);return result;
    }
    if(operation==SharedRender::SESSION_END)
    {
        if(api.lostFocus)api.lostFocus(api.userData);
        const XrResult result=api.endSession?api.endSession(session):XR_ERROR_FUNCTION_UNSUPPORTED;
        *running=false;return result;
    }
    if(operation==SharedRender::SESSION_EXIT)
    {
        if(api.lostFocus)api.lostFocus(api.userData);
        *running=false;if(exitRequested)*exitRequested=true;
    }
    return XR_SUCCESS;
}

XrResult HandleSharedRuntimeEvent(const XrEventDataBuffer& event,
    const SharedSessionApi& api,XrSession session,bool* running,
    SharedSystemRecenterState* recenter,XrSessionState* sessionState,
    bool* exitRequested)
{
    if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
    {
        const XrEventDataSessionStateChanged* changed=
            reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
        if(sessionState)*sessionState=changed->state;
        return ApplySharedSessionState(api,session,changed->state,running,exitRequested);
    }
    if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)
    {
        const XrEventDataReferenceSpaceChangePending* changed=
            reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&event);
        QueueSharedSystemRecenter(recenter,changed->changeTime);
    }
    return XR_SUCCESS;
}

bool BeginSharedFrame(const SharedFrameApi& api,XrSession session,
                      XrSpace space,SharedFrameState* state)
{
    if(!state||!api.waitFrame||!api.beginFrame||!api.locateViews)return false;
    *state=SharedFrameState();XrFrameWaitInfo wait={XR_TYPE_FRAME_WAIT_INFO};
    if(XR_FAILED(api.waitFrame(session,&wait,&state->frame)))return false;
    XrFrameBeginInfo begin={XR_TYPE_FRAME_BEGIN_INFO};
    if(XR_FAILED(api.beginFrame(session,&begin)))return false;
    state->begun=true;state->shouldRender=state->frame.shouldRender!=XR_FALSE;
    if(!state->shouldRender)return true;
    XrViewLocateInfo locate={XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime=state->frame.predictedDisplayTime;locate.space=space;
    uint32_t count=0;
    if(XR_FAILED(api.locateViews(session,&locate,&state->viewState,2,&count,
                                 state->views))||count!=2||
       !SharedRender::HasValidViewTracking(state->viewState.viewStateFlags))
        state->shouldRender=false;
    return true;
}

bool AcquireSharedFrameImage(const SharedFrameApi& api,XrSwapchain swapchain,
                             SharedFrameState* state)
{
    if(!state||!state->begun||!state->shouldRender||!api.acquireImage||
       !api.waitImage)return false;
    XrSwapchainImageAcquireInfo acquire={XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(XR_FAILED(api.acquireImage(swapchain,&acquire,&state->imageIndex)))return false;
    state->imageAcquired=true;
    XrSwapchainImageWaitInfo wait={XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout=XR_INFINITE_DURATION;
    if(XR_SUCCEEDED(api.waitImage(swapchain,&wait)))return true;
    // Acquire transfers ownership even when the subsequent wait fails.
    // Returning it here prevents exhausting the finite runtime swapchain.
    ReleaseSharedFrameImage(api,swapchain,state);
    return false;
}

bool BeginSharedVulkanTarget(VulkanContext& context,VkImage image,bool firstUse,
                             uint32_t layer,uint32_t width,uint32_t height)
{
    return image!=VK_NULL_HANDLE&&context.BeginPddiEye()&&
        context.ClearImageInPddiEye(image,firstUse,layer,width,height);
}

bool BeginSharedVulkanMultiview(VulkanContext& context,VkImage image,
    bool firstUse,uint32_t width,uint32_t height,
    SharedVulkanRenderSequence* sequence)
{
    if(!sequence||sequence->phase!=SharedVulkanRenderSequence::Idle)return false;
    if(!BeginSharedVulkanTarget(context,image,firstUse,2,width,height))return false;
    sequence->phase=SharedVulkanRenderSequence::World;
    return true;
}

bool BeginSharedVulkanGuiEye(VulkanContext& context,
    SharedVulkanRenderSequence* sequence,unsigned eye,
    SharedVulkanPresentCallback present,void* userData)
{
    if(!sequence||eye>1)return false;
    const SharedVulkanRenderSequence::Phase expected=eye==0?
        SharedVulkanRenderSequence::World:SharedVulkanRenderSequence::GuiLeft;
    if(sequence->phase!=expected)return false;
    if(eye==1&&present)present(userData,0);
    context.EndActiveRenderPass();
    sequence->phase=eye==0?SharedVulkanRenderSequence::GuiLeft:
                           SharedVulkanRenderSequence::GuiRight;
    return true;
}

bool EndSharedVulkanMultiview(VulkanContext& context,
    SharedVulkanRenderSequence* sequence,
    SharedVulkanPresentCallback present,void* userData)
{
    if(!sequence||sequence->phase!=SharedVulkanRenderSequence::GuiRight)return false;
    if(present)present(userData,1);
    context.EndActiveRenderPass();
    const bool submitted=context.EndPddiEye();
    sequence->phase=SharedVulkanRenderSequence::Idle;
    return submitted;
}

bool ReleaseSharedFrameImage(const SharedFrameApi& api,XrSwapchain swapchain,
                             SharedFrameState* state)
{
    if(!state||!state->imageAcquired)return true;
    XrSwapchainImageReleaseInfo release={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    const bool ok=api.releaseImage&&XR_SUCCEEDED(api.releaseImage(swapchain,&release));
    state->imageAcquired=false;return ok;
}

XrResult EndSharedFrame(const SharedFrameApi& api,XrSession session,
                        XrSpace space,XrSwapchain swapchain,
                        const int widths[2],const int heights[2],
                        SharedFrameState* state,float irisBlackoutAlpha,
                        bool colorScaleBiasEnabled)
{
    if(!state||!state->begun||!api.endFrame)return XR_ERROR_CALL_ORDER_INVALID;
    XrCompositionLayerProjectionView projectionViews[2];
    XrCompositionLayerProjection layer;
    SharedRender::BuildStereoProjectionLayer(state->views,swapchain,widths,
        heights,space,projectionViews,&layer);
    XrCompositionLayerColorScaleBiasKHR fade={
        XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR};
    if(colorScaleBiasEnabled&&irisBlackoutAlpha>0.0f)
    {
        const float scale=1.0f-std::max(0.0f,std::min(1.0f,irisBlackoutAlpha));
        fade.colorScale=XrColor4f{scale,scale,scale,1.0f};
        fade.colorBias=XrColor4f{0.0f,0.0f,0.0f,0.0f};
        layer.next=&fade;
    }
    const XrCompositionLayerBaseHeader* layers[]={
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
    XrFrameEndInfo end=SharedRender::BuildFrameEndInfo(
        state->frame.predictedDisplayTime,state->shouldRender,layers);
    const XrResult result=api.endFrame(session,&end);state->begun=false;
    state->shouldRender=false;
    return result;
}

bool BuildSharedMultiviewCameras(const XrPosef& origin,const XrView views[2],
    const rmt::Matrix& baseCamera,rmt::Matrix projections[2],
    rmt::Matrix adjustments[2],rmt::Matrix* centreCamera)
{
    if(!projections||!adjustments||!centreCamera)return false;
    rmt::Matrix eyes[2],worldToEye;
    for(unsigned eye=0;eye<2;++eye)
        SharedRender::ComposeTrackedCamera(origin,views[eye].pose,baseCamera,&eyes[eye]);
    SharedRender::ComposeTrackedCentreCamera(origin,views[0].pose,views[1].pose,
        baseCamera,centreCamera);
    for(unsigned eye=0;eye<2;++eye){worldToEye.InvertOrtho(eyes[eye]);
        adjustments[eye].Mult(*centreCamera,worldToEye);
        SharedRender::MakeProjection(views[eye].fov,0.1f,8000.0f,&projections[eye]);}
    return true;
}
}
#endif

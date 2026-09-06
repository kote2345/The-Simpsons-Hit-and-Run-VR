#ifndef OPENXR_SHARED_HUD_H
#define OPENXR_SHARED_HUD_H

#include <openxr/openxr.h>
#include <radmath/radmath.hpp>

#if defined(SRR2_VR_RENDERER_VULKAN)
#include <vulkan/vulkan.h>
#include <vr/vulkan/openxr_vulkan_context.h>
#endif

namespace SharOpenXR
{

struct SharedHudRuntime
{
    unsigned activeEye;
    bool multiviewImageAcquired;
    bool embeddedHudRendering;
    bool cullingBaseValid;
    bool vrModeEnabled;
    XrPosef origin;
    XrView views[2];
    int eyeWidth[2];
    int eyeHeight[2];
    rmt::Matrix cullingBaseCamera;
    XrPosef handPoses[2];
    bool handPoseValid[2];
    rmt::Vector activeWheelCentre;
#if defined(SRR2_VR_RENDERER_VULKAN)
    VkImage renderImage;
    VkFormat renderFormat;
#endif
};

typedef bool (*SharedHudRuntimeProvider)(SharedHudRuntime* runtime);

void SetSharedHudRuntimeProvider(SharedHudRuntimeProvider provider);
void SharedHudBeginFrame();
void SharedHudResetVisible();
void ShutdownSharedHud();
void DrawSharedRadarPlane();
void DrawSharedGameplayHud();
void DrawSharedMissionHudPlanes();
void DrawSharedPauseCoinIcon();
void ApplySharedIrisBlackout();
float UpdateSharedHudIrisAlpha();

#if defined(SRR2_VR_RENDERER_VULKAN)
bool GetSharedHudCaptureTarget(VulkanEyeTarget* target);
#endif

}

#endif

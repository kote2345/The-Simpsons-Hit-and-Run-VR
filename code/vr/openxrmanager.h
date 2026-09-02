#ifndef SHAR_OPENXR_MANAGER_H
#define SHAR_OPENXR_MANAGER_H

#if defined(RAD_ANDROID)

#include <radmath/radmath.hpp>
#if defined(SRR2_VR_RENDERER_VULKAN)
#include <vulkan/vulkan.h>
#endif

class tCamera;

namespace SharOpenXR
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    struct VulkanEyeTarget
    {
        VkImage image;
        VkFormat format;
        uint32_t width;
        uint32_t height;
        uint32_t arrayLayer;
        bool firstUse;
    };
    bool GetActiveVulkanEyeTarget(VulkanEyeTarget* target);
#endif
    bool Initialize();
    void Shutdown();
    void PollEvents();
    bool BeginFrame();
    unsigned GetEyeCount();
    bool BeginEye(unsigned eye);
    void EndEye(unsigned eye);
    bool IsMultiviewAvailable();
    bool IsMultiviewRendering();
    void SetMultiviewTargetActive(bool active);
    bool BeginMultiview();
    bool BeginMultiviewGuiEye(unsigned eye);
    void EndMultiview();
    bool PrepareMultiviewCamera(tCamera* baseCamera);
    bool GetMultiviewMatrices(rmt::Matrix* projections,
                              rmt::Matrix* viewAdjustments);
    void SetWorldRendering(bool enabled);
    void SetEmbeddedHudRendering(bool enabled);
    bool IsEmbeddedHudRendering() __attribute__((weak));
    void SetRadarRendering(bool enabled);
    bool BeginRadarCapture(int xMin,int yMin,int xMax,int yMax);
    void EndRadarCapture();
    bool BeginMissionHudCapture(unsigned slot, int xMin, int yMin,
                                int xMax, int yMax);
    void UpdateMissionHudLayout(unsigned slot,const rmt::Matrix& layout);
    void EndMissionHudCapture();
    void ResetMissionHudSlot(unsigned slot);
    void CaptureSpatialCoinIcon();
    void SetSpatialCoinAuthoredPosition(int x,int y,bool visible);
    void SetMissionObjectiveFrameRect(int xMin,int yMin,int xMax,int yMax);
    void SetMissionObjectiveIconRect(int xMin,int yMin,int xMax,int yMax);
    void SetRadarAuthoredRect(int xMin,int yMin,int xMax,int yMax);
    bool IsRadarRendering() __attribute__((weak));
    void SetGameplayHudScreen(const void* screen);
    bool IsGameplayHudScreen(const void* screen);
    bool BeginGameplayHudCapture();
    void EndGameplayHudCapture();
    bool IsGameplayHudCaptureActive() __attribute__((weak));
    bool IsMissionHudCaptureActive() __attribute__((weak));
    bool IsRightEyeRendering();
    void PrepareRadarDraw() __attribute__((weak));
    bool GetActiveRadarProjection(rmt::Matrix* projection, int* width,
                                  int* height) __attribute__((weak));
    void SetMovieRendering(bool enabled);
    bool IsMovieRendering() __attribute__((weak));
    void BeginMoviePlane();
    void EndMoviePlane();
    bool GetActiveMovieProjection(rmt::Matrix* projection, int* width,
                                  int* height) __attribute__((weak));
    void SetFrontendPlaneActive(bool active);
    void SetFrontendPlaneRendering(bool rendering);
    bool IsFrontendPlaneRendering() __attribute__((weak));
    bool GetActiveFrontendProjection(rmt::Matrix* projection, int* width,
                                     int* height) __attribute__((weak));
    void SetPauseCoinVisible(bool visible);
    void DrawPauseCoinIcon();
    void SetIrisBlackout(bool black);
    void SetEnhancedUiConvergence(bool enabled);
    bool HasEnhancedUiConvergence() __attribute__((weak));
    bool GetEyeCamera(unsigned eye, tCamera* baseCamera,
                      rmt::Matrix* cameraToWorld);
    bool GetActiveEyeCamera(tCamera* baseCamera,
                            rmt::Matrix* cameraToWorld);
    bool GetActiveCullingCamera(rmt::Matrix* cameraToWorld);
    bool GetLatestCullingCamera(rmt::Matrix* cameraToWorld);
    // Returns the gameplay camera before the tracked HMD pose is applied.
    // Simulation-facing presentation such as the rotating radar must use
    // this heading, otherwise looking around also rotates the road map.
    bool GetGameplayCamera(rmt::Matrix* cameraToWorld);
    void SetVrModeEnabled(bool enabled);
    bool IsVrModeEnabled();
    bool IsSpatialHudEnabled();
    void SetDeveloperMenusEnabled(bool enabled);
    bool IsDeveloperMenusEnabled();
    void SetVrBaseHeading(const rmt::Vector& heading);
    bool RecenterVrPose();
    bool GetPhysicalHeadHeight(float* heightMetres);
    bool ConsumeRoomscaleMovement(rmt::Vector* worldDelta);
    void SetSeatedMode(bool enabled);
    bool IsSeatedMode();
    void SetSnapTurnEnabled(bool enabled);
    bool IsSnapTurnEnabled();
    void SetSmoothTurnSpeed(float degreesPerSecond);
    float GetSmoothTurnSpeed();
    void SetSnapTurnAngle(float degrees);
    float GetSnapTurnAngle();
    void SetCsmEnabled(bool enabled);
    bool IsCsmEnabled();
    void SetEnhancedMaterialsEnabled(bool enabled);
    bool IsEnhancedMaterialsEnabled();
    void SetCustomMaterialsEnabled(bool enabled);
    bool AreCustomMaterialsEnabled();
    void SetEnhancedMaterialModel(int model);
    int GetEnhancedMaterialModel();
    void SetGtaoEnabled(bool enabled);
    bool IsGtaoEnabled();
    void SetVehicleLightMode(int mode);
    int GetVehicleLightMode();
    void SetReflectionMode(int mode);
    int GetReflectionMode();
    void SetPbrDebugMode(int mode);
    int GetPbrDebugMode();
    void SetVrSteeringWheelEnabled(bool enabled);
    bool IsVrSteeringWheelEnabled();
    void SetVehicleControlMode(int mode);
    int GetVehicleControlMode();
    bool IsThirdPersonVehicleMode();
    bool GetVrSteeringWheelValue(float* value);
    void SetRenderScale(float scale);
    float GetRenderScale();
    void SetRefreshRate(float hz);
    float GetRefreshRate();
    void ApplyGtao();
    bool IsHorizontalMenuInputDominant();
    bool IsVerticalMenuInputDominant();
    bool IsRightEyeRendering();
    bool GetHeadForward(rmt::Vector* forward);
    bool GetControllerWorldPose(unsigned hand, tCamera* baseCamera,
                                rmt::Matrix* controllerToWorld);
    bool GetControllerLocalPose(unsigned hand, rmt::Matrix* controllerPose);
    void RenderControllerHands(tCamera* baseCamera);
    void RecordPddiDraw(unsigned primitiveType,unsigned vertexCount,bool indexed,
                        double cpuMilliseconds);
    void RecordPddiMaterial(bool changed,double cpuMilliseconds);
    void RecordPddiUpload(unsigned bytes,double cpuMilliseconds);
    void RecordRenderSection(unsigned section,double cpuMilliseconds);
    void EndFrame();

    // Queried by the GLES PDDI backend whenever a view changes projection.
    bool GetActiveProjection(rmt::Matrix* projection, int* width, int* height)
        __attribute__((weak));
    bool GetActiveViewport(int* width, int* height)
        __attribute__((weak));
    bool GetActiveUiHorizontalOffset(float* offset)
        __attribute__((weak));
}

#endif
#endif

#ifndef SHAR_OPENXR_SHARED_STATE_H
#define SHAR_OPENXR_SHARED_STATE_H

#if defined(SRR2_OPENXR)
#include <radmath/radmath.hpp>

namespace SharOpenXR
{
// Runtime-independent VR gameplay/configuration state. OpenXR backends may
// consume these values, but must never own a second platform-specific copy.
struct SharedVrState
{
    bool vrModeEnabled;
    bool seatedMode;
    bool snapTurnEnabled;
    bool csmEnabled;
    bool enhancedMaterialsEnabled;
    bool gtaoEnabled;
    bool vehicleComfortEnabled;
    bool customMaterialsEnabled;
    bool spatialHudEnabled;
    bool developerMenusEnabled;
    bool giIndirectOnly;
    bool enhancedUiConvergence;
    bool volumetricLightEnabled;
    bool hdrEnabled;
    int enhancedMaterialModel;
    int vehicleControlMode;
    int vehicleLightMode;
    int reflectionMode;
    int pbrDebugMode;
    float smoothTurnSpeed;
    float snapTurnAngle;
    float renderScale;
    float appliedRenderScale;
    float refreshRate;
    bool renderScalePending;
    bool menuHorizontalInputDominant;
    bool menuVerticalInputDominant;
    unsigned menuAxisLock;
    unsigned menuAxisNeutralFrames;
    bool vrBaseHeadingValid;
    rmt::Vector vrBaseHeading;
    bool roomscaleMovementSuspended;
    bool wheelGrabbed[2];
    bool wheelHonk;
    float gripValue[2],wheelGrabAngle[2],wheelGrabOffset[2],wheelAngle;
    float wheelVisualAngle,wheelGrabTarget[2],wheelTrim,yokeThrottle;
    bool yokeFullGasLatched,yokeFullBrakeLatched,stickClick[2];
    bool wheelAdjustMode;
    float wheelAdjustHoldSec;
    char wheelAdjustVehicle[48];
    rmt::Vector activeWheelCentre,activeYokeAnchor;
    float activeWheelYaw,activeWheelPitch,activeWheelRadius;
    bool wheelMeshHidden;
    float wheelGrabOrientAngle[2];
    rmt::Matrix wheelGrabOrientRot[2];

    SharedVrState();
};

SharedVrState& GetSharedVrState();
}

#endif
#endif

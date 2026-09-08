#include <vr/openxr_shared_state.h>

#if defined(SRR2_OPENXR)
namespace SharOpenXR
{
SharedVrState::SharedVrState()
    : vrModeEnabled(true),seatedMode(false),snapTurnEnabled(true),
      csmEnabled(true),enhancedMaterialsEnabled(true),gtaoEnabled(true),
      vehicleComfortEnabled(true),customMaterialsEnabled(true),
      spatialHudEnabled(true),developerMenusEnabled(false),
      giIndirectOnly(false),enhancedUiConvergence(false),
      volumetricLightEnabled(true),hdrEnabled(true),
      enhancedMaterialModel(1),vehicleControlMode(0),vehicleLightMode(1),
      reflectionMode(0),pbrDebugMode(0),smoothTurnSpeed(120.0f),
      snapTurnAngle(45.0f),renderScale(1.0f),appliedRenderScale(1.0f),
      refreshRate(72.0f),renderScalePending(false),
      menuHorizontalInputDominant(false),menuVerticalInputDominant(false),
      menuAxisLock(0),menuAxisNeutralFrames(0),vrBaseHeadingValid(false),
      vrBaseHeading(0.0f,0.0f,1.0f),roomscaleMovementSuspended(false),
      wheelHonk(false),wheelAngle(0.0f),wheelVisualAngle(0.0f),
      wheelTrim(0.0f),yokeThrottle(0.0f),yokeFullGasLatched(false),
      yokeFullBrakeLatched(false),wheelAdjustMode(false),
      wheelAdjustHoldSec(0.0f),activeWheelCentre(0.0f,-0.32f,0.52f),
      activeYokeAnchor(0.0f,-0.33f,0.40f),activeWheelYaw(0.0f),
      activeWheelPitch(0.0f),activeWheelRadius(0.18f),wheelMeshHidden(false)
{
    for(unsigned i=0;i<2;++i)
    {
        wheelGrabbed[i]=false;gripValue[i]=wheelGrabAngle[i]=0.0f;
        wheelGrabOffset[i]=wheelGrabTarget[i]=0.0f;stickClick[i]=false;
        wheelGrabOrientAngle[i]=0.0f;wheelGrabOrientRot[i].Identity();
    }
    wheelAdjustVehicle[0]=0;
}

SharedVrState& GetSharedVrState()
{
    static SharedVrState state;
    return state;
}
}
#endif

#ifndef SHAR_OPENXR_SHARED_VEHICLE_H
#define SHAR_OPENXR_SHARED_VEHICLE_H

#include <radmath/radmath.hpp>
#include <openxr/openxr.h>

namespace SharOpenXR
{
struct VrVehicleInput
{
    bool active;
    bool yoke;
    const char* vehicleName;
    rmt::Matrix handPose[2];
    bool handValid[2];
    float grip[2];
    bool stickClick[2];
    float deltaSeconds;
};

typedef void (*VrVehicleHapticSink)(void* context,unsigned hand,
                                    float amplitude,unsigned durationMs);

void ResetVrVehicleState();
// Keep the seated player/NPC meshes consistent for every OpenXR backend.
// This must run even when no physical-wheel control mode is selected.
void UpdateVrInCarCharacterVisibility();
void UpdateVrVehicleState(const VrVehicleInput& input,
                          VrVehicleHapticSink haptic,void* context);
void UpdateTrackedVrVehicle(bool originValid,const XrPosef& origin,
    const XrPosef handPoses[2],const bool handValid[2],
    VrVehicleHapticSink haptic,void* context);
bool GetVrVehicleSteering(float* value,bool yoke);
bool GetVrVehicleHandPose(unsigned hand,bool yoke,rmt::Matrix* pose);
bool IsVrYokeVehicle(const char* vehicleName);
void GetVrVehicleInputOverrides(bool yoke,float rawLeftTrigger,
    float rawRightTrigger,float leftGrip,float rightGrip,bool leftStickClick,
    bool rightStickClick,float* leftTrigger,float* rightTrigger,
    bool* leftGripButton,bool* rightGripButton,bool* leftThumbButton,
    bool* rightThumbButton);
void RenderVrVehicleControls(const rmt::Matrix& cameraBase,bool yoke,
                             const rmt::Matrix handPose[2],
                             const bool handValid[2]);
}

#endif

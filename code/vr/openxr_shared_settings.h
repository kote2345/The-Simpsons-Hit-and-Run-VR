#ifndef SHAR_OPENXR_SHARED_SETTINGS_H
#define SHAR_OPENXR_SHARED_SETTINGS_H

namespace SharOpenXR
{
// Loads the platform-independent VR options from SDL's per-user preference
// directory. Safe to call from either runtime; values are loaded only once.
void LoadVrSettings();
bool SaveVrSettings();
typedef bool (*VrSettingBackendCallback)(void* userData,float value);
bool SetSharedRenderScale(float scale,VrSettingBackendCallback queueBackend,void* userData);
bool SetSharedRefreshRate(float hz,VrSettingBackendCallback applyBackend,void* userData);
bool ApplyPendingSharedRenderScale(VrSettingBackendCallback recreateBackend,void* userData);
void SetSharedVrModeEnabled(bool enabled);
void SetSharedVehicleControlMode(int mode);
bool IsSharedSpatialHudEnabled();
void SetSharedDeveloperMenusEnabled(bool enabled);
void SetSharedSeatedMode(bool enabled);
void SetSharedSnapTurnEnabled(bool enabled);
void SetSharedSmoothTurnSpeed(float value);
void SetSharedSnapTurnAngle(float value);
void SetSharedCsmEnabled(bool enabled);
void SetSharedEnhancedMaterialsEnabled(bool enabled);
void SetSharedCustomMaterialsEnabled(bool enabled);
void SetSharedEnhancedMaterialModel(int model);
void SetSharedGtaoEnabled(bool enabled);
void SetSharedVehicleLightMode(int mode);
void SetSharedReflectionMode(int mode);
void SetSharedPbrDebugMode(int mode);
void SetSharedVehicleComfortEnabled(bool enabled);
}

#endif

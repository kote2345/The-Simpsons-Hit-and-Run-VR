#ifndef VR_CSM_BRIDGE_H
#define VR_CSM_BRIDGE_H

#include <pddi/pddi.hpp>

#if defined(SRR2_OPENXR)
bool VrBeginSunShadowMap(pddiRenderContext* context,int cascadeIndex,
                         const pddiMatrix& eyeCameraToWorld,
                         pddiMatrix* lightWorldToCamera,
                         pddiMatrix* lightCameraToWorld);
void VrEndSunShadowMap(pddiRenderContext* context,int cascadeIndex,
                       const pddiMatrix& eyeCameraToWorld);
void VrEnableSunShadowReceivers(pddiRenderContext* context,bool enable);
void VrBeginSunShadowOverlay(pddiRenderContext* context);
void VrEndSunShadowOverlay(pddiRenderContext* context);
#endif

#endif

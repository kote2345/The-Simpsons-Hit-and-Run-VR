#ifndef VR_DYNAMICCUBEMAP_H
#define VR_DYNAMICCUBEMAP_H

#if defined(RAD_ANDROID)
#include <radmath/radmath.hpp>

class pddiRenderContext;

bool VrBeginVehicleCubeMapFace(pddiRenderContext* context,int face);
void VrEndVehicleCubeMapFace(pddiRenderContext* context,int face);
bool VrHasDynamicVehicleCubeMap();
bool VrIsDynamicVehicleCubeMapCapture();
void VrSetVehicleCubeMapTransparentSuppression(bool suppress);
void VrRestoreVehicleCubeMapRendering(pddiRenderContext* context);
void VrBindDynamicVehicleCubeMap();
#endif

#endif

#include <vr/openxr_shared_settings.h>
#include <vr/openxr_shared_state.h>
#include <vr/openxr_shared_vehicle.h>
#include <input/inputmanager.h>
#include <input/usercontroller.h>
#include <SDL.h>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

namespace SharOpenXR
{
namespace
{
bool loaded=false;

std::string PreferenceFile(const char* name)
{
    char* path=SDL_GetPrefPath("c4rlox","simpsons");
    if(!path)return std::string();
    std::string result(path);SDL_free(path);result+=name;return result;
}

void ReadBool(const char* value,bool& target){target=std::atoi(value)!=0;}
void ReadInt(const char* value,int& target,int low,int high)
{target=std::max(low,std::min(high,std::atoi(value)));}
void ReadFloat(const char* value,float& target,float low,float high)
{target=std::max(low,std::min(high,static_cast<float>(std::atof(value))));}
}

void LoadVrSettings()
{
    if(loaded)return;loaded=true;
    SharedVrState& s=GetSharedVrState();

    // Keep compatibility with existing installs while both platforms migrate
    // to the single shared settings writer.
    const std::string modePath=PreferenceFile("vrmode.cfg");
    if(!modePath.empty())if(FILE* file=std::fopen(modePath.c_str(),"rb"))
    {s.vrModeEnabled=std::fgetc(file)=='1';std::fclose(file);}

    const std::string path=PreferenceFile("vrsettings.cfg");
    if(path.empty())return;
    FILE* file=std::fopen(path.c_str(),"rb");if(!file)return;
    char line[256];
    while(std::fgets(line,sizeof(line),file))
    {
        char key[64],value[128];
        if(std::sscanf(line,"%63[^=]=%127s",key,value)!=2)continue;
        if(!std::strcmp(key,"vrMode"))ReadBool(value,s.vrModeEnabled);
        else if(!std::strcmp(key,"seated"))ReadBool(value,s.seatedMode);
        else if(!std::strcmp(key,"snap"))ReadBool(value,s.snapTurnEnabled);
        else if(!std::strcmp(key,"smooth"))ReadFloat(value,s.smoothTurnSpeed,1.0f,720.0f);
        else if(!std::strcmp(key,"angle"))ReadFloat(value,s.snapTurnAngle,1.0f,180.0f);
        else if(!std::strcmp(key,"csm"))ReadBool(value,s.csmEnabled);
        else if(!std::strcmp(key,"enhancedMaterials"))ReadBool(value,s.enhancedMaterialsEnabled);
        else if(!std::strcmp(key,"gtao"))ReadBool(value,s.gtaoEnabled);
        else if(!std::strcmp(key,"renderScale"))ReadFloat(value,s.renderScale,0.10f,2.0f);
        else if(!std::strcmp(key,"refreshRate"))ReadFloat(value,s.refreshRate,1.0f,240.0f);
        else if(!std::strcmp(key,"vrSteeringWheel"))ReadInt(value,s.vehicleControlMode,0,2);
        else if(!std::strcmp(key,"vehicleLights"))ReadInt(value,s.vehicleLightMode,0,2);
        else if(!std::strcmp(key,"spatialHud"))ReadBool(value,s.spatialHudEnabled);
        else if(!std::strcmp(key,"developerMenus"))ReadBool(value,s.developerMenusEnabled);
        else if(!std::strcmp(key,"materialModel"))ReadInt(value,s.enhancedMaterialModel,0,3);
        else if(!std::strcmp(key,"reflectionMode"))ReadInt(value,s.reflectionMode,0,2);
        else if(!std::strcmp(key,"pbrDebugMode"))ReadInt(value,s.pbrDebugMode,0,4);
        else if(!std::strcmp(key,"giIndirectOnly"))ReadBool(value,s.giIndirectOnly);
        else if(!std::strcmp(key,"volumetricLight"))ReadBool(value,s.volumetricLightEnabled);
        else if(!std::strcmp(key,"hdr"))ReadBool(value,s.hdrEnabled);
        else if(!std::strcmp(key,"customMaterials"))ReadBool(value,s.customMaterialsEnabled);
        else if(!std::strcmp(key,"vehicleComfort"))ReadBool(value,s.vehicleComfortEnabled);
    }
    std::fclose(file);
    if(s.volumetricLightEnabled) s.hdrEnabled=true;
    s.enhancedMaterialsEnabled=s.enhancedMaterialModel!=0;
    s.appliedRenderScale=s.renderScale;s.renderScalePending=false;
}

bool SaveVrSettings()
{
    const SharedVrState& s=GetSharedVrState();
    const std::string path=PreferenceFile("vrsettings.cfg");
    if(path.empty())return false;
    FILE* file=std::fopen(path.c_str(),"wb");if(!file)return false;
    std::fprintf(file,
        "vrMode=%d\nseated=%d\nsnap=%d\nsmooth=%.1f\nangle=%.1f\ncsm=%d\n"
        "enhancedMaterials=%d\ngtao=%d\nrenderScale=%.3f\nrefreshRate=%.0f\n"
        "vrSteeringWheel=%d\nvehicleLights=%d\nspatialHud=%d\ndeveloperMenus=%d\n"
        "materialModel=%d\nreflectionMode=%d\npbrDebugMode=%d\ngiIndirectOnly=%d\ncustomMaterials=%d\n"
        "vehicleComfort=%d\nvolumetricLight=%d\nhdr=%d\n",
        s.vrModeEnabled?1:0,s.seatedMode?1:0,s.snapTurnEnabled?1:0,
        s.smoothTurnSpeed,s.snapTurnAngle,s.csmEnabled?1:0,
        s.enhancedMaterialsEnabled?1:0,s.gtaoEnabled?1:0,s.renderScale,s.refreshRate,
        s.vehicleControlMode,s.vehicleLightMode,s.spatialHudEnabled?1:0,
        s.developerMenusEnabled?1:0,s.enhancedMaterialModel,s.reflectionMode,
        s.pbrDebugMode,s.giIndirectOnly?1:0,s.customMaterialsEnabled?1:0,
        s.vehicleComfortEnabled?1:0,s.volumetricLightEnabled?1:0,s.hdrEnabled?1:0);
    const bool ok=std::fclose(file)==0;

    // Continue writing the legacy one-byte file for older builds.
    const std::string modePath=PreferenceFile("vrmode.cfg");
    if(!modePath.empty())if(FILE* mode=std::fopen(modePath.c_str(),"wb"))
    {std::fputc(s.vrModeEnabled?'1':'0',mode);std::fclose(mode);}
    return ok;
}

bool SetSharedRenderScale(float scale,VrSettingBackendCallback callback,void* userData)
{
    SharedVrState& s=GetSharedVrState();scale=std::max(0.10f,std::min(2.0f,scale));
    if(std::fabs(scale-s.renderScale)<0.001f)return true;
    if(callback&&!callback(userData,scale))return false;
    s.renderScale=scale;s.renderScalePending=std::fabs(scale-s.appliedRenderScale)>=0.001f;
    SaveVrSettings();return true;
}

bool SetSharedRefreshRate(float hz,VrSettingBackendCallback callback,void* userData)
{
    SharedVrState& s=GetSharedVrState();
    if(hz!=72.0f&&hz!=90.0f&&hz!=120.0f)return false;
    if(std::fabs(hz-s.refreshRate)<0.1f)return true;
    if(callback&&!callback(userData,hz))return false;
    s.refreshRate=hz;SaveVrSettings();return true;
}

bool ApplyPendingSharedRenderScale(VrSettingBackendCallback callback,void* userData)
{
    SharedVrState& s=GetSharedVrState();if(!s.renderScalePending)return true;
    const float requested=s.renderScale,fallback=s.appliedRenderScale;
    if(callback&&callback(userData,requested))
    { s.appliedRenderScale=requested;s.renderScalePending=false;SaveVrSettings();return true; }
    s.renderScale=fallback;
    if(!callback||!callback(userData,fallback))return false;
    s.appliedRenderScale=fallback;s.renderScalePending=false;SaveVrSettings();return true;
}

void SetSharedVrModeEnabled(bool enabled)
{
    SharedVrState& s=GetSharedVrState();if(s.vrModeEnabled==enabled)return;
    s.vrModeEnabled=enabled;
    InputManager* input=InputManager::GetInstance();
    UserController* controller=input?input->GetController(0):NULL;
    if(controller)controller->LoadControllerMappings();
    SaveVrSettings();
}
void SetSharedVehicleControlMode(int mode)
{
    SharedVrState& s=GetSharedVrState();s.vehicleControlMode=std::max(0,std::min(2,mode));
    ResetVrVehicleState();SaveVrSettings();
}
bool IsSharedSpatialHudEnabled()
{
    const SharedVrState& s=GetSharedVrState();return s.vrModeEnabled&&s.spatialHudEnabled;
}
void SetSharedDeveloperMenusEnabled(bool value){GetSharedVrState().developerMenusEnabled=value;SaveVrSettings();}
void SetSharedSeatedMode(bool value){GetSharedVrState().seatedMode=value;SaveVrSettings();}
void SetSharedSnapTurnEnabled(bool value){GetSharedVrState().snapTurnEnabled=value;SaveVrSettings();}
void SetSharedSmoothTurnSpeed(float value){GetSharedVrState().smoothTurnSpeed=value;SaveVrSettings();}
void SetSharedSnapTurnAngle(float value){GetSharedVrState().snapTurnAngle=value;SaveVrSettings();}
void SetSharedCsmEnabled(bool value){GetSharedVrState().csmEnabled=value;SaveVrSettings();}
void SetSharedEnhancedMaterialsEnabled(bool value){GetSharedVrState().enhancedMaterialsEnabled=value;SaveVrSettings();}
void SetSharedCustomMaterialsEnabled(bool value){GetSharedVrState().customMaterialsEnabled=value;SaveVrSettings();}
void SetSharedEnhancedMaterialModel(int value)
{
    SharedVrState& s=GetSharedVrState();s.enhancedMaterialModel=std::max(0,std::min(3,value));
    s.enhancedMaterialsEnabled=s.enhancedMaterialModel!=0;SaveVrSettings();
}
void SetSharedGtaoEnabled(bool value)
{ SharedVrState& s=GetSharedVrState();s.gtaoEnabled=value&&!s.vrModeEnabled;SaveVrSettings(); }
void SetSharedVehicleLightMode(int value)
{ GetSharedVrState().vehicleLightMode=std::max(0,std::min(2,value));SaveVrSettings(); }
void SetSharedReflectionMode(int value)
{ GetSharedVrState().reflectionMode=std::max(0,std::min(2,value));SaveVrSettings(); }
void SetSharedPbrDebugMode(int value)
{ GetSharedVrState().pbrDebugMode=std::max(0,std::min(4,value));SaveVrSettings(); }
void SetSharedGiIndirectOnly(bool value)
{ GetSharedVrState().giIndirectOnly=value;SaveVrSettings(); }
void SetSharedVolumetricLightEnabled(bool value)
{
    SharedVrState& s=GetSharedVrState();
    s.volumetricLightEnabled=value;
    if(value) s.hdrEnabled=true;
    SaveVrSettings();
}
void SetSharedHdrEnabled(bool value)
{
    SharedVrState& s=GetSharedVrState();
    s.hdrEnabled=value;
    if(!value) s.volumetricLightEnabled=false;
    SaveVrSettings();
}
void SetSharedVehicleComfortEnabled(bool value)
{ GetSharedVrState().vehicleComfortEnabled=value;SaveVrSettings(); }
}

#if defined(RAD_ANDROID)

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#if defined(SRR2_VR_RENDERER_VULKAN)
#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#endif
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#include <jni.h>
#include <dlfcn.h>
#include <SDL.h>
#include <SDL_system.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <vr/openxrmanager.h>
#if defined(SRR2_VR_RENDERER_VULKAN)
#include <vr/vulkan/openxr_vulkan_context.h>
#endif
#include <p3d/camera.hpp>
#include <p3d/shader.hpp>
#include <p3d/texture.hpp>
#include <p3d/utility.hpp>
#include <vr/vr_hand_mesh.h>
#include <vr/vr_hand_texture.h>
#include <input/inputmanager.h>
#include <presentation/gui/guiscreen.h>
#include <worldsim/character/character.h>
#include <worldsim/character/charactercontroller.h>
#include <worldsim/character/charactermanager.h>
#include <worldsim/coins/coinmanager.h>
#include <worldsim/traffic/trafficmanager.h>
#include <camera/supercam.h>
#include <camera/supercamcentral.h>
#include <camera/supercammanager.h>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#if defined(RAD_ANDROID)
void pglSetEnhancedMaterialMode(int mode);
int pglGetEnhancedMaterialMode();
#endif
#include <cstdio>

#define XRLOG(...) SDL_Log("OpenXR: " __VA_ARGS__)
#define XRERR(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OpenXR: " __VA_ARGS__)

extern bool pglAreMultiviewProgramsReady();

namespace
{
#if defined(SRR2_VR_RENDERER_VULKAN)
SharOpenXR::VulkanContext& gVulkanContext=SharOpenXR::GetVulkanContext();
#endif
const int RADAR_TEXTURE_WIDTH=1440;
const int RADAR_TEXTURE_HEIGHT=1080;
const int MISSION_HUD_TEXTURE_WIDTH=720;
const int MISSION_HUD_TEXTURE_HEIGHT=540;
static int MissionHudTextureWidth(unsigned slot){return (slot==4||slot==5)?1440:MISSION_HUD_TEXTURE_WIDTH;}
static int MissionHudTextureHeight(unsigned slot){return (slot==4||slot==5)?1080:MISSION_HUD_TEXTURE_HEIGHT;}
static GLuint CompileGlShader(GLenum type,const char* source)
{
    GLuint shader=glCreateShader(type);
    glShaderSource(shader,1,&source,NULL);
    glCompileShader(shader);
    GLint ok=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok)
    {
        char log[1024]={0}; GLsizei length=0;
        glGetShaderInfoLog(shader,sizeof(log),&length,log);
        XRERR("GTAO shader compile failed: %s",log);
        glDeleteShader(shader); return 0;
    }
    return shader;
}

static GLuint CreateGlProgram(const char* vertex,const char* fragment)
{
    GLuint vs=CompileGlShader(GL_VERTEX_SHADER,vertex),fs=CompileGlShader(GL_FRAGMENT_SHADER,fragment);
    if(!vs||!fs) { if(vs)glDeleteShader(vs); if(fs)glDeleteShader(fs); return 0; }
    GLuint program=glCreateProgram(); glAttachShader(program,vs); glAttachShader(program,fs);
    glBindAttribLocation(program,0,"position"); glLinkProgram(program);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok=0; glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok) { XRERR("GTAO program link failed"); glDeleteProgram(program); return 0; }
    return program;
}

struct Eye
{
    XrSwapchain swapchain;
    int32_t width, height;
#if defined(SRR2_VR_RENDERER_VULKAN)
    VkFormat vulkanFormat;
#endif
    std::vector<XrSwapchainImageOpenGLESKHR> images;
#if defined(SRR2_VR_RENDERER_VULKAN)
    std::vector<XrSwapchainImageVulkanKHR> vulkanImages;
    std::vector<XrSwapchainImageFoveationVulkanFB> foveationImages;
    std::vector<unsigned char> vulkanImageInitialized;
#endif
    XrView view;
};

struct State
{
    void* loader;
    PFN_xrGetInstanceProcAddr getProc;
    XrInstance instance;
    XrSystemId system;
    XrSession session;
    XrSpace space;
    XrSessionState sessionState;
    bool running, frameBegun, shouldRender, originValid;
    bool multiviewAvailable,multiviewRendering,multiviewTargetActive,multiviewImageAcquired;
    bool renderModeLogged;
    uint32_t multiviewImageIndex;
    std::vector<unsigned char> multiviewFramebufferValid;
    bool usingStageSpace, systemRecenterPending, preserveHeightOnRecenter;
    XrTime systemRecenterTime;
    float preservedHeadHeight;
    unsigned int recenterSettleFrames;
    XrFrameState frameState;
    XrViewState viewState;
    Eye eyes[2];
    uint32_t activeEye;
    bool worldRendering;
    bool embeddedHudRendering;
    bool radarRendering;
    const void* gameplayHudScreen;
    bool gameplayHudCaptureActive;
    unsigned radarDrawCount;
    GLuint radarFramebuffer,radarTexture,radarDisplayTexture,radarDepthBuffer,radarProgram,hudQuadVbo,irisBlackProgram;
    GLint radarSavedFramebuffer,radarSavedViewport[4];
    GLint radarSavedScissor[4];
    bool radarSavedScissorEnabled;
    bool radarSavedDepthEnabled,radarSavedCullEnabled;
    GLboolean radarSavedColourMask[4];
    float radarUv[4];
    int radarRect[4];
    int radarMapRect[4];
    bool radarCropValid;
    enum { MISSION_HUD_COUNT=19 };
#if defined(SRR2_VR_RENDERER_VULKAN)
    VkImage vulkanRadarImage;
    VkDeviceMemory vulkanRadarMemory;
    VkImageView vulkanRadarView;
    VkSampler vulkanRadarSampler;
    VkDescriptorSet vulkanRadarDescriptor;
    bool vulkanRadarInitialized;
    bool vulkanCaptureActive;
    VkImage vulkanGameplayHudImage;
    VkDeviceMemory vulkanGameplayHudMemory;
    VkImageView vulkanGameplayHudView;
    VkSampler vulkanGameplayHudSampler;
    VkDescriptorSet vulkanGameplayHudDescriptor;
    bool vulkanGameplayHudInitialized;
    VkImage vulkanMissionHudImage[MISSION_HUD_COUNT];
    VkDeviceMemory vulkanMissionHudMemory[MISSION_HUD_COUNT];
    VkImageView vulkanMissionHudView[MISSION_HUD_COUNT];
    VkSampler vulkanMissionHudSampler[MISSION_HUD_COUNT];
    VkDescriptorSet vulkanMissionHudDescriptor[MISSION_HUD_COUNT];
    bool vulkanMissionHudInitialized[MISSION_HUD_COUNT];
#endif
    // Objective, message, timer, coin count, 3D coin, action prompt, and
    // the additional mission counters (par time, collectibles, race place).
    GLuint missionHudFramebuffer[MISSION_HUD_COUNT];
    GLuint missionHudTexture[MISSION_HUD_COUNT];
    float missionHudUv[MISSION_HUD_COUNT][4];
    int missionHudRect[MISSION_HUD_COUNT][4];
    rmt::Matrix missionHudLayout[MISSION_HUD_COUNT];
    bool missionHudLayoutValid[MISSION_HUD_COUNT];
    float missionHudAspect[MISSION_HUD_COUNT];
    bool missionHudVisible[MISSION_HUD_COUNT];
    bool missionHudCropValid[MISSION_HUD_COUNT];
    uint64_t hudFrameSerial;
    uint64_t radarCaptureFrame;
    uint64_t missionHudCaptureFrame[MISSION_HUD_COUNT];
    int missionHudActiveSlot;
    GLuint hudCaptureFramebuffer;
    bool movieRendering;
    bool moviePlaneActive;
    bool moviePlaneAnchorValid;
    XrPosef moviePlaneAnchor;
    bool frontendPlaneActive;
    bool frontendPlaneRendering;
    bool frontendPlaneAnchorValid;
    bool pauseCoinVisible;
    int spatialCoinAuthoredX,spatialCoinAuthoredY;
    bool spatialCoinAuthoredPositionValid;
    int missionObjectiveFrameRect[4];
    bool missionObjectiveFrameRectValid;
    int missionObjectiveIconRect[4];
    bool missionObjectiveIconRectValid;
    bool irisBlackoutTarget;
#if defined(SRR2_VR_RENDERER_VULKAN)
    float irisBlackoutAlpha;
    Uint32 irisBlackoutTicks;
#endif
    XrPosef frontendPlaneAnchor;
    bool enhancedUiConvergence;
    bool vrModeEnabled;
    bool seatedMode, snapTurnEnabled, csmEnabled, enhancedMaterialsEnabled, gtaoEnabled;
    bool vehicleComfortEnabled;
    bool customMaterialsEnabled;
    int enhancedMaterialModel;
    bool spatialHudEnabled;
    bool developerMenusEnabled;
    int vehicleControlMode; // 0 = stick, 1 = VR wheel, 2 = third person
    int vehicleLightMode;
    int reflectionMode;
    int pbrDebugMode;
    bool wheelGrabbed[2];
    bool wheelHonk; // hand resting near the hub of the round wheel = horn
    float gripValue[2],wheelGrabAngle[2],wheelGrabOffset[2],wheelAngle;
    // Visual-only smoothed angle for rim mesh + hand glue. Steering input
    // still uses the responsive wheelAngle; this just kills micro-jitter in
    // the drawn wheel and the hands locked to it.
    float wheelVisualAngle;
    // Per-hand accumulated steering target. Updated from frame-to-frame
    // atan2 deltas (not absolute angle) so crossing the ±π seam at the
    // bottom of the rim cannot flip full-left lock over to full-right.
    float wheelGrabTarget[2];
    // Slow-learned bias correction for the round wheel (normal cars only).
    // If the coded hub/plane doesn't exactly match the physical wheel prop,
    // "straight" reads as a small nonzero wheelAngle forever, since the
    // rim only recentres when BOTH hands fully let go. This trim slowly
    // absorbs a *sustained small* offset while gripped so the car stops
    // pulling to one side, without touching wheelAngle itself (so the rim
    // mesh and hand-glue visuals still show your real physical position).
    float wheelTrim;
    float yokeThrottle;  // Honor Roller: +1 gas, -1 brake
    bool yokeFullGasLatched, yokeFullBrakeLatched; // edge-detect for haptic pulse
    // Dual stick-click hold: reposition + orient VR wheel for this car.
    bool stickClick[2];
    bool wheelAdjustMode;
    float wheelAdjustHoldSec;
    char wheelAdjustVehicle[48];
    rmt::Vector activeWheelCentre;
    rmt::Vector activeYokeAnchor;
    float activeWheelYaw;   // left/right face
    float activeWheelPitch; // up/down tilt — kept after save; steering uses local plane
    float activeWheelRadius; // rim radius — set by pulling hands apart/together in adjust mode
    bool wheelMeshHidden;
    // Orientation snapshot at grab: rim angle + full hand rotation. While
    // held, the hand is spun only around the wheel axis by (currentAngle -
    // grabAngle) so palms do not twist relative to the rim.
    float wheelGrabOrientAngle[2];
    rmt::Matrix wheelGrabOrientRot[2];
    float smoothTurnSpeed, snapTurnAngle, renderScale, appliedRenderScale, refreshRate;
    bool renderScalePending;
    bool menuHorizontalInputDominant;
    bool menuVerticalInputDominant;
    unsigned int menuAxisLock,menuAxisNeutralFrames;
    bool vrBaseHeadingValid;
    rmt::Vector vrBaseHeading;
    bool roomscaleMovementSuspended;
    bool cullingBaseValid;
    rmt::Matrix cullingBaseCamera;
    GLuint framebuffer, layerFramebuffer, depthTexture, multiviewDepthTexture;
    PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC FramebufferTextureMultiviewOVR;
    PFNGLTEXSTORAGE3DPROC TexStorage3D;
    PFNGLFRAMEBUFFERTEXTURELAYERPROC FramebufferTextureLayer;
    PFNGLGETSTRINGIPROC GetStringi;
    PFNGLGENQUERIESEXTPROC GenQueriesEXT;
    PFNGLBEGINQUERYEXTPROC BeginQueryEXT;
    PFNGLENDQUERYEXTPROC EndQueryEXT;
    PFNGLGETQUERYOBJECTUIVEXTPROC GetQueryObjectuivEXT;
    PFNGLGETQUERYOBJECTUI64VEXTPROC GetQueryObjectui64vEXT;
    GLuint perfQueries[4];
    bool perfQueryPending[4],perfQueryActive,perfGpuAvailable,perfGpuChecked;
    unsigned perfQueryIndex,perfFrames;
    Uint64 perfFrameStart,perfRenderStart;
    double perfWaitSum,perfRenderSum,perfSubmitSum,perfGpuLast;
    double perfWaitMax,perfRenderMax,perfSubmitMax;
    unsigned perfDraws,perfIndexedDraws,perfVertices,perfTriangles,perfMaterials;
    unsigned perfUploadCalls,perfUploadBytes;
    double perfDrawCpu,perfMaterialCpu,perfUploadCpu,perfSections[22];
    rmt::Matrix multiviewProjection[2],multiviewViewAdjustment[2];
    GLuint gtaoFramebuffer[2],gtaoTexture[2],gtaoProgram,gtaoBlurProgram,gtaoCompositeProgram,gtaoVbo;
    int gtaoWidth,gtaoHeight;
    XrPosef origin;
    XrActionSet actionSet;
    XrAction moveXAction, moveYAction, lookXAction, lookYAction;
    XrAction selectAction, backAction, attackAction, useAction, menuAction;
    XrAction leftTriggerAction, rightTriggerAction, leftGripAction, rightGripAction;
    XrAction leftStickClickAction, rightStickClickAction;
    XrAction handPoseAction;
    XrAction hapticAction;
    XrSpace handSpaces[2];
    XrPosef handPoses[2];
    bool handPoseValid[2];
    XrPath leftHand, rightHand;
    bool keyState[SDL_NUM_SCANCODES];
    bool mouseState[6];

    PFN_xrDestroyInstance DestroyInstance;
    PFN_xrGetSystem GetSystem;
    PFN_xrGetOpenGLESGraphicsRequirementsKHR GetOpenGLESGraphicsRequirementsKHR;
    PFN_xrCreateSession CreateSession;
    PFN_xrDestroySession DestroySession;
    PFN_xrCreateReferenceSpace CreateReferenceSpace;
    PFN_xrDestroySpace DestroySpace;
    PFN_xrEnumerateViewConfigurationViews EnumerateViewConfigurationViews;
    PFN_xrEnumerateSwapchainFormats EnumerateSwapchainFormats;
    PFN_xrCreateSwapchain CreateSwapchain;
    PFN_xrDestroySwapchain DestroySwapchain;
    PFN_xrEnumerateSwapchainImages EnumerateSwapchainImages;
    PFN_xrPollEvent PollEvent;
    PFN_xrBeginSession BeginSession;
    PFN_xrEndSession EndSession;
    PFN_xrWaitFrame WaitFrame;
    PFN_xrBeginFrame BeginFrame;
    PFN_xrLocateViews LocateViews;
    PFN_xrAcquireSwapchainImage AcquireSwapchainImage;
    PFN_xrWaitSwapchainImage WaitSwapchainImage;
    PFN_xrReleaseSwapchainImage ReleaseSwapchainImage;
    PFN_xrEndFrame EndFrame;
    PFN_xrStringToPath StringToPath;
    PFN_xrCreateActionSet CreateActionSet;
    PFN_xrDestroyActionSet DestroyActionSet;
    PFN_xrCreateAction CreateAction;
    PFN_xrCreateActionSpace CreateActionSpace;
    PFN_xrLocateSpace LocateSpace;
    PFN_xrSuggestInteractionProfileBindings SuggestInteractionProfileBindings;
    PFN_xrAttachSessionActionSets AttachSessionActionSets;
    PFN_xrSyncActions SyncActions;
    PFN_xrGetActionStateBoolean GetActionStateBoolean;
    PFN_xrGetActionStateFloat GetActionStateFloat;
    PFN_xrGetActionStateVector2f GetActionStateVector2f;
    PFN_xrApplyHapticFeedback ApplyHapticFeedback;
    PFN_xrSetColorSpaceFB SetColorSpaceFB;
    PFN_xrRequestDisplayRefreshRateFB RequestDisplayRefreshRateFB;
    PFN_xrCreateFoveationProfileFB CreateFoveationProfileFB;
    PFN_xrDestroyFoveationProfileFB DestroyFoveationProfileFB;
    PFN_xrUpdateSwapchainFB UpdateSwapchainFB;
    XrFoveationProfileFB foveationProfile;
} g = {};

#define LOAD_XR(name) do { \
    if (XR_FAILED(g.getProc(g.instance, "xr" #name, reinterpret_cast<PFN_xrVoidFunction*>(&g.name))) || !g.name) { \
        XRERR("missing xr%s", #name); return false; \
    } } while (0)

static XrQuaternionf Conjugate(const XrQuaternionf& q)
{
    XrQuaternionf r = {-q.x, -q.y, -q.z, q.w}; return r;
}

static bool CreateGtaoResources(int width,int height)
{
    // GTAO is a full-screen pass for both eyes.  Quarter dimensions keep the
    // fragment cost practical on Quest while the bilateral pass reconstructs
    // the low-frequency contact shadow at eye resolution.
    g.gtaoWidth=std::max(1,width/4); g.gtaoHeight=std::max(1,height/4);
    glGenTextures(2,g.gtaoTexture); glGenFramebuffers(2,g.gtaoFramebuffer);
    for(int i=0;i<2;++i)
    {
        glBindTexture(GL_TEXTURE_2D,g.gtaoTexture[i]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,g.gtaoWidth,g.gtaoHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glBindFramebuffer(GL_FRAMEBUFFER,g.gtaoFramebuffer[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,g.gtaoTexture[i],0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) return false;
    }
    static const char* vs="precision highp float; attribute vec2 position; varying vec2 uv; void main(){uv=position*0.5+0.5;gl_Position=vec4(position,0.0,1.0);}";
    static const char* aoFs=
        "precision highp float; varying vec2 uv; uniform sampler2D depthTex; uniform vec2 fullInvSize; uniform vec2 focalPixels; uniform vec4 projXy; uniform vec2 clipPlanes;"
        "float zview(vec2 q){float d=texture2D(depthTex,q).r;return -(clipPlanes.x*clipPlanes.y)/(clipPlanes.y-d*(clipPlanes.y-clipPlanes.x));}"
        "vec3 pos(vec2 q){float z=zview(q);vec2 n=q*2.0-1.0;return vec3((n.x+projXy.z)*(-z)*projXy.x,(n.y+projXy.w)*(-z)*projXy.y,z);}" 
        "void main(){float raw=texture2D(depthTex,uv).r,z=-(clipPlanes.x*clipPlanes.y)/(clipPlanes.y-raw*(clipPlanes.y-clipPlanes.x)),ld=clamp(log2(max(1.0,-z+1.0))/9.967226,0.0,1.0);if(raw>0.99998){gl_FragColor=vec4(1.0,ld,0.0,1.0);return;}vec3 p=pos(uv),px=pos(uv+vec2(fullInvSize.x*4.0,0.0)),py=pos(uv+vec2(0.0,fullInvSize.y*4.0));vec3 N=normalize(cross(px-p,py-p));if(N.z<0.0)N=-N;float radius=0.24,screenScale=radius/max(0.45,-p.z),occ=0.0;float noise=fract(52.9829189*fract(dot(gl_FragCoord.xy,vec2(0.06711056,0.00583715))));for(int i=0;i<6;++i){float an=(float(i)+noise)*1.04719755;float t=0.55+noise*0.35;vec2 q=uv+vec2(cos(an),sin(an))*fullInvSize*focalPixels*screenScale*t;vec3 v=pos(q)-p;float dist=length(v),depthGap=abs(v.z);float horizon=max(dot(N,v/max(dist,0.0001))-0.055,0.0);float rangeWeight=1.0-smoothstep(radius*0.70,radius,dist);float thicknessWeight=1.0-smoothstep(radius*0.08,radius*0.30,depthGap);occ+=horizon*rangeWeight*thicknessWeight;}float ao=clamp(1.0-occ*0.78,0.28,1.0);gl_FragColor=vec4(ao,ld,0.0,1.0);}";
    static const char* blurFs=
        "precision mediump float; varying vec2 uv; uniform sampler2D aoTex; uniform sampler2D depthTex; uniform vec2 aoInvSize;"
        "void main(){vec4 c=texture2D(aoTex,uv);float d=c.g,sum=c.r,w=1.0;vec2 o[4];o[0]=vec2(aoInvSize.x,0.0);o[1]=vec2(-aoInvSize.x,0.0);o[2]=vec2(0.0,aoInvSize.y);o[3]=vec2(0.0,-aoInvSize.y);for(int i=0;i<4;++i){vec4 s=texture2D(aoTex,uv+o[i]);float wt=exp(-abs(s.g-d)*80.0);sum+=s.r*wt;w+=wt;}float a=sum/w;gl_FragColor=vec4(a,d,0.0,1.0);}";
    static const char* compositeFs="precision mediump float; varying vec2 uv; uniform sampler2D aoTex; void main(){float ao=texture2D(aoTex,uv).r;gl_FragColor=vec4(vec3(mix(1.0,ao,0.92)),1.0);}";
    g.gtaoProgram=CreateGlProgram(vs,aoFs); g.gtaoBlurProgram=CreateGlProgram(vs,blurFs); g.gtaoCompositeProgram=CreateGlProgram(vs,compositeFs);
    const float quad[]={-1,-1,1,-1,-1,1,-1,1,1,-1,1,1};
    glGenBuffers(1,&g.gtaoVbo); glBindBuffer(GL_ARRAY_BUFFER,g.gtaoVbo); glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    glBindFramebuffer(GL_FRAMEBUFFER,g.framebuffer);
    return g.gtaoProgram&&g.gtaoBlurProgram&&g.gtaoCompositeProgram;
}

static void SaveVrSettings()
{
    char* path=SDL_GetPrefPath("c4rlox","simpsons");
    if(!path) return;
    std::string filename(path); filename+="vrsettings.cfg";
    if(FILE* file=std::fopen(filename.c_str(),"wb"))
    {
        std::fprintf(file,"seated=%d\nsnap=%d\nsmooth=%.1f\nangle=%.1f\ncsm=%d\nenhancedMaterials=%d\ngtao=%d\nrenderScale=%.3f\nrefreshRate=%.0f\nvrSteeringWheel=%d\nvehicleLights=%d\nspatialHud=%d\ndeveloperMenus=%d\nmaterialModel=%d\nreflectionMode=%d\npbrDebugMode=%d\ncustomMaterials=%d\nvehicleComfort=%d\n",
                     g.seatedMode?1:0,g.snapTurnEnabled?1:0,
                     g.smoothTurnSpeed,g.snapTurnAngle,g.csmEnabled?1:0,
                     g.enhancedMaterialsEnabled?1:0,g.gtaoEnabled?1:0,
                     g.renderScale,g.refreshRate,g.vehicleControlMode,
                     g.vehicleLightMode,g.spatialHudEnabled?1:0,
                     g.developerMenusEnabled?1:0,g.enhancedMaterialModel,g.reflectionMode,
                     g.pbrDebugMode,g.customMaterialsEnabled?1:0,
                     g.vehicleComfortEnabled?1:0);
        std::fclose(file);
    }
    SDL_free(path);
}

static XrQuaternionf Mul(const XrQuaternionf& a, const XrQuaternionf& b)
{
    XrQuaternionf r = {
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z};
    return r;
}

static XrVector3f Rotate(const XrQuaternionf& q, const XrVector3f& v)
{
    XrQuaternionf p = {v.x, v.y, v.z, 0};
    XrQuaternionf r = Mul(Mul(q, p), Conjugate(q));
    XrVector3f out = {r.x, r.y, r.z}; return out;
}

static XrQuaternionf YawOnly(const XrQuaternionf& orientation)
{
    const XrVector3f forward=Rotate(orientation,XrVector3f{0.0f,0.0f,-1.0f});
    const float horizontalForward=forward.x*forward.x+forward.z*forward.z;
    float yaw;
    if(horizontalForward>0.01f)
    {
        yaw=std::atan2(-forward.x,-forward.z);
    }
    else
    {
        // Looking nearly straight up/down makes projected forward degenerate.
        // The projected head-right axis still gives a stable yaw for recenter.
        const XrVector3f right=Rotate(orientation,XrVector3f{1.0f,0.0f,0.0f});
        yaw=std::atan2(-right.z,right.x);
    }
    const float halfYaw=yaw*0.5f;
    XrQuaternionf result={0.0f,std::sin(halfYaw),0.0f,std::cos(halfYaw)};
    return result;
}

static XrPosef RelativePose(const XrPosef& origin, const XrPosef& pose)
{
    XrQuaternionf inv = Conjugate(origin.orientation);
    XrVector3f delta = {pose.position.x-origin.position.x,
                        pose.position.y-origin.position.y,
                        pose.position.z-origin.position.z};
    XrPosef out;
    out.orientation = Mul(inv, pose.orientation);
    out.position = Rotate(inv, delta);
    return out;
}

static rmt::Matrix PoseToGame(const XrPosef& pose)
{
    // One explicit coordinate conversion: OpenXR RH (X right, Y up, -Z
    // forward) to SHAR (X right, Y up, +Z forward), one game unit = 1 metre.
    rmt::Quaternion q(pose.orientation.w, -pose.orientation.x,
                      -pose.orientation.y, pose.orientation.z);
    rmt::Matrix m;
    // FillRotation only writes the 3x3 rotation block. Start from a valid
    // affine matrix so the homogeneous column cannot contain stack garbage.
    m.Identity();
    m.FillRotation(q);
    m.Row(3).Set(pose.position.x, pose.position.y, -pose.position.z);
    return m;
}

static const rmt::Vector kVrWheelCentreDefault(0.0f, -0.32f, 0.52f);
static const rmt::Vector kYokeAnchorDefault(0.0f, -0.33f, 0.40f);
static const rmt::Vector& kVrWheelCentre = kVrWheelCentreDefault;
static const float kVrWheelRadius     = 0.18f;
// Calibration-mode resize range: pulling hands apart/together sets the
// rim radius to half the hand-to-hand distance, clamped to this range.
static const float kVrWheelRadiusMin  = 0.10f;
static const float kVrWheelRadiusMax  = 0.30f;
// ~120 degrees physical rotation → full lock. Less hand travel = easier
// to turn vehicles without feeling sluggish or "tight".
static const float kVrWheelMaxAngle   = 2.09439510f;
static const float kWheelAdjustHoldSec = 3.0f;
static const float kWheelAdjustFollow  = 0.50f;
static const float kGrabGripThreshold = 0.55f;
// Release needs to drop further below the engage point than it took to grab
// — plain hysteresis so a grip sitting right at the threshold can't flicker
// the grab state (and therefore the wheel's target angle) frame to frame.
static const float kGrabReleaseThreshold = 0.42f;
static const float kRadialTolerance   = 0.12f;
static const float kDepthTolerance    = 0.16f;
// Follow the hands closely so the car turns when you turn (not laggy).
static const float kAngleSmooth       = 0.92f; // very direct 1:1 follow
// Lower = faster spring-back when neither hand is gripping.
static const float kCentreReturn      = 0.68f;
static const float kMinRadial         = 0.06f;  // ignore near-centre atan2 noise
// Steering trim (normal round wheel only): only adapt while the wheel is
// already near-centre (not mid-turn), and adapt slowly — a couple of
// seconds of sustained small offset to fully correct — so a deliberate
// gentle turn held for a moment isn't mistaken for a bias to cancel.
static const float kTrimEligibleAngle = 0.18f;  // ~10°: only true near-centre bias
static const float kTrimRate          = 0.008f; // slower learn — less fight on reverse
static const float kTrimMax           = 0.10f;  // ~6° cap
// Round wheel only: bring a hand to the hub (well inside kMinRadial, close
// to the wheel's face) to sound the horn. Independent of grabbing the rim.
static const float kHonkRadius        = 0.10f;
static const float kHonkDepth         = 0.16f;
// Legacy step-cap constant (no longer used by the pure-snapshot path).
static const float kMaxStepPerFrame   = 0.60f;
// Mild shaping only — stay easy to steer, not "tight".
static const float kSteerOutputDeadzone = 0.02f;
static const float kSteerOutputCurve    = 1.15f;
// Fixed 9-and-3 grab slots (radians from top of rim). hand 0 = left
// controller → left side; hand 1 = right controller → right side.
// ±90° is symmetric about the hub for a clean swivel.
static const float kOptimalGrabAngle  = 1.5707963f; // 90 degrees
// Honor Roller: small hand movement is enough for full input.
static const float kYokePitchRange    = 0.035f; // forward/back for gas/brake
static const float kYokeSteerRange    = 0.11f;  // how far one hand drops for full lock
static const float kYokeArmLength     = 0.11f;  // shaft length, anchor up to the bar
static const float kYokeFullThrottle  = 0.95f;  // |yokeThrottle| at/above this = "full" for haptics

// Defined further below, alongside the other input-handling helpers —
// forward-declared here since UpdateVrSteeringWheel (right below) calls it.
static void FireHaptic(unsigned hand, float amplitude, float durationMs, float frequencyHz = 0.0f);

static float UnwrapDelta(float a)
{
    while (a >  3.14159265f) a -= 6.28318531f;
    while (a < -3.14159265f) a += 6.28318531f;
    return a;
}

// Rotate the 3x3 part of `in` around the wheel axis (local +Z) by delta.
// Position row is left cleared; caller writes rim position separately.
static void RotateOrientAroundWheelAxis(float delta, const rmt::Matrix& in, rmt::Matrix& out)
{
    const float s = std::sin(delta);
    const float c = std::cos(delta);
    out.Identity();
    for (int i = 0; i < 3; ++i)
    {
        const rmt::Vector& r = in.Row(i);
        // (x,y,z) -> (c*x - s*y, s*x + c*y, z) matches rim motion
        // (sin a, cos a) advancing with +a.
        out.Row(i).Set(c * r.x - s * r.y, s * r.x + c * r.y, r.z);
    }
}


// Forward declaration — definition appears later with the other wheel-space helpers.
static rmt::Vector WheelLocalToCar(float lx, float ly, float lz);

// ---------------------------------------------------------------------------
// Baked VR steering wheel mesh (Canyonero / SteeringWheel.obj).
// Authored in wheel-local space, centred on the hub and scaled to a unit
// rim so draw multiplies by activeWheelRadius:
//   origin = hub centre
//   +Y     = top of rim (12 o'clock)
//   +X     = right (3 o'clock)
//   +Z     = toward the driver / face of the wheel
// ---------------------------------------------------------------------------
static const int kVrWheelObjTriCount = 108;

static const float kVrWheelObjPositions[] = {
    0.965981f,0.258612f,0.005904f, 0.888476f,-0.240144f,0.098584f, 0.965895f,-0.258913f,-0.005269f,
    0.707002f,-0.707062f,-0.014840f, 0.887505f,-0.235649f,-0.109615f, 0.965895f,-0.258913f,-0.005269f,
    0.888476f,-0.240144f,0.098584f, 0.649695f,-0.651945f,0.089790f, 0.965895f,-0.258913f,-0.005269f,
    0.886552f,0.235404f,0.108855f, 0.888476f,-0.240144f,0.098584f, 0.965981f,0.258612f,0.005904f,
    0.965895f,-0.258913f,-0.005269f, 0.887587f,0.239900f,-0.099344f, 0.965981f,0.258612f,0.005904f,
    0.649909f,0.647273f,0.117847f, 0.886552f,0.235404f,0.108855f, 0.965981f,0.258612f,0.005904f,
    0.887587f,0.239900f,-0.099344f, 0.707233f,0.706843f,0.015699f, 0.965981f,0.258612f,0.005904f,
    0.801180f,0.001995f,-0.097691f, 0.887587f,0.239900f,-0.099344f, 0.887505f,-0.235649f,-0.109615f,
    0.649609f,-0.647449f,-0.118405f, 0.801197f,-0.159238f,-0.002967f, 0.887505f,-0.235649f,-0.109615f,
    0.965895f,-0.258913f,-0.005269f, 0.887505f,-0.235649f,-0.109615f, 0.887587f,0.239900f,-0.099344f,
    0.649824f,0.651769f,-0.090344f, 0.707233f,0.706843f,0.015699f, 0.887587f,0.239900f,-0.099344f,
    0.801249f,0.162437f,0.003980f, 0.649824f,0.651769f,-0.090344f, 0.887587f,0.239900f,-0.099344f,
    0.586437f,-0.586488f,-0.012208f, 0.888476f,-0.240144f,0.098584f, 0.801197f,-0.159238f,-0.002967f,
    0.801180f,0.001995f,-0.097691f, 0.887505f,-0.235649f,-0.109615f, 0.801197f,-0.159238f,-0.002967f,
    0.362122f,-0.375927f,-0.007437f, -0.362242f,-0.375815f,-0.007119f, 0.801180f,0.001995f,-0.097691f,
    0.801249f,0.162437f,0.003980f, 0.887587f,0.239900f,-0.099344f, 0.801180f,0.001995f,-0.097691f,
    -0.801197f,0.162686f,0.004676f, 0.886552f,0.235404f,0.108855f, 0.801249f,0.162437f,0.003980f,
    0.801180f,0.001995f,-0.097691f, 0.801197f,-0.159238f,-0.002967f, 0.801249f,0.162437f,0.003980f,
    0.586634f,0.586308f,0.013118f, 0.649824f,0.651769f,-0.090344f, 0.801249f,0.162437f,0.003980f,
    0.965895f,-0.258913f,-0.005269f, 0.649695f,-0.651945f,0.089790f, 0.707002f,-0.707062f,-0.014840f,
    0.258664f,-0.965758f,-0.020233f, 0.649609f,-0.647449f,-0.118405f, 0.707002f,-0.707062f,-0.014840f,
    0.649695f,-0.651945f,0.089790f, 0.258664f,-0.965758f,-0.020233f, 0.707002f,-0.707062f,-0.014840f,
    0.649909f,0.647273f,0.117847f, 0.965981f,0.258612f,0.005904f, 0.707233f,0.706843f,0.015699f,
    0.649824f,0.651769f,-0.090344f, 0.258982f,0.965676f,0.021483f, 0.707233f,0.706843f,0.015699f,
    0.888476f,-0.240144f,0.098584f, 0.586437f,-0.586488f,-0.012208f, 0.649695f,-0.651945f,0.089790f,
    0.586437f,-0.586488f,-0.012208f, 0.237727f,-0.889652f,0.084831f, 0.649695f,-0.651945f,0.089790f,
    0.887505f,-0.235649f,-0.109615f, 0.707002f,-0.707062f,-0.014840f, 0.649609f,-0.647449f,-0.118405f,
    0.237637f,-0.885157f,-0.123365f, 0.586437f,-0.586488f,-0.012208f, 0.649609f,-0.647449f,-0.118405f,
    0.586634f,0.586308f,0.013118f, 0.886552f,0.235404f,0.108855f, 0.649909f,0.647273f,0.117847f,
    0.238023f,0.885114f,0.123158f, 0.586634f,0.586308f,0.013118f, 0.649909f,0.647273f,0.117847f,
    0.237933f,0.889609f,-0.085033f, 0.258982f,0.965676f,0.021483f, 0.649824f,0.651769f,-0.090344f,
    0.586634f,0.586308f,0.013118f, 0.237933f,0.889609f,-0.085033f, 0.649824f,0.651769f,-0.090344f,
    0.801197f,-0.159238f,-0.002967f, 0.649609f,-0.647449f,-0.118405f, 0.586437f,-0.586488f,-0.012208f,
    0.214819f,0.801004f,0.017914f, 0.237933f,0.889609f,-0.085033f, 0.586634f,0.586308f,0.013118f,
    0.801197f,-0.159238f,-0.002967f, 0.888476f,-0.240144f,0.098584f, 0.362122f,-0.375927f,-0.007437f,
    0.801180f,0.001995f,-0.097691f, 0.801197f,-0.159238f,-0.002967f, 0.362122f,-0.375927f,-0.007437f,
    0.349313f,-0.423847f,0.094844f, -0.349365f,-0.423740f,0.095145f, 0.362122f,-0.375927f,-0.007437f,
    0.362122f,-0.375927f,-0.007437f, 0.888476f,-0.240144f,0.098584f, 0.349313f,-0.423847f,0.094844f,
    0.649695f,-0.651945f,0.089790f, 0.237727f,-0.889652f,0.084831f, 0.258664f,-0.965758f,-0.020233f,
    -0.258982f,-0.965676f,-0.020005f, 0.237637f,-0.885157f,-0.123365f, 0.258664f,-0.965758f,-0.020233f,
    0.237727f,-0.889652f,0.084831f, -0.237933f,-0.889579f,0.085037f, 0.258664f,-0.965758f,-0.020233f,
    -0.237637f,0.885187f,0.123365f, 0.238023f,0.885114f,0.123158f, 0.258982f,0.965676f,0.021483f,
    0.237933f,0.889609f,-0.085033f, -0.258664f,0.965758f,0.021706f, 0.258982f,0.965676f,0.021483f,
    0.586437f,-0.586488f,-0.012208f, 0.214553f,-0.801064f,-0.016682f, 0.237727f,-0.889652f,0.084831f,
    0.214553f,-0.801064f,-0.016682f, -0.214819f,-0.801000f,-0.016497f, 0.237727f,-0.889652f,0.084831f,
    0.649609f,-0.647449f,-0.118405f, 0.258664f,-0.965758f,-0.020233f, 0.237637f,-0.885157f,-0.123365f,
    -0.238023f,-0.885079f,-0.123158f, 0.214553f,-0.801064f,-0.016682f, 0.237637f,-0.885157f,-0.123365f,
    0.214819f,0.801004f,0.017914f, 0.586634f,0.586308f,0.013118f, 0.238023f,0.885114f,0.123158f,
    -0.214553f,0.801069f,0.018103f, 0.214819f,0.801004f,0.017914f, 0.238023f,0.885114f,0.123158f,
    0.214819f,0.801004f,0.017914f, -0.237727f,0.889687f,-0.084827f, 0.237933f,0.889609f,-0.085033f,
    -0.237727f,0.889687f,-0.084827f, -0.258664f,0.965758f,0.021706f, 0.237933f,0.889609f,-0.085033f,
    0.586437f,-0.586488f,-0.012208f, 0.237637f,-0.885157f,-0.123365f, 0.214553f,-0.801064f,-0.016682f,
    -0.214553f,0.801069f,0.018103f, -0.237727f,0.889687f,-0.084827f, 0.214819f,0.801004f,0.017914f,
    0.214553f,-0.801064f,-0.016682f, -0.238023f,-0.885079f,-0.123158f, -0.214819f,-0.801000f,-0.016497f,
    -0.586394f,0.584260f,0.117018f, -0.649695f,0.651975f,-0.089782f, -0.214553f,0.801069f,0.018103f,
    0.237727f,-0.889652f,0.084831f, -0.214819f,-0.801000f,-0.016497f, -0.237933f,-0.889579f,0.085037f,
    -0.214819f,-0.801000f,-0.016497f, -0.586630f,-0.586304f,-0.011701f, -0.237933f,-0.889579f,0.085037f,
    0.237637f,-0.885157f,-0.123365f, -0.258982f,-0.965676f,-0.020005f, -0.238023f,-0.885079f,-0.123158f,
    -0.214553f,0.801069f,0.018103f, 0.238023f,0.885114f,0.123158f, -0.237637f,0.885187f,0.123365f,
    -0.586394f,0.584260f,0.117018f, -0.214553f,0.801069f,0.018103f, -0.237637f,0.885187f,0.123365f,
    -0.649695f,0.651975f,-0.089782f, -0.258664f,0.965758f,0.021706f, -0.237727f,0.889687f,-0.084827f,
    -0.214553f,0.801069f,0.018103f, -0.649695f,0.651975f,-0.089782f, -0.237727f,0.889687f,-0.084827f,
    -0.237637f,0.885187f,0.123365f, 0.258982f,0.965676f,0.021483f, -0.258664f,0.965758f,0.021706f,
    -0.649609f,0.647479f,0.118414f, -0.237637f,0.885187f,0.123365f, -0.258664f,0.965758f,0.021706f,
    0.258664f,-0.965758f,-0.020233f, -0.237933f,-0.889579f,0.085037f, -0.258982f,-0.965676f,-0.020005f,
    -0.237933f,-0.889579f,0.085037f, -0.649824f,-0.651743f,0.090353f, -0.258982f,-0.965676f,-0.020005f,
    -0.888467f,-0.239865f,0.099349f, -0.362242f,-0.375815f,-0.007119f, -0.349365f,-0.423740f,0.095145f,
    0.362122f,-0.375927f,-0.007437f, -0.349365f,-0.423740f,0.095145f, -0.362242f,-0.375815f,-0.007119f,
    -0.214819f,-0.801000f,-0.016497f, -0.238023f,-0.885079f,-0.123158f, -0.586630f,-0.586304f,-0.011701f,
    -0.801197f,0.162686f,0.004676f, -0.649695f,0.651975f,-0.089782f, -0.586394f,0.584260f,0.117018f,
    -0.237933f,-0.889579f,0.085037f, -0.586630f,-0.586304f,-0.011701f, -0.649824f,-0.651743f,0.090353f,
    -0.586630f,-0.586304f,-0.011701f, -0.238023f,-0.885079f,-0.123158f, -0.649909f,-0.647243f,-0.117838f,
    -0.238023f,-0.885079f,-0.123158f, -0.258982f,-0.965676f,-0.020005f, -0.649909f,-0.647243f,-0.117838f,
    -0.586394f,0.584260f,0.117018f, -0.237637f,0.885187f,0.123365f, -0.649609f,0.647479f,0.118414f,
    -0.886380f,0.235679f,0.109624f, -0.586394f,0.584260f,0.117018f, -0.649609f,0.647479f,0.118414f,
    -0.887596f,0.240179f,-0.098576f, -0.707002f,0.707062f,0.016313f, -0.649695f,0.651975f,-0.089782f,
    -0.649909f,-0.647243f,-0.117838f, -0.258982f,-0.965676f,-0.020005f, -0.707233f,-0.706838f,-0.014290f,
    -0.258982f,-0.965676f,-0.020005f, -0.649824f,-0.651743f,0.090353f, -0.707233f,-0.706838f,-0.014290f,
    -0.649824f,-0.651743f,0.090353f, -0.965981f,-0.258612f,-0.004431f, -0.707233f,-0.706838f,-0.014290f,
    -0.649609f,0.647479f,0.118414f, -0.258664f,0.965758f,0.021706f, -0.707002f,0.707062f,0.016313f,
    -0.965895f,0.258913f,0.006746f, -0.649609f,0.647479f,0.118414f, -0.707002f,0.707062f,0.016313f,
    -0.258664f,0.965758f,0.021706f, -0.649695f,0.651975f,-0.089782f, -0.707002f,0.707062f,0.016313f,
    -0.888467f,-0.239865f,0.099349f, -0.586630f,-0.586304f,-0.011701f, -0.801249f,-0.158989f,-0.002271f,
    -0.586630f,-0.586304f,-0.011701f, -0.649909f,-0.647243f,-0.117838f, -0.801249f,-0.158989f,-0.002271f,
    -0.801266f,0.002248f,-0.096996f, -0.801197f,0.162686f,0.004676f, -0.801249f,-0.158989f,-0.002271f,
    -0.362242f,-0.375815f,-0.007119f, -0.888467f,-0.239865f,0.099349f, -0.801249f,-0.158989f,-0.002271f,
    -0.801249f,-0.158989f,-0.002271f, -0.887677f,-0.235369f,-0.108847f, -0.801266f,0.002248f,-0.096996f,
    -0.887677f,-0.235369f,-0.108847f, -0.887596f,0.240179f,-0.098576f, -0.801266f,0.002248f,-0.096996f,
    -0.801197f,0.162686f,0.004676f, 0.801180f,0.001995f,-0.097691f, -0.801266f,0.002248f,-0.096996f,
    0.801180f,0.001995f,-0.097691f, -0.362242f,-0.375815f,-0.007119f, -0.801266f,0.002248f,-0.096996f,
    -0.362242f,-0.375815f,-0.007119f, -0.801249f,-0.158989f,-0.002271f, -0.801266f,0.002248f,-0.096996f,
    -0.887596f,0.240179f,-0.098576f, -0.649695f,0.651975f,-0.089782f, -0.801197f,0.162686f,0.004676f,
    -0.801266f,0.002248f,-0.096996f, -0.887596f,0.240179f,-0.098576f, -0.801197f,0.162686f,0.004676f,
    0.801249f,0.162437f,0.003980f, 0.801180f,0.001995f,-0.097691f, -0.801197f,0.162686f,0.004676f,
    0.886552f,0.235404f,0.108855f, -0.801197f,0.162686f,0.004676f, -0.886380f,0.235679f,0.109624f,
    -0.888467f,-0.239865f,0.099349f, -0.801197f,0.162686f,0.004676f, -0.886380f,0.235679f,0.109624f,
    -0.801249f,-0.158989f,-0.002271f, -0.649909f,-0.647243f,-0.117838f, -0.887677f,-0.235369f,-0.108847f,
    -0.649909f,-0.647243f,-0.117838f, -0.707233f,-0.706838f,-0.014290f, -0.887677f,-0.235369f,-0.108847f,
    -0.887677f,-0.235369f,-0.108847f, -0.965895f,0.258913f,0.006746f, -0.887596f,0.240179f,-0.098576f,
    -0.649824f,-0.651743f,0.090353f, -0.586630f,-0.586304f,-0.011701f, -0.888467f,-0.239865f,0.099349f,
    -0.801249f,-0.158989f,-0.002271f, -0.801197f,0.162686f,0.004676f, -0.888467f,-0.239865f,0.099349f,
    -0.887677f,-0.235369f,-0.108847f, -0.707233f,-0.706838f,-0.014290f, -0.965981f,-0.258612f,-0.004431f,
    -0.888467f,-0.239865f,0.099349f, -0.886380f,0.235679f,0.109624f, -0.965981f,-0.258612f,-0.004431f,
    -0.965895f,0.258913f,0.006746f, -0.887677f,-0.235369f,-0.108847f, -0.965981f,-0.258612f,-0.004431f,
    -0.649824f,-0.651743f,0.090353f, -0.888467f,-0.239865f,0.099349f, -0.965981f,-0.258612f,-0.004431f,
    -0.886380f,0.235679f,0.109624f, -0.649609f,0.647479f,0.118414f, -0.965895f,0.258913f,0.006746f,
    -0.965981f,-0.258612f,-0.004431f, -0.886380f,0.235679f,0.109624f, -0.965895f,0.258913f,0.006746f,
    -0.707002f,0.707062f,0.016313f, -0.887596f,0.240179f,-0.098576f, -0.965895f,0.258913f,0.006746f
};

static const float kVrWheelObjNormals[] = {
    0.934092f,0.244798f,0.259898f, 0.067199f,0.051899f,0.996389f, 0.965870f,-0.258992f,-0.004300f,
    0.735242f,-0.657737f,-0.163709f, 0.166898f,-0.161299f,-0.972691f, 0.965870f,-0.258992f,-0.004300f,
    0.067199f,0.051899f,0.996389f, 0.280601f,-0.299801f,0.911802f, 0.965870f,-0.258992f,-0.004300f,
    -0.324399f,-0.092400f,0.941397f, 0.067199f,0.051899f,0.996389f, 0.934092f,0.244798f,0.259898f,
    0.965870f,-0.258992f,-0.004300f, 0.303315f,0.172508f,-0.937145f, 0.934092f,0.244798f,0.259898f,
    0.279212f,0.259711f,0.924441f, -0.324399f,-0.092400f,0.941397f, 0.934092f,0.244798f,0.259898f,
    0.303315f,0.172508f,-0.937145f, 0.735498f,0.663798f,-0.135700f, 0.934092f,0.244798f,0.259898f,
    -0.666997f,0.015600f,-0.744897f, 0.303315f,0.172508f,-0.937145f, 0.166898f,-0.161299f,-0.972691f,
    -0.044001f,-0.091602f,-0.994823f, -0.902569f,0.239792f,-0.357588f, 0.166898f,-0.161299f,-0.972691f,
    0.965870f,-0.258992f,-0.004300f, 0.166898f,-0.161299f,-0.972691f, 0.303315f,0.172508f,-0.937145f,
    -0.045002f,0.133405f,-0.990039f, 0.735498f,0.663798f,-0.135700f, 0.303315f,0.172508f,-0.937145f,
    -0.900825f,-0.227006f,-0.370110f, -0.045002f,0.133405f,-0.990039f, 0.303315f,0.172508f,-0.937145f,
    -0.679628f,0.715330f,0.162507f, 0.067199f,0.051899f,0.996389f, -0.902569f,0.239792f,-0.357588f,
    -0.666997f,0.015600f,-0.744897f, 0.166898f,-0.161299f,-0.972691f, -0.902569f,0.239792f,-0.357588f,
    0.153498f,-0.720090f,-0.676690f, -0.155599f,-0.604197f,-0.781496f, 0.055601f,0.026000f,-0.998114f,
    -0.900825f,-0.227006f,-0.370110f, 0.303315f,0.172508f,-0.937145f, -0.666997f,0.015600f,-0.744897f,
    -0.000200f,0.691795f,-0.722094f, -0.000100f,0.820887f,-0.571091f, -0.000200f,0.691795f,-0.722094f,
    -1.000000f,0.000200f,0.000400f, -0.892548f,-0.010701f,0.450824f, -0.940613f,-0.007200f,0.339405f,
    -0.680287f,-0.721487f,0.129098f, -0.045002f,0.133405f,-0.990039f, -0.900825f,-0.227006f,-0.370110f,
    0.965870f,-0.258992f,-0.004300f, 0.280601f,-0.299801f,0.911802f, 0.735242f,-0.657737f,-0.163709f,
    0.293590f,-0.951368f,0.093297f, -0.044001f,-0.091602f,-0.994823f, 0.735242f,-0.657737f,-0.163709f,
    0.280601f,-0.299801f,0.911802f, 0.293590f,-0.951368f,0.093297f, 0.735242f,-0.657737f,-0.163709f,
    0.279212f,0.259711f,0.924441f, 0.934092f,0.244798f,0.259898f, 0.735498f,0.663798f,-0.135700f,
    -0.045002f,0.133405f,-0.990039f, 0.293816f,0.946450f,0.133807f, 0.735498f,0.663798f,-0.135700f,
    0.067199f,0.051899f,0.996389f, -0.679628f,0.715330f,0.162507f, 0.280601f,-0.299801f,0.911802f,
    -0.679628f,0.715330f,0.162507f, -0.091002f,0.321209f,0.942626f, 0.280601f,-0.299801f,0.911802f,
    0.166898f,-0.161299f,-0.972691f, 0.735242f,-0.657737f,-0.163709f, -0.044001f,-0.091602f,-0.994823f,
    -0.099104f,-0.029501f,-0.994640f, -0.679628f,0.715330f,0.162507f, -0.044001f,-0.091602f,-0.994823f,
    -0.680287f,-0.721487f,0.129098f, -0.324399f,-0.092400f,0.941397f, 0.279212f,0.259711f,0.924441f,
    -0.091496f,-0.361283f,0.927956f, -0.680287f,-0.721487f,0.129098f, 0.279212f,0.259711f,0.924441f,
    -0.099004f,0.072503f,-0.992442f, 0.293816f,0.946450f,0.133807f, -0.045002f,0.133405f,-0.990039f,
    -0.680287f,-0.721487f,0.129098f, -0.099004f,0.072503f,-0.992442f, -0.045002f,0.133405f,-0.990039f,
    -0.902569f,0.239792f,-0.357588f, -0.044001f,-0.091602f,-0.994823f, -0.679628f,0.715330f,0.162507f,
    -0.204302f,-0.961210f,-0.185302f, -0.099004f,0.072503f,-0.992442f, -0.680287f,-0.721487f,0.129098f,
    0.278012f,-0.547124f,-0.789534f, 0.311008f,-0.758619f,-0.572514f, 0.153498f,-0.720090f,-0.676690f,
    0.055601f,0.026000f,-0.998114f, 0.278012f,-0.547124f,-0.789534f, 0.153498f,-0.720090f,-0.676690f,
    0.152407f,-0.902144f,-0.403620f, -0.102004f,-0.905632f,-0.411614f, 0.153498f,-0.720090f,-0.676690f,
    0.153498f,-0.720090f,-0.676690f, 0.311008f,-0.758619f,-0.572514f, 0.152407f,-0.902144f,-0.403620f,
    0.280601f,-0.299801f,0.911802f, -0.091002f,0.321209f,0.942626f, 0.293590f,-0.951368f,0.093297f,
    -0.293990f,-0.947267f,-0.127496f, -0.099104f,-0.029501f,-0.994640f, 0.293590f,-0.951368f,0.093297f,
    -0.091002f,0.321209f,0.942626f, 0.097602f,-0.072001f,0.992618f, 0.293590f,-0.951368f,0.093297f,
    0.028601f,0.199304f,0.979520f, -0.091496f,-0.361283f,0.927956f, 0.293816f,0.946450f,0.133807f,
    -0.099004f,0.072503f,-0.992442f, -0.293698f,0.951995f,-0.086300f, 0.293816f,0.946450f,0.133807f,
    -0.679628f,0.715330f,0.162507f, -0.204089f,0.968349f,-0.143692f, -0.091002f,0.321209f,0.942626f,
    -0.204089f,0.968349f,-0.143692f, 0.204391f,0.959857f,0.192091f, -0.091002f,0.321209f,0.942626f,
    -0.044001f,-0.091602f,-0.994823f, 0.293590f,-0.951368f,0.093297f, -0.099104f,-0.029501f,-0.994640f,
    0.092297f,0.365888f,-0.926071f, -0.204089f,0.968349f,-0.143692f, -0.099104f,-0.029501f,-0.994640f,
    -0.204302f,-0.961210f,-0.185302f, -0.680287f,-0.721487f,0.129098f, -0.091496f,-0.361283f,0.927956f,
    0.278908f,-0.959726f,0.033701f, -0.204302f,-0.961210f,-0.185302f, -0.091496f,-0.361283f,0.927956f,
    -0.204302f,-0.961210f,-0.185302f, -0.005200f,-0.202705f,-0.979226f, -0.099004f,0.072503f,-0.992442f,
    -0.005200f,-0.202705f,-0.979226f, -0.293698f,0.951995f,-0.086300f, -0.099004f,0.072503f,-0.992442f,
    -0.679628f,0.715330f,0.162507f, -0.099104f,-0.029501f,-0.994640f, -0.204089f,0.968349f,-0.143692f,
    0.278908f,-0.959726f,0.033701f, -0.005200f,-0.202705f,-0.979226f, -0.204302f,-0.961210f,-0.185302f,
    -0.204089f,0.968349f,-0.143692f, 0.092297f,0.365888f,-0.926071f, 0.204391f,0.959857f,0.192091f,
    0.551889f,-0.554689f,0.622687f, 0.181092f,-0.081797f,-0.980059f, 0.278908f,-0.959726f,0.033701f,
    -0.091002f,0.321209f,0.942626f, 0.204391f,0.959857f,0.192091f, 0.097602f,-0.072001f,0.992618f,
    0.204391f,0.959857f,0.192091f, 0.679958f,0.715156f,0.161890f, 0.097602f,-0.072001f,0.992618f,
    -0.099104f,-0.029501f,-0.994640f, -0.293990f,-0.947267f,-0.127496f, 0.092297f,0.365888f,-0.926071f,
    0.278908f,-0.959726f,0.033701f, -0.091496f,-0.361283f,0.927956f, 0.028601f,0.199304f,0.979520f,
    0.551889f,-0.554689f,0.622687f, 0.278908f,-0.959726f,0.033701f, 0.028601f,0.199304f,0.979520f,
    0.181092f,-0.081797f,-0.980059f, -0.293698f,0.951995f,-0.086300f, -0.005200f,-0.202705f,-0.979226f,
    0.278908f,-0.959726f,0.033701f, 0.181092f,-0.081797f,-0.980059f, -0.005200f,-0.202705f,-0.979226f,
    0.028601f,0.199304f,0.979520f, 0.293816f,0.946450f,0.133807f, -0.293698f,0.951995f,-0.086300f,
    -0.403407f,0.386007f,0.829615f, 0.028601f,0.199304f,0.979520f, -0.293698f,0.951995f,-0.086300f,
    0.293590f,-0.951368f,0.093297f, 0.097602f,-0.072001f,0.992618f, -0.293990f,-0.947267f,-0.127496f,
    0.097602f,-0.072001f,0.992618f, -0.280106f,-0.299906f,0.911919f, -0.293990f,-0.947267f,-0.127496f,
    -0.311813f,-0.758431f,-0.572324f, -0.155599f,-0.604197f,-0.781496f, -0.102004f,-0.905632f,-0.411614f,
    0.153498f,-0.720090f,-0.676690f, -0.102004f,-0.905632f,-0.411614f, -0.155599f,-0.604197f,-0.781496f,
    0.204391f,0.959857f,0.192091f, 0.092297f,0.365888f,-0.926071f, 0.679958f,0.715156f,0.161890f,
    0.920613f,-0.270604f,-0.281504f, 0.181092f,-0.081797f,-0.980059f, 0.551889f,-0.554689f,0.622687f,
    0.097602f,-0.072001f,0.992618f, 0.679958f,0.715156f,0.161890f, -0.280106f,-0.299906f,0.911919f,
    0.679958f,0.715156f,0.161890f, 0.092297f,0.365888f,-0.926071f, 0.043298f,-0.091497f,-0.994864f,
    0.092297f,0.365888f,-0.926071f, -0.293990f,-0.947267f,-0.127496f, 0.043298f,-0.091497f,-0.994864f,
    0.551889f,-0.554689f,0.622687f, 0.028601f,0.199304f,0.979520f, -0.403407f,0.386007f,0.829615f,
    -0.231699f,-0.030900f,0.972297f, 0.551889f,-0.554689f,0.622687f, -0.403407f,0.386007f,0.829615f,
    -0.168496f,0.202495f,-0.964678f, -0.735340f,0.664236f,-0.134407f, 0.181092f,-0.081797f,-0.980059f,
    0.043298f,-0.091497f,-0.994864f, -0.293990f,-0.947267f,-0.127496f, -0.735537f,-0.657533f,-0.163208f,
    -0.293990f,-0.947267f,-0.127496f, -0.280106f,-0.299906f,0.911919f, -0.735537f,-0.657533f,-0.163208f,
    -0.280106f,-0.299906f,0.911919f, -0.934594f,-0.255398f,0.247599f, -0.735537f,-0.657533f,-0.163208f,
    -0.403407f,0.386007f,0.829615f, -0.293698f,0.951995f,-0.086300f, -0.735340f,0.664236f,-0.134407f,
    -0.965993f,0.258298f,0.011800f, -0.403407f,0.386007f,0.829615f, -0.735340f,0.664236f,-0.134407f,
    -0.293698f,0.951995f,-0.086300f, 0.181092f,-0.081797f,-0.980059f, -0.735340f,0.664236f,-0.134407f,
    0.320307f,0.049501f,0.946020f, 0.679958f,0.715156f,0.161890f, 0.902341f,0.239411f,-0.358416f,
    0.679958f,0.715156f,0.161890f, 0.043298f,-0.091497f,-0.994864f, 0.902341f,0.239411f,-0.358416f,
    1.000000f,-0.000100f,-0.000400f, 0.892404f,-0.011000f,0.451102f, 0.938902f,-0.007600f,0.344101f,
    -0.155599f,-0.604197f,-0.781496f, -0.311813f,-0.758431f,-0.572324f, -0.278895f,-0.546890f,-0.789385f,
    0.902341f,0.239411f,-0.358416f, -0.304387f,-0.132694f,-0.943261f, 0.666418f,0.015400f,-0.745420f,
    -0.304387f,-0.132694f,-0.943261f, -0.168496f,0.202495f,-0.964678f, 0.666418f,0.015400f,-0.745420f,
    -0.000200f,0.691795f,-0.722094f, 0.055601f,0.026000f,-0.998114f, -0.094297f,-0.068698f,-0.993171f,
    0.055601f,0.026000f,-0.998114f, -0.155599f,-0.604197f,-0.781496f, -0.094297f,-0.068698f,-0.993171f,
    -0.155599f,-0.604197f,-0.781496f, -0.278895f,-0.546890f,-0.789385f, -0.094297f,-0.068698f,-0.993171f,
    -0.168496f,0.202495f,-0.964678f, 0.181092f,-0.081797f,-0.980059f, 0.920613f,-0.270604f,-0.281504f,
    0.666418f,0.015400f,-0.745420f, -0.168496f,0.202495f,-0.964678f, 0.920613f,-0.270604f,-0.281504f,
    -0.000200f,0.691795f,-0.722094f, 0.055601f,0.026000f,-0.998114f, -0.000200f,0.691795f,-0.722094f,
    -0.000100f,0.820887f,-0.571091f, -0.000200f,0.691795f,-0.722094f, -0.000100f,0.820887f,-0.571091f,
    0.320307f,0.049501f,0.946020f, 0.892404f,-0.011000f,0.451102f, -0.231699f,-0.030900f,0.972297f,
    0.902341f,0.239411f,-0.358416f, 0.043298f,-0.091497f,-0.994864f, -0.304387f,-0.132694f,-0.943261f,
    0.043298f,-0.091497f,-0.994864f, -0.735537f,-0.657533f,-0.163208f, -0.304387f,-0.132694f,-0.943261f,
    -0.304387f,-0.132694f,-0.943261f, -0.965993f,0.258298f,0.011800f, -0.168496f,0.202495f,-0.964678f,
    -0.280106f,-0.299906f,0.911919f, 0.679958f,0.715156f,0.161890f, 0.320307f,0.049501f,0.946020f,
    0.938902f,-0.007600f,0.344101f, 0.892404f,-0.011000f,0.451102f, 0.320307f,0.049501f,0.946020f,
    -0.304387f,-0.132694f,-0.943261f, -0.735537f,-0.657533f,-0.163208f, -0.934594f,-0.255398f,0.247599f,
    0.320307f,0.049501f,0.946020f, -0.231699f,-0.030900f,0.972297f, -0.934594f,-0.255398f,0.247599f,
    -0.965993f,0.258298f,0.011800f, -0.304387f,-0.132694f,-0.943261f, -0.934594f,-0.255398f,0.247599f,
    -0.280106f,-0.299906f,0.911919f, 0.320307f,0.049501f,0.946020f, -0.934594f,-0.255398f,0.247599f,
    -0.231699f,-0.030900f,0.972297f, -0.403407f,0.386007f,0.829615f, -0.965993f,0.258298f,0.011800f,
    -0.934594f,-0.255398f,0.247599f, -0.231699f,-0.030900f,0.972297f, -0.965993f,0.258298f,0.011800f,
    -0.735340f,0.664236f,-0.134407f, -0.168496f,0.202495f,-0.964678f, -0.965993f,0.258298f,0.011800f
};

// Spin a unit-rim vertex by wheelAngle around local +Z, then place in car space.
static void TransformWheelObjVertex(float lx, float ly, float lz, float scale,
                                    float cAng, float sAng,
                                    rmt::Vector& outPos, rmt::Vector& outNrm,
                                    float nx, float ny, float nz)
{
    const float px = (lx * cAng + ly * sAng) * scale;
    const float py = (-lx * sAng + ly * cAng) * scale;
    const float pz = lz * scale;
    outPos = WheelLocalToCar(px, py, pz);

    const float qx = nx * cAng + ny * sAng;
    const float qy = -nx * sAng + ny * cAng;
    const float qz = nz;
    outNrm = WheelLocalToCar(qx, qy, qz);
    outNrm.x -= g.activeWheelCentre.x;
    outNrm.y -= g.activeWheelCentre.y;
    outNrm.z -= g.activeWheelCentre.z;
}

static void DrawObjWheel(pddiShader* shader)
{
    if (kVrWheelObjTriCount <= 0) return;

    const int vertCount = kVrWheelObjTriCount * 3;
    pddiPrimStream* stream = p3d::pddi->BeginPrims(shader,
        PDDI_PRIM_TRIANGLES, PDDI_V_N, vertCount);
    if (!stream) return;

    // Use visual angle so the mesh matches the smoothed hands.
    const float cAng = std::cos(g.wheelVisualAngle);
    const float sAng = std::sin(g.wheelVisualAngle);
    const float scale = g.activeWheelRadius;
    for (int i = 0; i < vertCount; ++i)
    {
        const float* p = &kVrWheelObjPositions[static_cast<size_t>(i) * 3];
        const float* n = &kVrWheelObjNormals[static_cast<size_t>(i) * 3];
        rmt::Vector local, normal;
        TransformWheelObjVertex(p[0], p[1], p[2], scale, cAng, sAng, local, normal,
                                n[0], n[1], n[2]);
        rmt::Vector worldPos, worldNormal;
        g.cullingBaseCamera.Transform(local, &worldPos);
        g.cullingBaseCamera.RotateVector(normal, &worldNormal);
        worldNormal.NormalizeSafe();
        stream->Normal(worldNormal.x, worldNormal.y, worldNormal.z);
        stream->Coord(worldPos.x, worldPos.y, worldPos.z);
    }
    p3d::pddi->EndPrims(stream);
}

// Honor Roller internal name confirmed via log: "honor_v"
static bool IsHonorRoller(Vehicle* v)
{
    return v && v->GetName() && std::strcmp(v->GetName(), "honor_v") == 0;
}

// wheeloffsets.cfg ONLY at /sdcard/SimpsonsHitRun/scripts
struct SavedWheelOffset
{
    char name[48];
    float x, y, z;
    float yaw, pitch;
    float radius;
    float yokeX, yokeY, yokeZ;
    bool hasYoke;
};
static SavedWheelOffset s_wheelOffsets[64];
static int s_wheelOffsetCount = 0;
static bool s_wheelOffsetsLoaded = false;
static char s_appliedVehicle[48] = {};

static bool EnsureDirectory(const char* path)
{
    if (!path || !path[0]) return false;
    struct stat st;
    if (stat(path, &st) == 0)
        return (st.st_mode & S_IFDIR) != 0;
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

static std::string GetWheelOffsetPath()
{
    static const char* kRoot = "/sdcard/SimpsonsHitRun";
    static const char* kScripts = "/sdcard/SimpsonsHitRun/scripts";
    if (EnsureDirectory(kRoot) && EnsureDirectory(kScripts))
        return std::string(kScripts) + "/wheeloffsets.cfg";
    char* path = SDL_GetPrefPath("c4rlox", "simpsons");
    if (path)
    {
        std::string fallback(path);
        fallback += "wheeloffsets.cfg";
        SDL_free(path);
        return fallback;
    }
    return std::string(kScripts) + "/wheeloffsets.cfg";
}

static void LoadWheelOffsets()
{
    if (s_wheelOffsetsLoaded) return;
    s_wheelOffsetsLoaded = true;
    s_wheelOffsetCount = 0;

    // Built-in defaults (baked from a full calibration pass). These are always
    // present so the mod works with no external wheeloffsets.cfg. A file on
    // disk, if present, can still override individual vehicles below.
    struct Builtin { const char* name; float x,y,z,yaw,pitch,radius; };
    static const Builtin kDefaults[] = {
        { "lisa_v",   0.01613f, -0.38500f, 0.39779f, -0.00659f, 0.50959f, 0.19446f },
        { "famil_v", -0.00382f, -0.41158f, 0.37945f,  0.02341f, 0.66233f, 0.20877f },
        { "apu_v",    0.00631f, -0.46367f, 0.36340f,  0.02732f, 0.44522f, 0.20648f },
        { "cNerd",    0.01044f, -0.31434f, 0.35121f,  0.00784f, 0.46942f, 0.20698f },
        { "otto_v",   0.10405f, -0.47492f, 0.31119f,  0.02658f, 0.60326f, 0.20893f },
        { "scorp_v",  0.01657f, -0.47334f, 0.43078f,  0.05542f, 0.61113f, 0.21795f },
        { "krust_v",  0.00171f, -0.47198f, 0.36888f,  0.02079f, 0.51784f, 0.21710f },
        { "snake_v",  0.03784f, -0.45360f, 0.37548f,  0.01195f, 0.50825f, 0.20563f },
        { "moe_v",    0.01468f, -0.36201f, 0.42586f,  0.00891f, 0.46700f, 0.21367f },
        { "skinn_v", -0.01012f, -0.44707f, 0.41667f,  0.00720f, 0.42524f, 0.21238f },
        { "homer_v",  0.01300f, -0.46922f, 0.33622f,  0.02873f, 0.51844f, 0.21542f },
        { "zombi_v",  0.00793f, -0.46667f, 0.47731f,  0.05720f, 0.52894f, 0.21259f },
        { "burns_v", -0.01447f, -0.45135f, 0.38583f, -0.01748f, 0.73589f, 0.18244f },
        { "willi_v", -0.00136f, -0.35275f, 0.47582f,  0.02595f, 1.04442f, 0.25279f },
        { "gramp_v",  0.02381f, -0.43905f, 0.42957f,  0.01888f, 1.15000f, 0.19824f },
        { "gramR_v",  0.02831f, -0.39646f, 0.44110f,  0.02155f, 1.08657f, 0.19177f },
        { "knigh_v",  0.02858f, -0.41090f, 0.31822f,  0.02204f, 0.58144f, 0.20157f },
        { "oblit_v", -0.01167f, -0.34204f, 0.30652f,  0.02919f, 0.53615f, 0.20857f },
        { "hype_v",  -0.00799f, -0.40367f, 0.38671f,  0.01870f, 0.49044f, 0.20550f },
        { "cArmor",  -0.04665f, -0.53862f, 0.43590f,  0.03451f, 0.47955f, 0.20010f },
        { "cSedan",  -0.03386f, -0.25671f, 0.45302f,  0.02565f, 0.48265f, 0.20716f },
        { "cCola",   -0.01237f, -0.35964f, 0.31904f, -0.00799f, 0.49753f, 0.19719f },
        { "cCube",    0.01124f, -0.36866f, 0.35387f,  0.03337f, 0.46037f, 0.20433f },
        { "cCurator", 0.04684f, -0.33153f, 0.32621f,  0.03073f, 0.48005f, 0.21307f },
        { "cDonut",   0.00071f, -0.44639f, 0.33799f, -0.02624f, 1.14421f, 0.20656f },
        { "cDuff",   -0.03209f, -0.35631f, 0.37125f,  0.02190f, 0.48579f, 0.20701f },
        { "cHears",  -0.00628f, -0.43141f, 0.34741f,  0.02927f, 0.49724f, 0.19691f },
        { "cKlimo",   0.00119f, -0.33745f, 0.38750f,  0.01110f, 0.48720f, 0.20670f },
        { "cLimo",    0.05795f, -0.41616f, 0.47305f,  0.01548f, 0.55664f, 0.20558f },
        { "cPolice",  0.05657f, -0.43103f, 0.38435f,  0.01117f, 0.52202f, 0.23457f },
        { "cVan",     0.00846f, -0.36129f, 0.29493f,  0.01696f, 0.50010f, 0.21625f },
        { "cFire_v", -0.01471f, -0.41707f, 0.41267f,  0.03243f, 1.15000f, 0.27762f },
        { "cBone",    0.01391f, -0.37719f, 0.34691f,  0.01425f, 0.44759f, 0.20645f },
        { "bookb_v", -0.00635f, -0.48137f, 0.36469f, -0.01464f, 0.73348f, 0.21530f },
        { "marge_v",  0.06615f, -0.35983f, 0.35690f,  0.01337f, 0.37457f, 0.20041f },
        { "carhom_v", 0.08383f, -0.46583f, 0.42535f,  0.06928f, 0.65502f, 0.20769f },
        { "bbman_v", -0.08973f, -0.37953f, 0.38698f,  0.01189f, 0.61379f, 0.25165f },
        { "elect_v",  0.01657f, -0.44480f, 0.36772f,  0.04736f, 0.55949f, 0.22825f },
        { "bart_v",   0.01335f, -0.48874f, 0.42682f,  0.02642f, 0.51202f, 0.20959f },
        { "frink_v", -0.05880f, -0.40912f, 0.39067f,  0.01342f, 0.71745f, 0.22367f },
        { "smith_v", -0.04799f, -0.36498f, 0.46251f,  0.00579f, 0.44360f, 0.25196f },
        { "mrplo_v",  0.02160f, -0.40818f, 0.34203f,  0.02051f, 0.29448f, 0.22384f },
        { "fone_v",  -0.00916f, -0.38110f, 0.33936f, -0.00373f, 0.56705f, 0.22477f },
        { "cletu_v",  0.00412f, -0.34147f, 0.37589f,  0.00175f, 0.57438f, 0.22487f },
        { "plowk_v", -0.00522f, -0.37062f, 0.31314f, -0.00703f, 0.67230f, 0.20901f },
        { "wiggu_v", -0.05401f, -0.35053f, 0.35330f,  0.01404f, 0.36360f, 0.20733f },
        { "huskA",    0.00667f,  0.43383f, 0.06179f,  0.12148f,-0.21514f, 0.10000f },
    };
    for (unsigned i = 0; i < sizeof(kDefaults)/sizeof(kDefaults[0]) && s_wheelOffsetCount < 64; ++i)
    {
        SavedWheelOffset& o = s_wheelOffsets[s_wheelOffsetCount];
        std::memset(&o, 0, sizeof(o));
        std::strncpy(o.name, kDefaults[i].name, sizeof(o.name)-1);
        o.x = kDefaults[i].x; o.y = kDefaults[i].y; o.z = kDefaults[i].z;
        o.yaw = kDefaults[i].yaw; o.pitch = kDefaults[i].pitch; o.radius = kDefaults[i].radius;
        o.yokeX = kYokeAnchorDefault.x; o.yokeY = kYokeAnchorDefault.y; o.yokeZ = kYokeAnchorDefault.z;
        o.hasYoke = false;
        ++s_wheelOffsetCount;
    }

    // Optional on-disk overrides (same format as before). Any vehicle listed
    // in the file replaces the matching built-in entry.
    const std::string filename = GetWheelOffsetPath();
    FILE* file = std::fopen(filename.c_str(), "rb");
    if (file)
    {
        char line[256];
        while (std::fgets(line, sizeof(line), file))
        {
            char name[48];
            float x=0,y=0,z=0,yaw=0,pitch=0,radius=kVrWheelRadius,yx=0,yy=0,yz=0;
            bool hasYoke = false;
            bool parsed = false;
            if (std::sscanf(line, "%47[^=]=%f,%f,%f,%f,%f,%f;%f,%f,%f",
                            name, &x, &y, &z, &yaw, &pitch, &radius, &yx, &yy, &yz) == 10)
            { hasYoke = true; parsed = true; }
            else if (std::sscanf(line, "%47[^=]=%f,%f,%f,%f,%f;%f,%f,%f",
                            name, &x, &y, &z, &yaw, &pitch, &yx, &yy, &yz) == 9)
            { hasYoke = true; parsed = true; }
            else if (std::sscanf(line, "%47[^=]=%f,%f,%f,%f,%f,%f", name, &x, &y, &z, &yaw, &pitch, &radius) == 7)
            { parsed = true; }
            else if (std::sscanf(line, "%47[^=]=%f,%f,%f,%f,%f", name, &x, &y, &z, &yaw, &pitch) == 6)
            { parsed = true; }
            else if (std::sscanf(line, "%47[^=]=%f,%f,%f", name, &x, &y, &z) == 4)
            { parsed = true; }
            if (!parsed) continue;

            int slot = -1;
            for (int i = 0; i < s_wheelOffsetCount; ++i)
                if (std::strcmp(s_wheelOffsets[i].name, name) == 0) { slot = i; break; }
            if (slot < 0)
            {
                if (s_wheelOffsetCount >= 64) continue;
                slot = s_wheelOffsetCount++;
                std::memset(&s_wheelOffsets[slot], 0, sizeof(s_wheelOffsets[slot]));
                std::strncpy(s_wheelOffsets[slot].name, name, sizeof(s_wheelOffsets[slot].name)-1);
            }
            SavedWheelOffset& o = s_wheelOffsets[slot];
            o.x=x; o.y=y; o.z=z; o.yaw=yaw; o.pitch=pitch; o.radius=radius;
            o.hasYoke = hasYoke;
            if (hasYoke) { o.yokeX=yx; o.yokeY=yy; o.yokeZ=yz; }
            else
            {
                o.yokeX = kYokeAnchorDefault.x;
                o.yokeY = kYokeAnchorDefault.y;
                o.yokeZ = kYokeAnchorDefault.z;
            }
        }
        std::fclose(file);
    }
    XRLOG("loaded %d wheel offset(s) (%d built-in)", s_wheelOffsetCount,
          (int)(sizeof(kDefaults)/sizeof(kDefaults[0])));
}

static void SaveWheelOffsets()
{
    const std::string filename = GetWheelOffsetPath();
    FILE* file = std::fopen(filename.c_str(), "wb");
    if (!file) { XRERR("failed to write %s", filename.c_str()); return; }
    for (int i = 0; i < s_wheelOffsetCount; ++i)
    {
        const SavedWheelOffset& o = s_wheelOffsets[i];
        if (o.hasYoke)
            std::fprintf(file, "%s=%.5f,%.5f,%.5f,%.5f,%.5f,%.5f;%.5f,%.5f,%.5f\n",
                         o.name, o.x, o.y, o.z, o.yaw, o.pitch, o.radius, o.yokeX, o.yokeY, o.yokeZ);
        else
            std::fprintf(file, "%s=%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n",
                         o.name, o.x, o.y, o.z, o.yaw, o.pitch, o.radius);
    }
    std::fclose(file);
}

static void StoreWheelOffsetForVehicle(const char* name, const rmt::Vector& centre,
                                       float yaw, float pitch, float radius,
                                       const rmt::Vector& yoke, bool honor)
{
    if (!name || !name[0]) return;
    LoadWheelOffsets();
    int slot = -1;
    for (int i = 0; i < s_wheelOffsetCount; ++i)
        if (std::strcmp(s_wheelOffsets[i].name, name) == 0) { slot = i; break; }
    if (slot < 0)
    {
        if (s_wheelOffsetCount >= 64) return;
        slot = s_wheelOffsetCount++;
        std::strncpy(s_wheelOffsets[slot].name, name, sizeof(s_wheelOffsets[slot].name)-1);
        s_wheelOffsets[slot].name[sizeof(s_wheelOffsets[slot].name)-1] = 0;
    }
    s_wheelOffsets[slot].x = centre.x; s_wheelOffsets[slot].y = centre.y; s_wheelOffsets[slot].z = centre.z;
    s_wheelOffsets[slot].yaw = yaw; s_wheelOffsets[slot].pitch = pitch; s_wheelOffsets[slot].radius = radius;
    s_wheelOffsets[slot].hasYoke = honor;
    if (honor)
    {
        s_wheelOffsets[slot].yokeX = yoke.x; s_wheelOffsets[slot].yokeY = yoke.y; s_wheelOffsets[slot].yokeZ = yoke.z;
    }
    SaveWheelOffsets();
    XRLOG("saved wheel '%s' pos+(yaw=%.2f pitch=%.2f radius=%.3f)", name, yaw, pitch, radius);
}

static void ApplyWheelOffsetForVehicle(Vehicle* v)
{
    LoadWheelOffsets();
    if (!v || !v->GetName())
    {
        g.activeWheelCentre = kVrWheelCentreDefault;
        g.activeYokeAnchor = kYokeAnchorDefault;
        g.activeWheelYaw = 0.0f; g.activeWheelPitch = 0.0f; g.activeWheelRadius = kVrWheelRadius;
        s_appliedVehicle[0] = 0;
        return;
    }
    const char* name = v->GetName();
    if (s_appliedVehicle[0] && std::strcmp(s_appliedVehicle, name) == 0)
        return;
    g.activeWheelCentre = kVrWheelCentreDefault;
    g.activeYokeAnchor = kYokeAnchorDefault;
    g.activeWheelYaw = 0.0f; g.activeWheelPitch = 0.0f; g.activeWheelRadius = kVrWheelRadius;
    std::strncpy(s_appliedVehicle, name, sizeof(s_appliedVehicle)-1);
    s_appliedVehicle[sizeof(s_appliedVehicle)-1] = 0;
    for (int i = 0; i < s_wheelOffsetCount; ++i)
    {
        if (std::strcmp(s_wheelOffsets[i].name, name) == 0)
        {
            g.activeWheelCentre.Set(s_wheelOffsets[i].x, s_wheelOffsets[i].y, s_wheelOffsets[i].z);
            g.activeWheelYaw = s_wheelOffsets[i].yaw;
            g.activeWheelPitch = s_wheelOffsets[i].pitch;
            g.activeWheelRadius = s_wheelOffsets[i].radius;
            if (s_wheelOffsets[i].hasYoke)
                g.activeYokeAnchor.Set(s_wheelOffsets[i].yokeX, s_wheelOffsets[i].yokeY, s_wheelOffsets[i].yokeZ);
            else
                g.activeYokeAnchor.Set(g.activeWheelCentre.x,
                                       g.activeWheelCentre.y - kYokeArmLength + 0.01f,
                                       g.activeWheelCentre.z - 0.12f);
            g.wheelMeshHidden = true;
            return;
        }
    }
    g.wheelMeshHidden = false;
}

// Wheel-local (rim plane XY, face +Z) → car space. Pitch then yaw.
static rmt::Vector WheelLocalToCar(float lx, float ly, float lz)
{
    const float cp = std::cos(g.activeWheelPitch), sp = std::sin(g.activeWheelPitch);
    const float cy = std::cos(g.activeWheelYaw),   sy = std::sin(g.activeWheelYaw);
    const float y1 = cp * ly - sp * lz;
    const float z1 = sp * ly + cp * lz;
    const float x2 = cy * lx + sy * z1;
    const float z2 = -sy * lx + cy * z1;
    return rmt::Vector(g.activeWheelCentre.x + x2,
                       g.activeWheelCentre.y + y1,
                       g.activeWheelCentre.z + z2);
}

// Inverse of WheelLocalToCar (delta from centre → wheel-local). Used for grab + steer
// so turning still works after a tilted save.
static void CarDeltaToWheelLocal(float dx, float dy, float dz, float& lx, float& ly, float& lz)
{
    const float cy = std::cos(g.activeWheelYaw), sy = std::sin(g.activeWheelYaw);
    const float cp = std::cos(g.activeWheelPitch), sp = std::sin(g.activeWheelPitch);
    // inverse yaw
    const float x1 =  cy * dx - sy * dz;
    const float z1 =  sy * dx + cy * dz;
    const float y1 = dy;
    // inverse pitch
    lx = x1;
    ly =  cp * y1 + sp * z1;
    lz = -sp * y1 + cp * z1;
}

static float LerpAngle(float from, float to, float t)
{
    return from + UnwrapDelta(to - from) * t;
}

static void UpdateVrSteeringWheel()
{
    Character* player = GetCharacterManager()->GetCharacter(0);
    const bool active = g.vrModeEnabled && g.vehicleControlMode==1
                        && player && player->IsInCar();
    if (!active)
    {
        g.wheelGrabbed[0] = g.wheelGrabbed[1] = false;
        g.wheelAngle = 0.0f;
        g.wheelVisualAngle = 0.0f;
        g.wheelHonk = false;
        g.wheelTrim = 0.0f;
        g.yokeThrottle = 0.0f;
        g.yokeFullGasLatched = g.yokeFullBrakeLatched = false;
        g.wheelAdjustMode = false;
        g.wheelAdjustHoldSec = 0.0f;
        g.wheelAdjustVehicle[0] = 0;
        s_appliedVehicle[0] = 0;
        return;
    }

    if (g.activeWheelCentre.x == 0.0f && g.activeWheelCentre.y == 0.0f &&
        g.activeWheelCentre.z == 0.0f && s_appliedVehicle[0] == 0)
    {
        g.activeWheelCentre = kVrWheelCentreDefault;
        g.activeYokeAnchor = kYokeAnchorDefault;
        g.activeWheelYaw = 0.0f;
        g.activeWheelPitch = 0.0f;
        g.activeWheelRadius = kVrWheelRadius;
    }

    Vehicle* vehicle = player->GetTargetVehicle();
    ApplyWheelOffsetForVehicle(vehicle);
    const bool honorRoller = IsHonorRoller(vehicle);
    g.wheelHonk = false; // re-evaluated below for the round wheel only

    {
        static Uint32 s_prevTicks = 0;
        const Uint32 now = SDL_GetTicks();
        float dt = (s_prevTicks == 0) ? 0.016f : (now - s_prevTicks) * 0.001f;
        s_prevTicks = now;
        if (dt < 0.0f || dt > 0.1f) dt = 0.016f;

        const bool bothClicks = g.stickClick[0] && g.stickClick[1];
        if (bothClicks)
        {
            g.wheelAdjustHoldSec += dt;
            if (!g.wheelAdjustMode && g.wheelAdjustHoldSec >= kWheelAdjustHoldSec)
            {
                g.wheelAdjustMode = true;
                g.wheelMeshHidden = false;
                if (vehicle && vehicle->GetName())
                {
                    std::strncpy(g.wheelAdjustVehicle, vehicle->GetName(), sizeof(g.wheelAdjustVehicle)-1);
                    g.wheelAdjustVehicle[sizeof(g.wheelAdjustVehicle)-1] = 0;
                }
                FireHaptic(0, 0.9f, 120.0f, 120.0f);
                FireHaptic(1, 0.9f, 120.0f, 120.0f);
                XRLOG("VR wheel adjust ON");
            }
        }
        else if (g.wheelAdjustMode)
        {
            const char* saveName = g.wheelAdjustVehicle[0] ? g.wheelAdjustVehicle
                : (vehicle && vehicle->GetName() ? vehicle->GetName() : "unknown");
            StoreWheelOffsetForVehicle(saveName, g.activeWheelCentre,
                                       g.activeWheelYaw, g.activeWheelPitch, g.activeWheelRadius,
                                       g.activeYokeAnchor, honorRoller);
            if (saveName && saveName[0])
            {
                std::strncpy(s_appliedVehicle, saveName, sizeof(s_appliedVehicle)-1);
                s_appliedVehicle[sizeof(s_appliedVehicle)-1] = 0;
            }
            g.wheelAngle = 0.0f;
            g.wheelMeshHidden = true;
            g.wheelGrabbed[0] = g.wheelGrabbed[1] = false;
            FireHaptic(0, 0.6f, 80.0f, 90.0f);
            FireHaptic(1, 0.6f, 80.0f, 90.0f);
            XRLOG("VR wheel adjust OFF — pose kept for steering");
            g.wheelAdjustMode = false;
            g.wheelAdjustHoldSec = 0.0f;
        }
        else
            g.wheelAdjustHoldSec = 0.0f;

        if (g.wheelAdjustMode && g.handPoseValid[0] && g.handPoseValid[1])
        {
            const rmt::Matrix pose0 = PoseToGame(RelativePose(g.origin, g.handPoses[0]));
            const rmt::Matrix pose1 = PoseToGame(RelativePose(g.origin, g.handPoses[1]));
            const rmt::Vector p0 = pose0.Row(3);
            const rmt::Vector p1 = pose1.Row(3);
            rmt::Vector mid((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f, (p0.z + p1.z) * 0.5f);

            rmt::Vector across(p1.x - p0.x, p1.y - p0.y, p1.z - p0.z);
            const float handDist = across.Magnitude();
            if (handDist > 0.02f) across.NormalizeSafe();
            else across.Set(1.0f, 0.0f, 0.0f);

            // Yaw from hand diameter (stable).
            float targetYaw = std::atan2(-across.z, across.x);

            // Radius from hand-to-hand distance — pull hands apart/together
            // to grow/shrink the rim (hands sit roughly opposite each other
            // on the rim, so distance ≈ diameter).
            float targetRadius = handDist * 0.5f;
            if (targetRadius < kVrWheelRadiusMin) targetRadius = kVrWheelRadiusMin;
            if (targetRadius > kVrWheelRadiusMax) targetRadius = kVrWheelRadiusMax;

            // Pitch from controller tilt — smooth up/down. Both hands averaged.
            rmt::Vector f0, f1;
            pose0.RotateVector(rmt::Vector(0.0f, 0.0f, -1.0f), &f0);
            pose1.RotateVector(rmt::Vector(0.0f, 0.0f, -1.0f), &f1);
            auto pitchOf = [](const rmt::Vector& f) {
                const float h = std::sqrt(f.x * f.x + f.z * f.z);
                return std::atan2(-f.y, std::max(h, 1e-5f));
            };
            float targetPitch = 0.5f * (pitchOf(f0) + pitchOf(f1));
            if (targetPitch >  1.15f) targetPitch =  1.15f;
            if (targetPitch < -1.15f) targetPitch = -1.15f;

            const float t = kWheelAdjustFollow;
            g.activeWheelCentre.x += (mid.x - g.activeWheelCentre.x) * t;
            g.activeWheelCentre.y += (mid.y - g.activeWheelCentre.y) * t;
            g.activeWheelCentre.z += (mid.z - g.activeWheelCentre.z) * t;
            g.activeYokeAnchor.Set(g.activeWheelCentre.x,
                                   g.activeWheelCentre.y - kYokeArmLength + 0.01f,
                                   g.activeWheelCentre.z - 0.12f);
            g.activeWheelYaw   = LerpAngle(g.activeWheelYaw, targetYaw, t);
            g.activeWheelPitch = g.activeWheelPitch + (targetPitch - g.activeWheelPitch) * t;
            g.activeWheelRadius = g.activeWheelRadius + (targetRadius - g.activeWheelRadius) * t;

            // Rim spin in the oriented plane (same space used later for driving).
            float lx0, ly0, lz0, lx1, ly1, lz1;
            CarDeltaToWheelLocal(p0.x - g.activeWheelCentre.x, p0.y - g.activeWheelCentre.y,
                                 p0.z - g.activeWheelCentre.z, lx0, ly0, lz0);
            CarDeltaToWheelLocal(p1.x - g.activeWheelCentre.x, p1.y - g.activeWheelCentre.y,
                                 p1.z - g.activeWheelCentre.z, lx1, ly1, lz1);
            float targetAngle = 0.5f * (
                UnwrapDelta(std::atan2(lx0, ly0) - (-kOptimalGrabAngle)) +
                UnwrapDelta(std::atan2(lx1, ly1) - ( kOptimalGrabAngle)));
            g.wheelAngle = LerpAngle(g.wheelAngle, targetAngle, t);

            g.wheelGrabbed[0] = g.wheelGrabbed[1] = true;
            g.wheelGrabOffset[0] = -kOptimalGrabAngle;
            g.wheelGrabOffset[1] =  kOptimalGrabAngle;
            g.wheelGrabOrientAngle[0] = g.wheelAngle - kOptimalGrabAngle;
            g.wheelGrabOrientAngle[1] = g.wheelAngle + kOptimalGrabAngle;
            g.wheelGrabOrientRot[0] = pose0; g.wheelGrabOrientRot[0].Row(3).Set(0,0,0);
            g.wheelGrabOrientRot[1] = pose1; g.wheelGrabOrientRot[1].Row(3).Set(0,0,0);
            g.yokeThrottle = 0.0f;
            return;
        }
        if (g.wheelAdjustMode)
            return;
    }

    // ----------------------------------------------------------------
    // HONOR ROLLER – T-handle yoke
    //   Lower left hand  → turn left  (T rocks down on the left)
    //   Lower right hand → turn right (T rocks down on the right)
    //   Push forward     → accelerate
    //   Pull back        → brake
    // ----------------------------------------------------------------
    if (honorRoller)
    {
        // Where the bar sits at rest — top of the fixed shaft.
        const rmt::Vector yokeGrabCentre(g.activeYokeAnchor.x, g.activeYokeAnchor.y + kYokeArmLength, g.activeYokeAnchor.z);
        float handY[2] = {0.0f, 0.0f};
        float handZ[2] = {0.0f, 0.0f};
        bool  held[2]  = {false, false};

        for (unsigned hand = 0; hand < 2; ++hand)
        {
            if (!g.handPoseValid[hand])
            {
                g.wheelGrabbed[hand] = false;
                continue;
            }

            const rmt::Matrix pose = PoseToGame(RelativePose(g.origin, g.handPoses[hand]));
            const rmt::Vector p = pose.Row(3);
            const float dx = p.x - yokeGrabCentre.x;
            const float dy = p.y - yokeGrabCentre.y;
            const float dz = p.z - yokeGrabCentre.z;
            const float radial = std::sqrt(dx * dx + dy * dy);

            // Generous grab volume – easy to hold and move freely
            const bool close = radial < (kVrWheelRadius + kRadialTolerance + 0.12f)
                            && std::fabs(dz) < 0.28f
                            && std::fabs(dy) < 0.28f;

            const bool squeezed = g.gripValue[hand] > kGrabGripThreshold;

            if (!g.wheelGrabbed[hand])
            {
                if (squeezed && close)
                {
                    g.wheelGrabbed[hand] = true;
                    // Store rest height at grab so drops are relative
                    g.wheelGrabOffset[hand] = p.y;
                    g.wheelGrabOrientRot[hand] = pose;
                    g.wheelGrabOrientRot[hand].Row(3).Set(0.0f, 0.0f, 0.0f);
                }
            }
            else if (!squeezed)
            {
                g.wheelGrabbed[hand] = false;
            }
            else
            {
                held[hand] = true;
                // dy relative to grab height (negative = hand pulled down)
                handY[hand] = p.y - g.wheelGrabOffset[hand];
                handZ[hand] = dz;
            }
        }

        if (held[0] || held[1])
        {
            // Differential height: left down → positive (left turn),
            // right down → negative (right turn). Single-hand also works.
            float leftDrop  = held[0] ? -handY[0] : 0.0f;  // positive when left lowered
            float rightDrop = held[1] ? -handY[1] : 0.0f; // positive when right lowered
            float steerNorm = (leftDrop - rightDrop) / kYokeSteerRange;
            steerNorm = std::max(-1.0f, std::min(1.0f, steerNorm));
            float steer = steerNorm * kVrWheelMaxAngle;
            g.wheelAngle += (steer - g.wheelAngle) * 0.80f;

            // Average forward/back of held hands → throttle
            float zSum = 0.0f;
            int zCount = 0;
            if (held[0]) { zSum += handZ[0]; ++zCount; }
            if (held[1]) { zSum += handZ[1]; ++zCount; }
            if (zCount > 0)
            {
                float pitch = (zSum / static_cast<float>(zCount)) / kYokePitchRange;
                pitch = std::max(-1.0f, std::min(1.0f, pitch));
                g.yokeThrottle += (pitch - g.yokeThrottle) * 0.75f;
            }

            // One crisp pulse the instant full gas or full brake is reached,
            // not every frame it stays pinned there. Gas gets a quick, high
            // buzz (revving feel); brake gets a heavier, lower thunk.
            if (g.yokeThrottle >= kYokeFullThrottle)
            {
                if (!g.yokeFullGasLatched)
                {
                    if (held[0]) FireHaptic(0, 0.85f, 70.0f, 160.0f);
                    if (held[1]) FireHaptic(1, 0.85f, 70.0f, 160.0f);
                    g.yokeFullGasLatched = true;
                }
                g.yokeFullBrakeLatched = false;
            }
            else if (g.yokeThrottle <= -kYokeFullThrottle)
            {
                if (!g.yokeFullBrakeLatched)
                {
                    if (held[0]) FireHaptic(0, 0.9f, 90.0f, 90.0f);
                    if (held[1]) FireHaptic(1, 0.9f, 90.0f, 90.0f);
                    g.yokeFullBrakeLatched = true;
                }
                g.yokeFullGasLatched = false;
            }
            else
            {
                g.yokeFullGasLatched = false;
                g.yokeFullBrakeLatched = false;
            }
        }
        else
        {
            g.wheelAngle *= 0.90f;
            if (std::fabs(g.wheelAngle) < 0.01f) g.wheelAngle = 0.0f;
            g.yokeThrottle *= 0.85f;
            if (std::fabs(g.yokeThrottle) < 0.02f) g.yokeThrottle = 0.0f;
            g.yokeFullGasLatched = false;
            g.yokeFullBrakeLatched = false;
        }
        // Visual smooth (same as normal cars) so yoke hands don't jitter.
        {
            const float visFollow = 0.55f;
            g.wheelVisualAngle += (g.wheelAngle - g.wheelVisualAngle) * visFollow;
            if (std::fabs(g.wheelVisualAngle) < 0.002f)
                g.wheelVisualAngle = 0.0f;
        }
        return;
    }

    // ----------------------------------------------------------------
    // NORMAL CARS – circular wheel (pure grab-time snapshot)
    //
    // On grab we record the hand's angle and the wheel's angle. Every
    // subsequent frame the target is simply:
    //   grabWheelAngle + unwrap(currentHandAngle - grabHandAngle)
    // The snapshots themselves never advance. There is no progressive
    // accumulation and no direction-dependent step limit, so left after
    // right is exactly as easy as right after left. When both hands
    // release, the rim springs back to centre quickly.
    // ----------------------------------------------------------------
    float targetSum = 0.0f;
    int targetCount = 0;

    for (unsigned hand = 0; hand < 2; ++hand)
    {
        // Tracking loss: keep the last known target so the rim does not
        // jump, but mark the hand-angle reference invalid so the next good
        // sample re-snapshots instead of inventing a huge delta.
        if (!g.handPoseValid[hand])
        {
            if (g.wheelGrabbed[hand])
            {
                targetSum += g.wheelGrabTarget[hand];
                ++targetCount;
                g.wheelGrabAngle[hand] = 1e10f; // sentinel → resync
            }
            continue;
        }

        const rmt::Matrix pose = PoseToGame(RelativePose(g.origin, g.handPoses[hand]));
        const rmt::Vector p = pose.Row(3);
        // Work in the wheel's local plane (supports saved tilt/yaw).
        float dx, dy, dz;
        CarDeltaToWheelLocal(p.x - g.activeWheelCentre.x,
                             p.y - g.activeWheelCentre.y,
                             p.z - g.activeWheelCentre.z,
                             dx, dy, dz);
        const float radial = std::sqrt(dx * dx + dy * dy);

        // Free hand on the hub = horn (independent of rim grab).
        if (!g.wheelGrabbed[hand] && radial < kHonkRadius && std::fabs(dz) < kHonkDepth)
            g.wheelHonk = true;

        const bool closeEnough =
            std::fabs(radial - g.activeWheelRadius) < kRadialTolerance &&
            std::fabs(dz) < kDepthTolerance &&
            radial > kMinRadial;

        // Hysteresis on the grip threshold so the grab state cannot flicker.
        const bool squeezed = g.wheelGrabbed[hand]
                            ? (g.gripValue[hand] > kGrabReleaseThreshold)
                            : (g.gripValue[hand] > kGrabGripThreshold);
        const float angle = std::atan2(dx, dy);
        const float slotOffset = (hand == 0) ? -kOptimalGrabAngle : kOptimalGrabAngle;

        if (!g.wheelGrabbed[hand])
        {
            if (squeezed && closeEnough)
            {
                g.wheelGrabbed[hand] = true;
                g.wheelGrabOffset[hand] = slotOffset;          // visual 9 / 3
                g.wheelGrabTarget[hand] = g.wheelAngle;       // snapshot wheel
                g.wheelGrabAngle[hand]  = angle;              // snapshot hand
                g.wheelGrabOrientAngle[hand] = g.wheelAngle + slotOffset;
                g.wheelGrabOrientRot[hand] = pose;
                g.wheelGrabOrientRot[hand].Row(3).Set(0.0f, 0.0f, 0.0f);
            }
            continue;
        }

        // Grip-only release – once held, do not drop the grab because of
        // minor radial/depth noise.
        if (!squeezed)
        {
            g.wheelGrabbed[hand] = false;
            continue;
        }

        // Re-acquire after a tracking gap: re-snapshot both the hand angle
        // and the wheel angle so the relative zero stays continuous with
        // whatever the rim was showing during the gap.
        if (g.wheelGrabAngle[hand] > 1e9f)
        {
            g.wheelGrabAngle[hand]  = angle;
            g.wheelGrabTarget[hand] = g.wheelAngle;
            targetSum += g.wheelGrabTarget[hand];
            ++targetCount;
            continue;
        }

        // Near the hub atan2 is noisy – contribute the fixed grab snapshot
        // (i.e. "no additional turn") so the average does not jump.
        if (radial <= kMinRadial)
        {
            targetSum += g.wheelGrabTarget[hand];
            ++targetCount;
            continue;
        }

        // Pure relative motion from the *fixed* grab snapshots.
        //   handTarget = grabWheelAngle + unwrap(currentHand - grabHand)
        // The snapshots themselves are never advanced. That is what makes
        // left and right perfectly symmetric and prevents any sticky
        // "unwind" feel when reversing direction.
        float delta = UnwrapDelta(angle - g.wheelGrabAngle[hand]);
        const float kMaxDelta = 2.5f; // ~143° – only rejects true teleports
        if (delta >  kMaxDelta) delta =  kMaxDelta;
        if (delta < -kMaxDelta) delta = -kMaxDelta;

        float handTarget = g.wheelGrabTarget[hand] + delta;
        handTarget = std::max(-kVrWheelMaxAngle, std::min(kVrWheelMaxAngle, handTarget));

        targetSum += handTarget;
        ++targetCount;
    }

    if (targetCount > 0)
    {
        const float target = targetSum / static_cast<float>(targetCount);

        // Very high follow rate so the rim feels directly connected to the
        // hands. Slightly higher still when the target is on the opposite
        // side of centre – this makes rapid left↔right switches feel instant.
        float follow = kAngleSmooth;
        if (target * g.wheelAngle < 0.0f)
            follow = std::min(1.0f, kAngleSmooth + 0.06f);
        g.wheelAngle += (target - g.wheelAngle) * follow;
        g.wheelAngle = std::max(-kVrWheelMaxAngle, std::min(kVrWheelMaxAngle, g.wheelAngle));

        // Slow bias learning only while the wheel is already near centre.
        // This cancels a persistent small offset caused by an imperfect
        // physical-to-virtual hub match without fighting deliberate turns.
        if (std::fabs(g.wheelAngle) < kTrimEligibleAngle)
            g.wheelTrim += (g.wheelAngle - g.wheelTrim) * kTrimRate;
        g.wheelTrim = std::max(-kTrimMax, std::min(kTrimMax, g.wheelTrim));
    }
    else if (!g.wheelGrabbed[0] && !g.wheelGrabbed[1])
    {
        // Free return to centre – strong exponential so the rim settles in
        // a fraction of a second and feels eager to go straight again.
        g.wheelAngle *= kCentreReturn;
        if (std::fabs(g.wheelAngle) < 0.01f)
            g.wheelAngle = 0.0f;
        g.wheelTrim = 0.0f; // fresh trim on next grab
    }

    // Smooth visual angle toward the authoritative wheelAngle. Hands and the
    // rim mesh use this so micro-jitter from high-rate steering updates is
    // filtered out. Steering input itself is unaffected.
    {
        const float visFollow = 0.55f;
        g.wheelVisualAngle += (g.wheelAngle - g.wheelVisualAngle) * visFollow;
        if (std::fabs(g.wheelVisualAngle) < 0.002f)
            g.wheelVisualAngle = 0.0f;
    }
}

// Hide the seated character mesh only for the first-person VR vehicle modes.
// mVisibleCharacters is only read when entering a car, so flipping that flag at
// runtime does nothing. Instead remove the drawable from the world scene (same
// path SetCharactersVisible(0) uses at get-in time). The original third-person
// camera must keep the driver visible through the vehicle windows.
static void UpdateVrInCarCharacterVisibility()
{
    Character* player = GetCharacterManager()->GetCharacter(0);
    if (!player)
        return;

    static bool s_playerHiddenByVr = false;
    const bool hide = g.vrModeEnabled && player->IsInCar() &&
                      g.vehicleControlMode != 2;

    if (hide)
    {
        if (player->IsVisible())
        {
            player->RemoveFromWorldScene();
            s_playerHiddenByVr = true;
        }

        // A distinct mission driver is rendered on the passenger seat in VR.
        // InCar keeps its driver animations but swaps its local seat position,
        // so only the player's own mesh must be hidden here.
        Vehicle* vehicle = player->GetTargetVehicle();
        Character* driver = vehicle ? vehicle->GetDriver() : NULL;
        if (driver && driver != player)
        {
            const bool trafficDriver =
                TrafficManager::GetInstance()->IsVehicleTrafficVehicle(vehicle);
            if (trafficDriver)
            {
                if (driver->IsVisible())
                    driver->RemoveFromWorldScene();
            }
            else
            {
                // Some mission cars normally hide their seated characters.
                // This NPC must remain visible after the VR seat swap.
                if (!driver->IsVisible())
                    driver->AddToWorldScene();

                rmt::Vector passengerPosition = vehicle->GetPassengerLocation();
                const rmt::Vector animatedPosition = driver->GetPuppet()->GetPosition();
                passengerPosition.y = animatedPosition.y;
                driver->GetPuppet()->SetPosition(passengerPosition);
            }
        }
    }
    else
    {
        // VR off while still in the car: put the mesh back if the vehicle
        // is meant to show characters. On foot, GetOut already re-adds us.
        if (s_playerHiddenByVr)
        {
            if (!player->IsVisible() && player->IsInCar())
            {
                Vehicle* vehicle = player->GetTargetVehicle();
                if (!vehicle || vehicle->mVisibleCharacters)
                    player->AddToWorldScene();
            }
            s_playerHiddenByVr = false;
        }
    }
}

static void MakeProjection(const XrFovf& fov, float n, float f, rmt::Matrix* m)
{
    const float l = std::tan(fov.angleLeft), r = std::tan(fov.angleRight);
    const float b = std::tan(fov.angleDown), t = std::tan(fov.angleUp);
    m->Identity();
    m->Row4(0).Set(2.0f/(r-l), 0, 0, 0);
    m->Row4(1).Set(0, 2.0f/(t-b), 0, 0);
    m->Row4(2).Set(-(r+l)/(r-l), -(t+b)/(t-b), (f+n)/(f-n), 1);
    m->Row4(3).Set(0, 0, (-2.0f*f*n)/(f-n), 0);
}

static bool CreateSwapchains()
{
    uint32_t count = 0;
    g.EnumerateViewConfigurationViews(g.instance, g.system,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, NULL);
    if (count != 2) { XRERR("runtime returned %u stereo views", count); return false; }
    std::vector<XrViewConfigurationView> configs(count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (XR_FAILED(g.EnumerateViewConfigurationViews(g.instance, g.system,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, configs.data()))) return false;

    uint32_t formatCount = 0;
    g.EnumerateSwapchainFormats(g.session, 0, &formatCount, NULL);
    std::vector<int64_t> formats(formatCount);
    g.EnumerateSwapchainFormats(g.session, formatCount, &formatCount, formats.data());
    const int64_t rgba8 =
#if defined(SRR2_VR_RENDERER_VULKAN)
        VK_FORMAT_R8G8B8A8_UNORM;
#else
        0x8058;
#endif
    const int64_t srgb8Alpha8 =
#if defined(SRR2_VR_RENDERER_VULKAN)
        VK_FORMAT_R8G8B8A8_SRGB;
#else
        0x8C43;
#endif
    int64_t chosen = formats.empty() ? rgba8 : formats[0];
#if defined(SRR2_VR_RENDERER_VULKAN)
    // The fragment shader linearizes legacy display-ready colours before the
    // attachment performs its automatic encoding, preserving GLES output.
    for (uint32_t i=0; i<formatCount; ++i)
        if (formats[i] == srgb8Alpha8) { chosen=formats[i]; break; }
    if (chosen != srgb8Alpha8)
        for (uint32_t i=0; i<formatCount; ++i)
            if (formats[i] == rgba8) { chosen=formats[i]; break; }
#else
    for (uint32_t i=0; i<formatCount; ++i)
        if (formats[i] == srgb8Alpha8) { chosen=formats[i]; break; }
    if (chosen != srgb8Alpha8)
        for (uint32_t i=0; i<formatCount; ++i)
            if (formats[i] == rgba8) { chosen=formats[i]; break; }
#endif
    XRLOG("swapchain format 0x%llx", static_cast<long long>(chosen));

    // A single two-layer swapchain is required by GL_OVR_multiview2. Both
    // views consequently use the same extent (OpenXR runtimes on Quest
    // advertise matching stereo recommendations).
    for (uint32_t i=0; i<2; ++i)
    {
        // Quest 3's physical panel is 2064x2208 per eye.  The runtime's
        // recommended size is commonly lower for performance, so request the
        // native panel dimensions while respecting the advertised maximum.
        const uint32_t nativeWidth=2064;
        const uint32_t nativeHeight=2208;
        Eye& e = g.eyes[i];
        const uint32_t scaledWidth=static_cast<uint32_t>(nativeWidth*g.renderScale+0.5f);
        const uint32_t scaledHeight=static_cast<uint32_t>(nativeHeight*g.renderScale+0.5f);
        e.width=static_cast<int32_t>(std::min(scaledWidth,configs[i].maxImageRectWidth))&~3;
        e.height=static_cast<int32_t>(std::min(scaledHeight,configs[i].maxImageRectHeight))&~3;
        e.width=std::max(64,e.width); e.height=std::max(64,e.height);
        e.view.type=XR_TYPE_VIEW;
        XRLOG("eye %u resolution scale=%.0f%% recommended=%ux%u max=%ux%u requested=%dx%d",
              i,g.renderScale*100.0f,configs[i].recommendedImageRectWidth,configs[i].recommendedImageRectHeight,
              configs[i].maxImageRectWidth,configs[i].maxImageRectHeight,e.width,e.height);
        XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
#if defined(SRR2_VR_RENDERER_VULKAN)
        XrSwapchainCreateInfoFoveationFB foveation={
            XR_TYPE_SWAPCHAIN_CREATE_INFO_FOVEATION_FB};
        foveation.flags=XR_SWAPCHAIN_CREATE_FOVEATION_FRAGMENT_DENSITY_MAP_BIT_FB;
        if(g.foveationProfile) ci.next=&foveation;
#endif
        ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT
#if defined(SRR2_VR_RENDERER_VULKAN)
                      |XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|
                       XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT
#endif
                      ;
        ci.format=chosen; ci.sampleCount=1; ci.width=e.width; ci.height=e.height;
#if defined(SRR2_VR_RENDERER_VULKAN)
        e.vulkanFormat=static_cast<VkFormat>(chosen);
#endif
        ci.faceCount=1; ci.arraySize=2; ci.mipCount=1;
        if(i==1)
        {
            e.width=g.eyes[0].width;e.height=g.eyes[0].height;
            e.swapchain=g.eyes[0].swapchain;e.images=g.eyes[0].images;
#if defined(SRR2_VR_RENDERER_VULKAN)
            e.vulkanImages=g.eyes[0].vulkanImages;
            e.foveationImages=g.eyes[0].foveationImages;
            e.vulkanImageInitialized=g.eyes[0].vulkanImageInitialized;
#endif
            break;
        }
        if (XR_FAILED(g.CreateSwapchain(g.session, &ci, &e.swapchain))) return false;
        uint32_t imageCount=0;
        g.EnumerateSwapchainImages(e.swapchain, 0, &imageCount, NULL);
#if defined(SRR2_VR_RENDERER_VULKAN)
        e.vulkanImages.resize(imageCount,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        e.foveationImages.resize(imageCount,{XR_TYPE_SWAPCHAIN_IMAGE_FOVEATION_VULKAN_FB});
        if(g.foveationProfile) for(uint32_t j=0;j<imageCount;++j)
            e.vulkanImages[j].next=&e.foveationImages[j];
        if(XR_FAILED(g.EnumerateSwapchainImages(e.swapchain,imageCount,&imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(e.vulkanImages.data())))) return false;
        if(g.foveationProfile)
        {
            XrSwapchainStateFoveationFB state={XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB};
            state.profile=g.foveationProfile;
            const XrResult result=g.UpdateSwapchainFB(e.swapchain,
                reinterpret_cast<const XrSwapchainStateBaseHeaderFB*>(&state));
            XRLOG("Vulkan fixed foveated rendering swapchain: %s (%d)",
                  XR_SUCCEEDED(result)?"enabled":"failed",static_cast<int>(result));
        }
        e.vulkanImageInitialized.assign(imageCount,0);
#else
        e.images.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        if (XR_FAILED(g.EnumerateSwapchainImages(e.swapchain, imageCount, &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data())))) return false;
        g.multiviewFramebufferValid.assign(imageCount,0);
#endif
    }
#if defined(SRR2_VR_RENDERER_VULKAN)
    g.appliedRenderScale=g.renderScale;
    g.renderScalePending=false;
    XRLOG("Vulkan stereo swapchain ready (%dx%d, 2 layers)",
          g.eyes[0].width,g.eyes[0].height);
    return true;
#else
    glGenFramebuffers(1, &g.framebuffer);
    // Never mutate a multiview framebuffer into a single-layer framebuffer.
    // Adreno can retain layered attachment state internally and intermittently
    // submit an empty second layer after such a transition.
    glGenFramebuffers(1, &g.layerFramebuffer);
    const int depthWidth=std::max(g.eyes[0].width,g.eyes[1].width);
    const int depthHeight=std::max(g.eyes[0].height,g.eyes[1].height);
    // Allocate once. Reallocating a 2064x2208 depth surface for every eye on
    // every frame causes intermittent GLES driver stalls despite low average
    // GPU utilization.
    // The VR projection spans 0.1..1000 game metres. A 16-bit depth buffer
    // does not have enough precision across that range and causes distant
    // coplanar/model surfaces to break into visible stripes and triangles.
    // Quest 3 exposes GLES 3 and supports the 24-bit renderbuffer format,
    // matching the precision expected by the normal Android render path.
    // The legacy GLES2 headers used by this project do not expose the core
    // GLES3 name, despite GL_OES_depth24 using the same enum value.
    const GLenum depthComponent24=0x81A6; // GL_DEPTH_COMPONENT24[_OES]
    glGenTextures(1,&g.depthTexture);
    glBindTexture(GL_TEXTURE_2D,g.depthTexture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,depthComponent24,depthWidth,depthHeight,0,
                 GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,NULL);
    glGenTextures(1,&g.multiviewDepthTexture);
    glBindTexture(GL_TEXTURE_2D_ARRAY,g.multiviewDepthTexture);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    g.TexStorage3D(GL_TEXTURE_2D_ARRAY,1,GL_DEPTH_COMPONENT24,depthWidth,depthHeight,2);
    if(!CreateGtaoResources(depthWidth,depthHeight))
        XRERR("half-resolution GTAO resources unavailable");
    g.appliedRenderScale=g.renderScale;
    g.renderScalePending=false;
    return true;
#endif
}

static void DestroySwapchainsAndRenderTargets()
{
#if !defined(SRR2_VR_RENDERER_VULKAN)
    if(g.framebuffer) glDeleteFramebuffers(1,&g.framebuffer);
    if(g.layerFramebuffer) glDeleteFramebuffers(1,&g.layerFramebuffer);
    if(g.radarFramebuffer) glDeleteFramebuffers(1,&g.radarFramebuffer);
    if(g.radarDepthBuffer) glDeleteRenderbuffers(1,&g.radarDepthBuffer);
    if(g.radarTexture) glDeleteTextures(1,&g.radarTexture);
    if(g.radarDisplayTexture) glDeleteTextures(1,&g.radarDisplayTexture);
    if(g.radarProgram) glDeleteProgram(g.radarProgram);
    if(g.irisBlackProgram) glDeleteProgram(g.irisBlackProgram);
    if(g.hudQuadVbo) glDeleteBuffers(1,&g.hudQuadVbo);
    glDeleteFramebuffers(State::MISSION_HUD_COUNT,g.missionHudFramebuffer);
    glDeleteTextures(State::MISSION_HUD_COUNT,g.missionHudTexture);
    if(g.depthTexture) glDeleteTextures(1,&g.depthTexture);
    glDeleteFramebuffers(2,g.gtaoFramebuffer);
    glDeleteTextures(2,g.gtaoTexture);
    if(g.gtaoProgram) glDeleteProgram(g.gtaoProgram);
    if(g.gtaoBlurProgram) glDeleteProgram(g.gtaoBlurProgram);
    if(g.gtaoCompositeProgram) glDeleteProgram(g.gtaoCompositeProgram);
    if(g.gtaoVbo) glDeleteBuffers(1,&g.gtaoVbo);
    g.framebuffer=0; g.layerFramebuffer=0; g.depthTexture=0;
    g.irisBlackProgram=0;
    std::memset(g.missionHudFramebuffer,0,sizeof(g.missionHudFramebuffer));
    std::memset(g.missionHudTexture,0,sizeof(g.missionHudTexture));
    std::memset(g.gtaoFramebuffer,0,sizeof(g.gtaoFramebuffer));
    std::memset(g.gtaoTexture,0,sizeof(g.gtaoTexture));
    g.gtaoProgram=g.gtaoBlurProgram=g.gtaoCompositeProgram=g.gtaoVbo=0;
    for(unsigned i=0;i<2;++i)
    {
        if(i==0 && g.eyes[i].swapchain) g.DestroySwapchain(g.eyes[i].swapchain);
        g.eyes[i].swapchain=XR_NULL_HANDLE;
        g.eyes[i].images.clear();
    }
    if(g.multiviewDepthTexture)glDeleteTextures(1,&g.multiviewDepthTexture);
    g.multiviewDepthTexture=0;
#else
    // Cached Vulkan framebuffers may still reference the radar image view.
    // Keep the target alive until VulkanContext destroys the cache and device
    // immediately after this function returns.
    for(unsigned i=0;i<2;++i)
    {
        if(i==0 && g.eyes[i].swapchain) g.DestroySwapchain(g.eyes[i].swapchain);
        g.eyes[i].swapchain=XR_NULL_HANDLE;
        g.eyes[i].vulkanImages.clear();
        g.eyes[i].foveationImages.clear();
        g.eyes[i].vulkanImageInitialized.clear();
    }
#endif
}

static void SetKey(SDL_Scancode scancode, bool down)
{
    if (g.keyState[scancode] == down) return;
    g.keyState[scancode] = down;
    SDL_Event e = {};
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.repeat = 0;
    e.key.keysym.scancode = scancode;
    e.key.keysym.sym = SDL_GetKeyFromScancode(scancode);
    SDL_PushEvent(&e);
}

static void SetMouse(Uint8 button, bool down)
{
    if (button >= 6 || g.mouseState[button] == down) return;
    g.mouseState[button] = down;
    SDL_Event e = {};
    e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.button.button = button;
    e.button.clicks = 1;
    SDL_PushEvent(&e);
}

static void FireHaptic(unsigned hand, float amplitude, float durationMs, float frequencyHz)
{
    if (!g.hapticAction || !g.session) return;
    XrHapticVibration vibration = {XR_TYPE_HAPTIC_VIBRATION};
    vibration.duration = static_cast<XrDuration>(durationMs * 1000000.0); // ms -> ns
    vibration.frequency = frequencyHz;
    vibration.amplitude = std::max(0.0f, std::min(1.0f, amplitude));
    XrHapticActionInfo info = {XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = g.hapticAction;
    info.subactionPath = (hand == 0) ? g.leftHand : g.rightHand;
    g.ApplyHapticFeedback(g.session, &info, reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
}


static bool CreateInputActions()
{
    XrActionSetCreateInfo setInfo = {XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(setInfo.actionSetName, "gameplay");
    std::strcpy(setInfo.localizedActionSetName, "Gameplay");
    setInfo.priority = 0;
    if (XR_FAILED(g.CreateActionSet(g.instance, &setInfo, &g.actionSet))) return false;
    g.StringToPath(g.instance, "/user/hand/left", &g.leftHand);
    g.StringToPath(g.instance, "/user/hand/right", &g.rightHand);
    XrPath hands[] = {g.leftHand, g.rightHand};
    auto create = [&](const char* name, const char* localized, XrActionType type,
                      XrAction* action, bool bothHands) -> bool {
        XrActionCreateInfo ai = {XR_TYPE_ACTION_CREATE_INFO};
        std::strncpy(ai.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
        std::strncpy(ai.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        ai.actionType = type;
        if (bothHands) { ai.countSubactionPaths = 2; ai.subactionPaths = hands; }
        return XR_SUCCEEDED(g.CreateAction(g.actionSet, &ai, action));
    };
    if (!create("move_x", "Move horizontal", XR_ACTION_TYPE_FLOAT_INPUT, &g.moveXAction, false) ||
        !create("move_y", "Move vertical", XR_ACTION_TYPE_FLOAT_INPUT, &g.moveYAction, false) ||
        !create("look_x", "Look horizontal", XR_ACTION_TYPE_FLOAT_INPUT, &g.lookXAction, false) ||
        !create("look_y", "Look vertical", XR_ACTION_TYPE_FLOAT_INPUT, &g.lookYAction, false) ||
        !create("select", "Select or jump", XR_ACTION_TYPE_BOOLEAN_INPUT, &g.selectAction, false) ||
        !create("back", "Back or sprint", XR_ACTION_TYPE_BOOLEAN_INPUT, &g.backAction, false) ||
        !create("attack", "Attack or handbrake", XR_ACTION_TYPE_BOOLEAN_INPUT, &g.attackAction, false) ||
        !create("use", "Action or exit vehicle", XR_ACTION_TYPE_BOOLEAN_INPUT, &g.useAction, false) ||
        !create("menu", "Game menu", XR_ACTION_TYPE_BOOLEAN_INPUT, &g.menuAction, false) ||
        !create("left_trigger", "Left trigger", XR_ACTION_TYPE_FLOAT_INPUT, &g.leftTriggerAction, false) ||
        !create("right_trigger", "Right trigger", XR_ACTION_TYPE_FLOAT_INPUT, &g.rightTriggerAction, false) ||
        !create("left_grip", "Left grip", XR_ACTION_TYPE_FLOAT_INPUT, &g.leftGripAction, false) ||
        !create("right_grip", "Right grip", XR_ACTION_TYPE_FLOAT_INPUT, &g.rightGripAction, false) ||
        !create("left_stick_click", "Left stick click", XR_ACTION_TYPE_BOOLEAN_INPUT, &g.leftStickClickAction, false) ||
        !create("right_stick_click", "Right stick click", XR_ACTION_TYPE_BOOLEAN_INPUT, &g.rightStickClickAction, false) ||
        !create("hand_pose", "Tracked hand pose", XR_ACTION_TYPE_POSE_INPUT, &g.handPoseAction, true) ||
        !create("haptic", "Controller vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT, &g.hapticAction, true)) return false;

    auto path = [&](const char* value) { XrPath p = XR_NULL_PATH; g.StringToPath(g.instance, value, &p); return p; };
    XrActionSuggestedBinding bindings[] = {
        {g.moveXAction, path("/user/hand/left/input/thumbstick/x")},
        {g.moveYAction, path("/user/hand/left/input/thumbstick/y")},
        {g.lookXAction, path("/user/hand/right/input/thumbstick/x")},
        {g.lookYAction, path("/user/hand/right/input/thumbstick/y")},
        {g.selectAction, path("/user/hand/right/input/a/click")},
        {g.backAction, path("/user/hand/right/input/b/click")},
        {g.attackAction, path("/user/hand/left/input/x/click")},
        {g.useAction, path("/user/hand/left/input/y/click")},
        {g.menuAction, path("/user/hand/left/input/menu/click")},
        {g.leftTriggerAction, path("/user/hand/left/input/trigger/value")},
        {g.rightTriggerAction, path("/user/hand/right/input/trigger/value")},
        {g.leftGripAction, path("/user/hand/left/input/squeeze/value")},
        {g.rightGripAction, path("/user/hand/right/input/squeeze/value")},
        {g.leftStickClickAction, path("/user/hand/left/input/thumbstick/click")},
        {g.rightStickClickAction, path("/user/hand/right/input/thumbstick/click")},
        {g.handPoseAction, path("/user/hand/left/input/grip/pose")},
        {g.handPoseAction, path("/user/hand/right/input/grip/pose")},
        {g.hapticAction, path("/user/hand/left/output/haptic")},
        {g.hapticAction, path("/user/hand/right/output/haptic")}
    };
    XrInteractionProfileSuggestedBinding suggested = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
    suggested.countSuggestedBindings = sizeof(bindings) / sizeof(bindings[0]);
    suggested.suggestedBindings = bindings;
    if (XR_FAILED(g.SuggestInteractionProfileBindings(g.instance, &suggested))) return false;
    XRLOG("Touch controller action bindings created");
    return true;
}

static void SyncInputActions()
{
    if (!g.actionSet || !g.running) return;
    XrActiveActionSet active = {g.actionSet, XR_NULL_PATH};
    XrActionsSyncInfo sync = {XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1; sync.activeActionSets = &active;
    if (XR_FAILED(g.SyncActions(g.session, &sync))) return;
    auto vec = [&](XrAction action) { XrActionStateGetInfo gi={XR_TYPE_ACTION_STATE_GET_INFO}; gi.action=action; XrActionStateVector2f s={XR_TYPE_ACTION_STATE_VECTOR2F}; g.GetActionStateVector2f(g.session,&gi,&s); return s.isActive?s.currentState:XrVector2f{0,0}; };
    auto boolean = [&](XrAction action) { XrActionStateGetInfo gi={XR_TYPE_ACTION_STATE_GET_INFO}; gi.action=action; XrActionStateBoolean s={XR_TYPE_ACTION_STATE_BOOLEAN}; g.GetActionStateBoolean(g.session,&gi,&s); return s.isActive && s.currentState; };
    auto value = [&](XrAction action) { XrActionStateGetInfo gi={XR_TYPE_ACTION_STATE_GET_INFO}; gi.action=action; XrActionStateFloat s={XR_TYPE_ACTION_STATE_FLOAT}; g.GetActionStateFloat(g.session,&gi,&s); return s.isActive?s.currentState:0.0f; };
    InputManager* input = InputManager::GetInstance();
    UserController* controller = input ? input->GetController(0) : NULL;
    if (!controller) return;
    controller->SetVirtualInputAvailable(true);
    auto set = [&](const char* name, float inputValue) {
        const int index=controller->GetIdByName(name);
        if(index>=0) controller->SetVirtualInputValue(static_cast<unsigned int>(index),inputValue);
    };
    const XrVector2f rawMove={value(g.moveXAction),value(g.moveYAction)};
    const XrVector2f rawLook={value(g.lookXAction),value(g.lookYAction)};
    auto applyDeadzone = [](const XrVector2f& raw) {
        const float deadzone=0.30f;
        const float length=std::sqrt(raw.x*raw.x+raw.y*raw.y);
        if(length<=deadzone) return XrVector2f{0.0f,0.0f};
        const float scaled=(length-deadzone)/(1.0f-deadzone);
        const float factor=(scaled>1.0f?1.0f:scaled)/length;
        return XrVector2f{raw.x*factor,raw.y*factor};
    };
    const XrVector2f move=applyDeadzone(rawMove), look=applyDeadzone(rawLook);
    const float menuHorizontal=std::max(std::fabs(move.x),std::fabs(look.x));
    const float menuVertical=std::max(std::fabs(move.y),std::fabs(look.y));
    const bool menuStickActive=std::max(menuHorizontal,menuVertical)>0.22f;
    if(g.menuAxisLock==0 && menuStickActive)
    {
        // Once a gesture starts, commit it to one axis. A slightly diagonal
        // thumb motion must never generate a value change followed by a row
        // change (or vice versa) on the same deflection.
        g.menuAxisLock=(menuHorizontal>=menuVertical)?1u:2u;
        g.menuAxisNeutralFrames=0;
    }
    else if(!menuStickActive)
    {
        // Keep the lock for a short neutral interval. This is the debounce
        // between consecutive menu gestures and absorbs stick spring-back.
        if(++g.menuAxisNeutralFrames>=6)
        {
            g.menuAxisLock=0;
            g.menuAxisNeutralFrames=0;
        }
    }
    else
    {
        g.menuAxisNeutralFrames=0;
    }
    g.menuHorizontalInputDominant=g.menuAxisLock==1;
    g.menuVerticalInputDominant=g.menuAxisLock==2;
#if defined(RAD_DEBUG)
#endif
    set("LeftStickX",move.x); set("LeftStickY",move.y);
    // In-car VR steering: kill look/turn stick so it cannot yaw the camera
    // (and with it the virtual wheel) while driving.
    {
        Character* drivePlayer = GetCharacterManager()->GetCharacter(0);
        const bool blockLookStick = g.vrModeEnabled && drivePlayer && drivePlayer->IsInCar() &&
                                    g.vehicleControlMode!=2;
        set("RightStickX", blockLookStick ? 0.0f : look.x);
        set("RightStickY", blockLookStick ? 0.0f : look.y);
    }
    const float select=boolean(g.selectAction)?1.0f:0.0f;
    const float back=boolean(g.backAction)?1.0f:0.0f;
    const float attack=boolean(g.attackAction)?1.0f:0.0f;
    const float use=boolean(g.useAction)?1.0f:0.0f;
    const float menu=boolean(g.menuAction)?1.0f:0.0f;
    set("A",select); set("B",back); set("X",attack);
    // A is confirm in front-end screens and also acts as DoAction in 3D
    // interaction screens; physical Y remains a dedicated action button.
    set("Y",(select>0.0f||use>0.0f)?1.0f:0.0f); set("Start",menu);
    // Honor Roller yoke: push forward = gas, pull back = brake
    {
        Character* drivePlayer = GetCharacterManager()->GetCharacter(0);
        Vehicle* v = (drivePlayer && drivePlayer->IsInCar()) ? drivePlayer->GetTargetVehicle() : NULL;
        const bool yokeActive = g.vrModeEnabled && g.vehicleControlMode==1 && IsHonorRoller(v)
                                && (g.wheelGrabbed[0] || g.wheelGrabbed[1]);
        if (yokeActive)
        {
            const float gas   = g.yokeThrottle > 0.0f ? g.yokeThrottle : 0.0f;
            const float brake = g.yokeThrottle < 0.0f ? -g.yokeThrottle : 0.0f;
            set("RightTrigger", gas);
            set("LeftTrigger",  brake);
        }
        else
        {
            set("LeftTrigger",value(g.leftTriggerAction));
            set("RightTrigger",value(g.rightTriggerAction));
        }
    }
    g.gripValue[0]=value(g.leftGripAction);
    g.gripValue[1]=value(g.rightGripAction);
    set("Black",g.gripValue[0]>0.55f?1.0f:0.0f);
    // Keep the right grip on its own virtual button. Folding it into X made
    // one squeeze trigger both a face-button action and the grip action.
    // In wheel mode squeeze belongs exclusively to grabbing the rim. This
    // prevents the right hand from applying the handbrake while steering.
    set("White",(!(g.vrModeEnabled && g.vehicleControlMode==1) &&
                  g.gripValue[1]>0.55f)?1.0f:0.0f);
    g.stickClick[0]=boolean(g.leftStickClickAction);
    g.stickClick[1]=boolean(g.rightStickClickAction);
    // Round VR wheel: a hand resting at the hub honks the horn (mapped to
    // LeftThumb, this game's actual horn input). g.stickClick[0] itself is
    // read directly by the wheel-adjust-mode hold logic elsewhere, so
    // OR-ing the honk into the LeftThumb button output here doesn't affect
    // that detection.
    const bool wheelHonkActive = g.vrModeEnabled && g.vehicleControlMode==1 && g.wheelHonk;
    set("LeftThumb",(g.stickClick[0] || wheelHonkActive)?1.0f:0.0f);
    set("RightThumb",g.stickClick[1]?1.0f:0.0f);
}
}

namespace SharOpenXR
{
void SetVrModeEnabled(bool enabled)
{
    if(g.vrModeEnabled==enabled) return;
    g.vrModeEnabled=enabled;
    XRLOG("gameplay mode: %s",enabled?"VR":"Original");
    InputManager* input=InputManager::GetInstance();
    UserController* controller=input?input->GetController(0):NULL;
    if(controller) controller->LoadControllerMappings();
    char* path=SDL_GetPrefPath("c4rlox","simpsons");
    if(path)
    {
        std::string filename(path);
        filename+="vrmode.cfg";
        if(FILE* file=std::fopen(filename.c_str(),"wb"))
        {
            std::fputc(enabled?'1':'0',file);
            std::fclose(file);
        }
        SDL_free(path);
    }
}
bool IsVrModeEnabled(){ return g.vrModeEnabled; }
bool IsSpatialHudEnabled()
{
    return g.vrModeEnabled;
}
void SetDeveloperMenusEnabled(bool enabled){ g.developerMenusEnabled=enabled; SaveVrSettings(); }
bool IsDeveloperMenusEnabled(){ return g.developerMenusEnabled; }
void SetSeatedMode(bool enabled){ g.seatedMode=enabled; SaveVrSettings(); }
bool IsSeatedMode(){ return g.seatedMode; }
void SetSnapTurnEnabled(bool enabled){ g.snapTurnEnabled=enabled; SaveVrSettings(); }
bool IsSnapTurnEnabled(){ return g.snapTurnEnabled; }
void SetSmoothTurnSpeed(float value){ g.smoothTurnSpeed=value; SaveVrSettings(); }
float GetSmoothTurnSpeed(){ return g.smoothTurnSpeed; }
void SetSnapTurnAngle(float value){ g.snapTurnAngle=value; SaveVrSettings(); }
float GetSnapTurnAngle(){ return g.snapTurnAngle; }
void SetCsmEnabled(bool enabled){ g.csmEnabled=enabled; SaveVrSettings(); }
bool IsCsmEnabled(){ return g.csmEnabled; }
void SetEnhancedMaterialsEnabled(bool enabled){ g.enhancedMaterialsEnabled=enabled; SaveVrSettings(); }
bool IsEnhancedMaterialsEnabled(){ return g.enhancedMaterialsEnabled; }
void SetCustomMaterialsEnabled(bool enabled){ g.customMaterialsEnabled=enabled; SaveVrSettings(); }
bool AreCustomMaterialsEnabled(){ return g.customMaterialsEnabled; }
void SetEnhancedMaterialModel(int model)
{
    g.enhancedMaterialModel=std::max(0,std::min(3,model));
    g.enhancedMaterialsEnabled=g.enhancedMaterialModel!=0;
    SaveVrSettings();
    XRLOG("enhanced material model: %s",
          g.enhancedMaterialModel==3?"NPR Toon":
          g.enhancedMaterialModel==2?"PBR":g.enhancedMaterialModel==1?"Phong":"off");
}
int GetEnhancedMaterialModel()
{
    return g.enhancedMaterialsEnabled?std::max(1,g.enhancedMaterialModel):0;
}
void SetGtaoEnabled(bool enabled)
{
    // GTAO currently samples a single GL_TEXTURE_2D depth target and is not
    // stereo-correct. Keep the saved option off for every standalone VR path.
    g.gtaoEnabled=enabled && !g.vrModeEnabled;
    SaveVrSettings();
}
bool IsGtaoEnabled(){ return g.gtaoEnabled && !g.vrModeEnabled; }
void SetVehicleLightMode(int mode)
{
    g.vehicleLightMode=std::max(0,std::min(2,mode));
    SaveVrSettings();
}
int GetVehicleLightMode(){ return g.vehicleLightMode; }
void SetReflectionMode(int mode)
{
    g.reflectionMode=std::max(0,std::min(2,mode));
    SaveVrSettings();
}
int GetReflectionMode(){ return g.reflectionMode; }
void SetPbrDebugMode(int mode)
{
    g.pbrDebugMode=std::max(0,std::min(4,mode));
    SaveVrSettings();
}
int GetPbrDebugMode(){ return g.pbrDebugMode; }
void SetVrSteeringWheelEnabled(bool enabled)
{
    SetVehicleControlMode(enabled?1:0);
}

// Match SuperCam's authored world range. A 1000-unit VR far plane clips the
// level-one WorldSphere: its animated cloud joints extend beyond 1700 units,
// while the original game renders the scene with SUPERCAM_FAR (8000).
static const float VR_WORLD_FAR_PLANE = 8000.0f;
bool IsVrSteeringWheelEnabled(){ return g.vehicleControlMode==1; }
void SetVehicleControlMode(int mode)
{
    g.vehicleControlMode=std::max(0,std::min(2,mode));
    g.wheelGrabbed[0]=g.wheelGrabbed[1]=false;
    g.wheelAngle=0.0f;
    g.wheelTrim=0.0f;
    SaveVrSettings();
}
int GetVehicleControlMode(){ return g.vehicleControlMode; }
void SetVehicleComfortEnabled(bool enabled)
{
    g.vehicleComfortEnabled=enabled;
    SaveVrSettings();
}
bool IsVehicleComfortEnabled(){ return g.vehicleComfortEnabled; }
bool IsThirdPersonVehicleMode(){ return g.vehicleControlMode==2; }
bool GetVrSteeringWheelValue(float* value)
{
    if (!value || !g.vrModeEnabled || g.vehicleControlMode!=1) return false;

    Character* p = GetCharacterManager()->GetCharacter(0);
    Vehicle* v = (p && p->IsInCar()) ? p->GetTargetVehicle() : NULL;
    // Trim only ever gets learned by the normal-wheel path (never touched
    // while driving the Honor Roller), but guard here too in case trim is
    // still nonzero from a normal car driven earlier this session.
    const float effectiveAngle = IsHonorRoller(v) ? g.wheelAngle : (g.wheelAngle - g.wheelTrim);

    float t = effectiveAngle / kVrWheelMaxAngle; // [-1, 1]
    const float sign = (t >= 0.0f) ? 1.0f : -1.0f;
    // Near-linear so vehicles stay easy to turn; not a heavy ease-in.
    t = sign * std::pow(std::fabs(t), kSteerOutputCurve);
    if (std::fabs(t) < kSteerOutputDeadzone)
        t = 0.0f;
    else
    {
        const float mag = (std::fabs(t) - kSteerOutputDeadzone) / (1.0f - kSteerOutputDeadzone);
        t = sign * std::max(0.0f, std::min(1.0f, mag));
    }
    // The Honor Roller's physical steering response is wired opposite to
    // every other vehicle's convention. Invert only the value sent to the
    // vehicle here; wheelAngle (and therefore the yoke mesh / hand
    // attachment) is left untouched so the T-handle keeps following the
    // player's hands correctly.
    if (IsHonorRoller(v))
        t = -t;
    *value = std::max(-1.0f, std::min(1.0f, t));
    return true;
}
void SetRenderScale(float scale)
{
    scale=std::max(0.10f,std::min(2.0f,scale));
    if(std::fabs(scale-g.renderScale)<0.001f) return;
    g.renderScale=scale;
    g.renderScalePending=std::fabs(g.renderScale-g.appliedRenderScale)>=0.001f;
    SaveVrSettings();
    XRLOG("render scale %.0f%% queued for next XR frame",g.renderScale*100.0f);
}
float GetRenderScale(){ return g.renderScale; }
void SetRefreshRate(float hz)
{
    if(hz!=72.0f && hz!=90.0f && hz!=120.0f) return;
    if(std::fabs(hz-g.refreshRate)<0.1f) return;
    if(g.session && g.RequestDisplayRefreshRateFB)
    {
        const XrResult result=g.RequestDisplayRefreshRateFB(g.session,hz);
        if(XR_FAILED(result))
        {
            XRERR("%.0f Hz refresh rate rejected (%d)",hz,static_cast<int>(result));
            return;
        }
    }
    g.refreshRate=hz;
    SaveVrSettings();
    XRLOG("%.0f Hz refresh rate applied",hz);
}
float GetRefreshRate(){ return g.refreshRate; }
bool IsHorizontalMenuInputDominant(){ return g.menuHorizontalInputDominant; }
bool IsVerticalMenuInputDominant(){ return g.menuVerticalInputDominant; }
bool IsRightEyeRendering(){ return g.activeEye==2; }
void SetVrBaseHeading(const rmt::Vector& heading)
{
    g.vrBaseHeading=heading;
    g.vrBaseHeading.y=0.0f;
    g.vrBaseHeadingValid=g.vrBaseHeading.NormalizeSafe()>0.0001f;
}
bool RecenterVrPose()
{
    if(!(g.viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) ||
       !(g.viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT))
    {
        XRERR("VR pose recenter requested without a valid head pose");
        return false;
    }

    // The game camera supplies the vehicle/character base transform.  Make
    // the current physical head pose the zero local offset so entering a car
    // cannot retain the player's pre-entry standing height or world yaw.
    g.origin.orientation=YawOnly(g.eyes[0].view.pose.orientation);
    g.origin.position.x=(g.eyes[0].view.pose.position.x+g.eyes[1].view.pose.position.x)*0.5f;
    g.origin.position.y=(g.eyes[0].view.pose.position.y+g.eyes[1].view.pose.position.y)*0.5f;
    g.origin.position.z=(g.eyes[0].view.pose.position.z+g.eyes[1].view.pose.position.z)*0.5f;
    g.originValid=true;
    // Re-place any visible world-locked 2D panel in front of the newly
    // recentered horizontal gaze on its next render.
    g.moviePlaneAnchorValid=false;
    g.frontendPlaneAnchorValid=false;
    XRLOG("VR pose recentered at (%.3f, %.3f, %.3f)",
          g.origin.position.x,g.origin.position.y,g.origin.position.z);
    return true;
}
bool GetPhysicalHeadHeight(float* heightMetres)
{
    if(!heightMetres || !g.usingStageSpace ||
       !(g.viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT))
    {
        return false;
    }
    // Use the height captured at the current origin, not the live eye Y.
    // Live vertical displacement is already applied once by RelativePose;
    // adding it to the base as well would double every crouch movement.
    float height = g.seatedMode ? 1.70f : g.origin.position.y;

    // Kid characters (Bart / Lisa) should sit closer to other kids in the world.
    // Scale the reported eye height so first-person VR matches their model size.
    // ~0.78 keeps adult proportions while dropping the viewpoint ~35–40 cm.
    Character* player = GetCharacterManager() ? GetCharacterManager()->GetCharacter(0) : NULL;
    if (player)
    {
        const tUID uid = player->GetUID();
        if (uid == tEntity::MakeUID("bart") || uid == tEntity::MakeUID("lisa"))
        {
            height *= 0.78f;          // tunable: 0.75–0.82 feel good
            // Alternative fixed offset (uncomment if you prefer absolute):
            // height = std::max(0.95f, height - 0.40f);
        }
    }

    *heightMetres = height;
    return *heightMetres > 0.25f && *heightMetres < 2.75f;
}
bool ConsumeRoomscaleMovement(rmt::Vector* worldDelta)
{
    if(!worldDelta) return false;
    worldDelta->Set(0.0f,0.0f,0.0f);
    if(!g.vrModeEnabled || !g.originValid ||
       !(g.viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT))
        return false;

    XrPosef head=g.eyes[0].view.pose;
    head.position.x=(g.eyes[0].view.pose.position.x+
                     g.eyes[1].view.pose.position.x)*0.5f;
    head.position.z=(g.eyes[0].view.pose.position.z+
                     g.eyes[1].view.pose.position.z)*0.5f;

    Character* player=GetCharacterManager() ?
                      GetCharacterManager()->GetCharacter(0) : NULL;
    if(player && player->IsInCar())
    {
        // The vehicle camera needs the origin captured on entry to remain
        // fixed. Advancing origin.x/z here cancels the HMD translation in
        // GetEyeCamera, leaving only rotational tracking in the car.
        g.roomscaleMovementSuspended=true;
        return false;
    }

    if(g.roomscaleMovementSuspended)
    {
        // Discard the lean accumulated inside the vehicle. Otherwise the
        // first on-foot frame would turn it into character locomotion.
        g.origin.position.x=head.position.x;
        g.origin.position.z=head.position.z;
        g.roomscaleMovementSuspended=false;
        return false;
    }

    const XrPosef relative=RelativePose(g.origin,head);
    rmt::Vector local(relative.position.x,0.0f,-relative.position.z);

    // Consume horizontal tracking motion as on-foot locomotion.
    g.origin.position.x=head.position.x;
    g.origin.position.z=head.position.z;

    // A real room-scale step is small. Ignore guardian recenter/tracking jumps.
    if(local.MagnitudeSqr()>0.25f) return false;
    if(local.MagnitudeSqr()<0.00000001f) return false;

    if(g.vrBaseHeadingValid)
    {
        rmt::Matrix base;
        base.Identity();
        base.FillHeading(g.vrBaseHeading,rmt::Vector(0.0f,1.0f,0.0f));
        local.Rotate(base);
    }
    *worldDelta=local;
    return true;
}
bool GetHeadForward(rmt::Vector* forward)
{
    if(!forward || !g.originValid) return false;
    const XrPosef relative=RelativePose(g.origin,g.eyes[0].view.pose);
    const XrVector3f xrForward=Rotate(relative.orientation,XrVector3f{0.0f,0.0f,-1.0f});
    forward->Set(xrForward.x,0.0f,-xrForward.z);
    if(g.vrBaseHeadingValid)
    {
        rmt::Matrix base;
        base.Identity();
        base.FillHeading(g.vrBaseHeading,rmt::Vector(0.0f,1.0f,0.0f));
        forward->Transform(base);
    }
    return forward->MagnitudeSqr()>0.0001f && forward->NormalizeSafe();
}
bool Initialize()
{
    if (g.instance) return true;
    g.smoothTurnSpeed=120.0f;
    g.snapTurnAngle=45.0f;
    g.csmEnabled=true;
    g.enhancedMaterialsEnabled=true;
    g.enhancedMaterialModel=1;
    g.gtaoEnabled=true;
    g.renderScale=1.0f;
    g.appliedRenderScale=1.0f;
    g.refreshRate=72.0f;
    g.vehicleControlMode=0;
    g.vehicleComfortEnabled=true;
    g.vehicleLightMode=1;
    g.reflectionMode=0;
    g.pbrDebugMode=0;
    g.customMaterialsEnabled=true;
    g.spatialHudEnabled=true;
    g.developerMenusEnabled=false;
    char* preferencePath=SDL_GetPrefPath("c4rlox","simpsons");
    if(preferencePath)
    {
        std::string filename(preferencePath);
        filename+="vrmode.cfg";
        if(FILE* file=std::fopen(filename.c_str(),"rb"))
        {
            g.vrModeEnabled=std::fgetc(file)=='1';
            std::fclose(file);
        }
        SDL_free(preferencePath);
    }
    preferencePath=SDL_GetPrefPath("c4rlox","simpsons");
    if(preferencePath)
    {
        std::string filename(preferencePath); filename+="vrsettings.cfg";
        if(FILE* file=std::fopen(filename.c_str(),"rb"))
        {
            int seated=0,snap=0,csm=1,enhancedMaterials=1,gtao=1,vrSteeringWheel=0,vehicleLights=1,spatialHud=1,developerMenus=0;
            float speed=120.0f,angle=45.0f,renderScale=1.0f,refreshRate=72.0f;
            if(std::fscanf(file,"seated=%d\nsnap=%d\nsmooth=%f\nangle=%f",
                           &seated,&snap,&speed,&angle)==4)
            {
                g.seatedMode=seated!=0; g.snapTurnEnabled=snap!=0;
                g.smoothTurnSpeed=speed; g.snapTurnAngle=angle;
                if(std::fscanf(file,"\ncsm=%d",&csm)==1)
                {
                    g.csmEnabled=csm!=0;
                    if(std::fscanf(file,"\nenhancedMaterials=%d",&enhancedMaterials)==1)
                    {
                        g.enhancedMaterialsEnabled=enhancedMaterials!=0;
                        if(std::fscanf(file,"\ngtao=%d",&gtao)==1)
                        {
                            g.gtaoEnabled=gtao!=0;
                            if(std::fscanf(file,"\nrenderScale=%f",&renderScale)==1)
                            {
                                g.renderScale=std::max(0.10f,std::min(2.0f,renderScale));
                                if(std::fscanf(file,"\nrefreshRate=%f",&refreshRate)==1 &&
                                   (refreshRate==72.0f || refreshRate==90.0f || refreshRate==120.0f))
                                    g.refreshRate=refreshRate;
                                if(std::fscanf(file,"\nvrSteeringWheel=%d",&vrSteeringWheel)==1)
                                    g.vehicleControlMode=std::max(0,std::min(2,vrSteeringWheel));
                                if(std::fscanf(file,"\nvehicleLights=%d",&vehicleLights)==1)
                                    g.vehicleLightMode=std::max(0,std::min(2,vehicleLights));
                                if(std::fscanf(file,"\nspatialHud=%d",&spatialHud)==1)
                                    g.spatialHudEnabled=spatialHud!=0;
                                if(std::fscanf(file,"\ndeveloperMenus=%d",&developerMenus)==1)
                                    g.developerMenusEnabled=developerMenus!=0;
                                int materialModel=enhancedMaterials!=0?1:0;
                                if(std::fscanf(file,"\nmaterialModel=%d",&materialModel)==1)
                                    g.enhancedMaterialModel=std::max(0,std::min(3,materialModel));
                                else g.enhancedMaterialModel=enhancedMaterials!=0?1:0;
                                g.enhancedMaterialsEnabled=g.enhancedMaterialModel!=0;
                                if(std::fscanf(file,"\nreflectionMode=%d",&materialModel)==1)
                                    g.reflectionMode=std::max(0,std::min(2,materialModel));
                                if(std::fscanf(file,"\npbrDebugMode=%d",&materialModel)==1)
                                    g.pbrDebugMode=std::max(0,std::min(4,materialModel));
                                int customMaterials=1;
                                if(std::fscanf(file,"\ncustomMaterials=%d",&customMaterials)==1)
                                    g.customMaterialsEnabled=customMaterials!=0;
                                int vehicleComfort=1;
                                if(std::fscanf(file,"\nvehicleComfort=%d",&vehicleComfort)==1)
                                    g.vehicleComfortEnabled=vehicleComfort!=0;
                            }
                        }
                    }
                }
            }
            std::fclose(file);
        }
        SDL_free(preferencePath);
    }
    XRLOG("saved gameplay mode: %s",g.vrModeEnabled?"VR":"Original");
    g.loader=dlopen("libopenxr_loader.so", RTLD_NOW|RTLD_LOCAL);
    if (!g.loader) { XRERR("loader unavailable: %s", dlerror()); return false; }
    g.getProc=reinterpret_cast<PFN_xrGetInstanceProcAddr>(dlsym(g.loader,"xrGetInstanceProcAddr"));
    if (!g.getProc) { XRERR("xrGetInstanceProcAddr unavailable"); return false; }
    PFN_xrInitializeLoaderKHR initLoader=NULL;
    g.getProc(XR_NULL_HANDLE,"xrInitializeLoaderKHR",reinterpret_cast<PFN_xrVoidFunction*>(&initLoader));
    if (initLoader)
    {
        XrLoaderInitInfoAndroidKHR li={XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        JNIEnv* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jobject activity=static_cast<jobject>(SDL_AndroidGetActivity());
        JavaVM* vm=NULL;
        if (!env || !activity || env->GetJavaVM(&vm)!=JNI_OK) return false;
        li.applicationVM=vm;
        li.applicationContext=activity;
        if (XR_FAILED(initLoader(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&li)))) return false;
    }
    const char* extensions[]={XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                              XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
#if defined(SRR2_VR_RENDERER_VULKAN)
                              "XR_KHR_vulkan_enable2",
                              XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME,
                              XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME,
                              XR_FB_FOVEATION_EXTENSION_NAME,
                              XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME,
                              XR_FB_FOVEATION_VULKAN_EXTENSION_NAME,
#endif
                              XR_FB_COLOR_SPACE_EXTENSION_NAME,
                              XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME};
    XrInstanceCreateInfoAndroidKHR androidInfo={XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    JNIEnv* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    JavaVM* vm=NULL; env->GetJavaVM(&vm); androidInfo.applicationVM=vm;
    androidInfo.applicationActivity=static_cast<jobject>(SDL_AndroidGetActivity());
    XrInstanceCreateInfo ci={XR_TYPE_INSTANCE_CREATE_INFO}; ci.next=&androidInfo;
    std::strncpy(ci.applicationInfo.applicationName,"The Simpsons Hit & Run VR",XR_MAX_APPLICATION_NAME_SIZE-1);
    ci.applicationInfo.applicationVersion=1;
    std::strncpy(ci.applicationInfo.engineName,"Pure3D",XR_MAX_ENGINE_NAME_SIZE-1);
    ci.applicationInfo.engineVersion=1; ci.applicationInfo.apiVersion=XR_CURRENT_API_VERSION;
    ci.enabledExtensionCount=sizeof(extensions)/sizeof(extensions[0]);
    ci.enabledExtensionNames=extensions;
    PFN_xrCreateInstance createInstance=NULL;
    g.getProc(XR_NULL_HANDLE,"xrCreateInstance",reinterpret_cast<PFN_xrVoidFunction*>(&createInstance));
    if (!createInstance || XR_FAILED(createInstance(&ci,&g.instance))) { XRERR("xrCreateInstance failed"); return false; }
    LOAD_XR(DestroyInstance); LOAD_XR(GetSystem); LOAD_XR(GetOpenGLESGraphicsRequirementsKHR);
    LOAD_XR(CreateSession); LOAD_XR(DestroySession); LOAD_XR(CreateReferenceSpace); LOAD_XR(DestroySpace);
    LOAD_XR(EnumerateViewConfigurationViews); LOAD_XR(EnumerateSwapchainFormats); LOAD_XR(CreateSwapchain);
    LOAD_XR(DestroySwapchain); LOAD_XR(EnumerateSwapchainImages); LOAD_XR(PollEvent); LOAD_XR(BeginSession);
    LOAD_XR(EndSession); LOAD_XR(WaitFrame); LOAD_XR(BeginFrame); LOAD_XR(LocateViews);
    LOAD_XR(AcquireSwapchainImage); LOAD_XR(WaitSwapchainImage); LOAD_XR(ReleaseSwapchainImage); LOAD_XR(EndFrame);
    LOAD_XR(StringToPath); LOAD_XR(CreateActionSet); LOAD_XR(DestroyActionSet); LOAD_XR(CreateAction);
    LOAD_XR(CreateActionSpace); LOAD_XR(LocateSpace);
    LOAD_XR(SuggestInteractionProfileBindings); LOAD_XR(AttachSessionActionSets); LOAD_XR(SyncActions);
    LOAD_XR(GetActionStateBoolean); LOAD_XR(GetActionStateFloat); LOAD_XR(GetActionStateVector2f);
    LOAD_XR(ApplyHapticFeedback);
    if (!CreateInputActions()) { XRERR("controller action creation failed"); return false; }
    XrSystemGetInfo si={XR_TYPE_SYSTEM_GET_INFO}; si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (XR_FAILED(g.GetSystem(g.instance,&si,&g.system))) { XRERR("no HMD system"); return false; }
#if defined(SRR2_VR_RENDERER_VULKAN)
    // During migration GLES still owns the visible swapchain. The experimental
    // build also creates the runtime-selected Vulkan device here, allowing the
    // Quest Vulkan foundation to be validated before PDDI switches over.
    if(!gVulkanContext.Initialize(g.instance,g.system,g.getProc))
    {
        XRERR("experimental Vulkan bootstrap failed");
        return false;
    }
#endif
#if defined(SRR2_VR_RENDERER_VULKAN)
    XrGraphicsBindingVulkan2KHR binding={XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance=gVulkanContext.GetInstance();
    binding.physicalDevice=gVulkanContext.GetPhysicalDevice();
    binding.device=gVulkanContext.GetDevice();
    binding.queueFamilyIndex=gVulkanContext.GetQueueFamilyIndex();
    binding.queueIndex=0;
#else
    XrGraphicsRequirementsOpenGLESKHR req={XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    if (XR_FAILED(g.GetOpenGLESGraphicsRequirementsKHR(g.instance,g.system,&req))) return false;
    XrGraphicsBindingOpenGLESAndroidKHR binding={XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    binding.display=eglGetCurrentDisplay(); binding.context=eglGetCurrentContext();
    binding.config=0;
    EGLint configId=0; eglQueryContext(binding.display,binding.context,EGL_CONFIG_ID,&configId);
    EGLint attrs[]={EGL_CONFIG_ID,configId,EGL_NONE}; EGLint found=0;
    eglChooseConfig(binding.display,attrs,&binding.config,1,&found);
#endif
    XrSessionCreateInfo sci={XR_TYPE_SESSION_CREATE_INFO}; sci.next=&binding; sci.systemId=g.system;
#if defined(SRR2_VR_RENDERER_VULKAN)
    if(XR_FAILED(g.CreateSession(g.instance,&sci,&g.session)))
#else
    if(!found || XR_FAILED(g.CreateSession(g.instance,&sci,&g.session)))
#endif
    { XRERR("xrCreateSession failed"); return false; }
#if defined(SRR2_VR_RENDERER_VULKAN)
    g.getProc(g.instance,"xrCreateFoveationProfileFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&g.CreateFoveationProfileFB));
    g.getProc(g.instance,"xrDestroyFoveationProfileFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&g.DestroyFoveationProfileFB));
    g.getProc(g.instance,"xrUpdateSwapchainFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&g.UpdateSwapchainFB));
    if(g.CreateFoveationProfileFB && g.UpdateSwapchainFB)
    {
        XrFoveationLevelProfileCreateInfoFB level={
            XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB};
        level.level=XR_FOVEATION_LEVEL_HIGH_FB;
        level.dynamic=XR_FOVEATION_DYNAMIC_DISABLED_FB;
        XrFoveationProfileCreateInfoFB profile={XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB};
        profile.next=&level;
        const XrResult result=g.CreateFoveationProfileFB(g.session,&profile,
                                                          &g.foveationProfile);
        XRLOG("Vulkan fixed foveated rendering profile: %s (%d)",
              XR_SUCCEEDED(result)?"high":"failed",static_cast<int>(result));
    }
#endif
    g.getProc(g.instance,"xrRequestDisplayRefreshRateFB",reinterpret_cast<PFN_xrVoidFunction*>(&g.RequestDisplayRefreshRateFB));
    if(g.RequestDisplayRefreshRateFB)
    {
        const XrResult refreshResult=g.RequestDisplayRefreshRateFB(g.session,g.refreshRate);
        XRLOG("%.0f Hz refresh rate: %s (%d)",g.refreshRate,XR_SUCCEEDED(refreshResult)?"requested":"rejected",static_cast<int>(refreshResult));
    }
    else
    {
        XRERR("display refresh rate function unavailable");
    }
    g.getProc(g.instance,"xrSetColorSpaceFB",reinterpret_cast<PFN_xrVoidFunction*>(&g.SetColorSpaceFB));
    if(g.SetColorSpaceFB)
    {
        XrResult colorResult=g.SetColorSpaceFB(g.session,XR_COLOR_SPACE_REC709_FB);
        XRLOG("Rec.709 color space: %s",XR_SUCCEEDED(colorResult)?"enabled":"rejected");
    }
    XrSessionActionSetsAttachInfo attach={XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets=1; attach.actionSets=&g.actionSet;
    if (XR_FAILED(g.AttachSessionActionSets(g.session,&attach))) { XRERR("controller action attach failed"); return false; }
    for(unsigned hand=0; hand<2; ++hand)
    {
        XrActionSpaceCreateInfo actionSpace={XR_TYPE_ACTION_SPACE_CREATE_INFO};
        actionSpace.action=g.handPoseAction;
        actionSpace.subactionPath=hand==0?g.leftHand:g.rightHand;
        actionSpace.poseInActionSpace.orientation.w=1.0f;
        if(XR_FAILED(g.CreateActionSpace(g.session,&actionSpace,&g.handSpaces[hand])))
        {
            XRERR("failed to create %s hand action space",hand==0?"left":"right");
            return false;
        }
    }
    XrReferenceSpaceCreateInfo rs={XR_TYPE_REFERENCE_SPACE_CREATE_INFO}; rs.poseInReferenceSpace.orientation.w=1;
    rs.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_STAGE;
    if (XR_FAILED(g.CreateReferenceSpace(g.session,&rs,&g.space))) {
        rs.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
        if (XR_FAILED(g.CreateReferenceSpace(g.session,&rs,&g.space))) return false;
        g.usingStageSpace=false;
        XRLOG("using LOCAL reference space");
    } else {
        g.usingStageSpace=true;
        XRLOG("using STAGE reference space");
    }
#if defined(SRR2_VR_RENDERER_VULKAN)
    if(!CreateSwapchains()) { XRERR("Vulkan swapchain creation failed"); return false; }
    g.multiviewAvailable=false;
    XRLOG("initialized: Vulkan compositor smoke-test path");
    return true;
#else
    g.TexStorage3D=reinterpret_cast<PFNGLTEXSTORAGE3DPROC>(eglGetProcAddress("glTexStorage3D"));
    g.FramebufferTextureLayer=reinterpret_cast<PFNGLFRAMEBUFFERTEXTURELAYERPROC>(eglGetProcAddress("glFramebufferTextureLayer"));
    g.GetStringi=reinterpret_cast<PFNGLGETSTRINGIPROC>(eglGetProcAddress("glGetStringi"));
    if(!g.TexStorage3D||!g.FramebufferTextureLayer){XRERR("GLES 3 texture-array entry points unavailable");return false;}
    if (!CreateSwapchains()) { XRERR("swapchain creation failed"); return false; }
    g.FramebufferTextureMultiviewOVR=reinterpret_cast<PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC>(
        eglGetProcAddress("glFramebufferTextureMultiviewOVR"));
    bool hasMultiview2=false;GLint extensionCount=0;glGetIntegerv(GL_NUM_EXTENSIONS,&extensionCount);
    for(GLint i=0;g.GetStringi&&i<extensionCount;++i){const char* extension=reinterpret_cast<const char*>(g.GetStringi(GL_EXTENSIONS,i));if(extension&&!std::strcmp(extension,"GL_OVR_multiview2")){hasMultiview2=true;break;}}
    g.multiviewAvailable=g.FramebufferTextureMultiviewOVR && hasMultiview2;
    XRLOG("GLES %s; GL_OVR_multiview2 %s",glGetString(GL_VERSION),
          g.multiviewAvailable?"enabled":"unavailable, using dual pass");
    XRLOG("Pure3D multiview programs %s",
          pglAreMultiviewProgramsReady()?"ready":"incomplete, forcing dual pass");
    XRLOG("initialized: GLES context, %dx%d + %dx%d",g.eyes[0].width,g.eyes[0].height,g.eyes[1].width,g.eyes[1].height);
    return true;
#endif
}

void Shutdown()
{
    DestroySwapchainsAndRenderTargets();
    if(g.foveationProfile && g.DestroyFoveationProfileFB)
        g.DestroyFoveationProfileFB(g.foveationProfile);
    for(unsigned i=0;i<2;++i) if(g.handSpaces[i]) g.DestroySpace(g.handSpaces[i]);
    if(g.space) g.DestroySpace(g.space); if(g.session) g.DestroySession(g.session);
    if(g.actionSet) g.DestroyActionSet(g.actionSet);
#if defined(SRR2_VR_RENDERER_VULKAN)
    gVulkanContext.Shutdown();
#endif
    if(g.instance) g.DestroyInstance(g.instance); if(g.loader) dlclose(g.loader);
    g = State();
}

void PollEvents()
{
    if(!g.instance) return; XrEventDataBuffer ev={XR_TYPE_EVENT_DATA_BUFFER};
    while(g.PollEvent(g.instance,&ev)==XR_SUCCESS) {
        if(ev.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            XrEventDataSessionStateChanged* s=reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
            g.sessionState=s->state; XRLOG("session state %d",(int)s->state);
            if(s->state==XR_SESSION_STATE_READY) { XrSessionBeginInfo bi={XR_TYPE_SESSION_BEGIN_INFO}; bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; if(XR_SUCCEEDED(g.BeginSession(g.session,&bi))) g.running=true; }
            else if(s->state==XR_SESSION_STATE_STOPPING) { g.EndSession(g.session); g.running=false; }
        }
        else if(ev.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            const XrEventDataReferenceSpaceChangePending* change=
                reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&ev);
            // Quest owns the long-press Meta-button gesture. It reports the
            // resulting native recenter here rather than exposing that button
            // as an application input action.
            g.systemRecenterPending=true;
            g.systemRecenterTime=change->changeTime;
            XRLOG("system recenter pending: space=%d time=%lld",
                  static_cast<int>(change->referenceSpaceType),
                  static_cast<long long>(change->changeTime));
        }
        ev={XR_TYPE_EVENT_DATA_BUFFER};
    }
}

bool BeginFrame()
{
    ++g.hudFrameSerial;
    if(g.hudFrameSerial==0) ++g.hudFrameSerial;
    g.perfFrameStart=SDL_GetPerformanceCounter();
    g.perfDraws=g.perfIndexedDraws=g.perfVertices=g.perfTriangles=g.perfMaterials=0;
    g.perfUploadCalls=g.perfUploadBytes=0;
    g.perfDrawCpu=g.perfMaterialCpu=g.perfUploadCpu=0.0;
    for(unsigned i=0;i<22;++i) g.perfSections[i]=0.0;
    PollEvents(); if(!g.running) return false;
    if(g.renderScalePending)
    {
        const float requestedScale=g.renderScale;
        const float fallbackScale=g.appliedRenderScale;
        // BeginFrame is reached only after the preceding eye images have been
        // released and xrEndFrame has consumed them, so this is the safe point
        // to replace immutable OpenXR swapchains without restarting the app.
#if defined(SRR2_VR_RENDERER_VULKAN)
        vkQueueWaitIdle(gVulkanContext.GetQueue());
#else
        glFinish();
#endif
        DestroySwapchainsAndRenderTargets();
        if(!CreateSwapchains())
        {
            XRERR("render scale %.0f%% rejected; restoring %.0f%%",
                  requestedScale*100.0f,fallbackScale*100.0f);
            DestroySwapchainsAndRenderTargets();
            g.renderScale=fallbackScale;
            if(!CreateSwapchains())
            {
                XRERR("failed to restore XR render targets");
                return false;
            }
            SaveVrSettings();
        }
        else
        {
            XRLOG("render scale %.0f%% applied live",g.renderScale*100.0f);
        }
    }
    SyncInputActions();
    XrFrameWaitInfo wi={XR_TYPE_FRAME_WAIT_INFO}; g.frameState={XR_TYPE_FRAME_STATE};
    const Uint64 waitStart=SDL_GetPerformanceCounter();
    if(XR_FAILED(g.WaitFrame(g.session,&wi,&g.frameState))) return false;
    const Uint64 waitEnd=SDL_GetPerformanceCounter();
    const double frequency=static_cast<double>(SDL_GetPerformanceFrequency());
    const double waitMs=(waitEnd-waitStart)*1000.0/frequency;
    g.perfWaitSum+=waitMs; g.perfWaitMax=std::max(g.perfWaitMax,waitMs);
    XrFrameBeginInfo bi={XR_TYPE_FRAME_BEGIN_INFO}; if(XR_FAILED(g.BeginFrame(g.session,&bi))) return false;
    g.frameBegun=true; g.shouldRender=g.frameState.shouldRender;
    g.perfRenderStart=SDL_GetPerformanceCounter();

#if !defined(SRR2_VR_RENDERER_VULKAN)
    // EXT_disjoint_timer_query is asynchronous: read an older slot only when
    // ready, never stall the render thread merely to collect telemetry.
    if(!g.perfGpuChecked)
    {
        const char* extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        g.perfGpuAvailable=extensions && strstr(extensions,"GL_EXT_disjoint_timer_query");
        if(g.perfGpuAvailable)
        {
            g.GenQueriesEXT=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));
            g.BeginQueryEXT=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));
            g.EndQueryEXT=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));
            g.GetQueryObjectuivEXT=reinterpret_cast<PFNGLGETQUERYOBJECTUIVEXTPROC>(eglGetProcAddress("glGetQueryObjectuivEXT"));
            g.GetQueryObjectui64vEXT=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));
            g.perfGpuAvailable=g.GenQueriesEXT&&g.BeginQueryEXT&&g.EndQueryEXT&&
                g.GetQueryObjectuivEXT&&g.GetQueryObjectui64vEXT;
            if(g.perfGpuAvailable) g.GenQueriesEXT(4,g.perfQueries);
        }
        XRLOG("VR PERF GPU timer: %s",g.perfGpuAvailable?"available":"unavailable");
        g.perfGpuChecked=true;
    }
    g.perfQueryActive=false;
    if(g.perfGpuAvailable)
    {
        const unsigned slot=g.perfQueryIndex;
        if(g.perfQueryPending[slot])
        {
            GLuint ready=0; g.GetQueryObjectuivEXT(g.perfQueries[slot],GL_QUERY_RESULT_AVAILABLE_EXT,&ready);
            if(ready)
            {
                GLuint64 nanoseconds=0; g.GetQueryObjectui64vEXT(g.perfQueries[slot],GL_QUERY_RESULT_EXT,&nanoseconds);
                GLint disjoint=0; glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);
                const double gpuMs=nanoseconds/1000000.0;
                if(!disjoint && gpuMs>=0.0 && gpuMs<1000.0) g.perfGpuLast=gpuMs;
                g.perfQueryPending[slot]=false;
            }
        }
        if(!g.perfQueryPending[slot])
        {
            g.BeginQueryEXT(GL_TIME_ELAPSED_EXT,g.perfQueries[slot]);
            g.perfQueryActive=true;
        }
    }
#endif
    if(!g.shouldRender) return true;
    XrViewLocateInfo li={XR_TYPE_VIEW_LOCATE_INFO}; li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime=g.frameState.predictedDisplayTime; li.space=g.space;
    g.viewState={XR_TYPE_VIEW_STATE}; uint32_t count=0;
    // Eye contains swapchain bookkeeping, so locate into a contiguous array.
    XrView views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
    if(XR_FAILED(g.LocateViews(g.session,&li,&g.viewState,2,&count,views)) || count!=2) { g.shouldRender=false; return true; }
    g.eyes[0].view=views[0]; g.eyes[1].view=views[1];
    if(g.systemRecenterPending && g.frameState.predictedDisplayTime>=g.systemRecenterTime)
    {
        // Rebuild our game-relative origin from the runtime's new pose. This
        // makes the native Quest reset-view gesture recenter both yaw and
        // local X/Y/Z around the player.
        // A Quest recenter performed while crouching must not redefine the
        // standing eye height. Preserve Y, while X/Z and yaw are recentered.
        g.preserveHeightOnRecenter=g.originValid && !g.seatedMode;
        g.preservedHeadHeight=g.origin.position.y;
        // Meta may finish applying the reference-space change over the next
        // few located frames. Re-sample during that short settling window so
        // the final horizontal origin cannot retain a forward offset.
        g.recenterSettleFrames=3;
        g.originValid=false;
        // The Meta reset-view gesture changes the OpenXR reference space.
        // Re-anchor active movie/menu panels after the new poses settle so
        // they appear directly in front of the player's new yaw.
        g.moviePlaneAnchorValid=false;
        g.frontendPlaneAnchorValid=false;
        g.systemRecenterPending=false;
        XRLOG("applying system VR recenter");
    }
    if(g.recenterSettleFrames>0)
    {
        g.originValid=false;
        // Quest can refine the new reference-space pose for several frames.
        // Follow that settling period instead of freezing the panel at the
        // first intermediate yaw returned by the runtime.
        g.moviePlaneAnchorValid=false;
        g.frontendPlaneAnchorValid=false;
    }
    if(!g.originValid && (g.viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        // Recenter heading only. Capturing pitch/roll in the origin makes the
        // relative pitch axis rotate with yaw, so a level horizon appears
        // sloped after looking behind.
        g.origin.orientation=YawOnly(g.eyes[0].view.pose.orientation);
        g.origin.position.x=(g.eyes[0].view.pose.position.x+g.eyes[1].view.pose.position.x)*0.5f;
        const float locatedHeight=(g.eyes[0].view.pose.position.y+g.eyes[1].view.pose.position.y)*0.5f;
        g.origin.position.y=g.preserveHeightOnRecenter?g.preservedHeadHeight:locatedHeight;
        g.origin.position.z=(g.eyes[0].view.pose.position.z+g.eyes[1].view.pose.position.z)*0.5f;
        if(g.recenterSettleFrames>0) --g.recenterSettleFrames;
        if(g.recenterSettleFrames==0) g.preserveHeightOnRecenter=false;
        g.originValid=true;
    }
    for(unsigned hand=0; hand<2; ++hand)
    {
        g.handPoseValid[hand]=false;
        if(!g.originValid || !g.handSpaces[hand]) continue;
        XrSpaceLocation location={XR_TYPE_SPACE_LOCATION};
        if(XR_SUCCEEDED(g.LocateSpace(g.handSpaces[hand],g.space,
                                     g.frameState.predictedDisplayTime,&location)) &&
           (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
           (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        {
            g.handPoses[hand]=location.pose;
            g.handPoseValid[hand]=true;
        }
    }
    UpdateVrSteeringWheel();
    UpdateVrInCarCharacterVisibility();
    return true;
}

static void DrawRadarPlane();
static void DrawMissionHudPlanes();
static void DrawGameplayHud();
#if defined(SRR2_VR_RENDERER_VULKAN)
static void DrawVulkanHudQuad(VkDescriptorSet texture,const float* vertices,
                              Eye& eye,float opacity);
#endif
static void ApplyIrisBlackout()
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    // Vulkan applies the fade to the completed stereo composition layer in
    // EndFrame. Never rasterize overlay geometry into individual eye images:
    // foveation/tile boundaries make those quads visible as rectangles.
    return;
#else
    // Match the proven GLES fade while keeping its target synchronized with
    // the authored Iris screen's intro/outro lifecycle.
    static float alpha=0.0f;
    static Uint32 lastTicks=0;
    const Uint32 now=SDL_GetTicks();
    const float dt=lastTicks?std::min(0.1f,(now-lastTicks)*0.001f):0.0f;
    lastTicks=now;
    const float target=g.irisBlackoutTarget?1.0f:0.0f;
    // Keep the transition visible for roughly 0.8 seconds. The old 3.5/s
    // value reached opaque in only 0.29 s and appeared instantaneous once the
    // original circular geometry was replaced by a full-eye cover.
    const float step=dt*1.25f;
    if(alpha<target) alpha=std::min(target,alpha+step);
    else if(alpha>target) alpha=std::max(target,alpha-step);
    if(alpha<=0.001f)return;

    if(!g.irisBlackProgram)
    {
        const char* vs="precision highp float;attribute vec2 position;void main(){gl_Position=vec4(position,0.0,1.0);}";
        const char* fs="precision mediump float;uniform float alpha;void main(){gl_FragColor=vec4(0.0,0.0,0.0,alpha);}";
        g.irisBlackProgram=CreateGlProgram(vs,fs);
    }
    if(!g.hudQuadVbo)glGenBuffers(1,&g.hudQuadVbo);
    if(!g.irisBlackProgram||!g.hudQuadVbo)return;

    GLint oldProgram=0,oldArray=0,oldSrc=0,oldDst=0;
    GLboolean oldMask[4];
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldArray);
    glGetIntegerv(GL_BLEND_SRC_RGB,&oldSrc);glGetIntegerv(GL_BLEND_DST_RGB,&oldDst);
    glGetBooleanv(GL_COLOR_WRITEMASK,oldMask);
    const GLboolean depth=glIsEnabled(GL_DEPTH_TEST),blend=glIsEnabled(GL_BLEND);
    const GLboolean cull=glIsEnabled(GL_CULL_FACE),scissor=glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    const float vertices[8]={-1,-1,1,-1,-1,1,1,1};
    glUseProgram(g.irisBlackProgram);glBindBuffer(GL_ARRAY_BUFFER,g.hudQuadVbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);
    const GLint position=glGetAttribLocation(g.irisBlackProgram,"position");
    glEnableVertexAttribArray(position);
    glVertexAttribPointer(position,2,GL_FLOAT,GL_FALSE,0,reinterpret_cast<const void*>(0));
    glUniform1f(glGetUniformLocation(g.irisBlackProgram,"alpha"),alpha);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);glDisableVertexAttribArray(position);
    glColorMask(oldMask[0],oldMask[1],oldMask[2],oldMask[3]);
    glBlendFunc(oldSrc,oldDst);glBindBuffer(GL_ARRAY_BUFFER,oldArray);glUseProgram(oldProgram);
    if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
    if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
    if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
#endif
}
unsigned GetEyeCount(){ return g.shouldRender ? 2u : 0u; }
void RecordPddiDraw(unsigned primitiveType,unsigned vertexCount,bool indexed,double cpuMilliseconds)
{
    ++g.perfDraws; if(indexed) ++g.perfIndexedDraws;
    g.perfVertices+=vertexCount; g.perfDrawCpu+=cpuMilliseconds;
    if(primitiveType==0) g.perfTriangles+=vertexCount/3;
    else if(primitiveType==1 && vertexCount>=3) g.perfTriangles+=vertexCount-2;
}
void RecordPddiMaterial(bool changed,double cpuMilliseconds)
{
    if(changed) ++g.perfMaterials;
    g.perfMaterialCpu+=cpuMilliseconds;
}
void RecordPddiUpload(unsigned bytes,double cpuMilliseconds)
{
    ++g.perfUploadCalls; g.perfUploadBytes+=bytes; g.perfUploadCpu+=cpuMilliseconds;
}
void RecordRenderSection(unsigned section,double cpuMilliseconds)
{
    if(section<22) g.perfSections[section]+=cpuMilliseconds;
}
bool IsMultiviewAvailable()
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    return gVulkanContext.IsMultiviewSupported();
#else
    return g.multiviewAvailable;
#endif
}
// This is queried from the material hot path. The renderer exclusively owns
// multiviewRendering, so asking the GL driver for its framebuffer binding on
// every shader selection only introduces a synchronous CPU/GPU round trip.
bool IsMultiviewRendering(){return g.multiviewRendering&&g.multiviewTargetActive;}
void SetMultiviewTargetActive(bool active)
{
    g.multiviewTargetActive=g.multiviewRendering&&active;
}
bool BeginMultiview()
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    if(!gVulkanContext.IsMultiviewSupported() || !g.shouldRender) return false;
    Eye& eye=g.eyes[0];
    uint32_t index=0;
    XrSwapchainImageAcquireInfo ai={XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(XR_FAILED(g.AcquireSwapchainImage(eye.swapchain,&ai,&index))) return false;
    XrSwapchainImageWaitInfo wi={XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout=XR_INFINITE_DURATION;
    if(XR_FAILED(g.WaitSwapchainImage(eye.swapchain,&wi)))
    {
        XrSwapchainImageReleaseInfo ri={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        g.ReleaseSwapchainImage(eye.swapchain,&ri);
        return false;
    }
    const bool firstUse=index>=eye.vulkanImageInitialized.size() ||
                        !eye.vulkanImageInitialized[index];
    if(index<eye.foveationImages.size())
        gVulkanContext.SetFragmentDensityMap(eye.foveationImages[index].image,
            eye.foveationImages[index].width,eye.foveationImages[index].height);
    else
        gVulkanContext.SetFragmentDensityMap(VK_NULL_HANDLE,0,0);
    if(index>=eye.vulkanImages.size() || !gVulkanContext.BeginPddiEye() ||
       !gVulkanContext.ClearImageInPddiEye(eye.vulkanImages[index].image,firstUse,2))
    {
        XrSwapchainImageReleaseInfo ri={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        g.ReleaseSwapchainImage(eye.swapchain,&ri);
        return false;
    }
    if(index<eye.vulkanImageInitialized.size()) eye.vulkanImageInitialized[index]=1;
    g.multiviewImageIndex=index;
    g.multiviewImageAcquired=true;
    g.multiviewRendering=true;
    g.multiviewTargetActive=true;
    g.activeEye=1;
    g.cullingBaseValid=false;
    std::memset(g.missionHudVisible,0,sizeof(g.missionHudVisible));
    if(!g.renderModeLogged)
    {
        XRLOG("VR render mode: Vulkan multiview single-pass");
        g.renderModeLogged=true;
    }
    return true;
#else
    if(!g.multiviewAvailable||!g.shouldRender||!pglAreMultiviewProgramsReady())
    {
        if(g.shouldRender&&!g.renderModeLogged)
        {
            XRLOG("VR render mode: dual-pass fallback (extension=%d programs=%d)",
                  g.multiviewAvailable?1:0,pglAreMultiviewProgramsReady()?1:0);
            g.renderModeLogged=true;
        }
        return false;
    }
    uint32_t index=0;XrSwapchainImageAcquireInfo ai={XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(XR_FAILED(g.AcquireSwapchainImage(g.eyes[0].swapchain,&ai,&index)))return false;
    XrSwapchainImageWaitInfo wi={XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;
    if(XR_FAILED(g.WaitSwapchainImage(g.eyes[0].swapchain,&wi))){XrSwapchainImageReleaseInfo ri={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};g.ReleaseSwapchainImage(g.eyes[0].swapchain,&ri);return false;}
    g.multiviewImageAcquired=true;g.multiviewImageIndex=index;g.multiviewRendering=true;g.multiviewTargetActive=true;g.activeEye=1;g.cullingBaseValid=false;
    std::memset(g.missionHudVisible,0,sizeof(g.missionHudVisible));
    glBindFramebuffer(GL_FRAMEBUFFER,g.framebuffer);
    g.FramebufferTextureMultiviewOVR(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,g.eyes[0].images[index].image,0,0,2);
    g.FramebufferTextureMultiviewOVR(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,g.multiviewDepthTexture,0,0,2);
    if(index>=g.multiviewFramebufferValid.size() ||
       (!g.multiviewFramebufferValid[index] &&
        glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE))
    {g.multiviewTargetActive=false;g.multiviewRendering=false;XrSwapchainImageReleaseInfo ri={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};g.ReleaseSwapchainImage(g.eyes[0].swapchain,&ri);g.multiviewImageAcquired=false;return false;}
    g.multiviewFramebufferValid[index]=1;
    glViewport(0,0,g.eyes[0].width,g.eyes[0].height);glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    if(!g.renderModeLogged){XRLOG("VR render mode: GLES multiview single-pass");g.renderModeLogged=true;}
    return true;
#endif
}
bool PrepareMultiviewCamera(tCamera* base)
{
    if(!g.multiviewRendering||!base)return false;
    rmt::Matrix leftWorld,rightWorld,centreWorld,worldToLeft,worldToRight;
    if(!GetEyeCamera(0,base,&leftWorld)||!GetEyeCamera(1,base,&rightWorld))return false;
    // Traverse/cull the scene once from the midpoint of the two eyes.  Using
    // the left eye as the common camera can reject geometry which is visible
    // only at the outer edge of the right eye.
    if(!GetLatestCullingCamera(&centreWorld))return false;
    worldToLeft.InvertOrtho(leftWorld);
    worldToRight.InvertOrtho(rightWorld);
    g.multiviewViewAdjustment[0].Mult(centreWorld,worldToLeft);
    g.multiviewViewAdjustment[1].Mult(centreWorld,worldToRight);
    MakeProjection(g.eyes[0].view.fov,0.1f,VR_WORLD_FAR_PLANE,&g.multiviewProjection[0]);
    MakeProjection(g.eyes[1].view.fov,0.1f,VR_WORLD_FAR_PLANE,&g.multiviewProjection[1]);
    return true;
}
bool GetMultiviewMatrices(rmt::Matrix* p,rmt::Matrix* a)
{
    if(!g.multiviewRendering||!g.worldRendering||g.embeddedHudRendering||!p||!a)return false;p[0]=g.multiviewProjection[0];p[1]=g.multiviewProjection[1];a[0]=g.multiviewViewAdjustment[0];a[1]=g.multiviewViewAdjustment[1];return true;
}
bool BeginMultiviewGuiEye(unsigned eye)
{
    if(!g.multiviewRendering||!g.multiviewImageAcquired||eye>=2) return false;
#if defined(SRR2_VR_RENDERER_VULKAN)
    // World multiview and conventional per-eye GUI use different render-pass
    // view masks, but they can remain in one command buffer. End only the
    // active render pass instead of submitting and waiting at every boundary.
    if(eye!=0)
    {
        DrawGameplayHud();
        ApplyIrisBlackout();
    }
    gVulkanContext.EndActiveRenderPass();
    g.multiviewTargetActive=false;
    g.activeEye=eye+1;
    g.worldRendering=false;
    return true;
#else
    // Stop broadcasting ordinary (non-multiview) Scrooby shaders. Attach one
    // array layer at a time while retaining the colour/depth produced by the
    // single-pass world render.
    g.multiviewTargetActive=false;
    g.activeEye=eye+1;
    g.worldRendering=false;
    glBindFramebuffer(GL_FRAMEBUFFER,g.layerFramebuffer);
    g.FramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
        g.eyes[0].images[g.multiviewImageIndex].image,0,eye);
    g.FramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,
        g.multiviewDepthTexture,0,eye);
    glViewport(0,0,g.eyes[eye].width,g.eyes[eye].height);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
#endif
}
void EndMultiview()
{
    if(!g.multiviewRendering)return;
#if defined(SRR2_VR_RENDERER_VULKAN)
    g.multiviewTargetActive=false;
    DrawGameplayHud();
    ApplyIrisBlackout();
    if(!gVulkanContext.EndPddiEye())
        XRERR("failed to submit Vulkan multiview GUI");
    g.perfGpuLast=gVulkanContext.GetLastGpuMilliseconds();
    if(g.multiviewImageAcquired)
    {
        XrSwapchainImageReleaseInfo ri={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        g.ReleaseSwapchainImage(g.eyes[0].swapchain,&ri);
    }
    g.multiviewImageAcquired=false;
    g.multiviewRendering=false;
    g.activeEye=0;
    g.worldRendering=false;
#else
    g.multiviewTargetActive=false;
    g.multiviewRendering=false;
    // Ordinary HUD shaders cannot target a two-view framebuffer. Draw their
    // cached planes into each array layer after the world broadcast finishes.
    glBindFramebuffer(GL_FRAMEBUFFER,g.layerFramebuffer);
    for(unsigned eye=0;eye<2;++eye){g.activeEye=eye+1;g.FramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,g.eyes[0].images[g.multiviewImageIndex].image,0,eye);g.FramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,g.multiviewDepthTexture,0,eye);DrawRadarPlane();DrawMissionHudPlanes();DrawPauseCoinIcon();ApplyIrisBlackout();}
    // xrReleaseSwapchainImage transfers ownership to the runtime. A flush is
    // sufficient to make queued GL work visible without stalling CPU and GPU
    // every frame as glFinish did.
    glFlush();if(g.multiviewImageAcquired){XrSwapchainImageReleaseInfo ri={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};g.ReleaseSwapchainImage(g.eyes[0].swapchain,&ri);}g.multiviewImageAcquired=false;g.activeEye=0;g.worldRendering=false;
#endif
}
bool BeginEye(unsigned eye)
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    if(eye>=2 || !g.shouldRender) return false;
    Eye& e=g.eyes[0];
    bool firstUse=false;
    if(eye==0)
    {
        uint32_t index=0;
        XrSwapchainImageAcquireInfo ai={XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if(XR_FAILED(g.AcquireSwapchainImage(e.swapchain,&ai,&index))) return false;
        XrSwapchainImageWaitInfo wi={XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wi.timeout=XR_INFINITE_DURATION;
        if(XR_FAILED(g.WaitSwapchainImage(e.swapchain,&wi)))
        {
            XrSwapchainImageReleaseInfo ri={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            g.ReleaseSwapchainImage(e.swapchain,&ri); return false;
        }
        g.multiviewImageIndex=index;
        g.multiviewImageAcquired=true;
        firstUse=index>=e.vulkanImageInitialized.size() ||
                 !e.vulkanImageInitialized[index];
        const bool cleared=index<e.vulkanImages.size();
        if(!cleared)
        {
            XrSwapchainImageReleaseInfo ri={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            g.ReleaseSwapchainImage(e.swapchain,&ri);
            g.multiviewImageAcquired=false;
            XRERR("failed to record Vulkan eye clear");
            return false;
        }
    }
    if(!g.multiviewImageAcquired) return false;
    if(!gVulkanContext.BeginPddiEye() ||
       !gVulkanContext.ClearImageInPddiEye(
           e.vulkanImages[g.multiviewImageIndex].image,
           firstUse,eye))
    {
        XRERR("failed to begin Vulkan PDDI eye %u",eye);
        if(eye==0)
        {
            XrSwapchainImageReleaseInfo ri={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            g.ReleaseSwapchainImage(e.swapchain,&ri);
            g.multiviewImageAcquired=false;
        }
        return false;
    }
    if(eye==0 && g.multiviewImageIndex<e.vulkanImageInitialized.size())
        e.vulkanImageInitialized[g.multiviewImageIndex]=1;
    g.activeEye=eye+1;
    g.cullingBaseValid=false;
    if(!gVulkanContext.DrawSmokeTriangle(
           e.vulkanImages[g.multiviewImageIndex].image,e.vulkanFormat,
           static_cast<uint32_t>(e.width),static_cast<uint32_t>(e.height),eye))
        XRERR("failed to draw Vulkan smoke triangle for eye %u",eye);
    return true;
#else
    if(eye>=2||!g.shouldRender) return false; Eye& e=g.eyes[eye]; uint32_t index=g.multiviewImageIndex;
    g.multiviewTargetActive=false;
    if(eye==0)
    {
    XrSwapchainImageAcquireInfo ai={XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(XR_FAILED(g.AcquireSwapchainImage(e.swapchain,&ai,&index))) return false;
    XrSwapchainImageWaitInfo wi={XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wi.timeout=XR_INFINITE_DURATION;
    if(XR_FAILED(g.WaitSwapchainImage(e.swapchain,&wi)))
    {
        // Every successful acquire must be paired with a release. Otherwise
        // the finite swapchain eventually starves and remains grey until the
        // XR session is recreated.
        XrSwapchainImageReleaseInfo ri={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        g.ReleaseSwapchainImage(e.swapchain,&ri);
        return false;
    }
    g.multiviewImageIndex=index;g.multiviewImageAcquired=true;
    }
    g.activeEye=eye+1; g.cullingBaseValid=false;
    if(eye==0)
        std::memset(g.missionHudVisible,0,sizeof(g.missionHudVisible));
    glBindFramebuffer(GL_FRAMEBUFFER,g.framebuffer);
    g.FramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,e.images[index].image,0,eye);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,g.depthTexture,0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
    {
        XrSwapchainImageReleaseInfo ri={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        g.ReleaseSwapchainImage(e.swapchain,&ri);
        g.activeEye=0;
        return false;
    }
    // Do not encode a second time: legacy Pure3D shader outputs are already
    // gamma-encoded, while the sRGB image metadata is for the XR compositor.
#ifdef GL_FRAMEBUFFER_SRGB
    glDisable(GL_FRAMEBUFFER_SRGB);
#endif
    glViewport(0,0,e.width,e.height);
    glClearColor(0,0,0,1);
    // A GUI scissor can remain enabled after the previous eye. It must not
    // clip the full-eye clear or leave an uncleared strip at an edge.
    const GLboolean scissorWasEnabled=glIsEnabled(GL_SCISSOR_TEST);
    if(scissorWasEnabled) glDisable(GL_SCISSOR_TEST);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    if(scissorWasEnabled) glEnable(GL_SCISSOR_TEST);
    return true;
#endif
}

void ApplyGtao()
{
    // The existing pass is mono-depth and must never run in VR, including
    // the compatibility dual-pass renderer.
    if(g.vrModeEnabled||g.multiviewRendering||!g.gtaoEnabled||!g.activeEye||!g.gtaoProgram||!g.depthTexture) return;
    const Eye& eye=g.eyes[g.activeEye-1];
    GLint oldFramebuffer=0,oldViewport[4]={0},oldActiveTexture=GL_TEXTURE0,oldTexture0=0,oldTexture1=0;
    GLint oldProgram=0,oldArrayBuffer=0,oldBlendSrc=GL_ONE,oldBlendDst=GL_ZERO;
    GLint oldAttribBuffer=0,oldAttribSize=4,oldAttribType=GL_FLOAT,oldAttribStride=0;
    GLvoid* oldAttribPointer=NULL; GLint oldAttribEnabled=0,oldAttribNormalized=0;
    GLboolean oldDepth=glIsEnabled(GL_DEPTH_TEST),oldBlend=glIsEnabled(GL_BLEND);
    GLboolean oldCull=glIsEnabled(GL_CULL_FACE),oldScissor=glIsEnabled(GL_SCISSOR_TEST),oldDepthMask=GL_TRUE;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&oldFramebuffer); glGetIntegerv(GL_VIEWPORT,oldViewport);
    glGetBooleanv(GL_DEPTH_WRITEMASK,&oldDepthMask); glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActiveTexture);
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram); glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldArrayBuffer);
    glGetIntegerv(GL_BLEND_SRC_RGB,&oldBlendSrc); glGetIntegerv(GL_BLEND_DST_RGB,&oldBlendDst);
    glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&oldAttribBuffer);
    glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_SIZE,&oldAttribSize);
    glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_TYPE,&oldAttribType);
    glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&oldAttribStride);
    glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&oldAttribNormalized);
    glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&oldAttribEnabled);
    glGetVertexAttribPointerv(0,GL_VERTEX_ATTRIB_ARRAY_POINTER,&oldAttribPointer);
    glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture0);
    glActiveTexture(GL_TEXTURE1); glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture1);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST); glDepthMask(GL_FALSE);
    glBindBuffer(GL_ARRAY_BUFFER,g.gtaoVbo); glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);

    const XrFovf& f=eye.view.fov;
    const float tanL=std::tan(f.angleLeft),tanR=std::tan(f.angleRight);
    const float tanD=std::tan(f.angleDown),tanU=std::tan(f.angleUp);
    const float invFx=(tanR-tanL)*0.5f,invFy=(tanU-tanD)*0.5f;
    const float offX=(tanR+tanL)/(tanR-tanL),offY=(tanU+tanD)/(tanU-tanD);

    glBindFramebuffer(GL_FRAMEBUFFER,g.gtaoFramebuffer[0]); glViewport(0,0,g.gtaoWidth,g.gtaoHeight);
    glUseProgram(g.gtaoProgram); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g.depthTexture);
    glUniform1i(glGetUniformLocation(g.gtaoProgram,"depthTex"),0);
    glUniform2f(glGetUniformLocation(g.gtaoProgram,"fullInvSize"),1.0f/eye.width,1.0f/eye.height);
    glUniform2f(glGetUniformLocation(g.gtaoProgram,"focalPixels"),
                static_cast<float>(eye.width)/(tanR-tanL),
                static_cast<float>(eye.height)/(tanU-tanD));
    glUniform4f(glGetUniformLocation(g.gtaoProgram,"projXy"),invFx,invFy,offX,offY);
    glUniform2f(glGetUniformLocation(g.gtaoProgram,"clipPlanes"),0.1f,VR_WORLD_FAR_PLANE);
    glDrawArrays(GL_TRIANGLES,0,6);

    // Smooth the quarter-resolution result before upsampling.  The AO pass keeps
    // logarithmic view depth in green so the bilateral weights remain useful
    // across the entire 0.1..1000 m depth range.
    glBindFramebuffer(GL_FRAMEBUFFER,g.gtaoFramebuffer[1]);
    glUseProgram(g.gtaoBlurProgram);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g.gtaoTexture[0]);
    glUniform1i(glGetUniformLocation(g.gtaoBlurProgram,"aoTex"),0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,g.depthTexture);
    glUniform1i(glGetUniformLocation(g.gtaoBlurProgram,"depthTex"),1);
    glUniform2f(glGetUniformLocation(g.gtaoBlurProgram,"aoInvSize"),
                1.0f/g.gtaoWidth,1.0f/g.gtaoHeight);
    glDrawArrays(GL_TRIANGLES,0,6);

    glBindFramebuffer(GL_FRAMEBUFFER,g.framebuffer); glViewport(0,0,eye.width,eye.height);
    glUseProgram(g.gtaoCompositeProgram); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g.gtaoTexture[1]);
    glUniform1i(glGetUniformLocation(g.gtaoCompositeProgram,"aoTex"),0);
    glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR,GL_ZERO); glDrawArrays(GL_TRIANGLES,0,6);

    glBindBuffer(GL_ARRAY_BUFFER,oldAttribBuffer);
    glVertexAttribPointer(0,oldAttribSize,oldAttribType,oldAttribNormalized != 0,oldAttribStride,oldAttribPointer);
    if(oldAttribEnabled != 0)glEnableVertexAttribArray(0);else glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,oldArrayBuffer); glUseProgram(oldProgram);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,oldTexture1);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,oldTexture0);
    glActiveTexture(oldActiveTexture); glBindFramebuffer(GL_FRAMEBUFFER,oldFramebuffer);
    glViewport(oldViewport[0],oldViewport[1],oldViewport[2],oldViewport[3]); glDepthMask(oldDepthMask);
    if(oldDepth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    if(oldBlend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
    glBlendFunc(oldBlendSrc,oldBlendDst);
    if(oldCull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
    if(oldScissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
}
static void DrawRadarPlane();
static void DrawMissionHudPlanes();
static void DrawGameplayHud();
void EndEye(unsigned eye)
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    // Composite the mono offscreen radar texture into the current eye while
    // the Vulkan command buffer and swapchain image are still writable.
    DrawGameplayHud();
    ApplyIrisBlackout();
    if(!gVulkanContext.EndPddiEye())
        XRERR("failed to submit Vulkan PDDI eye %u",eye);
    g.perfGpuLast=gVulkanContext.GetLastGpuMilliseconds();
    if(eye==1 && g.multiviewImageAcquired)
    {
        XrSwapchainImageReleaseInfo ri={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        g.ReleaseSwapchainImage(g.eyes[0].swapchain,&ri);
        g.multiviewImageAcquired=false;
    }
    g.worldRendering=false;
    g.activeEye=0;
#else
    // Scrooby's GUI layer is not guaranteed to be submitted for both legacy
    // render passes. Present the cached radar explicitly while each OpenXR eye
    // target is still bound, so its 2D frame cannot remain left-eye-only.
    DrawGameplayHud();
    DrawPauseCoinIcon();
    ApplyIrisBlackout();
    if(eye==1 && g.multiviewImageAcquired)
    {
        XrSwapchainImageReleaseInfo ri={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        glFlush();
        g.ReleaseSwapchainImage(g.eyes[eye].swapchain,&ri);
        g.multiviewImageAcquired=false;
    }
    g.worldRendering=false;
    g.activeEye=0;
#endif
}

#if defined(SRR2_VR_RENDERER_VULKAN)
static VkFormat VulkanRenderFormat(VkFormat format)
{
    if(format==VK_FORMAT_R8G8B8A8_SRGB) return VK_FORMAT_R8G8B8A8_UNORM;
    if(format==VK_FORMAT_B8G8R8A8_SRGB) return VK_FORMAT_B8G8R8A8_UNORM;
    return format;
}

bool GetActiveVulkanEyeTarget(VulkanEyeTarget* target)
{
    if(!target || !g.multiviewImageAcquired ||
       (g.activeEye==0 && !g.vulkanCaptureActive)) return false;
#if defined(SRR2_VR_RENDERER_VULKAN)
    if(g.vulkanCaptureActive)
    {
        if(g.gameplayHudCaptureActive)
        {
            if(!g.vulkanGameplayHudImage) return false;
            target->image=g.vulkanGameplayHudImage;
            target->format=VK_FORMAT_B8G8R8A8_UNORM;
            target->width=RADAR_TEXTURE_WIDTH;
            target->height=RADAR_TEXTURE_HEIGHT;
            target->arrayLayer=0;
            target->firstUse=!g.vulkanGameplayHudInitialized;
            return true;
        }
        const bool mission=g.missionHudActiveSlot>=0;
        const unsigned slot=mission?static_cast<unsigned>(g.missionHudActiveSlot):0;
        if((mission && !g.vulkanMissionHudImage[slot]) ||
           (!mission && !g.vulkanRadarImage)) return false;
        target->image=mission?g.vulkanMissionHudImage[slot]:g.vulkanRadarImage;
        target->format=VK_FORMAT_B8G8R8A8_UNORM;
        target->width=mission?MissionHudTextureWidth(slot):RADAR_TEXTURE_WIDTH;
        target->height=mission?MissionHudTextureHeight(slot):RADAR_TEXTURE_HEIGHT;
        target->arrayLayer=0;
        target->firstUse=mission?!g.vulkanMissionHudInitialized[slot]:
                                  !g.vulkanRadarInitialized;
        return true;
    }
#endif
    const Eye& eye=g.eyes[0];
    const uint32_t index=g.multiviewImageIndex;
    if(index>=eye.vulkanImages.size()) return false;
    target->image=eye.vulkanImages[index].image;
    // GLES disables framebuffer sRGB conversion and writes display-ready
    // colours directly. Use the compatible UNORM view of the sRGB swapchain
    // to preserve that contract without a costly pow() for every fragment.
    target->format=VulkanRenderFormat(eye.vulkanFormat);
    target->width=static_cast<uint32_t>(eye.width);
    target->height=static_cast<uint32_t>(eye.height);
    target->arrayLayer=g.multiviewRendering && g.multiviewTargetActive?2u:g.activeEye-1;
    target->firstUse=false;
    return true;
}
#endif
void SetWorldRendering(bool enabled){ g.worldRendering=enabled; }
void SetEmbeddedHudRendering(bool enabled){ g.embeddedHudRendering=enabled; }
bool IsEmbeddedHudRendering(){ return g.embeddedHudRendering; }
void SetRadarRendering(bool enabled){ g.radarRendering=enabled && g.vrModeEnabled; }
bool IsRadarRendering(){ return g.radarRendering; }
void SetGameplayHudScreen(const void* screen)
{
    g.gameplayHudScreen=screen;
    XRLOG("gameplay HUD screen %s (%p)",screen?"registered":"cleared",screen);
}
bool IsGameplayHudScreen(const void* screen)
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    const bool matches=screen && screen==g.gameplayHudScreen;
    static unsigned mismatches=0;
    if(screen && g.gameplayHudScreen && !matches &&
       mismatches++<12)
        XRLOG("gameplay HUD screen mismatch: displayed=%p registered=%p",
              screen,g.gameplayHudScreen);
    return matches;
#else
    return false;
#endif
}
bool IsGameplayHudCaptureActive(){ return g.gameplayHudCaptureActive; }
bool IsMissionHudCaptureActive(){ return g.missionHudActiveSlot>=0; }

bool BeginGameplayHudCapture()
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    static unsigned rejected=0;
    if(g.activeEye>1 || !g.multiviewImageAcquired || !g.gameplayHudScreen ||
       g.gameplayHudCaptureActive)
    {
        if(rejected++<8)
            XRERR("gameplay HUD capture rejected: eye=%u screen=%p active=%d",
                  g.activeEye,g.gameplayHudScreen,
                  g.gameplayHudCaptureActive?1:0);
        return false;
    }
    if(!g.vulkanGameplayHudImage && !gVulkanContext.CreateTexture2D(
        RADAR_TEXTURE_WIDTH,RADAR_TEXTURE_HEIGHT,1,&g.vulkanGameplayHudImage,
        &g.vulkanGameplayHudMemory,&g.vulkanGameplayHudView,
        &g.vulkanGameplayHudSampler,&g.vulkanGameplayHudDescriptor))
    {
        XRERR("gameplay HUD texture creation failed");
        return false;
    }
    if(!gVulkanContext.BeginOffscreenTarget(g.vulkanGameplayHudImage,
                                             g.vulkanGameplayHudInitialized))
    {
        XRERR("gameplay HUD offscreen begin failed: initialized=%d",
              g.vulkanGameplayHudInitialized?1:0);
        return false;
    }
    g.gameplayHudCaptureActive=true;
    g.vulkanCaptureActive=true;
    g.radarRendering=true;
    static bool logged=false;
    if(!logged)
    {
        XRLOG("gameplay HUD full-screen capture active");
        logged=true;
    }
    return true;
#else
    return false;
#endif
}

void EndGameplayHudCapture()
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    if(!g.gameplayHudCaptureActive) return;
    g.radarRendering=false;
    g.gameplayHudCaptureActive=false;
    g.vulkanCaptureActive=false;
    if(gVulkanContext.EndOffscreenTarget(g.vulkanGameplayHudImage))
        g.vulkanGameplayHudInitialized=true;
    else
        XRERR("gameplay HUD offscreen end failed");
#endif
}
void PrepareRadarDraw()
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    if(g.radarRendering) ++g.radarDrawCount;
    return;
#else
    if(!g.radarRendering || !g.hudCaptureFramebuffer) return;
    ++g.radarDrawCount;
    // Some Pure3D draw paths can restore their render target while selecting
    // a shader. Radar capture owns the target for the complete group pass.
    glBindFramebuffer(GL_FRAMEBUFFER,g.hudCaptureFramebuffer);
    // A Pure3D minimap owns a sub-viewport which SetupHardwareProjection has
    // already converted into radar-target pixels. Do not stretch it back over
    // the complete texture. Regular Scrooby sprites use the full canvas.
    if(!g.embeddedHudRendering)
    {
        const bool missionCapture=g.missionHudActiveSlot>=0;
        glViewport(0,0,
                   missionCapture?MissionHudTextureWidth((unsigned)g.missionHudActiveSlot):RADAR_TEXTURE_WIDTH,
                   missionCapture?MissionHudTextureHeight((unsigned)g.missionHudActiveSlot):RADAR_TEXTURE_HEIGHT);
        // Regular Scrooby sprites are a flat overlay and must not inherit the
        // depth/cull/colour-mask state left by Hole0 or Map0.
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    }
    // For embedded Pure3D HUD objects, retain the state selected by
    // FePure3dObject. Hole0 writes only depth and Map0 then uses that circular
    // depth mask. Overriding either depth testing or the colour mask here
    // removes the authored circular clipping.
    // The regular HUD scissor is expressed in eye-swapchain pixels. It lies
    // outside this smaller offscreen target and would reject every fragment.
    glDisable(GL_SCISSOR_TEST);
#endif
}
bool GetActiveRadarProjection(rmt::Matrix* out,int* width,int* height)
{
    // HudMap0 contains a regular Scrooby overlay and Map0, a Pure3D object
    // with its own top-down camera. The overlay needs the authored 640x480
    // projection, but Map0 must keep the camera projection installed by
    // FePure3dObject::Render().
    if(!out || !width || !height || !g.radarRendering ||
       g.embeddedHudRendering) return false;
    out->Identity();
    // FeScreen submits authored coordinates as x/640 and y/640, translated to
    // [-0.5..0.5] x [-0.375..0.375]. Map that space linearly so Radar0 sprites
    // and Map0's normalized viewport share exactly the same 640x480 canvas.
    out->SetOrthographic(-0.5f,0.5f,-0.375f,0.375f,-10.0f,10.0f);
    *width=g.missionHudActiveSlot>=0?MissionHudTextureWidth((unsigned)g.missionHudActiveSlot):RADAR_TEXTURE_WIDTH;
    *height=g.missionHudActiveSlot>=0?MissionHudTextureHeight((unsigned)g.missionHudActiveSlot):RADAR_TEXTURE_HEIGHT;
    return true;
}

bool BeginRadarCapture(int xMin,int yMin,int xMax,int yMax)
{
    if(g.gameplayHudCaptureActive || !g.multiviewImageAcquired || g.activeEye>1)
        return false;
#if !defined(SRR2_VR_RENDERER_VULKAN)
    if(!IsSpatialHudEnabled()) return false;
#endif
    g.missionHudActiveSlot=-1;
    g.radarMapRect[0]=xMin; g.radarMapRect[1]=yMin;
    g.radarMapRect[2]=xMax; g.radarMapRect[3]=yMax;
#if defined(SRR2_VR_RENDERER_VULKAN)
    // SetRadarAuthoredRect is supplied by the square Radar0 frame and is the
    // presentation/crop authority. Map0 itself intentionally owns a much
    // taller Pure3D viewport; replacing the frame rectangle with those bounds
    // produced the observed 0.328-aspect vertical strip in the headset.
    if(!g.radarCropValid)
    {
        g.radarRect[0]=xMin; g.radarRect[1]=yMin;
        g.radarRect[2]=xMax; g.radarRect[3]=yMax;
    }
    if(!g.vulkanRadarImage && !gVulkanContext.CreateTexture2D(
        RADAR_TEXTURE_WIDTH,RADAR_TEXTURE_HEIGHT,1,&g.vulkanRadarImage,
        &g.vulkanRadarMemory,&g.vulkanRadarView,&g.vulkanRadarSampler,
        &g.vulkanRadarDescriptor))
        return false;
    if(!gVulkanContext.BeginOffscreenTarget(g.vulkanRadarImage,
                                             g.vulkanRadarInitialized))
        return false;
    g.vulkanCaptureActive=true;
    g.radarRendering=true;
    g.radarCaptureFrame=g.hudFrameSerial;
    g.radarDrawCount=0;
    // The capture projection maps Pure3D's authored 640x480 GUI canvas over
    // the complete target. Crop by the group's authored bounds so the radar
    // fills its wrist quad instead of remaining a tiny element surrounded by
    // a large transparent canvas. This avoids GLES's synchronous readback.
    const float margin=4.0f;
    g.radarUv[0]=std::max(0.0f,(g.radarRect[0]-margin)/640.0f);
    g.radarUv[1]=std::max(0.0f,1.0f-(g.radarRect[3]+margin)/480.0f);
    g.radarUv[2]=std::min(1.0f,(g.radarRect[2]+margin)/640.0f);
    g.radarUv[3]=std::min(1.0f,1.0f-(g.radarRect[1]-margin)/480.0f);
    g.radarCropValid=true;
    return true;
#else
    if(!g.radarTexture)
    {
        glGenTextures(1,&g.radarTexture);
        glBindTexture(GL_TEXTURE_2D,g.radarTexture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        std::vector<unsigned char> emptyRadar(RADAR_TEXTURE_WIDTH*RADAR_TEXTURE_HEIGHT*4,0);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,RADAR_TEXTURE_WIDTH,RADAR_TEXTURE_HEIGHT,0,GL_RGBA,GL_UNSIGNED_BYTE,&emptyRadar[0]);
        glGenFramebuffers(1,&g.radarFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER,g.radarFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,g.radarTexture,0);
        // Hole0 is a colourless Pure3D object which writes the circular HUD
        // mask into depth before Map0 is rendered. A colour-only framebuffer
        // silently discards those writes and leaves the minimap rectangular.
        glGenRenderbuffers(1,&g.radarDepthBuffer);
        glBindRenderbuffer(GL_RENDERBUFFER,g.radarDepthBuffer);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,
                              RADAR_TEXTURE_WIDTH,RADAR_TEXTURE_HEIGHT);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER,g.radarDepthBuffer);
        const char* vs="precision highp float;attribute vec4 position;attribute vec2 texcoord;varying vec2 uv;void main(){gl_Position=position;uv=texcoord;}";
        const char* fs="precision mediump float;uniform sampler2D tex;varying vec2 uv;void main(){vec4 c=texture2D(tex,uv);if(c.a<0.01)discard;gl_FragColor=c;}";
        g.radarProgram=CreateGlProgram(vs,fs);
        if(!g.hudQuadVbo)glGenBuffers(1,&g.hudQuadVbo);
    }
    if(!g.radarFramebuffer || !g.radarTexture ||
       !g.radarDepthBuffer || !g.radarProgram) return false;
    // Scrooby owns the actual authored position and scale. Sampling its real
    // bounds avoids assuming that every HUD package puts the map at 535,385.
    const float margin=4.0f;
    if(!g.radarCropValid)
    {
        g.radarUv[0]=std::max(0.0f,(xMin-margin)/2016.0f);
        g.radarUv[1]=std::max(0.0f,1.0f-(yMax+margin)/1080.0f);
        g.radarUv[2]=std::min(1.0f,(xMax+margin)/2016.0f);
        g.radarUv[3]=std::min(1.0f,1.0f-(yMin-margin)/1080.0f);
    }
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&g.radarSavedFramebuffer);
    glGetIntegerv(GL_VIEWPORT,g.radarSavedViewport);
    glGetIntegerv(GL_SCISSOR_BOX,g.radarSavedScissor);
    g.radarSavedScissorEnabled=glIsEnabled(GL_SCISSOR_TEST)==GL_TRUE;
    g.radarSavedDepthEnabled=glIsEnabled(GL_DEPTH_TEST)==GL_TRUE;
    g.radarSavedCullEnabled=glIsEnabled(GL_CULL_FACE)==GL_TRUE;
    glGetBooleanv(GL_COLOR_WRITEMASK,g.radarSavedColourMask);
    // HUD capture is a mono texture pass; broadcasting it to two views wastes
    // vertex work and is invalid for this ordinary 2D framebuffer.
    SetMultiviewTargetActive(false);
    glBindFramebuffer(GL_FRAMEBUFFER,g.radarFramebuffer);
    g.hudCaptureFramebuffer=g.radarFramebuffer;
    static bool logged=false;
    if(!logged)
    {
        SDL_Log("VR radar capture: bounds=%d,%d..%d,%d uv=%.3f,%.3f..%.3f,%.3f fbo=0x%x",
                xMin,yMin,xMax,yMax,g.radarUv[0],g.radarUv[1],g.radarUv[2],g.radarUv[3],
                (unsigned)glCheckFramebufferStatus(GL_FRAMEBUFFER));
        logged=true;
    }
    glViewport(0,0,RADAR_TEXTURE_WIDTH,RADAR_TEXTURE_HEIGHT);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(0,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    g.radarDrawCount=0;
    g.radarRendering=true;
    g.radarCaptureFrame=g.hudFrameSerial;
    return true;
#endif
}

void EndRadarCapture()
{
    if(!g.radarRendering) return;
#if defined(SRR2_VR_RENDERER_VULKAN)
    g.radarRendering=false;
    g.vulkanCaptureActive=false;
    if(gVulkanContext.EndOffscreenTarget(g.vulkanRadarImage))
    {
        g.vulkanRadarInitialized=true;
    }
    return;
#else
    g.radarRendering=false;
    g.hudCaptureFramebuffer=0;
    // The complete left-eye capture texture is presented directly to both
    // eyes. This avoids a full 1440x1080 texture copy every frame.
    static bool textureLogged=false;
    static unsigned textureScanAttempts=0;
    if(!textureLogged)
    {
        ++textureScanAttempts;
        unsigned maxAlpha=0,maxRgb=0,nonZero=0;
        int pixelMinX=RADAR_TEXTURE_WIDTH,pixelMinY=RADAR_TEXTURE_HEIGHT,pixelMaxX=-1,pixelMaxY=-1;
        std::vector<unsigned char> pixels(RADAR_TEXTURE_WIDTH*RADAR_TEXTURE_HEIGHT*4);
        glReadPixels(0,0,RADAR_TEXTURE_WIDTH,RADAR_TEXTURE_HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,&pixels[0]);
        for(int py=0;py<RADAR_TEXTURE_HEIGHT;++py)
        for(int px=0;px<RADAR_TEXTURE_WIDTH;++px)
        {
            const unsigned char* pixel=&pixels[(py*RADAR_TEXTURE_WIDTH+px)*4];
            maxAlpha=std::max(maxAlpha,(unsigned)pixel[3]);
            maxRgb=std::max(maxRgb,(unsigned)std::max(pixel[0],std::max(pixel[1],pixel[2])));
            if(pixel[0] || pixel[1] || pixel[2] || pixel[3])
            {
                ++nonZero;
                pixelMinX=std::min(pixelMinX,px); pixelMaxX=std::max(pixelMaxX,px);
                pixelMinY=std::min(pixelMinY,py); pixelMaxY=std::max(pixelMaxY,py);
            }
        }
        if(nonZero)
        {
            const int margin=3;
            pixelMinX=std::max(0,pixelMinX-margin); pixelMaxX=std::min(RADAR_TEXTURE_WIDTH-1,pixelMaxX+margin);
            pixelMinY=std::max(0,pixelMinY-margin); pixelMaxY=std::min(RADAR_TEXTURE_HEIGHT-1,pixelMaxY+margin);
            g.radarUv[0]=pixelMinX/(float)RADAR_TEXTURE_WIDTH; g.radarUv[1]=pixelMinY/(float)RADAR_TEXTURE_HEIGHT;
            g.radarUv[2]=(pixelMaxX+1)/(float)RADAR_TEXTURE_WIDTH; g.radarUv[3]=(pixelMaxY+1)/(float)RADAR_TEXTURE_HEIGHT;
            g.radarCropValid=true;
        }
        GLint framebuffer=0,program=0,scissor[4]={0,0,0,0};
        GLboolean colourMask[4]={0,0,0,0};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        glGetIntegerv(GL_SCISSOR_BOX,scissor);
        glGetBooleanv(GL_COLOR_WRITEMASK,colourMask);
        if(nonZero || textureScanAttempts==1)
        SDL_Log("VR radar texture scan: attempt=%u draws=%u pixels=%u bounds=%d,%d..%d,%d uv=%.3f,%.3f..%.3f,%.3f maxRGB=%u maxAlpha=%u fbo=%d expected=%u program=%d scissor=%d depth=%d cull=%d mask=%d%d%d%d box=%d,%d,%d,%d error=0x%x",
                textureScanAttempts,
                g.radarDrawCount,nonZero,pixelMinX,pixelMinY,pixelMaxX,pixelMaxY,
                g.radarUv[0],g.radarUv[1],g.radarUv[2],g.radarUv[3],
                maxRgb,maxAlpha,framebuffer,g.radarFramebuffer,program,
                glIsEnabled(GL_SCISSOR_TEST)?1:0,glIsEnabled(GL_DEPTH_TEST)?1:0,
                glIsEnabled(GL_CULL_FACE)?1:0,colourMask[0]?1:0,colourMask[1]?1:0,
                colourMask[2]?1:0,colourMask[3]?1:0,
                scissor[0],scissor[1],scissor[2],scissor[3],
                (unsigned)glGetError());
        // The first eye/pass can legitimately be empty while Pure3D finishes
        // preparing the minimap drawable. Keep looking until an actual radar
        // frame exists, then retain its measured crop without further GPU
        // readbacks.
        textureLogged=nonZero!=0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,g.radarSavedFramebuffer);
    SetMultiviewTargetActive(true);
    glViewport(g.radarSavedViewport[0],g.radarSavedViewport[1],g.radarSavedViewport[2],g.radarSavedViewport[3]);
    glScissor(g.radarSavedScissor[0],g.radarSavedScissor[1],
              g.radarSavedScissor[2],g.radarSavedScissor[3]);
    if(g.radarSavedScissorEnabled) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);
    if(g.radarSavedDepthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if(g.radarSavedCullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glColorMask(g.radarSavedColourMask[0],g.radarSavedColourMask[1],
                g.radarSavedColourMask[2],g.radarSavedColourMask[3]);
#endif
}

bool BeginMissionHudCapture(unsigned slot,int xMin,int yMin,int xMax,int yMax)
{
    if(!g.multiviewImageAcquired || g.activeEye>1 ||
       g.gameplayHudCaptureActive || slot>=State::MISSION_HUD_COUNT
#if !defined(SRR2_VR_RENDERER_VULKAN)
       || !IsSpatialHudEnabled()
#endif
       )
        return false;
    const int rect[4]={xMin,yMin,xMax,yMax};
    if(std::memcmp(g.missionHudRect[slot],rect,sizeof(rect))!=0)
    {
        std::memcpy(g.missionHudRect[slot],rect,sizeof(rect));
        g.missionHudCropValid[slot]=false;
    }
#if defined(SRR2_VR_RENDERER_VULKAN)
    // Capture the complete authored Scrooby group. This preserves backgrounds,
    // frames, segmented meters, ordinal text and animation as one composition
    // instead of approximating them from a single foreground sprite.
    const uint32_t textureWidth=MissionHudTextureWidth(slot);
    const uint32_t textureHeight=MissionHudTextureHeight(slot);
    if(!g.vulkanMissionHudImage[slot] && !gVulkanContext.CreateTexture2D(
        textureWidth,textureHeight,1,&g.vulkanMissionHudImage[slot],
        &g.vulkanMissionHudMemory[slot],&g.vulkanMissionHudView[slot],
        &g.vulkanMissionHudSampler[slot],&g.vulkanMissionHudDescriptor[slot]))
    {
        XRERR("Vulkan HUD slot=%u texture creation failed (%ux%u)",
              slot,textureWidth,textureHeight);
        return false;
    }
    if(!gVulkanContext.BeginOffscreenTarget(g.vulkanMissionHudImage[slot],
        g.vulkanMissionHudInitialized[slot]))
    {
        XRERR("Vulkan HUD slot=%u offscreen begin failed activeEye=%u",
              slot,g.activeEye);
        return false;
    }
    g.missionHudActiveSlot=static_cast<int>(slot);
    g.vulkanCaptureActive=true;
    g.radarRendering=true;
    g.missionHudVisible[slot]=true;
    g.missionHudCaptureFrame[slot]=g.hudFrameSerial;
    float authoredMinX=static_cast<float>(xMin);
    float authoredMaxX=static_cast<float>(xMax);
    float authoredMinY=static_cast<float>(yMin);
    float authoredMaxY=static_cast<float>(yMax);
    const bool numericCounter=slot==2 || slot==3 ||
        (slot>=6 && slot<=12);
    // Numeric bitmap groups can report a clipped transient bounds rectangle
    // while their glyph buffer is being rebuilt. Keep the authored aspect and
    // widen only the sampled UV region so all digits remain visible without
    // changing the world-locked quad spacing.
    // Keep the crop margin identical to the GLES readback path.  The
    // counter-specific minimum width below is the only extra safety needed
    // for a transient first-glyph bounds report; a large symmetric margin
    // changes the sampled aspect and visibly squeezes the bitmap.
    const float margin=4.0f;
    const float minX=std::max(0.0f,authoredMinX-margin);
    const float minY=std::max(0.0f,authoredMinY-margin);
    const float maxX=std::min(640.0f,authoredMaxX+margin);
    const float maxY=std::min(480.0f,authoredMaxY+margin);
    g.missionHudUv[slot][0]=minX/640.0f;
    g.missionHudUv[slot][1]=1.0f-maxY/480.0f;
    g.missionHudUv[slot][2]=maxX/640.0f;
	g.missionHudUv[slot][3]=1.0f-minY/480.0f;
    // Numeric groups use a deliberately wider UV crop to retain every glyph;
    // keep the world-locked quad aspect in sync or the texture is visibly
    // squeezed horizontally. Other groups retain their authored aspect.
    const float aspectWidth=maxX-minX;
    const float aspectHeight=maxY-minY;
    g.missionHudAspect[slot]=std::max(1.0f,aspectWidth)/
                             std::max(1.0f,aspectHeight);
    g.missionHudCropValid[slot]=true;
    static bool captureLogged[State::MISSION_HUD_COUNT]={false};
    if(!captureLogged[slot])
    {
        XRLOG("Vulkan HUD capture slot=%u target=%ux%u bounds=%d,%d..%d,%d uv=%.3f,%.3f..%.3f,%.3f descriptor=%p",
            slot,textureWidth,textureHeight,xMin,yMin,xMax,yMax,
            g.missionHudUv[slot][0],g.missionHudUv[slot][1],
            g.missionHudUv[slot][2],g.missionHudUv[slot][3],
            reinterpret_cast<void*>(g.vulkanMissionHudDescriptor[slot]));
        captureLogged[slot]=true;
    }
    return true;
#else
    // Save the eye target before resource creation binds the offscreen FBO.
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&g.radarSavedFramebuffer);
    glGetIntegerv(GL_VIEWPORT,g.radarSavedViewport);
    glGetIntegerv(GL_SCISSOR_BOX,g.radarSavedScissor);
    g.radarSavedScissorEnabled=glIsEnabled(GL_SCISSOR_TEST)==GL_TRUE;
    g.radarSavedDepthEnabled=glIsEnabled(GL_DEPTH_TEST)==GL_TRUE;
    g.radarSavedCullEnabled=glIsEnabled(GL_CULL_FACE)==GL_TRUE;
    glGetBooleanv(GL_COLOR_WRITEMASK,g.radarSavedColourMask);
    if(!g.missionHudTexture[slot])
    {
        glGenTextures(1,&g.missionHudTexture[slot]);
        glBindTexture(GL_TEXTURE_2D,g.missionHudTexture[slot]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        const int textureWidth=MissionHudTextureWidth(slot);
        const int textureHeight=MissionHudTextureHeight(slot);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,textureWidth,
                     textureHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glGenFramebuffers(1,&g.missionHudFramebuffer[slot]);
        glBindFramebuffer(GL_FRAMEBUFFER,g.missionHudFramebuffer[slot]);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D,g.missionHudTexture[slot],0);
    }
    if(!g.missionHudFramebuffer[slot] || !g.missionHudTexture[slot])
        return false;

    // The P3D extents are authored coordinates. Final UVs are measured once
    // after FeScreen and GLES have applied their runtime matrices.
    SetMultiviewTargetActive(false);
    g.missionHudActiveSlot=(int)slot;
    g.hudCaptureFramebuffer=g.missionHudFramebuffer[slot];
    glBindFramebuffer(GL_FRAMEBUFFER,g.hudCaptureFramebuffer);
    const int textureWidth=MissionHudTextureWidth(slot);
    const int textureHeight=MissionHudTextureHeight(slot);
    glViewport(0,0,textureWidth,textureHeight);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
    g.radarRendering=true;
    g.missionHudVisible[slot]=true;
    g.missionHudCaptureFrame[slot]=g.hudFrameSerial;
    return true;
#endif
}

void EndMissionHudCapture()
{
    if(g.missionHudActiveSlot<0) return;
    const int slot=g.missionHudActiveSlot;
#if defined(SRR2_VR_RENDERER_VULKAN)
    g.radarRendering=false;
    g.vulkanCaptureActive=false;
    if(gVulkanContext.EndOffscreenTarget(g.vulkanMissionHudImage[slot]))
        g.vulkanMissionHudInitialized[slot]=true;
    g.missionHudActiveSlot=-1;
    return;
#else
    const int textureWidth=MissionHudTextureWidth((unsigned)slot);
    const int textureHeight=MissionHudTextureHeight((unsigned)slot);
    if(!g.missionHudCropValid[slot])
    {
        std::vector<unsigned char> pixels(
            textureWidth*textureHeight*4);
        glReadPixels(0,0,textureWidth,textureHeight,
                     GL_RGBA,GL_UNSIGNED_BYTE,&pixels[0]);
        int minX=textureWidth,minY=textureHeight,maxX=-1,maxY=-1;
        for(int y=0;y<textureHeight;++y)
        for(int x=0;x<textureWidth;++x)
        {
            const unsigned char* p=&pixels[(y*textureWidth+x)*4];
            if(p[0]||p[1]||p[2]||p[3])
            {minX=std::min(minX,x);minY=std::min(minY,y);
             maxX=std::max(maxX,x);maxY=std::max(maxY,y);}
        }
        if(maxX>=minX&&maxY>=minY)
        {
            const int margin=slot==4?12:4;
            minX=std::max(0,minX-margin);minY=std::max(0,minY-margin);
            maxX=std::min(textureWidth-1,maxX+margin);
            maxY=std::min(textureHeight-1,maxY+margin);
            // A spinning coin can be almost edge-on during the first capture.
            // Cropping to that instantaneous silhouette cuts its sides off as
            // soon as it rotates face-on.  Its maximum silhouette is circular,
            // so reserve a square using the observed full coin height.
            if(slot==4)
            {
                const int centreX=(minX+maxX)/2;
                const int half=(maxY-minY+1)/2;
                minX=std::max(0,centreX-half);
                maxX=std::min(textureWidth-1,centreX+half);
            }
            else if(slot==3)
            {
                // Bitmap text may expose only its final glyph during the
                // first transition frame.  Reserve room to its left for the
                // complete counter instead of permanently cropping to that
                // one glyph until the application is restarted.
                const int height=maxY-minY+1;
                const int minimumWidth=height*4;
                minX=std::max(0,std::min(minX,maxX-minimumWidth+1));
            }
            g.missionHudUv[slot][0]=minX/(float)textureWidth;
            g.missionHudUv[slot][1]=minY/(float)textureHeight;
            g.missionHudUv[slot][2]=(maxX+1)/(float)textureWidth;
            g.missionHudUv[slot][3]=(maxY+1)/(float)textureHeight;
            g.missionHudAspect[slot]=(maxX-minX+1)/(float)(maxY-minY+1);
            g.missionHudCropValid[slot]=true;
            SDL_Log("VR mission HUD slot=%d pixels=%d,%d..%d,%d uv=%.4f,%.4f..%.4f,%.4f",
                    slot,minX,minY,maxX,maxY,g.missionHudUv[slot][0],
                    g.missionHudUv[slot][1],g.missionHudUv[slot][2],
                    g.missionHudUv[slot][3]);
        }
    }
    g.radarRendering=false;
    g.hudCaptureFramebuffer=0;
    g.missionHudActiveSlot=-1;
    glBindFramebuffer(GL_FRAMEBUFFER,g.radarSavedFramebuffer);
    SetMultiviewTargetActive(true);
    glViewport(g.radarSavedViewport[0],g.radarSavedViewport[1],
               g.radarSavedViewport[2],g.radarSavedViewport[3]);
    glScissor(g.radarSavedScissor[0],g.radarSavedScissor[1],
              g.radarSavedScissor[2],g.radarSavedScissor[3]);
    if(g.radarSavedScissorEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if(g.radarSavedDepthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if(g.radarSavedCullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glColorMask(g.radarSavedColourMask[0],g.radarSavedColourMask[1],
                g.radarSavedColourMask[2],g.radarSavedColourMask[3]);
#endif
}

void UpdateMissionHudLayout(unsigned slot,const rmt::Matrix& layout)
{
    if(slot>=State::MISSION_HUD_COUNT) return;
    if(!g.missionHudLayoutValid[slot] ||
       std::memcmp(&g.missionHudLayout[slot],&layout,sizeof(layout))!=0)
    {
        g.missionHudLayout[slot]=layout;
        g.missionHudLayoutValid[slot]=true;
        // The layout matrix places the already captured HUD plane in VR; it
        // does not change the drawable's pixel bounds inside its offscreen
        // texture.  Animated groups commonly change this matrix every frame.
        // Invalidating the crop here therefore forced EndMissionHudCapture to
        // glReadPixels the complete texture every frame, synchronizing CPU and
        // GPU for roughly 10-13 ms on Quest.  Pixel bounds are invalidated by
        // an actual authored rectangle change in BeginMissionHudCapture and
        // by ResetMissionHudSlot instead.
    }
}

void ResetMissionHudSlot(unsigned slot)
{
    if(slot>=State::MISSION_HUD_COUNT) return;
    g.missionHudVisible[slot]=false;
    g.missionHudCropValid[slot]=false;
    g.missionHudAspect[slot]=0.0f;
    g.missionHudLayoutValid[slot]=false;
    g.missionHudCaptureFrame[slot]=0;
    std::memset(g.missionHudUv[slot],0,sizeof(g.missionHudUv[slot]));
    std::memset(g.missionHudRect[slot],0,sizeof(g.missionHudRect[slot]));
}

void CaptureSpatialCoinIcon()
{
#if !defined(SRR2_VR_RENDERER_VULKAN)
    if(
       g.missionHudTexture[4] &&
       g.missionHudCropValid[4])
    {
        g.missionHudVisible[4]=true;
        return;
    }
#endif
    // Use the original Pure3D coin, but submit it from one centralized point.
    // A second capture from FeGroup and a forced inverted cull mode were added
    // together and caused the shared world material to remain on its legacy
    // GLES program. Both have been removed.
    p3d::pddi->PushState(PDDI_STATE_ALL);
    // HUDRender(true) centres a roughly 0.2-wide orthographic coin. Capture
    // that tight region instead of a full 640x480 transparent canvas.
    if(BeginMissionHudCapture(4,256,176,384,304))
    {
        const pddiProjectionMode mode=p3d::pddi->GetProjectionMode();
        p3d::pddi->SetProjectionMode(mode);
        GetCoinManager()->HUDRender(true);
        EndMissionHudCapture();
    }
    p3d::pddi->PopState(PDDI_STATE_ALL);
}

void SetSpatialCoinAuthoredPosition(int x,int y,bool visible)
{
    g.spatialCoinAuthoredX=x;
    g.spatialCoinAuthoredY=y;
    g.spatialCoinAuthoredPositionValid=visible;
}

void SetMissionObjectiveFrameRect(int xMin,int yMin,int xMax,int yMax)
{
    if(xMax<=xMin || yMax<=yMin) return;
    g.missionObjectiveFrameRect[0]=xMin;
    g.missionObjectiveFrameRect[1]=yMin;
    g.missionObjectiveFrameRect[2]=xMax;
    g.missionObjectiveFrameRect[3]=yMax;
    g.missionObjectiveFrameRectValid=true;
}

void SetMissionObjectiveIconRect(int xMin,int yMin,int xMax,int yMax)
{
    if(xMax<=xMin || yMax<=yMin) return;
    g.missionObjectiveIconRect[0]=xMin;
    g.missionObjectiveIconRect[1]=yMin;
    g.missionObjectiveIconRect[2]=xMax;
    g.missionObjectiveIconRect[3]=yMax;
    g.missionObjectiveIconRectValid=true;
}

void SetRadarAuthoredRect(int xMin,int yMin,int xMax,int yMax)
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    if(xMax<=xMin||yMax<=yMin) return;
    g.radarRect[0]=xMin;g.radarRect[1]=yMin;
    g.radarRect[2]=xMax;g.radarRect[3]=yMax;
    g.radarCropValid=true;
    XRLOG("Vulkan radar authored rect=%d,%d..%d,%d",xMin,yMin,xMax,yMax);
#else
    (void)xMin;(void)yMin;(void)xMax;(void)yMax;
#endif
}

static void DrawRadarPlane()
{
    // While the authored full-page HUD is active, its separately captured
    // Map0 must use the same head-canvas placement in Original and VR modes.
    // Keep the spatial anchor path intact for the future independent HUD
    // switch, where the full-page capture is not used.
#if defined(SRR2_VR_RENDERER_VULKAN)
    const bool spatial=IsSpatialHudEnabled();
#else
    const bool spatial=IsSpatialHudEnabled();
#endif
    if(!g.activeEye || (spatial && !g.cullingBaseValid) || !g.radarCropValid ||
       g.radarCaptureFrame!=g.hudFrameSerial ||
       (spatial && g.missionHudCaptureFrame[13]!=g.hudFrameSerial) ||
#if defined(SRR2_VR_RENDERER_VULKAN)
       !g.vulkanRadarImage || !g.vulkanRadarDescriptor) return;
#else
       !g.radarTexture || !g.radarProgram) return;
#endif

    Eye& eye=g.eyes[g.activeEye-1];
    float vertices[24];
    if(!spatial)
    {
        // Original HUD: keep the complete captured HudMap0 group as one
        // object, but place it on the same physical 640x480 head canvas as
        // every independent HUD element. Passing it through each eye's view
        // restores stereo while retaining exact authored placement.
        const float margin=4.0f;
        const float x0=std::max(0.0f,g.radarRect[0]-margin);
        const float y0=std::max(0.0f,g.radarRect[1]-margin);
        const float x1=std::min(640.0f,g.radarRect[2]+margin);
        const float y1=std::min(480.0f,g.radarRect[3]+margin);
        // Match the full 0.72 m / 480 px authored HUD canvas exactly.
        const float metresPerPixel=0.0015f;
        const float left=(x0-320.0f)*metresPerPixel;
        const float right=(x1-320.0f)*metresPerPixel;
        // DrawVulkanHudQuad flips the full offscreen HUD texture vertically.
        // Mirror Map0's geometric canvas Y as well, otherwise its source UV
        // is upright but its plane is placed above the frame rendered below.
        const float bottom=(y0-240.0f)*metresPerPixel;
        const float top=(y1-240.0f)*metresPerPixel;
        rmt::Matrix headCamera=g.cullingBaseCamera;
        GetLatestCullingCamera(&headCamera);
        rmt::Matrix anchor=headCamera;
        anchor.Row(3)=headCamera.Row(3)+headCamera.Row(2)*0.90f-
                      headCamera.Row(1)*0.18f;
        const rmt::Matrix eyeLocal=PoseToGame(RelativePose(g.origin,eye.view.pose));
        rmt::Matrix eyeWorld,worldToEye,anchorToEye,projection,mvp;
        eyeWorld.Mult(eyeLocal,g.cullingBaseCamera);
        worldToEye.InvertOrtho(eyeWorld);anchorToEye.Mult(anchor,worldToEye);
        MakeProjection(eye.view.fov,0.05f,1000.0f,&projection);
        mvp.MultFull(anchorToEye,projection);
        const float xy[4][2]={{left,bottom},{right,bottom},{left,top},{right,top}};
        rmt::Vector4 projected[4];
        for(unsigned i=0;i<4;++i)
        {
            projected[i].Set(xy[i][0],xy[i][1],0.0f,1.0f);
            projected[i].Transform(mvp);
        }
        const float unified[24]={
            projected[0].x,projected[0].y,projected[0].z,projected[0].w,g.radarUv[0],g.radarUv[1],
            projected[1].x,projected[1].y,projected[1].z,projected[1].w,g.radarUv[2],g.radarUv[1],
            projected[2].x,projected[2].y,projected[2].z,projected[2].w,g.radarUv[0],g.radarUv[3],
            projected[3].x,projected[3].y,projected[3].z,projected[3].w,g.radarUv[2],g.radarUv[3]};
        std::memcpy(vertices,unified,sizeof(vertices));
    }
    else
    {
    rmt::Matrix anchor;
    Character* player=GetCharacterManager()->GetCharacter(0);
    const bool fixedToVehicle=player&&player->IsInCar()&&
        !IsThirdPersonVehicleMode();
    rmt::Vector handPosition;
    rmt::Matrix handWorld;
    if(fixedToVehicle){anchor.Identity();anchor.Row(3).Set(0.30f,kVrWheelCentre.y,0.54f);
        const rmt::Matrix local=anchor;anchor.Mult(local,g.cullingBaseCamera);}
    else{if(!g.handPoseValid[1]) return;
        rmt::Matrix hand=PoseToGame(RelativePose(g.origin,g.handPoses[1]));
        handWorld.Mult(hand,g.cullingBaseCamera);
        handPosition=handWorld.Row(3);anchor=handWorld;}
    const rmt::Matrix eyeLocal=PoseToGame(RelativePose(g.origin,eye.view.pose));
    rmt::Matrix eyeWorld,worldToEye,anchorToEye,proj,mvp;
    eyeWorld.Mult(eyeLocal,g.cullingBaseCamera);
    if(fixedToVehicle)
    {
        const rmt::Vector position=anchor.Row(3);
        rmt::Vector forward=g.cullingBaseCamera.Row(2);
        forward.y=0.0f;
        if(forward.NormalizeSafe()<0.0001f) forward.Set(0.0f,0.0f,1.0f);
        rmt::Vector right(forward.z,0.0f,-forward.x);
        right.NormalizeSafe();
        anchor.Identity();
        anchor.Row(0)=right;
        anchor.Row(1).Set(0.0f,1.0f,0.0f);
        anchor.Row(2)=forward;
        anchor.Row(3)=position;
    }
    if(!fixedToVehicle){const rmt::Vector towardElbow=handWorld.Row(1)*0.10f;
        // Keep the tracked attachment point at the geometric centre of the
        // quad. An extra vertical offset made the map orbit around its lower
        // edge when the controller was rotated.
        const rmt::Vector position=handPosition+towardElbow;
        anchor.Row(0)=eyeWorld.Row(0);anchor.Row(1)=eyeWorld.Row(1);
        anchor.Row(2)=eyeWorld.Row(2);anchor.Row(3)=position;}
    worldToEye.InvertOrtho(eyeWorld);
    anchorToEye.Mult(anchor,worldToEye); MakeProjection(eye.view.fov,0.05f,1000.0f,&proj);
    mvp.MultFull(anchorToEye,proj);
    // Map0 and the authored HudMap0 bezel are both 0.115 m high. The previous
    // vehicle branch doubled only Map0 to 0.23 m, making roads escape the
    // frame and exaggerating apparent rotation while looking around.
    const float handScale=fixedToVehicle?1.0f:0.75f;
    const float halfSize=(g.radarUv[3]-g.radarUv[1])*480.0f*
                         0.0005f*handScale;
    const float xy[4][2]={{-halfSize,-halfSize},{halfSize,-halfSize},
                          {-halfSize,halfSize},{halfSize,halfSize}};
    const float uv[4][2]={{g.radarUv[0],g.radarUv[1]},{g.radarUv[2],g.radarUv[1]},
                          {g.radarUv[0],g.radarUv[3]},{g.radarUv[2],g.radarUv[3]}};
    for(int i=0;i<4;++i){
        rmt::Vector4 p(xy[i][0],xy[i][1],0.0f,1.0f);
        p.Transform(mvp);
        vertices[i*6]=p.x;vertices[i*6+1]=p.y;vertices[i*6+2]=p.z;
        vertices[i*6+3]=p.w;vertices[i*6+4]=uv[i][0];vertices[i*6+5]=uv[i][1];}
    }
#if defined(SRR2_VR_RENDERER_VULKAN)
    for(unsigned i=0;i<4;++i)
    {
        // Never perspective-divide a plane crossing/behind the eye. Let it
        // disappear outside the field of view instead of exploding into a
        // full-screen stretched triangle as W approaches zero.
        if(vertices[i*6+3]<=0.05f || !std::isfinite(vertices[i*6+3])) return;
    }
    struct QuadVertex
    {
        float position[3],normal[3],uv[2];
        uint32_t colour;
        float uv1[2],uv2[2];
        float tangent[3];
        uint32_t skin[4];
    } quad[32*3]={};
    static_assert(sizeof(QuadVertex)==80,"Vulkan HUD vertex layout must match PDDI");
    for(unsigned i=0;i<4;++i)
    {
        const float inverseW=vertices[i*6+3]!=0.0f?1.0f/vertices[i*6+3]:1.0f;
        quad[i].position[0]=vertices[i*6]*inverseW;
        quad[i].position[1]=vertices[i*6+1]*inverseW;
        quad[i].position[2]=vertices[i*6+2]*inverseW;
        quad[i].normal[2]=1.0f;
        quad[i].uv[0]=vertices[i*6+4];
        // Flip within the selected crop, not around the complete texture.
        // 1-V moves an off-centre HUD crop into an unrelated transparent
        // region; exchanging its two rows preserves the crop and corrects
        // render-target orientation.
        const unsigned oppositeRow=i<2?i+2:i-2;
        quad[i].uv[1]=vertices[oppositeRow*6+5];
        quad[i].colour=0xffffffffu;
    }
    // Hole0 clips Map0 through depth in the original renderer. The Vulkan
    // capture retains colour only, so rebuild that circular boundary here.
    const unsigned radarSegments=32;
    const QuadVertex corners[4]={quad[0],quad[1],quad[2],quad[3]};
    const float cx=(corners[0].position[0]+corners[3].position[0])*0.5f;
    const float cy=(corners[0].position[1]+corners[3].position[1])*0.5f;
    // Derive the opening from Map0's real runtime rectangle instead of an
    // approximate inset. radarUv spans Radar0 plus its filter margin, while
    // radarMapRect is the exact 3D viewport that the bezel must expose.
    const float cropWidth=std::max(1.0f,
        (g.radarUv[2]-g.radarUv[0])*640.0f);
    const float cropHeight=std::max(1.0f,
        (g.radarUv[3]-g.radarUv[1])*480.0f);
    const float mapWidth=std::max(1,g.radarMapRect[2]-g.radarMapRect[0]);
    const float mapHeight=std::max(1,g.radarMapRect[3]-g.radarMapRect[1]);
    // Keep the filtered edge fully underneath the opaque inner lip of the
    // authored bezel. Six source pixels cover bilinear filtering plus the
    // antialiased frame edge; deriving the scale from Map0's real rectangle
    // keeps the inset resolution-independent.
    const float maskScaleX=std::min(1.0f,std::max(1.0f,mapWidth-12.0f)/cropWidth);
    const float maskScaleY=std::min(1.0f,std::max(1.0f,mapHeight-12.0f)/cropHeight);
    const float rx=(corners[3].position[0]-corners[0].position[0])*0.5f*maskScaleX;
    const float ry=(corners[3].position[1]-corners[0].position[1])*0.5f*maskScaleY;
    const float cu=(g.radarUv[0]+g.radarUv[2])*0.5f;
    const float cv=(g.radarUv[1]+g.radarUv[3])*0.5f;
    const float ru=(g.radarUv[2]-g.radarUv[0])*0.5f*maskScaleX;
    const float rv=(g.radarUv[3]-g.radarUv[1])*0.5f*maskScaleY;
    for(unsigned segment=0;segment<radarSegments;++segment)
    {
        const float a0=-rmt::PI_BY2+segment*(rmt::PI*2.0f/radarSegments);
        const float a1=-rmt::PI_BY2+(segment+1)*(rmt::PI*2.0f/radarSegments);
        const float positions[3][2]={{cx,cy},{cx+rmt::Cos(a0)*rx,cy+rmt::Sin(a0)*ry},
            {cx+rmt::Cos(a1)*rx,cy+rmt::Sin(a1)*ry}};
        const float texcoords[3][2]={{cu,cv},{cu+rmt::Cos(a0)*ru,cv-rmt::Sin(a0)*rv},
            {cu+rmt::Cos(a1)*ru,cv-rmt::Sin(a1)*rv}};
        for(unsigned point=0;point<3;++point)
        {
            QuadVertex& vertex=quad[segment*3+point];
            vertex=corners[0];
            vertex.position[0]=positions[point][0];
            vertex.position[1]=positions[point][1];
            vertex.uv[0]=texcoords[point][0];
            vertex.uv[1]=texcoords[point][1];
        }
    }
    VkBuffer buffer=VK_NULL_HANDLE; VkDeviceSize offset=0;
    if(!gVulkanContext.UploadTransientVertices(quad,sizeof(quad),&buffer,&offset)) return;
    float identity[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    SharOpenXR::VulkanMaterialState material={};
    material.hudPass=true;
    material.blendMode=PDDI_BLEND_ALPHA; material.twoSided=true;
    material.alphaRef=0.5f; material.alphaCompare=PDDI_COMPARE_ALWAYS;
    for(unsigned i=0;i<4;++i) material.colour[i]=1.0f;
    material.ambientTerm[0]=material.ambientTerm[1]=
        material.ambientTerm[2]=1.0f;
    material.cullMode=VK_CULL_MODE_NONE;
    material.colourWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
        VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    material.depthCompare=VK_COMPARE_OP_ALWAYS;
    material.scissorWidth=material.scissorSurfaceWidth=eye.width;
    material.scissorHeight=material.scissorSurfaceHeight=eye.height;
    material.viewportWidth=material.viewportHeight=1.0f;
    material.stencilCompare=VK_COMPARE_OP_ALWAYS;
    material.stencilFail=material.stencilDepthFail=material.stencilPass=VK_STENCIL_OP_KEEP;
    material.stencilCompareMask=material.stencilWriteMask=0xff;
    material.fogEnd=1.0f;
    material.fogColour[3]=1.0f;
    const uint32_t imageIndex=g.multiviewImageIndex;
    if(imageIndex<g.eyes[0].vulkanImages.size())
        gVulkanContext.DrawPddiGeometry(g.eyes[0].vulkanImages[imageIndex].image,
            VulkanRenderFormat(g.eyes[0].vulkanFormat),eye.width,eye.height,g.activeEye-1,
            buffer,VK_NULL_HANDLE,offset,radarSegments*3,0,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            identity,identity,g.vulkanRadarDescriptor,VK_NULL_HANDLE,
            VK_NULL_HANDLE,VK_NULL_HANDLE,material);
    return;
#endif
    static bool planeLogged[2]={false,false};
    const unsigned radarEyeIndex=g.activeEye-1;
    if(!planeLogged[radarEyeIndex])
    {
        Character* player=GetCharacterManager()->GetCharacter(0);
        SDL_Log("VR radar plane eye=%u: clip0=%.3f,%.3f,%.3f,%.3f clip3=%.3f,%.3f,%.3f,%.3f car=%d hand=%d",
                radarEyeIndex,
                vertices[0],vertices[1],vertices[2],vertices[3],
                vertices[18],vertices[19],vertices[20],vertices[21],
                player&&player->IsInCar()?1:0,g.handPoseValid[1]?1:0);
        planeLogged[radarEyeIndex]=true;
    }
    GLint oldProgram=0,oldArray=0,oldTexture=0,oldActive=0; glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram); glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldArray); glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive); glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);
    const GLboolean depth=glIsEnabled(GL_DEPTH_TEST),blend=glIsEnabled(GL_BLEND),cull=glIsEnabled(GL_CULL_FACE),scissor=glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g.radarProgram); glBindBuffer(GL_ARRAY_BUFFER,g.hudQuadVbo); glBindTexture(GL_TEXTURE_2D,g.radarTexture); glUniform1i(glGetUniformLocation(g.radarProgram,"tex"),0);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);
    GLint pa=glGetAttribLocation(g.radarProgram,"position"),ta=glGetAttribLocation(g.radarProgram,"texcoord"); glEnableVertexAttribArray(pa);glEnableVertexAttribArray(ta);glVertexAttribPointer(pa,4,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<const void*>(0));glVertexAttribPointer(ta,2,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<const void*>(4*sizeof(float)));glDrawArrays(GL_TRIANGLE_STRIP,0,4);glDisableVertexAttribArray(pa);glDisableVertexAttribArray(ta);
    glBindTexture(GL_TEXTURE_2D,oldTexture); glActiveTexture(oldActive); glBindBuffer(GL_ARRAY_BUFFER,oldArray); glUseProgram(oldProgram); if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
}

#if defined(SRR2_VR_RENDERER_VULKAN)
static void DrawVulkanHudQuad(VkDescriptorSet texture,const float* vertices,Eye& eye,
                              float opacity)
{
    for(unsigned i=0;i<4;++i)
        if(vertices[i*6+3]<=0.05f || !std::isfinite(vertices[i*6+3])) return;
    // The PDDI HUD pipeline accepts vec3 positions, so clip-space W cannot be
    // passed to the shader. A single CPU-divided quad consequently uses affine
    // UV interpolation and visibly bends/shears when a car-fixed panel is seen
    // obliquely. Subdivide before the divide: every small cell follows the true
    // projective surface while the panel itself remains rigidly car-locked.
    struct QuadVertex {float position[3],normal[3],uv[2];uint32_t colour;
        float uv1[2],uv2[2],tangent[3];uint32_t skin[4];};
    static_assert(sizeof(QuadVertex)==80,"Vulkan HUD vertex layout must match PDDI");
    static const unsigned divisions=8;
    QuadVertex quad[divisions*divisions*6]={};
    unsigned vertexCount=0;
    const uint32_t alpha=static_cast<uint32_t>(
        std::max(0.0f,std::min(opacity,1.0f))*255.0f+0.5f);
    for(unsigned y=0;y<divisions;++y)
    {
        for(unsigned x=0;x<divisions;++x)
        {
            const float x0=static_cast<float>(x)/divisions;
            const float x1=static_cast<float>(x+1)/divisions;
            const float y0=static_cast<float>(y)/divisions;
            const float y1=static_cast<float>(y+1)/divisions;
            const float points[6][2]={{x0,y0},{x1,y0},{x0,y1},
                                      {x1,y0},{x1,y1},{x0,y1}};
            for(unsigned point=0;point<6;++point)
            {
                const float u=points[point][0],v=points[point][1];
                float clip[4];
                for(unsigned component=0;component<4;++component)
                {
                    const float bottom=vertices[component]*(1.0f-u)+
                                       vertices[6+component]*u;
                    const float top=vertices[12+component]*(1.0f-u)+
                                    vertices[18+component]*u;
                    clip[component]=bottom*(1.0f-v)+top*v;
                }
                QuadVertex& out=quad[vertexCount++];
                const float inverseW=1.0f/clip[3];
                out.position[0]=clip[0]*inverseW;
                out.position[1]=clip[1]*inverseW;
                out.position[2]=clip[2]*inverseW;
                out.normal[2]=1.0f;
                const float bottomU=vertices[4]*(1.0f-u)+vertices[10]*u;
                const float topU=vertices[16]*(1.0f-u)+vertices[22]*u;
                out.uv[0]=bottomU*(1.0f-v)+topU*v;
                // Captured HUD images use the opposite Vulkan framebuffer Y.
                const float bottomV=vertices[17]*(1.0f-u)+vertices[23]*u;
                const float topV=vertices[5]*(1.0f-u)+vertices[11]*u;
                out.uv[1]=bottomV*(1.0f-v)+topV*v;
                out.colour=(alpha<<24)|0x00ffffffu;
            }
        }
    }
    VkBuffer buffer=VK_NULL_HANDLE;VkDeviceSize offset=0;
    if(!gVulkanContext.UploadTransientVertices(quad,sizeof(quad),&buffer,&offset)) return;
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    SharOpenXR::VulkanMaterialState material={};
    material.hudPass=true;
    material.blendMode=PDDI_BLEND_ALPHA;material.twoSided=true;
    material.alphaCompare=PDDI_COMPARE_ALWAYS;
    for(unsigned i=0;i<4;++i)material.colour[i]=1.0f;
    material.ambientTerm[0]=material.ambientTerm[1]=material.ambientTerm[2]=1.0f;
    material.cullMode=VK_CULL_MODE_NONE;
    material.colourWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
        VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    material.depthCompare=VK_COMPARE_OP_ALWAYS;
    material.scissorWidth=material.scissorSurfaceWidth=eye.width;
    material.scissorHeight=material.scissorSurfaceHeight=eye.height;
    material.viewportWidth=material.viewportHeight=1.0f;
    material.stencilCompare=VK_COMPARE_OP_ALWAYS;
    material.stencilFail=material.stencilDepthFail=material.stencilPass=VK_STENCIL_OP_KEEP;
    material.stencilCompareMask=material.stencilWriteMask=0xff;
    material.fogEnd=1.0f;material.fogColour[3]=1.0f;
    const uint32_t imageIndex=g.multiviewImageIndex;
    if(imageIndex<g.eyes[0].vulkanImages.size())
        gVulkanContext.DrawPddiGeometry(g.eyes[0].vulkanImages[imageIndex].image,
            VulkanRenderFormat(g.eyes[0].vulkanFormat),eye.width,eye.height,g.activeEye-1,buffer,
            VK_NULL_HANDLE,offset,vertexCount,0,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            identity,identity,texture,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,material);
}

static void DrawMissionHudQuad(VkDescriptorSet texture,const float* uv,
                               const rmt::Matrix& anchor,float height,
                               float aspect,Eye& eye,float localYOffset=0.0f);

static void DrawGameplayHud()
{
    if(!g.activeEye) return;
    static int previousSpatial=-1;
    const bool spatial=IsSpatialHudEnabled();
    if(previousSpatial!=(spatial?1:0))
    {
        XRLOG("HUD presentation active: %s (gameplay=%s)",
              spatial?"wrist":"screen",g.vrModeEnabled?"VR":"Original");
        previousSpatial=spatial?1:0;
    }
    static unsigned hudCompositeCalls=0;
    if((++hudCompositeCalls%180)==0)
        XRLOG("HUD composite: calls=%u eye=%u mode=%s culling=%d imageIndex=%u pddiEye=%d",
            hudCompositeCalls,g.activeEye,g.vrModeEnabled?"VR":"Original",
            g.cullingBaseValid?1:0,g.multiviewImageIndex,
            gVulkanContext.IsInitialized()?1:0);
    if(!spatial && g.vulkanGameplayHudInitialized &&
       g.vulkanGameplayHudDescriptor &&
       g.cullingBaseValid)
    {
        // Map0 is the background of Radar0. Draw it first, then composite the
        // complete authored HUD so its frame and markers cover the map edge.
        DrawRadarPlane();
        rmt::Matrix anchor=g.cullingBaseCamera;
        GetLatestCullingCamera(&anchor);
        anchor.Row(3)=anchor.Row(3)+anchor.Row(2)*0.90f-
                      anchor.Row(1)*0.18f;
        const float uv[4]={0.0f,0.0f,1.0f,1.0f};
        DrawMissionHudQuad(g.vulkanGameplayHudDescriptor,uv,anchor,
                           0.72f,4.0f/3.0f,g.eyes[g.activeEye-1]);
        return;
    }
    DrawRadarPlane();
    DrawMissionHudPlanes();
}

static void DrawMissionHudQuad(VkDescriptorSet texture,const float* uv,
#else
static void DrawGameplayHud()
{
    DrawRadarPlane();
    DrawMissionHudPlanes();
}

static void DrawMissionHudQuad(GLuint texture,const float* uv,
#endif
                               const rmt::Matrix& anchor,float height,
                               float aspect,Eye& eye,float localYOffset)
{
    const rmt::Matrix eyeLocal=PoseToGame(RelativePose(g.origin,eye.view.pose));
    rmt::Matrix eyeWorld,worldToEye,anchorToEye,proj,mvp;
    eyeWorld.Mult(eyeLocal,g.cullingBaseCamera); worldToEye.InvertOrtho(eyeWorld);
    anchorToEye.Mult(anchor,worldToEye); MakeProjection(eye.view.fov,0.05f,1000.0f,&proj);
    mvp.MultFull(anchorToEye,proj);
    aspect=std::max(0.01f,aspect);
    const float halfY=height*0.5f,halfX=halfY*aspect;
    const float xy[4][2]={{-halfX,-halfY+localYOffset},
                          { halfX,-halfY+localYOffset},
                          {-halfX, halfY+localYOffset},
                          { halfX, halfY+localYOffset}};
    const float tc[4][2]={{uv[0],uv[1]},{uv[2],uv[1]},{uv[0],uv[3]},{uv[2],uv[3]}};
    float vertices[24];
    for(int i=0;i<4;++i){
        rmt::Vector4 p(xy[i][0],xy[i][1],0.0f,1.0f);
        p.Transform(mvp);
        vertices[i*6]=p.x;vertices[i*6+1]=p.y;vertices[i*6+2]=p.z;vertices[i*6+3]=p.w;
        vertices[i*6+4]=tc[i][0];vertices[i*6+5]=tc[i][1];}
#if defined(SRR2_VR_RENDERER_VULKAN)
    DrawVulkanHudQuad(texture,vertices,eye,1.0f);
#else
    glBindTexture(GL_TEXTURE_2D,texture);
    glBindBuffer(GL_ARRAY_BUFFER,g.hudQuadVbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);
    const GLint pa=glGetAttribLocation(g.radarProgram,"position");
    const GLint ta=glGetAttribLocation(g.radarProgram,"texcoord");
    glEnableVertexAttribArray(pa);glEnableVertexAttribArray(ta);
    glVertexAttribPointer(pa,4,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<const void*>(0));
    glVertexAttribPointer(ta,2,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<const void*>(4*sizeof(float)));
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    glDisableVertexAttribArray(pa);glDisableVertexAttribArray(ta);
#endif
}

static void DrawMissionHudPlanes()
{
    if(!g.activeEye||!g.cullingBaseValid
#if !defined(SRR2_VR_RENDERER_VULKAN)
       ||!g.radarProgram
#endif
       ) return;
    bool anyVisible=false;
    for(unsigned slot=0;slot<State::MISSION_HUD_COUNT;++slot)
        anyVisible=anyVisible||g.missionHudVisible[slot];
    if(!anyVisible) return;
#if defined(SRR2_VR_RENDERER_VULKAN)
    static bool compositeLogged[2]={false,false};
    if(!compositeLogged[g.activeEye-1])
    {
        unsigned visibleCount=0,readyCount=0;
        for(unsigned slot=0;slot<State::MISSION_HUD_COUNT;++slot)
        {
            if(g.missionHudVisible[slot]) ++visibleCount;
            if(g.vulkanMissionHudDescriptor[slot] && g.missionHudCropValid[slot]) ++readyCount;
        }
        XRLOG("Vulkan HUD composite eye=%u visible=%u ready=%u culling=%d",
              g.activeEye-1,visibleCount,readyCount,g.cullingBaseValid?1:0);
        compositeLogged[g.activeEye-1]=true;
    }
#endif
    Eye& eye=g.eyes[g.activeEye-1];
    const rmt::Matrix eyeLocal=PoseToGame(RelativePose(g.origin,eye.view.pose));
    rmt::Matrix eyeWorld;eyeWorld.Mult(eyeLocal,g.cullingBaseCamera);
    Character* player=GetCharacterManager()->GetCharacter(0);
    const bool fixedToVehicle=player&&player->IsInCar()&&
        !IsThirdPersonVehicleMode();
    rmt::Matrix base;
    rmt::Vector objectiveHandPoint(0.0f,0.0f,0.0f);
    bool objectiveHandPointValid=false;
    if(fixedToVehicle){
        rmt::Vector wheelCentre=g.activeWheelCentre;
        if(wheelCentre.MagnitudeSqr()<0.0001f) wheelCentre=kVrWheelCentre;
        base.Identity();base.Row(3)=wheelCentre;
        const rmt::Matrix local=base;base.Mult(local,g.cullingBaseCamera);}
    else{
        base.Identity();base.Row(0)=eyeWorld.Row(0);base.Row(1)=eyeWorld.Row(1);
        base.Row(2)=eyeWorld.Row(2);
        if(g.handPoseValid[0])
        {
            const rmt::Matrix hand=PoseToGame(RelativePose(g.origin,g.handPoses[0]));
            rmt::Matrix handWorld;handWorld.Mult(hand,g.cullingBaseCamera);
            objectiveHandPoint=handWorld.Row(3);
            objectiveHandPointValid=true;
            const rmt::Vector towardElbow=handWorld.Row(1)*0.10f;
            base.Row(3)=handWorld.Row(3)+towardElbow;
        }
        else
        {
            // Central notifications must remain visible even while controller
            // tracking is unavailable. Wrist-bound items will skip below.
            base.Row(3)=g.cullingBaseCamera.Row(3)+
                g.cullingBaseCamera.Row(2)*0.85f;
        }}
#if !defined(SRR2_VR_RENDERER_VULKAN)
    GLint oldProgram=0,oldArray=0,oldTexture=0,oldActive=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldArray);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);
    const GLboolean depth=glIsEnabled(GL_DEPTH_TEST),blend=glIsEnabled(GL_BLEND);
    const GLboolean cull=glIsEnabled(GL_CULL_FACE),scissor=glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g.radarProgram);glBindBuffer(GL_ARRAY_BUFFER,0);
    glUniform1i(glGetUniformLocation(g.radarProgram,"tex"),0);
#endif
    const float hudPixelScale=fixedToVehicle?0.001f:0.00075f;
    const float hudGap=4.0f*hudPixelScale;
    // Visibility and texture freshness are different concerns. Mission
    // objective animations do not necessarily redraw their group every frame,
    // but the active icon still occupies layout space. Reserve its cached,
    // measured rectangle while only drawing textures stamped this frame.
    // Slot 1 is the authored MessageBox plus objective text; slot 0 is the
    // objective icon that belongs on top of the leading edge of that box.
    const bool objectiveOccupiesLayout=g.missionHudVisible[1] &&
        g.missionHudCropValid[1];
    const float objectiveHeight=objectiveOccupiesLayout?
        std::max(1.0f,(g.missionHudUv[1][3]-g.missionHudUv[1][1])*480.0f)*
        hudPixelScale:0.0f;
    float positiveEdge=objectiveHeight*0.5f+
        (objectiveOccupiesLayout?hudGap:0.0f);
    float negativeEdge=-objectiveHeight*0.5f-
        (objectiveOccupiesLayout?hudGap:0.0f);
    unsigned missionStackRow=0;
    for(unsigned drawIndex=0;drawIndex<State::MISSION_HUD_COUNT;++drawIndex){
        // Draw the message/frame first and its icon second so the icon is
        // composited over the beginning of the frame, matching the legacy HUD.
        const unsigned slot=drawIndex==0?1:(drawIndex==1?0:drawIndex);
        // Slot 4 is the persistent 3D icon belonging to counter slot 3.  Its
        // clean texture is refreshed independently, but it must follow the
        // counter's visibility (including the pause-menu counter).
        const unsigned sourceSlot=slot==4?3:slot;
        const bool visible=g.missionHudVisible[sourceSlot] &&
            g.missionHudCaptureFrame[sourceSlot]==g.hudFrameSerial;
        if(!visible||
#if defined(SRR2_VR_RENDERER_VULKAN)
           !g.vulkanMissionHudDescriptor[slot]||
#else
           !g.missionHudTexture[slot]||
#endif
           !g.missionHudCropValid[slot]) continue;
        const float aspect=g.missionHudAspect[slot]>0.0f?
            g.missionHudAspect[slot]:1.0f;
        const float cropPixels=(g.missionHudUv[slot][3]-
                                g.missionHudUv[slot][1])*480.0f;
        // Timer groups have taller authored rectangles than the other numeric
        // mission overlays (the bitmap font leaves vertical layout room in
        // the group). Scaling those rectangles directly makes both the normal
        // mission timer and Par Time visibly larger than their neighbours.
        // Preserve the established VR numeric-overlay height used by the
        // original spatial HUD implementation, while retaining the measured
        // aspect so no glyph is stretched or clipped.
        const bool timerSlot=slot==2 || slot==6;
        const float drawHeight=timerSlot?0.0425f:
            std::max(1.0f,cropPixels)*hudPixelScale;
        rmt::Matrix anchor=base;
        const bool centreNotification=slot>=14 && slot<=18;
        if(centreNotification)
        {
            // Transient announcements belong in front of the player rather
            // than on either wrist.  Anchor them once in the game-camera
            // space; the two eye projections then provide real stereo.
            anchor.Identity();
            anchor.Row(0)=g.cullingBaseCamera.Row(0);
            anchor.Row(1)=g.cullingBaseCamera.Row(1);
            anchor.Row(2)=g.cullingBaseCamera.Row(2);
            anchor.Row(3)=g.cullingBaseCamera.Row(3)+
                g.cullingBaseCamera.Row(2)*0.85f;
        }
        if((slot==0 || slot==1) && g.missionObjectiveFrameRectValid)
        {
            const rmt::Vector frameAttachment=objectiveHandPointValid?
                objectiveHandPoint:base.Row(3);
            const float groupCentreX=0.5f*(g.missionHudRect[1][0]+
                                            g.missionHudRect[1][2]);
            const float groupCentreY=0.5f*(g.missionHudRect[1][1]+
                                            g.missionHudRect[1][3]);
            const float frameCentreX=0.5f*(g.missionObjectiveFrameRect[0]+
                                            g.missionObjectiveFrameRect[2]);
            const float frameCentreY=0.5f*(g.missionObjectiveFrameRect[1]+
                                            g.missionObjectiveFrameRect[3]);
            // The visible frame centre is the attachment point: raw hand on
            // foot, actual calibrated steering-wheel centre in the vehicle.
            anchor.Row(3)=frameAttachment-anchor.Row(0)*
                ((frameCentreX-groupCentreX)*hudPixelScale)+anchor.Row(1)*
                ((frameCentreY-groupCentreY)*hudPixelScale);
        }
        if(slot==5)
        {
            // Contextual action prompt: same right-hand attachment as the
            // radar, with a small gap above its upper edge.
            if(fixedToVehicle)
            {
                anchor.Identity();
                anchor.Row(3).Set(0.30f,kVrWheelCentre.y+0.15f,0.54f);
                const rmt::Matrix local=anchor;
                anchor.Mult(local,g.cullingBaseCamera);
            }
            else
            {
                if(!g.handPoseValid[1]) continue;
                const rmt::Matrix hand=PoseToGame(RelativePose(g.origin,g.handPoses[1]));
                rmt::Matrix handWorld;handWorld.Mult(hand,g.cullingBaseCamera);
                anchor.Identity();anchor.Row(0)=eyeWorld.Row(0);
                anchor.Row(1)=eyeWorld.Row(1);anchor.Row(2)=eyeWorld.Row(2);
                anchor.Row(3)=handWorld.Row(3)+handWorld.Row(1)*0.10f+
                              eyeWorld.Row(1)*0.10f;
            }
        }
        else if(slot==13)
        {
            if(fixedToVehicle)
            {
                anchor.Identity();
                anchor.Row(3).Set(0.30f,kVrWheelCentre.y,0.54f);
                const rmt::Matrix local=anchor;
                anchor.Mult(local,g.cullingBaseCamera);
            }
            else
            {
                if(!g.handPoseValid[1]) continue;
                const rmt::Matrix hand=PoseToGame(RelativePose(g.origin,g.handPoses[1]));
                rmt::Matrix handWorld;handWorld.Mult(hand,g.cullingBaseCamera);
                anchor.Identity();anchor.Row(0)=eyeWorld.Row(0);
                anchor.Row(1)=eyeWorld.Row(1);anchor.Row(2)=eyeWorld.Row(2);
                anchor.Row(3)=handWorld.Row(3)+handWorld.Row(1)*0.10f;
            }
        }
        else if(slot==3 || slot==4)
        {
            if(fixedToVehicle)
            {
                anchor.Identity();
                // The original coin readout belongs directly below the radar,
                // not on the opposite side of the dashboard.
                anchor.Row(3).Set(0.30f,kVrWheelCentre.y-0.17f,0.54f);
                const rmt::Matrix local=anchor;
                anchor.Mult(local,g.cullingBaseCamera);
            }
            else
            {
                if(!g.handPoseValid[1]) continue;
                const rmt::Matrix hand=PoseToGame(RelativePose(g.origin,g.handPoses[1]));
                rmt::Matrix handWorld;handWorld.Mult(hand,g.cullingBaseCamera);
                anchor.Identity();anchor.Row(0)=eyeWorld.Row(0);
                anchor.Row(1)=eyeWorld.Row(1);anchor.Row(2)=eyeWorld.Row(2);
                anchor.Row(3)=handWorld.Row(3)+handWorld.Row(1)*0.10f-
                              eyeWorld.Row(1)*0.105f;
            }
        }
        // Pack left-hand mission windows using their measured texture height.
        // The occupied edges are advanced after every visible item, so no
        // combination of timer/counters/meters can overlap another one.
        const bool missionStackSlot=slot==2 || slot==6 || slot==7 ||
            slot==8 || slot==9 || slot==10 || slot==11 || slot==12;
        float verticalOffset=0.0f;
        if(slot==0)
        {
            if(objectiveOccupiesLayout && g.missionObjectiveFrameRectValid)
            {
                // The icon's geometric centre belongs exactly on the frame's
                // left edge and at the frame's vertical centre. Do not derive
                // this from the aggregate MissionObjective/Message groups:
                // their text and animation bounds move their centres.
                const float frameLeft=static_cast<float>(
                    g.missionObjectiveFrameRect[0]);
                const float frameCentreX=0.5f*(g.missionObjectiveFrameRect[0]+
                                                g.missionObjectiveFrameRect[2]);
                const rmt::Vector frameAttachment=objectiveHandPointValid?
                    objectiveHandPoint:base.Row(3);
                // Align the centre of the already corrected/cropped icon quad
                // to the visible left cap of the frame. ObjectiveIcon's raw
                // sprite contains asymmetric transparent padding, so its
                // optical centre is 30 authored pixels to the right of the
                // captured quad centre.
                const float iconOpticalInsetPixels=30.0f;
                anchor.Row(3)=frameAttachment+anchor.Row(0)*
                    ((frameLeft-frameCentreX+iconOpticalInsetPixels)*
                     hudPixelScale);
                verticalOffset=0.0f;
            }
        }
        else if(slot==1)
        {
            // The objective frame is the origin of the mission HUD stack.
            verticalOffset=0.0f;
        }
        else if(missionStackSlot)
        {
            // All numeric mission overlays use one common baseline grid.
            // Deriving every next centre from the previous crop height made
            // timer/counter pairs drift apart as their bitmap bounds changed.
            const float rowStep=fixedToVehicle?0.060f:0.050f;
            verticalOffset=positiveEdge+rowStep*++missionStackRow;
            // Numeric overlays have different widths (for example 1/5 versus
            // 02:34). Centre anchoring therefore makes their left edges wander
            // and the stack no longer reads as one column. Shift each centre
            // by its own half-width so every row begins on the same X axis.
            anchor.Row(3)=anchor.Row(3)+anchor.Row(0)*
                (aspect*drawHeight*0.5f);
        }
        // Keep stack displacement in HUD-local coordinates. Applying it to
        // the world translation here used to be undone by the subsequent
        // anchor-to-eye matrix composition on the target renderer.
        if(slot==4)
        {
            // The legacy coin position is an absolute 640x480 screen point,
            // while the VR counter is a tightly cropped, centre-anchored
            // plane. Mixing those coordinate spaces leaves a visible offset
            // after the counter crop or digit width changes. Attach the coin
            // directly to the measured left edge of the counter instead.
            // The counter's bitmap buffer and the rotating coin capture both
            // contain variable transparent padding, so neither UV nor authored
            // bounds is a stable visual edge. Keep the icon tied to the counter
            // centre with the original spatial-HUD visual offset.
            const float coinCentreOffset=fixedToVehicle?0.072f:0.060f;
            anchor.Row(3)=anchor.Row(3)-anchor.Row(0)*coinCentreOffset;
            // The rotating 3D mesh has a slightly high optical centre inside
            // its symmetric capture rectangle. Align its visible centre with
            // the bitmap digits rather than the empty texture rectangle.
            anchor.Row(3)=anchor.Row(3)-anchor.Row(1)*0.010f;
        }
        if(fixedToVehicle && !centreNotification)
        {
            // Keep both position and orientation rigidly fixed to the car.
            // Build an explicitly orthonormal yaw-only basis: copying camera
            // rows can retain vehicle pitch/roll or numerical scale/shear,
            // which deforms the supposedly rectangular HUD quad.
            const rmt::Vector position=anchor.Row(3);
            rmt::Vector forward=g.cullingBaseCamera.Row(2);
            forward.y=0.0f;
            if(forward.NormalizeSafe()<0.0001f)
                forward.Set(0.0f,0.0f,1.0f);
            rmt::Vector right(forward.z,0.0f,-forward.x);
            right.NormalizeSafe();
            anchor.Identity();
            anchor.Row(0)=right;
            anchor.Row(1).Set(0.0f,1.0f,0.0f);
            anchor.Row(2)=forward;
            anchor.Row(3)=position;
        }
        if(slot==2 && g.activeEye==1)
        {
            static uint64_t lastTimerLayoutLog=0;
            if(g.hudFrameSerial-lastTimerLayoutLog>=180)
            {
                XRLOG("timer layout frame=%llu fresh=%d objective=%d height=%.4f offset=%.4f aspect=%.3f uv=%.3f,%.3f..%.3f,%.3f",
                    static_cast<unsigned long long>(g.hudFrameSerial),
                    g.missionHudCaptureFrame[2]==g.hudFrameSerial?1:0,
                    objectiveOccupiesLayout?1:0,drawHeight,verticalOffset,aspect,
                    g.missionHudUv[2][0],g.missionHudUv[2][1],
                    g.missionHudUv[2][2],g.missionHudUv[2][3]);
                lastTimerLayoutLog=g.hudFrameSerial;
            }
        }
        DrawMissionHudQuad(
#if defined(SRR2_VR_RENDERER_VULKAN)
                           g.vulkanMissionHudDescriptor[slot],
#else
                           g.missionHudTexture[slot],
#endif
                           g.missionHudUv[slot],anchor,drawHeight,aspect,eye,
                           verticalOffset);
    }
#if !defined(SRR2_VR_RENDERER_VULKAN)
    glBindTexture(GL_TEXTURE_2D,oldTexture);glActiveTexture(oldActive);
    glBindBuffer(GL_ARRAY_BUFFER,oldArray);glUseProgram(oldProgram);
    if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
    if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
    if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
#endif
}

#if 0
static void DrawSpatialHudTexture(GLuint texture,const float* uv,
                                  const rmt::Matrix& anchor,float height,
                                  Eye& eye,float sourceAspect)
{
    if(!texture || !g.radarProgram) return;
    const rmt::Matrix eyeLocal=PoseToGame(RelativePose(g.origin,eye.view.pose));
    rmt::Matrix eyeWorld,worldToEye,anchorToEye,proj,mvp;
    eyeWorld.Mult(eyeLocal,g.cullingBaseCamera); worldToEye.InvertOrtho(eyeWorld);
    anchorToEye.Mult(anchor,worldToEye); MakeProjection(eye.view.fov,0.05f,1000.0f,&proj);
    mvp.MultFull(anchorToEye,proj);
    float aspect=sourceAspect;
    aspect=std::max(0.35f,std::min(4.0f,aspect));
    const float halfY=height*0.5f,halfX=halfY*aspect;
    const float xy[4][2]={{-halfX,-halfY},{halfX,-halfY},
                          {-halfX,halfY},{halfX,halfY}};
    const float tc[4][2]={{uv[0],uv[1]},{uv[2],uv[1]},
                          {uv[0],uv[3]},{uv[2],uv[3]}};
    float vertices[24];
    for(int i=0;i<4;++i){rmt::Vector4 p(xy[i][0],xy[i][1],0,1);p.Transform(mvp);
        vertices[i*6]=p.x;vertices[i*6+1]=p.y;vertices[i*6+2]=p.z;
        vertices[i*6+3]=p.w;vertices[i*6+4]=tc[i][0];vertices[i*6+5]=tc[i][1];}
    glBindTexture(GL_TEXTURE_2D,texture);
    glVertexAttribPointer(g.radarPositionAttrib,4,GL_FLOAT,GL_FALSE,6*sizeof(float),vertices);
    glVertexAttribPointer(g.radarTexcoordAttrib,2,GL_FLOAT,GL_FALSE,6*sizeof(float),vertices+4);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
}

static GLuint GetSpatialCoinTexture()
{
    if(g.spatialCoinTexture) return g.spatialCoinTexture;
    enum { SIZE=64 };
    unsigned char pixels[SIZE*SIZE*4];
    for(int y=0;y<SIZE;++y)
    for(int x=0;x<SIZE;++x)
    {
        const float dx=x-(SIZE-1)*0.5f,dy=y-(SIZE-1)*0.5f;
        const float radius=sqrtf(dx*dx+dy*dy);
        unsigned char* p=&pixels[(y*SIZE+x)*4];
        p[0]=p[1]=p[2]=p[3]=0;
        if(radius<=29.5f)
        {
            const bool rim=radius>25.0f;
            const bool inner=radius<19.0f;
            const float shine=std::max(0.0f,1.0f-radius/30.0f);
            p[0]=255;
            p[1]=(unsigned char)(rim?145:(inner?205:180)+25.0f*shine);
            p[2]=(unsigned char)(rim?20:(inner?45:28));
            p[3]=(unsigned char)(radius>28.5f?(29.5f-radius)*255.0f:255.0f);
        }
    }
    glGenTextures(1,&g.spatialCoinTexture);
    glBindTexture(GL_TEXTURE_2D,g.spatialCoinTexture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,SIZE,SIZE,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    return g.spatialCoinTexture;
}

static void DrawSpatialHudPlanes()
{
    if(!g.activeEye || !g.cullingBaseValid) return;
    Eye& eye=g.eyes[g.activeEye-1];
    const rmt::Matrix eyeLocal=PoseToGame(RelativePose(g.origin,eye.view.pose));
    rmt::Matrix eyeWorld; eyeWorld.Mult(eyeLocal,g.cullingBaseCamera);
    Character* player=GetCharacterManager()->GetCharacter(0);
    const bool inCar=player&&player->IsInCar();
    const float heights[State::SPATIAL_HUD_COUNT]={0.075f,0.10f,0.085f,0.065f};
    const rmt::Vector carPos[State::SPATIAL_HUD_COUNT]={
        rmt::Vector(0.0f,-0.34f,0.54f), rmt::Vector(-0.34f,-0.12f,0.56f),
        rmt::Vector(-0.20f,-0.12f,0.56f), rmt::Vector(0.0f,0.02f,0.54f)};
    for(int slot=0;slot<State::SPATIAL_HUD_COUNT;++slot)
    {
        if(!g.spatialHudValid[slot]||!g.spatialHudVisible[slot]) continue;
        rmt::Matrix anchor;
        if(inCar)
        {
            anchor.Identity();anchor.Row(3)=carPos[slot];
            const rmt::Matrix local=anchor;anchor.Mult(local,g.cullingBaseCamera);
        }
        else
        {
            const int handIndex=slot==3?1:0;
            if(!g.handPoseValid[handIndex]) continue;
            rmt::Matrix hand=PoseToGame(RelativePose(g.origin,g.handPoses[handIndex]));
            rmt::Matrix handWorld;handWorld.Mult(hand,g.cullingBaseCamera);
            anchor.Identity();anchor.Row(0)=eyeWorld.Row(0);anchor.Row(1)=eyeWorld.Row(1);
            anchor.Row(2)=eyeWorld.Row(2);
            rmt::Vector offset;
            if(slot==0) offset=eyeWorld.Row(1)*-0.07f;
            else if(slot==1) offset=eyeWorld.Row(0)*-0.10f+eyeWorld.Row(1)*0.04f;
            else if(slot==2) offset=eyeWorld.Row(0)*0.08f+eyeWorld.Row(1)*0.04f;
            else offset=eyeWorld.Row(1)*-0.10f;
            anchor.Row(3)=handWorld.Row(3)+offset;
        }
        DrawSpatialHudTexture(g.spatialHudTexture[slot],g.spatialHudUv[slot],
                              anchor,heights[slot],eye,
                              g.spatialHudRect[slot][2]/
                              (float)g.spatialHudRect[slot][3]);
        if(slot==3)
        {
            rmt::Matrix coinAnchor=anchor;
            coinAnchor.Row(3)=anchor.Row(3)-anchor.Row(0)*0.080f;
            const float fullUv[4]={0.0f,0.0f,1.0f,1.0f};
            DrawSpatialHudTexture(GetSpatialCoinTexture(),fullUv,
                                  coinAnchor,0.080f,eye,1.0f);
        }
    }
}

#endif
void SetMovieRendering(bool enabled)
{
    g.movieRendering=enabled;
    if(enabled && g.moviePlaneActive && !g.moviePlaneAnchorValid && g.activeEye)
    {
        g.moviePlaneAnchor=g.eyes[g.activeEye-1].view.pose;
        // A spatial screen is a vertical object in the world, not a
        // head-locked quad: retain yaw only and discard headset pitch/roll.
        g.moviePlaneAnchor.orientation=YawOnly(g.moviePlaneAnchor.orientation);
        g.moviePlaneAnchor.position.x=
            (g.eyes[0].view.pose.position.x+g.eyes[1].view.pose.position.x)*0.5f;
        g.moviePlaneAnchor.position.y=
            (g.eyes[0].view.pose.position.y+g.eyes[1].view.pose.position.y)*0.5f;
        g.moviePlaneAnchor.position.z=
            (g.eyes[0].view.pose.position.z+g.eyes[1].view.pose.position.z)*0.5f;
        g.moviePlaneAnchorValid=true;
        XRLOG("movie plane anchored at %.3f %.3f %.3f",
              g.moviePlaneAnchor.position.x,g.moviePlaneAnchor.position.y,
              g.moviePlaneAnchor.position.z);
    }
}
bool IsMovieRendering(){ return g.movieRendering; }
void BeginMoviePlane(){ g.moviePlaneActive=true; g.moviePlaneAnchorValid=false; }
void EndMoviePlane(){ g.moviePlaneActive=false; g.moviePlaneAnchorValid=false; }
bool GetActiveMovieProjection(rmt::Matrix* out,int* width,int* height)
{
    if(!out || !width || !height || !g.activeEye ||
       !g.moviePlaneActive || !g.moviePlaneAnchorValid) return false;
    Eye& eye=g.eyes[g.activeEye-1];
    rmt::Matrix anchor=PoseToGame(g.moviePlaneAnchor);
    rmt::Matrix eyeWorld=PoseToGame(eye.view.pose);
    rmt::Matrix worldToEye;
    worldToEye.InvertOrtho(eyeWorld);
    rmt::Matrix eyeProjection;
    MakeProjection(eye.view.fov,0.1f,1000.0f,&eyeProjection);
    rmt::Matrix anchorToEye;
    anchorToEye.Mult(anchor,worldToEye);
    // Matrix::Mult is the affine fast path and deliberately forces the last
    // column to (0,0,0,1). A perspective projection is non-affine, so using
    // Mult here destroyed clip W (it stayed 1) and placed the complete plane
    // outside the frustum. Preserve all four rows/columns.
    out->MultFull(anchorToEye,eyeProjection);
    static bool logged[2]={false,false};
    const unsigned eyeIndex=g.activeEye-1;
    if(!logged[eyeIndex])
    {
        rmt::Vector4 centre(0.0f,0.0f,4.0f,1.0f);
        centre.Transform(*out);
        XRLOG("movie eye %u clip centre %.3f %.3f %.3f %.3f ndc %.3f %.3f",
              eyeIndex,centre.x,centre.y,centre.z,centre.w,
              centre.w!=0.0f?centre.x/centre.w:999.0f,
              centre.w!=0.0f?centre.y/centre.w:999.0f);
        logged[eyeIndex]=true;
    }
    *width=eye.width;
    *height=eye.height;
    return true;
}
void SetFrontendPlaneActive(bool active)
{
    if(active!=g.frontendPlaneActive) g.frontendPlaneAnchorValid=false;
    g.frontendPlaneActive=active;
    if(!active) g.frontendPlaneRendering=false;
}
void SetFrontendPlaneRendering(bool rendering)
{
    g.frontendPlaneRendering=rendering && g.frontendPlaneActive;
    if(g.frontendPlaneRendering && !g.frontendPlaneAnchorValid && g.activeEye)
    {
        g.frontendPlaneAnchor=g.eyes[g.activeEye-1].view.pose;
        // Keep menus perpendicular to the ground while facing the player.
        // Pitch and roll would otherwise tilt the panel with the headset.
        g.frontendPlaneAnchor.orientation=YawOnly(g.frontendPlaneAnchor.orientation);
        g.frontendPlaneAnchor.position.x=(g.eyes[0].view.pose.position.x+g.eyes[1].view.pose.position.x)*0.5f;
        g.frontendPlaneAnchor.position.y=(g.eyes[0].view.pose.position.y+g.eyes[1].view.pose.position.y)*0.5f;
        g.frontendPlaneAnchor.position.z=(g.eyes[0].view.pose.position.z+g.eyes[1].view.pose.position.z)*0.5f;
        g.frontendPlaneAnchorValid=true;
        XRLOG("frontend plane anchored");
    }
}
bool IsFrontendPlaneRendering(){ return g.frontendPlaneRendering; }
bool GetActiveFrontendProjection(rmt::Matrix* out,int* width,int* height)
{
    if(!out || !width || !height || !g.activeEye ||
       !g.frontendPlaneRendering || !g.frontendPlaneAnchorValid) return false;
    Eye& eye=g.eyes[g.activeEye-1];
    rmt::Matrix anchor=PoseToGame(g.frontendPlaneAnchor);
    rmt::Matrix eyeWorld=PoseToGame(eye.view.pose),worldToEye;
    worldToEye.InvertOrtho(eyeWorld);
    rmt::Matrix localScale;
    localScale.Identity();
    localScale.Row4(0).Set(3.2f,0,0,0);
    localScale.Row4(1).Set(0,3.2f,0,0);
    localScale.Row4(2).Set(0,0,8.0f,0);
    rmt::Matrix anchorToEye,localToEye,eyeProjection;
    anchorToEye.Mult(anchor,worldToEye);
    localToEye.Mult(localScale,anchorToEye);
    MakeProjection(eye.view.fov,0.1f,1000.0f,&eyeProjection);
    out->MultFull(localToEye,eyeProjection);
    *width=eye.width;
    *height=eye.height;
    return true;
}
void SetPauseCoinVisible(bool visible){g.pauseCoinVisible=visible;}
void SetIrisBlackout(bool black){g.irisBlackoutTarget=black;}
void DrawPauseCoinIcon()
{
    const unsigned slot=4;
    if(!g.pauseCoinVisible || !g.activeEye ||
       !g.frontendPlaneAnchorValid || !g.radarProgram || !g.hudQuadVbo ||
       !g.missionHudTexture[slot] || !g.missionHudCropValid[slot]) return;

    rmt::Matrix projection;
    int width=0,height=0;
    // FrontEndRenderLayer disables projection interception after Scrooby so
    // later render layers keep their own cameras. Re-enable it only while we
    // obtain the same world-locked panel matrix for this final EndEye overlay.
    const bool frontendRendering=g.frontendPlaneRendering;
    g.frontendPlaneRendering=true;
    const bool haveProjection=GetActiveFrontendProjection(&projection,&width,&height);
    g.frontendPlaneRendering=frontendRendering;
    if(!haveProjection) return;

    // Scrooby's 640x480 canvas occupies x [-0.5, 0.5] and
    // y [-0.375, 0.375] on the frontend plane.  Put the cached clean coin
    // immediately to the left of the pause screen's NumCoins text.
    const float centreX=(CGuiScreen::IsWideScreenDisplay()?540.0f:605.0f)/640.0f-0.5f;
    const float centreY=432.0f/640.0f-0.375f;
    const float halfY=0.040f;
    float aspect=g.missionHudAspect[slot]>0.0f?g.missionHudAspect[slot]:1.0f;
    aspect=std::max(0.4f,std::min(2.0f,aspect));
    const float halfX=halfY*aspect;
    const float xy[4][2]={{centreX-halfX,centreY-halfY},
                          {centreX+halfX,centreY-halfY},
                          {centreX-halfX,centreY+halfY},
                          {centreX+halfX,centreY+halfY}};
    const float* uv=g.missionHudUv[slot];
    const float tc[4][2]={{uv[0],uv[1]},{uv[2],uv[1]},
                          {uv[0],uv[3]},{uv[2],uv[3]}};
    float vertices[24];
    for(int i=0;i<4;++i)
    {
        // FeScreen applies Translate(-0.5, -0.375, 0.5) before submitting
        // the pause page. Use that exact local depth so the icon lies on the
        // menu plane instead of floating several metres in front of it.
        rmt::Vector4 p(xy[i][0],xy[i][1],0.5f,1.0f);
        p.Transform(projection);
        vertices[i*6]=p.x;vertices[i*6+1]=p.y;
        vertices[i*6+2]=p.z;vertices[i*6+3]=p.w;
        vertices[i*6+4]=tc[i][0];vertices[i*6+5]=tc[i][1];
    }

    GLint oldProgram=0,oldArray=0,oldTexture=0,oldActive=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldArray);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);
    const GLboolean depth=glIsEnabled(GL_DEPTH_TEST),blend=glIsEnabled(GL_BLEND);
    const GLboolean cull=glIsEnabled(GL_CULL_FACE),scissor=glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g.radarProgram);glBindBuffer(GL_ARRAY_BUFFER,g.hudQuadVbo);
    glBindTexture(GL_TEXTURE_2D,g.missionHudTexture[slot]);
    glUniform1i(glGetUniformLocation(g.radarProgram,"tex"),0);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);
    const GLint pa=glGetAttribLocation(g.radarProgram,"position");
    const GLint ta=glGetAttribLocation(g.radarProgram,"texcoord");
    glEnableVertexAttribArray(pa);glEnableVertexAttribArray(ta);
    glVertexAttribPointer(pa,4,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<const void*>(0));
    glVertexAttribPointer(ta,2,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<const void*>(4*sizeof(float)));
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    glDisableVertexAttribArray(pa);glDisableVertexAttribArray(ta);
    glBindTexture(GL_TEXTURE_2D,oldTexture);glActiveTexture(oldActive);
    glBindBuffer(GL_ARRAY_BUFFER,oldArray);glUseProgram(oldProgram);
    if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
    if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
    if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
}
void SetEnhancedUiConvergence(bool enabled){ g.enhancedUiConvergence=enabled; }
bool HasEnhancedUiConvergence(){ return g.enhancedUiConvergence; }
bool GetEyeCamera(unsigned eye,tCamera* base,rmt::Matrix* out)
{
    if(!g.originValid||eye>=2||!base||!out) return false;
    if(g.worldRendering && !g.cullingBaseValid)
    {
        g.cullingBaseCamera=base->GetCameraToWorldMatrix();
        g.cullingBaseValid=true;
    }
    XrPosef rel=RelativePose(g.origin,g.eyes[eye].view.pose); rmt::Matrix local=PoseToGame(rel);
    // Pure3D uses row vectors: apply the local HMD/eye transform first, then
    // place it in the base game camera's world transform.
    out->Mult(local,base->GetCameraToWorldMatrix()); return true;
}
bool GetActiveEyeCamera(tCamera* base,rmt::Matrix* out)
{
    return g.activeEye && GetEyeCamera(g.activeEye-1,base,out);
}
bool GetActiveCullingCamera(rmt::Matrix* out)
{
    if(!g.activeEye) return false;
    return GetLatestCullingCamera(out);
}
bool GetLatestCullingCamera(rmt::Matrix* out)
{
    if(!out || !g.originValid || !g.cullingBaseValid) return false;
    XrPosef head=g.eyes[g.activeEye?g.activeEye-1:0].view.pose;
    // Both eyes share orientation. When called by simulation managers there
    // is no active render eye, so use the left view from the latest located
    // frame and the common midpoint position.
    if(!g.activeEye) head=g.eyes[0].view.pose;
    head.position.x=(g.eyes[0].view.pose.position.x+g.eyes[1].view.pose.position.x)*0.5f;
    head.position.y=(g.eyes[0].view.pose.position.y+g.eyes[1].view.pose.position.y)*0.5f;
    head.position.z=(g.eyes[0].view.pose.position.z+g.eyes[1].view.pose.position.z)*0.5f;
    rmt::Matrix local=PoseToGame(RelativePose(g.origin,head));
    out->Mult(local,g.cullingBaseCamera);
    return true;
}
bool GetGameplayCamera(rmt::Matrix* out)
{
    if(!out || !g.cullingBaseValid) return false;
    *out=g.cullingBaseCamera;
    return true;
}
bool GetControllerWorldPose(unsigned hand,tCamera* base,rmt::Matrix* out)
{
    if(hand>=2 || !base || !out || !g.originValid || !g.handPoseValid[hand]) return false;
    const rmt::Matrix local=PoseToGame(RelativePose(g.origin,g.handPoses[hand]));
    out->Mult(local,base->GetCameraToWorldMatrix());
    return true;
}
bool GetControllerLocalPose(unsigned hand,rmt::Matrix* out)
{
    if(hand>=2 || !out || !g.originValid || !g.handPoseValid[hand]) return false;
    *out=PoseToGame(RelativePose(g.origin,g.handPoses[hand]));
    return true;
}
void RenderControllerHands(tCamera* base)
{
    if(!g.vrModeEnabled || !base || !g.cullingBaseValid) return;
    Character* controlledCharacter=GetCharacterManager()->GetCharacter(0);
    if(!controlledCharacter || !controlledCharacter->GetController() ||
       !controlledCharacter->GetController()->IsActive()) return;
    // In third-person vehicle mode the tracked controllers remain active as
    // spatial HUD anchors, but their character hand meshes must stay hidden
    // beside the chase camera.
    if(controlledCharacter->IsInCar() && IsThirdPersonVehicleMode()) return;
    SuperCamCentral* cameraCentral=GetSuperCamManager()->GetSCC(0);
    SuperCam* activeCamera=cameraCentral?cameraCentral->GetActiveSuperCam():NULL;
    if(activeCamera)
    {
        const SuperCam::Type type=activeCamera->GetType();
        if(type==SuperCam::ANIMATED_CAM ||
           type==SuperCam::RELATIVE_ANIMATED_CAM ||
           type==SuperCam::CONVERSATION_CAM) return;
    }
    static tShader* shader=NULL;
    static tTexture* texture=NULL;
    if(!shader)
    {
        shader=new tShader("simple");
        shader->AddRef();
        texture=new tTexture;
        texture->AddRef();
        if(texture->Create(vr_hand_tex_width,vr_hand_tex_height,32,8,0))
        {
            pddiLockInfo* lock=texture->Lock(0);
            if(lock && lock->bits)
            {
                for(int y=0;y<vr_hand_tex_height;++y)
                {
                    PDDI_U32* dst=reinterpret_cast<PDDI_U32*>(
                        reinterpret_cast<unsigned char*>(lock->bits)+y*lock->pitch);
                    for(int x=0;x<vr_hand_tex_width;++x)
                    {
                        const unsigned char* src=&vr_hand_tex_rgba[(y*vr_hand_tex_width+x)*4];
                        pddiColour colour(src[0],src[1],src[2],src[3]);
                        dst[x]=lock->MakeColour(colour);
                    }
                }
                texture->Unlock(0);
            }
            shader->SetTexture(PDDI_SP_BASETEX,texture);
        }
        shader->SetInt(PDDI_SP_ISLIT,1);
        shader->SetInt(PDDI_SP_SHADEMODE,PDDI_SHADE_GOURAUD);
        shader->SetInt(PDDI_SP_FILTER,PDDI_FILTER_NONE);
        shader->SetInt(PDDI_SP_BLENDMODE,PDDI_BLEND_NONE);
        shader->SetInt(PDDI_SP_TWOSIDED,1);
        shader->SetColour(PDDI_SP_AMBIENT,tColour(255,255,255));
        shader->SetColour(PDDI_SP_DIFFUSE,tColour(255,255,255));
    }
    // Use the exact swatch material used by Homer whenever it is present in
    // the level inventory.  Besides selecting the intended yellow palette
    // entry, this preserves the game's character lighting/toon treatment.
    tShader* characterShader=p3d::find<tShader>("char_swatches_lit_m");
    pddiShader* handShader=characterShader?characterShader->GetShader():shader->GetShader();
#if defined(RAD_ANDROID)
    const int previousMaterialMode=pglGetEnhancedMaterialMode();
    if(IsEnhancedMaterialsEnabled()) pglSetEnhancedMaterialMode(3);
#endif
    struct HandMesh
    {
        const float* positions;
        const float* normals;
        const float* uvs;
        int count;
    };
    struct CharacterHands { HandMesh left,right; };
#define VR_HAND_MESH(character,side) \
    { vr_hand_##character##_##side##_positions, \
      vr_hand_##character##_##side##_normals, \
      vr_hand_##character##_##side##_uvs, \
      vr_hand_##character##_##side##_count }
    static const CharacterHands homerHands={VR_HAND_MESH(homer,l),VR_HAND_MESH(homer,r)};
    static const CharacterHands bartHands ={VR_HAND_MESH(bart,l), VR_HAND_MESH(bart,r)};
    static const CharacterHands lisaHands ={VR_HAND_MESH(lisa,l), VR_HAND_MESH(lisa,r)};
    static const CharacterHands margeHands={VR_HAND_MESH(marge,l),VR_HAND_MESH(marge,r)};
    static const CharacterHands apuHands  ={VR_HAND_MESH(apu,l),  VR_HAND_MESH(apu,r)};
#undef VR_HAND_MESH

    const CharacterHands* characterHands=&homerHands;
    Character* player=GetCharacterManager()->GetCharacter(0);
    if(player)
    {
        const tUID uid=player->GetUID();
        if(uid==tEntity::MakeUID("bart"))       characterHands=&bartHands;
        else if(uid==tEntity::MakeUID("lisa")) characterHands=&lisaHands;
        else if(uid==tEntity::MakeUID("marge"))characterHands=&margeHands;
        else if(uid==tEntity::MakeUID("apu"))  characterHands=&apuHands;
    }

    Character* drivingPlayer=GetCharacterManager()->GetCharacter(0);
    const bool showWheel=g.vehicleControlMode==1 && drivingPlayer && drivingPlayer->IsInCar();
    Vehicle* drawVehicle = (drivingPlayer && drivingPlayer->IsInCar()) ? drivingPlayer->GetTargetVehicle() : NULL;
    const bool drawingYoke = showWheel && IsHonorRoller(drawVehicle);
    const bool drawWheelMesh = showWheel && (g.wheelAdjustMode || !g.wheelMeshHidden);
    if(drawWheelMesh)
    {
        static tShader* wheelShader=NULL;
        if(!wheelShader)
        {
            wheelShader=new tShader("simple"); wheelShader->AddRef();
            wheelShader->SetInt(PDDI_SP_ISLIT,1);
            wheelShader->SetInt(PDDI_SP_SHADEMODE,PDDI_SHADE_GOURAUD);
            wheelShader->SetInt(PDDI_SP_BLENDMODE,PDDI_BLEND_NONE);
            wheelShader->SetInt(PDDI_SP_TWOSIDED,1);
            wheelShader->SetColour(PDDI_SP_AMBIENT,tColour(38,38,42));
            wheelShader->SetColour(PDDI_SP_DIFFUSE,tColour(115,115,125));
        }

        if (drawingYoke)
        {
            // T-handle that rocks with the hands (joystick-style).
            // Light movement is enough – ranges are small.
            const float halfWidth = 0.16f;
            const float barRadius = 0.016f;
            const float steerT    = g.wheelVisualAngle / kVrWheelMaxAngle;
            const float pitchT    = g.yokeThrottle;
            const float roll  = steerT * 0.55f;
            const float pitch = pitchT * 0.40f;
            const float cr = std::cos(roll),  sr = std::sin(roll);
            const float cp = std::cos(pitch), sp = std::sin(pitch);

            auto xf = [&](float lx, float ly, float lz) -> rmt::Vector
            {
                float y1 = ly * cp - lz * sp;
                float z1 = ly * sp + lz * cp;
                float x1 = lx;
                float x2 = x1 * cr - y1 * sr;
                float y2 = x1 * sr + y1 * cr;
                float z2 = z1;
                return rmt::Vector(
                    g.activeYokeAnchor.x + x2,
                    g.activeYokeAnchor.y + y2,
                    g.activeYokeAnchor.z + z2);
            };

            auto emitCylinder = [&](pddiPrimStream* stream,
                                    rmt::Vector a, rmt::Vector b, float radius)
            {
                rmt::Vector axis = b - a;
                const float len = axis.Magnitude();
                if (len < 1e-5f) return;
                axis.Scale(1.0f / len);
                rmt::Vector up(0,1,0);
                if (std::fabs(axis.y) > 0.9f) up.Set(1,0,0);
                rmt::Vector side;
                side.CrossProduct(axis, up); side.NormalizeSafe();
                up.CrossProduct(side, axis); up.NormalizeSafe();
                const int segs = 8;
                for (int i = 0; i < segs; ++i)
                {
                    const float a0 = (i     ) * 6.28318531f / segs;
                    const float a1 = (i + 1) * 6.28318531f / segs;
                    const float c0 = std::cos(a0), s0 = std::sin(a0);
                    const float c1 = std::cos(a1), s1 = std::sin(a1);
                    rmt::Vector n0 = side*c0 + up*s0;
                    rmt::Vector n1 = side*c1 + up*s1;
                    rmt::Vector p0 = a + n0*radius;
                    rmt::Vector p1 = a + n1*radius;
                    rmt::Vector p2 = b + n1*radius;
                    rmt::Vector p3 = b + n0*radius;
                    rmt::Vector verts[6] = {p0,p1,p2, p0,p2,p3};
                    rmt::Vector norms[6] = {n0,n1,n1, n0,n1,n0};
                    for (int v = 0; v < 6; ++v)
                    {
                        rmt::Vector wp, wn;
                        g.cullingBaseCamera.Transform(verts[v], &wp);
                        g.cullingBaseCamera.RotateVector(norms[v], &wn);
                        wn.NormalizeSafe();
                        stream->Normal(wn.x, wn.y, wn.z);
                        stream->Coord(wp.x, wp.y, wp.z);
                    }
                }
            };

            // One single arc per side, curving up and outward. Shifted down
            // AND inward so its midpoint (not its base) sits right on top
            // of the bar's own end — that's the touch point. The bottom-most
            // segments (near theta=0, before the touch point) are trimmed
            // off. Plenty of segments (like the round wheel's 32-gon rim) so
            // it reads as smooth, not faceted.
            const int hornSegs   = 18;
            const int hornCutoff = 4; // segments trimmed off the bottom
            const float hornRadius = 0.13f;
            const float hornSweep  = 2.6f; // radians, total sweep of the arc
            const float hornNotch  = hornSweep / hornSegs; // one segment's worth of angle
            // Touch point moved one notch further along the curve, which
            // pushes the whole arc down by one notch while it still meets
            // the bar at the same spot.
            const float hornTouch  = hornSweep * 0.5f + hornNotch;
            const float hornDrop   = hornRadius * (1.0f - std::cos(hornTouch));
            const float hornInset  = hornRadius * std::sin(hornTouch);

            auto hornPoint = [&](float side, float theta) -> rmt::Vector
            {
                const float lx = side * (halfWidth + hornRadius * std::sin(theta) - hornInset);
                const float ly = kYokeArmLength + hornRadius * (1.0f - std::cos(theta)) - hornDrop;
                return xf(lx, ly, 0.0f);
            };

            const int approxTris = 8*6 * 2 + 8*(hornSegs - hornCutoff) * 2;
            pddiPrimStream* stream = p3d::pddi->BeginPrims(wheelShader->GetShader(),
                PDDI_PRIM_TRIANGLES, PDDI_V_N, approxTris);
            if (stream)
            {
                rmt::Vector stemBase = g.activeYokeAnchor;
                rmt::Vector stemTop  = xf(0.0f, kYokeArmLength, 0.0f);
                rmt::Vector barL     = xf(-halfWidth, kYokeArmLength, 0.0f);
                rmt::Vector barR     = xf( halfWidth, kYokeArmLength, 0.0f);
                emitCylinder(stream, stemBase, stemTop, barRadius * 0.95f);
                emitCylinder(stream, barL, barR, barRadius);

                for (int endIdx = 0; endIdx < 2; ++endIdx)
                {
                    const float side = (endIdx == 0) ? -1.0f : 1.0f;
                    rmt::Vector prev = hornPoint(side, hornSweep * (static_cast<float>(hornCutoff) / hornSegs));
                    for (int i = hornCutoff + 1; i <= hornSegs; ++i)
                    {
                        const float theta = hornSweep * (static_cast<float>(i) / hornSegs);
                        rmt::Vector next = hornPoint(side, theta);
                        emitCylinder(stream, prev, next, barRadius);
                        prev = next;
                    }
                }

                p3d::pddi->EndPrims(stream);
            }
        }
        else
        {
            // Baked Canyonero steering-wheel mesh (SteeringWheel.obj),
            // same gray material as before.
            DrawObjWheel(wheelShader->GetShader());
        }
    }

    for(unsigned hand=0;hand<2;++hand)
    {
        rmt::Matrix world;
        // Keep drawing a gripped hand even if tracking blips briefly so the
        // mesh does not pop off the rim mid-turn.
        if(g.handPoseValid[hand] || (showWheel && g.wheelGrabbed[hand]))
        {
            rmt::Matrix local;
            if(showWheel && g.wheelGrabbed[hand])
            {
                if (drawingYoke)
                {
                    // Stick hand to ends of the rocking, pitching T bar
                    const float halfWidth = 0.16f;
                    const float steerT    = g.wheelVisualAngle / kVrWheelMaxAngle;
                    const float pitchT    = g.yokeThrottle;
                    const float roll  = steerT * 0.55f;
                    const float pitch = pitchT * 0.40f;
                    const float cr = std::cos(roll),  sr = std::sin(roll);
                    const float cp = std::cos(pitch), sp = std::sin(pitch);
                    const float side = (hand == 0) ? -1.0f : 1.0f;
                    // Grab point sits at the arc's midpoint, which is the
                    // touch point right on the bar's own end.
                    float lx = side * halfWidth;
                    float ly = kYokeArmLength;
                    float lz = 0.0f;
                    float y1 = ly * cp - lz * sp;
                    float z1 = ly * sp + lz * cp;
                    float x1 = lx;
                    float x2 = x1 * cr - y1 * sr;
                    float y2 = x1 * sr + y1 * cr;
                    float z2 = z1;
                    local = g.wheelGrabOrientRot[hand];
                    local.Row(3).Set(
                        g.activeYokeAnchor.x + x2,
                        g.activeYokeAnchor.y + y2,
                        g.activeYokeAnchor.z + z2);
                }
                else
                {
                    // Normal wheel – glued to the rim using the smoothed visual angle
                    // so hands don't jitter with every steering micro-update.
                    const float angle=g.wheelVisualAngle+g.wheelGrabOffset[hand];
                    const float s=std::sin(angle);
                    const float c=std::cos(angle);
                    const float delta=UnwrapDelta(g.wheelGrabOrientAngle[hand] - angle);
                    RotateOrientAroundWheelAxis(delta, g.wheelGrabOrientRot[hand], local);
                    {
                        const rmt::Vector rim = WheelLocalToCar(g.activeWheelRadius*s, g.activeWheelRadius*c, 0.0f);
                        local.Row(3).Set(rim.x, rim.y, rim.z);
                    }
                }
            }
            else
            {
                local=PoseToGame(RelativePose(g.origin,g.handPoses[hand]));
            }
            world.Mult(local,g.cullingBaseCamera);
            // OBJ labels are opposite to OpenXR's controller indices in the
            // authored export, consistently for every character.
            const HandMesh& mesh=hand==0?characterHands->right:characterHands->left;
            const float* positions=mesh.positions;
            const float* normals=mesh.normals;
            const float* uvs=mesh.uvs;
            const int count=mesh.count;
            const float handScale=characterHands==&homerHands?0.80f:1.0f;
            pddiPrimStream* stream=p3d::pddi->BeginPrims(handShader,
                PDDI_PRIM_TRIANGLES,PDDI_V_NT,count);
            if(stream)
            {
                for(int i=0;i<count;++i)
                {
                    rmt::Vector modelPos(positions[i*3],positions[i*3+1],positions[i*3+2]);
                    modelPos.Scale(handScale);
                    rmt::Vector modelNormal(normals[i*3],normals[i*3+1],normals[i*3+2]);
                    rmt::Vector worldPos,worldNormal;
                    world.Transform(modelPos,&worldPos);
                    world.RotateVector(modelNormal,&worldNormal);
                    worldNormal.NormalizeSafe();
                    stream->UV(uvs[i*2],uvs[i*2+1]);
                    stream->Normal(worldNormal.x,worldNormal.y,worldNormal.z);
                    stream->Coord(worldPos.x,worldPos.y,worldPos.z);
                }
                p3d::pddi->EndPrims(stream);
            }
        }
    }
#if defined(RAD_ANDROID)
    pglSetEnhancedMaterialMode(previousMaterialMode);
#endif
}
bool GetActiveProjection(rmt::Matrix* p,int* w,int* h)
{
    // Scrooby also labels its 2D canvas as perspective.  Only world and
    // presentation layers may consume the real eye projection; the GUI layer
    // must fall through to its legacy projection and the VR HUD transform.
    if(!g.activeEye || !g.worldRendering || g.embeddedHudRendering) return false; Eye& e=g.eyes[g.activeEye-1];
    MakeProjection(e.view.fov,0.1f,VR_WORLD_FAR_PLANE,p); *w=e.width; *h=e.height; return true;
}
bool GetActiveViewport(int* w,int* h)
{
    if(!g.activeEye) return false; Eye& e=g.eyes[g.activeEye-1]; *w=e.width; *h=e.height; return true;
}
bool GetActiveUiHorizontalOffset(float* offset)
{
    if(!g.activeEye || g.worldRendering || !offset) return false;
    const unsigned eyeIndex=g.activeEye-1;
    const XrFovf& fov=g.eyes[eyeIndex].view.fov;
    const float l=std::tan(fov.angleLeft), r=std::tan(fov.angleRight);
    // Treat legacy screen-space GUI as a head-locked plane two metres ahead.
    // Convert the real OpenXR eye position (not a guessed IPD) into the NDC
    // coordinate of the common plane centre for this eye's asymmetric FOV.
    XrVector3f centre={
        (g.eyes[0].view.pose.position.x+g.eyes[1].view.pose.position.x)*0.5f,
        (g.eyes[0].view.pose.position.y+g.eyes[1].view.pose.position.y)*0.5f,
        (g.eyes[0].view.pose.position.z+g.eyes[1].view.pose.position.z)*0.5f};
    XrVector3f eyeDelta={g.eyes[eyeIndex].view.pose.position.x-centre.x,
                        g.eyes[eyeIndex].view.pose.position.y-centre.y,
                        g.eyes[eyeIndex].view.pose.position.z-centre.z};
    const XrVector3f localEye=Rotate(Conjugate(g.eyes[eyeIndex].view.pose.orientation),eyeDelta);
    const float planeDistance=4.0f;
    const float tangentX=-localEye.x/planeDistance;
    // The legacy orthographic projection already contributes part of the
    // optical-centre shift. Apply a small residual correction for a distant,
    // comfortable plane instead of the previous close crossed disparity.
    *offset=0.48f*(2.0f*tangentX-(r+l))/(r-l);
    static bool loggedEye[2]={false,false};
    if(!loggedEye[eyeIndex])
    {
        XRLOG("HUD eye %u FOV tangent L=%.4f R=%.4f eyeX=%.4f offset=%.5f",
              eyeIndex,l,r,localEye.x,*offset);
        loggedEye[eyeIndex]=true;
    }
    return true;
}
void EndFrame()
{
    if(!g.frameBegun)return;
#if !defined(SRR2_VR_RENDERER_VULKAN)
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    if(g.perfQueryActive)
    {
        g.EndQueryEXT(GL_TIME_ELAPSED_EXT);
        g.perfQueryPending[g.perfQueryIndex]=true;
        g.perfQueryIndex=(g.perfQueryIndex+1)%4;
        g.perfQueryActive=false;
    }
#endif
    const Uint64 renderEnd=SDL_GetPerformanceCounter();
    XrCompositionLayerProjectionView pv[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    for(unsigned i=0;i<2;++i){ pv[i].pose=g.eyes[i].view.pose; pv[i].fov=g.eyes[i].view.fov; pv[i].subImage.swapchain=g.eyes[0].swapchain; pv[i].subImage.imageRect.extent.width=g.eyes[i].width; pv[i].subImage.imageRect.extent.height=g.eyes[i].height; pv[i].subImage.imageArrayIndex=i; }
    XrCompositionLayerProjection layer={XR_TYPE_COMPOSITION_LAYER_PROJECTION}; layer.space=g.space; layer.viewCount=2; layer.views=pv;
#if defined(SRR2_VR_RENDERER_VULKAN)
    const Uint32 irisNow=SDL_GetTicks();
    const float irisDt=g.irisBlackoutTicks?
        std::min(0.1f,(irisNow-g.irisBlackoutTicks)*0.001f):0.0f;
    g.irisBlackoutTicks=irisNow;
    const float irisTarget=g.irisBlackoutTarget?1.0f:0.0f;
    const float irisStep=irisDt*3.5f;
    if(g.irisBlackoutAlpha<irisTarget)
        g.irisBlackoutAlpha=std::min(irisTarget,g.irisBlackoutAlpha+irisStep);
    else if(g.irisBlackoutAlpha>irisTarget)
        g.irisBlackoutAlpha=std::max(irisTarget,g.irisBlackoutAlpha-irisStep);
    XrCompositionLayerColorScaleBiasKHR irisFade={
        XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR};
    const float irisScale=1.0f-g.irisBlackoutAlpha;
    irisFade.colorScale={irisScale,irisScale,irisScale,1.0f};
    irisFade.colorBias={0.0f,0.0f,0.0f,0.0f};
    layer.next=&irisFade;
#endif
    const XrCompositionLayerBaseHeader* layers[]={reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
    XrFrameEndInfo ei={XR_TYPE_FRAME_END_INFO}; ei.displayTime=g.frameState.predictedDisplayTime; ei.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount=g.shouldRender?1:0; ei.layers=g.shouldRender?layers:NULL;
    const Uint64 submitStart=SDL_GetPerformanceCounter();
    g.EndFrame(g.session,&ei);
    const Uint64 submitEnd=SDL_GetPerformanceCounter();
    const double frequency=static_cast<double>(SDL_GetPerformanceFrequency());
    const double renderMs=(renderEnd-g.perfRenderStart)*1000.0/frequency;
    const double submitMs=(submitEnd-submitStart)*1000.0/frequency;
    g.perfRenderSum+=renderMs; g.perfSubmitSum+=submitMs;
    g.perfRenderMax=std::max(g.perfRenderMax,renderMs);
    g.perfSubmitMax=std::max(g.perfSubmitMax,submitMs);
    ++g.perfFrames;
    if(g.perfFrames>=72)
    {
        double fenceWait=0.0;
#if defined(SRR2_VR_RENDERER_VULKAN)
        fenceWait=gVulkanContext.GetLastFenceWaitMilliseconds();
#endif
        XRLOG("VR PERF: wait %.2f/%.2f render %.2f/%.2f submit %.2f/%.2f GPU %.2f fence %.2f | draw %u idx %u vert %u tri %u drawCPU %.2f | mat %u/%.2f upload %u/%uKB/%.2f | layer gui %.2f pres %.2f level %.2f missions %.2f | world setup %.2f scene %.2f opaque %.2f trans %.2f guts %.2f CSM %.2f misc %.2f skin %.2f",
              g.perfWaitSum/g.perfFrames,g.perfWaitMax,
              g.perfRenderSum/g.perfFrames,g.perfRenderMax,
              g.perfSubmitSum/g.perfFrames,g.perfSubmitMax,g.perfGpuLast,
              fenceWait,
              g.perfDraws,g.perfIndexedDraws,g.perfVertices,g.perfTriangles,g.perfDrawCpu,
              g.perfMaterials,g.perfMaterialCpu,g.perfUploadCalls,g.perfUploadBytes/1024,g.perfUploadCpu,
              g.perfSections[0],g.perfSections[1],g.perfSections[2],g.perfSections[3]+g.perfSections[4],
              g.perfSections[5],g.perfSections[6],g.perfSections[7],g.perfSections[8],
              g.perfSections[9],g.perfSections[10],g.perfSections[11],g.perfSections[12]);
        g.perfFrames=0; g.perfWaitSum=g.perfRenderSum=g.perfSubmitSum=0.0;
        g.perfWaitMax=g.perfRenderMax=g.perfSubmitMax=0.0;
    }
    g.frameBegun=false;
}
}
#endif

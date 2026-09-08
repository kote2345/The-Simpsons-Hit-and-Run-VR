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
#include <SDL.h>
#include <SDL_system.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <vr/openxrmanager.h>
#include <vr/openxr_shared_render.h>
#include <vr/openxr_shared_frame.h>
#include <vr/openxr_shared_graphics.h>
#include <vr/openxr_shared_hud.h>
#include <vr/openxr_shared_hands.h>
#include <vr/openxr_shared_input.h>
#include <vr/openxr_shared_menu.h>
#include <vr/openxr_shared_settings.h>
#include <vr/openxr_shared_state.h>
#include <vr/openxr_shared_tracking.h>
#include <vr/openxr_shared_vehicle.h>
#include <vr/openxr_platform_loader.h>
#include <vr/openxr_platform_instance.h>
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
#include <presentation/gui/guisystem.h>
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
#include <new>

#define XRLOG(...) SDL_Log("OpenXR: " __VA_ARGS__)
#define XRERR(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OpenXR: " __VA_ARGS__)

extern bool pglAreMultiviewProgramsReady();

namespace
{
#if defined(SRR2_VR_RENDERER_VULKAN)
SharOpenXR::VulkanContext& gVulkanContext=SharOpenXR::GetVulkanContext();
#endif
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
    bool colorScaleBiasEnabled;
    uint32_t multiviewImageIndex;
    std::vector<unsigned char> multiviewFramebufferValid;
    bool usingStageSpace;
    SharOpenXR::SharedSystemRecenterState systemRecenter;
    XrFrameState frameState;
    XrViewState viewState;
    Eye eyes[2];
    uint32_t activeEye;
    bool worldRendering;
    bool embeddedHudRendering;
    bool movieRendering;
    bool moviePlaneActive;
    bool moviePlaneAnchorValid;
    XrPosef moviePlaneAnchor;
    bool& vrModeEnabled=SharOpenXR::GetSharedVrState().vrModeEnabled;
    bool& seatedMode=SharOpenXR::GetSharedVrState().seatedMode;
    bool& snapTurnEnabled=SharOpenXR::GetSharedVrState().snapTurnEnabled;
    bool& csmEnabled=SharOpenXR::GetSharedVrState().csmEnabled;
    bool& enhancedMaterialsEnabled=SharOpenXR::GetSharedVrState().enhancedMaterialsEnabled;
    bool& gtaoEnabled=SharOpenXR::GetSharedVrState().gtaoEnabled;
    bool& vehicleComfortEnabled=SharOpenXR::GetSharedVrState().vehicleComfortEnabled;
    bool& customMaterialsEnabled=SharOpenXR::GetSharedVrState().customMaterialsEnabled;
    int& enhancedMaterialModel=SharOpenXR::GetSharedVrState().enhancedMaterialModel;
    bool& spatialHudEnabled=SharOpenXR::GetSharedVrState().spatialHudEnabled;
    bool& developerMenusEnabled=SharOpenXR::GetSharedVrState().developerMenusEnabled;
    int& vehicleControlMode=SharOpenXR::GetSharedVrState().vehicleControlMode;
    int& vehicleLightMode=SharOpenXR::GetSharedVrState().vehicleLightMode;
    int& reflectionMode=SharOpenXR::GetSharedVrState().reflectionMode;
    int& pbrDebugMode=SharOpenXR::GetSharedVrState().pbrDebugMode;
    bool (&wheelGrabbed)[2]=SharOpenXR::GetSharedVrState().wheelGrabbed;
    bool& wheelHonk=SharOpenXR::GetSharedVrState().wheelHonk;
    float (&gripValue)[2]=SharOpenXR::GetSharedVrState().gripValue;
    float (&wheelGrabAngle)[2]=SharOpenXR::GetSharedVrState().wheelGrabAngle;
    float (&wheelGrabOffset)[2]=SharOpenXR::GetSharedVrState().wheelGrabOffset;
    float& wheelAngle=SharOpenXR::GetSharedVrState().wheelAngle;
    // Visual-only smoothed angle for rim mesh + hand glue. Steering input
    // still uses the responsive wheelAngle; this just kills micro-jitter in
    // the drawn wheel and the hands locked to it.
    float& wheelVisualAngle=SharOpenXR::GetSharedVrState().wheelVisualAngle;
    // Per-hand accumulated steering target. Updated from frame-to-frame
    // atan2 deltas (not absolute angle) so crossing the ±π seam at the
    // bottom of the rim cannot flip full-left lock over to full-right.
    float (&wheelGrabTarget)[2]=SharOpenXR::GetSharedVrState().wheelGrabTarget;
    // Slow-learned bias correction for the round wheel (normal cars only).
    // If the coded hub/plane doesn't exactly match the physical wheel prop,
    // "straight" reads as a small nonzero wheelAngle forever, since the
    // rim only recentres when BOTH hands fully let go. This trim slowly
    // absorbs a *sustained small* offset while gripped so the car stops
    // pulling to one side, without touching wheelAngle itself (so the rim
    // mesh and hand-glue visuals still show your real physical position).
    float& wheelTrim=SharOpenXR::GetSharedVrState().wheelTrim;
    float& yokeThrottle=SharOpenXR::GetSharedVrState().yokeThrottle;
    bool& yokeFullGasLatched=SharOpenXR::GetSharedVrState().yokeFullGasLatched;
    bool& yokeFullBrakeLatched=SharOpenXR::GetSharedVrState().yokeFullBrakeLatched;
    // Dual stick-click hold: reposition + orient VR wheel for this car.
    bool (&stickClick)[2]=SharOpenXR::GetSharedVrState().stickClick;
    bool& wheelAdjustMode=SharOpenXR::GetSharedVrState().wheelAdjustMode;
    float& wheelAdjustHoldSec=SharOpenXR::GetSharedVrState().wheelAdjustHoldSec;
    char (&wheelAdjustVehicle)[48]=SharOpenXR::GetSharedVrState().wheelAdjustVehicle;
    rmt::Vector& activeWheelCentre=SharOpenXR::GetSharedVrState().activeWheelCentre;
    rmt::Vector& activeYokeAnchor=SharOpenXR::GetSharedVrState().activeYokeAnchor;
    float& activeWheelYaw=SharOpenXR::GetSharedVrState().activeWheelYaw;
    float& activeWheelPitch=SharOpenXR::GetSharedVrState().activeWheelPitch;
    float& activeWheelRadius=SharOpenXR::GetSharedVrState().activeWheelRadius;
    bool& wheelMeshHidden=SharOpenXR::GetSharedVrState().wheelMeshHidden;
    // Orientation snapshot at grab: rim angle + full hand rotation. While
    // held, the hand is spun only around the wheel axis by (currentAngle -
    // grabAngle) so palms do not twist relative to the rim.
    float (&wheelGrabOrientAngle)[2]=SharOpenXR::GetSharedVrState().wheelGrabOrientAngle;
    rmt::Matrix (&wheelGrabOrientRot)[2]=SharOpenXR::GetSharedVrState().wheelGrabOrientRot;
    float& smoothTurnSpeed=SharOpenXR::GetSharedVrState().smoothTurnSpeed;
    float& snapTurnAngle=SharOpenXR::GetSharedVrState().snapTurnAngle;
    float& renderScale=SharOpenXR::GetSharedVrState().renderScale;
    float& appliedRenderScale=SharOpenXR::GetSharedVrState().appliedRenderScale;
    float& refreshRate=SharOpenXR::GetSharedVrState().refreshRate;
    bool& renderScalePending=SharOpenXR::GetSharedVrState().renderScalePending;
    bool& menuHorizontalInputDominant=SharOpenXR::GetSharedVrState().menuHorizontalInputDominant;
    bool& menuVerticalInputDominant=SharOpenXR::GetSharedVrState().menuVerticalInputDominant;
    unsigned& menuAxisLock=SharOpenXR::GetSharedVrState().menuAxisLock;
    unsigned& menuAxisNeutralFrames=SharOpenXR::GetSharedVrState().menuAxisNeutralFrames;
    bool& vrBaseHeadingValid=SharOpenXR::GetSharedVrState().vrBaseHeadingValid;
    rmt::Vector& vrBaseHeading=SharOpenXR::GetSharedVrState().vrBaseHeading;
    bool& roomscaleMovementSuspended=SharOpenXR::GetSharedVrState().roomscaleMovementSuspended;
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

static bool FillSharedHudRuntime(SharOpenXR::SharedHudRuntime* runtime)
{
    if(!runtime)return false;
    runtime->activeEye=g.activeEye;
    runtime->multiviewImageAcquired=g.multiviewImageAcquired;
    runtime->embeddedHudRendering=g.embeddedHudRendering;
    runtime->cullingBaseValid=g.cullingBaseValid;
    runtime->vrModeEnabled=g.vrModeEnabled;
    runtime->origin=g.origin;
    runtime->cullingBaseCamera=g.cullingBaseCamera;
    runtime->activeWheelCentre=g.activeWheelCentre;
    for(unsigned i=0;i<2;++i)
    {
        runtime->views[i]=g.eyes[i].view;
        runtime->eyeWidth[i]=g.eyes[i].width;
        runtime->eyeHeight[i]=g.eyes[i].height;
        runtime->handPoses[i]=g.handPoses[i];
        runtime->handPoseValid[i]=g.handPoseValid[i];
    }
#if defined(SRR2_VR_RENDERER_VULKAN)
    runtime->renderImage=VK_NULL_HANDLE;
    runtime->renderFormat=g.eyes[0].vulkanFormat;
    if(g.multiviewImageIndex<g.eyes[0].vulkanImages.size())
        runtime->renderImage=g.eyes[0].vulkanImages[g.multiviewImageIndex].image;
#endif
    return true;
}

#if defined(SRR2_VR_RENDERER_VULKAN)
static SharOpenXR::SharedFrameState gSharedFrame;
static SharOpenXR::SharedVulkanRenderSequence gVulkanRenderSequence;
static SharOpenXR::SharedFrameApi SharedFrameApiForRuntime()
{
    SharOpenXR::SharedFrameApi api={g.WaitFrame,g.BeginFrame,g.LocateViews,
        g.AcquireSwapchainImage,g.WaitSwapchainImage,g.ReleaseSwapchainImage,
        g.EndFrame};
    return api;
}
#endif

#define LOAD_XR(name) do { \
    if (XR_FAILED(g.getProc(g.instance, "xr" #name, reinterpret_cast<PFN_xrVoidFunction*>(&g.name))) || !g.name) { \
        XRERR("missing xr%s", #name); return false; \
    } } while (0)

static XrQuaternionf Conjugate(const XrQuaternionf& q)
{
    return SharOpenXR::SharedRender::Conjugate(q);
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

static XrQuaternionf Mul(const XrQuaternionf& a, const XrQuaternionf& b)
{
    return SharOpenXR::SharedRender::Multiply(a,b);
}

static XrVector3f Rotate(const XrQuaternionf& q, const XrVector3f& v)
{
    return SharOpenXR::SharedRender::Rotate(q,v);
}

static XrQuaternionf YawOnly(const XrQuaternionf& orientation)
{
    return SharOpenXR::SharedRender::YawOnly(orientation);
}

static XrPosef RelativePose(const XrPosef& origin, const XrPosef& pose)
{
    return SharOpenXR::SharedRender::RelativePose(origin,pose);
}

static rmt::Matrix PoseToGame(const XrPosef& pose)
{
    return SharOpenXR::SharedRender::PoseToGame(pose);
}

static void MakeProjection(const XrFovf& fov, float n, float f, rmt::Matrix* m)
{
    SharOpenXR::SharedRender::MakeProjection(fov,n,f,m);
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
    chosen=SharOpenXR::ChooseSharedVulkanSwapchainFormat(formats.data(),formatCount);
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
    if(g.depthTexture) glDeleteTextures(1,&g.depthTexture);
    glDeleteFramebuffers(2,g.gtaoFramebuffer);
    glDeleteTextures(2,g.gtaoTexture);
    if(g.gtaoProgram) glDeleteProgram(g.gtaoProgram);
    if(g.gtaoBlurProgram) glDeleteProgram(g.gtaoBlurProgram);
    if(g.gtaoCompositeProgram) glDeleteProgram(g.gtaoCompositeProgram);
    if(g.gtaoVbo) glDeleteBuffers(1,&g.gtaoVbo);
    g.framebuffer=0; g.layerFramebuffer=0; g.depthTexture=0;
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

static void FireHaptic(unsigned hand, float amplitude, float durationMs, float frequencyHz = 0.0f)
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
    XrAction* actionSlots[SharOpenXR::VR_ACTION_COUNT]={&g.moveXAction,&g.moveYAction,&g.lookXAction,&g.lookYAction,
        &g.selectAction,&g.backAction,&g.attackAction,&g.useAction,&g.menuAction,
        &g.leftTriggerAction,&g.rightTriggerAction,&g.leftGripAction,&g.rightGripAction,
        &g.leftStickClickAction,&g.rightStickClickAction};
    const SharOpenXR::VrActionSpec* specs=SharOpenXR::GetVrActionSpecs();
    for(unsigned i=0;i<SharOpenXR::VR_ACTION_COUNT;++i)
        if(!create(specs[i].name,specs[i].localizedName,
                   specs[i].kind==SharOpenXR::VR_ACTION_BOOLEAN?XR_ACTION_TYPE_BOOLEAN_INPUT:XR_ACTION_TYPE_FLOAT_INPUT,
                   actionSlots[i],false)) return false;
    if (!create("hand_pose", "Tracked hand pose", XR_ACTION_TYPE_POSE_INPUT, &g.handPoseAction, true) ||
        !create("haptic", "Controller vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT, &g.hapticAction, true)) return false;

    auto path = [&](const char* value) { XrPath p = XR_NULL_PATH; g.StringToPath(g.instance, value, &p); return p; };
    unsigned commonBindingCount=0;
    const SharOpenXR::VrBindingSpec* commonBindings=SharOpenXR::GetQuestTouchBindingSpecs(false,commonBindingCount);
    XrActionSuggestedBinding bindings[SharOpenXR::VR_ACTION_COUNT+4];
    for(unsigned i=0;i<commonBindingCount;++i)
        bindings[i]=XrActionSuggestedBinding{*actionSlots[commonBindings[i].action],path(commonBindings[i].path)};
    bindings[commonBindingCount++]=XrActionSuggestedBinding{g.handPoseAction,path("/user/hand/left/input/grip/pose")};
    bindings[commonBindingCount++]=XrActionSuggestedBinding{g.handPoseAction,path("/user/hand/right/input/grip/pose")};
    bindings[commonBindingCount++]=XrActionSuggestedBinding{g.hapticAction,path("/user/hand/left/output/haptic")};
    bindings[commonBindingCount++]=XrActionSuggestedBinding{g.hapticAction,path("/user/hand/right/output/haptic")};
    XrInteractionProfileSuggestedBinding suggested = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
    suggested.countSuggestedBindings = commonBindingCount;
    suggested.suggestedBindings = bindings;
    if (XR_FAILED(g.SuggestInteractionProfileBindings(g.instance, &suggested))) return false;
    XRLOG("Touch controller action bindings created");
    return true;
}

static void SetQuestConsoleInputRaw(void* context,const char* name,float value)
{
    UserController* controller=static_cast<UserController*>(context);
    const int index=controller->GetIdByName(name);
    if(index>=0) controller->SetVirtualInputValue(static_cast<unsigned int>(index),value);
}

static SharOpenXR::VrConsoleAdapterState sQuestConsoleAdapter;
static bool sDeveloperLeftGripHeld=false,sDeveloperRightGripHeld=false;
static void SetQuestConsoleInput(void* context,const char* name,float value)
{
    SharOpenXR::AdaptVrConsoleInput(name,value,false,
        g.menuHorizontalInputDominant,g.menuVerticalInputDominant,
        &sQuestConsoleAdapter,SetQuestConsoleInputRaw,context);
}

static void ResetQuestController(void*)
{
    InputManager* manager=InputManager::GetInstance();
    UserController* controller=manager?manager->GetController(0):NULL;
    if(!controller)return;
    SharOpenXR::EmitNeutralVrController(SetQuestConsoleInput,controller);
    SharOpenXR::ResetVrInputSemantics();
    sQuestConsoleAdapter=SharOpenXR::VrConsoleAdapterState();
    sDeveloperLeftGripHeld=sDeveloperRightGripHeld=false;
    controller->ClearVirtualInputs();
    controller->SetVirtualInputAvailable(false);
}

static void SyncInputActions()
{
    if (!g.actionSet || !g.running) return;
    XrActiveActionSet active = {g.actionSet, XR_NULL_PATH};
    XrActionsSyncInfo sync = {XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1; sync.activeActionSets = &active;
    if (XR_FAILED(g.SyncActions(g.session, &sync))) { ResetQuestController(NULL); return; }
    auto vec = [&](XrAction action) { XrActionStateGetInfo gi={XR_TYPE_ACTION_STATE_GET_INFO}; gi.action=action; XrActionStateVector2f s={XR_TYPE_ACTION_STATE_VECTOR2F}; g.GetActionStateVector2f(g.session,&gi,&s); return s.isActive?s.currentState:XrVector2f{0,0}; };
    auto boolean = [&](XrAction action) { XrActionStateGetInfo gi={XR_TYPE_ACTION_STATE_GET_INFO}; gi.action=action; XrActionStateBoolean s={XR_TYPE_ACTION_STATE_BOOLEAN}; g.GetActionStateBoolean(g.session,&gi,&s); return s.isActive && s.currentState; };
    auto value = [&](XrAction action) { XrActionStateGetInfo gi={XR_TYPE_ACTION_STATE_GET_INFO}; gi.action=action; XrActionStateFloat s={XR_TYPE_ACTION_STATE_FLOAT}; g.GetActionStateFloat(g.session,&gi,&s); return s.isActive?s.currentState:0.0f; };
    InputManager* inputManager = InputManager::GetInstance();
    UserController* controller = inputManager ? inputManager->GetController(0) : NULL;
    if (!controller) return;
    controller->SetVirtualInputAvailable(true);
    SharOpenXR::VrInputFrame raw={{value(g.moveXAction),value(g.moveYAction)},
                                  {value(g.lookXAction),value(g.lookYAction)},
                                  boolean(g.selectAction)?1.0f:0.0f,
                                  boolean(g.backAction)?1.0f:0.0f,
                                  boolean(g.attackAction)?1.0f:0.0f,
                                  boolean(g.useAction)?1.0f:0.0f,
                                  boolean(g.menuAction)?1.0f:0.0f,
                                  value(g.leftTriggerAction),value(g.rightTriggerAction),
                                  value(g.leftGripAction),value(g.rightGripAction),
                                  boolean(g.leftStickClickAction)?1.0f:0.0f,
                                  boolean(g.rightStickClickAction)?1.0f:0.0f};
    SharOpenXR::SubmitVrInputFrame(raw,SetQuestConsoleInput,controller);
    // Send grip edges straight to Scrooby. Depending on the current frontend
    // mappable, synthetic Black/White inputs are not guaranteed to be
    // registered, which made the developer selectors silently disappear.
    const bool leftDeveloperGrip=raw.leftGrip>=0.65f;
    const bool rightDeveloperGrip=raw.rightGrip>=0.65f;
    if(SharOpenXR::IsDeveloperMenusEnabled())
    {
        if(leftDeveloperGrip&&!sDeveloperLeftGripHeld)
            GetGuiSystem()->HandleMessage(GUI_MSG_CONTROLLER_L1,0,0);
        if(rightDeveloperGrip&&!sDeveloperRightGripHeld)
            GetGuiSystem()->HandleMessage(GUI_MSG_CONTROLLER_R1,0,0);
    }
    sDeveloperLeftGripHeld=leftDeveloperGrip;
    sDeveloperRightGripHeld=rightDeveloperGrip;
}
}

namespace SharOpenXR
{
void SetVrModeEnabled(bool enabled)
{
    if(g.vrModeEnabled==enabled) return;
    SetSharedVrModeEnabled(enabled);
    XRLOG("gameplay mode: %s",enabled?"VR":"Original");
}
bool IsVrModeEnabled(){ return g.vrModeEnabled; }
bool IsSpatialHudEnabled()
{
    return IsSharedSpatialHudEnabled();
}
void SetDeveloperMenusEnabled(bool enabled){ SetSharedDeveloperMenusEnabled(enabled); }
bool IsDeveloperMenusEnabled(){ return g.developerMenusEnabled; }
void SetSeatedMode(bool enabled){ SetSharedSeatedMode(enabled); }
bool IsSeatedMode(){ return g.seatedMode; }
void SetSnapTurnEnabled(bool enabled){ SetSharedSnapTurnEnabled(enabled); }
bool IsSnapTurnEnabled(){ return g.snapTurnEnabled; }
void SetSmoothTurnSpeed(float value){ SetSharedSmoothTurnSpeed(value); }
float GetSmoothTurnSpeed(){ return g.smoothTurnSpeed; }
void SetSnapTurnAngle(float value){ SetSharedSnapTurnAngle(value); }
float GetSnapTurnAngle(){ return g.snapTurnAngle; }
void SetCsmEnabled(bool enabled){ SetSharedCsmEnabled(enabled); }
bool IsCsmEnabled(){ return g.csmEnabled; }
void SetEnhancedMaterialsEnabled(bool enabled){ SetSharedEnhancedMaterialsEnabled(enabled); }
bool IsEnhancedMaterialsEnabled(){ return g.enhancedMaterialsEnabled; }
void SetCustomMaterialsEnabled(bool enabled){ SetSharedCustomMaterialsEnabled(enabled); }
bool AreCustomMaterialsEnabled(){ return g.customMaterialsEnabled; }
void SetEnhancedMaterialModel(int model)
{
    SetSharedEnhancedMaterialModel(model);
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
    SetSharedGtaoEnabled(enabled);
}
bool IsGtaoEnabled(){ return g.gtaoEnabled && !g.vrModeEnabled; }
void SetVehicleLightMode(int mode)
{
    SetSharedVehicleLightMode(mode);
}
int GetVehicleLightMode(){ return g.vehicleLightMode; }
void SetReflectionMode(int mode)
{
    SetSharedReflectionMode(mode);
}
int GetReflectionMode(){ return g.reflectionMode; }
void SetPbrDebugMode(int mode)
{
    SetSharedPbrDebugMode(mode);
}
int GetPbrDebugMode(){ return g.pbrDebugMode; }
void SetGiIndirectOnly(bool enabled){ SetSharedGiIndirectOnly(enabled); }
bool IsGiIndirectOnly(){ return GetSharedVrState().giIndirectOnly; }
void SetVolumetricLightEnabled(bool enabled){ SetSharedVolumetricLightEnabled(enabled); }
bool IsVolumetricLightEnabled(){ return GetSharedVrState().volumetricLightEnabled; }
void SetHdrEnabled(bool enabled){ SetSharedHdrEnabled(enabled); }
bool IsHdrEnabled(){ return GetSharedVrState().hdrEnabled; }
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
    SetSharedVehicleControlMode(mode);
}
int GetVehicleControlMode(){ return g.vehicleControlMode; }
void SetVehicleComfortEnabled(bool enabled)
{
    SetSharedVehicleComfortEnabled(enabled);
}
bool IsVehicleComfortEnabled(){ return g.vehicleComfortEnabled; }
bool IsThirdPersonVehicleMode(){ return g.vehicleControlMode==2; }
bool GetVrSteeringWheelValue(float* value)
{
    Character* p = GetCharacterManager()->GetCharacter(0);
    Vehicle* v = (p && p->IsInCar()) ? p->GetTargetVehicle() : NULL;
    return SharOpenXR::GetVrVehicleSteering(value,IsVrYokeVehicle(v?v->GetName():NULL));
}

void ApplyControllerHaptics(float amplitude,unsigned durationMs)
{
    FireHaptic(0,amplitude,static_cast<float>(durationMs));
    FireHaptic(1,amplitude,static_cast<float>(durationMs));
}
void SetRenderScale(float scale)
{
    const auto queue=[](void*,float){return true;};
    if(SetSharedRenderScale(scale,queue,NULL))
        XRLOG("render scale %.0f%% queued for next XR frame",g.renderScale*100.0f);
}
float GetRenderScale(){ return g.renderScale; }
void SetRefreshRate(float hz)
{
    const auto apply=[](void*,float value){return !g.session||!g.RequestDisplayRefreshRateFB||
        XR_SUCCEEDED(g.RequestDisplayRefreshRateFB(g.session,value));};
    if(SetSharedRefreshRate(hz,apply,NULL))XRLOG("%.0f Hz refresh rate applied",hz);
    else XRERR("%.0f Hz refresh rate rejected",hz);
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
    const XrView views[2]={g.eyes[0].view,g.eyes[1].view};
    if(!RecenterSharedTracking(views,g.viewState.viewStateFlags,&g.origin))
    {
        XRERR("VR pose recenter requested without a valid head pose");
        return false;
    }

    // The game camera supplies the vehicle/character base transform.  Make
    // the current physical head pose the zero local offset so entering a car
    // cannot retain the player's pre-entry standing height or world yaw.
    g.originValid=true;
    // Re-place any visible world-locked 2D panel in front of the newly
    // recentered horizontal gaze on its next render.
#if defined(SRR2_VR_RENDERER_VULKAN)
    GetSharedMoviePanel().InvalidateAnchor();
#else
    g.moviePlaneAnchorValid=false;
#endif
    SharOpenXR::GetSharedVrMenu().InvalidateAnchor();
    XRLOG("VR pose recentered at (%.3f, %.3f, %.3f)",
          g.origin.position.x,g.origin.position.y,g.origin.position.z);
    return true;
}
bool GetPhysicalHeadHeight(float* heightMetres)
{
    Character* player = GetCharacterManager() ? GetCharacterManager()->GetCharacter(0) : NULL;
    const bool child=player&&(player->GetUID()==tEntity::MakeUID("bart")||
                              player->GetUID()==tEntity::MakeUID("lisa"));
    return GetSharedPhysicalHeadHeight(g.origin,g.originValid,g.usingStageSpace,
        g.seatedMode,child,heightMetres);
}
bool ConsumeRoomscaleMovement(rmt::Vector* worldDelta)
{
    const XrView views[2]={g.eyes[0].view,g.eyes[1].view};
    Character* player=GetCharacterManager() ?
                      GetCharacterManager()->GetCharacter(0) : NULL;
    return ConsumeSharedRoomscale(views,g.viewState.viewStateFlags,&g.origin,
        g.originValid,player&&player->IsInCar(),worldDelta);
}
bool GetHeadForward(rmt::Vector* forward)
{
    const XrView views[2]={g.eyes[0].view,g.eyes[1].view};
    return GetSharedHeadForward(views,g.origin,g.originValid,forward);
}
bool Initialize()
{
    SetSharedHudRuntimeProvider(FillSharedHudRuntime);
    if (g.instance) return true;
    LoadVrSettings();
    SharOpenXR::GetSharedVrMenu().Reset();
#if defined(SRR2_VR_RENDERER_VULKAN)
    GetSharedMoviePanel().End();
#endif
    XRLOG("saved gameplay mode: %s",g.vrModeEnabled?"VR":"Original");
    g.loader=SharOpenXR::Platform::OpenLoader();
    if (!g.loader) { XRERR("loader unavailable: %s", SharOpenXR::Platform::GetLoaderError()); return false; }
    g.getProc=reinterpret_cast<PFN_xrGetInstanceProcAddr>(
        SharOpenXR::Platform::GetLoaderSymbol(g.loader,"xrGetInstanceProcAddr"));
    if (!g.getProc) { XRERR("xrGetInstanceProcAddr unavailable"); return false; }
    if(!SharOpenXR::Platform::InitializeOpenXRLoader(g.getProc))
    {
        XRERR("platform loader initialization failed");
        return false;
    }

    PFN_xrEnumerateInstanceExtensionProperties enumerateExtensions=NULL;
    g.getProc(XR_NULL_HANDLE,"xrEnumerateInstanceExtensionProperties",
        reinterpret_cast<PFN_xrVoidFunction*>(&enumerateExtensions));
    if(!enumerateExtensions) return false;
    uint32_t extensionCount=0;
    if(XR_FAILED(enumerateExtensions(NULL,0,&extensionCount,NULL))) return false;
    std::vector<XrExtensionProperties> availableExtensions(
        extensionCount,{XR_TYPE_EXTENSION_PROPERTIES});
    if(XR_FAILED(enumerateExtensions(NULL,extensionCount,&extensionCount,
                                     availableExtensions.data()))) return false;
    const auto hasExtension=[&availableExtensions](const char* name)
    {
        for(size_t i=0;i<availableExtensions.size();++i)
            if(std::strcmp(availableExtensions[i].extensionName,name)==0) return true;
        return false;
    };
    std::vector<const char*> extensions;
    SharOpenXR::Platform::AppendRequiredInstanceExtensions(extensions);
#if defined(SRR2_VR_RENDERER_VULKAN)
    extensions.push_back(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
#else
    extensions.push_back(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME);
#endif
    const char* optionalExtensions[]={
#if defined(SRR2_VR_RENDERER_VULKAN)
        XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME,
        XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME,
        XR_FB_FOVEATION_EXTENSION_NAME,
        XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME,
        XR_FB_FOVEATION_VULKAN_EXTENSION_NAME,
#endif
        XR_FB_COLOR_SPACE_EXTENSION_NAME,
        XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME};
    g.colorScaleBiasEnabled=hasExtension(
        XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME);
    for(size_t i=0;i<sizeof(optionalExtensions)/sizeof(optionalExtensions[0]);++i)
        if(hasExtension(optionalExtensions[i])) extensions.push_back(optionalExtensions[i]);
    for(size_t i=0;i<extensions.size();++i)
    {
        if(!hasExtension(extensions[i]))
        {
            XRERR("required instance extension unavailable: %s",extensions[i]);
            return false;
        }
    }

    XrInstanceCreateInfo ci={XR_TYPE_INSTANCE_CREATE_INFO};
    if(!SharOpenXR::Platform::PrepareInstanceCreateInfo(&ci)) return false;
    std::strncpy(ci.applicationInfo.applicationName,"The Simpsons Hit & Run VR",XR_MAX_APPLICATION_NAME_SIZE-1);
    ci.applicationInfo.applicationVersion=1;
    std::strncpy(ci.applicationInfo.engineName,"Pure3D",XR_MAX_ENGINE_NAME_SIZE-1);
    ci.applicationInfo.engineVersion=1; ci.applicationInfo.apiVersion=XR_CURRENT_API_VERSION;
    ci.enabledExtensionCount=static_cast<uint32_t>(extensions.size());
    ci.enabledExtensionNames=extensions.data();
    PFN_xrCreateInstance createInstance=NULL;
    g.getProc(XR_NULL_HANDLE,"xrCreateInstance",reinterpret_cast<PFN_xrVoidFunction*>(&createInstance));
    if (!createInstance || XR_FAILED(createInstance(&ci,&g.instance))) { XRERR("xrCreateInstance failed"); return false; }
    LOAD_XR(DestroyInstance); LOAD_XR(GetSystem);
#if !defined(SRR2_VR_RENDERER_VULKAN)
    LOAD_XR(GetOpenGLESGraphicsRequirementsKHR);
#endif
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
    SharOpenXR::GetSharedVrMenu().Reset();
    ShutdownSharedHud();
#if defined(SRR2_VR_RENDERER_VULKAN)
    GetSharedMoviePanel().End();
#endif
    DestroySwapchainsAndRenderTargets();
    if(g.foveationProfile && g.DestroyFoveationProfileFB)
        g.DestroyFoveationProfileFB(g.foveationProfile);
    for(unsigned i=0;i<2;++i) if(g.handSpaces[i]) g.DestroySpace(g.handSpaces[i]);
    if(g.space) g.DestroySpace(g.space); if(g.session) g.DestroySession(g.session);
    if(g.actionSet) g.DestroyActionSet(g.actionSet);
    if(g.instance) g.DestroyInstance(g.instance);
    if(g.loader) SharOpenXR::Platform::CloseLoader(g.loader);
    // Reconstruct runtime handles without rebinding or duplicating the shared
    // gameplay/configuration state referenced by State.
    g.~State();
    new (&g) State();
}

void PollEvents()
{
    if(!g.instance) return; XrEventDataBuffer ev={XR_TYPE_EVENT_DATA_BUFFER};
    while(g.PollEvent(g.instance,&ev)==XR_SUCCESS) {
        const SharOpenXR::SharedSessionApi api={g.BeginSession,g.EndSession,
            ResetQuestController,NULL};
        if(ev.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrEventDataSessionStateChanged* s=reinterpret_cast<const XrEventDataSessionStateChanged*>(&ev);
            XRLOG("session state %d",(int)s->state);
        }
        else if(ev.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            const XrEventDataReferenceSpaceChangePending* change=
                reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&ev);
            // Quest owns the long-press Meta-button gesture. It reports the
            // resulting native recenter here rather than exposing that button
            // as an application input action.
            XRLOG("system recenter pending: space=%d time=%lld",
                  static_cast<int>(change->referenceSpaceType),
                  static_cast<long long>(change->changeTime));
        }
        const XrResult result=HandleSharedRuntimeEvent(ev,api,g.session,&g.running,
            &g.systemRecenter,&g.sessionState);
        if(XR_FAILED(result))XRERR("runtime event failed (%d)",static_cast<int>(result));
        ev={XR_TYPE_EVENT_DATA_BUFFER};
    }
}

bool BeginFrame()
{
    SharedHudBeginFrame();
    g.perfFrameStart=SDL_GetPerformanceCounter();
    g.perfDraws=g.perfIndexedDraws=g.perfVertices=g.perfTriangles=g.perfMaterials=0;
    g.perfUploadCalls=g.perfUploadBytes=0;
    g.perfDrawCpu=g.perfMaterialCpu=g.perfUploadCpu=0.0;
    for(unsigned i=0;i<22;++i) g.perfSections[i]=0.0;
    PollEvents(); if(!g.running) return false;
    if(g.renderScalePending)
    {
        const auto recreate=[](void*,float)->bool {
#if defined(SRR2_VR_RENDERER_VULKAN)
            vkQueueWaitIdle(gVulkanContext.GetQueue());
#else
            glFinish();
#endif
            DestroySwapchainsAndRenderTargets();return CreateSwapchains();
        };
        if(!ApplyPendingSharedRenderScale(recreate,NULL))
        { XRERR("failed to recreate XR render targets");return false; }
        XRLOG("render scale %.0f%% applied live",g.renderScale*100.0f);
    }
    SyncInputActions();
#if defined(SRR2_VR_RENDERER_VULKAN)
    const Uint64 waitStart=SDL_GetPerformanceCounter();
    if(!SharOpenXR::BeginSharedFrame(SharedFrameApiForRuntime(),g.session,g.space,
                                    &gSharedFrame)) return false;
    const Uint64 waitEnd=SDL_GetPerformanceCounter();
    const double frequency=static_cast<double>(SDL_GetPerformanceFrequency());
    const double waitMs=(waitEnd-waitStart)*1000.0/frequency;
    g.perfWaitSum+=waitMs;g.perfWaitMax=std::max(g.perfWaitMax,waitMs);
    g.frameState=gSharedFrame.frame;g.viewState=gSharedFrame.viewState;
    g.eyes[0].view=gSharedFrame.views[0];g.eyes[1].view=gSharedFrame.views[1];
    g.frameBegun=gSharedFrame.begun;g.shouldRender=gSharedFrame.shouldRender;
    g.perfRenderStart=SDL_GetPerformanceCounter();
#else
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
#endif

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
#if !defined(SRR2_VR_RENDERER_VULKAN)
    XrViewLocateInfo li={XR_TYPE_VIEW_LOCATE_INFO}; li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime=g.frameState.predictedDisplayTime; li.space=g.space;
    g.viewState={XR_TYPE_VIEW_STATE}; uint32_t count=0;
    // Eye contains swapchain bookkeeping, so locate into a contiguous array.
    XrView views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
    if(XR_FAILED(g.LocateViews(g.session,&li,&g.viewState,2,&count,views)) || count!=2) { g.shouldRender=false; return true; }
    g.eyes[0].view=views[0]; g.eyes[1].view=views[1];
#endif
    const XrView locatedViews[2]={g.eyes[0].view,g.eyes[1].view};
    if(UpdateSharedSystemRecenter(&g.systemRecenter,g.frameState.predictedDisplayTime,
        locatedViews,g.viewState.viewStateFlags,g.seatedMode,&g.origin,&g.originValid))
    {
#if defined(SRR2_VR_RENDERER_VULKAN)
        GetSharedMoviePanel().InvalidateAnchor();
#else
        g.moviePlaneAnchorValid=false;
#endif
        GetSharedVrMenu().InvalidateAnchor();
    }
    if(!g.originValid && (g.viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        // Recenter heading only. Capturing pitch/roll in the origin makes the
        // relative pitch axis rotate with yaw, so a level horizon appears
        // sloped after looking behind.
        g.origin=SharOpenXR::SharedRender::CentreYawAnchor(
            g.eyes[0].view.pose,g.eyes[1].view.pose);
        g.originValid=true;
    }
    LocateSharedHandPoses(g.LocateSpace,g.handSpaces,g.space,
        g.frameState.predictedDisplayTime,g.originValid,g.handPoses,g.handPoseValid);
    const auto haptic=[](void*,unsigned hand,float amplitude,unsigned durationMs)
    { FireHaptic(hand,amplitude,static_cast<float>(durationMs)); };
    UpdateTrackedVrVehicle(g.originValid,g.origin,g.handPoses,g.handPoseValid,haptic,NULL);
    return true;
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
    if(!SharOpenXR::AcquireSharedFrameImage(SharedFrameApiForRuntime(),
                                            eye.swapchain,&gSharedFrame))return false;
    const uint32_t index=gSharedFrame.imageIndex;
    const bool firstUse=index>=eye.vulkanImageInitialized.size() ||
                        !eye.vulkanImageInitialized[index];
    if(index<eye.foveationImages.size())
        gVulkanContext.SetFragmentDensityMap(eye.foveationImages[index].image,
            eye.foveationImages[index].width,eye.foveationImages[index].height);
    else
        gVulkanContext.SetFragmentDensityMap(VK_NULL_HANDLE,0,0);
    if(index>=eye.vulkanImages.size() ||
       !SharOpenXR::BeginSharedVulkanMultiview(gVulkanContext,
           eye.vulkanImages[index].image,firstUse,eye.width,eye.height,
           &gVulkanRenderSequence))
    {
        SharOpenXR::ReleaseSharedFrameImage(SharedFrameApiForRuntime(),
                                            eye.swapchain,&gSharedFrame);
        return false;
    }
    if(index<eye.vulkanImageInitialized.size()) eye.vulkanImageInitialized[index]=1;
    g.multiviewImageIndex=index;
    g.multiviewImageAcquired=true;
    g.multiviewRendering=true;
    g.multiviewTargetActive=true;
    g.activeEye=1;
    g.cullingBaseValid=false;
    SharedHudResetVisible();
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
    SharedHudResetVisible();
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
    if(!g.cullingBaseValid){g.cullingBaseCamera=base->GetCameraToWorldMatrix();
        g.cullingBaseValid=true;}
    const XrView views[2]={g.eyes[0].view,g.eyes[1].view};rmt::Matrix centre;
    return BuildSharedMultiviewCameras(g.origin,views,g.cullingBaseCamera,
        g.multiviewProjection,g.multiviewViewAdjustment,&centre);
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
    const auto present=[](void*,unsigned){
        DrawSharedGameplayHud();
    };
    if(!SharOpenXR::BeginSharedVulkanGuiEye(gVulkanContext,
        &gVulkanRenderSequence,eye,present,NULL))return false;
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
    const auto present=[](void*,unsigned){
        DrawSharedGameplayHud();
    };
    if(!SharOpenXR::EndSharedVulkanMultiview(gVulkanContext,
        &gVulkanRenderSequence,present,NULL))
        XRERR("failed to submit Vulkan multiview GUI");
    g.perfGpuLast=gVulkanContext.GetLastGpuMilliseconds();
    if(g.multiviewImageAcquired)
        SharOpenXR::ReleaseSharedFrameImage(SharedFrameApiForRuntime(),
            g.eyes[0].swapchain,&gSharedFrame);
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
    for(unsigned eye=0;eye<2;++eye){g.activeEye=eye+1;g.FramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,g.eyes[0].images[g.multiviewImageIndex].image,0,eye);g.FramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,g.multiviewDepthTexture,0,eye);DrawSharedRadarPlane();DrawSharedMissionHudPlanes();DrawSharedPauseCoinIcon();ApplySharedIrisBlackout();}
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
        if(!SharOpenXR::AcquireSharedFrameImage(SharedFrameApiForRuntime(),
                                                e.swapchain,&gSharedFrame))return false;
        const uint32_t index=gSharedFrame.imageIndex;
        g.multiviewImageIndex=index;
        g.multiviewImageAcquired=true;
        firstUse=index>=e.vulkanImageInitialized.size() ||
                 !e.vulkanImageInitialized[index];
        const bool cleared=index<e.vulkanImages.size();
        if(!cleared)
        {
            SharOpenXR::ReleaseSharedFrameImage(SharedFrameApiForRuntime(),
                                                e.swapchain,&gSharedFrame);
            g.multiviewImageAcquired=false;
            XRERR("failed to record Vulkan eye clear");
            return false;
        }
    }
    if(!g.multiviewImageAcquired) return false;
    if(!SharOpenXR::BeginSharedVulkanTarget(gVulkanContext,
           e.vulkanImages[g.multiviewImageIndex].image,
           firstUse,eye,e.width,e.height))
    {
        XRERR("failed to begin Vulkan PDDI eye %u",eye);
        if(eye==0)
        {
            SharOpenXR::ReleaseSharedFrameImage(SharedFrameApiForRuntime(),
                                                e.swapchain,&gSharedFrame);
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
        SharedHudResetVisible();
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
void EndEye(unsigned eye)
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    DrawSharedGameplayHud();
    ApplySharedIrisBlackout();
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
    DrawSharedGameplayHud();
    DrawSharedPauseCoinIcon();
    ApplySharedIrisBlackout();
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
bool GetActiveVulkanEyeTarget(VulkanEyeTarget* target)
{
    if(!target)return false;
    if(GetSharedHudCaptureTarget(target))return true;
    if(!g.multiviewImageAcquired||g.activeEye==0)return false;
    const Eye& eye=g.eyes[0];
    const uint32_t index=g.multiviewImageIndex;
    if(index>=eye.vulkanImages.size()) return false;
    return BuildSharedVulkanEyeTarget(eye.vulkanImages[index].image,
        eye.vulkanFormat,eye.width,eye.height,g.activeEye-1,
        g.multiviewRendering,g.multiviewTargetActive,target);
}
#endif
void SetWorldRendering(bool enabled){ g.worldRendering=enabled; }
void SetEmbeddedHudRendering(bool enabled){ g.embeddedHudRendering=enabled; }
bool IsEmbeddedHudRendering(){ return g.embeddedHudRendering; }
void SetMovieRendering(bool enabled)
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    const XrView views[2]={g.eyes[0].view,g.eyes[1].view};
    GetSharedMoviePanel().SetRendering(enabled,g.activeEye!=0,views);
#else
    g.movieRendering=enabled;
    if(enabled && g.moviePlaneActive && !g.moviePlaneAnchorValid && g.activeEye)
    {
        g.moviePlaneAnchor=SharOpenXR::SharedRender::CentreYawAnchor(
            g.eyes[0].view.pose,g.eyes[1].view.pose);
        g.moviePlaneAnchorValid=true;
        XRLOG("movie plane anchored at %.3f %.3f %.3f",
              g.moviePlaneAnchor.position.x,g.moviePlaneAnchor.position.y,
              g.moviePlaneAnchor.position.z);
    }
#endif
}
bool IsMovieRendering(){
#if defined(SRR2_VR_RENDERER_VULKAN)
    return GetSharedMoviePanel().IsRendering();
#else
    return g.movieRendering;
#endif
}
void BeginMoviePlane(){
#if defined(SRR2_VR_RENDERER_VULKAN)
    GetSharedMoviePanel().Begin();
#else
    g.moviePlaneActive=true;g.moviePlaneAnchorValid=false;
#endif
}
void EndMoviePlane(){
#if defined(SRR2_VR_RENDERER_VULKAN)
    GetSharedMoviePanel().End();
#else
    g.movieRendering=false;g.moviePlaneActive=false;g.moviePlaneAnchorValid=false;
#endif
}
bool GetActiveMovieProjection(rmt::Matrix* out,int* width,int* height)
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    if(!g.activeEye)return false;Eye& eye=g.eyes[g.activeEye-1];
    return GetSharedMoviePanel().GetProjection(true,eye.view,eye.width,eye.height,
        out,width,height);
#else
    if(!out || !width || !height || !g.activeEye ||
       !g.moviePlaneActive || !g.moviePlaneAnchorValid) return false;
    Eye& eye=g.eyes[g.activeEye-1];
    SharOpenXR::SharedRender::ComposeWorldLockedPanel(
        g.moviePlaneAnchor,eye.view.pose,eye.view.fov,1.0f,1.0f,1.0f,out);
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
#endif
}
void SetFrontendPlaneActive(bool active)
{
    SharOpenXR::GetSharedVrMenu().SetActive(active);
}
void SetFrontendPlaneRendering(bool rendering)
{
    const XrView views[2]={g.eyes[0].view,g.eyes[1].view};
    SharOpenXR::GetSharedVrMenu().SetRendering(rendering,g.activeEye!=0,views);
}
bool IsFrontendPlaneRendering(){ return SharOpenXR::GetSharedVrMenu().IsRendering(); }
bool GetActiveFrontendProjection(rmt::Matrix* out,int* width,int* height)
{
    if(!g.activeEye) return false;
    Eye& eye=g.eyes[g.activeEye-1];
    return SharOpenXR::GetSharedVrMenu().GetProjection(
        true,eye.view,eye.width,eye.height,out,width,height);
}
void SetEnhancedUiConvergence(bool enabled){ GetSharedVrState().enhancedUiConvergence=enabled; }
bool HasEnhancedUiConvergence(){ return GetSharedVrState().enhancedUiConvergence; }
bool GetEyeCamera(unsigned eye,tCamera* base,rmt::Matrix* out)
{
    if(!g.originValid||eye>=2||!base||!out) return false;
    if(g.worldRendering && !g.cullingBaseValid)
    {
        g.cullingBaseCamera=base->GetCameraToWorldMatrix();
        g.cullingBaseValid=true;
    }
    SharOpenXR::SharedRender::ComposeTrackedCamera(
        g.origin,g.eyes[eye].view.pose,base->GetCameraToWorldMatrix(),out);
    return true;
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
    SharOpenXR::SharedRender::ComposeTrackedCentreCamera(
        g.origin,g.eyes[0].view.pose,g.eyes[1].view.pose,
        g.cullingBaseCamera,out);
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
    if(!base) return false;
    return SharedRender::ComposeControllerWorldPose(g.origin,g.originValid,
        g.handPoses,g.handPoseValid,hand,base->GetCameraToWorldMatrix(),out);
}
bool GetControllerLocalPose(unsigned hand,rmt::Matrix* out)
{
    return SharedRender::ComposeControllerLocalPose(g.origin,g.originValid,
        g.handPoses,g.handPoseValid,hand,out);
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
    // The ordinary tracked-hand path is shared with desktop OpenXR. Vehicle
    // wheel/yoke mode remains here because it also owns the steering model.
    {
        Character* player=GetCharacterManager()->GetCharacter(0);
        const bool vehicleHands=g.vehicleControlMode==1&&player&&player->IsInCar();
        if(!vehicleHands)
        {
            rmt::Matrix poses[2];
            bool valid[2]={false,false};
            for(unsigned hand=0;hand<2;++hand)
            {
                if(!g.handPoseValid[hand]) continue;
                const rmt::Matrix local=PoseToGame(RelativePose(g.origin,g.handPoses[hand]));
                poses[hand].Mult(local,g.cullingBaseCamera);
                valid[hand]=true;
            }
            RenderTrackedHandMeshes(poses,valid);
            return;
        }
        Vehicle* vehicle=player->GetTargetVehicle();
        rmt::Matrix localHands[2];bool localValid[2]={false,false};
        for(unsigned hand=0;hand<2;++hand)
        {
            localValid[hand]=g.handPoseValid[hand]&&g.originValid;
            if(localValid[hand]) localHands[hand]=PoseToGame(RelativePose(g.origin,g.handPoses[hand]));
        }
        RenderVrVehicleControls(g.cullingBaseCamera,IsVrYokeVehicle(vehicle?vehicle->GetName():NULL),
                                localHands,localValid);
        return;
    }
}
bool GetActiveProjection(rmt::Matrix* p,int* w,int* h)
{
    // Scrooby also labels its 2D canvas as perspective.  Only world and
    // presentation layers may consume the real eye projection; the GUI layer
    // must fall through to its legacy projection and the VR HUD transform.
    if(!g.activeEye)return false;
    const XrView views[2]={g.eyes[0].view,g.eyes[1].view};
    const int widths[2]={g.eyes[0].width,g.eyes[1].width};
    const int heights[2]={g.eyes[0].height,g.eyes[1].height};
    return GetSharedEyeProjection(views,widths,heights,g.activeEye-1,
        g.worldRendering,g.embeddedHudRendering,0.1f,VR_WORLD_FAR_PLANE,p,w,h);
}
bool GetActiveViewport(int* w,int* h)
{
    if(!g.activeEye)return false;
    const int widths[2]={g.eyes[0].width,g.eyes[1].width};
    const int heights[2]={g.eyes[0].height,g.eyes[1].height};
    return GetSharedEyeViewport(widths,heights,g.activeEye-1,w,h);
}
bool GetActiveUiHorizontalOffset(float* offset)
{
    if(!g.activeEye)return false;
    const XrView views[2]={g.eyes[0].view,g.eyes[1].view};
    return GetSharedUiHorizontalOffset(views,g.activeEye-1,g.worldRendering,
                                       4.0f,offset);
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
    const int widths[2]={g.eyes[0].width,g.eyes[1].width};
    const int heights[2]={g.eyes[0].height,g.eyes[1].height};
#if defined(SRR2_VR_RENDERER_VULKAN)
    gSharedFrame.frame=g.frameState;gSharedFrame.shouldRender=g.shouldRender;
    gSharedFrame.views[0]=g.eyes[0].view;gSharedFrame.views[1]=g.eyes[1].view;
#else
    const XrView views[2]={g.eyes[0].view,g.eyes[1].view};
    XrCompositionLayerProjectionView pv[2];XrCompositionLayerProjection layer;
    SharOpenXR::SharedRender::BuildStereoProjectionLayer(views,g.eyes[0].swapchain,
        widths,heights,g.space,pv,&layer);
    const XrCompositionLayerBaseHeader* layers[]={reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
    XrFrameEndInfo ei=SharOpenXR::SharedRender::BuildFrameEndInfo(
        g.frameState.predictedDisplayTime,g.shouldRender,layers);
#endif
    const Uint64 submitStart=SDL_GetPerformanceCounter();
#if defined(SRR2_VR_RENDERER_VULKAN)
    SharOpenXR::EndSharedFrame(SharedFrameApiForRuntime(),g.session,g.space,
        g.eyes[0].swapchain,widths,heights,&gSharedFrame,
        UpdateSharedHudIrisAlpha(),g.colorScaleBiasEnabled);
#else
    g.EndFrame(g.session,&ei);
#endif
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

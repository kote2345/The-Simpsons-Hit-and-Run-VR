#if defined(RAD_ANDROID)

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
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
    std::vector<XrSwapchainImageOpenGLESKHR> images;
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
    unsigned radarDrawCount;
    GLuint radarFramebuffer,radarTexture,radarDisplayTexture,radarDepthBuffer,radarProgram,hudQuadVbo,irisBlackProgram;
    GLint radarSavedFramebuffer,radarSavedViewport[4];
    GLint radarSavedScissor[4];
    bool radarSavedScissorEnabled;
    bool radarSavedDepthEnabled,radarSavedCullEnabled;
    GLboolean radarSavedColourMask[4];
    float radarUv[4];
    bool radarCropValid;
    // Objective, message, timer, coin count, 3D coin, action prompt, and
    // the additional mission counters (par time, collectibles, race place).
    enum { MISSION_HUD_COUNT=13 };
    GLuint missionHudFramebuffer[MISSION_HUD_COUNT];
    GLuint missionHudTexture[MISSION_HUD_COUNT];
    float missionHudUv[MISSION_HUD_COUNT][4];
    int missionHudRect[MISSION_HUD_COUNT][4];
    rmt::Matrix missionHudLayout[MISSION_HUD_COUNT];
    bool missionHudLayoutValid[MISSION_HUD_COUNT];
    float missionHudAspect[MISSION_HUD_COUNT];
    bool missionHudVisible[MISSION_HUD_COUNT];
    bool missionHudCropValid[MISSION_HUD_COUNT];
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
    bool irisBlackoutTarget;
    XrPosef frontendPlaneAnchor;
    bool enhancedUiConvergence;
    bool vrModeEnabled;
    bool seatedMode, snapTurnEnabled, csmEnabled, enhancedMaterialsEnabled, gtaoEnabled;
    bool spatialHudEnabled;
    bool developerMenusEnabled;
    int vehicleControlMode; // 0 = stick, 1 = VR wheel, 2 = third person
    int vehicleLightMode;
    bool wheelGrabbed[2];
    float gripValue[2],wheelGrabAngle[2],wheelGrabTarget[2],wheelGrabOffset[2],wheelAngle;
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
    PFN_xrSetColorSpaceFB SetColorSpaceFB;
    PFN_xrRequestDisplayRefreshRateFB RequestDisplayRefreshRateFB;
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
        std::fprintf(file,"seated=%d\nsnap=%d\nsmooth=%.1f\nangle=%.1f\ncsm=%d\nenhancedMaterials=%d\ngtao=%d\nrenderScale=%.3f\nrefreshRate=%.0f\nvrSteeringWheel=%d\nvehicleLights=%d\nspatialHud=%d\ndeveloperMenus=%d\n",
                     g.seatedMode?1:0,g.snapTurnEnabled?1:0,
                     g.smoothTurnSpeed,g.snapTurnAngle,g.csmEnabled?1:0,
                     g.enhancedMaterialsEnabled?1:0,g.gtaoEnabled?1:0,
                     g.renderScale,g.refreshRate,g.vehicleControlMode,
                     g.vehicleLightMode,g.spatialHudEnabled?1:0,
                     g.developerMenusEnabled?1:0);
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

static const rmt::Vector kVrWheelCentre(0.0f, -0.32f, 0.52f);
static const float kVrWheelRadius     = 0.18f;
// ~140 degrees of physical rotation maps to full lock. A bit less travel than
// 160 deg makes sharp turns easier without feeling twitchy near centre.
static const float kVrWheelMaxAngle   = 2.44346095f;
static const float kGrabGripThreshold = 0.55f;
static const float kRadialTolerance   = 0.08f;
static const float kDepthTolerance    = 0.16f;
// Temporal smoothing kept for calm tracking; response comes from max-angle
// and the output curve below, not from more lag.
static const float kAngleSmooth       = 0.18f;
// Closer to 1.0 = slower spring-back when neither hand is gripping.
static const float kCentreReturn      = 0.94f;
// atan2 becomes extremely sensitive near the hub. Freeze steering before an
// inward controller movement can amplify into a large angular jump.
static const float kMinRadial         = 0.10f;
// Reject impossible one-frame tracking changes without affecting deliberate
// fast steering (about 8.5 degrees per 72 Hz frame).
static const float kMaxGrabDelta      = 0.15f;
// Small deadzone so centre stays stable without eating turn authority.
static const float kSteerOutputDeadzone = 0.025f;
// Grip offsets are captured from the controller's actual point on the rim.
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

static void UpdateVrSteeringWheel()
{
    Character* player = GetCharacterManager()->GetCharacter(0);
    const bool active = g.vrModeEnabled && g.vehicleControlMode==1
                        && player && player->IsInCar();
    if (!active)
    {
        g.wheelGrabbed[0] = g.wheelGrabbed[1] = false;
        g.wheelAngle = 0.0f;
        return;
    }

    float targetSum = 0.0f;
    int targetCount = 0;

    for (unsigned hand = 0; hand < 2; ++hand)
    {
        if (!g.handPoseValid[hand])
        {
            g.wheelGrabbed[hand] = false;
            continue;
        }

        const rmt::Matrix pose = PoseToGame(RelativePose(g.origin, g.handPoses[hand]));
        const rmt::Vector p = pose.Row(3);
        const float dx = p.x - kVrWheelCentre.x;
        const float dy = p.y - kVrWheelCentre.y;
        const float radial = std::sqrt(dx * dx + dy * dy);

        const bool close = std::fabs(radial - kVrWheelRadius) < kRadialTolerance
                        && std::fabs(p.z - kVrWheelCentre.z) < kDepthTolerance
                        && radial > kMinRadial;

        const bool squeezed = g.gripValue[hand] > kGrabGripThreshold;
        const float angle = std::atan2(dx, dy);
        if (!g.wheelGrabbed[hand])
        {
            if (squeezed && close)
            {
                g.wheelGrabbed[hand] = true;
                g.wheelGrabOffset[hand] = UnwrapDelta(angle - g.wheelAngle);
                // Keep the real controller angle as the input baseline and
                // render the hand at that same point on the rim.
                g.wheelGrabAngle[hand] = angle;
                const unsigned other = 1u - hand;
                g.wheelGrabTarget[hand] = g.wheelGrabbed[other]
                    ? g.wheelGrabTarget[other] : g.wheelAngle;
                // Store the physical orientation at the actual grab angle so
                // the hand rotates rigidly with that point on the wheel.
                g.wheelGrabOrientAngle[hand] = angle;
                g.wheelGrabOrientRot[hand] = pose;
                g.wheelGrabOrientRot[hand].Row(3).Set(0.0f, 0.0f, 0.0f);
            }
        }
        else if (!squeezed)
        {
            g.wheelGrabbed[hand] = false;
        }
        else if (radial > kMinRadial)
        {
            // Accumulate the shortest angular movement from the previous
            // frame.  Using an absolute wrapped atan2 angle here made the
            // target jump from one steering lock to the other whenever a hand
            // crossed the +/-pi seam.  It also made off-slot grabs snap.
            float delta = UnwrapDelta(angle - g.wheelGrabAngle[hand]);
            delta = std::max(-kMaxGrabDelta, std::min(kMaxGrabDelta, delta));
            g.wheelGrabTarget[hand] += delta;
            g.wheelGrabTarget[hand] = std::max(-kVrWheelMaxAngle,
                                               std::min(kVrWheelMaxAngle,
                                                        g.wheelGrabTarget[hand]));
            targetSum += g.wheelGrabTarget[hand];
            ++targetCount;
            g.wheelGrabAngle[hand] = angle;
        }
    }

    if (targetCount > 0)
    {
        const float target = targetSum / static_cast<float>(targetCount);
        g.wheelAngle += (target - g.wheelAngle) * kAngleSmooth;
        g.wheelAngle = std::max(-kVrWheelMaxAngle, std::min(kVrWheelMaxAngle, g.wheelAngle));
    }
    else if (!g.wheelGrabbed[0] && !g.wheelGrabbed[1])
    {
        g.wheelAngle *= kCentreReturn;
        if (std::fabs(g.wheelAngle) < 0.002f)
            g.wheelAngle = 0.0f;
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
    const int64_t rgba8 = 0x8058;
    const int64_t srgb8Alpha8 = 0x8C43;
    int64_t chosen = formats.empty() ? rgba8 : formats[0];
    // Pure3D produces display-ready sRGB values (the phone backbuffer path
    // writes them directly). Mark the XR images as sRGB so the compositor
    // interprets those values correctly instead of treating them as linear,
    // which makes the result look bright and desaturated.
    for (uint32_t i=0; i<formatCount; ++i)
        if (formats[i] == srgb8Alpha8) { chosen=formats[i]; break; }
    if (chosen != srgb8Alpha8)
        for (uint32_t i=0; i<formatCount; ++i)
            if (formats[i] == rgba8) { chosen=formats[i]; break; }
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
        ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        ci.format=chosen; ci.sampleCount=1; ci.width=e.width; ci.height=e.height;
        ci.faceCount=1; ci.arraySize=2; ci.mipCount=1;
        if(i==1)
        {
            e.width=g.eyes[0].width;e.height=g.eyes[0].height;
            e.swapchain=g.eyes[0].swapchain;e.images=g.eyes[0].images;
            break;
        }
        if (XR_FAILED(g.CreateSwapchain(g.session, &ci, &e.swapchain))) return false;
        uint32_t imageCount=0;
        g.EnumerateSwapchainImages(e.swapchain, 0, &imageCount, NULL);
        e.images.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        if (XR_FAILED(g.EnumerateSwapchainImages(e.swapchain, imageCount, &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data())))) return false;
        g.multiviewFramebufferValid.assign(imageCount,0);
    }
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
}

static void DestroySwapchainsAndRenderTargets()
{
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
        !create("hand_pose", "Tracked hand pose", XR_ACTION_TYPE_POSE_INPUT, &g.handPoseAction, true)) return false;

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
        {g.handPoseAction, path("/user/hand/right/input/grip/pose")}
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
    set("LeftTrigger",value(g.leftTriggerAction));
    set("RightTrigger",value(g.rightTriggerAction));
    g.gripValue[0]=value(g.leftGripAction);
    g.gripValue[1]=value(g.rightGripAction);
    set("Black",g.gripValue[0]>0.55f?1.0f:0.0f);
    // Keep the right grip on its own virtual button. Folding it into X made
    // one squeeze trigger both a face-button action and the grip action.
    // In wheel mode squeeze belongs exclusively to grabbing the rim. This
    // prevents the right hand from applying the handbrake while steering.
    set("White",(!(g.vrModeEnabled && g.vehicleControlMode==1) &&
                  g.gripValue[1]>0.55f)?1.0f:0.0f);
    set("LeftThumb",boolean(g.leftStickClickAction)?1.0f:0.0f);
    set("RightThumb",boolean(g.rightStickClickAction)?1.0f:0.0f);
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
void SetSpatialHudEnabled(bool enabled){ g.spatialHudEnabled=enabled; SaveVrSettings(); }
// VR uses the spatial HUD unconditionally.  The legacy HUD remains active
// only when VR mode itself is disabled.
bool IsSpatialHudEnabled(){ return g.vrModeEnabled; }
bool IsSpatialHudConfigured(){ return g.spatialHudEnabled; }
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
    SaveVrSettings();
}
int GetVehicleControlMode(){ return g.vehicleControlMode; }
bool IsThirdPersonVehicleMode(){ return g.vehicleControlMode==2; }
bool GetVrSteeringWheelValue(float* value)
{
    if (!value || !g.vrModeEnabled || g.vehicleControlMode!=1) return false;

    float t = g.wheelAngle / kVrWheelMaxAngle; // [-1, 1]
    const float sign = (t >= 0.0f) ? 1.0f : -1.0f;
    // Mild ease-in: still softer near centre for lane control, but closer to
    // linear so lock comes on sooner for sharp corners.
    t = sign * std::pow(std::fabs(t), 1.30f);
    if (std::fabs(t) < kSteerOutputDeadzone)
        t = 0.0f;
    else
    {
        // Remap [deadzone..1] -> [0..1] so the deadzone does not waste travel.
        const float mag = (std::fabs(t) - kSteerOutputDeadzone) / (1.0f - kSteerOutputDeadzone);
        t = sign * std::max(0.0f, std::min(1.0f, mag));
    }
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
    g.gtaoEnabled=true;
    g.renderScale=1.0f;
    g.appliedRenderScale=1.0f;
    g.refreshRate=72.0f;
    g.vehicleControlMode=0;
    g.vehicleLightMode=1;
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
    ci.enabledExtensionCount=4; ci.enabledExtensionNames=extensions;
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
    if (!CreateInputActions()) { XRERR("controller action creation failed"); return false; }
    XrSystemGetInfo si={XR_TYPE_SYSTEM_GET_INFO}; si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (XR_FAILED(g.GetSystem(g.instance,&si,&g.system))) { XRERR("no HMD system"); return false; }
    XrGraphicsRequirementsOpenGLESKHR req={XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    if (XR_FAILED(g.GetOpenGLESGraphicsRequirementsKHR(g.instance,g.system,&req))) return false;
    XrGraphicsBindingOpenGLESAndroidKHR binding={XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    binding.display=eglGetCurrentDisplay(); binding.context=eglGetCurrentContext();
    binding.config=0;
    EGLint configId=0; eglQueryContext(binding.display,binding.context,EGL_CONFIG_ID,&configId);
    EGLint attrs[]={EGL_CONFIG_ID,configId,EGL_NONE}; EGLint found=0;
    eglChooseConfig(binding.display,attrs,&binding.config,1,&found);
    XrSessionCreateInfo sci={XR_TYPE_SESSION_CREATE_INFO}; sci.next=&binding; sci.systemId=g.system;
    if (!found || XR_FAILED(g.CreateSession(g.instance,&sci,&g.session))) { XRERR("xrCreateSession failed"); return false; }
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
}

void Shutdown()
{
    DestroySwapchainsAndRenderTargets();
    for(unsigned i=0;i<2;++i) if(g.handSpaces[i]) g.DestroySpace(g.handSpaces[i]);
    if(g.space) g.DestroySpace(g.space); if(g.session) g.DestroySession(g.session);
    if(g.actionSet) g.DestroyActionSet(g.actionSet);
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
        glFinish();
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
static void ApplyIrisBlackout()
{
    static float alpha=0.0f;
    static Uint32 lastTicks=0;
    const Uint32 now=SDL_GetTicks();
    const float dt=lastTicks?std::min(0.1f,(now-lastTicks)*0.001f):0.0f;
    lastTicks=now;
    const float target=g.irisBlackoutTarget?1.0f:0.0f;
    const float step=dt*3.5f;
    if(alpha<target)alpha=std::min(target,alpha+step);
    else if(alpha>target)alpha=std::max(target,alpha-step);
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
bool IsMultiviewAvailable(){return g.multiviewAvailable;}
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
}
void EndMultiview()
{
    if(!g.multiviewRendering)return;
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
}
bool BeginEye(unsigned eye)
{
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
void EndEye(unsigned eye)
{
    // Scrooby's GUI layer is not guaranteed to be submitted for both legacy
    // render passes. Present the cached radar explicitly while each OpenXR eye
    // target is still bound, so its 2D frame cannot remain left-eye-only.
    DrawRadarPlane();
    DrawMissionHudPlanes();
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
}
void SetWorldRendering(bool enabled){ g.worldRendering=enabled; }
void SetEmbeddedHudRendering(bool enabled){ g.embeddedHudRendering=enabled; }
bool IsEmbeddedHudRendering(){ return g.embeddedHudRendering; }
void SetRadarRendering(bool enabled){ g.radarRendering=enabled && g.vrModeEnabled; }
bool IsRadarRendering(){ return g.radarRendering; }
void PrepareRadarDraw()
{
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
}
bool GetActiveRadarProjection(rmt::Matrix* out,int* width,int* height)
{
    if(!out || !width || !height || !g.radarRendering) return false;
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
    if(!IsSpatialHudEnabled() || !g.activeEye) return false;
    g.missionHudActiveSlot=-1;
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
    return true;
}

void EndRadarCapture()
{
    if(!g.radarRendering) return;
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
}

bool BeginMissionHudCapture(unsigned slot,int xMin,int yMin,int xMax,int yMax)
{
    if(!IsSpatialHudEnabled() || !g.activeEye || slot>=State::MISSION_HUD_COUNT)
        return false;
    const int rect[4]={xMin,yMin,xMax,yMax};
    if(std::memcmp(g.missionHudRect[slot],rect,sizeof(rect))!=0)
    {
        std::memcpy(g.missionHudRect[slot],rect,sizeof(rect));
        g.missionHudCropValid[slot]=false;
    }
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
    return true;
}

void EndMissionHudCapture()
{
    if(g.missionHudActiveSlot<0) return;
    const int slot=g.missionHudActiveSlot;
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
    std::memset(g.missionHudUv[slot],0,sizeof(g.missionHudUv[slot]));
    std::memset(g.missionHudRect[slot],0,sizeof(g.missionHudRect[slot]));
}

void CaptureSpatialCoinIcon()
{
    if(g.missionHudTexture[4] && g.missionHudCropValid[4])
    {
        g.missionHudVisible[4]=true;
        return;
    }
    // Use the original Pure3D coin, but submit it from one centralized point.
    // A second capture from FeGroup and a forced inverted cull mode were added
    // together and caused the shared world material to remain on its legacy
    // GLES program. Both have been removed.
    p3d::pddi->PushState(PDDI_STATE_ALL);
    if(BeginMissionHudCapture(4,0,0,640,480))
    {
        const pddiProjectionMode mode=p3d::pddi->GetProjectionMode();
        p3d::pddi->SetProjectionMode(mode);
        GetCoinManager()->HUDRender(true);
        EndMissionHudCapture();
    }
    p3d::pddi->PopState(PDDI_STATE_ALL);
}

static void DrawRadarPlane()
{
    if(!IsSpatialHudEnabled() || !g.activeEye || !g.cullingBaseValid || !g.radarCropValid ||
       !g.radarTexture || !g.radarProgram) return;

    Eye& eye=g.eyes[g.activeEye-1];
    rmt::Matrix anchor;
    Character* player=GetCharacterManager()->GetCharacter(0);
    const bool inCar=player&&player->IsInCar();
    const bool fixedToVehicle=inCar&&!IsThirdPersonVehicleMode();
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
    const float halfSize=fixedToVehicle?0.115f:0.0575f;
    const float xy[4][2]={{-halfSize,-halfSize},{halfSize,-halfSize},
                          {-halfSize,halfSize},{halfSize,halfSize}};
    const float uv[4][2]={{g.radarUv[0],g.radarUv[1]},{g.radarUv[2],g.radarUv[1]},
                          {g.radarUv[0],g.radarUv[3]},{g.radarUv[2],g.radarUv[3]}};
    float vertices[24];
    for(int i=0;i<4;++i){rmt::Vector4 p(xy[i][0],xy[i][1],0,1);p.Transform(mvp);vertices[i*6]=p.x;vertices[i*6+1]=p.y;vertices[i*6+2]=p.z;vertices[i*6+3]=p.w;vertices[i*6+4]=uv[i][0];vertices[i*6+5]=uv[i][1];}
    static bool planeLogged[2]={false,false};
    const unsigned radarEyeIndex=g.activeEye-1;
    if(!planeLogged[radarEyeIndex])
    {
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

static void DrawMissionHudQuad(GLuint texture,const float* uv,
                               const rmt::Matrix& anchor,float height,
                               float aspect,Eye& eye)
{
    const rmt::Matrix eyeLocal=PoseToGame(RelativePose(g.origin,eye.view.pose));
    rmt::Matrix eyeWorld,worldToEye,anchorToEye,proj,mvp;
    eyeWorld.Mult(eyeLocal,g.cullingBaseCamera); worldToEye.InvertOrtho(eyeWorld);
    anchorToEye.Mult(anchor,worldToEye); MakeProjection(eye.view.fov,0.05f,1000.0f,&proj);
    mvp.MultFull(anchorToEye,proj);
    aspect=std::max(0.4f,std::min(4.0f,aspect));
    const float halfY=height*0.5f,halfX=halfY*aspect;
    const float xy[4][2]={{-halfX,-halfY},{halfX,-halfY},{-halfX,halfY},{halfX,halfY}};
    const float tc[4][2]={{uv[0],uv[1]},{uv[2],uv[1]},{uv[0],uv[3]},{uv[2],uv[3]}};
    float vertices[24];
    for(int i=0;i<4;++i){rmt::Vector4 p(xy[i][0],xy[i][1],0,1);p.Transform(mvp);
        vertices[i*6]=p.x;vertices[i*6+1]=p.y;vertices[i*6+2]=p.z;vertices[i*6+3]=p.w;
        vertices[i*6+4]=tc[i][0];vertices[i*6+5]=tc[i][1];}
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
}

static void DrawMissionHudPlanes()
{
    if(!IsSpatialHudEnabled()||!g.activeEye||!g.cullingBaseValid||!g.radarProgram) return;
    bool anyVisible=false;
    for(unsigned slot=0;slot<State::MISSION_HUD_COUNT;++slot)
        anyVisible=anyVisible||g.missionHudVisible[slot];
    if(!anyVisible) return;
    Eye& eye=g.eyes[g.activeEye-1];
    const rmt::Matrix eyeLocal=PoseToGame(RelativePose(g.origin,eye.view.pose));
    rmt::Matrix eyeWorld;eyeWorld.Mult(eyeLocal,g.cullingBaseCamera);
    Character* player=GetCharacterManager()->GetCharacter(0);
    const bool inCar=player&&player->IsInCar();
    const bool fixedToVehicle=inCar&&!IsThirdPersonVehicleMode();
    rmt::Matrix base;
    if(fixedToVehicle){base.Identity();base.Row(3)=kVrWheelCentre;
        const rmt::Matrix local=base;base.Mult(local,g.cullingBaseCamera);}
    else{if(!g.handPoseValid[0]) return;
        const rmt::Matrix hand=PoseToGame(RelativePose(g.origin,g.handPoses[0]));
        rmt::Matrix handWorld;handWorld.Mult(hand,g.cullingBaseCamera);
        base.Identity();base.Row(0)=eyeWorld.Row(0);base.Row(1)=eyeWorld.Row(1);
        base.Row(2)=eyeWorld.Row(2);
        const rmt::Vector towardElbow=handWorld.Row(1)*0.10f;
        base.Row(3)=handWorld.Row(3)+towardElbow;}
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
    unsigned missionStackRow=0;
    for(unsigned slot=0;slot<State::MISSION_HUD_COUNT;++slot){
        // Slot 4 is the persistent 3D icon belonging to counter slot 3.  Its
        // clean texture is refreshed independently, but it must follow the
        // counter's visibility (including the pause-menu counter).
        const bool visible=slot==4?g.missionHudVisible[3]:g.missionHudVisible[slot];
        if(!visible||!g.missionHudTexture[slot]||
           !g.missionHudCropValid[slot]) continue;
        rmt::Matrix anchor=base;
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
        else if(slot==3 || slot==4)
        {
            if(fixedToVehicle)
            {
                anchor.Identity();
                anchor.Row(3).Set(-0.25f,kVrWheelCentre.y,0.54f);
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
        // Vehicle and wrist layouts share the same centred stack above the
        // minimap/current timer. Extra mission counters occupy neighbouring
        // rows, so a timed collection or race remains readable in VR.
        const bool missionStackSlot=slot==2 || slot==6 || slot==7 ||
            slot==8 || slot==9 || slot==10 || slot==11 || slot==12;
        const float verticalOffset=missionStackSlot?
            0.080f+0.060f*missionStackRow++:(slot==0?0.0f:-0.085f);
        if(slot!=3 && slot!=4 && slot!=5) anchor.Row(3)=base.Row(3)+base.Row(1)*verticalOffset;
        if(slot==4) anchor.Row(3)=anchor.Row(3)-anchor.Row(0)*0.070f;
        const float aspect=g.missionHudAspect[slot]>0.0f?
            g.missionHudAspect[slot]:1.0f;
        DrawMissionHudQuad(g.missionHudTexture[slot],g.missionHudUv[slot],anchor,
                           (slot==1||slot==9)?0.060f:((slot==2||slot==6||slot==7||slot==8||slot==12)?0.0425f:((slot==10||slot==11)?0.050f:(slot==3?0.055f:(slot==4?0.105f:(slot==5?0.055f:0.085f))))),aspect,eye);
    }
    glBindTexture(GL_TEXTURE_2D,oldTexture);glActiveTexture(oldActive);
    glBindBuffer(GL_ARRAY_BUFFER,oldArray);glUseProgram(oldProgram);
    if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
    if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
    if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
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
    if(showWheel)
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
        const int segments=32,sides=6,count=segments*sides*6;
        pddiPrimStream* wheelStream=p3d::pddi->BeginPrims(wheelShader->GetShader(),
            PDDI_PRIM_TRIANGLES,PDDI_V_N,count);
        if(wheelStream)
        {
            const float tube=0.012f;
            for(int segment=0;segment<segments;++segment)
            for(int side=0;side<sides;++side)
            {
                const int nextSegment=(segment+1)%segments,nextSide=(side+1)%sides;
                const int corners[6][2]={{segment,side},{nextSegment,side},
                    {nextSegment,nextSide},{segment,side},{nextSegment,nextSide},{segment,nextSide}};
                for(int vertex=0;vertex<6;++vertex)
                {
                    const float a=(corners[vertex][0]*6.28318531f/segments)+g.wheelAngle;
                    const float b=corners[vertex][1]*6.28318531f/sides;
                    const float ring=kVrWheelRadius+tube*std::cos(b);
                    rmt::Vector local(kVrWheelCentre.x+ring*std::sin(a),
                                      kVrWheelCentre.y+ring*std::cos(a),
                                      kVrWheelCentre.z+tube*std::sin(b));
                    rmt::Vector normal(std::sin(a)*std::cos(b),
                                       std::cos(a)*std::cos(b),std::sin(b));
                    rmt::Vector worldPos,worldNormal;
                    // cullingBaseCamera is the car-mounted camera before the
                    // tracked head pose is applied. The eye camera would make
                    // the wheel follow every movement of the player's head.
                    g.cullingBaseCamera.Transform(local,&worldPos);
                    g.cullingBaseCamera.RotateVector(normal,&worldNormal);
                    worldNormal.NormalizeSafe();
                    wheelStream->Normal(worldNormal.x,worldNormal.y,worldNormal.z);
                    wheelStream->Coord(worldPos.x,worldPos.y,worldPos.z);
                }
            }
            p3d::pddi->EndPrims(wheelStream);
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
                // Rigidly glued to the rim: ride the arc with the wheel.
                // Position follows the rim angle; orientation rotates by the
                // same delta around the wheel axis so the hand stays planted
                // on the wheel (one solid piece), not twisting on its own.
                const float angle=g.wheelAngle+g.wheelGrabOffset[hand];
                const float s=std::sin(angle);
                const float c=std::cos(angle);
                // Opposite of the previous "spin the wrong way" sign so the
                // hands ride with the wheel mesh instead of against it.
                const float delta=UnwrapDelta(g.wheelGrabOrientAngle[hand] - angle);
                RotateOrientAroundWheelAxis(delta, g.wheelGrabOrientRot[hand], local);
                local.Row(3).Set(kVrWheelCentre.x+kVrWheelRadius*s,
                                 kVrWheelCentre.y+kVrWheelRadius*c,
                                 kVrWheelCentre.z);
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
    if(!g.frameBegun)return; glBindFramebuffer(GL_FRAMEBUFFER,0);
    if(g.perfQueryActive)
    {
        g.EndQueryEXT(GL_TIME_ELAPSED_EXT);
        g.perfQueryPending[g.perfQueryIndex]=true;
        g.perfQueryIndex=(g.perfQueryIndex+1)%4;
        g.perfQueryActive=false;
    }
    const Uint64 renderEnd=SDL_GetPerformanceCounter();
    XrCompositionLayerProjectionView pv[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    for(unsigned i=0;i<2;++i){ pv[i].pose=g.eyes[i].view.pose; pv[i].fov=g.eyes[i].view.fov; pv[i].subImage.swapchain=g.eyes[0].swapchain; pv[i].subImage.imageRect.extent.width=g.eyes[i].width; pv[i].subImage.imageRect.extent.height=g.eyes[i].height; pv[i].subImage.imageArrayIndex=i; }
    XrCompositionLayerProjection layer={XR_TYPE_COMPOSITION_LAYER_PROJECTION}; layer.space=g.space; layer.viewCount=2; layer.views=pv;
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
        XRLOG("VR PERF: wait %.2f/%.2f render %.2f/%.2f submit %.2f/%.2f GPU %.2f | draw %u idx %u vert %u tri %u drawCPU %.2f | mat %u/%.2f upload %u/%uKB/%.2f | layer gui %.2f pres %.2f level %.2f missions %.2f | world setup %.2f scene %.2f opaque %.2f trans %.2f guts %.2f CSM %.2f misc %.2f skin %.2f",
              g.perfWaitSum/g.perfFrames,g.perfWaitMax,
              g.perfRenderSum/g.perfFrames,g.perfRenderMax,
              g.perfSubmitSum/g.perfFrames,g.perfSubmitMax,g.perfGpuLast,
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

#if defined(RAD_ANDROID)
#define XR_USE_PLATFORM_ANDROID
#endif
#define XR_USE_GRAPHICS_API_OPENGL_ES
#if defined(SRR2_VR_RENDERER_VULKAN)
#define XR_USE_GRAPHICS_API_VULKAN
#endif
#if defined(RAD_ANDROID)
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#else
typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned char GLboolean;
#endif
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <vr/openxr_shared_hud.h>
#include <vr/openxr_shared_graphics.h>
#include <vr/openxr_shared_render.h>
#include <vr/openxr_shared_state.h>
#include <vr/openxr_shared_menu.h>
#include <vr/openxrmanager.h>
#include <p3d/camera.hpp>
#include <presentation/gui/guiscreen.h>
#include <worldsim/character/character.h>
#include <worldsim/character/charactermanager.h>
#include <worldsim/coins/coinmanager.h>

#define XRLOG(...) SDL_Log("OpenXR HUD: " __VA_ARGS__)
#define XRERR(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"OpenXR HUD: " __VA_ARGS__)

namespace SharOpenXR
{
namespace
{
const int RADAR_TEXTURE_WIDTH=1440;
const int RADAR_TEXTURE_HEIGHT=1080;
const int MISSION_HUD_TEXTURE_WIDTH=720;
const int MISSION_HUD_TEXTURE_HEIGHT=540;
static int MissionHudTextureWidth(unsigned slot){return (slot==4||slot==5)?1440:MISSION_HUD_TEXTURE_WIDTH;}
static int MissionHudTextureHeight(unsigned slot){return (slot==4||slot==5)?1080:MISSION_HUD_TEXTURE_HEIGHT;}

struct Eye
{
    int width,height;
    XrView view;
#if defined(SRR2_VR_RENDERER_VULKAN)
    VkFormat vulkanFormat;
    std::vector<VkImage> vulkanImages;
#endif
};

struct HudState
{
    Eye eyes[2];
    uint32_t activeEye,multiviewImageIndex;
    bool multiviewImageAcquired,embeddedHudRendering,cullingBaseValid,vrModeEnabled;
    XrPosef origin,handPoses[2];
    bool handPoseValid[2];
    rmt::Matrix cullingBaseCamera;
    rmt::Vector activeWheelCentre;
    bool radarRendering,gameplayHudCaptureActive;
    const void* gameplayHudScreen;
    unsigned radarDrawCount;
    GLuint radarFramebuffer,radarTexture,radarDisplayTexture,radarDepthBuffer,radarProgram,hudQuadVbo,irisBlackProgram;
    GLint radarSavedFramebuffer,radarSavedViewport[4],radarSavedScissor[4];
    bool radarSavedScissorEnabled,radarSavedDepthEnabled,radarSavedCullEnabled;
    GLboolean radarSavedColourMask[4];
    float radarUv[4];
    int radarRect[4],radarMapRect[4];
    bool radarCropValid;
    enum { MISSION_HUD_COUNT=20 };
#if defined(SRR2_VR_RENDERER_VULKAN)
    VkImage vulkanRadarImage,vulkanGameplayHudImage;
    VkDeviceMemory vulkanRadarMemory,vulkanGameplayHudMemory;
    VkImageView vulkanRadarView,vulkanGameplayHudView;
    VkSampler vulkanRadarSampler,vulkanGameplayHudSampler;
    VkDescriptorSet vulkanRadarDescriptor,vulkanGameplayHudDescriptor;
    bool vulkanRadarInitialized,vulkanCaptureActive,vulkanGameplayHudInitialized;
    VkImage vulkanMissionHudImage[MISSION_HUD_COUNT];
    VkDeviceMemory vulkanMissionHudMemory[MISSION_HUD_COUNT];
    VkImageView vulkanMissionHudView[MISSION_HUD_COUNT];
    VkSampler vulkanMissionHudSampler[MISSION_HUD_COUNT];
    VkDescriptorSet vulkanMissionHudDescriptor[MISSION_HUD_COUNT];
    bool vulkanMissionHudInitialized[MISSION_HUD_COUNT];
#endif
    GLuint missionHudFramebuffer[MISSION_HUD_COUNT],missionHudTexture[MISSION_HUD_COUNT];
    float missionHudUv[MISSION_HUD_COUNT][4];
    int missionHudRect[MISSION_HUD_COUNT][4];
    rmt::Matrix missionHudLayout[MISSION_HUD_COUNT];
    bool missionHudLayoutValid[MISSION_HUD_COUNT];
    float missionHudAspect[MISSION_HUD_COUNT];
    bool missionHudVisible[MISSION_HUD_COUNT],missionHudCropValid[MISSION_HUD_COUNT];
    uint64_t hudFrameSerial,radarCaptureFrame,missionHudCaptureFrame[MISSION_HUD_COUNT];
    int missionHudActiveSlot;
    GLuint hudCaptureFramebuffer;
    bool pauseCoinVisible;
    int spatialCoinAuthoredX,spatialCoinAuthoredY;
    bool spatialCoinAuthoredPositionValid;
    int missionObjectiveFrameRect[4],missionObjectiveIconRect[4];
    bool missionObjectiveFrameRectValid,missionObjectiveIconRectValid,irisBlackoutTarget;
#if defined(SRR2_VR_RENDERER_VULKAN)
    float irisBlackoutAlpha;
    Uint32 irisBlackoutTicks;
#endif
};

HudState g={};
SharedHudRuntimeProvider runtimeProvider=NULL;
VulkanContext& gVulkanContext=GetVulkanContext();

static bool RefreshRuntime()
{
    SharedHudRuntime runtime={};
    if(!runtimeProvider || !runtimeProvider(&runtime))return false;
    g.activeEye=runtime.activeEye;g.multiviewImageAcquired=runtime.multiviewImageAcquired;
    g.embeddedHudRendering=runtime.embeddedHudRendering;g.cullingBaseValid=runtime.cullingBaseValid;
    g.vrModeEnabled=runtime.vrModeEnabled;
    g.origin=runtime.origin;g.cullingBaseCamera=runtime.cullingBaseCamera;
    g.activeWheelCentre=runtime.activeWheelCentre;
    for(unsigned i=0;i<2;++i){g.eyes[i].view=runtime.views[i];g.eyes[i].width=runtime.eyeWidth[i];
        g.eyes[i].height=runtime.eyeHeight[i];g.handPoses[i]=runtime.handPoses[i];g.handPoseValid[i]=runtime.handPoseValid[i];}
#if defined(SRR2_VR_RENDERER_VULKAN)
    g.eyes[0].vulkanFormat=runtime.renderFormat;
    if(g.eyes[0].vulkanImages.size()<=g.multiviewImageIndex)g.eyes[0].vulkanImages.resize(g.multiviewImageIndex+1,VK_NULL_HANDLE);
    g.eyes[0].vulkanImages[g.multiviewImageIndex]=runtime.renderImage;
#endif
    return true;
}

static XrPosef RelativePose(const XrPosef& origin,const XrPosef& pose){return SharedRender::RelativePose(origin,pose);}
static rmt::Matrix PoseToGame(const XrPosef& pose){return SharedRender::PoseToGame(pose);}
static void MakeProjection(const XrFovf& f,float n,float z,rmt::Matrix* m){SharedRender::MakeProjection(f,n,z,m);}

}

static void DrawRadarPlane();
static void DrawMissionHudPlanes();
static void DrawGameplayHud();

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

void SetRadarRendering(bool enabled){
    RefreshRuntime();
    g.radarRendering=enabled&&g.vrModeEnabled;
}
bool IsRadarRendering(){
    return g.radarRendering;
}
void SetGameplayHudScreen(const void* screen)
{
    g.gameplayHudScreen=screen;
    XRLOG("gameplay HUD screen %s (%p)",screen?"registered":"cleared",screen);
}
bool IsGameplayHudScreen(const void* screen)
{
    return screen!=NULL && screen==g.gameplayHudScreen;
}
bool IsGameplayHudCaptureActive(){
return g.gameplayHudCaptureActive;
}
bool IsMissionHudCaptureActive(){
return g.missionHudActiveSlot>=0;
}

bool BeginGameplayHudCapture()
{
    RefreshRuntime();
#if defined(SRR2_VR_RENDERER_VULKAN)
    if(IsSpatialHudEnabled() || g.gameplayHudCaptureActive ||
       !g.multiviewImageAcquired || g.activeEye>1)
        return false;
    if(!g.vulkanGameplayHudImage && !gVulkanContext.CreateTexture2D(
        RADAR_TEXTURE_WIDTH,RADAR_TEXTURE_HEIGHT,1,&g.vulkanGameplayHudImage,
        &g.vulkanGameplayHudMemory,&g.vulkanGameplayHudView,
        &g.vulkanGameplayHudSampler,&g.vulkanGameplayHudDescriptor))
    {
        XRERR("Vulkan gameplay HUD texture creation failed");
        return false;
    }
    if(!gVulkanContext.BeginOffscreenTarget(g.vulkanGameplayHudImage,
                                             g.vulkanGameplayHudInitialized))
    {
        XRERR("Vulkan gameplay HUD offscreen begin failed");
        return false;
    }
    g.gameplayHudCaptureActive=true;
    g.vulkanCaptureActive=true;
    return true;
#else
    return false;
#endif
}

void EndGameplayHudCapture()
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    if(!g.gameplayHudCaptureActive)return;
    g.gameplayHudCaptureActive=false;
    g.vulkanCaptureActive=false;
    if(gVulkanContext.EndOffscreenTarget(g.vulkanGameplayHudImage))
        g.vulkanGameplayHudInitialized=true;
#endif
}
void PrepareRadarDraw()
{
    RefreshRuntime();
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
    RefreshRuntime();
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
    RefreshRuntime();
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
    RefreshRuntime();
    if(!g.multiviewImageAcquired || g.activeEye>1 ||
       g.gameplayHudCaptureActive || slot>=HudState::MISSION_HUD_COUNT
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
    static bool captureLogged[HudState::MISSION_HUD_COUNT]={false};
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
    if(slot>=HudState::MISSION_HUD_COUNT) return;
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
    if(slot>=HudState::MISSION_HUD_COUNT) return;
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
    (void)xMin;(void)yMin;(void)xMax;(void)yMax;
}

#if 1
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
    if(fixedToVehicle){anchor.Identity();anchor.Row(3).Set(0.30f,g.activeWheelCentre.y,0.54f);
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
        gVulkanContext.DrawPddiGeometry(g.eyes[0].vulkanImages[imageIndex],
            NormalizeVulkanRenderFormat(g.eyes[0].vulkanFormat),eye.width,eye.height,g.activeEye-1,
            buffer,VK_NULL_HANDLE,offset,radarSegments*3,0,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            identity,identity,g.vulkanRadarDescriptor,VK_NULL_HANDLE,
            VK_NULL_HANDLE,VK_NULL_HANDLE,material);
    return;
#endif
#if !defined(SRR2_VR_RENDERER_VULKAN)
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
#endif
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
        gVulkanContext.DrawPddiGeometry(g.eyes[0].vulkanImages[imageIndex],
            NormalizeVulkanRenderFormat(g.eyes[0].vulkanFormat),eye.width,eye.height,g.activeEye-1,buffer,
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
    for(unsigned slot=0;slot<HudState::MISSION_HUD_COUNT;++slot)
        anyVisible=anyVisible||g.missionHudVisible[slot];
    if(!anyVisible) return;
#if defined(SRR2_VR_RENDERER_VULKAN)
    static bool compositeLogged[2]={false,false};
    if(!compositeLogged[g.activeEye-1])
    {
        unsigned visibleCount=0,readyCount=0;
        for(unsigned slot=0;slot<HudState::MISSION_HUD_COUNT;++slot)
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
        if(wheelCentre.MagnitudeSqr()<0.0001f) wheelCentre=g.activeWheelCentre;
        base.Identity();base.Row(3)=wheelCentre;
        const rmt::Matrix local=base;base.Mult(local,g.cullingBaseCamera);}
    else{
        base.Identity();base.Row(0)=eyeWorld.Row(0);base.Row(1)=eyeWorld.Row(1);
        base.Row(2)=eyeWorld.Row(2);
        if(g.handPoseValid[0])
        {
            const rmt::Matrix hand=PoseToGame(RelativePose(g.origin,g.handPoses[0]));
            rmt::Matrix handWorld;handWorld.Mult(hand,g.cullingBaseCamera);
            const rmt::Vector towardElbow=handWorld.Row(1)*0.10f;
            base.Row(3)=handWorld.Row(3)+towardElbow;
            // Objective/message used to bypass the wrist-adjusted base and
            // attach to the grip centre. Keep every left-hand HUD element on
            // the same wrist point as the rest of the panel stack.
            objectiveHandPoint=base.Row(3);
            objectiveHandPointValid=true;
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
    for(unsigned drawIndex=0;drawIndex<HudState::MISSION_HUD_COUNT;++drawIndex){
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
        const bool centreNotification=slot>=14 && slot<=19;
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
                anchor.Row(3).Set(0.30f,g.activeWheelCentre.y+0.15f,0.54f);
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
                anchor.Row(3).Set(0.30f,g.activeWheelCentre.y,0.54f);
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
                anchor.Row(3).Set(0.30f,g.activeWheelCentre.y-0.17f,0.54f);
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

#endif // !SRR2_VR_RENDERER_VULKAN: legacy GLES HUD compositor


void SetPauseCoinVisible(bool visible){g.pauseCoinVisible=visible;
}
void SetIrisBlackout(bool black){
    if(g.irisBlackoutTarget!=black)
        XRLOG("iris blackout target=%s",black?"closed":"open");
    g.irisBlackoutTarget=black;
}
void DrawPauseCoinIcon()
{
#if !defined(SRR2_VR_RENDERER_VULKAN)
    const unsigned slot=4;
    if(!g.pauseCoinVisible || !g.activeEye ||
       !g.radarProgram || !g.hudQuadVbo ||
       !g.missionHudTexture[slot] || !g.missionHudCropValid[slot]) return;

    rmt::Matrix projection;
    int width=0,height=0;
    Eye& eye=g.eyes[g.activeEye-1];
    const bool haveProjection=SharOpenXR::GetSharedVrMenu().GetProjection(
        true,eye.view,eye.width,eye.height,&projection,&width,&height,false);
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
#endif
}

void SetSharedHudRuntimeProvider(SharedHudRuntimeProvider provider){runtimeProvider=provider;}
void SharedHudBeginFrame(){++g.hudFrameSerial;if(g.hudFrameSerial==0)++g.hudFrameSerial;std::memset(g.missionHudVisible,0,sizeof(g.missionHudVisible));RefreshRuntime();}
void SharedHudResetVisible(){std::memset(g.missionHudVisible,0,sizeof(g.missionHudVisible));}
void ShutdownSharedHud()
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    if(g.vulkanRadarImage)gVulkanContext.DestroyTexture(g.vulkanRadarImage,g.vulkanRadarMemory,g.vulkanRadarView,g.vulkanRadarSampler,g.vulkanRadarDescriptor);
    if(g.vulkanGameplayHudImage)gVulkanContext.DestroyTexture(g.vulkanGameplayHudImage,g.vulkanGameplayHudMemory,g.vulkanGameplayHudView,g.vulkanGameplayHudSampler,g.vulkanGameplayHudDescriptor);
    for(unsigned i=0;i<HudState::MISSION_HUD_COUNT;++i)
        if(g.vulkanMissionHudImage[i])gVulkanContext.DestroyTexture(g.vulkanMissionHudImage[i],g.vulkanMissionHudMemory[i],g.vulkanMissionHudView[i],g.vulkanMissionHudSampler[i],g.vulkanMissionHudDescriptor[i]);
#else
    if(g.radarFramebuffer)glDeleteFramebuffers(1,&g.radarFramebuffer);
    if(g.radarDepthBuffer)glDeleteRenderbuffers(1,&g.radarDepthBuffer);
    if(g.radarTexture)glDeleteTextures(1,&g.radarTexture);
    if(g.radarDisplayTexture)glDeleteTextures(1,&g.radarDisplayTexture);
    if(g.radarProgram)glDeleteProgram(g.radarProgram);
    if(g.irisBlackProgram)glDeleteProgram(g.irisBlackProgram);
    if(g.hudQuadVbo)glDeleteBuffers(1,&g.hudQuadVbo);
    glDeleteFramebuffers(HudState::MISSION_HUD_COUNT,g.missionHudFramebuffer);
    glDeleteTextures(HudState::MISSION_HUD_COUNT,g.missionHudTexture);
#endif
    g=HudState();
}
void DrawSharedRadarPlane(){if(RefreshRuntime())DrawRadarPlane();}
void DrawSharedGameplayHud(){if(RefreshRuntime())DrawGameplayHud();}
void DrawSharedMissionHudPlanes(){if(RefreshRuntime())DrawMissionHudPlanes();}
void DrawSharedPauseCoinIcon(){if(RefreshRuntime())DrawPauseCoinIcon();}
void ApplySharedIrisBlackout(){if(RefreshRuntime())ApplyIrisBlackout();}
float UpdateSharedHudIrisAlpha()
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    const Uint32 now=SDL_GetTicks();
    const float dt=g.irisBlackoutTicks?std::min(0.1f,(now-g.irisBlackoutTicks)*0.001f):0.0f;
    g.irisBlackoutTicks=now;
    const float target=g.irisBlackoutTarget?1.0f:0.0f;
    const float step=dt*1.25f;
    if(g.irisBlackoutAlpha<target)g.irisBlackoutAlpha=std::min(target,g.irisBlackoutAlpha+step);
    else if(g.irisBlackoutAlpha>target)g.irisBlackoutAlpha=std::max(target,g.irisBlackoutAlpha-step);
    return g.irisBlackoutAlpha;
#else
    return 0.0f;
#endif
}

#if defined(SRR2_VR_RENDERER_VULKAN)
bool GetSharedHudCaptureTarget(VulkanEyeTarget* target)
{
    RefreshRuntime();
    if(!target||!g.vulkanCaptureActive)return false;
    if(g.gameplayHudCaptureActive)
    {
        if(!g.vulkanGameplayHudImage)return false;
        target->image=g.vulkanGameplayHudImage;target->format=VK_FORMAT_B8G8R8A8_UNORM;
        target->width=RADAR_TEXTURE_WIDTH;target->height=RADAR_TEXTURE_HEIGHT;
        target->arrayLayer=0;target->firstUse=!g.vulkanGameplayHudInitialized;
        return true;
    }
    const bool mission=g.missionHudActiveSlot>=0;
    const unsigned slot=mission?static_cast<unsigned>(g.missionHudActiveSlot):0;
    if((mission&&!g.vulkanMissionHudImage[slot])||(!mission&&!g.vulkanRadarImage))return false;
    target->image=mission?g.vulkanMissionHudImage[slot]:g.vulkanRadarImage;
    target->format=VK_FORMAT_B8G8R8A8_UNORM;
    target->width=mission?MissionHudTextureWidth(slot):RADAR_TEXTURE_WIDTH;
    target->height=mission?MissionHudTextureHeight(slot):RADAR_TEXTURE_HEIGHT;
    target->arrayLayer=0;
    target->firstUse=mission?!g.vulkanMissionHudInitialized[slot]:!g.vulkanRadarInitialized;
    return true;
}
#endif

}

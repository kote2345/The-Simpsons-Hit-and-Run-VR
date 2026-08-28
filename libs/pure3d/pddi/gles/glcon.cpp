//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/glcon.hpp>
#include <pddi/gles/gldev.hpp>
#include <pddi/gles/gldisplay.hpp>
#include <pddi/gles/gltex.hpp>
#include <pddi/gles/glmat.hpp>
#include <pddi/gles/glprog.hpp>

#include <pddi/base/debug.hpp>
#include <math.h>

#if defined(RAD_ANDROID)
#include <vr/openxrmanager.h>
#include <vr/dynamiccubemap.h>
#endif
#include <string.h>
#include <SDL.h>
#include <vector>

#include <microprofile.h>

#if defined(RAD_ANDROID)
int gPglCsmBillboardMode=0; // 0 normal, 1 caster, 2 receiver overlay
static double PglTelemetryMilliseconds()
{
    return SDL_GetPerformanceCounter()*1000.0/SDL_GetPerformanceFrequency();
}

static GLuint gVehicleCubeTexture=0;
static GLuint gVehicleCubeFramebuffer=0;
static GLuint gVehicleCubeDepth=0;
// Keep the complete implementation available while dynamic probes are
// temporarily disabled. Setting this back to true restores capture and makes
// vehicle materials select the samplerCube reflection program again.
static const bool gVehicleCubeEnabled=false;
static bool gVehicleCubeReady=false;
static bool gVehicleCubeCapture=false;
static bool gVehicleCubeSuppressTransparent=false;
static bool gVehicleCubeSkipDraw=false;
static GLint gVehicleCubeSavedFramebuffer=0;
static GLint gVehicleCubeSavedRenderbuffer=0;
static GLint gVehicleCubeSavedViewport[4]={0,0,0,0};
static GLint gVehicleCubeSavedActiveTexture=GL_TEXTURE0;
static GLboolean gVehicleCubeSavedScissor=GL_FALSE;
static GLboolean gVehicleCubeSavedColourMask[4]={GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE};
static GLfloat gVehicleCubeSavedClearColour[4]={0,0,0,0};
static const int VEHICLE_CUBE_SIZE=128;

bool VrHasDynamicVehicleCubeMap(){return gVehicleCubeEnabled&&gVehicleCubeReady;}
bool VrIsDynamicVehicleCubeMapCapture(){return gVehicleCubeCapture;}
void VrSetVehicleCubeMapTransparentSuppression(bool suppress)
{
    gVehicleCubeSuppressTransparent=suppress;
    if(!suppress) gVehicleCubeSkipDraw=false;
    // Force the first material on either side of the scope boundary to publish
    // its alpha state even if it happens to share the previous shader UID.
    pddiBaseShader::ClearCurrentShader();
}
void VrBindDynamicVehicleCubeMap()
{
    glBindTexture(GL_TEXTURE_CUBE_MAP,gVehicleCubeTexture);
}
void VrRestoreVehicleCubeMapRendering(pddiRenderContext* context)
{
    if(context) static_cast<pglContext*>(context)->RestoreAfterVehicleCubeMap();
}

bool VrBeginVehicleCubeMapFace(pddiRenderContext*,int face)
{
    if(!gVehicleCubeEnabled || face<0 || face>=6 || gVehicleCubeCapture)
        return false;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&gVehicleCubeSavedFramebuffer);
    glGetIntegerv(GL_RENDERBUFFER_BINDING,&gVehicleCubeSavedRenderbuffer);
    glGetIntegerv(GL_VIEWPORT,gVehicleCubeSavedViewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&gVehicleCubeSavedActiveTexture);
    glGetFloatv(GL_COLOR_CLEAR_VALUE,gVehicleCubeSavedClearColour);
    gVehicleCubeSavedScissor=glIsEnabled(GL_SCISSOR_TEST);
    glGetBooleanv(GL_COLOR_WRITEMASK,gVehicleCubeSavedColourMask);
    // A cubemap face is an ordinary mono framebuffer.  During the normal VR
    // world pass IsMultiviewRendering() is true, which otherwise makes every
    // PDDI material select its two-view native program while drawing into this
    // single 2D attachment.  Switch program selection to the legacy mono
    // variants before the cube camera and any materials establish their state.
    SharOpenXR::SetMultiviewTargetActive(false);
    if(!gVehicleCubeTexture)
    {
        glGenTextures(1,&gVehicleCubeTexture);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_CUBE_MAP,gVehicleCubeTexture);
        for(int cubeFace=0;cubeFace<6;++cubeFace)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+cubeFace,0,GL_RGBA8,
                         VEHICLE_CUBE_SIZE,VEHICLE_CUBE_SIZE,0,GL_RGBA,
                         GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1,&gVehicleCubeFramebuffer);
        glGenRenderbuffers(1,&gVehicleCubeDepth);
        glBindRenderbuffer(GL_RENDERBUFFER,gVehicleCubeDepth);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,
                              VEHICLE_CUBE_SIZE,VEHICLE_CUBE_SIZE);
        SDL_Log("VR vehicle cubemap: allocated %dx%d incremental probe",
                VEHICLE_CUBE_SIZE,VEHICLE_CUBE_SIZE);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,gVehicleCubeFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,
                           gVehicleCubeTexture,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER,gVehicleCubeDepth);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
    {
        glBindFramebuffer(GL_FRAMEBUFFER,gVehicleCubeSavedFramebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER,gVehicleCubeSavedRenderbuffer);
        glActiveTexture(gVehicleCubeSavedActiveTexture);
        SharOpenXR::SetMultiviewTargetActive(true);
        return false;
    }
    glViewport(0,0,VEHICLE_CUBE_SIZE,VEHICLE_CUBE_SIZE);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(0.38f,0.58f,0.78f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    gVehicleCubeCapture=true;
    return true;
}

void VrEndVehicleCubeMapFace(pddiRenderContext*,int face)
{
    if(!gVehicleCubeCapture) return;
    gVehicleCubeCapture=false;
    if(face==5)
    {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_CUBE_MAP,gVehicleCubeTexture);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        if(!gVehicleCubeReady)
        {
            gVehicleCubeReady=true;
            SDL_Log("VR vehicle cubemap: first six-face capture complete");
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER,gVehicleCubeSavedFramebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER,gVehicleCubeSavedRenderbuffer);
    SharOpenXR::SetMultiviewTargetActive(true);
    glViewport(gVehicleCubeSavedViewport[0],gVehicleCubeSavedViewport[1],
               gVehicleCubeSavedViewport[2],gVehicleCubeSavedViewport[3]);
    if(gVehicleCubeSavedScissor) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);
    glColorMask(gVehicleCubeSavedColourMask[0],gVehicleCubeSavedColourMask[1],
                gVehicleCubeSavedColourMask[2],gVehicleCubeSavedColourMask[3]);
    glClearColor(gVehicleCubeSavedClearColour[0],gVehicleCubeSavedClearColour[1],
                 gVehicleCubeSavedClearColour[2],gVehicleCubeSavedClearColour[3]);
    glActiveTexture(gVehicleCubeSavedActiveTexture);
}
#endif

// vertex arrays rendering
GLenum primTypeTable[5] =
{
    GL_TRIANGLES, //PDDI_PRIM_TRIANGLES
    GL_TRIANGLE_STRIP, //PDDI_PRIM_TRISTRIP
    GL_LINES, //PDDI_PRIM_LINES
    GL_LINE_STRIP, // PDDI_PRIM_LINESTRIP
    GL_POINTS, //PDDI_PRIM_POINTS
};

static inline void FillGLColour(pddiColour c, float* f)
{
    f[0] = float(c.Red()) / 255;
    f[1] = float(c.Green()) / 255;
    f[2] = float(c.Blue()) / 255;
    f[3] = float(c.Alpha()) / 255;
}

// extensions
class pglExtContext : public pddiExtGLContext 
{
public:
    pglExtContext(pglDisplay* d) : display(d) {}

    void BeginContext()
    {
        display->BeginContext();
    }

    void EndContext()
    {
        display->EndContext();
    }

private:
    pglDisplay* display;
};

class pglExtGamma : public pddiExtGammaControl
{
public:
    pglExtGamma(pglDisplay* d) { display = d;}

    void SetGamma(float r, float g, float b)     {display->SetGamma(r,g,b);}
    void GetGamma(float *r, float *g, float *b)  {display->GetGamma(r,g,b);}

protected:
    pglDisplay* display;
};

pglContext::pglContext(pglDevice* dev, pglDisplay* disp) : pddiBaseContext((pddiDisplay*)disp,(pddiDevice*)dev)
{
    device = dev;
    display = disp;
    currentProgram = nullptr;
#if defined(RAD_ANDROID)
    legacyColorProgram=NULL;
    legacyTextureProgram=NULL;
    legacyAlphaTestProgram=NULL;
    enhancedColorProgram=NULL;
    enhancedTextureProgram=NULL;
    enhancedAlphaTestProgram=NULL;
    vehicleCsmColorProgram=NULL;
    vehicleCsmTextureProgram=NULL;
    vehicleCsmAlphaTestProgram=NULL;
    reflectionProgram=NULL;
    dynamicReflectionProgram=NULL;
    shadowDepthProgram=NULL;
    shadowOverlayProgram=NULL;
    particleTextureProgram=NULL;
    for(int i=0;i<SHADOW_CASCADE_COUNT;++i)
    {
        shadowFramebuffer[i]=shadowTexture[i]=shadowDepthBuffer[i]=0;
        shadowReady[i]=shadowRenderedThisFrame[i]=false;
        shadowCascadeCentreValid[i]=false;
    }
    shadowPass=false;
    shadowCurrentCascade=0;
    shadowStableCentreValid=false;
    shadowReceiverEnabled=false;
    shadowOverlayPass=false;
#endif

    device->AddRef();
    display->AddRef();
    disp->SetContext(this);

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    DefaultState();
    contextID = 0;

    extContext = new pglExtContext(display);
    extGamma = new pglExtGamma(display);

#ifdef RAD_CG
    CGprogram vertexShader = pglProgram::CompileShader(GL_VERTEX_SHADER,
        "typedef struct {\n"
        "    int enabled;\n"
        "    float4 position;\n"
        "    float4 colour;\n"
        "    float3 attenuation;\n"
        "} LightParams;\n"

        "float3 direction(float4 p1, float4 p2) { return normalize(p2.xyz * sign(p1.w) - p1.xyz * sign(p2.w)); }\n"
        "float power(float x, float y) { return y != 0.0 ? pow(x,y) : 1.0; }\n"
        "float product(float3 x, float3 y) { return max(dot(x,y), 0.0); }\n"

        "void main(float3 position      : ATTR0,\n"
        "          float3 normal        : ATTR1,\n"
        "          float2 texcoord      : ATTR2,\n"
        "          float4 color         : ATTR3,\n"
        "          out float4 oPosition : POSITION,\n"
        "          out float2 tc        : TEXCOORD0,\n"
        "          out float4 cpri      : COLOR0,\n"
        "          out float4 csec      : COLOR1,\n"
        "          uniform float4x4 projection,\n"
        "          uniform float4x4 modelview,\n"
        "          uniform float4x4 normalmatrix,\n"
        "          uniform float4 acs,\n"
        "          uniform float4 acm,\n"
        "          uniform float4 dcm,\n"
        "          uniform float4 scm,\n"
        "          uniform float4 ecm,\n"
        "          uniform float srm,\n"
        "          uniform LightParams lights[" PDDI_STRINGIZE( PDDI_MAX_LIGHTS ) "]) {\n"
        "    float4 V = mul(modelview, float4(position, 1.0));\n"
        "    float3 n = normalize(mul(float3x3(normalmatrix), normal));\n"

        "    float3 diff = ecm.rgb + acm.rgb * acs.rgb;\n"
        "    float3 spec = float3(0.0);\n"
        "    for (int i = 0; i < " PDDI_STRINGIZE(PDDI_MAX_LIGHTS) "; i++) {\n"
        "        if (lights[i].enabled == 0) continue;\n"

        "        float3 VP = direction(V, lights[i].position);\n"
        "        float f = product(n,VP) != 0.0 ? 1.0 : 0.0;\n"
        "        float3 h = normalize(VP + float3(0.0, 0.0, 1.0));\n"

        "        float3 k = lights[i].attenuation;\n"
        "        float d = distance(V.xyz, lights[i].position.xyz);\n"
        "        float att = lights[i].position.w != 0.0 ? 1.0 / (k[0] + k[1] * d + k[2] * d * d) : 1.0;\n"

        "        diff += att * product(n,VP) * dcm.rgb * lights[i].colour.rgb;\n"
        "        spec += att * f * power(product(n,h),srm) * scm.rgb * lights[i].colour.rgb;\n"
        "    }\n"

        "    tc = texcoord;\n"
        "    cpri = color * float4(diff, dcm.a);\n"
        "    csec = float4(spec, 0.0);\n"
        "    oPosition = mul(projection, V);\n"
        "}\n"
    );

    CGprogram fragmentShader = pglProgram::CompileShader( GL_FRAGMENT_SHADER,
        "void main(float2 tc        : TEXCOORD0,\n"
        "          float4 cpri      : COLOR0,\n"
        "          float4 csec      : COLOR1,\n"
        "          out float4 color : COLOR) {\n"
        "    color = cpri + csec;\n"
        "}\n"
    );

    CGprogram textureShader = pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        "void main(float2 tc : TEXCOORD0,\n"
        "          float4 cpri : COLOR0,\n"
        "          float4 csec : COLOR1,\n"
        "          out float4 color : COLOR,\n"
        "          uniform sampler2D tex) {\n"
        "    color = tex2D(tex, tc) * cpri + csec;\n"
        "}\n"
    );

    CGprogram alphaTestShader = pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        "void main(float2 tc : TEXCOORD0,\n"
        "          float4 cpri : COLOR0,\n"
        "          float4 csec : COLOR1,\n"
        "          out float4 color : COLOR,\n"
        "          uniform float alpharef,\n"
        "          uniform sampler2D tex) {\n"
        "    float4 c = tex2D(tex, tc) * cpri + csec;\n"
        "    if (c.a < alpharef) discard;\n"
        "    color = c;\n"
        "}\n"
    );
#else
    GLuint vertexShader = pglProgram::CompileShader(GL_VERTEX_SHADER,
        "precision highp float;\n"

        "attribute vec3 position;\n"
        "attribute vec3 normal;\n"
        "attribute vec2 texcoord;\n"
        "attribute vec4 color;\n"

        "uniform mat4 projection;\n"
        "uniform mat4 modelview;\n"
        "uniform mat4 normalmatrix;\n"
        "uniform mat4 reflectionViewToWorld;\n"
        "uniform mat4 shadowMatrix; uniform mat4 shadowMatrix1; uniform mat4 shadowMatrix2;\n"

        // Lights
        "uniform struct LightParams {\n"
        "    int enabled;\n"
        "    vec4 position;\n"
        "    vec4 colour;\n"
        "    vec3 attenuation;\n"
        "} lights[" PDDI_STRINGIZE(PDDI_MAX_LIGHTS) "];\n"

        // Scene
        "uniform vec4 acs;\n"

        // Material
        "uniform vec4 acm;\n"
        "uniform vec4 dcm;\n"
        "uniform vec4 scm;\n"
        "uniform vec4 ecm;\n"
        "uniform float srm;\n"

        #ifdef RAD_ANDROID
        // Indica si el material debe recibir iluminación.
        "uniform int lit;\n"
        "uniform int vehiclePaint;\n"
        "uniform int vehicleDentCount; uniform vec4 vehicleDents[4];\n"
        #endif

        "varying vec2 tc;\n"
        "varying vec4 cpri;\n"
        "varying vec4 csec;\n"
    "varying vec3 paintNormal;\n"
    "varying vec3 reflectionNormal;\n"
        "varying vec3 paintPosition;\n"
        "varying float paintEnabled;\n"
        "varying float pixelLit;\n"
        "varying vec4 shadowCoord0; varying vec4 shadowCoord1; varying vec4 shadowCoord2; varying float shadowViewDistance;\n"

        "vec3 direction(vec4 p1, vec4 p2) { return normalize(p2.xyz * sign(p1.w) - p1.xyz * sign(p2.w)); }\n"
        "float power(float x, float y) { return y != 0.0 ? pow(x,y) : 1.0; }\n"
        "float product(vec3 x, vec3 y) { return max(dot(x,y), 0.0); }\n"

                "void main() {\n"
                "    vec3 deformedPosition=position;\n"
                "    if(vehicleDentCount>0){\n"
                "      for(int di=0;di<4;++di){if(di>=vehicleDentCount)break; vec4 dent=vehicleDents[di]; vec3 delta=deformedPosition-dent.xyz; float radius=1.20+dent.w*0.65; float falloff=max(0.0,1.0-length(delta)/radius); falloff=falloff*falloff*(3.0-2.0*falloff); vec3 inward=normalize(-dent.xyz+vec3(0.0,0.20,0.0)); deformedPosition+=inward*(dent.w*falloff);}\n"
                "    }\n"
                "    vec4 V = modelview * vec4(deformedPosition, 1.0);\n"
                 

    #ifdef RAD_ANDROID
            "    vec3 diff;\n"
    #else
            "    vec3 n = normalize(mat3(normalmatrix) * normal);\n"
            "    vec3 diff = ecm.rgb + acm.rgb * acs.rgb;\n"
    #endif

            "    vec3 spec = vec3(0.0);\n"
            "    paintNormal = normalize(mat3(normalmatrix) * normal);\n"
            // Convert the ordinary view-space normal back to world space.
            // This preserves vehicle rotation while cancelling HMD rotation.
    "    reflectionNormal = normalize(mat3(reflectionViewToWorld) * paintNormal);\n"
            "    paintPosition = V.xyz;\n"
            "    paintEnabled = float(vehiclePaint);\n"
            // Enhanced Materials is a single on/off switch. Only genuinely
            // lit assets move from Gouraud to per-pixel lighting; SHAR's
            // unlit world geometry already has its light baked into albedo.
            // Vehicle bodywork covers a very large number of pixels up close.
            // Keep its authored multi-light result from this vertex pass and
            // reserve per-fragment lighting for world/character profiles. The
            // dedicated Phong paint highlight still runs per fragment.
            "    pixelLit = vehiclePaint>0&&vehiclePaint!=2&&lit!=0?1.0:0.0;\n"
            "    shadowCoord0=shadowMatrix*V; shadowCoord1=shadowMatrix1*V; shadowCoord2=shadowMatrix2*V; shadowViewDistance=length(V.xyz);\n"

    #ifdef RAD_ANDROID
            // Los materiales no iluminados deben conservar directamente
            // el color del vértice, sin luz ambiental, difusa o especular.
            "    if (lit == 0) {\n"
            "        diff = vec3(1.0);\n"
            "    } else {\n"
            "        vec3 n = normalize(mat3(normalmatrix) * normal);\n"
            "        diff = ecm.rgb + acm.rgb * acs.rgb;\n"
    #endif

            "        for (int i = 0; i < " PDDI_STRINGIZE(PDDI_MAX_LIGHTS) "; i++) {\n"
            "            if (lights[i].enabled == 0) continue;\n"

            "            vec3 VP = direction(V, lights[i].position);\n"
            "            float f = product(n,VP) != 0.0 ? 1.0 : 0.0;\n"
            "            vec3 h = normalize(VP + vec3(0.0, 0.0, 1.0));\n"

            "            vec3 k = lights[i].attenuation;\n"
            "            float d = distance(V.xyz, lights[i].position.xyz);\n"
            "            float att = lights[i].position.w != 0.0 ? 1.0 / (k[0] + k[1] * d + k[2] * d * d) : 1.0;\n"

            "            diff += att * product(n,VP) * dcm.rgb * lights[i].colour.rgb;\n"
            "            spec += att * f * power(product(n,h),srm) * scm.rgb * lights[i].colour.rgb;\n"
            "        }\n"

        #ifdef RAD_ANDROID
                "    }\n"
                // Keep the game's baked/base lighting. Phong is layered over
                // it in the fragment stage instead of replacing its exposure.
        #endif

        "    tc = texcoord;\n"
#ifdef RAD_ANDROID
        // Pass raw albedo only when fragment lighting will replace Gouraud.
        // This preserves the exact exposure of enhanced unlit scenery.
        "    if(vehiclePaint>0&&vehiclePaint!=2&&lit!=0){cpri=vec4(color.rgb,color.a*dcm.a);csec=vec4(0.0);}\n"
        "    else{cpri=color*vec4(diff,dcm.a);csec=vec4(spec,0.0);}\n"
#else
        "    cpri = color * vec4(diff, dcm.a);\n"
        "    csec = vec4(spec, 0.0);\n"
#endif
        "    gl_Position = projection * V;\n"
        "}\n"
    );

#define PGL_CSM_FRAGMENT \
        "uniform int shadowEnabled; uniform sampler2D shadowTex; uniform sampler2D shadowTex1; uniform sampler2D shadowTex2; uniform highp float shadowTexelSize; uniform highp float shadowTexelSize1; uniform highp float shadowTexelSize2; varying highp vec4 shadowCoord0; varying highp vec4 shadowCoord1; varying highp vec4 shadowCoord2; varying highp float shadowViewDistance;\n" \
        "highp float csmC0(highp vec2 u,highp float d){return d-0.00018>texture2D(shadowTex,u).r?1.0:0.0;} highp float csmC1(highp vec2 u,highp float d){return d-0.00018>texture2D(shadowTex1,u).r?1.0:0.0;} highp float csmC2(highp vec2 u,highp float d){return d-0.00018>texture2D(shadowTex2,u).r?1.0:0.0;}\n" \
        "highp float csmS0(highp vec3 p){highp vec2 q=p.xy/shadowTexelSize-0.5,f=fract(q),b=(floor(q)+0.5)*shadowTexelSize;return mix(mix(csmC0(b,p.z),csmC0(b+vec2(shadowTexelSize,0.0),p.z),f.x),mix(csmC0(b+vec2(0.0,shadowTexelSize),p.z),csmC0(b+vec2(shadowTexelSize),p.z),f.x),f.y);} highp float csmS1(highp vec3 p){highp vec2 q=p.xy/shadowTexelSize1-0.5,f=fract(q),b=(floor(q)+0.5)*shadowTexelSize1;return mix(mix(csmC1(b,p.z),csmC1(b+vec2(shadowTexelSize1,0.0),p.z),f.x),mix(csmC1(b+vec2(0.0,shadowTexelSize1),p.z),csmC1(b+vec2(shadowTexelSize1),p.z),f.x),f.y);} highp float csmS2(highp vec3 p){highp vec2 q=p.xy/shadowTexelSize2-0.5,f=fract(q),b=(floor(q)+0.5)*shadowTexelSize2;return mix(mix(csmC2(b,p.z),csmC2(b+vec2(shadowTexelSize2,0.0),p.z),f.x),mix(csmC2(b+vec2(0.0,shadowTexelSize2),p.z),csmC2(b+vec2(shadowTexelSize2),p.z),f.x),f.y);}\n" \
        "bool csmValid(highp vec3 p){return p.x>0.001&&p.x<0.999&&p.y>0.001&&p.y<0.999&&p.z>0.0&&p.z<1.0;} highp float csmShadow(){if(shadowEnabled==0)return 0.0;if(shadowViewDistance<24.0){highp vec3 p0=shadowCoord0.xyz/shadowCoord0.w*0.5+0.5,p1=shadowCoord1.xyz/shadowCoord1.w*0.5+0.5;highp float s0=csmValid(p0)?csmS0(p0):0.0,s1=csmValid(p1)?csmS1(p1):0.0;return shadowViewDistance<20.0?max(s0,s1):mix(max(s0,s1),s1,(shadowViewDistance-20.0)*0.25);}highp vec3 p1=shadowCoord1.xyz/shadowCoord1.w*0.5+0.5;highp float s1=csmValid(p1)?csmS1(p1):0.0;if(shadowViewDistance<50.0)return s1;highp vec3 p2=shadowCoord2.xyz/shadowCoord2.w*0.5+0.5;highp float s2=csmValid(p2)?csmS2(p2):0.0;if(shadowViewDistance<56.0)return mix(s1,s2,(shadowViewDistance-50.0)/6.0);return s2;}\n"

#define PGL_PIXEL_LIGHTING \
        "varying float pixelLit;\n" \
        "uniform int vehicleRearLightMode; uniform int vehicleRearLightCount; uniform vec3 vehicleRearLightPositions[8]; uniform vec3 vehicleRearLightDirections[8]; uniform vec3 vehicleRearLightColour;\n" \
        "vec3 lightDirection(vec3 p,vec4 l){return normalize(l.xyz-p*l.w);}\n" \
        "vec3 applyEnhancedLighting(vec3 base){if(paintEnabled<0.5||paintEnabled>5.5)return base;vec3 N=normalize(paintNormal),V=normalize(-paintPosition);if(pixelLit>0.5){vec3 diff=ecm.rgb+acm.rgb*acs.rgb,spec=vec3(0.0);for(int i=0;i<" PDDI_STRINGIZE(PDDI_MAX_LIGHTS) ";++i){if(lights[i].enabled==0)continue;vec3 D=lights[i].position.xyz-paintPosition*lights[i].position.w;float d=length(D);vec3 L=D/max(d,0.001);float ndl=max(dot(N,L),0.0);vec3 k=lights[i].attenuation;float att=lights[i].position.w!=0.0?1.0/(k.x+k.y*d+k.z*d*d):1.0;diff+=att*ndl*dcm.rgb*lights[i].colour.rgb;if(ndl>0.0){vec3 H=normalize(L+V);spec+=att*pow(max(dot(N,H),0.0),srm)*scm.rgb*lights[i].colour.rgb;}}base=base*diff+spec;}float car=1.0-step(0.25,abs(paintEnabled-2.0)),chr=1.0-step(0.25,abs(paintEnabled-3.0)),rough=1.0-step(0.25,abs(paintEnabled-4.0)),metal=1.0-step(0.25,abs(paintEnabled-5.0));vec3 L=normalize(enhancedSunDirection),H=normalize(L+V);float ndl=max(dot(N,L),0.0),sp=0.055+0.32*car+0.055*chr-0.035*rough+0.41*metal,sh=12.0+26.0*car+10.0*chr-3.0*rough+36.0*metal,spec=step(0.0001,ndl)*pow(max(dot(N,H),0.0),sh)*sp,ft=1.0-clamp(dot(N,V),0.0,1.0),f=ft*ft*ft*ft*ft,refl=0.10*car+0.24*metal;return clamp(base*(0.92+0.16*ndl)+vec3(spec)+vec3(0.16,0.20,0.28)*f*refl,0.0,1.0);}\n" \
        "vec3 applyVehicleRearLights(vec3 base){if(vehicleRearLightMode==0||paintEnabled<0.5)return base;vec3 N=normalize(paintNormal),add=vec3(0.0);float worldSurface=max(1.0-step(0.25,abs(paintEnabled-1.0)),1.0-step(0.25,abs(paintEnabled-6.0)));for(int i=0;i<4;++i){if(i>=vehicleRearLightCount)break;vec3 fromLamp=paintPosition-vehicleRearLightPositions[i];float d2=dot(fromLamp,fromLamp),radius=vehicleRearLightMode==1?5.0:7.0;if(d2>=radius*radius)continue;float d=sqrt(d2);vec3 ray=fromLamp/max(d,0.001);float coneDot=dot(ray,vehicleRearLightDirections[i]);if(coneDot<=0.48)continue;float cone=smoothstep(0.48,0.78,coneDot),fall=1.0-d/radius,nearFade=smoothstep(0.30,0.85,d),facing=0.12+0.88*max(dot(N,-ray),0.0),road=1.0+0.70*max(N.y,0.0),receiver=mix(1.0,1.85,worldSurface);add+=vehicleRearLightColour*fall*fall*nearFade*cone*facing*road*receiver*(vehicleRearLightMode==1?0.736:0.624);}return clamp(base+add,0.0,1.0);}\n"

    GLuint fragmentShader = pglProgram::CompileShader( GL_FRAGMENT_SHADER,
        "precision highp float;\n"
        "varying vec2 tc;\n"
        "varying vec4 cpri;\n"
        "varying vec4 csec;\n"
        "varying vec3 paintNormal; varying highp vec3 paintPosition; varying float paintEnabled;\n"
        "uniform struct LightParams { int enabled; vec4 position; vec4 colour; vec3 attenuation; } lights[" PDDI_STRINGIZE(PDDI_MAX_LIGHTS) "];\n"
        "uniform vec4 acs; uniform vec4 acm; uniform vec4 dcm; uniform vec4 ecm; uniform vec4 scm; uniform float srm; uniform vec3 enhancedSunDirection;\n"
        PGL_CSM_FRAGMENT
        PGL_PIXEL_LIGHTING
        "void main() {\n"
        "    vec4 c = cpri + csec;\n"
        "    c.rgb = applyEnhancedLighting(c.rgb);\n"
        "    float l = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
        "    vec3 graded=clamp(mix(vec3(l),c.rgb,1.25),0.0,1.0); c.rgb=graded*graded;\n"
        "    c.rgb *= 1.0-0.58*csmShadow();\n"
        "    c.rgb = applyVehicleRearLights(c.rgb);\n"
        "    gl_FragColor = c;\n"
        "}\n"
    );

    GLuint textureShader = pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        "precision highp float;\n"
        "varying vec2 tc;\n"
        "varying vec4 cpri;\n"
        "varying vec4 csec;\n"
        "varying vec3 paintNormal; varying highp vec3 paintPosition; varying float paintEnabled;\n"
        "uniform struct LightParams { int enabled; vec4 position; vec4 colour; vec3 attenuation; } lights[" PDDI_STRINGIZE(PDDI_MAX_LIGHTS) "];\n"
        "uniform vec4 acs; uniform vec4 acm; uniform vec4 dcm; uniform vec4 ecm; uniform vec4 scm; uniform float srm; uniform vec3 enhancedSunDirection;\n"
        PGL_CSM_FRAGMENT
        PGL_PIXEL_LIGHTING

        "uniform sampler2D tex;\n"

        "void main() {\n"
        "    vec4 c = texture2D(tex, tc) * cpri + csec;\n"
        "    c.rgb = applyEnhancedLighting(c.rgb);\n"
        "    float l = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
        "    vec3 graded=clamp(mix(vec3(l),c.rgb,1.25),0.0,1.0); c.rgb=graded*graded;\n"
        "    c.rgb *= 1.0-0.58*csmShadow();\n"
        "    c.rgb = applyVehicleRearLights(c.rgb);\n"
        "    gl_FragColor = c;\n"
        "}\n"
    );

    GLuint alphaTestShader = pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        "precision highp float;\n"
        "varying vec2 tc;\n"
        "varying vec4 cpri;\n"
        "varying vec4 csec;\n"
        "varying vec3 paintNormal; varying highp vec3 paintPosition; varying float paintEnabled;\n"
        "uniform struct LightParams { int enabled; vec4 position; vec4 colour; vec3 attenuation; } lights[" PDDI_STRINGIZE(PDDI_MAX_LIGHTS) "];\n"
        "uniform vec4 acs; uniform vec4 acm; uniform vec4 dcm; uniform vec4 ecm; uniform vec4 scm; uniform float srm; uniform vec3 enhancedSunDirection;\n"
        PGL_CSM_FRAGMENT
        PGL_PIXEL_LIGHTING

        "uniform float alpharef;\n"
        "uniform sampler2D tex;\n"

        "void main() {\n"
        "    vec4 c = texture2D(tex, tc) * cpri + csec;\n"
        "    if (c.a < alpharef) discard;\n"
        "    c.rgb = applyEnhancedLighting(c.rgb);\n"
        // Do not illuminate alpha-tested lamp billboards/decals: adding RGB to
        // their low-alpha texels exposes the underlying rectangular polygons.
        "    float l = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
        "    vec3 graded=clamp(mix(vec3(l),c.rgb,1.25),0.0,1.0); c.rgb=graded*graded;\n"
        "    c.rgb *= 1.0-0.58*csmShadow();\n"
        "    gl_FragColor = c;\n"
        "}\n"
    );

    // Vehicle + CSM specialization. Vehicle lighting is already evaluated in
    // the vertex stage, so this variant omits the generic per-pixel light loop
    // and all local/rear-light arrays. Profile 7 is used by the screen-filling
    // player vehicle and avoids a per-fragment view-vector normalization.
#define PGL_VEHICLE_CSM_COMMON \
        "precision highp float;varying vec2 tc;varying vec4 cpri,csec;varying vec3 paintNormal;varying highp vec3 paintPosition;varying float paintEnabled;uniform vec3 enhancedSunDirection;" \
        PGL_CSM_FRAGMENT \
        "float p38(float x){float x2=x*x,x4=x2*x2,x8=x4*x4,x16=x8*x8,x32=x16*x16;return x32*x4*x2;}" \
        "float p16(float x){float x2=x*x,x4=x2*x2,x8=x4*x4;return x8*x8;}" \
        "vec3 carPaint(vec3 base){vec3 N=normalize(paintNormal),L=enhancedSunDirection;float ndl=max(dot(N,L),0.0);if(paintEnabled>6.5){vec3 Hn=L+vec3(0.0,0.0,1.0);float spec=p16(max(dot(N,Hn)*0.62,0.0))*0.30,ft=1.0-clamp(N.z,0.0,1.0),f=ft*ft*ft*ft*ft;return clamp(base*(0.92+0.16*ndl)+vec3(spec)+vec3(0.016,0.020,0.028)*f,0.0,1.0);}vec3 V=normalize(-paintPosition),H=normalize(L+V);float spec=p38(max(dot(N,H),0.0))*0.375,ft=1.0-clamp(dot(N,V),0.0,1.0),f=ft*ft*ft*ft*ft;return clamp(base*(0.92+0.16*ndl)+vec3(spec)+vec3(0.016,0.020,0.028)*f,0.0,1.0);}" \
        "vec3 grade(vec3 c){float l=dot(c,vec3(0.2126,0.7152,0.0722));c=clamp(mix(vec3(l),c,1.25),0.0,1.0);return c*c;}"
    GLuint vehicleCsmColorFS=pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        PGL_VEHICLE_CSM_COMMON
        "void main(){vec4 c=cpri+csec;c.rgb=grade(carPaint(c.rgb))*(1.0-0.58*csmShadow());gl_FragColor=c;}");
    GLuint vehicleCsmTextureFS=pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        PGL_VEHICLE_CSM_COMMON
        "uniform sampler2D tex;void main(){vec4 c=texture2D(tex,tc)*cpri+csec;c.rgb=grade(carPaint(c.rgb))*(1.0-0.58*csmShadow());gl_FragColor=c;}");
    GLuint vehicleCsmAlphaFS=pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        PGL_VEHICLE_CSM_COMMON
        "uniform sampler2D tex;uniform float alpharef;void main(){vec4 c=texture2D(tex,tc)*cpri+csec;if(c.a<alpharef)discard;c.rgb=grade(carPaint(c.rgb))*(1.0-0.58*csmShadow());gl_FragColor=c;}");
#undef PGL_VEHICLE_CSM_COMMON

    // The feature-rich programs above preserve CSM, enhanced materials and
    // local vehicle lights.  Uniform branches are not a free substitute for
    // small shaders on tiled mobile GPUs: their varyings and register pressure
    // remain even when every feature is disabled.  These variants reproduce
    // the exact legacy colour path and are selected only when a draw needs none
    // of those optional features.
    GLuint legacyColorFS=pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        // Source and framebuffer colours are 8-bit, so mediump retains the
        // output while reducing fragment ALU cost for close, screen-filling
        // cars and state props in both eye buffers.
        "precision mediump float;\n"
        "varying vec4 cpri; varying vec4 csec;\n"
        "void main(){vec4 c=cpri+csec;float l=dot(c.rgb,vec3(0.2126,0.7152,0.0722));"
        "c.rgb=clamp(mix(vec3(l),c.rgb,1.25),0.0,1.0);c.rgb*=c.rgb;gl_FragColor=c;}\n");
    GLuint legacyTextureFS=pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        "precision mediump float;\n"
        "varying vec2 tc; varying vec4 cpri; varying vec4 csec; uniform sampler2D tex;\n"
        "void main(){vec4 c=texture2D(tex,tc)*cpri+csec;float l=dot(c.rgb,vec3(0.2126,0.7152,0.0722));"
        "c.rgb=clamp(mix(vec3(l),c.rgb,1.25),0.0,1.0);c.rgb*=c.rgb;gl_FragColor=c;}\n");
    GLuint legacyAlphaFS=pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        "precision mediump float;\n"
        "varying vec2 tc; varying vec4 cpri; varying vec4 csec; uniform sampler2D tex; uniform float alpharef;\n"
        "void main(){vec4 c=texture2D(tex,tc)*cpri+csec;if(c.a<alpharef)discard;"
        "float l=dot(c.rgb,vec3(0.2126,0.7152,0.0722));c.rgb=clamp(mix(vec3(l),c.rgb,1.25),0.0,1.0);"
        "c.rgb*=c.rgb;gl_FragColor=c;}\n");

    // Compact enhanced-only variants.  They deliberately contain neither CSM
    // samplers nor vehicle rear-light arrays, allowing the linker to remove
    // four shadow varyings and the local-light state for draws which need only
    // improved materials.  Lighting and paint share N/V instead of normalizing
    // them independently in two functions.
#define PGL_ENHANCED_ONLY_COMMON \
        "precision highp float; varying vec2 tc; varying vec4 cpri,csec; varying vec3 paintNormal; varying highp vec3 paintPosition; varying float paintEnabled,pixelLit;" \
        "uniform struct LightParams{int enabled;vec4 position;vec4 colour;vec3 attenuation;}lights[" PDDI_STRINGIZE(PDDI_MAX_LIGHTS) "];" \
        "uniform vec4 acs,acm,dcm,ecm,scm;uniform float srm;uniform vec3 enhancedSunDirection;" \
        "vec3 enhancedLight(vec3 base){if(paintEnabled<0.5)return base;vec3 N=normalize(paintNormal),V=normalize(-paintPosition);" \
        "if(pixelLit>0.5){vec3 diff=ecm.rgb+acm.rgb*acs.rgb,spec=vec3(0.0);for(int i=0;i<" PDDI_STRINGIZE(PDDI_MAX_LIGHTS) ";++i){if(lights[i].enabled==0)continue;vec3 D=lights[i].position.xyz-paintPosition*lights[i].position.w;float d=length(D);vec3 L=D/max(d,0.001);float ndl=max(dot(N,L),0.0);vec3 k=lights[i].attenuation;float att=lights[i].position.w!=0.0?1.0/(k.x+k.y*d+k.z*d*d):1.0;diff+=att*ndl*dcm.rgb*lights[i].colour.rgb;if(ndl>0.0){vec3 H=normalize(L+V);spec+=att*pow(max(dot(N,H),0.0),srm)*scm.rgb*lights[i].colour.rgb;}}base=base*diff+spec;}" \
        "float car=1.0-step(0.25,abs(paintEnabled-2.0)),chr=1.0-step(0.25,abs(paintEnabled-3.0)),rough=1.0-step(0.25,abs(paintEnabled-4.0)),metal=1.0-step(0.25,abs(paintEnabled-5.0));vec3 L=normalize(enhancedSunDirection),H=normalize(L+V);float ndl=max(dot(N,L),0.0),sp=0.055+0.32*car+0.055*chr-0.035*rough+0.41*metal,sh=12.0+26.0*car+10.0*chr-3.0*rough+36.0*metal,spec=step(0.0001,ndl)*pow(max(dot(N,H),0.0),sh)*sp,ft=1.0-clamp(dot(N,V),0.0,1.0),f=ft*ft*ft*ft*ft,refl=0.10*car+0.24*metal;return clamp(base*(0.92+0.16*ndl)+vec3(spec)+vec3(0.16,0.20,0.28)*f*refl,0.0,1.0);}" \
        "vec3 grade(vec3 c){float l=dot(c,vec3(0.2126,0.7152,0.0722));c=clamp(mix(vec3(l),c,1.25),0.0,1.0);return c*c;}"
    GLuint enhancedColorFS=pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        PGL_ENHANCED_ONLY_COMMON
        "void main(){vec4 c=cpri+csec;c.rgb=grade(enhancedLight(c.rgb));gl_FragColor=c;}");
    GLuint enhancedTextureFS=pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        PGL_ENHANCED_ONLY_COMMON
        "uniform sampler2D tex;void main(){vec4 c=texture2D(tex,tc)*cpri+csec;c.rgb=grade(enhancedLight(c.rgb));gl_FragColor=c;}");
    GLuint enhancedAlphaFS=pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        PGL_ENHANCED_ONLY_COMMON
        "uniform sampler2D tex;uniform float alpharef;void main(){vec4 c=texture2D(tex,tc)*cpri+csec;if(c.a<alpharef)discard;c.rgb=grade(enhancedLight(c.rgb));gl_FragColor=c;}");
#undef PGL_ENHANCED_ONLY_COMMON
#undef PGL_PIXEL_LIGHTING
#undef PGL_CSM_FRAGMENT
#endif

    colorProgram = pglProgram::CreateProgram(vertexShader, fragmentShader);

    textureProgram = pglProgram::CreateProgram(vertexShader, textureShader);

    alphaTestProgram = pglProgram::CreateProgram(vertexShader, alphaTestShader);
#if defined(RAD_ANDROID)
    legacyColorProgram=pglProgram::CreateProgram(vertexShader,legacyColorFS);
    legacyTextureProgram=pglProgram::CreateProgram(vertexShader,legacyTextureFS);
    legacyAlphaTestProgram=pglProgram::CreateProgram(vertexShader,legacyAlphaFS);
    enhancedColorProgram=pglProgram::CreateProgram(vertexShader,enhancedColorFS);
    enhancedTextureProgram=pglProgram::CreateProgram(vertexShader,enhancedTextureFS);
    enhancedAlphaTestProgram=pglProgram::CreateProgram(vertexShader,enhancedAlphaFS);
    vehicleCsmColorProgram=pglProgram::CreateProgram(vertexShader,vehicleCsmColorFS);
    vehicleCsmTextureProgram=pglProgram::CreateProgram(vertexShader,vehicleCsmTextureFS);
    vehicleCsmAlphaTestProgram=pglProgram::CreateProgram(vertexShader,vehicleCsmAlphaFS);
    // SHAR's authored vehicle reflection is a cheap 2D sphere map. Match the
    // D3D environment shader instead of introducing a costly dynamic cubemap.
    GLuint reflectionFS=pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        "precision highp float;varying vec2 tc;varying vec4 cpri,csec;"
        "varying vec3 paintNormal;varying vec3 reflectionNormal;varying highp vec3 paintPosition;"
        "uniform sampler2D tex;uniform sampler2D reflectionTex;uniform vec4 environmentBlend;"
        "uniform mat4 reflectionViewToWorld;uniform float alpharef;"
        "void main(){vec4 texel=texture2D(tex,tc);vec4 base=texel*cpri+csec;"
        "if(base.a<alpharef)discard;"
        // D3D's TCI_CAMERASPACEREFLECTIONVECTOR uses the direction from the
        // eye to each vertex, not one camera-forward vector for the complete
        // car. Transforming both that vector and the normal back to world
        // space cancels HMD rotation while restoring variation across broad,
        // almost-flat bonnet and trunk panels.
        "vec3 N=normalize(reflectionNormal);"
        "vec3 I=normalize(mat3(reflectionViewToWorld)*paintPosition);"
        "vec3 R=normalize(reflect(I,N));"
        // Match the original D3D TCI_CAMERASPACEREFLECTIONVECTOR path. EnvMap
        // is already authored as a circular reflection texture; applying a
        // second sphere-map projection compresses and distorts its clouds.
        "vec2 uv=clamp(vec2(0.5+0.5*R.x,0.5-0.5*R.y),0.001,0.999);"
        // Grade paint independently. Squaring the combined colour suppressed
        // most mid-grey cloud detail, while grading only the body keeps its
        // highlights controlled and lets the authored reflection stay clear.
        "float l=dot(base.rgb,vec3(0.2126,0.7152,0.0722));"
        "vec3 paint=clamp(mix(vec3(l),base.rgb,1.25),0.0,1.0);paint*=paint;"
        "vec3 env=texture2D(reflectionTex,uv).rgb;"
        "env=pow(max(env,vec3(0.0)),vec3(0.78));"
        // Traffic alpha is a swatch mask: near-opaque texels are restored to
        // white by the following gloss pass (bumpers/trim), while lower-alpha
        // texels are recolourable paint. Reflect only the inverse mask here.
        "if(environmentBlend.a<0.5){float bodyMask=1.0-step(250.0/255.0,texel.a);"
        // Reproduce the normal Android material grade before adding EnvMap.
        // Without the square/contrast curve the traffic swatch colour becomes
        // pale and appears overexposed merely by selecting reflectionProgram.
        "float tl=dot(base.rgb,vec3(0.2126,0.7152,0.0722));"
        "vec3 traffic=clamp(mix(vec3(tl),base.rgb,1.25),0.0,1.0);traffic*=traffic;"
        "base.rgb=clamp(traffic+env*environmentBlend.r*bodyMask,0.0,1.0);}"
        "else{env*=environmentBlend.rgb*0.72;base.rgb=clamp(paint+env,0.0,1.0);}"
        "gl_FragColor=base;}");
    reflectionProgram=pglProgram::CreateProgram(vertexShader,reflectionFS);
    glDeleteShader(reflectionFS);
    GLuint dynamicReflectionFS=pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        "precision highp float;varying vec2 tc;varying vec4 cpri,csec;"
        "varying vec3 paintNormal;varying vec3 reflectionNormal;varying highp vec3 paintPosition;"
        "uniform sampler2D tex;uniform samplerCube reflectionTex;uniform vec4 environmentBlend;"
        "uniform mat4 reflectionViewToWorld;uniform float alpharef;"
        "void main(){vec4 texel=texture2D(tex,tc);vec4 base=texel*cpri+csec;"
        "if(base.a<alpharef)discard;vec3 N=normalize(reflectionNormal);"
        "vec3 I=normalize(mat3(reflectionViewToWorld)*paintPosition);"
        "vec3 R=normalize(reflect(I,N));vec3 env=textureCube(reflectionTex,R).rgb;"
        "float l=dot(base.rgb,vec3(0.2126,0.7152,0.0722));"
        "vec3 paint=clamp(mix(vec3(l),base.rgb,1.25),0.0,1.0);paint*=paint;"
        "env=pow(max(env,vec3(0.0)),vec3(0.82));"
        "if(environmentBlend.a<0.5){float bodyMask=1.0-step(250.0/255.0,texel.a);"
        "float tl=dot(base.rgb,vec3(0.2126,0.7152,0.0722));"
        "vec3 traffic=clamp(mix(vec3(tl),base.rgb,1.25),0.0,1.0);traffic*=traffic;"
        "base.rgb=clamp(traffic+env*environmentBlend.r*bodyMask,0.0,1.0);}"
        "else{env*=environmentBlend.rgb*0.72;base.rgb=clamp(paint+env,0.0,1.0);}"
        "gl_FragColor=base;}");
    dynamicReflectionProgram=pglProgram::CreateProgram(vertexShader,dynamicReflectionFS);
    glDeleteShader(dynamicReflectionFS);
    // Alpha-blended effects never receive CSM or enhanced-material lighting.
    // Keeping their fragment stage free of those large dynamic branches is
    // essential for smoke, whose overlapping sprites are fill-rate bound.
    GLuint particleFS=pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        "precision mediump float; varying vec2 tc; varying vec4 cpri; varying vec4 csec; uniform sampler2D tex; void main(){vec4 c=texture2D(tex,tc)*cpri+csec;if(c.a<=0.003921569)discard;gl_FragColor=c;}");
    particleTextureProgram=pglProgram::CreateProgram(vertexShader,particleFS);
    glDeleteShader(particleFS);
    glDeleteShader(legacyColorFS);
    glDeleteShader(legacyTextureFS);
    glDeleteShader(legacyAlphaFS);
    glDeleteShader(enhancedColorFS);
    glDeleteShader(enhancedTextureFS);
    glDeleteShader(enhancedAlphaFS);
    glDeleteShader(vehicleCsmColorFS);
    glDeleteShader(vehicleCsmTextureFS);
    glDeleteShader(vehicleCsmAlphaFS);

    GLuint shadowVS=pglProgram::CompileShader(GL_VERTEX_SHADER,
        "precision highp float; attribute vec3 position; attribute vec2 texcoord; uniform mat4 projection; uniform mat4 modelview; uniform int vehicleDentCount; uniform vec4 vehicleDents[4]; varying vec2 tc; vec3 deform(vec3 p){for(int i=0;i<4;++i){if(i>=vehicleDentCount)break;vec4 d=vehicleDents[i];float r=1.20+d.w*0.65,f=max(0.0,1.0-length(p-d.xyz)/r);f=f*f*(3.0-2.0*f);p+=normalize(-d.xyz+vec3(0.0,0.20,0.0))*(d.w*f);}return p;} void main(){tc=texcoord;gl_Position=projection*modelview*vec4(deform(position),1.0);}");
    GLuint shadowFS=pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        "precision highp float; uniform sampler2D tex; uniform float alpharef; varying vec2 tc; vec4 packDepth(float d){vec4 c=fract(d*vec4(1.0,255.0,65025.0,16581375.0));c-=c.yzww*vec4(1.0/255.0,1.0/255.0,1.0/255.0,0.0);return c;} void main(){if(alpharef>0.0&&texture2D(tex,tc).a<alpharef)discard;gl_FragColor=packDepth(gl_FragCoord.z);}");
    shadowDepthProgram=pglProgram::CreateProgram(shadowVS,shadowFS);
    glDeleteShader(shadowVS); glDeleteShader(shadowFS);

    GLuint overlayVS=pglProgram::CompileShader(GL_VERTEX_SHADER,
        "precision highp float; attribute vec3 position; attribute vec2 texcoord; uniform mat4 projection; uniform mat4 modelview; uniform mat4 shadowMatrix; uniform mat4 shadowMatrix1; uniform mat4 shadowMatrix2; uniform int vehicleDentCount; uniform vec4 vehicleDents[4]; varying vec4 sc0; varying vec4 sc1; varying vec4 sc2; varying float viewDistance; varying vec2 tc; vec3 deform(vec3 p){for(int i=0;i<4;++i){if(i>=vehicleDentCount)break;vec4 d=vehicleDents[i];float r=1.20+d.w*0.65,f=max(0.0,1.0-length(p-d.xyz)/r);f=f*f*(3.0-2.0*f);p+=normalize(-d.xyz+vec3(0.0,0.20,0.0))*(d.w*f);}return p;} void main(){vec4 v=modelview*vec4(deform(position),1.0);tc=texcoord;sc0=shadowMatrix*v;sc1=shadowMatrix1*v;sc2=shadowMatrix2*v;viewDistance=length(v.xyz);gl_Position=projection*v;}");
    GLuint overlayFS=pglProgram::CompileShader(GL_FRAGMENT_SHADER,
        "precision highp float; uniform sampler2D tex; uniform float alpharef; uniform sampler2D shadowTex; uniform sampler2D shadowTex1; uniform sampler2D shadowTex2; uniform float shadowTexelSize; uniform float shadowTexelSize1; uniform float shadowTexelSize2; varying vec4 sc0; varying vec4 sc1; varying vec4 sc2; varying float viewDistance; varying vec2 tc; float c0(vec2 u,float d){return d-0.00018>texture2D(shadowTex,u).r?1.0:0.0;} float c1(vec2 u,float d){return d-0.00018>texture2D(shadowTex1,u).r?1.0:0.0;} float c2(vec2 u,float d){return d-0.00018>texture2D(shadowTex2,u).r?1.0:0.0;} float s0(vec3 p){vec2 q=p.xy/shadowTexelSize-0.5,f=fract(q),b=(floor(q)+0.5)*shadowTexelSize;return mix(mix(c0(b,p.z),c0(b+vec2(shadowTexelSize,0.0),p.z),f.x),mix(c0(b+vec2(0.0,shadowTexelSize),p.z),c0(b+vec2(shadowTexelSize),p.z),f.x),f.y);} float s1(vec3 p){vec2 q=p.xy/shadowTexelSize1-0.5,f=fract(q),b=(floor(q)+0.5)*shadowTexelSize1;return mix(mix(c1(b,p.z),c1(b+vec2(shadowTexelSize1,0.0),p.z),f.x),mix(c1(b+vec2(0.0,shadowTexelSize1),p.z),c1(b+vec2(shadowTexelSize1),p.z),f.x),f.y);} float s2(vec3 p){vec2 q=p.xy/shadowTexelSize2-0.5,f=fract(q),b=(floor(q)+0.5)*shadowTexelSize2;return mix(mix(c2(b,p.z),c2(b+vec2(shadowTexelSize2,0.0),p.z),f.x),mix(c2(b+vec2(0.0,shadowTexelSize2),p.z),c2(b+vec2(shadowTexelSize2),p.z),f.x),f.y);} bool valid(vec3 p){return p.x>0.001&&p.x<0.999&&p.y>0.001&&p.y<0.999&&p.z>0.0&&p.z<1.0;} void main(){if(alpharef>0.0&&texture2D(tex,tc).a<alpharef)discard;float s=0.0;if(viewDistance<24.0){vec3 p0=sc0.xyz/sc0.w*0.5+0.5,p1=sc1.xyz/sc1.w*0.5+0.5;float a=valid(p0)?s0(p0):0.0,b=valid(p1)?s1(p1):0.0;s=viewDistance<20.0?max(a,b):mix(max(a,b),b,(viewDistance-20.0)*0.25);}else{vec3 p1=sc1.xyz/sc1.w*0.5+0.5;float b=valid(p1)?s1(p1):0.0;if(viewDistance<50.0)s=b;else{vec3 p2=sc2.xyz/sc2.w*0.5+0.5;float c=valid(p2)?s2(p2):0.0;s=viewDistance<56.0?mix(b,c,(viewDistance-50.0)/6.0):c;}}if(s<0.02)discard;gl_FragColor=vec4(0.0,0.0,0.0,0.58*s);}");
    shadowOverlayProgram=pglProgram::CreateProgram(overlayVS,overlayFS);
    glDeleteShader(overlayVS); glDeleteShader(overlayFS);
#endif

    // Don't leak shaders
#ifdef RAD_CG
    cgDestroyProgram(vertexShader);
    cgDestroyProgram(fragmentShader);
    cgDestroyProgram(textureShader);
    cgDestroyProgram(alphaTestShader);
#else
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    glDeleteShader(textureShader);
    glDeleteShader(alphaTestShader);
#endif

    defaultShader = new pglMat(this);
    defaultShader->AddRef();
    SetShaderProgram(colorProgram);
}

pglContext::~pglContext()
{
#if defined(RAD_ANDROID)
    glDeleteFramebuffers(SHADOW_CASCADE_COUNT,shadowFramebuffer);
    glDeleteRenderbuffers(SHADOW_CASCADE_COUNT,shadowDepthBuffer);
    glDeleteTextures(SHADOW_CASCADE_COUNT,shadowTexture);
    if(shadowDepthProgram) shadowDepthProgram->Release();
    if(shadowOverlayProgram) shadowOverlayProgram->Release();
    if(particleTextureProgram) particleTextureProgram->Release();
    if(legacyColorProgram) legacyColorProgram->Release();
    if(legacyTextureProgram) legacyTextureProgram->Release();
    if(legacyAlphaTestProgram) legacyAlphaTestProgram->Release();
    if(enhancedColorProgram) enhancedColorProgram->Release();
    if(enhancedTextureProgram) enhancedTextureProgram->Release();
    if(enhancedAlphaTestProgram) enhancedAlphaTestProgram->Release();
    if(vehicleCsmColorProgram) vehicleCsmColorProgram->Release();
    if(vehicleCsmTextureProgram) vehicleCsmTextureProgram->Release();
    if(vehicleCsmAlphaTestProgram) vehicleCsmAlphaTestProgram->Release();
    if(reflectionProgram) reflectionProgram->Release();
    if(dynamicReflectionProgram) dynamicReflectionProgram->Release();
#endif
    defaultShader->Release();
    currentProgram->Release();
    colorProgram->Release();
    textureProgram->Release();
    alphaTestProgram->Release();

    delete extContext;
    delete extGamma;

    display->SetContext(NULL);
    display->Release();
    device->Release();
}

// frame synchronisation
void pglContext::BeginFrame()
{
    pddiBaseContext::BeginFrame();
#if defined(RAD_ANDROID)
    for(int i=0;i<SHADOW_CASCADE_COUNT;++i)
        shadowRenderedThisFrame[i]=false;
#endif

    SDL_GL_SetSwapInterval(display->GetForceVSync() ? 1 : 0);

    if(display->HasReset())
    {
        contextID++;

        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
#ifndef RAD_VITAGL
        glEnable(GL_DITHER);
#endif

        SyncState(0xffffffff);
    }

    projection.Identity();
}

void pglContext::EndFrame()
{
    pddiBaseContext::EndFrame();
}

// buffer clearing
void pglContext::Clear(unsigned bufferMask)
{
    pddiBaseContext::Clear(bufferMask);

    int myClearMask = 0;
    myClearMask |= (bufferMask & PDDI_BUFFER_COLOUR) ? GL_COLOR_BUFFER_BIT : 0;
    myClearMask |= (bufferMask & PDDI_BUFFER_DEPTH) ? GL_DEPTH_BUFFER_BIT : 0;
    //myClearMask |= (bufferMask & PDDI_BUFFER_STENCIL) ? GL_STENCIL_BUFFER_BIT : 0;

    glClearDepthf(state.viewState->clearDepth);
    glClearColor(float(state.viewState->clearColour.Red())/255.0f, 
                     float(state.viewState->clearColour.Green())/255.0f, 
                     float(state.viewState->clearColour.Blue())/255.0f,
                     float(state.viewState->clearColour.Alpha())/255.0f);
    glClearStencil(state.viewState->clearStencil);
    glClear(myClearMask);
}

void pglContext::SetupHardwareProjection(void)
{
#if defined(RAD_ANDROID)
    int xrWidth = 0, xrHeight = 0;
    // HudMap0 contains both regular Scrooby sprites and a Pure3D minimap with
    // its own perspective camera. Only remap the 2D canvas; replacing the
    // minimap camera projection puts all of its geometry outside clip space.
    if (SharOpenXR::IsRadarRendering && SharOpenXR::IsRadarRendering() &&
        (!SharOpenXR::IsEmbeddedHudRendering ||
         !SharOpenXR::IsEmbeddedHudRendering()) &&
        SharOpenXR::GetActiveRadarProjection &&
        SharOpenXR::GetActiveRadarProjection(&projection,&xrWidth,&xrHeight))
    {
        glViewport(0,0,xrWidth,xrHeight);
        if(currentProgram) currentProgram->SetProjectionMatrix(&projection);
        return;
    }
    if (SharOpenXR::IsMovieRendering && SharOpenXR::IsMovieRendering() &&
        SharOpenXR::GetActiveMovieProjection &&
        SharOpenXR::GetActiveMovieProjection(&projection,&xrWidth,&xrHeight))
    {
        // Projection includes the fixed movie-plane pose, current eye view
        // and the real asymmetric OpenXR FOV.
        glViewport(0, 0, xrWidth, xrHeight);
        if(currentProgram) currentProgram->SetProjectionMatrix(&projection);
        return;
    }
    if (SharOpenXR::IsFrontendPlaneRendering &&
        SharOpenXR::IsFrontendPlaneRendering() &&
        SharOpenXR::GetActiveFrontendProjection &&
        SharOpenXR::GetActiveFrontendProjection(&projection,&xrWidth,&xrHeight))
    {
        glViewport(0,0,xrWidth,xrHeight);
        if(currentProgram) currentProgram->SetProjectionMatrix(&projection);
        return;
    }
    if (state.viewState->projectionMode == PDDI_PROJECTION_PERSPECTIVE &&
        SharOpenXR::GetActiveProjection &&
        SharOpenXR::GetActiveProjection(&projection, &xrWidth, &xrHeight))
    {
        glViewport(0, 0, xrWidth, xrHeight);
        if(currentProgram) currentProgram->SetProjectionMatrix(&projection);
        return;
    }
#endif
    switch(state.viewState->projectionMode)
    {
        case PDDI_PROJECTION_DEVICE :
            projection.Identity();
            projection.SetOrthographic(0, display->GetWidth(),
                      display->GetHeight(), 0,
                      (state.viewState->camera.nearPlane),(state.viewState->camera.farPlane));
            glViewport(0, 0, display->GetWidth(), display->GetHeight());
            break;

        case PDDI_PROJECTION_ORTHOGRAPHIC :
            projection.Identity();
            projection.SetOrthographic(-0.5,  0.5,
                      -((1/state.viewState->camera.aspect)/2),  ((1/state.viewState->camera.aspect)/2),
                      (state.viewState->camera.nearPlane),(state.viewState->camera.farPlane));
            glViewport(int(state.viewState->viewWindow.left * display->GetWidth()), 
                              int((1.0f - state.viewState->viewWindow.bottom) * display->GetHeight() ),
                              int((state.viewState->viewWindow.right - state.viewState->viewWindow.left) * display->GetWidth()), 
                              int((state.viewState->viewWindow.bottom - state.viewState->viewWindow.top) * display->GetHeight()));
            break;

        case PDDI_PROJECTION_PERSPECTIVE :
            projection.Identity();
            projection.SetPerspective(state.viewState->camera.fov,state.viewState->camera.aspect,state.viewState->camera.nearPlane,state.viewState->camera.farPlane);
            glViewport(int(state.viewState->viewWindow.left * display->GetWidth()), 
                            int((1.0f - state.viewState->viewWindow.bottom) * display->GetHeight() ),
                            int((state.viewState->viewWindow.right - state.viewState->viewWindow.left) * display->GetWidth()), 
                            int((state.viewState->viewWindow.bottom - state.viewState->viewWindow.top) * display->GetHeight()));
            break;
        default:
            PDDIASSERTMSG(0, "Bad projection mode","");
            break;
    }

#if defined(RAD_ANDROID)
    if (VrIsDynamicVehicleCubeMapCapture())
    {
        // The legacy projection cases above size the viewport from the game
        // display.  A cubemap face owns a much smaller private attachment and
        // must override that display-sized viewport after SetCamera.
        glViewport(0,0,VEHICLE_CUBE_SIZE,VEHICLE_CUBE_SIZE);
    }
    else if (SharOpenXR::GetActiveViewport &&
        SharOpenXR::GetActiveViewport(&xrWidth, &xrHeight))
    {
        if( SharOpenXR::IsRadarRendering && SharOpenXR::IsRadarRendering() &&
            SharOpenXR::IsEmbeddedHudRendering &&
            SharOpenXR::IsEmbeddedHudRendering() )
        {
            // Both the Pure3D viewport and the 2D radar projection now map the
            // same authored Scrooby canvas directly into this target.
            glViewport(
                int(state.viewState->viewWindow.left*1440.0f),
                int((1.0f-state.viewState->viewWindow.bottom)*1080.0f),
                int((state.viewState->viewWindow.right-state.viewState->viewWindow.left)*1440.0f),
                int((state.viewState->viewWindow.bottom-state.viewState->viewWindow.top)*1080.0f));
        }
        else if( SharOpenXR::IsMovieRendering && SharOpenXR::IsMovieRendering() )
        {
            glViewport(0,0,xrWidth,xrHeight);
        }
        else if( SharOpenXR::IsEmbeddedHudRendering &&
            SharOpenXR::IsEmbeddedHudRendering() )
        {
            // A Pure3D HUD widget (the minimap) owns a sub-viewport. Convert
            // that viewport through the same centred VR HUD transform instead
            // of stretching its camera over the complete eye texture.
            const float uiScale=0.40f;
            const float uiVerticalOffset=-0.02f;
            float uiOffset=0.0f;
            SharOpenXR::GetActiveUiHorizontalOffset(&uiOffset);
            float left=state.viewState->viewWindow.left*xrWidth;
            float bottom=(1.0f-state.viewState->viewWindow.bottom)*xrHeight;
            float width=(state.viewState->viewWindow.right-state.viewState->viewWindow.left)*xrWidth;
            float height=(state.viewState->viewWindow.bottom-state.viewState->viewWindow.top)*xrHeight;
            // A projection-space HUD translation moves the regular Scrooby
            // frame farther than the equivalent sub-viewport translation.
            // Convert the minimap's absolute viewport with the full NDC to
            // pixel factor so that it stays registered with that frame.
            left=xrWidth*0.5f+(left-xrWidth*0.5f)*uiScale+uiOffset*xrWidth;
            bottom=xrHeight*0.5f+(bottom-xrHeight*0.5f)*uiScale+uiVerticalOffset*xrHeight*0.5f;
            glViewport(static_cast<int>(left),static_cast<int>(bottom),
                       static_cast<int>(width*uiScale),static_cast<int>(height*uiScale));
        }
        else
        {
            glViewport(0, 0, xrWidth, xrHeight);
        }
    }

    // OpenXR eye FOVs are asymmetric. Move head-locked 2D layers to the
    // optical centre of each eye so menus converge instead of separating.
    float uiOffset = 0.0f;
    if (!VrIsDynamicVehicleCubeMapCapture() &&
        (!SharOpenXR::IsMovieRendering || !SharOpenXR::IsMovieRendering()) &&
        (!SharOpenXR::IsEmbeddedHudRendering ||
         !SharOpenXR::IsEmbeddedHudRendering()) &&
        SharOpenXR::GetActiveUiHorizontalOffset &&
        SharOpenXR::GetActiveUiHorizontalOffset(&uiOffset))
    {
        // Keep the legacy 16:9 HUD inside Quest's comfortable central field.
        // Scaling clip-space X/Y around zero preserves the original layout.
        const float uiScale = 0.40f;
        if( SharOpenXR::HasEnhancedUiConvergence &&
            SharOpenXR::HasEnhancedUiConvergence() )
        {
            uiOffset*=2.0f;
        }
        for(int row=0;row<4;++row)
        {
            projection.m[row][0]*=uiScale;
            projection.m[row][1]*=uiScale;
        }
        projection.m[3][0] += uiOffset;
        // Lower the complete head-locked canvas into the comfortable view.
        // Keep this as one projection-space conversion so individual HUD
        // widgets (map, mission marker, menus) retain their relative layout.
        projection.m[3][1] -= 0.02f;
    }
#endif

    if(currentProgram)
        currentProgram->SetProjectionMatrix(&projection);
}

void pglContext::LoadHardwareMatrix(pddiMatrixType id)
{
    switch(id)
    {
        case PDDI_MATRIX_MODELVIEW :
        {
            if(currentProgram)
                currentProgram->SetModelViewMatrix(state.matrixStack[id]->Top());
        }
        break;
        default :
            PDDIASSERTMSG(0, "Invalid matrix load","");
            break;
    }
}

// viewport clipping
void pglContext::SetScissor(pddiRect* rect)
{
    pddiBaseContext::SetScissor(rect);
#if defined(RAD_ANDROID)
    if(SharOpenXR::IsFrontendPlaneRendering &&
       SharOpenXR::IsFrontendPlaneRendering())
    {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    rmt::Matrix ignoredProjection;
    int xrWidth = 0, xrHeight = 0;
    if (SharOpenXR::GetActiveViewport &&
        SharOpenXR::GetActiveViewport(&xrWidth, &xrHeight))
    {
        if(!rect)
        {
            glDisable(GL_SCISSOR_TEST);
            return;
        }
        const float scaleX=static_cast<float>(xrWidth)/display->GetWidth();
        const float scaleY=static_cast<float>(xrHeight)/display->GetHeight();
        float left=rect->left*scaleX;
        float bottom=(display->GetHeight()-rect->bottom)*scaleY;
        float width=(rect->right-rect->left)*scaleX;
        float height=(rect->bottom-rect->top)*scaleY;

        // The GUI projection is scaled and translated around the eye centre.
        // Its clipping rectangle must undergo the identical transform or a
        // Pure3D HUD widget (notably the map) is clipped above/right of frame.
        float uiOffset=0.0f;
        if( SharOpenXR::GetActiveUiHorizontalOffset &&
            SharOpenXR::GetActiveUiHorizontalOffset(&uiOffset) )
        {
            const float uiScale=0.40f;
            const float uiVerticalOffset=-0.02f;
            // Match the embedded minimap viewport conversion above.  The
            // clip must move by exactly the same amount as the map content.
            left=xrWidth*0.5f+(left-xrWidth*0.5f)*uiScale+uiOffset*xrWidth;
            bottom=xrHeight*0.5f+(bottom-xrHeight*0.5f)*uiScale+uiVerticalOffset*xrHeight*0.5f;
            width*=uiScale;
            height*=uiScale;
        }
        glScissor(static_cast<int>(left),static_cast<int>(bottom),
                  static_cast<int>(width),static_cast<int>(height));
        glEnable(GL_SCISSOR_TEST);
        return;
    }
#endif
    if(!rect)
    {
        glDisable(GL_SCISSOR_TEST);
    }
    else
    {
        glScissor(rect->left, display->GetHeight() - rect->bottom, rect->right - rect->left, rect->bottom - rect->top);
        glEnable(GL_SCISSOR_TEST);
    }
}

#include <vector>
class pglPrimStream : public pddiPrimStream
{
public:
    std::vector<pddiVector> coords;
    std::vector<pddiVector> normals;
    std::vector<GLubyte> colours;
    std::vector<pddiVector2> uvs;

    GLenum primitive;
    unsigned vertexType;

    void Coord(float x, float y, float z)  
    {
        coords.push_back( pddiVector{ x, y, z } );
    }

    void Normal(float x, float y, float z) 
    {
        normals.push_back( pddiVector{ x, y, z } );
    }

    void Colour(pddiColour colour, int channel = 0)
    {
        colours.push_back( colour.Red() );
        colours.push_back( colour.Green() );
        colours.push_back( colour.Blue() );
        colours.push_back( colour.Alpha() );
    }

    void UV(float u, float v, int channel = 0) 
    { 
        if(channel == 0)
        {
            uvs.push_back( pddiVector2{ u, v } );
        }
    }

    void Specular(pddiColour colour) 
    {
        //
    }

    void Vertex(pddiVector* v, pddiColour c) 
    {
        colours.push_back( c.Red() );
        colours.push_back( c.Green() );
        colours.push_back( c.Blue() );
        colours.push_back( c.Alpha() );
        coords.push_back( *v );
    }

    void Vertex(pddiVector* v, pddiVector* n)
    {
        normals.push_back( *n );
        coords.push_back( *v );
    }

    void Vertex(pddiVector* v, pddiVector2* uv)
    {
        uvs.push_back( *uv );
        coords.push_back( *v );
    }

    void Vertex(pddiVector* v, pddiColour c, pddiVector2* uv)
    {
        colours.push_back( c.Red() );
        colours.push_back( c.Green() );
        colours.push_back( c.Blue() );
        colours.push_back( c.Alpha() );
        uvs.push_back( *uv );
        coords.push_back( *v );
    }

    void Vertex(pddiVector* v, pddiVector* n, pddiVector2* uv)
    {
        normals.push_back( *n );
        uvs.push_back( *uv );
        coords.push_back( *v );
    }

} thePrimStream;

pddiPrimStream* pglContext::BeginPrims(pddiShader* mat, pddiPrimType primType, unsigned vertexType, int vertexCount, unsigned pass)
{
    if(!mat)
        mat = defaultShader;

    pddiBaseContext::BeginPrims(mat, primType, vertexType, vertexCount);
    pddiBaseShader* material = (pddiBaseShader*)mat;
    const bool materialChanged=!material->IsCurrent();
    ADD_STAT(PDDI_STAT_MATERIAL_OPS,materialChanged);
#if defined(RAD_ANDROID)
    const double materialStart=PglTelemetryMilliseconds();
#endif
    material->SetMaterial();
#if defined(RAD_ANDROID)
    SharOpenXR::RecordPddiMaterial(materialChanged,PglTelemetryMilliseconds()-materialStart);
#endif
    thePrimStream.primitive = primTypeTable[primType];
    thePrimStream.vertexType = vertexType;
    return &thePrimStream;
}

void pglContext::EndPrims(pddiPrimStream* stream)
{
    MICROPROFILE_SCOPEI("SRR2", "pglContext::EndPrims", MP_RED);

    pddiBaseContext::EndPrims(stream);
#if defined(RAD_ANDROID)
    if(gVehicleCubeCapture && gVehicleCubeSkipDraw) return;
#endif
    pglPrimStream* glstream = (pglPrimStream*)stream;

    glBindVertexArrayOES( 0 );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
    glEnableVertexAttribArray( 0 );
    glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 0, glstream->coords.data() );

    if( !glstream->normals.empty() )
    {
        glEnableVertexAttribArray( 1 );
        glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, 0, glstream->normals.data() );
    }
    else
    {
        glDisableVertexAttribArray( 1 );
        glVertexAttrib3f( 1, 0.0f, 0.0f, 0.0f );
    }

    if( !glstream->uvs.empty() )
    {
        glEnableVertexAttribArray( 2 );
        glVertexAttribPointer( 2, 2, GL_FLOAT, GL_FALSE, 0, glstream->uvs.data() );
    }
    else
    {
        glDisableVertexAttribArray( 2 );
        glVertexAttrib2f( 2, 0.0f, 0.0f );
    }

    if( !glstream->colours.empty() )
    {
        glEnableVertexAttribArray( 3 );
        glVertexAttribPointer( 3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, glstream->colours.data() );
    }
    else
    {
        glDisableVertexAttribArray( 3 );
        glVertexAttrib4f( 3, 1.0f, 1.0f, 1.0f, 1.0f );
    }

#if defined(RAD_ANDROID)
    if(SharOpenXR::PrepareRadarDraw) SharOpenXR::PrepareRadarDraw();
    const double drawStart=PglTelemetryMilliseconds();
#endif
    glDrawArrays( glstream->primitive, 0, glstream->coords.size() );
#if defined(RAD_ANDROID)
    const unsigned primitiveType=glstream->primitive==GL_TRIANGLES?0:
        glstream->primitive==GL_TRIANGLE_STRIP?1:4;
    SharOpenXR::RecordPddiDraw(primitiveType,glstream->coords.size(),false,
                               PglTelemetryMilliseconds()-drawStart);
#endif

    glstream->coords.clear();
    glstream->normals.clear();
    glstream->colours.clear();
    glstream->uvs.clear();
}

class pglPrimBufferStream : public pddiPrimBufferStream
{
public:
    pglPrimBuffer* buffer;

    pglPrimBufferStream(pglPrimBuffer* b)
    {
        buffer = b;
    }

    void Next(void)  
    {
        if(buffer->coord)
            buffer->coord = (float*)((char*)buffer->coord + buffer->stride);

        if(buffer->normal)
            buffer->normal = (float*)((char*)buffer->normal + buffer->stride);

        if(buffer->uv)
            buffer->uv = (float*)((char*)buffer->uv + buffer->stride);

        if(buffer->colour)
            buffer->colour += buffer->stride;

        buffer->total++;
        PDDIASSERT(buffer->total <= buffer->allocated);
    }

    void Position(float x, float y, float z)  
    { 
        buffer->coord[0] = x;
        buffer->coord[1] = y;
        buffer->coord[2] = z;
        Next();
    }

    void Normal(float x, float y, float z) 
    { 
        buffer->normal[0] = x;
        buffer->normal[1] = y;
        buffer->normal[2] = z;
    }

    void Colour(pddiColour colour, int channel = 0)         
    {
        // HBW: Multiple CBVs not yet implemented.  For now just ignore channel.
        buffer->colour[0] = colour.Red();
        buffer->colour[1] = colour.Green();
        buffer->colour[2] = colour.Blue();
        buffer->colour[3] = colour.Alpha();
    }

    void TexCoord1(float u, int channel = 0) {}

    void TexCoord2(float u, float v, int channel = 0) 
    { 
        if(channel == 0)
        {
            buffer->uv[0] = u;
            buffer->uv[1] = v;
        }
    }

    void TexCoord3(float u, float v, float s, int channel = 0) {}
    void TexCoord4(float u, float v, float s, float t, int channel = 0) {}

    void Specular(pddiColour colour) 
    {
        //
    }

    void SkinIndices(unsigned, unsigned, unsigned, unsigned)
    {
    }

    void SkinWeights(float, float, float)
    {
    }

    void Vertex(pddiVector* v, pddiColour c) 
    {
        buffer->colour[0] = c.Red();
        buffer->colour[1] = c.Green();
        buffer->colour[2] = c.Blue();
        buffer->colour[3] = c.Alpha();
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiVector* n)
    {
        buffer->normal[0] = n->x;
        buffer->normal[1] = n->y;
        buffer->normal[2] = n->z;
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiVector2* uv)
    {
        buffer->uv[0] = uv->u;
        buffer->uv[1] = uv->v;
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiColour c, pddiVector2* uv)
    {
        buffer->colour[0] = c.Red();
        buffer->colour[1] = c.Green();
        buffer->colour[2] = c.Blue();
        buffer->colour[3] = c.Alpha();
        buffer->uv[0] = uv->u;
        buffer->uv[1] = uv->v;
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiVector* n, pddiVector2* uv)
    {
        buffer->normal[0] = n->x;
        buffer->normal[1] = n->y;
        buffer->normal[2] = n->z;
        buffer->uv[0] = uv->u;
        buffer->uv[1] = uv->v;
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    bool CheckMemImageVersion(int version) { return false; }
    void* GetMemImagePtr()                 { return NULL; }
    unsigned GetMemImageLength()           { return 0; }

};

pglPrimBuffer::pglPrimBuffer(pglContext* c, pddiPrimType type, unsigned vertexFormat, int nVertex, int nIndex) : context(c)
{
    stream = new pglPrimBufferStream(this);

    total = allocated = stride = nStrips = 0;
    coord = normal = uv = NULL;
    colour = NULL;
    strips = NULL;
    indices = NULL;

    valid = false;
    indexValid = false;
    dynamicVertexBuffer = false;
    vertexBuffer = indexBuffer = vertexArray = 0;

    primType = type;
    vertexType = vertexFormat;

    allocated = nVertex;
    
    stride = 36;

    mem = stride * nVertex;
    buffer = new unsigned char[mem];

    unsigned char* ptr = buffer;
    coord = (float*)ptr;
    ptr += 12;
    
    if(vertexFormat & PDDI_V_NORMAL)
    {
        normal = (float*)ptr;
        ptr += 12;
    }
    
    if(vertexFormat & 0xf)
    {
        uv = (float*)ptr;
        ptr += 8;
    }
    
    if(vertexFormat & PDDI_V_COLOUR)
    {
        colour = ptr;
        ptr += 4;
    }

    indexCount = nIndex;
    if(indexCount) 
        indices = new unsigned short[indexCount];

    nStrips = 0;
    strips = NULL;

    context->ADD_STAT(PDDI_STAT_BUFFERED_COUNT, 1);
    context->ADD_STAT(PDDI_STAT_BUFFERED_ALLOC, mem / 1024.0f);
}

pglPrimBuffer::~pglPrimBuffer()
{
    delete stream;

    delete [] strips;
    delete [] indices;
    delete [] buffer;

    context->ADD_STAT(PDDI_STAT_BUFFERED_COUNT, -1);
    context->ADD_STAT(PDDI_STAT_BUFFERED_ALLOC, -mem / 1024.0f);

    GLuint buffers[] = { vertexBuffer, indexBuffer };
    glDeleteBuffers(2, buffers);
    glDeleteVertexArraysOES(1, &vertexArray);
}

pddiPrimBufferStream* pglPrimBuffer::Lock()
{
    total = 0;
    return stream;
}

void pglPrimBuffer::Unlock(pddiPrimBufferStream* stream)
{
    if(coord)
        coord = (float*)((char*)coord - total * stride);

    if(normal)
        normal = (float*)((char*)normal - total * stride);

    if(uv)
        uv = (float*)((char*)uv - total * stride);

    if(colour)
        colour -= total * stride;

    if(vertexBuffer)
        dynamicVertexBuffer = true;
    valid = false;
}

unsigned char* pglPrimBuffer::LockIndexBuffer()
{
    PDDIASSERT(0);
    return NULL;
}

void pglPrimBuffer::UnlockIndexBuffer(int count)
{
    PDDIASSERT(0);
}

void pglPrimBuffer::SetIndices(unsigned short* i, int count)
{
    PDDIASSERT(count <= (int)indexCount);
    memcpy(indices, i, count * sizeof(unsigned short));
    indexValid = false;
    valid = false;
}

void pglPrimBuffer::Display(void)
{
    MICROPROFILE_SCOPEI("PDDI", "pglPrimBuffer::Display", MP_RED);

    if(!valid)
    {
        if(!vertexArray)
            glGenVertexArraysOES(1, &vertexArray);
        glBindVertexArrayOES(vertexArray);

        if(!vertexBuffer)
            glGenBuffers(1, &vertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
#if defined(RAD_ANDROID)
        const double vertexUploadStart=PglTelemetryMilliseconds();
#endif
        glBufferData(GL_ARRAY_BUFFER, mem, buffer,
                     dynamicVertexBuffer ? GL_STREAM_DRAW : GL_STATIC_DRAW);
#if defined(RAD_ANDROID)
        SharOpenXR::RecordPddiUpload(mem,PglTelemetryMilliseconds()-vertexUploadStart);
#endif

        if(indexCount && indices)
        {
            if(!indexBuffer)
                glGenBuffers(1, &indexBuffer);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,indexBuffer);
            if(!indexValid)
            {
#if defined(RAD_ANDROID)
                const double indexUploadStart=PglTelemetryMilliseconds();
#endif
                glBufferData(GL_ELEMENT_ARRAY_BUFFER,indexCount*sizeof(unsigned short),indices,GL_STATIC_DRAW);
#if defined(RAD_ANDROID)
                SharOpenXR::RecordPddiUpload(indexCount*sizeof(unsigned short),
                    PglTelemetryMilliseconds()-indexUploadStart);
#endif
                indexValid = true;
            }
        }
        else
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
        }

        GLintptr offset = 0;
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,(void*)offset);
        offset += 12;

        if(vertexType & PDDI_V_NORMAL)
        {
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,stride,(void*)offset);
            offset += 12;
        }
        else
        {
            glDisableVertexAttribArray(1);
            glVertexAttrib3f(1, 0.0f, 0.0f, 0.0f);
        }

        if(vertexType & 0xf)
        {
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,stride,(void*)offset);
            offset += 8;
        }
        else
        {
            glDisableVertexAttribArray(2);
            glVertexAttrib2f(2, 0.0f, 0.0f);
        }

        if(vertexType & PDDI_V_COLOUR)
        {
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3,4,GL_UNSIGNED_BYTE,GL_TRUE,stride,(void*)offset);
            offset += 4;
        }
        else
        {
            glDisableVertexAttribArray(3);
            glVertexAttrib4f(3, 1.0f, 1.0f, 1.0f, 1.0f);
        }
        valid = true;
    }
    else
    {
        glBindVertexArrayOES(vertexArray);
    }

    if(indexCount && indices)
    {
#if defined(RAD_ANDROID)
        if(SharOpenXR::PrepareRadarDraw) SharOpenXR::PrepareRadarDraw();
        const double drawStart=PglTelemetryMilliseconds();
#endif
        glDrawElements(primTypeTable[primType],indexCount,GL_UNSIGNED_SHORT,0);
#if defined(RAD_ANDROID)
        SharOpenXR::RecordPddiDraw(static_cast<unsigned>(primType),indexCount,true,
                                   PglTelemetryMilliseconds()-drawStart);
#endif
    }
    else
    {
#if defined(RAD_ANDROID)
        if(SharOpenXR::PrepareRadarDraw) SharOpenXR::PrepareRadarDraw();
        const double drawStart=PglTelemetryMilliseconds();
#endif
        glDrawArrays(primTypeTable[primType], 0, total);
#if defined(RAD_ANDROID)
        SharOpenXR::RecordPddiDraw(static_cast<unsigned>(primType),total,false,
                                   PglTelemetryMilliseconds()-drawStart);
#endif
    }
}

/*
protected:
    float* coord;
    float* normal;
    float* uv;
    unsigned char* colour;

    unsigned allocated;
    unsigned total;

};
*/

void pglContext::DrawPrimBuffer(pddiShader* mat, pddiPrimBuffer* buffer)
{
    if(!mat)
        mat = defaultShader;

    pddiBaseShader* material = (pddiBaseShader*)mat;
    const bool materialChanged=!material->IsCurrent();
    ADD_STAT(PDDI_STAT_MATERIAL_OPS,materialChanged);
#if defined(RAD_ANDROID)
    const double materialStart=PglTelemetryMilliseconds();
#endif
    material->SetMaterial();
#if defined(RAD_ANDROID)
    SharOpenXR::RecordPddiMaterial(materialChanged,PglTelemetryMilliseconds()-materialStart);
    // Leaves, bushes, fences and other cutout/transparent surfaces cause very
    // high overdraw in a probe but contribute only noisy sub-pixel detail to
    // 128x128 vehicle reflections. Keep sky/cloud rendering outside this
    // suppression scope, and reject these world primitives before submission.
    if(gVehicleCubeCapture && gVehicleCubeSkipDraw) return;
    // Some level-specific materials restore their normal GLES program/state
    // from SetMaterial. During a CSM caster replay that can leak packed depth
    // geometry into the eye target as long coloured strips. The shadow pass
    // owns the target and state at the actual submission boundary.
    if(shadowPass)
    {
        glBindFramebuffer(GL_FRAMEBUFFER,shadowFramebuffer[shadowCurrentCascade]);
        glViewport(0,0,shadowCurrentCascade<2?2048:1024,
                         shadowCurrentCascade<2?2048:1024);
        SetShaderProgram(shadowDepthProgram);
        shadowDepthProgram->UseProgram();
        shadowDepthProgram->SetProjectionMatrix(&projection);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    }
    // Legacy materials configure their opaque blend state in SetMaterial().
    // The CSM overlay must win after that state change, immediately before the
    // primitive is submitted, otherwise its alpha is written as solid black.
    else if(shadowOverlayPass)
    {
        SetShaderProgram(shadowOverlayProgram);
        static const float texelSizes[SHADOW_CASCADE_COUNT]={1.0f/2048.0f,1.0f/2048.0f,1.0f/1024.0f};
        currentProgram->SetCascadeShadowState(true,shadowTexture,
                                              shadowReceiverMatrix,texelSizes);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
    }
#endif
    ((pglPrimBuffer*)buffer)->Display();
}

// lighting

int pglContext::GetMaxLights(void)
{
    return PDDI_MAX_LIGHTS;
}

void pglContext::SetupHardwareLight(int handle)
{
    if(currentProgram)
        currentProgram->SetLightState(handle, &state.lightingState->light[handle]);
}

void pglContext::SetAmbientLight(pddiColour col)
{
    pddiBaseContext::SetAmbientLight(col);
    if(currentProgram)
        currentProgram->SetAmbientLight(col);
}


// backface culling
GLenum cullModeTable[3] =
{
    GL_FRONT, // PDDI_CULL_NONE (disabled using glDisable())
    GL_FRONT, // PDDI_CULL_NORMAL
    GL_BACK   // PDDI_CULL_INVERTED
};
    
void pglContext::SetCullMode(pddiCullMode mode)
{
    pddiBaseContext::SetCullMode(mode);

    if(mode == PDDI_CULL_NONE)
    {
        glDisable(GL_CULL_FACE);
    }
    else
    {
        glEnable(GL_CULL_FACE);
        glCullFace(cullModeTable[mode]);
    }
}

// z-buffer control
GLenum compTable[8] = {
    GL_NEVER,
    GL_ALWAYS,  
    GL_LESS,
    GL_LEQUAL,
    GL_GREATER,    
    GL_GEQUAL,  
    GL_EQUAL,
    GL_NOTEQUAL,
};

void pglContext::SetColourWrite( bool red, bool green, bool blue, bool alpha )
{
    pddiBaseContext::SetColourWrite(red, green, blue, alpha);
    glColorMask(red, green, blue, alpha);
}

void pglContext::EnableZBuffer(bool enable)
{
    pddiBaseContext::EnableZBuffer(enable);
    if(enable)
    {
        glEnable(GL_DEPTH_TEST);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
}


void pglContext::SetZCompare(pddiCompareMode compareMode)
{
    pddiBaseContext::SetZCompare(compareMode);
    glDepthFunc(compTable[compareMode]);
}

void pglContext::SetZWrite(bool b)
{
    pddiBaseContext::SetZWrite(b);
    glDepthMask(b);
}

void pglContext::SetZBias(float bias)
{
    pddiBaseContext::SetZBias(bias);
//TODO : Figure out how ro do this
}

void pglContext::SetZRange(float n, float f)
{
    pddiBaseContext::SetZRange(n,f);
    glDepthRangef(n,f);
}

// stencil buffer control
GLenum stencilTable[6] = {
    GL_KEEP,
    GL_ZERO,
    GL_REPLACE,
    GL_INCR,
    GL_DECR,
    GL_INVERT
};

void pglContext::EnableStencilBuffer(bool enable)
{
    pddiBaseContext::EnableStencilBuffer(enable);
    if(enable)
        glEnable(GL_STENCIL_TEST);
    else
        glDisable(GL_STENCIL_TEST);
}
        
void pglContext::SetStencilCompare(pddiCompareMode compare)
{
    pddiBaseContext::SetStencilCompare(compare);
    glStencilFunc(compTable[compare], state.stencilState->ref, state.stencilState->mask);
}

void pglContext::SetStencilRef(int ref)
{
    pddiBaseContext::SetStencilRef(ref);
    glStencilFunc(compTable[state.stencilState->compare], ref, state.stencilState->mask);
}

void pglContext::SetStencilMask(unsigned mask)
{
    pddiBaseContext::SetStencilMask(mask);
    glStencilFunc(compTable[state.stencilState->compare], state.stencilState->ref, mask);
}

void pglContext::SetStencilWriteMask(unsigned mask)
{
    pddiBaseContext::SetStencilWriteMask(mask);
    glStencilMask(mask);
}

void pglContext::SetStencilOp(pddiStencilOp failOp, pddiStencilOp zFailOp, pddiStencilOp zPassOp)
{
    pddiBaseContext::SetStencilOp(failOp, zFailOp, zPassOp);
    glStencilOp(stencilTable[failOp],stencilTable[zFailOp],stencilTable[zPassOp]);
}

void pglContext::SetFillMode(pddiFillMode mode)
{
    pddiBaseContext::SetFillMode(mode);
}

// fog
void pglContext::EnableFog(bool enable)
{
    pddiBaseContext::EnableFog(enable);
}

void pglContext::SetFog(pddiColour colour, float start, float end)
{
    pddiBaseContext::SetFog(colour,start,end);

    float fog[4];
    fog[0] = float(colour.Red()) / 255;
    fog[1] = float(colour.Green()) / 255;
    fog[2] = float(colour.Blue()) / 255;
    fog[3] = float(colour.Alpha()) / 255;
}

int pglContext::GetMaxTextureDimension(void)
{
    return maxTexSize;
}

pddiExtension* pglContext::GetExtension(unsigned extID)
{ 
    switch(extID)
    {
        case PDDI_EXT_GL_CONTEXT :
            return extContext;
        case PDDI_EXT_GAMMACONTROL :
            return extGamma;
    }

    return pddiBaseContext::GetExtension(extID);
}

bool pglContext::VerifyExtension(unsigned extID)
{ 
    switch(extID)
    {
        case PDDI_EXT_GL_CONTEXT :
        case PDDI_EXT_GAMMACONTROL :
            return true;
    }

    return pddiBaseContext::VerifyExtension(extID);
}

void  pglContext::BeginTiming(void)
{
    display->BeginTiming();
}

float pglContext::EndTiming(void)
{
    return display->EndTiming();
}

void pglContext::SetShaderProgram(pglProgram* program)
{
    if(program == currentProgram)
    {
        // The same logical Pure3D material has two native programs on
        // Android: legacy and multiview. Crossing an offscreen/main target
        // boundary can therefore change the native program even though this
        // pointer is unchanged. Re-submit every context-owned uniform below.
        if(!currentProgram) return;
#if defined(RAD_ANDROID) && !defined(RAD_CG)
        // The old renderer returned immediately for an unchanged material.
        // Preserve that hot-path behaviour unless an offscreen pass changed
        // which native (legacy/multiview) program this logical shader needs.
        if(currentProgram->IsRequestedProgramActive()) return;
#else
        return;
#endif
    }
    else
    {
        if(currentProgram) currentProgram->Release();
        currentProgram = program;
        if(!currentProgram) return;
        currentProgram->AddRef();
    }
    currentProgram->UseProgram();
    currentProgram->SetProjectionMatrix(&projection);
#if defined(RAD_ANDROID)
    static const float texelSizes[SHADOW_CASCADE_COUNT]={1.0f/2048.0f,1.0f/2048.0f,1.0f/1024.0f};
    currentProgram->SetCascadeShadowState(shadowReady[0] && shadowReady[1] && shadowReady[2] && shadowReceiverEnabled && !shadowPass,
                                           shadowTexture,shadowReceiverMatrix,texelSizes);
#endif

    LoadHardwareMatrix(PDDI_MATRIX_MODELVIEW);
    if(currentProgram->SupportsLighting())
    {
        for (int i = 0; i < PDDI_MAX_LIGHTS; i++)
            SetupHardwareLight(i);
        SetAmbientLight(state.lightingState->ambient);
    }
}

void pglContext::SetTextureEnvironment(const pglTextureEnv* texEnv)
{
#if defined(RAD_ANDROID)
    gVehicleCubeSkipDraw=gVehicleCubeCapture &&
        gVehicleCubeSuppressTransparent &&
        (texEnv->alphaTest || texEnv->alphaBlendMode!=PDDI_BLEND_NONE);
#endif
#if defined(RAD_ANDROID)
    if(shadowPass)
    {
        SetShaderProgram(shadowDepthProgram);
        currentProgram->SetTextureEnvironment(texEnv);
        return;
    }
#endif
#if defined(RAD_ANDROID)
    if(shadowOverlayPass)
    {
        SetShaderProgram(shadowOverlayProgram);
        static const float texelSizes[SHADOW_CASCADE_COUNT]={1.0f/2048.0f,1.0f/2048.0f,1.0f/1024.0f};
        currentProgram->SetCascadeShadowState(true,shadowTexture,
                                              shadowReceiverMatrix,texelSizes);
        currentProgram->SetTextureEnvironment(texEnv);
        // Materials may have restored their own opaque state immediately
        // before SetDevPass, so enforce the overlay state for every draw.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        return;
    }
#endif
    // World profile 1 is useful only for materials authored as lit.  Sending
    // unlit decals/emissive props through the enhanced fragment path adds a
    // per-pixel normal/view/specular calculation without meaningful input.
    const int requestedMaterialMode=pglGetEnhancedMaterialMode();
    const int effectiveMaterialMode=(requestedMaterialMode==1 && !texEnv->lit)?
                                    0:requestedMaterialMode;
    const bool useLegacyProgram=!shadowReceiverEnabled &&
                                effectiveMaterialMode==0 &&
                                pglGetVehicleRearLightMode()==0;
    const bool useEnhancedOnlyProgram=!shadowReceiverEnabled &&
                                      effectiveMaterialMode>0 &&
                                      pglGetVehicleRearLightMode()==0;
    const bool useVehicleCsmProgram=shadowReceiverEnabled &&
                                    effectiveMaterialMode==2 &&
                                    pglGetVehicleRearLightMode()==0;
    if(texEnv->reflection && texEnv->texture && texEnv->reflectionMap)
        SetShaderProgram(VrHasDynamicVehicleCubeMap()?dynamicReflectionProgram:
                                                        reflectionProgram);
    else if(texEnv->texture)
    {
        if(pglIsParticleRendering() && !texEnv->alphaTest)
            SetShaderProgram(particleTextureProgram);
        else if(useLegacyProgram)
            SetShaderProgram(texEnv->alphaTest?legacyAlphaTestProgram:
                                               legacyTextureProgram);
        else if(useEnhancedOnlyProgram)
            SetShaderProgram(texEnv->alphaTest?enhancedAlphaTestProgram:
                                               enhancedTextureProgram);
        else if(useVehicleCsmProgram)
            SetShaderProgram(texEnv->alphaTest?vehicleCsmAlphaTestProgram:
                                               vehicleCsmTextureProgram);
        else
            SetShaderProgram(texEnv->alphaTest?alphaTestProgram:textureProgram);
    }
    else
        SetShaderProgram(useLegacyProgram?legacyColorProgram:
                         useEnhancedOnlyProgram?enhancedColorProgram:
                         useVehicleCsmProgram?vehicleCsmColorProgram:colorProgram);
    if(!currentProgram)
    {
        // A shader compiler failure must never turn into a null dereference
        // while streaming level geometry. Compile/link diagnostics above are
        // sufficient to identify the rejected variant.
        static bool loggedMissingProgram=false;
        if(!loggedMissingProgram)
        {
            SDL_Log("PDDI: skipping draw because its shader program is unavailable");
            loggedMissingProgram=true;
        }
        return;
    }
    currentProgram->SetTextureEnvironment(texEnv);
}

#if defined(RAD_ANDROID)
void pglContext::RestoreAfterVehicleCubeMap()
{
    // SetShaderProgram normally changes the native program only when a new
    // material is selected. A mono probe interrupted the multiview world pass,
    // so force a known program through that boundary even if Pure3D believes
    // the following material is still current.
    pddiBaseShader::ClearCurrentShader();
    SetShaderProgram(colorProgram);
}
#endif

#if defined(RAD_ANDROID)
bool pglContext::BeginSunShadowMap(int cascadeIndex,const pddiMatrix& eyeCameraToWorld,
                                  pddiMatrix* lightWorldToCamera,
                                  pddiMatrix* lightCameraToWorld)
{
    if(cascadeIndex<0 || cascadeIndex>=SHADOW_CASCADE_COUNT) return false;
    static const int cascadeSizes[SHADOW_CASCADE_COUNT]={2048,2048,1024};
    static const float cascadeHalfWidths[SHADOW_CASCADE_COUNT]={24.0f,56.0f,224.0f};
    static const float cascadeHalfDepths[SHADOW_CASCADE_COUNT]={64.0f,96.0f,155.0f};
    const int shadowSize=cascadeSizes[cascadeIndex];
    const float halfWidth=cascadeHalfWidths[cascadeIndex];
    const float halfDepth=cascadeHalfDepths[cascadeIndex];
    // The right eye reuses the map, but needs its own eye-space to light-space
    // conversion because modelview vertices are already in eye space.
    if(shadowRenderedThisFrame[cascadeIndex])
    {
        shadowReceiverMatrix[cascadeIndex].Mult(eyeCameraToWorld,shadowWorldToClip[cascadeIndex]);
        if(currentProgram)
        {
            static const float texelSizes[SHADOW_CASCADE_COUNT]={1.0f/2048.0f,1.0f/2048.0f,1.0f/1024.0f};
            currentProgram->SetCascadeShadowState(shadowReady[0] && shadowReady[1] && shadowReady[2] && shadowReceiverEnabled,
                                                   shadowTexture,shadowReceiverMatrix,texelSizes);
        }
        return false;
    }

    pddiLight* sun=NULL;
    for(int i=0;i<PDDI_MAX_LIGHTS;i++)
    {
        if(state.lightingState->light[i].enabled &&
           state.lightingState->light[i].type==PDDI_LIGHT_DIRECTIONAL)
        {
            sun=&state.lightingState->light[i];
            break;
        }
    }
    // The level's "sun" tLight is not consistently installed in the PDDI
    // light slots by the time this extra pass runs. CSM still needs a stable
    // world-space direction, so use a fixed Springfield daylight direction
    // when the legacy renderer exposes no directional light.
    static bool loggedFallbackSun=false;
    if(!sun && !loggedFallbackSun)
    {
        SDL_Log("VR CSM: no active PDDI directional light; using fixed sun direction");
        loggedFallbackSun=true;
    }

    // Save the OpenXR eye target and all state touched by the offscreen pass
    // before creating/binding any CSM resource. In particular, never restore
    // framebuffer 0: standalone OpenXR renders into its own framebuffer.
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&shadowSavedFramebuffer);
    glGetIntegerv(GL_RENDERBUFFER_BINDING,&shadowSavedRenderbuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&shadowSavedActiveTexture);
    glGetIntegerv(GL_VIEWPORT,shadowSavedViewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE,shadowSavedClearColour);
    shadowSavedScissor=glIsEnabled(GL_SCISSOR_TEST);
    shadowSavedDepthTest=glIsEnabled(GL_DEPTH_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK,&shadowSavedDepthMask);
    glGetBooleanv(GL_COLOR_WRITEMASK,shadowSavedColourMask);
    glGetIntegerv(GL_DEPTH_FUNC,&shadowSavedDepthFunc);
    glGetFloatv(GL_DEPTH_CLEAR_VALUE,&shadowSavedClearDepth);
    shadowSavedCull=glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_CULL_FACE_MODE,&shadowSavedCullFace);
    shadowSavedPolygonOffset=glIsEnabled(GL_POLYGON_OFFSET_FILL);
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&shadowSavedPolygonFactor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS,&shadowSavedPolygonUnits);
    shadowSavedProjection=projection;

    if(!shadowFramebuffer[cascadeIndex])
    {
        glGenTextures(1,&shadowTexture[cascadeIndex]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D,shadowTexture[cascadeIndex]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,shadowSize,shadowSize,0,
                     GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,NULL);
        // A regular depth sampler is compared manually in the overlay shader.
        // Keep it nearest: linear filtering of this native depth format produced
        // invalid interpolation on the Quest driver and shadowed every receiver.
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_NONE);
        glActiveTexture(GL_TEXTURE0);
        glGenFramebuffers(1,&shadowFramebuffer[cascadeIndex]);
        glBindFramebuffer(GL_FRAMEBUFFER,shadowFramebuffer[cascadeIndex]);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D,shadowTexture[cascadeIndex],0);
        GLenum staleError=GL_NO_ERROR;
        while((staleError=glGetError())!=GL_NO_ERROR) {}
        const GLenum attachStatus=glCheckFramebufferStatus(GL_FRAMEBUFFER);
        const GLenum attachError=glGetError();
        // Quest exposes GLES 3.2: use a genuine depth-only target to avoid the
        // bandwidth and storage of a discarded RGB565 colour attachment.
        // Retain the old attachment as a runtime fallback for other drivers.
        // GLES permits a framebuffer with only a depth attachment. Accept the
        // driver's completed framebuffer directly; glDrawBuffers/glReadBuffer
        // are optional here and this project's GLAD table does not expose
        // their GLES core entry points on Quest even though GLES 3.2 does.
        bool depthOnly=attachStatus==GL_FRAMEBUFFER_COMPLETE &&
                       attachError==GL_NO_ERROR;
        GLenum drawStatus=attachStatus,drawError=GL_NO_ERROR;
        GLenum readStatus=attachStatus,readError=GL_NO_ERROR;
        if(glDrawBuffers && glReadBuffer)
        {
            const GLenum noColour=GL_NONE;
            glDrawBuffers(1,&noColour);
            drawError=glGetError();
            drawStatus=glCheckFramebufferStatus(GL_FRAMEBUFFER);
            glReadBuffer(GL_NONE);
            readError=glGetError();
            readStatus=glCheckFramebufferStatus(GL_FRAMEBUFFER);
            depthOnly=depthOnly && readStatus==GL_FRAMEBUFFER_COMPLETE &&
                      drawError==GL_NO_ERROR && readError==GL_NO_ERROR;
        }
        SDL_Log("VR CSM depth-only probe c%d: attach=0x%x/e0x%x draw=0x%x/e0x%x read=0x%x/e0x%x funcs=%d",
                cascadeIndex,(unsigned)attachStatus,(unsigned)attachError,
                (unsigned)drawStatus,(unsigned)drawError,
                (unsigned)readStatus,(unsigned)readError,
                (glDrawBuffers&&glReadBuffer)?1:0);
        if(!depthOnly)
        {
            glGenRenderbuffers(1,&shadowDepthBuffer[cascadeIndex]);
            glBindRenderbuffer(GL_RENDERBUFFER,shadowDepthBuffer[cascadeIndex]);
            glRenderbufferStorage(GL_RENDERBUFFER,GL_RGB565,shadowSize,shadowSize);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
                                      GL_RENDERBUFFER,shadowDepthBuffer[cascadeIndex]);
            if(glDrawBuffers)
            {
                const GLenum colourAttachment=GL_COLOR_ATTACHMENT0;
                glDrawBuffers(1,&colourAttachment);
            }
        }
        shadowReady[cascadeIndex]=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER,shadowSavedFramebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER,shadowSavedRenderbuffer);
        glActiveTexture(shadowSavedActiveTexture);
        SDL_Log("VR CSM: cascade %d framebuffer %s (%dx%d, %s, manual bilinear PCF)",
                cascadeIndex,shadowReady[cascadeIndex]?"ready":"FAILED",shadowSize,shadowSize,
                depthOnly?"depth-only":"RGB565 fallback");
        if(!shadowReady[cascadeIndex]) return false;
    }

    rmt::Vector direction;
    if(sun)
    {
        direction.Set(sun->worldDirection.x,sun->worldDirection.y,
                      sun->worldDirection.z);
    }
    else
    {
        direction.Set(-0.45f,-1.0f,0.30f);
    }
    direction.Normalize();
    // Use the midpoint of the two OpenXR eyes. An individual eye origin moves
    // around the head as it rotates, which makes a world-locked cascade orbit
    // with the headset and produces two slightly different moving shadows.
    rmt::Matrix centreCamera=eyeCameraToWorld;
    SharOpenXR::GetLatestCullingCamera(&centreCamera);
    rmt::Vector requestedCentre=centreCamera.Row(3);
    if(!shadowStableCentreValid)
    {
        shadowStableCentre=requestedCentre;
        shadowStableCentreValid=true;
    }
    else
    {
        rmt::Vector delta=requestedCentre-shadowStableCentre;
        // HMD motion and eye offsets must not translate the cascade. Move it
        // only when the player has genuinely travelled through the level.
        if(delta.x*delta.x+delta.z*delta.z>16.0f || fabsf(delta.y)>2.0f)
            shadowStableCentre=requestedCentre;
    }
    rmt::Vector centre=shadowStableCentre;
    // Never derive the cascade origin from eye orientation: doing that makes
    // every world shadow swim when the user turns their head. Snap translation
    // to one shadow texel as a first-order temporal stabilization.
    const float worldTexel=(halfWidth*2.0f)/(float)shadowSize;
    centre.x=floorf(centre.x/worldTexel+0.5f)*worldTexel;
    centre.y=floorf(centre.y/worldTexel+0.5f)*worldTexel;
    centre.z=floorf(centre.z/worldTexel+0.5f)*worldTexel;

    // Mid/far cascades contain static geometry only. Keep their maps until
    // the stabilized cascade origin moves instead of replaying thousands of
    // legacy draw calls at 90 Hz. The near map remains dynamic every frame.
    if(cascadeIndex>0 && shadowReady[cascadeIndex] &&
       shadowCascadeCentreValid[cascadeIndex])
    {
        const rmt::Vector centreDelta=centre-shadowCascadeCentre[cascadeIndex];
        if(centreDelta.MagnitudeSqr()<0.000001f)
        {
            shadowRenderedThisFrame[cascadeIndex]=true;
            shadowReceiverMatrix[cascadeIndex].Mult(
                eyeCameraToWorld,shadowWorldToClip[cascadeIndex]);
            return false;
        }
    }
    shadowCascadeCentre[cascadeIndex]=centre;
    shadowCascadeCentreValid[cascadeIndex]=true;

    lightCameraToWorld->Identity();
    lightCameraToWorld->FillHeading(direction,rmt::Vector(0.0f,1.0f,0.0f));
    // A symmetric light-space depth interval is robust against differences
    // in the legacy camera-forward convention. The light's origin can stay
    // at the cascade centre; objects above the ground then have lower depth
    // than their receivers along the downward light direction.
    lightCameraToWorld->FillTranslate(centre);
    lightWorldToCamera->InvertOrtho(*lightCameraToWorld);

    pddiMatrix lightProjection;
    // radmath SetOrthographic only fills scale and translation components;
    // unlike a typical matrix builder it does not initialize the remaining
    // cells. Without Identity() the clip matrix contains stack garbage and
    // every caster can be clipped before rasterization.
    lightProjection.Identity();
    // Keep the light-depth volume proportional to each cascade. The previous
    // +/-155 m interval made the 48 m near map accept distant dynamic objects
    // along the sun axis that cannot cast a useful local shadow.
    lightProjection.SetOrthographic(-halfWidth,halfWidth,-halfWidth,halfWidth,
                                    -halfDepth,halfDepth);
    shadowWorldToClip[cascadeIndex].Mult(*lightWorldToCamera,lightProjection);

    // CSM is a mono offscreen pass even while the world target is multiview.
    // Disable multiview only after all cached-map early exits; EndSunShadowMap
    // restores it after the actual depth pass.
    SharOpenXR::SetMultiviewTargetActive(false);
    glBindFramebuffer(GL_FRAMEBUFFER,shadowFramebuffer[cascadeIndex]);
    glViewport(0,0,shadowSize,shadowSize);
    glDisable(GL_SCISSOR_TEST);
    glClearDepthf(1.0f);
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f,4.0f);
    projection=lightProjection;
    shadowPass=true;
    shadowCurrentCascade=cascadeIndex;
    gPglCsmBillboardMode=1;
    shadowRenderedThisFrame[cascadeIndex]=true;
    SetShaderProgram(shadowDepthProgram);
    // CSM crosses from the eye framebuffer to a private depth framebuffer.
    // The logical program cache can still name shadowDepthProgram from an
    // earlier cascade/eye even when a raw GLES utility draw changed the actual
    // binding. This boundary must always establish the depth program.
    shadowDepthProgram->UseProgram();
    shadowDepthProgram->SetProjectionMatrix(&projection);
    return true;
}

void pglContext::EndSunShadowMap(int cascadeIndex,const pddiMatrix& eyeCameraToWorld)
{
    shadowPass=false;
    gPglCsmBillboardMode=0;
    glBindFramebuffer(GL_FRAMEBUFFER,shadowSavedFramebuffer);
    SharOpenXR::SetMultiviewTargetActive(true);
    glBindRenderbuffer(GL_RENDERBUFFER,shadowSavedRenderbuffer);
    glViewport(shadowSavedViewport[0],shadowSavedViewport[1],
               shadowSavedViewport[2],shadowSavedViewport[3]);
    glClearColor(shadowSavedClearColour[0],shadowSavedClearColour[1],
                 shadowSavedClearColour[2],shadowSavedClearColour[3]);
    if(shadowSavedScissor) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);
    if(shadowSavedDepthTest) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glDepthFunc(shadowSavedDepthFunc);
    glDepthMask(shadowSavedDepthMask);
    glColorMask(shadowSavedColourMask[0],shadowSavedColourMask[1],
                shadowSavedColourMask[2],shadowSavedColourMask[3]);
    glClearDepthf(shadowSavedClearDepth);
    if(shadowSavedCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glCullFace(shadowSavedCullFace);
    if(shadowSavedPolygonOffset) glEnable(GL_POLYGON_OFFSET_FILL);
    else glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(shadowSavedPolygonFactor,shadowSavedPolygonUnits);
    glActiveTexture(shadowSavedActiveTexture);
    projection=shadowSavedProjection;
    shadowReceiverMatrix[cascadeIndex].Mult(eyeCameraToWorld,shadowWorldToClip[cascadeIndex]);
    // Force a normal program now; otherwise the cached depth-only program can
    // survive until a material happens to change its texture state.
    SetShaderProgram(colorProgram);
    colorProgram->UseProgram();
    colorProgram->SetProjectionMatrix(&projection);
    static const float texelSizes[SHADOW_CASCADE_COUNT]={1.0f/2048.0f,1.0f/2048.0f,1.0f/1024.0f};
    colorProgram->SetCascadeShadowState(shadowReady[0] && shadowReady[1] && shadowReady[2] && shadowReceiverEnabled,
                                        shadowTexture,shadowReceiverMatrix,texelSizes);
}

void pglContext::EnableSunShadowReceivers(bool enable)
{
    shadowReceiverEnabled=enable;
    if(currentProgram && !shadowPass)
    {
        static const float texelSizes[SHADOW_CASCADE_COUNT]={1.0f/2048.0f,1.0f/2048.0f,1.0f/1024.0f};
        currentProgram->SetCascadeShadowState(shadowReady[0] && shadowReady[1] && shadowReady[2] && enable,
                                               shadowTexture,shadowReceiverMatrix,texelSizes);
    }
}

void pglContext::BeginSunShadowOverlay()
{
    if(!shadowReady[0] || !shadowReady[1] || !shadowReady[2]) return;
    shadowOverlayPass=true;
    gPglCsmBillboardMode=2;
    shadowOverlaySavedBlend=glIsEnabled(GL_BLEND);
    glGetBooleanv(GL_DEPTH_WRITEMASK,&shadowOverlaySavedDepthMask);
    glGetIntegerv(GL_BLEND_SRC_RGB,&shadowOverlaySavedBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB,&shadowOverlaySavedBlendDst);
    glGetIntegerv(GL_DEPTH_FUNC,&shadowOverlaySavedDepthFunc);
    SetShaderProgram(shadowOverlayProgram);
    shadowOverlayProgram->UseProgram();
    shadowOverlayProgram->SetProjectionMatrix(&projection);
}

void pglContext::EndSunShadowOverlay()
{
    if(!shadowOverlayPass) return;
    shadowOverlayPass=false;
    gPglCsmBillboardMode=0;
    if(shadowOverlaySavedBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc(shadowOverlaySavedBlendSrc,shadowOverlaySavedBlendDst);
    glDepthMask(shadowOverlaySavedDepthMask);
    glDepthFunc(shadowOverlaySavedDepthFunc);
    SetShaderProgram(colorProgram);
    colorProgram->UseProgram();
    colorProgram->SetProjectionMatrix(&projection);
}

bool VrBeginSunShadowMap(pddiRenderContext* context,
                         int cascadeIndex,
                         const pddiMatrix& eyeCameraToWorld,
                         pddiMatrix* lightWorldToCamera,
                         pddiMatrix* lightCameraToWorld)
{
    return static_cast<pglContext*>(context)->BeginSunShadowMap(
        cascadeIndex,eyeCameraToWorld,lightWorldToCamera,lightCameraToWorld);
}

void VrEndSunShadowMap(pddiRenderContext* context,
                       int cascadeIndex,
                       const pddiMatrix& eyeCameraToWorld)
{
    static_cast<pglContext*>(context)->EndSunShadowMap(cascadeIndex,eyeCameraToWorld);
}


void VrEnableSunShadowReceivers(pddiRenderContext* context,bool enable)
{
    static_cast<pglContext*>(context)->EnableSunShadowReceivers(enable);
}

void VrBeginSunShadowOverlay(pddiRenderContext* context)
{
    static_cast<pglContext*>(context)->BeginSunShadowOverlay();
}

void VrEndSunShadowOverlay(pddiRenderContext* context)
{
    static_cast<pglContext*>(context)->EndSunShadowOverlay();
}
#endif

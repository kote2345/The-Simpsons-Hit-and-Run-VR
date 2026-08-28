//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/glprog.hpp>
#include <pddi/gles/glmat.hpp>
#include <pddi/gles/gltex.hpp>

#include <string>
#include <map>
#include <vector>
#include <SDL.h>
#if defined(RAD_ANDROID)
#include <vr/openxrmanager.h>
#endif

#ifdef RAD_CG
#define CGGL_NO_OPENGL
#include <Cg/cg.h>    /* Can't include this?  Is Cg Toolkit installed! */
#include <Cg/cgGL.h>

void pglProgram::checkForCgError()
{
    CGerror error;
    const char* string = cgGetLastErrorString(&error);

    if (error != CG_NO_ERROR) {
        SDL_Log("%s\n", string);
        if (error == CG_COMPILER_ERROR) {
            printf("%s\n", cgGetLastListing(pglProgram::context));
        }
        __debugbreak();
    }
}

CGcontext pglProgram::context = nullptr;
CGprofile pglProgram::vertexProfile, pglProgram::fragmentProfile;
#endif

#ifdef RAD_CG
static inline void UniformColour(CGparameter param, pddiColour c)
{
    cgSetParameter4f(param, float(c.Red()) / 255, float(c.Green()) / 255, float(c.Blue()) / 255, float(c.Alpha()) / 255);
}
#else
static inline void UniformColour(GLint loc, pddiColour c)
{
    glUniform4f(loc, float(c.Red()) / 255, float(c.Green()) / 255, float(c.Blue()) / 255, float(c.Alpha()) / 255);
}
#endif

#if defined(RAD_ANDROID) && !defined(RAD_CG)
namespace
{
struct ShaderSource { GLenum type; std::string text; };
static std::map<GLuint,ShaderSource> sShaderSources;
static bool sMultiviewProgramFailure=false;
static void ReplaceAll(std::string& s,const std::string& from,const std::string& to)
{ for(size_t p=0;(p=s.find(from,p))!=std::string::npos;p+=to.size()) s.replace(p,from.size(),to); }
static bool MakeHardwarePcf(std::string& s)
{
    if(s.find("uniform sampler2D shadowTex;")==std::string::npos) return false;
    ReplaceAll(s,"uniform sampler2D shadowTex;","uniform highp sampler2DShadow shadowTex;");
    ReplaceAll(s,"uniform sampler2D shadowTex1;","uniform highp sampler2DShadow shadowTex1;");
    ReplaceAll(s,"uniform sampler2D shadowTex2;","uniform highp sampler2DShadow shadowTex2;");

    // Replace the complete manual 4-fetch bilinear helpers. The comparison
    // sampler performs the driver's filtered 2x2 PCF in one shader lookup.
    size_t begin=s.find("highp float csmC0(");
    size_t end=s.find("bool csmValid(",begin);
    if(begin!=std::string::npos && end!=std::string::npos)
    {
        s.replace(begin,end-begin,
            "highp float csmC0(highp vec2 u,highp float d){return texture(shadowTex,vec3(u,d-0.00018));} "
            "highp float csmC1(highp vec2 u,highp float d){return texture(shadowTex1,vec3(u,d-0.00018));} "
            "highp float csmC2(highp vec2 u,highp float d){return texture(shadowTex2,vec3(u,d-0.00018));} "
            "highp float csmS0(highp vec3 p){return csmC0(p.xy,p.z);} "
            "highp float csmS1(highp vec3 p){return csmC1(p.xy,p.z);} "
            "highp float csmS2(highp vec3 p){return csmC2(p.xy,p.z);} ");
        return true;
    }
    begin=s.find("float c0(");
    end=s.find("bool valid(",begin);
    if(begin!=std::string::npos && end!=std::string::npos)
    {
        s.replace(begin,end-begin,
            "float s0(vec3 p){return texture(shadowTex,vec3(p.xy,p.z-0.00018));} "
            "float s1(vec3 p){return texture(shadowTex1,vec3(p.xy,p.z-0.00018));} "
            "float s2(vec3 p){return texture(shadowTex2,vec3(p.xy,p.z-0.00018));} ");
        return true;
    }
    return false;
}
static std::string MakeMultiviewShader(GLenum type,const std::string& legacy,
                                       bool hardwarePcf)
{
    std::string s="#version 320 es\n";
    if(type==GL_VERTEX_SHADER) s+="#extension GL_OVR_multiview2 : require\n";
    s+="precision highp float;\nprecision highp int;\n";
    if(type==GL_VERTEX_SHADER) s+="layout(num_views=2) in;\n";
    else s+="layout(location=0) out vec4 pglFragColor;\n";
    s+=legacy; ReplaceAll(s,"attribute ","in "); ReplaceAll(s,"texture2D(","texture(");
    if(type==GL_FRAGMENT_SHADER && hardwarePcf) MakeHardwarePcf(s);
    if(type==GL_VERTEX_SHADER)
    {
        ReplaceAll(s,"varying ","out ");
        ReplaceAll(s,"uniform mat4 projection;","uniform mat4 projection; uniform mat4 vrProjection[2]; uniform mat4 vrViewAdjustment[2];");
        const std::string p="vrProjection[gl_ViewID_OVR]*vrViewAdjustment[gl_ViewID_OVR]*";
        ReplaceAll(s,"projection * V",p+"V"); ReplaceAll(s,"projection*V",p+"V");
        ReplaceAll(s,"projection * v",p+"v"); ReplaceAll(s,"projection*v",p+"v");
        ReplaceAll(s,"projection * modelview",p+"modelview"); ReplaceAll(s,"projection*modelview",p+"modelview");
    }
    else { ReplaceAll(s,"varying ","in "); ReplaceAll(s,"gl_FragColor","pglFragColor"); }
    return s;
}
static GLuint CompileMultiviewShader(GLenum type,const std::string& legacy,
                                     bool hardwarePcf)
{
    const std::string source=MakeMultiviewShader(type,legacy,hardwarePcf); const char* text=source.c_str();
    GLuint shader=glCreateShader(type); glShaderSource(shader,1,&text,0); glCompileShader(shader);
    GLint ok=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&ok); if(ok)return shader;
    GLint n=0;glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&n);std::vector<GLchar> log(n>0?n:1);glGetShaderInfoLog(shader,n,&n,log.data());
    SDL_Log("Multiview shader compilation error: %s",log.data());glDeleteShader(shader);return 0;
}
}
bool pglAreMultiviewProgramsReady(){return !sMultiviewProgramFailure;}
#endif

pglProgram::pglProgram()
{
#ifdef RAD_CG
    program = nullptr;
    projection = modelview = normalmatrix = alpharef = sampler = acs = nullptr;
#else
    program = 0;
#ifdef RAD_ANDROID
    multiviewProgram=0;
    usingMultiviewProgram=false;
    multiviewHardwarePcf=false;
    vrProjection=vrViewAdjustment=-1;
#endif
    projection = modelview = normalmatrix = alpharef = sampler = acs = -1;

    #ifdef RAD_ANDROID
    reflectionSampler = environmentBlend = -1;
    reflectionViewToWorld = -1;
    lit = -1;
    vehiclePaint = -1;
    enhancedSunDirection = -1;
    vehicleDentCount=vehicleDents=-1;
    vehicleRearLightMode=vehicleRearLightCount=vehicleRearLightPositions=vehicleRearLightDirections=vehicleRearLightColour=-1;
    shadowEnabled=shadowTexture=shadowMatrix=shadowTexelSize=-1;
    shadowTextureExtra[0]=shadowTextureExtra[1]=-1;
    shadowMatrixExtra[0]=shadowMatrixExtra[1]=-1;
    shadowTexelSizeExtra[0]=shadowTexelSizeExtra[1]=-1;
    #endif

#endif
}

pglProgram::~pglProgram()
{
#ifdef RAD_CG
    if (program)
        cgDestroyProgram(program);
#else
    if (program)
        glDeleteProgram(program);
#ifdef RAD_ANDROID
    if(multiviewProgram) glDeleteProgram(multiviewProgram);
#endif
#endif
}

void pglProgram::SetProjectionMatrix(const pddiMatrix* matrix)
{
#ifdef RAD_CG
    if (projection)
        cgSetMatrixParameterfc(projection, matrix->m[0]);
    checkForCgError();
#else
    if (projection >= 0)
        glUniformMatrix4fv(projection, 1, GL_FALSE, matrix->m[0]);
#ifdef RAD_ANDROID
    if(usingMultiviewProgram)
    {
        pddiMatrix projections[2],adjustments[2];
        if(!SharOpenXR::GetMultiviewMatrices(reinterpret_cast<rmt::Matrix*>(projections),
                                             reinterpret_cast<rmt::Matrix*>(adjustments)))
        {
            // Orthographic GUI, frontend and other screen-space passes use
            // their Pure3D projection unchanged in both array layers.
            projections[0]=*matrix;projections[1]=*matrix;
            adjustments[0].Identity();adjustments[1].Identity();
        }
        if(vrProjection>=0)
            glUniformMatrix4fv(vrProjection,2,GL_FALSE,projections[0].m[0]);
        if(vrViewAdjustment>=0)
            glUniformMatrix4fv(vrViewAdjustment,2,GL_FALSE,adjustments[0].m[0]);
    }
#endif
#endif
}

void pglProgram::SetModelViewMatrix(const pddiMatrix* matrix)
{
#ifdef RAD_CG
    if (modelview)
        cgSetMatrixParameterfc(modelview, matrix->m[0]);
    checkForCgError();

    if (normalmatrix)
    {
        pddiMatrix inverse;
        inverse.Invert(*matrix);
        inverse.Transpose();
        cgSetMatrixParameterfc(normalmatrix, inverse.m[0]);
    }
    checkForCgError();
#else
    if (modelview >= 0)
        glUniformMatrix4fv(modelview, 1, GL_FALSE, matrix->m[0]);
    if (normalmatrix >= 0)
    {
        pddiMatrix inverse;
        inverse.Invert(*matrix);
        inverse.Transpose();
        glUniformMatrix4fv(normalmatrix, 1, GL_FALSE, inverse.m[0]);
    }
#endif
}

void pglProgram::SetTextureEnvironment(const pglTextureEnv* texEnv)
{
#ifdef RAD_CG
    if (texEnv->lit)
    {
        UniformColour(acm, texEnv->ambient);
        UniformColour(ecm, texEnv->emissive);
        UniformColour(dcm, texEnv->diffuse);
        UniformColour(scm, texEnv->specular);
        cgSetParameter1f(srm, texEnv->shininess);
    }
    else
    {
        UniformColour(acm, pddiColour(-1));
        UniformColour(ecm, pddiColour(-1));
        UniformColour(dcm, pddiColour(-1));
        UniformColour(scm, pddiColour(-1));
        cgSetParameter1f(srm, 0.0f);
    }
    checkForCgError();

    if (texEnv->alphaTest && alpharef >= 0)
    {
        PDDIASSERT(texEnv->alphaCompareMode == PDDI_COMPARE_GREATER ||
            texEnv->alphaCompareMode == PDDI_COMPARE_GREATEREQUAL);
        cgSetParameter1f(alpharef, texEnv->alphaTest ? texEnv->alphaRef : 0.0f);
    }
    checkForCgError();
#else
    if (sampler >= 0)
        glUniform1i(sampler, 0);
#ifdef RAD_ANDROID
    if(reflectionSampler >= 0)
        glUniform1i(reflectionSampler,4);
    if(environmentBlend >= 0)
        UniformColour(environmentBlend,texEnv->envBlend);
    if(reflectionViewToWorld >= 0)
    {
        rmt::Matrix cameraToWorld;
        if(SharOpenXR::GetLatestCullingCamera(&cameraToWorld))
            glUniformMatrix4fv(reflectionViewToWorld,1,GL_FALSE,cameraToWorld.m[0]);
    }
#endif


    #ifdef RAD_ANDROID
    // Characters deliberately use alpha blending even at full opacity.
    // Render-scope and shader-name filtering keep real transparent surfaces
    // out, so blend mode alone must not disable their Phong shading.
    // Alpha-tested fences and grates keep opaque surviving pixels. Their
    // fragment program discards the holes before applying Phong.
    int materialMode=pglGetEnhancedMaterialMode();
    // Profile 1 is the broad world-material scope.  Unlit materials have no
    // useful lighting inputs and commonly cover large screen areas, so leave
    // them on their original inexpensive path.  Explicit vehicle/character
    // profiles remain untouched.
    if(materialMode==1 && !texEnv->lit)
    {
        // Receiver-only profile: keep expensive enhanced material lighting
        // disabled, but retain world position/normal for vehicle lamp cones.
        materialMode=pglGetVehicleRearLightMode()!=0 ? 6 : 0;
    }
    if(materialMode>0)
    {
        static bool loggedModes[6]={false,false,false,false,false,false};
        if(materialMode<6 && !loggedModes[materialMode])
        {
            SDL_Log("GLES Enhanced Materials: active profile %d",materialMode);
            loggedModes[materialMode]=true;
        }
    }
    if (lit >= 0)
        glUniform1i(lit,texEnv->lit ? 1 : 0);
    if (vehiclePaint >= 0)
        glUniform1i(vehiclePaint,materialMode);
    if(enhancedSunDirection>=0)
        glUniform3fv(enhancedSunDirection,1,pglGetEnhancedSunDirection());
    if(vehicleDentCount>=0)
        glUniform1i(vehicleDentCount,pglGetVehicleDeformationCount());
    if(vehicleDents>=0)
        glUniform4fv(vehicleDents,4,pglGetVehicleDeformation());
    if(vehicleRearLightMode>=0) glUniform1i(vehicleRearLightMode,pglGetVehicleRearLightMode());
    if(vehicleRearLightCount>=0) glUniform1i(vehicleRearLightCount,pglGetVehicleRearLightCount());
    if(vehicleRearLightPositions>=0) glUniform3fv(vehicleRearLightPositions,8,pglGetVehicleRearLightPositions());
    if(vehicleRearLightDirections>=0) glUniform3fv(vehicleRearLightDirections,8,pglGetVehicleRearLightDirections());
    if(vehicleRearLightColour>=0) glUniform3fv(vehicleRearLightColour,1,pglGetVehicleRearLightColour());
    #endif

    if (texEnv->lit)
    {
        UniformColour(acm, texEnv->ambient);
        UniformColour(ecm, texEnv->emissive);
        UniformColour(dcm, texEnv->diffuse);
        UniformColour(scm, texEnv->specular);
        glUniform1f(srm, texEnv->shininess);
    }
    else
    {
        UniformColour(acm, pddiColour(-1));
        UniformColour(ecm, pddiColour(-1));
        UniformColour(dcm, pddiColour(-1));
        UniformColour(scm, pddiColour(-1));
        glUniform1f(srm, 0.0f);
    }

    if (alpharef >= 0)
    {
        if(texEnv->alphaTest)
            PDDIASSERT(texEnv->alphaCompareMode == PDDI_COMPARE_GREATER ||
                texEnv->alphaCompareMode == PDDI_COMPARE_GREATEREQUAL);
        glUniform1f(alpharef, texEnv->alphaTest ? texEnv->alphaRef : 0.0f);
    }
#endif
}

void pglProgram::SetLightState(int handle, const pddiLight* lightState)
{
    if( handle >= PDDI_MAX_LIGHTS )
        return;

    float dir[4];
    switch(lightState->type)
    {
        case PDDI_LIGHT_DIRECTIONAL :
            dir[0] = -lightState->worldDirection.x;
            dir[1] = -lightState->worldDirection.y;
            dir[2] = -lightState->worldDirection.z;
            dir[3] = 0.0f;
            break;

        case PDDI_LIGHT_POINT :
            dir[0] = lightState->worldPosition.x;
            dir[1] = lightState->worldPosition.y;
            dir[2] = lightState->worldPosition.z;
            dir[3] = 1.0f;
            break;

        case PDDI_LIGHT_SPOT :
            PDDIASSERT(0);
            break;
    }

#ifdef RAD_CG
    if (lights[handle].enabled)
    {
        cgSetParameter1i(lights[handle].enabled, lightState->enabled ? 1 : 0);
        cgSetParameter4fv(lights[handle].position, dir);
        UniformColour(lights[handle].colour, lightState->colour);
        cgSetParameter3f(lights[handle].attenuation, lightState->attenA, lightState->attenB, lightState->attenC);
    }
    checkForCgError();
#else
    glUniform1i(lights[handle].enabled, lightState->enabled ? 1 : 0);
    glUniform4fv(lights[handle].position, 1, dir);
    UniformColour(lights[handle].colour, lightState->colour);
    glUniform3f(lights[handle].attenuation, lightState->attenA, lightState->attenB, lightState->attenC);
#endif
}

void pglProgram::SetAmbientLight(pddiColour ambient)
{
    if (acs)
        UniformColour(acs, ambient);
}

#if defined(RAD_ANDROID) && !defined(RAD_CG)
void pglProgram::SetShadowState(bool enabled,GLuint texture,
                                const pddiMatrix* matrix,float texelSize)
{
    if(shadowEnabled>=0) glUniform1i(shadowEnabled,enabled?1:0);
    if(shadowMatrix>=0 && matrix)
        glUniformMatrix4fv(shadowMatrix,1,GL_FALSE,matrix->m[0]);
    if(shadowTexelSize>=0) glUniform1f(shadowTexelSize,texelSize);
    if(shadowTexture>=0)
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D,enabled?texture:0);
        glUniform1i(shadowTexture,1);
        glActiveTexture(GL_TEXTURE0);
    }
}

void pglProgram::SetCascadeShadowState(bool enabled,const GLuint* textures,
                                       const pddiMatrix* matrices,
                                       const float* texelSizes)
{
    // The ordinary GLSL ES 1.00 program is the guaranteed manual-PCF
    // fallback. Gameplay multiview uses its independently linked ES 3.20
    // comparison-sampler program. Change sampler state only when crossing
    // those render modes, never once per material.
    static int comparisonMode=-1;
    const int requestedMode=
        usingMultiviewProgram&&multiviewHardwarePcf?1:0;
    if(enabled && comparisonMode!=requestedMode)
    {
        for(int i=0;i<3;++i)
        {
            glActiveTexture(GL_TEXTURE1+i);
            glBindTexture(GL_TEXTURE_2D,textures[i]);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,
                            requestedMode?GL_LINEAR:GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,
                            requestedMode?GL_LINEAR:GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,
                            requestedMode?GL_COMPARE_REF_TO_TEXTURE:GL_NONE);
            if(requestedMode)
                // Manual CSM returns 1.0 for a shadowed receiver:
                // (receiverDepth-bias) > storedDepth. Preserve that convention
                // so both the material darkening and overlay discard paths see
                // the same shadow factor.
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_FUNC,GL_GREATER);
        }
        glActiveTexture(GL_TEXTURE0);
        comparisonMode=requestedMode;
        static bool loggedHardwarePcf=false;
        if(requestedMode && !loggedHardwarePcf)
        {
            SDL_Log("VR CSM: GLSL ES 3.20 hardware 2x2 PCF active");
            loggedHardwarePcf=true;
        }
    }
    SetShadowState(enabled,textures[0],&matrices[0],texelSizes[0]);
    for(int i=0;i<2;++i)
    {
        if(shadowMatrixExtra[i]>=0)
            glUniformMatrix4fv(shadowMatrixExtra[i],1,GL_FALSE,matrices[i+1].m[0]);
        if(shadowTexelSizeExtra[i]>=0)
            glUniform1f(shadowTexelSizeExtra[i],texelSizes[i+1]);
        if(shadowTextureExtra[i]>=0)
        {
            glActiveTexture(GL_TEXTURE2+i);
            glBindTexture(GL_TEXTURE_2D,enabled?textures[i+1]:0);
            glUniform1i(shadowTextureExtra[i],2+i);
        }
    }
    glActiveTexture(GL_TEXTURE0);
}
#endif

#ifdef RAD_CG
bool pglProgram::LinkProgram(CGprogram vertexShader, CGprogram fragmentShader)
{
    program = cgCombinePrograms2(vertexShader, fragmentShader);
    checkForCgError();
    if (!program)
        return false;

    cgGLLoadProgram(program);
    checkForCgError();

    projection = cgGetNamedParameter(program, "projection");
    modelview = cgGetNamedParameter(program, "modelview");
    normalmatrix = cgGetNamedParameter(program, "normalmatrix");
    alpharef = cgGetNamedParameter(program, "alpharef");
    sampler = cgGetNamedParameter(program, "tex");
    checkForCgError();

    for (int i = 0; i < PDDI_MAX_LIGHTS; i++)
    {
        std::string prefix = std::string("lights[") + char('0' + i) + "].";
        lights[i].enabled = cgGetNamedParameter(program, (prefix + "enabled").c_str());
        lights[i].position = cgGetNamedParameter(program, (prefix + "position").c_str());
        lights[i].colour = cgGetNamedParameter(program, (prefix + "colour").c_str());
        lights[i].attenuation = cgGetNamedParameter(program, (prefix + "attenuation").c_str());
        checkForCgError();
    }

    acs = cgGetNamedParameter(program, "acs");
    acm = cgGetNamedParameter(program, "acm");
    dcm = cgGetNamedParameter(program, "dcm");
    scm = cgGetNamedParameter(program, "scm");
    ecm = cgGetNamedParameter(program, "ecm");
    srm = cgGetNamedParameter(program, "srm");
    checkForCgError();
    return true;
}
#else
bool pglProgram::LinkProgram(GLuint vertexShader, GLuint fragmentShader)
{
    program = glCreateProgram();

    if(vertexShader)
        glAttachShader(program, vertexShader);
    if(fragmentShader)
        glAttachShader(program, fragmentShader);

    glBindAttribLocation(program, 0, "position");
    glBindAttribLocation(program, 1, "normal");
    glBindAttribLocation(program, 2, "texcoord");
    glBindAttribLocation(program, 3, "color");

    glLinkProgram(program);

    GLint isLinked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, (int *)&isLinked);
    if (isLinked == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

        // The maxLength includes the NULL character
        std::vector<GLchar> infoLog(maxLength);
        glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);

        SDL_Log("Program linking error: %s", infoLog.data());
        return false;
    }
#ifdef RAD_ANDROID
    const std::map<GLuint,ShaderSource>::const_iterator vsi=sShaderSources.find(vertexShader);
    const std::map<GLuint,ShaderSource>::const_iterator fsi=sShaderSources.find(fragmentShader);
    if(vsi!=sShaderSources.end() && fsi!=sShaderSources.end())
    {
        GLuint mvs=CompileMultiviewShader(GL_VERTEX_SHADER,vsi->second.text,false);
        const bool hasShadowSampler=
            fsi->second.text.find("uniform sampler2D shadowTex;")!=std::string::npos;
        GLuint mfs=CompileMultiviewShader(GL_FRAGMENT_SHADER,fsi->second.text,
                                          hasShadowSampler);
        bool hardwarePcf=hasShadowSampler && mfs!=0;
        if(!mfs && hasShadowSampler)
        {
            SDL_Log("VR CSM: hardware PCF shader rejected; using multiview manual-PCF fallback");
            mfs=CompileMultiviewShader(GL_FRAGMENT_SHADER,fsi->second.text,false);
            hardwarePcf=false;
        }
        if(mvs&&mfs)
        {
            multiviewProgram=glCreateProgram();glAttachShader(multiviewProgram,mvs);glAttachShader(multiviewProgram,mfs);
            glBindAttribLocation(multiviewProgram,0,"position");glBindAttribLocation(multiviewProgram,1,"normal");glBindAttribLocation(multiviewProgram,2,"texcoord");glBindAttribLocation(multiviewProgram,3,"color");
            glLinkProgram(multiviewProgram);GLint linked=0;glGetProgramiv(multiviewProgram,GL_LINK_STATUS,&linked);
            if(!linked && hardwarePcf)
            {
                GLint n=0;glGetProgramiv(multiviewProgram,GL_INFO_LOG_LENGTH,&n);std::vector<GLchar> log(n>0?n:1);glGetProgramInfoLog(multiviewProgram,n,&n,log.data());
                SDL_Log("VR CSM: hardware PCF program rejected at link (%s); using multiview manual-PCF fallback",log.data());
                glDeleteProgram(multiviewProgram);multiviewProgram=0;glDeleteShader(mfs);
                mfs=CompileMultiviewShader(GL_FRAGMENT_SHADER,fsi->second.text,false);
                hardwarePcf=false;
                if(mfs)
                {
                    multiviewProgram=glCreateProgram();glAttachShader(multiviewProgram,mvs);glAttachShader(multiviewProgram,mfs);
                    glBindAttribLocation(multiviewProgram,0,"position");glBindAttribLocation(multiviewProgram,1,"normal");glBindAttribLocation(multiviewProgram,2,"texcoord");glBindAttribLocation(multiviewProgram,3,"color");
                    glLinkProgram(multiviewProgram);glGetProgramiv(multiviewProgram,GL_LINK_STATUS,&linked);
                }
                else linked=0;
            }
            if(!linked)
            {
                if(multiviewProgram)
                {
                    GLint n=0;glGetProgramiv(multiviewProgram,GL_INFO_LOG_LENGTH,&n);std::vector<GLchar> log(n>0?n:1);glGetProgramInfoLog(multiviewProgram,n,&n,log.data());SDL_Log("Multiview program link error: %s",log.data());
                    glDeleteProgram(multiviewProgram);multiviewProgram=0;
                }
                sMultiviewProgramFailure=true;
            }
            else multiviewHardwarePcf=hardwarePcf;
        }
        else sMultiviewProgramFailure=true;
        if(mvs)glDeleteShader(mvs);if(mfs)glDeleteShader(mfs);
    }
#endif
    
    projection = glGetUniformLocation(program, "projection");
    modelview = glGetUniformLocation(program, "modelview");
    normalmatrix = glGetUniformLocation(program, "normalmatrix");
    alpharef = glGetUniformLocation(program, "alpharef");
    sampler = glGetUniformLocation(program, "tex");
#ifdef RAD_ANDROID
    reflectionSampler=glGetUniformLocation(program,"reflectionTex");
    environmentBlend=glGetUniformLocation(program,"environmentBlend");
    reflectionViewToWorld=glGetUniformLocation(program,"reflectionViewToWorld");
#endif

    for (int i = 0; i < PDDI_MAX_LIGHTS; i++)
    {
        std::string prefix = std::string("lights[") + char('0' + i) + "].";
        lights[i].enabled = glGetUniformLocation(program, (prefix + "enabled").c_str());
        lights[i].position = glGetUniformLocation(program, (prefix + "position").c_str());
        lights[i].colour = glGetUniformLocation(program, (prefix + "colour").c_str());
        lights[i].attenuation = glGetUniformLocation( program, (prefix + "attenuation").c_str() );
    }

    acs = glGetUniformLocation(program, "acs");
    acm = glGetUniformLocation(program, "acm");
    dcm = glGetUniformLocation(program, "dcm");
    scm = glGetUniformLocation(program, "scm");
    ecm = glGetUniformLocation(program, "ecm");
    srm = glGetUniformLocation(program, "srm");

    #ifdef RAD_ANDROID
    lit = glGetUniformLocation(program, "lit");
    vehiclePaint = glGetUniformLocation(program, "vehiclePaint");
    enhancedSunDirection = glGetUniformLocation(program,"enhancedSunDirection");
    vehicleDentCount=glGetUniformLocation(program,"vehicleDentCount");
    vehicleDents=glGetUniformLocation(program,"vehicleDents");
    vehicleRearLightMode=glGetUniformLocation(program,"vehicleRearLightMode");
    vehicleRearLightCount=glGetUniformLocation(program,"vehicleRearLightCount");
    vehicleRearLightPositions=glGetUniformLocation(program,"vehicleRearLightPositions");
    vehicleRearLightDirections=glGetUniformLocation(program,"vehicleRearLightDirections");
    vehicleRearLightColour=glGetUniformLocation(program,"vehicleRearLightColour");
    shadowEnabled=glGetUniformLocation(program,"shadowEnabled");
    shadowTexture=glGetUniformLocation(program,"shadowTex");
    shadowMatrix=glGetUniformLocation(program,"shadowMatrix");
    shadowTexelSize=glGetUniformLocation(program,"shadowTexelSize");
    shadowTextureExtra[0]=glGetUniformLocation(program,"shadowTex1");
    shadowTextureExtra[1]=glGetUniformLocation(program,"shadowTex2");
    shadowMatrixExtra[0]=glGetUniformLocation(program,"shadowMatrix1");
    shadowMatrixExtra[1]=glGetUniformLocation(program,"shadowMatrix2");
    shadowTexelSizeExtra[0]=glGetUniformLocation(program,"shadowTexelSize1");
    shadowTexelSizeExtra[1]=glGetUniformLocation(program,"shadowTexelSize2");
    #endif

#ifndef RAD_VITAGL
    // Always detach shaders after a successful link
    if(vertexShader)
        glDetachShader(program, vertexShader);
    if(fragmentShader)
        glDetachShader(program, fragmentShader);
#endif
    return true;
}
#endif

#ifdef RAD_CG
CGprogram pglProgram::CompileShader(GLenum type, const char* source)
{
    if(!context)
    {
        context = cgCreateContext();
        cgGLSetDebugMode(CG_FALSE);
        cgSetParameterSettingMode(context, CG_IMMEDIATE_PARAMETER_SETTING);
        vertexProfile = cgGetProfile("glslv");
        assert(cgGLIsProfileSupported(vertexProfile));
        cgGLSetOptimalOptions(vertexProfile);
        fragmentProfile = cgGetProfile("glslf");
        assert(cgGLIsProfileSupported(fragmentProfile));
        cgGLSetOptimalOptions(fragmentProfile);
    }
    checkForCgError();

    const char* args[] = { "version=330", NULL };
    CGprofile profile = type == GL_VERTEX_SHADER ?
        vertexProfile : fragmentProfile;
    cgGLSetOptimalOptions(profile);
    CGprogram program = cgCreateProgram(
        context,
        CG_SOURCE,
        source,
        profile,
        "main",
        args);
    checkForCgError();
    return program;
}
#else
GLuint pglProgram::CompileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, 0);
    glCompileShader(shader);

    GLint isCompiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if(isCompiled == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

        // The maxLength includes the NULL character
        std::vector<GLchar> infoLog(maxLength);
        glGetShaderInfoLog(shader, maxLength, &maxLength, &infoLog[0]);

        // We don't need the shader anymore.
        glDeleteShader(shader);

        SDL_Log("Shader compilation error: %s", infoLog.data());
        return 0;
    }
#ifdef RAD_ANDROID
    sShaderSources[shader]=ShaderSource{type,source};
#endif
    return shader;
}
#endif

#if defined(RAD_ANDROID) && !defined(RAD_CG)
bool pglProgram::IsRequestedProgramActive() const
{
    return usingMultiviewProgram ==
           (multiviewProgram && SharOpenXR::IsMultiviewRendering());
}
void pglProgram::UseProgram()
{
    const bool requestedMultiview=multiviewProgram && SharOpenXR::IsMultiviewRendering();
    const bool nativeProgramChanged=requestedMultiview!=usingMultiviewProgram;
    usingMultiviewProgram=requestedMultiview;
    glUseProgram(usingMultiviewProgram?multiviewProgram:program);
    // Uniform locations are immutable after link. The legacy locations were
    // cached by LinkProgram; only refresh when crossing between the legacy and
    // multiview native programs, not for every material submission.
    if(nativeProgramChanged) RefreshUniformLocations();
}
void pglProgram::RefreshUniformLocations()
{
    const GLuint p=usingMultiviewProgram?multiviewProgram:program;
    vrProjection=usingMultiviewProgram?glGetUniformLocation(p,"vrProjection[0]"):-1;
    vrViewAdjustment=usingMultiviewProgram?glGetUniformLocation(p,"vrViewAdjustment[0]"):-1;
    projection=glGetUniformLocation(p,"projection");modelview=glGetUniformLocation(p,"modelview");normalmatrix=glGetUniformLocation(p,"normalmatrix");alpharef=glGetUniformLocation(p,"alpharef");sampler=glGetUniformLocation(p,"tex");reflectionSampler=glGetUniformLocation(p,"reflectionTex");environmentBlend=glGetUniformLocation(p,"environmentBlend");reflectionViewToWorld=glGetUniformLocation(p,"reflectionViewToWorld");
    for(int i=0;i<PDDI_MAX_LIGHTS;i++){std::string n=std::string("lights[")+char('0'+i)+"].";lights[i].enabled=glGetUniformLocation(p,(n+"enabled").c_str());lights[i].position=glGetUniformLocation(p,(n+"position").c_str());lights[i].colour=glGetUniformLocation(p,(n+"colour").c_str());lights[i].attenuation=glGetUniformLocation(p,(n+"attenuation").c_str());}
    acs=glGetUniformLocation(p,"acs");acm=glGetUniformLocation(p,"acm");dcm=glGetUniformLocation(p,"dcm");scm=glGetUniformLocation(p,"scm");ecm=glGetUniformLocation(p,"ecm");srm=glGetUniformLocation(p,"srm");
    lit=glGetUniformLocation(p,"lit");vehiclePaint=glGetUniformLocation(p,"vehiclePaint");enhancedSunDirection=glGetUniformLocation(p,"enhancedSunDirection");vehicleDentCount=glGetUniformLocation(p,"vehicleDentCount");vehicleDents=glGetUniformLocation(p,"vehicleDents");vehicleRearLightMode=glGetUniformLocation(p,"vehicleRearLightMode");vehicleRearLightCount=glGetUniformLocation(p,"vehicleRearLightCount");vehicleRearLightPositions=glGetUniformLocation(p,"vehicleRearLightPositions");vehicleRearLightDirections=glGetUniformLocation(p,"vehicleRearLightDirections");vehicleRearLightColour=glGetUniformLocation(p,"vehicleRearLightColour");shadowEnabled=glGetUniformLocation(p,"shadowEnabled");shadowTexture=glGetUniformLocation(p,"shadowTex");shadowMatrix=glGetUniformLocation(p,"shadowMatrix");shadowTexelSize=glGetUniformLocation(p,"shadowTexelSize");for(int i=0;i<2;i++){std::string n=std::to_string(i+1);shadowTextureExtra[i]=glGetUniformLocation(p,("shadowTex"+n).c_str());shadowMatrixExtra[i]=glGetUniformLocation(p,("shadowMatrix"+n).c_str());shadowTexelSizeExtra[i]=glGetUniformLocation(p,("shadowTexelSize"+n).c_str());}
}
#elif !defined(RAD_CG)
void pglProgram::UseProgram(){glUseProgram(program);}
#endif

#ifdef RAD_CG
void pglProgram::UseProgram()
{
    cgUpdateProgramParameters(program);
    cgGLBindProgram(program);
    checkForCgError();
}
#endif

#ifdef RAD_CG
pglProgram* pglProgram::CreateProgram(CGprogram vertexShader, CGprogram fragmentShader)
#else
pglProgram* pglProgram::CreateProgram(GLuint vertexShader, GLuint fragmentShader)
#endif
{
    pglProgram* program = new pglProgram();
    program->AddRef();
    if(!program->LinkProgram(vertexShader, fragmentShader))
    {
        program->Release();
        return nullptr;
    }
    return program;
}

//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/glmat.hpp>
#if defined(RAD_ANDROID)
#include <vr/dynamiccubemap.h>
#endif
#include <pddi/gles/gltex.hpp>
#include <pddi/gles/glcon.hpp>
#include <pddi/gles/glprog.hpp>

#include <vector>
#include <microprofile.h>
#include <SDL.h>

#if defined(RAD_ANDROID)
static int gEnhancedMaterialMode=0;
static bool gParticleRendering=false;
static float gEnhancedSunDirection[3]={0.39f,0.87f,-0.26f};
static float gVehicleDeformation[16]={0.0f};
static int gVehicleDeformationCount=0;
static int gVehicleRearLightMode=0;
static int gVehicleRearLightCount=0;
static bool gVehicleRearLightsSuppressed=false;
static float gVehicleRearLightPositions[24]={0.0f};
static float gVehicleRearLightDirections[24]={0.0f};
static float gVehicleRearLightColour[3]={1.0f,0.04f,0.02f};
void pglSetEnhancedMaterialMode(int mode) { gEnhancedMaterialMode=mode; }
int pglGetEnhancedMaterialMode() { return gEnhancedMaterialMode; }
void pglSetParticleRendering(bool enabled) { gParticleRendering=enabled; }
bool pglIsParticleRendering() { return gParticleRendering; }
void pglSetEnhancedSunDirection(float x,float y,float z)
{ gEnhancedSunDirection[0]=x; gEnhancedSunDirection[1]=y; gEnhancedSunDirection[2]=z; }
const float* pglGetEnhancedSunDirection() { return gEnhancedSunDirection; }
void pglSetVehicleDeformation(const float* dents,int count)
{
    gVehicleDeformationCount=rmt::Clamp(count,0,4);
    for(int i=0;i<16;++i) gVehicleDeformation[i]=(dents && i<gVehicleDeformationCount*4)?dents[i]:0.0f;
}
const float* pglGetVehicleDeformation() { return gVehicleDeformation; }
int pglGetVehicleDeformationCount() { return gVehicleDeformationCount; }
void pglSetVehicleRearLights(int mode,int count,const float* positions,const float* directions,const float* colour)
{
    gVehicleRearLightMode=rmt::Clamp(mode,0,2);
    gVehicleRearLightCount=rmt::Clamp(count,0,8);
    for(int i=0;i<24;++i) gVehicleRearLightPositions[i]=positions?positions[i]:0.0f;
    for(int i=0;i<24;++i) gVehicleRearLightDirections[i]=directions?directions[i]:0.0f;
    for(int i=0;i<3;++i) gVehicleRearLightColour[i]=colour?colour[i]:0.0f;
}
void pglSuppressVehicleRearLights(bool suppress){ gVehicleRearLightsSuppressed=suppress; }
int pglGetVehicleRearLightMode(){ return gVehicleRearLightsSuppressed?0:gVehicleRearLightMode; }
int pglGetVehicleRearLightCount(){ return gVehicleRearLightsSuppressed?0:gVehicleRearLightCount; }
const float* pglGetVehicleRearLightPositions(){ return gVehicleRearLightPositions; }
const float* pglGetVehicleRearLightDirections(){ return gVehicleRearLightDirections; }
const float* pglGetVehicleRearLightColour(){ return gVehicleRearLightColour; }
#endif

pddiShadeColourTable pglMat::colourTable[] = 
{
    {PDDI_SP_AMBIENT  , SHADE_COLOUR(&pglMat::SetAmbient)},
    {PDDI_SP_DIFFUSE  , SHADE_COLOUR(&pglMat::SetDiffuse)},
    {PDDI_SP_EMISSIVE , SHADE_COLOUR(&pglMat::SetEmissive)},
    {PDDI_SP_SPECULAR , SHADE_COLOUR(&pglMat::SetSpecular)},
    // Simple traffic-body shaders can be promoted to the reflection path at
    // runtime. The value is inert until a reflection map is assigned.
    {PDDI_SP_ENVBLEND , SHADE_COLOUR(&pglMat::SetEnvBlend)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeTextureTable pglMat::textureTable[] = 
{
    {PDDI_SP_BASETEX , SHADE_TEXTURE(&pglMat::SetTexture)},
    {PDDI_SP_REFLMAP, SHADE_TEXTURE(&pglMat::SetReflectionMap)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeTextureTable pglMat::reflectionTextureTable[] =
{
    {PDDI_SP_BASETEX, SHADE_TEXTURE(&pglMat::SetTexture)},
    {PDDI_SP_REFLMAP, SHADE_TEXTURE(&pglMat::SetReflectionMap)},
    {PDDI_SP_NULL, NULL}
};

pddiShadeColourTable pglMat::reflectionColourTable[] =
{
    {PDDI_SP_AMBIENT, SHADE_COLOUR(&pglMat::SetAmbient)},
    {PDDI_SP_DIFFUSE, SHADE_COLOUR(&pglMat::SetDiffuse)},
    {PDDI_SP_EMISSIVE, SHADE_COLOUR(&pglMat::SetEmissive)},
    {PDDI_SP_SPECULAR, SHADE_COLOUR(&pglMat::SetSpecular)},
    {PDDI_SP_ENVBLEND, SHADE_COLOUR(&pglMat::SetEnvBlend)},
    {PDDI_SP_NULL, NULL}
};

pddiShadeIntTable pglMat::intTable[] = 
{
    {PDDI_SP_UVMODE , SHADE_INT(&pglMat::SetUVMode)},
    {PDDI_SP_FILTER , SHADE_INT(&pglMat::SetFilterMode)},
    {PDDI_SP_SHADEMODE , SHADE_INT(&pglMat::SetShadeMode)},
    {PDDI_SP_ISLIT , SHADE_INT(&pglMat::EnableLighting)},
    {PDDI_SP_BLENDMODE , SHADE_INT(&pglMat::SetBlendMode)},
    {PDDI_SP_ALPHATEST , SHADE_INT(&pglMat::EnableAlphaTest)},
    {PDDI_SP_ALPHACOMPARE , SHADE_INT(&pglMat::SetAlphaCompare)},
    {PDDI_SP_TWOSIDED , SHADE_INT(&pglMat::SetTwoSided)},
    {PDDI_SP_EMISSIVEALPHA , SHADE_INT(&pglMat::SetEmissiveAlpha)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeFloatTable pglMat::floatTable[] = 
{
    {PDDI_SP_SHININESS , SHADE_FLOAT(&pglMat::SetShininess)},
    {PDDI_SP_ALPHACOMPARE_THRESHOLD , SHADE_FLOAT(&pglMat::SetAlphaRef)},
    {PDDI_SP_NULL , NULL}
};

GLenum filterMagTable[5] =
{
    GL_NEAREST,
    GL_LINEAR,
    GL_NEAREST,
    GL_LINEAR,
    GL_LINEAR
};

GLenum filterMinTable[5] =
{
    GL_NEAREST,
    GL_LINEAR,
    GL_NEAREST_MIPMAP_NEAREST,
    GL_LINEAR_MIPMAP_NEAREST,
    GL_LINEAR_MIPMAP_LINEAR
};

GLenum uvTable[3] =
{
    GL_REPEAT,
    GL_CLAMP_TO_EDGE,
    GL_CLAMP_TO_EDGE
};

GLenum alphaBlendTable[8][3] =
{
    { GL_FUNC_ADD, GL_ONE, GL_ZERO },                       //PDDI_BLEND_NONE,
    { GL_FUNC_ADD, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA },  //PDDI_BLEND_ALPHA
    { GL_FUNC_ADD, GL_ONE, GL_ONE },                        //PDDI_BLEND_ADD
    { GL_FUNC_REVERSE_SUBTRACT, GL_ONE, GL_ONE },           //PDDI_BLEND_SUBTRACT
    { GL_FUNC_ADD, GL_DST_COLOR, GL_ZERO },                 //PDDI_BLEND_MODULATE,
    { GL_FUNC_ADD, GL_DST_COLOR, GL_SRC_COLOR},             //PDDI_BLEND_MODULATE2,
    { GL_FUNC_ADD, GL_ONE, GL_SRC_ALPHA},                   //PDDI_BLEND_ADDMODULATEALPHA,
    { GL_FUNC_REVERSE_SUBTRACT, GL_SRC_ALPHA, GL_SRC_ALPHA} //PDDI_BLEND_SUBMODULATEALPHA
};

pglMat::pglMat(pglContext* c, bool reflection)
{
    context = c;

    for(int i = 0; i < pglMaxPasses; i++)
    {
        texEnv[i].enabled = false;
        texEnv[i].texture = NULL;
        texEnv[i].reflectionMap = NULL;
        texEnv[i].reflection = reflection;
        texEnv[i].envBlend.Set(128,128,128,128);
        texEnv[i].uvSet = i;
        texEnv[i].texGen = PDDI_TEXGEN_NONE;
        texEnv[i].uvMode = PDDI_UV_CLAMP;
        texEnv[i].filterMode = PDDI_FILTER_BILINEAR;

        texEnv[i].lit = false;
        texEnv[i].twoSided = false;
        texEnv[i].shadeMode = PDDI_SHADE_GOURAUD;
        texEnv[i].ambient.Set(255,255,255);
        texEnv[i].diffuse.Set(255,255,255);
        texEnv[i].specular.Set(0,0,0);
        texEnv[i].emissive.Set(0,0,0);
        texEnv[i].shininess = 0.0f;

    //   srcBlend = PDDI_BF_ONE;
    //   destBlend = PDDI_BF_ZERO;

        texEnv[i].alphaTest = false;
        texEnv[i].alphaCompareMode = PDDI_COMPARE_GREATEREQUAL;
        texEnv[i].alphaBlendMode = PDDI_BLEND_NONE;
        texEnv[i].alphaRef = 0.5f;
    }
    texEnv[0].enabled = true;
    pass = 0;
}

pglMat::~pglMat() 
{
    for(int i = 0; i < pglMaxPasses; i++)
    {
        if(texEnv[i].texture)
            texEnv[i].texture->Release();
        if(texEnv[i].reflectionMap)
            texEnv[i].reflectionMap->Release();
    }
}


const char* pglMat::GetType(void)
{
    static char simple[] = "simple";
    return simple;
}

int pglMat::GetPasses(void)
{
    return 1;
}

void pglMat::SetPass(int pass)
{
    SetDevPass(pass);
}

void pglMat::SetTexture(pddiTexture* t) 
{
    if(t == texEnv[pass].texture)
        return;

    if(texEnv[pass].texture)
        texEnv[pass].texture->Release();

    texEnv[pass].texture = (pglTexture*)t;

    if(texEnv[pass].texture)
        texEnv[pass].texture->AddRef();
}

void pglMat::SetReflectionMap(pddiTexture* t)
{
    if(t == texEnv[pass].reflectionMap)
        return;
    if(texEnv[pass].reflectionMap)
        texEnv[pass].reflectionMap->Release();
    texEnv[pass].reflectionMap = static_cast<pglTexture*>(t);
    texEnv[pass].reflection = texEnv[pass].reflectionMap != NULL;
    if(texEnv[pass].reflectionMap)
        texEnv[pass].reflectionMap->AddRef();
}

void pglMat::SetEnvBlend(pddiColour colour)
{
    texEnv[pass].envBlend = colour;
}

void pglMat::SetUVMode(int mode) 
{
    texEnv[pass].uvMode = (pddiUVMode)mode;
}

void pglMat::SetFilterMode(int mode) 
{
    texEnv[pass].filterMode = (pddiFilterMode)mode;
}

void pglMat::SetShadeMode(int shade) 
{
    texEnv[pass].shadeMode = (pddiShadeMode)shade;
}

void pglMat::SetTwoSided(int b)
{
    texEnv[pass].twoSided = b != 0;
}

void pglMat::EnableLighting(int b)
{
    texEnv[pass].lit = b != 0;
}

void pglMat::SetAmbient(pddiColour a) 
{
    texEnv[pass].ambient = a;
}

void pglMat::SetDiffuse(pddiColour colour) 
{
    texEnv[pass].diffuse = colour;
}

void pglMat::SetSpecular(pddiColour c) 
{
    texEnv[pass].specular = c;
}

void pglMat::SetEmissive(pddiColour c) 
{
    texEnv[pass].emissive = c;
    SetEmissiveAlpha(c.Alpha());
}

void pglMat::SetEmissiveAlpha(int alpha)
{
    texEnv[pass].diffuse.SetAlpha(alpha);
    if(alpha < 255)
    {
        texEnv[pass].specular.SetAlpha(0);
        texEnv[pass].ambient.SetAlpha(0);
        texEnv[pass].emissive.SetAlpha(0);
    }
    else
    {
        texEnv[pass].specular.SetAlpha(255);
        texEnv[pass].ambient.SetAlpha(255);
        texEnv[pass].emissive.SetAlpha(255);
    }
}

void pglMat::SetShininess(float power) 
{
    texEnv[pass].shininess = power;
}

void pglMat::SetBlendMode(int mode) 
{
    texEnv[pass].alphaBlendMode = (pddiBlendMode)mode;
}

void pglMat::EnableAlphaTest(int b) 
{
    texEnv[pass].alphaTest = b != 0;
}

void pglMat::SetAlphaCompare(int compare) 
{
    texEnv[pass].alphaCompareMode = pddiCompareMode(compare);
}

void pglMat::SetAlphaRef(float ref) 
{
    texEnv[pass].alphaRef = ref;
}

int pglMat::CountDevPasses(void) 
{
    return 1;
}

void pglMat::SetDevPass(unsigned pass)
{
    MICROPROFILE_SCOPEI( "PDDI", "pglMat::SetDevPass", MP_RED );

    int i = 0;
    
    context->SetTextureEnvironment(&texEnv[i]);

    if(texEnv[i].texture)
    {
#if defined(RAD_ANDROID)
        // Unit 1 is reserved for the sun shadow map.
        glActiveTexture(GL_TEXTURE0);
#endif
        texEnv[i].texture->SetGLState();

        const bool hasMipmaps=texEnv[i].texture->GetNumMipMaps()>0;
        const bool wantsMipmaps=texEnv[i].filterMode>=PDDI_FILTER_MIPMAP;
        const int minFilter=hasMipmaps && wantsMipmaps ? filterMinTable[texEnv[i].filterMode] :
            filterMinTable[texEnv[i].filterMode==PDDI_FILTER_NONE ? PDDI_FILTER_NONE : PDDI_FILTER_BILINEAR];

#if defined(RAD_ANDROID)
        // Quest exposes EXT_texture_filter_anisotropic. Limit it to 8x: this
        // greatly improves roads and ground viewed at grazing angles without
        // the bandwidth cost of blindly requesting the hardware maximum.
        static bool anisotropyChecked=false;
        static float anisotropy=1.0f;
        if(!anisotropyChecked)
        {
            const char* extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
            if(extensions && strstr(extensions,"GL_EXT_texture_filter_anisotropic"))
            {
                float maximum=1.0f;
                glGetFloatv(0x84FF,&maximum); // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
                anisotropy=rmt::Min(8.0f,maximum);
                SDL_Log("GLES: texture anisotropy enabled at %.1fx",anisotropy);
            }
            anisotropyChecked=true;
        }
        texEnv[i].texture->SetSamplerState(filterMagTable[texEnv[i].filterMode],minFilter,
            uvTable[texEnv[i].uvMode],uvTable[texEnv[i].uvMode],
            hasMipmaps && wantsMipmaps ? anisotropy : 1.0f);
#else
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filterMagTable[texEnv[i].filterMode]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,minFilter);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,uvTable[texEnv[i].uvMode]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,uvTable[texEnv[i].uvMode]);
#endif
    }


#if defined(RAD_ANDROID)
    if(texEnv[i].reflection && texEnv[i].reflectionMap)
    {
        // Units 1-3 are reserved for the three sun-shadow cascades.
        glActiveTexture(GL_TEXTURE4);
        if(VrHasDynamicVehicleCubeMap())
        {
            VrBindDynamicVehicleCubeMap();
        }
        else
        {
            texEnv[i].reflectionMap->SetGLState();
            texEnv[i].reflectionMap->SetSamplerState(GL_LINEAR,GL_LINEAR,
                GL_CLAMP_TO_EDGE,GL_CLAMP_TO_EDGE,1.0f);
        }
        glActiveTexture(GL_TEXTURE0);
    }
#endif

    if(texEnv[i].alphaBlendMode == PDDI_BLEND_NONE)
    {
        glDisable(GL_BLEND);
    }
    else
    {
        glEnable(GL_BLEND);
        glBlendEquation(alphaBlendTable[texEnv[i].alphaBlendMode][0]);
        glBlendFunc(alphaBlendTable[texEnv[i].alphaBlendMode][1],alphaBlendTable[texEnv[i].alphaBlendMode][2]);
    }

    if( texEnv[i].twoSided || context->GetCullMode() == PDDI_CULL_NONE )
    {
        glDisable(GL_CULL_FACE);
    }
    else
    {
        glEnable(GL_CULL_FACE);
    }
}

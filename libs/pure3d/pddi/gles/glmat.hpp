//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef _GLMAT_HPP_
#define _GLMAT_HPP_

#include <pddi/pddi.hpp>
#include <pddi/base/baseshader.hpp>
#include <pddi/gles/gl.hpp>
#include <pddi/gles/glcon.hpp>
class pglTexture;
class pglProgram;

#if defined(RAD_ANDROID)
void pglSetEnhancedMaterialMode(int mode);
int pglGetEnhancedMaterialMode();
void pglSetParticleRendering(bool enabled);
bool pglIsParticleRendering();
void pglSetEnhancedSunDirection(float x,float y,float z);
const float* pglGetEnhancedSunDirection();
void pglSetVehicleDeformation(const float* dents, int count);
const float* pglGetVehicleDeformation();
int pglGetVehicleDeformationCount();
void pglSetVehicleRearLights(int mode,int count,const float* positions,const float* directions,const float* colour);
void pglSuppressVehicleRearLights(bool suppress);
int pglGetVehicleRearLightMode();
int pglGetVehicleRearLightCount();
const float* pglGetVehicleRearLightPositions();
const float* pglGetVehicleRearLightDirections();
const float* pglGetVehicleRearLightColour();
#endif

const int pglMaxPasses = 1;

struct pglTextureEnv
{
    bool enabled;
    pglTexture* texture;
    pglTexture* reflectionMap;
    bool reflection;
    pddiColour envBlend;

    int uvSet;
    pddiTextureGen texGen;
    pddiUVMode uvMode;
    pddiFilterMode filterMode;

    bool alphaTest;
    pddiBlendMode alphaBlendMode;
    pddiCompareMode alphaCompareMode;
    float alphaRef;

    bool lit;
    bool twoSided;
    pddiShadeMode shadeMode;
    pddiColour diffuse;
    pddiColour specular;
    pddiColour ambient;
    pddiColour emissive;
    float shininess;
};

class pglMat : public pddiBaseShader
{
public:
    pglMat(pglContext*, bool reflection = false);
    ~pglMat();

    static pddiShadeColourTable colourTable[];
    static pddiShadeTextureTable textureTable[];
    static pddiShadeIntTable intTable[];
    static pddiShadeFloatTable floatTable[];
    static pddiShadeTextureTable reflectionTextureTable[];
    static pddiShadeColourTable reflectionColourTable[];

    const char* GetType(void);
    int         GetPasses(void);
    void        SetPass(int pass);

    pddiShadeTextureTable* GetTextureTable(void) { return texEnv[0].reflection ? reflectionTextureTable : textureTable;}
    pddiShadeIntTable*     GetIntTable(void)     { return intTable;}
    pddiShadeFloatTable*   GetFloatTable(void)   { return floatTable;}
    pddiShadeColourTable*  GetColourTable(void)  { return texEnv[0].reflection ? reflectionColourTable : colourTable;}

    // texture
    void SetTexture(pddiTexture* texture);
    void SetReflectionMap(pddiTexture* texture);
    void SetEnvBlend(pddiColour colour);
    void SetUVMode(int mode);
    void SetFilterMode(int mode);

    // shading
    void SetShadeMode(int shade);
    void SetTwoSided(int);

    // lighting
    void EnableLighting(int);

    void SetDiffuse(pddiColour colour);
    void SetAmbient(pddiColour colour);
    void SetEmissive(pddiColour);
    void SetEmissiveAlpha(int);
    void SetSpecular(pddiColour);
    void SetShininess(float power);

    // alpha blending
    void SetBlendMode(int mode);
    void EnableAlphaTest(int);
    void SetAlphaCompare(int compare);
    void SetAlphaRef(float ref);

    int  CountDevPasses(void);
    void SetDevPass(unsigned);

private:
    pglContext* context;
    int pass;
    pglTextureEnv texEnv[pglMaxPasses];
};

#endif


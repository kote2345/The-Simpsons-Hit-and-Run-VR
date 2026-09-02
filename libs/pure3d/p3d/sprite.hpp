//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef _SPRITE_HPP_
#define _SPRITE_HPP_

#include <p3d/drawable.hpp>
#include <p3d/loadmanager.hpp>
#include <pddi/pdditype.hpp>

struct sprVertex
{
    pddiVector v;
    pddiColour c;
    pddiVector2 uv0;
};

class tTexture;
class pddiMat;
class tImageConverter;
class tImageLoader;
class tImageFactory;
class tImage;
class tShader;

struct tRect;

class tSprite : public tDrawable
{
public:
#ifdef RAD_PS2
    tSprite(tImage*, tShader* Shader = NULL, int blitBorder = 1, int nativeX = 640, int nativeY = 448,tImageConverter* conv = 0);
    tSprite(tTexture**, int w, int h, int count, tShader* Shader = NULL, int blitBorder = 1, int nativeX = 640, int nativeY = 448,tImageConverter* conv = 0);
#else
    tSprite(tImage*, tShader* Shader = NULL, int blitBorder = 1, int nativeX = 640, int nativeY = 480,tImageConverter* conv = 0);
    tSprite(tTexture**, int w, int h, int count, tShader* Shader = NULL, int blitBorder = 1, int nativeX = 640, int nativeY = 480,tImageConverter* conv = 0);
#endif

    void Display();

    // Hemos añadido esta nueva función para representar los iconos en pantalla del táctil
    // la funcion Display no nos servía ya que no utilizaba coordenadas normalizas, por lo que el icono se dibujaba a menos de 1 pixel y no se representaba
    // con esta funcion tendremos control total sobre la posición del icono en pantalla lo cual es importante
    //El flujo será p3d carga en memoria los iconos y pddi se encarga de dibujarlos con esta función
    void DisplayAt( float x, float y, float displayWidth, float displayHeight );

    int GetNativeX(void) const;
    int GetNativeY(void) const;

    int GetHeight(void) const {return height; }
    int GetWidth(void) const {return width; }
    // Read-only access for render backends which reuse the authored sprite
    // resource without invoking the legacy 2D HUD layout.
    tTexture* GetImageTexture(int index = 0) const
    { return index >= 0 && index < nPolys ? textures[index] : NULL; }
    int GetImageCount() const { return nPolys; }
    const sprVertex* GetImageVertices(int index) const
    { return index >= 0 && index < nPolys ? &polys[index * 4] : NULL; }

    virtual void GetBoundingBox(rmt::Box3D* b)          { *b = box; }
    virtual void GetBoundingSphere(rmt::Sphere* s)      { *s = sphere; }

    virtual void ProcessShaders(ShaderCallback&);

    void SetColour(tColour colour); 
    tColour GetColour(void)                         { return colour; }

    void SetBlitBorder(int border);
    int  GetBlitBorder() const                      { return blitBorder; }

    tShader* GetShader(void);

#ifdef RAD_XBOX
    static void EnableLinearTexture(bool enable)  { createLinear = enable; }
#else
    static void EnableLinearTexture(bool enable)  { createLinear = false; }
#endif

protected:
    ~tSprite();

    int  SelectSegment(int val);
    int  CalcSections(tRect** sections, int &numXSect, int &numYSect);
    void BuildSections(tImage* image, tImageConverter* conv);
    void BuildTexture(int texNum, tRect& src, int newX, int newY, tImage* image, tImage* buffer, tImageConverter* conv);
    void BuildPoly(int polyNum, tRect& src, int newX, int newY);

    int nPolys;
    sprVertex* polys;
    tTexture** textures;

    tShader* Shader;

    tColour colour;
    int height, width;
    int blitBorder;
    int nativeX, nativeY;
    int maxSize, minSize;
    float scaleX, scaleY;

    rmt::Box3D box;
    rmt::Sphere sphere;

    bool linear;
    static bool createLinear;
};


class tSpriteLoader : public tSimpleChunkHandler
{
public:
    tSpriteLoader();

    tImageConverter* GetImageConverter(void) { return converter; }

    void SetAlphaTest(bool b) { forceAlphaTest = b; }
    bool GetAlphaTest(void) { return forceAlphaTest; }

    virtual void SetNormalization(bool n) { normalize = n;}
    virtual bool GetNormalization(void) { return normalize;}

    virtual tImage* LoadImage(tChunkFile*,int);
    virtual tTexture* LoadTexture(tChunkFile*,int);

protected:
    ~tSpriteLoader();
    tEntity* LoadObject(tChunkFile*, tEntityStore*);
    tImageConverter* converter;
    tImageLoader* imageLoader;
    tImageFactory* imageFactory;
    bool forceAlphaTest;
    bool normalize;
};

#endif


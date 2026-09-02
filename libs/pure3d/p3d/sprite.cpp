//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include <p3d/sprite.hpp>
#include <p3d/utility.hpp>
#include <p3d/chunkfile.hpp>
#include <p3d/image.hpp>
#include <p3d/imageconverter.hpp>
#include <p3d/texture.hpp>
#include <p3d/file.hpp>
#include <constants/chunks.h>
#include <constants/chunkids.hpp>
#include <p3d/shader.hpp>
#include <p3d/targa.hpp>

static const int IMAGE_VERSION = 14000;

#ifdef RAD_XBOX 
    bool tSprite::createLinear = true;
    static int minSpriteSize = 2;
    static const int maxSpriteSize = 256;
#else
    bool tSprite::createLinear = false;
    static const int minSpriteSize = 32;
    static const int maxSpriteSize = 256;
#endif

struct tSpriteConstruction
{
    tImage* image;
    tImage* buffer;
    tImageConverter* conv;
    int wantAlpha;
};

struct tRect
{
    int top;
    int left;
    int bottom;
    int right;
    int height;
    int width;
};

tSprite::tSprite(tImage* image, tShader* mat, int border, int nx, int ny, tImageConverter* conv)
{
    P3DASSERT(image);

    linear = createLinear;
    
    if (linear)
    {
        SetBlitBorder(0);
    }
    else
    {
        SetBlitBorder(border);
    }

    nativeX = nx;
    nativeY = ny;

    scaleX = 1.0f / float(nativeX);
    scaleY = 1.0f / float(nativeX);

    box.low.x = box.low.y = box.low.z = box.high.z = 0.0f;
    box.high.x = scaleX * image->GetWidth();
    box.high.y = scaleY * image->GetHeight();

    sphere.centre.Set(box.high.x / 2, box.high.y / 2, 0);
    sphere.radius = rmt::Sqrt(box.high.x*box.high.x + box.high.y*box.high.y);

    Shader = mat ? mat : new tShader;
    Shader->AddRef();

    if(!mat)
    {
        Shader->SetInt(PDDI_SP_ISLIT, 0);
        Shader->SetInt(PDDI_SP_BLENDMODE, image->HasAlpha());
    }

    height = image->GetHeight();
    width = image->GetWidth();

    BuildSections(image, conv);

    SetColour(tColour(255,255,255));
}

tSprite::tSprite(tTexture** images, int w, int h, int count, tShader* mat, int border, int nx, int ny, tImageConverter* conv)
{
    linear = false;

#ifdef RAD_XBOX
    minSpriteSize = 32;
#endif

    SetBlitBorder(border);

    nativeX = nx;
    nativeY = ny;

    scaleX = 1.0f / float(nativeX);
    scaleY = 1.0f / float(nativeX);

    box.low.x = box.low.y = box.low.z = box.high.z = 0.0f;
    box.high.x = scaleX * w;
    box.high.y = scaleY * h;

    sphere.centre.Set(box.high.x / 2, box.high.y / 2, 0);
    sphere.radius = rmt::Sqrt(box.high.x*box.high.x + box.high.y*box.high.y);

    Shader = mat ? mat : new tShader;
    Shader->AddRef();

    if(!mat)
    {
        Shader->SetInt(PDDI_SP_ISLIT, 0);
        Shader->SetInt(PDDI_SP_BLENDMODE, 1);
    }

    height = h;
    width = w;

    textures = images;
    polys = new sprVertex[count*4];
    nPolys = count;

    // We've only got one texture, so ignore the blitborder
    if ((textures[0]->GetHeight() == h) && (textures[0]->GetWidth() == w))
    {
        blitBorder = 0;
    }

    int x = 0;
    int y = -blitBorder;
    tRect dim;
    int index = 0;
    int newX = 0;
    int newY = 0;

    while (y<h-1)
    {
        x = -blitBorder;
        while (x<w-1)
        {
            newX = textures[index]->GetWidth();
            newY = textures[index]->GetHeight();
            dim.left = x+blitBorder;
            dim.top = y+blitBorder;
            dim.width = textures[index]->GetWidth()>w ? w : (newX - (blitBorder<<1));
            dim.height = textures[index]->GetHeight()>h ? h : (newY - (blitBorder<<1));
            dim.right = dim.left+dim.width;
            dim.bottom = dim.top+dim.height;
            BuildPoly(index,dim,newX,newY);
            index++;
            x += newX-(blitBorder<<1);
        }
        y += newY-(blitBorder<<1);
    }

    P3DASSERT(index == count);

    SetColour(tColour(255,255,255));

}

tSprite::~tSprite()
{
    Shader->Release();

    for(int i = 0; i < nPolys; i++)
        textures[i]->Release();

    delete [] textures;
    delete [] polys;
}

void tSprite::ProcessShaders(ShaderCallback& callback)
{
    tRefCounted::Assign(Shader, callback.Process(Shader));
}

int tSprite::SelectSegment(int val)
{
    P3DASSERT(val > 0);

    if (val >= maxSize)
    {
        return maxSize;
    }

    if (val <= minSize)
    {
        return val;
    }
  
    return rmt::PrevPow2(val) - (blitBorder<<1);
}

void tSprite::SetBlitBorder(int border)
{ 
    blitBorder = border>=0 ? border : blitBorder; 

    maxSize = p3d::pddi->GetMaxTextureDimension() - (blitBorder<<1);
    minSize = minSpriteSize - (blitBorder<<1);

    P3DASSERT((minSize>=2)&&(maxSize>=minSize));   
}

int tSprite::CalcSections(tRect** sections, int &numXSect, int &numYSect)
{  
    if (linear)
    {
        if (!sections)
        {
            numXSect = 1;
            numYSect = 1;
            return 1;
        }
        else
        {
            sections[0][0].left = 0;
            sections[0][0].right = width;
            sections[0][0].width = width;
            sections[0][0].top = 0;
            sections[0][0].bottom = height;
            sections[0][0].height = height;
            return 1;
        }
    }
    else
    {
        int x = 0;
        int y = 0;
        int xSect, ySect;
        int current, accumulated;

        xSect = 0;
        accumulated = 0;
        while (x<width)
        {
            current = SelectSegment(width-x);
            accumulated = x + current;
            if (sections)
            {
                for (ySect = 0; ySect<numYSect; ySect++)
                {
                    sections[xSect][ySect].left = x;
                    sections[xSect][ySect].right = accumulated;
                    sections[xSect][ySect].width = current;
                }
            }
            x = accumulated;
            xSect++;
        }

        ySect = 0;
        accumulated = 0;
        while (y<height)
        {
            current = SelectSegment(height-y);
            accumulated = y + current;
            if (sections)
            {
                for (xSect = 0; xSect<numXSect; xSect++)
                {
                    sections[xSect][ySect].top = y;
                    sections[xSect][ySect].bottom = accumulated;
                    sections[xSect][ySect].height = current;
                }
            }
            y = accumulated;
            ySect++;
        }

        if (!sections)
        {
            numXSect = xSect;
            numYSect = ySect;
        }

        P3DASSERT(xSect>0);
        P3DASSERT(ySect>0);
        return xSect*ySect;
    }
}

void tSprite::BuildSections(tImage* image, tImageConverter* conv)
{
    int numXSect, numYSect; 
    int xSect, ySect;
    int sectionNum=0;

    nPolys = CalcSections(NULL, numXSect, numYSect);

    tRect** sections = (tRect**)p3d::MallocTemp(numXSect * sizeof(tRect*));
    for (xSect = 0; xSect < numXSect; xSect++)
    {
        sections[xSect] = (tRect*)p3d::MallocTemp(numYSect * sizeof(tRect));
    }

    CalcSections(sections, numXSect, numYSect);

    //create the polygons and textures for the seperated sections of the sprite
    textures = new tTexture*[nPolys];
    polys = new sprVertex[nPolys*4];

    if(!conv)
    {
        static tImageConverter staticConv;
        conv = &staticConv;
    }
    
    tImage* buffer = NULL;

#ifndef RAD_PS2
    if (linear)
    {
        buffer = new tImage32; 
        buffer->EnableColourKey(image->GetColourKeyStatus());
    }
    else
#endif
    {
        switch(image->GetDepth())
        {
            case 8 :  buffer = new tImage8; 
                         ((tImage8*)buffer)->SetPalette(((tImage8*)image)->GetPalette());

                         break;
#ifndef RAD_PS2
            case 32 : buffer = new tImage32; 
                         buffer->EnableColourKey(image->GetColourKeyStatus());
                         break;
#endif
        }  
    }

    buffer->SetAlpha(image->HasAlpha());
    buffer->SetAlphaDepth(image->GetAlphaDepth());

    P3DASSERT(buffer);
    buffer->AddRef();
    
    int border = blitBorder<<1;
    for (xSect = 0; xSect<numXSect; xSect++)
    {
        for (ySect = 0; ySect<numYSect; ySect++)
        {
            int newX = sections[xSect][ySect].width>minSize ?  sections[xSect][ySect].width + border : minSpriteSize;
            int newY = sections[xSect][ySect].height>minSize ?  sections[xSect][ySect].height + border : minSpriteSize;
            BuildTexture(sectionNum, sections[xSect][ySect], newX, newY, image, buffer, conv);
            BuildPoly(sectionNum, sections[xSect][ySect], newX, newY);
            sectionNum++;
        }
    }

    buffer->Release();

    for (xSect = 0; xSect < numXSect; xSect++)
    {
        p3d::FreeTemp(sections[xSect]);
    }
    p3d::FreeTemp(sections);
}

void tSprite::BuildTexture(int texNum, tRect& src, int newX, int newY, tImage* image, tImage* buffer, tImageConverter* conv)
{
    buffer->SetSize(newX, newY);
    buffer->Clear(0);

    int srcX1, srcX2, srcY1, srcY2;
    int destX1, destX2, destY1, destY2;
    int copyWidth, copyHeight;
    int x, y;
    
    //note all src?1 and dest?1 are the starting pixels and all src?2 and dest?2 are ending pixels that are not included!
    //ie srcX1 = 2 and srcX2 = 10, means that pixels 2 thru to 9 inclusive will be copied!
    srcX1 = src.left>blitBorder ? src.left-blitBorder : 0;
    srcX2 = (width-src.right)>blitBorder ? src.right+blitBorder : width;
    srcY1 = src.top>blitBorder ? src.top-blitBorder : 0;
    srcY2 = (height-src.bottom)>blitBorder ? src.bottom+blitBorder : height;
    copyWidth = srcX2-srcX1;
    copyHeight = srcY2-srcY1;
    destX1 = src.left>blitBorder ? 0 : blitBorder-src.left;
    destX2 = destX1 + copyWidth;
    destY1 = src.top>blitBorder ? 0 : blitBorder-src.top;
    destY2 = destY1 + copyHeight;

    //copy inner region of src image to the new texture
    buffer->CopyRegion(image, srcX1, srcY1, copyWidth, copyHeight, destX1, destY1);

    //copy the sides of the texture that don't overlap the original image (ie srcX<0)
    if (copyWidth<newX)
    {
        if (destX1>0)
        {
            for (x = destX1-1; x>=0; x--)
            {
                buffer->CopyRegion(image, srcX1, srcY1, 1, copyHeight, x, destY1);
            }
        }
        if (destX2<newX)
        {
            for (x = destX2; x<newX; x++)
            {
                buffer->CopyRegion(image, srcX2-1, srcY1, 1, copyHeight, x, destY1);
            }
        }
    }
    if (copyHeight<newY)
    {
        if (destY1>0)
        {
            for (y = destY1-1; y>=0; y--)
            {
                buffer->CopyRegion(image, srcX1, srcY1, copyWidth, 1, destX1, y);
            }
        }
        if (destY2<newY)
        {
            for (y = destY2; y<newY; y++)
            {
                buffer->CopyRegion(image, srcX1, srcY2-1, copyWidth, 1, destX1, y);
            }
        }
    }

    unsigned int corner;

    //copy the corner colors
    if (destX1>0)
    {
        if (destY1>0)
        {
            if (buffer->GetDepth()==8)
            {
                corner = ((tImage8*)(buffer))->GetPixelIndex(destX1, destY1);
            }
            else
            {
                corner = buffer->GetColour(destX1, destY1);
            }
            for (x = destX1-1; x>=0; x--)
            {
                for (y = destY1-1; y>=0; y--)
                {
                    buffer->SetColour(x,y,corner);
                }
            }
        }
        if (destY2<newY)
        {
            if (buffer->GetDepth()==8)
            {
                corner = ((tImage8*)(buffer))->GetPixelIndex(destX1, destY2-1);
            }
            else
            {
                corner = buffer->GetColour(destX1, destY2-1);
            }
            for (x = destX1-1; x>=0; x--)
            {
                for (y = destY2; y<newY; y++)
                {
                    buffer->SetColour(x,y,corner);
                }
            }
        }
    }

    if (destX2<newX)
    {
        if (destY1>0)
        {
            if (buffer->GetDepth()==8)
            {
                corner = ((tImage8*)(buffer))->GetPixelIndex(destX2-1, destY1);
            }
            else
            {
                corner = buffer->GetColour(destX2-1, destY1);
            }
            for (x = destX2; x<newX; x++)
            {
                for (y = destY1-1; y>=0; y--)
                {
                    buffer->SetColour(x,y,corner);
                }
            }
        }
        if (destY2<newY)
        {
            if (buffer->GetDepth()==8)
            {
                corner = ((tImage8*)(buffer))->GetPixelIndex(destX2-1, destY2-1);
            }
            else
            {
                corner = buffer->GetColour(destX2-1, destY2-1);
            }
            for (x = destX2; x<newX; x++)
            {
                for (y = destY2; y<newY; y++)
                {
                    buffer->SetColour(x,y,corner);
                }
            }
        }
    }

    textures[texNum] = conv->ImageToTexture(buffer,linear);
    textures[texNum]->AddRef();

/*
char imageName[256];
sprintf(imageName,"sprite_texture_%d.tga",texNum);
tTargaHandler handler;
handler.SaveImage(buffer,imageName);
*/
}

void tSprite::BuildPoly(int polyNum, tRect& src, int newX, int newY)
{
    float u0, u1, v0, v1;

    if (linear)
    {
        u0 = 0.0f;
        u1 = (float)src.width;
        v0 = (float)src.height;
        v1 = 0.0f;
    }
    else
    {
        #ifdef RAD_PS2
            float fudgeU = 0.0f;
            float fudgeV = 0.0f;
        #else
            float fudgeU = (1/((float)newX * 2));
            float fudgeV = (1/((float)newY * 4));
        #endif
        u0 = float(blitBorder) / float(newX) + fudgeU;
        u1 = float(blitBorder+src.width) / float(newX) + fudgeU;
        v0 = 1.0f - float(blitBorder) / float(newY) - fudgeV;
        v1 = 1.0f - float(blitBorder+src.height) / float(newY) - fudgeV;
    }

    for(int i = 0; i < 4; i++)
    {
        polys[(polyNum * 4) + i].c.Set(255,255,255);
    }

    polys[(polyNum * 4) + 0].v.Set(float(src.right) * scaleX, float(height-src.bottom) * scaleY, 0.0f);
    polys[(polyNum * 4) + 0].uv0.u = u1;
    polys[(polyNum * 4) + 0].uv0.v = v1;

    polys[(polyNum * 4) + 1].v.Set(float(src.left) * scaleX, float(height-src.bottom) * scaleY, 0.0f);
    polys[(polyNum * 4) + 1].uv0.u = u0;
    polys[(polyNum * 4) + 1].uv0.v = v1;

    polys[(polyNum * 4) + 2].v.Set(float(src.right) * scaleX, float(height-src.top) * scaleY, 0.0f);
    polys[(polyNum * 4) + 2].uv0.u = u1;
    polys[(polyNum * 4) + 2].uv0.v = v0;

    polys[(polyNum * 4) + 3].v.Set(float(src.left) * scaleX, float(height-src.top) * scaleY, 0.0f);
    polys[(polyNum * 4) + 3].uv0.u = u0;
    polys[(polyNum * 4) + 3].uv0.v = v0;
}

int tSprite::GetNativeX(void) const
{
    return nativeX;
}

int tSprite::GetNativeY(void) const
{
    return nativeY;
}


tShader* tSprite::GetShader(void)
{
    return Shader;
}

void tSprite::SetColour(tColour c) 
{
    colour = c;

    for (int a = 0; a < nPolys * 4; a++)
    {
        polys[a].c = colour;
    }
}

void tSprite::Display()
{
    for(int i = 0; i < nPolys; i++)
    {
//      if(tDrawable2D::RectClip(scratch,uv,colours))
        {
            Shader->GetShader()->SetTexture(PDDI_SP_BASETEX, textures[i]->GetTexture());   

            pddiPrimStream* stream = p3d::pddi->BeginPrims(Shader->GetShader(), PDDI_PRIM_TRISTRIP, PDDI_V_CT, 4);
            stream->Vertex(&polys[i*4].v, polys[i*4].c, &polys[i*4].uv0);
            stream->Vertex(&polys[(i*4)+1].v, polys[(i*4)+1].c, &polys[(i*4)+1].uv0);
            stream->Vertex(&polys[(i*4)+2].v, polys[(i*4)+2].c, &polys[(i*4)+2].uv0);
            stream->Vertex(&polys[(i*4)+3].v, polys[(i*4)+3].c, &polys[(i*4)+3].uv0);

            p3d::pddi->EndPrims(stream);

        }
    }
}

void tSprite::DisplayAt( float x, float y, float displayWidth, float displayHeight )
{
    if( displayWidth <= 0.0f || displayHeight <= 0.0f )
    {
        return;
    }

    if( width <= 0 || height <= 0 )
    {
        return;
    }

    float nearPlane = 0.0f;
    float farPlane = 0.0f;
    float fov = 0.0f;
    float aspect = 0.0f;

    p3d::pddi->GetCamera( &nearPlane, &farPlane, &fov, &aspect );

    const float z = nearPlane + 0.0001f;

    for( int i = 0; i < nPolys; i++ )
    {
        const sprVertex* src = &polys[ i * 4 ];

        float minX = src[ 0 ].v.x;
        float maxX = src[ 0 ].v.x;
        float minY = src[ 0 ].v.y;
        float maxY = src[ 0 ].v.y;

        for( int v = 1; v < 4; ++v )
        {
            if( src[ v ].v.x < minX )
            {
                minX = src[ v ].v.x;
            }

            if( src[ v ].v.x > maxX )
            {
                maxX = src[ v ].v.x;
            }

            if( src[ v ].v.y < minY )
            {
                minY = src[ v ].v.y;
            }

            if( src[ v ].v.y > maxY )
            {
                maxY = src[ v ].v.y;
            }
        }

        const float srcLeftPixels = minX / scaleX;
        const float srcRightPixels = maxX / scaleX;

        const float srcTopPixels = static_cast<float>( height ) - ( maxY / scaleY );
        const float srcBottomPixels = static_cast<float>( height ) - ( minY / scaleY );

        const float left = x + ( srcLeftPixels / static_cast<float>( width ) ) * displayWidth;
        const float right = x + ( srcRightPixels / static_cast<float>( width ) ) * displayWidth;

        const float top = y + ( srcTopPixels / static_cast<float>( height ) ) * displayHeight;
        const float bottom = y + ( srcBottomPixels / static_cast<float>( height ) ) * displayHeight;

        sprVertex quad[ 4 ];

        quad[ 0 ].v.Set( right, bottom, z );
        quad[ 0 ].c = src[ 0 ].c;
        quad[ 0 ].uv0 = src[ 0 ].uv0;

        quad[ 1 ].v.Set( left, bottom, z );
        quad[ 1 ].c = src[ 1 ].c;
        quad[ 1 ].uv0 = src[ 1 ].uv0;

        quad[ 2 ].v.Set( right, top, z );
        quad[ 2 ].c = src[ 2 ].c;
        quad[ 2 ].uv0 = src[ 2 ].uv0;

        quad[ 3 ].v.Set( left, top, z );
        quad[ 3 ].c = src[ 3 ].c;
        quad[ 3 ].uv0 = src[ 3 ].uv0;

        Shader->GetShader()->SetTexture(
            PDDI_SP_BASETEX,
            textures[ i ]->GetTexture()
        );

        pddiPrimStream* stream = p3d::pddi->BeginPrims(
            Shader->GetShader(),
            PDDI_PRIM_TRISTRIP,
            PDDI_V_CT,
            4
        );

        stream->Vertex( &quad[ 0 ].v, quad[ 0 ].c, &quad[ 0 ].uv0 );
        stream->Vertex( &quad[ 1 ].v, quad[ 1 ].c, &quad[ 1 ].uv0 );
        stream->Vertex( &quad[ 2 ].v, quad[ 2 ].c, &quad[ 2 ].uv0 );
        stream->Vertex( &quad[ 3 ].v, quad[ 3 ].c, &quad[ 3 ].uv0 );

        p3d::pddi->EndPrims( stream );
    }
}

tSpriteLoader::tSpriteLoader() : tSimpleChunkHandler(Pure3D::Texture::SPRITE)//P3D_SPRITE)
{
    forceAlphaTest = false;
    normalize = false;
    converter = new tImageConverter;

    imageLoader = new tImageLoader;
    imageLoader->AddRef();
    imageFactory = new tImageFactory;
}

tSpriteLoader::~tSpriteLoader()
{
    imageLoader->Release();
    delete converter;
    delete imageFactory;
}

//------------------------------------------------------------------------
tEntity* tSpriteLoader::LoadObject(tChunkFile* f, tEntityStore* store)
{
    char name[256];
    int nativeX;
    int nativeY;
    char shader[256];
    int imageHeight;
    int imageWidth;
    int blitBorder;
    int imageCount;
    tImage* image = NULL;
    tTexture** images = NULL;
        
    f->GetPString(name);
    nativeX = f->GetLong();
    nativeY = f->GetLong();
    f->GetPString(shader);
    imageWidth = f->GetLong();
    imageHeight = f->GetLong();
    imageCount = f->GetLong();
    blitBorder = f->GetLong();
    int count = 0;
    if ((imageCount>1) || blitBorder)
    {
         images = new tTexture*[imageCount];
    }

    while(f->ChunksRemaining())
    {
        switch(f->BeginChunk())
        {
            case P3D_IMAGE:
                {
                    image = imageLoader->LoadImage(f, 32);
                }
                break;
            case Pure3D::Texture::IMAGE:
                 {
                      if (images)
                      {
                            images[count] = LoadTexture(f,32);
                            images[count]->AddRef();
                            count++;
                      }
                      else
                      {
                          image = LoadImage(f, 32);
                      }

                 }
                 break;

            default:
                break;
        }
        f->EndChunk();
    }

    if(image)
    {
        tShader* mat = p3d::find<tShader>(store, shader);
        tSprite* sprite = new tSprite(image, mat, 1, nativeX, nativeY, converter);
        sprite->SetName(name);

        image->Release();
        return sprite;
    }
    else if (images)
    {
         tShader* mat = p3d::find<tShader>(store, shader);
         tSprite* sprite = new tSprite(images, imageWidth, imageHeight, imageCount, mat, 1, nativeX, nativeY, converter);
         sprite->SetName(name);

         // don't delete the textures any more, cause we use them right off the bat.
         /*for (int i=0; i < imageCount; i++)
         {
              images[i]->Release();
         }*/
         return sprite;
    }
    return NULL;
}

tImage* tSpriteLoader::LoadImage(tChunkFile* f, int depth /*=32*/)
{
    tImage* image = NULL;
    char name[128];
    f->GetPString(name);
    int version = f->GetLong();
    P3DASSERT(version == IMAGE_VERSION);
    int width = f->GetLong();
    int height = f->GetLong();
    int bpp = f->GetLong();
    bool palettized = f->GetLong() == 1;
    bool alpha = f->GetLong() == 1;
    unsigned format = f->GetLong();

    imageFactory->SetDesiredDepth(bpp);

    while(f->ChunksRemaining())
    {
        switch(f->BeginChunk())
        {
            case Pure3D::Texture::IMAGE_DATA:
            {
                unsigned size = f->GetLong();
                tFile* file = f->BeginInset();
                image = imageFactory->ParseAsImage(file, name, (tImageHandler::Format)format);
                f->EndInset(file);
                break;
            }

            case Pure3D::Texture::IMAGE_FILENAME:
            {
                char fileName[255];
                f->GetPString(fileName);
                image = imageFactory->LoadAsImage(fileName, name);
                break;
            }

            default:
                break;
        }
        f->EndChunk();
    }

    if(image)
    {
        image->SetAlpha(alpha);
    }
    return image;
}


tTexture* tSpriteLoader::LoadTexture(tChunkFile* f, int depth /*=32*/)
{
    tTexture* image = NULL;
    char name[128];
    f->GetPString(name);
    int version = f->GetLong();
    P3DASSERT(version == IMAGE_VERSION);
    int width = f->GetLong();
    int height = f->GetLong();
    int bpp = f->GetLong();
    bool palettized = f->GetLong() == 1;
    bool alpha = f->GetLong() == 1;
    unsigned format = f->GetLong();

    imageFactory->SetDesiredDepth(bpp);
    switch(format)
    {
    case tImageHandler::IMG_PS2_4BIT:
         imageFactory->SetTextureHints(8,0,PDDI_TEXTYPE_PS2_4BIT,PDDI_USAGE_STATIC);break;
    case tImageHandler::IMG_PS2_8BIT:
        imageFactory->SetTextureHints(8,0,PDDI_TEXTYPE_PS2_8BIT,PDDI_USAGE_STATIC);break;
    case tImageHandler::IMG_PS2_16BIT:
        imageFactory->SetTextureHints(8,0,PDDI_TEXTYPE_PS2_16BIT,PDDI_USAGE_STATIC);break;
    case tImageHandler::IMG_PS2_32BIT:
        imageFactory->SetTextureHints(8,0,PDDI_TEXTYPE_PS2_32BIT,PDDI_USAGE_STATIC);break;
    default:
         break;
    }


    while(f->ChunksRemaining())
    {
        switch(f->BeginChunk())
        {
            case Pure3D::Texture::IMAGE_DATA:
            {
                unsigned size = f->GetLong();
                tFile* file = f->BeginInset();
                image = imageFactory->ParseAsTexture(file, name, size, (tImageHandler::Format)format);
                f->EndInset(file);
                break;
            }

            case Pure3D::Texture::IMAGE_FILENAME:
            {
                char fileName[255];
                f->GetPString(fileName);
                image = imageFactory->LoadAsTexture(fileName, name);
                break;
            }

            default:
                break;
        }
        f->EndChunk();
    }

    return image;
}


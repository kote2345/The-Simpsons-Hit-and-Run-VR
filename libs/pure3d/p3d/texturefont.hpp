//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef _TEXTURE_FONT_HPP_
#define _TEXTURE_FONT_HPP_

#include <p3d/loadmanager.hpp>
#include <p3d/font.hpp>
#include <radmath/radmath.hpp>

class tTextureFontLoader;
class tTexture;
class tShader;

class pddiPrimStream;

//**************************************************************
// Class tTextureGlyph
//**************************************************************
class tTextureGlyph : public tFontGlyph
{
public:
    tTextureGlyph();
    ~tTextureGlyph();

    inline unsigned int GetTextureNum() const { return textureNum; }
    inline rmt::Vector2 GetBottomLeft() const { return bottomLeft; }
    inline rmt::Vector2 GetTopRight() const   { return topRight; }

    friend class tTextureFontLoader;

protected:
    unsigned int textureNum;
    rmt::Vector2 bottomLeft;
    rmt::Vector2 topRight;
};

//**************************************************************
// Class tTextureFont
//**************************************************************
class tTextureFont : public tFont
{
public:
    // gets the actual font size
    virtual float GetFontSize() const                           { return fontSize; }

    // gets the width of the largest glyph in the font
    virtual float GetFontWidth() const                          { return fontWidth; }

    // gets the height of the font
    virtual float GetFontHeight() const                         { return fontHeight; }

    // gets the base line of the font from top of font
    virtual float GetFontBaseLine() const                       { return fontBaseLine; }
    
    // gets the height of single or multiline text 
    virtual float GetTextHeight(const P3D_UNICODE* text) const;

    // gets the maximum width of single or multiline text
    virtual float GetTextWidth(const P3D_UNICODE* text) const;

    // get the spacing between lines of multiline text
    virtual float GetLineSpacing() const                        { return lineSpacing; } 

    // set the spacing between lines of multiline text
    virtual void SetLineSpacing(float space)                    { lineSpacing = space>0.0f ? space : 0.0f; }

    // get the colour used to draw the font
    virtual tColour GetColour() const                           { return colour; }

    // set the colour used to draw the font
    virtual void SetColour(tColour c)                           { colour = c; }

    // get shader used to draw the font
    virtual tShader* GetShader() const                          { return shader; }

    // set shader used to draw the font
    virtual void SetShader(tShader* s);

    // get the glyph to display if an unknown glyph is encountered in text
    P3D_UNICODE GetMissingLetter() const                        { return glyphs[missingGlyph].GetCode(); }       

    // set the glyph to display if an unknown glyph is encountered in text
    // returns false if glyph doesn't exist
    bool SetMissingLetter(P3D_UNICODE letter);

    unsigned GetTextureCount() const { return numTextures; }
    tTexture* GetTexture(unsigned i) const { return i<numTextures?textures[i]:NULL; }
    unsigned GetGlyphCount() const { return numGlyphs; }
    const tTextureGlyph* GetGlyph(unsigned i) const { return i<numGlyphs?&glyphs[i]:NULL; }
    const tTextureGlyph* GetGlyphForCode(P3D_UNICODE c) const
    { int i=FindGlyph(c);return i>=0?&glyphs[i]:&glyphs[missingGlyph]; }

    friend class tTextureFontLoader;

protected :
    tTextureFont();
    ~tTextureFont();

    // get the width of one line of text
    virtual float GetLineWidth(const P3D_UNICODE* text) const;

    // display a single line of text
    virtual void DisplayLine(const P3D_UNICODE* text, int formatting = 0) const;

    // find the glyph that corresponds to a character code
    int FindGlyph(P3D_UNICODE code) const;

    // display a single glyph
    void DrawGlyph(pddiPrimStream*, unsigned int glyphIndex, float x) const;

    // fill in the spaces between two glyphs
    void DrawBlank(pddiPrimStream* buffer, float x, float width) const;

    float           fontSize;
    float           fontWidth;
    float           fontHeight;
    float           fontBaseLine;
    float           lineSpacing;
    tColour         colour;
    tShader*        shader;
    unsigned int    numTextures;
    tTexture**      textures;
    unsigned int    missingGlyph;
    unsigned int    numGlyphs;
    tTextureGlyph*  glyphs;
    unsigned short *mGlyphCodes;
};

//**************************************************************
// Class tTextureFontLoader
//**************************************************************
class tTextureFontLoader : public tSimpleChunkHandler
{
public:
    tTextureFontLoader();   

protected:
    ~tTextureFontLoader() {}
    virtual tEntity* LoadObject(tChunkFile*, tEntityStore* store);
};

#endif


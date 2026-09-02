//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef _FONT_HPP_
#define _FONT_HPP_

#include <p3d/p3dtypes.hpp>
#include <p3d/entity.hpp>
#include <p3d/unicode.hpp>

class tShader;

enum p3dCentreMode
{
    CENTRE_X = 0x1,
    CENTRE_Y = 0x2
};

//**************************************************************
// Class tFontGlyph
//**************************************************************
class tFontGlyph
{
public:
    tFontGlyph();
    ~tFontGlyph();

    inline float GetLeftBearing() const  { return leftBearing; }
    inline float GetRightBearing() const { return rightBearing; }
    inline float GetWidth() const        { return width; }
    inline float GetAdvance() const      { return advance; }
    inline P3D_UNICODE GetCode() const   { return code; }

protected:
    float        leftBearing;
    float        rightBearing;
    float        width;
    float        advance;
    P3D_UNICODE  code;
};

//**************************************************************
// Class tFont
//**************************************************************
class tFont : public tEntity
{
public:
    // gets the actual font size
    virtual float GetFontSize() const = 0;

    // gets the width of the largest glyph in the font
    virtual float GetFontWidth() const = 0;

    // gets the height of the font
    virtual float GetFontHeight() const = 0;

    // gets the base line of the font from top of font
    virtual float GetFontBaseLine() const = 0;

    // gets the height of single or multiline text 
    float GetTextHeight(const char* text) const
    {
        if (!text)
        {
            return 0.0f;
        }

        P3D_UNICODE unicode[512];
        p3d::AsciiToUnicode(text,unicode,512);
        return GetTextHeight(unicode);
    }
    virtual float GetTextHeight(const P3D_UNICODE* text) const = 0;

    // gets the maximum width of single or multiline text
    float GetTextWidth(const char* text) const
    {
        if (!text)
        {
            return 0.0f;
        }

        P3D_UNICODE unicode[512];
        p3d::AsciiToUnicode(text,unicode,512);
        return GetTextWidth(unicode);
    }
    virtual float GetTextWidth(const P3D_UNICODE* text) const = 0;

    // display single or multiline text
    void DisplayText(const char* text, int formatting) const
    {
        if (!text)
        {
            return;
        }

        P3D_UNICODE unicode[512];
        p3d::AsciiToUnicode(text,unicode,512);
        DisplayText(unicode,formatting);
    }
    virtual void DisplayText(const P3D_UNICODE* text, int formatting = 0) const; 

    // get the spacing between lines of multiline text
    virtual float GetLineSpacing() const = 0;

    // set the spacing between lines of multiline text
    virtual void SetLineSpacing(float space) = 0;

    // get the colour used to draw the font
    virtual tColour GetColour() const = 0;

    // set the colour used to draw the font
    virtual void SetColour(tColour colour) = 0;

    // get shader used to draw the font
    virtual tShader* GetShader() const = 0;

    // set shader used to draw the font
    virtual void SetShader(tShader* shader) = 0;

protected:
    // get the width of one line of text
    virtual float GetLineWidth(const P3D_UNICODE* text) const = 0;

    // display a single line of text
    virtual void DisplayLine(const P3D_UNICODE* text, int formatting = 0) const = 0;
};

#endif


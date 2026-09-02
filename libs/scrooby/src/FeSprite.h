// FeSprite.h
// Created by wng on Wed, Mar 29, 2000 @ 3:08 PM.
//=============================================================================
// Copyright (C) 2001 Radical Entertainment Ltd.  All Rights Reserved
//
// Component   : Scrooby
//
// Description : Implementation of the scrooby sprite class
//
// Author(s)   : wng, Mike Perzel
//
//=============================================================================

// Recompillation Protection
#ifndef __FeSprite__
#define __FeSprite__

//=============================================================================
// Nested Includes
//=============================================================================
#include "Sprite.h"
#include "FeParent.h"
#include "FeBoundedDrawable.h"
#include "utility/rVector.hpp"

//=============================================================================
// Forward References
//=============================================================================
class PascalCString;
class tSprite;

#ifdef WIN32
#pragma warning( disable : 4250 )
#endif

class FeSprite
:
public FeBoundedDrawable,
virtual public Scrooby::Sprite
//=============================================================================
// Description : A class to encapsulate a list of strings referenced by a hash
//               value.
//
// Constraints : Should only be used internally
//=============================================================================
{    
private:
    typedef FeBoundedDrawable Parent;
public:
    void CopyImagesFrom( const FeSprite& source );
    void ReplaceImagesFrom( const FeSprite& source );

    FeSprite( const tName& name, int x, int y );
     
    // add images to the sprite
    void AddImage( unsigned int imageResourceID );
    void AddImage( const char* alias );
    const tSprite* GetRawSprite();
    void SetImage( int index, const char* alias );
    void SetRawSprite( tSprite* sprite, bool updateDrawable = false );

    // methods for indexing of multi-sprites
    virtual int GetIndex();
    virtual void SetIndex( int n );    
    virtual int GetNumOfImages();

    // Overridden methods
    virtual void Reset();
    virtual void Display();
    virtual void ResizeToBoundingBox();
    virtual void GetBoundingBox( int& xMin, int& yMin,
                                 int& xMax, int& yMax ) const;

#ifdef BITMAP_TEXT_SUPPORT
    // display/render bitmap text
    //
    void DisplayBitmapText();
    void DisplayBitmapChar( UnicodeChar character );

    // set current sprite mode
    //
    virtual void SetSpriteMode( Scrooby::eSpriteMode mode );

    // set current bitmap text string
    //
    virtual void CreateBitmapTextBuffer( unsigned int size );
    virtual void SetBitmapText( const UnicodeChar* text );
    virtual void SetBitmapText( const char* text );

    // set character spacing
    //
    virtual void SetBitmapTextSpacing( short numPixels );

    // set line spacing (in y-direction)
    //
    virtual void SetBitmapTextLineSpacing( short numPixels );
#endif

    #ifdef DEBUGWATCH
        virtual void WatchAll       ( const char* nameSpace );
        virtual void WatchIndex     ( const char* nameSpace );
    #endif


protected:
    virtual ~FeSprite();

#ifdef BITMAP_TEXT_SUPPORT
    static float ConvertToScreenX( int pixels );
    static float ConvertToScreenY( int pixels );

    void SetupTextWrapping();

    struct BitmapTextData
    {
        UnicodeChar* m_textBuffer;
        short m_textBufferSize;
        short m_textSpacing;
        short m_lineSpacing;

        // for text wrapping
        //
        static const int MAX_NUM_LINES = 8;
        short m_newlineIndex[ MAX_NUM_LINES + 1 ];
        float m_textWidth[ MAX_NUM_LINES ];
    };

    BitmapTextData* m_bitmapTextData;
#endif

    // Index of the current image
    int mIndex;
    rVector<PascalCString*> mAliases;
    tSprite* mSprite;
};

#endif

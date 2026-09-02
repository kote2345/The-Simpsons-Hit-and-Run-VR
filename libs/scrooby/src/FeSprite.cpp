//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   Scrooby Runtime Public Interface
//
// Description: This contains the Drawable Base class.
//
// Authors:     Ian Gipson, Mike Perzel( 11/30/2001 )
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================

// MIKE IMPROVE : Can this be cleaned up a little?
#include "FeSprite.h"

#include "FeApp.h"
#include "FeScreen.h"
#include <raddebug.hpp>
#include "ResourceManager/FeResourceManager.h"
#include <p3d/matrixstack.hpp>
#include <p3d/shader.hpp>
#include <p3d/sprite.hpp>
#include <p3d/utility.hpp>
#include "utility/debugMessages.h"

#include <FeCharMap.h>
#include <p3d/unicode.hpp>
#include <algorithm>

#ifdef BITMAP_TEXT_SUPPORT
    const short DEFAULT_BITMAP_TEXT_SPACING = 0; // in pixels
    const UnicodeChar BITMAP_TEXT_LINEBREAK = '\\';
#endif

//===========================================================================
// Member Functions
//===========================================================================

//===========================================================================
// FeSprite::FeSprite
//===========================================================================
// Description: constructor
//
// Parameters:    name - the name to be passed up to the FeDrawable
//              x - int - the x coordinate of the sprite
//              y - int - the y coordinate of the sprite
//
//===========================================================================
FeSprite::FeSprite( const tName& name, int x, int y ) 
: 
    FeBoundedDrawable( name ),
#ifdef BITMAP_TEXT_SUPPORT
    m_bitmapTextData( NULL ),
#endif
    mIndex( -1 ),
    mSprite( NULL )
{
    NormalizeCoord( x, y, mPosX, mPosY );
    Reset();
}

//===========================================================================
// FeSprite::~FeSprite
//===========================================================================
// Description: destructor
//
// Constraints:    should only be called by the Release method
//
//===========================================================================
FeSprite::~FeSprite()
{
    while( mAliases.Size() )
    {
        int i = mAliases.Size() - 1;
        if( mAliases[i] )
        {
            delete mAliases[i];
            mAliases.Erase( i );
        }
    }

#ifdef BITMAP_TEXT_SUPPORT
    if( m_bitmapTextData != NULL )
    {
        if( m_bitmapTextData->m_textBuffer != NULL )
        {
            delete [] m_bitmapTextData->m_textBuffer;
            m_bitmapTextData->m_textBuffer = NULL;
        }

        delete m_bitmapTextData;
        m_bitmapTextData = NULL;
    }
#endif
}

//===========================================================================
// FeSprite::AddImage
//===========================================================================
// Description: Adds an image as a child of the sprite
//
// Parameters:    imageResourceID - the id of the image in the resource manager
//
//===========================================================================
void FeSprite::AddImage( unsigned int imageResourceID )
{
    //FeResourceEntry* pResourceEntry = new FeResourceEntry( "", imageResourceID );
    //AddChild( pResourceEntry );
}

//===========================================================================
// FeSprite::AddImage
//===========================================================================
// Description: Add an image alias to the sprite.  This alias will refer to
//              the alias name in the resource manager
//
// Constraints:    Should only be used by the ResourceManager
//
// Parameters:    alias - the symbolic name of the resource in the resource manager
//
//===========================================================================
void FeSprite::AddImage( const char* alias )
{
    PascalCString* newAlias = new PascalCString( alias );
    mAliases.Insert( newAlias );
}

//===========================================================================
// FeSprite::SetImage
//===========================================================================
// Description: Override an image alias to the sprite. This alias will refer to
//              the alias name in the resource manager.
//
// Constraints:   
//
// Parameters:    alias - the symbolic name of the resource in the resource manager
//
//===========================================================================
void FeSprite::SetImage( int index, const char* alias )
{
    rAssert( index >= 0 && index < mAliases.Size() );

    // TC: *** this is will free and re-allocate memory *** BAD!
    //
    *mAliases[ index ] = alias;
}

//===========================================================================
// FeSprite::Display
//===========================================================================
// Description: Draws the sprite
//
//===========================================================================
void FeSprite::Display()
{
#ifdef BITMAP_TEXT_SUPPORT
    if( m_bitmapTextData != NULL )
    {
        this->DisplayBitmapText();
    }
    else
#endif
    {
        if( !mSprite )
        {
            SetIndex( 0 );
        }

        if( mSprite )
        {
            mSprite->SetColour( GetColour() );
            mSprite->Display();
#ifdef RAD_DEBUG
            if( FeApp::GetInstance()->IsShowingBoundingBoxes() )
            {
                mSprite->DisplayBoundingBox();
                DisplayBoundingBox();
            }
#endif
        }
    }
}

void FeSprite::CopyImagesFrom( const FeSprite& source )
{
    for( int i = 0; i < source.mAliases.Size(); ++i )
        AddImage( static_cast<const char*>( *source.mAliases[i] ) );
    if( source.mIndex >= 0 && source.mIndex < mAliases.Size() )
        SetIndex( source.mIndex );
}

void FeSprite::ReplaceImagesFrom( const FeSprite& source )
{
    if( &source==this ) return;
    while( mAliases.Size() )
    {
        const int i=mAliases.Size()-1;
        delete mAliases[i];
        mAliases.Erase(i);
    }
    mIndex=-1;
    mSprite=NULL;
    CopyImagesFrom(source);
}

//===========================================================================
// FeSprite::GetIndex
//===========================================================================
// Description: Returns the index of the sprite to be displayed if the sprite
//              is a multi-sprite
//
// Return:      the index of the currently displaying sprite
//
//===========================================================================
int FeSprite::GetIndex()
{
    return mIndex;
}

//===========================================================================
// FeSprite::GetNumOfImages
//===========================================================================
// Description: Returns the number of images in the sprite
//
// Return:      The number of child images
//
//===========================================================================
int FeSprite::GetNumOfImages()
{
    return mAliases.Size();
}

//===========================================================================
// FeSprite::Reset
//===========================================================================
// Description: Sets all of the parameters for the drawable back to their
//              original settings
//
//===========================================================================
void FeSprite::Reset()
{
    FeBoundedDrawable::Reset();
    mIndex = 0;
} 


//===========================================================================
// ResizeToBoundingBox
//===========================================================================
// Description: forces the sprite to be scaled to the size of the bounding
//              box when it's rendered
//
// Constraints:    NONE
//
// Parameters:    NONE
//
// Return:      NONE
//
//===========================================================================
void FeSprite::ResizeToBoundingBox()
{
    if( mSprite != NULL )
    {
        float spriteWidth = static_cast< float >( mSprite->GetWidth() );
        float spriteHeight = static_cast< float >( mSprite->GetHeight() );
        float sizeX = m_width / spriteWidth;
        float sizeY = m_height / spriteHeight;
        this->ScaleAboutCenter( sizeX, sizeY, 1.0f );
    }
}

//===========================================================================
// SetIndex
//===========================================================================
// Description: changes which of the images in the multisprite gets drawn
//
// Constraints:    NONE
//
// Parameters:    n - the index of the image to set
//
// Return:      NONE
//
//===========================================================================
void FeSprite::SetIndex( int n )
{
    if( n < mAliases.Size() )
    {
        //no need to redisplay if nothing has changed
        if( (mIndex == n) && mSprite )
        {
        }
        else
        {
            mIndex = n;
            m_isDirty = true;
            mSprite = dynamic_cast<tSprite*>(FeApp::GetInstance()->GetFeResourceManager().GetResource( *mAliases[mIndex] ));
            m_drawable = mSprite;
        }
    }
    else
    {
        rAssertMsg( false, "FeSprite::SetIndex - index was too high" );
    }
}

//===========================================================================
// SetRawSprite
//===========================================================================
// Description: sets the sprite pointer to the passed in sprite
//
// Constraints: the FeSprite does not keep a reference to this other sprite 
//              object. You need to de-allocate it yourself
//
// Parameters:  sprite - pointer to the actual sprite
//
// Return:      NONE
//
//===========================================================================
void FeSprite::SetRawSprite( tSprite* sprite, bool updateDrawable )
{
    mSprite = sprite != NULL ?
              sprite :
              dynamic_cast<tSprite*>( FeApp::GetInstance()->GetFeResourceManager().GetResource( *mAliases[mIndex] ) );

    rAssert( mSprite != NULL );
    m_isDirty = true;
/*
    int height = mSprite->GetHeight();
    int width = mSprite->GetWidth();
    SetBoundingBoxSize( width, height );
*/
    if( updateDrawable )
    {
        m_drawable = mSprite;
    }
}


#ifdef BITMAP_TEXT_SUPPORT

void
FeSprite::DisplayBitmapText()
{
    p3d::stack->Push(); // save current matrix

    rAssert( m_bitmapTextData != NULL );

    int textBufferLength = p3d::UnicodeStrLen( m_bitmapTextData->m_textBuffer );

    for( int currentLine = 0;
         m_bitmapTextData->m_newlineIndex[ currentLine ] < static_cast<short>( textBufferLength );
         currentLine++ )
    {
        p3d::stack->Push();

        switch( m_horizontalJustification )
        {
            case Scrooby::Centre:
            {
                float centerOffset = this->ConvertToScreenX( m_width / 4 ) - m_bitmapTextData->m_textWidth[ currentLine ] / 2;
                p3d::stack->Translate( centerOffset, 0.0f, 0.0f );

                // follow-thru
                //
            }
            case Scrooby::Left:
            {
                for( short i = m_bitmapTextData->m_newlineIndex[ currentLine ];
                     i < m_bitmapTextData->m_newlineIndex[ currentLine + 1 ] && i < m_bitmapTextData->m_textBufferSize;
                     i++ )
                {
                    UnicodeChar currentChar = m_bitmapTextData->m_textBuffer[ i ];
                    if( currentChar == '\0' )
                    {
                        break;
                    }

                    if( currentChar == BITMAP_TEXT_LINEBREAK )
                    {
                        continue;
                    }

                    rAssertMsg( (currentChar & 0xFF00) == 0, "Only ASCII text is currently supported!" );

                    rAssert( currentChar < FE_CHAR_MAP_SIZE );
                    this->SetIndex( static_cast<int>( FE_CHAR_MAP[ currentChar ] ) );

                    this->DisplayBitmapChar( currentChar );

                    float translateX = ConvertToScreenX( mSprite->GetWidth() + m_bitmapTextData->m_textSpacing );
                    p3d::stack->Translate( translateX, 0.0f, 0.0f );
                }

                break;
            }
            case Scrooby::Right:
            {
                p3d::stack->Translate( ConvertToScreenX( m_width ), 0.0f, 0.0f );

                for( short i = m_bitmapTextData->m_newlineIndex[ currentLine + 1 ] - 1;
                     i >= m_bitmapTextData->m_newlineIndex[ currentLine ];
                     i-- )
                {
                    UnicodeChar currentChar = m_bitmapTextData->m_textBuffer[ i ];
                    rAssert( currentChar != '\0' );

                    if( currentChar == BITMAP_TEXT_LINEBREAK )
                    {
                        continue;
                    }

                    rAssertMsg( (currentChar & 0xFF00) == 0, "Only ASCII text is currently supported!" );

                    rAssert( currentChar < FE_CHAR_MAP_SIZE );
                    this->SetIndex( static_cast<int>( FE_CHAR_MAP[ currentChar ] ) );

                    float translateX = ConvertToScreenX( mSprite->GetWidth() + m_bitmapTextData->m_textSpacing );
                    p3d::stack->Translate( -translateX, 0.0f, 0.0f );

                    this->DisplayBitmapChar( currentChar );
                }

                break;
            }
            default:
            {
                rAssertMsg( false, "Invalid horizontal justification!" );

                break;
            }
        }

        p3d::stack->Pop();

        p3d::stack->Translate( 0.0f, this->ConvertToScreenY( -(m_height + m_bitmapTextData->m_lineSpacing) ) , 0.0f );
    }


    p3d::stack->Pop(); // restore original matrix
}

void
FeSprite::DisplayBitmapChar( UnicodeChar character )
{
    rAssert( mSprite != NULL );
    mSprite->SetColour( GetColour() );
    mSprite->Display();
}

void
FeSprite::SetSpriteMode( Scrooby::eSpriteMode mode )
{
    if( mode == Scrooby::SPRITE_BITMAP_TEXT )
    {
        if( m_bitmapTextData == NULL )
        {
            m_bitmapTextData = new BitmapTextData;
            rAssert( m_bitmapTextData != NULL );

            m_bitmapTextData->m_textBuffer = NULL;
            m_bitmapTextData->m_textBufferSize = 0;
            m_bitmapTextData->m_textSpacing = DEFAULT_BITMAP_TEXT_SPACING;
            m_bitmapTextData->m_lineSpacing = 0;

            for( int i = 0; i < BitmapTextData::MAX_NUM_LINES; i++ )
            {
                m_bitmapTextData->m_newlineIndex[ i ] = 0;
                m_bitmapTextData->m_textWidth[ i ] = 0.0f;
            }
        }
    }
}

void
FeSprite::CreateBitmapTextBuffer( unsigned int size )
{
    rAssert( m_bitmapTextData != NULL );

    if( m_bitmapTextData->m_textBuffer != NULL )
    {
        // free up memory from old text buffer
        //
        delete [] m_bitmapTextData->m_textBuffer;
        m_bitmapTextData->m_textBuffer = NULL;
    }

    rAssert( (size & 0xFFFF0000) == 0 );
    m_bitmapTextData->m_textBufferSize = static_cast<short>( size );

    // allocate memory for new text buffer
    //
    rAssert( m_bitmapTextData->m_textBufferSize > 0 );
    m_bitmapTextData->m_textBuffer = new UnicodeChar[ m_bitmapTextData->m_textBufferSize ];
    rAssert( m_bitmapTextData->m_textBuffer != NULL );
    m_bitmapTextData->m_textBuffer[ 0 ] = '\0';
}

void
FeSprite::SetBitmapText( const UnicodeChar* text )
{
    rAssert( m_bitmapTextData != NULL );
    rAssert( m_bitmapTextData->m_textBuffer != NULL );
    p3d::UnicodeStrCpy( text, static_cast<P3D_UNICODE*>( m_bitmapTextData->m_textBuffer ),
                        static_cast<int>( m_bitmapTextData->m_textBufferSize ) );

    this->SetupTextWrapping();
}

void
FeSprite::SetBitmapText( const char* text )
{
    rAssert( m_bitmapTextData != NULL );
    rAssert( m_bitmapTextData->m_textBuffer != NULL );
    p3d::AsciiToUnicode( text, static_cast<P3D_UNICODE*>( m_bitmapTextData->m_textBuffer ),
                         static_cast<int>( m_bitmapTextData->m_textBufferSize ) );

    this->SetupTextWrapping();
}

void
FeSprite::SetBitmapTextSpacing( short numPixels )
{
    rAssert( m_bitmapTextData != NULL );
    m_bitmapTextData->m_textSpacing = numPixels;
}

void
FeSprite::SetBitmapTextLineSpacing( short numPixels )
{
    rAssert( m_bitmapTextData != NULL );
    m_bitmapTextData->m_lineSpacing = numPixels;
}

void FeSprite::GetBoundingBox( int& xMin, int& yMin,
                               int& xMax, int& yMax ) const
{
    Parent::GetBoundingBox( xMin, yMin, xMax, yMax );
#ifdef BITMAP_TEXT_SUPPORT
    if( m_bitmapTextData == NULL || m_bitmapTextData->m_textBuffer == NULL )
        return;

    float widest = 0.0f;
    int lines = 0;
    for( int line = 0; line < BitmapTextData::MAX_NUM_LINES; ++line )
    {
        if( m_bitmapTextData->m_newlineIndex[ line ] >=
            m_bitmapTextData->m_textBufferSize ) break;
        widest = std::max( widest, m_bitmapTextData->m_textWidth[ line ] );
        ++lines;
        if( m_bitmapTextData->m_newlineIndex[ line + 1 ] >=
            m_bitmapTextData->m_textBufferSize ||
            m_bitmapTextData->m_textBuffer[
                m_bitmapTextData->m_newlineIndex[ line + 1 ] ] == '\0' ) break;
    }
    if( lines == 0 ) return;

    const int textWidth = static_cast<int>( widest *
        FeApp::GetInstance()->GetScreenWidth() + 0.5f );
    const int glyphHeight = mSprite != NULL ? mSprite->GetHeight() : m_height;
    const int lineAdvance = m_height + m_bitmapTextData->m_lineSpacing;
    const int originalY = yMin;
    if( m_horizontalJustification == Scrooby::Right )
        xMin = xMax - textWidth;
    else if( m_horizontalJustification == Scrooby::Centre )
    {
        // DisplayBitmapText uses ConvertToScreenX(m_width / 4) before
        // subtracting half the measured line width. Mirror that legacy
        // coordinate convention exactly; a conventional m_width / 2 centre
        // shifts the crop right and removes the left side of centred text.
        const int centre = xMin + m_width / 4;
        xMin = centre - textWidth / 2;
        xMax = xMin + textWidth;
    }
    else
        xMax = xMin + textWidth;
    // Subsequent bitmap lines are translated in negative screen Y.
    yMin = originalY - ( lines - 1 ) * lineAdvance;
    yMax = originalY + glyphHeight;
#endif
}

float
FeSprite::ConvertToScreenX( int pixels )
{
    // convert to screen space in x-direction
    //
    return( pixels / FeApp::GetInstance()->GetScreenWidth() );
}

float
FeSprite::ConvertToScreenY( int pixels )
{
    // convert to screen space in y-direction
    //
    return( pixels / FeApp::GetInstance()->GetScreenWidth() );
}

void
FeSprite::SetupTextWrapping()
{
    rAssert( m_bitmapTextData != NULL );

    float textWidth = 0.0f; // in screen space
    int currentLine = 0;
    int newLineIndex = 0;

    for( short i = 0; i < m_bitmapTextData->m_textBufferSize; i++ )
    {
        UnicodeChar currentChar = m_bitmapTextData->m_textBuffer[ i ];
        if( currentChar == '\0' )
        {
            rAssert( currentLine < BitmapTextData::MAX_NUM_LINES );
            m_bitmapTextData->m_newlineIndex[ currentLine ] = newLineIndex;
            m_bitmapTextData->m_textWidth[ currentLine ] = textWidth;

            // termination
            //
            m_bitmapTextData->m_newlineIndex[ currentLine + 1 ] = i;

            break;
        }

        rAssertMsg( (currentChar & 0xFF00) == 0, "Only ASCII text is currently supported!" );

        if( currentChar == BITMAP_TEXT_LINEBREAK )
        {
            rAssert( currentLine < BitmapTextData::MAX_NUM_LINES );
            m_bitmapTextData->m_newlineIndex[ currentLine ] = newLineIndex;
            m_bitmapTextData->m_textWidth[ currentLine ] = textWidth;
            currentLine++;

            newLineIndex = i + 1;
            textWidth = 0.0f;

            continue;
        }

        rAssert( currentChar < FE_CHAR_MAP_SIZE );
        int index = static_cast<int>( FE_CHAR_MAP[ currentChar ] );
        if( index < mAliases.Size() )
        {
            this->SetIndex( index );
        }
        else
        {
            rAssertMsg( false, "Character not found in multi-sprite!" );
        }

        if( i > 0 )
        {
            textWidth += ConvertToScreenX( m_bitmapTextData->m_textSpacing );
        }

        if( mSprite != NULL )
        {
            textWidth += ConvertToScreenX( mSprite->GetWidth() );
        }
    }
}
#endif

const tSprite* FeSprite::GetRawSprite()
{
    return mSprite;
}

#ifdef DEBUGWATCH
void FeSprite::WatchAll( const char* nameSpace )
{
    Parent::WatchAll( nameSpace );
    WatchIndex( nameSpace );
}

void FeSprite::WatchIndex( const char* nameSpace )
{
    const char* drawableName = GetName();
    char finishedNamespace[ 1024 ];
    sprintf( finishedNamespace, "%s\\%s\\", nameSpace, drawableName );
    ::radDbgWatchDelete( &mIndex);
    ::radDbgWatchAddInt( &mIndex, "Index", finishedNamespace, NULL, NULL, false );
}
#endif


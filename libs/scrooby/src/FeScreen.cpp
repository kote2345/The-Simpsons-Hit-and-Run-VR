//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   Scrooby Runtime Public Interface
//
// Description: This contains the Drawable Base class.
//
// Authors:     Wilkin Ng, Ian Gipson, Mike Perzel
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================
#include <p3d/matrixstack.hpp>
#include <p3d/utility.hpp>
#include "FeScreen.h"
#if defined(RAD_ANDROID)
#include <vr/openxrmanager.h>
#endif
#include "FeApp.h"
#include "FeProject.h"
#include "FePage.h"
#include "FeLoadTags.h"
#include "ResourceManager/FeResourceManager.h"
#include "Page.h"
#include "tLinearTable.h"
#include "xml/XMLTree.h"
#include <raddebug.hpp>

//===========================================================================
// Global Data, Local Data, Local Classes
//===========================================================================

#ifdef SCREEN_SCALING_HACK
    bool g_screenScalingEnabled = false;

    float g_screenScaleX = 1.0f; // 1.07f;
    float g_screenScaleY = 1.0f; // 0.91f;
    float g_screenScaleZ = 1.0f;
#endif

//===========================================================================
// Member Functions
//===========================================================================

//===========================================================================
// FeScreen::FeScreen
//===========================================================================
// Description: constructor
//
// Constraints:    NONE
//
// Parameters:    fullFileName - the filename + path to this screen
//              project - the project to which this screen will belong
//
// Return:      NONE
//
//===========================================================================
FeScreen::FeScreen( const char* fullFileName, FeProject* project ) 
:

    FeOwner( fullFileName ),
    mProject( project ),
    m_screenScale( 1.0f )
{
    //rValid( project );

    //
    // Store the whole file name, e.g. "Screen1.feScr"
    //
    mFileName = fullFileName;

    //
    // Remove the file extension for the entity name, e.g. "Screen1"
    //
    radMemoryAllocator old = ::radMemorySetCurrentAllocator( RADMEMORY_ALLOC_TEMP );
        PascalCString pathParser( fullFileName );
        PascalCString filename = pathParser.FullFilename();
    ::radMemorySetCurrentAllocator( old );
    FeEntity::SetName( filename );
    mDivide = "FeDivide.pag";
}


FeScreen::~FeScreen()
{
    int i = GetChildrenCount();
    while( i > 0 )
    {
        FeEntity* child = GetChildIndex( i - 1 );
         RemoveChild( child );
        FeApp::GetInstance()->GetFeResourceManager().RemoveResource( child );
        //child->Release();
        i = GetChildrenCount();
    }

}

//===========================================================================
// GetBoundingBox
//===========================================================================
// Description: gets the bounding box for this screen
//
// Constraints:    NONE
//
// Parameters:    xMin - this is where the function stores the return values
//              yMin
//              xMax
//              yMax
//
// Return:      NONE
//
//===========================================================================
void FeScreen::GetBoundingBox( int& xMin, int& yMin, int& xMax, int& yMax ) const
{
    //IAN IMPROVE: this is identical to the FePage::GetBoundingBox function
    xMin = 0;
    yMin = 0;
    xMax = static_cast<int>(FeApp::GetInstance()->GetScreenWidth()) - 1;
    yMax = static_cast<int>(FeApp::GetInstance()->GetScreenHeight()) - 1;
}

//===========================================================================
// GetBoundingBoxSice
//===========================================================================
// Description: gets the size of the bounding box for this screen
//
// Constraints:    NONE
//
// Parameters:    width  - this is where the function stores its results
//              height
//
// Return:      NONE
//
//===========================================================================
void FeScreen::GetBoundingBoxSize( int& width, int& height ) const
{
    width = static_cast<int>(FeApp::GetInstance()->GetScreenWidth());
    height = static_cast<int>(FeApp::GetInstance()->GetScreenHeight());
}


const char* FeScreen::GetFileName()
{
    return( static_cast<const char*>( mFileName ) );
}

FePage* FeScreen::AddPage( const char* name )
{
    FePage* s = mProject->AddPage( name );
    s->SetParent( this );
    AddChild( s );
     
    return( s );
}

void FeScreen::AddChild( FeEntity* s )
{
	FeParent::AddChild( s );
}

//===========================================================================
// Display()
//===========================================================================
// Description: overloads the display function for screens so that it does 
//              nothing
//
// Constraints:    NONE
//
// Parameters:    NONE
//
// Return:      NONE
//
//===========================================================================
void FeScreen::Display()
{
#if defined(RAD_ANDROID)
    const bool gameplayHud=SharOpenXR::IsGameplayHudScreen(
        static_cast<Scrooby::Screen*>(this));
    if(gameplayHud && SharOpenXR::IsRightEyeRendering()) return;
    // Spatial HUD elements own independent render targets and world-space
    // placement. Capturing the complete screen here prevents FeGroup from
    // entering its radar/mission captures (nested offscreen targets are
    // deliberately rejected) and reduces the VR HUD to one flat texture.
    // Keep the full-screen path available only for a non-spatial fallback.
    const bool gameplayHudCaptured=gameplayHud &&
        SharOpenXR::BeginGameplayHudCapture();
#endif
    p3d::pddi->PushState( PDDI_STATE_RENDER );
    p3d::pddi->PushState( PDDI_STATE_VIEW );
    p3d::pddi->EnableZBuffer( false );

    float aspect = static_cast<float>( FeApp::GetInstance()->GetScreenWidth() ) /
                   static_cast<float>( FeApp::GetInstance()->GetScreenHeight() );

    p3d::pddi->SetCamera( 0.1f, 10.0f, 1.5708f, aspect ); // 1.5707 = PI /2
    p3d::pddi->SetProjectionMode( PDDI_PROJECTION_PERSPECTIVE );
    p3d::pddi->SetCullMode( PDDI_CULL_NONE );
    //p3d::pddi->SetAlphaRef( 1 / 127.0f );
    p3d::stack->Push();
    p3d::stack->LoadIdentity();

// MIKE IMPROVE : This will make the display look good, but this is ridiculous
#ifdef SCREEN_SCALING_HACK
    if( g_screenScalingEnabled )
    {
        p3d::stack->Scale( g_screenScaleX, g_screenScaleY, g_screenScaleZ );
    }
#endif

    if( m_screenScale != 1.0f )
    {
        p3d::stack->Scale( m_screenScale, m_screenScale, 1.0f );
    }

    p3d::stack->Translate( -0.5f, -0.5f / aspect, 0.5f );

    // update all screen objects
#if defined(RAD_ANDROID)
    // The multiview world is followed by one conventional GUI render per
    // eye. Simulation must advance once per XR frame, not once per eye.
    // Updating action-prompt animations twice was the dominant render-thread
    // cost whenever a contextual car/door prompt became visible.
    if(!SharOpenXR::IsRightEyeRendering())
        FeOwner::Update( FeApp::GetInstance()->GetDeltaTime() );
#else
    FeOwner::Update( FeApp::GetInstance()->GetDeltaTime() );
#endif

    // render all screen objects
    FeOwner::Display();

    p3d::stack->Pop();
    p3d::pddi->PopState( PDDI_STATE_RENDER );
    p3d::pddi->PopState( PDDI_STATE_VIEW );

#if defined(RAD_ANDROID)
    if(gameplayHudCaptured) SharOpenXR::EndGameplayHudCapture();
#endif

}

void FeScreen::DisplayBackground()
{
#if defined(RAD_ANDROID)
    const bool gameplayHud=SharOpenXR::IsGameplayHudScreen(
        static_cast<Scrooby::Screen*>(this));
    if(gameplayHud && SharOpenXR::IsRightEyeRendering()) return;
    const bool gameplayHudCaptured=gameplayHud &&
        SharOpenXR::BeginGameplayHudCapture();
#endif
    p3d::pddi->PushState( PDDI_STATE_RENDER );
    p3d::pddi->PushState( PDDI_STATE_VIEW );
    p3d::pddi->EnableZBuffer( false );

    float aspect = static_cast<float>( FeApp::GetInstance()->GetScreenWidth() ) /
                   static_cast<float>( FeApp::GetInstance()->GetScreenHeight() );

    p3d::pddi->SetCamera( 0.1f, 10.0f, 1.5708f, aspect ); // 1.5707 = PI /2
    p3d::pddi->SetProjectionMode( PDDI_PROJECTION_PERSPECTIVE );
    p3d::pddi->SetCullMode( PDDI_CULL_NONE );
    //p3d::pddi->SetAlphaRef( 1 / 127.0f );
    p3d::stack->Push();
    p3d::stack->LoadIdentity();

// MIKE IMPROVE : This will make the display look good, but this is ridiculous
#ifdef SCREEN_SCALING_HACK
    if( g_screenScalingEnabled )
    {
        p3d::stack->Scale( g_screenScaleX, g_screenScaleY, g_screenScaleZ );
    }
#endif

    if( m_screenScale != 1.0f )
    {
        p3d::stack->Scale( m_screenScale, m_screenScale, 1.0f );
    }

    // update all screen objects
    FeOwner::Update( FeApp::GetInstance()->GetDeltaTime() );

    p3d::stack->Translate( -0.5f, -0.5f / aspect, 0.5f );
    bool done = false;
    for( int i = 0; (i < GetChildrenCount()) && !done; i++ )
    {
        FeEntity* feEntity = this->GetChildIndex( i );
        rAssert( feEntity );

        if( feEntity->IsDrawable() )
        {
            FeDrawable* drawable = static_cast< FeDrawable* >( feEntity );
            rAssert( drawable );

            drawable->Display();
            if( mDivide.EqualsInsensitive( drawable->GetName() ) )
            {
                done = true;
            }
        }
    }
    p3d::stack->Pop();
    p3d::pddi->PopState( PDDI_STATE_RENDER );
    p3d::pddi->PopState( PDDI_STATE_VIEW );

#if defined(RAD_ANDROID)
    if(gameplayHudCaptured) SharOpenXR::EndGameplayHudCapture();
#endif
}
void FeScreen::DisplayForeground()
{
#if defined(RAD_ANDROID)
    const bool gameplayHud=SharOpenXR::IsGameplayHudScreen(
        static_cast<Scrooby::Screen*>(this));
    if(gameplayHud && SharOpenXR::IsRightEyeRendering()) return;
    const bool gameplayHudCaptured=gameplayHud &&
        SharOpenXR::BeginGameplayHudCapture();
#endif
    p3d::pddi->PushState( PDDI_STATE_RENDER );
    p3d::pddi->PushState( PDDI_STATE_VIEW );
    p3d::pddi->EnableZBuffer( false );

    float aspect = static_cast<float>( FeApp::GetInstance()->GetScreenWidth() ) /
                   static_cast<float>( FeApp::GetInstance()->GetScreenHeight() );

    p3d::pddi->SetCamera( 0.1f, 10.0f, 1.5708f, aspect ); // 1.5707 = PI /2
    p3d::pddi->SetProjectionMode( PDDI_PROJECTION_PERSPECTIVE );
    p3d::pddi->SetCullMode( PDDI_CULL_NONE );
    //p3d::pddi->SetAlphaRef( 1 / 127.0f );
    p3d::stack->Push();
    p3d::stack->LoadIdentity();

// MIKE IMPROVE : This will make the display look good, but this is ridiculous
#ifdef SCREEN_SCALING_HACK
    if( g_screenScalingEnabled )
    {
        p3d::stack->Scale( g_screenScaleX, g_screenScaleY, g_screenScaleZ );
    }
#endif

    if( m_screenScale != 1.0f )
    {
        p3d::stack->Scale( m_screenScale, m_screenScale, 1.0f );
    }

    p3d::stack->Translate( -0.5f, -0.5f / aspect, 0.5f );
    bool draw = false;
    for( int i = 0; i < GetChildrenCount(); i++ )
    {
        FeEntity* feEntity = this->GetChildIndex( i );
        rAssert( feEntity );

        if( feEntity->IsDrawable() )
        {
            FeDrawable* drawable = static_cast< FeDrawable* >( feEntity );
            rAssert( drawable );

            if( draw )
            {
                drawable->Display();
            }
            else if( mDivide.EqualsInsensitive( drawable->GetName() ) )
            {
                draw = true;
            }
        }
    }
    p3d::stack->Pop();
    p3d::pddi->PopState( PDDI_STATE_RENDER );
    p3d::pddi->PopState( PDDI_STATE_VIEW );

#if defined(RAD_ANDROID)
    if(gameplayHudCaptured) SharOpenXR::EndGameplayHudCapture();
#endif

}

//===========================================================================
// GetProject()
//===========================================================================
// Description: returns the project associated with this screen
//
// Constraints:    NONE
//
// Parameters:    NONE
//
// Return:      FeProject* pointer to this project
//
//===========================================================================
FeProject* FeScreen::GetProject()
{
    return( mProject );
}

//===========================================================================
// FePage* FeScreen::GetPage( const char* name )
//===========================================================================
// Description: returns a pointer to the child page of a particular screen
//
// Constraints:    NONE
//
// Parameters:    char* name - the name of the page to return
//
// Return:      a pointer to the page or null if nothing found
//
//===========================================================================
Scrooby::Page* FeScreen::GetPage( const char* name )
{
    char s[32];  // TODO: magic number
    sprintf (s, "%s%s", name, ".pag");
    tUID uid = FeEntity::MakeUID( s );
    return( GetPage( uid ) );
}

//===========================================================================
// Scrooby::Page* FeScreen::GetPage( const tUID hashValue )
//===========================================================================
// Description: get a page by hashValue
//
// Constraints:    NONE
//
// Parameters:    char* name - the name of the page to return
//
// Return:      a pointer to the page or null if nothing found
//
//===========================================================================
Scrooby::Page* FeScreen::GetPage( const tUID hashValue )
{
    return dynamic_cast< Scrooby::Page* >( GetChild( hashValue ) );
}

//===========================================================================
// virtual Scrooby::Page* GetPageByIndex( const int index );
//===========================================================================
// Description: get a page by index
//
// Constraints:    NONE
//
// Parameters:    
//
// Return:      a pointer to the page or null if nothing found
//
//===========================================================================
Scrooby::Page* FeScreen::GetPageByIndex( const int index )
{
    return dynamic_cast< Scrooby::Page* >( GetChildIndex( index ) );
}

//get the number of pages used by this screen
//===========================================================================
// virtual Scrooby::Page* GetPageByIndex( const int index );
//===========================================================================
// Description: get a page by index
//
// Constraints:    NONE
//
// Parameters:    
//
// Return:      a pointer to the page or null if nothing found
//
//===========================================================================
int FeScreen::GetNumberOfPages() const
{
    return this->GetChildrenCount();
}

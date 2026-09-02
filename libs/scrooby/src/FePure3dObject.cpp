//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   Scrooby Runtime Component
//
// Description: this object coordinates the Pure3dObject widget
//
// Authors:     Wilkin Ng, Ian Gipson, Mike Perzel
//
// Revisions        Date            Author    Revision
//                    2000/03/29        WNG        Created
//                    2000/12/22        IJG        Revised Big Time
//                  2001/01/07      MP      Altered for new loading/rendering methods
//
//===========================================================================
#include <p3d/anim/multicontroller.hpp>
#include <p3d/camera.hpp>
#include <p3d/drawable.hpp>
#include <p3d/matrixstack.hpp>
#include <p3d/utility.hpp>
#include <p3d/view.hpp>
#include <p3d/scenegraph/scenegraph.hpp>
#include <p3d/light.hpp>
#include "utility/debugMessages.h"
#include "FeApp.h"
#include "FePure3dObject.h"
#include "FeResourceEntry.h"
#include "FeScreen.h"
#include <radmath/radmath.hpp>
#if defined(RAD_ANDROID)
#include <SDL.h>
#endif
#include "ResourceManager/FeResourceManager.h"
#if defined(RAD_ANDROID)
#include <vr/openxrmanager.h>
#include <gameflow/gameflow.h>
static Scrooby::Pure3dObject* gVrIrisPure3dObject=NULL;
static Scrooby::Pure3dObject* gVrFrontendWorldPure3dObject=NULL;
static FePure3dObject* gVrRadarMapPure3dObject=NULL;
static FePure3dObject* gVrRadarHolePure3dObject=NULL;
void ScroobySetVrIrisPure3dObject(Scrooby::Pure3dObject* object)
{
    gVrIrisPure3dObject=object;
}
void ScroobySetVrFrontendWorldPure3dObject(Scrooby::Pure3dObject* object)
{
    gVrFrontendWorldPure3dObject=object;
}
void ScroobySetVrRadarPure3dObjects(Scrooby::Pure3dObject* map,
                                    Scrooby::Pure3dObject* hole)
{
    gVrRadarMapPure3dObject=dynamic_cast<FePure3dObject*>(map);
    gVrRadarHolePure3dObject=dynamic_cast<FePure3dObject*>(hole);
    SDL_Log("OpenXR: radar Pure3D registered map=%p impl=%p hole=%p impl=%p",
            map,gVrRadarMapPure3dObject,hole,gVrRadarHolePure3dObject);
}
void ScroobyDisplayVrRadarMap()
{
#if defined(SRR2_VR_RENDERER_VULKAN)
    static bool logged=false;
    if(!logged)
    {
        SDL_Log("OpenXR: forced radar Map0 state impl=%p visible=%d",
                gVrRadarMapPure3dObject,
                gVrRadarMapPure3dObject&&gVrRadarMapPure3dObject->IsVisible()?1:0);
        logged=true;
    }
    if(gVrRadarMapPure3dObject)
        gVrRadarMapPure3dObject->Display();
#endif
}
#endif

// TC [HACK]: Override camera settings.
//
//#define OVERRIDE_CAMERA_SETTINGS

namespace Scrooby
{
    float g_CameraNearPlane = 1.0f;
    float g_CameraFarPlane = 1000.0f;
}

//===========================================================================
// FePure3dObject::FePure3dObject
//===========================================================================
// Description: Constructor.
//
// Parameters:    name - the name assinged to this object in the builder
//              x - x position (left)
//              y - y position (bottom)
//
//===========================================================================
FePure3dObject::FePure3dObject
( 
    const tName& name,
    int x,
    int y
)
:   FeBoundedDrawable( name ),
    m_View( NULL ),
    m_Camera( NULL ),
    m_MultiController( NULL ),
    m_RuntimeDrawable( NULL ),
    m_DefaultLight( NULL ),
    m_numLightsAdded( 0 ),
    m_alias( NULL ),
    m_CameraNearPlane( 1.0f ),
    m_CameraFarPlane( 1000.0f ),
    m_drawableTranslateX( 0.0f ),
    m_drawableTranslateY( 0.0f ),
    m_drawableTranslateZ( 0.0f ),
    m_drawableScale( 1.0f ),
    m_alreadyRendered( false ),
    m_colourWriteEnabled( true ),
    m_clearDepthBufferEnabled( false ),
    m_zbufferEnabled( true ),
    m_isWideScreenCorrectionEnabled( false )
{
    NormalizeCoord( x, y, mPosX, mPosY );
/*
    // un-do aspect ratio factor in y-coord
    float width = FeApp::GetInstance()->GetScreenWidth();
    float height = FeApp::GetInstance()->GetScreenHeight();
    mPosY *= width / height;
*/
    Reset();
    m_alias = new PascalCString();
}



//===========================================================================
// FePure3dObject::~FePure3dObject
//===========================================================================
// Description: Destructor.
//
//===========================================================================
FePure3dObject::~FePure3dObject()
{
#if defined(RAD_ANDROID)
    if(gVrIrisPure3dObject==static_cast<Scrooby::Pure3dObject*>(this))
        gVrIrisPure3dObject=NULL;
#endif
    // remove all lights that were added to view
    //
    for( unsigned int i = 0; i < m_numLightsAdded; i++ )
    {
        rAssert( m_View != NULL );
        m_View->RemoveLight( m_lightsAdded[ i ] );
    }

    if( m_DefaultLight != NULL )
    {
        m_DefaultLight->Release();
        m_DefaultLight = NULL;
    }

    if( m_View != NULL )
    {
        m_View->Release();
        m_View = NULL;
    }

    if( m_Camera != NULL )
    {
        m_Camera->Release();
        m_Camera = NULL;
    }

    if( m_MultiController != NULL )
    {
        m_MultiController->Release();
        m_MultiController = NULL;
    }

    if( m_RuntimeDrawable != NULL )
    {
        m_RuntimeDrawable->Release();
        m_RuntimeDrawable = NULL;
    }

    delete m_alias;
}


//===========================================================================
// FePure3dObject::Reset
//===========================================================================
// Description: resets the object to where it started, with default colors, 
//              etc
//
//===========================================================================
void FePure3dObject::Reset()
{
    FeBoundedDrawable::Reset();
    SetAlpha( 1.0f );
    tColour t( 255, 255, 255 );
    SetColour( t );
    mViewTranslateX = mViewTranslateY = 0.0f;
}

//===========================================================================
// FePure3dObject::SetupCamera
//===========================================================================
// Description: Sets up the camera internally - handles reference counting
//
//===========================================================================
void FePure3dObject::SetupCamera()
{
    tCamera* oldCamera = m_Camera;
    m_Camera = GetCamera();
    if( m_Camera != NULL )
    {
        m_Camera->AddRef();
    }
    if( oldCamera != NULL )
    {
        oldCamera->Release();
    }
}

//===========================================================================
// FePure3dObject::SetupMultiController
//===========================================================================
// Description: Sets up the multicontroller internally - handles reference 
//              counting
//
//===========================================================================
void FePure3dObject::SetupMultiController()
{
    tMultiController* oldController = m_MultiController;
    m_MultiController = GetMultiController();
    if( m_MultiController != NULL )
    {
        m_MultiController->AddRef();
    }
    if( oldController != NULL )
    {
        oldController->Release();
    }
}


//===========================================================================
// FePure3dObject::Update
//===========================================================================
// Description: Renders the pure3d object to the screen
//
//===========================================================================
void FePure3dObject::Update( float elapsedTime )
{
    AdvanceAnimation( elapsedTime );
}


//===========================================================================
// FePure3dObject::Display
//===========================================================================
// Description: Renders the pure3d object to the screen
//
//===========================================================================
void FePure3dObject::Display()
{
#if defined(RAD_ANDROID) && defined(SRR2_VR_RENDERER_VULKAN)
    if(this==gVrRadarHolePure3dObject)
        return;
    if(this==gVrRadarMapPure3dObject)
    {
        // Slot 13 captures HudMap0 only as the original 2D radar overlay.
        // Map0 owns its independent circular texture underneath that overlay.
        if(SharOpenXR::IsMissionHudCaptureActive()) return;
        if(SharOpenXR::IsRightEyeRendering()) return;
        // When the complete authored gameplay HUD owns the active offscreen
        // target, Map0 must render into that target directly. Attempting a
        // nested radar capture fails by design; the old code then returned
        // without drawing any 3D roads.
        if(SharOpenXR::IsGameplayHudCaptureActive())
        {
            // The independently captured radar is composited through the
            // frame opening after the authored HUD page.  Do not leave an
            // unmasked square copy in the full-page texture.
            return;
        }
        if(SharOpenXR::IsRadarRendering())
        {
            Render();
            return;
        }
        int x0=0,y0=0,x1=0,y1=0;GetBoundingBox(x0,y0,x1,y1);
        const bool captured=SharOpenXR::BeginRadarCapture(x0,y0,x1,y1);
        if(!captured) return;
        Render();
        SharOpenXR::EndRadarCapture();
        return;
    }
#endif
    Render();
}

//===========================================================================
// FePure3dObject::Add3dModel
//===========================================================================
// Description: Sets the resource entry for the pure3d object
//
// Constraints:   NONE
//
// Parameters:    resourceID - the index of the resource in the ResourceManager
//
//===========================================================================
void FePure3dObject::Add3dModel( const char* alias )
{
    *m_alias = alias;
}

//===========================================================================
// FePure3dObject::SetCameraZ
//===========================================================================
// Description: allows you to set the camera that the P3D object uses when
//              rendering
//
// Parameters:    camera - tPointCamera* to the camera that we're using
//
//===========================================================================
void FePure3dObject::SetCamera( tCamera* camera )
{
    if( m_Camera )
    {
        m_Camera->Release();
    }
    m_Camera = camera;
    if( m_Camera )
    {
        m_Camera->AddRef();

        m_CameraNearPlane = m_Camera->GetNearPlane();
        m_CameraFarPlane = m_Camera->GetFarPlane();
    }
}

//===========================================================================
// FePure3dObject::GetCamera
//===========================================================================
// Description: get the pointer to the camera we're using to draw this thing
//
// Return:      the pointer to the camera
//
//===========================================================================
tCamera* FePure3dObject::GetCamera()
{
    return m_Camera;
}

//===========================================================================
// FePure3dObject::GetMultiController
//===========================================================================
// Description: get the pointer to the underlying MultiController
//
// Return:      the pointer to the underlying MultiController
//
//===========================================================================
tMultiController* FePure3dObject::GetMultiController()
{
    return m_MultiController;
}

//===========================================================================
// FePure3dObject::AdvanceAnimation
//===========================================================================
// Description: Sends a time to the animation controller to allow it to advance
//
// Parameters: deltaTime - time elapsed
//
//===========================================================================
void FePure3dObject::AdvanceAnimation( float deltaTime )
{
    if( m_MultiController != NULL )
    {
        m_MultiController->Advance( deltaTime );
    }
}

//===========================================================================
// FePure3dObject::Render
//===========================================================================
// Description: Render the 3d object to the screen in our own view
//
//===========================================================================
void FePure3dObject::Render()
{
    // get the current view window
    //
    tView* currentView = p3d::context->GetView();
    rAssert( currentView );

    // get the original camera
    //
    tCamera* originalCamera = currentView->GetCamera();
    originalCamera->AddRef();

    if( !m_alreadyRendered )
    {
        m_alreadyRendered = true;

        // save reference to the rendering view
        //
        if( m_View == NULL )
        {
            m_View = currentView;
            m_View->AddRef();
        }

        // Try and set up the drawable, camera, and multi-controller
        //
        int index = FeApp::GetInstance()->GetFeResourceManager().GetIndex( static_cast< const char* >( *m_alias ) );

        tDrawable* drawable = GetDrawable();
        if( drawable == NULL )
        {
            SetDrawable( dynamic_cast<tDrawable*>( FeApp::GetInstance()->GetFeResourceManager().GetResource( index ) ) );
            drawable = GetDrawable();

            tShaderIntBroadcast colourWrite(PDDI_SP_COLOURWRITE, m_colourWriteEnabled ? PDDI_WRITE_ALL : PDDI_WRITE_ALPHA);
            drawable->ProcessShaders(colourWrite);
        }

        rAssert( drawable != NULL );
        sg::Scenegraph* scene = p3d::find<sg::Scenegraph>( drawable->GetUID() );
        if( scene != NULL )
        {
            // add any light groups found under scenegraph root
            //
            sg::LightGroupIterator it( scene );
            for( it.First(); !it.IsDone(); it.Next() )
            {
                tLightGroup* lightGroup = it.GetLightGroup()->GetLights();
                if( lightGroup != NULL )
                {
                    // add each light in light group to current view
                    //
                    for( int i = 0; i < lightGroup->GetNumLights(); i++ )
                    {
                        rAssert( m_numLightsAdded < MAX_NUM_LIGHTS );
                        m_lightsAdded[ m_numLightsAdded ] = m_View->AddLight( lightGroup->GetLight( i ) );
                        m_numLightsAdded++;
                    }
                }
            }
        }

        if( GetCamera() == NULL )
        {
            tCamera* camera = FeApp::GetInstance()->GetFeResourceManager().GetCamera( index );
            if( camera != NULL )
            {
                SetCamera( camera );
            }
        }

        if( GetMultiController() == NULL )
        {
            tMultiController* multiController = FeApp::GetInstance()->GetFeResourceManager().GetMultiController( index );
            if( multiController != NULL )
            {
                SetMultiController( multiController );
            }
        }

#if defined(RAD_ANDROID)
        // The main-menu world controller is safe to seek only here, after the
        // resource manager has supplied its camera, drawable and tracks. The
        // intro window can observe the shared controller earlier than this.
        if( SharOpenXR::IsVrModeEnabled() &&
            static_cast<Scrooby::Pure3dObject*>(this) ==
                gVrFrontendWorldPure3dObject &&
            m_Camera != NULL && m_MultiController != NULL )
        {
            m_MultiController->SetFrameRange( 721.0f, 770.0f );
            m_MultiController->Reset();
            m_MultiController->SetFrame( 770.0f );
        }
#endif

        // add default light, if exists
        //
        if( m_DefaultLight != NULL )
        {
            rAssert( m_numLightsAdded < MAX_NUM_LIGHTS );
            m_lightsAdded[ m_numLightsAdded ] = m_View->AddLight( m_DefaultLight );
            m_numLightsAdded++;
        }
    }

    if( m_RuntimeDrawable == NULL )
    {
#if defined(RAD_ANDROID) && defined(SRR2_VR_RENDERER_VULKAN)
        if(this==gVrRadarMapPure3dObject)
            SDL_Log("OpenXR: Map0 has no runtime drawable alias=%s alreadyRendered=%d",
                    static_cast<const char*>(*m_alias),m_alreadyRendered?1:0);
#endif
        // nothing to draw, return
        //
        return;
    }

#if defined(RAD_ANDROID)
    // Frontend Scrooby pages mix the authored 3D living-room scene with the
    // 2D menu in one draw tree. Render this Pure3D node through the real eye
    // cameras, then restore the world-locked panel projection for subsequent
    // text, sprites and buttons. Pause/HUD Pure3D widgets retain their existing
    // paths because only CONTEXT_FRONTEND uses this split.
    const bool vrFrontendWorld =
        GetGameFlow()->GetCurrentContext() == CONTEXT_FRONTEND &&
        SharOpenXR::IsFrontendPlaneRendering();
    if( vrFrontendWorld )
    {
        SharOpenXR::SetFrontendPlaneRendering( false );
    }
#endif

    // clear the depth buffer, if enabled
    //
    if( m_clearDepthBufferEnabled )
    {
        p3d::pddi->Clear( PDDI_BUFFER_DEPTH );
    }

    // push the view state
    //
    p3d::pddi->PushState( PDDI_STATE_VIEW );

    // set colour write
    //
    bool alphaWriteEnabled = true;
#if defined(RAD_ANDROID)
    // Hole0 is the depth-only circular mask for the spatial radar. The legacy
    // path always writes alpha even when colour writes are disabled, producing
    // opaque black corners in a transparent offscreen texture. Keep every
    // colour channel untouched while the mask writes depth.
    if( SharOpenXR::IsRadarRendering() && !m_colourWriteEnabled )
        alphaWriteEnabled = false;
#endif
    p3d::pddi->SetColourWrite( m_colourWriteEnabled,
                               m_colourWriteEnabled,
                               m_colourWriteEnabled,
                               alphaWriteEnabled );
#if defined(RAD_ANDROID)
    const bool suppressVrIris=
        gVrIrisPure3dObject==static_cast<Scrooby::Pure3dObject*>(this);
    // In Original mode the authored Hole0 alpha mask is only needed to cut
    // the minimap to the circular bezel. Vulkan's depth-only rendition can
    // resolve that textured mask as a solid plane and consequently reject
    // every road fragment submitted by Map0. The ordinary radar bezel already
    // covers the edge, so keep Hole0 visual-only here and retain the authored
    // depth mask for the spatial offscreen capture.
    const bool suppressOriginalRadarHole=
        m_isWideScreenCorrectionEnabled && !m_colourWriteEnabled &&
        !SharOpenXR::IsSpatialHudEnabled();
    const bool oldZWrite=p3d::pddi->GetZWrite();
    if(suppressVrIris || suppressOriginalRadarHole)
    {
        // Keep Display and its animation lifecycle active, but emit neither
        // colour nor an invisible depth mask into the VR eyes.
        p3d::pddi->SetColourWrite(false,false,false,false);
        p3d::pddi->SetZWrite(false);
    }
#endif

    // save z-buffer setting
    //
    bool oldZBufferEnabled = p3d::pddi->IsZBufferEnabled();
    p3d::pddi->EnableZBuffer( m_zbufferEnabled );

#ifdef RAD_XBOX
    // save cull mode setting
    //
    pddiCullMode oldCullMode = p3d::pddi->GetCullMode();
    p3d::pddi->SetCullMode( PDDI_CULL_NORMAL );
#endif

    p3d::stack->Push();

    // get the window, and the scissor region right
    float width;
    float height;
    FeDrawable::NormalizeCoord( m_width, m_height, width, height );

    // Aspect gets doublely assigned to the height, so we'll just make it proper
    height = (float)m_height / FeApp::GetInstance()->GetScreenHeight();

    // TC: Aspect also gets doubly assigned to the y-coord,
    //     so we'll just undo one of them
    float posY = mPosY * FeApp::GetInstance()->GetScreenWidth() /
                         FeApp::GetInstance()->GetScreenHeight();

    float posX = mPosX;

    // TC: Hack!! (for SRR2 HUD map)
    //
    static const float WIDE_SCREEN_CORRECTION_SCALE = (4.0f / 3.0f) * (9.0f / 16.0f);
    if( m_isWideScreenCorrectionEnabled && p3d::display->IsWidescreen() )
    {
        width *= WIDE_SCREEN_CORRECTION_SCALE;
        posX = (posX - 0.5f) * WIDE_SCREEN_CORRECTION_SCALE + 0.5f;
    }

    float l = posX + mViewTranslateX;
    float r = l + width;
    float b = 1.0f - (posY - mViewTranslateY);
    float t = b - height;  //Bottom and top are in reversed P3D coordinates

    // put the new camera on the stack
    
    p3d::stack->LoadIdentity();
    currentView->SetCamera( m_Camera );

#if defined(RAD_ANDROID)
    // Match the GLES path in both HUD modes. Map0/Hole0 use wide-screen
    // correction as their embedded-map marker: it prevents the map camera
    // from being replaced by the OpenXR eye projection and converts their
    // sub-viewport through the same HUD transform as the surrounding radar.
    const bool vrEmbeddedHud = m_isWideScreenCorrectionEnabled ||
        this==gVrRadarMapPure3dObject || this==gVrRadarHolePure3dObject;
    if( vrEmbeddedHud )
    {
        SharOpenXR::SetEmbeddedHudRendering( true );
    }
    rmt::Matrix originalVrCamera;
    bool vrCameraChanged = false;
    // HUD map Pure3D widgets use this flag.  Their camera is a map-space
    // camera and must stay inside the 2D clipped map rectangle, not inherit
    // the HMD eye pose.  Other embedded 3D menu objects remain stereoscopic.
    if( m_Camera && !m_isWideScreenCorrectionEnabled &&
        this != gVrRadarMapPure3dObject &&
        this != gVrRadarHolePure3dObject )
    {
        originalVrCamera = m_Camera->GetCameraToWorldMatrix();
        rmt::Matrix eyeCamera;
        if( SharOpenXR::GetActiveEyeCamera( m_Camera, &eyeCamera ) )
        {
            SharOpenXR::SetWorldRendering( true );
            m_Camera->SetCameraMatrix( &eyeCamera );
            vrCameraChanged = true;
        }
    }
#endif

    if( m_Camera )
    {
//        m_Camera->SetNearPlane( m_CameraNearPlane );
//        m_Camera->SetFarPlane( m_CameraFarPlane );

#ifdef OVERRIDE_CAMERA_SETTINGS
        m_Camera->SetNearPlane( Scrooby::g_CameraNearPlane );
        m_Camera->SetFarPlane( Scrooby::g_CameraFarPlane );
#endif
        m_Camera->SetState();
        p3d::context->LoadViewMatrix( m_Camera->GetWorldToCameraMatrix(), m_Camera->GetCameraToWorldMatrix() );
    }

    p3d::pddi->SetViewWindow(l, t, r, b);

    // TC [BUG FIX]: need to set 'm_rect' data first with FeDrawable::SetClip
    //               before calling pddi->SetScissor
    //
    FeDrawable::SetClip( l, b, r, t );
    p3d::pddi->SetScissor( &m_rect );

    // get the transform matrix
    // we dont want FeBoundedDrawable::GetMatrix() which have the extra offset
    // since can't justify 3d object

    // MIKE IMPROVE : is this Always just the identity?
    p3d::stack->Multiply( *FeDrawable::GetMatrix() );

    // apply any drawable transformations
    //
    p3d::stack->Scale( m_drawableScale );
    p3d::stack->Translate( m_drawableTranslateX, m_drawableTranslateY, m_drawableTranslateZ );

    // draw the damn thing!
    //
    rAssert( m_RuntimeDrawable != NULL );
#if defined(RAD_ANDROID) && defined(SRR2_VR_RENDERER_VULKAN)
    static bool loggedMapDrawable=false;
    if(this==gVrRadarMapPure3dObject && !loggedMapDrawable)
    {
        SDL_Log("OpenXR: Map0 runtime drawable display alias=%s drawable=%p embedded=%d",
                static_cast<const char*>(*m_alias),m_RuntimeDrawable,
                SharOpenXR::IsEmbeddedHudRendering()?1:0);
        loggedMapDrawable=true;
    }
#endif
    m_RuntimeDrawable->Display();

    // restore everything we changed
    //
#if defined(RAD_ANDROID)
    if( vrCameraChanged )
    {
        m_Camera->SetCameraMatrix( &originalVrCamera );
        SharOpenXR::SetWorldRendering( false );
    }
    if( vrEmbeddedHud )
    {
        SharOpenXR::SetEmbeddedHudRendering( false );
    }
#endif
    originalCamera->SetState();
    currentView->SetCamera( originalCamera );
    originalCamera->Release();

    p3d::stack->Pop();

#ifdef RAD_XBOX
	// restore cull mode
    //
    p3d::pddi->SetCullMode( oldCullMode );
#endif

    // restore z-buffer test
    //
    p3d::pddi->EnableZBuffer( oldZBufferEnabled );

    // restore colour write
    //
#if defined(RAD_ANDROID)
    if(suppressVrIris || suppressOriginalRadarHole)
        p3d::pddi->SetZWrite(oldZWrite);
    if(vrFrontendWorld)
    {
        SharOpenXR::SetFrontendPlaneRendering( true );
    }
#endif
    p3d::pddi->SetColourWrite( true, true, true, true );

    // pop the view state
    //
    p3d::pddi->PopState( PDDI_STATE_VIEW );
}

//===========================================================================
// FePure3dObject::GetMatrix
//===========================================================================
// Description: returns the transformation matrix.  Overrides
//              FeBoundedDrawable::GetMatrix, because we don't need the overhead
//
// Return:      pointer to the transformation matrix
//
//===========================================================================
rmt::Matrix* FePure3dObject::GetMatrix()
{
    // Do we need to recalculate the view
    return &m_matrix; // not used since we render in our own view
}

//===========================================================================
// FePure3dObject::GetMatrix
//===========================================================================
// Description: returns the transformation matrix.  Overrides
//              FeBoundedDrawable::GetMatrix, because we don't need the overhead
//
// Return:      pointer to the transformation matrix
//
//===========================================================================
const rmt::Matrix* FePure3dObject::GetMatrix() const
{
    return &m_matrix;
}


//===========================================================================
// FePure3dObject::RecalcView
//===========================================================================
// Description: recalc view position and aspect ratio, reposition camera
//
//===========================================================================
void FePure3dObject::RecalcView()
{
    //alter the clip rectangle to reflect the new l/r/t/b of the bounding box
    float width;
    float height;
    FeDrawable::NormalizeCoord( m_width, m_height, width, height );
    FeDrawable::SetClip( mPosX, mPosY, mPosX + width, mPosY + height );
}

//===========================================================================
// FePure3dObject::Translate
//===========================================================================
// Description: Overrides FeDrawable::Translate so we don't modify the matrix
//              stack, just the view
//
// Parameters:    x,y - integral amounts to translate by (in screen coordinates)
//
//===========================================================================
void FePure3dObject::Translate( int x, int y )
{
    float transX, transY;
    FeDrawable::NormalizeCoord( x, y, transX, transY );

    mViewTranslateX += transX;
    mViewTranslateY += transY;
}

//===========================================================================
// FePure3dObject::SetTranslation
//===========================================================================
// Description: Overrides FeDrawable::SetTranslation so we don't modify the matrix
//              stack, just the view
//
// Parameters:    x,y - new integral translation amounts (in screen coordinates)
//
//===========================================================================
void FePure3dObject::SetTranslation( int x, int y )
{
    float transX, transY;
    FeDrawable::NormalizeCoord( x, y, transX, transY );
    mViewTranslateX = transX;
    mViewTranslateY = transY;
}

//===========================================================================
// FePure3dObject::ResetTransformation
//===========================================================================
// Description: Since the other translation methods are overridde, this one
//              must be too in order to compensate for them
//
//===========================================================================
void FePure3dObject::ResetTransformation()
{
    FeBoundedDrawable::ResetTransformation();
    mViewTranslateX = mViewTranslateY = 0.0f;
    
    m_isDirty = true;
}

//===========================================================================
// FePure3dObject::GetDrawable
//===========================================================================
// Description: Returns the current drawable object
//
// Returns:        pointer to tDrawable object
//
//===========================================================================
tDrawable* FePure3dObject::GetDrawable()
{
    return m_RuntimeDrawable;
}

//===========================================================================
// FePure3dObject::SetDrawable
//===========================================================================
// Description: Sets the new pure3d drawable this object will display
//
// Parameters:    drawable - pointer to tDrawable object
//
//===========================================================================
void FePure3dObject::SetDrawable( tDrawable* drawable )
{
    if( m_RuntimeDrawable )
    {
        m_RuntimeDrawable->Release();
    }
    m_RuntimeDrawable = drawable;
    if( m_RuntimeDrawable )
    {
        m_RuntimeDrawable->AddRef();
    }
    m_drawable = m_RuntimeDrawable;
}

//===========================================================================
// FePure3dObject::SetMultiController
//===========================================================================
// Description: Sets the new pure3d multi-controller for the pure3d object
//
// Parameters:    controller - pointer to tMultiController object
//
//===========================================================================
void FePure3dObject::SetMultiController( tMultiController* controller )
{
    if( m_MultiController )
    {
        m_MultiController->Release();
    }
    m_MultiController = controller;
    if( m_MultiController )
    {
        m_MultiController->AddRef();
        m_MultiController->Advance( 0.0f );
    }
}

//===========================================================================
// FePure3dObject::SetDefaultLight
//===========================================================================
// Description: Add a default light to the p3d object's rendering view
//
// Parameters:  pointer to a tLight object
//
//===========================================================================
void FePure3dObject::SetDefaultLight( tLight* light )
{
    if( m_DefaultLight != NULL )
    {
        m_DefaultLight->Release();
        m_DefaultLight = NULL;
    }

    if( light != NULL )
    {
        m_DefaultLight = light;
        m_DefaultLight->AddRef();
    }
}


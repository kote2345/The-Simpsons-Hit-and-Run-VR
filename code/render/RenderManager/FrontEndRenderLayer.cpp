//========================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        FrontEndRenderLayer.cpp
//
// Description: Implementation for FrontEndRenderLayer class.
//
// History:     + Initial Implementation -- Tony [6/05/2002]
//
//========================================================================

//========================================
// System Includes
//========================================
#include <raddebug.hpp>
#include <p3d/view.hpp>
#include <p3d/billboardobject.hpp>
#include <p3d/utility.hpp>

//========================================
// Project Includes
//========================================
#include <render/RenderManager/FrontEndRenderLayer.h>
#include <debug/profiler.h>
#include <presentation/gui/guisystem.h>
#include <presentation/gui/guimanager.h>
#include <presentation/gui/guiwindow.h>
#include <presentation/presentation.h>
#include <gameflow/gameflow.h>
#include <contexts/contextenum.h>
#include <contexts/gameplay/gameplaycontext.h>
#include <worldsim/coins/coinmanager.h>
#include <worldsim/coins/sparkle.h>
#ifdef RAD_WIN32
#include <input/inputmanager.h>
#endif
#ifdef RAD_ANDROID
#include <presentation/fmvplayer/fmvplayer.h>
#include <input/touch/touchhudrenderer.h>
#include <vr/openxrmanager.h>
#endif


#if defined(RAD_ANDROID)
  #include <android/log.h>
#include "RenderManager.h"

#define LOG_TAG "SimpsonsHitAndRun"
  #define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
  #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#elif defined(RAD_VITA)
  #include <psp2/kernel/clib.h>
  #define LOGI(...) do { sceClibPrintf(__VA_ARGS__); sceClibPrintf("\n"); } while(0)
  #define LOGE(...) do { sceClibPrintf(__VA_ARGS__); sceClibPrintf("\n"); } while(0)

#else
  #include <cstdio>
  #define LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); std::fflush(stdout); } while(0)
  #define LOGE(...) do { std::printf(__VA_ARGS__); std::printf("\n"); std::fflush(stdout); } while(0)
#endif
//************************************************************************
//
// Global Data, Local Data, Local Classes
//
//************************************************************************

//************************************************************************
//
// Public Member Functions : Context Interface
//
//************************************************************************

//========================================================================
// FrontEndFrontEndRenderLayer::FrontEndRenderLayer
//========================================================================
//
// Description: Inits state and variables to represent Dead State
//
// Parameters:	 None
//
// Return:      None.
//
// Constraints: 
//
//========================================================================
FrontEndRenderLayer::FrontEndRenderLayer()
:   RenderLayer(),
    mpScroobyApp( NULL )
{
#ifdef DEBUGWATCH
    radDbgWatchAddUnsignedInt(&mDebugRenderTime, "Render Time", "Front End Render Layer" );
#endif
}

//========================================================================
// FrontEndFrontEndRenderLayer::~FrontEndRenderLayer
//========================================================================
//
// Description: Cleans state and variables to represent Dead State
//
// Parameters:	 None
//
// Return:      None.
//
// Constraints: 
//
//========================================================================
FrontEndRenderLayer::~FrontEndRenderLayer()
{
#ifdef DEBUGWATCH
    radDbgWatchDelete(&mDebugRenderTime);
#endif
}

void FrontEndRenderLayer::DrawCoinObject()
{
#if defined(RAD_ANDROID)
    // Coins flying into the counter are rendered separately from Scrooby but
    // belong on the same converged head-locked UI plane.
    SharOpenXR::SetWorldRendering( false );
    SharOpenXR::SetEnhancedUiConvergence( true );
#endif
    // Render HUD coin effects.
    const ContextEnum coinContext=GetGameFlow()->GetCurrentContext();
    if((coinContext == CONTEXT_GAMEPLAY || coinContext == CONTEXT_PAUSE) &&
	   (coinContext == CONTEXT_PAUSE || !GetPresentationManager()->IsBusy()))
	{
#if defined(RAD_ANDROID)
        // Never submit the shared world coin drawable through a legacy
        // offscreen pass. Its GLES material selects a non-multiview program
        // and then makes every world coin disappear. The spatial counter has
        // an independent texture; this call only retains flying HUD coins.
        // Vulkan uses the rebuilt HUD in both Original and VR gameplay modes.
        // The genuine animated coin model therefore has to be captured in
        // either mode; the legacy spatial-HUD preference must not gate it.
#if defined(SRR2_VR_RENDERER_VULKAN)
        // This is now a resource update for the rebuilt renderer, not the
        // legacy spatial-HUD layout. Keep the original 3D coin animated in
        // both gameplay modes.
        const bool spatialCoinHud=true;
#else
        const bool spatialCoinHud=SharOpenXR::IsSpatialHudEnabled();
#endif
        if(spatialCoinHud)
            SharOpenXR::CaptureSpatialCoinIcon();
        else
            GetCoinManager()->HUDRender();
#else
        GetCoinManager()->HUDRender();
#endif
#if defined(RAD_ANDROID)
        // Flying-to-counter coins and their sparkles use legacy screen
        // coordinates and become a head-locked duplicate in VR.
        if(!spatialCoinHud) GetSparkleManager()->HUDRender();
#else
        GetSparkleManager()->HUDRender();
#endif
        //??? GetHitnRunManager()->HUDRender();
    }
	else
	{
        GetCoinManager()->ClearHUDCoins();
	}
#if defined(RAD_ANDROID)
    SharOpenXR::SetEnhancedUiConvergence( false );
#endif

}
//************************************************************************
// Render Interface
//************************************************************************
//========================================================================
// FrontEndFrontEndRenderLayer::Render
//========================================================================
//
// Description: Renders all (TODO:visible/DSG) drawables
//
// Parameters:	 None
//
// Return:      None.
//
// Constraints: 
//
//========================================================================

// FUNCION ORIGINAL DA PROBLEMAS EN ANDROID 64 bits por un problema de orden de render de capas, la capa gui se pinta por encima tapando el video 
/*
void FrontEndRenderLayer::Render()
{
    BEGIN_PROFILE( "FE Render" );

#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds();
#endif

    for( unsigned int view = 0; view < mNumViews; view++ )
    {
        mpView[ view ]->BeginRender();

        rAssert(!IsDead());

        HeapMgr()->PushHeap( GMA_TEMP );

        if (!GetCoinManager()->DrawAfterGui())
            DrawCoinObject();
        // display Scrooby screen (and updates all Pure3d objects)
        //
        mpScroobyApp->DrawFrame( static_cast<float>( g_scroobySimulationTime ) );
#ifdef RAD_PC
        // Update the frontend cursor.
        GetInputManager()->GetFEMouse()->Update();
#endif

        if (GetCoinManager()->DrawAfterGui())
            DrawCoinObject();

        HeapMgr()->PopHeap ( GMA_TEMP );

//        GetBillboardQuadManager()->DisplayAll();

        mpView[ view ]->EndRender();
    }

#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds()-mDebugRenderTime;
#endif

    END_PROFILE( "FE Render" );
}

*/




// ESTA FUNCION ES LA DEFINITIVA(6/03/2026 19:00)  (Esta es la que actualmente funciona las cinematicas in game con parpadeo )

/*
void FrontEndRenderLayer::Render()
{
    BEGIN_PROFILE( "FE Render" );

#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds();
#endif

    bool fmvPlaying = false;
    PresentationManager* pm = GetPresentationManager();
    if( pm && pm->GetFMVPlayer() )
    {
#if defined(RAD_ANDROID)
        // The Android decoder is serviced independently at the OpenXR frame
        // rate. AnimationPlayer::IsPlaying() can be false while its decoder
        // is actively presenting, which allowed the legacy head-locked GUI
        // mask to be drawn over the world-locked movie plane.
        fmvPlaying = pm->GetFMVPlayer()->IsDecoderPlaying();
#else
        fmvPlaying = pm->GetFMVPlayer()->IsPlaying();
#endif
    }

    for( unsigned int view = 0; view < mNumViews; view++ )
    {
        rAssert(!IsDead());

        // nuevos cambios

        if( mpView[ view ] == NULL )
        {
            continue;
        }

        mpView[ view ]->BeginRender();

        if( !fmvPlaying )
        {
            HeapMgr()->PushHeap( GMA_TEMP );

            if( !GetCoinManager()->DrawAfterGui() )
                DrawCoinObject();

            if( mpScroobyApp != NULL )
            {
                mpScroobyApp->DrawFrame( static_cast<float>( g_scroobySimulationTime ) );
            }

#ifdef RAD_PC
            GetInputManager()->GetFEMouse()->Update();
#endif

            if( GetCoinManager()->DrawAfterGui() )
                DrawCoinObject();

            HeapMgr()->PopHeap( GMA_TEMP );
        }

        mpView[ view ]->EndRender();

        //Fin nuevos cambios
    }

#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds() - mDebugRenderTime;
#endif

    END_PROFILE( "FE Render" );
}
*/


// PRUEBO ESTA NUEVA VERSION 

void FrontEndRenderLayer::Render()
{
    BEGIN_PROFILE( "FE Render" );

#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds();
#endif

    bool fmvPlaying = false;
    PresentationManager* pm = GetPresentationManager();
    if( pm && pm->GetFMVPlayer() )
    {
#if defined(RAD_ANDROID)
        fmvPlaying = pm->GetFMVPlayer()->IsDecoderPlaying();
#else
        fmvPlaying = pm->GetFMVPlayer()->IsPlaying();
#endif
    }

    if( fmvPlaying )
    {
#ifdef DEBUGWATCH
        mDebugRenderTime = radTimeGetMicroseconds() - mDebugRenderTime;
#endif
        END_PROFILE( "FE Render" );
        return;
    }

#if defined(RAD_ANDROID)
    const ContextEnum guiContext=GetGameFlow()->GetCurrentContext();
    CGuiManager* guiManager=GetGuiSystem()->GetCurrentManager();
    const CGuiWindow::eGuiWindowID screenId=guiManager?
        guiManager->GetCurrentScreen():CGuiWindow::GUI_WINDOW_ID_UNDEFINED;
    static CGuiWindow::eGuiWindowID loggedScreenId=
        CGuiWindow::GUI_WINDOW_ID_UNDEFINED;
    if(screenId!=loggedScreenId)
    {
        LOGI("OpenXR: frontend screen id=%d context=%d",
             static_cast<int>(screenId),static_cast<int>(guiContext));
        loggedScreenId=screenId;
    }
    // Present the complete frontend, including the main menu, on one
    // world-locked VR panel. Keeping it active across screen changes preserves
    // the same anchor instead of making every submenu follow a new camera pose.
    const bool loadingScreen=
        screenId==CGuiWindow::GUI_SCREEN_ID_LOADING ||
        screenId==CGuiWindow::GUI_SCREEN_ID_LOADING_FE;
    const bool pauseScreen=
        screenId==CGuiWindow::GUI_SCREEN_ID_PAUSE_SUNDAY ||
        screenId==CGuiWindow::GUI_SCREEN_ID_PAUSE_MISSION;
    const bool spatialFrontend=guiContext==CONTEXT_BOOTUP ||
        guiContext==CONTEXT_PAUSE ||
        pauseScreen ||
        loadingScreen ||
        guiContext==CONTEXT_FRONTEND;
    SharOpenXR::SetFrontendPlaneActive(spatialFrontend);
    SharOpenXR::SetFrontendPlaneRendering(spatialFrontend);
    SharOpenXR::SetPauseCoinVisible(pauseScreen);
#endif

    for( unsigned int view = 0; view < mNumViews; view++ )
    {
        rAssert( !IsDead() );

        if( mpView[ view ] == NULL )
        {
            continue;
        }

        mpView[ view ]->BeginRender();

        HeapMgr()->PushHeap( GMA_TEMP );

#if defined(RAD_ANDROID)
        // Gameplay is paused, but CoinManager keeps advancing its HUD angle.
        // Refresh the clean cached icon so the final EndEye overlay retains
        // the original rotating pause-menu coin animation.
        if( pauseScreen )
        {
            SharOpenXR::CaptureSpatialCoinIcon();
        }
#endif

        if( !GetCoinManager()->DrawAfterGui() )
            DrawCoinObject();

        if( mpScroobyApp != NULL )
        {
            mpScroobyApp->DrawFrame( static_cast<float>( g_scroobySimulationTime ) );
        }

#ifdef RAD_PC
        GetInputManager()->GetFEMouse()->Update();
#endif

        if( GetCoinManager()->DrawAfterGui() )
            DrawCoinObject();
        
        
        // Touch controls are intentionally not drawn in the standalone VR
        // build; input comes from the OpenXR motion controllers.
        HeapMgr()->PopHeap( GMA_TEMP );

        mpView[ view ]->EndRender();

        
    }

#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds() - mDebugRenderTime;
#endif

    END_PROFILE( "FE Render" );

#if defined(RAD_ANDROID)
    SharOpenXR::SetFrontendPlaneRendering(false);
#endif

}



//************************************************************************
// Resource Interface
//************************************************************************
//////////////////////////////////////////////////////////////////////////
// Guts; Renderable Type Things
//////////////////////////////////////////////////////////////////////////
//========================================================================
// FrontEndRenderLayer::AddGuts
//========================================================================
//
// Description: Add a tDrawable
//
// Parameters:	 tDrawable to add
//
// Return:      None.
//
// Constraints: 
//
//========================================================================
void FrontEndRenderLayer::AddGuts( tDrawable* ipDrawable )
{
   //The Basic FrontEndRenderLayer does not support this type
   rAssert(false);
}

//========================================================================
// FrontEndRenderLayer::AddGuts
//========================================================================
//
// Description: Add a tGeometry
//
// Parameters:	 tGeometry to add
//
// Return:      None.
//
// Constraints: 
//
//========================================================================
void FrontEndRenderLayer::AddGuts( tGeometry* ipGeometry )
{
   //The Basic FrontEndRenderLayer does not support this type
   rAssert(false);
}

//========================================================================
// FrontEndRenderLayer::AddGuts
//========================================================================
//
// Description: Add an IntersectDSG
//
// Parameters:  IntersectDSG to add
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void FrontEndRenderLayer::AddGuts( IntersectDSG* ipIntersectDSG )
{
   //The Basic FrontEndRenderLayer does not support this type
   rAssert(false);
}

//========================================================================
// FrontEndRenderLayer::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void FrontEndRenderLayer::AddGuts( StaticEntityDSG* ipStaticEntityDSG )
{
   //The Basic FrontEndRenderLayer does not support this type
   rAssert(false);
}

//========================================================================
// FrontEndRenderLayer::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void FrontEndRenderLayer::AddGuts( StaticPhysDSG* ipStaticPhysDSG )
{
   //The Basic FrontEndRenderLayer does not support this type
   rAssert(false);
}

//========================================================================
// FrontEndRenderLayer::AddGuts
//========================================================================
//
// Description: Add the Scrooby App reference
//
// Parameters:  Scrooby App to add
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void FrontEndRenderLayer::AddGuts( Scrooby::App* ipScroobyApp )
{
    rAssert( mpScroobyApp == NULL );

    mpScroobyApp = ipScroobyApp;
}

//========================================================================
// FrontEndRenderLayer::SetUpGuts
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void FrontEndRenderLayer::SetUpGuts()
{
    // do nothing
}

//========================================================================
// FrontEndRenderLayer::NullifyGuts
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void FrontEndRenderLayer::NullifyGuts()
{
    mpScroobyApp = NULL;
}

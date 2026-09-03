//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        RenderManager.cpp
//
// Description: Implementation for RenderManager class.
//
// History:     + Stolen and cleaned up from Penthouse -- Darwin Chau
//              + Stolen from Darwin and Tailored 
//                to RenderFlow from GameFlow      -- Devin [4/17/2002]
//              + Stolen from Devin  and Tailored 
//                to RenderManager from RenderFlow -- Devin [4/17/2002]
//
//=============================================================================

//If you want only level 1, do this.
//#define MS8_PANIC

//I got yer synchronous loads right here!
//#define LOAD_SYNC

//========================================
// System Includes
//========================================
#include <constants/chunkids.hpp>
#include <raddebug.hpp>
#include <radtime.hpp>
#include <raddebugwatch.hpp>
 
//========================================
// Project Includes
//========================================
#include <render/RenderManager/RenderLayer.h>
#include <render/Culling/WorldScene.h>
#include <render/RenderManager/RenderManager.h>
#if defined(RAD_ANDROID)
#include <vr/openxrmanager.h>
#include <p3d/camera.hpp>
#endif
#if defined(SRR2_OPENXR_PLATFORM_WIN32)
#include <vr/openxr_desktop_runtime.h>
#endif
#include <render/RenderManager/WorldRenderLayer.h>
#include <render/RenderManager/FrontEndRenderLayer.h>
#include <render/IntersectManager/IntersectManager.h>
#include <render/Loaders/AllWrappers.h>
#include <render/DSG/IntersectDSG.h>
#include <render/DSG/WorldSphereDSG.h>
#include <render/DSG/DSGFactory.h>
#include <render/DSG/LensFlareDSG.h>
#include <render/DSG/animcollisionentitydsg.h>
#include <render/DSG/animentitydsg.h>
#include <render/Enums/RenderEnums.h>
#include <constants/srrchunks.h>
#include <memory/srrmemory.h>

#include <mission/gameplaymanager.h>

#include <worldsim/avatarmanager.h>
#include <worldsim/avatar.h>
#include <worldsim/redbrick/vehicle.h>
#include <worldsim/redbrick/geometryvehicle.h>

#include <worldsim/character/character.h>

#include <debug/profiler.h>
#include <debug/debuginfo.h>

#include <meta/locatorevents.h>
#include <events/eventdata.h>
#include <events/eventenum.h>
#include <events/eventmanager.h>

#include <meta/zoneeventlocator.h>
#include <meta/occlusionlocator.h>
#include <meta/triggervolume.h>

#include <worldsim/character/charactermanager.h>

#include <main/game.h>
#include <main/platform.h>

#include <memory/srrmemory.h>

#include <sound/soundmanager.h>

#include <presentation/presentation.h>
#include <presentation/fmvplayer/fmvplayer.h>
#include <gameflow/gameflow.h>
#if defined(RAD_ANDROID)
#include <presentation/gui/guisystem.h>
#include <presentation/gui/guimanager.h>
#include <presentation/gui/guiwindow.h>
#endif

#include <pddi/pddiext.hpp>
#include <p3d/light.hpp>
#include <p3d/view.hpp>

#ifdef DEBUGWATCH
#include <radmemorymonitor.hpp>
#include <worldsim/worldphysicsmanager.h>
#endif


#if defined(RAD_ANDROID)
#include <android/log.h>
#include <input/touch/touchhudrenderer.h>
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
//******************************************************************************
//
// Global Data, Local Data, Local Classes
//
//******************************************************************************

//
// Static pointer to instance of this singleton.
//
RenderManager* RenderManager::mspInstance = NULL;

#ifndef RAD_RELEASE
char gZoneLoadID[256];
const char* ZONE_STRING = "%s : %d";
void LoadTag( const char* zone, int& index )
{
    char name[ 9 ] = "";
    strncpy( name, &zone[4], strlen( zone ) - 8 );
    name[8] = '\0';
    sprintf( gZoneLoadID, ZONE_STRING, name, index );
    SetMemoryIdentification( gZoneLoadID );

    index++;
}

void FinishedZone()
{
    strcat( gZoneLoadID, " - FINSIHED" );
    SetMemoryIdentification( gZoneLoadID );
}
#endif

//
// Static list of items to load per level, per mission
//
static const char* sLevelLoadList[] = 
{
   "art\\L1_TERRA.p3d", //L1
   "art\\l2_TERRA.p3d", //L2
   "art\\l3_TERRA.p3d", //L3
   "art\\L4_TERRA.p3d", //L4
   "art\\l5_TERRA.p3d", //L5
   "art\\l6_TERRA.p3d", //L6
   "art\\L7_TERRA.p3d", //L7
   "art\\b00.p3d",       //SUPER_SPRINT
   "art\\b01.p3d",       //SUPER_SPRINT
   "art\\b02.p3d",       //SUPER_SPRINT
   "art\\b03.p3d",       //SUPER_SPRINT
   "art\\b04.p3d",       //SUPER_SPRINT
   "art\\b05.p3d",       //SUPER_SPRINT
   "art\\b06.p3d",       //SUPER_SPRINT
   "art\\B07.p3d"       //SUPER_SPRINT
};

static const char* sIntersectLoadList[] = 
{
   "art\\l1_inter.p3d", //L1
   "art\\l2_inter.p3d", //L2
   "art\\l3_inter.p3d", //L3
   "art\\l4_inter.p3d", //L4
   "art\\l5_inter.p3d", //L5
   "art\\l6_inter.p3d", //L6
   "art\\l7_inter.p3d", //L7
   "",
   "",
   "",
   "",
   "",
   "",
   "",
   ""
};

static const char* sMissionLoadList[] = 
{
   "everground.p3d"  //L1M1
};

bool ENABLE_MOTION_BLUR = true;

float BLUR_SCALE = 0.9f;
float BLUR_START = 33.33f;
float MAX_BLUR = 0.15f; // Blur alpha will never go over this level
// Blur gradient is slope in the linear blur equation
// So max blur will be reached at 15 fps, scaled linearly from 30 fps
float BLUR_GRADIENT = MAX_BLUR / ( 66.66f - 33.33f );

// Vlad wants the PS2 to use a minimum amount of fixed blurring all the time
#ifdef RAD_PS2
//float MIN_PS2_BLUR = 0.075f;
float MIN_PS2_BLUR = 0.15f;
float MIN_PS2_BLUR_CHEAT = 0.8f;
#endif

#if defined( RAD_PS2) || defined( RAD_XBOX )
#define USE_BLUR
#endif

//******************************************************************************
//
// Public Member Functions : RenderManager Interface
//
//******************************************************************************

//==============================================================================
// RenderManager::DumpAllLoadedData
//==============================================================================
//
// Description: .
//
// Parameters:	
//
// Return:      None.
//
// Constraints: 
//
//==============================================================================
void RenderManager::DumpAllLoadedData
( 
)
{
   mpRenderLayers[RenderEnums::LevelSlot]->Freeze();
//   mpRenderLayers[RenderEnums::LevelSlot]->NullifyGuts();
   mpRenderLayers[RenderEnums::LevelSlot]->Kill();

   // this can release some objects, need to do it now or 
   // we'll die next time a frame is rendered since heaps have probably been 
   // blown away
   LensFlareDSG::ReadFrameBufferIntensities(); 
   
   //
   // This will cause a lag when dumping level data GC Lot Check Violation
   //
   FlushDelList();
   //
   // Clean Reinit
   //
    // mpRenderLayers[RenderEnums::LevelSlot]->DoPreStaticLoad();
    // mpRenderLayers[RenderEnums::InteriorSlot]->DoPreStaticLoad();
   //
   // Kill the Default Pure3D inventory; 
   // this is where the first level load went
   //
   p3d::inventory->RemoveSectionElements(tName::MakeUID("Default"));
   p3d::inventory->DeleteSection(tName::MakeUID("Default"));

    AllWrappers::GetInstance()->ClearGlobalEntities();
    AllWrappers::GetInstance()->mLoader( AllWrappers::msGeometry      ).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msStaticEntity  ).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msStaticPhys    ).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msTreeDSG       ).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msFenceEntity   ).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msIntersectDSG  ).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msAnimCollEntity).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msAnimEntity    ).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msDynaPhys      ).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msInstStatPhys  ).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msInstStatEntity).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msLocator       ).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msWorldSphere   ).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msRoadSegment   ).ModRegdListener( this, RenderEnums::BogusUserData );        
    AllWrappers::GetInstance()->mLoader( AllWrappers::msPathSegment   ).ModRegdListener( this, RenderEnums::BogusUserData );        
    AllWrappers::GetInstance()->mLoader( AllWrappers::msBillboard     ).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msLensFlare     ).ModRegdListener( this, RenderEnums::BogusUserData );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msAnimDynaPhys  ).ModRegdListener( this, RenderEnums::BogusUserData );         

}

//==============================================================================
// RenderManager::LoadAllNeededData
//==============================================================================
//
// Description: Loads data unique to a Level,Mission tuple.
//
// Parameters:	 Level and Mission specifier
//
// Return:      Bool: was the layer that got wiped out marked as dead?
//
// Constraints: 
//
//==============================================================================
void RenderManager::SetLoadData
(
   RenderEnums::LayerEnum   isLayer,
   RenderEnums::LevelEnum   isLevel, 
   RenderEnums::MissionEnum isMission 
)
{
   //Valid Layer?
   rAssert( isLayer     < RenderEnums::numLayers );
   rAssert( isLevel     < RenderEnums::MAX_LEVEL );
   rAssert( isMission   < RenderEnums::numMissions );

   msLayer     = isLayer;
   msLevel     = isLevel;
   msMission   = isMission;

   mCurWorldLayer = msLayer;
}
//========================================================================
// RenderManager::
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
void RenderManager::RedirectChunks( int ChunkDestinationMask )
{
    AllWrappers::GetInstance()->mLoader( AllWrappers::msStaticEntity  ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::StaticEntityGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msStaticPhys    ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::StaticPhysGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msTreeDSG       ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::TreeDSGGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msFenceEntity   ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::FenceGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msGeometry      ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::IgnoreGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msIntersectDSG  ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::IntersectGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msAnimCollEntity).ModRegdListener( this, ChunkDestinationMask | RenderEnums::AnimCollGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msAnimEntity    ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::AnimGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msDynaPhys      ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::DynaPhysGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msInstStatPhys  ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::StaticPhysGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msInstStatEntity).ModRegdListener( this, ChunkDestinationMask | RenderEnums::StaticEntityGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msLocator       ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::LocatorGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msWorldSphere   ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::WorldSphereGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msRoadSegment   ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::RoadSegmentGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msPathSegment   ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::PathSegmentGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msBillboard     ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::StaticEntityGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msLensFlare     ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::StaticEntityGuts );         
    AllWrappers::GetInstance()->mLoader( AllWrappers::msAnimDynaPhys  ).ModRegdListener( this, ChunkDestinationMask | RenderEnums::DynaPhysGuts );         

}
//========================================================================
// RenderManager::
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
void RenderManager::FlushDelList()
{
    radTime64 start = radTimeGetMicroseconds64();

    while(mEntityDeletionList.mUseSize)
    {
        mEntityDeletionList[0]->Release();
        mEntityDeletionList.Remove(0);
    }

    radTime64 end = radTimeGetMicroseconds64();
    unsigned deleteTime = (unsigned) (end - start);

    rTunePrintf("RenderManager::FlushDelList Delete time: %.3fms\n", deleteTime / 1000.0F);
}
//========================================================================
// RenderManager::
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
void RenderManager::MunchDelList(unsigned us)
{
    // optimización evitamos llamar si no hay entidades pendientes
    if (mEntityDeletionList.mUseSize == 0)
    {
        return;
    }
    radTime64 start = radTimeGetMicroseconds64();

    while(mEntityDeletionList.mUseSize)
    {
        mEntityDeletionList[0]->Release();
        mEntityDeletionList.Remove(0);
        radTime64 elapsed = radTimeGetMicroseconds64() - start;

        if(elapsed > us)
        {
            break; // too spikey, delete some more next frame
        }
    }
}

//========================================================================
// RenderManager::
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
bool RenderManager::LoadAllNeededData
( 
)
{
    mbLoadZonesDumped = false;
    mDoneInitialLoad  = false;

   //MS7
   //static int sFuckinCompilerWontShutUp = isLayer;

   switch( msLayer ) 
   {
   case RenderEnums::LevelSlot:
      if( mpRenderLayers[msLayer]->IsDead() )
      {
#if 0
         int TheEnum = RenderEnums::LevelSlot | RenderEnums::GeometryGuts;
         rDebugPrintf( "Wrapper Init: Layer:%X  GutsID: %X\n", 
                        (TheEnum & RenderEnums::LayerOnlyMask),
                        (TheEnum & RenderEnums::GutsOnlyMask));
#endif
         mpRenderLayers[msLayer]->DoPreStaticLoad();
         HeapMgr()->PushHeap (GMA_LEVEL_ZONE);

         (dynamic_cast<tGeometryLoader*>(&AllWrappers::GetInstance()->mLoader( AllWrappers::msGeometry )))->SetOptimize(true);
         
         RedirectChunks(RenderEnums::LevelSlot);
         AllWrappers::GetInstance()->mLoader( AllWrappers::msWorldSphere ).ModRegdListener( this, RenderEnums::LevelSlot | RenderEnums::GlobalWSphereGuts );         


//////////////////////////////////////////////////////////////////////////
//  SRR2_LOAD_ASYNC
//////////////////////////////////////////////////////////////////////////
        tName LevelName(sLevelLoadList[msLevel]);
        mpRenderLayers[RenderEnums::LevelSlot]->DoPreDynaLoad(LevelName);

        AllWrappers::GetInstance()->mLoader( AllWrappers::msBillboard     ).ModRegdListener( this, RenderEnums::IgnoreGuts );         

#ifndef RAD_RELEASE
        static int loadTag1 = 0;
        LoadTag( sLevelLoadList[msLevel], loadTag1 );
#endif

#ifdef LOAD_SYNC
        HeapMgr()->DumpHeapStats( true );
        GetLoadingManager()->LoadSync( FILEHANDLER_PURE3D, sLevelLoadList[msLevel], GMA_LEVEL_ZONE );
        HeapMgr()->DumpHeapStats( true );
#else
        GetLoadingManager()->AddRequest( FILEHANDLER_PURE3D, sLevelLoadList[msLevel], GMA_LEVEL_ZONE, this, &msLayer);  
#endif

        //////////////////////////////////////////////////////////////////////////
        //intesects rolled into zone files 
        //GetLoadingManager()->AddRequest( FILEHANDLER_PURE3D, sIntersectLoadList[msLevel], GMA_LEVEL_ZONE, sLevelLoadList[msLevel]);  

        msLayer &= ~RenderEnums::AllIntersectLoadingComplete;
        msLayer |= RenderEnums::AllRenderLoadingComplete;
//        GetLoadingManager()->ProcessRequests( this, &msLayer );

        //mbDynaLoading = true;
        mbDynaLoading = false;
        mbDrivingTooFastLoad = false;

        HeapMgr()->PopHeap (GMA_LEVEL_ZONE);

#ifdef LOAD_SYNC
        GetLoadingManager()->AddCallback( this, &msLayer );
#endif

        return true;
      }
   	break;
   default:
      break;
   }
   return false;
}

//==============================================================================
// RenderManager::ContextUpdate
//==============================================================================
//
// Description: Called (responsibly) from managing Context
//
// Parameters:	 None.
//
// Return:      None.
//
// Constraints: 
//
//==============================================================================
// FUNCION ORIGINAL 
/*
void RenderManager::ContextUpdate( unsigned int iElapsedTime )
{
    // On the PS2, use a minimum blur all the time    
#ifdef RAD_PS2
    ApplyPS2Blur();
#endif

    GetIntersectManager()->mbSameFrame = false;
#ifdef DEBUGWATCH
   unsigned int t0 = radTimeGetMicroseconds();
#endif 
    BEGIN_PROFILE( "Rendering" );


    BEGIN_PROFILE( "Swap Buffers" );
    p3d::context->SwapBuffers();
    END_PROFILE( "Swap Buffers" );

#if defined( RAD_XBOX ) || defined ( RAD_GAMECUBE ) || defined( RAD_WIN32 )
    LoadingManager* lm = GetLoadingManager();
    PresentationManager* pm = GetPresentationManager();
    p3d::display->SetForceVSync( lm && !lm->IsLoading(), !(pm && pm->GetFMVPlayer()->IsPlaying()));               
#endif

#ifdef LOAD_SYNC
    FlushDelList(); 
#else
    MunchDelList(2000);   // work on the DelList for up to 2000 microseconds, then return
#endif
    
	BEGIN_PROFILE( "Lens Flare Frame Buffer Read" );
	LensFlareDSG::ReadFrameBufferIntensities();
	END_PROFILE( "Lens Flare Frame Buffer Read" );

#ifdef DEBUGWATCH
    mDebugSwapTime = radTimeGetMicroseconds()-t0;
    t0 = radTimeGetMicroseconds();
#endif 

    if( mMood.mTransition >= 0.0f )
    {
        TransitionMoodLighting( iElapsedTime );
    }

    BEGIN_PROFILE( "Begin Frame" );
    p3d::context->BeginFrame();
    END_PROFILE( "Begin Frame" );

    // Render Stuff; call your Render shots, Tex.
    // FOR original,
    
    for( int i=RenderEnums::numLayers-1; i>-1; i-- )
    {
#if defined(DEBUGINFO_ENABLED) && !defined(RAD_ANDROID)
        // We need to render the debug info just before we render the GUI layer
        //since rendering that layer changes the world matrix.
        if( i == RenderEnums::GUI )
        {
            DEBUGINFO_RENDER();
        }
#endif
        if( mpRenderLayers[i]->IsRenderReady() )
        {    
    BEGIN_PROFILE( "Layers" );
            mpRenderLayers[i]->Render();
    END_PROFILE( "Layers" );
        }
    }



    END_PROFILE( "Rendering" );

#ifdef DEBUGWATCH
    t0 = radTimeGetMicroseconds();
#endif 
#ifndef FINAL
    BEGIN_PROFILE( "Dump Stats" );
    HeapMgr()->DumpHeapStats();
    HeapMgr()->DumpArtStats();
    END_PROFILE( "Dump Stats" );
#endif 

    #if !defined(RAD_ANDROID)
    RENDER_PROFILER();
    #endif

    //MEMTRACK_RENDER();

    //HEAPSTACKS_RENDER();

    #if !defined(RAD_ANDROID)
    SOUNDDEBUG_RENDER();
    #endif

#ifdef USE_BLUR
    ((pddiExtFramebufferEffects*)p3d::pddi->GetExtension( PDDI_EXT_FRAMEBUFFER_EFFECTS ))->EnableMotionBlur( mEnableMotionBlur || ENABLE_MOTION_BLUR, mBlurAlpha, BLUR_SCALE, false );
    ((pddiExtFramebufferEffects*)p3d::pddi->GetExtension( PDDI_EXT_FRAMEBUFFER_EFFECTS ))->SetQuality( pddiExtFramebufferEffects::Smallest );

    if ( mEnableMotionBlur || ENABLE_MOTION_BLUR )
    {
        ((pddiExtFramebufferEffects*)p3d::pddi->GetExtension( PDDI_EXT_FRAMEBUFFER_EFFECTS ))->RenderMotionBlur();
    }
#endif


    if ( CommandLineOptions::Get( CLO_DEMO_TEST ) ||
         GetCheatInputSystem()->IsCheatEnabled( CHEAT_ID_DEMO_TEST ) )
    {
        char buffy[32];
        sprintf( buffy, "Demo Count: %d", GetGame()->GetDemoCount() );

        const int LEFT = 35;
        const int TOP = 45;
        const pddiColour WHITE(128,128,128);

        p3d::pddi->DrawString( buffy, LEFT , 80 + TOP, WHITE );

        static unsigned int time = 0;
        time = GetGame()->GetTime();
        time += iElapsedTime;

        unsigned int hours = time / 3600000;
        unsigned int deltaTime = time % 3600000;

        unsigned int minutes = deltaTime / 60000;
        deltaTime = deltaTime % 60000;

        unsigned int seconds = deltaTime / 1000;
        deltaTime = deltaTime % 1000;

        sprintf( buffy, "Time: %d:%d:%d.%d", hours, minutes, seconds, deltaTime );
        p3d::pddi->DrawString( buffy, LEFT , 100 + TOP, WHITE );

        if ( GetGameplayManager() )
        {
            sprintf( buffy, "Level %d", GetGameplayManager()->GetCurrentLevelIndex() );
            p3d::pddi->DrawString( buffy, LEFT , 120 + TOP, WHITE );
        }

        GetGame()->SetTime( time );
    }
#if defined(RAD_ANDROID) && defined (RAD_DEBUG)
{
	if(!p3d::display || !p3d::pddi){
		// NO HACER NADA. MUCHO OJO ESTO SIN EL IF ELSE DE ARRIBA HA PROVOCADO CRASH EN 32 bits, el codigo de este juego es poco estable me parece
	}else{
    static int frames = 0;
    static unsigned int lastTime = 0;
    static int fps = 0;

    unsigned int now = radTimeGetMilliseconds();

    if (lastTime == 0)
        lastTime = now;

    frames++;

    if (now - lastTime >= 1000)
    {
        fps = frames;
        frames = 0;
        lastTime = now;
    }

    char buff[32];
    sprintf(buff, "FPS: %d", fps);

    const int RIGHT = p3d::display->GetWidth() - 150;
    const int TOP   = 40;

    const pddiColour GREEN(0,255,0);
    const pddiColour BLACK(0,0,0);

// sombra (ligeramente desplazada)
    p3d::pddi->DrawString(buff, RIGHT + 2, TOP + 2, BLACK);

// texto principal
p3d::pddi->DrawString(buff, RIGHT, TOP, GREEN);
}
}
#endif
   p3d::context->EndFrame( false );


#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds()-t0;
    
    if(mDebugDumpAllZones)
    {
        mDebugDumpAllZones = false;
        mpLayer( RenderEnums::LevelSlot )->DumpAllDynaLoads(1, mEntityDeletionList );
        GetWorldPhysicsManager()->FreeAllCollisionAreaIndicies();
        FlushDelList();
        ::radMemoryMonitorSuspend();
    }
#endif 
    //unsigned int time1 = radTimeGetMicroseconds();
    //rReleasePrintf( "Render Loop: %d micro's\n", time1-time0 );
}

*/




void RenderManager::ContextUpdate( unsigned int iElapsedTime )
{
    // On the PS2, use a minimum blur all the time
#ifdef RAD_PS2
    ApplyPS2Blur();
#endif

    GetIntersectManager()->mbSameFrame = false;

#ifdef DEBUGWATCH
    unsigned int t0 = radTimeGetMicroseconds();
#endif

    BEGIN_PROFILE("Rendering");

    BEGIN_PROFILE("Swap Buffers");
#if !defined(RAD_ANDROID)
    p3d::context->SwapBuffers();
#endif
    END_PROFILE("Swap Buffers");

#if defined( RAD_XBOX ) || defined ( RAD_GAMECUBE ) || defined( RAD_WIN32 )
    LoadingManager *lm = GetLoadingManager();
    PresentationManager *pm = GetPresentationManager();
    p3d::display->SetForceVSync(lm && !lm->IsLoading(), !(pm && pm->GetFMVPlayer()->IsPlaying()));
#endif

#ifdef LOAD_SYNC
    FlushDelList();
#else
    MunchDelList(2000);
#endif

    BEGIN_PROFILE("Lens Flare Frame Buffer Read");
    LensFlareDSG::ReadFrameBufferIntensities();
    END_PROFILE("Lens Flare Frame Buffer Read");

#ifdef DEBUGWATCH
    mDebugSwapTime = radTimeGetMicroseconds() - t0;
    t0 = radTimeGetMicroseconds();
#endif

    if (mMood.mTransition >= 0.0f)
    {
        TransitionMoodLighting(iElapsedTime);
    }

    bool xrFrame = false;
#if defined(SRR2_OPENXR_PLATFORM_WIN32)
    SharOpenXR::Desktop::PumpCompositor();
#endif
#if defined(RAD_ANDROID)
    static bool xrInitializationAttempted = false;
    static bool xrAvailable = false;
    if (!xrInitializationAttempted)
    {
        xrInitializationAttempted = true;
        xrAvailable = SharOpenXR::Initialize();
    }
    if (xrAvailable)
    {
        xrFrame = SharOpenXR::BeginFrame();
    }
#endif

    BEGIN_PROFILE("Begin Frame");
    p3d::context->BeginFrame();
    END_PROFILE("Begin Frame");

    #if defined(RAD_ANDROID)
    {
    PresentationManager* pm = GetPresentationManager();

        if (pm != NULL &&
            pm->GetFMVPlayer() != NULL &&
            pm->GetFMVPlayer()->IsPlaying())
        {
            p3d::pddi->SetClearColour(pddiColour(0, 0, 0));
            p3d::pddi->Clear(PDDI_BUFFER_COLOUR | PDDI_BUFFER_DEPTH);
        }
    }
#endif

    unsigned int renderPasses = 1;
#if defined(RAD_ANDROID)
    bool multiviewActive=false;
#endif
#if defined(RAD_ANDROID)
    const ContextEnum currentXrContext=GetGameFlow()->GetCurrentContext();
    // Keep the spatial GUI anchor alive while a loading or pause screen is
    // being rendered. Normal gameplay clears it so opening pause establishes
    // a fresh panel directly in front of the player's current gaze.
    if(currentXrContext!=CONTEXT_BOOTUP &&
       currentXrContext!=CONTEXT_FRONTEND &&
       currentXrContext!=CONTEXT_LOADING_DEMO &&
       currentXrContext!=CONTEXT_LOADING_SUPERSPRINT &&
       currentXrContext!=CONTEXT_LOADING_GAMEPLAY &&
       currentXrContext!=CONTEXT_PAUSE)
    {
        SharOpenXR::SetFrontendPlaneActive(false);
    }
    if (xrFrame && SharOpenXR::GetEyeCount() == 2)
    {
        CGuiManager* currentGuiManager=GetGuiSystem()->GetCurrentManager();
        const CGuiWindow::eGuiWindowID currentGuiScreen=currentGuiManager?
            currentGuiManager->GetCurrentScreen():CGuiWindow::GUI_WINDOW_ID_UNDEFINED;
        const CGuiWindow::eGuiWindowID nextGuiScreen=currentGuiManager?
            currentGuiManager->GetNextScreen():CGuiWindow::GUI_WINDOW_ID_UNDEFINED;
        // The GUI switches to a pause screen one update before GameFlow
        // changes CONTEXT_GAMEPLAY to CONTEXT_PAUSE. Detect that transition
        // here so its first frame is never submitted through the world-only
        // multiview path (which produced a single white flash in the left eye).
        const bool pauseGuiPending=GetGameFlow()->GetNextContext()==CONTEXT_PAUSE ||
            currentGuiScreen==CGuiWindow::GUI_SCREEN_ID_PAUSE_SUNDAY ||
            currentGuiScreen==CGuiWindow::GUI_SCREEN_ID_PAUSE_MISSION ||
            nextGuiScreen==CGuiWindow::GUI_SCREEN_ID_PAUSE_SUNDAY ||
            nextGuiScreen==CGuiWindow::GUI_SCREEN_ID_PAUSE_MISSION;
        PresentationManager* xrPresentation=GetPresentationManager();
        FMVPlayer* xrMoviePlayer=xrPresentation?
            xrPresentation->GetFMVPlayer():NULL;
        const bool movieActive=xrMoviePlayer &&
            (xrMoviePlayer->IsPlaying() || xrMoviePlayer->IsDecoderPlaying());
        // Screen-space frontend/pause/loading layers contain eye-dependent
        // convergence and anchoring that cannot be broadcast with the world
        // draw. Keep those relatively cheap contexts on the proven per-eye
        // path. The movie renderer also uses a conventional single-view GLES
        // shader, so keep an active FMV on the same proven dual-pass path.
        multiviewActive=currentXrContext==CONTEXT_GAMEPLAY && !pauseGuiPending &&
                        !movieActive &&
                        SharOpenXR::BeginMultiview();
        renderPasses=multiviewActive?1:2;
    }
#endif

    for (unsigned int renderPass = 0; renderPass < renderPasses; ++renderPass)
    {
        bool eyeActive = false;
#if defined(RAD_ANDROID)
        eyeActive = (renderPasses == 2) && SharOpenXR::BeginEye(renderPass);
        if (renderPasses == 2 && !eyeActive)
            continue;
#endif

      for (int i = RenderEnums::numLayers - 1; i > -1; i--)
      {
#if defined(RAD_ANDROID)
        // Legacy Scrooby shaders do not write both layers of a multiview
        // framebuffer. Render the GUI separately into each eye after the
        // single-pass world, while keeping its hand-attached groups cached.
        if(multiviewActive && i==RenderEnums::GUI)
            continue;
#endif
#if defined(DEBUGINFO_ENABLED) && !defined(RAD_ANDROID)
        if (i == RenderEnums::GUI)
        {
            DEBUGINFO_RENDER();
        }
#endif

        RenderLayer *pLayer = mpRenderLayers[i];

#if defined(RAD_ANDROID)
        if (eyeActive || multiviewActive)
        {
            PresentationManager* xrPresentation = GetPresentationManager();
            const bool xrMoviePlaying = xrPresentation &&
                xrPresentation->GetFMVPlayer() &&
                xrPresentation->GetFMVPlayer()->IsPlaying();
            SharOpenXR::SetWorldRendering(
                i >= RenderEnums::PresentationSlot && !xrMoviePlaying);
        }
#endif

        if (pLayer == NULL)
        {
            continue;
        }

        bool ready = pLayer->IsRenderReady();

        if (ready)
        {
#if defined(RAD_ANDROID)
            rmt::Matrix originalCameras[MAX_PLAYERS];
            bool changedCameras[MAX_PLAYERS] = { false };
            if (eyeActive || multiviewActive)
            {
                for (unsigned int v = 0; v < pLayer->GetNumViews(); ++v)
                {
                    tCamera* camera = pLayer->pCam(v);
                    if (camera)
                    {
                        originalCameras[v] = camera->GetCameraToWorldMatrix();
                        rmt::Matrix eyeCamera;
                        bool haveEyeCamera=false;
                        if(multiviewActive)
                        {
                            haveEyeCamera=SharOpenXR::PrepareMultiviewCamera(camera) &&
                                          SharOpenXR::GetLatestCullingCamera(&eyeCamera);
                        }
                        else
                        {
                            haveEyeCamera=SharOpenXR::GetEyeCamera(renderPass, camera, &eyeCamera);
                        }
                        if (haveEyeCamera)
                        {
                            camera->SetCameraMatrix(&eyeCamera);
                            changedCameras[v] = true;
                        }
                    }
                }
            }
#endif
            BEGIN_PROFILE("Layers");
#if defined(RAD_ANDROID)
            const radTime64 vrLayerStart=radTimeGetMicroseconds64();
#endif
            pLayer->Render();
#if defined(RAD_ANDROID)
            SharOpenXR::RecordRenderSection(static_cast<unsigned>(i),
                (radTimeGetMicroseconds64()-vrLayerStart)/1000.0);
#endif
            END_PROFILE("Layers");
#if defined(RAD_ANDROID)
            if (eyeActive || multiviewActive)
            {
                for (unsigned int v = 0; v < pLayer->GetNumViews(); ++v)
                    if (changedCameras[v] && pLayer->pCam(v))
                        pLayer->pCam(v)->SetCameraMatrix(&originalCameras[v]);
            }
#endif
        }
      }
#if defined(RAD_ANDROID)
        if(eyeActive)
        {
            PresentationManager* moviePresentation=GetPresentationManager();
            FMVPlayer* moviePlayer=moviePresentation?moviePresentation->GetFMVPlayer():NULL;
            // RenderCurrentVrEye checks the underlying decoder state. Do not
            // use AnimationPlayer::IsPlaying here: Android FMV audio can be
            // active while that wrapper reports false.
            if(moviePlayer) moviePlayer->RenderCurrentVrEye();
        }
        if (eyeActive) SharOpenXR::EndEye(renderPass);
#endif
    }
#if defined(RAD_ANDROID)
    if(multiviewActive)
    {
        RenderLayer* guiLayer=mpRenderLayers[RenderEnums::GUI];
        PresentationManager* moviePresentation=GetPresentationManager();
        FMVPlayer* moviePlayer=moviePresentation?
            moviePresentation->GetFMVPlayer():NULL;
        for(unsigned int guiEye=0;guiEye<2;++guiEye)
        {
            if(!SharOpenXR::BeginMultiviewGuiEye(guiEye)) continue;

            // BeginMultiviewGuiEye binds one layer of the array framebuffer.
            // The movie renderer uses an ordinary (non-multiview) shader, so
            // submit the decoded texture once to each bound eye just like the
            // dual-pass path above.
            if(moviePlayer) moviePlayer->RenderCurrentVrEye();

            if(guiLayer && guiLayer->IsRenderReady())
            {
#if defined(RAD_ANDROID)
                const radTime64 vrGuiStart=radTimeGetMicroseconds64();
#endif
                rmt::Matrix originalGuiCameras[MAX_PLAYERS];
                bool changedGuiCameras[MAX_PLAYERS]={false};
                for(unsigned int view=0;view<guiLayer->GetNumViews();++view)
                {
                    tCamera* camera=guiLayer->pCam(view);
                    if(!camera) continue;
                    originalGuiCameras[view]=camera->GetCameraToWorldMatrix();
                    rmt::Matrix eyeCamera;
                    if(SharOpenXR::GetEyeCamera(guiEye,camera,&eyeCamera))
                    {
                        camera->SetCameraMatrix(&eyeCamera);
                        changedGuiCameras[view]=true;
                    }
                }
                guiLayer->Render();
#if defined(RAD_ANDROID)
                SharOpenXR::RecordRenderSection(0,
                    (radTimeGetMicroseconds64()-vrGuiStart)/1000.0);
#endif
                for(unsigned int view=0;view<guiLayer->GetNumViews();++view)
                    if(changedGuiCameras[view] && guiLayer->pCam(view))
                        guiLayer->pCam(view)->SetCameraMatrix(&originalGuiCameras[view]);
            }
        }
        SharOpenXR::EndMultiview();
    }
#endif

    END_PROFILE("Rendering");

#ifdef DEBUGWATCH
    t0 = radTimeGetMicroseconds();
#endif

#ifndef FINAL
    BEGIN_PROFILE("Dump Stats");
    HeapMgr()->DumpHeapStats();
    HeapMgr()->DumpArtStats();
    END_PROFILE("Dump Stats");
#endif

    #if !defined(RAD_ANDROID)
    RENDER_PROFILER();
    SOUNDDEBUG_RENDER();
    #endif

#ifdef USE_BLUR
    ((pddiExtFramebufferEffects*)p3d::pddi->GetExtension(PDDI_EXT_FRAMEBUFFER_EFFECTS))->EnableMotionBlur(
        mEnableMotionBlur || ENABLE_MOTION_BLUR, mBlurAlpha, BLUR_SCALE, false);

    ((pddiExtFramebufferEffects*)p3d::pddi->GetExtension(PDDI_EXT_FRAMEBUFFER_EFFECTS))->SetQuality(
        pddiExtFramebufferEffects::Smallest);

    if (mEnableMotionBlur || ENABLE_MOTION_BLUR)
    {
        ((pddiExtFramebufferEffects*)p3d::pddi->GetExtension(PDDI_EXT_FRAMEBUFFER_EFFECTS))->RenderMotionBlur();
    }
#endif

    if (CommandLineOptions::Get(CLO_DEMO_TEST) ||
        GetCheatInputSystem()->IsCheatEnabled(CHEAT_ID_DEMO_TEST))
    {
        char buffy[32];
        sprintf(buffy, "Demo Count: %d", GetGame()->GetDemoCount());

        const int LEFT = 35;
        const int TOP = 45;
        const pddiColour WHITE(128, 128, 128);

        p3d::pddi->DrawString(buffy, LEFT, 80 + TOP, WHITE);

        static unsigned int time = 0;
        time = GetGame()->GetTime();
        time += iElapsedTime;

        unsigned int hours = time / 3600000;
        unsigned int deltaTime = time % 3600000;

        unsigned int minutes = deltaTime / 60000;
        deltaTime = deltaTime % 60000;

        unsigned int seconds = deltaTime / 1000;
        deltaTime = deltaTime % 1000;

        sprintf(buffy, "Time: %d:%d:%d.%d", hours, minutes, seconds, deltaTime);
        p3d::pddi->DrawString(buffy, LEFT, 100 + TOP, WHITE);

        if (GetGameplayManager())
        {
            sprintf(buffy, "Level %d", GetGameplayManager()->GetCurrentLevelIndex());
            p3d::pddi->DrawString(buffy, LEFT, 120 + TOP, WHITE);
        }

        GetGame()->SetTime(time);
    }

    // No touch-control overlay in standalone VR.
    p3d::context->EndFrame(false);
#if defined(RAD_ANDROID)
    if (xrFrame) SharOpenXR::EndFrame();
#endif

#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds() - t0;

    if (mDebugDumpAllZones)
    {
        mDebugDumpAllZones = false;
        mpLayer(RenderEnums::LevelSlot)->DumpAllDynaLoads(1, mEntityDeletionList);
        GetWorldPhysicsManager()->FreeAllCollisionAreaIndicies();
        FlushDelList();
        ::radMemoryMonitorSuspend();
    }
#endif
}


//******************************************************************************
//
// Public Member Functions : Instance Interface
//
//******************************************************************************

//==============================================================================
// RenderManager::CreateInstance
//==============================================================================
//
// Description: Create the RenderManager controller if needed.
//
// Parameters:	None.
//
// Return:      Pointer to the created RenderManager controller.
//
// Constraints: This is a singleton so only one instance is allowed.
//
//==============================================================================
RenderManager* RenderManager::CreateInstance()
{
MEMTRACK_PUSH_GROUP( "RenderManager" );
   rAssert( mspInstance == NULL );
   mspInstance = new(GMA_PERSISTENT) RenderManager();
MEMTRACK_POP_GROUP( "RenderManager" );

   return mspInstance;
}

//==============================================================================
// RenderManager::GetInstance
//==============================================================================
//
// Description: Get the RenderManager controller if exists.
//
// Parameters:	None.
//
// Return:      Pointer to the created RenderManager controller.
//
// Constraints: This is a singleton so only one instance is allowed.
//
//==============================================================================
RenderManager* RenderManager::GetInstance()
{
   rAssert( mspInstance != NULL );
   
   return mspInstance;
}


//==============================================================================
// RenderManager::DestroyInstance
//==============================================================================
//
// Description: Destroy the RenderManager controller.
//
// Parameters:	None.
//
// Return:      None.
//
//==============================================================================
void RenderManager::DestroyInstance()
{
    //
    // Make sure this doesn't get called twice.
    //
    rAssert( mspInstance != NULL );
    delete mspInstance;
    mspInstance = NULL;
}
//========================================================================
// RenderManager::
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
WorldScene* RenderManager::pWorldScene()
{
//   return ((WorldRenderLayer*)mpRenderLayers[RenderEnums::LevelSlot])->pWorldScene();
    return ((WorldRenderLayer*)mpRenderLayers[mCurWorldLayer])->pWorldScene();
}
//========================================================================
// RenderManager::
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
WorldRenderLayer* RenderManager::pWorldRenderLayer()
{
    return ((WorldRenderLayer*)mpRenderLayers[mCurWorldLayer]);
}

//========================================================================
// RenderManager::OnChunkLoaded
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
void RenderManager::OnChunkLoaded
(   
   tEntity*   ipEntity, 
   int        iUserData, 
   unsigned   iChunkID    
)        
{

   if( (iUserData == RenderEnums::BogusUserData) ||
       (iUserData == (int)(RenderEnums::IgnoreGuts)) )// ignore bogus idata -go to hell, compiler! With your trite, meaningless warnings
      return;
   
   rAssert( ipEntity != NULL );
   rAssert( (iUserData & RenderEnums::LayerOnlyMask) < RenderEnums::numLayers && 
            (iUserData & RenderEnums::LayerOnlyMask) > -1 );

   /*rDebugPrintf( "Chunk: Layer:%X  GutsID: %X\n", 
                  (iUserData & RenderEnums::LayerOnlyMask),
                  (iUserData & RenderEnums::GutsOnlyMask));*/

   IntersectDSG* pIDSG = NULL;
   
  // if( (iUserData & RenderEnums::CompletionOnlyMask) == RenderEnums::AllRenderLoadingComplete )
  // {
  //    return;
  // }

   switch( iChunkID ) 
   {
    case SRR2::ChunkID::LENS_FLARE_DSG:
    case SRR2::ChunkID::INSTA_ENTITY_DSG:
    case SRR2::ChunkID::ENTITY_DSG:
      switch( iUserData & RenderEnums::GutsOnlyMask )
      {
      case RenderEnums::StaticEntityGuts:
         mpRenderLayers[iUserData & RenderEnums::LayerOnlyMask]->AddGuts((StaticEntityDSG*)ipEntity);
         break;
      default:
         //Unexpected GutsEnum
         rAssert(false);
         break;
      }
      break;


   case SRR2::ChunkID::INSTA_STATIC_PHYS_DSG:
   case SRR2::ChunkID::STATIC_PHYS_DSG:
      switch( iUserData & RenderEnums::GutsOnlyMask )
      {
      case RenderEnums::StaticPhysGuts:
         mpRenderLayers[iUserData & RenderEnums::LayerOnlyMask]->AddGuts((StaticPhysDSG*)ipEntity);
         break;
      default:
         //Unexpected GutsEnum
         rAssert(false);
         break;
      }
      break;

   case SRR2::ChunkID::DYNA_PHYS_DSG:
   case SRR2::ChunkID::INSTA_ANIM_DYNA_PHYS_DSG:		
		
      switch( iUserData & RenderEnums::GutsOnlyMask )
      {
      case RenderEnums::DynaPhysGuts:
          {
            int renderLayer = iUserData & RenderEnums::LayerOnlyMask;
            DynaPhysDSG* pDynaPhys = static_cast< DynaPhysDSG* >( ipEntity );
            rAssert( dynamic_cast< DynaPhysDSG* >( ipEntity  ) != NULL );
            mpRenderLayers[ renderLayer ]->AddGuts( pDynaPhys );
            pDynaPhys->SetRenderLayer( static_cast< RenderEnums::LayerEnum >( renderLayer ) );
          }         
         break;
      default:
         //Unexpected GutsEnum
         rAssert(false);
         break;
      }
      break;

   case SRR2::ChunkID::TREE_DSG:
      switch( iUserData & RenderEnums::GutsOnlyMask )
      {
      case RenderEnums::TreeDSGGuts:
         mpRenderLayers[iUserData & RenderEnums::LayerOnlyMask]->AddGuts((SpatialTree*)ipEntity);
         break;
      default:
         //Unexpected GutsEnum
         rAssert(false);
         break;
      }
      break;

   case SRR2::ChunkID::FENCE_DSG:
      switch( iUserData & RenderEnums::GutsOnlyMask )
      {
      case RenderEnums::FenceGuts:
         mpRenderLayers[iUserData & RenderEnums::LayerOnlyMask]->AddGuts((FenceEntityDSG*)ipEntity);
         break;
      default:
         //Unexpected GutsEnum
         rAssert(false);
         break;
      }
      break;

   case SRR2::ChunkID::INTERSECT_DSG:
      switch( iUserData & RenderEnums::GutsOnlyMask )
      {
      case RenderEnums::IntersectGuts:
         mpRenderLayers[iUserData & RenderEnums::LayerOnlyMask]->AddGuts((IntersectDSG*)ipEntity);
         break;
      default:
         //Unexpected GutsEnum
         rAssert(false);
         break;
      }
      break;

      case SRR2::ChunkID::ANIM_DSG:
          switch ( iUserData & RenderEnums::GutsOnlyMask )
          {
          case RenderEnums::AnimGuts:
              {
//              mpRenderLayers[iUserData & RenderEnums::LayerOnlyMask]->AddGuts((AnimEntityDSG*)ipEntity);
                int renderLayer = iUserData & RenderEnums::LayerOnlyMask;
                AnimEntityDSG* pAnimDSG = static_cast< AnimEntityDSG* >( ipEntity );
                rAssert( dynamic_cast< AnimEntityDSG* >( ipEntity  ) != NULL );
                mpRenderLayers[ renderLayer ]->AddGuts( pAnimDSG );
                pAnimDSG->SetRenderLayer( static_cast< RenderEnums::LayerEnum > (renderLayer) );
              }
              break;
          default:
              rAssert( false );
              break;
          }
          break;



   case SRR2::ChunkID::ANIM_COLL_DSG:
      switch( iUserData & RenderEnums::GutsOnlyMask )
      {
      case RenderEnums::AnimCollGuts:
          {
    //         mpRenderLayers[iUserData & RenderEnums::LayerOnlyMask]->AddGuts((AnimCollisionEntityDSG*)ipEntity);
            int renderLayer = iUserData & RenderEnums::LayerOnlyMask;
            AnimCollisionEntityDSG* pAnimCollDSG = static_cast< AnimCollisionEntityDSG* >( ipEntity );
            rAssert( dynamic_cast< AnimCollisionEntityDSG* >( ipEntity  ) != NULL );
            mpRenderLayers[ renderLayer ]->AddGuts( pAnimCollDSG );
            pAnimCollDSG->SetRenderLayer( static_cast< RenderEnums::LayerEnum > (renderLayer) );
         break;
          }
      default:
         //Unexpected GutsEnum
         rAssert(false);
         break;
      }
      break;



   case Pure3D::Mesh::MESH:
      switch( iUserData & RenderEnums::GutsOnlyMask )
      {
      case RenderEnums::GeometryGuts:
         mpRenderLayers[iUserData & RenderEnums::LayerOnlyMask]->AddGuts((tGeometry*)ipEntity);
      	break;
      case RenderEnums::DrawableGuts:
         mpRenderLayers[iUserData & RenderEnums::LayerOnlyMask]->AddGuts((tDrawable*)ipEntity);
      	break;
      case RenderEnums::IntersectGuts:
         pIDSG = GetDSGFactory()->CreateIntersectDSG( (tGeometry*)ipEntity );
         mpRenderLayers[iUserData & RenderEnums::LayerOnlyMask]->AddGuts( pIDSG );
         break;
      case RenderEnums::IgnoreGuts:
      	break;
      default:
         //Unexpected GutsEnum
         rAssert(false);
         break;
      }
      break;

   case SRR2::ChunkID::LOCATOR:
      switch( iUserData & RenderEnums::GutsOnlyMask )
      {
      case RenderEnums::LocatorGuts:
         mpRenderLayers[iUserData & RenderEnums::LayerOnlyMask]->AddGuts((TriggerVolume*)ipEntity);
         break;
      default:
         //Unexpected GutsEnum
         rAssert(false);
         break;
      }
      break;

   case SRR2::ChunkID::ROAD_SEGMENT:
      switch( iUserData & RenderEnums::GutsOnlyMask )
      {
      case RenderEnums::RoadSegmentGuts:
         mpRenderLayers[iUserData & RenderEnums::LayerOnlyMask]->AddGuts((RoadSegment*)ipEntity);
         break;
      default:
         //Unexpected GutsEnum
         rAssert(false);
         break;
      }
      break;

   case SRR2::ChunkID::PED_PATH_SEGMENT:
      switch( iUserData & RenderEnums::GutsOnlyMask )
      {
      case RenderEnums::PathSegmentGuts:
         mpRenderLayers[iUserData & RenderEnums::LayerOnlyMask]->AddGuts((PathSegment*)ipEntity);
         break;
      default:
         //Unexpected GutsEnum
         rAssert(false);
         break;
      }
      break;

   case SRR2::ChunkID::WORLD_SPHERE_DSG:
      switch( iUserData & RenderEnums::GutsOnlyMask )
      {
      case RenderEnums::GlobalWSphereGuts:
         ((WorldSphereDSG*)ipEntity)->Activate();
      case RenderEnums::WorldSphereGuts:
         mpRenderLayers[iUserData & RenderEnums::LayerOnlyMask]->AddGuts((WorldSphereDSG*)ipEntity);
         break;
      default:
         //Unexpected GutsEnum
         rAssert(false);
         break;
      }
      break;

    default:
      //Unexpected ChunkID
      rAssert(false);
      break;
   } 
}

//========================================================================
// RenderManager::OnProcessRequestsComplete( void* pUserData );
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
void RenderManager::OnProcessRequestsComplete( void* pUserData )
{
   rAssert( pUserData != NULL );

    //
    // When done queued loading, Ignore any new geo's loaded through the 
    // LoaderWrappers
    //
    //AllWrappers::GetInstance()->mLoader( AllWrappers::msGeometry ).ModRegdListener( this, RenderEnums::LevelSlot | RenderEnums::IgnoreGuts );
    GetEventManager()->TriggerEvent( EVENT_DYNAMIC_ZONE_LOAD_ENDED );
#ifndef RAD_RELEASE
    FinishedZone();
#endif

//***
    //////////////////////////////////////////////////////////////////////////
    // Dynamic Loading
    //////////////////////////////////////////////////////////////////////////
    if( ((*(int*)pUserData) & RenderEnums::CompletionOnlyMask) == RenderEnums::DynamicLoadComplete )
    {
        tName GiveItAFuckinName;
        static char spSomeDamnFile[128];
        int i = ((*(int*)pUserData) & RenderEnums::ZoneMask) >> RenderEnums::ZoneShift;

        mpRenderLayers[(*(int*)pUserData) & RenderEnums::LayerOnlyMask]->DoPostDynaLoad();

        bool alreadyLoaded=true;

        if( i<mpZEL->GetNumLoadZones() && (msLayer & RenderEnums::LevelSlot) )
        {
            rReleasePrintf("Zone Loading Ended: %s\n",mpZEL->GetLoadZone(i));
            //HeapMgr()->DumpHeapStats(true);
            tUID tempUID = tName::MakeUID(mpZEL->GetLoadZone(i));
            GetEventManager()->TriggerEvent( EVENT_NAMED_DYNAMIC_ZONE_LOAD_ENDED, &tempUID );
            
            if( mbLoadZonesDumped )
            {
                mbLoadZonesDumped = false;
                
                //
                // Do no further loading; it's been "cancelled".
                // This is to cover the load resumption logged as bug 10575.
                // Essentially, the dump would get queued before a zone finished loading
                // (through a mission reset). Then, on completion, it would dump the load 
                // (with the doPost above) and start loading all the other zones listed in the ZEL.
                //
                mZELs.mUseSize = 0;   
                i = mpZEL->GetNumLoadZones();
                rReleasePrintf("***All LoadZones Dumped; ZEL's cancelled.***\n");
                
            }

            for(i;i<mpZEL->GetNumLoadZones()&&alreadyLoaded; )
            {
                i++;
                if(i<mpZEL->GetNumLoadZones())
                {
                    HeapMgr()->PushHeap (GMA_TEMP);
                    GiveItAFuckinName.SetText(mpZEL->GetLoadZone(i));
                    HeapMgr()->PopHeap ( GMA_TEMP);

                    alreadyLoaded= ! mpRenderLayers[RenderEnums::LevelSlot]->DoPreDynaLoad(GiveItAFuckinName);
                }
            }

            if(i<mpZEL->GetNumLoadZones())
            {
                sprintf(spSomeDamnFile,"art\\%s",mpZEL->GetLoadZone(i));

                (*(int*)pUserData) &= ~RenderEnums::CompletionOnlyMask;
                (*(int*)pUserData) |= RenderEnums::DynamicLoadComplete;

                (*(int*)pUserData) &= ~RenderEnums::ZoneMask;
                (*(int*)pUserData) |= i<<RenderEnums::ZoneShift;

                (*(int*)pUserData) &= ~RenderEnums::LayerOnlyMask;
                (*(int*)pUserData) |= RenderEnums::LevelSlot; 

                RedirectChunks(RenderEnums::LevelSlot);

#ifndef RAD_RELEASE
                static int loadTag2 = 0;
                LoadTag( spSomeDamnFile, loadTag2 );
#endif

#ifdef LOAD_SYNC
                HeapMgr()->DumpHeapStats( true );
                GetLoadingManager()->LoadSync( FILEHANDLER_PURE3D, spSomeDamnFile, GMA_LEVEL_ZONE, mpZEL->GetLoadZone(i) );
                HeapMgr()->DumpHeapStats( true );
                GetLoadingManager()->AddCallback( this, (int*)pUserData );
#else
                GetLoadingManager()->AddRequest( FILEHANDLER_PURE3D, spSomeDamnFile, GMA_LEVEL_ZONE, mpZEL->GetLoadZone(i), NULL, this, (int*)pUserData);  
#endif
            }
            else
            {
                if(mpZEL->IsInteriorLoad())
                {
                    InteriorLoadedEventData data;
                    data.interiorName = mpZEL->GetNameObject();
                    data.sectionName = mpZEL->GetInteriorSection();
                    data.first = mbFirstDynamicZone;
                    GetEventManager()->TriggerEvent( EVENT_INTERIOR_LOADED, reinterpret_cast<void*>( &data ) );
                }

                if( mbFirstDynamicZone )
                {
                    mbFirstDynamicZone = false;
                    GetEventManager()->TriggerEvent( EVENT_FIRST_DYNAMIC_ZONE_END );
                }
/*
                //////////////////////////////////////////////////////////////////////////
                // Find a Zone to load
                //////////////////////////////////////////////////////////////////////////
BEGIN_PROFILE( "Find Load Zone" );
                i=-1;
                for(i;i<mpZEL->GetNumLoadZones()&&alreadyLoaded; )
                {
                    i++;
                    if(i<mpZEL->GetNumLoadZones())
                    {
                        HeapMgr()->PushHeap (GMA_TEMP);
                        GiveItAFuckinName.SetText(mpZEL->GetLoadZone(i));
                        HeapMgr()->PopHeap ( GMA_TEMP );

                        alreadyLoaded= ! mpRenderLayers[RenderEnums::LevelSlot]->DoPreDynaLoad(GiveItAFuckinName);
                    }
                }
END_PROFILE( "Find Load Zone" );

                //////////////////////////////////////////////////////////////////////////
                // Zone Loading
                //////////////////////////////////////////////////////////////////////////
                if(i<mpZEL->GetNumLoadZones())
                {
                    sprintf(spSomeDamnFile,"art\\%s",mpZEL->GetLoadZone(i));

                    msLayer &= ~RenderEnums::CompletionOnlyMask;
                    msLayer |= RenderEnums::DynamicLoadComplete;

                    msLayer &= ~RenderEnums::ZoneMask;
                    msLayer |= i<<RenderEnums::ZoneShift;
              
                    msLayer &= ~RenderEnums::LayerOnlyMask;
                    msLayer |= RenderEnums::LevelSlot; 
                       
                    RedirectChunks(RenderEnums::LevelSlot);

BEGIN_PROFILE( "Add Requests Int" );
#ifndef RAD_RELEASE
                    static int loadTag3 = 0;
                    LoadTag( spSomeDamnFile, loadTag3 );
#endif

#ifdef LOAD_SYNC
                    HeapMgr()->DumpHeapStats( true );
                    GetLoadingManager()->LoadSync( FILEHANDLER_PURE3D, spSomeDamnFile, GMA_LEVEL_ZONE, mpZEL->GetLoadZone(i) );
                    HeapMgr()->DumpHeapStats( true );
#else
                    GetLoadingManager()->AddRequest( FILEHANDLER_PURE3D, spSomeDamnFile, GMA_LEVEL_ZONE, mpZEL->GetLoadZone(i), NULL, this, &msLayer);  
#endif

END_PROFILE( "Add Requests Int" );

                    rReleasePrintf("ZoneLoadingStart--===--%s--===--\n", spSomeDamnFile);
                    mbDynaLoading = true;

#ifdef LOAD_SYNC   
                    GetLoadingManager()->AddCallback( this, &msLayer );
#endif
                    return;
                }
*/
                //
                // If other loading sets were queued while loading
                //
                if( mZELs.mUseSize != 0 )
                {
                    mpZEL = mZELs[0];
                    mZELs.RemoveKeepOrder(0);

                    (*(int*)pUserData) &= ~RenderEnums::ZoneMask;
                    (*(int*)pUserData) |= 0<<RenderEnums::ZoneShift;

                    rReleasePrintf("Attemptimg to Load a Driving Too Fast Zone.\n");

                    mbDynaLoading = false;
                    mbDrivingTooFastLoad = true;
                    HandleEvent((EventEnum)(EVENT_LOCATOR+LocatorEvent::DYNAMIC_ZONE), mpZEL);
                }
                else
                {
                    GetEventManager()->TriggerEvent( EVENT_ALL_DYNAMIC_ZONE_END );
                    rReleasePrintf("ZoneLoadingEnded Verified\n");
                    mbDynaLoading = false;
                    AllWrappers::GetInstance()->mLoader( AllWrappers::msBillboard     ).ModRegdListener( this, RenderEnums::IgnoreGuts );         
                }
            }
        }
   }
}
//========================================================================
// RenderManager::
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
void RenderManager::DoPostLevelLoad()
{
    //////////////////////////////////////////////////////////////////////////
    // PreDynamic Loading
    //////////////////////////////////////////////////////////////////////////
//   if( ((*(int*)pUserData) & RenderEnums::CompletionOnlyMask) == RenderEnums::AllRenderLoadingComplete )
   {
//      mpRenderLayers[(*(int*)pUserData) & RenderEnums::LayerOnlyMask]->DoPostStaticLoad();
////      mpRenderLayers[(*(int*)pUserData) & RenderEnums::LayerOnlyMask]->Resurrect();

      //mpRenderLayers[(*(int*)pUserData) & RenderEnums::LayerOnlyMask]->FreezeCorpse();

      mpRenderLayers[RenderEnums::LevelSlot]->DoPostDynaLoad();
      mpRenderLayers[RenderEnums::LevelSlot]->FreezeCorpse();
      //
      // When done queued loading, Ignore any new geo's loaded through the 
      // LoaderWrappers
      //
      AllWrappers::GetInstance()->mLoader( AllWrappers::msGeometry ).ModRegdListener( this, RenderEnums::LevelSlot | RenderEnums::IgnoreGuts );
      mDoneInitialLoad = true;
      mbDynaLoading = false;
      AllWrappers::GetInstance()->mLoader( AllWrappers::msBillboard     ).ModRegdListener( this, RenderEnums::IgnoreGuts );         
   }

}
bool RenderManager::DoneInitialLoad()
{
   return mDoneInitialLoad;
}
//=============================================================================
// RenderManager::mpLayer
//=============================================================================
// Description: Comment
//
// Parameters:  ( RenderEnums::LayerEnum isLayer )
//
// Return:      RenderLayer
//
//=============================================================================
RenderLayer* RenderManager::mpLayer( RenderEnums::LayerEnum isLayer )
{
   //Valid Layer?
   rAssert( isLayer < RenderEnums::numLayers );

   //
   // Walk (talk?) like an egyptian
   //
   return mpRenderLayers[isLayer];
}

//=============================================================================
// RenderManager::FreezeAllLayers
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void RenderManager::FreezeAllLayers()
{
    for( unsigned int i = 0; i < RenderEnums::numLayers; i++ )
    {
        mpRenderLayers[ i ]->Freeze();
    }
}
/*=============================================================================
Description:    Freeze the layers, except for the presentation layer. The trick
                here however is that the layer remembers it's previous state
                because some layers could have already been frozen (such as
                interior/exterior layers) and we don't want them to thaw after
                the presentation. So match this call will a call to
                ThawFromPresentation().
=============================================================================*/
void RenderManager::FreezeForPresentation( void )
{
    for( unsigned int i = 0; i < RenderEnums::numLayers; ++i )
    {
        if( i != RenderEnums::GUI ) // exclude GUI layer
        {
            mpRenderLayers[ i ]->Chill();
        }
    }
}
/*=============================================================================
Description:    Thaws all layers. The idea is that we'll freeze all the layers
                for playing an FMV, then thaw the presentation layer. After
                the movie we need to thaw all the layers and then freeze the
                presentation layer. I'm not sure this will work so well for
                cases where we had a layer frozen for another reason before the
                movie started.
=============================================================================*/
void RenderManager::ThawFromPresentation( void )
{
    for( unsigned int i = 0; i < RenderEnums::numLayers; ++i )
    {
        if( i != RenderEnums::GUI ) // exclude GUI layer
        {
            mpRenderLayers[ i ]->Warm();
        }
    }
}
//========================================================================
// RenderManager::
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
void RenderManager::HandleEvent( EventEnum id, void* pEventData )
{
BEGIN_PROFILE( "RenderManager HandleEvent" );
   switch(id)
   {
   case EVENT_MISSION_RESET:
       {
#if defined( RAD_XBOX )
           // XBox seems to like this syntax better.
           bool jumpStage = reinterpret_cast<bool>( pEventData );
#else
           bool jumpStage = (bool)( pEventData );
#endif
           if( jumpStage )
           {
               ResetMoodLighting( true );
           }
       }
       break;
   case EVENT_LOCATOR + LocatorEvent::LIGHT_CHANGE:
    {
        EventLocator* pLocator = static_cast<EventLocator*>( pEventData );
        rAssert( pLocator );
        tColour lightMod( pLocator->GetData() );
        if ( pLocator->GetPlayerEntered() )
        {
            SetLightMod( lightMod );
            ++(mMood.mVolumeCount);
        }
        else
        {
            --(mMood.mVolumeCount);
            if( mMood.mVolumeCount == 0 )
            {
                ResetMoodLighting();
            }
        }
        if( mMood.mVolumeCount < 0 )
        {
            mMood.mVolumeCount = 0;
        }
    }
    break;
   case EVENT_LOCATOR+LocatorEvent::OCCLUSION_ZONE:
   {
       OcclusionLocator* pOccLocator = ((OcclusionLocator*)pEventData);
       SpatialTreeIter& rTreeWalker = pWorldScene()->mStaticTreeWalker; 

       SphereSP occSphere;
       rmt::Sphere sphere;

       BoxPts occBox;
       rmt::Box3D box;    

       if(pOccLocator->GetPlayerEntered())
       {
            if(mbInVsibilityVolume)
            {
                rTreeWalker.AndTree(0x00000000);
                mbIgnoreVisibilityClear = true;
            }
            mbInVsibilityVolume  = true;
            tMark curMark = SpatialTreeIter::msFilterInvisible;
            int numOccluders = pOccLocator->GetNumOccTriggers();
            int stopCondition = pOccLocator->GetNumTriggers();
            for(int i=1; i<stopCondition; i++)
            { 
                if(i>numOccluders)
                    curMark = SpatialTreeIter::msFilterVisible;
                else
                    curMark = SpatialTreeIter::msFilterInvisible;

                if(pOccLocator->GetTriggerVolume(i)->GetType() == TriggerVolume::SPHERE )
                {
                    pOccLocator->GetTriggerVolume(i)->GetBoundingSphere(&sphere);
                    occSphere.SetTo( sphere.centre, sphere.radius );
                    rTreeWalker.MarkSubTrees(occSphere, curMark );
                }
                else
                {
                    pOccLocator->GetTriggerVolume(i)->GetBoundingBox(&box);
                    occBox.mBounds.mMin.SetTo(box.low);
                    occBox.mBounds.mMax.SetTo(box.high);
                    rTreeWalker.MarkSubTrees(occBox, curMark );
                }
            }
       }
       else
       {
           if(mbIgnoreVisibilityClear)
           {
               mbIgnoreVisibilityClear = false; 
           }
           else
           {
               mbInVsibilityVolume  = false;
               rTreeWalker.AndTree(0x00000000);
           }
       }

   }
   break;

   case EVENT_ALL_DYNAMIC_ZONES_DUMPED:
   {
       if(mbDynaLoading)
           mbLoadZonesDumped = true; 
       break;
   }
   case EVENT_FIRST_DYNAMIC_ZONE_START:       
         mbFirstDynamicZone = true;
   case EVENT_LOCATOR+LocatorEvent::DYNAMIC_ZONE:
   {
         ZoneEventLocator* pZEL = (ZoneEventLocator*)pEventData;
         //HeapMgr()->DumpHeapStats(true);

        /* if(!(pZEL->GetPlayerEntered()))
         {
             int j;
             for( j=pZEL->GetNumLWSActivates()-1; j>-1; j-- )
             {
                 pWorldRenderLayer()->DeactivateWS(tName::MakeUID(pZEL->GetLWSActivates(j)));
             }
             for( j=pZEL->GetNumLWSDeactivates()-1; j>-1; j-- )
             {
                 pWorldRenderLayer()->ActivateWS(tName::MakeUID(pZEL->GetLWSDeactivates(j)));
             }
         }*/
         if(pZEL->GetPlayerEntered()||mbDrivingTooFastLoad)
         {
            if(pZEL->IsInteriorLoad())
            {
                InteriorLoadedEventData data;
                data.interiorName = pZEL->GetUID();
                data.sectionName = tEntity::MakeUID(pZEL->GetInteriorSection());
                data.first = mbFirstDynamicZone;
                GetEventManager()->TriggerEvent( EVENT_INTERIOR_LOAD_START, reinterpret_cast<void*>( &data ) );
            }

             int j;
             for( j=pZEL->GetNumLWSActivates()-1; j>-1; j-- )
             {
                 pWorldRenderLayer()->ActivateWS(tName::MakeUID(pZEL->GetLWSActivates(j)));
             }
             for( j=pZEL->GetNumLWSDeactivates()-1; j>-1; j-- )
             {
                 pWorldRenderLayer()->DeactivateWS(tName::MakeUID(pZEL->GetLWSDeactivates(j)));
             }

            if(pZEL->GetNumLoadZones()==0 && pZEL->GetNumDumpZones()==0)
            {
END_PROFILE( "RenderManager HandleEvent" );
               rReleasePrintf("Nothin to Load, skipping zone\n");
               return;
            }

            //If we're already Loading
            if(mbDynaLoading == true)
            {
                rReleasePrintf("Driving TOO FAST: Adding to queue.\n");
                mZELs.Add((ZoneEventLocator*&)pEventData);
END_PROFILE( "RenderManager HandleEvent" );
                return;
            }

             mbDrivingTooFastLoad = false;
             mpZEL = (ZoneEventLocator*)pEventData;

             //If we're not currently loading
            static char spSomeDamnFile[128];
            int i;
            bool alreadyLoaded = true;
            tName GiveItAFuckinName;

            rReleasePrintf("Encountered Dynamic Zone:\n");
            for(i=0; i<mpZEL->GetNumDumpZones(); i++)
            {
               rReleasePrintf("Dump: %s\n",mpZEL->GetDumpZone(i));
            }
            for(i=0; i<mpZEL->GetNumLoadZones(); i++)
            {
               rReleasePrintf("Load: %s\n",mpZEL->GetLoadZone(i));
            }
            //////////////////////////////////////////////////////////////////////////
            // Zone Dumping
            //////////////////////////////////////////////////////////////////////////
BEGIN_PROFILE( "Zone/Int Dump" );

BEGIN_PROFILE( "Dump Zones" );
            for(i=0; i<mpZEL->GetNumDumpZones(); i++)
            {
               HeapMgr()->PushHeap (GMA_TEMP);
               GiveItAFuckinName.SetText(mpZEL->GetDumpZone(i));
               HeapMgr()->PopHeap ( GMA_TEMP);

               //mpRenderLayers[msLayer & RenderEnums::LayerOnlyMask]->DumpDynaLoad(GiveItAFuckinName);
               mpRenderLayers[RenderEnums::LevelSlot]->DumpDynaLoad(GiveItAFuckinName, mEntityDeletionList);
            }

BEGIN_PROFILE( "Trigger IntDump Event" );
            if(mpZEL->IsInteriorDump())
            {
               GetEventManager()->TriggerEvent( EVENT_INTERIOR_DUMPED );
            }
END_PROFILE( "Trigger IntDump Event" );

END_PROFILE( "Dump Zones" );
        
            //MunchDelList(50);

END_PROFILE( "Zone/Int Dump" );

            //////////////////////////////////////////////////////////////////////////
            // Find a Zone to load
            //////////////////////////////////////////////////////////////////////////
BEGIN_PROFILE( "Find Load Zone" );
            i=-1;
            for(i;i<mpZEL->GetNumLoadZones()&&alreadyLoaded; )
            {
               i++;
               if(i<mpZEL->GetNumLoadZones())
               {
                  HeapMgr()->PushHeap (GMA_TEMP);
                  GiveItAFuckinName.SetText(mpZEL->GetLoadZone(i));
                  HeapMgr()->PopHeap ( GMA_TEMP );

                  alreadyLoaded= ! mpRenderLayers[RenderEnums::LevelSlot]->DoPreDynaLoad(GiveItAFuckinName);
               }
            }
END_PROFILE( "Find Load Zone" );

            //////////////////////////////////////////////////////////////////////////
            // Zone Loading
            //////////////////////////////////////////////////////////////////////////
            if(i<mpZEL->GetNumLoadZones())
            {
               sprintf(spSomeDamnFile,"art\\%s",mpZEL->GetLoadZone(i));

               msLayer &= ~RenderEnums::CompletionOnlyMask;
               msLayer |= RenderEnums::DynamicLoadComplete;

               msLayer &= ~RenderEnums::ZoneMask;
               msLayer |= i<<RenderEnums::ZoneShift;
      
               msLayer &= ~RenderEnums::LayerOnlyMask;
               msLayer |= RenderEnums::LevelSlot; 
               
               RedirectChunks(RenderEnums::LevelSlot);

BEGIN_PROFILE( "Add Requests Int" );
#ifndef RAD_RELEASE
                static int loadTag4 = 0;
                LoadTag( spSomeDamnFile, loadTag4 );
#endif

#ifdef LOAD_SYNC
                HeapMgr()->DumpHeapStats( true );
                GetLoadingManager()->LoadSync( FILEHANDLER_PURE3D, spSomeDamnFile, GMA_LEVEL_ZONE, mpZEL->GetLoadZone(i) );
                HeapMgr()->DumpHeapStats( true );
#else
                GetLoadingManager()->AddRequest( FILEHANDLER_PURE3D, spSomeDamnFile, GMA_LEVEL_ZONE, mpZEL->GetLoadZone(i), NULL, this, &msLayer);  
#endif

END_PROFILE( "Add Requests Int" );

               rReleasePrintf("ZoneLoadingStart--===--%s--===--\n", spSomeDamnFile);
               mbDynaLoading = true;
#ifdef LOAD_SYNC
               GetLoadingManager()->AddCallback( this, &msLayer );
#endif
            }
            else
            {
                rReleasePrintf("Nothing to load in that Zone, checking for others...\n");
                if( mZELs.mUseSize != 0 )
                {
                    mpZEL = mZELs[0];
                    mZELs.RemoveKeepOrder(0);

                    msLayer &= ~RenderEnums::ZoneMask;
                    msLayer |= i<<RenderEnums::ZoneShift;

                    rReleasePrintf("Attemptimg to Load a Driving Too Fast Zone.\n");

                    mbDynaLoading = false;
                    mbDrivingTooFastLoad = true;
                    HandleEvent((EventEnum)(EVENT_LOCATOR+LocatorEvent::DYNAMIC_ZONE), mpZEL);
                }
                else
                {
                    GetEventManager()->TriggerEvent( EVENT_ALL_DYNAMIC_ZONE_END );
                    rReleasePrintf("ZoneLoadingEnded Verified\n");
                    mbDynaLoading = false;
                    AllWrappers::GetInstance()->mLoader( AllWrappers::msBillboard     ).ModRegdListener( this, RenderEnums::IgnoreGuts );         
                }

            }
         }
      }
      break;
   default:
      break;
   }
END_PROFILE( "RenderManager HandleEvent" );
}

//******************************************************************************
//
// Private Member Functions
//
//******************************************************************************

//========================================================================
// RenderManager::InitLayers
//========================================================================
//
// Description: Initialize all Layers
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void RenderManager::InitLayers()
{
MEMTRACK_PUSH_GROUP( "RenderManager" );
    HeapMgr()->PushHeap (GMA_PERSISTENT);

    for( int i=RenderEnums::numLayers-1; i>-1; i-- )
    {
        switch(i) 
        {
        case RenderEnums::LevelSlot:
            mpRenderLayers[i] = new WorldRenderLayer();
            break;
        case RenderEnums::GUI:
            mpRenderLayers[i] = new FrontEndRenderLayer();
            mpRenderLayers[i]->DoAllSetups();
            
            break;
        case RenderEnums::PresentationSlot:
            mpRenderLayers[ i ] = new RenderLayer();
            mpRenderLayers[ i ]->SetBeginView( false );
            mpRenderLayers[ i ]->DoAllSetups();

            break;
        default:
            mpRenderLayers[i] = new RenderLayer();
            mpRenderLayers[i]->DoAllSetups();
            break;
        }
        mpRenderLayers[i]->SetUpViewCam();
    }

    HeapMgr()->PopHeap (GMA_PERSISTENT);
MEMTRACK_POP_GROUP( "RenderManager" );
}

void RenderManager::InitLevel()
{
   /*
    for( unsigned int i = 0; i < RenderEnums::numLayers-1; i++ )
    {
        mpRenderLayers[ i ]->DoPostStaticLoad();
    }

    mpRenderLayers[ RenderEnums::LevelSlot ]->Resurrect();

    for( unsigned int i = 0; i < GetGameplayManager()->GetNumPlayers(); i++ )
    {
        mpRenderLayers[RenderEnums::LevelSlot]->AddGuts((tDrawable*)(GetAvatarManager( )->GetAvatarForPlayer( i )->GetVehicle() ) );
        mpRenderLayers[RenderEnums::LevelSlot]->AddGuts((tDrawable*)(GetAvatarManager( )->GetAvatarForPlayer( i )->GetCharacter()->GetDrawablePose() ) );
    }

    //Find and attach a light to the view
    //MS7, this should go somewhere else.
    tLight* sun = p3d::find<tLight>("sun");
    rAssert( sun );

    for( unsigned int i = 0; i < mpRenderLayers[RenderEnums::LevelSlot]->GetNumViews(); i++ )
    {
        mpRenderLayers[RenderEnums::LevelSlot]->pView( i )->AddLight( sun );
    }

    //Get the clouds too!
    mClouds = p3d::find<tMultiController>("CloudController");
    rAssert( mClouds );
     */      
}

void RenderManager::SetLevelLayerLights( tLightGroup* SunGroup )
{
    HeapMgr()->PushHeap( GMA_LEVEL_OTHER );
    if ( mMood.mOriginals != 0 )
    {
        delete [] mMood.mOriginals;
        mMood.mOriginals = 0;
    }
    mMood.mSunGroup = SunGroup;
    if ( SunGroup != NULL )
    {
        // Mood lighting is enabled, set the new parameters
        rAssert( SunGroup );
        mMood.mVolumeCount = 0;
        mMood.mTransition = -1.0f;
        mMood.mSrcModulus.Set( 0xff, 0xff, 0xff );
        mMood.mDstModulus.Set( 0xff, 0xff, 0xff );
        mMood.mOriginals = new tColour[ SunGroup->GetNumLights() ];
        rAssert( mMood.mOriginals );

        for( int j = 0; j < SunGroup->GetNumLights(); ++j )
        {
            mMood.mOriginals[ j ] = SunGroup->GetLight( j )->GetColour();
        }
        RenderLayer* rl = mpLayer( RenderEnums::LevelSlot );
        rAssert( rl );
        for( unsigned int i = 0; i < rl->GetNumViews(); ++i )
        {
            for(int j = 0; j < SunGroup->GetNumLights(); ++j )
            {
                rl->pView( i )->AddLight( SunGroup->GetLight(j) );
            }
        }  
    }
    HeapMgr()->PopHeap( GMA_LEVEL_OTHER );
}

void RenderManager::ClearLevelLayerLights()
{
    delete [] mMood.mOriginals;
    mMood.mOriginals = 0;
    mMood.mSunGroup = 0;
}


//==============================================================================
// RenderManager::RenderManager
//==============================================================================
//
// Description: Constructor.
//
// Parameters:  None.
//
// Return:      N/A.
//
//==============================================================================// 
RenderManager::RenderManager() :
//MS7: Cary
    mClouds( NULL ),
#ifdef DEBUGWATCH
    mDebugDumpAllZones( false ),
#endif
    mEnableMotionBlur( false ),
    mBlurAlpha( 0 )
{
   mEntityDeletionList.Allocate(5000);
#ifdef DEBUGWATCH
   radDbgWatchAddUnsignedInt( &mDebugRenderTime, "Debug Render All Layers micros", "RenderManager", NULL, NULL );
   radDbgWatchAddUnsignedInt( &mDebugSwapTime, "Debug Render Swap micros", "RenderManager", NULL, NULL );
   radDbgWatchAddBoolean( &mDebugDumpAllZones, "Dump All Zones", "RenderManager", NULL, NULL );
#ifdef RAD_PS2
   radDbgWatchAddFloat( &MIN_PS2_BLUR, "Minimum PS2 motion blur", "RenderManager", NULL, NULL );
#endif

//   radDbgWatchAddFloat( &BLUR_ALPHA, "Blur Alpha", "RenderManager", NULL, NULL, 0.0f, 1.0f );
   radDbgWatchAddFloat( &BLUR_SCALE, "Blur Scale", "RenderManager", NULL, NULL, 0.0f, 1.0f );
   radDbgWatchAddFloat( &MAX_BLUR, "Blur max", "RenderManager", NULL, NULL, 0.0f, 1.0f );
   radDbgWatchAddBoolean( &ENABLE_MOTION_BLUR, "Enable Blur", "RenderManager" );

//   radDbgWatchAddFloat( &BLUR_ALPHA, "Blur Alpha", "RenderManager", NULL, NULL, 0.0f, 1.0f );
   radDbgWatchAddFloat( &BLUR_SCALE, "Blur Scale", "RenderManager", NULL, NULL, 0.0f, 1.0f );
   
#endif
   InitLayers();
   mDoneInitialLoad = false;
   mbLoadZonesDumped = false;
   mbIgnoreVisibilityClear = false;
   mbInVsibilityVolume = false;

   mZELs.Allocate(10);


}

//==============================================================================
// RenderManager::~RenderManager
//==============================================================================
//
// Description: Destructor.
//
// Parameters:  None.
//
// Return:      N/A.
//
//==============================================================================// 
RenderManager::~RenderManager()
{
#ifdef DEBUGWATCH
   radDbgWatchDelete(&mDebugRenderTime);
   radDbgWatchDelete(&mDebugSwapTime);
//   radDbgWatchDelete(&BLUR_ALPHA);
   radDbgWatchDelete(&BLUR_SCALE);
   radDbgWatchDelete(&MAX_BLUR);
   radDbgWatchDelete(&ENABLE_MOTION_BLUR );
#endif
}

RenderManager::MoodLighting::MoodLighting() :
mSunGroup( 0 ),
mSrcModulus( 0xffffffff ),
mDstModulus( 0xffffffff ),
mOriginals( 0 ),
mTransition( -1.0f )
{
}

RenderManager::MoodLighting::~MoodLighting()
{
    delete mOriginals;
    mOriginals = 0;
    mSunGroup = 0;
}

tColour RenderManager::MoodLighting::CalculateModulus( void )
{
    int red   = rmt::Clamp( mSrcModulus.Red() -   int( ( mSrcModulus.Red()   - mDstModulus.Red() )   * mTransition ),   0, 0xFF );
    int green = rmt::Clamp( mSrcModulus.Green() - int( ( mSrcModulus.Green() - mDstModulus.Green() ) * mTransition ), 0, 0xFF );
    int blue  = rmt::Clamp( mSrcModulus.Blue() -  int( ( mSrcModulus.Blue()  - mDstModulus.Blue() )  * mTransition ),  0, 0xFF );
    int alpha = rmt::Clamp( mSrcModulus.Alpha() - int( ( mSrcModulus.Alpha() - mDstModulus.Alpha() ) * mTransition ), 0, 0xFF );
    return tColour( red, green, blue, alpha );
}

void RenderManager::SetLightMod( const tColour& LightMod )
{
    if( LightMod.c != mMood.mDstModulus.c )
    {
        mMood.mSrcModulus = mMood.CalculateModulus();
        mMood.mDstModulus = LightMod;
        mMood.mTransition = 0.0f;
    }
}


void RenderManager::TransitionMoodLighting( unsigned int ElapsedTime )
{
    // Check for no mood lighting (supersprint doesnt have any)
    if ( mMood.mSunGroup == NULL )
        return;

    const float TIME_RATIO = ( 1.0f / 1000.0f ) / 1.0f; // Transition over 1 second.
    float deltaTransition = (float)ElapsedTime * TIME_RATIO;
    mMood.mTransition += deltaTransition;
    if( mMood.mTransition > 1.0f )
    {
        mMood.mTransition = 1.0f;
    }

    tColour curMod = mMood.CalculateModulus();

    for( int i = 0; i < mMood.mSunGroup->GetNumLights(); ++i )
    {
        tLight* l = mMood.mSunGroup->GetLight( i );
        if( curMod.c == 0xffffffff )
        {
            l->SetColour( mMood.mOriginals[ i ] );
        }
        else
        {
            int red =   ( ( mMood.mOriginals[ i ].Red()   * curMod.Red() )   + 0x80 ) >> 8;
            int green = ( ( mMood.mOriginals[ i ].Green() * curMod.Green() ) + 0x80 ) >> 8;
            int blue =  ( ( mMood.mOriginals[ i ].Blue()  * curMod.Blue() )  + 0x80 ) >> 8;
            int alpha = ( ( mMood.mOriginals[ i ].Alpha() * curMod.Alpha() ) + 0x80 ) >> 8;
            l->SetColour( tColour( red, green, blue ) );
        }
    }  

    if( mMood.mTransition == 1.0f )
    {
        mMood.mSrcModulus = mMood.mDstModulus;
        mMood.mTransition = -1.0f;
    }
}

void RenderManager::ResetMoodLighting( bool Immediate )
{
    if ( mMood.mSunGroup == NULL )
        return;

    mMood.mVolumeCount = 0;
    if( Immediate )
    {
        mMood.mTransition = -1.0f;
        mMood.mSrcModulus.c = 0xFFFFFFFF;
        mMood.mDstModulus.c = 0xFFFFFFFF;
        for( int i = 0; i < mMood.mSunGroup->GetNumLights(); ++i )
        {
            tLight* l = mMood.mSunGroup->GetLight( i );
            l->SetColour( mMood.mOriginals[ i ] );
        }
    }
    else
    {
        mMood.mSrcModulus = mMood.CalculateModulus();
        mMood.mDstModulus.c = 0xFFFFFFFF;
        mMood.mTransition = 0.0f;
    }
}

#ifdef RAD_PS2
// Bump up the blur to a minimum level on the PS2
void RenderManager::ApplyPS2Blur()
{
    // We only want blur in game, not in the frontend.
    if ( mpRenderLayers[ RenderEnums::LevelSlot ]->IsRenderReady() )
    {
        float blurThreshold = GetCheatInputSystem()->IsCheatEnabled( CHEAT_ID_TRIPPY ) ? MIN_PS2_BLUR_CHEAT : MIN_PS2_BLUR;
        if ( mBlurAlpha < blurThreshold )
        {
            mBlurAlpha = blurThreshold;         
        }
    }
    else
    {
        mBlurAlpha = 0;
    }

}
#endif

void RenderManager::AdjustBlurByFrameRate( unsigned int elapsedTime )
{
    // Lets adjust the blur effect if the framerate gets too bad
    // Blurring effect is scaled linearly
    // it kicks in at BLUR_START 
    // So y = m(x - x1) + y1
    // blur_alpha = blur_gradient ( elapsedtime - blur_start ) 
    
    float blur;
    // We only want blur in game, not in the frontend.
    if ( mpRenderLayers[ RenderEnums::LevelSlot ]->IsRenderReady() )
    {
        float alpha = BLUR_GRADIENT * ( elapsedTime - BLUR_START );
        blur = rmt::Clamp( alpha, 0.0f, MAX_BLUR );
    }
    else
    {
        blur = 0;
    }
    if ( blur > mBlurAlpha ) 
        mBlurAlpha = blur;
}


//==============================================================================
// AvgTimeCounter::AvgTimeCounter
//==============================================================================
//
// Description: Ctor.
//
//              Intialize the AvgTimeCounter class with idealized initial values
//              i.e. first run it will report an average milliseconds elapsed of 17
//
// Return:      N/A.
//
//==============================================================================// 

RenderManager::AvgTimeCounter::AvgTimeCounter():
mCurrentArrayIndex( 0 )
{
    const unsigned int IDEAL_ELAPSED_TIME = 17;
    // Division replaced by a bit shift if array is a power of two right?
    mElapsedTimeCount.Allocate( ELAPSED_TIME_ARRAY_SIZE );
    for ( int i = 0 ; i < ELAPSED_TIME_ARRAY_SIZE ; i++ )
    {
        // Fill with the idealized time, i.e. a rock solid 60fps.
        mElapsedTimeCount.Add( IDEAL_ELAPSED_TIME );                
    }
    mElapsedTimeSum = IDEAL_ELAPSED_TIME * ELAPSED_TIME_ARRAY_SIZE;
}
//==============================================================================
// AvgTimeCounter::Tick
//==============================================================================
//
// Description: Call this function every frame to update the average fps count
//              
//
//
//
// Return:      N/A.
//
//==============================================================================// 

void
RenderManager::AvgTimeCounter::Tick( unsigned int elapsedTime )
{
    // New frame
    // Adjust the sum
    mElapsedTimeSum -= mElapsedTimeCount[ mCurrentArrayIndex ];
    mElapsedTimeSum += elapsedTime;        
    // update the mElapsedTimeCount array    
    mElapsedTimeCount[ mCurrentArrayIndex ] = elapsedTime;
    mCurrentArrayIndex++;
    // Wrap around to the start of the array
    if ( mCurrentArrayIndex >= mElapsedTimeCount.mUseSize )
        mCurrentArrayIndex = 0;
}

unsigned int 
RenderManager::AvgTimeCounter::GetAverageTimePerFrame()const
{
    return mElapsedTimeSum / ELAPSED_TIME_ARRAY_SIZE;
}


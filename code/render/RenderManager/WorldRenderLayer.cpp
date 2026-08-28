//========================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        WorldRenderLayer.cpp
//
// Description: Implementation for WorldRenderLayer class.
//
// History:     Implemented	                         --Devin [4/23/2002]
//========================================================================

//========================================
// System Includes
//========================================
#include <raddebugwatch.hpp>
#include <radtime.hpp>
#include <p3d/billboardobject.hpp>

//========================================
// Project Includes
//========================================
#include <render/RenderManager/WorldRenderLayer.h>
#include <render/DSG/DynaPhysDSG.h>
#include <render/DSG/StaticPhysDSG.h>
#include <render/DSG/IntersectDSG.h>
#include <render/DSG/StaticEntityDSG.h>
#include <render/DSG/FenceEntityDSG.h>
#include <render/DSG/animcollisionentitydsg.h>
#include <render/DSG/animentitydsg.h>
#include <render/DSG/WorldSphereDSG.h>
#include <roads/roadsegment.h>
#include <pedpaths/pathsegment.h>
#include <meta/triggervolume.h>

#include <worldsim/character/charactermanager.h>
#include <worldsim/character/character.h>
#include <worldsim/vehiclecentral.h>
#include <worldsim/redbrick/vehicle.h>
#include <worldsim/redbrick/geometryvehicle.h>
#include <worldsim/coins/coinmanager.h>
#include <worldsim/coins/sparkle.h>
#include <worldsim/character/footprint/footprintmanager.h>

#include <worldsim/worldphysicsmanager.h>

#include <meta/triggervolumetracker.h> // HACK for drawing the trigger volumes

#include <render/breakables/breakablesmanager.h>
#include <render/Particles/particlemanager.h>
#include <render/animentitydsgmanager/animentitydsgmanager.h>

#include <p3d/camera.hpp>
#include <p3d/pointcamera.hpp>
#include <p3d/shadow.hpp>
#include <p3d/view.hpp>
#if defined(RAD_ANDROID)
#include <SDL.h>
#include <vr/csmbridge.h>
#include <vr/openxrmanager.h>
#include <vr/dynamiccubemap.h>
#include <p3d/primgroup.hpp>
void pglSetVehicleRearLights(int mode,int count,const float* positions,const float* directions,const float* colour);
#endif

#include <events/eventmanager.h>
#include <events/eventenum.h>

#include <data/PersistentWorldManager.h>

#include <ai/vehicle/vehicleairender.h>

#include <render/Loaders/BillboardWrappedLoader.h>
#include <worldsim/avatarmanager.h>

//NUEVOS INCLUDES
#include <presentation/presentation.h>
#include <presentation/fmvplayer/fmvplayer.h>
//FIN NUEVOS INCLUDES 



#ifdef RAD_PS2
#define DEFAULT_R 67;
#define DEFAULT_G 67;
#define DEFAULT_B 67;
#elif defined(RAD_XBOX)
#define DEFAULT_R 67;
#define DEFAULT_G 67;
#define DEFAULT_B 67;
#elif defined(RAD_WIN32)
#define DEFAULT_R 67;
#define DEFAULT_G 67;
#define DEFAULT_B 67;
#else
#define DEFAULT_R 67;
#define DEFAULT_G 67;
#define DEFAULT_B 67;
#endif

static unsigned char gWashColourR = DEFAULT_R;
static unsigned char gWashColourG = DEFAULT_G;
static unsigned char gWashColourB = DEFAULT_B;

//************************************************************************
//
// Global Data, Local Data, Local Classes
//
//************************************************************************

//************************************************************************
//
// Public Member Functions : WorldRenderLayer Interface
//
//************************************************************************

//========================================================================
// WorldRenderLayer::WorldRenderLayer
//========================================================================
//
// Description: Do some init stuff
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
WorldRenderLayer::WorldRenderLayer()
{
   mQdDump = false;
   OnWorldRenderLayerInit();
   //mpShadowGenerator = NULL;    // VolShadows

#ifdef DEBUGWATCH
   static bool firstTimeAdding = true;

   radDbgWatchAddUnsignedInt( &mDebugInnerRenderTime, "Inner Render Time", "World Render Layer" );
   radDbgWatchAddUnsignedInt( &mDebugRenderTime,      "Render Time",       "World Render Layer" );
   radDbgWatchAddUnsignedInt( &mDebugGutsTime,        "Guts Time",         "World Render Layer" );

   if ( firstTimeAdding )
   {
       radDbgWatchAddUnsignedChar( &gWashColourR, "Colour R", "Shadows" );
       radDbgWatchAddUnsignedChar( &gWashColourG, "Colour G", "Shadows" );
       radDbgWatchAddUnsignedChar( &gWashColourB, "Colour B", "Shadows" );
       firstTimeAdding = false;
   }
#endif

   mMirror = false;
}

//========================================================================
// WorldRenderLayer::~WorldRenderLayer()
//========================================================================
//
// Description: Quick! to the Garbage heap!
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
WorldRenderLayer::~WorldRenderLayer()
{
   NullifyGuts();
   //delete mpShadowGenerator;    // VolShadows
   
#ifdef DEBUGWATCH
   static bool firstTimeRemoving = true;

   radDbgWatchDelete( &mDebugInnerRenderTime );
   radDbgWatchDelete( &mDebugRenderTime );
   radDbgWatchDelete( &mDebugGutsTime );

   if ( firstTimeRemoving )
   {
       radDbgWatchDelete( &gWashColourR );
       radDbgWatchDelete( &gWashColourG );
       radDbgWatchDelete( &gWashColourB );
       firstTimeRemoving = false;
   }
#endif

}

static bool simpleShadows = true;

//////////////////////////////////////////////////////////////////////////
// Render Interface
//////////////////////////////////////////////////////////////////////////
//========================================================================
// WorldRenderLayer::Render
//========================================================================
//
// Description: Whacha got? Render it.
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================

// ESTA ES LA FUNCION ORIGINAL, DA UN PARPADEO MOLESTO CUANDO PASAMOS DE MUNDO 3D A CINEMATICAS 
// POR ESE MOTIVO VOY A COMENTARLA Y A PROBAR CON ESTA OTRA A VER SI SE SOLUCIONA 

void WorldRenderLayer::Render()
{
    BEGIN_PROFILE( "WRL Render" );
#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds();
#endif
    rAssert(!IsDead());

    // nuevos cambios
    PresentationManager* pm = GetPresentationManager();
    if( pm && pm->GetFMVPlayer() && pm->GetFMVPlayer()->IsPlaying() )
    {
#ifdef DEBUGWATCH
        mDebugRenderTime = radTimeGetMicroseconds() - mDebugRenderTime;
#endif
        END_PROFILE( "WRL Render" );
        return;
    }
    // Fin nuevos cambios

    BEGIN_PROFILE( "pddi ZBuf" );
    p3d::pddi->EnableZBuffer(true);
    END_PROFILE( "pddi ZBuf" );

    // VolShadows
    /*
    if(mpShadowGenerator == NULL)
    {
         mpShadowGenerator = new tShadowGenerator;
    }

    BEGIN_PROFILE( "PreRender Shadows" );
    mpShadowGenerator->PreRender();
    END_PROFILE( "PreRender Shadows" );
    */

    // Hack for the number of players
    for ( unsigned int view = 0; view < GetNumViews(); view++ )
    {
#ifdef DEBUGWATCH
        mDebugInnerRenderTime = radTimeGetMicroseconds();
#endif

        for(int mirrorPass = mMirror ? 1 : 0; mirrorPass >= 0; mirrorPass--)
        {
            rmt::Matrix originalCamera;
            if(mirrorPass == 1)
            {
                tCamera * camera = mpView[ view ]->GetCamera();
                originalCamera = camera->GetCameraToWorldMatrix();
                rmt::Matrix mirroredCamera;
                mirroredCamera.Mult(originalCamera, mMirrorMatrix);
                camera->SetCameraMatrix(&mirroredCamera);
            }

            if(mMirror && (mirrorPass == 0))
            {
                mpView[ view ]->SetClearMask(PDDI_BUFFER_DEPTH);
            }
            else
            {
                mpView[ view ]->SetClearMask(PDDI_BUFFER_ALL);
            }

            BEGIN_PROFILE( "View Begin Render" );
            mpView[ view ]->BeginRender();
            END_PROFILE( "View Begin Render" );

#if defined(RAD_ANDROID)
            // Enhanced mode selects the per-pixel lighting path. Enable it
            // before WorldScene::Render because static level geometry is
            // submitted directly during the spatial-tree traversal.
            const bool enhancedMaterials=SharOpenXR::IsEnhancedMaterialsEnabled();
            const radTime64 vrSetupStart=radTimeGetMicroseconds64();
            rmt::Vector worldSun(0.45f,1.0f,-0.30f),eyeSun;
            worldSun.Normalize();
            mpView[view]->GetCamera()->GetWorldToCameraMatrix().RotateVector(worldSun,&eyeSun);
            eyeSun.Normalize();
            p3dSetEnhancedSunDirection(eyeSun);
            p3dSetEnhancedWorldMaterials(enhancedMaterials);

            // Feed the closest active pair of rear lamps to the lightweight
            // GLES lighting path. Positions are taken from brake/reverse
            // joints, then converted to the current eye space used by shaders.
            int rearMode=SharOpenXR::GetVehicleLightMode();
            float rearPositions[24]={0.0f},rearDirections[24]={0.0f},rearColour[3]={1.0f,0.025f,0.012f};
            Vehicle* nearestRear[4]={NULL,NULL,NULL,NULL};
            float nearestDistance[4]={1000000.0f,1000000.0f,1000000.0f,1000000.0f};
            VehicleCentral* rearVehicles=GetVehicleCentral();
            Vehicle** rearList=NULL; int rearCount=0;
            if(rearMode>0 && rearVehicles)
                rearVehicles->GetActiveVehicleList(rearList,rearCount);
            const rmt::Vector eyeWorld=mpView[view]->GetCamera()->GetCameraToWorldMatrix().Row(3);
            for(int rearIndex=0;rearIndex<rearCount;++rearIndex)
            {
                Vehicle* candidate=rearList[rearIndex];
                if(!candidate || !candidate->mGeometryVehicle ||
                   (!candidate->mGeometryVehicle->AreBrakeLightsOn() &&
                    !candidate->mReverseLightsOn)) continue;
                rmt::Vector delta=candidate->GetTransform().Row(3)-eyeWorld;
                const float distance=delta.MagnitudeSqr();
                for(int slot=0;slot<4;++slot)
                {
                    if(distance>=nearestDistance[slot]) continue;
                    for(int move=3;move>slot;--move)
                    {
                        nearestDistance[move]=nearestDistance[move-1];
                        nearestRear[move]=nearestRear[move-1];
                    }
                    nearestDistance[slot]=distance;
                    nearestRear[slot]=candidate;
                    break;
                }
            }
            int rearLightCount=0;
            const rmt::Matrix& worldToEye=mpView[view]->GetCamera()->GetWorldToCameraMatrix();
            // Two closest cars are enough for the small rear-light volumes and
            // halve the worst-case fragment cost on standalone VR (2/4 lights
            // for Optimized/Max instead of 4/8).
            for(int vehicleSlot=0;vehicleSlot<2;++vehicleSlot)
            {
                Vehicle* rearVehicle=nearestRear[vehicleSlot];
                if(!rearVehicle || !rearVehicle->mGeometryVehicle) continue;
                rmt::Vector rearWorld[2];
                if(!rearVehicle->mGeometryVehicle->GetRearLightWorldPositions(
                       rearVehicle->mReverseLightsOn,rearWorld)) continue;
                rmt::Vector rearEye[2];
                rmt::Vector rearWorldDirection;
                rearVehicle->GetTransform().RotateVector(rmt::Vector(0.0f,-0.22f,-1.0f),&rearWorldDirection);
                rearWorldDirection.Normalize();
                rmt::Vector rearEyeDirection;
                worldToEye.RotateVector(rearWorldDirection,&rearEyeDirection);
                rearEyeDirection.Normalize();
                // rmt::Matrix::Transform uses the affine translation layout
                // opposite to the one uploaded as the GLES view matrix here.
                // Transforming an absolute world point consequently left most
                // of the world translation in the result (hundreds of units),
                // while paintPosition is camera-relative.  Rotate an explicit
                // camera-relative delta instead so both sides use eye space.
                const rmt::Vector cameraWorld=mpView[view]->GetCamera()->GetCameraToWorldMatrix().Row(3);
                worldToEye.RotateVector(rearWorld[0]-cameraWorld,&rearEye[0]);
                worldToEye.RotateVector(rearWorld[1]-cameraWorld,&rearEye[1]);
                if(rearMode==1)
                {
                    const rmt::Vector midpoint=(rearEye[0]+rearEye[1])*0.5f;
                    rearPositions[rearLightCount*3]=midpoint.x;
                    rearPositions[rearLightCount*3+1]=midpoint.y;
                    rearPositions[rearLightCount*3+2]=midpoint.z;
                    rearDirections[rearLightCount*3]=rearEyeDirection.x;
                    rearDirections[rearLightCount*3+1]=rearEyeDirection.y;
                    rearDirections[rearLightCount*3+2]=rearEyeDirection.z;
                    ++rearLightCount;
                }
                else
                {
                    for(int light=0;light<2 && rearLightCount<8;++light,++rearLightCount)
                    {
                        rearPositions[rearLightCount*3]=rearEye[light].x;
                        rearPositions[rearLightCount*3+1]=rearEye[light].y;
                        rearPositions[rearLightCount*3+2]=rearEye[light].z;
                        rearDirections[rearLightCount*3]=rearEyeDirection.x;
                        rearDirections[rearLightCount*3+1]=rearEyeDirection.y;
                        rearDirections[rearLightCount*3+2]=rearEyeDirection.z;
                    }
                }
            }
            if(rearLightCount==0) rearMode=0;
            pglSetVehicleRearLights(rearMode,rearLightCount,rearPositions,rearDirections,rearColour);
            SharOpenXR::RecordRenderSection(5,
                (radTimeGetMicroseconds64()-vrSetupStart)/1000.0);

            // Update one 128x128 face every other frame. A complete probe
            // refresh still takes only about 167 ms at 72 Hz, while halving
            // the extra world submissions on the standalone headset. The
            // player vehicle is omitted by WorldScene to avoid
            // sampling from the cubemap attachment currently being written.
            Avatar* cubeAvatar=GetAvatarManager()->GetAvatarForPlayer(0);
            static bool cubeCaptureTurn=false;
            if(!SharOpenXR::IsRightEyeRendering())
                cubeCaptureTurn=!cubeCaptureTurn;
            if(SharOpenXR::IsVrModeEnabled() && cubeAvatar &&
               !SharOpenXR::IsRightEyeRendering() && cubeCaptureTurn)
            {
                static tPointCamera* cubeCamera=NULL;
                static int cubeFace=0;
                static rmt::Vector cubeCapturePosition(0.0f,0.0f,0.0f);
                if(!cubeCamera)
                {
                    cubeCamera=new tPointCamera;
                    cubeCamera->AddRef();
                    cubeCamera->SetFOV(rmt::PI_BY2,1.0f);
                    cubeCamera->SetNearPlane(0.6f);
                    // WorldSphere geometry is deliberately enormous (level
                    // one cloud joints extend beyond 1700 units). Match the
                    // normal VR world range so the authored sky survives the
                    // cubemap projection; spatial-tree culling still limits
                    // ordinary level geometry to its gameplay draw distance.
                    cubeCamera->SetFarPlane(8000.0f);
                }
                static const rmt::Vector directions[6]={
                    rmt::Vector(1,0,0),rmt::Vector(-1,0,0),
                    rmt::Vector(0,1,0),rmt::Vector(0,-1,0),
                    rmt::Vector(0,0,1),rmt::Vector(0,0,-1)};
                static const rmt::Vector upVectors[6]={
                    rmt::Vector(0,-1,0),rmt::Vector(0,-1,0),
                    rmt::Vector(0,0,1),rmt::Vector(0,0,-1),
                    rmt::Vector(0,-1,0),rmt::Vector(0,-1,0)};
                // Every face in a cycle must share exactly one origin. At
                // driving speed, sampling the moving vehicle position once per
                // face produces six displaced images whose edges look like a
                // literal box in the paint reflection.
                if(cubeFace==0)
                {
                    // Keep the probe fresh while the player is on foot as
                    // well. A parked car must not retain the environment from
                    // the previous drive until the first six in-car frames.
                    cubeAvatar->GetPosition(cubeCapturePosition);
                    cubeCapturePosition.y+=1.0f;
                }
                cubeCamera->SetPosition(cubeCapturePosition);
                cubeCamera->SetTarget(cubeCapturePosition+directions[cubeFace]);
                cubeCamera->SetUpVector(upVectors[cubeFace]);
                if(VrBeginVehicleCubeMapFace(p3d::pddi,cubeFace))
                {
                    // This render happens inside the OpenXR world pass, but it
                    // is not an eye render.  Leaving world rendering enabled
                    // makes SetupHardwareProjection replace the cube camera's
                    // square 90-degree projection with the active eye's
                    // asymmetric projection.  Every face then captures a
                    // narrow, displaced part of the scene instead of its own
                    // cube direction.
                    SharOpenXR::SetWorldRendering(false);
                    cubeCamera->SetState();
                    p3d::context->LoadViewMatrix(
                        cubeCamera->GetWorldToCameraMatrix(),
                        cubeCamera->GetCameraToWorldMatrix());
                    p3dSetEnhancedWorldMaterials(false);
                    // WorldSphereDSG centres the sky and orients its cloud
                    // billboards from the camera stored on the active tView.
                    // Point it at the probe camera for this private pass.
                    tView* cubeView=p3d::context->GetView();
                    tCamera* eyeCamera=mpView[view]->GetCamera();
                    cubeView->SetCamera(cubeCamera);
                    p3d::pddi->EnableZBuffer(false);
                    for(int sphere=0;sphere<mWorldSpheres.mUseSize;++sphere)
                        mWorldSpheres[sphere]->Display();
                    p3d::pddi->EnableZBuffer(true);
                    // Preserve the authored sky and cloud layers above, but
                    // omit alpha-tested foliage and blended world effects from
                    // the expensive environment probe.
                    VrSetVehicleCubeMapTransparentSuppression(true);
                    mpWorldScene->RenderFromCamera(cubeCamera,
                                                   WorldScene::msVisible2);
                    mpWorldScene->RenderOpaque();
                    VrSetVehicleCubeMapTransparentSuppression(false);
                    cubeView->SetCamera(eyeCamera);
                    VrEndVehicleCubeMapFace(p3d::pddi,cubeFace);
                    SharOpenXR::SetWorldRendering(true);
                    eyeCamera->SetState();
                    p3d::context->LoadViewMatrix(
                        eyeCamera->GetWorldToCameraMatrix(),
                        eyeCamera->GetCameraToWorldMatrix());
                    // The probe used ordinary mono programs. Re-establish a
                    // multiview native program before any cached/direct world
                    // draw can continue into only the left framebuffer layer.
                    VrRestoreVehicleCubeMapRendering(p3d::pddi);
                    p3dSetEnhancedWorldMaterials(enhancedMaterials);
                    cubeFace=(cubeFace+1)%6;
                }
            }
#endif

            int i;
            
            if(!mMirror)
            {
                BEGIN_PROFILE( "Render World Spheres" );
                p3d::pddi->EnableZBuffer(false);
                for(i=0; i<mWorldSpheres.mUseSize; i++)
                {
                    mWorldSpheres[i]->Display();
                }
                BEGIN_PROFILE( "pddi ZBuf" );
                p3d::pddi->EnableZBuffer(true);
                END_PROFILE( "pddi ZBuf" );
                END_PROFILE( "Render World Spheres" );
            }

            BEGIN_PROFILE( "Render WorldScene" );
#if defined(RAD_ANDROID)
            const radTime64 vrSceneStart=radTimeGetMicroseconds64();
            // Both eyes use the same midpoint VR culling camera. Rebuilding
            // and sorting identical visibility lists for the right eye was a
            // large single-threaded CPU cost; retain the left-eye lists and
            // submit them again with the right-eye view/projection matrices.
            if(!SharOpenXR::IsRightEyeRendering() || GetNumViews()>1)
#endif
                mpWorldScene->Render( view );
#if defined(RAD_ANDROID)
            SharOpenXR::RecordRenderSection(6,
                (radTimeGetMicroseconds64()-vrSceneStart)/1000.0);
#endif
#ifdef DEBUGWATCH
            mDebugInnerRenderTime = radTimeGetMicroseconds()-mDebugInnerRenderTime;
#endif
            END_PROFILE( "Render WorldScene" );

#if defined(RAD_ANDROID)
            // Generate three world-locked cascades once per XR frame. The
            // second eye reuses their depth maps and only updates matrices.
            tCamera* shadowEyeCamera=mpView[view]->GetCamera();
            const bool csmAllowed=SharOpenXR::IsCsmEnabled();
            const radTime64 vrCsmStart=radTimeGetMicroseconds64();
            const float casterHalfWidths[3]={24.0f,56.0f,224.0f};
            const float casterHalfDepths[3]={64.0f,96.0f,155.0f};
            for(int cascadeIndex=0;csmAllowed && cascadeIndex<3;++cascadeIndex)
            {
                rmt::Matrix lightWorldToCamera,lightCameraToWorld;
                if(VrBeginSunShadowMap(p3d::pddi,cascadeIndex,
                       shadowEyeCamera->GetCameraToWorldMatrix(),
                       &lightWorldToCamera,&lightCameraToWorld))
                {
                    p3d::context->LoadViewMatrix(lightWorldToCamera,
                                                 lightCameraToWorld);
                    // The near map is dynamic-only and cleared every frame.
                    // Static casters live in cached mid/far maps and are merged
                    // with it while sampling, avoiding a full static replay at
                    // headset refresh rate.
                    mpWorldScene->RenderCsmCasters(cascadeIndex>0,
                                                   cascadeIndex==0,
                                                   lightWorldToCamera,
                                                   casterHalfWidths[cascadeIndex],
                                                   casterHalfDepths[cascadeIndex]);
                    if(cascadeIndex==0)
                    {
                        GetCoinManager()->RenderCsmCasters();
                        CharacterManager* characters=GetCharacterManager();
                        for(int characterIndex=0;
                            characters && characterIndex<characters->GetMaxCharacters();
                            ++characterIndex)
                        {
                            Character* character=characters->GetCharacter(characterIndex);
                            if(character) character->DisplayCsmCaster();
                        }

                        // Dynamic vehicles are already present in the world
                        // scene's CSM caster list.  Replaying the active list
                        // here submitted every vehicle a second time (the
                        // player car measured 25 main draws versus 50 CSM
                        // draws) without adding any shadow information.
                    }
                    VrEndSunShadowMap(p3d::pddi,cascadeIndex,
                        shadowEyeCamera->GetCameraToWorldMatrix());
                    p3d::context->LoadViewMatrix(
                        shadowEyeCamera->GetWorldToCameraMatrix(),
                        shadowEyeCamera->GetCameraToWorldMatrix());
                }
            }
            SharOpenXR::RecordRenderSection(10,
                (radTimeGetMicroseconds64()-vrCsmStart)/1000.0);
#endif

            //p3d::inventory->PushSection();
            //p3d::inventory->SelectSection("Default");

#if defined(RAD_ANDROID)
            VrEnableSunShadowReceivers(p3d::pddi,csmAllowed);
            const radTime64 vrOpaqueStart=radTimeGetMicroseconds64();
#endif
            mpWorldScene->RenderOpaque();

#if defined(RAD_ANDROID)
            SharOpenXR::RecordRenderSection(7,
                (radTimeGetMicroseconds64()-vrOpaqueStart)/1000.0);
            // CSM is sampled by the normal opaque shader. Keep transparent
            // foliage, glass, particles and later UI passes on the legacy path.
            VrEnableSunShadowReceivers(p3d::pddi,false);
#endif

            BEGIN_PROFILE( "Render coins" );
            GetCoinManager()->Render();
            END_PROFILE( "Render coins" );

            // VolShadows
            /*
            BEGIN_PROFILE( "Render Shadows" );
            // let's draw ourselves some shadows
            mpShadowGenerator->Begin();
            mpWorldScene->RenderShadows();
            tColour washColour(gWashColourR, gWashColourG, gWashColourB);
            mpShadowGenerator->SetWashColour(washColour);
            mpShadowGenerator->End();
            END_PROFILE( "Render Shadows" );
            */
            GetFootprintManager()->Render();
            BEGIN_PROFILE( "Render Simple Shadows" );
#if defined(RAD_ANDROID)
            if(SharOpenXR::IsVrModeEnabled())
            {
                // Without CSM, retain the original blob shadows except for
                // the vehicle surrounding the first-person camera. With CSM,
                // keep only character blobs and discard every old vehicle or
                // prop shadow so it cannot double the dynamic sun shadow.
                Avatar* shadowAvatar=GetAvatarManager()->GetAvatarForPlayer(0);
                Vehicle* playerVehicle=shadowAvatar?shadowAvatar->GetVehicle():NULL;
                mpWorldScene->RenderSimpleShadows(csmAllowed,
                                                   csmAllowed?NULL:playerVehicle);
            }
            else
            {
                mpWorldScene->RenderSimpleShadows();
            }
#else
            mpWorldScene->RenderSimpleShadows();
#endif
            END_PROFILE( "Render Simple Shadows" );

#if defined(RAD_ANDROID)
            // The following sorted pass contains glass, particles and foliage.
            // Vehicles opt their opaque body groups back into mode 2 locally.
            p3dSetEnhancedWorldMaterials(false);
#endif

            //Temp Disable BBQ optimisation for cars, as it may be outstripped by
            // Kevin's fix to the art
            BEGIN_PROFILE( "Render Shadow Casters" );
            //mpWorldScene->RenderShadowCasters();
            END_PROFILE( "Render Shadow Casters" );

            BEGIN_PROFILE( "RenderTranslucent" );
#if defined(RAD_ANDROID)
            const radTime64 vrTranslucentStart=radTimeGetMicroseconds64();
#endif
            mpWorldScene->RenderTranslucent();
#if defined(RAD_ANDROID)
            SharOpenXR::RecordRenderSection(8,
                (radTimeGetMicroseconds64()-vrTranslucentStart)/1000.0);
#endif
            END_PROFILE( "RenderTranslucent" );

            BillboardQuadManager::Enable();
            BEGIN_PROFILE( "BBQ Display All" );
            GetBillboardQuadManager()->DisplayAll();
            END_PROFILE( "BBQ Display All" );
            BillboardQuadManager::Disable();

            //Drawing the characters and stuff after the shadows to attempt to 
            //eliminate the bleeding shadows.
            BEGIN_PROFILE( "Render Guts" );
#if defined(RAD_ANDROID)
            const radTime64 vrGutsStart=radTimeGetMicroseconds64();
            // Character shaders use PDDI_BLEND_ALPHA even when fully opaque.
            // Apply CSM during their normal skinned colour pass. Replaying a
            // second receiver pass rebuilt some level-specific skin buffers
            // and could explode their vertices into long coloured strips.
            VrEnableSunShadowReceivers(p3d::pddi,csmAllowed);
            p3dSetEnhancedWorldMaterials(enhancedMaterials);
#endif
            for(i = mpGuts.mUseSize-1; i>-1; i-- )
            {
                mpGuts[i]->Display();
            }
#if defined(RAD_ANDROID)
            SharOpenXR::RecordRenderSection(9,
                (radTimeGetMicroseconds64()-vrGutsStart)/1000.0);
#endif
            END_PROFILE( "Render Guts" );

#if defined(RAD_ANDROID)
            VrEnableSunShadowReceivers(p3d::pddi,false);
            // Apply AO only after opaque world geometry, vehicle composite
            // drawables and characters have all populated the eye depth
            // buffer. UI and the later CSM receiver overlay remain untouched.
            SharOpenXR::ApplyGtao();

            // Shadow receiver replay and every later debug/GUI-adjacent pass
            // must use the original shaders.
            p3dSetEnhancedWorldMaterials(false);
            // Opaque vehicle groups receive CSM in their normal colour draw.
            // The former composite overlay duplicated every body mesh here.
#endif

            BEGIN_PROFILE( "RenderSparkles" );
            // Draw procedural transparency after the CSM receiver overlay.
            // Otherwise the overlay is blended on top of vehicle damage
            // smoke, making the vehicle's cascaded shadow visible through it.
#if defined(RAD_ANDROID)
            GetSparkleManager()->Render(SharOpenXR::IsVrModeEnabled() ?
                                        Sparkle::SRM_IncludeSorted :
                                        Sparkle::SRM_ExcludeSorted);
#else
            GetSparkleManager()->Render( Sparkle::SRM_ExcludeSorted );
#endif
            END_PROFILE( "RenderSparkles" );

            BEGIN_PROFILE( "Render Trigger Volume Tracker" );
            GetTriggerVolumeTracker()->Render();
            END_PROFILE( "Render Trigger Volume Tracker" );
        
#ifdef DEBUGWATCH
            BEGIN_PROFILE( "Render Vehicle AI Debug" );
            VehicleAIRender::GetVehicleAIRender()->Display();
            END_PROFILE( "Render Vehicle AI Debug" );
#endif

            BEGIN_PROFILE( "Lens Flare Render" );
            LensFlareDSG::DisplayAllFlares();
            END_PROFILE( "Lens Flare Render" );

#if defined(RAD_ANDROID)
            // Do not project the world shadow map onto subsequent GUI layers.
            VrEnableSunShadowReceivers(p3d::pddi,false);

            // Keep controller hands inside the active tView.  EndRender tears
            // down the level lights, which made hands submitted later by the
            // RenderManager flat and much darker than character geometry.
            if(mirrorPass==0)
                SharOpenXR::RenderControllerHands(mpView[view]->GetCamera());
#endif

            BEGIN_PROFILE( "View End Render" );
            mpView[ view ]->EndRender();
            END_PROFILE( "View End Render" );

            if(mirrorPass == 1)
            {
                tCamera * camera = mpView[ view ]->GetCamera();
                camera->SetCameraMatrix(&originalCamera);
            }
            if(GetCheatInputSystem()->IsCheatEnabled(CHEAT_ID_SHOW_TREE))
            {
                rmt::Vector posn, pTriPts[3], intPosn, intNorm;
                IntersectDSG* pIntersectChunk;
                AvatarManager::GetInstance()->GetAvatarForPlayer(0)->GetPosition(posn);
                pIntersectChunk = GetIntersectManager()->FindIntersectionTri(posn,pTriPts,intPosn,intNorm);
                if(pIntersectChunk!=NULL)
                {
                    pddiCompareMode origZCompare = p3d::pddi->GetZCompare();
                    p3d::pddi->SetZCompare(PDDI_COMPARE_ALWAYS);
                    pIntersectChunk->Display();
                    pIntersectChunk->DrawTri(pTriPts, tColour(255,0,0));
                    p3d::pddi->SetZCompare(origZCompare);
                }
            }
        }
    }
#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds()-mDebugRenderTime;
#endif
    END_PROFILE( "WRL Render" );
}


//////////////////////////////////////////////////////////////////////////
// Resource Interface
//////////////////////////////////////////////////////////////////////////
//========================================================================
// WorldRenderLayer::AddGuts
//========================================================================
//
// Description: Add a tDrawable
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
//void WorldRenderLayer::AddGuts( tDrawable* ipDrawable )
//{
//   // This is a currently unsupported function
//   rAssert(false);
//}

//========================================================================
// WorldRenderLayer::AddGuts
//========================================================================
//
// Description: Add a tGeometry
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
//void WorldRenderLayer::AddGuts( tGeometry* ipGeometry )
//{
//   mpWorldScene->Add( ipGeometry );
//
//}

//========================================================================
// WorldRenderLayer::AddGuts
//========================================================================
//
// Description: add an IntersectDSG
//
// Parameters:  IntersectDSG to add
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void WorldRenderLayer::AddGuts( IntersectDSG* ipIntersectDSG )
{
   mpWorldScene->Add( ipIntersectDSG );

   if(mDynaLoadState==msLoad)
   {
      mLoadLists[mCurLoadIndex]->mIntersectElems.Add(ipIntersectDSG);      
   }
   else
   {
      rAssert(mDynaLoadState==msPreLoads);
   }
}
//========================================================================
// RenderLayer::
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
void WorldRenderLayer::AddGuts( StaticEntityDSG* ipStaticEntityDSG )
{
   mpWorldScene->Add( ipStaticEntityDSG );

   if(mDynaLoadState==msLoad)
   {
      mLoadLists[mCurLoadIndex]->mSEntityElems.Add(ipStaticEntityDSG);      
   }
   else
   {
      rAssert(mDynaLoadState==msPreLoads);
   }
}
//========================================================================
// RenderLayer::
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
void WorldRenderLayer::AddGuts( StaticPhysDSG* ipStaticPhysDSG )
{
   mpWorldScene->Add( ipStaticPhysDSG );

   if(mDynaLoadState==msLoad)
   {
      mLoadLists[mCurLoadIndex]->mSPhysElems.Add(ipStaticPhysDSG);      
   }
   else
   {
      rAssert(mDynaLoadState==msPreLoads);
   }
}
//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::AddGuts( FenceEntityDSG* ipFenceEntityDSG )
{
   mpWorldScene->Add( ipFenceEntityDSG );

   if(mDynaLoadState==msLoad)
   {
      mLoadLists[mCurLoadIndex]->mFenceElems.Add(ipFenceEntityDSG);      
   }
   else
   {
      rAssert(mDynaLoadState==msPreLoads);
   }
}
//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::AddGuts( AnimCollisionEntityDSG* ipAnimCollDSG )
{
   mpWorldScene->Add( ipAnimCollDSG );

   if(mDynaLoadState==msLoad)
   {
      mLoadLists[mCurLoadIndex]->mAnimCollElems.Add(ipAnimCollDSG); 
	  // Add it to a list of managed animentitydsgs so that Update can be called on it every frame
      GetAnimEntityDSGManager()->Add( ipAnimCollDSG );
   }
   else
   {
      rAssert(mDynaLoadState==msPreLoads);
   }
}
//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::AddGuts( AnimEntityDSG* ipAnimDSG )
{
   mpWorldScene->Add( ipAnimDSG );

   if(mDynaLoadState==msLoad)
   {
      mLoadLists[mCurLoadIndex]->mAnimElems.Add(ipAnimDSG);    
	  // Add it to a list of managed animentitydsgs so that Update can be called on it every frame
	  GetAnimEntityDSGManager()->Add( ipAnimDSG );
   }
   else
   {
      rAssert(mDynaLoadState==msPreLoads);
   }
}
//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::AddGuts( DynaPhysDSG* ipDynaPhysDSG )
{
   mpWorldScene->Add( ipDynaPhysDSG );

   if(mDynaLoadState==msLoad)
   {
      mLoadLists[mCurLoadIndex]->mDPhysElems.Add(ipDynaPhysDSG);      
   }
   else
   {
      rAssert(mDynaLoadState==msPreLoads);
   }
}
//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::AddGuts( TriggerVolume* ipTriggerVolume )
{
   mpWorldScene->Add( ipTriggerVolume );

   if(mDynaLoadState==msLoad)
   {
      mLoadLists[mCurLoadIndex]->mTrigVolElems.Add(ipTriggerVolume);      
   }
   else
   {

       rAssert(mDynaLoadState==msPreLoads);
   }
}

//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::AddGuts( RoadSegment* ipRoadSegment )
{
   mpWorldScene->Add( ipRoadSegment );

   if(mDynaLoadState==msLoad)
   {
      mLoadLists[mCurLoadIndex]->mRoadSegmentElems.Add(ipRoadSegment);      
   }
   else
   {
      rAssert(mDynaLoadState==msPreLoads);
   }
}


//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::AddGuts( PathSegment* ipPathSegment )
{
   mpWorldScene->Add( ipPathSegment );

   if(mDynaLoadState==msLoad)
   {
      mLoadLists[mCurLoadIndex]->mPathSegmentElems.Add(ipPathSegment);      
   }
   else
   {
      rAssert(mDynaLoadState==msPreLoads);
   }
}


//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::AddGuts( WorldSphereDSG* ipWorldSphereDSG )
{
   ipWorldSphereDSG->AddRef();
   mWorldSpheres.Add( ipWorldSphereDSG );

   if(mDynaLoadState==msLoad)
   {
      mLoadLists[mCurLoadIndex]->mWorldSphereElems.Add(ipWorldSphereDSG);      
   }
   else
   {
      rAssert(mDynaLoadState==msPreLoads);
   }
}

//=============================================================================
// WorldRenderLayer::GetCurSectionName
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      tName
//
//=============================================================================
tName& WorldRenderLayer::GetCurSectionName()
{
    return mLoadLists[mCurLoadIndex]->mGiveItAFuckinName;
}

//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::RemoveGuts( IEntityDSG* ipEDSG )
{
   rmt::Box3D BBox;
   BoxPts     BBoxSP;

   int i,j;
   for(i=0;i<mLoadLists.mUseSize;i++ )
   {
     //////////////////////////////////////////////////////////////////////////
     // DPhys
     //////////////////////////////////////////////////////////////////////////
     for(j=0;j<mLoadLists[i]->mDPhysElems.mUseSize;j++)
     {
        if(ipEDSG == mLoadLists[i]->mDPhysElems[j])
        {
            mLoadLists[i]->mDPhysElems.Remove(j);
            mpWorldScene->Remove(ipEDSG);
            return;
        }
     }
     //////////////////////////////////////////////////////////////////////////
     // Intersect
     //////////////////////////////////////////////////////////////////////////
     for(j=0;j<mLoadLists[i]->mIntersectElems.mUseSize;j++)
     {
        if(ipEDSG == mLoadLists[i]->mIntersectElems[j])
        {
            mLoadLists[i]->mIntersectElems.Remove(j);
            mpWorldScene->Remove(ipEDSG);
            return;
        }
     }
     //////////////////////////////////////////////////////////////////////////
     // SEntity
     //////////////////////////////////////////////////////////////////////////
     for(j=0;j<mLoadLists[i]->mSEntityElems.mUseSize;j++)
     {
        if(ipEDSG == mLoadLists[i]->mSEntityElems[j])
        {
            mLoadLists[i]->mSEntityElems.Remove(j);
            mpWorldScene->Remove(ipEDSG);
            return;
        }
     }
     //////////////////////////////////////////////////////////////////////////
     // SPhys
     //////////////////////////////////////////////////////////////////////////
     for(j=0;j<mLoadLists[i]->mSPhysElems.mUseSize;j++)
     {
        if(ipEDSG == mLoadLists[i]->mSPhysElems[j])
        {
            mLoadLists[i]->mSPhysElems.Remove(j);
            mpWorldScene->Remove(ipEDSG);
            return;
        }
     }
     //////////////////////////////////////////////////////////////////////////
     // AnimColl
     //////////////////////////////////////////////////////////////////////////
     for(j=0;j<mLoadLists[i]->mAnimCollElems.mUseSize;j++)
     {
        if(ipEDSG == mLoadLists[i]->mAnimCollElems[j])
        {
            mLoadLists[i]->mAnimCollElems.Remove(j);
            mpWorldScene->Remove(ipEDSG);
            return;
        }
     }
     //////////////////////////////////////////////////////////////////////////
     // Anim
     //////////////////////////////////////////////////////////////////////////
     for(j=0;j<mLoadLists[i]->mAnimElems.mUseSize;j++)
     {
        if(ipEDSG == mLoadLists[i]->mAnimElems[j])
        {
            mLoadLists[i]->mAnimElems.Remove(j);
            mpWorldScene->Remove(ipEDSG);
            return;
        }
     }
     //////////////////////////////////////////////////////////////////////////
     // Fences
     //////////////////////////////////////////////////////////////////////////
     for(j=0;j<mLoadLists[i]->mFenceElems.mUseSize;j++)
     {
        if(ipEDSG == mLoadLists[i]->mFenceElems[j])
        {
            mLoadLists[i]->mFenceElems.Remove(j);
            mpWorldScene->Remove(ipEDSG);
            return;
        }
     }
     //////////////////////////////////////////////////////////////////////////
     // Trigger Volumes
     //////////////////////////////////////////////////////////////////////////
     for(j=0;j<mLoadLists[i]->mTrigVolElems.mUseSize;j++)
     {
        if(ipEDSG == mLoadLists[i]->mTrigVolElems[j])
        {
            mLoadLists[i]->mTrigVolElems.Remove(j);
            mpWorldScene->Remove(ipEDSG);
            return;
        }
     }
     //////////////////////////////////////////////////////////////////////////
     // Road Segments
     //////////////////////////////////////////////////////////////////////////
     for(j=0;j<mLoadLists[i]->mRoadSegmentElems.mUseSize;j++)
     {
        if(ipEDSG == mLoadLists[i]->mRoadSegmentElems[j])
        {
            mLoadLists[i]->mRoadSegmentElems.Remove(j);
            mpWorldScene->Remove(ipEDSG);
            return;
        }
     }
     //////////////////////////////////////////////////////////////////////////
     // Path Segments
     //////////////////////////////////////////////////////////////////////////
     for(j=0;j<mLoadLists[i]->mPathSegmentElems.mUseSize;j++)
     {
        if(ipEDSG == mLoadLists[i]->mPathSegmentElems[j])
        {
            mLoadLists[i]->mPathSegmentElems.Remove(j);
            mpWorldScene->Remove(ipEDSG);
            return;
        }
     }
   }   

   //Item ipEDSG was not found in any of the dynaloadlists
   rTuneAssert(false);
}


//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::AddGuts( SpatialTree* ipSpatialTree )
{
   mpWorldScene->SetTree(ipSpatialTree);
}
//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::ActivateWS(tUID iUID)
{
    for(int i=mWorldSpheres.mUseSize-1; i>-1; i--)
    {
        if(mWorldSpheres[i]->GetUID() == iUID)
        {
            mWorldSpheres[i]->Activate();
            return;
        }
    }
    //rAssert(false);
}
//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::DeactivateWS(tUID iUID)
{
    for(int i=mWorldSpheres.mUseSize-1; i>-1; i--)
    {
        if(mWorldSpheres[i]->GetUID() == iUID)
        {
            mWorldSpheres[i]->Deactivate();
            return;
        }
    }
   // rAssert(false);
}
//========================================================================
// WorldRenderLayer::NullifyGuts()
//========================================================================
//
// Description: Get rid of it all! Burn. it. all. down.
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void WorldRenderLayer::NullifyGuts()
{
   RenderLayer::NullifyGuts();
//   RenderLayer::DumpGuts();

   DumpAllDynaLoads();

   if( mpWorldScene != NULL )
   {
      delete mpWorldScene;
      mpWorldScene = NULL;
   }

   mLoadLists.Clear();
   mStaticLoadLists.Clear();
   mWorldSpheres.Clear();

   mDynaLoadState = msPreLoads;
   mCurLoadIndex  = -1;
}

//========================================================================
// WorldRenderLayer::SetUpGuts()
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
void WorldRenderLayer::SetUpGuts()
{
MEMTRACK_PUSH_GROUP( "WorldRenderLayer" );
   HeapMgr()->PushHeap (GMA_LEVEL_ZONE);

   RenderLayer::SetUpGuts();
   rAssert( mpWorldScene == NULL );
   mpWorldScene = new WorldScene;
   mpWorldScene->Init(msMaxGuts);

   mStaticLoadLists.Allocate(7);
   mLoadLists.Allocate(7);

   mLoadLists.AddUse(7);
   mStaticLoadLists.AddUse(7);

   mWorldSpheres.Allocate(10);

   mDynaLoadState = msPreLoads;
   mnLoadListRefs = 2000;
   mCurLoadIndex  = -1;

   int i;
   for(i=0;i<7;i++)
   {
      mStaticLoadLists[i].AllocateAll(mnLoadListRefs);
      mLoadLists[i] = &(mStaticLoadLists[i]);
   }

   mLoadLists.ClearUse();


   HeapMgr()->PopHeap (GMA_LEVEL_ZONE);
MEMTRACK_POP_GROUP( "WorldRenderLayer" );
}

//************************************************************************
// Load-Related Interface
//************************************************************************
void WorldRenderLayer::DoPreStaticLoad()
{
   if( !IsGutsSetup() )
      SetUpGuts();

   mExportedState = msFrozen;
}

void WorldRenderLayer::DoPostStaticLoad()
{
//   mpWorldScene->GenerateSpatialReps();
}
//========================================================================
// WorldRenderLayer::
//========================================================================
//
// Description: 
//
// Parameters:  unsigned int start.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void WorldRenderLayer::DumpAllDynaLoads( unsigned int start, SwapArray<tRefCounted*>& irEntityDeletionList )
{
    GetEventManager()->TriggerEvent( EVENT_INTERIOR_DUMPED );

    rTuneAssert(mQdDump==false);

    if(mDynaLoadState==msLoad)
    {
        rReleasePrintf("-=-=-=-=-=-=-=-Dump Queued-=-=-=-=-=-=-=-\n");
        mQdDump = true;
        mQdDeletionStart = start;
        mpQdDeletionList = &irEntityDeletionList;
    }
    else
    {
        while( mLoadLists.mUseSize > static_cast<int>( start ) )
        {
            DumpDynaLoad(mLoadLists[start]->mGiveItAFuckinName, irEntityDeletionList );
        }
    }
}
//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::DumpDynaLoad(tName& irGiveItAFuckinName, SwapArray<tRefCounted*>& irEntityDeletionList)
{
    //TODO: this is the ugliest, most embarrasing piece of code I've ever written.
    // re-write.

   rmt::Box3D BBox;
   BoxPts     BBoxSP;
 
   int i,j,k;
   for(i=0;i<mLoadLists.mUseSize;i++ )
   {
      if( mLoadLists[i]->mGiveItAFuckinName.GetUID() == irGiveItAFuckinName.GetUID() )
      {
BEGIN_PROFILE( "Remove Searches" );

         GetEventManager()->TriggerEvent( EVENT_DUMP_DYNA_SECTION, (void*)(&(mLoadLists[i]->mGiveItAFuckinName)) );
         //////////////////////////////////////////////////////////////////////////
         // WorldSpheres
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mWorldSphereElems.mUseSize;j++)
         {
             for(k=0;k<mWorldSpheres.mUseSize;k++)
             {
                 if(mLoadLists[i]->mWorldSphereElems[j] == mWorldSpheres[k])
                 {
                     irEntityDeletionList.Add((tRefCounted*&)mWorldSpheres[k]);
                     //mWorldSpheres[k]->Release();
                     mWorldSpheres.Remove(k);
                     k = mWorldSpheres.mUseSize +10;
                 }
             }
             rAssert( k= mWorldSpheres.mUseSize+10 );
         }

         //////////////////////////////////////////////////////////////////////////
         // DPhys
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mDPhysElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mDPhysElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
                */
            SpatialNode& rSNode = *(mLoadLists[i]->mDPhysElems[j]->mpSpatialNode);


            for(k=0;k<rSNode.mDPhysElems.mUseSize;k++)
            {
               if( rSNode.mDPhysElems[k] == mLoadLists[i]->mDPhysElems[j] )
               {
                  irEntityDeletionList.Add((tRefCounted*&)rSNode.mDPhysElems[k]);
                  GetWorldPhysicsManager()->RemoveFromAnyOtherCurrentDynamicsListAndCollisionArea(rSNode.mDPhysElems[k]);
                  rSNode.mDPhysElems.Remove(k);
                  k = -1;
                  break;
               }
            }

            if(k!=-1)
            {
                rAssert(false);
                //it might be in the root node, which now doubles as overflow...
            
                SpatialNode& rRootNode = mpWorldScene->mStaticTreeWalker.rIthNode(0);

                for(k=0;k<rRootNode.mDPhysElems.mUseSize;k++)
                {
                    if( rRootNode.mDPhysElems[k] == mLoadLists[i]->mDPhysElems[j] )
                    {
                        irEntityDeletionList.Add((tRefCounted*&)rRootNode.mDPhysElems[k]);
                        GetWorldPhysicsManager()->RemoveFromAnyOtherCurrentDynamicsListAndCollisionArea(rRootNode.mDPhysElems[k]);
                        rRootNode.mDPhysElems.Remove(k);
                        k = -1;
                        break;
                    }
                }
            }
           //Cant find the dynaphys where the bbox says it is
            rAssert(k==-1);
       }
         //////////////////////////////////////////////////////////////////////////
         // SEntity
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mSEntityElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mSEntityElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
            */
            SpatialNode& rSNode = *(mLoadLists[i]->mSEntityElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mSEntityElems.mUseSize;k++)
            {
               if( rSNode.mSEntityElems[k] == mLoadLists[i]->mSEntityElems[j] )
               {
                  irEntityDeletionList.Add((tRefCounted*&)rSNode.mSEntityElems[k]);
                  rSNode.mSEntityElems.Remove(k);
                  k = -1;
                  break;
               }
            }
            if(k!=-1)
            {
                rAssert(false);
                //it might be in the root node, which now doubles as overflow...
            
                SpatialNode& rRootNode = mpWorldScene->mStaticTreeWalker.rIthNode(0);

                for(k=0;k<rRootNode.mSEntityElems.mUseSize;k++)
                {
                    if( rRootNode.mSEntityElems[k] == mLoadLists[i]->mSEntityElems[j] )
                    {
                        irEntityDeletionList.Add((tRefCounted*&)rRootNode.mSEntityElems[k]);
                        rRootNode.mSEntityElems.Remove(k);
                        k = -1;
                        break;
                    }
                }
            }
            rAssert(k==-1);
         }
         //////////////////////////////////////////////////////////////////////////
         // AnimColl
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mAnimCollElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mAnimCollElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
                */
            SpatialNode& rSNode = *(mLoadLists[i]->mAnimCollElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mAnimCollElems.mUseSize;k++)
            {
               if( rSNode.mAnimCollElems[k] == mLoadLists[i]->mAnimCollElems[j] )
               {
                  irEntityDeletionList.Add((tRefCounted*&)rSNode.mAnimCollElems[k]);
                  GetAnimEntityDSGManager()->Remove( rSNode.mAnimCollElems[k] );
                  rSNode.mAnimCollElems.Remove(k);
                  k = -1;
                  break;
               }
            }
            if(k!=-1)
            {
                rAssert(false);
                SpatialNode& rRootNode = mpWorldScene->mStaticTreeWalker.rIthNode(0);

                for(k=0;k<rRootNode.mAnimCollElems.mUseSize;k++)
                {
                    if( rRootNode.mAnimCollElems[k] == mLoadLists[i]->mAnimCollElems[j] )
                    {
                        irEntityDeletionList.Add((tRefCounted*&)rRootNode.mAnimCollElems[k]);
                        GetAnimEntityDSGManager()->Remove( rRootNode.mAnimCollElems[k] );
                        rRootNode.mAnimCollElems.Remove(k);
                        k = -1;
                        break;
                    }
                }
            }
            rAssert(k==-1);
         }
         //////////////////////////////////////////////////////////////////////////
         // Anim
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mAnimElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mAnimElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
            */

            SpatialNode& rSNode = *(mLoadLists[i]->mAnimElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mAnimElems.mUseSize;k++)
            {
               if( rSNode.mAnimElems[k] == mLoadLists[i]->mAnimElems[j] )
               {
				   // Remove it from the list of managed animentities
 				   GetAnimEntityDSGManager()->Remove( rSNode.mAnimElems[k] );
                  irEntityDeletionList.Add((tRefCounted*&)rSNode.mAnimElems[k]);
                  rSNode.mAnimElems.Remove(k);
                  k = -1;
                  break;
               }
            }
            if(k!=-1)
            {
                rAssert(false);
                SpatialNode& rRootNode = mpWorldScene->mStaticTreeWalker.rIthNode(0);

                for(k=0;k<rRootNode.mAnimElems.mUseSize;k++)
                {
                    if( rRootNode.mAnimElems[k] == mLoadLists[i]->mAnimElems[j] )
                    {
				        // Remove it from the list of managed animentities
 				        GetAnimEntityDSGManager()->Remove( rRootNode.mAnimElems[k] );
                        irEntityDeletionList.Add((tRefCounted*&)rRootNode.mAnimElems[k]);
                        rRootNode.mAnimElems.Remove(k);
                        k = -1;
                        break;
                    }
                }
            }
            rAssert(k==-1);
         }
         //////////////////////////////////////////////////////////////////////////
         // Intersect
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mIntersectElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mIntersectElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
            */
            SpatialNode& rSNode = *(mLoadLists[i]->mIntersectElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mIntersectElems.mUseSize;k++)
            {
               if( rSNode.mIntersectElems[k] == mLoadLists[i]->mIntersectElems[j] )
               {
                  irEntityDeletionList.Add((tRefCounted*&)rSNode.mIntersectElems[k]);
                  rSNode.mIntersectElems.Remove(k);
                  k = rSNode.mIntersectElems.mUseSize+10;
               }
            }
            rAssert(k==rSNode.mIntersectElems.mUseSize+11);
         }

         //////////////////////////////////////////////////////////////////////////
         // SPhys
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mSPhysElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mSPhysElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
                */
            SpatialNode& rSNode = *(mLoadLists[i]->mSPhysElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mSPhysElems.mUseSize;k++)
            {
               if( rSNode.mSPhysElems[k] == mLoadLists[i]->mSPhysElems[j] )
               {
                  irEntityDeletionList.Add((tRefCounted*&)rSNode.mSPhysElems[k]);
                  //rSNode.mSPhysElems[k]->Release();
    //              rSNode.mSPhysElems[k]->ReleaseVerified();
                  rSNode.mSPhysElems.Remove(k);
                  k = rSNode.mSPhysElems.mUseSize+10;
               }
            }
            rAssert(k==rSNode.mSPhysElems.mUseSize+11);
         }
         //////////////////////////////////////////////////////////////////////////
         // Fences
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mFenceElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mFenceElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
            */
            SpatialNode& rSNode = *(mLoadLists[i]->mFenceElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mFenceElems.mUseSize;k++)
            {
               if( rSNode.mFenceElems[k] == mLoadLists[i]->mFenceElems[j] )
               {
                  irEntityDeletionList.Add((tRefCounted*&)rSNode.mFenceElems[k]);
                  //rSNode.mFenceElems[k]->Release();
    //              rSNode.mSPhysElems[k]->ReleaseVerified();
                  rSNode.mFenceElems.Remove(k);
                  k = rSNode.mFenceElems.mUseSize+10;
               }
            }
            rAssert(k==rSNode.mFenceElems.mUseSize+11);
         }

         //////////////////////////////////////////////////////////////////////////
         // Trigger Volumes
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mTrigVolElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mTrigVolElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
            */
            SpatialNode& rSNode = *(mLoadLists[i]->mTrigVolElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mTrigVolElems.mUseSize;k++)
            {
               if( rSNode.mTrigVolElems[k] == mLoadLists[i]->mTrigVolElems[j] )
               {
                  irEntityDeletionList.Add((tRefCounted*&)rSNode.mTrigVolElems[k]);
                  //rSNode.mTrigVolElems[k]->Release();
                  rSNode.mTrigVolElems.Remove(k);
                  k = rSNode.mTrigVolElems.mUseSize+10;
               }
            }
            rAssert(k==rSNode.mTrigVolElems.mUseSize+11);
         }
         //////////////////////////////////////////////////////////////////////////
         // Road Segments
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mRoadSegmentElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mRoadSegmentElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
            */

            SpatialNode& rSNode = *(mLoadLists[i]->mRoadSegmentElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mRoadSegmentElems.mUseSize;k++)
            {
               if( rSNode.mRoadSegmentElems[k] == mLoadLists[i]->mRoadSegmentElems[j] )
               {
                  irEntityDeletionList.Add((tRefCounted*&)rSNode.mRoadSegmentElems[k]);
                  //rSNode.mRoadSegmentElems[k]->Release();
                  rSNode.mRoadSegmentElems.Remove(k);
                  k = rSNode.mRoadSegmentElems.mUseSize+10;
               }
            }
            rAssert(k==rSNode.mRoadSegmentElems.mUseSize+11);
         }

         //////////////////////////////////////////////////////////////////////////
         // Path Segments
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mPathSegmentElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mPathSegmentElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
            */
            SpatialNode& rSNode = *(mLoadLists[i]->mPathSegmentElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mPathSegmentElems.mUseSize;k++)
            {
               if( rSNode.mPathSegmentElems[k] == mLoadLists[i]->mPathSegmentElems[j] )
               {
                  irEntityDeletionList.Add((tRefCounted*&)rSNode.mPathSegmentElems[k]);
                  //rSNode.mPathSegmentElems[k]->Release();
                  rSNode.mPathSegmentElems.Remove(k);
                  k = rSNode.mPathSegmentElems.mUseSize+10;
               }
            }
            rAssert(k==rSNode.mPathSegmentElems.mUseSize+11);
         }
 
END_PROFILE( "Remove Searches" );

         radTime64 start = radTimeGetMicroseconds64();

BEGIN_PROFILE( "Remove Section Elems" );
         p3d::inventory->RemoveSectionElements(irGiveItAFuckinName.GetUID());
END_PROFILE( "Remove Section Elems" );
BEGIN_PROFILE( "Delete Section" );
         p3d::inventory->DeleteSection(irGiveItAFuckinName.GetUID());
END_PROFILE( "Delete Section" );

         radTime64 end = radTimeGetMicroseconds64();
         unsigned deleteTime = (unsigned) (end - start);

         rTunePrintf("WorldRenderLayer::DumpDynaLoad Delete time: %.3fms\n", deleteTime / 1000.0F);

         HeapMgr()->PushHeap( GMA_LEVEL_OTHER );
BEGIN_PROFILE( "Cleanup" );
		rReleasePrintf("LoadList Dump: %s Num: %d\n", mLoadLists[i]->mGiveItAFuckinName.GetText(), i );
         //mLoadLists[i]->ClearAll();
	     //mLoadLists[i]->AllocateAll(mnLoadListRefs);
         mLoadLists[i]->ClearAllUse();
         mLoadLists.Remove(i);
END_PROFILE( "Cleanup" );
         HeapMgr()->PopHeap(GMA_LEVEL_OTHER);
      }
   }
}
//========================================================================
// WorldRenderLayer::
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
bool WorldRenderLayer::DoPreDynaLoad(tName& irGiveItAFuckinName)//tUID iUID)
{
   int i;
   for(i=0;i<mLoadLists.mUseSize;i++)
   {
      if(mLoadLists[i]->mGiveItAFuckinName.GetUID() == irGiveItAFuckinName.GetUID() )
         return false;
   }

   HeapMgr()->PushHeap (GMA_LEVEL_ZONE);

   if( mDynaLoadState == msLoad )
   {
      rReleasePrintf("ARGH! You're driving TOO FAST!*******************\n");
      rAssert(mDynaLoadState != msLoad);
   }
   else
   {
      rReleasePrintf("*******************Loading*****%s****\n", irGiveItAFuckinName.GetText());
      mLoadLists.AddUse(1);
      mCurLoadUID    = irGiveItAFuckinName.GetUID(); 
	  rReleasePrintf("CurLoadIndex was %d\n", mCurLoadIndex );
      mCurLoadIndex  = mLoadLists.mUseSize-1; 
	  rReleasePrintf("CurLoadIndex is %d\n", mCurLoadIndex );
      mDynaLoadState = msLoad;
      HeapMgr()->PushHeap( GMA_LEVEL_OTHER );
      mLoadLists[mCurLoadIndex]->mGiveItAFuckinName= irGiveItAFuckinName;
      //mLoadLists[mCurLoadIndex]->ClearAll();
      //mLoadLists[mCurLoadIndex]->AllocateAll(mnLoadListRefs);
      mLoadLists[mCurLoadIndex]->ClearAllUse();
      HeapMgr()->PopHeap(GMA_LEVEL_OTHER);
      BillboardWrappedLoader::OverrideLoader( false );
   }

   GetPersistentWorldManager()->OnSectorLoad( irGiveItAFuckinName.GetUID() );

   HeapMgr()->PopHeap (GMA_LEVEL_ZONE);

   return true;
}
//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::DoPostDynaLoad()
{
   rAssert(mDynaLoadState != msNoLoad);
   mDynaLoadState = msNoLoad;
   mCurLoadUID = 0;

   if(mQdDump)
   {
      rReleasePrintf("-=-=-=-=-=-=-=-Queued Dump, Dumped-=-=-=-=-=-=-=-\n");
      mQdDump = false;
      DumpAllDynaLoads(mQdDeletionStart,*mpQdDeletionList);
      GetEventManager()->TriggerEvent( EVENT_ALL_DYNAMIC_ZONES_DUMPED, NULL ); 
   }
   BillboardWrappedLoader::OverrideLoader( true );
}

//========================================================================
// WorldRenderLayer::
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
WorldScene* WorldRenderLayer::pWorldScene()
{
   rAssert( mpWorldScene != NULL );

   return mpWorldScene;
}

//************************************************************************
//
// Protected Member Functions : WorldRenderLayer 
//
//************************************************************************

//************************************************************************
//
// Private Member Functions : WorldRenderLayer 
//
//************************************************************************
//========================================================================
// WorldRenderLayer::IsGutsSetup()
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
bool WorldRenderLayer::IsGutsSetup()
{
   return( mpGuts.IsSetUp() && (mpWorldScene != NULL) );
}
//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::OnWorldRenderLayerInit()
{
   mpWorldScene = NULL;
}
//========================================================================
// WorldRenderLayer::
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
//========================================================================
// WorldRenderLayer::
//========================================================================
//
// Description: 
//
// Parameters:  unsigned int start.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void WorldRenderLayer::DumpAllDynaLoads()
{
    GetEventManager()->TriggerEvent( EVENT_INTERIOR_DUMPED );

    int start = 0;
    while( mLoadLists.mUseSize > start )
    {
        DumpDynaLoad(mLoadLists[start]->mGiveItAFuckinName);
    }
}
//========================================================================
// WorldRenderLayer::
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
void WorldRenderLayer::DumpDynaLoad(tName& irGiveItAFuckinName)
{
   rmt::Box3D BBox;
   BoxPts     BBoxSP;
  
   int i,j,k;
   for(i=0;i<mLoadLists.mUseSize;i++ )
   {
      if( mLoadLists[i]->mGiveItAFuckinName.GetUID() == irGiveItAFuckinName.GetUID() )
      {
         GetEventManager()->TriggerEvent( EVENT_DUMP_DYNA_SECTION, (void*)(&(mLoadLists[i]->mGiveItAFuckinName)) );

         //////////////////////////////////////////////////////////////////////////
         // WorldSpheres
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mWorldSphereElems.mUseSize;j++)
         {
             for(k=0;k<mWorldSpheres.mUseSize;k++)
             {
                 if(mLoadLists[i]->mWorldSphereElems[j] == mWorldSpheres[k])
                 {
                     //irEntityDeletionList.Add(mWorldSpheres[k]);
                     mWorldSpheres[k]->Release();
                     mWorldSpheres.Remove(k);
                     k = mWorldSpheres.mUseSize +10;
                 }
             }
             rAssert( k= mWorldSpheres.mUseSize+10 );
         }

         //////////////////////////////////////////////////////////////////////////
         // DPhys
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mDPhysElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mDPhysElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
                */
            SpatialNode& rSNode = *(mLoadLists[i]->mDPhysElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mDPhysElems.mUseSize;k++)
            {
               if( rSNode.mDPhysElems[k] == mLoadLists[i]->mDPhysElems[j] )
               {
                  GetWorldPhysicsManager()->RemoveFromAnyOtherCurrentDynamicsListAndCollisionArea(rSNode.mDPhysElems[k]);
                  rSNode.mDPhysElems[k]->Release();
                  rSNode.mDPhysElems.Remove(k);
                  k = -1;
                  break;
               }
            }
            if(k!=-1)
            {
                rAssert(false);
                SpatialNode& rRootNode = mpWorldScene->mStaticTreeWalker.rIthNode(0);

                for(k=0;k<rRootNode.mDPhysElems.mUseSize;k++)
                {
                    if( rRootNode.mDPhysElems[k] == mLoadLists[i]->mDPhysElems[j] )
                    {
                        GetWorldPhysicsManager()->RemoveFromAnyOtherCurrentDynamicsListAndCollisionArea(rRootNode.mDPhysElems[k]);
                        rRootNode.mDPhysElems[k]->Release();
                        rRootNode.mDPhysElems.Remove(k);
                        k = -1;
                        break;
                    }
                }
            }
            rAssert(k==-1);
         }
         //////////////////////////////////////////////////////////////////////////
         // SEntity
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mSEntityElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mSEntityElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
                */
            SpatialNode& rSNode = *(mLoadLists[i]->mSEntityElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mSEntityElems.mUseSize;k++)
            {
               if( rSNode.mSEntityElems[k] == mLoadLists[i]->mSEntityElems[j] )
               {
                  rSNode.mSEntityElems[k]->Release();
                  rSNode.mSEntityElems.Remove(k);
                  k = -1;
                  break;
               }
            }
            if(k!=-1)
            {
                rAssert(false);
                SpatialNode& rRootNode = mpWorldScene->mStaticTreeWalker.rIthNode(0);

                for(k=0;k<rRootNode.mSEntityElems.mUseSize;k++)
                {
                    if( rRootNode.mSEntityElems[k] == mLoadLists[i]->mSEntityElems[j] )
                    {
                        rRootNode.mSEntityElems[k]->Release();
                        rRootNode.mSEntityElems.Remove(k);
                        k = -1;
                        break;
                    }
                }
            }
            rAssert(k==-1);
         }
         //////////////////////////////////////////////////////////////////////////
         // AnimColl
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mAnimCollElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mAnimCollElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
                */
            SpatialNode& rSNode = *(mLoadLists[i]->mAnimCollElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mAnimCollElems.mUseSize;k++)
            {
               if( rSNode.mAnimCollElems[k] == mLoadLists[i]->mAnimCollElems[j] )
               {
                  rSNode.mAnimCollElems[k]->Release();
                  GetAnimEntityDSGManager()->Remove( rSNode.mAnimCollElems[k] );
                  rSNode.mAnimCollElems.Remove(k);
                  k = -1;
                  break;
               }
            }
            if(k!=-1)
            {
                rAssert(false);
                SpatialNode& rRootNode = mpWorldScene->mStaticTreeWalker.rIthNode(0);

                for(k=0;k<rRootNode.mAnimCollElems.mUseSize;k++)
                {
                    if( rRootNode.mAnimCollElems[k] == mLoadLists[i]->mAnimCollElems[j] )
                    {
                        rRootNode.mAnimCollElems[k]->Release();
                        GetAnimEntityDSGManager()->Remove( rRootNode.mAnimCollElems[k] );
                        rRootNode.mAnimCollElems.Remove(k);
                        k = -1;
                        break;
                    }
                }
            }
            rAssert(k==-1);
         }
         //////////////////////////////////////////////////////////////////////////
         // Anim
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mAnimElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mAnimElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
            */
            SpatialNode& rSNode = *(mLoadLists[i]->mAnimElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mAnimElems.mUseSize;k++)
            {
               if( rSNode.mAnimElems[k] == mLoadLists[i]->mAnimElems[j] )
               {
				   // Remove it from the list of managed animentities
 				   GetAnimEntityDSGManager()->Remove( rSNode.mAnimElems[k] );
				   rSNode.mAnimElems[k]->Release();
                   rSNode.mAnimElems.Remove(k);
                   k = -1;
                   break;
               }
            }
            if(k!=-1)
            {
                rAssert(false);
                SpatialNode& rRootNode = mpWorldScene->mStaticTreeWalker.rIthNode(0);

                for(k=0;k<rRootNode.mAnimElems.mUseSize;k++)
                {
                    if( rRootNode.mAnimElems[k] == mLoadLists[i]->mAnimElems[j] )
                    {
				        // Remove it from the list of managed animentities
 				        GetAnimEntityDSGManager()->Remove( rRootNode.mAnimElems[k] );
				        rRootNode.mAnimElems[k]->Release();
                        rRootNode.mAnimElems.Remove(k);
                        k = -1;
                        break;
                    }
                }
            }
            rAssert(k==-1);
         }
         //////////////////////////////////////////////////////////////////////////
         // Intersect
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mIntersectElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mIntersectElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
            */
            SpatialNode& rSNode = *(mLoadLists[i]->mIntersectElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mIntersectElems.mUseSize;k++)
            {
               if( rSNode.mIntersectElems[k] == mLoadLists[i]->mIntersectElems[j] )
               {
                  //irEntityDeletionList.Add(rSNode.mIntersectElems[k]);
                  rSNode.mIntersectElems[k]->Release();
   //               rSNode.mIntersectElems[k]->ReleaseVerified();
                  rSNode.mIntersectElems.Remove(k);
                  k = rSNode.mIntersectElems.mUseSize+10;
               }
            }
            rAssert(k==rSNode.mIntersectElems.mUseSize+11);
         }
         //////////////////////////////////////////////////////////////////////////
         // SPhys
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mSPhysElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mSPhysElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
            */
            SpatialNode& rSNode = *(mLoadLists[i]->mSPhysElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mSPhysElems.mUseSize;k++)
            {
               if( rSNode.mSPhysElems[k] == mLoadLists[i]->mSPhysElems[j] )
               {
                  //irEntityDeletionList.Add(rSNode.mSPhysElems[k]);
                  rSNode.mSPhysElems[k]->Release();
    //              rSNode.mSPhysElems[k]->ReleaseVerified();
                  rSNode.mSPhysElems.Remove(k);
                  k = rSNode.mSPhysElems.mUseSize+10;
               }
            }
            rAssert(k==rSNode.mSPhysElems.mUseSize+11);
         }
         //////////////////////////////////////////////////////////////////////////
         // Fences
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mFenceElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mFenceElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
            */
            SpatialNode& rSNode = *(mLoadLists[i]->mFenceElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mFenceElems.mUseSize;k++)
            {
               if( rSNode.mFenceElems[k] == mLoadLists[i]->mFenceElems[j] )
               {
                  //irEntityDeletionList.Add(rSNode.mFenceElems[k]);
                  rSNode.mFenceElems[k]->Release();
    //              rSNode.mSPhysElems[k]->ReleaseVerified();
                  rSNode.mFenceElems.Remove(k);
                  k = rSNode.mFenceElems.mUseSize+10;
               }
            }
            rAssert(k==rSNode.mFenceElems.mUseSize+11);
         }

         //////////////////////////////////////////////////////////////////////////
         // Trigger Volumes
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mTrigVolElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mTrigVolElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
            */
            SpatialNode& rSNode = *(mLoadLists[i]->mTrigVolElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mTrigVolElems.mUseSize;k++)
            {
               if( rSNode.mTrigVolElems[k] == mLoadLists[i]->mTrigVolElems[j] )
               {
                  //irEntityDeletionList.Add(rSNode.mTrigVolElems[k]);
                  rSNode.mTrigVolElems[k]->Release();
                  rSNode.mTrigVolElems.Remove(k);
                  k = rSNode.mTrigVolElems.mUseSize+10;
               }
            }
            rAssert(k==rSNode.mTrigVolElems.mUseSize+11);
         }
         //////////////////////////////////////////////////////////////////////////
         // Road Segments
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mRoadSegmentElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mRoadSegmentElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
            */
            SpatialNode& rSNode = *(mLoadLists[i]->mRoadSegmentElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mRoadSegmentElems.mUseSize;k++)
            {
               if( rSNode.mRoadSegmentElems[k] == mLoadLists[i]->mRoadSegmentElems[j] )
               {
                  //irEntityDeletionList.Add(rSNode.mRoadSegmentElems[k]);
                  rSNode.mRoadSegmentElems[k]->Release();
                  rSNode.mRoadSegmentElems.Remove(k);
                  k = rSNode.mRoadSegmentElems.mUseSize+10;
               }
            }
            rAssert(k==rSNode.mRoadSegmentElems.mUseSize+11);
         }

         //////////////////////////////////////////////////////////////////////////
         // Path Segments
         //////////////////////////////////////////////////////////////////////////
         for(j=0;j<mLoadLists[i]->mPathSegmentElems.mUseSize;j++)
         {
             /*
            mLoadLists[i]->mPathSegmentElems[j]->GetBoundingBox(&BBox);
            BBoxSP.mBounds.mMin.Add(BBox.low, mpWorldScene->mEpsilonOffset);
            BBoxSP.mBounds.mMax.Sub(BBox.high,mpWorldScene->mEpsilonOffset);
            
            SpatialNode& rSNode = mpWorldScene->mStaticTreeWalker.rSeekNode(BBoxSP);
            */
            SpatialNode& rSNode = *(mLoadLists[i]->mPathSegmentElems[j]->mpSpatialNode);

            for(k=0;k<rSNode.mPathSegmentElems.mUseSize;k++)
            {
               if( rSNode.mPathSegmentElems[k] == mLoadLists[i]->mPathSegmentElems[j] )
               {
                  //irEntityDeletionList.Add(rSNode.mPathSegmentElems[k]);
                  rSNode.mPathSegmentElems[k]->Release();
                  rSNode.mPathSegmentElems.Remove(k);
                  k = rSNode.mPathSegmentElems.mUseSize+10;
               }
            }
            rAssert(k==rSNode.mPathSegmentElems.mUseSize+11);
         }

         radTime64 start = radTimeGetMicroseconds64();

         p3d::inventory->RemoveSectionElements(irGiveItAFuckinName.GetUID());
         p3d::inventory->DeleteSection(irGiveItAFuckinName.GetUID());

         radTime64 end = radTimeGetMicroseconds64();
         unsigned deleteTime = (unsigned) (end - start);

//         printf("WorldRenderLayer::DumpDynaLoad Delete time: %.3fms\n", deleteTime / 1000.0F);

         HeapMgr()->PushHeap( GMA_LEVEL_OTHER );
         //mLoadLists[i]->ClearAll();
	     //mLoadLists[i]->AllocateAll(mnLoadListRefs);
         mLoadLists[i]->ClearAllUse();
         mLoadLists.Remove(i);
         HeapMgr()->PopHeap(GMA_LEVEL_OTHER);
      }
   }
}

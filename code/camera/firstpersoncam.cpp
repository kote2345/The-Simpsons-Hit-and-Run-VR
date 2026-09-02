//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        firstpersoncam.cpp
//
// Description: Implement FirstPersonCam
//
// History:     2/21/2003 + Created -- Cary Brisebois
//
//=============================================================================

//========================================
// System Includes
//========================================
// Foundation Tech
#include <raddebug.hpp>
#include <radmath/radmath.hpp>

//========================================
// Project Includes
//========================================
#include <camera/firstpersoncam.h>
#include <camera/isupercamtarget.h>
#include <camera/supercamcontroller.h>
#include <camera/supercamconstants.h>
#include <camera/supercammanager.h>

#include <input/inputmanager.h>
#if defined(RAD_ANDROID)
#include <SDL.h>
#include <vr/openxrmanager.h>
#include <worldsim/avatarmanager.h>
#include <worldsim/avatar.h>
#include <worldsim/character/character.h>
#include <worldsim/character/charactertarget.h>
#include <worldsim/redbrick/vehicle.h>
#endif

//*****************************************************************************
//
// Global Data, Local Data, Local Classes
//
//*****************************************************************************
#define DEFAULT_MAGNITUDE 5.0f
#define DEFAULT_ROTATION rmt::PI_BY2
#define DEFAULT_ELEVATION rmt::PI_BY2

#define MAX_ROT_ANGLE rmt::DegToRadian( 80.0f )
#define MAX_ELEV_ANGLE rmt::DegToRadian( 80.0f )

#if defined(RAD_ANDROID)
static const float VR_STICK_YAW_DEAD_ZONE = 0.15f;
// DriverLocation is an authored, vehicle-local seat point and is stable before,
// during and after the entry animation.  Never derive the VR camera from the
// animated character pose: the transition pose is not guaranteed to have been
// evaluated when SuperCam switches targets.
static const float VR_DRIVER_EYE_HEIGHT = 0.96f;
// Preserve the vehicle's real pitch/roll, but cap extreme impacts and flips.
// Yaw remains unrestricted and tilt keeps the existing temporal smoothing.
static const float VR_VEHICLE_TILT_SCALE = 1.0f;
static const float VR_VEHICLE_TILT_LIMIT = rmt::DegToRadian( 45.0f );
static const float VR_VEHICLE_TILT_LAG_SECONDS = 0.25f;

static float ClampVrVehicleTilt(float angle)
{
    if(angle>VR_VEHICLE_TILT_LIMIT) return VR_VEHICLE_TILT_LIMIT;
    if(angle<-VR_VEHICLE_TILT_LIMIT) return -VR_VEHICLE_TILT_LIMIT;
    return angle;
}

static void BuildVrStabilizedVehicleTransform(const rmt::Matrix& vehicleTransform,
                                               float deltaSeconds,
                                               bool initialize,
                                               float* filteredPitch,
                                               float* filteredRoll,
                                               rmt::Matrix* stabilized)
{
    rmt::Vector horizontalForward=vehicleTransform.Row(2);
    horizontalForward.y=0.0f;
    if(horizontalForward.NormalizeSafe()<0.0001f)
        horizontalForward.Set(0.0f,0.0f,1.0f);

    float forwardY=vehicleTransform.Row(2).y;
    if(forwardY>1.0f) forwardY=1.0f;
    if(forwardY<-1.0f) forwardY=-1.0f;
    const float desiredPitch=ClampVrVehicleTilt(
        rmt::ASin(forwardY)*VR_VEHICLE_TILT_SCALE);
    const float desiredRoll=ClampVrVehicleTilt(
        rmt::ATan2(vehicleTransform.Row(0).y,vehicleTransform.Row(1).y)*
        VR_VEHICLE_TILT_SCALE);

    if(initialize)
    {
        *filteredPitch=desiredPitch;
        *filteredRoll=desiredRoll;
    }
    else
    {
        float blend=deltaSeconds/(VR_VEHICLE_TILT_LAG_SECONDS+deltaSeconds);
        if(blend>1.0f) blend=1.0f;
        *filteredPitch+=(desiredPitch-*filteredPitch)*blend;
        *filteredRoll+=(desiredRoll-*filteredRoll)*blend;
    }

    const float pitchSin=rmt::Sin(*filteredPitch);
    const float pitchCos=rmt::Cos(*filteredPitch);
    const float rollSin=rmt::Sin(*filteredRoll);
    const float rollCos=rmt::Cos(*filteredRoll);
    const rmt::Vector worldUp(0.0f,1.0f,0.0f);
    const rmt::Vector yawRight(horizontalForward.z,0.0f,-horizontalForward.x);
    const rmt::Vector forward=horizontalForward*pitchCos+worldUp*pitchSin;
    const rmt::Vector upWithoutRoll=worldUp*pitchCos-horizontalForward*pitchSin;

    stabilized->Identity();
    stabilized->Row(0)=yawRight*rollCos+upWithoutRoll*rollSin;
    stabilized->Row(1)=upWithoutRoll*rollCos-yawRight*rollSin;
    stabilized->Row(2)=forward;
    stabilized->Row(3)=vehicleTransform.Row(3);
}
#endif

#ifdef DEBUGWATCH
float FIRST_PERSON_CAM_MIN_FOV = SUPERCAM_DEFAULT_MIN_FOV;
float FIRST_PERSON_CAM_MAX_FOV = SUPERCAM_DEFAULT_MAX_FOV;
float FIRST_PERSON_CAM_LOOK_LAG = 0.05f;
#else
const float FIRST_PERSON_CAM_MIN_FOV = SUPERCAM_DEFAULT_MIN_FOV;
const float FIRST_PERSON_CAM_MAX_FOV = SUPERCAM_DEFAULT_MAX_FOV;
const float FIRST_PERSON_CAM_LOOK_LAG = SUPERCAM_DEFAULT_FOV_LAG;
#endif


//*****************************************************************************
//
// Public Member Functions
//
//*****************************************************************************

//=============================================================================
// FirstPersonCam::FirstPersonCam
//=============================================================================
// Description: Constructor.
//
// Parameters:  None.
//
// Return:      N/A.
//
//=============================================================================
FirstPersonCam::FirstPersonCam() :
    mTarget( NULL ),
    mTargetDirty( false ),
    mVrStickYaw( 0.0f ),
    mVrSnapReady( true ),
    mVrVehiclePitch( 0.0f ),
    mVrVehicleRoll( 0.0f ),
    mVrVehicleAnchorValid( false ),
    mVrVehicleTiltValid( false ),
    mVrVehicleCameraLogged( false ),
    mVrAnchoredVehicle( NULL ),
    mRotation( DEFAULT_ROTATION ),
    mElevation( DEFAULT_ELEVATION ),
    mRotationDelta( 0.0f ),
    mElevationDelta( 0.0f ),
    mFOVDelta( 0.0f ),
    mCollisionOffset( NULL ),
    mNumCollisions( 0 )
{
    mTargetPositionOffset.Set( 0.0f, 0.0f, 0.0f );
    mVrBaseHeading.Set( 0.0f, 0.0f, 1.0f );
    mVrVehicleAnchorLocal.Identity();
}

//=============================================================================
// FirstPersonCam::~FirstPersonCam
//=============================================================================
// Description: Destructor.
//
// Parameters:  None.
//
// Return:      N/A.
//
//=============================================================================
FirstPersonCam::~FirstPersonCam()
{
}

//=============================================================================
// FirstPersonCam::Init
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void FirstPersonCam::OnInit()
{
#if defined(RAD_ANDROID)
    // Mission restart can reactivate this same camera without calling
    // SetTarget(), especially when the stage starts with the player already
    // seated. Never reuse a seat pose/recenter state across activations.
    mVrVehicleAnchorValid=false;
    mVrVehicleTiltValid=false;
    mVrVehicleCameraLogged=false;
    mVrAnchoredVehicle=NULL;
#endif
    InitMyController();
}

//=============================================================================
// FirstPersonCam::OnShutdown
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void FirstPersonCam::OnShutdown()
{
    //Reset the controller state if we're not in a new state since we went in.
    //Switching to pause when in first person will cause this to fail.
    if ( GetInputManager()->GetGameState() == Input::ACTIVE_FIRST_PERSON )
    {
        GetInputManager()->SetGameState( Input::ACTIVE_GAMEPLAY );
    }
}

//=============================================================================
// FirstPersonCam::Update
//=============================================================================
// Description: Comment
//
// Parameters:  ( unsigned int milliseconds )
//
// Return:      void 
//
//=============================================================================
void FirstPersonCam::Update( unsigned int milliseconds )
{
    rAssert( mTarget );

    if ( mTargetDirty )
    {
        //Update the position offset since we just got a new target.
        mTarget->GetFirstPersonPosition( &mTargetPositionOffset );
#if defined(RAD_ANDROID)
        if( SharOpenXR::IsVrModeEnabled() )
        {
            mTarget->GetHeading( &mVrBaseHeading );
            mVrBaseHeading.y=0.0f;
            mVrBaseHeading.NormalizeSafe();
            mVrStickYaw=0.0f;
            SharOpenXR::SetVrBaseHeading( mVrBaseHeading );
        }
#endif
        mTargetDirty = false;
    }

#if defined(RAD_ANDROID)
    if( !SharOpenXR::IsVrModeEnabled() )
#endif
    if ( GetInputManager()->GetGameState() == Input::ACTIVE_GAMEPLAY || GetInputManager()->GetGameState() == Input::ACTIVE_ALL )
    {
        GetInputManager()->SetGameState( Input::ACTIVE_FIRST_PERSON );
    }

    if ( GetFlag((Flag)FIRST_TIME) || GetFlag((Flag)CUT) )
    {
        mRotation = DEFAULT_ROTATION;
        mElevation = DEFAULT_ELEVATION;
        mRotationDelta = 0.0f;
        mElevationDelta = 0.0f;
        mFOVDelta = 0.0f;

        SetFlag( (Flag)FIRST_TIME, false );
        SetFlag( (Flag)CUT, false );
    }

    //---------  Calculate positino and target

    float timeMod = milliseconds / 16.0f;

#if defined(RAD_ANDROID)
    if( SharOpenXR::IsVrModeEnabled() )
    {
        float stickYaw=mController->GetAxisValue( SuperCamController::stickX );
        if( rmt::Fabs( stickYaw ) < VR_STICK_YAW_DEAD_ZONE )
        {
            stickYaw=0.0f;
        }

        float yawDelta=0.0f;
        if( SharOpenXR::IsSnapTurnEnabled() )
        {
            if( rmt::Fabs(stickYaw)<0.35f ) mVrSnapReady=true;
            if( mVrSnapReady && rmt::Fabs(stickYaw)>0.70f )
            {
                yawDelta=(stickYaw>0.0f?1.0f:-1.0f)*
                         rmt::DegToRadian(SharOpenXR::GetSnapTurnAngle());
                mVrSnapReady=false;
            }
        }
        else
        {
            yawDelta=stickYaw*rmt::DegToRadian(SharOpenXR::GetSmoothTurnSpeed())*
                     (static_cast<float>(milliseconds)/1000.0f);
        }
        rmt::Matrix yawMatrix;
        // FillRotateY writes only the 3x3 rotation. Initialize the affine row
        // before using the matrix; otherwise its stack garbage is interpreted
        // as translation by Vector::Transform and collapses heading to a
        // seemingly fixed yaw.
        yawMatrix.Identity();
        yawMatrix.FillRotateY( yawDelta );

        if( mTarget->IsCar() )
        {
            // The base follows vehicle steering, with stick yaw layered on it.
            mVrStickYaw+=yawDelta;
            Vehicle* vehicle=static_cast<Vehicle*>(mTarget);
            mVrBaseHeading=vehicle->GetTransform().Row(2);
            mVrBaseHeading.y=0.0f;
            mVrBaseHeading.NormalizeSafe();
            yawMatrix.FillRotateY( mVrStickYaw );
            mVrBaseHeading.Rotate( yawMatrix );
        }
        else if( stickYaw != 0.0f )
        {
            // Heading is a direction (w=0), not a point (w=1).
            mVrBaseHeading.Rotate( yawMatrix );
            mVrBaseHeading.y=0.0f;
            mVrBaseHeading.NormalizeSafe();
        }

        SharOpenXR::SetVrBaseHeading( mVrBaseHeading );
    }
#endif

    //place the target at the position and deal with controller input.

    rmt::Vector position, target;
#if defined(RAD_ANDROID)
    rmt::Matrix vrVehicleTransform;
    bool vrVehicleTransformValid=false;
#endif
#if defined(RAD_ANDROID)
    if( SharOpenXR::IsVrModeEnabled() )
    {
        rmt::Vector roomscaleDelta;
        const bool moved=SharOpenXR::ConsumeRoomscaleMovement(&roomscaleDelta);
        Avatar* avatar=GetAvatarManager()->GetAvatarForPlayer(GetPlayerID());
        if(moved && avatar && !avatar->IsInCar())
        {
            Character* character=avatar->GetCharacter();
            if(character)
            {
                rmt::Vector characterPosition;
                character->GetPosition(characterPosition);
                characterPosition.Add(roomscaleDelta);
                character->SetPosition(characterPosition);
            }
        }
    }
#endif
    mTarget->GetPosition( &position );
    position.Add( mTargetPositionOffset );
#if defined(RAD_ANDROID)
    if( SharOpenXR::IsVrModeEnabled() && !mTarget->IsCar() )
    {
        // On foot, replace the character model's authored eye height with the
        // user's real eye height above the Quest stage floor. Keep the base
        // exactly over the character root: the legacy first-person X/Z offset
        // otherwise becomes a visible forward displacement after recentering
        // while crouched. Horizontal HMD motion then remains purely local.
        position.x-=mTargetPositionOffset.x;
        position.z-=mTargetPositionOffset.z;
        float physicalHeadHeight=0.0f;
        if( SharOpenXR::GetPhysicalHeadHeight( &physicalHeadHeight ) )
        {
            position.y+=physicalHeadHeight-mTargetPositionOffset.y;
        }
    }
    if( SharOpenXR::IsVrModeEnabled() && mTarget->IsCar() )
    {
        Avatar* avatar=GetAvatarManager()->GetAvatarForPlayer( GetPlayerID() );
        Vehicle* vehicle=static_cast<Vehicle*>(mTarget);
        // A restart may replace the mission car while retaining the same
        // FirstPersonCam and target adapter. Rebuild the local seat anchor for
        // the actual vehicle before using it to place the eye camera.
        if(mVrAnchoredVehicle!=vehicle)
        {
            mVrVehicleAnchorValid=false;
            mVrVehicleTiltValid=false;
            mVrVehicleCameraLogged=false;
            mVrAnchoredVehicle=vehicle;
        }
        BuildVrStabilizedVehicleTransform(vehicle->GetTransform(),
            static_cast<float>(milliseconds)/1000.0f,!mVrVehicleTiltValid,
            &mVrVehiclePitch,&mVrVehicleRoll,&vrVehicleTransform);
        mVrVehicleTiltValid=true;
        vrVehicleTransformValid=true;
        if( !mVrVehicleAnchorValid && avatar && avatar->IsInCar() )
        {
            // Use the car's authored seat socket. Character head joints can
            // contain an unevaluated entry pose here (including a 180-degree
            // rotation or stale height), whereas this point is deterministic.
            mVrVehicleAnchorLocal.Identity();
            mVrVehicleAnchorLocal.Row(3)=vehicle->GetDriverLocation();
            mVrVehicleAnchorLocal.Row(3).y+=VR_DRIVER_EYE_HEIGHT;

            const rmt::Vector vehicleWorldPosition=vehicle->GetTransform().Row(3);
            const rmt::Vector capturedLocal=mVrVehicleAnchorLocal.Row(3);
            SDL_Log("VR vehicle anchor captured: vehicle=(%.3f %.3f %.3f) local=(%.3f %.3f %.3f)",
                    vehicleWorldPosition.x,vehicleWorldPosition.y,vehicleWorldPosition.z,
                    capturedLocal.x,capturedLocal.y,capturedLocal.z);

            // Capture the complete entry pose.  The local anchor preserves
            // X/Y/Z in vehicle space; recentering makes the current HMD yaw
            // and physical height the zero offset from that seated anchor.
            // From the next frame onward the vehicle transform supplies the
            // base position/orientation and tracked motion stays local to it.
            mVrBaseHeading=vrVehicleTransform.Row(2);
            mVrBaseHeading.y=0.0f;
            mVrBaseHeading.NormalizeSafe();
            mVrStickYaw=0.0f;
            SharOpenXR::SetVrBaseHeading( mVrBaseHeading );
            SharOpenXR::RecenterVrPose();
            mVrVehicleAnchorValid=true;
        }
        if( mVrVehicleAnchorValid )
        {
            rmt::Matrix anchorWorld;
            anchorWorld.Mult(mVrVehicleAnchorLocal,vrVehicleTransform);
            position=anchorWorld.Row(3);
        }
    }
#endif

    //Take controller values and calculate desired rotation and position.
    float desiredRot, desiredElev;
#if defined(RAD_ANDROID)
    if( SharOpenXR::IsVrModeEnabled() )
    {
        // In VR the right stick must not add artificial yaw or pitch.  The
        // tracked HMD supplies both axes one-to-one.
        desiredRot=DEFAULT_ROTATION;
        desiredElev=DEFAULT_ELEVATION;
    }
    else
    {
#endif
    desiredRot = mRotation + ( MAX_ROT_ANGLE * -(mController->GetAxisValue( SuperCamController::stickX )) );

    float invert = -1.0f;
    if ( GetSuperCamManager()->GetSCC( GetPlayerID() )->IsInvertedCameraEnabled() )
    {
        invert = 1.0f;
    }

    desiredElev = MAX_ELEV_ANGLE * ( invert * mController->GetAxisValue( SuperCamController::stickY ) ) + DEFAULT_ELEVATION;
#if defined(RAD_ANDROID)
    }
#endif

    float lag = FIRST_PERSON_CAM_LOOK_LAG * timeMod;
    CLAMP_TO_ONE( lag );

    MotionCubic( &mRotation, &mRotationDelta, desiredRot, lag );
    MotionCubic( &mElevation, &mElevationDelta, desiredElev, lag );

    rmt::SphericalToCartesian( DEFAULT_MAGNITUDE, mRotation, mElevation, &target.x, &target.z, &target.y );

    rmt::Vector targetHeading;
    mTarget->GetHeading( &targetHeading );
#if defined(RAD_ANDROID)
    if( SharOpenXR::IsVrModeEnabled() )
    {
        // Body yaw follows the HMD separately.  Keep the base camera at the
        // activation heading so OpenXR yaw is applied exactly once.
        targetHeading=mVrBaseHeading;
    }
#endif
    rmt::Vector targetVUP;
    mTarget->GetVUP( &targetVUP );
#if defined(RAD_ANDROID)
    if( SharOpenXR::IsVrModeEnabled() && mTarget->IsCar() )
    {
        // Use the actual chassis orientation rather than the target adapter's
        // cached heading/up. This keeps the seated pose rigidly attached to
        // vehicle yaw, pitch and roll.
        Vehicle* vehicle=static_cast<Vehicle*>(mTarget);
        targetVUP=vehicle->GetTransform().Row(1);
    }
#endif

    rmt::Matrix mat;
#if defined(RAD_ANDROID)
    if( SharOpenXR::IsVrModeEnabled() && mTarget->IsCar() )
    {
        // Use the complete head pose captured at successful entry and rigidly
        // parented to the vehicle. No live character animation or camera
        // collision can move this anchor after it has been established.
        // Build orientation from the chassis only. The seat anchor contributes
        // translation above; including its full world matrix here couples yaw
        // to a translated transform and can produce an invalid view matrix.
        rmt::Matrix anchorWorld=vrVehicleTransformValid ?
                                vrVehicleTransform :
                                static_cast<Vehicle*>(mTarget)->GetTransform();
        anchorWorld.Row(3).Set(0.0f,0.0f,0.0f);
        rmt::Matrix localYaw;
        localYaw.Identity();
        localYaw.FillRotateY(mVrStickYaw);
        mat.Mult(localYaw,anchorWorld);
    }
    else
    {
#endif
        mat.Identity();
        mat.FillHeading( targetHeading, targetVUP );
#if defined(RAD_ANDROID)
    }
#endif

    target.Transform( mat );
    target.Add( position );
#if defined(RAD_ANDROID)
    if(SharOpenXR::IsVrModeEnabled() && mTarget->IsCar() &&
       mVrVehicleAnchorValid && !mVrVehicleCameraLogged)
    {
        SDL_Log("VR vehicle camera final: position=(%.3f %.3f %.3f) target=(%.3f %.3f %.3f) forward=(%.3f %.3f %.3f)",
                position.x,position.y,position.z,target.x,target.y,target.z,
                mat.Row(2).x,mat.Row(2).y,mat.Row(2).z);
        mVrVehicleCameraLogged=true;
    }
#endif

    //---------  Goofin' with the FOV

    float zoom = mController->GetValue( SuperCamController::zToggle );

    float FOV = GetFOV();
    FilterFov( zoom, FIRST_PERSON_CAM_MIN_FOV, FIRST_PERSON_CAM_MAX_FOV, FOV, mFOVDelta, SUPERCAM_DEFAULT_FOV_LAG, timeMod );

    SetFOV( FOV );

    //---------  Set values

    SetCameraValues( milliseconds, position, target );
}

//=============================================================================
// FirstPersonCam::UpdateForPhysics
//=============================================================================
// Description: Comment
//
// Parameters:  ( unsigned int milliseconds )
//
// Return:      void 
//
//=============================================================================
void FirstPersonCam::UpdateForPhysics( unsigned int milliseconds )
{
#if defined(RAD_ANDROID)
    // Tracked VR cameras intentionally occupy the player's physical eye
    // position. Generic third-person camera collision push-out makes the view
    // jump at walls, vehicles and between competing collision normals.
    if( SharOpenXR::IsVrModeEnabled() )
    {
        return;
    }
#endif
    if ( mNumCollisions )
    {
        rmt::Vector offset( 0.0f, 0.0f, 0.0f );

        unsigned int i;
        for ( i = 0; i < mNumCollisions; ++i )
        {
            offset += mCollisionOffset[ i ];
        }

        //offset.x /= mNumCollisions;
        //offset.y /= mNumCollisions;
        //offset.z /= mNumCollisions;

        rmt::Vector camPos;
        GetPosition( &camPos );

        rmt::Vector camTarg;
        GetTarget( &camTarg );

        camPos += offset;
        camTarg += offset;

        SetCameraValues( 0, camPos, camTarg );
    }

    if ( mTarget->IsUnstable() )
    {
        int controllerID = GetInputManager()->GetControllerIDforPlayer( GetPlayerID() );
        GetSuperCamManager()->GetSCC( 0 )->ToggleFirstPerson( controllerID );
    }
}

//=============================================================================
// FirstPersonCam::SetTarget
//=============================================================================
// Description: Comment
//
// Parameters:  ( ISuperCamTarget* target )
//
// Return:      void 
//
//=============================================================================
void FirstPersonCam::SetTarget( ISuperCamTarget* target )
{
    mTarget = target;
    mTargetDirty = true;
#if defined(RAD_ANDROID)
    mVrVehicleAnchorValid=false;
    mVrVehicleTiltValid=false;
    mVrVehicleCameraLogged=false;
    mVrAnchoredVehicle=NULL;
#endif
}

//=============================================================================
// FirstPersonCam::OverrideOldState
//=============================================================================
// Description: Comment
//
// Parameters:  ( Input::ActiveState state )
//
// Return:      void 
//
//=============================================================================
void FirstPersonCam::OverrideOldState( Input::ActiveState state )
{
    mOldState = state;
}


//*****************************************************************************
//
// Protected Member Functions
//
//*****************************************************************************

//=============================================================================
// FirstPersonCam::OnRegisterDebugControls
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void FirstPersonCam::OnRegisterDebugControls()
{
#ifdef DEBUGWATCH
    char nameSpace[256];
    sprintf( nameSpace, "SuperCam\\Player%d\\1st Person", GetPlayerID() );

    radDbgWatchAddFloat( &FIRST_PERSON_CAM_MIN_FOV, "Min FOV", nameSpace, NULL, NULL, 0.0f, rmt::PI );
    radDbgWatchAddFloat( &FIRST_PERSON_CAM_MAX_FOV, "Max FOV", nameSpace, NULL, NULL, 0.0f, rmt::PI );
    radDbgWatchAddFloat( &FIRST_PERSON_CAM_LOOK_LAG, "Look Lag", nameSpace, NULL, NULL, 0.0f, rmt::PI );
#endif
}

//=============================================================================
// FirstPersonCam::OnUnregisterDebugControls
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void FirstPersonCam::OnUnregisterDebugControls()
{
#ifdef DEBUGWATCH
    radDbgWatchDelete( &FIRST_PERSON_CAM_MIN_FOV );
    radDbgWatchDelete( &FIRST_PERSON_CAM_MAX_FOV );
    radDbgWatchDelete( &FIRST_PERSON_CAM_LOOK_LAG );
#endif
}

//*****************************************************************************
//
// Private Member Functions
//
//*****************************************************************************

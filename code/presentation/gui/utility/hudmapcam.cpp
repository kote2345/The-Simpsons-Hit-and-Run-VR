//===========================================================================
// Copyright (C) 2003 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   HudMapCam
//
// Description: Implementation of the CHudMap class.
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2003/03/10      TChu        Created for SRR2
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================
#include <presentation/gui/utility/hudmapcam.h>

#include <camera/supercammanager.h>
#include <camera/supercamcentral.h>

#include <raddebug.hpp>

#if defined(SRR2_OPENXR)
#include <vr/openxrmanager.h>
#include <worldsim/avatar.h>
#include <worldsim/avatarmanager.h>
#include <worldsim/character/charactermanager.h>
#include <worldsim/character/character.h>
#include <worldsim/redbrick/vehicle.h>
#endif

//===========================================================================
// Global Data, Local Data, Local Classes
//===========================================================================

//===========================================================================
// Public Member Functions
//===========================================================================

HudMapCam::HudMapCam( int playerID )
:   KullCam(),
    m_camera( NULL ),
    m_originalHeading( -1, 0, 0 )
{
    SetPlayerID( playerID );
    mIgnoreDebugController = true;

    m_camera = new tPointCamera;
    m_camera->AddRef();
    SetCamera( m_camera );

    // override default KullCam parameters
    mElevation = 0.0f;
    mMagnitude = 150.0f;

    // fix hud map cam FOV
    //
    this->SetFOV( rmt::PI_BY2 );
}

HudMapCam::~HudMapCam()
{
    if( m_camera != NULL )
    {
        m_camera->Release();
        m_camera = NULL;
    }
}

void
HudMapCam::Update( unsigned int milliseconds )
{
    // Adjust KullCam rotation to match the gameplay heading. In VR the active
    // eye camera also contains the live HMD yaw; using it here makes the 3D
    // radar rotate when the player looks around. The OpenXR gameplay camera
    // is the same base camera used for world simulation, before HMD tracking.
    rmt::Vector camHeading;
    bool haveGameplayHeading = false;
#if defined(SRR2_OPENXR)
    // Resolve the vehicle from the same character state that drives the
    // in-car HUD.  The map camera must follow the chassis yaw, not a render
    // camera whose orientation may contain the HMD pose.
    CharacterManager* characters = GetCharacterManager();
    Character* player = characters ? characters->GetCharacter( 0 ) : NULL;
    AvatarManager* avatars = GetAvatarManager();
    Avatar* avatar = avatars ? avatars->GetAvatarForPlayer( 0 ) : NULL;
    Vehicle* vehicle = ( player && player->IsInCar() ) ?
        player->GetTargetVehicle() : NULL;
    if( !vehicle && avatar && avatar->IsInCar() )
        vehicle = avatar->GetVehicle();
    if( vehicle )
    {
        // A vehicle radar is chassis-relative: neither HMD yaw nor the
        // view/stick camera yaw is allowed to rotate its 3D projection.
        // The vehicle transform is the simulation's authoritative world
        // basis.  It is independent of the render camera and HMD pose.
        camHeading = vehicle->GetTransform().Row( 2 );
        haveGameplayHeading = true;
    }
    else if( SharOpenXR::IsVrModeEnabled() )
    {
        rmt::Matrix gameplayCamera;
        if( SharOpenXR::GetGameplayCamera( &gameplayCamera ) )
        {
            camHeading = gameplayCamera.Row( 2 );
            haveGameplayHeading = true;
        }
    }
#endif
    if( !haveGameplayHeading )
    {
        GetSuperCamManager()->GetSCC( this->GetPlayerID() )->GetActiveSuperCam()->GetHeading( &camHeading );
    }
    camHeading.y = 0;

    if( camHeading.MagnitudeSqr() > 0 )
    {
        float ratio = camHeading.DotProduct( m_originalHeading ) / camHeading.Magnitude();
        if( ratio > 1.0f )
        {
            mRotation = 0.0f;
        }
        else if( ratio < -1.0f )
        {
            mRotation = rmt::PI;
        }
        else
        {
            mRotation = rmt::ACos( ratio );
        }

        rAssert( !rmt::IsNan( mRotation ) );

        rmt::Vector normal = camHeading;
        normal.CrossProduct( m_originalHeading );
        if( normal.y < 0 )
        {
            mRotation = -mRotation;
        }
    }

    KullCam::Update( milliseconds );

    // fix hud map cam at 'mMagnitude' above sea level
    //
    rmt::Vector camPosition = m_camera->GetPosition();
    camPosition.y = mMagnitude;
    m_camera->SetPosition( camPosition );
}

void
HudMapCam::SetHeight( float height )
{
    rAssert( height > 0.0f );
    mMagnitude = height;
}


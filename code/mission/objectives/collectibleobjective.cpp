//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        CollectibleObjective.cpp
//
// Description: Implement CollectibleObjective
//
// History:     10/8/2002 + Created -- Cary Brisebois
//
//=============================================================================

//========================================
// System Includes
//========================================
// Foundation Tech
#include <raddebug.hpp>

#if defined(RAD_ANDROID)
#include <android/log.h>
#define VRPICKUP_LOG(...) __android_log_print(ANDROID_LOG_INFO, "VRPICKUP", __VA_ARGS__)
#else
#define VRPICKUP_LOG(...) ((void)0)
#endif
#include <p3d/utility.hpp>
#include <string.h>

//========================================
// Project Includes
//========================================
#include <mission/objectives/collectibleobjective.h>

#include <events/eventmanager.h>

#include <mission/animatedicon.h>
#include <mission/gameplaymanager.h>
#include <mission/missionmanager.h>

#include <render/DSG/InstStatEntityDSG.h>

#include <memory/srrmemory.h>

#include <meta/triggerlocator.h>
#include <meta/eventlocator.h>
#include <meta/actioneventlocator.h>
#include <meta/spheretriggervolume.h>

#include <presentation/gui/ingame/guimanageringame.h>
#include <presentation/gui/ingame/guiscreenhud.h>
#include <ai/actor/intersectionlist.h> 

#include <roads/road.h>
//#include <roads/roadmanager.h>

#include <worldsim/avatarmanager.h>
#if defined(RAD_ANDROID) || defined(SRR2_OPENXR_PLATFORM_WIN32)
#include <vr/openxr_shared_input.h>
#endif
#include <worldsim/redbrick/vehicle.h>

//******************************************************************************
//
// Global Data, Local Data, Local Classes
//
//******************************************************************************
#if defined( DEBUGWATCH ) || defined( RAD_WIN32 )
float DEFAULT_DIST = 5.0f;
#else
const float DEFAULT_DIST = 5.0f;
#endif
//******************************************************************************
//
// Public Member Functions
//
//******************************************************************************

//==============================================================================
// CollectibleObjective::CollectibleObjective
//==============================================================================
// Description: Constructor.
//
// Parameters:	None.
//
// Return:      N/A.
//
//==============================================================================
CollectibleObjective::CollectibleObjective() :
    mNumCollectibles( 0 ),
    mNumCollected( 0 ),
    mPendingCollectibleIndex( -1 ),
    mCollectEffect( NULL ),
    mAllowUserDump( false ),
    mCurrentFocus( MAX_COLLECTIBLES )
{
    mEffectName[0] = '\0';
}

//==============================================================================
// CollectibleObjective::~CollectibleObjective
//==============================================================================
// Description: Destructor.
//
// Parameters:	None.
//
// Return:      N/A.
//
//==============================================================================
CollectibleObjective::~CollectibleObjective()
{
    if ( mCollectEffect )
    {
        delete mCollectEffect;
    }
}

//=============================================================================
// CollectibleObjective::MoveCollectible
//=============================================================================
// Description: Comment
//
// Parameters:  ( unsigned int num, const rmt::Vector& newPos )
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::MoveCollectible( unsigned int num, const rmt::Vector& newPos )
{
    rAssert( num < mNumCollectibles );

    mCollectibles[ num ].pLocator->SetLocation( newPos );

    TriggerLocator* trigLoc = dynamic_cast<TriggerLocator*>(mCollectibles[ num ].pLocator);
    rAssert( trigLoc );

    unsigned int numTriggers = trigLoc->GetNumTriggers();
    unsigned int i;
    for ( i = 0; i < numTriggers; ++i )
    {
        //These locators are always surrounded by their volumes.
        trigLoc->GetTriggerVolume( i )->SetPosition( newPos );
    }

    mCollectibles[ num ].mAnimatedIcon->Move( newPos );
}

//=============================================================================
// CollectibleObjective::AddCollectibleLocatorName
//=============================================================================
// Description: Comment
//
// Parameters:  ( const char* locatorname, const char* p3dname, float scale )
//              dialogName - number crunched from name of dialog event to trigger
//                           when collectible is collected
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::AddCollectibleLocatorName( const char* locatorname, 
                                                      const char* p3dname, 
                                                      radKey32 dialogName,
                                                      tUID speakerName,
                                                      float scale )
{
    rAssert( mNumCollectibles < MAX_COLLECTIBLES );
    strcpy( mCollectibles[ mNumCollectibles ].locatorName, locatorname );
    strcpy( mCollectibles[ mNumCollectibles ].p3dname, p3dname );
    mCollectibles[ mNumCollectibles ].fScaleFactor = scale;
    mCollectibles[ mNumCollectibles ].mDialogName = dialogName;
    mCollectibles[ mNumCollectibles ].mSpeakerName = speakerName;

    mNumCollectibles++;
}

//=============================================================================
// CollectibleObjective::SetCollectEffectName
//=============================================================================
// Description: Comment
//
// Parameters:  ( const char* name )
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::SetCollectEffectName( const char* name )
{
    rAssert( name );
    strcpy( mEffectName, name );
}

//=============================================================================
// CollectibleObjective::HandleEvent
//=============================================================================
// Description: Comment
//
// Parameters:  ( EventEnum id, void* pEventData )
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::HandleEvent( EventEnum id, void* pEventData )
{
    EventLocator* locator = static_cast<EventLocator*>( pEventData );
    if( !locator )
        return;

#if defined(RAD_ANDROID) || defined(SRR2_OPENXR_PLATFORM_WIN32)
    Avatar* avatar = GetAvatarManager()->GetAvatarForPlayer( 0 );
    if( !avatar || !avatar->IsInCar() )
    {
        int matchedIndex = -1;
        for( unsigned int i = 0; i < mNumCollectibles; ++i )
        {
            if( locator == mCollectibles[ i ].pLocator )
            {
                matchedIndex = static_cast<int>( i );
                break;
            }
        }

        if( locator->GetPlayerEntered() )
        {
            mPendingCollectibleIndex = matchedIndex;
            VRPICKUP_LOG("MISSION BODY_ENTER_ARMED locator=%p idx=%d", locator, matchedIndex);
        }
        else if( matchedIndex >= 0 && mPendingCollectibleIndex == matchedIndex )
        {
            VRPICKUP_LOG("MISSION BODY_EXIT_DISARMED locator=%p idx=%d", locator, matchedIndex);
            mPendingCollectibleIndex = -1;
        }
        // On-foot body events never collect.
        return;
    }
#endif

    if( !locator->GetPlayerEntered() )
        return;

    VRPICKUP_LOG("MISSION VEHICLE locator path locator=%p", locator);
    if( CheckCollectibleLocators( locator ) )
    {
        if( mNumCollected == mNumCollectibles )
            SetFinished( true );
    }
}


//******************************************************************************
//
// Private Member Functions
//
//******************************************************************************

//=============================================================================
// CollectibleObjective::OnUpdate
//=============================================================================
// Description: Comment
//
// Parameters:  ( unsigned int elapsedTime )
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::OnUpdate( unsigned int elapsedTime )
{
    OnUpdateCollectibleObjective( elapsedTime );
    
    unsigned int i;
    for ( i = 0; i < mNumCollectibles; ++i )
    {
        if ( !mCollectibles[ i ].bTriggered && mCollectibles[ i ].mAnimatedIcon )
        {
            mCollectibles[ i ].mAnimatedIcon->Update( elapsedTime );
        }

        if( mCollectibles[i].pLocator->GetFlag( Locator::ACTIVE ) )
        {
            UpdateLightPath(mCollectibles[i].mArrowPath);
        }
    }

    if ( mCollectEffect )
    {
        mCollectEffect->Update( elapsedTime );
    }

#if defined(RAD_ANDROID) || defined(SRR2_OPENXR_PLATFORM_WIN32)
    // Proven functional interaction model: mission body trigger selects the
    // nearby mission collectible, but only a grip press can collect it.
    Avatar* avatar = GetAvatarManager()->GetAvatarForPlayer( 0 );
    if( avatar && !avatar->IsInCar() && mPendingCollectibleIndex >= 0 &&
        static_cast<unsigned int>(mPendingCollectibleIndex) < mNumCollectibles )
    {
        const unsigned int idx = static_cast<unsigned int>( mPendingCollectibleIndex );
        if( mCollectibles[ idx ].bTriggered || !mCollectibles[ idx ].pLocator ||
            !mCollectibles[ idx ].pLocator->GetFlag( Locator::ACTIVE ) )
        {
            mPendingCollectibleIndex = -1;
        }
        else
        {
            static const float GRIP_THRESHOLD = 0.20f;
            static const float HAND_PROXIMITY_RADIUS = 0.70f;

            rmt::Vector itemPos;
            // Measure against the rendered collectible centre, not the locator
            // trigger origin (which is often at ground level below the model).
            if( mCollectibles[ idx ].mAnimatedIcon )
                mCollectibles[ idx ].mAnimatedIcon->GetPosition( itemPos );
            else
                mCollectibles[ idx ].pLocator->GetLocation( &itemPos );

            bool handGripMatch = false;
            unsigned matchedHand = 0;
            float matchedGrip = 0.0f;
            float matchedDistance = -1.0f;

            for( unsigned hand = 0; hand < 2; ++hand )
            {
                const float grip = SharOpenXR::GetHandGripValue( hand );
                if( grip < 0.05f )
                    continue;

                rmt::Vector handPos;
                const bool handValid = SharOpenXR::GetHandWorldPosition( hand, &handPos );
                float distance = -1.0f;
                if( handValid )
                {
                    rmt::Vector delta = handPos;
                    delta.Sub( itemPos );
                    distance = delta.Magnitude();
                }

                VRPICKUP_LOG("MISSION HAND_PROBE idx=%u hand=%u grip=%.3f valid=%d distance=%.3f radius=%.2f item=(%.3f %.3f %.3f) handpos=(%.3f %.3f %.3f)",
                             idx, hand, grip, handValid ? 1 : 0, distance,
                             HAND_PROXIMITY_RADIUS, itemPos.x, itemPos.y, itemPos.z,
                             handValid ? handPos.x : 0.0f, handValid ? handPos.y : 0.0f,
                             handValid ? handPos.z : 0.0f);

                if( grip >= GRIP_THRESHOLD && handValid && distance <= HAND_PROXIMITY_RADIUS )
                {
                    handGripMatch = true;
                    matchedHand = hand;
                    matchedGrip = grip;
                    matchedDistance = distance;
                    break;
                }
            }

            if( handGripMatch )
            {
                bool shouldReset = false;
                if( OnCollection( idx, shouldReset ) )
                {
                    VRPICKUP_LOG("MISSION HAND_GRIP_SUCCESS idx=%u hand=%u grip=%.3f distance=%.3f",
                                 idx, matchedHand, matchedGrip, matchedDistance);
                    Collect( idx, shouldReset );
                    if( mNumCollected == mNumCollectibles )
                        SetFinished( true );
                }
                mPendingCollectibleIndex = -1;
            }
        }
    }
#endif
}

//=============================================================================
// CollectibleObjective::OnInitalize
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::OnInitialize()
{
MEMTRACK_PUSH_GROUP( "Mission - Collectible Objective" );
    GameMemoryAllocator gma = GetGameplayManager()->GetCurrentMissionHeap();

    for( unsigned int i = 0; i < mNumCollectibles; i++ )
    {
        rAssert( mCollectibles[ i ].pLocator == NULL );

        mCollectibles[ i ].pLocator = p3d::find<Locator>( mCollectibles[ i ].locatorName );

#ifndef FINAL
        if (mCollectibles[ i ].pLocator == NULL)
        {
		    char errorbuffer[255];
		    sprintf(errorbuffer,"ERROR: Can't Find Locator %s, Make Sure you Loaded it!!!!\n", mCollectibles[ i ].locatorName);			
		    rReleasePrintf(errorbuffer);
        }

#endif

        rAssert( mCollectibles[ i ].pLocator != NULL );
        mCollectibles[ i ].pLocator->AddRef();

        // get the locator's position
		rmt::Vector locPos;
        mCollectibles[ i ].pLocator->GetLocation( &locPos );

        //Try to make it an event locator
        EventLocator* eventLocator = dynamic_cast<EventLocator*>( mCollectibles[ i ].pLocator );
        ////////////////////////////////////////////////////////////////////////
		if( strcmp( mCollectibles[ i ].p3dname, "" ) != 0 )
		{
		    HeapMgr()->PushHeap(GetGameplayManager()->GetCurrentMissionHeap());
			mCollectibles[ i ].mAnimatedIcon = new AnimatedIcon();
		    HeapMgr()->PopHeap(GetGameplayManager()->GetCurrentMissionHeap());

            //If the locator *happens* to be an oriented (eventLocator) locator, we will 
            //pass in it's matrix.
            if ( eventLocator != NULL )
            {
                rmt::Matrix mat = eventLocator->GetMatrix();
                mat.FillTranslate( locPos );

                mCollectibles[ i ].mAnimatedIcon->Init( mCollectibles[ i ].p3dname, mat, false );
            }
            else 
            {
                mCollectibles[ i ].mAnimatedIcon->Init( mCollectibles[ i ].p3dname, locPos, false );
            }
		}

        mCollectibles[ i ].bTriggered = false;

        if( eventLocator != NULL )
        {
            GetEventManager()->AddListener( this, (EventEnum)(EVENT_LOCATOR + eventLocator->GetEventType()));

            if ( !rmt::Epsilon( mCollectibles[ i ].fScaleFactor, 0.0f ) )
            {
                // Reinterpret cast does not do what you think it does. 
                // Use a dynamic cast to check the type
                TriggerVolume* tv = eventLocator->GetTriggerVolume( 0 );
                rTuneAssert( dynamic_cast< SphereTriggerVolume* >( tv ) != NULL );
                SphereTriggerVolume* sphT = reinterpret_cast<SphereTriggerVolume*>( tv );
                rAssertMsg( sphT, "Collectibles should only use sphere trigger volumes!" );

                if ( sphT )
                {
                    sphT->SetSphereRadius( mCollectibles[ i ].fScaleFactor );
                }
            }
	   		eventLocator->SetFlag(Locator::ACTIVE, false);
        }

        ActionEventLocator* pActionEventLocator = dynamic_cast<ActionEventLocator*>( mCollectibles[ i ].pLocator );
        if ( pActionEventLocator )
        {
            pActionEventLocator->Reset( );
        }
    }

    GetEventManager()->AddListener( this, EVENT_OBJECT_DESTROYED );

    //Setup the collectibles
    OnInitCollectibles();

    //Setup the objective
    OnInitCollectibleObjective();

    mNumCollected = 0;

    //Set up the effect;
    if ( mEffectName[0] != '\0' && strcmp( mEffectName, "none" ) != 0 )
    {
        mCollectEffect = new AnimatedIcon();
        mCollectEffect->Init( mEffectName, rmt::Vector( 0.0f, 0.0f, 0.0f ), false, true );
    }

    MEMTRACK_POP_GROUP("Mission - Collectible Objective"); 
}

//=============================================================================
// CollectibleObjective::GetCollectiblePathInfo
//=============================================================================
// Description: Comment
//
// Parameters:  ( unsigned int index, RoadManager::PathElement& elem, float& roadT )
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::GetCollectiblePathInfo( unsigned int index, RoadManager::PathElement& elem, float& roadT )
{
    rAssert( index < mNumCollectibles );

    elem = mCollectibles[ index ].elem;
    roadT = mCollectibles[ index ].roadT;
}

//=============================================================================
// CollectibleObjective::OnFinalize
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::OnFinalize()
{
    mPendingCollectibleIndex = -1;
    //Finalize the collectible objective
    OnFinalizeCollectibleObjective();

    unsigned int i;
    for( i = 0; i < mNumCollectibles; i++ )
    {
        rAssert( mCollectibles[ i ].pLocator != NULL );
        Activate( i, false, false );

        mCollectibles[ i ].pLocator->Release();
        mCollectibles[ i ].pLocator = NULL;

		if( mCollectibles[ i ].mAnimatedIcon )
		{
            delete mCollectibles[ i ].mAnimatedIcon;
			mCollectibles[ i ].mAnimatedIcon = NULL;
		}
    }

    GetEventManager()->RemoveAll( this );

    if ( mCollectEffect )
    {
        delete mCollectEffect;
        mCollectEffect = NULL;
    }
}

//=============================================================================
// CollectibleObjective::CheckCollectibleLocators
//=============================================================================
// Description: Comment
//
// Parameters:  ( Locator* locator )
//
// Return:      void 
//
//=============================================================================
bool CollectibleObjective::CheckCollectibleLocators( Locator* locator )
{
    bool found = false;
    unsigned int i;
    for( i = 0; i < mNumCollectibles; i++ )
    {
        if( locator == mCollectibles[ i ].pLocator ) 
        {
            VRPICKUP_LOG("MISSION CheckCollectibleLocators MATCH idx=%u locator=%p", i, locator);
            bool shouldReset = false;
            if ( OnCollection( i, shouldReset ) )
            {
                VRPICKUP_LOG("MISSION LOCATOR_PATH calling Collect idx=%u shouldReset=%d",
                             i, (int)shouldReset);
                Collect( i, shouldReset );
                found = true;
                break;
            }
        }
    }

    return found;
}

//=============================================================================
// CollectibleObjective::Collect
//=============================================================================
// Description: Comment
//
// Parameters:  ( unsigned int index, bool shouldReset )
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::Collect( unsigned int index, bool shouldReset )
{
    rAssert( index >= 0 && index < mNumCollectibles
        && mCollectibles[ index ].pLocator != NULL );

    rmt::Vector dbgItem(0.0f, 0.0f, 0.0f);
    mCollectibles[ index ].pLocator->GetLocation( &dbgItem );
    Avatar* dbgAvatar = GetAvatarManager()->GetAvatarForPlayer( 0 );
    VRPICKUP_LOG("MISSION COLLECT idx=%u shouldReset=%d alreadyTriggered=%d locator=%p avatar=%p incar=%d item=(%.3f %.3f %.3f)",
                 index, (int)shouldReset, (int)mCollectibles[ index ].bTriggered,
                 mCollectibles[ index ].pLocator, dbgAvatar,
                 dbgAvatar ? (int)dbgAvatar->IsInCar() : -1,
                 dbgItem.x, dbgItem.y, dbgItem.z);

    if( !mCollectibles[ index ].bTriggered )
    {
        mNumCollected += 1;

        mCollectibles[ index ].bTriggered = true;
        Activate( index, false, false );

        GetEventManager()->TriggerEvent( EVENT_MISSION_COLLECTIBLE_PICKED_UP );

        if( mCollectibles[index].mDialogName != 0 )
        {
            DialogEventData data;

            //
            // Make with the funny talk
            //
            data.dialogName = mCollectibles[index].mDialogName;

            if( mCollectibles[index].mSpeakerName != static_cast< tUID >( 0 ) )
            {
                data.charUID1 = mCollectibles[index].mSpeakerName;
            }
            else
            {
                //
                // No character specified, use player character
                //
                data.char1 = GetAvatarManager()->GetAvatarForPlayer( 0 )->GetCharacter();
            }

            GetEventManager()->TriggerEvent( EVENT_IN_GAMEPLAY_CONVERSATION, static_cast<void*>(&data) );
        }

        //I do this so that races that have multiple laps don't get screwed because
        //of shared waypoints.
        if ( shouldReset )
        {
            mCollectibles[ index ].pLocator->SetFlag( Locator::ACTIVE, true );
            mCollectibles[ index ].bTriggered = false;
        }   
    }

    if ( mCollectEffect )
    {
        mCollectEffect->Reset();
        rmt::Vector pos;
        mCollectibles[ index ].pLocator->GetLocation( &pos );
        mCollectEffect->Move( pos );
        mCollectEffect->ShouldRender( true );
    }
    else
    {
        //Only show the effect if there is a drawable.
        if ( strcmp( mCollectibles[ index ].p3dname, "" ) != 0 && strcmp( mEffectName, "none" ) != 0 )
        {
            //USe the default one from the mission manager
            rmt::Vector pos;
            mCollectibles[ index ].pLocator->GetLocation( &pos );
            GetMissionManager()->PutEffectHere( pos );  
        }
    }

    //Disable the NAV system for this collectible.
    UnlightPath( mCollectibles[index].mArrowPath.mPathRoute );
}


//=============================================================================
// CollectibleObjective::Activate
//=============================================================================
// Description: Comment
//
// Parameters:  ( unsigned int index, bool bIsActive, bool primary, HudMapIcon::eIconType icon )
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::Activate( unsigned int index, 
                                     bool bIsActive, 
                                     bool primary, 
                                     HudMapIcon::eIconType icon,
                                     bool render )
{
    rAssert( index < mNumCollectibles );

    bool alreadyActive = mCollectibles[ index ].pLocator->GetFlag( Locator::ACTIVE );

    mCollectibles[ index ].pLocator->SetFlag( Locator::ACTIVE, bIsActive );
    
    if( bIsActive )
    {
		if( mCollectibles[ index ].mAnimatedIcon != NULL )
		{
            mCollectibles[ index ].mAnimatedIcon->ShouldRender( render );

            if ( mCollectibles[ index ].iHUDIndex == -1 )
            {
                //Only put up an icon in the HUD if there is a drawable in the world.
                RegisterLocator( mCollectibles[ index ].pLocator, mCollectibles[ index ].iHUDIndex, primary, icon );
            }
		}
    }
    else if( !bIsActive && alreadyActive)
    {
		if( mCollectibles[ index ].mAnimatedIcon != NULL )
		{
            mCollectibles[ index ].mAnimatedIcon->ShouldRender( false );

            UnregisterLocator( mCollectibles[ index ].iHUDIndex );
		}

        //Just in case
        UnlightPath( mCollectibles[index].mArrowPath.mPathRoute );
    }
}

//=============================================================================
// CollectibleObjective::SetFocus
//=============================================================================
// Description: Comment
//
// Parameters:  ( unsigned int index )
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::SetFocus( unsigned int index )
{
    rAssert( index < mNumCollectibles );

    CGuiScreenMultiHud* currentHud = GetCurrentHud();
    if( currentHud )
    {
        currentHud->GetHudMap( 0 )->SetFocalPointIcon( mCollectibles[ index ].iHUDIndex );
    }    

    rmt::Vector posn;
    mCollectibles[index].pLocator->GetPosition(&posn);

    if ( index == mCurrentFocus )
    {
        UnlightPath( mCollectibles[index].mArrowPath.mPathRoute );
    }

    LightPath( posn, mCollectibles[index].mArrowPath );
    mCurrentFocus = index;
}

//=============================================================================
// CollectibleObjective::ChangeIcon
//=============================================================================
// Description: Comment
//
// Parameters:  ( unsigned int index, HudMapIcon::eIconType type )
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::ChangeIcon( unsigned int index, HudMapIcon::eIconType type )
{
    rAssert( index < mNumCollectibles );

    CGuiScreenMultiHud* currentHud = GetCurrentHud();
    if( currentHud )
    {
        mCollectibles[ index ].iHUDIndex = ChangeIconType( mCollectibles[ index ].iHUDIndex, type );
    }    
}

//=============================================================================
// CollectibleObjective::ResetCollectibles
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::ResetCollectibles()
{
    unsigned int i;
    for ( i = 0; i < mNumCollectibles; ++i )
    {
        mCollectibles[ i ].bTriggered = false;
        mCollectibles[ i ].pLocator->SetFlag( Locator::ACTIVE, true );
        UnlightPath( mCollectibles[ i ].mArrowPath.mPathRoute );
    }

    //I hate to do this, but it should only be this way for a very brief amount
    //of time and no one should be able to query it and get the bad, bad value it represents.
    mNumCollected = ~0;//hmmmm
}

//=============================================================================
// CollectibleObjective::DumpCollectible
//=============================================================================
// Description: Comment
//
// Parameters:  ( int collectibleNum, Vehicle* dumper, Vehicle* hitter, bool useIntersectionList )
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::DumpCollectible( int collectibleNum, Vehicle* dumper, Vehicle* hitter, bool useIntersectionList, bool terminal )
{

    //If it is, drop the collectible on the ground where the car is.
    //Also, set the focus on the collectible.
    rmt::Vector newPos;
    dumper->GetPosition( &newPos );
    
    rmt::Vector dumperPos;
    dumper->GetPosition( &dumperPos );

    if(!terminal) // final dump always goes right where car is
    {
        if ( hitter )
        {
            rmt::Vector eNorm( 0.0f, 0.0f, 0.0f );

            rmt::Vector hitterHeading;
            hitter->GetHeading( &hitterHeading );

            rmt::Vector dumpHeading;
            dumper->GetHeading( &dumpHeading );



            if ( rmt::Fabs( hitterHeading.DotProduct( dumpHeading ) ) > 0.5f )
            {
                rmt::Vector toHitter;
                hitter->GetPosition( &toHitter);
                
                toHitter.Sub(dumperPos);
                toHitter.Normalize();

                eNorm = dumper->mTransform.Row(0);

                
                if( rmt::Fabs(dumpHeading.DotProduct(toHitter)) < 0.5)
                {
                    if ( toHitter.DotProduct(eNorm) < 0 )
                    {
                        eNorm *= -1.0f;
                    }
                }
                else
                {
                    if ( rand() % 2 == 1 )
                    {
                        eNorm *= -1.0f;
                    }
                }

                eNorm += dumpHeading;
                eNorm.Normalize();
            }
            else
            {
                eNorm = hitterHeading;
            }

            eNorm *= DEFAULT_DIST;
            newPos.Add( eNorm );    
            
            //Make sure this hits the ground
            rmt::Vector vWaste;
            bool found;
            GetIntersectManager()->FindIntersection( newPos, found, vWaste, newPos );
            if ( !found )
            {
                dumper->GetPosition( &newPos );
            }    
        }

        if ( useIntersectionList )
        {
            // A holds pointers to sim::CollisionObjects and provides an interface to querying 
            // for LineOfSight and getting find the closest intersection along a line segment
            IntersectionList staticIntersectionList;    
            // Lets try and minimize the size of our query
            // Center = ( start + end ) / 2
            // radius = abs( ( start - end ) / 2 )
            rmt::Vector queryCenter = ( newPos + dumperPos ) / 2.0f;
            float queryRadius = ( newPos - dumperPos ).Magnitude() / 2.0f; // ugh
            // Grab all the static objects (and fence pieces) in the given area
            staticIntersectionList.FillIntersectionListStatics( queryCenter, queryRadius );
            rmt::Vector intersection;
            // Did we hit anything?
            if ( staticIntersectionList.TestIntersectionStatics( dumperPos, newPos, &intersection ) )
            {
                // Hit something. Lets adjust newpos so that it is halfway between the dumper and the intersection
                // Sound like a good compromise Cary?
                newPos = ( queryCenter + intersection ) / 2.0f;
                // Better do a ground intersection query in case we got some REALLY uneven terrain
                rmt::Vector vWaste;
                bool found;
                GetIntersectManager()->FindIntersection( newPos, found, vWaste, newPos );
            }
        }
    }

    if ( rmt::Fabs( newPos.y - dumperPos.y ) > 5.0f )
    {
        newPos.y = dumperPos.y;
    }

    MoveCollectible( collectibleNum, newPos );
    Activate( collectibleNum, true, false );
}

//=============================================================================
// CollectibleObjective::GetAnyCollectedID
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      int 
//
//=============================================================================
int CollectibleObjective::GetAnyCollectedID() const
{
    unsigned int i;
    for ( i = 0; i < mNumCollectibles; ++i )
    {
        if ( mCollectibles[ i ].bTriggered )
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}

//=============================================================================
// CollectibleObjective::Uncollect
//=============================================================================
// Description: Comment
//
// Parameters:  ( int collectibleNum )
//
// Return:      void 
//
//=============================================================================
void CollectibleObjective::Uncollect( int collectibleNum )
{
    if ( collectibleNum > -1 && collectibleNum < static_cast<int>(mNumCollectibles) )
    {
        mCollectibles[ collectibleNum ].bTriggered = false;
        mNumCollected--;

        //Trigger an event
        GetEventManager()->TriggerEvent( EVENT_LOSE_COLLECTIBLE );
    }
}



//******************************************************************************
//
// Private Member Functions
//
//******************************************************************************

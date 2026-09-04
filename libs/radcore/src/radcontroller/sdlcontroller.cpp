//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


//=============================================================================
//
// File:        sdlcontroller.cpp
//
// Subsystem:	Foundation Technologies - Controller System
//
// Description:	This file contains the implementation of the Foundation 
//              Technologies sdl contoller
//
// Date:    	
//
//=============================================================================

//============================================================================
// Include Files
//============================================================================

#include "pch.hpp"
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <radobject.hpp>
#include <radcontroller.hpp>
#include <raddebug.hpp>
#include <radstring.hpp>
#include <radobjectlist.hpp>
#include <radtime.hpp>
#include <radmemorymonitor.hpp>
#include "radcontrollerbuffer.hpp"

#include <SDL.h>

#if defined(_WIN32) && !defined(ANDROID)
static int s_SdlVirtualKeyToIndex[256];
struct SdlVirtualKeyTableInitializer
{
    SdlVirtualKeyTableInitializer()
    {
        for(unsigned i=0;i<256;++i) s_SdlVirtualKeyToIndex[i]=-1;
    }
} s_SdlVirtualKeyTableInitializer;
const int* VirtualKeyToIndex=&s_SdlVirtualKeyToIndex[-1];
#endif

#if defined(RAD_ANDROID)
#include <SDL_system.h>
#include <jni.h>
#endif




//============================================================================
// Internal Interfaces
//============================================================================

struct IRadControllerInputPointSDL
    :
    public IRadControllerInputPoint
{
	virtual void iInitialize( void ) = 0;
    virtual void iVirtualTimeReMapped( unsigned int virtualTime ) = 0;
    virtual void iVirtualTimeChanged( unsigned int virtualTime ) = 0;
};

struct IRadControllerSDL
    :
    public IRadController
{
    virtual void iPoll( unsigned int virtualTime ) = 0;
    virtual void iVirtualTimeReMapped( unsigned int virtualTime ) = 0;
    virtual void iVirtualTimeChanged( unsigned int virtualTime ) = 0;
    virtual void iSetBufferTime( unsigned int milliseconds, unsigned int pollingRate ) = 0;
};

//============================================================================
// Globals
//============================================================================

struct SDLInputPoint
{
    const char * m_pType;
    const char * m_pName;
    unsigned int m_Mask;
};

static const char * g_Sdlipt[] =
{
    "Button",
    "AnalogButton",
    "XAxis",
    "YAxis"
};

static SDLInputPoint g_SDLPoints[] =
{
#if SDL_MAJOR_VERSION < 3
    { g_Sdlipt[ 0 ], "DPadUp",           SDL_CONTROLLER_BUTTON_DPAD_UP },
    { g_Sdlipt[ 0 ], "DPadDown",         SDL_CONTROLLER_BUTTON_DPAD_DOWN },
    { g_Sdlipt[ 0 ], "DPadLeft",         SDL_CONTROLLER_BUTTON_DPAD_LEFT },
    { g_Sdlipt[ 0 ], "DPadRight",        SDL_CONTROLLER_BUTTON_DPAD_RIGHT },
    { g_Sdlipt[ 0 ], "Start",            SDL_CONTROLLER_BUTTON_START },
    { g_Sdlipt[ 0 ], "Back",             SDL_CONTROLLER_BUTTON_BACK },
    { g_Sdlipt[ 0 ], "LeftThumb",        SDL_CONTROLLER_BUTTON_LEFTSTICK },
    { g_Sdlipt[ 0 ], "RightThumb",       SDL_CONTROLLER_BUTTON_RIGHTSTICK },
#ifdef __SWITCH__
    { g_Sdlipt[ 0 ], "A",                SDL_CONTROLLER_BUTTON_B },
    { g_Sdlipt[ 0 ], "B",                SDL_CONTROLLER_BUTTON_A },
    { g_Sdlipt[ 0 ], "X",                SDL_CONTROLLER_BUTTON_Y },
    { g_Sdlipt[ 0 ], "Y",                SDL_CONTROLLER_BUTTON_X },
#else
    { g_Sdlipt[ 0 ], "A",                SDL_CONTROLLER_BUTTON_A },
    { g_Sdlipt[ 0 ], "B",                SDL_CONTROLLER_BUTTON_B },
    { g_Sdlipt[ 0 ], "X",                SDL_CONTROLLER_BUTTON_X },
    { g_Sdlipt[ 0 ], "Y",                SDL_CONTROLLER_BUTTON_Y },
#endif
    { g_Sdlipt[ 0 ], "Black",            SDL_CONTROLLER_BUTTON_LEFTSHOULDER },
    { g_Sdlipt[ 0 ], "White",            SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
    { g_Sdlipt[ 1 ], "LeftTrigger",      SDL_CONTROLLER_AXIS_TRIGGERLEFT },
    { g_Sdlipt[ 1 ], "RightTrigger",     SDL_CONTROLLER_AXIS_TRIGGERRIGHT },
    { g_Sdlipt[ 2 ], "LeftStickX",       SDL_CONTROLLER_AXIS_LEFTX },
    { g_Sdlipt[ 3 ], "LeftStickY",       SDL_CONTROLLER_AXIS_LEFTY },
    { g_Sdlipt[ 2 ], "RightStickX",      SDL_CONTROLLER_AXIS_RIGHTX },
    { g_Sdlipt[ 3 ], "RightStickY",      SDL_CONTROLLER_AXIS_RIGHTY }
#else
    { g_Sdlipt[ 0 ], "DPadUp",           SDL_GAMEPAD_BUTTON_DPAD_UP },
    { g_Sdlipt[ 0 ], "DPadDown",         SDL_GAMEPAD_BUTTON_DPAD_DOWN },
    { g_Sdlipt[ 0 ], "DPadLeft",         SDL_GAMEPAD_BUTTON_DPAD_LEFT },
    { g_Sdlipt[ 0 ], "DPadRight",        SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
    { g_Sdlipt[ 0 ], "Start",            SDL_GAMEPAD_BUTTON_START },
    { g_Sdlipt[ 0 ], "Back",             SDL_GAMEPAD_BUTTON_BACK },
    { g_Sdlipt[ 0 ], "LeftThumb",        SDL_GAMEPAD_BUTTON_LEFT_STICK },
    { g_Sdlipt[ 0 ], "RightThumb",       SDL_GAMEPAD_BUTTON_RIGHT_STICK },
#ifdef __SWITCH__
    { g_Sdlipt[ 0 ], "A",                SDL_GAMEPAD_BUTTON_EAST },
    { g_Sdlipt[ 0 ], "B",                SDL_GAMEPAD_BUTTON_SOUTH },
    { g_Sdlipt[ 0 ], "X",                SDL_GAMEPAD_BUTTON_NORTH },
    { g_Sdlipt[ 0 ], "Y",                SDL_GAMEPAD_BUTTON_WEST },
#else
    { g_Sdlipt[ 0 ], "A",                SDL_GAMEPAD_BUTTON_SOUTH },
    { g_Sdlipt[ 0 ], "B",                SDL_GAMEPAD_BUTTON_EAST },
    { g_Sdlipt[ 0 ], "X",                SDL_GAMEPAD_BUTTON_WEST },
    { g_Sdlipt[ 0 ], "Y",                SDL_GAMEPAD_BUTTON_NORTH },
#endif
    { g_Sdlipt[ 0 ], "Black",            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER },
    { g_Sdlipt[ 0 ], "White",            SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
    { g_Sdlipt[ 1 ], "LeftTrigger",      SDL_GAMEPAD_AXIS_LEFT_TRIGGER },
    { g_Sdlipt[ 1 ], "RightTrigger",     SDL_GAMEPAD_AXIS_RIGHT_TRIGGER },
    { g_Sdlipt[ 2 ], "LeftStickX",       SDL_GAMEPAD_AXIS_LEFTX },
    { g_Sdlipt[ 3 ], "LeftStickY",       SDL_GAMEPAD_AXIS_LEFTY },
    { g_Sdlipt[ 2 ], "RightStickX",      SDL_GAMEPAD_AXIS_RIGHTX },
    { g_Sdlipt[ 3 ], "RightStickY",      SDL_GAMEPAD_AXIS_RIGHTY }
#endif
};

static class radControllerSystemSDL* s_pTheSDLControllerSystem2 = NULL;
static radMemoryAllocator g_ControllerSystemAllocator = RADMEMORY_ALLOC_DEFAULT;






#if defined(RAD_ANDROID)
// Es decir el touch entrará como mando principal respetando la estructura del juego de puertos, en este caso puerto 0
static const char* ANDROID_TOUCH_CONTROLLER_LOCATION = "Port0\\Slot0";

typedef bool (*AndroidRumblePolicyCallback)();

static AndroidRumblePolicyCallback sAndroidRumblePolicyCallback = NULL;

extern "C" void radControllerSDLSetAndroidRumblePolicyCallback(
    AndroidRumblePolicyCallback callback
)
{
    sAndroidRumblePolicyCallback = callback;
}

typedef void (*AndroidGamepadCandidateConnectedCallback)(int instanceId);
typedef void (*AndroidGamepadInputCallback)(int instanceId, float magnitude);
typedef void (*AndroidGamepadDisconnectedCallback)(int instanceId);

static AndroidGamepadCandidateConnectedCallback
    sAndroidGamepadCandidateConnectedCallback = NULL;

static AndroidGamepadInputCallback
    sAndroidGamepadInputCallback = NULL;

static AndroidGamepadDisconnectedCallback
    sAndroidGamepadDisconnectedCallback = NULL;

extern "C" void radControllerSDLSetAndroidGamepadCandidateConnectedCallback
(
    AndroidGamepadCandidateConnectedCallback callback
)
{
    sAndroidGamepadCandidateConnectedCallback = callback;
}

extern "C" void radControllerSDLSetAndroidGamepadInputCallback
(
    AndroidGamepadInputCallback callback
)
{
    sAndroidGamepadInputCallback = callback;
}

extern "C" void radControllerSDLSetAndroidGamepadDisconnectedCallback
(
    AndroidGamepadDisconnectedCallback callback
)
{
    sAndroidGamepadDisconnectedCallback = callback;
}

#if SDL_MAJOR_VERSION < 3
static int AndroidGetGamepadInstanceId( SDL_GameController* pController )
{
    if ( pController == NULL )
    {
        return -1;
    }

    SDL_Joystick* pJoystick =
        SDL_GameControllerGetJoystick( pController );

    if ( pJoystick == NULL )
    {
        return -1;
    }

    return static_cast<int>( SDL_JoystickInstanceID( pJoystick ) );
}
#else
static int AndroidGetGamepadInstanceId( SDL_Gamepad* pController )
{
    if ( pController == NULL )
    {
        return -1;
    }

    SDL_Joystick* pJoystick =
        SDL_GetGamepadJoystick( pController );

    if ( pJoystick == NULL )
    {
        return -1;
    }

    return static_cast<int>( SDL_GetJoystickID( pJoystick ) );
}
#endif

static void AndroidNotifyPhysicalGamepadCandidateConnected
(
    int instanceId
)
{
    if
    (
        instanceId >= 0 &&
        sAndroidGamepadCandidateConnectedCallback != NULL
    )
    {
        sAndroidGamepadCandidateConnectedCallback( instanceId );
    }
}

static void AndroidNotifyPhysicalGamepadDisconnected
(
    int instanceId
)
{
    if
    (
        instanceId >= 0 &&
        sAndroidGamepadDisconnectedCallback != NULL
    )
    {
        sAndroidGamepadDisconnectedCallback( instanceId );
    }
}


static bool AndroidIsGamepadRumbleAllowed()
{
    if ( sAndroidRumblePolicyCallback == NULL )
    {
        return false;
    }

    return sAndroidRumblePolicyCallback();
}

static void AndroidNotifyPhysicalGamepadInputFromPoint
(
#if SDL_MAJOR_VERSION < 3
    SDL_GameController* pController,
#else
    SDL_Gamepad* pController,
#endif
    const char* pType,
    float oldValue,
    float newValue
)
{
    if ( sAndroidGamepadInputCallback == NULL )
    {
        return;
    }

    const int instanceId = AndroidGetGamepadInstanceId( pController );

    if ( instanceId < 0 )
    {
        return;
    }

    float magnitude = 0.0f;

    /*
     * Confirm the gamepad only from real button/trigger input.
     * Do not confirm from stick axes, because some Android devices can
     * expose noisy internal axes.
     */
    if ( pType == g_Sdlipt[ 0 ] ) // Button
    {
        if ( oldValue <= 0.5f && newValue > 0.5f )
        {
            magnitude = 1.0f;
        }
    }
    else if ( pType == g_Sdlipt[ 1 ] ) // AnalogButton / trigger
    {
        if ( oldValue <= 0.5f && newValue > 0.5f )
        {
            magnitude = newValue;
        }
    }

    if ( magnitude > 0.0f )
    {
        sAndroidGamepadInputCallback( instanceId, magnitude );
    }
}



static bool AndroidJavaRumbleFallback(float intensity, int length)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (env == NULL)
    {
       
        return false;
    }

    jclass cls = env->FindClass("org/libsdl/app/SDLControllerManager");
    if (cls == NULL)
    {
        
        if (env->ExceptionCheck())
        {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        return false;
    }

    jmethodID mid = env->GetStaticMethodID(cls, "rumbleFallback", "(FI)Z");
    if (mid == NULL)
    {
        
        if (env->ExceptionCheck())
        {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        env->DeleteLocalRef(cls);
        return false;
    }

    jboolean result = env->CallStaticBooleanMethod(cls, mid, (jfloat)intensity, (jint)length);
    if (env->ExceptionCheck())
    {
        
        env->ExceptionDescribe();
        env->ExceptionClear();
        env->DeleteLocalRef(cls);
        return false;
    }

    env->DeleteLocalRef(cls);
    return result == JNI_TRUE;
}

static void AndroidJavaStopRumbleFallback()
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (env == NULL)
    {
        
        return;
    }

    jclass cls = env->FindClass("org/libsdl/app/SDLControllerManager");
    if (cls == NULL)
    {
        
        if (env->ExceptionCheck())
        {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        return;
    }

    jmethodID mid = env->GetStaticMethodID(cls, "stopRumbleFallback", "()V");
    if (mid == NULL)
    {
        
        if (env->ExceptionCheck())
        {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        env->DeleteLocalRef(cls);
        return;
    }

    env->CallStaticVoidMethod(cls, mid);
    if (env->ExceptionCheck())
    {
       
        env->ExceptionDescribe();
        env->ExceptionClear();
    }

    env->DeleteLocalRef(cls);
}
#endif
//============================================================================
// Component: radControllerOutputPointSDL
//============================================================================

class radControllerOutputPointSDL
    :
    public IRadControllerOutputPoint,
    public radRefCount
{
    public:

    IMPLEMENT_REFCOUNTED( "radControllerOutputPointSDL" )

    //========================================================================
    // radControllerOutputPointSDL::rSDLControllerOutputPoint
    //========================================================================

    radControllerOutputPointSDL( const char * pName )
        :
        radRefCount( 0 ),
        m_pName( pName ),
        m_Gain( 0.0f )
    {
        radMemoryMonitorIdentifyAllocation( this, g_nameFTech, "radControllerOutputPointSDL" );
    }

    //========================================================================
    // radControllerOutputPointSDL::~rSDLControllerOutputPoint
    //========================================================================

    ~radControllerOutputPointSDL( void )
    {
    }

    //========================================================================
    // radControllerOutputPointSDL::GetName
    //========================================================================

    virtual const char * GetName( void )
    {
        return m_pName;
    }

    //========================================================================
    // radControllerOutputPointSDL::GetType
    //========================================================================

    virtual const char * GetType( void )
    {
        return "Analog";
    }

    //========================================================================
    // radControllerOutputPointSDL::GetGain
    //========================================================================

    virtual float GetGain( void )
    {
        return m_Gain;
    }

    //========================================================================
    // radControllerOutputPointSDL::SetGain
    //========================================================================

    virtual void SetGain( float value )
    {
        if ( value < 0.0f )
        {
            value = 0.0f;
        }
        else if ( value > 1.0f )
        {
            value = 1.0f;
        }
		
		

        m_Gain = value;
    }

    // FIXME
    long GetOffset() const { return 0; }
#ifdef WIN32
    void UpdateEffect(const DIEFFECT*) {}
#else
    void UpdateEffect(const SDL_HapticEffect*) {}
#endif
    void Start() {}
    void Stop() {}
    void ReleaseEffect() {}

    //========================================================================
    // radControllerOutputPointSDL Data Members
    //========================================================================

    const char * m_pName;
    float m_Gain;
};

//============================================================================
// Component: radControllerInputPointSDL
//============================================================================

class radControllerInputPointSDL
    :
    public IRadControllerInputPointSDL,
    public radRefCount
{
    public:

    IMPLEMENT_REFCOUNTED( "radControllerInputPointSDL" )

    //========================================================================
    // radControllerInputPointSDL::iIVirtualTimeReMapped
    //========================================================================

    virtual void iVirtualTimeReMapped( unsigned int virtualTime )
    {
        //
        // The client has done a re-sync to game time, all we can do is
        // set our changed-state time to "now".
        //
        m_TimeInState = 0;
        m_TimeOfStateChange = virtualTime;
    }

    //========================================================================
    // radControllerInputPointSDL::CalculateNewValue
    //========================================================================

	float CalculateNewValue( void )
{
   
   

    float newValue = 0.0f;

        if ( m_pController != NULL )
        {
            if ( m_pType == g_Sdlipt[ 0 ] ) // Button
            {
#if SDL_MAJOR_VERSION < 3
                newValue = SDL_GameControllerGetButton( m_pController, (SDL_GameControllerButton)m_Identifier ) ? 1.0f : 0.0f;
#else
                newValue = SDL_GetGamepadButton( m_pController, (SDL_GamepadButton)m_Identifier ) ? 1.0f : 0.0f;
#endif
            }
            else if ( m_pType == g_Sdlipt[ 1 ] ) // Analog Button
            {
#if SDL_MAJOR_VERSION < 3
                newValue = SDL_GameControllerGetAxis( m_pController, (SDL_GameControllerAxis)m_Identifier );
#else
                newValue = SDL_GetGamepadAxis( m_pController, (SDL_GamepadAxis)m_Identifier );
#endif
                newValue /= 32767.0f;
            }
            else if ( ( m_pType == g_Sdlipt[ 2 ] ) || ( m_pType == g_Sdlipt[ 3 ] ) ) // X/Y Axis
            {
#if SDL_MAJOR_VERSION < 3
                newValue = SDL_GameControllerGetAxis( m_pController, (SDL_GameControllerAxis)m_Identifier );
#else
                newValue = SDL_GetGamepadAxis( m_pController, (SDL_GamepadAxis)m_Identifier );
#endif
                if ( newValue > 0.0f )
                {
                    newValue /= 65534.0f;
                }
                else
                {
                    newValue /= 65536.0f;
                }

                newValue += 0.5f;

#if SDL_MAJOR_VERSION < 3
                if ( m_Identifier == SDL_CONTROLLER_AXIS_LEFTY || m_Identifier == SDL_CONTROLLER_AXIS_RIGHTY )
#else
                if ( m_Identifier == SDL_GAMEPAD_AXIS_LEFTY || m_Identifier == SDL_GAMEPAD_AXIS_RIGHTY )
#endif
                {
                    newValue = 1.0f - newValue;
                }
            }
            else
            {
                rAssert( 0 );
            }
        }

		return newValue;
	}

    //========================================================================
    // radControllerInputPointSDL::iIVirtualTimeChanged
    //========================================================================

    virtual void iVirtualTimeChanged( unsigned int virtualTime )
    {
		// Get a new value from the pData structure

		float newValue = CalculateNewValue( );
        #if defined(RAD_ANDROID)
            float oldValue = m_Value;
        #endif
        //
        // Check tolerance
        //

        if
        (
            ( newValue != m_Value ) && 
            ( fabsf( newValue - m_Value ) >= m_Tolerance )
        )
        {
            //
            // The input point has changed and we are in tolerance.
            //

            m_Value = newValue;
            #if defined(RAD_ANDROID)
                AndroidNotifyPhysicalGamepadInputFromPoint(
                    m_pController,
                    m_pType,
                    oldValue,
                    newValue
                );
            #endif

            m_TimeOfStateChange = virtualTime;
            m_TimeInState = 0; // Just changed

            //
            // Notify callbacks
            //

            AddRef( ); // Don't want to self destruct while we're calling out

            IRadWeakCallbackWrapper * pIWcr;

            m_xIOl_Callbacks->Reset( );

            if ((pIWcr = reinterpret_cast< IRadWeakCallbackWrapper * >( m_xIOl_Callbacks->GetNext( ) )))
            {
                IRadControllerInputPointCallback * pCallback = ( IRadControllerInputPointCallback* ) pIWcr->GetWeakInterface( );
                unsigned int userData = reinterpret_cast< uintptr_t >( pIWcr->GetUserData( ) );

                pCallback->OnControllerInputPointChange( userData, m_Value );           
            }

            Release( );
        }
        else
        {
            //
            // This input point has not changed value, or failed the tolerance
            // test, so count time in state.
            //

            m_TimeInState = virtualTime - m_TimeOfStateChange;
        }
    }

    //========================================================================
    // radControllerInputPointSDL::iInitialize
    //========================================================================

	virtual void iInitialize( void )
	{
		// Set the new value

		m_Value = CalculateNewValue( );
	}

    //========================================================================
    // radControllerInputPointSDL::GetName
    //========================================================================

    virtual const char * GetName( void )
    {
        //
        // This points to a string in the global controller definition array
        // (see top of file)
        //

        return m_pName;
    }
    
    //========================================================================
    // radControllerInputPointSDL::GetType
    //========================================================================
    
    virtual const char * GetType( void )
    {
        //
        // This points to a string in the global controller definition array
        // (see top of file)
        //

        return m_pType;      
    }

    //========================================================================
    // radControllerInputPointSDL::SetTolerance
    //========================================================================

    virtual void  SetTolerance( float percentage )
    {
        //
        // Set tolerance in range.
        //

        rAssert( percentage >= 0.0f && percentage <= 1.0f );

        if ( percentage < 0.0f )
        {
            percentage = 0.0f;
        }
        else if ( percentage > 1.0f )
        {
            percentage = 1.0f;
        }

        m_Tolerance = percentage;
    }

    //========================================================================
    // radControllerInputPointSDL::GetTolerance
    //========================================================================

    virtual float /* percentage */ GetTolerance( void )
    {
        //
        // Simply retur the tolerance
        // 

        return m_Tolerance;
    }

    //========================================================================
    // radControllerInputPointSDL::RegisterControllerInputPointCallback
    //========================================================================
           
    virtual void RegisterControllerInputPointCallback
    (
        IRadControllerInputPointCallback * pCallback,
        unsigned int userData = 0
    )
    {
        //
        // Wrap the weak interface we a callback wrapper so we can store it
        // in our object list.
        //

        rAssert( pCallback != NULL );

        ref< IRadWeakCallbackWrapper > xIWcr;

        radWeakCallbackWrapperCreate( &xIWcr, g_ControllerSystemAllocator ); 

        rAssert( xIWcr != NULL );

        if ( xIWcr != NULL )
        {
            xIWcr->SetWeakInterface( pCallback );
            xIWcr->SetUserData( (void*)(uintptr_t) userData );
        }

        m_xIOl_Callbacks->AddObject( xIWcr );
    }

    //========================================================================
    // radControllerInputPointSDL::UnRegisterControllerInputPointCallback
    //========================================================================

    virtual void UnRegisterControllerInputPointCallback
    (
        IRadControllerInputPointCallback * pCallback
    )
    {
        //
        // Simply look for the callback in the list and delete it, if found.
        //
        rAssert( pCallback != NULL );

        IRadWeakCallbackWrapper * pIWcr;

        m_xIOl_Callbacks->Reset( );

        if ((pIWcr = reinterpret_cast< IRadWeakCallbackWrapper * >( m_xIOl_Callbacks->GetNext( ) )))
        {
            if ( pIWcr->GetWeakInterface( ) == pCallback )
            {
                m_xIOl_Callbacks->RemoveObject( pIWcr );
                return;
            }
        }

        rAssertMsg( false, "Controller Input Point Callback Not Registered." );
    }

    //========================================================================
    // radControllerInputPointSDL::GetCurrentValue
    //========================================================================

    virtual float GetCurrentValue( unsigned int * pTime = NULL )
    {
        if ( pTime != NULL )
        {
            *pTime = m_TimeInState;
        }                
        
        //
        // we map it to the client's range here, this solves a bunch of
        // problems with the client remapping in mid-game.
        //

        return (( m_MaxRange - m_MinRange ) * m_Value ) + m_MinRange;
    }

    //========================================================================
    // radControllerInputPointSDL::SetRange
    //========================================================================

    virtual void  SetRange( float min, float max )
    {
        //
        // Note that max range CAN be less than min range, if this is what
        // the client wants.
        //

        m_MinRange = min;
        m_MaxRange = max;
    }

    //========================================================================
    // radControllerInputPointSDL::GetRange
    //========================================================================
    
    virtual void GetRange( float * pMin, float * pMax )
    {
        //
        // Either param can be null, but not both!
        //

        rAssert( pMin != NULL || pMax != NULL );

        if ( pMin != NULL )
        {
            *pMin = m_MinRange;
        }

        if ( pMax != NULL )
        {
            *pMax = m_MaxRange;
        }
    }

    //========================================================================
    // radControllerInputPointSDL::radControllerInputPointSDL
    //========================================================================
#if SDL_MAJOR_VERSION < 3
    radControllerInputPointSDL( SDL_GameController * pController, const char * pType, const char * pName, int id )
#else
    radControllerInputPointSDL( SDL_Gamepad * pController, const char * pType, const char * pName, int id )
#endif
        :
        radRefCount( 0 ),
        m_Value( 0.0f ),
        m_MinRange( 0.0f ),
        m_MaxRange( 1.0f ),
        m_Tolerance( 0.0f ),
        m_TimeInState( 0 ),
        m_TimeOfStateChange( 0 ),
        m_pType( pType ),
        m_pName( pName ),
        m_Identifier( id ),
        m_pController( pController )
    {
        radMemoryMonitorIdentifyAllocation( this, g_nameFTech, "radControllerInputPointSDL" );
        
        //
        // Object list to store our callbacks
        //

        ::radObjectListCreate( & m_xIOl_Callbacks, g_ControllerSystemAllocator );
    }

    //========================================================================
    // radControllerInputPointSDL::~radControllerInputPointSDL
    //========================================================================
    
    ~radControllerInputPointSDL( void )
    {        
        rAssertMsg( m_xIOl_Callbacks->GetSize() == 0, "Sombody forgot to UnRegister an input point callback" );
    }

    //========================================================================
    // radControllerInputPointSDL Data Members
    //========================================================================

    float m_Value;
    float m_MinRange;
    float m_MaxRange;
    float m_Tolerance;

    unsigned int m_TimeInState;
    unsigned int m_TimeOfStateChange;

    const char * m_pType;
    const char * m_pName;

    int m_Identifier;
#if SDL_MAJOR_VERSION < 3
    SDL_GameController * m_pController;
#else
    SDL_Gamepad * m_pController;
#endif

    ref< IRadObjectList > m_xIOl_Callbacks;
};

#if defined(RAD_ANDROID)

//============================================================================
// Component: radControllerInputPointAndroidTouch
//
// Input point used by the Android/touch virtual controller.
//
// Important:
// - This does NOT read from SDL.
// - Buttons and triggers stay released.
// - X/Y axes stay centered.
// - Real touch values are injected later through TouchInputAdapter directly
//   into UserController::mButtonArray.
//============================================================================

class radControllerInputPointAndroidTouch
    :
    public IRadControllerInputPointSDL,
    public radRefCount
{
public:

    IMPLEMENT_REFCOUNTED( "radControllerInputPointAndroidTouch" )

    radControllerInputPointAndroidTouch
    (
        const char* pType,
        const char* pName,
        int id
    )
        :
        radRefCount( 0 ),
        m_Value( 0.0f ),
        m_MinRange( 0.0f ),
        m_MaxRange( 1.0f ),
        m_Tolerance( 0.0f ),
        m_TimeInState( 0 ),
        m_TimeOfStateChange( 0 ),
        m_pType( pType ),
        m_pName( pName ),
        m_Identifier( id )
    {
        radMemoryMonitorIdentifyAllocation(
            this,
            g_nameFTech,
            "radControllerInputPointAndroidTouch"
        );

        ::radObjectListCreate( &m_xIOl_Callbacks, g_ControllerSystemAllocator );

        m_Value = GetNeutralInternalValue();
    }

    ~radControllerInputPointAndroidTouch()
    {
        rAssertMsg(
            m_xIOl_Callbacks->GetSize() == 0,
            "Somebody forgot to UnRegister an Android touch input point callback"
        );
    }

    virtual void iInitialize( void )
    {
        m_Value = GetNeutralInternalValue();
    }

    virtual void iVirtualTimeReMapped( unsigned int virtualTime )
    {
        m_TimeInState = 0;
        m_TimeOfStateChange = virtualTime;
    }

    virtual void iVirtualTimeChanged( unsigned int virtualTime )
    {
        /*
         * No SDL polling here. This input point is only a neutral placeholder.
         * Real touch input is injected through TouchInputAdapter.
         */
        m_TimeInState = virtualTime - m_TimeOfStateChange;
    }

    virtual const char* GetName( void )
    {
        return m_pName;
    }

    virtual const char* GetType( void )
    {
        return m_pType;
    }

    virtual void SetTolerance( float percentage )
    {
        rAssert( percentage >= 0.0f && percentage <= 1.0f );

        if ( percentage < 0.0f )
        {
            percentage = 0.0f;
        }
        else if ( percentage > 1.0f )
        {
            percentage = 1.0f;
        }

        m_Tolerance = percentage;
    }

    virtual float GetTolerance( void )
    {
        return m_Tolerance;
    }

    virtual void RegisterControllerInputPointCallback
    (
        IRadControllerInputPointCallback* pCallback,
        unsigned int userData = 0
    )
    {
        rAssert( pCallback != NULL );

        ref< IRadWeakCallbackWrapper > xIWcr;

        radWeakCallbackWrapperCreate( &xIWcr, g_ControllerSystemAllocator );

        rAssert( xIWcr != NULL );

        if ( xIWcr != NULL )
        {
            xIWcr->SetWeakInterface( pCallback );
            xIWcr->SetUserData( (void*)(uintptr_t)userData );
        }

        m_xIOl_Callbacks->AddObject( xIWcr );
    }

    virtual void UnRegisterControllerInputPointCallback
    (
        IRadControllerInputPointCallback* pCallback
    )
    {
        rAssert( pCallback != NULL );

        IRadWeakCallbackWrapper* pIWcr;

        m_xIOl_Callbacks->Reset();

        while ( ( pIWcr = reinterpret_cast< IRadWeakCallbackWrapper* >( m_xIOl_Callbacks->GetNext() ) ) )
        {
            if ( pIWcr->GetWeakInterface() == pCallback )
            {
                m_xIOl_Callbacks->RemoveObject( pIWcr );
                return;
            }
        }

        rAssertMsg( false, "Android touch input point callback not registered." );
    }

    virtual float GetCurrentValue( unsigned int* pTime = NULL )
    {
        if ( pTime != NULL )
        {
            *pTime = m_TimeInState;
        }

        return ( ( m_MaxRange - m_MinRange ) * m_Value ) + m_MinRange;
    }

    virtual void SetRange( float min, float max )
    {
        m_MinRange = min;
        m_MaxRange = max;
    }

    virtual void GetRange( float* pMin, float* pMax )
    {
        rAssert( pMin != NULL || pMax != NULL );

        if ( pMin != NULL )
        {
            *pMin = m_MinRange;
        }

        if ( pMax != NULL )
        {
            *pMax = m_MaxRange;
        }
    }

private:

    float GetNeutralInternalValue() const
    {
        /*
         * radControllerInputPointSDL stores stick axes internally in 0..1:
         *
         *   0.0 = negative edge
         *   0.5 = center
         *   1.0 = positive edge
         *
         * UserController later maps XAxis/YAxis to -1..1.
         * Therefore the virtual neutral axis value must be 0.5f.
         */
        if ( ( m_pType == g_Sdlipt[ 2 ] ) || ( m_pType == g_Sdlipt[ 3 ] ) )
        {
            return 0.5f;
        }

        /*
         * Buttons and analog triggers stay released.
         */
        return 0.0f;
    }

private:

    float m_Value;
    float m_MinRange;
    float m_MaxRange;
    float m_Tolerance;

    unsigned int m_TimeInState;
    unsigned int m_TimeOfStateChange;

    const char* m_pType;
    const char* m_pName;

    int m_Identifier;

    ref< IRadObjectList > m_xIOl_Callbacks;
};

#endif // RAD_ANDROID

//============================================================================
// Component: radControllerSDL
//============================================================================

class radControllerSDL
    :
    public IRadControllerSDL,
    public radRefCount
{
    public:

    IMPLEMENT_REFCOUNTED( "radControllerSDL" )

    

    //========================================================================
    // radControllerSDL::iPoll
    //========================================================================

// FUNCION ORIGINAL(LA COMENTO PARA PONER LA DE ABAJO CON ALGUNOS LOGI Y LOGE EXTRA)
/**
    virtual void iPoll( unsigned int virtualTime )
    {
        //
        // Query the hardware for current state and store it in the
        // controller buffer, it will be pulled out by virtual time
        // changing.
        //

        if ( GetRefCount( ) > 1 )
        {
            if ( m_pController != NULL )
            {
				
#if SDL_MAJOR_VERSION < 3
                SDL_GameControllerUpdate();
#else
                SDL_UpdateGamepads();
#endif
            }

            //
            // Send our output point data to the device here
            //

            {                
                IRadControllerOutputPoint * pICop2_Left  = reinterpret_cast< IRadControllerOutputPoint * >( m_xIOl_OutputPoints->GetAt( 0 ) );
                IRadControllerOutputPoint * pICop2_Right = reinterpret_cast< IRadControllerOutputPoint * >( m_xIOl_OutputPoints->GetAt( 1 ) );

                uint16_t newLeftGain  = (uint16_t) ( pICop2_Left->GetGain( ) * 65535.0f );
                uint16_t newRightGain = (uint16_t) ( pICop2_Right->GetGain( ) * 65535.0f );
        
                if
                (
                    ( newLeftGain  != m_LeftGain ) ||
                    ( newRightGain != m_RightGain )
                )
                {
                    m_LeftGain =  newLeftGain;
                    m_RightGain = newRightGain;

					rAssert(m_pController != NULL);

                    int result = 0;
					if(m_pController != NULL)
					{
#if SDL_MAJOR_VERSION < 3
                        result = SDL_GameControllerRumble( m_pController,
                            m_LeftGain, m_RightGain, 0 );
#else
                        result = SDL_RumbleGamepad( m_pController,
                            m_LeftGain, m_RightGain, 0 );
#endif
					}

                    //
                    // Old Controllers don't support output and this will
                    // fail
                    //

                    // rAssert( result == ERROR_IO_PENDING );
                }
            }
        }                  
    }

*/

// NUEVOS METODOS

    #if defined(RAD_ANDROID)
    int GetAndroidInstanceId() const
    {
        return mAndroidInstanceId;
    }
    #endif

    bool EnsureHapticReady( void )
    {
        if ( m_HapticReady && m_pHaptic != NULL )
        {
            return true;
        }

        if ( m_pController == NULL )
        {

            return false;
        }

#if SDL_MAJOR_VERSION < 3
        SDL_Joystick * pJoystick = SDL_GameControllerGetJoystick( m_pController );
#else
        SDL_Joystick * pJoystick = SDL_GetGamepadJoystick( m_pController );
#endif

        if ( pJoystick == NULL )
        {

            return false;
        }

        if ( SDL_JoystickIsHaptic( pJoystick ) != SDL_TRUE )
        {

            return false;
        }

        if ( m_pHaptic == NULL )
        {
            m_pHaptic = SDL_HapticOpenFromJoystick( pJoystick );
            if ( m_pHaptic == NULL )
            {

                return false;
            }
        }

        if ( SDL_HapticRumbleInit( m_pHaptic ) != 0 )
        {

            SDL_HapticClose( m_pHaptic );
            m_pHaptic = NULL;
            return false;
        }

        m_HapticReady = true;

        return true;
    }

    bool RunHapticFallback( uint16_t left, uint16_t right )
    {
        if ( !EnsureHapticReady() )
        {
            return false;
        }

        float strength = ( ( left > right ) ? left : right ) / 65535.0f;

        if ( strength <= 0.0f )
        {
            if ( SDL_HapticRumbleStop( m_pHaptic ) != 0 )
            {

                return false;
            }


            return true;
        }

        // Pequeña duración para que el poll la vaya renovando continuamente.
        Uint32 durationMs = 50;

        if ( SDL_HapticRumblePlay( m_pHaptic, strength, durationMs ) != 0 )
        {

            return false;
        }


        return true;
    }
// FIN NUEVOS METODOS 

virtual void iPoll( unsigned int virtualTime )
{
    if ( GetRefCount( ) > 1 )
    {
        if ( m_pController != NULL )
        {
#if SDL_MAJOR_VERSION < 3
            SDL_GameControllerUpdate();
#else
            SDL_UpdateGamepads();
#endif
        }

        {
            IRadControllerOutputPoint * pICop2_Left  =
                reinterpret_cast< IRadControllerOutputPoint * >( m_xIOl_OutputPoints->GetAt( 0 ) );
            IRadControllerOutputPoint * pICop2_Right =
                reinterpret_cast< IRadControllerOutputPoint * >( m_xIOl_OutputPoints->GetAt( 1 ) );

            uint16_t newLeftGain  = (uint16_t)( pICop2_Left->GetGain()  * 65535.0f );
            uint16_t newRightGain = (uint16_t)( pICop2_Right->GetGain() * 65535.0f );

            #if defined(RAD_ANDROID)
            // Comprobamos si la fuente de verdad del archivo txt nos da permiso para vibrar el mando, 
            // en caso de poner false el mando no vibrará lo ponemos a 0
                const bool androidRumbleAllowed = AndroidIsGamepadRumbleAllowed();

                if ( !androidRumbleAllowed )
                {
                    newLeftGain = 0;
                    newRightGain = 0;
                }
            #endif

            if
            (
                ( newLeftGain  != m_LeftGain ) ||
                ( newRightGain != m_RightGain )
            )
            {
                m_LeftGain  = newLeftGain;
                m_RightGain = newRightGain;

                rAssert( m_pController != NULL );

                int result = 0;

                if ( m_pController != NULL )
                {


#if SDL_MAJOR_VERSION < 3
                    result = SDL_GameControllerRumble(
                        m_pController,
                        m_LeftGain,
                        m_RightGain,
                        0
                    );
#else
                    result = SDL_RumbleGamepad(
                        m_pController,
                        m_LeftGain,
                        m_RightGain,
                        0
                    );
#endif

                    #if defined(RAD_ANDROID)
                    if ( !androidRumbleAllowed )
                    {
                        AndroidJavaStopRumbleFallback();
                    }
                    #endif
                    if ( result != 0 )
                    {


							#if defined(RAD_ANDROID)
							float strength = ( ( m_LeftGain > m_RightGain ) ? m_LeftGain : m_RightGain ) / 65535.0f;
							
							//strength *= 1.5f;

							if ( strength > 1.0f )
							{
						strength = 1.0f;
							}

					if ( strength > 0.0f )
{
    AndroidJavaRumbleFallback( strength, 60 );
}
else
{
    AndroidJavaStopRumbleFallback();
}
#endif
                }
                else
                {
#if SDL_MAJOR_VERSION < 3
                    SDL_GameControllerRumble(
                        m_pController,
                        m_LeftGain,
                        m_RightGain,
                        0
                    );
#else
                    SDL_RumbleGamepad(
                        m_pController,
                        m_LeftGain,
                        m_RightGain,
                        0
                    );
#endif
                }
            }
        }
    }
}
}

    //========================================================================
    // radControllerSDL::iVirtualTimeReMapped
    //========================================================================

    virtual void iVirtualTimeReMapped( unsigned int virtualTime )
    {
        IRadControllerInputPointSDL * pICip2;

        m_xIOl_InputPoints->Reset( );

        while ((pICip2 = reinterpret_cast< IRadControllerInputPointSDL * >( m_xIOl_InputPoints->GetNext( ) )))
        {
            pICip2->iVirtualTimeReMapped( virtualTime );
        }
    }

    //========================================================================
    // radControllerSDL::iVirtualTimeChanged
    //========================================================================

    virtual void iVirtualTimeChanged( unsigned int virtualTime )
    {
        if( GetRefCount( ) > 1 )
        {
            //
            // Regardless of whether anything happened, notify all the input
            // points that time has passed
            //

            if( m_xIOl_InputPoints != NULL )
            {
                m_xIOl_InputPoints->Reset();

                IRadControllerInputPointSDL* pIXbcip2;

                while((pIXbcip2 = reinterpret_cast<IRadControllerInputPointSDL*>(m_xIOl_InputPoints->GetNext())))
                {
                    pIXbcip2->iVirtualTimeChanged( virtualTime );
                }
            }
        }
    }

    //========================================================================
    // radControllerSDL::iSetBufferTime
    //========================================================================

    virtual void iSetBufferTime
    (
        unsigned int milliseconds,
        unsigned int pollingRate
    )
    {
    }

    //========================================================================
    // radControllerSDL::IsConnection
    //========================================================================

    virtual bool IsConnected( void )
    {
#if SDL_MAJOR_VERSION < 3
        return SDL_GameControllerGetAttached( m_pController ) == SDL_TRUE;
#else
        return SDL_GamepadConnected( m_pController );
#endif
    }

    //========================================================================
    // radControllerSDL::GetType
    //========================================================================

    virtual const char * GetType( void )
    {
        //
        // Allways an SDLStandard joystick
        //
        return "SDLStandard";
    }

    //========================================================================
    // radControllerSDL::GetClassification
    //========================================================================

    virtual const char * GetClassification( void )
    {
        //
        // Always a joystick
        //

        return "Joystick";
    }

    //========================================================================
    // radControllerSDL::GetNumberOfInputPointsOfType
    //========================================================================

    virtual unsigned int GetNumberOfInputPointsOfType
    (
        const char * pType
    )
    {
        //
        // Count up the number of input points of this time in the input
        // point list
        //

        rAssert( pType != NULL );

        unsigned int count = 0;

        m_xIOl_InputPoints->Reset( );

        IRadControllerInputPoint * pICip2;

        while ((pICip2 = reinterpret_cast< IRadControllerInputPointSDL * >( m_xIOl_InputPoints->GetNext( ) )))
        {
            if ( strcmp( pICip2->GetType( ), pType ) == 0 )
            {
                count++;
            }
        }

        return count;
    }

    //========================================================================
    // radControllerSDL::GetNumberOfOutputPointsOfType
    //========================================================================

    unsigned int GetNumberOfOutputPointsOfType( const char * pType )
    {
        //
        // Count up the number of Output points of this time in the Output
        // point list
        //

        rAssert( pType != NULL );

        unsigned int count = 0;

        m_xIOl_OutputPoints->Reset( );

        IRadControllerOutputPoint * pICip2;

        while ((pICip2 = reinterpret_cast< IRadControllerOutputPoint * >( m_xIOl_OutputPoints->GetNext( ) )))
        {
            if ( strcmp( pICip2->GetType( ), pType ) == 0 )
            {
                count++;
            }
        }

        return count;
    }

    //========================================================================
    // radControllerSDL::GetInputPointByTypeAndIndex
    //========================================================================

    virtual IRadControllerInputPoint * GetInputPointByTypeAndIndex
    (
        const char * pType,
        unsigned int index
    )
    {
        //
        // Just loop through all of the input points counting each one of
        // that time
        //

        rAssert( pType != NULL );

        unsigned int count = 0;

        m_xIOl_InputPoints->Reset( );

        IRadControllerInputPoint * pICip2;

        while ((pICip2 = reinterpret_cast< IRadControllerInputPointSDL * >( m_xIOl_InputPoints->GetNext( ) )))
        {
            if ( strcmp( pICip2->GetType( ), pType ) == 0 )
            {
                if ( count == index )
                {
                    return pICip2;
                }

                count++;
            }
        }

        return NULL;
    }

    //========================================================================
    // radControllerSDL::GetOutputPointByTypeAndIndex
    //========================================================================

    IRadControllerOutputPoint * GetOutputPointByTypeAndIndex
    (
        const char * pType,
        unsigned int index
    )
    {
        //
        // Just loop through all of the Output points counting each one of
        // that time
        //

        rAssert( pType != NULL );

        unsigned int count = 0;

        m_xIOl_OutputPoints->Reset( );

        IRadControllerOutputPoint * pICip2;

        while ((pICip2 = reinterpret_cast< IRadControllerOutputPoint * >( m_xIOl_OutputPoints->GetNext( ) )))
        {
            if ( strcmp( pICip2->GetType( ), pType ) == 0 )
            {
                if ( count == index )
                {
                    return pICip2;
                }

                count++;
            }
        }

        return NULL;
    }

    //========================================================================
    // radControllerSDL::GetInputPointByName
    //========================================================================

    virtual IRadControllerInputPoint * GetInputPointByName
    (
        const char * pName
    )
    {
        //
        // Just loop through all of the input points comparing each ones
        // name to the name passed in.
        //

        rAssert( pName != NULL );

        m_xIOl_InputPoints->Reset( );

        IRadControllerInputPoint * pICip2;

        while ((pICip2 = reinterpret_cast< IRadControllerInputPointSDL * >( m_xIOl_InputPoints->GetNext( ) )))
        {
            if ( strcmp( pName, pICip2->GetName( ) ) == 0 )
            {
                return pICip2;
            }
        }

        return NULL;
    }

    //========================================================================
    // radControllerSDL::GetOutputPointByName
    //========================================================================

    IRadControllerOutputPoint * GetOutputPointByName
    (
        const char * pName
    )
    {
        //
        // Just loop through all of the Output points comparing each ones
        // name to the name passed in.
        //

        rAssert( pName != NULL );

        m_xIOl_OutputPoints->Reset( );

        IRadControllerOutputPoint * pICip2;

        while ((pICip2 = reinterpret_cast< IRadControllerOutputPoint * >( m_xIOl_OutputPoints->GetNext( ) )))
        {
            if ( strcmp( pName, pICip2->GetName( ) ) == 0 )
            {
                return pICip2;
            }
        }

        return NULL;
    }

    //========================================================================
    // radControllerSDL::GetLocation
    //========================================================================

    virtual const char * GetLocation( void )
    {
        //
        // Just return the location string;
        //

        return m_xIString_Location->GetChars( );
    }

    //========================================================================
    // radControllerSDL::GetNumberOfInputPoints
    //========================================================================

    virtual unsigned int GetNumberOfInputPoints( void )
    {
        return m_xIOl_InputPoints->GetSize( );
    }

    //========================================================================
    // radControllerSDL::GetInputPointByIndex
    //========================================================================

    virtual IRadControllerInputPoint * GetInputPointByIndex( unsigned int index )
    {
        return reinterpret_cast< IRadControllerInputPointSDL * >( m_xIOl_InputPoints->GetAt( index ) );
    }

    //========================================================================
    // radControllerSDL::GetNumberOfInputPoints
    //========================================================================

    virtual unsigned int GetNumberOfOutputPoints( void )
    {
        return m_xIOl_OutputPoints->GetSize( );
    }

    //========================================================================
    // radControllerSDL::GetOutputPointByIndex
    //========================================================================

    virtual IRadControllerOutputPoint * GetOutputPointByIndex( unsigned int index )
    {
        return reinterpret_cast< IRadControllerOutputPoint * >( m_xIOl_OutputPoints->GetAt( index ) );
    }

    //========================================================================
    // radControllerSDL::radControllerSDL
    //========================================================================

    radControllerSDL
    (
        unsigned int thisAllocator,
#if SDL_MAJOR_VERSION < 3
        SDL_GameController * pController,
#else
        SDL_Gamepad * pController,
#endif
        unsigned int virtualTime,
        unsigned int bufferTime,
        unsigned int pollingRate
    )
        :
        radRefCount( 0 ),
        m_pController( pController ),
		m_pHaptic( NULL ),
		m_HapticReady( false )
        #if defined(RAD_ANDROID)
        ,mAndroidInstanceId( -1 )
        #endif
    {
        radMemoryMonitorIdentifyAllocation( this, g_nameFTech, "radControllerSDL" );

        m_LeftGain = m_RightGain = 0;
        #if defined(RAD_ANDROID)
        mAndroidInstanceId = AndroidGetGamepadInstanceId( m_pController );
        #endif

        //
        // Get an object list to store our input points
        //

        ::radObjectListCreate( & m_xIOl_InputPoints, g_ControllerSystemAllocator );
        ::radObjectListCreate( & m_xIOl_OutputPoints, g_ControllerSystemAllocator );

        //
        // Get a string to store our location
        //

        ::radStringCreate( & m_xIString_Location, g_ControllerSystemAllocator );

        //
        // Create our location name based on our port and slot
        //
#if SDL_MAJOR_VERSION < 3
        int iController = std::max(SDL_GameControllerGetPlayerIndex( pController ), 0);
#else
        int iController = std::max(SDL_GetGamepadPlayerIndex( pController ), 0);
#endif
		m_xIString_Location->SetSize( 12 );
        m_xIString_Location->Append( "Port" );
        m_xIString_Location->Append( (unsigned int) iController );
        m_xIString_Location->Append( "\\Slot0" );

        //
        // Create all of our intput points, this is always the same for every
        // sdl controller.  See static array above.
        //

        for ( unsigned int button = 0; button < ( sizeof( g_SDLPoints ) / sizeof( SDLInputPoint ) ); button++ )
        {
			ref< radControllerInputPointSDL > pInputPoint = new( g_ControllerSystemAllocator ) radControllerInputPointSDL
			(
                m_pController,
                g_SDLPoints[ button ].m_pType,
                g_SDLPoints[ button ].m_pName,
                g_SDLPoints[ button ].m_Mask
            );

            m_xIOl_InputPoints->AddObject( pInputPoint );

			//
			// Hand the point its first value
			//

            pInputPoint->iInitialize( );
        }

        if ( m_xIOl_OutputPoints != NULL )
        {
            radControllerOutputPointSDL * pLeft = new( g_ControllerSystemAllocator ) radControllerOutputPointSDL( "LeftMotor" );

            radControllerOutputPointSDL * pRight = new( g_ControllerSystemAllocator ) radControllerOutputPointSDL( "RightMotor" );

            m_xIOl_OutputPoints->AddObject( reinterpret_cast< IRefCount * >( pLeft ) );
            m_xIOl_OutputPoints->AddObject( reinterpret_cast< IRefCount * >( pRight ) );


        }

        //
        // Set everything to a know state using our regular runtime functions.
        // Note that the controller might get created during gameplay after
        // the controller system has been run for a while.
        //

        iSetBufferTime( bufferTime, pollingRate );
        iVirtualTimeReMapped( virtualTime );
    }

    //========================================================================
    // radControllerSDL::
    //========================================================================

    ~radControllerSDL( void )
    {
    if ( m_pHaptic != NULL )
    {
        SDL_HapticClose( m_pHaptic );
        m_pHaptic = NULL;
    }
}

    //========================================================================
    // radControllerSDL Data Members
    //========================================================================
#if SDL_MAJOR_VERSION < 3
    SDL_GameController *              m_pController;
#else
    SDL_Gamepad *                     m_pController;
#endif
	SDL_Haptic *                      m_pHaptic; // nuevas lineas para hacer fallback a SDL haptic y de esta forma tener vibracion
    bool                              m_HapticReady; // nueva linea para el mismo proposito de arriba
    #if defined(RAD_ANDROID)
    int                               mAndroidInstanceId;
    #endif

    ref< IRadObjectList >             m_xIOl_InputPoints;
    ref< IRadObjectList >             m_xIOl_OutputPoints;

    ref< IRadString >                 m_xIString_Location;

    uint16_t                          m_LeftGain, m_RightGain;
};

#if defined(RAD_ANDROID)

//============================================================================
// Component: radControllerAndroidTouch
//
// Android/touch virtual controller.
//
// This controller is NOT a physical SDL gamepad.
// It exists only so the original input pipeline can initialize controller 0
// when Android has touch input but no physical gamepad connected.
//
// Real touch values are still written by TouchInputAdapter directly into
// UserController::mButtonArray through SetVirtualInputValue().
//============================================================================

class radControllerAndroidTouch
    :
    public IRadControllerSDL,
    public radRefCount
{
public:

    IMPLEMENT_REFCOUNTED( "radControllerAndroidTouch" )

    radControllerAndroidTouch
    (
        unsigned int virtualTime,
        unsigned int bufferTime,
        unsigned int pollingRate
    )
        :
        radRefCount( 0 )
    {
        radMemoryMonitorIdentifyAllocation( this, g_nameFTech, "radControllerAndroidTouch" );

        ::radObjectListCreate( &m_xIOl_InputPoints, g_ControllerSystemAllocator );
        ::radObjectListCreate( &m_xIOl_OutputPoints, g_ControllerSystemAllocator );
        ::radStringCreate( &m_xIString_Location, g_ControllerSystemAllocator );

        m_xIString_Location->SetSize( 12 );
        m_xIString_Location->Append( "Port0\\Slot0" );

        /*
         * Create the same input point layout as a normal SDL controller.
         * The input points have a NULL SDL controller, so their value stays 0.
         * Actual touch values are injected later through TouchInputAdapter.
         */
       for ( unsigned int button = 0; button < ( sizeof( g_SDLPoints ) / sizeof( SDLInputPoint ) ); button++ )
        {
            ref< radControllerInputPointAndroidTouch > pInputPoint =
                new( g_ControllerSystemAllocator ) radControllerInputPointAndroidTouch
                (
                    g_SDLPoints[ button ].m_pType,
                    g_SDLPoints[ button ].m_pName,
                    g_SDLPoints[ button ].m_Mask
                );

            m_xIOl_InputPoints->AddObject( pInputPoint );

            pInputPoint->iInitialize();
        }

        radControllerOutputPointSDL* pLeft =
            new( g_ControllerSystemAllocator ) radControllerOutputPointSDL( "LeftMotor" );

        radControllerOutputPointSDL* pRight =
            new( g_ControllerSystemAllocator ) radControllerOutputPointSDL( "RightMotor" );

        m_xIOl_OutputPoints->AddObject( reinterpret_cast< IRefCount* >( pLeft ) );
        m_xIOl_OutputPoints->AddObject( reinterpret_cast< IRefCount* >( pRight ) );

        iSetBufferTime( bufferTime, pollingRate );
        iVirtualTimeReMapped( virtualTime );
    }

    virtual ~radControllerAndroidTouch()
    {
    }

    virtual void iPoll( unsigned int virtualTime )
    {
        /*
         * No SDL polling here.
         * This is not a physical gamepad.
         */
        (void)virtualTime;
    }

    virtual void iVirtualTimeReMapped( unsigned int virtualTime )
    {
        IRadControllerInputPointSDL* pInputPoint;

        m_xIOl_InputPoints->Reset();

        while ( ( pInputPoint = reinterpret_cast< IRadControllerInputPointSDL* >( m_xIOl_InputPoints->GetNext() ) ) )
        {
            pInputPoint->iVirtualTimeReMapped( virtualTime );
        }
    }

    virtual void iVirtualTimeChanged( unsigned int virtualTime )
    {
        IRadControllerInputPointSDL* pInputPoint;

        m_xIOl_InputPoints->Reset();

        while ( ( pInputPoint = reinterpret_cast< IRadControllerInputPointSDL* >( m_xIOl_InputPoints->GetNext() ) ) )
        {
            pInputPoint->iVirtualTimeChanged( virtualTime );
        }
    }

    virtual void iSetBufferTime( unsigned int milliseconds, unsigned int pollingRate )
    {
        (void)milliseconds;
        (void)pollingRate;
    }

    virtual bool IsConnected( void )
    {
        /*
         * Logical connection only.
         * Physical gamepad state must be tracked by TouchInputModeManager,
         * not by this virtual controller.
         */
        return true;
    }

    virtual const char* GetType( void )
    {
        /*
         * Keep SDLStandard for compatibility with existing mappings.
         */
        return "SDLStandard";
    }

    virtual const char* GetClassification( void )
    {
        return "Joystick";
    }

    virtual unsigned int GetNumberOfInputPointsOfType( const char* pType )
    {
        rAssert( pType != NULL );

        unsigned int count = 0;

        m_xIOl_InputPoints->Reset();

        IRadControllerInputPoint* pInputPoint;

        while ( ( pInputPoint = reinterpret_cast< IRadControllerInputPointSDL* >( m_xIOl_InputPoints->GetNext() ) ) )
        {
            if ( strcmp( pInputPoint->GetType(), pType ) == 0 )
            {
                count++;
            }
        }

        return count;
    }

    virtual unsigned int GetNumberOfOutputPointsOfType( const char* pType )
    {
        rAssert( pType != NULL );

        unsigned int count = 0;

        m_xIOl_OutputPoints->Reset();

        IRadControllerOutputPoint* pOutputPoint;

        while ( ( pOutputPoint = reinterpret_cast< IRadControllerOutputPoint* >( m_xIOl_OutputPoints->GetNext() ) ) )
        {
            if ( strcmp( pOutputPoint->GetType(), pType ) == 0 )
            {
                count++;
            }
        }

        return count;
    }

    virtual IRadControllerInputPoint* GetInputPointByTypeAndIndex
    (
        const char* pType,
        unsigned int index
    )
    {
        rAssert( pType != NULL );

        unsigned int count = 0;

        m_xIOl_InputPoints->Reset();

        IRadControllerInputPoint* pInputPoint;

        while ( ( pInputPoint = reinterpret_cast< IRadControllerInputPointSDL* >( m_xIOl_InputPoints->GetNext() ) ) )
        {
            if ( strcmp( pInputPoint->GetType(), pType ) == 0 )
            {
                if ( count == index )
                {
                    return pInputPoint;
                }

                count++;
            }
        }

        return NULL;
    }

    virtual IRadControllerOutputPoint* GetOutputPointByTypeAndIndex
    (
        const char* pType,
        unsigned int index
    )
    {
        rAssert( pType != NULL );

        unsigned int count = 0;

        m_xIOl_OutputPoints->Reset();

        IRadControllerOutputPoint* pOutputPoint;

        while ( ( pOutputPoint = reinterpret_cast< IRadControllerOutputPoint* >( m_xIOl_OutputPoints->GetNext() ) ) )
        {
            if ( strcmp( pOutputPoint->GetType(), pType ) == 0 )
            {
                if ( count == index )
                {
                    return pOutputPoint;
                }

                count++;
            }
        }

        return NULL;
    }

    virtual IRadControllerInputPoint* GetInputPointByName( const char* pName )
    {
        rAssert( pName != NULL );

        m_xIOl_InputPoints->Reset();

        IRadControllerInputPoint* pInputPoint;

        while ( ( pInputPoint = reinterpret_cast< IRadControllerInputPointSDL* >( m_xIOl_InputPoints->GetNext() ) ) )
        {
            if ( strcmp( pName, pInputPoint->GetName() ) == 0 )
            {
                return pInputPoint;
            }
        }

        return NULL;
    }

    virtual IRadControllerOutputPoint* GetOutputPointByName( const char* pName )
    {
        rAssert( pName != NULL );

        m_xIOl_OutputPoints->Reset();

        IRadControllerOutputPoint* pOutputPoint;

        while ( ( pOutputPoint = reinterpret_cast< IRadControllerOutputPoint* >( m_xIOl_OutputPoints->GetNext() ) ) )
        {
            if ( strcmp( pName, pOutputPoint->GetName() ) == 0 )
            {
                return pOutputPoint;
            }
        }

        return NULL;
    }

    virtual const char* GetLocation( void )
    {
        return m_xIString_Location->GetChars();
    }

    virtual unsigned int GetNumberOfInputPoints( void )
    {
        return m_xIOl_InputPoints->GetSize();
    }

    virtual IRadControllerInputPoint* GetInputPointByIndex( unsigned int index )
    {
        return reinterpret_cast< IRadControllerInputPointSDL* >( m_xIOl_InputPoints->GetAt( index ) );
    }

    virtual unsigned int GetNumberOfOutputPoints( void )
    {
        return m_xIOl_OutputPoints->GetSize();
    }

    virtual IRadControllerOutputPoint* GetOutputPointByIndex( unsigned int index )
    {
        return reinterpret_cast< IRadControllerOutputPoint* >( m_xIOl_OutputPoints->GetAt( index ) );
    }

private:

    ref< IRadObjectList > m_xIOl_InputPoints;
    ref< IRadObjectList > m_xIOl_OutputPoints;
    ref< IRadString > m_xIString_Location;
};

#endif // RAD_ANDROID

//============================================================================
// Component: radControllerSystemSDL
//============================================================================

class radControllerSystemSDL
    :
    public IRadControllerSystem,
    public IRadTimerCallback,
    public radRefCount
{
    public:

    IMPLEMENT_REFCOUNTED( "radControllerSystemSDL" )

    //========================================================================
    // radControllerSystemSDL::FindPhysicalControllerAtLocation
    //
    // Searches only real SDL physical controllers.
    // Does NOT return the Android touch virtual fallback.
    //========================================================================

    IRadController* FindPhysicalControllerAtLocation( const char* pLocation )
    {
        rAssert( pLocation != NULL );

        m_xIOl_Controllers->Reset();

        IRadController* pIC2;

        while ( ( pIC2 = reinterpret_cast< IRadControllerSDL* >( m_xIOl_Controllers->GetNext() ) ) )
        {
            if ( strcmp( pIC2->GetLocation(), pLocation ) == 0 )
            {
                return pIC2;
            }
        }

        return NULL;
    }

    #if defined(RAD_ANDROID)
        IRadController* FindPhysicalControllerByAndroidInstanceId
        (
            int instanceId
        )
        {
            if ( instanceId < 0 )
            {
                return NULL;
            }

            m_xIOl_Controllers->Reset();

            IRadController* pController;

            while
            (
                ( pController =
                    reinterpret_cast< IRadControllerSDL* >(
                        m_xIOl_Controllers->GetNext()
                    )
                )
            )
            {
                radControllerSDL* pSDLController =
                    reinterpret_cast< radControllerSDL* >( pController );

                if
                (
                    pSDLController != NULL &&
                    pSDLController->GetAndroidInstanceId() == instanceId
                )
                {
                    return pController;
                }
            }

            return NULL;
        }
#endif

     //========================================================================
    // radControllerSystemSDL::CheckDeviceConnectionStatus
    //========================================================================
        #if SDL_MAJOR_VERSION < 3
        static int CheckDeviceConnectionStatus( void * userdata, SDL_Event * event )
        #else
        static bool CheckDeviceConnectionStatus( void * userdata, SDL_Event * event )
        #endif
        {
            //
            // Check if devices have been inserted or removed
            //

        #if SDL_MAJOR_VERSION < 3
            SDL_GameController* pController = NULL;
        #else
            SDL_Gamepad* pController = NULL;
        #endif

        #if defined(RAD_ANDROID)
            int androidInstanceId = -1;
        #endif

    #if SDL_MAJOR_VERSION < 3
        if( event->type == SDL_CONTROLLERDEVICEADDED )
        {
            pController = SDL_GameControllerOpen( event->cdevice.which );
        }
        else if( event->type == SDL_CONTROLLERDEVICEREMOVED )
        {
        #if defined(RAD_ANDROID)
            /*
            * On Android, for removed events, event->cdevice.which is already
            * the controller instance id.
            *
            * Do not try to recover the SDL controller object here. It may already
            * be gone. We only need the instance id to remove our tracked state.
            */
            pController = NULL;
        #else
            pController = SDL_GameControllerFromInstanceID( event->cdevice.which );
        #endif
        }
    #else
        if( event->type == SDL_EVENT_GAMEPAD_ADDED )
        {
            pController = SDL_OpenGamepad( event->cdevice.which );
        }
        else if( event->type == SDL_EVENT_GAMEPAD_REMOVED )
        {
        #if defined(RAD_ANDROID)
            /*
            * On Android, for removed events, event->cdevice.which is already
            * the controller instance id.
            *
            * Do not try to recover the SDL gamepad object here. It may already
            * be gone. We only need the instance id to remove our tracked state.
            */
            pController = NULL;
        #else
            pController = SDL_GetGamepadFromID( event->cdevice.which );
        #endif
        }
    #endif
        else
        {
            return true;
        }

        #if defined(RAD_ANDROID)
        #if SDL_MAJOR_VERSION < 3
            if ( event->type == SDL_CONTROLLERDEVICEREMOVED )
        #else
            if ( event->type == SDL_EVENT_GAMEPAD_REMOVED )
        #endif
            {
                /*
                * In SDL removed events, event->cdevice.which is already the
                * controller instance id.
                */
                androidInstanceId = static_cast<int>( event->cdevice.which );
            }
            else
            {
                androidInstanceId = AndroidGetGamepadInstanceId( pController );
            }
        #endif

        radControllerSystemSDL* sys = (radControllerSystemSDL*)userdata;
        sys->AddRef( );

        //
        // Find the controller in question (may not exist though)
        //
        ref< IRadController > xIController2;
        ref< IRadControllerSDL > xISDLController2;

        char location[255];

        #if defined(RAD_ANDROID)
        xIController2 =
            sys->FindPhysicalControllerByAndroidInstanceId(
                androidInstanceId
            );
        #endif

        if ( xIController2 == NULL && pController != NULL )
        {
        #if SDL_MAJOR_VERSION < 3
            int iController =
                std::max( SDL_GameControllerGetPlayerIndex( pController ), 0 );
        #else
            int iController =
                std::max( SDL_GetGamepadPlayerIndex( pController ), 0 );
        #endif

            sprintf( location, "Port%d\\Slot0", iController );

            /*
            * Fallback by location.
            * Useful for ADDED events or if instance id lookup is not available.
            */
            xIController2 = sys->FindPhysicalControllerAtLocation( location );
        }

        if ( xIController2 != NULL )
        {
            xISDLController2 = (IRadControllerSDL*)xIController2.m_pInterface;
            rAssert( xISDLController2 != NULL );
        }

#if SDL_MAJOR_VERSION < 3
        if( event->type == SDL_CONTROLLERDEVICEADDED )
#else
        if( event->type == SDL_EVENT_GAMEPAD_ADDED )
#endif
        {
            //
            // Here a device has been inserted, so open it
            //

            if( xISDLController2 == NULL )
            {
                 if ( pController == NULL )
                {
                    sys->Release();
                    return false;
                }
                //
                // Here the controller at this location has not yet been
                // constructed, so construct a new controller
                //

                unsigned int virtualTime = 0;
                unsigned int pollingRate = 10;

                virtualTime = radTimeGetMilliseconds() + sys->m_VirtualTimeAdjust;

                if ( sys->m_xITimer != NULL )
                {
                    pollingRate = sys->m_xITimer->GetTimeout( );
                }

                xIController2 = new ( g_ControllerSystemAllocator ) radControllerSDL
                (
                    g_ControllerSystemAllocator,
                    pController,
                    virtualTime,
                    sys->m_EventBufferTime,
                    pollingRate
                );

                sys->m_xIOl_Controllers->AddObject
                (
                    xIController2
                );
                #if defined(RAD_ANDROID)
                AndroidNotifyPhysicalGamepadCandidateConnected(androidInstanceId);
                #endif
            }
            else
            {
                sys->Release( );
                return false;
            }
        }
        #if SDL_MAJOR_VERSION < 3
        else if( event->type == SDL_CONTROLLERDEVICEREMOVED )
        #else
        else if( event->type == SDL_EVENT_GAMEPAD_REMOVED )
        #endif
        {
            //
            // Here a device has been removed
            //
            if ( xIController2 != NULL )
            {
                /*
                * Remove the physical SDL controller from the controller list.
                */
                sys->m_xIOl_Controllers->RemoveObject( xIController2 );
            }

        #if defined(RAD_ANDROID)
            /*
            * Direct Android notification.
            *
            * This is the fast path that lets InputManager restore touch as soon
            * as SDL reports that the confirmed physical gamepad was removed.
            */
            AndroidNotifyPhysicalGamepadDisconnected(
                androidInstanceId
            );
        #endif
        }

        IRadWeakInterfaceWrapper * pIWir;

        sys->m_xIOl_Callbacks->Reset( );

        while((pIWir = reinterpret_cast< IRadWeakInterfaceWrapper * >( sys->m_xIOl_Callbacks->GetNext( ) )))
        {

            IRadControllerConnectionChangeCallback * pCallback = (IRadControllerConnectionChangeCallback *) pIWir->GetWeakInterface( );
            pCallback->OnControllerConnectionStatusChange( xIController2 );
        }

        sys->Release( );
        return false;
    }

    //========================================================================
    // radControllerSystemSDL::OnTimerDone
    //========================================================================

    virtual void OnTimerDone( unsigned int elapsedtime, void* pUserData )
    {
        //
        // Now, update all of our controllers
        //

        m_xIOl_Controllers->Reset( );

        IRadControllerSDL * pIXbc2;

        while ((pIXbc2 = reinterpret_cast< IRadControllerSDL * >( m_xIOl_Controllers->GetNext( ) )))
        {
            //
            // The controller stamps packets with virtual time, so
            // we pass in virtual time.
            //

            pIXbc2->iPoll( radTimeGetMilliseconds( ) + m_VirtualTimeAdjust );
        }

        //
        // If the client is not driving us, we drive ourselves from this timer
        //

        if ( m_UsingVirtualTime == false )
        {
            SetVirtualTime( radTimeGetMilliseconds( ) );
        }
    }

    //========================================================================
    // radControllerSystemSDL::GetNumberOfControlelrs
    //========================================================================

    virtual unsigned int GetNumberOfControllers( void )
    {
        return m_xIOl_Controllers->GetSize( );
    }

    //========================================================================
    // radControllerSystemSDL::GetControllerByIndex
    //========================================================================

    virtual IRadController * GetControllerByIndex( unsigned int myindex )
    {
        return reinterpret_cast< IRadControllerSDL * >( m_xIOl_Controllers->GetAt( myindex ) );
    }

    //========================================================================
    // radControllerSystemSDL::GetControllerAtLocation
    //========================================================================

    virtual IRadController * GetControllerAtLocation
    (
        const char * pLocation
    )
    {
        rAssert( pLocation != NULL );

        /*
        * First priority: real physical SDL controller.
        */
        IRadController* pPhysicalController = FindPhysicalControllerAtLocation( pLocation );

        if ( pPhysicalController != NULL )
        {
            return pPhysicalController;
        }

    #if defined(RAD_ANDROID)
        /*
        * Android touch fallback:
        *
        * If no physical controller exists in Port0\Slot0, return the internal
        * virtual touch controller so the original game input pipeline can
        * initialize controller 0 normally.
        *
        * This does NOT mean a physical gamepad is connected.
        * TouchInputModeManager remains responsible for physical gamepad/touch UI.
        */
        if ( strcmp( pLocation, ANDROID_TOUCH_CONTROLLER_LOCATION ) == 0 )
        {
            return m_xIAndroidTouchController;
        }
    #endif

        return NULL;
    }

    //========================================================================
    // radControllerSystemSDL::SetBufferTime
    //========================================================================

    virtual void SetBufferTime( unsigned int milliseconds )
    {
        if ( milliseconds == 0 )
        {
            //
            // We are always buffering behind the scenes, so we set the
            // buffering time to one 60Hz frame even if the client thinks
            // we are just maintaining state.
            //

            m_UsingVirtualTime = false;

            MapVirtualTime( 0, 0 );

            milliseconds = 10;
        }
        else
        {
            m_UsingVirtualTime = true;
        }


        unsigned int pollingRate = 10;

        pollingRate = m_xITimer->GetTimeout( );

        m_EventBufferTime = milliseconds;

        m_xIOl_Controllers->Reset( );

        IRadControllerSDL * pIDipc2;

        while ((pIDipc2 = reinterpret_cast< IRadControllerSDL * >( m_xIOl_Controllers->GetNext( ) )))
        {
            pIDipc2->iSetBufferTime( milliseconds, pollingRate );
        }
    }

    //========================================================================
    // radControllerSystemSDL::MapVirtualTime
    //========================================================================

    virtual void MapVirtualTime
    (
        unsigned int systemTicks,
        unsigned int virtualTicks
    )
    {
        //
        // Here the client has requested a (re)mapping of virtual time to
        // timer manager time.
        //

        //
        // Recalculate the adjustment factor.  This number represents the
        // number to add to timer manager ticks to get game ticks.
        //
        m_VirtualTimeAdjust = virtualTicks - systemTicks;

        //
        // Inform each controller that virtual time has been remapped.
        //

        m_xIOl_Controllers->Reset( );

        IRadControllerSDL * pIXbc2;

        while ((pIXbc2 = reinterpret_cast< IRadControllerSDL * >( m_xIOl_Controllers->GetNext( ) )))
        {
            pIXbc2->iVirtualTimeReMapped( radTimeGetMilliseconds() + m_VirtualTimeAdjust );
        }
    }

    //========================================================================
    // radControllerSystemSDL::SetVirtualTime
    //========================================================================

    virtual void SetVirtualTime( unsigned int virtualTime )
    {
        //
        // Inform each controller that virtual time has changed.
        //

        m_xIOl_Controllers->Reset( );

        IRadControllerSDL * pIXbc2;

        while ((pIXbc2 = reinterpret_cast< IRadControllerSDL * >( m_xIOl_Controllers->GetNext( ) )))
        {
            pIXbc2->iVirtualTimeChanged( virtualTime );
        }
    }

    //========================================================================
    // radControllerSystemSDL::SetCaptureRate
    //========================================================================

    virtual void SetCaptureRate( unsigned int ms )
    {
        m_xITimer->SetTimeout( ms );

        //
        // Reset event queue size given new filling rate
        //

        SetBufferTime( m_EventBufferTime );
    }

    //========================================================================
    // radControllerSystemSDL::RegisterConnectionChangeCallback2
    //========================================================================

    virtual void RegisterConnectionChangeCallback
    (
        IRadControllerConnectionChangeCallback * pCallback
    )
    {
        //
        // Store the callback in a weak interface wrapper so we can store
        // it in an object list.
        //

        rAssert( pCallback != NULL );

        ref< IRadWeakInterfaceWrapper > xIWir;

        ::radWeakInterfaceWrapperCreate( & xIWir, g_ControllerSystemAllocator );

        xIWir->SetWeakInterface( pCallback );

        m_xIOl_Callbacks->AddObject( xIWir );
    }

    //========================================================================
    // radControllerSystemSDL::UnRegisterConnectionChagneCallback2
    //========================================================================

    virtual void UnRegisterConnectionChangeCallback
    (
        IRadControllerConnectionChangeCallback * pCallback
    )
    {
        //
        // Look for the callback in the list and remove it if found.
        //

        rAssert( pCallback != NULL );

        IRadWeakInterfaceWrapper * pIWir;

        m_xIOl_Callbacks->Reset( );

        while ((pIWir = reinterpret_cast< IRadWeakInterfaceWrapper * >( m_xIOl_Callbacks->GetNext( ) )))
        {
            if ( pIWir->GetWeakInterface() == pCallback )
            {
                m_xIOl_Callbacks->RemoveObject( pIWir  );
                return;
            }
        }

        rAssertMsg( false, "Controller connection change callback not registered." );
    }

    //========================================================================
    // radControllerSystemSDL::Service
    //========================================================================

    void Service( void )
    {
        m_xITimerList->Service(  );
    }

    //========================================================================
    // radControllerSystemSDL::radControllerSystemSDL
    //========================================================================

    radControllerSystemSDL
    (
        IRadControllerConnectionChangeCallback* pConnectionChangeCallback,
        radMemoryAllocator thisAllocator
    )
        :
        m_UsingVirtualTime( false ),
        m_VirtualTimeAdjust( 0 ),
        m_EventBufferTime( 0 ),
        m_DefaultConnectionChangeCallback( NULL )
    {
        radMemoryMonitorIdentifyAllocation( this, g_nameFTech, "radControllerSystemSDL" );

        //
        // Set our singleton to "this"
        //
        rAssert( s_pTheSDLControllerSystem2 == NULL );
        s_pTheSDLControllerSystem2 = this;

        g_ControllerSystemAllocator = thisAllocator;

		//
        // Create a timer to poll with, this will be changed to a thread if
        // people start complaining about latencies.
        //

        //
        // Create a timer list to drive the processing of contollers
        //

        radTimeCreateList( &m_xITimerList, 1, g_ControllerSystemAllocator );

        m_xITimerList->CreateTimer( & m_xITimer, 10, this );

        //
        // Object list to hold controllers
        //
        ::radObjectListCreate( & m_xIOl_Controllers, g_ControllerSystemAllocator );
        rAssert( m_xIOl_Controllers != NULL );

        //
        // Object list to hold callbacks
        //
        ::radObjectListCreate( & m_xIOl_Callbacks, g_ControllerSystemAllocator );
        rAssert( m_xIOl_Callbacks != NULL );
        #if defined(RAD_ANDROID)
        {
            unsigned int virtualTime = radTimeGetMilliseconds() + m_VirtualTimeAdjust;
            unsigned int pollingRate = 10;

            if ( m_xITimer != NULL )
            {
                pollingRate = m_xITimer->GetTimeout();
            }

            m_xIAndroidTouchController =
                new( g_ControllerSystemAllocator ) radControllerAndroidTouch
                (
                    virtualTime,
                    m_EventBufferTime,
                    pollingRate
                );
        }
        #endif

        //
        // Register the default connection state callback
        //
        m_DefaultConnectionChangeCallback = pConnectionChangeCallback;
        if( pConnectionChangeCallback )
        {
            m_DefaultConnectionChangeCallback = pConnectionChangeCallback;
            RegisterConnectionChangeCallback( pConnectionChangeCallback );
        }

        //
        // TODO: If there is no connection change callback, wait synchronously for the connection
        //
#if SDL_MAJOR_VERSION < 3
        int numJoysticks = SDL_NumJoysticks();
#else
        int numJoysticks;
        SDL_JoystickID* joysticks = SDL_GetJoysticks( &numJoysticks );
#endif
        for( int i = 0; i < numJoysticks; i++ )
        {
#if SDL_MAJOR_VERSION < 3
            if( SDL_IsGameController( i ) )
#else
            if( SDL_IsGamepad( joysticks[i] ) )
#endif
            {
                ref< IRadController > xIController2;
                unsigned int virtualTime = 0;
                unsigned int pollingRate = 10;

                virtualTime = radTimeGetMilliseconds() + m_VirtualTimeAdjust;

                if( m_xITimer != NULL )
                {
                    pollingRate = m_xITimer->GetTimeout();
                }

                xIController2 = new (g_ControllerSystemAllocator) radControllerSDL
                (
                    g_ControllerSystemAllocator,
#if SDL_MAJOR_VERSION < 3
                    SDL_GameControllerOpen( i ),
#else
                    SDL_OpenGamepad( joysticks[i] ),
#endif
                    virtualTime,
                    m_EventBufferTime,
                    pollingRate
                );

                m_xIOl_Controllers->AddObject
                (
                    xIController2
                );

                IRadWeakInterfaceWrapper* pIWir;

                m_xIOl_Callbacks->Reset();

                while((pIWir = reinterpret_cast<IRadWeakInterfaceWrapper*>(m_xIOl_Callbacks->GetNext())))
                {

                    IRadControllerConnectionChangeCallback* pCallback = (IRadControllerConnectionChangeCallback*)pIWir->GetWeakInterface();
                    pCallback->OnControllerConnectionStatusChange( xIController2 );
                }
            }
        }
#if SDL_MAJOR_VERSION > 2
        SDL_free( joysticks );
#endif

        //
        // Set everything to know state
        //
        SetCaptureRate( 10 );
        MapVirtualTime( 0, 0 );
        SetBufferTime( 0 );
    }

    //========================================================================
    // radControllerSystemSDL::~radControllerSystemSDL
    //========================================================================

    ~radControllerSystemSDL( void )
    {
        //
        // Unregister the default connection change callback
        //
        if( m_DefaultConnectionChangeCallback != NULL )
        {
            UnRegisterConnectionChangeCallback( m_DefaultConnectionChangeCallback );
            m_DefaultConnectionChangeCallback = NULL;
        }

        //
        // Make sure the client(s) unregistered all of their callbacks.
        //

        rAssertMsg( m_xIOl_Callbacks->GetSize() == 0, "Somebody forgot to unregister a controller connection change callback" );

        g_ControllerSystemAllocator = RADMEMORY_ALLOC_DEFAULT;

        //
        // Set the singleton back to null.
        //

        rAssert( s_pTheSDLControllerSystem2 == this );
        s_pTheSDLControllerSystem2 = NULL;

    }


    //========================================================================
    // Data Members
    //========================================================================

    bool m_UsingVirtualTime;
    unsigned int m_VirtualTimeAdjust;
    unsigned int m_EventBufferTime;

    IRadControllerConnectionChangeCallback* m_DefaultConnectionChangeCallback;

    ref< IRadObjectList >     m_xIOl_Callbacks;

    ref< IRadObjectList >     m_xIOl_Controllers;
    ref< IRadTimer >          m_xITimer;
    ref< IRadTimerList >      m_xITimerList;
    #if defined(RAD_ANDROID)
    ref< IRadControllerSDL > m_xIAndroidTouchController;
    #endif

};

//============================================================================
// Function:    radControllerInitialize
// Paramters:   pConnectionChangeCallback - a callback that is called when the
//                  connection status of controllers changes.  It is called
//                  as soon as a controller is found and properly initialized.
//============================================================================

void radControllerInitialize
(
    IRadControllerConnectionChangeCallback* pConnectionChangeCallback,
    radMemoryAllocator alloc
)
{
    rAssert( s_pTheSDLControllerSystem2 == NULL );

    new ( alloc ) radControllerSystemSDL( pConnectionChangeCallback, alloc );

    SDL_AddEventWatch( radControllerSystemSDL::CheckDeviceConnectionStatus, s_pTheSDLControllerSystem2 );
}

void radControllerTerminate( void )
{
#if SDL_MAJOR_VERSION < 3
    SDL_DelEventWatch( radControllerSystemSDL::CheckDeviceConnectionStatus, s_pTheSDLControllerSystem2 );
#else
    SDL_RemoveEventWatch( radControllerSystemSDL::CheckDeviceConnectionStatus, s_pTheSDLControllerSystem2 );
#endif

    radRelease( s_pTheSDLControllerSystem2, NULL );
}

//============================================================================
// Function: radControllerSystemGet
//============================================================================
//
// Use this function to obtain an interface to the controller system object.
//
IRadControllerSystem* radControllerSystemGet
(
    void
)
{
    rAssert( s_pTheSDLControllerSystem2 != NULL );

    return( s_pTheSDLControllerSystem2 );
}

//============================================================================
// Function: radControllerSystemGet
//============================================================================
//
// Use this function to drive the processing of the controller system
//
void radControllerSystemService( void )
{
    if( s_pTheSDLControllerSystem2 != NULL )
    {
        s_pTheSDLControllerSystem2->Service( );
    }
}



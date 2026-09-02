//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   CGuiScreenPauseOptions
//
// Description: Implementation of the CGuiScreenPauseOptions class.
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2002/07/04      TChu        Created for SRR2
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================
#include <presentation/gui/ingame/guiscreenpauseoptions.h>
#include <presentation/gui/guimenu.h>

#include <cheats/cheatinputsystem.h>
#include <memory/srrmemory.h>

#include <raddebug.hpp> // Foundation
#include <Group.h>
#include <Screen.h>
#include <Page.h>
#include <Text.h>

#ifdef RAD_ANDROID
#include <input/touch/touchhudsystem.h>
#endif

//===========================================================================
// Global Data, Local Data, Local Classes
//===========================================================================

enum ePauseMenuItem
{
#if defined(RAD_PC) || defined(RAD_ANDROID)
    MENU_ITEM_DISPLAY,
#endif
    MENU_ITEM_CONTROLLER,
    MENU_ITEM_SOUND,


    MENU_ITEM_SETTINGS,
//    MENU_ITEM_CAMERA,

    NUM_PAUSE_MENU_ITEMS
};


static const char* PAUSE_MENU_ITEMS[] =
{
#if defined(RAD_PC) || defined(RAD_ANDROID)
    "Display",
#endif
    "Controller",
    "Sound",

//#ifndef RAD_ANDROID // reestablecemos configuración
    "Settings",
//#endif
//    "Camera",

    ""
};

//===========================================================================
// Public Member Functions
//===========================================================================

//===========================================================================
// CGuiScreenPauseOptions::CGuiScreenPauseOptions
//===========================================================================
// Description: Constructor.
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
CGuiScreenPauseOptions::CGuiScreenPauseOptions
(
    Scrooby::Screen* pScreen,
    CGuiEntity* pParent
)
:   CGuiScreen( pScreen, pParent, GUI_SCREEN_ID_OPTIONS ),
    m_pMenu( NULL )
{
MEMTRACK_PUSH_GROUP( "CGUIScreenPauseOptions" );
    // Retrieve the Scrooby drawing elements.
    //
    Scrooby::Page* pPage;
	pPage = m_pScroobyScreen->GetPage( "PauseOptions" );
	rAssert( pPage );

    // Create a menu.
    //
    m_pMenu = new(GMA_LEVEL_HUD) CGuiMenu( this, NUM_PAUSE_MENU_ITEMS );
    rAssert( m_pMenu != NULL );

    // Add menu items
    //
    Scrooby::Group* menu = pPage->GetGroup( "Menu" );
    rAssert( menu != NULL );
    char itemName[ 32 ];
    for( int i = 0; i < NUM_PAUSE_MENU_ITEMS; i++ )
    {
        sprintf( itemName, "%s_Value", PAUSE_MENU_ITEMS[ i ] );
        Scrooby::Text* pTextValue = pPage->GetText( itemName );

        sprintf( itemName, "%s_LArrow", PAUSE_MENU_ITEMS[ i ] );
        Scrooby::Sprite* pLArrow = pPage->GetSprite( itemName );

        sprintf( itemName, "%s_RArrow", PAUSE_MENU_ITEMS[ i ] );
        Scrooby::Sprite* pRArrow = pPage->GetSprite( itemName );

        m_pMenu->AddMenuItem( menu->GetText( PAUSE_MENU_ITEMS[ i ] ),
                              pTextValue,
                              NULL,
                              NULL,
                              pLArrow,
                              pRArrow );
    }

#ifdef RAD_ANDROID
    // The Android project does not instantiate the controller-options screen.
    // Reuse its otherwise hidden top-level row as the entry to VR settings.
    Scrooby::Text* vrEntry = menu->GetText( "Controller" );
    if( vrEntry != NULL )
    {
        vrEntry->SetString( 0, "VR" );
    }

    Scrooby::Text* graphicsEntry = menu->GetText( "Display" );
    if( graphicsEntry != NULL )
    {
        graphicsEntry->SetString( 0, "Graphics" );
    }

    // Reuse the authored Settings row as an entry to the dedicated debug page.
    Scrooby::Text* settingsEntry = menu->GetText( "Settings" );
    if( settingsEntry != NULL ) settingsEntry->SetString( 0, "Debug" );
#endif

    

#if !defined(RAD_PC) && !defined(RAD_ANDROID)
    Scrooby::Text* pText = menu->GetText( "Display" );
    if( pText )
        pText->SetVisible( false );

    // re-center menu items
    //
    menu->ResetTransformation();
    menu->Translate( 0, 50 );
#endif

    // TC: [TEMP] disable controller screen for now to free up some memory for HUD map
    //
#if !defined(RAD_PC) && !defined(RAD_ANDROID)
    m_pMenu->SetMenuItemEnabled( MENU_ITEM_CONTROLLER, false, true );
#endif

#ifdef RAD_E3
    // disable pause menu settings for E3 build
    //
    m_pMenu->SetMenuItemEnabled( MENU_ITEM_SETTINGS, false );
#endif
MEMTRACK_POP_GROUP("CGUIScreenPauseOptions");
}


//===========================================================================
// CGuiScreenPauseOptions::~CGuiScreenPauseOptions
//===========================================================================
// Description: Destructor.
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
CGuiScreenPauseOptions::~CGuiScreenPauseOptions()
{
    #ifdef RAD_ANDROID
    TouchHudSystem::GetInstance().SetTouchControlsEditorEntryAllowed( false );
    #endif

    if( m_pMenu != NULL )
    {
        delete m_pMenu;
        m_pMenu = NULL;
    }
}


//===========================================================================
// CGuiScreenPauseOptions::HandleMessage
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenPauseOptions::HandleMessage
(
	eGuiMessage message, 
	unsigned int param1,
	unsigned int param2 
)
{
    if( this->IsControllerMessage( message ) &&
        GetCheatInputSystem()->IsActivated( param1 ) )
    {
        // ignore all controller inputs when cheat input system is activated
        return;
    }

    if( m_state == GUI_WINDOW_STATE_RUNNING )
    {
        switch( message )
        {
            case GUI_MSG_CONTROLLER_START:
            {
                if( !m_pMenu->HasSelectionBeenMade() )
                {
                    // resume game
                    m_pParent->HandleMessage( GUI_MSG_UNPAUSE_INGAME );
                }

                break;
            }

            case GUI_MSG_MENU_SELECTION_MADE:
            {
                if( param1 == MENU_ITEM_CONTROLLER )
                {
#ifdef RAD_ANDROID
                    m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_VR );
#else
                    m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_CONTROLLER );
#endif
                }
                else if( param1 == MENU_ITEM_SOUND )
                {
                    m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_SOUND );
                }
                
                else if( param1 == MENU_ITEM_SETTINGS )
                {
#ifdef RAD_ANDROID
                     m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_DEBUG );
#else
                     m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_SETTINGS );
#endif
                }
            
#if defined(RAD_PC) || defined(RAD_ANDROID)
                else if( param1 == MENU_ITEM_DISPLAY )
                {
                    m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_DISPLAY );
                }
#endif
                else
                {
                    rAssertMsg( false, "Invalid menu selection!" );
                }

                break;
            }
/*
            case GUI_MSG_MENU_SELECTION_CHANGED:
            {
                this->SetButtonVisible( BUTTON_ICON_ACCEPT, (param1 != MENU_ITEM_CAMERA) );

                break;
            }
*/
            default:
            {
                break;
            }
        }

        // relay message to menu
        if( m_pMenu != NULL )
        {
            m_pMenu->HandleMessage( message, param1, param2 );
        }
    }

	// Propogate the message up the hierarchy.
	//
	CGuiScreen::HandleMessage( message, param1, param2 );
}


//===========================================================================
// CGuiScreenPauseOptions::InitIntro
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenPauseOptions::InitIntro()
{
//    this->SetButtonVisible( BUTTON_ICON_ACCEPT, (m_pMenu->GetSelection() != MENU_ITEM_CAMERA) );

#ifndef RAD_E3
    GetCheatInputSystem()->SetEnabled( true );
#endif

#ifdef RAD_ANDROID
    TouchHudSystem::GetInstance().SetTouchControlsEditorEntryAllowed( true );
#endif
}


//===========================================================================
// CGuiScreenPauseOptions::InitRunning
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenPauseOptions::InitRunning()
{
}


//===========================================================================
// CGuiScreenPauseOptions::InitOutro
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenPauseOptions::InitOutro()
{
#ifndef RAD_E3
    GetCheatInputSystem()->SetEnabled( false );
#endif

#ifdef RAD_ANDROID
    TouchHudSystem::GetInstance().SetTouchControlsEditorEntryAllowed( false );
#endif
}


//---------------------------------------------------------------------
// Private Functions
//---------------------------------------------------------------------


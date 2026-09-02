//===========================================================================
// Copyright (C) 2003 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   CGuiScreenDisplay
//
// Description: Implementation of the CGuiScreenDisplay class.
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2003/06/16      TChu        Created for SRR2
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================
#include <presentation/gui/frontend/guiscreendisplay.h>
#include <presentation/gui/guimenu.h>

#include <data/config/gameconfigmanager.h>
#include <main/win32platform.h>
#include <memory/srrmemory.h>
#include <render/RenderFlow/renderflow.h>
#ifdef RAD_ANDROID
#include <vr/openxrmanager.h>
#include <presentation/gui/ingame/vrmenubuilder.h>
#endif

#include <raddebug.hpp> // Foundation
#include <Screen.h>
#include <Page.h>
#include <Group.h>
#include <Text.h>
#ifdef RAD_ANDROID
#include <Sprite.h>
#include <FePage.h>
#include <FeGroup.h>
#include <FeText.h>
#include <FeSprite.h>
#endif

//===========================================================================
// Global Data, Local Data, Local Classes
//===========================================================================


const char* DISPLAY_MENU_ITEMS[] =
{
    "Resolution",
    "ColourDepth",
    "DisplayMode",
    "Gamma",
    "ApplyChanges",

    ""
};

const float SLIDER_ICON_SCALE = 0.5f;

//===========================================================================
// Public Member Functions
//===========================================================================

//===========================================================================
// CGuiScreenDisplay::CGuiScreenDisplay
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
CGuiScreenDisplay::CGuiScreenDisplay
(
    Scrooby::Screen* pScreen,
    CGuiEntity* pParent
)
:   CGuiScreen( pScreen, pParent, GUI_SCREEN_ID_DISPLAY ),
    m_pMenu( NULL ),
    m_changedGamma( false )
#ifdef RAD_ANDROID
    , m_pRenderScaleLabel( NULL )
    , m_pRefreshRateLabel( NULL )
#endif
{
MEMTRACK_PUSH_GROUP( "CGuiScreenDisplay" );
    // Retrieve the Scrooby drawing elements.
    //
    Scrooby::Page* pPage = m_pScroobyScreen->GetPage( "Display" );
    rAssert( pPage != NULL );

    // Create a menu.
    //
    m_pMenu = new CGuiMenu( this, NUM_MENU_ITEMS );
    rAssert( m_pMenu != NULL );

    // Add menu items
    //
#ifdef RAD_ANDROID
    Scrooby::Group* authored=pPage->GetGroup("Menu");if(authored)authored->SetVisible(false);
    FeText* style=VrMenuBuilder::FindStyleText(pPage);rAssert(style);
    VrMenuBuilder::RememberGraphicsStyle(style);
    const char* const toggle[]={"Off","On"};
    const char* const materials[]={"Off","Phong","PBR","NPR Toon"};
    const char* const lights[]={"Off","Optimized","Max"};
    const char* const reflections[]={"Off","Static","Dynamic"};
    const char* const rates[]={"72 Hz","90 Hz","120 Hz"};
    const char* const scales[]={"50%","60%","70%","80%","90%","100%","110%","120%"};
    const char* const labels[]={"CSM Shadows","Custom Materials","Enhanced Materials","Vehicle Lights","Reflections","Refresh Rate","Render Scale"};
    const char* const* values[]={toggle,toggle,materials,lights,reflections,rates,scales};
    const int counts[]={2,2,4,3,3,3,8};
    for(int i=0;i<NUM_MENU_ITEMS;++i){
        VrMenuBuilder::Row row=VrMenuBuilder::AddRow(pPage,style,"CleanGraphics",i,labels[i],values[i],counts[i],112,51);
        m_pMenu->AddMenuItem(row.label,row.value,NULL,NULL,NULL,NULL,SELECTION_ENABLED|VALUES_WRAPPED|TEXT_OUTLINE_ENABLED);
        m_pMenu->SetSelectionValueCount(i,counts[i]);
        if(i==MENU_ITEM_REFRESH_RATE)m_pRefreshRateLabel=row.value;
        if(i==MENU_ITEM_RENDER_SCALE)m_pRenderScaleLabel=row.value;
    }
#else
    char itemName[ 32 ];

    for( int i = 0; i < MENU_ITEM_GAMMA; i++ )
    {
        Scrooby::Group* group = pPage->GetGroup( DISPLAY_MENU_ITEMS[ i ] );
        rAssert( group != NULL );

        sprintf( itemName, "%s_Value", DISPLAY_MENU_ITEMS[ i ] );
        Scrooby::Text* pTextValue = group->GetText( itemName );

        sprintf( itemName, "%s_ArrowL", DISPLAY_MENU_ITEMS[ i ] );
        Scrooby::Sprite* pLArrow = group->GetSprite( itemName );

        sprintf( itemName, "%s_ArrowR", DISPLAY_MENU_ITEMS[ i ] );
        Scrooby::Sprite* pRArrow = group->GetSprite( itemName );

        m_pMenu->AddMenuItem( group->GetText( DISPLAY_MENU_ITEMS[ i ] ),
                              pTextValue,
                              NULL,
                              NULL,
                              pLArrow,
                              pRArrow,
                              SELECTION_ENABLED | VALUES_WRAPPED | TEXT_OUTLINE_ENABLED );
    }

    // Add the gamma slider
    Scrooby::Group* pgroup = pPage->GetGroup( "Gamma" );
    rAssert(pgroup  != NULL );

    Scrooby::Text* pText = pgroup->GetText( "Gamma" );

    Scrooby::Group* sliderGroup = pgroup->GetGroup( "Gamma_Slider" );
    rAssert( sliderGroup != NULL );

    sliderGroup->ResetTransformation();

    m_pMenu->AddMenuItem( pText,
                          NULL,
                          NULL,
                          sliderGroup->GetSprite( "Gamma_Slider" ),
                          NULL,
                          NULL,
                          SELECTION_ENABLED | VALUES_WRAPPED | TEXT_OUTLINE_ENABLED );

    m_pMenu->GetMenuItem( MENU_ITEM_GAMMA )->m_slider.m_type = Slider::HORIZONTAL_SLIDER_ABOUT_CENTER;

    Scrooby::Sprite* soundOnIcon = pgroup->GetSprite( "Gamma_Icon" );
    soundOnIcon->ScaleAboutCenter( SLIDER_ICON_SCALE );

    // Add the apply changes button

    pgroup = pPage->GetGroup( "Menu" );
    rAssert( pgroup != NULL );

    m_pMenu->AddMenuItem( pgroup->GetText( "ApplyChanges" ) );
#endif

MEMTRACK_POP_GROUP("CGuiScreenDisplay");
}


//===========================================================================
// CGuiScreenDisplay::~CGuiScreenDisplay
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
CGuiScreenDisplay::~CGuiScreenDisplay()
{
    if( m_pMenu != NULL )
    {
        delete m_pMenu;
        m_pMenu = NULL;
    }
}


//===========================================================================
// CGuiScreenDisplay::HandleMessage
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
void CGuiScreenDisplay::HandleMessage
(
	eGuiMessage message, 
	unsigned int param1,
	unsigned int param2 
)
{
    if( m_state == GUI_WINDOW_STATE_RUNNING )
    {
#ifdef RAD_ANDROID
        // PauseDisplay's authored navigation has its vertical axis opposite
        // to PauseSettings (used by VR). Normalize it here so both screens
        // react identically to the Quest stick.
        if( message == GUI_MSG_CONTROLLER_UP ) message = GUI_MSG_CONTROLLER_DOWN;
        else if( message == GUI_MSG_CONTROLLER_DOWN ) message = GUI_MSG_CONTROLLER_UP;

        // Use the same latched-axis navigation as the dedicated VR screen.
        // The OpenXR stick must return to neutral before a perpendicular menu
        // direction can be accepted.
        if( ( message == GUI_MSG_CONTROLLER_UP || message == GUI_MSG_CONTROLLER_DOWN ) &&
            SharOpenXR::IsHorizontalMenuInputDominant() )
        {
            message = GUI_MSG_UPDATE;
            param1 = 0;
        }
        if( ( message == GUI_MSG_CONTROLLER_LEFT || message == GUI_MSG_CONTROLLER_RIGHT ) &&
            SharOpenXR::IsVerticalMenuInputDominant() )
        {
            message = GUI_MSG_UPDATE;
            param1 = 0;
        }
#endif
        switch( message )
        {
            case GUI_MSG_MENU_SELECTION_MADE:
            {
                switch( param1 )
                {
#ifdef RAD_ANDROID
                    case MENU_ITEM_REFRESH_RATE:
                    {
                        const float current=SharOpenXR::GetRefreshRate();
                        SharOpenXR::SetRefreshRate(current<80.0f?90.0f:(current<105.0f?120.0f:72.0f));
                        UpdateVrDisplayLabels();
                        break;
                    }
#else
                    case MENU_ITEM_APPLY_CHANGES:
                    {
                        ApplySettings();
                        break;
                    }
#endif
                }
                break;
            }
            case GUI_MSG_MENU_SELECTION_VALUE_CHANGED:
            {
                rAssert( m_pMenu );
                GuiMenuItem* currentItem = m_pMenu->GetMenuItem( param1 );
                rAssert( currentItem );

                switch( param1 )
                {
#ifdef RAD_ANDROID
                    case MENU_ITEM_REFRESH_RATE:
                    {
                        const float rates[3]={72.0f,90.0f,120.0f};
                        if(param2<3) SharOpenXR::SetRefreshRate(rates[param2]);
                        UpdateVrDisplayLabels();
                        break;
                    }
                    case MENU_ITEM_CSM:
                    {
                        SharOpenXR::SetCsmEnabled( param2 != 0 );
                        break;
                    }
                    case MENU_ITEM_ENHANCED_MATERIALS:
                    {
                        SharOpenXR::SetEnhancedMaterialModel( static_cast<int>(param2) );
                        break;
                    }
                    case MENU_ITEM_VEHICLE_LIGHTS:
                    {
                        SharOpenXR::SetVehicleLightMode( static_cast<int>(param2) );
                        break;
                    }
                    case MENU_ITEM_CUSTOM_MATERIALS:
                    {
                        SharOpenXR::SetCustomMaterialsEnabled(param2!=0);
                        break;
                    }
                    case MENU_ITEM_REFLECTIONS:
                    {
                        SharOpenXR::SetReflectionMode(static_cast<int>(param2));
                        break;
                    }
                    case MENU_ITEM_RENDER_SCALE:
                    {
                        if(param2<8) SharOpenXR::SetRenderScale(0.5f+0.1f*param2);
                        break;
                    }
#else
                    case MENU_ITEM_GAMMA:
                    {
                        float gamma = 2 * currentItem->m_slider.m_value + 0.5f;
                        GetRenderFlow()->SetGamma( gamma );
                        m_changedGamma = true;

                        break;
                    }
#endif
                }
                break;
            }
#ifdef RAD_ANDROID
            case GUI_MSG_MENU_SLIDER_NOT_CHANGING:
            {
                if( param1 == MENU_ITEM_RENDER_SCALE )
                {
                    const float slider=m_pMenu->GetMenuItem( MENU_ITEM_RENDER_SCALE )->m_slider.m_value;
                    SharOpenXR::SetRenderScale( 0.10f + slider * 1.90f );
                    UpdateVrDisplayLabels();
                }
                break;
            }
#endif
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
// CGuiScreenDisplay::InitIntro
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
void CGuiScreenDisplay::InitIntro()
{
#ifdef RAD_ANDROID
    m_pMenu->SetSelectionValue( MENU_ITEM_CSM,
                                SharOpenXR::IsCsmEnabled() ? 1 : 0 );
    m_pMenu->SetSelectionValue( MENU_ITEM_CUSTOM_MATERIALS,
                                SharOpenXR::AreCustomMaterialsEnabled() ? 1 : 0 );
    m_pMenu->SetSelectionValue( MENU_ITEM_ENHANCED_MATERIALS,
                                SharOpenXR::GetEnhancedMaterialModel() );
    m_pMenu->SetSelectionValue( MENU_ITEM_VEHICLE_LIGHTS,
                                SharOpenXR::GetVehicleLightMode() );
    m_pMenu->SetSelectionValue( MENU_ITEM_REFLECTIONS,
                                SharOpenXR::GetReflectionMode() );
    m_pMenu->SetSelectionValue(MENU_ITEM_RENDER_SCALE,
        static_cast<int>(rmt::Clamp((SharOpenXR::GetRenderScale()-0.5f)/0.1f+0.5f,0.0f,7.0f)));
    UpdateVrDisplayLabels();
#else
    // update settings
    //
    Win32Platform* plat = Win32Platform::GetInstance();

    Win32Platform::Resolution res = plat->GetResolution();
    m_pMenu->SetSelectionValue( MENU_ITEM_RESOLUTION,
                                res );

    int bpp = plat->GetBPP();
    m_pMenu->SetSelectionValue( MENU_ITEM_COLOUR_DEPTH,
                                bpp == 16 ? 0: 1 );

    bool fullscreen = plat->IsFullscreen();
    m_pMenu->SetSelectionValue( MENU_ITEM_DISPLAY_MODE,
                                fullscreen ? 1 : 0 );

    GuiMenuItem* menuItem = m_pMenu->GetMenuItem( MENU_ITEM_GAMMA );
    rAssert( menuItem );
    menuItem->m_slider.SetValue( ( GetRenderFlow()->GetGamma() - 0.5f ) / 2.0f );
#endif
}


//===========================================================================
// CGuiScreenDisplay::InitRunning
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
void CGuiScreenDisplay::InitRunning()
{
}


//===========================================================================
// CGuiScreenDisplay::InitOutro
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
void CGuiScreenDisplay::InitOutro()
{
#ifndef RAD_ANDROID
    // Save the config if we've changed the gamma settings
    if( m_changedGamma )
    {
        GetGameConfigManager()->SaveConfigFile();
        m_changedGamma = false;
    }
#endif
}


//---------------------------------------------------------------------
// Private Functions
//---------------------------------------------------------------------

//===========================================================================
// CGuiScreenDisplay::ApplySettings
//===========================================================================
// Description: Applies the current display settings to teh game. 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenDisplay::ApplySettings()
{
#ifndef RAD_ANDROID
    // Retrieve the settings.
    //
    Win32Platform::Resolution res = static_cast< Win32Platform::Resolution >( m_pMenu->GetSelectionValue( MENU_ITEM_RESOLUTION ) );

    int bpp = m_pMenu->GetSelectionValue( MENU_ITEM_COLOUR_DEPTH ) ? 32: 16;

    bool fullscreen = m_pMenu->GetSelectionValue( MENU_ITEM_DISPLAY_MODE ) == 1;

    // Set the resolution.
    Win32Platform::GetInstance()->SetResolution( res, bpp, fullscreen );

    // Save the change to the config file.
    GetGameConfigManager()->SaveConfigFile();
    m_changedGamma = false;
#endif
}

#ifdef RAD_ANDROID
void CGuiScreenDisplay::UpdateVrDisplayLabels()
{
    if( m_pRefreshRateLabel != NULL )
    {
        const float rate=SharOpenXR::GetRefreshRate();
        const int selection=rate<80.0f?0:(rate<105.0f?1:2);
        // SetSelectionValue emits GUI_MSG_MENU_SELECTION_VALUE_CHANGED.  Do
        // not emit it again while handling that same notification.
        if(m_pMenu->GetSelectionValue(MENU_ITEM_REFRESH_RATE)!=selection)
            m_pMenu->SetSelectionValue(MENU_ITEM_REFRESH_RATE,selection);
    }
}
#endif

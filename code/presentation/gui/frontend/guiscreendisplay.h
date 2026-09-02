//===========================================================================
// Copyright (C) 2003 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   CGuiScreenDisplay
//
// Description: 
//              
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2003/06/16      TChu        Created for SRR2
//
//===========================================================================

#ifndef GUISCREENDISPLAY_H
#define GUISCREENDISPLAY_H

//===========================================================================
// Nested Includes
//===========================================================================
#include <presentation/gui/guiscreen.h>

//===========================================================================
// Forward References
//===========================================================================
class CGuiMenu;
namespace Scrooby { class Text; }

//===========================================================================
// Interface Definitions
//===========================================================================
class CGuiScreenDisplay : public CGuiScreen
{
public:
    CGuiScreenDisplay( Scrooby::Screen* pScreen, CGuiEntity* pParent );
    virtual ~CGuiScreenDisplay();

	virtual void HandleMessage( eGuiMessage message, 
			                    unsigned int param1 = 0,
								unsigned int param2 = 0 );

    virtual CGuiMenu* HasMenu() { return m_pMenu; }

protected:
    void InitIntro();
	void InitRunning();
	void InitOutro();

private:
    void ApplySettings();

private:
    enum eMenuItem
    {
#ifdef RAD_ANDROID
        MENU_ITEM_CSM,
        MENU_ITEM_CUSTOM_MATERIALS,
        MENU_ITEM_ENHANCED_MATERIALS,
        MENU_ITEM_VEHICLE_LIGHTS,
        MENU_ITEM_REFLECTIONS,
        MENU_ITEM_REFRESH_RATE,
        MENU_ITEM_RENDER_SCALE,
#else
        MENU_ITEM_RESOLUTION,
        MENU_ITEM_COLOUR_DEPTH,
        MENU_ITEM_DISPLAY_MODE,
        MENU_ITEM_GAMMA,
        MENU_ITEM_APPLY_CHANGES,
#endif

        NUM_MENU_ITEMS
    };

    CGuiMenu* m_pMenu;
    bool      m_changedGamma;
#ifdef RAD_ANDROID
    Scrooby::Text* m_pRenderScaleLabel;
    Scrooby::Text* m_pRefreshRateLabel;
    void UpdateVrDisplayLabels();
#endif
};

#endif // GUISCREENDISPLAY_H

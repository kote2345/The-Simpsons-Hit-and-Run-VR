#ifndef GUISCREENPAUSEVR_H
#define GUISCREENPAUSEVR_H

#include <presentation/gui/guiscreen.h>

class CGuiMenu;
namespace Scrooby { class Screen; class Page; class Group; class Text; }

class CGuiScreenPauseVR : public CGuiScreen
{
public:
    CGuiScreenPauseVR( Scrooby::Screen* screen, CGuiEntity* parent );
    virtual ~CGuiScreenPauseVR();
    virtual void HandleMessage( eGuiMessage message,
                                unsigned int param1 = 0,
                                unsigned int param2 = 0 );
    virtual CGuiMenu* HasMenu() { return m_pMenu; }

protected:
    void InitIntro();
    void InitRunning();
    void InitOutro();

private:
    void SetVrLayoutVisible( bool visible );
    void SetRowVisible( int row, bool visible );
    void UpdateNumericValue( int row );
    CGuiMenu* m_pMenu;
    Scrooby::Page* m_pPage;
    Scrooby::Group* m_pRows[7];
    Scrooby::Text* m_pLabels[7];
    Scrooby::Text* m_pValues[7];
    int m_numericValues[2];
    int m_numRows;
    bool m_frontendLayout;
};

#endif

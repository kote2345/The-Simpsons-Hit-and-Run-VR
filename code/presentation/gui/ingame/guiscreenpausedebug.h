#ifndef GUISCREENPAUSEDEBUG_H
#define GUISCREENPAUSEDEBUG_H

#include <presentation/gui/guiscreen.h>

class CGuiMenu;
namespace Scrooby { class Screen; class Page; class Group; }

class CGuiScreenPauseDebug : public CGuiScreen
{
public:
    CGuiScreenPauseDebug( Scrooby::Screen* screen, CGuiEntity* parent );
    virtual ~CGuiScreenPauseDebug();
    virtual void HandleMessage( eGuiMessage message,
                                unsigned int param1 = 0,
                                unsigned int param2 = 0 );
    virtual CGuiMenu* HasMenu() { return m_pMenu; }

protected:
    void InitIntro();
    void InitRunning();
    void InitOutro();

private:
    CGuiMenu* m_pMenu;
    Scrooby::Page* m_pPage;
    Scrooby::Group* m_pRow;
};

#endif

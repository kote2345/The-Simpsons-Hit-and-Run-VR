#include <presentation/gui/ingame/guiscreenpausedebug.h>
#include <presentation/gui/ingame/vrmenubuilder.h>
#include <presentation/gui/guimenu.h>
#include <vr/openxrmanager.h>
#include <raddebug.hpp>
#include <Screen.h>
#include <Page.h>
#include <Group.h>
#include <Text.h>
#include <FeText.h>

namespace
{
const char* const PbrMapValues[]={"Off","Normal","Roughness","Metallic","All"};
}

CGuiScreenPauseDebug::CGuiScreenPauseDebug(Scrooby::Screen* screen,CGuiEntity* parent)
: CGuiScreen(screen,parent,GUI_SCREEN_ID_DEBUG),m_pMenu(NULL),m_pPage(NULL),m_pRow(NULL)
{
    m_pPage=m_pScroobyScreen->GetPage("PauseSettings");
    rAssert(m_pPage);
    Scrooby::Group* authored=m_pPage->GetGroup("Menu");
    if(authored) authored->SetVisible(false);
    FeText* style=VrMenuBuilder::FindStyleText(m_pPage);
    rAssert(style);
    m_pMenu=new CGuiMenu(this,1);
    VrMenuBuilder::Row row=VrMenuBuilder::AddRow(m_pPage,style,"CleanDebug",0,
        "PBR Maps",PbrMapValues,5,118,49,true);
    m_pRow=row.group;
    rAssert(row.label&&row.value);
    m_pMenu->AddMenuItem(row.label,row.value,NULL,NULL,NULL,NULL,
        SELECTION_ENABLED|VALUES_WRAPPED|TEXT_OUTLINE_ENABLED);
    m_pMenu->SetSelectionValueCount(0,5);
    if(m_pRow) m_pRow->SetVisible(false);
}

CGuiScreenPauseDebug::~CGuiScreenPauseDebug()
{
    delete m_pMenu;
    m_pMenu=NULL;
}

void CGuiScreenPauseDebug::HandleMessage(eGuiMessage message,unsigned int param1,unsigned int param2)
{
    if(m_state==GUI_WINDOW_STATE_RUNNING)
    {
        if(message==GUI_MSG_CONTROLLER_UP) message=GUI_MSG_CONTROLLER_DOWN;
        else if(message==GUI_MSG_CONTROLLER_DOWN) message=GUI_MSG_CONTROLLER_UP;
        if((message==GUI_MSG_CONTROLLER_UP||message==GUI_MSG_CONTROLLER_DOWN)&&
           SharOpenXR::IsHorizontalMenuInputDominant()) message=GUI_MSG_UPDATE;
        if((message==GUI_MSG_CONTROLLER_LEFT||message==GUI_MSG_CONTROLLER_RIGHT)&&
           SharOpenXR::IsVerticalMenuInputDominant()) message=GUI_MSG_UPDATE;
        if(message==GUI_MSG_CONTROLLER_START)
            m_pParent->HandleMessage(GUI_MSG_UNPAUSE_INGAME);
        else if(message==GUI_MSG_MENU_SELECTION_VALUE_CHANGED && param1==0)
            SharOpenXR::SetPbrDebugMode(static_cast<int>(param2));
        if(m_pMenu) m_pMenu->HandleMessage(message,param1,param2);
    }
    CGuiScreen::HandleMessage(message,param1,param2);
}

void CGuiScreenPauseDebug::InitIntro()
{
    if(m_pRow) m_pRow->SetVisible(true);
    m_pMenu->SetSelectionValue(0,SharOpenXR::GetPbrDebugMode());
}

void CGuiScreenPauseDebug::InitRunning() {}

void CGuiScreenPauseDebug::InitOutro()
{
    if(m_pRow) m_pRow->SetVisible(false);
}

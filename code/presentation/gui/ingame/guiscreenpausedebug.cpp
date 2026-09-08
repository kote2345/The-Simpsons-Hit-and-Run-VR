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
const char* const GiViewValues[]={"Composite","Volume Only"};
}

CGuiScreenPauseDebug::CGuiScreenPauseDebug(Scrooby::Screen* screen,CGuiEntity* parent)
: CGuiScreen(screen,parent,GUI_SCREEN_ID_DEBUG),m_pMenu(NULL),m_pPage(NULL)
{
    m_pPage=m_pScroobyScreen->GetPage("PauseSettings");
    rAssert(m_pPage);
    Scrooby::Group* authored=m_pPage->GetGroup("Menu");
    if(authored) authored->SetVisible(false);
    FeText* style=VrMenuBuilder::FindStyleText(m_pPage);
    rAssert(style);
    m_pRows[0]=m_pRows[1]=NULL;
    m_pMenu=new CGuiMenu(this,2);
    VrMenuBuilder::Row row=VrMenuBuilder::AddRow(m_pPage,style,"CleanDebug",0,
        "PBR Maps",PbrMapValues,5,118,49,true);
    m_pRows[0]=row.group;
    rAssert(row.label&&row.value);
    m_pMenu->AddMenuItem(row.label,row.value,NULL,NULL,NULL,NULL,
        SELECTION_ENABLED|VALUES_WRAPPED|TEXT_OUTLINE_ENABLED);
    m_pMenu->SetSelectionValueCount(0,5);
    row=VrMenuBuilder::AddRow(m_pPage,style,"CleanDebug",1,
        "Volume View",GiViewValues,2,118,49,true);
    m_pRows[1]=row.group;
    rAssert(row.label&&row.value);
    m_pMenu->AddMenuItem(row.label,row.value,NULL,NULL,NULL,NULL,
        SELECTION_ENABLED|VALUES_WRAPPED|TEXT_OUTLINE_ENABLED);
    m_pMenu->SetSelectionValueCount(1,2);
    for(unsigned i=0;i<2;++i) if(m_pRows[i]) m_pRows[i]->SetVisible(false);
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
        else if(message==GUI_MSG_MENU_SELECTION_VALUE_CHANGED && param1==1)
            SharOpenXR::SetGiIndirectOnly(param2!=0);
        if(m_pMenu) m_pMenu->HandleMessage(message,param1,param2);
    }
    CGuiScreen::HandleMessage(message,param1,param2);
}

void CGuiScreenPauseDebug::InitIntro()
{
    for(unsigned i=0;i<2;++i) if(m_pRows[i]) m_pRows[i]->SetVisible(true);
    m_pMenu->SetSelectionValue(0,SharOpenXR::GetPbrDebugMode());
    m_pMenu->SetSelectionValue(1,SharOpenXR::IsGiIndirectOnly()?1:0);
}

void CGuiScreenPauseDebug::InitRunning() {}

void CGuiScreenPauseDebug::InitOutro()
{
    for(unsigned i=0;i<2;++i) if(m_pRows[i]) m_pRows[i]->SetVisible(false);
}

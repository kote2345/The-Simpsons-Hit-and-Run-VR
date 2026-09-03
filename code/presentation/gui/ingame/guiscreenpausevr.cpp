#include <presentation/gui/ingame/guiscreenpausevr.h>
#include <presentation/gui/ingame/vrmenubuilder.h>
#include <presentation/gui/guimenu.h>
#include <vr/openxrmanager.h>
#include <raddebug.hpp>
#include <Screen.h>
#include <Page.h>
#include <Group.h>
#include <Text.h>
#include <FeText.h>

namespace {
const char* const Labels[7]={"Mode","Seated Mode","Turn Mode","Turn Speed","Vehicle Control","Vehicle Comfort","Developer Menus"};
const float SmoothSpeeds[5]={45,90,120,180,240},SnapAngles[5]={15,30,45,60,90};
const char* const ModeValues[]={"Original","VR"};
const char* const ToggleValues[]={"Off","On"};
const char* const TurnValues[]={"Smooth","Snap"};
const char* const SmoothSpeedValues[]={"45","90","120","180","240"};
const char* const SnapAngleValues[]={"15","30","45","60","90"};
const char* const VehicleValues[]={"Stick","VR Wheel","Third Person"};
int Closest(const float* v,float x){int b=0;for(int i=1;i<5;++i)if(rmt::Fabs(v[i]-x)<rmt::Fabs(v[b]-x))b=i;return b;}
}

CGuiScreenPauseVR::CGuiScreenPauseVR(Scrooby::Screen* screen,CGuiEntity* parent)
:CGuiScreen(screen,parent,GUI_SCREEN_ID_VR),m_pMenu(NULL),m_pPage(NULL),m_numRows(7),m_frontendLayout(false)
{
    for(int i=0;i<7;++i){m_pRows[i]=NULL;m_pLabels[i]=NULL;m_pValues[i]=NULL;}
    m_numericValues[0]=0;
    m_numericValues[1]=0;
    m_pPage=m_pScroobyScreen->GetPage("PauseSettings");
    if(!m_pPage){m_pPage=m_pScroobyScreen->GetPage("Controller");m_frontendLayout=true;}
    rAssert(m_pPage);
    Scrooby::Group* authored=m_pPage->GetGroup("Menu");if(authored)authored->SetVisible(false);
    FeText* style=VrMenuBuilder::FindStyleText(m_pPage);rAssert(style);
    m_pMenu=new CGuiMenu(this,m_numRows);
    const char* const* values[7]={ModeValues,ToggleValues,TurnValues,SmoothSpeedValues,VehicleValues,ToggleValues,ToggleValues};
    const int counts[7]={2,2,2,5,3,2,2};
    for(int i=0;i<7;++i){
        VrMenuBuilder::Row row=VrMenuBuilder::AddRow(m_pPage,style,"CleanVR",i,Labels[i],values[i],counts[i],82,43,true);
        m_pRows[i]=row.group;m_pLabels[i]=row.label;m_pValues[i]=row.value;rAssert(row.label&&row.value);
        m_pMenu->AddMenuItem(row.label,row.value,NULL,NULL,NULL,NULL,SELECTION_ENABLED|VALUES_WRAPPED|TEXT_OUTLINE_ENABLED);
        m_pMenu->SetSelectionValueCount(i,counts[i]);
    }
    SetVrLayoutVisible(false);
}
CGuiScreenPauseVR::~CGuiScreenPauseVR(){delete m_pMenu;m_pMenu=NULL;}

void CGuiScreenPauseVR::HandleMessage(eGuiMessage message,unsigned int param1,unsigned int param2)
{
    if(m_state==GUI_WINDOW_STATE_RUNNING){
        if(message==GUI_MSG_CONTROLLER_UP)message=GUI_MSG_CONTROLLER_DOWN;
        else if(message==GUI_MSG_CONTROLLER_DOWN)message=GUI_MSG_CONTROLLER_UP;
        if((message==GUI_MSG_CONTROLLER_UP||message==GUI_MSG_CONTROLLER_DOWN)&&SharOpenXR::IsHorizontalMenuInputDominant())message=GUI_MSG_UPDATE;
        if((message==GUI_MSG_CONTROLLER_LEFT||message==GUI_MSG_CONTROLLER_RIGHT)&&SharOpenXR::IsVerticalMenuInputDominant())message=GUI_MSG_UPDATE;
        if(message==GUI_MSG_CONTROLLER_START)m_pParent->HandleMessage(GUI_MSG_UNPAUSE_INGAME);
        else if(message==GUI_MSG_MENU_SELECTION_VALUE_CHANGED){
            if(param1==0)SharOpenXR::SetVrModeEnabled(param2!=0);
            else if(param1==1)SharOpenXR::SetSeatedMode(param2!=0);
            else if(param1==2){SharOpenXR::SetSnapTurnEnabled(param2!=0);UpdateNumericValue(3);}
            else if(param1==3&&param2<5){const int mode=SharOpenXR::IsSnapTurnEnabled()?1:0;m_numericValues[mode]=param2;if(mode)SharOpenXR::SetSnapTurnAngle(SnapAngles[param2]);else SharOpenXR::SetSmoothTurnSpeed(SmoothSpeeds[param2]);}
            else if(param1==4)SharOpenXR::SetVehicleControlMode(static_cast<int>(param2));
            else if(param1==5)SharOpenXR::SetVehicleComfortEnabled(param2!=0);
            else if(param1==6)SharOpenXR::SetDeveloperMenusEnabled(param2!=0);
        }
        if(m_pMenu)m_pMenu->HandleMessage(message,param1,param2);
    }
    CGuiScreen::HandleMessage(message,param1,param2);
}
void CGuiScreenPauseVR::SetVrLayoutVisible(bool visible){for(int i=0;i<7;++i)SetRowVisible(i,visible);}
void CGuiScreenPauseVR::SetRowVisible(int row,bool visible){if(row>=0&&row<7&&m_pRows[row])m_pRows[row]->SetVisible(visible);}
void CGuiScreenPauseVR::UpdateNumericValue(int row){
    if(row!=3)return;
    const bool snap=SharOpenXR::IsSnapTurnEnabled();
    const char* const* values=snap?SnapAngleValues:SmoothSpeedValues;
    if(m_pLabels[row])m_pLabels[row]->SetString(0,snap?"Turn Angle":"Turn Speed");
    if(m_pValues[row])for(int i=0;i<5;++i)m_pValues[row]->SetString(i,values[i]);
    m_pMenu->SetSelectionValue(row,m_numericValues[snap?1:0]);
}
void CGuiScreenPauseVR::InitIntro(){
    m_numericValues[0]=Closest(SmoothSpeeds,SharOpenXR::GetSmoothTurnSpeed());m_numericValues[1]=Closest(SnapAngles,SharOpenXR::GetSnapTurnAngle());
    SetVrLayoutVisible(true);m_pMenu->SetSelectionValue(0,SharOpenXR::IsVrModeEnabled()?1:0);
    m_pMenu->SetSelectionValue(1,SharOpenXR::IsSeatedMode()?1:0);m_pMenu->SetSelectionValue(2,SharOpenXR::IsSnapTurnEnabled()?1:0);
    m_pMenu->SetSelectionValue(4,SharOpenXR::GetVehicleControlMode());
    m_pMenu->SetSelectionValue(5,SharOpenXR::IsVehicleComfortEnabled()?1:0);
    m_pMenu->SetSelectionValue(6,SharOpenXR::IsDeveloperMenusEnabled()?1:0);
    UpdateNumericValue(3);
}
void CGuiScreenPauseVR::InitRunning(){}
void CGuiScreenPauseVR::InitOutro(){SetVrLayoutVisible(false);}

#include <vr/openxr_shared_vehicle.h>
#include <vr/openxr_shared_state.h>
#include <vr/openxr_shared_hands.h>
#include <vr/openxr_shared_render.h>
#include <p3d/shader.hpp>
#include <p3d/utility.hpp>
#include <worldsim/character/charactermanager.h>
#include <worldsim/character/character.h>
#include <worldsim/redbrick/vehicle.h>
#include <worldsim/traffic/trafficmanager.h>
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace SharOpenXR
{
namespace
{
const float PI=3.14159265f;
const float MAX_ANGLE=2.09439510f;
const float GRAB=0.55f,RELEASE=0.42f;
const float OPTIMAL=1.5707963f;
const float YOKE_ARM=0.11f;

struct WheelOffset
{
    char name[48];float x,y,z,yaw,pitch,radius,yokeX,yokeY,yokeZ;bool hasYoke;
};
WheelOffset offsets[64];int offsetCount=0;bool offsetsLoaded=false;
char appliedVehicle[48]={};

std::string WheelOffsetPath()
{
    char* base=SDL_GetPrefPath("c4rlox","simpsons");if(!base)return std::string();
    std::string path(base);SDL_free(base);path+="wheeloffsets.cfg";return path;
}

void AddOffset(const char* name,float x,float y,float z,float yaw,float pitch,float radius)
{
    if(offsetCount>=64)return;WheelOffset& o=offsets[offsetCount++];std::memset(&o,0,sizeof(o));
    std::strncpy(o.name,name,sizeof(o.name)-1);o.x=x;o.y=y;o.z=z;o.yaw=yaw;o.pitch=pitch;o.radius=radius;
    o.yokeX=0.0f;o.yokeY=-0.33f;o.yokeZ=0.40f;
}

void LoadOffsets()
{
    if(offsetsLoaded)return;offsetsLoaded=true;
    struct Builtin{const char* n;float x,y,z,a,p,r;};
    static const Builtin defaults[]={
        {"lisa_v",.01613f,-.38500f,.39779f,-.00659f,.50959f,.19446f},{"famil_v",-.00382f,-.41158f,.37945f,.02341f,.66233f,.20877f},
        {"apu_v",.00631f,-.46367f,.36340f,.02732f,.44522f,.20648f},{"cNerd",.01044f,-.31434f,.35121f,.00784f,.46942f,.20698f},
        {"otto_v",.10405f,-.47492f,.31119f,.02658f,.60326f,.20893f},{"scorp_v",.01657f,-.47334f,.43078f,.05542f,.61113f,.21795f},
        {"krust_v",.00171f,-.47198f,.36888f,.02079f,.51784f,.21710f},{"snake_v",.03784f,-.45360f,.37548f,.01195f,.50825f,.20563f},
        {"moe_v",.01468f,-.36201f,.42586f,.00891f,.46700f,.21367f},{"skinn_v",-.01012f,-.44707f,.41667f,.00720f,.42524f,.21238f},
        {"homer_v",.01300f,-.46922f,.33622f,.02873f,.51844f,.21542f},{"zombi_v",.00793f,-.46667f,.47731f,.05720f,.52894f,.21259f},
        {"burns_v",-.01447f,-.45135f,.38583f,-.01748f,.73589f,.18244f},{"willi_v",-.00136f,-.35275f,.47582f,.02595f,1.04442f,.25279f},
        {"gramp_v",.02381f,-.43905f,.42957f,.01888f,1.15f,.19824f},{"gramR_v",.02831f,-.39646f,.44110f,.02155f,1.08657f,.19177f},
        {"knigh_v",.02858f,-.41090f,.31822f,.02204f,.58144f,.20157f},{"oblit_v",-.01167f,-.34204f,.30652f,.02919f,.53615f,.20857f},
        {"hype_v",-.00799f,-.40367f,.38671f,.01870f,.49044f,.20550f},{"cArmor",-.04665f,-.53862f,.43590f,.03451f,.47955f,.20010f},
        {"cSedan",-.03386f,-.25671f,.45302f,.02565f,.48265f,.20716f},{"cCola",-.01237f,-.35964f,.31904f,-.00799f,.49753f,.19719f},
        {"cCube",.01124f,-.36866f,.35387f,.03337f,.46037f,.20433f},{"cCurator",.04684f,-.33153f,.32621f,.03073f,.48005f,.21307f},
        {"cDonut",.00071f,-.44639f,.33799f,-.02624f,1.14421f,.20656f},{"cDuff",-.03209f,-.35631f,.37125f,.02190f,.48579f,.20701f},
        {"cHears",-.00628f,-.43141f,.34741f,.02927f,.49724f,.19691f},{"cKlimo",.00119f,-.33745f,.38750f,.01110f,.48720f,.20670f},
        {"cLimo",.05795f,-.41616f,.47305f,.01548f,.55664f,.20558f},{"cPolice",.05657f,-.43103f,.38435f,.01117f,.52202f,.23457f},
        {"cVan",.00846f,-.36129f,.29493f,.01696f,.50010f,.21625f},{"cFire_v",-.01471f,-.41707f,.41267f,.03243f,1.15f,.27762f},
        {"cBone",.01391f,-.37719f,.34691f,.01425f,.44759f,.20645f},{"bookb_v",-.00635f,-.48137f,.36469f,-.01464f,.73348f,.21530f},
        {"marge_v",.06615f,-.35983f,.35690f,.01337f,.37457f,.20041f},{"carhom_v",.08383f,-.46583f,.42535f,.06928f,.65502f,.20769f},
        {"bbman_v",-.08973f,-.37953f,.38698f,.01189f,.61379f,.25165f},{"elect_v",.01657f,-.44480f,.36772f,.04736f,.55949f,.22825f},
        {"bart_v",.01335f,-.48874f,.42682f,.02642f,.51202f,.20959f},{"frink_v",-.05880f,-.40912f,.39067f,.01342f,.71745f,.22367f},
        {"smith_v",-.04799f,-.36498f,.46251f,.00579f,.44360f,.25196f},{"mrplo_v",.02160f,-.40818f,.34203f,.02051f,.29448f,.22384f},
        {"fone_v",-.00916f,-.38110f,.33936f,-.00373f,.56705f,.22477f},{"cletu_v",.00412f,-.34147f,.37589f,.00175f,.57438f,.22487f},
        {"plowk_v",-.00522f,-.37062f,.31314f,-.00703f,.67230f,.20901f},{"wiggu_v",-.05401f,-.35053f,.35330f,.01404f,.36360f,.20733f},
        {"huskA",.00667f,.43383f,.06179f,.12148f,-.21514f,.10f}};
    for(unsigned i=0;i<sizeof(defaults)/sizeof(defaults[0]);++i)
        AddOffset(defaults[i].n,defaults[i].x,defaults[i].y,defaults[i].z,defaults[i].a,defaults[i].p,defaults[i].r);
    const std::string path=WheelOffsetPath();FILE* file=path.empty()?NULL:std::fopen(path.c_str(),"rb");if(!file)return;
    char line[256];while(std::fgets(line,sizeof(line),file))
    {
        char name[48]={};float x=0,y=0,z=0,a=0,p=0,r=.18f,yx=0,yy=0,yz=0;bool hasYoke=false;
        int n=std::sscanf(line,"%47[^=]=%f,%f,%f,%f,%f,%f;%f,%f,%f",name,&x,&y,&z,&a,&p,&r,&yx,&yy,&yz);
        if(n!=10){n=std::sscanf(line,"%47[^=]=%f,%f,%f,%f,%f,%f",name,&x,&y,&z,&a,&p,&r);if(n!=7)continue;}else hasYoke=true;
        int slot=-1;for(int i=0;i<offsetCount;++i)if(!std::strcmp(offsets[i].name,name)){slot=i;break;}
        if(slot<0){if(offsetCount>=64)continue;slot=offsetCount++;std::memset(&offsets[slot],0,sizeof(offsets[slot]));std::strncpy(offsets[slot].name,name,47);}
        WheelOffset& o=offsets[slot];o.x=x;o.y=y;o.z=z;o.yaw=a;o.pitch=p;o.radius=r;o.hasYoke=hasYoke;
        if(hasYoke){o.yokeX=yx;o.yokeY=yy;o.yokeZ=yz;}
    }std::fclose(file);
}

void ApplyOffset(const char* name,bool yoke)
{
    SharedVrState& s=GetSharedVrState();LoadOffsets();if(!name)name="";
    if(!std::strcmp(appliedVehicle,name))return;
    std::strncpy(appliedVehicle,name,sizeof(appliedVehicle)-1);appliedVehicle[sizeof(appliedVehicle)-1]=0;
    s.activeWheelCentre.Set(0.0f,-0.32f,0.52f);s.activeWheelYaw=s.activeWheelPitch=0;s.activeWheelRadius=.18f;
    s.activeYokeAnchor.Set(0.0f,-0.33f,0.40f);s.wheelMeshHidden=false;
    for(int i=0;i<offsetCount;++i)if(!std::strcmp(offsets[i].name,name))
    {const WheelOffset& o=offsets[i];s.activeWheelCentre.Set(o.x,o.y,o.z);s.activeWheelYaw=o.yaw;s.activeWheelPitch=o.pitch;s.activeWheelRadius=o.radius;
     if(yoke&&o.hasYoke)s.activeYokeAnchor.Set(o.yokeX,o.yokeY,o.yokeZ);else s.activeYokeAnchor.Set(o.x,o.y-YOKE_ARM+.01f,o.z-.12f);s.wheelMeshHidden=true;break;}
}

void StoreOffset(const char* name,bool yoke)
{
    if(!name||!*name)return;SharedVrState& s=GetSharedVrState();LoadOffsets();int slot=-1;
    for(int i=0;i<offsetCount;++i)if(!std::strcmp(offsets[i].name,name)){slot=i;break;}
    if(slot<0){if(offsetCount>=64)return;slot=offsetCount++;std::memset(&offsets[slot],0,sizeof(offsets[slot]));std::strncpy(offsets[slot].name,name,47);}
    WheelOffset& o=offsets[slot];o.x=s.activeWheelCentre.x;o.y=s.activeWheelCentre.y;o.z=s.activeWheelCentre.z;o.yaw=s.activeWheelYaw;o.pitch=s.activeWheelPitch;o.radius=s.activeWheelRadius;o.hasYoke=yoke;
    if(yoke){o.yokeX=s.activeYokeAnchor.x;o.yokeY=s.activeYokeAnchor.y;o.yokeZ=s.activeYokeAnchor.z;}
    const std::string path=WheelOffsetPath();FILE* file=path.empty()?NULL:std::fopen(path.c_str(),"wb");if(!file)return;
    for(int i=0;i<offsetCount;++i){const WheelOffset& w=offsets[i];if(w.hasYoke)std::fprintf(file,"%s=%.5f,%.5f,%.5f,%.5f,%.5f,%.5f;%.5f,%.5f,%.5f\n",w.name,w.x,w.y,w.z,w.yaw,w.pitch,w.radius,w.yokeX,w.yokeY,w.yokeZ);else std::fprintf(file,"%s=%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n",w.name,w.x,w.y,w.z,w.yaw,w.pitch,w.radius);}std::fclose(file);
}

float Unwrap(float value)
{
    while(value>PI)value-=2.0f*PI;
    while(value<-PI)value+=2.0f*PI;
    return value;
}

void ToWheelLocal(const SharedVrState& s,const rmt::Vector& point,
                  float& x,float& y,float& z)
{
    const float dx=point.x-s.activeWheelCentre.x;
    const float dy=point.y-s.activeWheelCentre.y;
    const float dz=point.z-s.activeWheelCentre.z;
    const float cy=std::cos(s.activeWheelYaw),sy=std::sin(s.activeWheelYaw);
    const float cp=std::cos(s.activeWheelPitch),sp=std::sin(s.activeWheelPitch);
    const float x1=cy*dx-sy*dz,z1=sy*dx+cy*dz;
    x=x1;y=cp*dy+sp*z1;z=-sp*dy+cp*z1;
}

rmt::Vector WheelPoint(const SharedVrState& s,float x,float y,float z)
{
    const float cp=std::cos(s.activeWheelPitch),sp=std::sin(s.activeWheelPitch);
    const float cy=std::cos(s.activeWheelYaw),sy=std::sin(s.activeWheelYaw);
    const float y1=cp*y-sp*z,z1=sp*y+cp*z;
    return rmt::Vector(s.activeWheelCentre.x+cy*x+sy*z1,
                       s.activeWheelCentre.y+y1,
                       s.activeWheelCentre.z-sy*x+cy*z1);
}

void RotateAroundAxis(float delta,const rmt::Matrix& input,rmt::Matrix& output)
{
    const float sn=std::sin(delta),cs=std::cos(delta);output.Identity();
    for(int i=0;i<3;++i){const rmt::Vector& row=input.Row(i);output.Row(i).Set(
        cs*row.x-sn*row.y,sn*row.x+cs*row.y,row.z);}
}
}

bool IsVrYokeVehicle(const char* name)
{
    return name&&std::strcmp(name,"honor_v")==0;
}

void UpdateVrInCarCharacterVisibility()
{
    CharacterManager* characters=GetCharacterManager();
    Character* player=characters?characters->GetCharacter(0):NULL;
    if(!player)return;

    static Character* hiddenPlayer=NULL;
    SharedVrState& state=GetSharedVrState();
    const bool hide=state.vrModeEnabled&&player->IsInCar()&&
                    state.vehicleControlMode!=2;
    if(hide)
    {
        if(player->IsVisible())
        {
            player->RemoveFromWorldScene();
            hiddenPlayer=player;
        }

        Vehicle* vehicle=player->GetTargetVehicle();
        Character* driver=vehicle?vehicle->GetDriver():NULL;
        if(driver&&driver!=player)
        {
            const bool traffic=TrafficManager::GetInstance()->
                IsVehicleTrafficVehicle(vehicle);
            if(traffic)
            {
                if(driver->IsVisible())driver->RemoveFromWorldScene();
            }
            else
            {
                if(!driver->IsVisible())driver->AddToWorldScene();
                rmt::Vector passenger=vehicle->GetPassengerLocation();
                passenger.y=driver->GetPuppet()->GetPosition().y;
                driver->GetPuppet()->SetPosition(passenger);
            }
        }
        return;
    }

    // GetOut normally restores the drawable itself. This branch covers a VR
    // mode/camera change while the character remains seated.
    if(hiddenPlayer==player)
    {
        if(!player->IsVisible()&&player->IsInCar())
        {
            Vehicle* vehicle=player->GetTargetVehicle();
            if(!vehicle||vehicle->mVisibleCharacters)player->AddToWorldScene();
        }
        hiddenPlayer=NULL;
    }
}

void UpdateTrackedVrVehicle(bool originValid,const XrPosef& origin,
    const XrPosef handPoses[2],const bool handValid[2],
    VrVehicleHapticSink haptic,void* context)
{
    CharacterManager* characters=GetCharacterManager();
    Character* player=characters?characters->GetCharacter(0):NULL;
    Vehicle* vehicle=(player&&player->IsInCar())?player->GetTargetVehicle():NULL;
    SharedVrState& s=GetSharedVrState();VrVehicleInput input={};
    input.active=s.vrModeEnabled&&s.vehicleControlMode==1&&vehicle;
    input.vehicleName=vehicle?vehicle->GetName():NULL;
    input.yoke=IsVrYokeVehicle(input.vehicleName);
    for(unsigned hand=0;hand<2;++hand)
    {
        input.grip[hand]=s.gripValue[hand];input.stickClick[hand]=s.stickClick[hand];
        input.handValid[hand]=originValid&&handValid&&handValid[hand];
        if(input.handValid[hand])input.handPose[hand]=SharedRender::PoseToGame(
            SharedRender::RelativePose(origin,handPoses[hand]));
    }
    UpdateVrVehicleState(input,haptic,context);
    UpdateVrInCarCharacterVisibility();
}

void GetVrVehicleInputOverrides(bool yoke,float rawLeftTrigger,
    float rawRightTrigger,float leftGrip,float rightGrip,bool leftStickClick,
    bool rightStickClick,float* leftTrigger,float* rightTrigger,
    bool* leftGripButton,bool* rightGripButton,bool* leftThumbButton,
    bool* rightThumbButton)
{
    SharedVrState& s=GetSharedVrState();const bool wheel=s.vrModeEnabled&&s.vehicleControlMode==1;
    *leftTrigger=rawLeftTrigger;*rightTrigger=rawRightTrigger;
    if(wheel&&yoke&&(s.wheelGrabbed[0]||s.wheelGrabbed[1]))
    {*rightTrigger=std::max(0.0f,s.yokeThrottle);*leftTrigger=std::max(0.0f,-s.yokeThrottle);}
    *leftGripButton=wheel?false:leftGrip>GRAB;*rightGripButton=wheel?false:rightGrip>GRAB;
    *leftThumbButton=leftStickClick||(wheel&&s.wheelHonk);
    *rightThumbButton=rightStickClick;
}

void ResetVrVehicleState()
{
    SharedVrState& s=GetSharedVrState();
    s.wheelGrabbed[0]=s.wheelGrabbed[1]=false;s.wheelHonk=false;
    s.wheelAngle=s.wheelVisualAngle=s.wheelTrim=s.yokeThrottle=0.0f;
    s.yokeFullGasLatched=s.yokeFullBrakeLatched=false;
    s.wheelAdjustMode=false;s.wheelAdjustHoldSec=0.0f;s.wheelAdjustVehicle[0]=0;
}

void UpdateVrVehicleState(const VrVehicleInput& in,VrVehicleHapticSink haptic,
                          void* context)
{
    SharedVrState& s=GetSharedVrState();
    if(!in.active){ResetVrVehicleState();return;}
    ApplyOffset(in.vehicleName,in.yoke);
    s.gripValue[0]=in.grip[0];s.gripValue[1]=in.grip[1];
    s.stickClick[0]=in.stickClick[0];s.stickClick[1]=in.stickClick[1];
    s.wheelHonk=false;

    const bool bothClicks=in.stickClick[0]&&in.stickClick[1];
    const float dt=in.deltaSeconds>0.0f&&in.deltaSeconds<0.1f?in.deltaSeconds:1.0f/72.0f;
    if(bothClicks)
    {
        s.wheelAdjustHoldSec+=dt;
        if(!s.wheelAdjustMode&&s.wheelAdjustHoldSec>=3.0f)
        {
            s.wheelAdjustMode=true;s.wheelMeshHidden=false;
            if(in.vehicleName){std::strncpy(s.wheelAdjustVehicle,in.vehicleName,sizeof(s.wheelAdjustVehicle)-1);s.wheelAdjustVehicle[sizeof(s.wheelAdjustVehicle)-1]=0;}
            if(haptic){haptic(context,0,0.9f,120);haptic(context,1,0.9f,120);}
        }
    }
    else if(s.wheelAdjustMode)
    {
        StoreOffset(s.wheelAdjustVehicle[0]?s.wheelAdjustVehicle:in.vehicleName,in.yoke);
        s.wheelAdjustMode=false;s.wheelAdjustHoldSec=0.0f;s.wheelMeshHidden=true;
        s.wheelGrabbed[0]=s.wheelGrabbed[1]=false;s.wheelAngle=0.0f;
        if(haptic){haptic(context,0,0.6f,80);haptic(context,1,0.6f,80);}
    }
    else s.wheelAdjustHoldSec=0.0f;

    if(s.wheelAdjustMode&&in.handValid[0]&&in.handValid[1])
    {
        const rmt::Vector p0=in.handPose[0].Row(3),p1=in.handPose[1].Row(3);
        const rmt::Vector mid=(p0+p1)*0.5f; rmt::Vector across=p1-p0;
        const float distance=across.Magnitude();if(distance>0.02f)across.NormalizeSafe();else across.Set(1,0,0);
        const float targetYaw=std::atan2(-across.z,across.x);
        const float targetRadius=std::max(0.10f,std::min(0.30f,distance*0.5f));
        rmt::Vector forward[2];for(unsigned h=0;h<2;++h)in.handPose[h].RotateVector(rmt::Vector(0,0,-1),&forward[h]);
        const auto pitch=[](const rmt::Vector& f){return std::atan2(-f.y,std::max(0.00001f,std::sqrt(f.x*f.x+f.z*f.z)));};
        const float targetPitch=std::max(-1.15f,std::min(1.15f,(pitch(forward[0])+pitch(forward[1]))*0.5f));
        s.activeWheelCentre+=(mid-s.activeWheelCentre)*0.5f;
        s.activeYokeAnchor.Set(s.activeWheelCentre.x,s.activeWheelCentre.y-YOKE_ARM+0.01f,s.activeWheelCentre.z-0.12f);
        s.activeWheelYaw+=Unwrap(targetYaw-s.activeWheelYaw)*0.5f;
        s.activeWheelPitch+=(targetPitch-s.activeWheelPitch)*0.5f;
        s.activeWheelRadius+=(targetRadius-s.activeWheelRadius)*0.5f;
        s.wheelGrabbed[0]=s.wheelGrabbed[1]=true;
        for(unsigned h=0;h<2;++h){s.wheelGrabOffset[h]=h?OPTIMAL:-OPTIMAL;s.wheelGrabOrientRot[h]=in.handPose[h];s.wheelGrabOrientRot[h].Row(3).Set(0,0,0);}
        return;
    }

    if(in.yoke)
    {
        const rmt::Vector centre(s.activeYokeAnchor.x,
            s.activeYokeAnchor.y+YOKE_ARM,s.activeYokeAnchor.z);
        float handY[2]={0,0},handZ[2]={0,0};bool held[2]={false,false};
        for(unsigned hand=0;hand<2;++hand)
        {
            if(!in.handValid[hand]){s.wheelGrabbed[hand]=false;continue;}
            const rmt::Vector p=in.handPose[hand].Row(3);const rmt::Vector d=p-centre;
            const bool close=std::sqrt(d.x*d.x+d.y*d.y)<s.activeWheelRadius+0.24f&&
                             std::fabs(d.z)<0.28f&&std::fabs(d.y)<0.28f;
            if(!s.wheelGrabbed[hand])
            {
                if(in.grip[hand]>GRAB&&close){s.wheelGrabbed[hand]=true;
                    s.wheelGrabOffset[hand]=p.y;s.wheelGrabOrientRot[hand]=in.handPose[hand];
                    s.wheelGrabOrientRot[hand].Row(3).Set(0,0,0);}
            }
            else if(in.grip[hand]<=RELEASE)s.wheelGrabbed[hand]=false;
            if(s.wheelGrabbed[hand]){held[hand]=true;handY[hand]=p.y-s.wheelGrabOffset[hand];handZ[hand]=d.z;}
        }
        if(held[0]||held[1])
        {
            const float steering=std::max(-1.0f,std::min(1.0f,
                ((held[0]?-handY[0]:0.0f)-(held[1]?-handY[1]:0.0f))/0.11f));
            s.wheelAngle+=(steering*MAX_ANGLE-s.wheelAngle)*0.80f;
            float z=0;int count=0;for(unsigned h=0;h<2;++h)if(held[h]){z+=handZ[h];++count;}
            const float throttle=std::max(-1.0f,std::min(1.0f,(z/count)/0.035f));
            s.yokeThrottle+=(throttle-s.yokeThrottle)*0.75f;
            if(s.yokeThrottle>=0.95f&&!s.yokeFullGasLatched&&haptic)
                for(unsigned h=0;h<2;++h)if(held[h])haptic(context,h,0.85f,70);
            if(s.yokeThrottle<=-0.95f&&!s.yokeFullBrakeLatched&&haptic)
                for(unsigned h=0;h<2;++h)if(held[h])haptic(context,h,0.90f,90);
            s.yokeFullGasLatched=s.yokeThrottle>=0.95f;
            s.yokeFullBrakeLatched=s.yokeThrottle<=-0.95f;
        }
        else{s.wheelAngle*=0.90f;s.yokeThrottle*=0.85f;
            s.yokeFullGasLatched=s.yokeFullBrakeLatched=false;}
    }
    else
    {
        float sum=0;int count=0;
        for(unsigned hand=0;hand<2;++hand)
        {
            if(!in.handValid[hand]){if(s.wheelGrabbed[hand]){sum+=s.wheelGrabTarget[hand];++count;s.wheelGrabAngle[hand]=1e10f;}continue;}
            const rmt::Vector p=in.handPose[hand].Row(3);float x,y,z;ToWheelLocal(s,p,x,y,z);
            const float radial=std::sqrt(x*x+y*y),angle=std::atan2(x,y);
            if(!s.wheelGrabbed[hand]&&radial<0.10f&&std::fabs(z)<0.16f)s.wheelHonk=true;
            const bool squeezed=in.grip[hand]>(s.wheelGrabbed[hand]?RELEASE:GRAB);
            if(!s.wheelGrabbed[hand])
            {
                if(squeezed&&std::fabs(radial-s.activeWheelRadius)<0.12f&&
                   std::fabs(z)<0.16f&&radial>0.06f){s.wheelGrabbed[hand]=true;
                    s.wheelGrabOffset[hand]=hand?OPTIMAL:-OPTIMAL;
                    s.wheelGrabTarget[hand]=s.wheelAngle;s.wheelGrabAngle[hand]=angle;
                    s.wheelGrabOrientAngle[hand]=s.wheelAngle+s.wheelGrabOffset[hand];
                    s.wheelGrabOrientRot[hand]=in.handPose[hand];s.wheelGrabOrientRot[hand].Row(3).Set(0,0,0);}
                continue;
            }
            if(!squeezed){s.wheelGrabbed[hand]=false;continue;}
            if(s.wheelGrabAngle[hand]>1e9f){s.wheelGrabAngle[hand]=angle;s.wheelGrabTarget[hand]=s.wheelAngle;}
            float delta=std::max(-2.5f,std::min(2.5f,Unwrap(angle-s.wheelGrabAngle[hand])));
            sum+=std::max(-MAX_ANGLE,std::min(MAX_ANGLE,s.wheelGrabTarget[hand]+delta));++count;
        }
        if(count){const float target=sum/count;float follow=target*s.wheelAngle<0?0.98f:0.92f;
            s.wheelAngle+=((target-s.wheelAngle)*follow);s.wheelAngle=std::max(-MAX_ANGLE,std::min(MAX_ANGLE,s.wheelAngle));
            if(std::fabs(s.wheelAngle)<0.18f)s.wheelTrim+=(s.wheelAngle-s.wheelTrim)*0.008f;
            s.wheelTrim=std::max(-0.10f,std::min(0.10f,s.wheelTrim));}
        else if(!s.wheelGrabbed[0]&&!s.wheelGrabbed[1]){s.wheelAngle*=0.68f;s.wheelTrim=0;}
    }
    if(std::fabs(s.wheelAngle)<0.01f)s.wheelAngle=0;
    s.wheelVisualAngle+=(s.wheelAngle-s.wheelVisualAngle)*0.55f;
    if(std::fabs(s.wheelVisualAngle)<0.002f)s.wheelVisualAngle=0;
}

bool GetVrVehicleSteering(float* value,bool yoke)
{
    SharedVrState& s=GetSharedVrState();if(!value||!s.vrModeEnabled||s.vehicleControlMode!=1)return false;
    float t=(s.wheelAngle-(yoke?0.0f:s.wheelTrim))/MAX_ANGLE;
    const float sign=t<0?-1.0f:1.0f;t=sign*std::pow(std::fabs(t),1.15f);
    if(std::fabs(t)<0.02f)t=0;else t=sign*(std::fabs(t)-0.02f)/0.98f;
    if(yoke)t=-t;*value=std::max(-1.0f,std::min(1.0f,t));return true;
}

bool GetVrVehicleHandPose(unsigned hand,bool yoke,rmt::Matrix* pose)
{
    SharedVrState& s=GetSharedVrState();if(!pose||hand>1||!s.wheelGrabbed[hand])return false;
    if(yoke)
    {
        const float side=hand?1.0f:-1.0f;const float roll=s.wheelVisualAngle/MAX_ANGLE*0.55f;
        const float pitch=s.yokeThrottle*0.40f,cr=std::cos(roll),sr=std::sin(roll),cp=std::cos(pitch),sp=std::sin(pitch);
        const float x=side*0.16f,y=YOKE_ARM;*pose=s.wheelGrabOrientRot[hand];
        pose->Row(3).Set(s.activeYokeAnchor.x+x*cr-y*cp*sr,
                         s.activeYokeAnchor.y+x*sr+y*cp*cr,
                         s.activeYokeAnchor.z+y*sp);return true;
    }
    const float angle=s.wheelVisualAngle+s.wheelGrabOffset[hand];
    RotateAroundAxis(Unwrap(s.wheelGrabOrientAngle[hand]-angle),s.wheelGrabOrientRot[hand],*pose);
    pose->Row(3)=WheelPoint(s,s.activeWheelRadius*std::sin(angle),s.activeWheelRadius*std::cos(angle),0);
    return true;
}

void RenderVrVehicleControls(const rmt::Matrix& cameraBase,bool yoke,
                             const rmt::Matrix handPose[2],const bool handValid[2])
{
    SharedVrState& s=GetSharedVrState();
    static tShader* shader=NULL;
    if(!shader){shader=new tShader("simple");shader->AddRef();
        shader->SetInt(PDDI_SP_ISLIT,1);shader->SetInt(PDDI_SP_SHADEMODE,PDDI_SHADE_GOURAUD);
        shader->SetInt(PDDI_SP_BLENDMODE,PDDI_BLEND_NONE);shader->SetInt(PDDI_SP_TWOSIDED,1);
        shader->SetColour(PDDI_SP_AMBIENT,tColour(38,38,42));shader->SetColour(PDDI_SP_DIFFUSE,tColour(115,115,125));}
    const auto world=[&](const rmt::Vector& local){rmt::Vector out;cameraBase.Transform(local,&out);return out;};
    const auto emit=[&](pddiPrimStream* stream,const rmt::Vector& p){rmt::Vector n;cameraBase.RotateVector(rmt::Vector(0,0,1),&n);n.NormalizeSafe();const rmt::Vector w=world(p);stream->Normal(n.x,n.y,n.z);stream->Coord(w.x,w.y,w.z);};
    if(yoke)
    {
        const float roll=s.wheelVisualAngle/MAX_ANGLE*0.55f,pitch=s.yokeThrottle*0.40f;
        const float cr=std::cos(roll),sr=std::sin(roll),cp=std::cos(pitch),sp=std::sin(pitch);
        const auto point=[&](float x,float y){return rmt::Vector(s.activeYokeAnchor.x+x*cr-y*cp*sr,s.activeYokeAnchor.y+x*sr+y*cp*cr,s.activeYokeAnchor.z+y*sp);};
        pddiPrimStream* stream=p3d::pddi->BeginPrims(shader->GetShader(),PDDI_PRIM_TRIANGLES,PDDI_V_N,12);
        if(stream){const float t=0.014f;const rmt::Vector a=point(-0.16f,YOKE_ARM),b=point(0.16f,YOKE_ARM),c=point(0,0),d=point(0,YOKE_ARM);
            rmt::Vector q[12]={a+rmt::Vector(0,t,0),a-rmt::Vector(0,t,0),b-rmt::Vector(0,t,0),a+rmt::Vector(0,t,0),b-rmt::Vector(0,t,0),b+rmt::Vector(0,t,0),c+rmt::Vector(t,0,0),c-rmt::Vector(t,0,0),d-rmt::Vector(t,0,0),c+rmt::Vector(t,0,0),d-rmt::Vector(t,0,0),d+rmt::Vector(t,0,0)};
            for(unsigned i=0;i<12;++i)emit(stream,q[i]);p3d::pddi->EndPrims(stream);}
    }
    else
    {
        const int segments=32;pddiPrimStream* stream=p3d::pddi->BeginPrims(shader->GetShader(),PDDI_PRIM_TRIANGLES,PDDI_V_N,segments*6);
        if(stream){const float half=0.012f;for(int i=0;i<segments;++i){const float a0=2*PI*i/segments+s.wheelVisualAngle,a1=2*PI*(i+1)/segments+s.wheelVisualAngle;
            const rmt::Vector p0=WheelPoint(s,(s.activeWheelRadius-half)*std::sin(a0),(s.activeWheelRadius-half)*std::cos(a0),0);
            const rmt::Vector p1=WheelPoint(s,(s.activeWheelRadius+half)*std::sin(a0),(s.activeWheelRadius+half)*std::cos(a0),0);
            const rmt::Vector p2=WheelPoint(s,(s.activeWheelRadius+half)*std::sin(a1),(s.activeWheelRadius+half)*std::cos(a1),0);
            const rmt::Vector p3=WheelPoint(s,(s.activeWheelRadius-half)*std::sin(a1),(s.activeWheelRadius-half)*std::cos(a1),0);
            emit(stream,p0);emit(stream,p1);emit(stream,p2);emit(stream,p0);emit(stream,p2);emit(stream,p3);}p3d::pddi->EndPrims(stream);}
    }
    rmt::Matrix worldHands[2];bool valid[2]={false,false};
    for(unsigned hand=0;hand<2;++hand){rmt::Matrix local;if(GetVrVehicleHandPose(hand,yoke,&local)||(handValid[hand]&&(local=handPose[hand],true))){worldHands[hand].Mult(local,cameraBase);valid[hand]=true;}}
    RenderTrackedHandMeshes(worldHands,valid);
}
}

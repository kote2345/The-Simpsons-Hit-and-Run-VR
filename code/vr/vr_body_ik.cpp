#include <vr/vr_body_ik.h>
#if defined(SHAR_VR_BODY_IK) && defined(SRR2_OPENXR)
#include <vr/vr_body_ik_math.h>
#include <vr/openxrmanager.h>
#include <vr/openxr_shared_state.h>
#include <vr/openxr_shared_vehicle.h>
#include <worldsim/character/character.h>
#include <worldsim/character/charactermanager.h>
#include <worldsim/character/charactercontroller.h>
#include <ai/state.h>
#include <ai/statemanager.h>
#include <camera/supercam.h>
#include <camera/supercamcentral.h>
#include <camera/supercammanager.h>
#include <worldsim/redbrick/vehicle.h>
#include <worldsim/traffic/trafficmanager.h>
#include <p3d/anim/pose.hpp>
#include <p3d/anim/skeleton.hpp>
#include <SDL.h>
#include <cstdio>
#include <limits>

namespace SharOpenXR
{
namespace
{
using namespace SharBodyIK;
bool drawn=false;
void ReportRig(Character* player,unsigned reason,const char* text)
{
    // Report once per character/model identity and status, not once per eye.
    static tUID lastUID=0;
    static unsigned lastReason=~0u;
    if(lastUID!=player->GetUID()||lastReason!=reason)
    {
        SDL_Log("VR Body IK: %s: %s",player->GetName(),text);
        lastUID=player->GetUID();lastReason=reason;
    }
}
// Reused only for the local character. No cached Character or skeleton pointer:
// SetSkeleton owns its reference; mapping is rebuilt for every rendered pose.
struct ScratchPose
{
    tPose* pose;
    ScratchPose():pose(NULL){}
    ~ScratchPose(){if(pose)pose->Release();}
    tPose* Copy(tPose* source)
    {
        if(!pose||pose->GetNumJoint()!=source->GetNumJoint())
        {
            if(pose)pose->Release();
            pose=new tPose(source->GetSkeleton());pose->AddRef();
        }
        pose->SetSkeleton(source->GetSkeleton());
        for(int i=0;i<pose->GetNumJoint();++i)
        {
            pose->GetJoint(i)->uid=source->GetJoint(i)->uid;
            const int parent=source->GetSkeleton()->GetJoint(i)->parentIndex;
            pose->GetJoint(i)->parent=(parent>=0&&parent<pose->GetNumJoint())?pose->GetJoint(parent):NULL;
            pose->GetJoint(i)->worldMatrix=source->GetJoint(i)->worldMatrix;
            pose->GetJoint(i)->objectMatrix=source->GetJoint(i)->objectMatrix;
        }
        pose->SetPoseReady(true);return pose;
    }
} scratch;
struct Chain{int upper,middle,end;};
bool Descendant(tPose* p,int joint,int ancestor)
{
    for(int count=0;joint>=0&&joint<p->GetNumJoint()&&count<p->GetNumJoint();++count)
    {
        if(joint==ancestor)return true;
        joint=p->GetSkeleton()->GetJoint(joint)->parentIndex;
    }
    return false;
}
bool Resolve(tPose* p,const char* name,Chain& c)
{
    c.end=p->FindJointIndex(name);if(c.end<0)return false;
    c.middle=p->GetSkeleton()->GetJoint(c.end)->parentIndex;
    if(c.middle<=0||c.middle>=p->GetNumJoint())return false;
    c.upper=p->GetSkeleton()->GetJoint(c.middle)->parentIndex;
    return c.upper>0&&c.upper<p->GetNumJoint()&&c.upper!=c.middle&&c.middle!=c.end&&c.upper!=c.end;
}
rmt::Vector Position(tPose* p,int i){return p->GetJoint(i)->worldMatrix.Row(3);}
void MoveBranch(tPose* p,int root,const rmt::Matrix& rotation,const rmt::Vector& target)
{
    const rmt::Vector pivot=Position(p,root);
    for(int i=0;i<p->GetNumJoint();++i)if(Descendant(p,i,root))
    {
        rmt::Matrix old=p->GetJoint(i)->worldMatrix,result;
        const rmt::Vector relative=old.Row(3)-pivot;
        result.Mult(old,rotation);
        rmt::Vector moved;rotation.RotateVector(relative,&moved);
        result.Row(3)=target+moved;p->GetJoint(i)->worldMatrix=result;
    }
}
bool SolveChain(tPose* p,const Chain& c,const rmt::Vector& target,const rmt::Vector& pole,
                float maxStretch=1.0f)
{
    rmt::Vector a=Position(p,c.upper),b=Position(p,c.middle),e=Position(p,c.end),k,w;
    const rmt::Vector upperSegment=b-a,lowerSegment=e-b;
    const float upperLength=upperSegment.Magnitude(),lowerLength=lowerSegment.Magnitude();
    if(upperLength<1e-4f||lowerLength<1e-4f)return false;
    // Keep a small amount of elbow bend, but never leave the tracked wrist
    // beyond the chain's reachable sphere.  Arms may be substantially shorter
    // than the player's, so the arm call passes an unlimited stretch budget;
    // legs retain their original segment lengths (maxStretch == 1).
    const float requiredStretch=(target-a).Magnitude()/
        ((upperLength+lowerLength)*0.98f);
    const float stretch=maxStretch>1.0f?
        Clamp(requiredStretch,1.0f,maxStretch):Clamp(requiredStretch,1.0f,1.0f);
    if(stretch>1.0f)
    {
        // Lengthen joint spacing on this private pose without enlarging hands
        // or fingers. Move descendants with each joint to preserve attachment.
        rmt::Matrix translation;translation.Identity();
        MoveBranch(p,c.middle,translation,b+upperSegment*(stretch-1.0f));
        MoveBranch(p,c.end,translation,Position(p,c.end)+lowerSegment*(stretch-1.0f));
        b=Position(p,c.middle);e=Position(p,c.end);
    }
    if(!Solve(a,target,(b-a).Magnitude(),(e-b).Magnitude(),pole,k,w))return false;
    MoveBranch(p,c.upper,Rotation(b-a,k-a),a);
    b=Position(p,c.middle);e=Position(p,c.end);
    MoveBranch(p,c.middle,Rotation(e-b,w-k),k);
    return true;
}
// Explicit fist articulation, independent of locomotion animation amplitude.
// Rodrigues uses the same row-vector convention as the IK Rotation helper.
rmt::Matrix FingerHinge(rmt::Vector axis,float angle)
{
    axis=Unit(axis,rmt::Vector(1,0,0));
    const float c=std::cos(angle),sn=std::sin(angle);
    rmt::Matrix rotation;rotation.Identity();
    for(int row=0;row<3;++row)
    {
        const rmt::Vector basis=rotation.Row(row);
        rmt::Vector cross;cross.CrossProduct(axis,basis);
        rotation.Row(row)=basis*c+cross*sn+axis*(axis.DotProduct(basis)*(1.0f-c));
    }
    return rotation;
}
rmt::Vector RotateAxisAngle(const rmt::Vector& v,rmt::Vector axis,float angle)
{
    rmt::Vector out;
    const rmt::Matrix rotation=FingerHinge(axis,angle);
    rotation.RotateVector(v,&out);
    return out;
}
float ChooseThumbAcrossSign(const rmt::Vector& thumbDirection,
                            const rmt::Vector& targetDirection,
                            const rmt::Vector& palmNormal)
{
    // Pick whichever direction around the palm normal turns the thumb toward
    // the opposite side of the palm. This avoids assuming mirrored local axes.
    const float probe=20.0f*0.01745329252f;
    const rmt::Vector positiveDirection=Unit(
        RotateAxisAngle(thumbDirection,palmNormal, probe),thumbDirection);
    const rmt::Vector negativeDirection=Unit(
        RotateAxisAngle(thumbDirection,palmNormal,-probe),thumbDirection);
    const float positive=positiveDirection.DotProduct(targetDirection);
    const float negative=negativeDirection.DotProduct(targetDirection);
    return positive>=negative?1.0f:-1.0f;
}
int FindThumbRoot(tPose* pose,int wrist,unsigned hand,
                  const rmt::Vector& wristPosition,const rmt::Matrix& palm)
{
    tSkeleton* skeleton=pose->GetSkeleton();
    // Prefer a named direct child when this character rig exposes one.
    const char side=hand==0?'L':'R';
    const char* middle[] = {"Thumb","Thumb1","Thumb01","Thumb0","ThumbBase","Finger0"};
    for(unsigned n=0;n<sizeof(middle)/sizeof(middle[0]);++n)
    {
        char names[6][48];
        std::snprintf(names[0],sizeof(names[0]),"%s_%c",middle[n],side);
        std::snprintf(names[1],sizeof(names[1]),"%c_%s",side,middle[n]);
        std::snprintf(names[2],sizeof(names[2]),"%c%s",side,middle[n]);
        std::snprintf(names[3],sizeof(names[3]),"%s%c",middle[n],side);
        std::snprintf(names[4],sizeof(names[4]),"%s_01_%c",middle[n],side);
        std::snprintf(names[5],sizeof(names[5]),"%c_%s_01",side,middle[n]);
        for(int i=0;i<pose->GetNumJoint();++i)
        {
            if(skeleton->GetJoint(i)->parentIndex!=wrist)continue;
            const tUID uid=skeleton->GetJoint(i)->uid;
            for(unsigned k=0;k<6;++k)if(uid==tEntity::MakeUID(names[k]))return i;
        }
    }

    // Fallback for SHAR character variants with different digit names:
    // identify the direct wrist child whose root position AND first segment
    // are most lateral relative to the authored palm frame. Ordinary fingers
    // start forward and continue forward; the thumb is the lateral chain.
    int best=-1;
    float bestScore=-1000000.0f;
    for(int root=0;root<pose->GetNumJoint();++root)
    {
        if(skeleton->GetJoint(root)->parentIndex!=wrist)continue;
        int child=-1;
        for(int j=0;j<pose->GetNumJoint();++j)
            if(skeleton->GetJoint(j)->parentIndex==root){child=j;break;}
        if(child<0)continue;

        const rmt::Vector rootDir=Unit(skeleton->GetJoint(root)->worldMatrix.Row(3)-wristPosition,
                                      palm.Row(2));
        const rmt::Vector segmentDir=Unit(skeleton->GetJoint(child)->worldMatrix.Row(3)-
                                          skeleton->GetJoint(root)->worldMatrix.Row(3),palm.Row(2));
        const float rootAcross=std::fabs(rootDir.DotProduct(palm.Row(0)));
        const float rootForward=std::fabs(rootDir.DotProduct(palm.Row(2)));
        const float segmentAcross=std::fabs(segmentDir.DotProduct(palm.Row(0)));
        const float segmentForward=std::fabs(segmentDir.DotProduct(palm.Row(2)));
        const float score=rootAcross*2.0f+segmentAcross*3.0f-
                          rootForward*1.25f-segmentForward*1.75f;
        if(score>bestScore){bestScore=score;best=root;}
    }
    return best;
}
void ApplyGripFingers(tPose* p,int wrist,unsigned hand)
{
    const float rawGrip=GetHandGripValue(hand);
    float grip=std::isfinite(rawGrip)?rawGrip:0.0f;
    // Reach full closure at a comfortable 80% squeeze; no need to crush grip.
    grip=Clamp((grip-0.05f)/0.75f,0.0f,1.0f);
    grip=grip*grip*(3.0f-2.0f*grip);
    tSkeleton* skeleton=p->GetSkeleton();
    const int elbow=skeleton->GetJoint(wrist)->parentIndex;
    if(elbow<0)return;
    const rmt::Vector wristBind=skeleton->GetJoint(wrist)->worldMatrix.Row(3);
    const rmt::Matrix palm=NeutralGripFrame(wristBind-skeleton->GetJoint(elbow)->worldMatrix.Row(3),
                                          skeleton->GetJoint(0)->worldMatrix.Row(1));
    const int thumbRoot=FindThumbRoot(p,wrist,hand,wristBind,palm);

    // Map the authored palm frame through the ACTUAL tracked wrist rotation.
    // Thumb opposition below is therefore around the current palm normal,
    // not a guessed local bone axis. With the palm facing up this axis points
    // vertically, so rotating around it can only move the thumb LEFT/RIGHT
    // across the palm -- it cannot fold downward with the fingers.
    rmt::Matrix bindWrist=skeleton->GetJoint(wrist)->worldMatrix;
    rmt::Matrix currentWrist=p->GetJoint(wrist)->worldMatrix;
    for(int row=0;row<3;++row)
    {
        bindWrist.Row(row)=Unit(bindWrist.Row(row),palm.Row(row));
        currentWrist.Row(row)=Unit(currentWrist.Row(row),bindWrist.Row(row));
    }
    bindWrist.Row(3).Set(0,0,0);currentWrist.Row(3).Set(0,0,0);
    bindWrist.InvertOrtho();
    rmt::Matrix wristDelta;wristDelta.Mult(bindWrist,currentWrist);
    rmt::Vector currentPalmNormal;
    wristDelta.RotateVector(palm.Row(1),&currentPalmNormal);
    currentPalmNormal=Unit(currentPalmNormal,rmt::Vector(0,1,0));

    float thumbAcrossSign=1.0f;
    if(thumbRoot>=0)
    {
        const rmt::Vector rootOffset=skeleton->GetJoint(thumbRoot)->worldMatrix.Row(3)-wristBind;
        const float rootSide=rootOffset.DotProduct(palm.Row(0));
        const float inwardSide=rootSide>=0.0f?-1.0f:1.0f;
        rmt::Vector thumbDirection=rootOffset;
        for(int child=0;child<p->GetNumJoint();++child)
            if(skeleton->GetJoint(child)->parentIndex==thumbRoot)
            {
                thumbDirection=skeleton->GetJoint(child)->worldMatrix.Row(3)-
                               skeleton->GetJoint(thumbRoot)->worldMatrix.Row(3);break;
            }
        thumbDirection=Unit(thumbDirection,palm.Row(2));
        const rmt::Vector targetDirection=Unit(palm.Row(0)*inwardSide+palm.Row(2)*0.15f,palm.Row(2));
        thumbAcrossSign=ChooseThumbAcrossSign(thumbDirection,targetDirection,palm.Row(1));
    }
    int fingerJoints=0,thumbJoints=0;
    for(int i=0;i<p->GetNumJoint();++i)
    {
        if(i==wrist||!Descendant(p,i,wrist))continue;
        const tSkeleton::Joint* joint=skeleton->GetJoint(i);
        if(joint->parentIndex<0||joint->parentIndex>=i)continue;
        int root=i,depth=0;
        while(skeleton->GetJoint(root)->parentIndex!=wrist&&depth<p->GetNumJoint())
        {
            root=skeleton->GetJoint(root)->parentIndex;++depth;
            if(root<0)break;
        }
        if(root<0||depth>=p->GetNumJoint())continue;
        const bool thumb=(root==thumbRoot);
        ++fingerJoints;if(thumb)++thumbJoints;
        const rmt::Matrix& rest=joint->restPose;
        rmt::Matrix local=rest;
        if(grip>0.0f)
        {
            if(thumb)
            {
                // PURE SIDEWAYS THUMB OPPOSITION. No flex/curl axis is used.
                // Convert the CURRENT palm normal to this CURRENT parent frame
                // and rotate only around that axis. Palm-up: left thumb sweeps
                // right across the palm; right thumb sweeps left across it.
                rmt::Matrix inverseParent=p->GetJoint(joint->parentIndex)->worldMatrix;
                for(int row=0;row<3;++row)
                    inverseParent.Row(row)=Unit(inverseParent.Row(row),currentWrist.Row(row));
                inverseParent.Row(3).Set(0,0,0);inverseParent.InvertOrtho();
                rmt::Vector parentAxis;inverseParent.RotateVector(currentPalmNormal,&parentAxis);
                const float acrossDegrees=depth==0?92.0f:(depth==1?26.0f:(depth==2?10.0f:0.0f));
                const rmt::Matrix across=FingerHinge(parentAxis,
                    acrossDegrees*0.01745329252f*grip*thumbAcrossSign);
                local.Mult(rest,across);
            }
            else
            {
                // Main fingers retain their existing fist curl.
                rmt::Matrix inverseParent=skeleton->GetJoint(joint->parentIndex)->worldMatrix;
                for(int row=0;row<3;++row)
                    inverseParent.Row(row)=Unit(inverseParent.Row(row),palm.Row(row));
                inverseParent.Row(3).Set(0,0,0);inverseParent.InvertOrtho();
                rmt::Vector parentAxis;inverseParent.RotateVector(palm.Row(0),&parentAxis);
                const float degrees=depth==0?95.0f:(depth==1?85.0f:(depth==2?55.0f:0.0f));
                const rmt::Matrix bend=FingerHinge(parentAxis,degrees*0.01745329252f*grip);
                local.Mult(rest,bend);
            }
            local.Row(3)=rest.Row(3);
        }
        p->GetJoint(i)->objectMatrix=local;
        p->GetJoint(i)->worldMatrix.Mult(local,p->GetJoint(joint->parentIndex)->worldMatrix);
    }
    static int lastJoints[2]={-1,-1},lastGripBucket[2]={-1,-1};
    const int bucket=grip<0.1f?0:(grip>0.9f?2:1);
    if(lastJoints[hand]!=fingerJoints||lastGripBucket[hand]!=bucket)
    {
        SDL_Log("VR Body IK fingers [GRIP_POSE_V2 FIST_V8_PURE_OPPOSITION]: hand=%u joints=%d thumbRoot=%d thumbJoints=%d grip=%.3f closure=%.3f",
                hand,fingerJoints,thumbRoot,thumbJoints,rawGrip,grip);
        lastJoints[hand]=fingerJoints;lastGripBucket[hand]=bucket;
    }
}
void MatchRotation(tPose* p,int joint,const rmt::Matrix& requested)
{
    rmt::Matrix current=p->GetJoint(joint)->worldMatrix;
    // Strip scale before inversion; retain authored model scale in the branch.
    for(int i=0;i<3;++i)current.Row(i)=Unit(current.Row(i),requested.Row(i));
    current.Row(3).Set(0,0,0);current.InvertOrtho();
    rmt::Matrix target=requested;target.Row(3).Set(0,0,0);
    rmt::Matrix delta;delta.Mult(current,target);
    MoveBranch(p,joint,delta,Position(p,joint));
}
}
void BeginBodyIKEye(){drawn=false;}
bool WasBodyIKDrawn(){return drawn;}
void MarkBodyIKDrawn(){drawn=true;}
tPose* BuildBodyIKPose(Character* player,tPose* animated,const rmt::Vector& origin)
{
    if(!IsVrModeEnabled()||!IsBodyIKEnabled()||!player||!GetCharacterManager()||
       player!=GetCharacterManager()->GetCharacter(0)||
       !player->GetController()||!player->GetController()->IsActive()||
       !player->GetStateManager()||
       (player->GetStateManager()->GetState()!=CharacterAi::LOCO &&
        player->GetStateManager()->GetState()!=CharacterAi::INCAR)||
       !animated||!animated->GetSkeleton()||!GetSuperCamManager())return NULL;
    SuperCamCentral* central=GetSuperCamManager()->GetSCC(0);
    SuperCam* camera=central?central->GetActiveSuperCam():NULL;
    // Traffic vehicles may use a relative/animated first-person camera even
    // though the VR camera has already supplied the final seated transform.
    // Those camera types must not disable the vehicle body pose; retain the
    // original rejection for on-foot cinematic cameras.
    if(!camera||(!player->IsInCar()&&
       (camera->GetType()==SuperCam::ANIMATED_CAM||
        camera->GetType()==SuperCam::RELATIVE_ANIMATED_CAM||
        camera->GetType()==SuperCam::CONVERSATION_CAM)))return NULL;
    rmt::Matrix head,base,hands[2],local;
    if(!GetActiveCullingCamera(&head)||!GetGameplayCamera(&base))
    {ReportRig(player,10,"fallback: VR camera pose unavailable");return NULL;}
    // Solve seated IK in the vehicle's local frame. The puppet is parented to
    // this same transform, while OpenXR poses arrive in world camera space.
    const bool inCar=player->IsInCar()&&player->GetTargetVehicle();
    if(inCar)
    {
        static Vehicle* loggedVehicle=NULL;
        static int loggedAnchor=-1;
        Vehicle* vehicle=player->GetTargetVehicle();
        const SharedVrState& state=GetSharedVrState();
        const int anchor=state.vehicleBodyAnchorValid?1:0;
        if(loggedVehicle!=vehicle||loggedAnchor!=anchor)
        {
            const rmt::Vector driver=vehicle->GetDriverLocation();
            const rmt::Vector passenger=vehicle->GetPassengerLocation();
            SDL_Log("VR BODY IK input vehicle=%p traffic=%d state=%d control=%d anchor=%d driverLocal=(%.3f %.3f %.3f) passengerLocal=(%.3f %.3f %.3f)",
                vehicle,TrafficManager::GetInstance()->IsVehicleTrafficVehicle(vehicle)?1:0,
                player->GetStateManager()->GetState(),state.vehicleControlMode,anchor,
                driver.x,driver.y,driver.z,passenger.x,passenger.y,passenger.z);
            loggedVehicle=vehicle;loggedAnchor=anchor;
        }
    }
    rmt::Matrix vehicleWorld,vehicleToWorld,worldToVehicle;
    vehicleToWorld.Identity();worldToVehicle.Identity();
    rmt::Vector targetOrigin=origin,anchorOffsetWorld(0,0,0);
    rmt::Matrix trackingBase=base;
    if(inCar)
    {
        const SharOpenXR::SharedVrState& vrState=SharOpenXR::GetSharedVrState();
        if(vrState.vehicleBodyAnchorValid)
            trackingBase=vrState.vehicleBodyCameraWorld;
        // GetActiveCullingCamera is composed from the pre-anchor gameplay
        // base. Rebuild its tracked HMD pose on top of the exact seated VR
        // camera frame so head and hand targets use the driver's socket.
        rmt::Matrix inverseBase=base;
        inverseBase.InvertOrtho();
        rmt::Matrix trackedHead;trackedHead.Mult(head,inverseBase);
        head.Mult(trackedHead,trackingBase);
    }
    if(inCar)
    {
        vehicleWorld=player->GetParentTransform();
        vehicleToWorld=vehicleWorld;
        vehicleToWorld.Row(3).Set(0,0,0);
        worldToVehicle=vehicleToWorld;
        worldToVehicle.InvertOrtho();
        if(!SharOpenXR::GetSharedVrState().vehicleBodyAnchorValid)
        {
            // The camera anchor may not have been published yet on the first
            // render pass. Build the same deterministic driver's eye frame as
            // FirstPersonCam instead of falling back to the passenger base.
            rmt::Vector driverEye=player->GetTargetVehicle()->GetDriverLocation();
            driverEye.y+=0.96f;
            driverEye.Transform(vehicleWorld);
            trackingBase=vehicleWorld;
            trackingBase.Row(3)=driverEye;
        }
        // Use the already stabilized VR gameplay-camera anchor rather than
        // Character's original in-car seat. Traffic vehicles intentionally
        // keep the player puppet on the passenger socket, while the VR
        // camera/wheel are on the driver's side. Convert the camera eye point
        // to vehicle-local space and remove the same eye height used by the
        // VR camera, yielding the body root at the driver's seat.
        const float vrDriverEyeHeight=0.96f;
        rmt::Vector cameraAnchorWorld=trackingBase.Row(3);
        const SharOpenXR::SharedVrState& vrState=SharOpenXR::GetSharedVrState();
        if(vrState.vehicleBodyAnchorValid)
            cameraAnchorWorld=vrState.vehicleBodyAnchorWorld.Row(3);
        rmt::Vector cameraRelative=cameraAnchorWorld-vehicleWorld.Row(3);
        rmt::Vector cameraLocal;
        worldToVehicle.RotateVector(cameraRelative,&cameraLocal);
        cameraLocal.y-=vrDriverEyeHeight;
        rmt::Vector anchorRelative;
        vehicleToWorld.RotateVector(cameraLocal,&anchorRelative);
        rmt::Vector anchor=vehicleWorld.Row(3)+anchorRelative;
        targetOrigin=anchor;
        targetOrigin.y+=player->GetYAdjust();
        // The private pose is converted to vehicle-local coordinates below.
        // Its root therefore must be expressed from the vehicle origin, not
        // as a delta from the original passenger puppet root.
        anchorOffsetWorld=anchor-vehicleWorld.Row(3);
    }
    rmt::Vector targetOriginLocal(0,0,0);
    if(inCar)
    {
        rmt::Vector relative=targetOrigin-vehicleWorld.Row(3);
        worldToVehicle.RotateVector(relative,&targetOriginLocal);
    }
    for(int i=0;i<2;++i)
    {
        bool haveWheelPose=false;
        if(inCar)
        {
            const SharedVrState& state=GetSharedVrState();
            Vehicle* vehicle=player->GetTargetVehicle();
            const bool yoke=vehicle&&IsVrYokeVehicle(vehicle->GetName());
            haveWheelPose=state.vehicleControlMode==1&&GetVrVehicleHandPose(i,yoke,&local);
        }
        if(!haveWheelPose&&!GetControllerLocalPose(i,&local))
        {ReportRig(player,11+i,"fallback: controller pose unavailable");return NULL;}
        hands[i].Mult(local,trackingBase);hands[i].Row(3)-=targetOrigin;
        if(inCar)
        {
            rmt::Matrix converted;converted.Mult(hands[i],worldToVehicle);
            hands[i]=converted;
            hands[i].Row(3)+=targetOriginLocal;
        }
        for(int row=0;row<4;++row)if(!Finite(hands[i].Row(row)))return NULL;
    }
    head.Row(3)-=targetOrigin;
    if(inCar)
    {
        rmt::Matrix converted;converted.Mult(head,worldToVehicle);head=converted;
        head.Row(3)+=targetOriginLocal;
    }
    for(int row=0;row<4;++row)if(!Finite(head.Row(row)))return NULL;
    Chain arms[2],legs[2];
    // OpenXR 0 is LEFT. The separate OBJ hands' reversed labels do not apply
    // to native skeleton names. Resolve ancestors instead of numeric arms.
    if(!Resolve(animated,"Wrist_L",arms[0])||!Resolve(animated,"Wrist_R",arms[1])||
       !Resolve(animated,"Ankle_L",legs[0])||!Resolve(animated,"Ankle_R",legs[1]))
    {ReportRig(player,1,"fallback hands: wrist/ankle chain not resolved");return NULL;}
    int headIndex=animated->FindJointIndex("Head");
    // CharacterTarget::{GetHeadWorldPosition,GetHeadWorldTransform} uses 17.
    if(headIndex<0&&animated->GetNumJoint()>17)headIndex=17;
    const int pelvis=animated->FindJointIndex("Balance_Root");
    if(headIndex<0||pelvis<=0||!Descendant(animated,headIndex,pelvis))
    {ReportRig(player,2,"fallback hands: head/Balance_Root hierarchy not supported");return NULL;}
    for(int i=0;i<2;++i)
        if(!Descendant(animated,arms[i].upper,pelvis)||!Descendant(animated,legs[i].upper,pelvis))
        {ReportRig(player,3,"fallback hands: limb outside Balance_Root");return NULL;}
    for(int i=0;i<animated->GetNumJoint();++i)
        for(int row=0;row<4;++row)if(!Finite(animated->GetJoint(i)->worldMatrix.Row(row)))return NULL;
    tPose* p=scratch.Copy(animated);
    if(inCar)
    {
        for(int i=0;i<p->GetNumJoint();++i)
        {
            rmt::Matrix converted;converted.Mult(p->GetJoint(i)->worldMatrix,worldToVehicle);
            p->GetJoint(i)->worldMatrix=converted;
        }
        rmt::Vector anchorOffset;
        worldToVehicle.RotateVector(anchorOffsetWorld,&anchorOffset);
        rmt::Matrix identity;identity.Identity();
        MoveBranch(p,0,identity,anchorOffset);
    }
    // The gameplay camera contains stick yaw but excludes physical HMD yaw.
    // Rotate the WHOLE render pose around its motion root before capturing
    // ankle targets, so planted targets cannot undo a snap/smooth body turn.
    rmt::Vector nativeForward;player->GetFacing(nativeForward);nativeForward.y=0;
    nativeForward=Unit(nativeForward,rmt::Vector(0,0,1));
    rmt::Vector bodyForward;
    rmt::Vector bodyRotationSource=nativeForward;
    if(inCar)
    {
        // Character::UpdateParentTransform attaches the seated puppet to the
        // The character mesh is authored facing local -Z.  The seated puppet
        // must therefore use the opposite of the vehicle's +Z travel axis so
        // its visible front points through the windshield.
        bodyForward=rmt::Vector(0,0,-1);
        bodyRotationSource=Unit(
            rmt::Vector(p->GetJoint(0)->worldMatrix.Row(2).x,0,
                        p->GetJoint(0)->worldMatrix.Row(2).z),bodyForward);
    }
    else
    {
        // On foot the gameplay camera carries the normal locomotion/stick yaw.
        bodyForward=Unit(rmt::Vector(base.Row(2).x,0,base.Row(2).z),
                         nativeForward);
    }
    MoveBranch(p,0,Rotation(bodyRotationSource,bodyForward),Position(p,0));
    rmt::Matrix foot[2]={p->GetJoint(legs[0].end)->worldMatrix,p->GetJoint(legs[1].end)->worldMatrix};
    const rmt::Vector forward=Unit(rmt::Vector(head.Row(2).x,0,head.Row(2).z),rmt::Vector(0,0,1));
    rmt::Vector right;right.CrossProduct(rmt::Vector(0,1,0),forward);
    // Camera sits 10 cm above the neck attachment. Subtract the matching
    // world-up clearance so IK does not lift the body and cancel that gap.
    // Keep in sync with Character::GetVrNeckCameraHeight.
    const float neckEyeClearance=0.10f;
    const rmt::Vector headTarget=head.Row(3)-rmt::Vector(0,neckEyeClearance,0);
    rmt::Vector offset=headTarget-Position(p,headIndex);
    // Bound visual lean/crouch; gameplay collision/root/roomscale stay native.
    const float horizontal=std::sqrt(offset.x*offset.x+offset.z*offset.z);
    if(horizontal>0.30f){offset.x*=0.30f/horizontal;offset.z*=0.30f/horizontal;}
    offset.y=Clamp(offset.y,-0.45f,0.10f);
    rmt::Matrix identity;identity.Identity();
    MoveBranch(p,pelvis,identity,Position(p,pelvis)+offset);
    // Use the lowest common arm ancestor as chest; no guessed spine indices.
    int chest=arms[0].upper;
    while(chest>0&&!Descendant(p,arms[1].upper,chest))
        chest=p->GetSkeleton()->GetJoint(chest)->parentIndex;
    if(chest>0&&chest!=pelvis&&Descendant(p,headIndex,chest))
    {
        // On foot the torso follows the gameplay/head heading.  In a vehicle
        // the torso is anchored to the chassis; applying head yaw here made a
        // small headset turn spin the entire body by roughly 30 degrees.
        if(!inCar)
        {
            const float angle=Clamp(std::atan2(bodyForward.x*forward.z-bodyForward.z*forward.x,
                                              bodyForward.DotProduct(forward)),-0.65f,0.65f);
            const float c=std::cos(angle),s=std::sin(angle);
            rmt::Vector limited(bodyForward.x*c-bodyForward.z*s,0,bodyForward.x*s+bodyForward.z*c);
            MoveBranch(p,chest,Rotation(bodyForward,limited),Position(p,chest));
        }
        rmt::Vector from=Position(p,headIndex)-Position(p,chest);
        rmt::Vector to=headTarget-Position(p,chest);
        // Limit spine lean to avoid folding during extreme tracking offsets.
        from=Unit(from,rmt::Vector(0,1,0));to=Unit(to,from);
        if(from.DotProduct(to)<0.90f)to=Unit(from*0.8f+to*0.2f,from);
        MoveBranch(p,chest,Rotation(from,to),Position(p,chest));
    }
    for(int i=0;i<2;++i)
    {
        if(!SolveChain(p,legs[i],foot[i].Row(3),bodyForward))
        {
            // A transient tracking/animation singularity must not discard
            // the complete body pose. Keep this leg's animated pose and
            // continue solving the other limbs.
            ReportRig(player,20+i,"partial: leg IK solve failed");
        }
        else
            MatchRotation(p,legs[i].end,foot[i]);
        const rmt::Vector pole=right*(i==0?-0.65f:0.65f)-forward*0.30f+rmt::Vector(0,-0.55f,0);
        // OpenXR hands are measured in the player's space, while the chosen
        // character can have a shorter arm span.  The solver already applies
        // stretch only when the target is near/outside the native reach; give
        // it enough headroom to keep the rendered wrist on the tracked hand.
        // This affects the private render pose only, never gameplay physics.
        // Calculate whatever visual extension is required for this character's
        // arm to reach the tracked wrist exactly.  This only modifies the
        // private render pose; gameplay physics and the authored skeleton stay
        // unchanged.
        const float maxArmStretch=std::numeric_limits<float>::max();
        if(!SolveChain(p,arms[i],hands[i].Row(3),pole,maxArmStretch))
        {
            // Keep the animated arm for this frame instead of switching the
            // entire character back to the non-VR pose.
            ReportRig(player,22+i,"partial: arm IK solve failed");
            continue;
        }
        // Bind wrist axes alone still contain the model's T-pose arm yaw.
        // Express them in a neutral anatomical grip frame first. The frame
        // is derived separately for each arm, so mirrored rigs do not need
        // guessed +/-90 degree constants or left/right controller swaps.
        tSkeleton* skeleton=animated->GetSkeleton();
        rmt::Matrix wristBind=skeleton->GetJoint(arms[i].end)->worldMatrix;
        const rmt::Vector bindForearm=wristBind.Row(3)-
            skeleton->GetJoint(arms[i].middle)->worldMatrix.Row(3);
        const rmt::Vector bindUp=skeleton->GetJoint(0)->worldMatrix.Row(1);
        rmt::Matrix inverseGrip=NeutralGripFrame(bindForearm,bindUp);
        inverseGrip.InvertOrtho();
        for(int row=0;row<3;++row)wristBind.Row(row)=Unit(wristBind.Row(row),identity.Row(row));
        wristBind.Row(3).Set(0,0,0);
        rmt::Matrix wristToGrip;wristToGrip.Mult(wristBind,inverseGrip);
        // User-calibrated neutral wrist roll: left 90 degrees left,
        // right 90 degrees right, viewed along the extended hands.
        // Apply about grip-local +Z BEFORE the tracked world orientation.
        // OpenXR index 0 = left, 1 = right; row-vector +Z rotation moves
        // local up toward -X, hence +90 left and -90 right.
        const float neutralRollRadians=(i==0?1.0f:-1.0f)*1.57079632679f;
        rmt::Matrix neutralRoll;neutralRoll.Identity();
        neutralRoll.FillRotateZ(neutralRollRadians);
        rmt::Matrix calibratedWrist;calibratedWrist.Mult(wristToGrip,neutralRoll);
        // Pitch AFTER the mirrored roll, around the common grip-local X.
        // Positive X rotation sends +Z forward toward -Y in SHAR's math.
        const float neutralDownTiltRadians=5.0f*0.01745329252f;
        rmt::Matrix downTilt;downTilt.Identity();downTilt.FillRotateX(neutralDownTiltRadians);
        rmt::Matrix tiltedWrist;tiltedWrist.Mult(calibratedWrist,downTilt);
        rmt::Matrix wrist;wrist.Mult(tiltedWrist,hands[i]);
        MatchRotation(p,arms[i].end,wrist);
        ApplyGripFingers(p,arms[i].end,static_cast<unsigned>(i));
    }
    // Use the animated head pose as the neutral anatomical orientation.
    // The tracked culling camera is composed as HMD-local * gameplay-camera,
    // so remove the gameplay camera again to recover only the user's physical
    // head rotation. At a neutral headset pose this becomes identity, leaving
    // the character head exactly in its authored straight-ahead orientation.
    rmt::Matrix baseForPose=inCar?trackingBase:base;
    if(inCar)
    {
        rmt::Matrix converted;converted.Mult(baseForPose,worldToVehicle);
        baseForPose=converted;
    }
    rmt::Matrix inverseBase=baseForPose;
    for(int row=0;row<3;++row)inverseBase.Row(row)=Unit(inverseBase.Row(row),identity.Row(row));
    inverseBase.Row(3).Set(0,0,0);inverseBase.InvertOrtho();
    rmt::Matrix trackedHead=head;
    for(int row=0;row<3;++row)trackedHead.Row(row)=Unit(trackedHead.Row(row),identity.Row(row));
    trackedHead.Row(3).Set(0,0,0);
    rmt::Matrix physicalHead;physicalHead.Mult(trackedHead,inverseBase);
    rmt::Matrix neutralHead=p->GetJoint(headIndex)->worldMatrix;
    for(int row=0;row<3;++row)neutralHead.Row(row)=Unit(neutralHead.Row(row),identity.Row(row));
    neutralHead.Row(3).Set(0,0,0);
    rmt::Matrix headRotation;headRotation.Mult(physicalHead,neutralHead);
    MatchRotation(p,headIndex,headRotation);
    if(inCar)
    {
        for(int i=0;i<p->GetNumJoint();++i)
        {
            rmt::Matrix converted;converted.Mult(p->GetJoint(i)->worldMatrix,vehicleToWorld);
            p->GetJoint(i)->worldMatrix=converted;
        }
    }
    ReportRig(player,0,"active [GRIP_POSE_V2 FIST_V8_THUMB HEAD_NEUTRAL_V1]: tracked arms, neutral-relative head and animated-foot leg IK");
    return p;
}
void HideBodyIKHead(tPose* p)
{
    int head=p->FindJointIndex("Head");if(head<0&&p->GetNumJoint()>17)head=17;
    if(head<0)return;
    // Collapse head/hair descendants only in the local eye pose. A small
    // nonzero scale avoids singular normal matrices in skinning shaders.
    const rmt::Vector anchor=Position(p,head);
    for(int i=0;i<p->GetNumJoint();++i)if(Descendant(p,i,head))
    {
        rmt::Matrix& m=p->GetJoint(i)->worldMatrix;
        for(int row=0;row<3;++row)m.Row(row).Scale(0.001f);
        m.Row(3)=anchor+(m.Row(3)-anchor)*0.001f;
    }
}
}
#else
namespace SharOpenXR
{
void BeginBodyIKEye(){}
bool WasBodyIKDrawn(){return false;}
void MarkBodyIKDrawn(){}
tPose* BuildBodyIKPose(Character*,tPose*,const rmt::Vector&){return 0;}
void HideBodyIKHead(tPose*){}
}
#endif

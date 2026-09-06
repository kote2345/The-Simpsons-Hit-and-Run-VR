#include <vr/openxr_shared_tracking.h>
#include <vr/openxr_shared_render.h>
#include <vr/openxr_shared_state.h>
namespace SharOpenXR
{
void QueueSharedSystemRecenter(SharedSystemRecenterState* state,XrTime time)
{
    if(!state)return;state->pending=true;state->changeTime=time;
}

bool UpdateSharedSystemRecenter(SharedSystemRecenterState* state,
    XrTime displayTime,const XrView views[2],XrViewStateFlags flags,
    bool seated,XrPosef* origin,bool* valid)
{
    if(!state||!views||!origin||!valid)return false;
    bool invalidated=false;
    if(state->pending&&displayTime>=state->changeTime)
    {
        state->preserveHeight=*valid&&!seated;
        state->preservedHeight=origin->position.y;
        state->settleFrames=3;state->pending=false;*valid=false;invalidated=true;
    }
    if(state->settleFrames)
    {
        *valid=false;invalidated=true;
        if((flags&XR_VIEW_STATE_ORIENTATION_VALID_BIT)&&
           (flags&XR_VIEW_STATE_POSITION_VALID_BIT))
        {
            *origin=SharedRender::CentreYawAnchor(views[0].pose,views[1].pose);
            const float height=(views[0].pose.position.y+views[1].pose.position.y)*0.5f;
            origin->position.y=state->preserveHeight?state->preservedHeight:height;
            *valid=true;if(--state->settleFrames==0)state->preserveHeight=false;
        }
    }
    return invalidated;
}
void LocateSharedHandPoses(PFN_xrLocateSpace locate,const XrSpace spaces[2],
    XrSpace base,XrTime time,bool originValid,XrPosef poses[2],bool valid[2])
{
    if(!valid)return;
    for(unsigned hand=0;hand<2;++hand)
    {
        valid[hand]=false;
        if(!locate||!spaces||!poses||!originValid||spaces[hand]==XR_NULL_HANDLE)continue;
        XrSpaceLocation location={XR_TYPE_SPACE_LOCATION};
        const XrResult result=locate(spaces[hand],base,time,&location);
        SharedRender::AcceptLocatedHandPose(originValid,result,location,
            &poses[hand],&valid[hand]);
    }
}
bool RecenterSharedTracking(const XrView views[2],XrViewStateFlags flags,XrPosef* origin)
{
    if(!views||!origin||!(flags&XR_VIEW_STATE_ORIENTATION_VALID_BIT)||
       !(flags&XR_VIEW_STATE_POSITION_VALID_BIT))return false;
    *origin=SharedRender::CentreYawAnchor(views[0].pose,views[1].pose);return true;
}
bool ConsumeSharedRoomscale(const XrView views[2],XrViewStateFlags flags,
    XrPosef* origin,bool originValid,bool inCar,rmt::Vector* delta)
{
    if(!delta)return false;delta->Set(0,0,0);SharedVrState& s=GetSharedVrState();
    if(!views||!origin||!originValid||!s.vrModeEnabled||!(flags&XR_VIEW_STATE_POSITION_VALID_BIT))return false;
    XrPosef head=views[0].pose;head.position.x=(views[0].pose.position.x+views[1].pose.position.x)*.5f;
    head.position.z=(views[0].pose.position.z+views[1].pose.position.z)*.5f;
    if(inCar){s.roomscaleMovementSuspended=true;return false;}
    if(s.roomscaleMovementSuspended){origin->position.x=head.position.x;origin->position.z=head.position.z;s.roomscaleMovementSuspended=false;return false;}
    const XrPosef relative=SharedRender::RelativePose(*origin,head);
    rmt::Vector local(relative.position.x,0,-relative.position.z);
    origin->position.x=head.position.x;origin->position.z=head.position.z;
    const float distance=local.MagnitudeSqr();if(distance>.25f||distance<.00000001f)return false;
    if(s.vrBaseHeadingValid){rmt::Matrix base;base.Identity();base.FillHeading(s.vrBaseHeading,rmt::Vector(0,1,0));local.Rotate(base);}
    *delta=local;return true;
}
bool GetSharedPhysicalHeadHeight(const XrPosef& origin,bool valid,bool stage,
    bool seated,bool childCharacter,float* height)
{
    if(!height||!valid||!stage)return false;float value=seated?1.70f:origin.position.y;
    if(childCharacter)value*=.78f;
    *height=value;return value>.25f&&value<2.75f;
}
bool GetSharedHeadForward(const XrView views[2],const XrPosef& origin,bool valid,rmt::Vector* forward)
{
    if(!views||!forward||!valid)return false;const XrPosef relative=SharedRender::RelativePose(origin,views[0].pose);
    const XrVector3f f=SharedRender::Rotate(relative.orientation,XrVector3f{0,0,-1});forward->Set(f.x,0,-f.z);
    SharedVrState& s=GetSharedVrState();if(s.vrBaseHeadingValid){rmt::Matrix base;base.Identity();base.FillHeading(s.vrBaseHeading,rmt::Vector(0,1,0));forward->Transform(base);}
    return forward->MagnitudeSqr()>.0001f&&forward->NormalizeSafe();
}
}

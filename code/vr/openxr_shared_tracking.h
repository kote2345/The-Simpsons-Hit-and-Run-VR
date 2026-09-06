#ifndef SHAR_OPENXR_SHARED_TRACKING_H
#define SHAR_OPENXR_SHARED_TRACKING_H
#if defined(SRR2_OPENXR)
#include <openxr/openxr.h>
#include <radmath/radmath.hpp>
namespace SharOpenXR
{
struct SharedSystemRecenterState
{
    bool pending,preserveHeight;
    XrTime changeTime;
    float preservedHeight;
    unsigned settleFrames;
    SharedSystemRecenterState():pending(false),preserveHeight(false),changeTime(0),
        preservedHeight(0),settleFrames(0){}
};
void QueueSharedSystemRecenter(SharedSystemRecenterState* state,XrTime changeTime);
bool UpdateSharedSystemRecenter(SharedSystemRecenterState* state,
    XrTime displayTime,const XrView views[2],XrViewStateFlags flags,
    bool seatedMode,XrPosef* origin,bool* originValid);
void LocateSharedHandPoses(PFN_xrLocateSpace locateSpace,const XrSpace handSpaces[2],
    XrSpace baseSpace,XrTime displayTime,bool originValid,
    XrPosef poses[2],bool valid[2]);
bool RecenterSharedTracking(const XrView views[2],XrViewStateFlags flags,
                            XrPosef* origin);
bool ConsumeSharedRoomscale(const XrView views[2],XrViewStateFlags flags,
    XrPosef* origin,bool originValid,bool playerInCar,rmt::Vector* worldDelta);
bool GetSharedPhysicalHeadHeight(const XrPosef& origin,bool originValid,
    bool stageSpace,bool seatedMode,bool childCharacter,float* height);
bool GetSharedHeadForward(const XrView views[2],const XrPosef& origin,
    bool originValid,rmt::Vector* forward);
}
#endif
#endif

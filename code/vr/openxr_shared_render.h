#ifndef SHAR_OPENXR_SHARED_RENDER_H
#define SHAR_OPENXR_SHARED_RENDER_H

#include <openxr/openxr.h>
#include <radmath/radmath.hpp>
#include <cmath>

namespace SharOpenXR
{
namespace SharedRender
{
enum SessionOperation { SESSION_NO_OP,SESSION_BEGIN,SESSION_END,SESSION_EXIT };

inline SessionOperation GetSessionOperation(XrSessionState state)
{
    if(state==XR_SESSION_STATE_READY) return SESSION_BEGIN;
    if(state==XR_SESSION_STATE_STOPPING) return SESSION_END;
    if(state==XR_SESSION_STATE_EXITING||state==XR_SESSION_STATE_LOSS_PENDING) return SESSION_EXIT;
    return SESSION_NO_OP;
}

inline bool HasValidViewTracking(XrViewStateFlags flags)
{
    const XrViewStateFlags required=XR_VIEW_STATE_POSITION_VALID_BIT|
                                    XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    return (flags&required)==required;
}

inline bool HasValidTrackedPose(XrSpaceLocationFlags flags)
{
    const XrSpaceLocationFlags required=XR_SPACE_LOCATION_POSITION_VALID_BIT|
                                        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    return (flags&required)==required;
}

inline void BuildStereoProjectionLayer(const XrView views[2],XrSwapchain swapchain,
                                       const int widths[2],const int heights[2],
                                       XrSpace space,
                                       XrCompositionLayerProjectionView outputViews[2],
                                       XrCompositionLayerProjection* outputLayer)
{
    for(unsigned eye=0;eye<2;++eye)
    {
        outputViews[eye]=XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        outputViews[eye].pose=views[eye].pose;
        outputViews[eye].fov=views[eye].fov;
        outputViews[eye].subImage.swapchain=swapchain;
        outputViews[eye].subImage.imageRect.extent.width=widths[eye];
        outputViews[eye].subImage.imageRect.extent.height=heights[eye];
        outputViews[eye].subImage.imageArrayIndex=eye;
    }
    *outputLayer=XrCompositionLayerProjection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    outputLayer->space=space;
    outputLayer->viewCount=2;
    outputLayer->views=outputViews;
}

inline XrFrameEndInfo BuildFrameEndInfo(XrTime displayTime,bool shouldRender,
                                        const XrCompositionLayerBaseHeader* const* layers)
{
    XrFrameEndInfo result={XR_TYPE_FRAME_END_INFO};
    result.displayTime=displayTime;
    result.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    result.layerCount=shouldRender?1u:0u;
    result.layers=shouldRender?layers:NULL;
    return result;
}

inline bool AcceptLocatedHandPose(bool originValid,XrResult result,
                                  const XrSpaceLocation& location,
                                  XrPosef* pose,bool* valid)
{
    *valid=originValid&&XR_SUCCEEDED(result)&&HasValidTrackedPose(location.locationFlags);
    if(*valid) *pose=location.pose;
    return *valid;
}

inline XrQuaternionf Conjugate(const XrQuaternionf& q)
{
    XrQuaternionf result={-q.x,-q.y,-q.z,q.w};
    return result;
}

inline XrQuaternionf Multiply(const XrQuaternionf& a,const XrQuaternionf& b)
{
    XrQuaternionf result={
        a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
    return result;
}

inline XrVector3f Rotate(const XrQuaternionf& q,const XrVector3f& v)
{
    const XrQuaternionf p={v.x,v.y,v.z,0.0f};
    const XrQuaternionf r=Multiply(Multiply(q,p),Conjugate(q));
    XrVector3f result={r.x,r.y,r.z};
    return result;
}

inline XrQuaternionf YawOnly(const XrQuaternionf& orientation)
{
    const XrVector3f forward=Rotate(orientation,XrVector3f{0.0f,0.0f,-1.0f});
    float yaw=0.0f;
    if(forward.x*forward.x+forward.z*forward.z>0.01f)
        yaw=std::atan2(-forward.x,-forward.z);
    else
    {
        const XrVector3f right=Rotate(orientation,XrVector3f{1.0f,0.0f,0.0f});
        yaw=std::atan2(-right.z,right.x);
    }
    XrQuaternionf result={0.0f,std::sin(yaw*0.5f),0.0f,std::cos(yaw*0.5f)};
    return result;
}

inline XrPosef RelativePose(const XrPosef& origin,const XrPosef& pose)
{
    const XrQuaternionf inverse=Conjugate(origin.orientation);
    const XrVector3f delta={pose.position.x-origin.position.x,
                            pose.position.y-origin.position.y,
                            pose.position.z-origin.position.z};
    XrPosef result;
    result.orientation=Multiply(inverse,pose.orientation);
    result.position=Rotate(inverse,delta);
    return result;
}

inline rmt::Matrix PoseToGame(const XrPosef& pose)
{
    const rmt::Quaternion rotation(pose.orientation.w,-pose.orientation.x,
                                   -pose.orientation.y,pose.orientation.z);
    rmt::Matrix result;
    result.Identity();
    result.FillRotation(rotation);
    result.Row(3).Set(pose.position.x,pose.position.y,-pose.position.z);
    return result;
}

inline bool ComposeControllerLocalPose(const XrPosef& origin,bool originValid,
                                       const XrPosef handPoses[2],
                                       const bool handValid[2],unsigned hand,
                                       rmt::Matrix* result)
{
    if(!result||hand>=2||!originValid||!handValid[hand]) return false;
    *result=PoseToGame(RelativePose(origin,handPoses[hand]));
    return true;
}

inline bool ComposeControllerWorldPose(const XrPosef& origin,bool originValid,
                                       const XrPosef handPoses[2],
                                       const bool handValid[2],unsigned hand,
                                       const rmt::Matrix& baseCamera,
                                       rmt::Matrix* result)
{
    rmt::Matrix local;
    if(!ComposeControllerLocalPose(origin,originValid,handPoses,handValid,hand,&local)) return false;
    result->Mult(local,baseCamera);
    return true;
}

inline void MakeProjection(const XrFovf& fov,float nearPlane,float farPlane,
                           rmt::Matrix* result)
{
    const float left=std::tan(fov.angleLeft),right=std::tan(fov.angleRight);
    const float bottom=std::tan(fov.angleDown),top=std::tan(fov.angleUp);
    result->Identity();
    result->Row4(0).Set(2.0f/(right-left),0,0,0);
    result->Row4(1).Set(0,2.0f/(top-bottom),0,0);
    result->Row4(2).Set(-(right+left)/(right-left),
                        -(top+bottom)/(top-bottom),
                        (farPlane+nearPlane)/(farPlane-nearPlane),1);
    result->Row4(3).Set(0,0,(-2.0f*farPlane*nearPlane)/
                            (farPlane-nearPlane),0);
}

inline XrPosef CentreYawAnchor(const XrPosef& left,const XrPosef& right)
{
    XrPosef result=left;
    result.orientation=YawOnly(left.orientation);
    result.position.x=(left.position.x+right.position.x)*0.5f;
    result.position.y=(left.position.y+right.position.y)*0.5f;
    result.position.z=(left.position.z+right.position.z)*0.5f;
    return result;
}

inline void ComposeTrackedCamera(const XrPosef& origin,const XrPosef& view,
                                 const rmt::Matrix& baseCamera,
                                 rmt::Matrix* result)
{
    const rmt::Matrix local=PoseToGame(RelativePose(origin,view));
    result->Mult(local,baseCamera);
}

inline void ComposeTrackedCentreCamera(const XrPosef& origin,
                                       const XrPosef& left,
                                       const XrPosef& right,
                                       const rmt::Matrix& baseCamera,
                                       rmt::Matrix* result)
{
    XrPosef centre=left;
    centre.position.x=(left.position.x+right.position.x)*0.5f;
    centre.position.y=(left.position.y+right.position.y)*0.5f;
    centre.position.z=(left.position.z+right.position.z)*0.5f;
    ComposeTrackedCamera(origin,centre,baseCamera,result);
}

// Pure3D frontend/movie geometry is authored around a unit canvas. This is
// the common Quest + PCVR transform which turns that canvas into a
// world-locked plane and then projects it through the current OpenXR eye.
inline void ComposeWorldLockedPanel(const XrPosef& anchor,
                                    const XrPosef& eyePose,
                                    const XrFovf& eyeFov,
                                    float scaleX,float scaleY,float scaleZ,
                                    rmt::Matrix* result)
{
    const rmt::Matrix anchorWorld=PoseToGame(anchor);
    const rmt::Matrix eyeWorld=PoseToGame(eyePose);
    rmt::Matrix worldToEye;
    worldToEye.InvertOrtho(eyeWorld);
    rmt::Matrix scale;
    scale.Identity();
    scale.Row4(0).Set(scaleX,0,0,0);
    scale.Row4(1).Set(0,scaleY,0,0);
    scale.Row4(2).Set(0,0,scaleZ,0);
    rmt::Matrix anchorToEye,localToEye,projection;
    anchorToEye.Mult(anchorWorld,worldToEye);
    localToEye.Mult(scale,anchorToEye);
    MakeProjection(eyeFov,0.1f,1000.0f,&projection);
    result->MultFull(localToEye,projection);
}
}
}

#endif

#ifndef SHAR_VR_BODY_IK_MATH_H
#define SHAR_VR_BODY_IK_MATH_H
#include <radmath/radmath.hpp>
#include <cmath>
namespace SharBodyIK
{
inline float Clamp(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
inline bool Finite(const rmt::Vector& v)
{return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline rmt::Vector Unit(rmt::Vector v,const rmt::Vector& fallback)
{const float n=v.MagnitudeSqr();return n>1e-10f?v*(1.0f/std::sqrt(n)):fallback;}
// Anatomical neutral grip frame: +Z along the authored forearm toward
// the fingers, +Y toward the back of a palm-down hand in the bind pose.
// Remove this frame before applying the converted OpenXR grip orientation;
// otherwise the left/right T-pose directions remain baked into each wrist.
inline rmt::Matrix NeutralGripFrame(rmt::Vector forward,rmt::Vector up)
{
    forward=Unit(forward,rmt::Vector(0,0,1));
    up-=forward*up.DotProduct(forward);
    if(up.MagnitudeSqr()<1e-8f)
    {
        up=std::fabs(forward.y)<0.8f?rmt::Vector(0,1,0):rmt::Vector(0,0,1);
        up-=forward*up.DotProduct(forward);
    }
    up=Unit(up,rmt::Vector(0,1,0));
    rmt::Vector right;right.CrossProduct(up,forward);
    right=Unit(right,rmt::Vector(1,0,0));
    up.CrossProduct(forward,right);
    rmt::Matrix frame;frame.Identity();
    frame.Row(0)=right;frame.Row(1)=up;frame.Row(2)=forward;
    return frame;
}
// Row-vector rotation: rotate every basis vector using Rodrigues' formula.
inline rmt::Matrix Rotation(rmt::Vector from,rmt::Vector to)
{
    from=Unit(from,rmt::Vector(0,1,0));to=Unit(to,from);
    float c=Clamp(from.DotProduct(to),-1.0f,1.0f);
    rmt::Vector axis;axis.CrossProduct(from,to);
    float s=axis.Magnitude();
    if(s<1e-6f)
    {
        if(c>0){rmt::Matrix identity;identity.Identity();return identity;}
        axis.CrossProduct(from,std::fabs(from.x)<0.8f?rmt::Vector(1,0,0):rmt::Vector(0,1,0));
        axis=Unit(axis,rmt::Vector(0,0,1));s=0;
    }
    else axis.Scale(1.0f/s);
    rmt::Matrix result;result.Identity();
    for(int i=0;i<3;++i)
    {
        const rmt::Vector v=result.Row(i);rmt::Vector cross;cross.CrossProduct(axis,v);
        result.Row(i)=v*c+cross*s+axis*(axis.DotProduct(v)*(1-c));
    }
    return result;
}
// Exact measured segment lengths; unreachable targets are projected onto
// the reachable annulus. Pole is a direction, not a world position.
inline bool Solve(const rmt::Vector& root,const rmt::Vector& requested,
                  float upper,float lower,rmt::Vector pole,
                  rmt::Vector& middle,rmt::Vector& end)
{
    if(!Finite(root)||!Finite(requested)||!Finite(pole)||
       !std::isfinite(upper)||!std::isfinite(lower)||upper<1e-4f||lower<1e-4f)return false;
    rmt::Vector direction=Unit(requested-root,rmt::Vector(0,0,1));
    const float epsilon=(upper+lower)*1e-4f;
    const float distance=Clamp((requested-root).Magnitude(),std::fabs(upper-lower)+epsilon,upper+lower-epsilon);
    pole-=direction*pole.DotProduct(direction);
    if(pole.MagnitudeSqr()<1e-8f)
    {
        pole=std::fabs(direction.y)<0.8f?rmt::Vector(0,1,0):rmt::Vector(1,0,0);
        pole-=direction*pole.DotProduct(direction);
    }
    pole=Unit(pole,rmt::Vector(1,0,0));
    const float along=(upper*upper-lower*lower+distance*distance)/(2*distance);
    const float height=std::sqrt(Clamp(upper*upper-along*along,0,upper*upper));
    middle=root+direction*along+pole*height;end=root+direction*distance;
    return Finite(middle)&&Finite(end);
}
}
#endif

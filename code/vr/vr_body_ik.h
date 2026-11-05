#ifndef SHAR_VR_BODY_IK_H
#define SHAR_VR_BODY_IK_H
#include <radmath/radmath.hpp>
class Character;
class tPose;
namespace SharOpenXR
{
// Runtime body joints in the same local tracking frame as controllers.
struct UpperBodyTracking {
    bool valid=false;
    bool torsoValid=false,armValid[2]={false,false};
    rmt::Vector chest,neck,shoulders[2],elbows[2];
    bool hipsValid=false,legValid[2]={false,false};
    rmt::Vector hips,thighs[2],knees[2],ankles[2],toes[2];
};
inline UpperBodyTracking& GetUpperBodyTracking(){static UpperBodyTracking state;return state;}
// Camera moves from the head/neck centre toward the character's eyes.
const float VrEyeForwardOffset=0.10f;
// Per-eye ownership, reset even when no character is submitted this eye.
void BeginBodyIKEye();
bool WasBodyIKDrawn();
void MarkBodyIKDrawn();
// Returns a private render pose, or NULL to retain the existing hands path.
// Input matrices have already been scaled and made root-relative by Character.
tPose* BuildBodyIKPose(Character* player,tPose* animated,const rmt::Vector& renderOrigin);
// Apply only after the full-body shadow snapshot has been captured.
void HideBodyIKHead(tPose* pose);
}
#endif

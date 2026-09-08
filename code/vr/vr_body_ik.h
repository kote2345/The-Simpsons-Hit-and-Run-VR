#ifndef SHAR_VR_BODY_IK_H
#define SHAR_VR_BODY_IK_H
#include <radmath/radmath.hpp>
class Character;
class tPose;
namespace SharOpenXR
{
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

#ifndef OPENXR_SHARED_HANDS_H
#define OPENXR_SHARED_HANDS_H

#include <radmath/radmath.hpp>

namespace SharOpenXR
{
// Draws the authored character hands at already game-world-space controller poses.
void RenderTrackedHandMeshes(const rmt::Matrix worldPoses[2], const bool valid[2]);
}

#endif

#ifndef OPENXR_SHARED_HANDS_H
#define OPENXR_SHARED_HANDS_H

#include <radmath/radmath.hpp>

namespace SharOpenXR
{
// Draws the authored character hands at already game-world-space controller poses.
// The same poses are cached here so gameplay interaction tests use the exact
// controller positions that the player sees rendered, rather than rebuilding
// a second world transform on a different update/render timeline.
void RenderTrackedHandMeshes(const rmt::Matrix worldPoses[2], const bool valid[2]);
bool GetRenderedHandWorldPosition(unsigned hand, rmt::Vector* outPosition);
void ResetRenderedHandWorldPositions();
}

#endif

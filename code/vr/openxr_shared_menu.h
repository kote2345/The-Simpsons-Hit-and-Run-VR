#ifndef SHAR_OPENXR_SHARED_MENU_H
#define SHAR_OPENXR_SHARED_MENU_H

#include <openxr/openxr.h>
#include <radmath/radmath.hpp>

namespace SharOpenXR
{
// Runtime-independent state and placement policy for the authored frontend.
// Backends only provide the current stereo views and target dimensions.
class SharedVrMenu
{
public:
    SharedVrMenu();

    void Reset();
    void InvalidateAnchor();
    void SetActive(bool active);
    void SetRendering(bool rendering,bool eyeActive,const XrView views[2]);
    bool IsRendering() const;
    bool GetProjection(bool eyeActive,const XrView& eye,int width,int height,
                       rmt::Matrix* projection,int* targetWidth,
                       int* targetHeight,bool requireRendering=true) const;

private:
    bool mActive;
    bool mRendering;
    bool mAnchorValid;
    XrPosef mAnchor;
};

SharedVrMenu& GetSharedVrMenu();

class SharedMoviePanel
{
public:
    SharedMoviePanel();
    void Begin();
    void End();
    void InvalidateAnchor();
    void SetRendering(bool rendering,bool eyeActive,const XrView views[2]);
    bool IsRendering() const { return mRendering; }
    bool GetProjection(bool eyeActive,const XrView& eye,int width,int height,
        rmt::Matrix* projection,int* targetWidth,int* targetHeight) const;
private:
    bool mActive,mRendering,mAnchorValid;
    XrPosef mAnchor;
};
SharedMoviePanel& GetSharedMoviePanel();
}

#endif

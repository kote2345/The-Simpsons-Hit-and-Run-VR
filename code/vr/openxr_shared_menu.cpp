#include <vr/openxr_shared_menu.h>
#include <vr/openxr_shared_render.h>

namespace SharOpenXR
{
SharedVrMenu::SharedVrMenu()
{
    Reset();
}

void SharedVrMenu::Reset()
{
    mActive=false;
    mRendering=false;
    mAnchorValid=false;
    mAnchor=XrPosef{{0.0f,0.0f,0.0f,1.0f},{0.0f,0.0f,0.0f}};
}

void SharedVrMenu::InvalidateAnchor()
{
    mAnchorValid=false;
}

void SharedVrMenu::SetActive(bool active)
{
    if(active!=mActive) InvalidateAnchor();
    mActive=active;
    if(!active) mRendering=false;
}

void SharedVrMenu::SetRendering(bool rendering,bool eyeActive,
                                const XrView views[2])
{
    mRendering=rendering&&mActive;
    if(mRendering&&!mAnchorValid&&eyeActive&&views)
    {
        mAnchor=SharedRender::CentreYawAnchor(views[0].pose,views[1].pose);
        mAnchorValid=true;
    }
}

bool SharedVrMenu::IsRendering() const
{
    return mRendering;
}

bool SharedVrMenu::GetProjection(bool eyeActive,const XrView& eye,
                                 int width,int height,rmt::Matrix* projection,
                                 int* targetWidth,int* targetHeight,
                                 bool requireRendering) const
{
    if(!projection||!targetWidth||!targetHeight||!eyeActive||!mAnchorValid||
       (requireRendering&&!mRendering)) return false;
    SharedRender::ComposeWorldLockedPanel(mAnchor,eye.pose,eye.fov,
                                          3.2f,3.2f,8.0f,projection);
    *targetWidth=width;
    *targetHeight=height;
    return true;
}

SharedVrMenu& GetSharedVrMenu()
{
    static SharedVrMenu menu;
    return menu;
}

SharedMoviePanel::SharedMoviePanel():mActive(false),mRendering(false),
    mAnchorValid(false),mAnchor{{0,0,0,1},{0,0,0}} {}
void SharedMoviePanel::Begin(){mActive=true;mRendering=false;mAnchorValid=false;}
void SharedMoviePanel::End(){mActive=false;mRendering=false;mAnchorValid=false;}
void SharedMoviePanel::InvalidateAnchor(){mAnchorValid=false;}
void SharedMoviePanel::SetRendering(bool rendering,bool eyeActive,const XrView views[2])
{
    mRendering=rendering&&mActive;
    if(mRendering&&!mAnchorValid&&eyeActive&&views)
    { mAnchor=SharedRender::CentreYawAnchor(views[0].pose,views[1].pose);mAnchorValid=true; }
}
bool SharedMoviePanel::GetProjection(bool eyeActive,const XrView& eye,int width,
    int height,rmt::Matrix* projection,int* targetWidth,int* targetHeight) const
{
    if(!mActive||!mAnchorValid||!eyeActive||!projection||!targetWidth||!targetHeight)return false;
    SharedRender::ComposeWorldLockedPanel(mAnchor,eye.pose,eye.fov,1.0f,1.0f,1.0f,projection);
    *targetWidth=width;*targetHeight=height;return true;
}
SharedMoviePanel& GetSharedMoviePanel(){static SharedMoviePanel panel;return panel;}
}

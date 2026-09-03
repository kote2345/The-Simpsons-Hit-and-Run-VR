// FeGroup.cpp
// Created by wng on Thu, May 11, 2000 @ 6:51 PM.
#include "stdafx.h"
#ifndef __FeGroup__
#include "FeGroup.h"
#endif
#include "tLinearTable.h"
#include "FePure3dObject.h"
#if defined(RAD_ANDROID)
#include <vr/openxrmanager.h>
#include <p3d/pure3d.hpp>
#endif

#if defined(RAD_ANDROID)
enum { VR_MISSION_HUD_GROUP_COUNT=19 };
enum { VR_HUD_GROUP_INSTANCES=8 };
static Scrooby::Group* gVrRadarGroup[VR_HUD_GROUP_INSTANCES]={NULL};
static Scrooby::Group* gVrMissionHudGroup[VR_MISSION_HUD_GROUP_COUNT][VR_HUD_GROUP_INSTANCES]={{NULL}};
void ScroobyDisplayVrRadarMap();
#if defined(SRR2_VR_RENDERER_VULKAN)
static void DisplayVrRadarModels(FeOwner* parent)
{
    for(int i=0;i<parent->GetChildrenCount();++i)
    {
        FeEntity* child=parent->GetChildIndex(i);
        FeDrawable* drawable=child&&child->IsDrawable()?static_cast<FeDrawable*>(child):NULL;
        if(!drawable||!drawable->IsVisible()) continue;
        FePure3dObject* model=dynamic_cast<FePure3dObject*>(child);
        FeOwner* nested=dynamic_cast<FeOwner*>(child);
        if(!model&&!nested) continue;
        p3d::stack->Push();
        p3d::stack->Multiply(*drawable->GetMatrix());
        float x=0.0f,y=0.0f;drawable->GetNormalizedPosition(x,y);
        p3d::stack->Translate(x,y,0.0f);
        const tColour original=drawable->GetColour();
        tColour modulated=original;
        drawable->ModulateColour(modulated,parent->GetColour());
        drawable->SetColour(modulated);
        if(model) model->Display(); else DisplayVrRadarModels(nested);
        drawable->SetColour(original);
        p3d::stack->Pop();
    }
}
#endif
void ScroobySetVrRadarGroup(Scrooby::Group* group)
{
    if(!group) return;
    for(unsigned i=0;i<VR_HUD_GROUP_INSTANCES;++i)
    {
        if(gVrRadarGroup[i]==group) return;
        if(!gVrRadarGroup[i]) {gVrRadarGroup[i]=group;return;}
    }
}
void ScroobySetVrMissionHudGroup(unsigned slot,Scrooby::Group* group)
{
    if(slot>=VR_MISSION_HUD_GROUP_COUNT || !group) return;
    for(unsigned i=0;i<VR_HUD_GROUP_INSTANCES;++i)
    {
        if(gVrMissionHudGroup[slot][i]==group) return;
        if(!gVrMissionHudGroup[slot][i])
        {
            gVrMissionHudGroup[slot][i]=group;
            SharOpenXR::ResetMissionHudSlot(slot);
            return;
        }
    }
}
#endif

FeGroup::FeGroup( const tName& name ) 
: 
    FeOwner( name ),
    m_offsetsComputed( false )
{
    //mHandle = Fe2DCore::GetInstance()->AddDummy( this );
}


FeGroup::~FeGroup()
{
#if defined(RAD_ANDROID)
    Scrooby::Group* self=static_cast<Scrooby::Group*>(this);
    for(unsigned i=0;i<VR_HUD_GROUP_INSTANCES;++i)
        if(gVrRadarGroup[i]==self) gVrRadarGroup[i]=NULL;
    for(unsigned slot=0;slot<VR_MISSION_HUD_GROUP_COUNT;++slot)
    {
        for(unsigned instance=0;instance<VR_HUD_GROUP_INSTANCES;++instance)
        {
            if(gVrMissionHudGroup[slot][instance]==self)
            {
                gVrMissionHudGroup[slot][instance]=NULL;
                SharOpenXR::ResetMissionHudSlot(slot);
            }
        }
    }
#endif
}

void FeGroup::Display()
{
#if defined(RAD_ANDROID)
#if defined(SRR2_VR_RENDERER_VULKAN)
    // A complete Hud page capture must traverse Scrooby exactly like the
    // working Android/GLES Original HUD.  Per-group capture here would be a
    // nested render target and is what reduced the result to isolated pieces
    // such as the coin counter.
    if(SharOpenXR::IsGameplayHudCaptureActive())
    {
        FeOwner::Display();
        return;
    }
#endif
    Scrooby::Group* self=static_cast<Scrooby::Group*>(this);
    bool isRadar=false;
    for(unsigned i=0;i<VR_HUD_GROUP_INSTANCES;++i)
        isRadar=isRadar||self==gVrRadarGroup[i];
#if defined(SRR2_VR_RENDERER_VULKAN)
    bool isLegacyHudGroup=false;
    for(unsigned slot=0;slot<VR_MISSION_HUD_GROUP_COUNT;++slot)
        for(unsigned instance=0;instance<VR_HUD_GROUP_INSTANCES;++instance)
            isLegacyHudGroup=isLegacyHudGroup||
                self==gVrMissionHudGroup[slot][instance];
    // Registered gameplay groups are rendered into their own Vulkan targets
    // below. They are still suppressed from the eye framebuffer by the
    // offscreen target, so this does not duplicate the legacy HUD.
#endif
#if !defined(SRR2_VR_RENDERER_VULKAN)
    // GLES keeps its proven direct Original-mode path. Vulkan uses the new
    // unified offscreen radar layer in both Original and spatial HUD modes.
    isRadar=isRadar&&SharOpenXR::IsSpatialHudEnabled();
#endif
    int missionSlot=-1;
    for(int slot=0;slot<VR_MISSION_HUD_GROUP_COUNT;++slot)
        for(unsigned instance=0;instance<VR_HUD_GROUP_INSTANCES;++instance)
            if(self==gVrMissionHudGroup[slot][instance]
#if !defined(SRR2_VR_RENDERER_VULKAN)
               && SharOpenXR::IsSpatialHudEnabled()
#endif
               ) missionSlot=slot;
    // The legacy GUI is stateful: on its second eye traversal Map0 can draw
    // again while Radar0 sprites are skipped. Capturing that incomplete pass
    // overwrites the shared texture and leaves the frame visible only in the
    // left eye. Capture the complete source group once, then EndEye presents
    // that same cached texture stereoscopically for both eyes.
    if((isRadar||missionSlot>=0) && SharOpenXR::IsRightEyeRendering())
        return;
    int radarXMin=0,radarYMin=0,radarXMax=640,radarYMax=480;
    if(isRadar) GetBoundingBox(radarXMin,radarYMin,radarXMax,radarYMax);
    if(missionSlot>=0) GetBoundingBox(radarXMin,radarYMin,radarXMax,radarYMax);
    if(missionSlot>=0)
        SharOpenXR::UpdateMissionHudLayout((unsigned)missionSlot,*GetMatrix());
    const bool captured=isRadar ? SharOpenXR::BeginRadarCapture(
        radarXMin,radarYMin,radarXMax,radarYMax) :
        (missionSlot>=0 && SharOpenXR::BeginMissionHudCapture(
            (unsigned)missionSlot,radarXMin,radarYMin,radarXMax,radarYMax));
#if defined(SRR2_VR_RENDERER_VULKAN)
    // A registered spatial group must never fall through to the current eye
    // framebuffer. If its mono capture cannot start this frame, retain the
    // previous wrist texture instead of drawing a head-locked duplicate.
    if((isRadar || missionSlot>=0) && SharOpenXR::IsSpatialHudEnabled() &&
       !captured)
        return;
#endif
    if(captured)
    {
        // BeginRadarCapture changes the framebuffer and enables the radar
        // projection override after PDDI has already submitted the normal HUD
        // projection. Force it into the currently bound shader before any of
        // this group's children are drawn.
        const pddiProjectionMode mode=p3d::pddi->GetProjectionMode();
        p3d::pddi->SetProjectionMode(mode);
    }
    // The legacy mission-overlay layout scales/translates the higher-indexed
    // overlay when (for example) the timer and a counter are visible at the
    // same time.  FeOwner has already put this group's matrix on the P3D
    // stack before entering Display().  Applying it to the timer capture while
    // cropping with the group's authored child bounds moves most of the timer
    // outside the sampled rectangle (the familiar thin strip at the bottom).
    //
    // Spatial HUD presentation performs its own non-overlapping stack layout,
    // so capture slot 2 in authored space.  Undo only the timer group's outer
    // layout matrix; child transforms, bitmap glyph animation and colour remain
    // untouched.  This also keeps the regular/original HUD path unchanged.
    bool timerCaptureMatrixUndone=false;
    if(captured && missionSlot==2 && SharOpenXR::IsSpatialHudEnabled())
    {
        rmt::Matrix inverseTimerLayout=*GetMatrix();
        inverseTimerLayout.Invert();
        float timerOriginX=0.0f;
        float timerOriginY=0.0f;
        GetNormalizedPosition(timerOriginX,timerOriginY);
        p3d::stack->Push();
        // The parent applied groupMatrix followed by the group's authored
        // origin. Conjugate the inverse around that origin so the resulting
        // stack retains the authored placement: M*T*(T^-1*M^-1*T) == T.
        p3d::stack->Translate(-timerOriginX,-timerOriginY,0.0f);
        p3d::stack->Multiply(inverseTimerLayout);
        p3d::stack->Translate(timerOriginX,timerOriginY,0.0f);
        timerCaptureMatrixUndone=true;
    }
#endif
#if defined(RAD_ANDROID) && defined(SRR2_VR_RENDERER_VULKAN)
    // Map0 and Hole0 are page siblings of HudMap0. Bring them into the same
    // target before traversing Radar0 so the spatial plane contains the exact
    // original map, depth mask, frame, markers and Hit & Run artwork.
    if(isRadar && captured) ScroobyDisplayVrRadarMap();
    FeOwner::Display();
#else
    FeOwner::Display();
#endif
#if defined(RAD_ANDROID)
    if(timerCaptureMatrixUndone)
        p3d::stack->Pop();
    if(captured)
    {
        if(isRadar) SharOpenXR::EndRadarCapture();
        else
        {
            SharOpenXR::EndMissionHudCapture();
        }
        // Re-submit the unchanged projection mode: the spatial radar matrix
        // was selected without changing this enum, so otherwise PDDI keeps it
        // cached and projects all following HUD groups off screen as well.
        const pddiProjectionMode mode=p3d::pddi->GetProjectionMode();
        p3d::pddi->SetProjectionMode(mode);
    }
#endif
}

//===========================================================================
// GetBoundingBox
//===========================================================================
// Description: gets the extents of the bounding box
//
// Constraints:    
//
// Parameters:    xMin - the smallext x value
//              yMin - the smallest y value
//              xMax - the largest x value
//              yMax - the largest y value
//
// Return:      NONE
//
//===========================================================================
void FeGroup::GetBoundingBox( int& xMin, int& yMin, int& xMax, int& yMax ) const
{
    bool initialized = false;
       int childrenCount = this->GetChildrenCount();
    for( int i = 0; i < childrenCount; i++ )
    {
        const FeEntity* child = this->GetChildIndex( i );
        const FeDrawable* drawable = dynamic_cast< const FeDrawable* >( child );
        if( drawable != NULL )
        {
            int dxMin;
            int dxMax;
            int dyMin;
            int dyMax;

            drawable->GetBoundingBox( dxMin, dyMin, dxMax, dyMax );

            if( initialized == false )
            {
                xMin = dxMin;
                xMax = dxMax;
                yMin = dyMin;
                yMax = dyMax;
                initialized = true;
            }
            else
            {
                if( dxMin < xMin )
                {
                    xMin = dxMin;
                }

                if( dxMax > xMax )
                {
                    xMax = dxMax;
                }

                if( dyMin < yMin )
                {
                    yMin = dyMin;
                }

                if( dyMax > yMax )
                {
                    yMax = dyMax;
                }
            }
        }
    }
    int x, y;
    this->GetOriginPosition( x,y);
    xMin += x;
    xMax += x;
    yMin += y;
    yMax += y;


}

void FeGroup::Show()
{
    //check if we need ot recompute the offsets of this item
    if( m_offsetsComputed == false )
    {
        RecomputeOffsets();
    }
    
    FeOwner::Show();
}

void FeGroup::GetBoundingBoxSize( int& width, int& height ) const
{
    int xMin;
    int xMax;
    int yMin;
    int yMax;
    this->GetBoundingBox( xMin, yMin, xMax, yMax );
    width = xMax - xMin;
    height = yMax - yMin;
}


//===========================================================================
// RecomputeOffsets
//===========================================================================
// Description: adjusts the positions of the bounding box, so that the 
//              children's positions will be relative to it
//
// Constraints:    
//
// Parameters:       width - the width is returned here
//                 height - the height is returned here
//
// Return:      
//
//===========================================================================
void FeGroup::RecomputeOffsets()
{
    //first find the minX and minY for all the children
    int minX = 65535;   //IAN IMPROVE: this is assumed to be larger than the posiiton
    int minY = 65535;   //of any of the drawables

    tLinearTable::RawIterator iter( mChildren );
    FeDrawable* drawable = dynamic_cast< FeDrawable* >( iter.First() );
    while ( drawable != NULL )
    {
        int drawableMinX;
        int drawableMaxX;
        int drawableMinY;
        int drawableMaxY;
        drawable->GetBoundingBox( drawableMinX, drawableMinY, drawableMaxX, drawableMaxY );
      
        if( drawableMinX < minX )
        {
            minX = drawableMinX;
        }

        if( drawableMinY < minY )
        {
            minY = drawableMinY;
        }
        drawable = dynamic_cast<FeDrawable*>( iter.Next() );
    }


    //go through and translate all the children down and left by xMin, yMin
    tLinearTable::RawIterator iter2( mChildren );
    drawable = dynamic_cast< FeDrawable* >( iter2.First() );
    while ( drawable )
    {
        drawable->TranslatePosition( -minX, -minY );
        drawable = dynamic_cast<FeDrawable*>( iter2.Next() );
    }

    this->SetPosition( minX, minY );

    m_offsetsComputed = true;
};

//===========================================================================
// ScaleAboutCenter
//===========================================================================
// Description: Scale uniformly about bounded drawable's center point.
//
// Constraints:    None
//
// Parameters:    None
//
// Return:      None
//
//===========================================================================
void FeGroup::ScaleAboutCenter( float factor )
{
    ScaleAboutCenter( factor, factor, 1.0f );
}

//===========================================================================
// ScaleAboutCenter
//===========================================================================
// Description: Scale about bounded drawable's center point.
//
// Constraints:    None
//
// Parameters:    None
//
// Return:      None
//
//===========================================================================
void FeGroup::ScaleAboutCenter( float factorX, float factorY, float factorZ )
{
    int xmin;
    int ymin;
    int xmax;
    int ymax;
    GetBoundingBox( xmin, ymin, xmax, ymax );
    int centerPosX = ( xmin + xmax ) / 2;
    int centerPosY = ( ymin + ymax ) / 2;

    // translate drawable to screen origin first
    Translate( -centerPosX, -centerPosY );

    // do the scaling
    Scale( factorX, factorY, factorZ );

    // translate drawable back to original position
    Translate( centerPosX, centerPosY );
}

//===========================================================================
// ScaleAboutPoint
//===========================================================================
// Description: Scale about a point on the bounded group
//
// Constraints:    None
//
// Parameters:    None
//
// Return:      None
//
//===========================================================================
void FeGroup::ScaleAboutPoint( float factor, const int x, const int y )
{
    int xmin;
    int ymin;
    int xmax;
    int ymax;
    GetBoundingBox( xmin, ymin, xmax, ymax );

    int pointX = xmin + x;
    int pointY = ymin + y;

    // translate drawable to screen origin first
    Translate( -pointX, -pointY );

    // do the scaling
    Scale( factor, factor, factor );

    // translate drawable back to original position
    Translate( pointX, pointY );
}

//===========================================================================
// RotateAboutCenter
//===========================================================================
// Description: Rotate about bounded drawable's center point.
//
// Constraints:    None
//
// Parameters:    None
//
// Return:      None
//
//===========================================================================
void FeGroup::RotateAboutCenter( float angle, rmt::Vector axis )
{
    int xmin = 0;
    int ymin = 0;
    int xmax = 0;
    int ymax = 0;
    GetBoundingBox( xmin, ymin, xmax, ymax );

    int centerPosX = ( xmin + xmax ) / 2;
    int centerPosY = ( ymin + ymax ) / 2;

    // translate drawable to screen origin first
    Translate( -centerPosX, -centerPosY );

    // do the rotation(s)
    //
    RotateArbitrary( axis.x, axis.y, axis.z, angle );

    // translate drawable back to original position
    Translate( centerPosX, centerPosY );
}


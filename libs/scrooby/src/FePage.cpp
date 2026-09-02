//===========================================================================
// Copyright (C) 2001 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   FePage
//
// Description: implementation of the FePage class. This class handles the
//              project and all associated screens
//
//===========================================================================
#ifndef __FePage__
#include "FePage.h"
#endif
#if defined(RAD_ANDROID)
#include <vr/openxrmanager.h>
#endif

//===========================================================================
// Includes
//===========================================================================
#include "FeApp.h"
#include "FeProject.h"
#include "FeLayer.h"
#include <raddebug.hpp>

#if defined(RAD_ANDROID)
static FePage* gVrGameplayHudPage=NULL;
void ScroobyDisplayVrRadarMap();
void ScroobySetVrGameplayHudPage(Scrooby::Page* page)
{
    gVrGameplayHudPage=dynamic_cast<FePage*>(page);
}
#endif
#include "tLinearTable.h"
#include "utility/debugMessages.h"

//===========================================================================
// Constructor
//===========================================================================
// Description: Constructor
//
// Constraints:    NONE
//
// Parameters:    NONE
//
// Return:      NONE
//
//===========================================================================
FePage::FePage( const char* fullFileName, FeProject* project ) 
: 
    FeOwner( fullFileName ),
    mProject( project )
{
    // Store the whole file name, e.g. "Page1.fePag"
    mFileName = fullFileName;

    radMemoryAllocator old = ::radMemorySetCurrentAllocator( RADMEMORY_ALLOC_TEMP );
        PascalCString pathParser( fullFileName );
        PascalCString fileName = pathParser.FullFilename();
    ::radMemorySetCurrentAllocator( old );
    FeEntity::SetName( fileName );
}

//===========================================================================
// Destructor
//===========================================================================
// Description: Destructor
//
// Constraints:    NONE
//
// Parameters:    NONE
//
// Return:      NONE
//
//===========================================================================
FePage::~FePage()
{
    for( int i = 0; i < mResources.Size(); i++ )
    {
        FeApp::GetInstance()->GetFeResourceManager().RemoveResource( mResources[i] );
    }
}


const char* FePage::GetFileName()
{
    return( static_cast<const char*>( mFileName ) );
}

//===========================================================================
// AddLayer
//===========================================================================
// Description: Adds a layer to a page
//
// Constraints:    NONE
//
// Parameters:    name - ASCII name of the page
//
// Return:      FeLayer* the layer we just added
//
//===========================================================================
FeLayer* FePage::AddLayer( const char* name )
{
    FeLayer* newLayer = new FeLayer( name );
    newLayer->SetParent( this );
    AddChild( newLayer );
    return newLayer;
}

void FePage::GetBoundingBoxSize( int& width, int& height ) const
{
    width = static_cast<int>(FeApp::GetInstance()->GetScreenWidth());
    height = static_cast<int>(FeApp::GetInstance()->GetScreenHeight());
}

void FePage::GetBoundingBox( int& xMin, int& yMin, int& xMax, int& yMax ) const
{
    xMin = 0;
    yMin = 0;
    xMax = static_cast<int>(FeApp::GetInstance()->GetScreenWidth()) - 1;
    yMax = static_cast<int>(FeApp::GetInstance()->GetScreenHeight()) - 1;
}


void FePage::Display()
{
#if defined(RAD_ANDROID)
    const bool gameplayHud=this==gVrGameplayHudPage;
    if(gameplayHud && SharOpenXR::IsRightEyeRendering()) return;
    // Capture Map0 on its own before entering the full HUD target.  A nested
    // radar capture is intentionally unsupported, and drawing Map0 directly
    // into the full target bypasses Hole0 and leaves a visible square behind
    // the later circular composite.
    // Prime Map0 before the authored HudMap0 overlay is traversed. This makes
    // the first spatial-HUD frame deterministic; previously Map0 was first
    // captured only after toggling modes had exercised the Original path.
    if(gameplayHud)
        ScroobyDisplayVrRadarMap();
    const bool gameplayHudCaptured=gameplayHud &&
        !SharOpenXR::IsSpatialHudEnabled() &&
        SharOpenXR::BeginGameplayHudCapture();
#endif
    FeOwner::Display();
#if defined(RAD_ANDROID)
    if(gameplayHudCaptured) SharOpenXR::EndGameplayHudCapture();
#endif
}

//gets a group object from within the page by hash
Scrooby::Group* FePage::GetGroup( const tUID hashValue )
{
    return FeParent::GetGroup( hashValue );
};

//gets a group object from within the page by name
Scrooby::Group* FePage::GetGroup( const char* name )
{
    return FeParent::GetGroup( name );
};

//gets a polygon object from within the page by hash
Scrooby::Polygon* FePage::GetPolygon( const tUID hashValue ) 
{
    return FeParent::GetPolygon( hashValue );
};

//gets a polygon object from within the page by name
Scrooby::Polygon* FePage::GetPolygon( const char* name )
{
    return FeParent::GetPolygon( name );
};

//gets a layer object from within the page by hash
Scrooby::Layer* FePage::GetLayer( const tUID hashValue )
{
    return FeParent::GetLayer( hashValue );
};

//gets a layer object from within the page by name
Scrooby::Layer* FePage::GetLayer( const char* name )
{
    return FeParent::GetLayer( name );
};

//===========================================================================
// GetLayerByIndex
//===========================================================================
// Description: gets a text object from within the page by name
//
// Constraints:    NONE
//
// Parameters:    index - int pointing to the layer we want to get
//
// Return:      Scrooby::Layer* thie layer we just got
//
//===========================================================================
Scrooby::Layer* FePage::GetLayerByIndex( const int index )
{
    FeEntity* thisItem = this->GetChildIndex( index );
    Scrooby::Layer* thisLayer = dynamic_cast< Scrooby::Layer* >( thisItem );
    rAssert( thisLayer );
    return thisLayer;
}


//gets a movie object from within the page by hash
Scrooby::Movie* FePage::GetMovie( const tUID hashValue ) 
{
    return FeParent::GetMovie( hashValue );
};

//gets a movie object from within the page by name
Scrooby::Movie* FePage::GetMovie( const char* name )
{
    return FeParent::GetMovie( name );
};

//===========================================================================
// GetNumberOfLayers
//===========================================================================
// Description: allows access to the number of layers stored in this page
//
// Constraints:    NONE
//
// Parameters:    NONE
//
// Return:      int - the total number of layers stored in this page
//
//===========================================================================
int FePage::GetNumberOfLayers() const
{
    return this->GetChildrenCount();
}

//gets a pure3d object from within the page by hash
Scrooby::Pure3dObject* FePage::GetPure3dObject( const tUID hashValue ) 
{
    return FeParent::GetPure3dObject( hashValue );
};

//gets a pure3d object from within the page by name
Scrooby::Pure3dObject* FePage::GetPure3dObject( const char* name )
{
    return FeParent::GetPure3dObject( name );
};

//===========================================================================
// IsPage
//===========================================================================
// Description: returns whether or not this object is a page - used instead
//              of dynamic cast
//
// Constraints:    NONE
//
// Parameters:    NONE
//
// Return:      bool - true or false is the object was a page
//
//===========================================================================
bool FePage::IsPage() const
{
    return true;
}

FeProject* FePage::Project()
{
    return mProject;
}

//===========================================================================
// SetAlpha
//===========================================================================
// Description: Sets the alpha value
//
// Constraints:    NONE
//
// Parameters:    NONE
//
// Return:      NONE
//
//===========================================================================
void FePage::SetAlpha( float a )
{
    //IAN IMPROVE: is this what we really want to do?
    //shouldn't this function be like a group?

    tLinearTable::RawIterator iter(mChildren);
    FeEntity* first = iter.First();
    FeLayer* layer = dynamic_cast< FeLayer* >( first );
    while (layer)
    {
        layer->SetAlpha( a );
        FeEntity* next = iter.Next();
        layer = dynamic_cast< FeLayer* >( next );
    }
}

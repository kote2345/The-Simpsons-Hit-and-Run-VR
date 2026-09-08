//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        p3dfilehandler.cpp
//
// Description: Implement P3DFileHandler
//
// History:     3/25/2002 + Created -- Darwin Chau
//
//=============================================================================

//========================================
// System Includes
//========================================
#include <string.h>
#include <stdio.h>
#include <string>
// Pure 3D
#include <p3d/utility.hpp>
// Foundation Tech
#include <raddebug.hpp>
#if defined(SRR2_OPENXR_PLATFORM_WIN32)
#include <SDL.h>
void pglSetBakedLightmapActive(bool active);
#endif

//========================================
// Project Includes
//========================================
#include <loading/p3dfilehandler.h>

//******************************************************************************
//
// Global Data, Local Data, Local Classes
//
//******************************************************************************

#if defined(SRR2_OPENXR_PLATFORM_WIN32)
namespace
{
bool FileExistsBesideExecutable( const std::string& relativePath )
{
    std::string nativePath = relativePath;
    for( std::string::size_type i = 0; i < nativePath.size(); ++i )
    {
        if( nativePath[i] == '\\' )
        {
            nativePath[i] = '/';
        }
    }

    char* basePath = SDL_GetBasePath();
    const std::string fullPath = basePath ? std::string( basePath ) + nativePath : nativePath;
    if( basePath )
    {
        SDL_free( basePath );
    }
    FILE* file = fopen( fullPath.c_str(), "rb" );
    if( !file )
    {
        return false;
    }
    fclose( file );
    return true;
}

std::string ResolveCustomLightmapLevel( const char* filename )
{
    const std::string source = filename ? filename : "";
    const std::string::size_type slash = source.find_last_of( "\\/" );
    const std::string leaf = slash == std::string::npos ? source : source.substr( slash + 1 );
    const std::string::size_type dot = leaf.find_last_of( '.' );
    if( dot == std::string::npos || leaf.substr( dot ) != ".p3d" )
    {
        return source;
    }

    const bool regionFile=leaf.size()==8 && (leaf[0]=='l'||leaf[0]=='L') &&
        leaf[1]>='1'&&leaf[1]<='7' && (leaf[2]=='r'||leaf[2]=='R') &&
        leaf[3]>='0'&&leaf[3]<='9';
    if(regionFile) pglSetBakedLightmapActive(false);

    // A baked atlas needs UV1 geometry as well as the image.  Keep the stock
    // art file immutable and opt into a generated companion only when present.
    const std::string custom = "custom\\lightmaps\\" + leaf.substr( 0, dot ) + ".lightmap.p3d";
    if( FileExistsBesideExecutable( custom ) )
    {
        if(regionFile) pglSetBakedLightmapActive(true);
        SDL_Log( "PCVR lightmap override: %s -> %s", source.c_str(), custom.c_str() );
        return custom;
    }
    return source;
}
}
#endif

//******************************************************************************
//
// Public Member Functions
//
//******************************************************************************

//==============================================================================
// P3DFileHandler::P3DFileHandler
//==============================================================================
// Description: Constructor.
//
// Parameters: None.
//
// Return:      N/A.
//
//==============================================================================
P3DFileHandler::P3DFileHandler() : m_RefCount( 0 )
{
}

//==============================================================================
// P3DFileHandler::~P3DFileHandler
//==============================================================================
// Description: Destructor.
//
// Parameters: None.
//
// Return:      N/A.
//
//==============================================================================
P3DFileHandler::~P3DFileHandler()
{
}


//==============================================================================
// P3DFileHandler::LoadFile 
//==============================================================================
//
// Description: Load a Pure3D file asynchronously.
//
// Parameters:  filename - fully qualified path and filename
//              pCallback - client callback to invoke when load is complete
//              pUserData - optional client supplied user data
//
// Return:      None.
//
//==============================================================================
void P3DFileHandler::LoadFile 
(
    const char* filename, 
    FileHandler::LoadFileCallback* pCallback,
    void* pUserData,
    GameMemoryAllocator heap
)
{
    rAssert( filename );
    rAssert( pCallback );

    mpCallback = pCallback;
    mpUserData = pUserData;

    //
    // Ensure that the specified inventory section exists before loading
    //
    HeapMgr()->PushHeap (heap);
    HeapMgr()->PushHeap (GMA_TEMP);
    p3d::inventory->AddSection( mcSectionName );
    HeapMgr()->PopHeap (GMA_TEMP);
#if defined(SRR2_OPENXR_PLATFORM_WIN32)
    const std::string resolvedFilename = ResolveCustomLightmapLevel( filename );
    p3d::loadAsync( resolvedFilename.c_str(), mcSectionName, this, pUserData, heap );
#else
    p3d::loadAsync( filename, mcSectionName, this, pUserData, heap );
#endif
    HeapMgr()->PopHeap (heap);
}


//==============================================================================
// P3DFileHandler::Done
//==============================================================================
//
// Description: Pure3D (via FTech) invokes this callback when the async load
//              is complete.
//
// Parameters:  tLoadStatus status, tLoadRequest *load
//
// Return:      None.
//
//==============================================================================
void P3DFileHandler::Done( tLoadStatus status, tLoadRequest *load )
{
#if defined(SRR2_OPENXR_PLATFORM_WIN32)
    SDL_Log("PCVR P3D callback: status=%d file=%s",static_cast<int>(status),
            load && load->GetFilename()?load->GetFilename():"<unknown>");
#endif
    //
    // Percolate the callback up to the client.
    //
    mpCallback->OnLoadFileComplete( mpUserData );
}


//==============================================================================
// P3DFileHandler::LoadFileSync 
//==============================================================================
//
// Description: Load a Pure3D file synchronously.
//
// Parameters:  filename - fully qualified path and filename
//
// Return:      None.
//
//==============================================================================
void P3DFileHandler::LoadFileSync( const char* filename )
{
    rAssert( filename );

    rReleasePrintf("Synchronous File Load. Bastard! %s\n", filename);
    rAssert( false );

    p3d::inventory->PushSection();

    //
    // Ensure that the specified inventory section exists before loading
    //
    p3d::inventory->AddSection( mcSectionName );
    p3d::inventory->SelectSection( mcSectionName );
    
    // SetInventorySection(mcSectionName );

#if defined(SRR2_OPENXR_PLATFORM_WIN32)
    const std::string resolvedFilename = ResolveCustomLightmapLevel( filename );
    tLoadRequest* pLR = new tLoadRequest(p3d::openFile(resolvedFilename.c_str()));
#else
    tLoadRequest* pLR = new tLoadRequest(p3d::openFile(filename));
#endif
    pLR->SetInventorySection(mcSectionName);
    tLoadStatus result = p3d::loadManager->Load(pLR);

    // bool result = p3d::load( filename );

    p3d::inventory->PopSection();

    rAssert( result == LOAD_OK );
}



//==============================================================================
// P3DFileHandler::SetSectionName
//==============================================================================
//
// Description: 
//
// Parameters:  
//
// Return:      
//
//==============================================================================
void P3DFileHandler::SetSectionName( const char* sectionName )
{
    rAssert( sectionName );

    strcpy( mcSectionName, sectionName );
}

//******************************************************************************
//
// Private Member Functions
//
//******************************************************************************







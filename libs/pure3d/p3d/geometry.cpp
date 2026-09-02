//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include <p3d/geometry.hpp>
#include <p3d/vertexlist.hpp>
#include <p3d/primgroup.hpp>
#include <p3d/chunkfile.hpp>
#include <p3d/file.hpp>
#include <constants/chunks.h>
#include <p3d/utility.hpp>
#include <pddi/pddiext.hpp>
#include <constants/chunkids.hpp>
#include <p3d/array.hpp>

#include <string.h>


using namespace RadicalMathLibrary;

static bool gLegacyGroundShadowsSuppressed = false;

void p3dSetLegacyGroundShadowsSuppressed(bool suppressed)
{
    gLegacyGroundShadowsSuppressed = suppressed;
}

bool p3dAreLegacyGroundShadowsSuppressed()
{
    return gLegacyGroundShadowsSuppressed;
}

//------------------------------------------------------------------------
tGeometry::tGeometry(int nPG) :
    //nFaceNormal(0),
    //faceNormal(NULL),
    primGroup( 0 )
{
    P3DASSERT(p3d::pddi);
    sphere.centre.x = 0.0f;
    sphere.centre.y = 0.0f;
    sphere.centre.z = 0.0f;
    sphere.radius = 1.0f;
    box.low.x = 0.0f;
    box.low.y = 0.0f;
    box.low.z = 0.0f;
    box.high.x = 0.0f;
    box.high.y = 0.0f;
    box.high.z = 0.0f;

    primGroup.SetSize( nPG );
}

//------------------------------------------------------------------------
tGeometry::~tGeometry()
{
//    delete[] faceNormal;


    for(unsigned i = 0; i < primGroup.Size(); i++)
    {
        tRefCounted::Release( primGroup[i] );
    }

}

//------------------------------------------------------------------------
tShader* tGeometry::GetShader(int i)
{ 
    if( primGroup[i])
        return primGroup[i]->GetShader(); 

    return NULL;
}

void tGeometry::SetShader(int i, tShader* shader) 
{ 
    if( primGroup[i] )
        primGroup[i]->SetShader(shader); 
}

void tGeometry::ProcessShaders(ShaderCallback& callback)
{
    for( unsigned i = 0; i < primGroup.Size(); i++)
    {
        tShader* shader = primGroup[i]->GetShader();
        tShader* newShader = callback.Process(shader);
        if(newShader != shader)
        {
            primGroup[i]->SetShader(newShader);
        }
    }
}

void tGeometry::SetPrimGroup(int i, tPrimGroup* group)
{
    tRefCounted::Assign(primGroup[i], group);
}

//------------------------------------------------------------------------
void tGeometry::Display()
{
    // Unlike the explicit simple-shadow pass, authored tree shadows can also
    // be reached as a normal drawable.  Dropping the geometry here removes
    // both its visible fake projection and the square it would write into a
    // CSM cascade.
    const tUID uid=GetUID();
    if(gLegacyGroundShadowsSuppressed &&
       (uid==tName::MakeUID("treeshadowbig") ||
        uid==tName::MakeUID("treeshadowsmall") ||
        uid==tName::MakeUID("treeshadoweversmallShape") ||
        uid==tName::MakeUID("deadtree_shadowShape") ||
        uid==tName::MakeUID("l5_streetlamp_lightpoolShape")))
        return;

    for( unsigned i = 0; i < primGroup.Size(); i++)
    {
        if( primGroup[i])
        {
            primGroup[i]->Display();
        }
    }
}
    
//------------------------------------------------------------------------
void tGeometry::SetBoundingBox(float x1, float y1, float z1,
                                         float x2, float y2, float z2)
{
    box.low.x = x1;
    box.low.y = y1;
    box.low.z = z1;
    box.high.x = x2;
    box.high.y = y2;
    box.high.z = z2;
}

//------------------------------------------------------------------------
void tGeometry::SetBoundingSphere(float centerx, float centery, float centerz,
                                             float radius)
{
    sphere.centre.x =  centerx;
    sphere.centre.y =  centery;
    sphere.centre.z =  centerz;
    sphere.radius   =  radius;
}

//------------------------------------------------------------------------
tGeometryLoader::tGeometryLoader() : 
    tSimpleChunkHandler(Pure3D::Mesh::MESH), 
    mEnableFaceNormals(false), 
    mOptimize(true), 
    mVertexMask(0xffffffff) { /**/ }


//------------------------------------------------------------------------
tGeometry* tGeometryLoader::Allocate(int nPrimGroup)
{
    return new tGeometry(nPrimGroup);
}

static const int GEOMETRY_VERSION = 0;
static const int GEOMETRY_NONOPTIMIZE_VERSION = 1;      //temporarily used

//------------------------------------------------------------------------
tEntity* tGeometryLoader::LoadObject(tChunkFile* f, tEntityStore* store)
{ 
    char name[255];
    f->GetPString(name);

    int version = f->GetLong();
    P3DASSERT(version == GEOMETRY_VERSION || version == GEOMETRY_NONOPTIMIZE_VERSION );

    bool bOptimized = ( version != GEOMETRY_NONOPTIMIZE_VERSION );

    int nPrimGroup = f->GetLong();
    tGeometry *geo = Allocate(nPrimGroup);
    geo->SetName(name);

    int primGroupCount = 0;

    while(f->ChunksRemaining())
    {      
        f->BeginChunk();
        switch(f->GetCurrentID())
        {
        
            case Pure3D::Mesh::PRIMGROUP:
            {
                P3DASSERT(primGroupCount < nPrimGroup);
                tPrimGroupLoader pgLoader;
                pgLoader.SetVertexFormatMask(mVertexMask);
                tPrimGroup *pg = pgLoader.Load(f, store, NULL, mOptimize && bOptimized, false);
                geo->SetPrimGroup( primGroupCount, pg );
                ++primGroupCount;
                
                break;
            }

/*****
            case P3D_FACE_NORMAL_LIST:
            {
                if(enableFaceNormals)
                {
                    geo->nFaceNormal = f->GetLong();
                    geo->faceNormal = new rmt::Vector[geo->nFaceNormal];
                    f->GetData(geo->faceNormal, geo->nFaceNormal * 3, tFile::DWORD);
                }
                break;
            }
*****/
            case Pure3D::Mesh::BOX:
            {
                float minx = f->GetFloat();
                float miny = f->GetFloat();
                float minz = f->GetFloat();
                float maxx = f->GetFloat();
                float maxy = f->GetFloat();
                float maxz = f->GetFloat();

                geo->SetBoundingBox( minx, miny, minz, maxx, maxy, maxz);
                break;
            }

            case Pure3D::Mesh::SPHERE:
            {
                float cx = f->GetFloat();
                float cy = f->GetFloat();
                float cz = f->GetFloat();
                float r  = f->GetFloat();

                geo->SetBoundingSphere(cx,cy,cz,r);
                break;
            }

            case Pure3D::Mesh::RENDERSTATUS:
            {
                geo->SetCastsShadow(!(f->GetLong()));
                break;
            }

            default:
                break;
        } // switch
        f->EndChunk();
    } // while

    return geo;
}


/*===========================================================================
    polyskin.cpp
    30-Nov-99 Created by Neall
    12-Nov-00 Revamped for R14

    Copyright (c)1999, 2000 Radical Entertainment, Inc.
    All rights reserved.
===========================================================================*/

#include <p3d/anim/polyskin.hpp>
#include <p3d/shader.hpp>
#include <p3d/vertexlist.hpp>
#include <p3d/primgroup.hpp>
#include <constants/chunkids.hpp>
#include <p3d/chunkfile.hpp>
#include <p3d/utility.hpp>
#include <p3d/anim/skeleton.hpp>
#include <p3d/matrixstack.hpp>
#include <radmath/radmath.hpp>
#include <pddi/pddiext.hpp>
#include <p3d/anim/instancedgeometry.hpp>
#if defined(RAD_ANDROID) || defined(SRR2_OPENXR_PLATFORM_WIN32)
#include <vr/openxrmanager.h>
#include <radtime.hpp>
#endif

#include <string.h>

using namespace RadicalMathLibrary;

static rmt::Matrix s_SkinMatrices[256]
#ifdef RAD_PS2
__attribute__((aligned(16)))
#endif
;

tPolySkin::tPolySkin(int npg) :
    boneMatrix(NULL),
    primGroup(npg),
    exprAnimOffsets(NULL)
{
    boundingSphere.centre.Set(0.0f, 0.0f, 0.0f);
    boundingSphere.radius = 1.0f;
    boundingBox.low.Set(0,0,0);
    boundingBox.high.Set(1,1,1);
}

tPolySkin::~tPolySkin()
{
//    delete[] boneMatrix;

    for(unsigned i=0; i < primGroup.Size(); i++)
    {
        primGroup[i]->Release();
    }
    
    tRefCounted::Release(skeleton);

    if(exprAnimOffsets)
    {
        exprAnimOffsets->Release();
    }
}


void tPolySkin::Display(tPose* p)
{
    if (p == NULL)
    {
        return;
    }
#if defined(RAD_ANDROID) || defined(SRR2_OPENXR_PLATFORM_WIN32)
    const radTime64 vrSkinStart=radTimeGetMicroseconds64();
#endif

    // transform the pose joints into world space
    p->Evaluate();

    // The skin vertices are stored in rest-pose relative world space.
    // The below matrix will transform from this space, into bone-local
    // space where the animation is applied, and then back into camera space.
    for(int i = 0; i < p->GetNumJoint(); i++)
    {
#ifndef RAD_PS2
        boneMatrix[i].Mult(skeleton->GetJoint(i)->inverseWorldMatrix, p->GetJoint(i)->worldMatrix);
#else
        boneMatrix[i].MultAligned(skeleton->GetJoint(i)->inverseWorldMatrix, p->GetJoint(i)->worldMatrix);
#endif
    }

    boneMatrix[skeleton->GetNumJoint()].Identity();

    pddiExtHardwareSkinning* hwSkin = p3d::context->GetHardwareSkinning();

    if(hwSkin)
    {
        hwSkin->Begin();
    }

    for(unsigned j=0; j < primGroup.Size(); j++)
    {
        primGroup[j]->Display();
    }

    if(hwSkin)
    {
        hwSkin->End();
    }
#if defined(RAD_ANDROID) || defined(SRR2_OPENXR_PLATFORM_WIN32)
    SharOpenXR::RecordRenderSection(12,
        (radTimeGetMicroseconds64()-vrSkinStart)/1000.0);
#endif
}

void tPolySkin::DisplayInstanced(tPose* p, unsigned count)
{
    P3DASSERT(p);

    for(int i = 0; i < p->GetNumJoint(); i++)
    {
        boneMatrix[i] = p->GetJoint(i)->worldMatrix;
    }

    pddiExtHardwareSkinning* hwSkin = p3d::context->GetHardwareSkinning();

    if(hwSkin)
    {
        hwSkin->Begin();
    }

    for(unsigned j=0; j < primGroup.Size(); j++)
    {
        primGroup[j]->DisplayInstanced(count);
    }

    if(hwSkin)
    {
        hwSkin->End();
    }
}


void tPolySkin::ProcessShaders(ShaderCallback& callback)
{
    for(unsigned i = 0; i < primGroup.Size(); i++)
    {
        tShader* shader = primGroup[i]->GetShader();
        tShader* newShader = callback.Process(shader);
        if(newShader != shader)
        {
            primGroup[i]->SetShader(newShader);
        }
    }
}

//-------------------------------------------------------------------
static const int POLYSKIN_VERSION = 3;
static const int POLYSKIN_NONOPTIMISED_VERSION = 4;

tPolySkinLoader::tPolySkinLoader() : tSimpleChunkHandler(Pure3D::Mesh::SKIN), optimise(true) 
{
};

tEntity* tPolySkinLoader::LoadObject(tChunkFile* f, tEntityStore* store)
{
    char name[128];
    f->GetPString(name);
    
    int version = f->GetLong();
    P3DASSERT(version == POLYSKIN_VERSION || version == POLYSKIN_NONOPTIMISED_VERSION );

    optimise = version == POLYSKIN_VERSION;

    char skelName[128];
    f->GetPString(skelName);
    int nPrimGroup = f->GetLong();

    tPolySkin *skin = new tPolySkin(nPrimGroup);
    skin->SetName(name);

    skin->skeleton = p3d::find<tSkeleton>(store, skelName);
    if(skin->skeleton)
    {
        skin->skeleton->AddRef();

        P3DASSERT(skin->skeleton->GetNumJoint() < 256);

        // allocate one extra bone for the special identity matrix
        skin->boneMatrix = s_SkinMatrices;//new rmt::Matrix[skin->skeleton->GetNumJoint()+1];
    }
    int index = 0;
    bool deformed = false;

    while(f->ChunksRemaining())
    {
        f->BeginChunk();
        switch(f->GetCurrentID())
        {
            case Pure3D::Mesh::PRIMGROUP:
            {
                P3DASSERT(index < nPrimGroup);
                tPrimGroupLoader pgLoader;                
                skin->primGroup[index] = pgLoader.Load(f, store, skin->boneMatrix, optimise, true);
                skin->primGroup[index]->AddRef();
                index++;
            }
            break;

            case Pure3D::Mesh::SPHERE:
                skin->boundingSphere.centre.x = f->GetFloat();
                skin->boundingSphere.centre.y = f->GetFloat();
                skin->boundingSphere.centre.z = f->GetFloat();
                skin->boundingSphere.radius = f->GetFloat();
            break;

            case Pure3D::Mesh::BOX:
                skin->boundingBox.low.x = f->GetFloat();
                skin->boundingBox.low.y = f->GetFloat();
                skin->boundingBox.low.z = f->GetFloat();
                skin->boundingBox.high.x = f->GetFloat();
                skin->boundingBox.high.y = f->GetFloat();
                skin->boundingBox.high.z = f->GetFloat();
            break;

            case Pure3D::Mesh::EXPRESSIONOFFSETS:
            {
                skin->exprAnimOffsets = dynamic_cast<tExpressionOffsets*> ( offsetLoader.LoadObject(f, store) );
                if(skin->exprAnimOffsets)
                {
                    (skin->exprAnimOffsets)->AddRef();
                }
                deformed = true;
            }
            break;

            default:
            break;
        }
        f->EndChunk();
    }

    if(!deformed)
    {
        for(int i = 0; i < nPrimGroup; i++)
        {
            skin->primGroup[i]->ReleaseTempVertices();
        }
    }

#if defined(RAD_WIN32) || defined(RAD_XBOX)
    if( skin->primGroup[0]->GetInstanceCount())
    {
        tInstancedGeometry* inst = new tInstancedGeometry(skin);
        inst->SetName(name);
        char str[256];
        strcpy(str,name);
        strcat(str, "_inst");
        skin->SetName(str);
        return inst;
    }
#endif

    return skin;
}

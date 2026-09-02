//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef _GEOMETRY_HPP
#define _GEOMETRY_HPP

#include <p3d/drawable.hpp>
#include <p3d/loadmanager.hpp>
#include <p3d/array.hpp>

class pddiPrimBuffer;

class tGeometryLoader;
class tVertexList;
class tPrimGroup;
class tShader;

// Legacy projected-shadow meshes are ordinary geometries in several old
// world assets.  CSM must be able to suppress them in both the colour and
// depth passes.
void p3dSetLegacyGroundShadowsSuppressed(bool suppressed);
bool p3dAreLegacyGroundShadowsSuppressed();

class tGeometry : public tDrawable
{
public:
    tGeometry(int nPrimGroups);

    virtual void Display();

    virtual void GetBoundingBox(rmt::Box3D* b)          { *b = box; }
    virtual void GetBoundingSphere(rmt::Sphere* s)      { *s = sphere; }
    virtual void SetBoundingBox(float x1, float y1, float z1,
                                         float x2, float y2, float z2);
    virtual void SetBoundingSphere(float x, float y, float z, float radius);

    virtual int GetNumShader()                      { return primGroup.Size( ); }
    virtual tShader* GetShader(int i);
    virtual void SetShader(int i, tShader* shader);

    virtual int GetNumPrimGroup()                   { return primGroup.Size( ); } 
    virtual tPrimGroup* GetPrimGroup(int i)         { return primGroup[i];}
    virtual void SetPrimGroup(int i, tPrimGroup* group);

    virtual int GetNumFaceNormals()                 { return 0; }
    virtual rmt::Vector* GetFaceNormals()           { return NULL; }

    virtual void ProcessShaders(ShaderCallback&);

    virtual int  CastsShadow()                       { return mCastsShadow; }
    virtual void SetCastsShadow(int iCastsShadow)    { mCastsShadow = iCastsShadow; }                

protected:
    virtual ~tGeometry();
    tGeometry(const tGeometry&) { /**/ }

    // the loader can fiddle with the internal data
    friend class tGeometryLoader;
    friend class tGeometryLoaderV13;

    rmt::Box3D  box;
    rmt::Sphere sphere;

    int mCastsShadow;
    //int nFaceNormal;
    //rmt::Vector* faceNormal;
    tPtrArray<tPrimGroup *> primGroup;
};

//-------------------------------------------------------------------
class tGeometryLoader : public tSimpleChunkHandler
{
public:
    tGeometryLoader();
    void SetEnableFaceNormals(bool enable) { mEnableFaceNormals = enable; }
    bool GetEnableFaceNormals(void ) { return mEnableFaceNormals; }

    void SetOptimize(bool enable) { mOptimize = enable; }
    bool GetOptimize(void )       { return mOptimize;   }

    void SetVertexMask(unsigned v) { mVertexMask = v; }
    unsigned GetVertexMask(void )  { return mVertexMask; }

    virtual tEntity* LoadObject(tChunkFile*, tEntityStore* store);
protected:
    virtual tGeometry* Allocate(int nPrimGroup);

    bool mEnableFaceNormals;
    bool mOptimize;
    unsigned mVertexMask;
};


#endif /* _GEOMETRY_HPP */


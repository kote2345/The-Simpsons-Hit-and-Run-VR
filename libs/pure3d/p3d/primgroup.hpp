//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef _PRIMGROUP_HPP_
#define _PRIMGROUP_HPP_

#include <p3d/p3dtypes.hpp>
#include <p3d/refcounted.hpp>
#include <pddi/pddienum.hpp>
#include <radmath/radmath.hpp>

class tPrimGroupLoader;
class tEntityStore;
class tPrimGroupStreamed;
class tVertexList;
class tShader;
class tSkeleton;

class pddiPrimBuffer;
class pddiPrimStream;
class pddiPrimBufferStream;

void p3dSetVrVehicleDriverSuppressed(bool suppressed);
void p3dSetCsmOpaqueReceiverOnly(bool enabled);
void p3dSetCsmIntegratedVehicleReceiver(bool enabled);
void p3dSetEnhancedWorldMaterials(bool enabled);
void p3dSetEnhancedCharacterMaterials(bool enabled);
void p3dSetEnhancedSunDirection(const rmt::Vector& direction);
void p3dSetEnhancedVehicleMaterials(bool enabled);

//-------------------------------------------------------------------
class tPrimGroup : public tRefCounted
{
public:
    tPrimGroup(int nVertex);
    virtual ~tPrimGroup();

    virtual void Display() = 0;

    void SetShader(tShader *);
    tShader *GetShader()            { return mShader; }

    pddiPrimType GetPrimType()      { return mPrimType; }
    void SetPrimType(pddiPrimType pt) { mPrimType = pt; }

    void         SetVertexFormat(unsigned f) { mVertexFormat = f; }
    unsigned int GetVertexFormat()  { return mVertexFormat; }

    unsigned int GetVertexCount(void) const { return mVertexCount; }
    virtual bool GetVertices(unsigned start, unsigned count, rmt::Vector*) {return false;}
    virtual bool SetVertices(unsigned start, unsigned count, rmt::Vector*) {return false;}
    virtual void ReleaseTempVertices(void) {}

    unsigned GetInstanceCount(void) { return instanceCount;}
    virtual void DisplayInstanced(unsigned) {};

protected:
    tShader     *mShader;
    pddiPrimType mPrimType;
    unsigned int mVertexFormat;
    unsigned int mVertexCount;

    unsigned instanceCount;
    unsigned instanceSize;
};

//-------------------------------------------------------------------
class tPrimGroupOptimised : public tPrimGroup
{
public:

    tPrimGroupOptimised(int nVerts);
    tPrimGroupOptimised(pddiPrimBuffer *buffer, int nVerts);
    tPrimGroupOptimised(tPrimGroupStreamed &pgStreamed, unsigned optimisationHint = 0, bool writeOnce = true);
    virtual ~tPrimGroupOptimised();

    virtual void Display();

    void SetBuffer(pddiPrimBuffer *buffer);
    pddiPrimBuffer *GetBuffer() { return mBuffer; }

    bool GetVertices(unsigned start, unsigned count, rmt::Vector*);
    bool SetVertices(unsigned start, unsigned count, rmt::Vector*);
    virtual void ReleaseTempVertices(void);

protected:
    friend class tPrimGroupLoader;

    pddiPrimBuffer *mBuffer;
    rmt::Vector* tempPositions;
};

class tPrimGroupSkinnedOptimised : public tPrimGroupOptimised
{
public:
    tPrimGroupSkinnedOptimised(int nVertex);
    virtual ~tPrimGroupSkinnedOptimised();

    virtual void Display();
    virtual void DisplayInstanced(unsigned);

protected:
    friend class tPrimGroupLoader;

    unsigned nMatrices;
    rmt::Matrix** matrixPalette;   
};



//-------------------------------------------------------------------
class tPrimGroupStreamed : public tPrimGroup
{
public:
    tPrimGroupStreamed(int nVertex, unsigned format, int nIndex, bool allocate = false);
    virtual ~tPrimGroupStreamed();

    virtual void Display();

    void Display(pddiPrimStream *stream);
    void Display(pddiPrimBufferStream *stream);

    void SetVertexFormat(unsigned f) { mVertexFormat = f;}

    int GetNumVertex() const;

    tVertexList *GetVertexList() const  { return mVertexList; }
    void SetVertexList(tVertexList *vl);

    unsigned int GetNumIndices() const     { return mIndexCount; }
    unsigned short *GetIndices() const  { return mIndices; }
    
protected:
    friend class tPrimGroupLoader;

    int mIndexCount;
    
    unsigned short *mIndices;
    tVertexList    *mVertexList;

#ifdef RAD_XBOX
    unsigned vertexProgram;
#endif

    void IndexedDisplay(pddiPrimStream *stream);
    void IndexedDisplay(pddiPrimBufferStream *stream);
    void NonIndexedDisplay(pddiPrimStream *stream);
    void NonIndexedDisplay(pddiPrimBufferStream *stream);
};

//-------------------------------------------------------------------
class tPrimGroupSkinnedStreamed : public tPrimGroupStreamed
{
public:

    tPrimGroupSkinnedStreamed(int nVertex, unsigned format, int nIndex, bool allocate = false);
    virtual ~tPrimGroupSkinnedStreamed();

    virtual void Display();
    virtual void DisplayInstanced(unsigned);

    struct SkinVertex
    {
        float         weights[3];
        unsigned char indices[4];
        rmt::Vector   normal;
        rmt::Vector   position;
    };

    bool GetVertices(unsigned start, unsigned count, rmt::Vector*);
    bool SetVertices(unsigned start, unsigned count, rmt::Vector*);
    
protected:
    friend class tPrimGroupLoader;

    unsigned nMatrices;
    rmt::Matrix** matrixPalette;   

    SkinVertex* mVertices;
};

//-------------------------------------------------------------------
//		skin with software blending but buffered rendering
//-------------------------------------------------------------------
#ifndef RAD_PS2
class tPrimGroupSkinnedPC : public tPrimGroupSkinnedOptimised
{
    tPrimGroupSkinnedPC(int nVertex, unsigned format, int nIndex, bool allocate = false);
    virtual ~tPrimGroupSkinnedPC();

    virtual void Display();
    virtual void DisplayInstanced(unsigned);

    struct SkinVertex
    {
        float         weights[3];
        unsigned char indices[4];
        rmt::Vector   normal;
        rmt::Vector   position;
    };
    
    bool GetVertices(unsigned start, unsigned count, rmt::Vector*);
    bool SetVertices(unsigned start, unsigned count, rmt::Vector*);

protected:
	
    friend class tPrimGroupLoader;	
    SkinVertex* mVertices;
    // GLES has no hardware-skinning extension.  Keep the palette used for the
    // current VBO so a second render pass (notably the near CSM pass) can draw
    // the already-skinned vertices instead of doing the same CPU work again.
    rmt::Matrix* mCachedMatrixPalette;
    unsigned mCachedMatrixCount;
    bool mSkinCacheValid;
};
#endif
//-------------------------------------------------------------------
class tChunkFile;

class tPrimGroupLoader
{
public:
    tPrimGroupLoader(void);
    virtual ~tPrimGroupLoader();

    virtual tPrimGroup*          Load(tChunkFile *file, tEntityStore *store, rmt::Matrix* bones, bool optimise, bool deform);

    void SetHomerThumbRepair(bool enabled, tSkeleton* skeleton)
    { mHomerThumbRepair=enabled; mHomerHandSkeleton=enabled?skeleton:NULL; }

    void SetVertexFormatMask(unsigned int m) { mVertexFormatMask = m; }

private:
    bool mHomerThumbRepair=false;
    tSkeleton* mHomerHandSkeleton=NULL;
    virtual tPrimGroup*          LoadStreamed(tChunkFile* f, tEntityStore* store, rmt::Matrix* bones);

    #ifndef RAD_PS2
	    virtual tPrimGroup*          LoadPCSkin(tChunkFile* f, tEntityStore* store, rmt::Matrix* bones);
    #endif

    bool Load(tChunkFile *f, tEntityStore *store);

    bool ParseHeader(tChunkFile *f, tEntityStore *store);

    tShader *mShader;
    pddiPrimType mPrimType;
    int mVertexFormat;
    int mVertexCount;
    int mIndexCount;
    int mMatrixCount;

    unsigned mVertexFormatMask;

};

#endif


//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include <p3d/primgroup.hpp>
#include <p3d/utility.hpp>
#include <p3d/shader.hpp>
#include <p3d/vertexlist.hpp>
#include <p3d/geometry.hpp>
#include <constants/chunks.h>
#include <constants/chunkids.hpp>
#include <p3d/chunkfile.hpp>
#include <p3d/file.hpp>
#include <pddi/pddiext.hpp>
#include <p3d/anim/vertexanimkey.hpp>
#include <pddi/base/baseshader.hpp>

#ifdef RAD_PS2
    #include <pddi/ps2/shaders/ps2refractionshader.hpp>
#endif

#include <string.h>
#if defined(RAD_ANDROID)
#include <vr/csmbridge.h>
void pglSetEnhancedMaterialMode(int mode);
void pglSetEnhancedSunDirection(float x,float y,float z);
#endif


#define INDEXED_STREAM_COLOUR_WITH_OFFSET(  i, color, offset )  \
                    int r, g, b, a; \
                    r = color[ indices[ i ] ].Red( ) + offset[ indices[ i ] ].red;  \
                    g = color[ indices[ i ] ].Green( ) + offset[ indices[ i ]].green;   \
                    b = color[ indices[ i ] ].Blue( ) + offset[ indices[ i ] ].blue;    \
                    a = color[ indices[ i ] ].Alpha( ) + offset[ indices[ i ] ].alpha;  \
                    colour.Set( r, g, b, a );   \
                    stream->Colour( colour );

#define INDEXED_STREAM_POSITION_WITH_OFFSET( i, position, offset )  \
                    stream->Coord( position[ indices[ i ] ].x + offset[ indices[ i ] ].x, position[ indices[ i ] ].y + offset[ indices[ i ] ].y, position[ indices[ i ] ].z + offset[ indices[ i ] ].z );

#define INDEXED_STREAM_NORMAL_WITH_OFFSET( i, normal, offset )  \
                    stream->Normal( normal[ indices[ i ] ].x + offset[ indices[ i ] ].x, normal[ indices[ i ] ].y + offset[ indices[ i ] ].y, normal[ indices[ i ] ].z + offset[ indices[ i ] ].z ); 
                    
#define INDEXED_STREAM_UV_WITH_OFFSET( i, uv, offset, uvIdx )  \
                    stream->UV( uv[ indices[ i ] ].u + offset[ indices[ i ] ].u, uv[ indices[ i ] ].v + offset[ indices[ i ] ].v, uvIdx );

using namespace RadicalMathLibrary;

static bool gFadeVrVehicleGlass=false;
static bool gSuppressVrVehicleDriver=false;
static bool gCsmOpaqueReceiverOnly=false;
static bool gCsmIntegratedVehicleReceiver=false;
static bool gEnhancedWorldMaterials=false;
static bool gEnhancedCharacterMaterials=false;
static bool gEnhancedVehicleMaterials=false;
static tShader* gFadedVrVehicleShaders[128];
static unsigned gFadedVrVehicleShaderCount=0;

static bool SuppressLegacyGroundShadowPrimGroup(tShader* shader)
{
    if(!p3dAreLegacyGroundShadowsSuppressed() || !shader) return false;
    // Entity text is compiled out of release builds (GetName returns
    // NO_NAME_TEXT), but the asset UID is always retained.
    return shader->GetUID()==tName::MakeUID("treeshadow_m");
}

void p3dSetVrVehicleGlassFaded(bool faded)
{
    if(!faded)
    {
        for(unsigned i=0; i<gFadedVrVehicleShaderCount; ++i)
            gFadedVrVehicleShaders[i]->SetInt(PDDI_SP_EMISSIVEALPHA,255);
        gFadedVrVehicleShaderCount=0;
    }
    gFadeVrVehicleGlass=faded;
}

void p3dSetVrVehicleDriverSuppressed(bool suppressed)
{
    gSuppressVrVehicleDriver=suppressed;
}

void p3dSetCsmOpaqueReceiverOnly(bool enabled)
{
    gCsmOpaqueReceiverOnly=enabled;
}

void p3dSetCsmIntegratedVehicleReceiver(bool enabled)
{
    gCsmIntegratedVehicleReceiver=enabled;
#if defined(RAD_ANDROID)
    if(!enabled) VrEnableSunShadowReceivers(p3d::pddi,false);
#endif
}

#if defined(RAD_ANDROID)
static void SelectIntegratedVehicleReceiver(tShader* shader)
{
    if(gCsmIntegratedVehicleReceiver)
        VrEnableSunShadowReceivers(p3d::pddi,
                                   !shader || !shader->mTranslucent);
}
#endif

void p3dSetEnhancedWorldMaterials(bool enabled)
{
    gEnhancedWorldMaterials=enabled;
#if defined(RAD_ANDROID)
    if(!enabled && !gEnhancedVehicleMaterials) pglSetEnhancedMaterialMode(0);
#endif
}

void p3dSetEnhancedCharacterMaterials(bool enabled)
{
    gEnhancedCharacterMaterials=enabled;
#if defined(RAD_ANDROID)
    if(!enabled) pglSetEnhancedMaterialMode(gEnhancedWorldMaterials ? 1 : 0);
#endif
}

void p3dSetEnhancedSunDirection(const rmt::Vector& direction)
{
#if defined(RAD_ANDROID)
    pglSetEnhancedSunDirection(direction.x,direction.y,direction.z);
#endif
}

void p3dSetEnhancedVehicleMaterials(bool enabled)
{
    gEnhancedVehicleMaterials=enabled;
#if defined(RAD_ANDROID)
    if(!enabled) pglSetEnhancedMaterialMode(gEnhancedWorldMaterials ? 1 : 0);
#endif
}

#if defined(RAD_ANDROID)
static int EnhancedMaterialMode(tShader* shader)
{
    // mTranslucent is a conservative asset-level flag and is also set on a
    // number of opaque character/vehicle shaders. The GLES material's actual
    // blend state performs the reliable transparency rejection later.
    if(!shader) return 0;
    if(gEnhancedCharacterMaterials) return 3;
    const char* name=shader->GetName();
    static const char* transparent[]={
        "glass","Glass","window","Window","wind","Wind","tire","Tire",
        "light","Light","lamp","Lamp","shadow","Shadow"
    };
    if(name)
        for(unsigned i=0;i<sizeof(transparent)/sizeof(transparent[0]);++i)
            if(strstr(name,transparent[i])) return 0;
    if(!gEnhancedVehicleMaterials)
    {
        if(!gEnhancedWorldMaterials) return 0;
        // All regular opaque world geometry uses one deliberately restrained
        // Phong profile.  Filename heuristics made some scenery completely
        // matte and other scenery implausibly metallic in stripped releases.
        return 1;
    }
    if(!name) return 2;
    static tShader* logged[48]={0};
    static unsigned loggedCount=0;
    bool seen=false;
    for(unsigned i=0;i<loggedCount;++i) if(logged[i]==shader) { seen=true; break; }
    if(!seen && loggedCount<sizeof(logged)/sizeof(logged[0]))
    {
        logged[loggedCount++]=shader;
        p3d::printf("VR enhanced vehicle paint: %s\n",name);
    }
    return 2;
}
#endif

static void FadeVrVehicleGlass(tShader* shader)
{
    if(!gFadeVrVehicleGlass || !shader || !shader->mTranslucent) return;
    for(unsigned i=0; i<gFadedVrVehicleShaderCount; ++i)
        if(gFadedVrVehicleShaders[i]==shader) return;
    if(gFadedVrVehicleShaderCount<sizeof(gFadedVrVehicleShaders)/sizeof(gFadedVrVehicleShaders[0]))
        gFadedVrVehicleShaders[gFadedVrVehicleShaderCount++]=shader;
    shader->SetInt(PDDI_SP_EMISSIVEALPHA,64);
}

static bool SuppressVrVehicleDriver(tShader* shader)
{
    if(!gSuppressVrVehicleDriver || !shader) return false;
    // RAD_RELEASE deliberately strips entity name strings, making GetName()
    // return "NO_NAME_TEXT". Runtime material filtering must therefore use
    // the stable Pure3D UID hashes generated from the authored shader names.
    static const tUID hiddenShaderUids[]={
        tEntity::MakeUID("char_swatches_m"),
        tEntity::MakeUID("char_swatches_m1"),
        tEntity::MakeUID("char_swatches_lit_m"),
        tEntity::MakeUID("Windsheild_m"),
        tEntity::MakeUID("WindsheildT_m"),
        tEntity::MakeUID("WindsheildTx_m"),
        tEntity::MakeUID("WindsheildC_m"),
        tEntity::MakeUID("Windsheildc_m"),
        tEntity::MakeUID("WindsheildH_m"),
        tEntity::MakeUID("Windshield_m"),
        tEntity::MakeUID("WindshieldT_m"),
        tEntity::MakeUID("cNonup_WindsheildT_m"),
        tEntity::MakeUID("marge_vWindsheild_m"),
        tEntity::MakeUID("scorp_vWindsheild_m"),
        tEntity::MakeUID("smith_vWindsheild_m"),
        tEntity::MakeUID("ttWindsheild_m")
    };
    const tUID uid=shader->GetUID();
    for(unsigned i=0;i<sizeof(hiddenShaderUids)/sizeof(hiddenShaderUids[0]);++i)
        if(uid==hiddenShaderUids[i]) return true;
    return false;
}

#ifdef RAD_XBOX
bool IsVertexShader( tShader *shader )
{
	const int vertexShaderNum = 5;
	static const char * vertexShaderNames[ vertexShaderNum ]  = {   "refract",
													"toon",
													"toon2",
													"spheremap",
													"bumpmap"
	};

	for( int i = 0; i < vertexShaderNum; ++i ){

        if( strcmp( shader->GetType(), vertexShaderNames[ i ] ) == 0 )
			return true;

	}

	return false;
}
#endif

tPrimGroup::tPrimGroup(int nVertex) :
    mShader(NULL), 
    mPrimType(PDDI_PRIM_TRIANGLES), 
    mVertexCount(nVertex),
    instanceCount(0),
    instanceSize(0)
{
    //
}

tPrimGroup::~tPrimGroup()
{
    tRefCounted::Release(mShader);
}

void tPrimGroup::SetShader(tShader* m)
{
    tRefCounted::Assign(mShader, m);
}


//-------------------------------------------------------------------
tPrimGroupOptimised::tPrimGroupOptimised(int nVerts) : 
    tPrimGroup(nVerts), 
    mBuffer(NULL),
    tempPositions(NULL)
{
}

tPrimGroupOptimised::tPrimGroupOptimised(pddiPrimBuffer *buffer, int nVerts) : 
    tPrimGroup(nVerts), 
    mBuffer(buffer),
    tempPositions(NULL)
{
    if(mBuffer)
        mBuffer->AddRef();
}

tPrimGroupOptimised::tPrimGroupOptimised(tPrimGroupStreamed& pg, unsigned hint, bool writeOnce) :
    tPrimGroup(pg.GetVertexCount()),
    mBuffer(NULL),
    tempPositions(NULL)
{
    SetPrimType(pg.GetPrimType());
    pddiPrimBufferDesc desc( mPrimType,                          
                             pg.GetVertexFormat(),
                             pg.GetNumVertex(),
                             pg.GetNumIndices());

    mBuffer = p3d::device->NewPrimBuffer(&desc);

    pddiPrimBufferStream* stream = mBuffer->Lock();
    pg.Display(stream);
    mBuffer->Unlock(stream);
}

tPrimGroupOptimised::~tPrimGroupOptimised()
{
    tRefCounted::Release(mBuffer);
    p3d::FreeTemp(tempPositions);
}

void tPrimGroupOptimised::Display()
{
    if(SuppressLegacyGroundShadowPrimGroup(mShader)) return;
    if(SuppressVrVehicleDriver(mShader)) return;
    FadeVrVehicleGlass(mShader);
    if(gCsmOpaqueReceiverOnly && mShader && mShader->mTranslucent) return;
#if defined(RAD_ANDROID)
    SelectIntegratedVehicleReceiver(mShader);
    pglSetEnhancedMaterialMode(EnhancedMaterialMode(mShader));
#endif
    P3DASSERT(mBuffer);
    pddiShader* shader = mShader->GetShader();

#ifdef RAD_PS2
    bool refractionShadersInUse = ps2RefractionShader::RefractionShadersLoaded();
    if( refractionShadersInUse )
    {
        pddiBaseShader* clone = shader->CloneSimple();
        if( clone != NULL )
        {
            p3d::pddi->DrawPrimBuffer( clone, mBuffer );
        }
        else
        {
            p3d::pddi->DrawPrimBuffer( shader, mBuffer );
        }
    }
    else
    {
        p3d::pddi->DrawPrimBuffer( shader, mBuffer );
    }
#else
    p3d::pddi->DrawPrimBuffer( shader, mBuffer );
#endif
#if defined(RAD_ANDROID)
    pglSetEnhancedMaterialMode(gEnhancedWorldMaterials ? 1 : 0);
#endif
}


void tPrimGroupOptimised::SetBuffer(pddiPrimBuffer *buffer) 
{ 
    tRefCounted::Assign(mBuffer, buffer);
}

bool tPrimGroupOptimised::GetVertices(unsigned start, unsigned count, rmt::Vector* v)
{
    P3DASSERT(tempPositions);
    memcpy(v, &tempPositions[start], sizeof(rmt::Vector)*count);
    return true;
}

bool tPrimGroupOptimised::SetVertices(unsigned start, unsigned count, rmt::Vector* v)
{
    P3DASSERT(start == 0);

    pddiPrimBufferStream* stream = mBuffer->Lock();
    for (unsigned a = 0; a < count; a++) 
    {
        stream->Coord(v[a].x,v[a].y,v[a].z);
    }
    mBuffer->Unlock(stream);
    return true;
}

void tPrimGroupOptimised::ReleaseTempVertices(void)
{
    p3d::FreeTemp(tempPositions);
    tempPositions = NULL;

}

//---------------------------------------------------------------

tPrimGroupSkinnedOptimised::tPrimGroupSkinnedOptimised(int nVertex):
    tPrimGroupOptimised(nVertex),
    nMatrices(0),
    matrixPalette(NULL)
{   
}

tPrimGroupSkinnedOptimised::~tPrimGroupSkinnedOptimised()
{
    delete [] matrixPalette;
}

void tPrimGroupSkinnedOptimised::Display() 
{
    if(SuppressLegacyGroundShadowPrimGroup(mShader)) return;
    if(SuppressVrVehicleDriver(mShader)) return;
    FadeVrVehicleGlass(mShader);
    if(gCsmOpaqueReceiverOnly && mShader && mShader->mTranslucent) return;
#if defined(RAD_ANDROID)
    SelectIntegratedVehicleReceiver(mShader);
    pglSetEnhancedMaterialMode(EnhancedMaterialMode(mShader));
#endif
    pddiExtHardwareSkinning* hwSkin = p3d::context->GetHardwareSkinning();
    hwSkin->SetMatrixCount(nMatrices);

    P3DASSERT(mBuffer);
    for(unsigned i = 0; i < nMatrices; i++)
    {
        hwSkin->SetMatrix(i, (pddiMatrix*)matrixPalette[i]);
    }
    hwSkin->DrawSkin(GetShader()->GetShader(),GetBuffer());
#if defined(RAD_ANDROID)
    pglSetEnhancedMaterialMode(gEnhancedWorldMaterials ? 1 : 0);
#endif
}

void tPrimGroupSkinnedOptimised::DisplayInstanced(unsigned count)
{
    mBuffer->SetUsedSize(count * instanceSize);
    Display();
    mBuffer->SetUsedSize(0);
}
    
//-------------------------------------------------------------------
tPrimGroupStreamed::tPrimGroupStreamed(int nVertex, unsigned format, int nIndex, bool allocate) :
    tPrimGroup(nVertex), mIndexCount(nIndex), mIndices(NULL), mVertexList(NULL)
{
    mVertexFormat = format;

    if (allocate)
    {
        mVertexList = new tVertexList(nVertex, mVertexFormat);

        if (mIndexCount > 0)
        {
            mIndices = new unsigned short[mIndexCount];
            memset(mIndices, 0, sizeof(unsigned short) * mIndexCount);
        }
    }

#ifdef RAD_XBOX
    vertexProgram = 0;
#endif

}

tPrimGroupStreamed::~tPrimGroupStreamed()
{
    tRefCounted::Release(mVertexList);
    delete[] mIndices;
}

void tPrimGroupStreamed::Display()
{
    if(SuppressLegacyGroundShadowPrimGroup(mShader)) return;
    if(SuppressVrVehicleDriver(mShader)) return;
    FadeVrVehicleGlass(mShader);
    if(gCsmOpaqueReceiverOnly && mShader && mShader->mTranslucent) return;
#if defined(RAD_ANDROID)
    SelectIntegratedVehicleReceiver(mShader);
    pglSetEnhancedMaterialMode(EnhancedMaterialMode(mShader));
#endif
    P3DASSERT(mVertexList);

    // don't send indices and weights to the stream renderer
    // the skinning is done by tPrimGroupStreamed::Display();
    unsigned vertexFormat = mVertexList->GetFormat();
    vertexFormat &= ~(PDDI_V_INDICES | PDDI_V_WEIGHTS);

#ifdef RAD_XBOX
    if(vertexProgram)
    {
        pddiExtVertexProgram* vp = (pddiExtVertexProgram*)p3d::pddi->GetExtension(PDDI_EXT_VERTEX_PROGRAM);
        vp->SetStreamProgram(vertexProgram);
    }
#endif

    pddiBaseShader* pShader = (pddiBaseShader *) mShader->GetShader( );

#ifdef RAD_PS2
    const int numPasses = 1;
#else
    const int numPasses = pShader->GetPasses();
#endif

    for( int i = 0; i < numPasses;  ++i ){     
        pddiPrimStream* stream = p3d::pddi->BeginPrims(mShader->GetShader(), mPrimType, vertexFormat, (mIndexCount > 0) ? mIndexCount : mVertexList->GetNumVertices(), i );

        Display(stream);

        p3d::pddi->EndPrims(stream);

    }


#ifdef RAD_XBOX
    if(vertexProgram)
    {
        pddiExtVertexProgram* vp = (pddiExtVertexProgram*)p3d::pddi->GetExtension(PDDI_EXT_VERTEX_PROGRAM);
        vp->SetStreamProgram(0);
    }
#endif
#if defined(RAD_ANDROID)
    pglSetEnhancedMaterialMode(gEnhancedWorldMaterials ? 1 : 0);
#endif
}

void tPrimGroupStreamed::SetVertexList(tVertexList* vl)
{
    tRefCounted::Assign(mVertexList, vl);
}

int tPrimGroupStreamed::GetNumVertex() const
{
    return mVertexList ? mVertexList->GetNumVertex() : 0;
}

void tPrimGroupStreamed::Display(pddiPrimStream* stream)
{
    if(mIndexCount > 0)
    {
        IndexedDisplay(stream);
    }
    else
    {
        NonIndexedDisplay(stream);
    }
}

void tPrimGroupStreamed::Display(pddiPrimBufferStream* stream)
{
    if(mIndexCount > 0)
    {
        IndexedDisplay(stream);
    }
    else
    {
        P3DASSERT(0 && "Attempt to display non-indexed buffered streams");
    }
}

//---------------------------------------------------------------
tPrimGroupSkinnedStreamed::tPrimGroupSkinnedStreamed(int nVertex, unsigned format, int nIndex, bool allocate)  :
    tPrimGroupStreamed(nVertex, format, nIndex, allocate) ,
    matrixPalette(NULL),
    mVertices(NULL)
{   
    if (mVertexCount > 0)
    {
        mVertices = new SkinVertex[mVertexCount];
    }
}

tPrimGroupSkinnedStreamed::~tPrimGroupSkinnedStreamed()
{
    delete[] mVertices;
    delete[] matrixPalette;
}


void tPrimGroupSkinnedStreamed::Display(void)
{
    if(SuppressLegacyGroundShadowPrimGroup(mShader)) return;
    if(SuppressVrVehicleDriver(mShader)) return;
    FadeVrVehicleGlass(mShader);
    if(gCsmOpaqueReceiverOnly && mShader && mShader->mTranslucent) return;
    int count = mVertexCount;
    SkinVertex *verts = mVertices;
    rmt::Vector* outNormals = mVertexList->GetNormals();
    rmt::Vector* outPositions = mVertexList->GetPositions();

    if(mVertexFormat & PDDI_V_NORMAL)
    {
        for(int i = 0; i < count; i++)
        {
            // fetch first bone index
            Matrix* m = matrixPalette[verts[i].indices[0]];

            // compute normal
            // note that we only use the first bone index - normals aren't weighted
            Vector normal;
			Vector &oldNormal = verts[i].normal;

            normal.x = m->m[0][0]*oldNormal.x + m->m[1][0]*oldNormal.y + m->m[2][0]*oldNormal.z;
            normal.y = m->m[0][1]*oldNormal.x + m->m[1][1]*oldNormal.y + m->m[2][1]*oldNormal.z;
            normal.z = m->m[0][2]*oldNormal.x + m->m[1][2]*oldNormal.y + m->m[2][2]*oldNormal.z;

            // compute weighted skin co-ordinate
            Vector vertex;
            Vector  & sv = verts[i].position;
            float weight = verts[i].weights[0];
            vertex.x = (m->m[0][0]*sv.x + m->m[1][0]*sv.y + m->m[2][0]*sv.z + m->m[3][0]) * weight;
            vertex.y = (m->m[0][1]*sv.x + m->m[1][1]*sv.y + m->m[2][1]*sv.z + m->m[3][1]) * weight;
            vertex.z = (m->m[0][2]*sv.x + m->m[1][2]*sv.y + m->m[2][2]*sv.z + m->m[3][2]) * weight;

            if(weight != 1.0f)
            {
                weight = verts[i].weights[1];
                m = matrixPalette[verts[i].indices[1]];
                vertex.x += (m->m[0][0]*sv.x + m->m[1][0]*sv.y + m->m[2][0]*sv.z + m->m[3][0]) * weight;
                vertex.y += (m->m[0][1]*sv.x + m->m[1][1]*sv.y + m->m[2][1]*sv.z + m->m[3][1]) * weight;
                vertex.z += (m->m[0][2]*sv.x + m->m[1][2]*sv.y + m->m[2][2]*sv.z + m->m[3][2]) * weight;

                weight = verts[i].weights[2];
                m = matrixPalette[verts[i].indices[2]];
                vertex.x += (m->m[0][0]*sv.x + m->m[1][0]*sv.y + m->m[2][0]*sv.z + m->m[3][0]) * weight;
                vertex.y += (m->m[0][1]*sv.x + m->m[1][1]*sv.y + m->m[2][1]*sv.z + m->m[3][1]) * weight;
                vertex.z += (m->m[0][2]*sv.x + m->m[1][2]*sv.y + m->m[2][2]*sv.z + m->m[3][2]) * weight;

                weight = 1.0f - (verts[i].weights[2] + verts[i].weights[1] + verts[i].weights[0]);
                m = matrixPalette[verts[i].indices[3]];
                vertex.x += (m->m[0][0]*sv.x + m->m[1][0]*sv.y + m->m[2][0]*sv.z + m->m[3][0]) * weight;
                vertex.y += (m->m[0][1]*sv.x + m->m[1][1]*sv.y + m->m[2][1]*sv.z + m->m[3][1]) * weight;
                vertex.z += (m->m[0][2]*sv.x + m->m[1][2]*sv.y + m->m[2][2]*sv.z + m->m[3][2]) * weight;
            }

            // output transformed vertex
            *outPositions++ = vertex;
            *outNormals++ = normal;
        }
    }
    else
    {
        for(int i = 0; i < count; i++)
        {
            // fetch first bone index
            Matrix* m = matrixPalette[verts[i].indices[0]];

            // compute weighted skin co-ordinate
            Vector vertex;
            Vector* sv = &verts[i].position;
            float weight = verts[i].weights[0];
            vertex.x = (m->m[0][0]*sv->x + m->m[1][0]*sv->y + m->m[2][0]*sv->z + m->m[3][0]) * weight;
            vertex.y = (m->m[0][1]*sv->x + m->m[1][1]*sv->y + m->m[2][1]*sv->z + m->m[3][1]) * weight;
            vertex.z = (m->m[0][2]*sv->x + m->m[1][2]*sv->y + m->m[2][2]*sv->z + m->m[3][2]) * weight;

            if(weight != 1.0f)
            {
                weight = verts[i].weights[1];
                m = matrixPalette[verts[i].indices[1]];
                vertex.x += (m->m[0][0]*sv->x + m->m[1][0]*sv->y + m->m[2][0]*sv->z + m->m[3][0]) * weight;
                vertex.y += (m->m[0][1]*sv->x + m->m[1][1]*sv->y + m->m[2][1]*sv->z + m->m[3][1]) * weight;
                vertex.z += (m->m[0][2]*sv->x + m->m[1][2]*sv->y + m->m[2][2]*sv->z + m->m[3][2]) * weight;

                weight = verts[i].weights[2];
                m = matrixPalette[verts[i].indices[2]];
                vertex.x += (m->m[0][0]*sv->x + m->m[1][0]*sv->y + m->m[2][0]*sv->z + m->m[3][0]) * weight;
                vertex.y += (m->m[0][1]*sv->x + m->m[1][1]*sv->y + m->m[2][1]*sv->z + m->m[3][1]) * weight;
                vertex.z += (m->m[0][2]*sv->x + m->m[1][2]*sv->y + m->m[2][2]*sv->z + m->m[3][2]) * weight;

                weight = 1.0f - (verts[i].weights[2] + verts[i].weights[1] + verts[i].weights[0]);
                m = matrixPalette[verts[i].indices[3]];
                vertex.x += (m->m[0][0]*sv->x + m->m[1][0]*sv->y + m->m[2][0]*sv->z + m->m[3][0]) * weight;
                vertex.y += (m->m[0][1]*sv->x + m->m[1][1]*sv->y + m->m[2][1]*sv->z + m->m[3][1]) * weight;
                vertex.z += (m->m[0][2]*sv->x + m->m[1][2]*sv->y + m->m[2][2]*sv->z + m->m[3][2]) * weight;
            }

            // output transformed vertex
            *outPositions++ = vertex;
        }
    }

    tPrimGroupStreamed::Display();
}

void tPrimGroupSkinnedStreamed::DisplayInstanced(unsigned count)
{
    Display();
}

bool tPrimGroupSkinnedStreamed::GetVertices(unsigned start, unsigned count, rmt::Vector* v)
{
    for(unsigned i = start; i < start + count; i++)
    {
        *v++ = mVertices[i].position;
    }
    return true;
}

bool tPrimGroupSkinnedStreamed::SetVertices(unsigned start, unsigned count, rmt::Vector* v)
{
    for(unsigned i = start; i < start + count; i++)
    {
        mVertices[i].position = *v++;
    }
    return true;
}
//--------------------------------pure3d skin for PC--------------------------------
#ifndef RAD_PS2
tPrimGroupSkinnedPC::tPrimGroupSkinnedPC(int nVertex, unsigned format, int nIndex, bool allocate)  :
	tPrimGroupSkinnedOptimised(nVertex), mVertices( NULL ),
    mCachedMatrixPalette( NULL ), mCachedMatrixCount( 0 ),
    mSkinCacheValid( false )
{
    if (mVertexCount > 0)
    {
        mVertices = new SkinVertex[mVertexCount];
    }
}

tPrimGroupSkinnedPC::~tPrimGroupSkinnedPC()
{
	if( mVertices )
		delete[ ] mVertices;
    delete[] mCachedMatrixPalette;
}

bool tPrimGroupSkinnedPC::GetVertices(unsigned start, unsigned count, rmt::Vector* v)
{
    for(unsigned i = start; i < start + count; i++)
    {
        *v++ = mVertices[i].position;
    }
    return true;
}

bool tPrimGroupSkinnedPC::SetVertices(unsigned start, unsigned count, rmt::Vector* v)
{
    for(unsigned i = start; i < start + count; i++)
    {
        mVertices[i].position = *v++;
    }
    mSkinCacheValid = false;
    return true;
}

void tPrimGroupSkinnedPC::Display(void)
{
    if(SuppressLegacyGroundShadowPrimGroup(mShader)) return;
	if(SuppressVrVehicleDriver(mShader)) return;
	FadeVrVehicleGlass(mShader);
	if(gCsmOpaqueReceiverOnly && mShader && mShader->mTranslucent) return;
#if defined(RAD_ANDROID)
    SelectIntegratedVehicleReceiver(mShader);
    pglSetEnhancedMaterialMode(EnhancedMaterialMode(mShader));
#endif
	pddiPrimBufferStream *stream;

    bool paletteChanged = !mSkinCacheValid || mCachedMatrixCount != nMatrices;
    if(!paletteChanged)
    {
        for(unsigned matrix = 0; matrix < nMatrices; ++matrix)
        {
            if(memcmp(&mCachedMatrixPalette[matrix], matrixPalette[matrix],
                      sizeof(rmt::Matrix)) != 0)
            {
                paletteChanged = true;
                break;
            }
        }
    }

    if(!paletteChanged)
    {
        p3d::pddi->DrawPrimBuffer(mShader->GetShader(), mBuffer);
#if defined(RAD_ANDROID)
        pglSetEnhancedMaterialMode(gEnhancedWorldMaterials ? 1 : 0);
#endif
        return;
    }

    if(mCachedMatrixCount != nMatrices)
    {
        delete[] mCachedMatrixPalette;
        mCachedMatrixPalette = nMatrices ? new rmt::Matrix[nMatrices] : NULL;
        mCachedMatrixCount = nMatrices;
    }
    for(unsigned matrix = 0; matrix < nMatrices; ++matrix)
        mCachedMatrixPalette[matrix] = *matrixPalette[matrix];
    mSkinCacheValid = true;
	
    int count = mVertexCount;
    SkinVertex *verts = mVertices;

	if(mVertexFormat & PDDI_V_NORMAL){

		stream = mBuffer->Lock( );

        for(int i = 0; i < count; i++){
            // fetch first bone index			
            Matrix* m = matrixPalette[verts[i].indices[0]];

            // compute normal
            // note that we only use the first bone index - normals aren't weighted
            Vector normal;
            normal.x = m->m[0][0]*verts[i].normal.x + m->m[1][0]*verts[i].normal.y + m->m[2][0]*verts[i].normal.z;
            normal.y = m->m[0][1]*verts[i].normal.x + m->m[1][1]*verts[i].normal.y + m->m[2][1]*verts[i].normal.z;
            normal.z = m->m[0][2]*verts[i].normal.x + m->m[1][2]*verts[i].normal.y + m->m[2][2]*verts[i].normal.z;

		   // compute weighted skin co-ordinate
			Vector vertex;
			Vector* sv = &verts[i].position;
			float weight = verts[i].weights[0];
			vertex.x = (m->m[0][0]*sv->x + m->m[1][0]*sv->y + m->m[2][0]*sv->z + m->m[3][0]) * weight;
			vertex.y = (m->m[0][1]*sv->x + m->m[1][1]*sv->y + m->m[2][1]*sv->z + m->m[3][1]) * weight;
			vertex.z = (m->m[0][2]*sv->x + m->m[1][2]*sv->y + m->m[2][2]*sv->z + m->m[3][2]) * weight;

			if(weight != 1.0f)
			{
				weight = verts[i].weights[1];
				m = matrixPalette[verts[i].indices[1]];
				vertex.x += (m->m[0][0]*sv->x + m->m[1][0]*sv->y + m->m[2][0]*sv->z + m->m[3][0]) * weight;
				vertex.y += (m->m[0][1]*sv->x + m->m[1][1]*sv->y + m->m[2][1]*sv->z + m->m[3][1]) * weight;
				vertex.z += (m->m[0][2]*sv->x + m->m[1][2]*sv->y + m->m[2][2]*sv->z + m->m[3][2]) * weight;

				weight = verts[i].weights[2];
				m = matrixPalette[verts[i].indices[2]];
				vertex.x += (m->m[0][0]*sv->x + m->m[1][0]*sv->y + m->m[2][0]*sv->z + m->m[3][0]) * weight;
				vertex.y += (m->m[0][1]*sv->x + m->m[1][1]*sv->y + m->m[2][1]*sv->z + m->m[3][1]) * weight;
				vertex.z += (m->m[0][2]*sv->x + m->m[1][2]*sv->y + m->m[2][2]*sv->z + m->m[3][2]) * weight;

				weight = 1.0f - (verts[i].weights[2] + verts[i].weights[1] + verts[i].weights[0]);
				m = matrixPalette[verts[i].indices[3]];
				vertex.x += (m->m[0][0]*sv->x + m->m[1][0]*sv->y + m->m[2][0]*sv->z + m->m[3][0]) * weight;
				vertex.y += (m->m[0][1]*sv->x + m->m[1][1]*sv->y + m->m[2][1]*sv->z + m->m[3][1]) * weight;
				vertex.z += (m->m[0][2]*sv->x + m->m[1][2]*sv->y + m->m[2][2]*sv->z + m->m[3][2]) * weight;
			}
			stream->Normal( normal.x, normal.y, normal.z );
			stream->Position( vertex.x, vertex.y, vertex.z );			
        }
		
        if(stream){
            mBuffer->Unlock(stream);
            stream = NULL;
        }
    }
	else{			//no normal blend the position only

		stream = mBuffer->Lock( );
		for(int i = 0; i < count; i++){
			// fetch first bone index
			Matrix* m = matrixPalette[verts[i].indices[0]];

			// compute weighted skin co-ordinate
			Vector vertex;
			Vector* sv = &verts[i].position;
			float weight = verts[i].weights[0];
			vertex.x = (m->m[0][0]*sv->x + m->m[1][0]*sv->y + m->m[2][0]*sv->z + m->m[3][0]) * weight;
			vertex.y = (m->m[0][1]*sv->x + m->m[1][1]*sv->y + m->m[2][1]*sv->z + m->m[3][1]) * weight;
			vertex.z = (m->m[0][2]*sv->x + m->m[1][2]*sv->y + m->m[2][2]*sv->z + m->m[3][2]) * weight;

			if(weight != 1.0f)
			{
				weight = verts[i].weights[1];
				m = matrixPalette[verts[i].indices[1]];
				vertex.x += (m->m[0][0]*sv->x + m->m[1][0]*sv->y + m->m[2][0]*sv->z + m->m[3][0]) * weight;
				vertex.y += (m->m[0][1]*sv->x + m->m[1][1]*sv->y + m->m[2][1]*sv->z + m->m[3][1]) * weight;
				vertex.z += (m->m[0][2]*sv->x + m->m[1][2]*sv->y + m->m[2][2]*sv->z + m->m[3][2]) * weight;

				weight = verts[i].weights[2];
				m = matrixPalette[verts[i].indices[2]];
				vertex.x += (m->m[0][0]*sv->x + m->m[1][0]*sv->y + m->m[2][0]*sv->z + m->m[3][0]) * weight;
				vertex.y += (m->m[0][1]*sv->x + m->m[1][1]*sv->y + m->m[2][1]*sv->z + m->m[3][1]) * weight;
				vertex.z += (m->m[0][2]*sv->x + m->m[1][2]*sv->y + m->m[2][2]*sv->z + m->m[3][2]) * weight;

				weight = 1.0f - (verts[i].weights[2] + verts[i].weights[1] + verts[i].weights[0]);
				m = matrixPalette[verts[i].indices[3]];
				vertex.x += (m->m[0][0]*sv->x + m->m[1][0]*sv->y + m->m[2][0]*sv->z + m->m[3][0]) * weight;
				vertex.y += (m->m[0][1]*sv->x + m->m[1][1]*sv->y + m->m[2][1]*sv->z + m->m[3][1]) * weight;
				vertex.z += (m->m[0][2]*sv->x + m->m[1][2]*sv->y + m->m[2][2]*sv->z + m->m[3][2]) * weight;
			}

			// output transformed vertex
			stream->Position( vertex.x, vertex.y, vertex.z );
		}


		if(stream){
			mBuffer->Unlock(stream);
			stream = NULL;
		}

	}
	//buffer rendering
	p3d::pddi->DrawPrimBuffer(mShader->GetShader(), mBuffer);
#if defined(RAD_ANDROID)
    pglSetEnhancedMaterialMode(gEnhancedWorldMaterials ? 1 : 0);
#endif

}

void tPrimGroupSkinnedPC::DisplayInstanced(unsigned count)
{
    Display();
}
#endif
//---------------------------------------------------------------
template<class T> inline static void IndexedDisplayInternal(tVertexList* vertexList, T* stream, unsigned short* indices, int nIndex)
{
    int i;
    pddiVector* coord = (pddiVector*)vertexList->GetPositions();
    pddiVector* normal = (pddiVector*)vertexList->GetNormals();
    pddiColour* c[tVertexList::MAX_NUM_COLOUR_SETS];
    for(i = 0; i < tVertexList::MAX_NUM_COLOUR_SETS; i++)
    {
        c[i] = (pddiColour*)vertexList-> GetColours(i);
    }
    pddiColour* colour0 = c[0];
    pddiVector2* uv0 = (pddiVector2*)vertexList->GetUVs(0);
    pddiVector2* uv1 = (pddiVector2*)vertexList->GetUVs(1);
    pddiVector2* uv2 = (pddiVector2*)vertexList->GetUVs(2);
    pddiVector2* uv3 = (pddiVector2*)vertexList->GetUVs(3);

    unsigned vertexFormat = vertexList->GetFormat() & ~PDDI_V_COLOUR_MASK;
    switch(vertexFormat)
    {
        case PDDI_V_C:
            for(i=0; i < nIndex; i++)
                stream->Vertex(&coord[indices[i]], colour0[indices[i]]);
        break;

        case PDDI_V_N:
            for(i=0; i < nIndex; i++)
                stream->Vertex(&coord[indices[i]], &normal[indices[i]]);
        break;

        case PDDI_V_T:
            for(i=0; i < nIndex; i++)
                stream->Vertex(&coord[indices[i]], &uv0[indices[i]]);
        break;

        case PDDI_V_CT:
            for(i=0; i < nIndex; i++)
                stream->Vertex(&coord[indices[i]], colour0[indices[i]], &uv0[indices[i]]);
        break;

        case PDDI_V_NT:
            for(i=0; i < nIndex; i++)
                stream->Vertex(&coord[indices[i]], &normal[indices[i]], &uv0[indices[i]]);
        break;

        default:
            // warning:  This will probably be slower than you want...
            for(i=0; i < nIndex; i++)
            {
                if(normal) stream->Normal(normal[indices[i]].x, normal[indices[i]].y, normal[indices[i]].z);
                int j;
                for(j = 1; j < tVertexList::MAX_NUM_COLOUR_SETS; j++)
                {
                    if(c[j]) stream->Colour( c[j][indices[i]], indices[i] );
                }
                if(uv0) stream->UV(uv0[indices[i]].u, uv0[indices[i]].v, 0);
                if(uv1) stream->UV(uv1[indices[i]].u, uv1[indices[i]].v, 1);
                if(uv2) stream->UV(uv2[indices[i]].u, uv2[indices[i]].v, 2);
                if(uv3) stream->UV(uv3[indices[i]].u, uv3[indices[i]].v, 3);
                stream->Coord(coord[indices[i]].x, coord[indices[i]].y, coord[indices[i]].z);
            }
        break;
    }
}

//---------------------------------------------------------------
template<class T> inline static void IndexedDisplayWithOffsetInternal(tVertexList* vertexList, T* stream, unsigned short* indices, int nIndex)
{
    int i;
    pddiVector* p = (pddiVector*)vertexList->GetPositions();
    pddiColour* c[tVertexList::MAX_NUM_COLOUR_SETS];
    for(i = 0; i < tVertexList::MAX_NUM_COLOUR_SETS; i++)
    {
        c[i] = (pddiColour*)vertexList->GetColours(i);
    }
    pddiColour* c0 = c[0];
    pddiVector* n = (pddiVector*)vertexList->GetNormals();
    pddiVector* b = (pddiVector*)vertexList->GetBinormals();
    pddiVector* t = (pddiVector*)vertexList->GetTangents();
    pddiVector2* t0 = (pddiVector2*)vertexList->GetUVs(0);
    pddiVector2* t1 = (pddiVector2*)vertexList->GetUVs(1);
    pddiVector2* t2 = (pddiVector2*)vertexList->GetUVs(2);
    pddiVector2* t3 = (pddiVector2*)vertexList->GetUVs(3);


    tVertexOffsetList *offset = vertexList->GetOffsetList( );

    assert( offset );           //offset must not be NULL
  
    pddiColour colour;

    pddiVector *op = (pddiVector*) ( offset->mMask & tVertexOffsetList::eOffsetCoord ? offset->mCoord : NULL);
    tColourOffset *oc = (tColourOffset*) ( offset->mMask & tVertexOffsetList::eOffsetColour ? offset->mColour : NULL );
    pddiVector *on = (pddiVector*) ( offset->mMask & tVertexOffsetList::eOffsetNormal ? offset->mNormal : NULL );
    pddiVector2 *ot0 = (pddiVector2*) ( offset->mMask & tVertexOffsetList::eOffsetUV0 ? offset->mUV[ 0 ] : NULL ); 
    pddiVector2 *ot1 = (pddiVector2*) ( offset->mMask & tVertexOffsetList::eOffsetUV1 ? offset->mUV[ 1 ] : NULL ); 
    pddiVector2 *ot2 = (pddiVector2*) ( offset->mMask & tVertexOffsetList::eOffsetUV2 ? offset->mUV[ 2 ] : NULL ); 
    pddiVector2 *ot3 = (pddiVector2*) ( offset->mMask & tVertexOffsetList::eOffsetUV3 ? offset->mUV[ 3 ] : NULL ); 

    unsigned vertexFormat = vertexList->GetFormat() & ~PDDI_V_COLOUR_MASK;
    switch(vertexFormat)
    {
        case PDDI_V_C:
            if( oc && op ){         //with color and position offset
                for( i = 0; i < nIndex; i++ ){
                    INDEXED_STREAM_COLOUR_WITH_OFFSET( i, c0, oc );
                    INDEXED_STREAM_POSITION_WITH_OFFSET( i, p, op );
                }
            }
            else if( op ){      //position offset
                for( i = 0; i < nIndex; i++ ){
                    stream->Colour( c0[ indices[ i ] ] );                    
                    INDEXED_STREAM_POSITION_WITH_OFFSET( i, p, op );
                }
                
            }
            else if( oc ){  //colour offset only
                for( i = 0; i < nIndex; i++ ){                    
                    INDEXED_STREAM_COLOUR_WITH_OFFSET( i, c0, oc );
                    stream->Coord( p[ indices[ i ] ].x, p[ indices[ i ] ].y, p[ indices[ i ] ].z );
                }
            }
            else{       //no offset
                for( i = 0; i < nIndex; i++ ){                    
                    stream->Vertex(&p[indices[i]], c0[indices[i]]);
                }
            }
        break;

        case PDDI_V_N:
            if( on && op){         //with normal and position offset
                for( i = 0; i < nIndex; i++ ){
                    INDEXED_STREAM_NORMAL_WITH_OFFSET( i, n, on );
                    INDEXED_STREAM_POSITION_WITH_OFFSET( i, p, op );
                }
            }
            else if( op ){      //with position offset
                for( i = 0; i < nIndex; i++ ){
                    stream->Normal( n[ indices[ i ] ].x, n[ indices[ i ] ].y, n[ indices[ i ] ].z );
                    INDEXED_STREAM_POSITION_WITH_OFFSET( i, p, op ); 
                }
                
            }
            if( on ){    //with normal offset
                for( i = 0; i < nIndex; i++ ){                    
                    INDEXED_STREAM_NORMAL_WITH_OFFSET( i, n, on );
                    stream->Coord( p[ indices[ i ] ].x, p[ indices[ i ] ].y, p[ indices[ i ] ].z );
                }
            }
            else{   //no offset
                for( i = 0; i < nIndex; i++ ){                    
                    stream->Vertex( &p[indices[i]], &n[indices[i]] );
                }
            }
        break;

        case PDDI_V_T:
            if( ot0  && op ){         //with UV and position offset
                for( i = 0; i < nIndex; i++ )
                    INDEXED_STREAM_UV_WITH_OFFSET( i, t0, ot0, 0 );
                    INDEXED_STREAM_POSITION_WITH_OFFSET( i, p, op ); 

            }
            else if( op ){      //position offset only
                for( i = 0; i < nIndex; i++ ){
                    stream->UV( t0[ indices[ i ] ].u, t0[ indices[ i ] ].v, 0 );
                    INDEXED_STREAM_POSITION_WITH_OFFSET( i, p, op );
                }
            }
            else if( ot0 ){    //with uv offset only
                for( i = 0; i < nIndex; i++ ){                    
                    INDEXED_STREAM_UV_WITH_OFFSET( i, t0, ot0, 0 );
                    stream->Coord( p[ indices[ i ] ].x, p[ indices[ i ] ].y, p[ indices[ i ] ].z );
                }
            }
            else{   //no offset
                for( i = 0; i < nIndex; i++ ){                    
                    stream->Vertex(&p[indices[i]], &t0[indices[i]]);
                }
            }

        break;

        case PDDI_V_CT:
            if( oc && ot0 && op){         //with color, uv and position offset
                for( i = 0; i < nIndex; i++ ){
                    INDEXED_STREAM_COLOUR_WITH_OFFSET( i, c0, oc );
                    INDEXED_STREAM_UV_WITH_OFFSET( i, t0, ot0, 0 );
                    INDEXED_STREAM_POSITION_WITH_OFFSET( i, p, op );
                }
            }
            else if( oc && ot0 ){      //color and uv offset
                for( i = 0; i < nIndex; i++ ){
                    INDEXED_STREAM_COLOUR_WITH_OFFSET( i, c0, oc );
                    INDEXED_STREAM_UV_WITH_OFFSET( i, t0, ot0, 0 );
                    stream->Coord( p[ indices[ i ] ].x, p[ indices[ i ] ].y, p[ indices[ i ] ].z );
                }                
            }
            else if( ot0 && op ){       //uv and position offset
                for( i = 0; i < nIndex; i++ ){
                    stream->Colour( c0[ indices[ i ] ] ); 
                    INDEXED_STREAM_UV_WITH_OFFSET( i, t0, ot0, 0 );
                    INDEXED_STREAM_POSITION_WITH_OFFSET( i, p, op );
                }
            }
            else if( oc && op ){    //colour and position offset
                for( i = 0; i < nIndex; i++ ){
                    INDEXED_STREAM_COLOUR_WITH_OFFSET( i, c0, oc );
                    stream->UV( t0[ indices[ i ] ].u, t0[ indices[ i ] ].v, 0 );
                    INDEXED_STREAM_POSITION_WITH_OFFSET( i, p, op );
                }
            }
            else if( oc ){      //colour offset only
                for( i = 0; i < nIndex; i++ ){
                    INDEXED_STREAM_COLOUR_WITH_OFFSET( i, c0, oc );
                    stream->UV( t0[ indices[ i ] ].u, t0[ indices[ i ] ].v, 0 );
                    stream->Coord( p[ indices[ i ] ].x, p[ indices[ i ] ].y, p[ indices[ i ] ].z );
                }  
            }
            else if( ot0 ){     //uv offset only
                for( i = 0; i < nIndex; i++ ){
                    stream->Colour( c0[ indices[ i ] ] );
                    INDEXED_STREAM_UV_WITH_OFFSET( i, t0, ot0, 0 );
                    stream->Coord( p[ indices[ i ] ].x, p[ indices[ i ] ].y, p[ indices[ i ] ].z );
                }
            }
            else if( op ){      //position offset only
                for( i = 0; i < nIndex; i++ ){
                    stream->Colour( c0[ indices[ i ] ] );
                    stream->UV( t0[ indices[ i ] ].u, t0[ indices[ i ] ].v, 0 );
                    INDEXED_STREAM_POSITION_WITH_OFFSET( i, p, op );
                }
            }
            else{ //no offset
                for( i = 0; i < nIndex; i++ ){
                // Bug fix: Vertex format is PDDI_V_CT --> Colour, not Normal !!
                    stream->Vertex(&p[indices[i]], c0[indices[i]], /* &n[indices[i]], */ &t0[indices[i]]);
                }
            }
            
        break;

        case PDDI_V_NT:

            if( ot0 && on && op){         //with uv, normal and position offset
                for( i = 0; i < nIndex; i++ ){
                    INDEXED_STREAM_UV_WITH_OFFSET( i, t0, ot0, 0 );
                    INDEXED_STREAM_NORMAL_WITH_OFFSET( i, n, on );
                    INDEXED_STREAM_POSITION_WITH_OFFSET( i, p, op );
                }            
            }
            else if( ot0 && on ){   //with uv and normal offset
                for( i = 0; i < nIndex; i++ ){
                    INDEXED_STREAM_UV_WITH_OFFSET( i, t0, ot0, 0 );
                    INDEXED_STREAM_NORMAL_WITH_OFFSET( i, n, on );
                    stream->Coord( p[ indices[ i ] ].x, p[ indices[ i ] ].y, p[ indices[ i ] ].z );
                }   
            }
            else if( ot0 && op ){   //with uv and position offset
                for( i = 0; i < nIndex; i++ ){
                    INDEXED_STREAM_UV_WITH_OFFSET( i, t0, ot0, 0 );
                    stream->Normal( n[ indices[ i ] ].x, n[ indices[ i ] ].y, n[ indices[ i ] ].z );
                    INDEXED_STREAM_POSITION_WITH_OFFSET( i, p, op );
                }      
            }
            else if( on && op ){    //with normal and position offset
               for( i = 0; i < nIndex; i++ ){
                    stream->UV( t0[ indices[ i ] ].u, t0[ indices[ i ] ].v, 0 );
                    INDEXED_STREAM_NORMAL_WITH_OFFSET( i, n, on );
                    INDEXED_STREAM_POSITION_WITH_OFFSET( i, p, op );
               }    
            }
            else if( ot0 ){     //uv offset only
               for( i = 0; i < nIndex; i++ ){
                    INDEXED_STREAM_UV_WITH_OFFSET( i, t0, ot0, 0 );
                    stream->Normal( n[ indices[ i ] ].x, n[ indices[ i ] ].y, n[ indices[ i ] ].z );
                    stream->Coord( p[ indices[ i ] ].x, p[ indices[ i ] ].y, p[ indices[ i ] ].z );
               }   
            }
            else if( on ){      //normal offset only
                for( i = 0; i < nIndex; i++ ){
                    stream->UV( t0[ indices[ i ] ].u, t0[ indices[ i ] ].v, 0 );
                    INDEXED_STREAM_NORMAL_WITH_OFFSET( i, n, on );
                    stream->Coord( p[ indices[ i ] ].x, p[ indices[ i ] ].y, p[ indices[ i ] ].z );
                }    
            }
            else if( op ){  //position offset only
                for( i = 0; i < nIndex; i++ ){
                    stream->UV( t0[ indices[ i ] ].u, t0[ indices[ i ] ].v, 0 );
                    stream->Normal( n[ indices[ i ] ].x, n[ indices[ i ] ].y, n[ indices[ i ] ].z );
                    INDEXED_STREAM_POSITION_WITH_OFFSET( i, p, op );
                }   
            }
            else{       //no offset 
                for( i = 0; i < nIndex; i++ ){
                    stream->Vertex(&p[indices[i]], &n[indices[i]], &t0[indices[i]]);
                }   

            }
        break;

        default:
            // warning:  This will probably be slower than you want...
            for(i=0; i < nIndex; i++)
            {
                if(n){
                    if( on )
                        stream->Normal( n[ indices[ i ] ].x + on[ indices[ i ] ].x, n[ indices[ i ] ].y + on[ indices[ i ] ].y, n[ indices[ i ] ].z + on[ indices[ i ] ].z ); 
                    else
                        stream->Normal(n[ indices[ i ] ].x, n[ indices[ i ] ].y, n[ indices[ i ] ].z);
                }

                if(b)  stream->Binormal(b[ indices[ i ] ].x, b[ indices[ i ] ].y, b[ indices[ i ] ].z);
                if(t)  stream->Tangent(t[ indices[ i ] ].x, t[ indices[ i ] ].y, t[ indices[ i ] ].z);

                if(c0){  
                    if( oc ){
                        int r, g, b, a;
                        r = c0[ indices[ i ] ].Red( ) + oc[ indices[ i ] ].red;
                        g = c0[ indices[ i ] ].Green( ) + oc[ indices[ i ]].green;
                        b = c0[ indices[ i ] ].Blue( ) + oc[ indices[ i ] ].blue;
                        a = c0[ indices[ i ] ].Alpha( ) + oc[ indices[ i ] ].alpha;
                        colour.Set( r, g, b, a );       
                        stream->Colour( colour );
                    }
                    else 
                        stream->Colour( c0[ indices[ i ] ] );
                }
                int j;
                for(j = 1; j < tVertexList::MAX_NUM_COLOUR_SETS; j++)
                {
                    if(c[j]) stream->Colour( c[j][ indices[ i ] ] );
                }                

                if(t0){ 
                    if( ot0 )
                        stream->UV( t0[ indices[ i ] ].u + ot0[ indices[ i ] ].u, t0[ indices[ i ] ].v + ot0[ indices[ i ] ].v, 0 );
                    else
                        stream->UV( t0[ indices[ i ] ].u, t0[ indices[ i ] ].v, 0 );
                }

                if(t1){ 
                    if( ot1 )
                        stream->UV( t1[ indices[ i ] ].u + ot1[ indices[ i ] ].u, t1[ indices[ i ] ].v + ot1[ indices[ i ] ].v, 1 );
                    else
                        stream->UV( t1[ indices[ i ] ].u, t1[ indices[ i ] ].v, 1 );
                }

                if(t2){ 
                    if( ot2 )
                        stream->UV( t2[ indices[ i ] ].u + ot2[ indices[ i ] ].u, t2[ indices[ i ] ].v + ot2[ indices[ i ] ].v, 2 );
                    else
                        stream->UV( t2[ indices[ i ] ].u, t2[ indices[ i ] ].v, 2 );
                }               
                
                if(t3){ 
                    if( ot3 )
                        stream->UV( t3[ indices[ i ] ].u + ot3[ indices[ i ] ].u, t3[ indices[ i ] ].v + ot3[ indices[ i ] ].v, 3 );
                    else
                        stream->UV( t3[ indices[ i ] ].u, t3[ indices[ i ] ].v, 3 );
                }

                if( op )
                    stream->Coord( p[ indices[ i ] ].x + op[ indices[ i ] ].x, p[ indices[ i ] ].y + op[ indices[ i ] ].y, p[ indices[ i ] ].z + op[ indices[ i ] ].z );                
                else
                    stream->Coord( p[ indices[ i ] ].x, p[ indices[ i ] ].y, p[ indices[ i ] ].z );

            }
        break;
    }
}

void tPrimGroupStreamed::IndexedDisplay(pddiPrimBufferStream* stream)
{
    IndexedDisplayInternal(mVertexList, stream, mIndices, mIndexCount);
}

void tPrimGroupStreamed::IndexedDisplay(pddiPrimStream* stream)
{
    if( mVertexList->HasOffsets( ) )
    {
        rAssert( false );
        //UNUSED CODE
        //IndexedDisplayWithOffsetInternal(mVertexList, stream, mIndices, mIndexCount);
    }
    else
    {
        IndexedDisplayInternal(mVertexList, stream, mIndices, mIndexCount);
    }
}

void tPrimGroupStreamed::NonIndexedDisplay(pddiPrimStream* stream)
{
    mVertexList->Stream(stream, 0, mVertexList->GetNumVertex());
}


//************************************************************************************
//************************************************************************************
//
// tPrimGroupLoader
// Default constructor
//
//
tPrimGroupLoader::tPrimGroupLoader(void) 
: 
mShader(NULL),
mVertexFormatMask(0xFFFFFFFF)
{
}

//*******************************************************
//
// tPrimGroupLoader
// Destructor
//
//
tPrimGroupLoader::~tPrimGroupLoader()
{
    tRefCounted::Release(mShader);
}


//*******************************************************
//
// tPrimGroupLoader
// ParseHeader
//
//
bool tPrimGroupLoader::ParseHeader(tChunkFile *f, tEntityStore *store)
{
    if (f->GetCurrentID() != Pure3D::Mesh::PRIMGROUP) return false;

    int version = f->GetLong();

    char ShaderName[255];
    f->GetPString(ShaderName);
    tRefCounted::Release(mShader);
    mShader = p3d::find<tShader>(store, ShaderName);

    if(mShader)
    {
        mShader->AddRef();
    }
    else
    {
        //
        // Allocate an error shader if shader isnt found
        //
        p3d::printf("warning : shader \"%s\" not found during primgroup load\n", ShaderName);
        mShader = new tShader("error");
        mShader->AddRef();
    }


    mPrimType     = (pddiPrimType) f->GetLong();
    mVertexFormat = (unsigned int) f->GetLong() & mVertexFormatMask;
    mVertexCount  = (unsigned int) f->GetLong();
    mIndexCount   = (unsigned int) f->GetLong();
    mMatrixCount  = (unsigned int) f->GetLong();

    mVertexFormat |= PDDI_V_POSITION; // position didn't used to exist, so all old prim groups will be missing it

    return true;
}


#ifndef RAD_PS2
tPrimGroup* tPrimGroupLoader::LoadPCSkin(tChunkFile* f, tEntityStore* store, rmt::Matrix* bones)
{
	//don't create a primBuffer whose vertex contains weights and matrices indices 
	//for PC, since PC skin is software blended, the weights and matrices indices
	//are stored separately( in SkinVertex ), exclude binormal & tangent also
    mVertexFormat = mVertexFormat & ~(PDDI_V_WEIGHTS | PDDI_V_INDICES | PDDI_V_BINORMAL | PDDI_V_TANGENT );
    
	pddiPrimBuffer* primBuffer = NULL;
	pddiPrimBufferStream *stream = NULL;
    tPrimGroupOptimised* primGroup = NULL;

	tPrimGroupSkinnedPC* skinPrimGroup = NULL;


	skinPrimGroup = new tPrimGroupSkinnedPC(mVertexCount, mVertexFormat, mIndexCount, true);

	primGroup = skinPrimGroup;

    for(int i = 0; i < mVertexCount; i++)
    {
        skinPrimGroup->mVertices[i].weights[0] = 1.0f;
        skinPrimGroup->mVertices[i].weights[1] = 0.0f;
        skinPrimGroup->mVertices[i].weights[2] = 0.0f;
    }

    primGroup->SetShader(mShader);
    primGroup->SetPrimType(mPrimType);
    primGroup->SetVertexFormat(mVertexFormat);


	bool primBufferInitialized = false;
    
    int nUVChannel = mVertexFormat & 0xf;
    int count;    

    while (f->ChunksRemaining())
    {
        f->BeginChunk();

        switch (f->GetCurrentID())
        {
            case Pure3D::Mesh::POSITIONLIST:
            case Pure3D::Mesh::NORMALLIST:
            case Pure3D::Mesh::PACKEDNORMALLIST:
            case Pure3D::Mesh::COLOURLIST:
            case Pure3D::Mesh::UVLIST:
                if(!primBufferInitialized)
                {
                    primBufferInitialized = true;
                    pddiPrimBufferDesc desc(mPrimType, mVertexFormat, mVertexCount, mIndexCount);
                    desc.SetMatrixCount(mMatrixCount);
                    primBuffer  = p3d::device->NewPrimBuffer(&desc);
                    primGroup->SetBuffer(primBuffer);
                   
                }
            break;

            default:
            break;
        }

        if(primBufferInitialized)
        {
            stream = primBuffer->Lock();
        }

        switch (f->GetCurrentID())
        {
            case Pure3D::Mesh::MEMORYIMAGEVERTEXLIST:
            case Pure3D::Mesh::MEMORYIMAGEINDEXLIST:
                P3DASSERT(0 && "Can't load memory imaged geometry as streamed!");
                break;

            case Pure3D::Mesh::POSITIONLIST:
			{
                count = f->GetLong();
                P3DASSERT(count == mVertexCount);
				pddiVector v;
                for (int a = 0; a < count; a++)
                {					
                    f->GetData(&v, 3, tFile::DWORD);
                    stream->Coord(v.x, v.y, v.z);
                    if(skinPrimGroup)
                        skinPrimGroup->mVertices[a].position = v;

                }
			}
            break;

            case Pure3D::Mesh::NORMALLIST:
			{
                if (!(mVertexFormat & PDDI_V_NORMAL)) break;
                count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                pddiVector v;
                for (int a = 0; a < count; a++) 
                {
                    f->GetData(&v, 3, tFile::DWORD);
                    stream->Normal(v.x, v.y, v.z);
                    stream->Next();
					if(skinPrimGroup)
                        skinPrimGroup->mVertices[a].normal = v;
                }
			}
            break;

#ifdef RAD_XBOX
            case Pure3D::Mesh::BINORMALLIST:
            {
                P3DASSERT(primBufferInitialized);
                if (!(mVertexFormat & PDDI_V_BINORMAL)) break;
                int count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                pddiVector v;
                for (int a = 0; a < count; a++) 
                {
                    f->GetData(&v, 3, tFile::DWORD);
                    stream->Binormal(v.x, v.y, v.z);	
                    stream->Next();
                }
            }
            break;

            case Pure3D::Mesh::TANGENTLIST:
            {
                P3DASSERT(primBufferInitialized);
                if (!(mVertexFormat & PDDI_V_TANGENT)) break;
                int count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                pddiVector v;
                for (int a = 0; a < count; a++) 
                {
                    f->GetData(&v, 3, tFile::DWORD);
                    stream->Tangent(v.x, v.y, v.z);
                    stream->Next();
                }
            }
            break;

#endif

            case Pure3D::Mesh::COLOURLIST:
            {
                P3DASSERT(primBufferInitialized);
                if (!(mVertexFormat & PDDI_V_COLOUR)) break;
                int count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                pddiColour c;
                for (int a = 0; a < count; a++) 
                {
                    f->GetData(&c, 1, tFile::DWORD);
                    stream->Colour(c);
                    stream->Next();
                }
            }
            break;

            case Pure3D::Mesh::UVLIST:
            {
                P3DASSERT(primBufferInitialized);
                int count   = f->GetLong();
                int channel = f->GetLong();
                if(channel >= nUVChannel) break;
                P3DASSERT(count == mVertexCount);
                pddiVector2 uv;
                for (int a = 0; a < count; a++) 
                {
                    f->GetData(&uv, 2, tFile::DWORD);
                    stream->UV(uv.u, uv.v, channel);
                    stream->Next();
                }
            }
            break;

            case Pure3D::Mesh::INDEXLIST:
            {
                P3DASSERT(primBufferInitialized);
                int count = f->GetLong();
                P3DASSERT(count == mIndexCount);
                unsigned short* tempIndexList = (unsigned short *)p3d::MallocTemp(sizeof(unsigned short) * count);
                for (int a = 0; a < count; a++) 
                {
                    tempIndexList[a] = (unsigned short)f->GetLong();
                }
                primBuffer->SetIndices(tempIndexList, count);
                p3d::FreeTemp(tempIndexList);
            }
            break;

            case Pure3D::Mesh::WEIGHTLIST:
            {
                P3DASSERT(primBufferInitialized);
                int count = f->GetLong();
                P3DASSERT(count == mVertexCount);           
                for (int a = 0; a < count; a++)
                {
					f->GetData(skinPrimGroup->mVertices[a].weights, 3, tFile::DWORD);
                }
            }
            break;

            case Pure3D::Mesh::MATRIXLIST:
            {
                P3DASSERT(skinPrimGroup);
                int count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                unsigned char indices[4];
                for (int a = 0; a < count; a++)
                {
                    f->GetData(indices, 4, tFile::BYTE);
                    skinPrimGroup->mVertices[a].indices[0] = indices[3];
                    skinPrimGroup->mVertices[a].indices[1] = indices[2];
                    skinPrimGroup->mVertices[a].indices[2] = indices[1];
                    skinPrimGroup->mVertices[a].indices[3] = indices[0];
                }
            }
            break;

            case Pure3D::Mesh::MATRIXPALETTE:
            {
                P3DASSERT(skinPrimGroup);
                int count = f->GetLong();
                skinPrimGroup->nMatrices = count;
                skinPrimGroup->matrixPalette = new Matrix*[count];
                for(int a = 0; a < count; a++)
                {
                    unsigned index;
                    f->GetData(&index, 1, tFile::DWORD);
                    skinPrimGroup->matrixPalette[a] = &bones[index];
                }
            }
            break;

            case Pure3D::Mesh::INSTANCEINFO:
            {
                primGroup->instanceCount = (unsigned int)f->GetLong();
                unsigned vCount = (unsigned int)f->GetLong();
                unsigned iCount = (unsigned int)f->GetLong();

                primGroup->instanceSize = (mIndexCount > 0) ? iCount : vCount;
            }
            break;

            default:
                break;

        }

        if(stream)
        {
            primBuffer->Unlock(stream);
            stream = NULL;
        }

        f->EndChunk();
    }

    return primGroup;
}
#endif //RAD_PS2

tPrimGroup* tPrimGroupLoader::LoadStreamed(tChunkFile* f, tEntityStore* store, rmt::Matrix* bones)
{
    mVertexFormat = mVertexFormat & ~(PDDI_V_WEIGHTS | PDDI_V_INDICES);
    
    tPrimGroupStreamed* primGroup = NULL;

    tPrimGroupSkinnedStreamed* skinPrimGroup = NULL;

    if(bones)
    {

        skinPrimGroup = new tPrimGroupSkinnedStreamed(mVertexCount, mVertexFormat, mIndexCount, true);
        primGroup = skinPrimGroup;

        for(int i = 0; i < mVertexCount; i++)
        {
            skinPrimGroup->mVertices[i].weights[0] = 1.0f;
            skinPrimGroup->mVertices[i].weights[1] = 0.0f;
            skinPrimGroup->mVertices[i].weights[2] = 0.0f;
        }
    }
    else
    {
        primGroup = new tPrimGroupStreamed(mVertexCount, mVertexFormat, mIndexCount, true);
    }

    primGroup->SetShader(mShader);
    primGroup->SetPrimType(mPrimType);
    primGroup->SetVertexFormat(mVertexFormat);

    #ifdef RAD_XBOX
        if( IsVertexShader( mShader ) )
        {
            pddiExtVertexProgram* vp = (pddiExtVertexProgram*)p3d::pddi->GetExtension(PDDI_EXT_VERTEX_PROGRAM);
  
            primGroup->vertexProgram = vp->GetVertexProgram(mShader->GetType(), mPrimType, mVertexFormat, pddiExtVertexProgram::COMPRESSED);
        }

    #endif

    int nColourChannels = PddiNumColourSets(mVertexFormat);
    int nUVChannel = mVertexFormat & 0xf;
    int count, channel, a;

    tVertexList *vl = primGroup->GetVertexList();

    while (f->ChunksRemaining())
    {
        f->BeginChunk();

        switch (f->GetCurrentID())
        {
            case Pure3D::Mesh::MEMORYIMAGEVERTEXLIST:
            case Pure3D::Mesh::MEMORYIMAGEINDEXLIST:
                P3DASSERT(0 && "Can't load memory imaged geometry as streamed!");
                break;

            case Pure3D::Mesh::POSITIONLIST:
                count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                for (a = 0; a < count; a++) 
                {
                    f->GetData(&vl->GetPositions()[a], 3, tFile::DWORD);
                    if(skinPrimGroup)
                        skinPrimGroup->mVertices[a].position = vl->GetPositions()[a];
                }
                break;

            case Pure3D::Mesh::NORMALLIST:
                if (!(mVertexFormat & PDDI_V_NORMAL)) break;
                count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                for (a = 0; a < count; a++) 
                {
                    f->GetData(&vl->GetNormals()[a], 3, tFile::DWORD);
                    if(skinPrimGroup)
                        skinPrimGroup->mVertices[a].normal = vl->GetNormals()[a];
                }
                break;

            case Pure3D::Mesh::BINORMALLIST:
                if (!(mVertexFormat & PDDI_V_BINORMAL)) break;
                count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                for (a = 0; a < count; a++)
                {
                    f->GetData(&vl->GetBinormals()[a], 3, tFile::DWORD);
                }
                break;

            case Pure3D::Mesh::TANGENTLIST:
                if (!(mVertexFormat & PDDI_V_TANGENT)) break;
                count= f->GetLong();
                P3DASSERT(count == mVertexCount);
                for(a = 0; a < count; a++)
                {
                    f->GetData(&vl->GetTangents()[a], 3, tFile::DWORD);
                }
                break;

            case Pure3D::Mesh::COLOURLIST:
                if (!(mVertexFormat & PDDI_V_COLOUR)) break;
                count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                for (a = 0; a < count; a++) 
                {
                    f->GetData(&vl->GetColours(0)[a], 1, tFile::DWORD);
                }
                break;

            case Pure3D::Mesh::MULTICOLOURLIST:
                if (!(mVertexFormat & PDDI_V_COLOUR2)) break;
                count   = f->GetLong();
                channel = f->GetLong();
                P3DASSERT(count == mVertexCount);
                P3DASSERT(channel < tVertexList::MAX_NUM_COLOUR_SETS);
                for (a = 0; a < count; a++) 
                {
                    f->GetData(&vl->GetColours(channel)[a], 1, tFile::DWORD);
                }
                break;

            case Pure3D::Mesh::UVLIST:
                count   = f->GetLong();
                channel = f->GetLong();
                if(channel >= nUVChannel) break;
                P3DASSERT(count == mVertexCount);
                P3DASSERT(channel < tVertexList::MAX_NUM_UV_SETS);
                for (a = 0; a < count; a++) 
                {
                    f->GetData(&vl->GetUVs(channel)[a], 2, tFile::DWORD);
                }
                break;

            case Pure3D::Mesh::INDEXLIST:
                count = f->GetLong();
                P3DASSERT(count == mIndexCount);
                for (a = 0; a < count; a++)
                {
                    primGroup->GetIndices()[a] = (unsigned short) f->GetLong();
                }
                break;

            case Pure3D::Mesh::WEIGHTLIST:
            {
                P3DASSERT(skinPrimGroup);
                int count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                for (int a = 0; a < count; a++)
                {
                    f->GetData(skinPrimGroup->mVertices[a].weights, 3, tFile::DWORD);
                }
            }
            break;

            case Pure3D::Mesh::MATRIXLIST:
            {
                P3DASSERT(skinPrimGroup);
                int count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                unsigned char indices[4];
                for (int a = 0; a < count; a++)
                {
                    f->GetData(indices, 4, tFile::BYTE);
                    skinPrimGroup->mVertices[a].indices[0] = indices[3];
                    skinPrimGroup->mVertices[a].indices[1] = indices[2];
                    skinPrimGroup->mVertices[a].indices[2] = indices[1];
                    skinPrimGroup->mVertices[a].indices[3] = indices[0];
                }
            }
            break;

            case Pure3D::Mesh::MATRIXPALETTE:
            {
                P3DASSERT(skinPrimGroup);
                int count = f->GetLong();
                skinPrimGroup->nMatrices = count;
                skinPrimGroup->matrixPalette = new Matrix*[count];
                for(int a = 0; a < count; a++)
                {
                    unsigned index;
                    f->GetData(&index, 1, tFile::DWORD);
                    skinPrimGroup->matrixPalette[a] = &bones[index];
                }
            }
            break;

            case Pure3D::Mesh::INSTANCEINFO:
            {
                primGroup->instanceCount = (unsigned int)f->GetLong();
                unsigned vCount = (unsigned int)f->GetLong();
                unsigned iCount = (unsigned int)f->GetLong();

                primGroup->instanceSize = (mIndexCount > 0) ? iCount : vCount;
            }
            break;

            default:
                break;

        }
        f->EndChunk();
    }

    return primGroup;
}

//-------------------------------------------------------------------------------------------------------------------
tPrimGroup*  tPrimGroupLoader::Load(tChunkFile *f, tEntityStore *store, rmt::Matrix* bones, bool optimise, bool deform)
{
    pddiExtHardwareSkinning* hwSkin = p3d::context->GetHardwareSkinning();

    if(!ParseHeader(f, store))
    {
        return NULL;
    }
/*    
    if((bones && !hwSkin) || !optimise)
    {
        return LoadStreamed(f, store, bones);
    }
*/
    // Non-optimised skins can be deformed by expression animation and are
    // intentionally backed by the PC CPU representation. Advertising the
    // Vulkan hardware extension made these assets fall through to
    // tPrimGroupSkinnedStreamed, whose legacy draw path assumes a
    // pddiBaseShader (the Vulkan shader is a direct pddiShader). Keep those
    // exceptional/deformable assets on the compatible path; ordinary
    // optimised characters still use GPU skinning.
    if( bones && (!hwSkin || !optimise) )
    {
        #ifdef RAD_PS2
            rAssert( false );
            return NULL;
        #else
            return LoadPCSkin(f, store, bones);
        #endif
    }
	else if( !optimise )
    {
		return LoadStreamed(f, store, bones);
    }

#ifdef RAD_PS2
    if (mIndexCount > 0)
    {
        return LoadStreamed(f, store, bones);
    }
#endif
    
    rmt::Vector* tmpPositions = NULL;
    if(deform)
    {
        tmpPositions = (rmt::Vector*)p3d::MallocTemp(sizeof(rmt::Vector) * mVertexCount);
    }

    int version;
    unsigned param;
    unsigned bufferSize;

    tPrimGroupOptimised* primGroup;
    
    if(bones)
    {
        primGroup = new tPrimGroupSkinnedOptimised(mVertexCount);
    }
    else
    {
        primGroup = new tPrimGroupOptimised(mVertexCount);
    }

    pddiPrimBuffer* primBuffer = NULL;
    bool primBufferInitialized = false;
    
    primGroup->SetShader(mShader);
    primGroup->SetPrimType(mPrimType);
    primGroup->SetVertexFormat(mVertexFormat);


    int nColourChannels = PddiNumColourSets(mVertexFormat);
    int nUVChannel = mVertexFormat & 0xf;

    pddiPrimBufferStream *stream = NULL;

    bool memoryImaged = false;

    while (f->ChunksRemaining())
    {
        f->BeginChunk();
        switch (f->GetCurrentID())
        {
#ifdef RAD_GAMECUBE
            case Pure3D::Mesh::MEMORYIMAGEVERTEXDESCRIPTIONLIST:
                memoryImaged = true;
                primBufferInitialized = true;
                pddiPrimBufferDesc desc(mPrimType, mVertexFormat, mVertexCount, mIndexCount);
                desc.SetMemoryImaged(true);
                desc.SetMatrixCount(mMatrixCount);
                primBuffer = p3d::device->NewPrimBuffer(&desc);
                primGroup->SetBuffer(primBuffer);
            break;
    
#else
            case Pure3D::Mesh::MEMORYIMAGEVERTEXLIST:
                {
                    P3DASSERT(!primBufferInitialized);
                    memoryImaged = true;
                    primBufferInitialized = true;
                    pddiPrimBufferDesc desc(mPrimType, mVertexFormat, mVertexCount, mIndexCount);
                    desc.SetMemoryImaged(true);
                    desc.SetMatrixCount(mMatrixCount);
                    primBuffer  = p3d::device->NewPrimBuffer(&desc);
                    primGroup->SetBuffer(primBuffer);

                    #ifdef RAD_XBOX
					if( IsVertexShader( mShader ) )                    
					{
                        pddiExtVertexProgram* vp = (pddiExtVertexProgram*)p3d::pddi->GetExtension(PDDI_EXT_VERTEX_PROGRAM);
                        unsigned handle;

                        if( strcmp(mShader->GetType(), "toon")==0 && bones )
                            handle= vp->GetVertexProgram( "skin_toon", mPrimType, mVertexFormat, pddiExtVertexProgram::COMPRESSED);                        
                        else if( strcmp(mShader->GetType(), "refract")==0 && bones )
                            handle= vp->GetVertexProgram( "skin_refract", mPrimType, mVertexFormat, pddiExtVertexProgram::COMPRESSED);
                        else
                            handle= vp->GetVertexProgram( mShader->GetType(), mPrimType, mVertexFormat, pddiExtVertexProgram::COMPRESSED);

                        vp->SetBufferProgram(primBuffer, handle);
                    }
                    #endif
                }
            break;
#endif
            case Pure3D::Mesh::POSITIONLIST:
            case Pure3D::Mesh::NORMALLIST:
            case Pure3D::Mesh::PACKEDNORMALLIST:
            case Pure3D::Mesh::COLOURLIST:
            case Pure3D::Mesh::UVLIST:
#ifdef  RAD_XBOX
            case Pure3D::Mesh::BINORMALLIST:
            case Pure3D::Mesh::TANGENTLIST:
#endif
                if(!primBufferInitialized)
                {
                    primBufferInitialized = true;
                    pddiPrimBufferDesc desc(mPrimType, mVertexFormat, mVertexCount, mIndexCount);
                    desc.SetMatrixCount(mMatrixCount);
                    primBuffer  = p3d::device->NewPrimBuffer(&desc);
                    primGroup->SetBuffer(primBuffer);
                    
#ifdef RAD_XBOX
                    if( IsVertexShader( mShader ) )
                    {
                        pddiExtVertexProgram* vp = (pddiExtVertexProgram*)p3d::pddi->GetExtension(PDDI_EXT_VERTEX_PROGRAM);
                        unsigned handle;
                        
                        if( strcmp(mShader->GetType(), "toon")==0 && bones )
                            handle= vp->GetVertexProgram( "skin_toon", mPrimType, mVertexFormat, pddiExtVertexProgram::COMPRESSED);                        
                        else if( strcmp(mShader->GetType(), "refract")==0 && bones )
                            handle= vp->GetVertexProgram( "skin_refract", mPrimType, mVertexFormat, pddiExtVertexProgram::COMPRESSED);
                        else
                            handle= vp->GetVertexProgram( mShader->GetType(), mPrimType, mVertexFormat, pddiExtVertexProgram::COMPRESSED);
                        
                        vp->SetBufferProgram(primBuffer, handle);
                    }
#endif
                }
            break;

            default:
            break;
        }

#ifdef RAD_PS2
        if(primBufferInitialized && !memoryImaged)
#else
        if(primBufferInitialized)
#endif
        {
            stream = primBuffer->Lock();
        }

        switch (f->GetCurrentID())
        {
            case Pure3D::Mesh::POSITIONLIST:
            {
                P3DASSERT(primBufferInitialized);
                int count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                pddiVector v;
                for (int a = 0; a < count; a++)
                {
                    f->GetData(&v, 3, tFile::DWORD);
                    stream->Coord(v.x, v.y, v.z);
                    if(tmpPositions)
                    {
                        tmpPositions[a] = *(rmt::Vector*)&v;
                    }

                }
            }
            break;

            case Pure3D::Mesh::NORMALLIST:
            {
                P3DASSERT(primBufferInitialized);
                if (!(mVertexFormat & PDDI_V_NORMAL)) break;
                int count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                pddiVector v;
                for (int a = 0; a < count; a++) 
                {
                    f->GetData(&v, 3, tFile::DWORD);
                    stream->Normal(v.x, v.y, v.z);
                    stream->Next();
                }
            }
            break;
#ifdef RAD_XBOX
            case Pure3D::Mesh::BINORMALLIST:
            {
                P3DASSERT(primBufferInitialized);
                if (!(mVertexFormat & PDDI_V_BINORMAL)) break;
                int count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                pddiVector v;
                for (int a = 0; a < count; a++) 
                {
                    f->GetData(&v, 3, tFile::DWORD);
                    stream->Binormal(v.x, v.y, v.z);	
                    stream->Next();
                }
            }
            break;

            case Pure3D::Mesh::TANGENTLIST:
            {
                P3DASSERT(primBufferInitialized);
                if (!(mVertexFormat & PDDI_V_TANGENT)) break;
                int count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                pddiVector v;
                for (int a = 0; a < count; a++) 
                {
                    f->GetData(&v, 3, tFile::DWORD);
                    stream->Tangent(v.x, v.y, v.z);
                    stream->Next();
                }
            }
            break;

#endif

            case Pure3D::Mesh::COLOURLIST:
            {
                P3DASSERT(primBufferInitialized);
                if (!(mVertexFormat & PDDI_V_COLOUR)) break;
                int count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                pddiColour c;
                for (int a = 0; a < count; a++) 
                {
                    f->GetData(&c, 1, tFile::DWORD);
                    stream->Colour(c);
                    stream->Next();
                }
            }
            break;

            case Pure3D::Mesh::MULTICOLOURLIST:
            {
                P3DASSERT(primBufferInitialized);
                if(!(mVertexFormat & PDDI_V_COLOUR2)) break;
                int count   = f->GetLong();
                int channel = f->GetLong();
                if(channel >= nColourChannels) break;
                P3DASSERT(count == mVertexCount);
                pddiColour c;
                for (int a = 0; a < count; a++) 
                {
                    f->GetData(&c, 1, tFile::DWORD);
                    stream->Colour(c, channel);
                    stream->Next();
                }
            }
            break;

            case Pure3D::Mesh::UVLIST:
            {
                P3DASSERT(primBufferInitialized);
                int count   = f->GetLong();
                int channel = f->GetLong();
                if(channel >= nUVChannel) break;
                P3DASSERT(count == mVertexCount);
                pddiVector2 uv;
                for (int a = 0; a < count; a++) 
                {
                    f->GetData(&uv, 2, tFile::DWORD);
                    stream->UV(uv.u, uv.v, channel);
                    stream->Next();
                }
            }
            break;

            case Pure3D::Mesh::INDEXLIST:
            {
                P3DASSERT(primBufferInitialized);
                int count = f->GetLong();
                P3DASSERT(count == mIndexCount);
                unsigned short* tempIndexList = (unsigned short *)p3d::MallocTemp(sizeof(unsigned short) * count);
                for (int a = 0; a < count; a++) 
                {
                    tempIndexList[a] = (unsigned short)f->GetLong();
                }
                primBuffer->SetIndices(tempIndexList, count);
                p3d::FreeTemp(tempIndexList);
            }
            break;

            case Pure3D::Mesh::WEIGHTLIST:
            {
                P3DASSERT(primBufferInitialized);
                int count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                float weight[3];
                for (int a = 0; a < count; a++)
                {
                    f->GetData(weight, 3, tFile::DWORD);
                    stream->SkinWeights(weight[0], weight[1], weight[2]);
                    stream->Next();
                }
            }
            break;

            case Pure3D::Mesh::MATRIXLIST:
            {
                P3DASSERT(primBufferInitialized);
                P3DASSERT(bones);
                int count = f->GetLong();
                P3DASSERT(count == mVertexCount);
                char index[4];
                for (int a = 0; a < count; a++)
                {
                    f->GetData(&index, 4, tFile::BYTE);
                    stream->SkinIndices(index[3], index[2], index[1], index[0]);
                    stream->Next();
                }
            }
            break;

            case Pure3D::Mesh::MATRIXPALETTE:
            {
                P3DASSERT(primBufferInitialized);
                P3DASSERT(bones);
                int count = f->GetLong();
                {
                    tPrimGroupSkinnedOptimised* skin = dynamic_cast<tPrimGroupSkinnedOptimised*>(primGroup);
                    P3DASSERT(skin);
                    skin->nMatrices = count;
                    skin->matrixPalette = new Matrix*[count];
                    for(int a = 0; a < count; a++)
                    {
                        unsigned index;
                        f->GetData(&index, 1, tFile::DWORD);
                        skin->matrixPalette[a] = &bones[index];
                    }
                }
            }
            break;

            case Pure3D::Mesh::INSTANCEINFO:
            {
                primGroup->instanceCount = (unsigned int)f->GetLong();
                unsigned vCount = (unsigned int)f->GetLong();
                unsigned iCount = (unsigned int)f->GetLong();

                primGroup->instanceSize = (mIndexCount > 0) ? iCount : vCount;
            }
            break;

            case Pure3D::Mesh::MEMORYIMAGEVERTEXLIST:
            {
                P3DASSERT(primBufferInitialized);
                memoryImaged = true;
                version = f->GetLong();
                param = f->GetLong();
                bufferSize = f->GetLong();
                P3DASSERT(primBuffer->CheckMemImageVersion(version));
                primBuffer->SetMemImageParam(f->GetCurrentID(), param);
                void *ptr = primBuffer->LockMemImage(bufferSize);
                f->GetData(ptr, bufferSize, tFile::BYTE);
                primBuffer->UnlockMemImage();
#ifdef RAD_GAMECUBE
                DCFlushRange(ptr, bufferSize);
#endif
            }
            break;

            case Pure3D::Mesh::MEMORYIMAGEVERTEXDESCRIPTIONLIST:
            {
                P3DASSERT(primBufferInitialized);
                memoryImaged = true;
                version = f->GetLong();
                param = f->GetLong();
                bufferSize = f->GetLong();
                primBuffer->SetMemImageParam(f->GetCurrentID(), param);
                void *ptr = primBuffer->LockMemImage(bufferSize);
                f->GetData(ptr, bufferSize, tFile::BYTE);
                primBuffer->UnlockMemImage();
            }
            break;

            case Pure3D::Mesh::MEMORYIMAGEINDEXLIST:
            {
                P3DASSERT(primBufferInitialized);
                memoryImaged = true;
                version = f->GetLong();
                param = f->GetLong();                                                 
                bufferSize = f->GetLong();

                primBuffer->SetMemImageParam(f->GetCurrentID(), param);
                unsigned short* index = (unsigned short*)primBuffer->LockIndexBuffer();
                f->GetData(index, bufferSize, tFile::BYTE);
                primBuffer->UnlockIndexBuffer(mIndexCount);
            }
            break;

            default:
                break;
        }

        if(stream)
        {
            primBuffer->Unlock(stream);
            stream = NULL;
        }
        f->EndChunk();
    }

    //Todo: tmpPositions should be removed when PDDI supports expression system
    if (primGroup && tmpPositions)
    {
        primGroup->tempPositions = tmpPositions;
    }

    primBuffer->Finalize();  // the PS2 requires this
    
    return primGroup;
}


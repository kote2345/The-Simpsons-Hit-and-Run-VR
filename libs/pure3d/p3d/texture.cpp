/*===========================================================================
    texture.cpp

    Copyright (c)2000 Radical Entertainment, Inc.
    All rights reserved.
===========================================================================*/
#include <p3d/texture.hpp>
#if defined(RAD_ANDROID)
#include <pddi/gles/gltex.hpp>
#elif defined(SRR2_OPENXR_PLATFORM_WIN32) && defined(SRR2_VR_RENDERER_VULKAN)
// The Vulkan backend provides the same external-material hook as GLES.
// Do not include the GLES texture implementation in the desktop build.
void pglSetTextureSourceName(pddiTexture* texture,const char* name);
#endif
#include <p3d/image.hpp>
#include <p3d/imagefactory.hpp>
#include <p3d/imageconverter.hpp>
#include <p3d/utility.hpp>
#include <p3d/file.hpp>
#include <p3d/chunkfile.hpp>
#include <constants/chunks.h>
#include <constants/chunkids.hpp>
#include <constants/srrchunks.h> // For SetChunk.

tTexture::tTexture() : texture(NULL)
{
}

tTexture::~tTexture()
{
    tRefCounted::Release(texture);
}

void tTexture::SetTexture(pddiTexture* t)
{
    tRefCounted::Assign(texture, t);
}

bool tTexture::Create(int xSize, int ySize, int bpp, int alphaDepth, int nMip,
                pddiTextureType textureType,
                pddiTextureUsageHint usageHint)
    {  
        pddiTextureDesc desc;
        desc.SetSizeX(xSize);
        desc.SetSizeY(ySize);
        desc.SetBitDepth(bpp);
        desc.SetAlphaDepth(alphaDepth);
        desc.SetMipMapCount(nMip);
        desc.SetType(textureType);
        desc.SetUsage(usageHint);
        texture = p3d::device->NewTexture(&desc);

        if(!texture)
        {
            return false;
        }

        texture->AddRef();
        return true;
    }

bool tTexture::CreateVolume(int xSize, int ySize, int zSize, int bpp, int alphaDepth, int nMip,
                        pddiTextureType textureType, 
                        pddiTextureUsageHint usageHint)
    {  
        pddiTextureDesc desc;
        desc.SetSizeX(xSize);
        desc.SetSizeY(ySize);
        desc.SetSizeZ(zSize);
        desc.SetBitDepth(bpp);
        desc.SetAlphaDepth(alphaDepth);
        desc.SetMipMapCount(nMip);
        desc.SetType(textureType);
        desc.SetUsage(usageHint);
        desc.SetVolume(true);
        texture = p3d::device->NewTexture(&desc);

        if(!texture)
        {
            return false;
        }

        texture->AddRef();
        return true;
    }

//-------------------------------------------------------------------
rmt::Randomizer tSetLoader::m_Random( 0x1A30E31E );

tSetLoader::tSetLoader()
{}

tSetLoader::~tSetLoader()
{}

void tSetLoader::SetRandomSeed( unsigned NewSeed )
{
    m_Random.Seed( NewSeed );
}

tLoadStatus tSetLoader::Load( tChunkFile* InFile, tEntityStore* TempLoadingStore )
{
    if( InFile->GetCurrentID() != SRR2::ChunkID::CHUNK_SET )
    {
        return LOAD_ERROR;
    }

    // Go through the set and pick on child at random.
    char name[256];
    InFile->GetPString( name );
    int version = InFile->GetLong();
    int childCount = InFile->GetUChar();
    int childIndex = m_Random.IntRanged( 0, childCount - 1 );
    // Skip some of the children.
    for( int i = 0; i < childIndex; ++i )
    {
        InFile->BeginChunk();
        // Do nothing with the chunk.
        InFile->EndChunk();
    }
    unsigned chunkID = InFile->BeginChunk();
    tChunkHandler* chunkHandler = p3d::loadManager->GetP3DHandler()->GetHandler( chunkID );
    tLoadStatus status = LOAD_ERROR;
    if( chunkHandler )
    {
        chunkHandler->SetNameOverride( name );
        status = chunkHandler->Load( InFile, TempLoadingStore );
    }
    InFile->EndChunk();
    return status;
}

bool tSetLoader::CheckChunkID( unsigned ID )
{
    return ID == SRR2::ChunkID::CHUNK_SET;
}

unsigned int tSetLoader::GetChunkID()
{
	return SRR2::ChunkID::CHUNK_SET;
}

//-------------------------------------------------------------------
static const int TEXTURE_VERSION_V13 = 0;
static const int IMAGE_VERSION_V13 = 0;

static const int TEXTURE_VERSION = 14000;
static const int IMAGE_VERSION = 14000;
static const int VOLUME_IMAGE_VERSION = 14000;

tTextureLoader::tTextureLoader() : tSimpleChunkHandler(Pure3D::Texture::TEXTURE) 
{
    imageFactory = new tImageFactory;
    imageConverter = new tImageConverter;
};

tTextureLoader::~tTextureLoader() 
{
    delete imageFactory;
    delete imageConverter;
};

tEntity* tTextureLoader::LoadObject(tChunkFile* f, tEntityStore* store)
{
    return LoadTexture(f);
}

tTexture* tTextureLoader::LoadTexture(tChunkFile* f)
{
    char name[128];
    f->GetPString(name);

    int version = f->GetLong();
    P3DASSERT(version == TEXTURE_VERSION);   

    int width = f->GetLong();
    int height = f->GetLong();
    int bpp = f->GetLong();
    int alphaDepth = f->GetLong();
    int numMipMaps = f->GetLong();
    numMipMaps = numMipMaps ? numMipMaps - 1 : numMipMaps;
    pddiTextureType textureType = (pddiTextureType)f->GetLong();
    pddiTextureUsageHint usage = (pddiTextureUsageHint)f->GetLong();
    int priority = f->GetLong();

    imageFactory->SetDesiredDepth(bpp);
    imageFactory->SetTextureHints(alphaDepth, numMipMaps, textureType, usage);

    tTexture* texture = NULL;

    int mipmap = 0;
    bool volume = false;
    bool image = false;
    while( (f->ChunksRemaining()) && (mipmap<=numMipMaps) )
    {           
        switch ( f->BeginChunk() )
        {
            case Pure3D::Texture::IMAGE:
            {
                if (!volume) 
                {
                    texture = LoadImage(f, imageFactory, texture, mipmap);
                    volume = false;
                    image = true;
                    mipmap++;
                }
                break;
            }
            case Pure3D::Texture::VOLUME_IMAGE:
            {
                if (!image)
                {
                    texture = LoadVolumeImage(f, imageFactory, texture, mipmap, numMipMaps);
                    volume = true;
                    image = false;
                    mipmap++;
                }
                break;
            }
            default:
                break;    
        }
        f->EndChunk();
    }
    if (texture != NULL)
    {
        texture->SetName(name);
#if defined(RAD_ANDROID) || (defined(SRR2_OPENXR_PLATFORM_WIN32) && defined(SRR2_VR_RENDERER_VULKAN))
        pglSetTextureSourceName(texture->GetTexture(),name);
#endif
        texture->SetPriority(priority);
    }
    return texture;
}

tTexture* tTextureLoader::LoadImage(tChunkFile* f, tImageFactory* factory, tTexture* buildTexture, int mipLevel)
{
    if(!factory)
        factory = imageFactory;

    char name[128];
    f->GetPString(name);
    int version = f->GetLong();
    P3DASSERT(version == IMAGE_VERSION);   
    int width = f->GetLong();
    int height = f->GetLong();
    int bpp = f->GetLong();
    bool palettized = f->GetLong() == 1;
    bool alpha = f->GetLong() == 1;
    unsigned format = f->GetLong();

    tTexture* texture = buildTexture;

    while(f->ChunksRemaining())
    {
        switch(f->BeginChunk())
        {
            case Pure3D::Texture::IMAGE_DATA:
            {
                unsigned size = f->GetLong();

                if (texture == NULL)
                {
                    tFile* file = f->BeginInset();
                    texture = factory->ParseAsTexture( file, name, size, (tImageHandler::Format)format);
                    f->EndInset(file);
                }
                else
                {
                    tFile* file = f->BeginInset();
                    factory->ParseIntoTexture(file, texture, (tImageHandler::Format)format, mipLevel);
                    f->EndInset(file);
                }

                break;
            }

            case Pure3D::Texture::IMAGE_FILENAME:
            {
                char fileName[255];
                f->GetPString(fileName);
                if (texture == NULL)
                {
                    texture = factory->LoadAsTexture(fileName, name);
                }
                else
                {
                    factory->LoadIntoTexture(fileName, texture, mipLevel);
                }
                break;
            }

            default:
                break;
        }
        f->EndChunk();
    }

    return texture;
}

tTexture* tTextureLoader::LoadVolumeImage(tChunkFile* f, tImageFactory* factory, tTexture* buildTexture, int mipLevel, int numMipMaps)
{
    if(!factory)
        factory = imageFactory;

    char name[128];
    f->GetPString(name);
    int version = f->GetLong();
    P3DASSERT(version == VOLUME_IMAGE_VERSION);   
    int width = f->GetLong();
    int height = f->GetLong();
    int depth = f->GetLong();
    int bpp = f->GetLong();
    bool palettized = f->GetLong() == 1;
    bool alpha = f->GetLong() == 1;
    unsigned format = f->GetLong();

#ifdef RAD_XBOX
    P3DASSERT(depth>0);
    tTexture* texture = buildTexture;
    tImage** images = new tImage*[depth];
    bool autoStore = factory->GetAutoStore();
    factory->SetAutoStore(false);

    int imageNum = 0;

    while(f->ChunksRemaining())
    {
        switch(f->BeginChunk())
        {
            case Pure3D::Texture::IMAGE:
            {
                if (imageNum<depth)
                {              
                    bool found = false;
                    char buf[256];
                    sprintf(buf,"%s_%i",name,imageNum);
                    while((f->ChunksRemaining()) && (!found))
                    {
                        switch(f->BeginChunk())
                        {
                            case Pure3D::Texture::IMAGE_DATA:
                            {
                                unsigned size = f->GetLong();
                                tFile* file = f->BeginInset();
                                images[imageNum] = factory->ParseAsImage(file, buf, (tImageHandler::Format)format);
                                images[imageNum]->AddRef();
                                imageNum++;
                                f->EndInset(file);
                                found = true;
                                break;
                            }

                            case Pure3D::Texture::IMAGE_FILENAME:
                            {
                                char fileName[255];
                                f->GetPString(fileName);
                                images[imageNum] = factory->LoadAsImage(fileName, buf);
                                images[imageNum]->AddRef(); 
                                imageNum++;
                                found = true;
                                break;
                            }

                            default:
                                break;
                        }
                        f->EndChunk();
                    }
                }
                break;
            }
            default:
                break;
        }
        f->EndChunk();
    }

    if (imageNum>0)
    {
        if (texture == NULL)
        {
            texture = imageConverter->ImageToVolumeTexture(images, imageNum, numMipMaps);
        }
        else
        {
            texture = imageConverter->ImageInToVolumeTexture(images, imageNum, texture, mipLevel);
        }
    }

    factory->SetAutoStore(autoStore);

    for (int i = 0; i < imageNum; i++)
    {
        images[i]->Release();
    }

    delete [] images;
    return texture;

#else
    tTexture* texture = buildTexture;
    while(f->ChunksRemaining())
    {
        switch(f->BeginChunk())
        {
            case Pure3D::Texture::IMAGE:
            {
                if (texture==NULL)
                {
                    texture = LoadImage(f, factory, texture, 0);
                }
                break;
            }
            default:
                break;
        }
        f->EndChunk();
    }
    return texture;
#endif
}

#include <pddi/pddi.hpp>
#include <pddi/pddipc.hpp>
#include <pddi/pddiext.hpp>
#include <pddi/pddishade.hpp>
#include <pddi/base/basecontext.hpp>
#include <pddi/gles/decompress.hpp>
#include <vr/vulkan/openxr_vulkan_context.h>
#include <vr/openxrmanager.h>
#include <pddi/vulkan/vktexture_resource.h>

#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <cctype>
#include <memory>
#include <unordered_map>
#include <vector>
#include <sys/stat.h>

#define STB_IMAGE_STATIC
#define STBI_NO_THREAD_LOCALS
#define STBI_FAILURE_USERMSG
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "../../../SDL3/src/video/stb_image.h"

extern int gPglCsmBillboardMode;

namespace
{
unsigned gCreatedPrimBuffers=0;
unsigned gUploadedVertexBuffers=0;
unsigned gDrawPrimBufferCalls=0;
unsigned gImmediateDrawCalls=0;
std::vector<std::string> gVulkanShaderTypes;
unsigned gSkinMatrixCount=0;
pddiMatrix gSkinMatrices[SharOpenXR::VulkanMaterialState::MaxSkinMatrices];
bool gDrawingSkin=false;
int gVulkanEnhancedMaterialMode=0;
float gVulkanEnhancedSunDirection[3]={0.0f,1.0f,0.0f};
int gVulkanVehicleRearLightMode=0;
int gVulkanVehicleRearLightCount=0;
bool gVulkanVehicleRearLightsSuppressed=false;
float gVulkanVehicleRearLightPositions[4][4]={};
float gVulkanVehicleRearLightDirections[4][4]={};
float gVulkanVehicleRearLightColour[3]={1.0f,0.04f,0.02f};

struct ExternalImage { int width=0,height=0; std::vector<unsigned char> pixels; };
std::unordered_map<std::string,std::shared_ptr<ExternalImage> > gExternalImageCache;

std::shared_ptr<ExternalImage> LoadExternalImage(const std::string& path)
{
    const auto cached=gExternalImageCache.find(path);
    if(cached!=gExternalImageCache.end()) return cached->second;
    SDL_RWops* file=SDL_RWFromFile(path.c_str(),"rb");
    if(!file) { gExternalImageCache[path]=std::shared_ptr<ExternalImage>(); return {}; }
    const Sint64 length=SDL_RWsize(file);
    std::vector<unsigned char> encoded(length>0?static_cast<size_t>(length):0u);
    const bool readOk=!encoded.empty() &&
        SDL_RWread(file,encoded.data(),1,encoded.size())==encoded.size();
    SDL_RWclose(file);
    int width=0,height=0,channels=0;
    unsigned char* decoded=readOk?stbi_load_from_memory(encoded.data(),
        static_cast<int>(encoded.size()),&width,&height,&channels,4):NULL;
    if(!decoded) { gExternalImageCache[path]=std::shared_ptr<ExternalImage>(); return {}; }
    std::shared_ptr<ExternalImage> result(new ExternalImage);
    result->width=width; result->height=height;
    result->pixels.resize(static_cast<size_t>(width)*height*4u);
    // Pure3D uploads ordinary textures through a negative lock pitch, so its
    // first source scanline ends up in the final Vulkan image row. stb_image
    // returns external PNGs top-to-bottom. Mirror the row order here to give
    // every custom map the same UV origin as the texture it replaces.
    const size_t rowBytes=static_cast<size_t>(width)*4u;
    for(int y=0;y<height;++y)
        std::memcpy(result->pixels.data()+static_cast<size_t>(y)*rowBytes,
                    decoded+static_cast<size_t>(height-1-y)*rowBytes,rowBytes);
    stbi_image_free(decoded); gExternalImageCache[path]=result;
    return result;
}

std::shared_ptr<ExternalImage> FindExternalImage(const std::string& base,const char* suffix)
{
    const std::string filename=base+suffix+".png";
    std::shared_ptr<ExternalImage> image=LoadExternalImage(
        "/storage/emulated/0/SimpsonsHitRun/custom/textures/"+filename);
    if(!image) image=LoadExternalImage("custom/textures/"+filename);
    if(image) return image;
    std::string lower=filename;
    std::transform(lower.begin(),lower.end(),lower.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if(lower==filename) return {};
    image=LoadExternalImage("/storage/emulated/0/SimpsonsHitRun/custom/textures/"+lower);
    if(!image) image=LoadExternalImage("custom/textures/"+lower);
    return image;
}

unsigned char SampleChannel(const ExternalImage& image,int x,int y,int targetWidth,
                            int targetHeight,int channel)
{
    const int sx=std::min(image.width-1,std::max(0,x*image.width/targetWidth));
    const int sy=std::min(image.height-1,std::max(0,y*image.height/targetHeight));
    return image.pixels[(static_cast<size_t>(sy)*image.width+sx)*4u+channel];
}

void DumpPackedPbrDebug(const std::string& base,int width,int height,
                        const std::vector<unsigned char>& pixels)
{
#if defined(__ANDROID__)
    const char* external=SDL_AndroidGetExternalStoragePath();
    if(!external || !*external || width<=0 || height<=0 || pixels.empty()) return;
    const std::string directory=std::string(external)+"/pbr_debug";
    mkdir(directory.c_str(),0777);
    const std::string prefix=directory+"/"+base;
    if(FILE* raw=std::fopen((prefix+"_pbr_gpu.rgba").c_str(),"wb"))
    {
        std::fwrite(pixels.data(),1,pixels.size(),raw);
        std::fclose(raw);
    }
    static const char* names[4]={"normal_x","normal_y","rough","metal"};
    for(int channel=0;channel<4;++channel)
    {
        if(FILE* image=std::fopen((prefix+"_"+names[channel]+".pgm").c_str(),"wb"))
        {
            std::fprintf(image,"P5\n%d %d\n255\n",width,height);
            // The GPU buffer follows Pure3D's bottom-up texture storage. PGM
            // is top-down, so reverse rows only in the human-readable dump.
            for(int y=height-1;y>=0;--y)
                for(int x=0;x<width;++x)
                    std::fputc(pixels[(static_cast<size_t>(y)*width+x)*4u+channel],image);
            std::fclose(image);
        }
    }
    if(FILE* info=std::fopen((prefix+"_metadata.txt").c_str(),"wb"))
    {
        std::fprintf(info,"width=%d\nheight=%d\nformat=VK_FORMAT_R8G8B8A8_UNORM\n"
            "R=normal_x\nG=normal_y\nB=roughness\nA=metallic\n"
            "raw_origin=bottom_left\nraw_bytes=%u\n",width,height,
            static_cast<unsigned>(pixels.size()));
        std::fclose(info);
    }
    SDL_Log("Vulkan PBR debug dump: %s",prefix.c_str());
#else
    (void)base; (void)width; (void)height; (void)pixels;
#endif
}

unsigned PackVertexColour(pddiColour colour)
{
    return static_cast<unsigned>(colour.Red()) |
           (static_cast<unsigned>(colour.Green()) << 8) |
           (static_cast<unsigned>(colour.Blue()) << 16) |
           (static_cast<unsigned>(colour.Alpha()) << 24);
}

class VulkanDisplay final : public pddiDisplay
{
public:
    VulkanDisplay()
    {
        std::memset(&info, 0, sizeof(info));
        info.id = 0;
        std::strcpy(info.description, "OpenXR Vulkan display");
        mode.width = 2064;
        mode.height = 2208;
        mode.bpp = 32;
        info.nDisplayModes = 1;
        info.modeInfo = &mode;
    }

    bool InitDisplay(int x, int y, int bpp) override
    {
        width = x > 0 ? x : width;
        height = y > 0 ? y : height;
        depth = bpp > 0 ? bpp : depth;
        return true;
    }
    bool InitDisplay(const pddiDisplayInit* init) override
    {
        if (init)
            return InitDisplay(init->xsize, init->ysize, init->bpp);
        return true;
    }
    long ProcessWindowMessage(SDL_Window*, const SDL_WindowEvent*) override { return 0; }
    void SetWindow(SDL_Window*) override {}
    pddiDisplayInfo* GetDisplayInfo() override { return &info; }
    int GetHeight() override { return height; }
    int GetWidth() override { return width; }
    int GetDepth() override { return depth; }
    pddiDisplayMode GetDisplayMode() override { return PDDI_DISPLAY_WINDOW; }
    int GetNumColourBuffer() override { return 3; }
    unsigned GetBufferMask() override { return PDDI_BUFFER_COLOUR | PDDI_BUFFER_DEPTH | PDDI_BUFFER_STENCIL; }
    unsigned GetFreeTextureMem() override { return 0; }
    void SwapBuffers() override {}
    unsigned Screenshot(pddiColour*, int) override { return 0; }

private:
    int width = 2064;
    int height = 2208;
    int depth = 32;
    pddiDisplayInfo info;
    pddiModeInfo mode;
};

class VulkanTexture final : public pddiTexture
{
public:
    explicit VulkanTexture(pddiTextureDesc* desc)
        : width(desc ? desc->GetSizeX() : 1), height(desc ? desc->GetSizeY() : 1),
          depth(desc ? desc->GetBitDepth() : 32), alpha(desc ? desc->GetAlphaDepth() : 8),
          extraMipCount(desc ? desc->GetMipMapCount() : 0u),
          type(desc ? desc->GetType() : PDDI_TEXTYPE_RGB),
          mipData(extraMipCount + 1u), decodeJobs(extraMipCount+1u)
    {
        std::fill(samplers,samplers+10,VK_NULL_HANDLE);
        std::fill(descriptorSets,descriptorSets+10,VK_NULL_HANDLE);
        std::fill(externalColorSamplers,externalColorSamplers+10,VK_NULL_HANDLE);
        std::fill(externalColorDescriptors,externalColorDescriptors+10,VK_NULL_HANDLE);
        std::fill(pbrSamplers,pbrSamplers+10,VK_NULL_HANDLE);
        std::fill(pbrDescriptors,pbrDescriptors+10,VK_NULL_HANDLE);
        for (unsigned level = 0; level < mipData.size(); ++level)
        {
            const unsigned w = std::max(1u, width >> level);
            const unsigned h = std::max(1u, height >> level);
            if(IsDxt())
            {
                const unsigned blockBytes=type==PDDI_TEXTYPE_DXT1 ? 8u : 16u;
                mipData[level].resize(((w+3u)/4u)*((h+3u)/4u)*blockBytes);
            }
            else mipData[level].resize(w * h * 4);
        }
    }
    ~VulkanTexture() override
    {
        SharOpenXR::VulkanContext& context=SharOpenXR::GetVulkanContext();
        for(unsigned i=0;i<10;++i)
            if(descriptorSets[i] && descriptorSets[i]!=descriptorSet)
                context.DestroyTextureSamplerDescriptor(samplers[i],descriptorSets[i]);
        for(unsigned i=0;i<10;++i)
            if(externalColorDescriptors[i] &&
               externalColorDescriptors[i]!=externalColorDescriptor)
                context.DestroyTextureSamplerDescriptor(externalColorSamplers[i],
                                                        externalColorDescriptors[i]);
        for(unsigned i=0;i<10;++i)
            if(pbrDescriptors[i] && pbrDescriptors[i]!=pbrDescriptor)
                context.DestroyTextureSamplerDescriptor(pbrSamplers[i],
                                                        pbrDescriptors[i]);
        SharOpenXR::GetVulkanContext().DestroyTexture(image,memory,view,sampler,descriptorSet);
        context.DestroyTexture(externalColorImage,externalColorMemory,externalColorView,
                               externalColorSampler,externalColorDescriptor);
        context.DestroyTexture(pbrImage,pbrMemory,pbrView,pbrSampler,pbrDescriptor);
    }
    pddiPixelFormat GetPixelFormat() override { return PixelFormat(); }
    int GetWidth() override { return static_cast<int>(width); }
    int GetHeight() override { return static_cast<int>(height); }
    int GetDepth() override { return static_cast<int>(depth); }
    int GetAlphaDepth() override { return static_cast<int>(alpha); }
    int GetNumMipMaps() override { return static_cast<int>(extraMipCount); }
    int GetNumPaletteEntries() override { return static_cast<int>(palette.size()); }
    bool CopyLevelZeroPixels(std::vector<unsigned char>* output,
                             unsigned* outputWidth,unsigned* outputHeight)
    {
        if(!output || !outputWidth || !outputHeight || mipData.empty())
            return false;
        *outputWidth=width; *outputHeight=height;
        if(IsDxt())
        {
            output->assign(static_cast<size_t>(width)*height*4u,0);
            const unsigned char* source=mipData[0].data();
            const unsigned blocksX=(width+3u)/4u,blocksY=(height+3u)/4u;
            const unsigned blockBytes=type==PDDI_TEXTYPE_DXT1?8u:16u;
            for(unsigned by=0;by<blocksY;++by) for(unsigned bx=0;bx<blocksX;++bx)
            {
                const unsigned char* block=source+(by*blocksX+bx)*blockBytes;
                const unsigned char* colour=block+(blockBytes-8u);
                const unsigned c0=colour[0]|(colour[1]<<8),c1=colour[2]|(colour[3]<<8);
                unsigned char rgba[4][4]={};
                const unsigned colours[2]={c0,c1};
                for(unsigned c=0;c<2;++c)
                {
                    rgba[c][2]=static_cast<unsigned char>(((colours[c]>>11)&31)*255/31);
                    rgba[c][1]=static_cast<unsigned char>(((colours[c]>>5)&63)*255/63);
                    rgba[c][0]=static_cast<unsigned char>((colours[c]&31)*255/31);rgba[c][3]=255;
                }
                const bool transparent=type==PDDI_TEXTYPE_DXT1&&c0<=c1;
                for(unsigned k=0;k<3;++k)
                {
                    rgba[2][k]=static_cast<unsigned char>(transparent?(rgba[0][k]+rgba[1][k])/2:
                        (2*rgba[0][k]+rgba[1][k])/3);
                    rgba[3][k]=static_cast<unsigned char>(transparent?0:(rgba[0][k]+2*rgba[1][k])/3);
                }
                rgba[2][3]=255;rgba[3][3]=transparent?0:255;
                uint64_t alphaBits=0;unsigned char alphaTable[8]={255,255,255,255,255,255,255,255};
                if(type==PDDI_TEXTYPE_DXT3||type==PDDI_TEXTYPE_DXT2)
                    for(unsigned i=0;i<8;++i) alphaBits|=static_cast<uint64_t>(block[i])<<(8*i);
                else if(type==PDDI_TEXTYPE_DXT5||type==PDDI_TEXTYPE_DXT4)
                {
                    alphaTable[0]=block[0];alphaTable[1]=block[1];
                    if(alphaTable[0]>alphaTable[1]) for(unsigned i=1;i<=6;++i)
                        alphaTable[i+1]=static_cast<unsigned char>(((7-i)*alphaTable[0]+i*alphaTable[1])/7);
                    else
                    {
                        for(unsigned i=1;i<=4;++i) alphaTable[i+1]=static_cast<unsigned char>(((5-i)*alphaTable[0]+i*alphaTable[1])/5);
                        alphaTable[6]=0;alphaTable[7]=255;
                    }
                    for(unsigned i=0;i<6;++i) alphaBits|=static_cast<uint64_t>(block[2+i])<<(8*i);
                }
                const uint32_t indices=colour[4]|(colour[5]<<8)|(colour[6]<<16)|(colour[7]<<24);
                for(unsigned py=0;py<4;++py) for(unsigned px=0;px<4;++px)
                {
                    const unsigned x=bx*4+px,y=by*4+py;if(x>=width||y>=height) continue;
                    const unsigned pixel=py*4+px,index=(indices>>(pixel*2))&3;
                    unsigned char* destination=&(*output)[(static_cast<size_t>(y)*width+x)*4u];
                    std::memcpy(destination,rgba[index],4);
                    if(type==PDDI_TEXTYPE_DXT3||type==PDDI_TEXTYPE_DXT2)
                        destination[3]=static_cast<unsigned char>(((alphaBits>>(pixel*4))&15)*17);
                    else if(type==PDDI_TEXTYPE_DXT5||type==PDDI_TEXTYPE_DXT4)
                        destination[3]=alphaTable[(alphaBits>>(pixel*3))&7];
                }
            }
            return true;
        }
        *output=mipData[0];
        return output->size()==static_cast<size_t>(width)*height*4u;
    }
    void SetPalette(int count, pddiColour* values) override
    {
        palette.assign(values, values + std::max(0, count));
    }
    int GetPalette(pddiColour* values) override
    {
        if (values && !palette.empty()) std::memcpy(values, palette.data(), palette.size() * sizeof(pddiColour));
        return static_cast<int>(palette.size());
    }
    pddiLockInfo* Lock(int level, pddiRect*) override
    {
        if (level < 0 || static_cast<unsigned>(level) >= mipData.size()) return nullptr;
        const int w = static_cast<int>(std::max(1u, width >> level));
        const int h = static_cast<int>(std::max(1u, height >> level));
        lock = pddiLockInfo();
        lock.width = w; lock.height = h; lock.depth = IsDxt() ? depth : 32;
        lock.format = PixelFormat();
        if(IsDxt())
        {
            lock.pitch=((w+3)/4)*(type==PDDI_TEXTYPE_DXT1 ? 8 : 16);
            lock.bits=mipData[level].data();
        }
        else
        {
            // Preserve the GLES pglTexture contract exactly. Pure3D loaders
            // write the first source scanline through a pointer to the final
            // destination row and advance using a negative pitch. Keeping the
            // same memory layout removes the need for a global shader UV flip
            // and also handles Scrooby/loading textures consistently.
            lock.pitch=-(w*4);
            lock.bits=mipData[level].data()+static_cast<size_t>(w)*(h-1)*4u;
        }
        lock.native = true;
        return &lock;
    }
    void Unlock(int level) override
    {
        if(level<0 || static_cast<unsigned>(level)>=mipData.size()) return;
        SharOpenXR::VulkanContext& context=SharOpenXR::GetVulkanContext();
        if(!image && IsDxt())
        {
            const VkFormat candidate=type==PDDI_TEXTYPE_DXT1?
                VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
                (type==PDDI_TEXTYPE_DXT2 || type==PDDI_TEXTYPE_DXT3)?
                    VK_FORMAT_BC2_UNORM_BLOCK:VK_FORMAT_BC3_UNORM_BLOCK;
            compressedFormat=context.SupportsSampledTextureFormat(candidate)?candidate:
                                                                        VK_FORMAT_UNDEFINED;
        }
        if(!image && !context.CreateTexture2D(width,height,static_cast<uint32_t>(mipData.size()),
             &image,&memory,&view,&sampler,&descriptorSet,
             compressedFormat!=VK_FORMAT_UNDEFINED?compressedFormat:
                                                    VK_FORMAT_B8G8R8A8_UNORM)) return;
        if(descriptorSet && !descriptorSets[6])
        { samplers[6]=sampler; descriptorSets[6]=descriptorSet; }
        const uint32_t w=std::max(1u,width>>level);
        const uint32_t h=std::max(1u,height>>level);
        if(IsDxt())
        {
            if(compressedFormat!=VK_FORMAT_UNDEFINED)
            {
                context.UploadTextureMip(image,w,h,static_cast<uint32_t>(level),
                                         mipData[level].data(),mipData[level].size());
                return;
            }
            const pddiTextureType decodeType=type;
            const std::vector<unsigned char> compressed=mipData[level];
            decodeJobs[level]=std::async(std::launch::async,
                [compressed,decodeType,w,h]() -> std::vector<unsigned char>
                {
                    const uint32_t decodedWidth=std::max(4u,w);
                    const uint32_t decodedHeight=std::max(4u,h);
                    std::vector<unsigned char> decoded(decodedWidth*decodedHeight*4u);
                    if(decodeType==PDDI_TEXTYPE_DXT1)
                        BlockDecompressImageBC1(decodedWidth,decodedHeight,
                                                compressed.data(),decoded.data());
                    else if(decodeType==PDDI_TEXTYPE_DXT2 || decodeType==PDDI_TEXTYPE_DXT3)
                        BlockDecompressImageBC2(decodedWidth,decodedHeight,
                                                compressed.data(),decoded.data());
                    else
                        BlockDecompressImageBC3(decodedWidth,decodedHeight,
                                                compressed.data(),decoded.data());
                    std::vector<unsigned char> bgra(w*h*4u);
                    for(uint32_t y=0;y<h;++y) for(uint32_t x=0;x<w;++x)
                    {
                        const unsigned char* source=&decoded[(y*decodedWidth+x)*4u];
                        unsigned char* target=&bgra[(y*w+x)*4u];
                        target[0]=source[2]; target[1]=source[1];
                        target[2]=source[0]; target[3]=source[3];
                    }
                    return bgra;
                });
        }
        else context.UploadTextureMip(image,w,h,static_cast<uint32_t>(level),
                                      mipData[level].data(),mipData[level].size());
    }
    void Prefetch() override {}
    void Discard() override {}
    void SetPriority(int value) override { priority = value; }
    int GetPriority() override { return priority; }
    VkImageView GetImageView() const { return view; }
    VkSampler GetSampler() const { return sampler; }
    VkDescriptorSet GetDescriptorSet(unsigned uvMode,unsigned filterMode)
    {
        PumpDecodedUploads();
        SharOpenXR::VulkanContext& context=SharOpenXR::GetVulkanContext();
        if(image && context.HasPendingTextureUploads(image))
            context.PrepareTextureForSampling(image);
        if(SharOpenXR::AreCustomMaterialsEnabled() && externalColorDescriptor &&
           !context.HasPendingTextureUploads(externalColorImage))
        {
            // A replacement must preserve the material's authored addressing
            // mode. Returning the descriptor created by CreateTexture2D here
            // forced CLAMP_TO_EDGE for every custom image. Static-world UVs
            // commonly exceed 0..1 and therefore stretched the edge texel
            // over whole walls and roads instead of repeating the texture.
            uvMode=std::min(1u,uvMode); filterMode=std::min(4u,filterMode);
            // A high-resolution external texture must be minified through its
            // mip chain even when the 2002 material requested plain bilinear
            // filtering (which was reasonable for its original 64/128px
            // image). Keep explicit nearest filtering intact for pixel-art UI.
            if(externalColorMipLevels>1 && filterMode==1) filterMode=4;
            const unsigned externalIndex=uvMode*5u+filterMode;
            if(!externalColorDescriptors[externalIndex] && externalColorView)
                context.CreateTextureSamplerDescriptor(externalColorView,externalColorMipLevels,uvMode,
                    filterMode,&externalColorSamplers[externalIndex],
                    &externalColorDescriptors[externalIndex]);
            return externalColorDescriptors[externalIndex]?
                externalColorDescriptors[externalIndex]:externalColorDescriptor;
        }
        if(!image || context.HasPendingTextureUploads(image))
            return VK_NULL_HANDLE;
        uvMode=std::min(1u,uvMode); filterMode=std::min(4u,filterMode);
        const unsigned index=uvMode*5u+filterMode;
        if(!descriptorSets[index] && view)
            context.CreateTextureSamplerDescriptor(
                view,static_cast<uint32_t>(mipData.size()),uvMode,filterMode,
                &samplers[index],&descriptorSets[index]);
        return descriptorSets[index]?descriptorSets[index]:descriptorSet;
    }
    VkDescriptorSet GetPbrDescriptorSet(unsigned uvMode,unsigned filterMode)
    {
        if(!SharOpenXR::AreCustomMaterialsEnabled() || !pbrImage || !pbrDescriptor)
            return VK_NULL_HANDLE;
        SharOpenXR::VulkanContext& context=SharOpenXR::GetVulkanContext();
        // Array layers are uploaded through the same deferred staging path as
        // colour replacements. The colour getter finalized that upload before
        // sampling, while PBR maps previously returned their descriptor in
        // transfer state forever. Flush and transition the complete array
        // before exposing it to the fragment shader.
        if(context.HasPendingTextureUploads(pbrImage))
            context.PrepareTextureForSampling(pbrImage);
        const bool ready=!context.HasPendingTextureUploads(pbrImage);
        if(ready && !pbrReadyLogged)
        {
            SDL_Log("Vulkan custom PBR ready: %s flags=0x%x",
                    sourceName.c_str(),pbrMapFlags);
            pbrReadyLogged=true;
        }
        if(!ready) return VK_NULL_HANDLE;
        uvMode=std::min(1u,uvMode); filterMode=std::min(4u,filterMode);
        if(pbrMipLevels>1 && filterMode==1) filterMode=4;
        const unsigned index=uvMode*5u+filterMode;
        if(!pbrDescriptors[index] && pbrView)
            context.CreateTextureSamplerDescriptor(pbrView,pbrMipLevels,uvMode,
                filterMode,&pbrSamplers[index],&pbrDescriptors[index]);
        return pbrDescriptors[index]?pbrDescriptors[index]:pbrDescriptor;
    }
    uint32_t GetPbrMapFlags() const
    { return SharOpenXR::AreCustomMaterialsEnabled()?pbrMapFlags:0u; }
    void LogPbrMaterialState(uint32_t model,uint32_t profile,bool lit,
                             bool reflectionEnabled,int reflectionMode)
    {
        if(!pbrMapFlags) return;
        if(pbrLoggedModel==model && pbrLoggedProfile==profile &&
           pbrLoggedReflectionMode==reflectionMode && pbrLoggedLit==lit &&
           pbrLoggedReflectionEnabled==reflectionEnabled) return;
        SDL_Log("Vulkan custom PBR material: %s model=%u profile=%u lit=%d flags=0x%x reflectionMode=%d reflectionEnabled=%d",
                sourceName.c_str(),model,profile,lit?1:0,pbrMapFlags,
                reflectionMode,reflectionEnabled?1:0);
        pbrLoggedModel=model; pbrLoggedProfile=profile;
        pbrLoggedReflectionMode=reflectionMode; pbrLoggedLit=lit;
        pbrLoggedReflectionEnabled=reflectionEnabled;
    }
    void SetSourceName(const char* value)
    {
        if(!value || !*value || !sourceName.empty()) return;
        sourceName=value;
        const size_t slash=sourceName.find_last_of("/\\");
        std::string base=slash==std::string::npos?sourceName:sourceName.substr(slash+1);
        const size_t dot=base.find_last_of('.'); if(dot!=std::string::npos) base.resize(dot);
        if(base.empty()) return;
        std::shared_ptr<ExternalImage> color=FindExternalImage(base,"_color");
        std::shared_ptr<ExternalImage> normal=FindExternalImage(base,"_normal");
        std::shared_ptr<ExternalImage> packed=FindExternalImage(base,"_pbr");
        std::shared_ptr<ExternalImage> rough=FindExternalImage(base,"_rough");
        std::shared_ptr<ExternalImage> metal=FindExternalImage(base,"_metal");
        SharOpenXR::VulkanContext& context=SharOpenXR::GetVulkanContext();
        uint32_t colorMipLevels=1;
        if(color)
            for(int dimension=std::max(color->width,color->height);dimension>1;
                dimension/=2) ++colorMipLevels;
        if(color && context.CreateTexture2D(color->width,color->height,colorMipLevels,&externalColorImage,
           &externalColorMemory,&externalColorView,&externalColorSampler,&externalColorDescriptor,
           VK_FORMAT_B8G8R8A8_UNORM,4))
        {
            std::vector<unsigned char> bgra(color->pixels.size());
            for(size_t i=0;i<color->pixels.size();i+=4)
            { bgra[i]=color->pixels[i+2]; bgra[i+1]=color->pixels[i+1];
              bgra[i+2]=color->pixels[i]; bgra[i+3]=color->pixels[i+3]; }
            context.UploadTextureMip(externalColorImage,color->width,color->height,0,
                                     bgra.data(),bgra.size());
            // High-resolution replacements need the same complete mip chain as
            // authored Pure3D textures. Sampling a 1K replacement at full LOD
            // for every distant world triangle destroys texture-cache locality
            // in stereo and was the main cost even with all effects disabled.
            std::vector<unsigned char> previous=std::move(bgra);
            int previousWidth=color->width,previousHeight=color->height;
            for(uint32_t level=1;level<colorMipLevels;++level)
            {
                // Vulkan defines mip n as floor(base / 2^n). Rounding odd
                // dimensions up writes beyond the actual subresource extent
                // (1254 -> 627 -> 313, never 314).
                const int levelWidth=std::max(1,previousWidth/2);
                const int levelHeight=std::max(1,previousHeight/2);
                std::vector<unsigned char> next(
                    static_cast<size_t>(levelWidth)*levelHeight*4u);
                for(int y=0;y<levelHeight;++y) for(int x=0;x<levelWidth;++x)
                {
                    unsigned sums[4]={0,0,0,0},samples=0;
                    for(int oy=0;oy<2;++oy) for(int ox=0;ox<2;++ox)
                    {
                        const int sx=std::min(previousWidth-1,x*2+ox);
                        const int sy=std::min(previousHeight-1,y*2+oy);
                        const size_t source=(static_cast<size_t>(sy)*previousWidth+sx)*4u;
                        for(unsigned channel=0;channel<4;++channel)
                            sums[channel]+=previous[source+channel];
                        ++samples;
                    }
                    const size_t target=(static_cast<size_t>(y)*levelWidth+x)*4u;
                    for(unsigned channel=0;channel<4;++channel)
                        next[target+channel]=static_cast<unsigned char>(
                            (sums[channel]+samples/2u)/samples);
                }
                context.UploadTextureMip(externalColorImage,levelWidth,levelHeight,
                                         level,next.data(),next.size());
                previous.swap(next);
                previousWidth=levelWidth; previousHeight=levelHeight;
            }
            externalColorMipLevels=colorMipLevels;
            // CreateTexture2D's initial descriptor is clamp + trilinear.
            externalColorSamplers[9]=externalColorSampler;
            externalColorDescriptors[9]=externalColorDescriptor;
            // Keep replacement colour in the same display-ready UNORM
            // contract as original Pure3D textures. PBR explicitly decodes it
            // to linear; Legacy and Phong must not receive an implicit sRGB
            // decode or replacements become much darker than originals.
            SDL_Log("Vulkan custom texture: %s_color.png (%dx%d, %u mips)",
                    base.c_str(),color->width,color->height,colorMipLevels);
        }
        const std::shared_ptr<ExternalImage> sizeSource=normal?normal:(packed?packed:(rough?rough:metal));
        if(!sizeSource) return;
        const int w=sizeSource->width,h=sizeSource->height;
        // One unambiguous RGBA material texture:
        // R/G = tangent normal X/Y, B = roughness, A = metallic.
        // Packing the maps into one ordinary 2D image avoids array-layer
        // upload/layout ambiguity on mobile Vulkan drivers.
        std::vector<unsigned char> pbrPixels(static_cast<size_t>(w)*h*4u);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x)
        {
            const size_t offset=(static_cast<size_t>(y)*w+x)*4u;
            pbrPixels[offset]=normal?SampleChannel(*normal,x,y,w,h,0):128;
            pbrPixels[offset+1]=normal?SampleChannel(*normal,x,y,w,h,1):128;
            pbrPixels[offset+2]=rough?SampleChannel(*rough,x,y,w,h,0):
                (packed?SampleChannel(*packed,x,y,w,h,0):128);
            pbrPixels[offset+3]=metal?SampleChannel(*metal,x,y,w,h,0):
                (packed?SampleChannel(*packed,x,y,w,h,1):0);
        }
        pbrMapFlags|=(normal?1u:0u)|((rough||packed)?2u:0u)|
                     ((metal||packed)?4u:0u);
        DumpPackedPbrDebug(base,w,h,pbrPixels);
        uint32_t mipLevels=1;
        for(int dimension=std::max(w,h);dimension>1;dimension/=2)
            ++mipLevels;
        if(context.CreateTexture2D(w,h,mipLevels,&pbrImage,&pbrMemory,&pbrView,
           &pbrSampler,&pbrDescriptor,VK_FORMAT_R8G8B8A8_UNORM,4))
        {
            pbrMipLevels=mipLevels;
            // CreateTexture2D starts with clamp + trilinear (index 9).
            pbrSamplers[9]=pbrSampler;
            pbrDescriptors[9]=pbrDescriptor;
            context.UploadTextureMip(pbrImage,w,h,0,pbrPixels.data(),pbrPixels.size());
            std::vector<unsigned char> previous=std::move(pbrPixels);
            int previousWidth=w,previousHeight=h;
            for(uint32_t level=1;level<mipLevels;++level)
            {
                const int levelWidth=std::max(1,previousWidth/2);
                const int levelHeight=std::max(1,previousHeight/2);
                std::vector<unsigned char> next(static_cast<size_t>(levelWidth)*levelHeight*4u);
                for(int y=0;y<levelHeight;++y) for(int x=0;x<levelWidth;++x)
                {
                    float nx=0.0f,ny=0.0f,nz=0.0f,roughnessSquared=0.0f,metallic=0.0f;
                    unsigned samples=0;
                    for(int oy=0;oy<2;++oy) for(int ox=0;ox<2;++ox)
                    {
                        const int sx=std::min(previousWidth-1,x*2+ox);
                        const int sy=std::min(previousHeight-1,y*2+oy);
                        const size_t source=(static_cast<size_t>(sy)*previousWidth+sx)*4u;
                        const float tx=previous[source]/127.5f-1.0f;
                        const float ty=previous[source+1]/127.5f-1.0f;
                        const float tz=std::sqrt(std::max(0.0f,1.0f-tx*tx-ty*ty));
                        const float r=previous[source+2]/255.0f;
                        nx+=tx; ny+=ty; nz+=tz;
                        roughnessSquared+=r*r; metallic+=previous[source+3]/255.0f;
                        ++samples;
                    }
                    const float inverseSamples=1.0f/static_cast<float>(samples);
                    nx*=inverseSamples; ny*=inverseSamples; nz*=inverseSamples;
                    const float normalLength=std::sqrt(std::max(nx*nx+ny*ny+nz*nz,0.000001f));
                    nx/=normalLength; ny/=normalLength;
                    const size_t target=(static_cast<size_t>(y)*levelWidth+x)*4u;
                    next[target]=static_cast<unsigned char>(std::max(0.0f,std::min(255.0f,(nx*0.5f+0.5f)*255.0f+0.5f)));
                    next[target+1]=static_cast<unsigned char>(std::max(0.0f,std::min(255.0f,(ny*0.5f+0.5f)*255.0f+0.5f)));
                    next[target+2]=static_cast<unsigned char>(std::max(0.0f,std::min(255.0f,std::sqrt(roughnessSquared*inverseSamples)*255.0f+0.5f)));
                    next[target+3]=static_cast<unsigned char>(std::max(0.0f,std::min(255.0f,metallic*inverseSamples*255.0f+0.5f)));
                }
                context.UploadTextureMip(pbrImage,levelWidth,levelHeight,level,
                                         next.data(),next.size());
                previous.swap(next); previousWidth=levelWidth; previousHeight=levelHeight;
            }
            SDL_Log("Vulkan PBR maps: %s flags=0x%x (%dx%d, %u mips)",
                    base.c_str(),pbrMapFlags,w,h,mipLevels);
        }
    }

private:
    void PumpDecodedUploads()
    {
        for(unsigned level=0;level<decodeJobs.size();++level)
        {
            std::future<std::vector<unsigned char> >& job=decodeJobs[level];
            if(!job.valid() || job.wait_for(std::chrono::seconds(0))!=
                               std::future_status::ready) continue;
            std::vector<unsigned char> decoded=job.get();
            const uint32_t w=std::max(1u,width>>level);
            const uint32_t h=std::max(1u,height>>level);
            if(!decoded.empty())
                SharOpenXR::GetVulkanContext().UploadTextureMip(
                    image,w,h,level,decoded.data(),decoded.size());
        }
    }
    bool IsDxt() const
    { return type>=PDDI_TEXTYPE_DXT1 && type<=PDDI_TEXTYPE_DXT5; }
    pddiPixelFormat PixelFormat() const
    {
        switch(type)
        {
            case PDDI_TEXTYPE_DXT1:return PDDI_PIXEL_DXT1;
            case PDDI_TEXTYPE_DXT2:return PDDI_PIXEL_DXT2;
            case PDDI_TEXTYPE_DXT3:return PDDI_PIXEL_DXT3;
            case PDDI_TEXTYPE_DXT4:return PDDI_PIXEL_DXT4;
            case PDDI_TEXTYPE_DXT5:return PDDI_PIXEL_DXT5;
            default:return PDDI_PIXEL_ARGB8888;
        }
    }
    unsigned width, height, depth, alpha, extraMipCount;
    pddiTextureType type;
    int priority = 0;
    pddiLockInfo lock;
    std::vector<pddiColour> palette;
    std::vector<std::vector<unsigned char>> mipData;
    std::vector<std::future<std::vector<unsigned char> > > decodeJobs;
    VkImage image=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    VkSampler sampler=VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet=VK_NULL_HANDLE;
    VkFormat compressedFormat=VK_FORMAT_UNDEFINED;
    VkSampler samplers[10];
    VkDescriptorSet descriptorSets[10];
    VkSampler externalColorSamplers[10];
    VkDescriptorSet externalColorDescriptors[10];
    VkSampler pbrSamplers[10];
    VkDescriptorSet pbrDescriptors[10];
    std::string sourceName;
    VkImage externalColorImage=VK_NULL_HANDLE,pbrImage=VK_NULL_HANDLE;
    VkDeviceMemory externalColorMemory=VK_NULL_HANDLE,pbrMemory=VK_NULL_HANDLE;
    VkImageView externalColorView=VK_NULL_HANDLE,pbrView=VK_NULL_HANDLE;
    VkSampler externalColorSampler=VK_NULL_HANDLE,pbrSampler=VK_NULL_HANDLE;
    VkDescriptorSet externalColorDescriptor=VK_NULL_HANDLE,pbrDescriptor=VK_NULL_HANDLE;
    uint32_t pbrMapFlags=0;
    uint32_t externalColorMipLevels=1;
    uint32_t pbrMipLevels=1;
    bool pbrReadyLogged=false;
    uint32_t pbrLoggedModel=~0u,pbrLoggedProfile=~0u;
    int pbrLoggedReflectionMode=-1;
    bool pbrLoggedLit=false,pbrLoggedReflectionEnabled=false;
};

class VulkanShader final : public pddiShader
{
public:
    explicit VulkanShader(const char* shaderName) : name(shaderName ? shaderName : "simple")
    {
        if(name=="layeredlmap")
        {
            materialMode=3;
            shaderClass=SharOpenXR::VulkanMaterialState::ShaderLayeredLightMap;
        }
        else if(name=="layered")
        {
            materialMode=1;
            shaderClass=SharOpenXR::VulkanMaterialState::ShaderLayered;
        }
        else if(name=="lightmap")
        {
            materialMode=2;
            shaderClass=SharOpenXR::VulkanMaterialState::ShaderLightMap;
        }
        else if(name=="environment" || name=="spheremap")
            shaderClass=SharOpenXR::VulkanMaterialState::ShaderEnvironment;
        else if(name=="shadow")
            shaderClass=SharOpenXR::VulkanMaterialState::ShaderShadow;
        else if(name!="simple")
            shaderClass=SharOpenXR::VulkanMaterialState::ShaderOther;
        if(std::find(gVulkanShaderTypes.begin(),gVulkanShaderTypes.end(),name)==
           gVulkanShaderTypes.end())
        {
            gVulkanShaderTypes.push_back(name);
            SDL_Log("Vulkan PDDI shader type[%u]: %s (class %u)",
                    static_cast<unsigned>(gVulkanShaderTypes.size()-1),name.c_str(),
                    shaderClass);
        }
    }
    ~VulkanShader() override
    {
        if(texture) texture->Release();
        if(reflectionTexture) reflectionTexture->Release();
        if(topTexture) topTexture->Release();
        if(lightMap) lightMap->Release();
    }
    const char* GetType() override { return name.c_str(); }
    bool SetTexture(unsigned param, pddiTexture* value) override
    {
        if(param==PDDI_SP_REFLMAP)
        {
            VulkanTexture* replacement=static_cast<VulkanTexture*>(value);
            if(replacement==reflectionTexture) return true;
            if(replacement) replacement->AddRef();
            if(reflectionTexture) reflectionTexture->Release();
            reflectionTexture=replacement;
            return true;
        }
        if(param==PDDI_SP_TOPTEX || param==PDDI_SP_LIGHTMAP)
        {
            VulkanTexture*& slot=param==PDDI_SP_TOPTEX?topTexture:lightMap;
            VulkanTexture* replacement=static_cast<VulkanTexture*>(value);
            if(replacement==slot) return true;
            if(replacement) replacement->AddRef();
            if(slot) slot->Release();
            slot=replacement;
            return true;
        }
        if(param!=PDDI_SP_BASETEX) return true;
        VulkanTexture* replacement=static_cast<VulkanTexture*>(value);
        if(replacement==texture) return true;
        if(replacement) replacement->AddRef();
        if(texture) texture->Release();
        texture=replacement;
        return true;
    }
    bool SetInt(unsigned param, int value) override
    {
        if(param==PDDI_SP_BLENDMODE) blendMode=static_cast<unsigned>(std::max(0,value));
        else if(param==PDDI_SP_ALPHATEST) alphaTest=value!=0;
        else if(param==PDDI_SP_ALPHACOMPARE) alphaCompare=static_cast<unsigned>(std::max(0,value));
        else if(param==PDDI_SP_ISLIT) lit=value!=0;
        else if(param==PDDI_SP_UVMODE)
            uvMode=static_cast<unsigned>(std::max(0,std::min(1,value)));
        else if(param==PDDI_SP_FILTER)
            filterMode=static_cast<unsigned>(std::max(0,std::min(4,value)));
        else if(param==PDDI_SP_TWOSIDED) twoSided=value!=0;
        else if(param==PDDI_SP_TEXBLENDMODE)
            textureBlendMode=static_cast<unsigned>(std::max(0,std::min(8,value)));
        else if(param==PDDI_SP_TWOLAYERCBV) twoLayerColourByVertex=value!=0;
        else if(param==PDDI_SP_EMISSIVEALPHA) SetEmissiveAlpha(value);
        return true;
    }
    bool SetFloat(unsigned param, float value) override
    {
        if(param==PDDI_SP_ALPHACOMPARE_THRESHOLD) alphaRef=value;
        else if(param==PDDI_SP_SHININESS) shininess=value;
        return true;
    }
    bool SetColour(unsigned param, pddiColour value) override
    {
        if(param==PDDI_SP_DIFFUSE) diffuse=value;
        else if(param==PDDI_SP_AMBIENT) ambient=value;
        else if(param==PDDI_SP_EMISSIVE)
        {
            emissive=value;
            SetEmissiveAlpha(value.Alpha());
        }
        else if(param==PDDI_SP_SPECULAR) specular=value;
        else if(param==PDDI_SP_ENVBLEND) environmentBlend=value;
        return true;
    }
    bool SetVector(unsigned, const rmt::Vector&) override { return true; }
    bool SetMatrix(unsigned, const rmt::Matrix&) override { return true; }
    VkDescriptorSet GetTextureSet() const
    { return texture?texture->GetDescriptorSet(uvMode,filterMode):VK_NULL_HANDLE; }
    VkDescriptorSet GetReflectionSet() const
    { return reflectionTexture?reflectionTexture->GetDescriptorSet(PDDI_UV_CLAMP,PDDI_FILTER_MIPMAP_TRILINEAR):VK_NULL_HANDLE; }
    VkDescriptorSet GetTopTextureSet() const
    { return topTexture?topTexture->GetDescriptorSet(uvMode,filterMode):VK_NULL_HANDLE; }
    VkDescriptorSet GetLightMapSet() const
    { return lightMap?lightMap->GetDescriptorSet(uvMode,filterMode):VK_NULL_HANDLE; }
    VkDescriptorSet GetPbrTextureSet() const
    { return texture?texture->GetPbrDescriptorSet(uvMode,filterMode):VK_NULL_HANDLE; }
    void FillMaterialState(SharOpenXR::VulkanMaterialState* result) const
    {
        result->shaderClass=shaderClass;
        result->blendMode=blendMode; result->twoSided=twoSided;
        result->alphaTest=alphaTest; result->alphaRef=alphaRef;
        result->alphaCompare=alphaCompare;
        result->colour[0]=diffuse.Red()/255.0f;
        result->colour[1]=diffuse.Green()/255.0f;
        result->colour[2]=diffuse.Blue()/255.0f;
        result->colour[3]=diffuse.Alpha()/255.0f;
        result->ambientTerm[0]=ambient.Red()/255.0f;
        result->ambientTerm[1]=ambient.Green()/255.0f;
        result->ambientTerm[2]=ambient.Blue()/255.0f;
        result->ambientTerm[3]=0.0f;
        result->specular[0]=specular.Red()/255.0f;
        result->specular[1]=specular.Green()/255.0f;
        result->specular[2]=specular.Blue()/255.0f;
        result->specular[3]=0.0f;
        result->shininess=shininess;
        // Temporarily retain emissive until the context combines it with the
        // global ambient exactly as the GLES acm/ecm/acs expression does.
        result->lightPosition[0][0]=emissive.Red()/255.0f;
        result->lightPosition[0][1]=emissive.Green()/255.0f;
        result->lightPosition[0][2]=emissive.Blue()/255.0f;
        result->lit=lit;
        result->environmentBlend[0]=environmentBlend.Red()/255.0f;
        result->environmentBlend[1]=environmentBlend.Green()/255.0f;
        result->environmentBlend[2]=environmentBlend.Blue()/255.0f;
        result->environmentBlend[3]=environmentBlend.Alpha()/255.0f;
        result->materialMode=materialMode;
        result->pbrTextureSet=texture?
            texture->GetPbrDescriptorSet(uvMode,filterMode):VK_NULL_HANDLE;
        result->pbrMapFlags=texture?texture->GetPbrMapFlags():0u;
        result->pbrDebugMode=static_cast<uint32_t>(SharOpenXR::GetPbrDebugMode());
        const uint32_t selectedModel=static_cast<uint32_t>(
            SharOpenXR::GetEnhancedMaterialModel());
        // PBR maps belong exclusively to the PBR model. Previously any
        // enabled enhanced mode promoted a replacement carrying rough/metal/
        // normal maps to model 2, so selecting Phong still executed PBR and
        // both menu choices looked identical.
        const bool hasCustomPbrMaps=(result->pbrMapFlags&7u)!=0u;
        const bool customPbr=hasCustomPbrMaps && selectedModel==2u;
        const int reflectionMode=SharOpenXR::GetReflectionMode();
        // Reflection mode is a source selector.  A dynamic cube is a global
        // PBR environment and must be available to every surface in the
        // active enhanced-world pass.  Restricting it to primitive groups
        // carrying an authored EnvMap or a custom PBR sidecar made adjacent
        // road sections use different environments and exposed every material
        // seam.  The enhanced-world gate keeps GUI/HUD draws out of this path.
        // Static mode is different: it may only sample an actual reflection
        // texture authored by the game.  Never bind the white fallback as a
        // pretend static environment for a custom PBR material.
        const bool authoredReflection=reflectionTexture!=nullptr;
        const bool enhancedWorldPbr=selectedModel==2u &&
            (gVulkanEnhancedMaterialMode>0 || customPbr);
        result->reflectionEnabled=reflectionMode==2?
            (authoredReflection || enhancedWorldPbr):
            (reflectionMode==1 && authoredReflection);
        result->enhancedMaterialProfile=static_cast<uint32_t>(
            gVulkanEnhancedMaterialMode>0?gVulkanEnhancedMaterialMode:
            (hasCustomPbrMaps && selectedModel!=0u?1:0));
        result->enhancedMaterialModel=result->enhancedMaterialProfile>0?
            selectedModel:0u;
        if(texture) texture->LogPbrMaterialState(result->enhancedMaterialModel,
            result->enhancedMaterialProfile,result->lit,result->reflectionEnabled,
            reflectionMode);
        std::memcpy(result->enhancedSunDirection,gVulkanEnhancedSunDirection,
                    sizeof(gVulkanEnhancedSunDirection));
        result->vehicleRearLightMode=gVulkanVehicleRearLightsSuppressed?0u:
            static_cast<uint32_t>(gVulkanVehicleRearLightMode);
        result->vehicleRearLightCount=gVulkanVehicleRearLightsSuppressed?0u:
            static_cast<uint32_t>(gVulkanVehicleRearLightCount);
        std::memcpy(result->vehicleRearLightPositions,gVulkanVehicleRearLightPositions,
                    sizeof(gVulkanVehicleRearLightPositions));
        std::memcpy(result->vehicleRearLightDirections,gVulkanVehicleRearLightDirections,
                    sizeof(gVulkanVehicleRearLightDirections));
        std::memcpy(result->vehicleRearLightColour,gVulkanVehicleRearLightColour,
                    sizeof(gVulkanVehicleRearLightColour));
        result->textureBlendMode=textureBlendMode;
        result->twoLayerColourByVertex=twoLayerColourByVertex;
    }
private:
    void SetEmissiveAlpha(int value)
    {
        const unsigned char alpha=static_cast<unsigned char>(std::max(0,std::min(255,value)));
        diffuse.SetAlpha(alpha);
        const unsigned char materialAlpha=alpha<255?0:255;
        ambient.SetAlpha(materialAlpha);
        emissive.SetAlpha(materialAlpha);
        specular.SetAlpha(materialAlpha);
    }
    std::string name;
    uint32_t shaderClass=SharOpenXR::VulkanMaterialState::ShaderSimple;
    VulkanTexture* texture=nullptr;
    VulkanTexture* reflectionTexture=nullptr;
    VulkanTexture* topTexture=nullptr;
    VulkanTexture* lightMap=nullptr;
    unsigned blendMode=PDDI_BLEND_NONE,alphaCompare=PDDI_COMPARE_GREATEREQUAL;
    unsigned uvMode=PDDI_UV_CLAMP,filterMode=PDDI_FILTER_BILINEAR;
    unsigned materialMode=0,textureBlendMode=PDDI_BLEND_MODULATE;
    bool twoLayerColourByVertex=false;
    bool alphaTest=false,twoSided=false,lit=false;
    float alphaRef=0.5f,shininess=0.0f;
    pddiColour diffuse=pddiColour(255,255,255,255);
    pddiColour ambient=pddiColour(255,255,255,255);
    pddiColour emissive=pddiColour(0,0,0,255);
    pddiColour specular=pddiColour(0,0,0,255);
    pddiColour environmentBlend=pddiColour(128,128,128,128);
};

struct VulkanVertex
{
    float position[3] = {0,0,0};
    float normal[3] = {0,0,1};
    float uv[2] = {0,0};
    unsigned colour = 0xffffffffu;
    float uv1[2] = {0,0};
    float uv2[2] = {0,0};
    float skinWeights[3] = {1,0,0};
    unsigned skinIndices[4] = {0,0,0,0};
};

class VulkanPrimBufferStream final : public pddiPrimBufferStream
{
public:
    explicit VulkanPrimBufferStream(std::vector<VulkanVertex>* value) : vertices(value) {}
    void Reset() { vertex=0; }
    void Position(float x, float y, float z) override
    {
        if(Current())
        {
            Current()->position[0]=x; Current()->position[1]=y; Current()->position[2]=z;
        }
        // Pure3D's retained stream advances on Position/Coord. Other
        // attribute lists explicitly call Next() after writing an attribute.
        ++vertex;
    }
    void Normal(float x, float y, float z) override
    { if(Current()) { Current()->normal[0]=x; Current()->normal[1]=y; Current()->normal[2]=z; } }
    void Colour(pddiColour value, int) override { if(Current()) Current()->colour=PackVertexColour(value); }
    void TexCoord1(float, int) override {}
    void TexCoord2(float u, float v, int channel) override
    {
        if(!Current()) return;
        float* target=channel==0?Current()->uv:(channel==1?Current()->uv1:Current()->uv2);
        target[0]=u; target[1]=v;
    }
    void TexCoord3(float, float, float, int) override {}
    void TexCoord4(float, float, float, float, int) override {}
    void Specular(pddiColour) override {}
    void SkinIndices(unsigned a, unsigned b, unsigned c, unsigned d) override
    { if(Current()) { Current()->skinIndices[0]=a; Current()->skinIndices[1]=b;
                      Current()->skinIndices[2]=c; Current()->skinIndices[3]=d; } }
    void SkinWeights(float a, float b, float c) override
    { if(Current()) { Current()->skinWeights[0]=a; Current()->skinWeights[1]=b;
                      Current()->skinWeights[2]=c; } }
    void Vertex(rmt::Vector* p, pddiColour c) override
    { if(Current()) { Current()->position[0]=p->x; Current()->position[1]=p->y; Current()->position[2]=p->z; Current()->colour=PackVertexColour(c); } ++vertex; }
    void Vertex(rmt::Vector* p, rmt::Vector* n) override
    { if(Current()) { Current()->position[0]=p->x; Current()->position[1]=p->y; Current()->position[2]=p->z; Current()->normal[0]=n->x; Current()->normal[1]=n->y; Current()->normal[2]=n->z; } ++vertex; }
    void Vertex(rmt::Vector* p, rmt::Vector2* t) override
    { if(Current()) { Current()->position[0]=p->x; Current()->position[1]=p->y; Current()->position[2]=p->z; Current()->uv[0]=t->x; Current()->uv[1]=t->y; } ++vertex; }
    void Vertex(rmt::Vector* p, pddiColour c, rmt::Vector2* t) override
    { if(Current()) { Current()->position[0]=p->x; Current()->position[1]=p->y; Current()->position[2]=p->z; Current()->colour=PackVertexColour(c); Current()->uv[0]=t->x; Current()->uv[1]=t->y; } ++vertex; }
    void Vertex(rmt::Vector* p, rmt::Vector* n, rmt::Vector2* t) override
    { if(Current()) { Current()->position[0]=p->x; Current()->position[1]=p->y; Current()->position[2]=p->z; Current()->normal[0]=n->x; Current()->normal[1]=n->y; Current()->normal[2]=n->z; Current()->uv[0]=t->x; Current()->uv[1]=t->y; } ++vertex; }
    void Next() override { ++vertex; }
private:
    VulkanVertex* Current() { return vertices && vertex<vertices->size() ? &(*vertices)[vertex] : nullptr; }
    std::vector<VulkanVertex>* vertices;
    unsigned vertex = 0;
};

class VulkanPrimBuffer final : public pddiPrimBuffer
{
public:
    explicit VulkanPrimBuffer(pddiPrimBufferDesc* desc)
        : vertices(desc ? desc->GetVertexCount() : 0),
          indices(desc ? desc->GetIndexCount() : 0), stream(&vertices),
          primitiveType(desc ? desc->GetPrimType() : PDDI_PRIM_TRIANGLES),
          hasAuthoredNormals(desc && (desc->GetVertexFormat()&PDDI_V_NORMAL)!=0) {}
    ~VulkanPrimBuffer() override
    {
        SharOpenXR::GetVulkanContext().DestroyBuffer(vertexBuffer,vertexMemory);
        SharOpenXR::GetVulkanContext().DestroyBuffer(indexBuffer,indexMemory);
    }
    pddiPrimBufferStream* Lock() override { stream.Reset(); return &stream; }
    void Unlock(pddiPrimBufferStream*) override { GenerateMissingNormals(); UploadVertices(); }
    unsigned char* LockIndexBuffer() override { return reinterpret_cast<unsigned char*>(indices.data()); }
    void UnlockIndexBuffer(int count) override { indices.resize(std::max(0, count)); GenerateMissingNormals(); UploadVertices(); UploadIndices(); }
    void SetIndices(unsigned short* data, int count) override
    {
        indices.assign(data, data + std::max(0, count)); GenerateMissingNormals(); UploadVertices(); UploadIndices();
    }
    bool CheckMemImageVersion(int) override { return false; }
    void* LockMemImage(unsigned size) override { memoryImage.resize(size); return memoryImage.data(); }
    void UnlockMemImage() override {}
    VkBuffer GetVertexBuffer() const { return vertexBuffer; }
    VkBuffer GetIndexBuffer() const { return indexBuffer; }
    uint32_t GetVertexCount() const { return static_cast<uint32_t>(vertices.size()); }
    uint32_t GetIndexCount() const { return static_cast<uint32_t>(indices.size()); }
    pddiPrimType GetPrimitiveType() const { return primitiveType; }
private:
    void GenerateMissingNormals()
    {
        if(hasAuthoredNormals || vertices.empty()) return;
        for(VulkanVertex& vertex:vertices)
            vertex.normal[0]=vertex.normal[1]=vertex.normal[2]=0.0f;
        const auto addTriangle=[&](unsigned ia,unsigned ib,unsigned ic)
        {
            if(ia>=vertices.size()||ib>=vertices.size()||ic>=vertices.size()||ia==ib||ib==ic||ia==ic) return;
            const VulkanVertex& a=vertices[ia]; const VulkanVertex& b=vertices[ib]; const VulkanVertex& c=vertices[ic];
            const float abx=b.position[0]-a.position[0],aby=b.position[1]-a.position[1],abz=b.position[2]-a.position[2];
            const float acx=c.position[0]-a.position[0],acy=c.position[1]-a.position[1],acz=c.position[2]-a.position[2];
            const float nx=aby*acz-abz*acy,ny=abz*acx-abx*acz,nz=abx*acy-aby*acx;
            const unsigned ids[3]={ia,ib,ic};
            for(unsigned id:ids) { vertices[id].normal[0]+=nx; vertices[id].normal[1]+=ny; vertices[id].normal[2]+=nz; }
        };
        const size_t count=indices.empty()?vertices.size():indices.size();
        const auto indexAt=[&](size_t i)->unsigned { return indices.empty()?static_cast<unsigned>(i):indices[i]; };
        if(primitiveType==PDDI_PRIM_TRIANGLES)
        {
            for(size_t i=0;i+2<count;i+=3) addTriangle(indexAt(i),indexAt(i+1),indexAt(i+2));
        }
        else if(primitiveType==PDDI_PRIM_TRISTRIP)
        {
            for(size_t i=0;i+2<count;++i)
            {
                if((i&1)==0) addTriangle(indexAt(i),indexAt(i+1),indexAt(i+2));
                else addTriangle(indexAt(i+1),indexAt(i),indexAt(i+2));
            }
        }
        for(VulkanVertex& vertex:vertices)
        {
            const float length=std::sqrt(vertex.normal[0]*vertex.normal[0]+vertex.normal[1]*vertex.normal[1]+vertex.normal[2]*vertex.normal[2]);
            if(length>0.000001f) { vertex.normal[0]/=length; vertex.normal[1]/=length; vertex.normal[2]/=length; }
            else vertex.normal[2]=1.0f;
        }
    }
    void UploadVertices()
    {
        if(vertices.empty()) return;
        SharOpenXR::VulkanContext& context=SharOpenXR::GetVulkanContext();
        const VkDeviceSize size=vertices.size()*sizeof(VulkanVertex);
        const std::chrono::steady_clock::time_point uploadStart=
            std::chrono::steady_clock::now();
        // CPU-skinned meshes unlock their vertex stream whenever the pose
        // changes. Keep the coherent allocation and update it in place;
        // allocating/freeing VkDeviceMemory for every character part on every
        // animation tick creates large driver spikes during area streaming.
        VkBuffer replacement=VK_NULL_HANDLE;
        VkDeviceMemory replacementMemory=VK_NULL_HANDLE;
        // Quest uses unified memory. Uploading directly into a coherent
        // vertex buffer avoids one queue submit + infinite fence wait for
        // every primitive created while a new area streams in.
        bool uploaded=context.CreateBuffer(size,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &replacement,&replacementMemory) &&
                context.UploadMemory(replacementMemory,0,vertices.data(),size);
        if(uploaded)
        {
            context.DestroyBuffer(vertexBuffer,vertexMemory);
            vertexBuffer=replacement;
            vertexMemory=replacementMemory;
            ++gUploadedVertexBuffers;
            SharOpenXR::RecordPddiUpload(static_cast<unsigned>(size),
                std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-uploadStart).count());
        }
        else context.DestroyBuffer(replacement,replacementMemory);
    }
    void UploadIndices()
    {
        if(indices.empty()) return;
        SharOpenXR::VulkanContext& context=SharOpenXR::GetVulkanContext();
        const VkDeviceSize size=indices.size()*sizeof(unsigned short);
        const std::chrono::steady_clock::time_point uploadStart=
            std::chrono::steady_clock::now();
        VkBuffer replacement=VK_NULL_HANDLE;
        VkDeviceMemory replacementMemory=VK_NULL_HANDLE;
        bool uploaded=context.CreateBuffer(size,VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &replacement,&replacementMemory) &&
                context.UploadMemory(replacementMemory,0,indices.data(),size);
        if(uploaded)
        {
            context.DestroyBuffer(indexBuffer,indexMemory);
            indexBuffer=replacement;
            indexMemory=replacementMemory;
            SharOpenXR::RecordPddiUpload(static_cast<unsigned>(size),
                std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-uploadStart).count());
        }
        else context.DestroyBuffer(replacement,replacementMemory);
    }
    std::vector<VulkanVertex> vertices;
    std::vector<unsigned short> indices;
    std::vector<unsigned char> memoryImage;
    VulkanPrimBufferStream stream;
    VkBuffer vertexBuffer=VK_NULL_HANDLE,indexBuffer=VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory=VK_NULL_HANDLE,indexMemory=VK_NULL_HANDLE;
    pddiPrimType primitiveType;
    bool hasAuthoredNormals;
};

class VulkanImmediateStream final : public pddiPrimStream
{
public:
    void Reset(int expected)
    {
        positions.clear(); normals.clear(); uvs.clear(); colours.clear(); vertices.clear();
        if(expected > 0)
        {
            const size_t count=static_cast<size_t>(expected);
            positions.reserve(count); normals.reserve(count); uvs.reserve(count);
            colours.reserve(count); vertices.reserve(count);
        }
    }
    void Coord(float x, float y, float z) override
    { positions.push_back({x,y,z}); }
    void Normal(float x, float y, float z) override
    { normals.push_back({x,y,z}); }
    void Colour(pddiColour colour, int channel) override
    { if(channel==0) colours.push_back(PackVertexColour(colour)); }
    void UV(float u, float v, int channel) override
    { if(channel==0) uvs.push_back({u,v}); }
    void Specular(pddiColour) override {}
    void Vertex(pddiVector* p, pddiColour c) override
    { Colour(c,0); Coord(p->x,p->y,p->z); }
    void Vertex(pddiVector* p, pddiVector* n) override
    { Normal(n->x,n->y,n->z); Coord(p->x,p->y,p->z); }
    void Vertex(pddiVector* p, pddiVector2* uv) override
    { UV(uv->x,uv->y,0); Coord(p->x,p->y,p->z); }
    void Vertex(pddiVector* p, pddiColour c, pddiVector2* uv) override
    { Colour(c,0); UV(uv->x,uv->y,0); Coord(p->x,p->y,p->z); }
    void Vertex(pddiVector* p, pddiVector* n, pddiVector2* uv) override
    { Normal(n->x,n->y,n->z); UV(uv->x,uv->y,0); Coord(p->x,p->y,p->z); }
    const std::vector<VulkanVertex>& GetVertices()
    {
        vertices.assign(positions.size(),VulkanVertex());
        for(size_t i=0;i<positions.size();++i)
        {
            vertices[i].position[0]=positions[i].x;
            vertices[i].position[1]=positions[i].y;
            vertices[i].position[2]=positions[i].z;
            if(i<normals.size())
            {
                vertices[i].normal[0]=normals[i].x;
                vertices[i].normal[1]=normals[i].y;
                vertices[i].normal[2]=normals[i].z;
            }
            if(i<uvs.size()) { vertices[i].uv[0]=uvs[i].x; vertices[i].uv[1]=uvs[i].y; }
            if(i<colours.size()) vertices[i].colour=colours[i];
        }
        return vertices;
    }
private:
    std::vector<pddiVector> positions,normals;
    std::vector<pddiVector2> uvs;
    std::vector<unsigned> colours;
    std::vector<VulkanVertex> vertices;
};

class VulkanGammaControl final : public pddiExtGammaControl
{
public:
    void SetGamma(float red, float green, float blue) override
    {
        r = red; g = green; b = blue;
    }
    void GetGamma(float* red, float* green, float* blue) override
    {
        if (red) *red = r; if (green) *green = g; if (blue) *blue = b;
    }
private:
    float r = 1.0f, g = 1.0f, b = 1.0f;
};

class VulkanMemoryRegistration final : public pddiExtMemRegistration
{
public:
    void Register(CallBack* value) override { callback = value; }
private:
    CallBack* callback = nullptr;
};

class VulkanHardwareSkinning final : public pddiExtHardwareSkinning
{
public:
    explicit VulkanHardwareSkinning(pddiRenderContext* value) : context(value) {}
    int MaxMatrixPaletteSize(unsigned) override
    { return SharOpenXR::VulkanMaterialState::MaxSkinMatrices; }
    void Begin() override {}
    void End() override {}
    void SetMatrixCount(unsigned count) override
    { gSkinMatrixCount=std::min<unsigned>(count,SharOpenXR::VulkanMaterialState::MaxSkinMatrices); }
    void SetMatrix(unsigned index,pddiMatrix* matrix) override
    { if(index<gSkinMatrixCount && matrix) gSkinMatrices[index]=*matrix; }
    void DrawSkin(pddiShader* shader,pddiPrimBuffer* skin) override
    {
        gDrawingSkin=true;
        context->DrawPrimBuffer(shader,skin);
        gDrawingSkin=false;
    }
private:
    pddiRenderContext* context;
};

class VulkanContext final : public pddiBaseContext
{
public:
    VulkanContext(pddiDevice* device, pddiDisplay* display) : pddiBaseContext(display, device),
        hardwareSkinning(this)
    {
        DefaultState();
        projection.Identity();
        started = std::chrono::steady_clock::now();
    }
    void BeginFrame() override { pddiBaseContext::BeginFrame(); }
    void Clear(unsigned bufferMask) override
    {
        SharOpenXR::GetVulkanContext().ClearPddiBuffers(bufferMask);
    }
    bool BeginSunShadowMap(int cascadeIndex,const pddiMatrix& eyeCameraToWorld,
                           pddiMatrix* lightWorldToCamera,pddiMatrix* lightCameraToWorld)
    {
        if(cascadeIndex<0 || cascadeIndex>=3 || !lightWorldToCamera || !lightCameraToWorld) return false;
        // Keep the same depth precision and PCF footprint as the proven GLES
        // path.  Reducing the cached cascades made their texels large enough
        // to open visible gaps where a wall/pole meets its receiver.
        static const uint32_t sizes[3]={2048,2048,1024};
        static const float halfWidths[3]={24.0f,56.0f,224.0f};
        static const float halfDepths[3]={64.0f,96.0f,155.0f};
        rmt::Matrix centreCamera=eyeCameraToWorld;
        SharOpenXR::GetLatestCullingCamera(&centreCamera);
        rmt::Vector requested=centreCamera.Row(3);
        // Do not cache any cascade across world frames. A separately rendered
        // right eye may only reuse maps produced earlier in this XR frame.
        if(SharOpenXR::IsRightEyeRendering() && shadowReady[cascadeIndex])
        {
            // The depth image was rendered with the stored world-to-clip
            // matrix. Never replace that matrix unless the image is rendered
            // again; only rebuild the eye-space conversion for this eye.
            // Vulkan multiview submits geometry from the shared midpoint
            // culling camera.  Its viewPosition must be converted back to
            // world space with that same camera, not the legacy mpView
            // camera passed by WorldRenderLayer.
            shadowReceiverMatrix[cascadeIndex].Mult(
                centreCamera,shadowWorldToClip[cascadeIndex]);
            std::memcpy(shadowReceiverMatrices+cascadeIndex*16,
                        shadowReceiverMatrix[cascadeIndex].m[0],sizeof(float)*16);
            return false;
        }

        rmt::Vector direction(-0.45f,-1.0f,0.30f);
        if(state.lightingState) for(int i=0;i<PDDI_MAX_LIGHTS;++i)
            if(state.lightingState->light[i].enabled && state.lightingState->light[i].type==PDDI_LIGHT_DIRECTIONAL)
            { direction.Set(state.lightingState->light[i].worldDirection.x,
                            state.lightingState->light[i].worldDirection.y,
                            state.lightingState->light[i].worldDirection.z); break; }
        direction.Normalize();
        // A cached map is only valid for the light direction with which it
        // was rendered. This additionally fixes the GLES cache's stale-map
        // behaviour when a level or time-of-day changes its directional light.
        if(!shadowLightDirectionValid || direction.DotProduct(shadowLightDirection)<0.99999f)
        {
            for(int i=0;i<3;++i) shadowCascadeCentreValid[i]=false;
            shadowLightDirection=direction;
            shadowLightDirectionValid=true;
        }
        if(!shadowStableCentreValid)
        {
            shadowStableCentre=requested;
            shadowStableCentreValid=true;
        }
        else
        {
            const rmt::Vector delta=requested-shadowStableCentre;
            if(delta.x*delta.x+delta.z*delta.z>16.0f || fabsf(delta.y)>2.0f)
                shadowStableCentre=requested;
        }
        // Match the proven GLES stabilization exactly. Keep the cascade
        // centre in world space and quantize world translation, avoiding an
        // extra radmath Transform round-trip whose convention displaced the
        // Vulkan map as the player moved.
        rmt::Vector centre=shadowStableCentre;
        const float texel=halfWidths[cascadeIndex]*2.0f/sizes[cascadeIndex];
        centre.x=floorf(centre.x/texel+0.5f)*texel;
        centre.y=floorf(centre.y/texel+0.5f)*texel;
        centre.z=floorf(centre.z/texel+0.5f)*texel;
        // Mid/far cascades contain static casters. Keep their depth images
        // until the GLES-style stabilized origin actually changes.
        if(cascadeIndex>0 && shadowReady[cascadeIndex] &&
           shadowCascadeCentreValid[cascadeIndex])
        {
            const rmt::Vector delta=centre-shadowCascadeCentre[cascadeIndex];
            if(delta.MagnitudeSqr()<0.000001f)
            {
                shadowReceiverMatrix[cascadeIndex].Mult(
                    centreCamera,shadowWorldToClip[cascadeIndex]);
                std::memcpy(shadowReceiverMatrices+cascadeIndex*16,
                            shadowReceiverMatrix[cascadeIndex].m[0],sizeof(float)*16);
                return false;
            }
        }
        shadowCascadeCentre[cascadeIndex]=centre;
        shadowCascadeCentreValid[cascadeIndex]=true;
        // Match the proven GLES shadow camera exactly.  The sun direction was
        // calculated above, but the Vulkan path previously discarded it and
        // left the light camera with only a translation.  Consequently the
        // orthographic volume looked along the default world axis instead of
        // along the sun and contained few (often no) useful caster/receiver
        // pairs.
        lightCameraToWorld->Identity();
        lightCameraToWorld->FillHeading(direction,rmt::Vector(0.0f,1.0f,0.0f));
        lightCameraToWorld->FillTranslate(centre);
        lightWorldToCamera->InvertOrtho(*lightCameraToWorld);
        pddiMatrix lightProjection; lightProjection.Identity();
        lightProjection.SetOrthographic(-halfWidths[cascadeIndex],halfWidths[cascadeIndex],
            -halfWidths[cascadeIndex],halfWidths[cascadeIndex],-halfDepths[cascadeIndex],halfDepths[cascadeIndex]);
        shadowWorldToClip[cascadeIndex].Mult(*lightWorldToCamera,lightProjection);
        shadowReceiverMatrix[cascadeIndex].Mult(centreCamera,shadowWorldToClip[cascadeIndex]);
        std::memcpy(shadowReceiverMatrices+cascadeIndex*16,shadowReceiverMatrix[cascadeIndex].m[0],sizeof(float)*16);
        shadowSavedProjection=projection; projection=lightProjection;
        if(!SharOpenXR::GetVulkanContext().BeginShadowCascade(cascadeIndex,sizes[cascadeIndex]))
        { projection=shadowSavedProjection; return false; }
        gPglCsmBillboardMode=1; shadowCurrentCascade=cascadeIndex; return true;
    }
    void EndSunShadowMap(int cascadeIndex,const pddiMatrix& eyeCameraToWorld)
    {
        if(cascadeIndex!=shadowCurrentCascade) return;
        SharOpenXR::GetVulkanContext().EndShadowCascade(cascadeIndex);
        shadowReady[cascadeIndex]=true; gPglCsmBillboardMode=0; projection=shadowSavedProjection;
        rmt::Matrix receiverCamera=eyeCameraToWorld;
        SharOpenXR::GetLatestCullingCamera(&receiverCamera);
        shadowReceiverMatrix[cascadeIndex].Mult(receiverCamera,shadowWorldToClip[cascadeIndex]);
        std::memcpy(shadowReceiverMatrices+cascadeIndex*16,shadowReceiverMatrix[cascadeIndex].m[0],sizeof(float)*16);
        shadowCurrentCascade=-1;
    }
    void EnableSunShadowReceivers(bool enable)
    { SharOpenXR::GetVulkanContext().SetShadowReceiverState(enable,shadowReceiverMatrices); }
    void EndFrame() override
    {
        pddiBaseContext::EndFrame();
        if(submittedDraws && !drawPathLogged)
        {
            SDL_Log("Vulkan PDDI: submitted %u draw calls (immediate total %u)",
                    submittedDraws,gImmediateDrawCalls);
            drawPathLogged=true;
        }
        submittedDraws=0;
        if(!diagnosticsLogged && (gCreatedPrimBuffers || gDrawPrimBufferCalls))
        {
            SDL_Log("Vulkan PDDI: buffers=%u uploads=%u draw-visits=%u",
                    gCreatedPrimBuffers,gUploadedVertexBuffers,gDrawPrimBufferCalls);
            diagnosticsLogged=true;
        }
    }
    pddiPrimStream* BeginPrims(pddiShader* shader, pddiPrimType type, unsigned, int count, unsigned) override
    {
        immediateType=type;
        immediateShader=static_cast<VulkanShader*>(shader);
        immediate.Reset(count);
        return &immediate;
    }
    void EndPrims(pddiPrimStream* value) override
    {
        if(value != &immediate || immediate.GetVertices().empty()) return;
        SharOpenXR::VulkanEyeTarget target={};
        SharOpenXR::VulkanContext& context=SharOpenXR::GetVulkanContext();
        if(!context.GetVehicleCubeMapTarget(&target) &&
           !SharOpenXR::GetActiveVulkanEyeTarget(&target)) return;
        VkBuffer buffer=VK_NULL_HANDLE;
        VkDeviceSize vertexOffset=0;
        const std::vector<VulkanVertex>& vertices=immediate.GetVertices();
        const VkDeviceSize size=vertices.size()*sizeof(VulkanVertex);
        bool uploaded=context.UploadTransientVertices(vertices.data(),size,&buffer,&vertexOffset);
        const SharOpenXR::VulkanMaterialState material=MakeMaterialState(immediateShader);
        const std::chrono::steady_clock::time_point drawStart=std::chrono::steady_clock::now();
        bool drawn=uploaded && context.DrawPddiGeometry(
              target.image,target.format,target.width,target.height,target.arrayLayer,
              buffer,VK_NULL_HANDLE,vertexOffset,static_cast<uint32_t>(vertices.size()),0,
              ToTopology(immediateType),projection.m[0],GetMatrix(PDDI_MATRIX_MODELVIEW)->m[0],
              immediateShader?immediateShader->GetTextureSet():VK_NULL_HANDLE,
              immediateShader?immediateShader->GetReflectionSet():VK_NULL_HANDLE,
              immediateShader?immediateShader->GetTopTextureSet():VK_NULL_HANDLE,
              immediateShader?immediateShader->GetLightMapSet():VK_NULL_HANDLE,material);
        if(drawn)
        {
            ++submittedDraws; ++gImmediateDrawCalls;
            const double milliseconds=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-drawStart).count();
            SharOpenXR::RecordPddiDraw(immediateType==PDDI_PRIM_TRISTRIP?1u:0u,
                static_cast<unsigned>(vertices.size()),false,milliseconds);
        }
    }
    void DrawPrimBuffer(pddiShader* shader, pddiPrimBuffer* value) override
    {
        ++gDrawPrimBufferCalls;
        VulkanPrimBuffer* buffer=static_cast<VulkanPrimBuffer*>(value);
        if(!buffer || !buffer->GetVertexBuffer()) return;
        SharOpenXR::VulkanEyeTarget target={};
        SharOpenXR::VulkanContext& context=SharOpenXR::GetVulkanContext();
        if(!context.GetVehicleCubeMapTarget(&target) &&
           !SharOpenXR::GetActiveVulkanEyeTarget(&target)) return;
        pddiMatrix* modelview=GetMatrix(PDDI_MATRIX_MODELVIEW);
        VulkanShader* vulkanShader=static_cast<VulkanShader*>(shader);
        const SharOpenXR::VulkanMaterialState material=MakeMaterialState(vulkanShader);
        const std::chrono::steady_clock::time_point drawStart=std::chrono::steady_clock::now();
        if(SharOpenXR::GetVulkanContext().DrawPddiGeometry(
              target.image,target.format,target.width,target.height,target.arrayLayer,
              buffer->GetVertexBuffer(),buffer->GetIndexBuffer(),
              0,
              buffer->GetVertexCount(),buffer->GetIndexCount(),ToTopology(buffer->GetPrimitiveType()),
              projection.m[0],modelview->m[0],
              vulkanShader?vulkanShader->GetTextureSet():VK_NULL_HANDLE,
              vulkanShader?vulkanShader->GetReflectionSet():VK_NULL_HANDLE,
              vulkanShader?vulkanShader->GetTopTextureSet():VK_NULL_HANDLE,
              vulkanShader?vulkanShader->GetLightMapSet():VK_NULL_HANDLE,material))
        {
            ++submittedDraws;
            const uint32_t count=buffer->GetIndexCount()?buffer->GetIndexCount():
                                                       buffer->GetVertexCount();
            const double milliseconds=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-drawStart).count();
            SharOpenXR::RecordPddiDraw(buffer->GetPrimitiveType()==PDDI_PRIM_TRISTRIP?1u:0u,
                count,buffer->GetIndexCount()!=0,milliseconds);
        }
    }
    int GetMaxTextureDimension() override { return 16384; }
    pddiExtension* GetExtension(unsigned id) override
    {
        if (id == PDDI_EXT_GAMMACONTROL) return &gamma;
        if (id == PDDI_EXT_MEM_REGISTRATION) return &memoryRegistration;
        if (id == PDDI_EXT_HARDWARE_SKINNING) return &hardwareSkinning;
        return nullptr;
    }
    bool VerifyExtension(unsigned id) override
    {
        return id == PDDI_EXT_GAMMACONTROL || id == PDDI_EXT_MEM_REGISTRATION ||
               id == PDDI_EXT_HARDWARE_SKINNING;
    }
protected:
    void LoadHardwareMatrix(pddiMatrixType) override {}
    void SetupHardwareProjection() override
    {
        int width=0,height=0;
        if(SharOpenXR::IsRadarRendering && SharOpenXR::IsRadarRendering() &&
           (!SharOpenXR::IsEmbeddedHudRendering || !SharOpenXR::IsEmbeddedHudRendering()) &&
           SharOpenXR::GetActiveRadarProjection &&
            SharOpenXR::GetActiveRadarProjection(&projection,&width,&height))
            return;
        if(SharOpenXR::IsMovieRendering && SharOpenXR::IsMovieRendering() &&
           SharOpenXR::GetActiveMovieProjection &&
           SharOpenXR::GetActiveMovieProjection(&projection,&width,&height))
            return;
        // Frontend, pause and loading screens are authored as a flat Pure3D
        // canvas. The OpenXR manager supplies a world-locked panel transform
        // for each eye; using the ordinary device projection here leaves the
        // menu pasted to the eye buffer instead of forming the GLES-style 3D
        // window.
        if(SharOpenXR::IsFrontendPlaneRendering &&
           SharOpenXR::IsFrontendPlaneRendering() &&
           SharOpenXR::GetActiveFrontendProjection &&
           SharOpenXR::GetActiveFrontendProjection(&projection,&width,&height))
            return;
        if(SharOpenXR::GetActiveProjection(&projection,&width,&height)) return;
        projection.Identity();
        if(state.viewState->projectionMode==PDDI_PROJECTION_DEVICE)
            projection.SetOrthographic(0,display->GetWidth(),display->GetHeight(),0,
                state.viewState->camera.nearPlane,state.viewState->camera.farPlane);
        else if(state.viewState->projectionMode==PDDI_PROJECTION_ORTHOGRAPHIC)
            projection.SetOrthographic(-0.5f,0.5f,-0.5f/state.viewState->camera.aspect,
                0.5f/state.viewState->camera.aspect,state.viewState->camera.nearPlane,
                state.viewState->camera.farPlane);
        else projection.SetPerspective(state.viewState->camera.fov,state.viewState->camera.aspect,
                state.viewState->camera.nearPlane,state.viewState->camera.farPlane);

        // Match the GLES head-locked HUD contract. The authored 16:9 canvas
        // occupies the comfortable central part of each eye rather than the
        // complete native Quest render target, and converges on a common
        // virtual plane despite the asymmetric per-eye FOV.
        float uiOffset=0.0f;
        if((!SharOpenXR::IsEmbeddedHudRendering ||
            !SharOpenXR::IsEmbeddedHudRendering()) &&
           SharOpenXR::GetActiveUiHorizontalOffset &&
           SharOpenXR::GetActiveUiHorizontalOffset(&uiOffset))
        {
            const float uiScale=0.40f;
            if(SharOpenXR::HasEnhancedUiConvergence &&
               SharOpenXR::HasEnhancedUiConvergence())
                uiOffset*=2.0f;
            for(int row=0;row<4;++row)
            {
                projection.m[row][0]*=uiScale;
                projection.m[row][1]*=uiScale;
            }
            projection.m[3][0]+=uiOffset;
            projection.m[3][1]-=0.02f;
        }
    }
    void SetupHardwareLight(int) override {}
    void BeginTiming() override { started = std::chrono::steady_clock::now(); }
    float EndTiming() override
    {
        return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
    }
private:
    SharOpenXR::VulkanMaterialState MakeMaterialState(const VulkanShader* shader) const
    {
        SharOpenXR::VulkanMaterialState result={};
        result.hudPass=(SharOpenXR::IsEmbeddedHudRendering &&
                        SharOpenXR::IsEmbeddedHudRendering()) ||
                       (SharOpenXR::IsFrontendPlaneRendering &&
                        SharOpenXR::IsFrontendPlaneRendering());
        result.colour[0]=result.colour[1]=result.colour[2]=result.colour[3]=1.0f;
        result.alphaRef=0.5f;
        result.alphaCompare=PDDI_COMPARE_GREATEREQUAL;
        if(shader) shader->FillMaterialState(&result);
        if(gDrawingSkin)
        {
            result.skinMatrixCount=gSkinMatrixCount;
            for(unsigned i=0;i<gSkinMatrixCount;++i)
                std::memcpy(result.skinMatrices[i],gSkinMatrices[i].m[0],sizeof(float)*16);
        }
        rmt::Matrix reflectionViewToWorld;
        reflectionViewToWorld.Identity();
        if(result.reflectionEnabled || result.enhancedMaterialModel!=0)
            SharOpenXR::GetLatestCullingCamera(&reflectionViewToWorld);
        std::memcpy(result.reflectionViewToWorld,reflectionViewToWorld.m[0],
                    sizeof(result.reflectionViewToWorld));
        if(result.lit && state.lightingState)
        {
            const pddiColour globalAmbient=state.lightingState->ambient;
            const float emissive[3]={result.lightPosition[0][0],
                                     result.lightPosition[0][1],
                                     result.lightPosition[0][2]};
            for(unsigned channel=0;channel<3;++channel)
            {
                const float global=channel==0?globalAmbient.Red()/255.0f:
                                   channel==1?globalAmbient.Green()/255.0f:
                                              globalAmbient.Blue()/255.0f;
                result.ambientTerm[channel]=
                    emissive[channel]+result.ambientTerm[channel]*global;
            }
            for(int i=0;i<PDDI_MAX_LIGHTS;++i)
            {
                const pddiLight& light=state.lightingState->light[i];
                if(!light.enabled) continue;
                if(light.type==PDDI_LIGHT_DIRECTIONAL)
                {
                    result.lightPosition[i][0]=-light.worldDirection.x;
                    result.lightPosition[i][1]=-light.worldDirection.y;
                    result.lightPosition[i][2]=-light.worldDirection.z;
                    result.lightPosition[i][3]=0.0f;
                }
                else if(light.type==PDDI_LIGHT_POINT)
                {
                    result.lightPosition[i][0]=light.worldPosition.x;
                    result.lightPosition[i][1]=light.worldPosition.y;
                    result.lightPosition[i][2]=light.worldPosition.z;
                    result.lightPosition[i][3]=1.0f;
                }
                else continue;
                result.lightColour[i][0]=light.colour.Red()/255.0f;
                result.lightColour[i][1]=light.colour.Green()/255.0f;
                result.lightColour[i][2]=light.colour.Blue()/255.0f;
                result.lightColour[i][3]=1.0f;
                result.lightAttenuation[i][0]=light.attenA;
                result.lightAttenuation[i][1]=light.attenB;
                result.lightAttenuation[i][2]=light.attenC;
                result.lightAttenuation[i][3]=1.0f;
            }
        }
        else
        {
            result.ambientTerm[0]=result.ambientTerm[1]=
            result.ambientTerm[2]=1.0f;
            result.ambientTerm[3]=0.0f;
            std::memset(result.lightPosition,0,sizeof(result.lightPosition));
            std::memset(result.lightColour,0,sizeof(result.lightColour));
            std::memset(result.lightAttenuation,0,sizeof(result.lightAttenuation));
        }
        const pddiRenderState* render=state.renderState;
        result.scissorX=0; result.scissorY=0;
        result.scissorWidth=static_cast<uint32_t>(std::max(1,display->GetWidth()));
        result.scissorHeight=static_cast<uint32_t>(std::max(1,display->GetHeight()));
        result.scissorSurfaceWidth=result.scissorWidth;
        result.scissorSurfaceHeight=result.scissorHeight;
        result.viewportLeft=result.viewportTop=0.0f;
        result.viewportWidth=result.viewportHeight=1.0f;
        result.depthBias=0.0f;
        result.stencilTest=false; result.stencilCompare=VK_COMPARE_OP_ALWAYS;
        result.stencilFail=result.stencilDepthFail=result.stencilPass=VK_STENCIL_OP_KEEP;
        result.stencilReference=0; result.stencilCompareMask=result.stencilWriteMask=0xff;
        result.fogEnabled=false; result.fogStart=0.0f; result.fogEnd=1.0f;
        result.fogColour[0]=result.fogColour[1]=result.fogColour[2]=0.0f;
        result.fogColour[3]=1.0f;
        if(state.viewState)
        {
            const pddiFloatRect& window=state.viewState->viewWindow;
            result.viewportLeft=window.left;
            result.viewportTop=window.top;
            result.viewportWidth=window.right-window.left;
            result.viewportHeight=window.bottom-window.top;
            const pddiRect& scissor=state.viewState->scissor;
            if(scissor.right>scissor.left && scissor.bottom>scissor.top)
            {
                result.scissorX=scissor.left; result.scissorY=scissor.top;
                result.scissorWidth=static_cast<uint32_t>(scissor.right-scissor.left);
                result.scissorHeight=static_cast<uint32_t>(scissor.bottom-scissor.top);
            }
            result.depthBias=state.viewState->zBias;
        }
        if(SharOpenXR::IsRadarRendering && SharOpenXR::IsRadarRendering())
        {
            // GLES explicitly disables the eye/UI scissor while drawing into
            // a smaller HUD framebuffer. Do the same in Vulkan; otherwise a
            // stale screen-space box clips icons and wrapped sampling makes
            // the surviving pieces appear at opposite sides of the panel.
            int captureWidth=0,captureHeight=0; rmt::Matrix unused;
            const bool hasRadarProjection=SharOpenXR::GetActiveRadarProjection &&
                SharOpenXR::GetActiveRadarProjection(&unused,&captureWidth,&captureHeight);
            // GLES PrepareRadarDraw disables the inherited GUI scissor for
            // every radar draw, including embedded Map0 geometry.  Map0 keeps
            // its own top-down projection, so GetActiveRadarProjection
            // intentionally returns false for it; that must not retain the
            // stale eye-space scissor and discard all road primitives.
            if(!hasRadarProjection)
            {
                captureWidth=display->GetWidth();
                captureHeight=display->GetHeight();
            }
            result.scissorX=result.scissorY=0;
            result.scissorWidth=result.scissorSurfaceWidth=std::max(1,captureWidth);
            result.scissorHeight=result.scissorSurfaceHeight=std::max(1,captureHeight);
            if(SharOpenXR::IsEmbeddedHudRendering &&
               SharOpenXR::IsEmbeddedHudRendering())
            {
                static bool loggedEmbeddedRadarState=false;
                if(!loggedEmbeddedRadarState)
                {
                    SDL_Log("OpenXR: Vulkan Map0 draw-state viewport=%.4f,%.4f %.4fx%.4f scissor=%d,%d %ux%u surface=%ux%u",
                            result.viewportLeft,result.viewportTop,
                            result.viewportWidth,result.viewportHeight,
                            result.scissorX,result.scissorY,
                            result.scissorWidth,result.scissorHeight,
                            result.scissorSurfaceWidth,result.scissorSurfaceHeight);
                    loggedEmbeddedRadarState=true;
                }
            }
        }
        else
        {
            float uiOffset=0.0f;
            if(SharOpenXR::GetActiveUiHorizontalOffset &&
               SharOpenXR::GetActiveUiHorizontalOffset(&uiOffset))
            {
                // The projection already centres and scales the complete
                // legacy HUD. Keeping its old screen-space scissor produces
                // a head-locked rectangular clipping window in Vulkan.
                result.scissorX=result.scissorY=0;
                result.scissorWidth=result.scissorSurfaceWidth=static_cast<uint32_t>(
                    std::max(1,display->GetWidth()));
                result.scissorHeight=result.scissorSurfaceHeight=static_cast<uint32_t>(
                    std::max(1,display->GetHeight()));
                if(SharOpenXR::IsEmbeddedHudRendering &&
                   SharOpenXR::IsEmbeddedHudRendering())
                {
                    const float uiScale=0.40f;
                    // uiOffset is an NDC translation. A viewport origin is
                    // normalized in [0,1], so the equivalent displacement is
                    // half of the NDC value. Applying it one-for-one shifted
                    // the independent Map0 camera to the right of its 2D
                    // bezel and could move the right-eye copy out of view.
                    result.viewportLeft=0.5f+(result.viewportLeft-0.5f)*uiScale+
                                        uiOffset*0.5f;
                    result.viewportTop=0.5f+(result.viewportTop-0.5f)*uiScale+0.01f;
                    result.viewportWidth*=uiScale;
                    result.viewportHeight*=uiScale;
                }
            }
        }
        if(state.stencilState)
        {
            const pddiStencilState& stencil=*state.stencilState;
            result.stencilTest=stencil.enabled;
            switch(stencil.compare)
            {
                case PDDI_COMPARE_LESS: result.stencilCompare=VK_COMPARE_OP_LESS; break;
                case PDDI_COMPARE_LESSEQUAL: result.stencilCompare=VK_COMPARE_OP_LESS_OR_EQUAL; break;
                case PDDI_COMPARE_GREATER: result.stencilCompare=VK_COMPARE_OP_GREATER; break;
                case PDDI_COMPARE_GREATEREQUAL: result.stencilCompare=VK_COMPARE_OP_GREATER_OR_EQUAL; break;
                case PDDI_COMPARE_EQUAL: result.stencilCompare=VK_COMPARE_OP_EQUAL; break;
                case PDDI_COMPARE_NOTEQUAL: result.stencilCompare=VK_COMPARE_OP_NOT_EQUAL; break;
                case PDDI_COMPARE_NONE: result.stencilCompare=VK_COMPARE_OP_NEVER; break;
                default: result.stencilCompare=VK_COMPARE_OP_ALWAYS; break;
            }
            const auto stencilOp=[](pddiStencilOp op)
            {
                switch(op)
                {
                    case PDDI_STENCIL_ZERO:return VK_STENCIL_OP_ZERO;
                    case PDDI_STENCIL_REPLACE:return VK_STENCIL_OP_REPLACE;
                    case PDDI_STENCIL_INCR:return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
                    case PDDI_STENCIL_DECR:return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
                    case PDDI_STENCIL_INVERT:return VK_STENCIL_OP_INVERT;
                    default:return VK_STENCIL_OP_KEEP;
                }
            };
            result.stencilFail=stencilOp(stencil.failOp);
            result.stencilDepthFail=stencilOp(stencil.zFailOp);
            result.stencilPass=stencilOp(stencil.zPassOp);
            result.stencilReference=static_cast<uint32_t>(stencil.ref);
            result.stencilCompareMask=stencil.mask;
            result.stencilWriteMask=stencil.writeMask;
        }
        // Android GLES records fog state but does not consume it in its
        // shader. Applying it only in Vulkan darkens/desaturates the world,
        // so keep it disabled until fog is offered as an explicit enhancement.
        result.cullMode=VK_CULL_MODE_BACK_BIT;
        if(render)
        {
            if(render->cullMode==PDDI_CULL_NONE) result.cullMode=VK_CULL_MODE_NONE;
            else if(render->cullMode==PDDI_CULL_INVERTED ||
                    render->cullMode==PDDI_CULL_SHADOW_FRONTFACE)
                result.cullMode=VK_CULL_MODE_FRONT_BIT;
            result.colourWriteMask=0;
            if(render->redWrite) result.colourWriteMask|=VK_COLOR_COMPONENT_R_BIT;
            if(render->greenWrite) result.colourWriteMask|=VK_COLOR_COMPONENT_G_BIT;
            if(render->blueWrite) result.colourWriteMask|=VK_COLOR_COMPONENT_B_BIT;
            if(render->alphaWrite) result.colourWriteMask|=VK_COLOR_COMPONENT_A_BIT;
            result.depthTest=render->zEnabled;
            result.depthWrite=render->zWrite;
            switch(render->zCompare)
            {
                case PDDI_COMPARE_ALWAYS: result.depthCompare=VK_COMPARE_OP_ALWAYS; break;
                case PDDI_COMPARE_LESS: result.depthCompare=VK_COMPARE_OP_LESS; break;
                case PDDI_COMPARE_LESSEQUAL: result.depthCompare=VK_COMPARE_OP_LESS_OR_EQUAL; break;
                case PDDI_COMPARE_GREATER: result.depthCompare=VK_COMPARE_OP_GREATER; break;
                case PDDI_COMPARE_GREATEREQUAL: result.depthCompare=VK_COMPARE_OP_GREATER_OR_EQUAL; break;
                case PDDI_COMPARE_EQUAL: result.depthCompare=VK_COMPARE_OP_EQUAL; break;
                case PDDI_COMPARE_NOTEQUAL: result.depthCompare=VK_COMPARE_OP_NOT_EQUAL; break;
                default: result.depthCompare=VK_COMPARE_OP_ALWAYS; break;
            }
        }
        else
        {
            result.colourWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                                   VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
            result.depthCompare=VK_COMPARE_OP_LESS_OR_EQUAL;
        }
        // Map0 is a coloured Pure3D widget embedded in the ordinary flat HUD.
        // Unlike a world draw, its roads must not be rejected by depth left by
        // the eye scene (or by the authored Hole0 mask). GLES renders this
        // widget with effective HUD visibility. Preserve the spatial radar's
        // offscreen depth mask, but give Original mode the same deterministic
        // result independently of winding and world depth.
        const bool originalEmbeddedMap=
            SharOpenXR::IsEmbeddedHudRendering &&
            SharOpenXR::IsEmbeddedHudRendering() &&
            !SharOpenXR::IsSpatialHudEnabled() &&
            (result.colourWriteMask&(VK_COLOR_COMPONENT_R_BIT|
                                     VK_COLOR_COMPONENT_G_BIT|
                                     VK_COLOR_COMPONENT_B_BIT))!=0;
        if(originalEmbeddedMap)
        {
            result.depthTest=false;
            result.depthWrite=false;
            result.cullMode=VK_CULL_MODE_NONE;
            result.twoSided=true;
        }
        return result;
    }
    static VkPrimitiveTopology ToTopology(pddiPrimType type)
    {
        switch(type)
        {
            case PDDI_PRIM_TRISTRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            case PDDI_PRIM_LINES: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case PDDI_PRIM_LINESTRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case PDDI_PRIM_POINTS: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }
    }
    std::chrono::steady_clock::time_point started;
    VulkanGammaControl gamma;
    VulkanMemoryRegistration memoryRegistration;
    VulkanHardwareSkinning hardwareSkinning;
    VulkanImmediateStream immediate;
    pddiPrimType immediateType=PDDI_PRIM_TRIANGLES;
    VulkanShader* immediateShader=nullptr;
    pddiMatrix projection;
    unsigned submittedDraws=0;
    bool drawPathLogged=false;
    bool diagnosticsLogged=false;
    bool shadowReady[3]={false,false,false};
    bool shadowStableCentreValid=false;
    rmt::Vector shadowStableCentre;
    bool shadowCascadeCentreValid[3]={false,false,false};
    rmt::Vector shadowCascadeCentre[3];
    bool shadowLightDirectionValid=false;
    rmt::Vector shadowLightDirection;
    pddiMatrix shadowWorldToClip[3],shadowReceiverMatrix[3],shadowSavedProjection;
    float shadowReceiverMatrices[48]={};
    int shadowCurrentCascade=-1;
};

class VulkanDevice final : public pddiDevice
{
public:
    VulkanDevice() { std::memset(&displayInfo, 0, sizeof(displayInfo)); }
    void GetLibraryInfo(pddiLibInfo* info) override
    {
        info->versionMajor = PDDI_VERSION_MAJOR; info->versionMinor = PDDI_VERSION_MINOR;
        info->versionBuild = 1; info->libID = PDDI_LIBID_STUB;
        std::strcpy(info->description, "Pure3D Vulkan PDDI");
    }
    int GetDisplayInfo(pddiDisplayInfo** info) override
    {
        if (info) *info = &displayInfo;
        return 1;
    }
    void SetCurrentContext(pddiRenderContext* value) override { context = value; }
    pddiRenderContext* GetCurrentContext() override { return context; }
    pddiDisplay* NewDisplay(int) override { return new VulkanDisplay(); }
    pddiRenderContext* NewRenderContext(pddiDisplay* display) override { return new VulkanContext(this, display); }
    pddiTexture* NewTexture(pddiTextureDesc* desc) override { return new VulkanTexture(desc); }
    pddiPrimBuffer* NewPrimBuffer(pddiPrimBufferDesc* desc) override
    {
        ++gCreatedPrimBuffers;
        return new VulkanPrimBuffer(desc);
    }
    pddiShader* NewShader(const char* name, const char*) override { return new VulkanShader(name); }
    void AddCustomShader(const char*, const char*) override {}
    void Release() override {}
private:
    pddiRenderContext* context = nullptr;
    pddiDisplayInfo displayInfo;
};

VulkanDevice device;
}

int pddiCreate(int major, int minor, pddiDevice** output)
{
    if (major != PDDI_VERSION_MAJOR || minor != PDDI_VERSION_MINOR)
    {
        *output = nullptr;
        return PDDI_VERSION_ERROR;
    }
    *output = &device;
    return PDDI_OK;
}

bool pddiVulkanCopyTexturePixels(pddiTexture* texture,
                                 std::vector<unsigned char>* pixels,
                                 unsigned* width,unsigned* height)
{
    VulkanTexture* vulkanTexture=dynamic_cast<VulkanTexture*>(texture);
    return vulkanTexture && vulkanTexture->CopyLevelZeroPixels(pixels,width,height);
}

// Compatibility entry points used by game-side enhancements that were added
// directly to the GLES PDDI. Vulkan implementations are introduced alongside
// their corresponding render passes; until then the passes stay disabled.
int gPglCsmBillboardMode = 0;
void pglSetTextureSourceName(pddiTexture* texture, const char* name)
{
    if(texture) static_cast<VulkanTexture*>(texture)->SetSourceName(name);
}
void pglSetEnhancedMaterialMode(int mode) { gVulkanEnhancedMaterialMode = mode; }
int pglGetEnhancedMaterialMode() { return gVulkanEnhancedMaterialMode; }
void pglSetParticleRendering(bool) {}
void pglSetEnhancedSunDirection(float x, float y, float z)
{
    const float length=std::sqrt(x*x+y*y+z*z);
    if(length<=0.0001f) return;
    gVulkanEnhancedSunDirection[0]=x/length;
    gVulkanEnhancedSunDirection[1]=y/length;
    gVulkanEnhancedSunDirection[2]=z/length;
}
void pglSetVehicleDeformation(const float*, int) {}
void pglSuppressVehicleRearLights(bool suppress)
{
    gVulkanVehicleRearLightsSuppressed=suppress;
}
void pglSetVehicleRearLights(int mode, int count, const float* positions,
                             const float* directions, const float* colour)
{
    gVulkanVehicleRearLightMode=std::max(0,std::min(2,mode));
    gVulkanVehicleRearLightCount=std::max(0,std::min(4,count));
    for(int i=0;i<4;++i)
    {
        for(int component=0;component<3;++component)
        {
            const int source=i*3+component;
            gVulkanVehicleRearLightPositions[i][component]=positions?positions[source]:0.0f;
            gVulkanVehicleRearLightDirections[i][component]=directions?directions[source]:0.0f;
        }
        gVulkanVehicleRearLightPositions[i][3]=0.0f;
        gVulkanVehicleRearLightDirections[i][3]=0.0f;
    }
    for(int i=0;i<3;++i) gVulkanVehicleRearLightColour[i]=colour?colour[i]:0.0f;
}

bool VrHasDynamicVehicleCubeMap()
{ return SharOpenXR::GetVulkanContext().HasDynamicVehicleCubeMap(); }
void VrBindDynamicVehicleCubeMap() {}
bool VrIsDynamicVehicleCubeMapCapture()
{
    SharOpenXR::VulkanEyeTarget target={};
    return SharOpenXR::GetVulkanContext().GetVehicleCubeMapTarget(&target);
}
void VrSetVehicleCubeMapTransparentSuppression(bool) {}
void VrRestoreVehicleCubeMapRendering(pddiRenderContext*) {}
bool VrBeginVehicleCubeMapFace(pddiRenderContext*, int face)
{ return SharOpenXR::GetVulkanContext().BeginVehicleCubeMapFace(face); }
void VrEndVehicleCubeMapFace(pddiRenderContext*, int face)
{ SharOpenXR::GetVulkanContext().EndVehicleCubeMapFace(face); }

bool VrBeginSunShadowMap(pddiRenderContext* context, int cascade,const rmt::Matrix& eye,
                         rmt::Matrix* worldToLight,rmt::Matrix* lightToWorld)
{ return static_cast<VulkanContext*>(context)->BeginSunShadowMap(cascade,eye,worldToLight,lightToWorld); }
void VrEndSunShadowMap(pddiRenderContext* context,int cascade,const rmt::Matrix& eye)
{ static_cast<VulkanContext*>(context)->EndSunShadowMap(cascade,eye); }
void VrEnableSunShadowReceivers(pddiRenderContext* context,bool enable)
{ static_cast<VulkanContext*>(context)->EnableSunShadowReceivers(enable); }
void VrBeginSunShadowOverlay(pddiRenderContext*) {}
void VrEndSunShadowOverlay(pddiRenderContext*) {}

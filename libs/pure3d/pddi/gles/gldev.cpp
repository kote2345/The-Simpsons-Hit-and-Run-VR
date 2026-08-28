//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/gldev.hpp>
#include <pddi/gles/gldisplay.hpp>
#include <pddi/gles/glcon.hpp>
#include <pddi/gles/gltex.hpp>
#include <pddi/gles/glmat.hpp>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
// #include <io.h>
#include <pddi/base/debug.hpp>
#include <SDL.h>

#define PDDI_GL_BUILD 36

static pglDevice gblDevice;

char libName [] = "OpenGL";

int pddiCreate(int versionMajor, int versionMinor, pddiDevice** device)
{
    if((versionMajor != PDDI_VERSION_MAJOR) || (versionMinor != PDDI_VERSION_MINOR))
    {
        *device = NULL;
        return PDDI_VERSION_ERROR;
    }

    *device = &gblDevice;
    return PDDI_OK;
}

//--------------------------------------------------------------
pglDevice::pglDevice() 
{
    nDisplays = 0;
    displayInfo = NULL;
}

//--------------------------------------------------------------
pglDevice::~pglDevice()
{
    for( int i = 0; i < nDisplays; i++ )
    {
        delete[] displayInfo[i].modeInfo;
    }
    delete[] displayInfo;
    displayInfo = NULL;
}

//--------------------------------------------------------------
void pglDevice::GetLibraryInfo(pddiLibInfo* info)
{
    info->versionMajor = PDDI_VERSION_MAJOR;
    info->versionMinor = PDDI_VERSION_MINOR;
    info->versionBuild = PDDI_GL_BUILD;
    info->libID = PDDI_LIBID_OPENGL;
    strcpy( info->description, libName );
}

unsigned pglDevice::GetCaps()
{
    return 0;
}

int pglDevice::GetDisplayInfo(pddiDisplayInfo** info)
{
    *info = displayInfo;

    if (displayInfo)
    {
        return nDisplays;
    }

#if SDL_MAJOR_VERSION < 3
    int totalDisplay = SDL_GetNumVideoDisplays();
#else
    int totalDisplay;
    SDL_DisplayID* displayIds = SDL_GetDisplays(&totalDisplay);
#endif
    displayInfo = new pddiDisplayInfo[totalDisplay];

    nDisplays = 0;
    for(int i = 0; i < totalDisplay; i++)
    {
#if SDL_MAJOR_VERSION < 3
        SDL_DisplayMode devMode;
        const char* displayName = SDL_GetDisplayName(i);
        int totalModes = SDL_GetNumDisplayModes(i);
        if (!displayName || totalModes <= 0)
            continue;
#else
        const char* displayName = SDL_GetDisplayName(displayIds[i]);
        int totalModes;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(displayIds[i], &totalModes);
        if (!displayName || !modes)
            continue;
#endif

        displayInfo[nDisplays].id = i;
        strcpy(displayInfo[0].description, displayName);
        displayInfo[nDisplays].pci = 0;
        displayInfo[nDisplays].vendor = 0;
        displayInfo[nDisplays].fullscreenOnly = false;
        displayInfo[nDisplays].caps = 0;

        int nModes = 0;
        pddiModeInfo* displayModes = new pddiModeInfo[totalModes];
#if SDL_MAJOR_VERSION < 3
        for(int j = 0; j < totalModes; j++)
        {
            if(SDL_GetDisplayMode(j, j, &devMode) == 0)
            {
                displayModes[nModes].width = devMode.w;
                displayModes[nModes].height = devMode.h;
                displayModes[nModes].bpp = 32;
                nModes++;
            }
        }
#else
        for(int j = 0; j < totalModes; j++)
        {
            displayModes[nModes].width = modes[j]->w;
            displayModes[nModes].height = modes[j]->h;
            displayModes[nModes].bpp = 32;
            nModes++;
        }
        SDL_free(modes);
#endif

        displayInfo[nDisplays].modeInfo = displayModes;
        displayInfo[nDisplays].nDisplayModes = nModes;
        nDisplays++;
    }
#if SDL_MAJOR_VERSION > 2
    SDL_free(displayIds);
#endif

    return nDisplays;
}

const char* pglDevice::GetDeviceDescription()
{
    return libName;
}

void pglDevice::SetCurrentContext(pddiRenderContext* c)
{
    context = c;
}

pddiRenderContext* pglDevice::GetCurrentContext(void)
{
    return context;
}

pddiDisplay *pglDevice::NewDisplay(int id)
{
    pddiDisplayInfo* dummy;
    GetDisplayInfo(&dummy);

    PDDIASSERT(id < nDisplays);
    pglDisplay* display = new pglDisplay(&displayInfo[id]);

    if(display->GetLastError() != PDDI_OK)
    {
        delete display;
        return NULL;
    }

    return (pddiDisplay *)display;
}
//--------------------------------------------------------------
pddiRenderContext *pglDevice::NewRenderContext(pddiDisplay* display)
{
    pglContext* context = new pglContext(this, (pglDisplay*)display);

    if(context->GetLastError() != PDDI_OK)
    {
        delete context;
        return NULL;
    }
    return context;
}

//--------------------------------------------------------------
pddiTexture* pglDevice::NewTexture(pddiTextureDesc* desc)
{
    pglTexture* tex = new pglTexture((pglContext*)context);
    if(!tex->Create(desc->GetSizeX(), desc->GetSizeY(), desc->GetBitDepth(), 
                     desc->GetAlphaDepth(), desc->GetMipMapCount(),desc->GetType(),desc->GetUsage()))
    {
        lastError = tex->GetLastError();
        delete tex;
        return NULL;
    }
    return tex;
}
//--------------------------------------------------------------
pddiShader *pglDevice::NewShader(const char* name, const char*) 
{ 
    const bool reflection = name &&
        (!strcmp(name, "environment") || !strcmp(name, "spheremap"));
    pglMat* mat= new pglMat((pglContext*)context, reflection);
    if(mat->GetLastError() != PDDI_OK) {
        delete mat;
        return NULL;
    }

    return mat;
}

pddiPrimBuffer *pglDevice::NewPrimBuffer(pddiPrimBufferDesc* desc) 
{ 
    return new pglPrimBuffer((pglContext*)context, desc->GetPrimType(), desc->GetVertexFormat(), desc->GetVertexCount(), desc->GetIndexCount());;
}

void pglDevice::AddCustomShader(const char* name, const char* aux)
{
}

void pglDevice::Release(void)
{
}


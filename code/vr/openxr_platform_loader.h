#ifndef SHAR_OPENXR_PLATFORM_LOADER_H
#define SHAR_OPENXR_PLATFORM_LOADER_H

namespace SharOpenXR
{
namespace Platform
{
    void* OpenLoader();
    void* GetLoaderSymbol(void* loader, const char* name);
    const char* GetLoaderError();
    void CloseLoader(void* loader);
}
}

#endif

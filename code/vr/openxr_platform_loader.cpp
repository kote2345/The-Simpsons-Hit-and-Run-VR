#include <vr/openxr_platform_loader.h>

#if defined(SRR2_OPENXR)

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#else
#include <dlfcn.h>
#endif

namespace SharOpenXR
{
namespace Platform
{
#if defined(_WIN32)
namespace
{
    char gLoaderError[160] = { 0 };
}

void* OpenLoader()
{
    HMODULE loader = LoadLibraryA("openxr_loader.dll");
    if(!loader)
    {
        std::snprintf(gLoaderError,sizeof(gLoaderError),
                      "LoadLibrary(openxr_loader.dll) failed (%lu)",
                      static_cast<unsigned long>(GetLastError()));
    }
    return reinterpret_cast<void*>(loader);
}

void* GetLoaderSymbol(void* loader, const char* name)
{
    if(!loader || !name) return 0;
    return reinterpret_cast<void*>(GetProcAddress(
        reinterpret_cast<HMODULE>(loader),name));
}

const char* GetLoaderError()
{
    return gLoaderError[0] ? gLoaderError : "unknown Windows loader error";
}

void CloseLoader(void* loader)
{
    if(loader) FreeLibrary(reinterpret_cast<HMODULE>(loader));
}
#else
void* OpenLoader()
{
#if defined(RAD_ANDROID)
    return dlopen("libopenxr_loader.so",RTLD_NOW|RTLD_LOCAL);
#else
    return dlopen("libopenxr_loader.so.1",RTLD_NOW|RTLD_LOCAL);
#endif
}

void* GetLoaderSymbol(void* loader, const char* name)
{
    return loader && name ? dlsym(loader,name) : 0;
}

const char* GetLoaderError()
{
    const char* error=dlerror();
    return error ? error : "unknown dynamic loader error";
}

void CloseLoader(void* loader)
{
    if(loader) dlclose(loader);
}
#endif
}
}

#endif

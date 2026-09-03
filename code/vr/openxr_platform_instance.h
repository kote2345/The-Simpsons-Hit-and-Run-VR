#ifndef SHAR_OPENXR_PLATFORM_INSTANCE_H
#define SHAR_OPENXR_PLATFORM_INSTANCE_H

#if defined(SRR2_OPENXR)
#include <openxr/openxr.h>
#include <vector>

namespace SharOpenXR
{
namespace Platform
{
    bool InitializeOpenXRLoader(PFN_xrGetInstanceProcAddr getProc);
    void AppendRequiredInstanceExtensions(std::vector<const char*>& extensions);
    bool PrepareInstanceCreateInfo(XrInstanceCreateInfo* createInfo);
}
}
#endif

#endif

#ifndef SHAR_OPENXR_DESKTOP_RUNTIME_H
#define SHAR_OPENXR_DESKTOP_RUNTIME_H
#if defined(SRR2_OPENXR_PLATFORM_WIN32)
namespace SharOpenXR { namespace Desktop {
bool InitializeRuntime();
void ShutdownRuntime();
bool IsRuntimeReady();
bool BeginFrame();
bool BeginEye(unsigned eye);
void EndEye(unsigned eye);
void EndFrame();
} }
#endif
#endif

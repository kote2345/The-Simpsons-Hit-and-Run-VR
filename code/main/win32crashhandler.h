#ifndef SRR2_WIN32_CRASH_HANDLER_H
#define SRR2_WIN32_CRASH_HANDLER_H

#if defined(SRR2_OPENXR_PLATFORM_WIN32)
void InstallWin32CrashHandler();
#else
inline void InstallWin32CrashHandler() {}
#endif

#endif

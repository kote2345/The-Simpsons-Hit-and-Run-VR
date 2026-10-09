#ifndef SRR2_PCVR_LOG_H
#define SRR2_PCVR_LOG_H

#if defined(SRR2_OPENXR_PLATFORM_WIN32)
#include <windows.h>
#include <cstdio>
#include <io.h>
#include <string>

// Resolve from the executable, rather than the shortcut's working directory.
// Redirect the console streams before SDL/OpenXR initialization, including
// native Windows standard handles used by libraries outside the CRT.
inline bool OpenPcvrLog()
{
    wchar_t executable[32768]={};
    const DWORD length=GetModuleFileNameW(NULL,executable,32768);
    if(!length||length>=32768)return false;
    std::wstring path(executable,length);
    const size_t slash=path.find_last_of(L"\\/");
    if(slash==std::wstring::npos)return false;
    path.resize(slash+1);
    path+=L"SimpsonsVR.log";

    FILE* redirected=NULL;
    if(_wfreopen_s(&redirected,path.c_str(),L"wb",stdout)!=0)
    {
        OutputDebugStringA("PCVR: cannot open SimpsonsVR.log next to the executable\n");
        return false;
    }
    setvbuf(stdout,NULL,_IONBF,0);
    // GUI executables may start with an invalid stderr descriptor. Reopen it
    // first, then duplicate stdout so both streams share the same file offset.
    // Use NUL only to obtain a valid descriptor; reopening the same log can
    // fail with the CRT's sharing mode. _dup2 immediately replaces NUL.
    if(_wfreopen_s(&redirected,L"NUL",L"wb",stderr)!=0||
       _dup2(_fileno(stdout),_fileno(stderr))!=0)return false;
    setvbuf(stderr,NULL,_IONBF,0);
    SetStdHandle(STD_OUTPUT_HANDLE,reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdout))));
    SetStdHandle(STD_ERROR_HANDLE,reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr))));

    SYSTEMTIME now={};GetLocalTime(&now);
    fprintf(stdout,"===== Simpsons Hit & Run PCVR log %04u-%02u-%02u %02u:%02u:%02u =====\n",
        now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);
    return true;
}
#endif
#endif

#include <main/win32crashhandler.h>

#if defined(SRR2_OPENXR_PLATFORM_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>

namespace
{
volatile LONG gCrashWritten=0;

void BuildCrashPath(wchar_t* path,size_t capacity,const wchar_t* extension)
{
    DWORD length=GetModuleFileNameW(NULL,path,static_cast<DWORD>(capacity));
    if(length==0 || length>=capacity) return;
    wchar_t* slash=wcsrchr(path,L'\\');
    if(slash) ++slash; else slash=path;

    SYSTEMTIME now={};
    GetLocalTime(&now);
    _snwprintf_s(slash,capacity-static_cast<size_t>(slash-path),_TRUNCATE,
        L"SRR2_crash_%04u%02u%02u_%02u%02u%02u.%s",
        now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,extension);
}

void WriteText(HANDLE file,const char* text)
{
    if(file==INVALID_HANDLE_VALUE || !text) return;
    DWORD written=0;
    WriteFile(file,text,static_cast<DWORD>(strlen(text)),&written,NULL);
}

void WriteCrashFiles(EXCEPTION_POINTERS* exceptionInfo)
{
    if(InterlockedCompareExchange(&gCrashWritten,1,0)!=0) return;

    wchar_t dumpPath[MAX_PATH]={};
    wchar_t logPath[MAX_PATH]={};
    BuildCrashPath(dumpPath,MAX_PATH,L"dmp");
    BuildCrashPath(logPath,MAX_PATH,L"log");

    HANDLE dump=CreateFileW(dumpPath,GENERIC_WRITE,FILE_SHARE_READ,NULL,
                            CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(dump!=INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION details={};
        details.ThreadId=GetCurrentThreadId();
        details.ExceptionPointers=exceptionInfo;
        details.ClientPointers=FALSE;
        MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),dump,
            static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs |
                                      MiniDumpWithHandleData |
                                      MiniDumpWithThreadInfo |
                                      MiniDumpWithUnloadedModules |
                                      MiniDumpWithIndirectlyReferencedMemory |
                                      MiniDumpScanMemory),
            exceptionInfo ? &details : NULL,NULL,NULL);
        CloseHandle(dump);
    }

    HANDLE log=CreateFileW(logPath,GENERIC_WRITE,FILE_SHARE_READ,NULL,
                           CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(log==INVALID_HANDLE_VALUE) return;

    char line[1024]={};
    const DWORD code=exceptionInfo && exceptionInfo->ExceptionRecord ?
        exceptionInfo->ExceptionRecord->ExceptionCode : 0;
    const void* address=exceptionInfo && exceptionInfo->ExceptionRecord ?
        exceptionInfo->ExceptionRecord->ExceptionAddress : NULL;
    HMODULE module=NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(address),&module);
    char modulePath[MAX_PATH]={};
    if(module) GetModuleFileNameA(module,modulePath,MAX_PATH);
    const ULONG_PTR offset=module && address ?
        reinterpret_cast<ULONG_PTR>(address)-reinterpret_cast<ULONG_PTR>(module) : 0;

    _snprintf_s(line,sizeof(line),_TRUNCATE,
        "SRR2 PCVR crash\r\nException: 0x%08lX\r\nAddress: %p\r\n"
        "Module: %s + 0x%llX\r\nProcess: %lu\r\nThread: %lu\r\n\r\nStack:\r\n",
        code,address,modulePath[0]?modulePath:"(unknown)",
        static_cast<unsigned long long>(offset),GetCurrentProcessId(),GetCurrentThreadId());
    WriteText(log,line);

    if(exceptionInfo && exceptionInfo->ContextRecord)
    {
        HANDLE process=GetCurrentProcess();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        if(SymInitialize(process,NULL,TRUE))
        {
            CONTEXT context=*exceptionInfo->ContextRecord;
            STACKFRAME64 frame={};
            frame.AddrPC.Offset=context.Rip;
            frame.AddrPC.Mode=AddrModeFlat;
            frame.AddrFrame.Offset=context.Rbp;
            frame.AddrFrame.Mode=AddrModeFlat;
            frame.AddrStack.Offset=context.Rsp;
            frame.AddrStack.Mode=AddrModeFlat;

            char symbolStorage[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]={};
            SYMBOL_INFO* symbol=reinterpret_cast<SYMBOL_INFO*>(symbolStorage);
            symbol->SizeOfStruct=sizeof(SYMBOL_INFO);
            symbol->MaxNameLen=MAX_SYM_NAME;
            for(unsigned index=0;index<64;++index)
            {
                if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,GetCurrentThread(),
                    &frame,&context,NULL,SymFunctionTableAccess64,SymGetModuleBase64,NULL) ||
                   frame.AddrPC.Offset==0) break;
                DWORD64 displacement=0;
                const BOOL named=SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol);
                _snprintf_s(line,sizeof(line),_TRUNCATE,"%02u  0x%016llX  %s%s0x%llX\r\n",
                    index,static_cast<unsigned long long>(frame.AddrPC.Offset),
                    named?symbol->Name:"",named?" + ":"",
                    static_cast<unsigned long long>(named?displacement:frame.AddrPC.Offset));
                WriteText(log,line);
            }
            SymCleanup(process);
        }
    }
    FlushFileBuffers(log);
    CloseHandle(log);
}

LONG WINAPI UnhandledCrashFilter(EXCEPTION_POINTERS* exceptionInfo)
{
    WriteCrashFiles(exceptionInfo);
    return EXCEPTION_EXECUTE_HANDLER;
}

LONG CALLBACK EarlyFatalExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
{
    if(!exceptionInfo || !exceptionInfo->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code=exceptionInfo->ExceptionRecord->ExceptionCode;
    if(code==STATUS_HEAP_CORRUPTION || code==STATUS_STACK_BUFFER_OVERRUN)
        WriteCrashFiles(exceptionInfo);
    return EXCEPTION_CONTINUE_SEARCH;
}
}

void InstallWin32CrashHandler()
{
    SetUnhandledExceptionFilter(UnhandledCrashFilter);
    AddVectoredExceptionHandler(1,EarlyFatalExceptionHandler);
}

#endif

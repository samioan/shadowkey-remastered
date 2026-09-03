#include "platform/win32/crash_report.h"

#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// dbghelp.h must follow windows.h.
#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")

namespace sk {
namespace {

const char* ExceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
        case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
        default: return "UNKNOWN";
    }
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* rec = info->ExceptionRecord;
    std::printf("\n=== shadowkey-port CRASH ===\n");
    std::printf("exception %s (0x%08lx) at %p\n", ExceptionName(rec->ExceptionCode),
                static_cast<unsigned long>(rec->ExceptionCode), rec->ExceptionAddress);
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
        // Parameter 0: 0 = read, 1 = write, 8 = DEP. Parameter 1: address.
        const char* how = rec->ExceptionInformation[0] == 1   ? "writing"
                          : rec->ExceptionInformation[0] == 8 ? "executing"
                                                              : "reading";
        std::printf("  %s address 0x%p\n", how,
                    reinterpret_cast<void*>(rec->ExceptionInformation[1]));
    }

    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    bool haveSymbols = SymInitialize(process, nullptr, TRUE) != FALSE;

    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame = {};
    DWORD machine;
#if defined(_M_X64)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_IX86)
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrStack.Offset = context.Esp;
#else
    machine = 0;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    std::printf("stack:\n");
    for (int depth = 0; machine != 0 && depth < 48; ++depth) {
        if (!StackWalk64(machine, process, GetCurrentThread(), &frame, &context, nullptr,
                          SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }
        DWORD64 pc = frame.AddrPC.Offset;
        if (pc == 0) break;

        char symbolBuffer[sizeof(SYMBOL_INFO) + 512] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 displacement = 0;
        const char* name = "(no symbol)";
        if (haveSymbols && SymFromAddr(process, pc, &displacement, symbol)) name = symbol->Name;

        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        if (haveSymbols && SymGetLineFromAddr64(process, pc, &lineDisplacement, &line)) {
            std::printf("  %2d  %s  (%s:%lu)\n", depth, name, line.FileName,
                        static_cast<unsigned long>(line.LineNumber));
        } else {
            // No line info: a module-relative offset is still resolvable
            // afterwards from the build's own map/pdb.
            DWORD64 moduleBase = SymGetModuleBase64(process, pc);
            std::printf("  %2d  %s  (+0x%llx)\n", depth, name,
                        static_cast<unsigned long long>(moduleBase ? pc - moduleBase : pc));
        }
    }
    std::printf("=== end crash report ===\n");
    std::fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;  // terminate, having said why
}

}  // namespace

void InstallCrashReporter() { SetUnhandledExceptionFilter(OnUnhandledException); }

}  // namespace sk

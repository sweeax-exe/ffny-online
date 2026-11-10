// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// abort(), std::terminate and an uncaught C++ exception: a [CRASH] report like an access violation's.
//
// The crash report (src/main.c's vectored handler) sees access violations and a few other faults. An
// uncaught C++ exception goes another way: Windows' unhandled-exception filter, the C runtime's
// terminate(), abort(), and a fail-fast (0xC0000409 in ucrtbase.dll), with nothing in the log. Build 52
// closed that way twice on 2026-11-04 (std::mutex locked twice in the chat's worker thread). Here:
//
//   - an unhandled-exception filter in front of the C runtime's: a C++ exception's type and what() go
//     to the report, then the runtime's filter runs as before (terminate, abort);
//   - SIGABRT (process-wide): abort() from anywhere else, e.g. the runtime's own checks;
//   - std::set_terminate on the main thread (the handler is per thread in the MSVC runtime; the filter
//     covers the others);
//   - _set_purecall_handler: a pure virtual call;
//   - _set_invalid_parameter_handler: a C runtime function given a bad argument (fclose(NULL),
//     localtime_s out of range) is logged and returns its error code instead of ending the game;
//   - _set_abort_behavior: no message box; Windows' own error report (and a local dump where that is
//     set up) stays, it is how this crash was found.
//
// The report itself is host_crash_report_abort in src/main.c (once per run).
#ifdef _WIN32

#include <windows.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exception>

extern "C" void host_crash_report_abort(const char *what);     // src/main.c

namespace {

LPTOP_LEVEL_EXCEPTION_FILTER g_previous_filter;

// The MSVC C++ exception's throw information (x64: image-relative offsets), enough to name the type and
// reach std::exception::what().
const DWORD CXX_EXCEPTION = 0xE06D7363;     // 'msc'
struct ThrowInfo { unsigned attributes; int unwind, forward_compat, catchable_types; };
struct CatchableTypes { int count; int types[1]; };
struct CatchableType { unsigned properties; int type; int mdisp, pdisp, vdisp; int size, copy; };
struct TypeDescriptor { const void *vftable; void *spare; char name[1]; };

// "uncaught C++ exception std::system_error: resource deadlock would occur"
void describe_cxx(const EXCEPTION_RECORD *er, char *out, size_t size)
{
    snprintf(out, size, "uncaught C++ exception");
    if (er->NumberParameters < 4) return;
    void *object = (void *)er->ExceptionInformation[1];
    const ThrowInfo *ti = (const ThrowInfo *)er->ExceptionInformation[2];
    const char *base = (const char *)er->ExceptionInformation[3];
    if (!ti || !base || !ti->catchable_types) return;
    const CatchableTypes *types = (const CatchableTypes *)(base + ti->catchable_types);
    const char *first = nullptr;
    const std::exception *ex = nullptr;
    for (int i = 0; i < types->count && i < 16; i++) {
        const CatchableType *ct = (const CatchableType *)(base + types->types[i]);
        const TypeDescriptor *td = ct->type ? (const TypeDescriptor *)(base + ct->type) : nullptr;
        if (!td) continue;
        if (!first) first = td->name;
        if (!strcmp(td->name, ".?AVexception@std@@") && object && ct->pdisp < 0)
            ex = (const std::exception *)((char *)object + ct->mdisp);
    }
    const char *what = nullptr;
    if (ex) {
        __try {
            what = ex->what();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            what = nullptr;
        }
    }
    snprintf(out, size, "uncaught C++ exception %s%s%s", first ? first : "(unknown type)", what ? ": " : "",
             what ? what : "");
}

LONG WINAPI unhandled_filter(EXCEPTION_POINTERS *ep)
{
    if (ep && ep->ExceptionRecord && ep->ExceptionRecord->ExceptionCode == CXX_EXCEPTION) {
        char what[256];
        describe_cxx(ep->ExceptionRecord, what, sizeof what);
        host_crash_report_abort(what);
    }
    return g_previous_filter ? g_previous_filter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

void on_terminate()
{
    char what[256] = "std::terminate";
    if (std::exception_ptr p = std::current_exception()) {
        try {
            std::rethrow_exception(p);
        } catch (const std::exception &e) {
            snprintf(what, sizeof what, "std::terminate: uncaught C++ exception: %s", e.what());
        } catch (...) {
            snprintf(what, sizeof what, "std::terminate: uncaught C++ exception (not a std::exception)");
        }
    }
    host_crash_report_abort(what);
    abort();
}

void on_sigabrt(int)
{
    host_crash_report_abort("abort()");
    // returning lets abort() go on: Windows' error report, then the process ends
}

void on_purecall()
{
    host_crash_report_abort("pure virtual function call");
    abort();
}

void on_invalid_parameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned, uintptr_t)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 10) {
        fprintf(stderr, "[CRT] a C runtime function was given an invalid argument (it returned an error; the game goes on)\n");
        fflush(stderr);
    }
}

}   // namespace

extern "C" void abort_report_install(void)
{
    g_previous_filter = SetUnhandledExceptionFilter(unhandled_filter);
    std::set_terminate(on_terminate);
    signal(SIGABRT, on_sigabrt);
    _set_purecall_handler(on_purecall);
    _set_invalid_parameter_handler(on_invalid_parameter);
    _set_abort_behavior(0, _WRITE_ABORT_MSG);
}

#endif  // _WIN32

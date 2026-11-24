/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* How a run of the game ended: the file beside the logs and its way to the account service (run_end.h). */
#include "run_end.h"

#if defined(_WIN32)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <windows.h>

#include "account.h"
#include "recomp_settings.h"

#define RUN_END_FILE "logs\\run-end.txt"

static HANDLE g_file = INVALID_HANDLE_VALUE;    /* the game's process: open for the run, so a crash only writes */
static volatile LONG g_quit;

static int allowed(void) { return recomp_settings_get("online", "run_end_reports", RUN_END_ASK) == RUN_END_ON; }

void run_end_setting_changed(void)
{
    if (allowed()) return;      /* on: from the next start of the game */
    if (g_file != INVALID_HANDLE_VALUE) CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
    DeleteFileA(RUN_END_FILE);
}

static void line(HANDLE h, const char *text)
{
    DWORD got;
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, text, (DWORD)strlen(text), &got, NULL);    /* one write a line, at the file's end (FILE_APPEND_DATA) */
}

void run_end_begin(int build)
{
    char text[96];
    fprintf(stderr, "[BUILD] build %d, compiled by cl %ld\n", build, RUN_END_CL);
    DeleteFileA(RUN_END_FILE);
    if (!allowed()) return;
    CreateDirectoryA("logs", NULL);
    g_file = CreateFileA(RUN_END_FILE, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    snprintf(text, sizeof text, "begin build=%d cl=%ld at=%lld\r\n", build, RUN_END_CL, (long long)time(NULL));
    line(g_file, text);
}

void run_end_note(const char *how)
{
    char text[32];
    snprintf(text, sizeof text, "%.12s\r\n", how);
    line(g_file, text);
}

void run_end_quit(void) { InterlockedExchange(&g_quit, 1); }
void run_end_closing(void) { run_end_note(InterlockedCompareExchange(&g_quit, 0, 0) ? "quit" : "closed"); }

void run_end_crash(struct _EXCEPTION_POINTERS *ep)
{
    static volatile LONG lines;
    char path[MAX_PATH], text[200], name[61] = "none";
    HMODULE self = GetModuleHandleW(NULL);
    uintptr_t rip = (uintptr_t)ep->ContextRecord->Rip, base = 0, from = 0, *sp = (uintptr_t *)ep->ContextRecord->Rsp;
    uintptr_t top = (uintptr_t)((NT_TIB *)NtCurrentTeb())->StackBase;
    MEMORY_BASIC_INFORMATION mi;
    if (g_file == INVALID_HANDLE_VALUE || InterlockedIncrement(&lines) > 8) return;
    if (VirtualQuery((void *)rip, &mi, sizeof mi) && mi.Type == MEM_IMAGE) {
        base = (uintptr_t)mi.AllocationBase;
        if (GetModuleFileNameA((HMODULE)base, path, sizeof path)) {
            const char *file = strrchr(path, '\\');
            char *c;
            snprintf(name, sizeof name, "%s", file ? file + 1 : path);      /* the file's name alone, never its folder */
            for (c = name; *c; c++) if (*c == ' ') *c = '_';                /* a blank would end the value */
        }
    }
    if (base != (uintptr_t)self && ((uintptr_t)sp & 7) == 0 && (uintptr_t)sp < top) {
        size_t i, n = (top - (uintptr_t)sp) / sizeof(*sp);
        if (n > 256) n = 256;
        for (i = 0; i < n && !from; i++)
            if (sp[i] >= (uintptr_t)self && sp[i] < (uintptr_t)self + 0x10000000u) from = sp[i] - (uintptr_t)self;
    }
    snprintf(text, sizeof text, "crash code=%08lX at=%s+%llX", ep->ExceptionRecord->ExceptionCode, name,
             (unsigned long long)(base ? rip - base : rip));
    if (from) snprintf(text + strlen(text), sizeof text - strlen(text), " from=%llX", (unsigned long long)from);
    snprintf(text + strlen(text), sizeof text - strlen(text), "\r\n");
    line(g_file, text);
}

void run_end_exit_code(unsigned long code)
{
    char text[64];
    HANDLE h = CreateFileA(RUN_END_FILE, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    snprintf(text, sizeof text, "exit code=%08lX at=%lld\r\n", code, (long long)time(NULL));
    line(h, text);
    CloseHandle(h);
}

#define SIGN_IN_WAIT_MS 30000       /* a saved sign-in is checked at the launcher's start */

static DWORD WINAPI send_thread(LPVOID param)
{
    char *json = (char *)param;
    ULONGLONG until = GetTickCount64() + SIGN_IN_WAIT_MS;
    int status = -1;
    while (account_state() == ACCOUNT_CHECKING && GetTickCount64() < until) Sleep(500);    /* the saved sign-in */
    if (account_state() == ACCOUNT_IN && allowed()) status = account_send_run_end(json);
    fprintf(stderr, "[RUN-END] the last run: %s: %s (%d)\n", json,
            status == 202 ? "sent" : status == -1 ? "not signed in or turned off, not sent" : "not taken", status);
    fflush(stderr);
    free(json);
    return 0;
}

void run_end_send(void)
{
    static volatile LONG once;
    char text[2048], json[400], *copy;
    DWORD got = 0;
    RunEnd r;
    HANDLE h, t;
    if (InterlockedExchange(&once, 1)) return;
    if (!allowed()) {           /* not asked yet, or off: what there is goes unsent */
        if (DeleteFileA(RUN_END_FILE)) fprintf(stderr, "[RUN-END] not allowed: the last run's file deleted unsent\n");
        return;
    }
    account_init();
    h = CreateFileA(RUN_END_FILE, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);       /* a game still running has it open for writing: not now */
    if (h == INVALID_HANDLE_VALUE) return;
    if (!ReadFile(h, text, sizeof text - 1, &got, NULL)) got = 0;
    text[got] = 0;
    CloseHandle(h);
    DeleteFileA(RUN_END_FILE);
    if (!run_end_parse(text, &r) || !run_end_json(&r, (long long)time(NULL), json, sizeof json)) return;
    copy = _strdup(json);
    t = copy ? CreateThread(NULL, 0, send_thread, copy, 0, NULL) : NULL;
    if (t) CloseHandle(t);
    else free(copy);
}

#else

void run_end_begin(int build) { (void)build; }
void run_end_setting_changed(void) {}
void run_end_note(const char *how) { (void)how; }
void run_end_quit(void) {}
void run_end_closing(void) {}
void run_end_exit_code(unsigned long code) { (void)code; }
void run_end_send(void) {}

#endif

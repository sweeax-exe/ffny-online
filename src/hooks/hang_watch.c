/*
 * The hang watch (build 58). A frozen game could not be reported: F9 takes its picture at the next
 * present, and a freeze has none (ranked set W34VFS: both players on the results picture, F9 dead,
 * the window closed by hand, nothing on the server). This host thread looks once a second whether the
 * game still moves: a guest frame, or megabytes read (a screen or venue that loads reads all the time and
 * has no frames; the relay's packets and the music stream go on reading through a freeze, but little).
 * After HANG_SECS without either it writes a [HANG] block to the log: the host
 * stack of every thread, the game thread's guest stack, the netplay and rollback state, and leaves a
 * "<log>.hang" mark that the next start offers as a report (bug_report_last_crash). If the game moves
 * again the mark goes and the watch is armed again. It never kills or changes anything.
 *
 * The threads are suspended one at a time only to copy their registers and walk the unwind data: a
 * suspended thread may hold the C runtime's locks, so nothing here prints or allocates meanwhile.
 *
 * RECOMP_TEST_HANG=<secs>[,<stall secs>] (tests): the game thread stalls at the first update <secs>
 * after the watch started, for <stall secs> (default: for good).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hang_watch.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <io.h>

#include "recomp/gen/recomp_types.h"     /* g_esp, MEM32, XBOX_PTR */
#include "nv2a/nv2a_pgraph_d3d11.h"         /* the guest's frames: the window itself is presented without them */

void netplay_hang_dump(void);   /* netplay.c: no locks taken */
void rollback_hang_dump(void);  /* rollback.c: no locks taken */

#define HANG_SECS        20u    /* no present and no load's reads */
#define HANG_LOAD_BYTES (4u * 1024u * 1024u)    /* bytes read in HANG_SECS that say a load is working (relay and music: far less) */
#define HANG_LOAD_SECS   30u    /* the same while the screen manager says a screen loads */
#define HANG_MAX_BLOCKS   3     /* in one run */
#define MAX_THREADS     160
#define MAX_FRAMES       20
#define GUEST_WORDS     256u

static volatile LONG s_updates;
static volatile DWORD s_game_tid;
static const volatile uint32_t *s_game_esp;     /* the game thread's own g_esp (thread-local) */
static double s_test_at = -1.0, s_test_for = -1.0;
static ULONGLONG s_started;
static char s_mark[MAX_PATH + 16];

void hang_watch_note_update(void)
{
    InterlockedIncrement(&s_updates);
    if (!s_game_tid) {
        s_game_esp = &g_esp;
        s_game_tid = GetCurrentThreadId();
    }
    if (s_test_at >= 0.0 && s_started && (double)(GetTickCount64() - s_started) / 1000.0 >= s_test_at) {
        s_test_at = -1.0;
        fprintf(stderr, "[HANG] tests: the game thread stalls here (RECOMP_TEST_HANG)\n");
        fflush(stderr);
        if (s_test_for < 0.0) for (;;) Sleep(1000);
        Sleep((DWORD)(s_test_for * 1000.0));
        fprintf(stderr, "[HANG] tests: the stall is over\n");
    }
}

typedef struct {
    DWORD tid;
    int captured, frames;
    uint64_t pc[MAX_FRAMES];
} ThreadShot;

/* Registers and unwind only: no printing, no allocation, while the thread is suspended. */
static void shoot_thread(ThreadShot *t)
{
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, t->tid);
    CONTEXT c;
    if (!h) return;
    if (SuspendThread(h) == (DWORD)-1) { CloseHandle(h); return; }
    memset(&c, 0, sizeof(c));
    c.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(h, &c)) {
        t->captured = 1;
#if defined(_M_X64) || defined(__x86_64__)
        while (t->frames < MAX_FRAMES && c.Rip) {
            DWORD64 base = 0;
            PRUNTIME_FUNCTION fn;
            void *handler = NULL;
            DWORD64 frame = 0;
            t->pc[t->frames++] = c.Rip;
            fn = RtlLookupFunctionEntry(c.Rip, &base, NULL);
            if (fn) RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, fn, &c, &handler, &frame, NULL);
            else {      /* a leaf: the return address is on top of the stack */
                if (IsBadReadPtr((const void *)c.Rsp, 8)) break;
                c.Rip = *(DWORD64 *)c.Rsp;
                c.Rsp += 8;
            }
        }
#endif
    }
    ResumeThread(h);
    CloseHandle(h);
}

static void print_pc(uint64_t pc)
{
    HMODULE m = NULL;
    char name[MAX_PATH] = "?";
    const char *base = name;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)pc, &m) && m) {
        GetModuleFileNameA(m, name, sizeof(name));
        if (strrchr(name, '\\')) base = strrchr(name, '\\') + 1;
        fprintf(stderr, " %s+%llX", base, (unsigned long long)(pc - (uint64_t)(uintptr_t)m));
    } else
        fprintf(stderr, " %llX", (unsigned long long)pc);
}

static void dump_threads(void)
{
    static ThreadShot shots[MAX_THREADS];
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;
    DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    int n = 0, i, f;
    memset(shots, 0, sizeof(shots));
    if (snap == INVALID_HANDLE_VALUE) return;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) do {
        if (te.th32OwnerProcessID == pid && te.th32ThreadID != self && n < MAX_THREADS)
            shots[n++].tid = te.th32ThreadID;
    } while (Thread32Next(snap, &te));
    CloseHandle(snap);
    /* the game thread first: the one that matters most */
    for (i = 0; i < n; i++)
        if (shots[i].tid == s_game_tid) { ThreadShot g = shots[i]; shots[i] = shots[0]; shots[0] = g; break; }
    for (i = 0; i < n; i++) shoot_thread(&shots[i]);
    for (i = 0; i < n; i++) {
        HMODULE self_m = GetModuleHandleA(NULL);
        int ours = 0;
        if (!shots[i].captured) continue;
        /* the game's own threads: the graphics driver's and the system's say nothing and fill the log */
        for (f = 0; f < shots[i].frames; f++) {
            HMODULE m = NULL;
            ours |= GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       (LPCSTR)(uintptr_t)shots[i].pc[f], &m) && m == self_m;
        }
        if (!ours) continue;
        fprintf(stderr, "  thread %lu%s:", shots[i].tid, shots[i].tid == s_game_tid ? " (game)" : "");
        for (f = 0; f < shots[i].frames; f++) print_pc(shots[i].pc[f]);
        fprintf(stderr, "\n");
    }
}

/* The return addresses on the game thread's guest stack: words that look like the game's code. */
static void dump_guest_stack(void)
{
    uint32_t esp = s_game_esp ? *s_game_esp : 0, i;
    int shown = 0;
    if (!esp) return;
    fprintf(stderr, "  guest esp %08X, code words:", esp);
    for (i = 0; i < GUEST_WORDS && shown < 32; i++) {
        uint32_t a = esp + i * 4u, v;
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery((const void *)XBOX_PTR(a), &mi, sizeof(mi)) || mi.State != MEM_COMMIT ||
            (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            break;
        v = MEM32(a);
        if (v >= 0x00011000u && v < 0x00240000u) {
            fprintf(stderr, " %08X", v);
            shown++;
        }
    }
    fprintf(stderr, "\n");
}

static int screen_loading(uint32_t *state)
{
    uint32_t sm;
    *state = 0;
    sm = MEM32(0x002FB148u + 0x17u * 4u);
    if (sm < 0x10000u) return 0;
    *state = MEM32(sm + 0x24u);
    return *state != 0;
}

static void write_mark(void)
{
    HANDLE h = (HANDLE)_get_osfhandle(_fileno(stderr));
    char path[MAX_PATH + 8];
    DWORD n;
    HANDLE m;
    s_mark[0] = 0;
    if (h == INVALID_HANDLE_VALUE || GetFileType(h) != FILE_TYPE_DISK) return;
    n = GetFinalPathNameByHandleA(h, path, sizeof(path), FILE_NAME_NORMALIZED);
    if (!n || n >= sizeof(path)) return;
    snprintf(s_mark, sizeof(s_mark), "%s.hang", !strncmp(path, "\\\\?\\", 4) ? path + 4 : path);
    m = CreateFileA(s_mark, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m != INVALID_HANDLE_VALUE) CloseHandle(m);
    else s_mark[0] = 0;
}

static DWORD WINAPI hang_thread(LPVOID unused)
{
    LONG presents = 0, updates = 0;
    ULONGLONG reads = 0, last = GetTickCount64(), tick = last;
    ULONGLONG read_at[HANG_SECS];
    unsigned at = 0;
    int armed = 0, hung = 0, blocks = 0;
    memset(read_at, 0, sizeof(read_at));
    (void)unused;
    for (;;) {
        IO_COUNTERS io;
        ULONGLONG now;
        LONG p, u;
        PgraphD3D11Stats st;
        int moved;
        Sleep(1000);
        now = GetTickCount64();
        /* the PC slept, or this thread had no time: that is not the game's hang */
        if (now - tick > 5000u) last = now;
        tick = now;
        pgraph_d3d11_get_stats(&st);
        p = (LONG)st.frames;
        u = s_updates;
        memset(&io, 0, sizeof(io));
        GetProcessIoCounters(GetCurrentProcess(), &io);
        /* the reads of the last HANG_SECS seconds */
        reads = io.ReadTransferCount;
        moved = p != presents || (at >= HANG_SECS && reads - read_at[at % HANG_SECS] >= HANG_LOAD_BYTES);
        read_at[at % HANG_SECS] = reads;
        at++;
        if (p != presents) armed = 1;       /* from the first picture on: never during the start-up */
        presents = p;
        if (moved) {
            if (hung) {
                fprintf(stderr, "[HANG] over: the game moves again after %.0f s\n", (double)(now - last) / 1000.0);
                fflush(stderr);
                if (s_mark[0]) DeleteFileA(s_mark);
                s_mark[0] = 0;
                hung = 0;
            }
            last = now;
            continue;
        }
        if (armed && !hung && blocks < HANG_MAX_BLOCKS) {
            uint32_t state;
            int loading = screen_loading(&state);
            if (now - last >= (loading ? HANG_LOAD_SECS : HANG_SECS) * 1000u) {
                hung = 1;
                blocks++;
                write_mark();
                fprintf(stderr, "[HANG] no present and no load's reads for %.0f s; game updates in the last second: %ld "
                        "(screen manager state %u)\n", (double)(now - last) / 1000.0, (long)(u - updates), state);
                dump_threads();
                dump_guest_stack();
                netplay_hang_dump();
                rollback_hang_dump();
                fprintf(stderr, "[HANG] end of the block; the game is left running\n");
                fflush(stderr);
            }
        }
        updates = u;
    }
    return 0;
}

void hang_watch_start(void)
{
    const char *t = getenv("RECOMP_TEST_HANG");
    HANDLE h;
    if (t && *t) {
        const char *comma = strchr(t, ',');
        s_test_at = atof(t);
        if (comma) s_test_for = atof(comma + 1);
    }
    s_started = GetTickCount64();
    h = CreateThread(NULL, 0, hang_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
}

#else

void hang_watch_start(void) {}
void hang_watch_note_update(void) {}

#endif

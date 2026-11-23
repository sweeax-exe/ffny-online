/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/*
 * The hang watch (build 58). A frozen game could not be reported: F9 takes its picture at the next
 * present, and a freeze has none (ranked set W34VFS: both players on the results picture, F9 dead,
 * the window closed by hand, nothing on the server). This host thread looks once a second whether the
 * game still moves: a guest frame, or megabytes read (a screen or venue that loads reads all the time and
 * has no frames; the relay's packets and the music stream go on reading through a freeze, but little).
 * After HANG_SECS without either it writes a [HANG] block to the log: the host
 * stack of every thread (with the lifted function each frame is in), the guest view of the title's main
 * thread and of the game thread (registers, the guest functions on the call chain, a stack scan like the
 * [CRASH] block's), the last indirect-call targets, the netplay and rollback state, and leaves a
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
#include "run_end.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <io.h>

#include <dbghelp.h>                     /* SymFromAddr: main.c loads the symbols at the start */

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

/* A guest thread's registers are thread-local; another thread reads them through the addresses
 * the thread itself took. The title's main thread is known from the start (hang_watch_start runs on
 * it), the game thread from its first update: a boot that hangs before the first update has none. */
enum { GR_EAX, GR_ECX, GR_EDX, GR_EBX, GR_ESP, GR_EBP, GR_ESI, GR_EDI, GR_N };
typedef struct {
    DWORD tid;
    const volatile uint32_t *reg[GR_N];
    /* taken while the thread is suspended */
    int taken;
    uint32_t val[GR_N];
    uint32_t words, stack[GUEST_WORDS];
} GuestThread;
static GuestThread s_main, s_game;
static const char *const s_reg_names[GR_N] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };

static void guest_thread_here(GuestThread *g)
{
    g->reg[GR_EAX] = &g_eax; g->reg[GR_ECX] = &g_ecx; g->reg[GR_EDX] = &g_edx; g->reg[GR_EBX] = &g_ebx;
    g->reg[GR_ESP] = &g_esp; g->reg[GR_EBP] = &g_ebp; g->reg[GR_ESI] = &g_esi; g->reg[GR_EDI] = &g_edi;
    g->tid = GetCurrentThreadId();
}
static double s_test_at = -1.0, s_test_for = -1.0;
static ULONGLONG s_started;
static char s_mark[MAX_PATH + 16];

void hang_watch_note_update(void)
{
    InterlockedIncrement(&s_updates);
    if (!s_game_tid) {
        guest_thread_here(&s_game);
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

/* The guest registers and the words above the guest esp, while the thread is suspended. */
static void take_guest(GuestThread *g)
{
    uint32_t i, sp;
    g->taken = 0;
    g->words = 0;
    if (!g->tid || !g->reg[GR_ESP]) return;
    for (i = 0; i < GR_N; i++) g->val[i] = *g->reg[i];
    g->taken = 1;
    sp = g->val[GR_ESP];
    if (!sp) return;
    for (i = 0; i < GUEST_WORDS; i++) {
        uint32_t a = sp + i * 4u;
        MEMORY_BASIC_INFORMATION mi;
        if ((i == 0 || (a & 0xFFFu) == 0) &&
            (!VirtualQuery((const void *)XBOX_PTR(a), &mi, sizeof(mi)) || mi.State != MEM_COMMIT ||
             (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))))
            break;
        g->stack[i] = MEM32(a);
        g->words = i + 1;
    }
}

/* Registers and unwind only: no printing, no allocation, while the thread is suspended. */
static void shoot_thread(ThreadShot *t)
{
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, t->tid);
    CONTEXT c;
    if (!h) return;
    if (SuspendThread(h) == (DWORD)-1) { CloseHandle(h); return; }
    memset(&c, 0, sizeof(c));
    c.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(h, &c)) {      /* returns once the thread has really stopped */
        t->captured = 1;
        if (t->tid == s_main.tid) take_guest(&s_main);
        if (t->tid == s_game.tid && s_game.tid != s_main.tid) take_guest(&s_game);
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

/* The function a host address is in, from the exe's PDB (the setup builds it beside the exe, so this
 * works on a player's PC, where the bare offsets cannot be mapped: every player compiles their own). */
static int pc_symbol(uint64_t pc, char *out, size_t len)
{
    char buf[sizeof(SYMBOL_INFO) + 128];
    SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
    DWORD64 disp = 0;
    memset(buf, 0, sizeof(buf));
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 127;
    if (!SymFromAddr(GetCurrentProcess(), (DWORD64)pc, &disp, sym)) return 0;
    snprintf(out, len, "%s+0x%llX", sym->Name, (unsigned long long)disp);
    return 1;
}

static void print_pc(uint64_t pc)
{
    HMODULE m = NULL;
    char name[MAX_PATH] = "?";
    const char *base = name;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)pc, &m) && m) {
        char sym[160];
        GetModuleFileNameA(m, name, sizeof(name));
        if (strrchr(name, '\\')) base = strrchr(name, '\\') + 1;
        fprintf(stderr, " %s+%llX", base, (unsigned long long)(pc - (uint64_t)(uintptr_t)m));
        if (m == GetModuleHandleA(NULL) && pc_symbol(pc, sym, sizeof(sym)))
            fprintf(stderr, "(%s)", sym);
    } else
        fprintf(stderr, " %llX", (unsigned long long)pc);
}

static ThreadShot s_main_shot, s_game_shot;    /* the host frames dump_threads took */

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
    /* the title's main thread and the game thread first: the ones that matter most */
    for (i = 0; i < n; i++)
        if (shots[i].tid == s_main.tid) { ThreadShot g = shots[i]; shots[i] = shots[0]; shots[0] = g; break; }
    for (i = 1; i < n; i++)
        if (s_game_tid && s_game_tid != s_main.tid && shots[i].tid == s_game_tid) {
            ThreadShot g = shots[i]; shots[i] = shots[1]; shots[1] = g; break;
        }
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
        fprintf(stderr, "  thread %lu%s%s:", shots[i].tid, shots[i].tid == s_main.tid ? " (main)" : "",
                shots[i].tid == s_game_tid ? " (game)" : "");
        for (f = 0; f < shots[i].frames; f++) print_pc(shots[i].pc[f]);
        fprintf(stderr, "\n");
        if (shots[i].tid == s_main.tid) s_main_shot = shots[i];
        if (shots[i].tid == s_game_tid) s_game_shot = shots[i];
    }
}

/* One guest thread as the [CRASH] block shows the faulting one. The lifted code keeps no guest EIP:
 * the guest function the thread is in comes from its host frames (each lifted function is a symbol,
 * sub_XXXXXXXX), innermost first, which is a walk and not a scan; the stack scan below it lists every
 * word in the code range, stale return addresses included (scripts/guest-stack.py --log keeps the
 * real call sites). */
static void dump_guest_thread(const char *what, const GuestThread *g, const ThreadShot *t)
{
    uint32_t i;
    int shown = 0, f;
    if (!g->taken) {
        if (g->tid) fprintf(stderr, "  %s guest thread %lu: not captured\n", what, g->tid);
        return;
    }
    fprintf(stderr, "  %s guest thread %lu:", what, g->tid);
    for (i = 0; i < GR_N; i++) fprintf(stderr, " %s=%08X", s_reg_names[i], g->val[i]);
    fprintf(stderr, "\n");
    if (t && t->tid == g->tid) {
        fprintf(stderr, "    guest functions (host frames, innermost first):");
        for (f = 0; f < t->frames && shown < 12; f++) {
            char sym[160];
            if (pc_symbol(t->pc[f], sym, sizeof(sym)) && !strncmp(sym, "sub_", 4)) {
                fprintf(stderr, " %s", sym);
                shown++;
            }
        }
        fprintf(stderr, shown ? "\n" : " none (in the runtime or a system call)\n");
    }
    if (g->words)
        fprintf(stderr, "    [esp] 0x%08X (the return address of its last call, when it is in one)\n", g->stack[0]);
    fprintf(stderr, "    guest stack (scan, not a walk -- includes stale frames):\n");
    for (i = 0, shown = 0; i < g->words && shown < 24; i++) {
        uint32_t v = g->stack[i];
        if (v > g_xbox_code_lo && v < g_xbox_code_hi) {
            fprintf(stderr, "    [esp+%-4u] 0x%08X\n", i * 4u, v);
            shown++;
        }
    }
}

static void dump_guest(void)
{
    uint32_t k;
    dump_guest_thread("main", &s_main, &s_main_shot);
    if (s_game.tid && s_game.tid != s_main.tid)
        dump_guest_thread("game", &s_game, &s_game_shot);
    /* every thread's, newest last: a spin through the kernel thunks or a vtable shows here */
    fprintf(stderr, "  recent indirect-call targets (all threads, oldest first):");
    for (k = 0; k < ICALL_TRACE_SIZE; k++) {
        uint32_t v = g_icall_trace[(g_icall_trace_idx + k) & (ICALL_TRACE_SIZE - 1)];
        if (v) fprintf(stderr, " %08X", v);
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
                run_end_note("moved");
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
                run_end_note("hung");
                fprintf(stderr, "[HANG] no present and no load's reads for %.0f s; game updates in the last second: %ld "
                        "(screen manager state %u)\n", (double)(now - last) / 1000.0, (long)(u - updates), state);
                dump_threads();
                dump_guest();
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
    guest_thread_here(&s_main);         /* main.c calls this on the thread that enters the title */
    h = CreateThread(NULL, 0, hang_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
}

#else

void hang_watch_start(void) {}
void hang_watch_note_update(void) {}

#endif

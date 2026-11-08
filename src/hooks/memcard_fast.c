/* Faster save loading: the memory-card driver's steps run several to a frame while it only reads.
 *
 * How the game reads saves (measured, build 66, RECOMP_MEMCARD_TRACE=1):
 *   - MemCard.Update (sub_000D0C20) runs once per frame from the main loop and calls the driver's tick
 *     sub_00197370 (a thunk to sub_0019A170; this = MEM32(MEM32(0x39FBF0)), the "impl"). The tick takes
 *     at most ONE response from the driver's worker thread (mailbox MEM32(0x3B8090)), hands it to the
 *     handler for the driver state (impl+0x6C), and that handler posts the next request
 *     (MEM32(0x2EB73C)). After every handled response the tick sleeps 1 ms on the game's thread sleep
 *     (sub_000D1270 through impl+4 -> vtable +0x18, return addresses 0x0019A1C7..0x0019A257).
 *   - The worker thread (sub_0019AB60) polls: when the mailbox is empty and a request is pending it
 *     does the file IO synchronously and fills the mailbox; then it sleeps 5 ms the same way
 *     (return address 0x0019ACA5).
 *   - On Windows at the default 15.6 ms timer resolution each of those sleeps takes a whole timer
 *     tick, and the tick handles one response a frame: one step per frame, about 17 frames (SaveMeta,
 *     open, 76-byte header, 64 bytes, body, close) per profile; 20 user IDs took 348-379 frames,
 *     6.1-6.6 s. Nothing in the chain counts frames or measures time.
 *
 * What this does, only while the front end runs a load (op MEM32(0x39FD00) 1 = a user-ID list from
 * MemCard.ChooseMCDevice, 6 = the boot load) and the driver is in a read state:
 *   1. those two sleeps last what they ask for (1 ms, 5 ms) on the performance counter instead of a
 *      timer tick, and end early once the other side has answered (the reason they are there);
 *   2. after the frame's own tick, the tick runs again for each response that is ready, within a
 *      small time budget per frame (RECOMP_MEMCARD_FAST_US, default 4000 us).
 * The game reads exactly what it did before, in the same order, through its own code; only more of
 * its steps fit in a frame. Saves, deletes and every op not listed run at the game's own pace (and
 * the driver's write state 3 and the write request types are excluded on their own as well).
 *
 * Switch: RECOMP_MEMCARD_FAST=0 turns it off (default on, on Windows; elsewhere off unless =1).
 * RECOMP_MEMCARD_TRACE=1 logs each step.
 * Either way one "[MCFAST] load" line per load gives its steps, frames and time. */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "host.h"
#include "recomp/gen/recomp_types.h"
#include "hooks/memcard_fast.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static uint64_t now_us(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart * 1000000.0 / (double)f.QuadPart);
}
static void yield_cpu(void) { SwitchToThread(); }
#else
#include <time.h>
#include <sched.h>
static uint64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}
static void yield_cpu(void) { sched_yield(); }
#endif

#define MC_DRIVER_PTR   0x0039FBF0u   /* the driver's wrapper object; the impl is MEM32(wrapper) */
#define MC_FE_STATE     0x0039FD00u   /* front-end op: 1 user-ID list, 6 boot load, 2 save, ... */
#define MC_SLOT         0x003B8090u   /* worker -> tick response, 0 = none */
#define MC_REQUEST      0x002EB73Cu   /* tick -> worker request, 0 = none */
#define MC_WORKER_STOP  0x003B808Cu
#define MC_WORKER_SLEEP_RET 0x0019ACA5u          /* sub_0019AB60's sleep(5) */
#define MC_TICK_SLEEP_LO    0x0019A1B5u          /* sub_0019A170's sleep(1) after each handler */
#define MC_TICK_SLEEP_HI    0x0019A260u

static int g_on = -1, g_trace;
static uint32_t g_budget_us = 4000;

int memcard_fast_on(void)
{
    if (g_on < 0) {
        const char *e = getenv("RECOMP_MEMCARD_FAST");
        const char *b = getenv("RECOMP_MEMCARD_FAST_US");
        const char *t = getenv("RECOMP_MEMCARD_TRACE");
        g_trace = t && *t && strcmp(t, "0");
        if (b && atoi(b) > 0) g_budget_us = (uint32_t)atoi(b);
#ifdef _WIN32
        g_on = !(e && (!strcmp(e, "0") || !strcmp(e, "off")));
#else
        /* Measured on Windows only. Elsewhere the guest threads take turns, and a yield loop that
         * keeps its turn would hold the other side back, so it stays opt-in (=1) until tested. */
        g_on = e && (!strcmp(e, "1") || !strcmp(e, "on"));
#endif
        fprintf(stderr, "[MCFAST] %s (budget %u us a frame)%s\n", g_on ? "on" : "off",
                (unsigned)g_budget_us, g_trace ? ", trace" : "");
    }
    return g_on;
}

static uint32_t rd32(uint32_t va) { return *(volatile uint32_t *)XBOX_PTR(va); }

static uint32_t impl_now(void)
{
    uint32_t w = rd32(MC_DRIVER_PTR);
    return w ? rd32(w) : 0;
}

static int request_type(void)
{
    uint32_t r = rd32(MC_REQUEST);
    return r ? (int)rd32(r + 8) : 0;
}

/* Which work is sped up, from traces of the boot load, the user-ID lists, a profile load and a save:
 *   front-end op 6 (boot load) and 1 (user-ID list) only;
 *   driver state 1 mount/enumerate, 6 list, 4 file read, 7 close (3 is the save's write state);
 *   no pending request of the types the save used (6, 23, 31). */
static int pumpable(uint32_t impl)
{
    uint32_t st, fe = rd32(MC_FE_STATE);
    int type;
    if (!impl || (fe != 1u && fe != 6u)) return 0;
    st = rd32(impl + 0x6C);
    if (st != 1u && st != 4u && st != 6u && st != 7u) return 0;
    type = request_type();
    return type != 6 && type != 23 && type != 31;
}

static void trace_step(const char *what, uint32_t impl, uint32_t result)
{
    static volatile LONG printed;
    if (!g_trace || InterlockedIncrement(&printed) > 4000) return;
    fprintf(stderr, "[MCFAST] %s t=%llu r=%u drv=%u fe=%u type=%d slot=%08X q=%u/%u\n", what,
            (unsigned long long)(now_us() / 1000u), result, impl ? rd32(impl + 0x6C) : 0,
            rd32(MC_FE_STATE), request_type(), rd32(MC_SLOT), MEM8(0x0039FCF9u), MEM8(0x0039FCFAu));
}

/* One line per front-end load, on or off, so the two can be compared:
 * [MCFAST] load op=1 steps=.. frames=.. ms=.. (steps = driver responses handled; frames and ms from
 * the first frame with work to the last one). */
static struct { uint64_t t0, t_last; uint32_t op; int frames, frames_last, steps, idle; } g_load;

static void load_account(uint64_t t_begin, int steps)
{
    uint32_t fe = rd32(MC_FE_STATE), impl = impl_now();
    int active = (fe == 1u || fe == 6u) && impl && rd32(impl + 0x6C) != 0;
    if (active && !g_load.t0) {
        memset(&g_load, 0, sizeof g_load);
        g_load.t0 = t_begin;
        g_load.op = fe;
    }
    if (!g_load.t0) return;
    g_load.frames++;
    g_load.steps += steps;
    if (active || steps) {
        g_load.idle = 0;
        g_load.t_last = now_us();
        g_load.frames_last = g_load.frames;
    } else if (++g_load.idle >= 3 || (fe != 1u && fe != 6u)) {
        fprintf(stderr, "[MCFAST] load op=%u steps=%d frames=%d ms=%.1f %s\n", (unsigned)g_load.op,
                g_load.steps, g_load.frames_last, (double)(g_load.t_last - g_load.t0) / 1000.0,
                g_on > 0 ? "fast" : "off");
        if (g_trace) {      /* what the load left in the 16 profile slots, to compare on and off */
            uint32_t gs = rd32(0x2FB148u + 4u * 0x18u), base = gs ? rd32(gs + 4u) : 0, i, j, h;
            fprintf(stderr, "[MCFAST] slots");
            for (i = 0; base && i < 16; i++) {
                const uint8_t *p = (const uint8_t *)XBOX_PTR(base + 0x177Cu + i * 0x5950u);
                for (h = 2166136261u, j = 0; j < 0x5910u; j++) h = (h ^ p[j]) * 16777619u;
                fprintf(stderr, " %08X", h);
            }
            fputc(10, stderr);
        }
        g_load.t0 = 0;
    }
}

/* Called from the sub_00197370 wrapper with the guest stack as the caller left it:
 * [esp] return address, [esp+4] dt (ms); this = ecx = MEM32(0x39FBF0); returns al, `ret 4`. */
void memcard_fast_tick(void (*tick)(void))
{
    uint32_t esp0 = g_esp, ecx0 = g_ecx, ret = MEM32(esp0);
    uint32_t impl = ecx0 ? rd32(ecx0) : 0, eax, any;
    uint64_t start = now_us(), deadline;
    int steps;

    tick();
    eax = g_eax;
    any = eax & 0xFFu;
    steps = any ? 1 : 0;
    if (any) trace_step("tick", impl, any);
    if (memcard_fast_on() && pumpable(impl)) {
        deadline = start + g_budget_us;
        for (;;) {
            while (!rd32(MC_SLOT) && now_us() < deadline && pumpable(impl) && !MEM8(MC_WORKER_STOP))
                yield_cpu();
            if (!rd32(MC_SLOT) || !pumpable(impl) || now_us() >= deadline) break;
            g_esp = esp0;
            MEM32(esp0) = ret;
            MEM32(esp0 + 4) = 0;      /* the frame's time already went to the first tick */
            g_ecx = ecx0;
            tick();
            eax = g_eax;
            if (eax & 0xFFu) steps++;
            any |= eax & 0xFFu;
            trace_step("extra", impl, eax & 0xFFu);
        }
        g_esp = esp0 + 8;             /* ret 4 */
        g_eax = (eax & ~0xFFu) | (any ? 1u : 0u);
    }
    load_account(start, steps);
}

/* The game's thread sleep (sub_000D1270: argument in ms, `ret 4`), for the two memory-card callers
 * above while a load is pumped: the wait the game asked for, measured on the performance counter,
 * ending early once the other side has answered. Every other caller sleeps as before. */
void memcard_fast_sleep(void (*sleep_fn)(void))
{
    uint32_t ms = MEM32(g_esp + 4), ret = MEM32(g_esp);
    int worker = ret == MC_WORKER_SLEEP_RET;
    int ticker = ret >= MC_TICK_SLEEP_LO && ret < MC_TICK_SLEEP_HI;
    uint64_t deadline;

    if ((!worker && !ticker) || ms > 50u || !memcard_fast_on() || !pumpable(impl_now())) {
        sleep_fn();
        return;
    }
    deadline = now_us() + (uint64_t)ms * 1000u;
    while (now_us() < deadline && !MEM8(MC_WORKER_STOP)) {
        if (worker ? (rd32(MC_SLOT) == 0 && rd32(MC_REQUEST) != 0) : rd32(MC_SLOT) != 0) break;
        if (!pumpable(impl_now())) break;
        yield_cpu();
    }
    g_esp += 8;                       /* ret 4 */
}

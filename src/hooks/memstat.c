/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* RECOMP_MEMSTAT=<seconds>: a [MEMSTAT] line every so often with how full the game's memory is.
 *
 * The game takes one block from the contiguous window at start (MmQueryStatistics' free pages less
 * 14 MB, sub_0001A890) and runs its own pool allocator over it (docs/research/rollback-state.md 1.3,
 * 1.4): everything it loads -- venue models and textures, fighters, the crowd, animation, the HUD --
 * is a chunk of that block. Pools are registered at 0x3C9158[64]; a pool's first chunk is at
 * pool+8 and each chunk's next at chunk+8 (sub_001EA4A0, the game's own pool check). A chunk starts
 * with a 16-bit tag: 'BM' (0x424D) in use, 'BF' (0x4246) free, 'BS' (0x4253) the pool's own
 * sentinel (sub_001EA310). The span to the next chunk is the chunk's size, header included.
 *
 * Read from a host thread while the game allocates, so a walk can see a list mid-change: every
 * pointer is checked to stay inside the pool block and to move forward, and a walk that fails is
 * reported as such rather than counted. Diagnostics only; reads guest memory, writes nothing.
 *
 * Per line: the root pool (the largest) used/free/largest free block and the low-water marks of free
 * and largest free since start; the other pools' free space (sub-heaps carved from the root are
 * "used" in the root); the contiguous window in use, its peak and high-water; and the heap. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#include "recomp/gen/recomp_types.h"
#include "guest_window.h"

#define POOL_TABLE 0x003C9158u
#define POOLS      64
#define TAG_USED   0x424Du
#define TAG_FREE   0x4246u
#define TAG_SENT   0x4253u

extern uint32_t xbox_ContiguousInUseBytes(void);
extern uint32_t xbox_ContiguousAllocatedBytes(void);
extern uint32_t xbox_ContiguousLiveBytes(void);
extern uint32_t xbox_HeapLiveBytes(void);
extern uint32_t xbox_ContiguousBlockSize(uint32_t va);
extern size_t g_xbox_total_ram;

typedef struct {
    uint32_t pool, lo, hi;       /* the pool and the bounds its chunks must keep to */
    uint32_t used, free, largest, chunks, used_chunks;
    int ok;
} PoolWalk;

static int in_window(uint32_t va)
{
    return (va >= GUEST_WINDOW_LO && va < GUEST_WINDOW_HI) || (va >= 0x00010000u && va < 0x04000000u);
}

/* The contiguous block a pool lives in, so chunk pointers can be bounded: the game's block (the largest
 * block of the window, found once, as rollback.c's find_heap does) when the pool is in it, else the
 * pool's own block from the pool on. */
static uint32_t s_game_lo, s_game_hi;
static void find_game_block(void)
{
    uint32_t va = 0x80001000u, best = 0, best_size = 0, s;
    if (s_game_lo) return;
    while (va >= GUEST_WINDOW_LO && va < GUEST_WINDOW_HI) {
        s = xbox_ContiguousBlockSize(va);
        if (!s) { va += 0x1000u; if (va >= GUEST_WINDOW_HI) break; continue; }
        if (s > best_size) { best = va; best_size = s; }
        va += (s + 0xFFFu) & ~0xFFFu;
    }
    if (best_size >= 16u * 1024u * 1024u) { s_game_lo = best; s_game_hi = best + best_size; }
}

static void block_of(uint32_t pool, uint32_t *lo, uint32_t *hi)
{
    uint32_t n;
    find_game_block();
    if (s_game_lo && pool >= s_game_lo && pool < s_game_hi) { *lo = s_game_lo; *hi = s_game_hi; return; }
    n = xbox_ContiguousBlockSize(pool);
    *lo = n ? pool : 0;
    *hi = n ? pool + n : 0;
}

static void walk(PoolWalk *w)
{
    uint32_t c = MEM32(w->pool + 8u), next, span;
    unsigned guard = 0;
    w->used = w->free = w->largest = w->chunks = w->used_chunks = 0;
    w->ok = 0;
    while (c) {
        uint16_t tag;
        if (c < w->lo || c >= w->hi || ++guard > 400000u) return;
        tag = (uint16_t)(MEM32(c) & 0xFFFFu);
        next = MEM32(c + 8u);
        if (next && (next <= c || next > w->hi)) return;
        span = next ? next - c : 0;
        w->chunks++;
        if (tag == TAG_FREE) {
            w->free += span;
            if (span > w->largest) w->largest = span;
        } else if (tag == TAG_USED) {
            w->used += span;
            w->used_chunks++;
        } else if (tag != TAG_SENT) {
            return;
        }
        c = next;
    }
    w->ok = 1;
}

static uint32_t s_low_free = 0xFFFFFFFFu, s_low_largest = 0xFFFFFFFFu;

static void sample(double t)
{
    PoolWalk root = {0}, w;
    uint32_t others_free = 0, others_size = 0;
    int k, n = 0, bad = 0;
    uint32_t best_span = 0;

    for (k = 0; k < POOLS; k++) {
        uint32_t p = MEM32(POOL_TABLE + 4u * k), lo, hi;
        if (!p || !in_window(p)) continue;
        block_of(p, &lo, &hi);
        if (!lo) continue;
        memset(&w, 0, sizeof w);
        w.pool = p; w.lo = lo; w.hi = hi;
        walk(&w);
        n++;
        if (!w.ok) { bad++; continue; }
        if (w.used + w.free > best_span) {
            if (root.pool) { others_free += root.free; others_size += root.used + root.free; }
            root = w;
            best_span = w.used + w.free;
        } else {
            others_free += w.free;
            others_size += w.used + w.free;
        }
    }
    if (root.ok) {
        if (root.free < s_low_free) s_low_free = root.free;
        if (root.largest < s_low_largest) s_low_largest = root.largest;
    }
    fprintf(stderr, "[MEMSTAT] t=%.1f root pool %08X: %u KB, used %u KB, free %u KB (largest %u KB; low %u / %u KB),"
            " chunks %u | %d other pools %u KB, free %u KB%s | contig in use %u KB, peak %u KB | heap %u KB | ram %u MB\n",
            t, root.pool, (root.used + root.free) / 1024u, root.used / 1024u, root.free / 1024u,
            root.largest / 1024u, s_low_free == 0xFFFFFFFFu ? 0 : s_low_free / 1024u,
            s_low_largest == 0xFFFFFFFFu ? 0 : s_low_largest / 1024u, root.chunks,
            n - 1 - bad > 0 ? n - 1 - bad : 0, others_size / 1024u, others_free / 1024u,
            bad ? " (a walk saw a list mid-change)" : "",
            xbox_ContiguousInUseBytes() / 1024u, xbox_ContiguousAllocatedBytes() / 1024u,
            xbox_HeapLiveBytes() / 1024u, (unsigned)(g_xbox_total_ram >> 20));
    fflush(stderr);
}

#if defined(_WIN32)
static DWORD WINAPI memstat_thread(LPVOID arg)
{
    DWORD ms = (DWORD)(uintptr_t)arg, t0 = GetTickCount();
    for (;;) {
        Sleep(ms);
        sample((GetTickCount() - t0) / 1000.0);
    }
    return 0;
}
#endif

void memstat_start(void)
{
#if defined(_WIN32)
    const char *e = getenv("RECOMP_MEMSTAT");
    double s = e ? atof(e) : 0.0;
    HANDLE h;
    if (s <= 0.0) return;
    if (s < 0.2) s = 0.2;
    h = CreateThread(NULL, 0, memstat_thread, (LPVOID)(uintptr_t)(DWORD)(s * 1000.0), 0, NULL);
    if (h) CloseHandle(h);
#endif
}

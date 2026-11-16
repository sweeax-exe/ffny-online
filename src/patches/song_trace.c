/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Music playlist trace (diagnostic, off unless RECOMP_SONG_TRACE is set). Once a frame on the game's
 * thread (sub_00077040_enter) it logs the music playlist when it changes (the names at 0x33E798, 16 bytes
 * each, the count at 0x33E9DC), the song the playlist machine moves to (index 0x33EC94), and with a new
 * playlist the profile's song store (record + 0x34: keys, count, one state byte per key: bits 0-1 on/off,
 * bits 2-7 the Beatbox order). RECOMP_SONG_TRACE_SKIP=S ends the playing song every S seconds the way
 * a song's end does (timer 0x33E790 past the length 0x33EC40, state 0x340398 = 2), so a run sees many
 * track changes. Reads and these test writes only; nothing else of the game is touched. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "recomp/gen/recomp_types.h"
#include "song_trace.h"

#define PL_NAMES 0x33E798u
#define PL_COUNT 0x33E9DCu
#define PL_INDEX 0x33EC94u

static void name_at(uint32_t i, char out[17])
{
    for (int k = 0; k < 16; k++) out[k] = (char)MEM8(PL_NAMES + 16 * i + k);
    out[16] = 0;
}

static void dump_store(void)
{
    static int dumps;
    uint32_t mgr = MEM32(0x2FB148u + 0x18u * 4u), record, store, keys, states, count, i;
    if (++dumps > 4) return;   /* the first few lists only */
    if (!mgr) { fprintf(stderr, "[MUSIC] trace: no profile manager yet\n"); return; }
    record = MEM32(mgr + 4);
    if (!record) { fprintf(stderr, "[MUSIC] trace: no profile record yet\n"); return; }
    store = record + 0x34;   /* the song store (vtable 0x286204), as sub_0003DA40 passes it */
    keys = MEM32(store + 4);
    count = MEM32(store + 8);
    states = MEM32(store + 0xC);
    if (!keys || !states || count > 64) {
        fprintf(stderr, "[MUSIC] trace: no song store (record %08X keys %08X states %08X count %u)\n", record, keys,
                states, count);
        return;
    }
    fprintf(stderr, "[MUSIC] trace: song store %u key(s), order counter %u, mode %s, jukebox %s\n", count,
            MEM32(store + 0x18) ? MEM32(MEM32(store + 0x18)) : 0xFFFFFFFFu,
            MEM8(record + 0x14DC) ? "shuffled" : "ordered", MEM8(record + 0x14DD) ? "on" : "off");
    for (i = 0; i < count; i++) {
        uint32_t k = MEM32(keys + 4 * i);
        uint8_t s = MEM8(states + i);
        fprintf(stderr, "[MUSIC] trace:   slot %2u key %08X rank %3u state %u order %2u\n", i, k, k & 0xFFFFu,
                s & 3u, s >> 2);
    }
}

/* RECOMP_SONG_TRACE_REBUILD=N (test aid, a disposable save only): N times, two seconds apart, at the main
 * menu: every locked song is unlocked as the game's own unlock does (sub_00016C30: state 1, the next unlock
 * order; again each time, as a profile load brings the saved states back), then the menus' playlist is rebuilt
 * and started from its first song as the front end does when its music starts (sub_0003DA40, then
 * sub_0003CF70(1, 0, 0) as sub_00049250 does). Each rebuild logs the song that starts the list: how often
 * each song gets to play first. */
extern void sub_0003DA40(void);
extern void sub_0003CF70(void);

static void guest_call(void (*fn)(void), int nargs, uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx;
    if (nargs > 2) PUSH32(g_esp, a2);
    if (nargs > 1) PUSH32(g_esp, a1);
    if (nargs > 0) PUSH32(g_esp, a0);
    PUSH32(g_esp, 0);       /* the return address the callee pops */
    fn();
    g_esp += 4u * (uint32_t)nargs;  /* cdecl: the caller pops the arguments */
    g_eax = eax;
    g_ecx = ecx;
    g_edx = edx;
}

static int unlock_songs(void)
{
    uint32_t mgr = MEM32(0x2FB148u + 0x18u * 4u), record, store, states, count, counter, i, n = 0;
    if (!mgr || !(record = MEM32(mgr + 4))) return -1;
    store = record + 0x34;   /* the song store (vtable 0x286204), as sub_0003DA40 passes it */
    count = MEM32(store + 8);
    states = MEM32(store + 0xC);
    counter = MEM32(store + 0x18);
    if (!states || !counter || count > 64) return -1;
    for (i = 0; i < count; i++)
        if (!MEM8(states + i)) {
            MEM8(states + i) = (uint8_t)(1u | MEM32(counter) << 2);
            MEM32(counter) = MEM32(counter) + 1;
            n++;
        }
    if (n)
        fprintf(stderr, "[MUSIC] trace: test unlock: %u locked song(s) unlocked, %u in all, mode %s\n", n, count,
            MEM8(record + 0x14DC) ? "shuffled" : "ordered");
    return (int)n;
}

static void rebuild_test(void)
{
    static int left = -1;
    static clock_t last;
    char first[17];
    if (left < 0) {
        const char *e = getenv("RECOMP_SONG_TRACE_REBUILD");
        left = e ? atoi(e) : 0;
        last = clock();
    }
    if (left <= 0 || !MEM32(PL_COUNT) || (double)(clock() - last) / CLOCKS_PER_SEC < 2.0)
        return;
    last = clock();
    if (unlock_songs() < 0)     /* each time: a profile load brings the saved states back */
        return;
    guest_call(sub_0003DA40, 0, 0, 0, 0);
    guest_call(sub_0003CF70, 3, 1, 0, 0);
    left--;
    name_at(0, first);
    fprintf(stderr, "[MUSIC] trace: rebuild: %u songs, first %s\n", MEM32(PL_COUNT), first);
}

void song_trace_tick(void)
{
    static int on = -1;
    static double skip_s;
    static clock_t last_skip;
    static char seen[36][17];
    static uint32_t seen_count = 0xFFFFFFFFu, seen_index = 0xFFFFFFFFu;
    uint32_t count, index, i;
    int changed = 0;
    if (on < 0) {
        const char *s = getenv("RECOMP_SONG_TRACE_SKIP");
        on = getenv("RECOMP_SONG_TRACE") != NULL;
        skip_s = s ? atof(s) : 0.0;
        last_skip = clock();
    }
    if (!on) return;
    rebuild_test();
    count = MEM32(PL_COUNT);
    if (count > 36) return;
    for (i = 0; i < count; i++) {
        char n[17];
        name_at(i, n);
        if (strcmp(n, seen[i])) { changed = 1; strcpy(seen[i], n); }
    }
    if (changed || count != seen_count) {
        seen_count = count;
        fprintf(stderr, "[MUSIC] trace: playlist %u:", count);
        for (i = 0; i < count; i++) fprintf(stderr, " %s", seen[i]);
        fprintf(stderr, "\n");
        dump_store();
        seen_index = 0xFFFFFFFFu;
    }
    index = MEM32(PL_INDEX);
    if (count && index != seen_index && MEM8(0x33E794)) {
        seen_index = index;
        fprintf(stderr, "[MUSIC] trace: plays #%u %s\n", index, index < count ? seen[index] : "?");
    }
    if (skip_s > 0 && count && MEM8(0x33E794) && MEM32(0x340398) == 1 &&
        (double)(clock() - last_skip) / CLOCKS_PER_SEC >= skip_s) {
        uint32_t loading = MEM32(0x3407B4);
        if (loading != 1 && loading != 2) {
            last_skip = clock();
            MEMF(0x33E790) = MEMF(0x33EC40) + 1.0f;
            MEM32(0x340398) = 2;
        }
    }
}

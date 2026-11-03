/*
 * Songs in the profile's jukebox store (docs/research/custom-songs.md).
 *
 * The jukebox and the playlist use one item store per profile manager B (B + 0x30, vtable
 * 0x286204, "ItemMgr"):
 *   +4 keys (sorted u32 record handles), +8 count, +0xC states (one byte per key: bits 0-1
 *   state, 2 disabled / 3 enabled / 0 locked; bits 2-7 jukebox order), +0x10 sorted flag.
 * Its buffers are fixed in B: states at B + 0x1420 (36 bytes), keys at B + 0x1444 (36 words).
 * sub_00013340 fills the keys with one handle per Song row; loading a profile (sub_00014540,
 * this = B) then copies the saved block over B + 0x4C .. B + 0x14DC, keys and states
 * included, but leaves the count. A song added to the database after the profile was saved
 * is then missing from the store: the playlist skips it, and the jukebox's state calls index
 * states[-1] for it (a byte outside the array).
 *
 * After the load, every handle that was built but is not in the store goes into a slot past
 * the saved ones: enabled, last in jukebox order. The saved block has room for 36, and the
 * retail game reads only its own count, so the extra entry is invisible to it.
 */
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp/gen/recomp_types.h"
#include "song_store.h"

#define STORE(b)       ((b) + 0x30u)
#define STORE_CAP      36u

static uint32_t s_built[STORE_CAP], s_built_count, s_manager, s_last_manager;

void song_store_before_load(uint32_t manager)
{
    uint32_t store = STORE(manager), count = MEM32(store + 8), keys = MEM32(store + 4);
    s_manager = 0;
    if (!keys || count > STORE_CAP) return;
    for (uint32_t i = 0; i < count; i++) s_built[i] = MEM32(keys + i * 4);
    s_built_count = count;
    s_manager = manager;
}

/*
 * Test aid: RECOMP_TEST_SONGS_ONLY=<rank> disables every other song of the loaded profile in
 * memory (the save is not written unless the player changes the jukebox), so the playlist the
 * game builds next holds that song alone and the menu's song banner announces it at start-up.
 * The rank is the record handle's low bits (0-31 retail; the restored songs follow).
 */
static void song_store_test_only(uint32_t keys, uint32_t states, uint32_t count)
{
    const char *only = getenv("RECOMP_TEST_SONGS_ONLY");
    if (!only || !*only) return;
    uint32_t want = (uint32_t)strtoul(only, NULL, 0);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t s = MEM8(states + i);
        if ((MEM32(keys + i * 4) & 0xFFFFFFu) == want) MEM8(states + i) = (uint8_t)((s & 0xFC) | 3);
        else if (s & 3) MEM8(states + i) = (uint8_t)((s & 0xFC) | 2);
        else MEM8(states + i) = (uint8_t)((63u << 2) | 2);
    }
    fprintf(stderr, "[SONGS] test: only song %u enabled\n", want);
}

void song_store_after_load(uint32_t manager)
{
    uint32_t store = STORE(manager), count = MEM32(store + 8);
    uint32_t keys = MEM32(store + 4), states = MEM32(store + 0xC);
    uint32_t used[STORE_CAP], missing[STORE_CAP], nused = 0, nmissing = 0, order = 0;
    if (manager != s_manager || !keys || !states || count != s_built_count || count > STORE_CAP) return;
    s_manager = 0;
    s_last_manager = manager;
    if (getenv("RECOMP_SONGS_TRACE")) {
        for (uint32_t i = 0; i < STORE_CAP; i++)
            fprintf(stderr, "[SONGS] slot %2u built %08X loaded %08X state %02X\n", i,
                    i < count ? s_built[i] : 0, MEM32(manager + 0x1444 + i * 4), MEM8(manager + 0x1420 + i));
        fprintf(stderr, "[SONGS] keys at %08X (B+0x1444 = %08X), states at %08X, count %u\n", keys,
                manager + 0x1444, states, count);
    }

    song_store_test_only(keys, states, count);

    /* Which loaded slots hold a built handle; which built handles no slot holds. */
    for (uint32_t b = 0; b < count; b++) {
        int found = 0;
        for (uint32_t i = 0; i < count && !found; i++) found = MEM32(keys + i * 4) == s_built[b];
        if (!found) missing[nmissing++] = s_built[b];
    }
    if (!nmissing) return;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t k = MEM32(keys + i * 4);
        int built = 0;
        for (uint32_t b = 0; b < count && !built; b++) built = k == s_built[b];
        if (built) {
            used[nused++] = i;
            uint32_t o = MEM8(states + i) >> 2;
            if ((MEM8(states + i) & 3) && o + 1 > order) order = o + 1;
        }
    }
    /*
     * The jukebox order of an added song comes from the store's order counter (+0x18 points
     * at it, B + 0x14D4, saved with the profile), as the game's own "append" does: right after
     * the load the game numbers the songs it has never arranged (state 0) from that counter, so
     * an order taken from the highest one in use would collide with theirs, and two songs at
     * one position make the jukebox drop one and repeat another.
     */
    uint32_t counter_at = MEM32(store + 0x18);
    if (counter_at) {
        uint32_t c = MEM32(counter_at);
        if (c < 64 && c > order) order = c;
    }
    /* Slots that hold no built handle (unused when the profile was saved) take the missing ones. */
    uint32_t m = 0;
    for (uint32_t i = 0; i < count && m < nmissing; i++) {
        int is_used = 0;
        for (uint32_t u = 0; u < nused && !is_used; u++) is_used = used[u] == i;
        if (is_used) continue;
        MEM32(keys + i * 4) = missing[m++];
        MEM8(states + i) = (uint8_t)(((order < 63 ? order : 63) << 2) | 3);
        if (order < 63) order++;
    }
    if (counter_at) MEM32(counter_at) = order;
    if (getenv("RECOMP_SONGS_TRACE"))
        fprintf(stderr, "[SONGS] order counter at %08X now %u\n", counter_at, counter_at ? MEM32(counter_at) : 0);
    /* Keys stay sorted (lookups are binary searches), each with its state byte. */
    for (uint32_t i = 1; i < count; i++)
        for (uint32_t j = i; j > 0 && MEM32(keys + (j - 1) * 4) > MEM32(keys + j * 4); j--) {
            uint32_t k = MEM32(keys + j * 4);
            uint8_t s = MEM8(states + j);
            MEM32(keys + j * 4) = MEM32(keys + (j - 1) * 4);
            MEM8(states + j) = MEM8(states + j - 1);
            MEM32(keys + (j - 1) * 4) = k;
            MEM8(states + j - 1) = s;
        }
    fprintf(stderr, "[SONGS] profile loaded: %u song(s) added to its jukebox (enabled, last in order)\n", m);
}

/* After the game builds its music playlist (sub_0003CFF0): with RECOMP_SONGS_TRACE, list it.
 * 36 names of 16 bytes at 0x33E798, the count at 0x33E9DC. */
void song_store_playlist_built(void)
{
    static int trace = -1;
    if (trace < 0) trace = getenv("RECOMP_SONGS_TRACE") != NULL;
    if (!trace) return;
    uint32_t count = MEM32(0x33E9DC);
    fprintf(stderr, "[SONGS] playlist built: %u song(s):", count);
    for (uint32_t i = 0; i < count && i < STORE_CAP; i++) {
        char name[17];
        for (int c = 0; c < 16; c++) name[c] = (char)MEM8(0x33E798 + i * 16 + c);
        name[16] = 0;
        fprintf(stderr, " %s", name);
    }
    fprintf(stderr, "\n");
    if (s_last_manager) {
        uint32_t st = STORE(s_last_manager), keys = MEM32(st + 4), states = MEM32(st + 0xC), n = MEM32(st + 8);
        fprintf(stderr, "[SONGS] store:");
        for (uint32_t i = 0; i < n && i < STORE_CAP; i++) fprintf(stderr, " %X=%02X", MEM32(keys + i * 4), MEM8(states + i));
        fprintf(stderr, "\n[SONGS] jukebox list 0x3A07B0:");
        for (uint32_t i = 0; i < 40; i++) fprintf(stderr, " %X", MEM32(0x3A07B0 + i * 4));
        fprintf(stderr, "\n");
    }
}

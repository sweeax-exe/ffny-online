/* SPDX-License-Identifier: GPL-3.0-or-later */
/* An even shuffle for the game's shuffled music playlist.
 *
 * sub_00020D70 (the Beatbox's "shuffled" playlist: the menus and every fight's list after the match's own
 * song) collects the songs that are on in Song-table order and places each at slot rand() % n; when that slot
 * is taken it uses the LOWEST free slot instead of probing on from the collision. The first collisions come
 * after a handful of songs, so slot 0 (and the slots after it) go to songs from the first half of the table
 * almost every time. With 32 songs on, the song in Song-table place 16 ("Get It Now", Bless) starts the list
 * about 1 time in 700, places 20 and later practically never (simulation and docs/research/custom-songs.md),
 * and the menus restart the list at its first song each time they start the music. Songs late in the table
 * (Get It Now, Afterhours, Walk With Me, Anything Goes, Dragon House, Mother Mother...) therefore almost never
 * play unless the other songs are turned off. A retail bug; the original game behaves the same.
 *
 * Fix: after the game's builder has made its list (its own allocations, its own n draws from the game's
 * generator, so nothing the game or an online match reads changes), the list's name pointers are reordered by
 * a Fisher-Yates shuffle on a host generator. Every song is then equally likely at every place.
 * RECOMP_SONG_SHUFFLE=retail keeps the game's order (tests).
 */
#include "song_shuffle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t splitmix64(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* An unbiased draw in [0, bound) (bound >= 1). */
static uint32_t below(uint64_t *rng, uint32_t bound)
{
    uint64_t limit = UINT64_MAX - UINT64_MAX % bound, r;
    do r = splitmix64(rng); while (r >= limit);
    return (uint32_t)(r % bound);
}

void song_shuffle_permute(uint32_t *items, uint32_t n, uint64_t *rng)
{
    uint32_t i;
    for (i = n; i > 1; i--) {
        uint32_t j = below(rng, i), t = items[i - 1];
        items[i - 1] = items[j];
        items[j] = t;
    }
}

#ifndef SONG_SHUFFLE_NO_GUEST
#include "recomp/gen/recomp_types.h"

#define PLAYLIST_MAX 36u    /* the builder's own array; the music playlist holds 36 */

void song_shuffle_after_build(uint32_t names, uint32_t count_ptr)
{
    static int mode = -1;
    static uint64_t rng;
    uint32_t items[PLAYLIST_MAX], n, i;
    if (mode < 0) {
        const char *e = getenv("RECOMP_SONG_SHUFFLE");
        struct timespec ts;
        mode = !(e && !strcmp(e, "retail"));
        timespec_get(&ts, TIME_UTC);
        rng = (uint64_t)ts.tv_sec * 1000000007ull ^ (uint64_t)ts.tv_nsec ^ (uint64_t)clock() << 32 ^
              (uint64_t)(uintptr_t)&rng;
        fprintf(stderr, "[MUSIC] shuffled playlists: %s\n",
                mode ? "every song equally likely at every place" : "the game's own order (RECOMP_SONG_SHUFFLE=retail)");
    }
    if (!mode || !names || !count_ptr)
        return;
    n = MEM32(count_ptr);
    if (n < 2 || n > PLAYLIST_MAX)
        return;
    for (i = 0; i < n; i++) {
        items[i] = MEM32(names + 4 * i);
        if (!items[i])      /* not the list the builder makes: leave it */
            return;
    }
    song_shuffle_permute(items, n, &rng);
    for (i = 0; i < n; i++)
        MEM32(names + 4 * i) = items[i];
}
#endif

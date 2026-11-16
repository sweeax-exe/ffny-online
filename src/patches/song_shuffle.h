/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SONG_SHUFFLE_H
#define SONG_SHUFFLE_H
#include <stdint.h>

/* An even shuffle of the game's shuffled music playlist (src/patches/song_shuffle.c). */

/* Fisher-Yates over items[0..n-1] with the generator state *rng (splitmix64). Host only, no guest state. */
void song_shuffle_permute(uint32_t *items, uint32_t n, uint64_t *rng);

/* After sub_00020D70 (the shuffled playlist builder) has filled names[0..*count-1] (guest pointers to the
 * song ids, a NULL after the last): reorder them evenly. Off with RECOMP_SONG_SHUFFLE=retail. */
void song_shuffle_after_build(uint32_t names, uint32_t count_ptr);

#endif

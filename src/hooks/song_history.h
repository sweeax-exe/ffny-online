/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Previous song (build 96): a short history of the songs that played, for the "previous song" key, as on
 * a music player. The first back press plays the current song from its start; a second one within
 * 3 seconds, or a back press within the first 3 seconds of a song, goes to the song that played before
 * it (and on back from there). Pure: no guest memory, the clock is passed in (next_song.c uses it;
 * tests/unit/test_song_history_native.py tests it).
 *
 * An entry is a game song (its playlist id, "dirty") or one of the player's own songs (its path in the
 * music folder). Going back moves the place in the history without adding to it; the song that then
 * starts is that entry. A new song after that drops the entries after the place (as a browser's
 * history does) and is added at the end. */
#ifndef DEFJAM_SONG_HISTORY_H
#define DEFJAM_SONG_HISTORY_H

#ifdef __cplusplus
extern "C" {
#endif

enum { SONG_HIST_GAME = 1, SONG_HIST_OWN = 2 };

#define SONG_HIST_MAX     16
#define SONG_HIST_BACK_MS 3000ull   /* back again within this, or this far into a song: the previous one */
#define SONG_HIST_WAIT_MS 6000ull   /* after a back, what else plays is not a new song for this long */

typedef struct SongHistEntry {
    int kind;                   /* SONG_HIST_GAME, SONG_HIST_OWN */
    char id[160];
    unsigned long long started; /* when it began (or was started again) */
} SongHistEntry;

typedef struct SongHistory {
    SongHistEntry e[SONG_HIST_MAX];
    int n, pos;                 /* entries; the one playing now (-1: none) */
    int expect;                 /* a back was made: the song at `pos` is about to start */
    unsigned long long expect_at, last_back;
    int backs;                  /* back presses so far (0: last_back is not set) */
} SongHistory;

void song_hist_reset(SongHistory *h);

/* What plays now (call it as often as you like). Returns 1 when it is a new entry, else 0. */
int song_hist_played(SongHistory *h, int kind, const char *id, unsigned long long now);

/* The entry playing now, or NULL. */
const SongHistEntry *song_hist_current(const SongHistory *h);

/* A back press. Returns -1: play the current song from its start; else the index of the entry to play
 * now (the place moves there). `playable` says whether an earlier entry can be played now (NULL: all);
 * the ones that cannot are passed over. */
int song_hist_back(SongHistory *h, unsigned long long now, int (*playable)(void *ctx, const SongHistEntry *e),
                   void *ctx);

#ifdef __cplusplus
}
#endif

#endif

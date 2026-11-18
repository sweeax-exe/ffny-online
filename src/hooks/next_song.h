/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Next song (build 94, a player's wish): a key ([audio] next_song_key, M) or pad buttons
 * ([audio] next_song_pad, off) skip to the next song of the music playing now, with a short
 * "Now playing: <title>" note.
 *
 * Purely local, as the music already is (each PC shuffles its own list, songs end at different
 * moments on each PC):
 *  - the request comes from the overlay's input layer (pc_ui.cpp: the window's key messages, the
 *    pads read once a frame), on any thread, and only sets a flag;
 *  - it is acted on at the clean point between two game updates (sub_0002C530_enter, after
 *    netplay_on_update, through custom_music_on_update), never inside a rollback re-run step;
 *  - one of the player's own songs (custom_music.cpp) goes on to the next in their list on the host
 *    player thread; the game's own playlist is moved on the way a song's end moves it: the playlist
 *    machine (sub_0003D060) in state 2 with its timer past the song's length, the index a step on
 *    when the "hold" byte would keep it. Those words (0x33E790.., 0x340398) are the game's sound
 *    globals, which rollback never saves or restores and the lockstep checksum does not cover; the
 *    game's own next frame loads the song as when one ends.
 * The playlist holds only the songs that are on, so turned-off songs stay off; a new match still
 * builds its own list ("new song every match" is untouched). */
#ifndef DEFJAM_NEXT_SONG_H
#define DEFJAM_NEXT_SONG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ask for the next song (`from`: "key", "pad", "test" for the log). Any thread. */
void next_song_request(const char *from);

/* Previous song (build 96, [audio] prev_song_key / prev_song_pad; song_history.h): the first press plays
 * the current song from its start, a second one within 3 s (or one in a song's first 3 s) the song that
 * played before it, a game song or one of the player's own. Any thread; acted on like the next song. */
void next_song_back_request(const char *from);

/* custom_music.cpp's player thread: one of the player's songs (its path in the music folder) starts. */
void next_song_own_started(const char *rel);

/* Tests: the clock the history reads (NULL: the real one). */
void next_song_test_clock(unsigned long long (*clock)(void));

/* Game thread, at the clean point between two updates; `rerun` 1 inside a rollback re-run step
 * (then nothing happens: the request waits for a real step). */
void next_song_game_tick(int rerun);

/* The note for the overlay: 1 and the text while one is up (3.5 s after a change), else 0.
 * `now_ms`: any millisecond clock that only goes forward. */
int next_song_toast(char *out, int size, unsigned long long now_ms);

/* The song that now plays, for the note (the custom music player thread, or the game tick). */
void next_song_note_set(const char *title);

/* ---- pure parts (tests/unit/test_next_song_native.py) ---- */

typedef struct NextSongTitle {
    char entity[24];        /* the song's folder id, as the playlist names it ("dirty") */
    char title[64];
    char artist[64];
} NextSongTitle;

/* The Song table of djv2.sod: up to `max` rows. Returns the rows read, -1 when it is not there. */
int next_song_sod_titles(const uint8_t *p, size_t n, NextSongTitle *out, int max);

/* "Title - Artist", "Title" or "" for a playlist id (from the game's database, read once). */
const char *next_song_title_for(const char *entity);

#ifdef __cplusplus
}
#endif

#endif

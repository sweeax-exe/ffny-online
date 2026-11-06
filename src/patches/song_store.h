/* Songs in the profile's jukebox and the music playlist (song_store.c). */
#ifndef DEFJAM_SONG_STORE_H
#define DEFJAM_SONG_STORE_H

#include <stdint.h>

/* Before sub_00013340 (the song store is built; this = store, arguments on the stack). */
void song_store_before_build(uint32_t store);
void song_store_after_build(void);

/* Around sub_00014540 (a profile's saved block copied into manager B, this = B). */
void song_store_before_load(uint32_t manager);
void song_store_after_load(uint32_t manager);

/* Every front-end command, before the game handles it (the MemCard ones save). */
void song_store_on_funccall(const char *text);

/* In place of sub_00020C40 (shuffled = 0) / sub_00020D70 (shuffled = 1): builder is the
 * generated body. */
void song_store_build_playlist(void (*builder)(void), int shuffled);

/* In place of sub_0003CFF0 (SetMusicPlaylist); set is the generated body. */
void song_store_set_playlist(void (*set)(void));

/* After sub_0003CFF0 (the music playlist is built): a trace with RECOMP_SONGS_TRACE. */
void song_store_playlist_built(void);

/* In place of sub_0004FD80 (the Beatbox list) and sub_00050E00 (EnableSong); original is the
 * generated body, used when no song was added. */
void song_jukebox_build(void (*original)(void));
void song_jukebox_enable(void (*original)(void));
void song_jukebox_play(void (*original)(void));
/* Whether songs were added (the store is ours). */
int song_store_active(void);

/* RECOMP_TEST_SONGS_ONLY (a test aid). */
void song_store_test_only(void);

#endif

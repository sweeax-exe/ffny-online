/* Songs added to the song list get a place in a loaded profile's jukebox (song_store.c). */
#ifndef DEFJAM_SONG_STORE_H
#define DEFJAM_SONG_STORE_H

#include <stdint.h>

/* Around sub_00014540 (a profile's saved block copied into manager B, this = B). */
void song_store_before_load(uint32_t manager);
void song_store_after_load(uint32_t manager);

/* After sub_0003CFF0 (the music playlist is built): a trace with RECOMP_SONGS_TRACE. */
void song_store_playlist_built(void);

#endif

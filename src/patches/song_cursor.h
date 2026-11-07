/* Custom songs played from start to end in every mode (song_cursor.c). */
#ifndef DEFJAM_SONG_CURSOR_H
#define DEFJAM_SONG_CURSOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A converted custom song (pc_songs.c): its folder holds tune.mpf. */
void song_cursor_register(const char *id, const char *dir, int interactive);
void song_cursor_clear(void);

void song_cursor_map_load(void);                /* sub_00131080_enter: a song's map is being set up */
void song_cursor_match_start(void);             /* sub_001A4D90_enter: a match is being built */
void song_cursor_play_node(uint32_t track);     /* sub_001350E0_enter: a track plays its node */

#ifdef __cplusplus
}
#endif

#endif

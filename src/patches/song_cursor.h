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

/* Skip to the next song or play the current one from its start, as the game does when a song
 * ends (the [songs] shortcuts, src/hooks/pc_ui.cpp). Any thread: the request waits for the
 * game's next frame. */
enum { SONG_REQUEST_SKIP = 1, SONG_REQUEST_RESTART = 2 };
void song_cursor_request(int what, const char *from);
void song_cursor_game_tick(void);               /* sub_00077040_enter: once a frame, on the game's thread */

#ifdef __cplusplus
}
#endif

#endif

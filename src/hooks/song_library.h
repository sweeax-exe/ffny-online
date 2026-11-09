/* Custom songs: which music files there are and what the launcher's Songs page chose for them
 * (shared by the game and the game-free app). docs/custom-songs.md. */
#ifndef DEFJAM_SONG_LIBRARY_H
#define DEFJAM_SONG_LIBRARY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SONG_LIBRARY_PATH 1024
#define SONG_LIBRARY_MAX  1024
/* Custom songs the game holds next to its own 33 (pc_songs.h: 60 rows). */
#define SONG_LIBRARY_ROOM 27

typedef struct {
    char path[SONG_LIBRARY_PATH];   /* UTF-8 */
    char id[16];                    /* "u" + 8 hex digits of the path: the song's folder under music/ */
    int on;                         /* in the game (default) */
    int interactive;                /* experimental interactive arrangement (default straight) */
} SongEntry;

/* Every music file under <data_root>/songs and the [songs] folders, sorted by path, with the
 * choices from <data_root>/song-state/library.txt. Returns the count. */
int song_library_scan(const char *data_root, SongEntry *out, int max);

/* Write the choices of all entries to library.txt. Returns 1 on success. */
int song_library_save(const char *data_root, const SongEntry *entries, int n);

/* The folder songs are read from by default (created when missing). */
void song_library_folder(const char *data_root, char *out, size_t size);

void song_library_id(const char *path, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif

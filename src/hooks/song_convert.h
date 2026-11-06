/* Custom songs: the player's music files as songs the game plays (docs/research/custom-songs.md). */
#ifndef DEFJAM_SONG_CONVERT_H
#define DEFJAM_SONG_CONVERT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump when the files song_convert writes change: every cached song is rebuilt. */
#define SONG_CONVERT_VERSION 1

/* Title, artist and album, as text the game's menus can draw (ASCII; accents dropped). */
typedef struct {
    char title[48], artist[48], album[48];
} SongTags;

/* Whether the file is one of the formats read (by extension: .mp3 .flac .ogg .wav). */
int song_is_music_file(const char *path);

/* The file's tags; missing ones from the file name ("Artist - Title") and its folder (album).
 * path is UTF-8. Returns 1, or 0 when the file cannot be read. */
int song_read_tags(const char *path, SongTags *out);

/* The song the converted files are built on, from the player's own dump: minitrack2's map, its
 * two clip banks and the header of its first stream (AUDIO/MUSIC0-4.VIV under game_dir). */
typedef struct {
    uint8_t *mpf, *loading, *overlay;
    size_t mpf_n, loading_n, overlay_n;
    uint8_t header[128];
    size_t header_n;
    uint32_t hash;                  /* of all of the above: part of each song's cache stamp */
} SongTemplate;

int song_template_load(const char *game_dir, SongTemplate *t, char *err, size_t errn);
void song_template_free(SongTemplate *t);

/* Convert path (UTF-8) into out_dir/{main.mus,tune.mpf,loading0.mus,overlay0.mus}: decoded,
 * made 44.1 kHz stereo, cut along the template's full play and encoded as the game's streams.
 * Returns 1, or 0 with a reason in err. */
int song_convert(const char *path, const char *out_dir, const SongTemplate *t, char *err, size_t errn);

/* The EA-XA encoder (exposed for the unit test): 28 samples -> one 15-byte frame, with the
 * decoder's history carried in *h1, *h2. */
void song_eaxa_frame(const int16_t *x, int n, int32_t *h1, int32_t *h2, uint8_t out[15]);

#ifdef __cplusplus
}
#endif

#endif

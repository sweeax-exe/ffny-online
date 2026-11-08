/* Custom songs: the player's music files as songs the game plays (docs/research/custom-songs.md). */
#ifndef DEFJAM_SONG_CONVERT_H
#define DEFJAM_SONG_CONVERT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump when the files song_convert writes change: every cached song is rebuilt. */
#define SONG_CONVERT_VERSION 7

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
    int interactive;                /* built on holler's map (lanes) rather than minitrack2's */
} SongTemplate;

int song_template_load(const char *game_dir, int interactive, SongTemplate *t, char *err, size_t errn);
void song_template_free(SongTemplate *t);

/* interactive (experimental): built on holler's map, whose fight sections switch every segment
 * between four lanes by a value of the game's; lane 0 is the song, lanes 1-3 the same pieces
 * filtered (muffled, heavy, bright). t must be the template loaded for that mode.
 * Convert path (UTF-8) into out_dir/{main.mus,tune.mpf,loading0.mus,overlay0.mus}: decoded,
 * made 44.1 kHz stereo, cut along the template's full play and encoded as the game's streams.
 * Returns 1, or 0 with a reason in err. */
int song_convert(const char *path, const char *out_dir, const SongTemplate *t, int interactive, char *err, size_t errn);

/* The play-time order of a converted song: its pieces in the song's order, per lane, from its
 * tune.mpf and the pieces.txt written next to it. The game plays a map's samples in whatever order
 * its sections, events and routes take; the play-time remap (src/patches/song_cursor.c) hands each
 * segment the next piece of the song instead, in the lane (filter) the game chose, so the song
 * plays through from start to end.
 * table: the sample table's offset in the map (u32 offset / 0x80, u32 length in ms per sample);
 * lane: per track-0 sample (0-based), 0 = the song as is, 1-3 the filtered lanes (interactive);
 * off/ms[lane][k]: piece k of that lane, as a sample table entry. Returns 1, or 0. */
typedef struct {
    uint32_t table;
    int track0, pieces, lanes;
    uint8_t lane[256];
    uint32_t off[4][256], ms[4][256];
} SongPlan;

int song_plan(const uint8_t *mpf, size_t n, const char *pieces_txt, SongPlan *p);

/* The EA-XA encoder (exposed for the unit test): 28 samples -> one 15-byte frame, with the
 * decoder's history carried in *h1, *h2. */
void song_eaxa_frame(const int16_t *x, int n, int32_t *h1, int32_t *h2, uint8_t out[15]);

#ifdef __cplusplus
}
#endif

#endif

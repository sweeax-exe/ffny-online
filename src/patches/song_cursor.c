/*
 * Custom songs played from start to end, in the menus and in fights (docs/research/custom-songs.md).
 *
 * The music engine (EA's PathFinder) plays a song's map: nodes, each naming one sample (a piece of
 * main.mus), joined by routes the game picks by a value of its own, and sections the game jumps to
 * on events (a fight starting, Blazin, a knock-down). The licensed songs are cut so that every jump
 * lands somewhere musical. A converted custom song cannot be: its pieces are the player's song cut
 * in order along the menu's play, so the fights' sections and jumps would play it out of order.
 *
 * So, at play time, every segment of track 0 (main.mus) of a custom song gets the next piece of the
 * song instead of the one its node names, in the lane the game chose (interactive: the song or one
 * of its three filtered versions): the sample table entry of that node's sample is rewritten with
 * the piece's (offset, length) just before the engine reads it.
 *
 * The engine (CONFIRMED in the lifted code, recomp_0009.c):
 *   MEM32(0x3AD928) the active map context; ctx + 0x34 the map, + 0x38 its node offset table
 *   (node = map + 4 * s16 table[i]), + 0x50 its sample table (8 bytes per sample, 1-based:
 *   u32 offset / 0x80, u32 length in ms), + 0x58 + 4 * t the per-track state.
 *   sub_001350E0(track) plays the node at s16 track + 0x36: it reads the node's sample number (the
 *   s16 at node + 0) and that sample's table entry. Every advance (sub_00135860, by route) and every
 *   jump (sub_00133E60's event case) ends in it, once per segment.
 *   sub_00131080 sets up a map context.
 *
 * Where the song is: a cursor per song, the next piece to play. It starts at 0 when the song's map
 * is set up, except that in the match it last played in, the song goes on from where it stopped
 * (a map set up again in the same match, as around Blazin). At the song's end it starts again.
 * RECOMP_SONGS_SEGMENTS=1 turns this off (the map's own order, for comparison).
 */
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp/gen/recomp_types.h"
#include "hooks/song_convert.h"
#include "hooks/pc_songs.h"
#include "song_cursor.h"

#define CTX_PTR   0x3AD928u
#define MAX_SONGS PC_SONGS_MAX_ROWS

typedef struct {
    char id[16];
    SongPlan plan;
    uint32_t orig[512];             /* the converted map's sample table: offset, length per sample */
    int cursor, match;              /* next piece; the match it last played in (0: none) */
} CursorSong;

static CursorSong *s_songs;
static int s_count, s_off = -1, s_trace = -1;
static int s_match, s_reload = 1, s_cur = -1;
static uint32_t s_table;            /* the guest table as this file last left it ... */
static uint32_t s_left[512];        /* ... and its contents */
static int s_left_n;

static int trace(void)
{
    if (s_trace < 0) s_trace = getenv("RECOMP_SONGS_TRACE") != NULL;
    return s_trace;
}

void song_cursor_clear(void)
{
    free(s_songs);
    s_songs = NULL;
    s_count = 0;
    s_cur = -1;
    s_reload = 1;
}

static uint8_t *read_file(const char *dir, const char *name, size_t *n)
{
    char path[1100];
    snprintf(path, sizeof path, "%s" HOST_SEP "%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *d = (uint8_t *)malloc((1u << 20) + 1);
    *n = d ? fread(d, 1, 1u << 20, f) : 0;
    fclose(f);
    if (d) d[*n] = 0;
    return d;
}

void song_cursor_register(const char *id, const char *dir, int interactive)
{
    size_t n = 0, pn = 0;
    (void)interactive;
    if (s_off < 0) s_off = getenv("RECOMP_SONGS_SEGMENTS") != NULL;
    if (s_off) return;
    if (!s_songs && !(s_songs = (CursorSong *)calloc(MAX_SONGS, sizeof *s_songs))) return;
    if (s_count >= MAX_SONGS) return;
    uint8_t *d = read_file(dir, "tune.mpf", &n), *pieces = read_file(dir, "pieces.txt", &pn);
    CursorSong *s = &s_songs[s_count];
    memset(s, 0, sizeof *s);
    if (d && pieces && song_plan(d, n, (const char *)pieces, &s->plan)) {
        snprintf(s->id, sizeof s->id, "%s", id);
        for (int i = 0; i < s->plan.track0 * 2; i++) {
            const uint8_t *p = d + s->plan.table + i * 4;
            s->orig[i] = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        }
        s_count++;
        if (trace()) fprintf(stderr, "[SONGS] %s: %d pieces in %d lane(s), %d samples\n", id, s->plan.pieces, s->plan.lanes, s->plan.track0);
    } else {
        fprintf(stderr, "[SONGS] %s: its map was not read; it plays in the map's own order\n", id);
    }
    free(d);
    free(pieces);
}

static int same_as_left(uint32_t table)
{
    if (table != s_table) return 0;
    for (int i = 0; i < s_left_n; i++)
        if (MEM32(table + i * 4) != s_left[i]) return 0;
    return 1;
}

/* Which custom song, if any, the table at `table` is (still as converted). */
static int identify(uint32_t table)
{
    for (int k = 0; k < s_count; k++) {
        const CursorSong *s = &s_songs[k];
        int i = 0, words = s->plan.track0 * 2;
        while (i < words && MEM32(table + i * 4) == s->orig[i]) i++;
        if (i == words) return k;
    }
    return -1;
}

static void remember(uint32_t table, int words)
{
    s_table = table;
    s_left_n = words < 512 ? words : 512;
    for (int i = 0; i < s_left_n; i++) s_left[i] = MEM32(table + i * 4);
}

/* A map is being set up. If it is the custom song's table this file left (the engine keeps a map
 * it sets up again), its entries go back to the converted order first: the game sizes its music
 * read buffer from consecutive entries (sub_001317C0, at a match's start too), and the remapped
 * ones would make that size nonsense. */
void song_cursor_map_load(void)
{
    s_reload = 1;
    if (s_cur < 0 || !s_table || !same_as_left(s_table)) return;
    const CursorSong *s = &s_songs[s_cur];
    for (int i = 0; i < s->plan.track0 * 2 && i < s_left_n; i++) MEM32(s_table + i * 4) = s->orig[i];
    remember(s_table, s->plan.track0 * 2);
}

void song_cursor_match_start(void) { s_match++; }

void song_cursor_play_node(uint32_t track)
{
    if (!s_count) return;
    uint32_t ctx = MEM32(CTX_PTR);
    if (!ctx || !track || track != MEM32(ctx + 0x58)) return;          /* track 0 only: main.mus */
    uint32_t map = MEM32(ctx + 0x34), nodes = MEM32(ctx + 0x38), table = MEM32(ctx + 0x50);
    if (!map || !nodes || !table) return;

    int same = same_as_left(table);
    if (s_reload || !same) {
        int was = s_cur;
        s_reload = 0;
        /* a map set up again over the table this file left (the engine kept it): the same song */
        if (!(same && s_cur >= 0)) s_cur = identify(table);
        remember(table, s_cur >= 0 ? s_songs[s_cur].plan.track0 * 2 : 64);
        if (s_cur >= 0) {
            CursorSong *s = &s_songs[s_cur];
            int resume = s->match && s->match == s_match && s->cursor;
            if (!resume) s->cursor = 0;
            s->match = s_match;
            if (trace())
                fprintf(stderr, "[SONGS] %s: map set up, %s at piece %d of %d\n", s->id,
                        resume ? "goes on" : "starts", s->cursor, s->plan.pieces);
        } else if (was >= 0 && trace()) {
            fprintf(stderr, "[SONGS] not a custom song's map any more\n");
        }
    }
    if (s_cur < 0) return;

    CursorSong *s = &s_songs[s_cur];
    int idx = SMEM16(track + 0x36);
    if (idx < 0 || idx >= (int)MEM16(map + 0x12)) return;
    uint32_t node = map + 4u * (uint32_t)(int32_t)SMEM16(nodes + 2u * (uint32_t)idx);
    int sample = SMEM16(node);
    if (sample <= 0 || sample > s->plan.track0) return;                 /* section starts, control */

    int lane = s->plan.lane[sample - 1], k = s->cursor;
    if (lane >= s->plan.lanes) lane = 0;
    uint32_t off = s->plan.off[lane][k], ms = s->plan.ms[lane][k];
    MEM32(table + (uint32_t)(sample - 1) * 8) = off;
    MEM32(table + (uint32_t)(sample - 1) * 8 + 4) = ms;
    s_left[(sample - 1) * 2] = off;
    s_left[(sample - 1) * 2 + 1] = ms;
    s->cursor = (k + 1) % s->plan.pieces;
    if (trace())
        fprintf(stderr, "[SONGS] %s: node %d sample %d lane %d -> piece %d (%u ms)\n", s->id, idx, sample, lane, k, ms);
}

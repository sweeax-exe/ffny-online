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
    int playing;                    /* the piece handed out last (-1: none since the map was set up) */
    double started, length;         /* when, and its length (ms) */
} CursorSong;

static CursorSong *s_songs;
static int s_count, s_off = -1, s_trace = -1;
static int s_match, s_reload = 1, s_cur = -1;
static int s_fresh;                 /* the next custom song set up starts at its beginning (a shortcut) */
static volatile int s_request;      /* SONG_REQUEST_*, from the shortcuts; done on the game's thread */
static int s_request_wait;          /* frames it has waited for the song to be playing */
static uint32_t s_table;            /* the guest table as this file last left it ... */
static uint32_t s_left[512];        /* ... and its contents */
static int s_left_n;

static double now_ms(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

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
            int resume = !s_fresh && s->match && s->match == s_match && s->cursor;
            if (!resume) s->cursor = 0;
            s->match = s_match;
            s->playing = -1;
            s_fresh = 0;
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

    /* A node played again before the piece handed out last got half way through (a fight's
     * start fired its event a dozen times in half a second in a play-test, and every call took a
     * piece: the song jumped 30 s) gets that piece again: the song does not skip what it has not
     * played. */
    double t_now = now_ms();
    int again = s->playing >= 0 && t_now - s->started < s->length / 2;
    int lane = s->plan.lane[sample - 1], k = again ? s->playing : s->cursor;
    if (lane >= s->plan.lanes) lane = 0;
    uint32_t off = s->plan.off[lane][k], ms = s->plan.ms[lane][k];
    MEM32(table + (uint32_t)(sample - 1) * 8) = off;
    MEM32(table + (uint32_t)(sample - 1) * 8 + 4) = ms;
    s_left[(sample - 1) * 2] = off;
    s_left[(sample - 1) * 2 + 1] = ms;
    s->started = t_now;                                     /* a piece played again starts over */
    if (!again) {
        s->playing = k;
        s->length = ms;
        s->cursor = (k + 1) % s->plan.pieces;
    }
    if (trace())
        fprintf(stderr, "[SONGS] %s: node %d sample %d lane %d -> piece %d (%u ms)%s\n", s->id, idx, sample, lane, k, ms,
                again ? " again" : "");
}

/* ---- the shortcuts: next song, the song from its start ---------------------------------------- */
/*
 * The music playlist's per-frame machine (sub_0003D060, menus and fights alike; research notes):
 * MEM32(0x340398) its state (1 playing, 2 ending), MEMF(0x33E790) the song's timer and
 * MEMF(0x33EC40) its length (s), MEM32(0x33EC94) the playlist index and MEM32(0x33E9DC) the count,
 * MEM8(0x33EC90) "hold" (the index is not moved on), MEM8(0x33E794) the playlist on, MEM32(0x340374)
 * the audio mode (1 menus, 2 fight), MEM32(0x3407B4) a song description loading (1, 2). In state 2,
 * once the timer passes the length, it moves the index on and loads that song, with its banner. So a
 * shortcut sets state 2 with the timer past the length, the index a step back first for "from the
 * start", and the game's own next frame does the rest, as when a song ends.
 */
void song_cursor_request(int what, const char *from)
{
    s_request = what;
    fprintf(stderr, "[SONGS] shortcut (%s): %s\n", from ? from : "?", what == SONG_REQUEST_SKIP ? "next song" : "song from its start");
}

/* Test aid: RECOMP_TEST_SONG_SHORTCUT="next@40,start@70" asks for the shortcuts that many seconds
 * after the first frame (scripted runs have no host pads, so the chords cannot be pressed there). */
static void test_schedule(void)
{
    static int parsed, n, done;
    static double at[8], t0;
    static int what_at[8];
    if (!parsed) {
        const char *e = getenv("RECOMP_TEST_SONG_SHORTCUT");
        parsed = 1;
        t0 = now_ms();
        while (e && *e && n < 8) {
            double secs = 0;
            int skip = !strncmp(e, "next@", 5), start = !strncmp(e, "start@", 6);
            if ((skip || start) && sscanf(e + (skip ? 5 : 6), "%lf", &secs) == 1) {
                at[n] = secs * 1000.0;
                what_at[n++] = skip ? SONG_REQUEST_SKIP : SONG_REQUEST_RESTART;
            }
            e = strchr(e, ',');
            if (e) e++;
        }
    }
    if (done < n && now_ms() - t0 >= at[done]) {
        song_cursor_request(what_at[done], "test");
        done++;
    }
}

void song_cursor_game_tick(void)
{
    test_schedule();
    int what = s_request;
    if (!what) return;
    uint32_t mode = MEM32(0x340374), count = MEM32(0x33E9DC), state = MEM32(0x340398), loading = MEM32(0x3407B4);
    if (!MEM8(0x33E794) || !count || count > 64 || (mode != 1 && mode != 2)) {
        s_request = 0;
        s_request_wait = 0;
        fprintf(stderr, "[SONGS] shortcut: no music playlist playing (mode %u, %u songs)\n", mode, count);
        return;
    }
    if (state != 1 || loading == 1 || loading == 2) {          /* a song is ending or loading: wait */
        if (++s_request_wait > 180) {
            s_request = 0;
            s_request_wait = 0;
            fprintf(stderr, "[SONGS] shortcut: the music stayed busy, dropped\n");
        }
        return;
    }
    s_request = 0;
    s_request_wait = 0;
    uint32_t idx = MEM32(0x33EC94) % count;
    int hold = MEM8(0x33EC90) != 0;
    if (what == SONG_REQUEST_SKIP && hold) idx = (idx + 1) % count;            /* the machine will not */
    if (what == SONG_REQUEST_RESTART && !hold) idx = (idx + count - 1) % count; /* it moves on by one */
    MEM32(0x33EC94) = idx;
    MEMF(0x33E790) = MEMF(0x33EC40) + 1.0f;
    MEM32(0x340398) = 2;
    s_fresh = 1;
    fprintf(stderr, "[SONGS] shortcut: %s (playlist %u songs, %s)\n", what == SONG_REQUEST_SKIP ? "next song" : "from the start",
            count, mode == 2 ? "fight" : "menus");
}

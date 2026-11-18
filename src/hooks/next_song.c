/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Next song: the request, the step at the clean point, the note (next_song.h). */
#include "next_song.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

#include "recomp/gen/recomp_types.h"
#include "custom_music.h"

FILE *netplay_open_fight_file(const char *name, int *overridden);   /* netplay.c: the copy the game reads */

/* The music playlist machine (sub_0003D060, menus and fights; docs/research/custom-songs.md 5c). */
#define PL_TIMER   0x0033E790u   /* float: the song's time (s) */
#define PL_ON      0x0033E794u   /* u8: the playlist plays */
#define PL_NAMES   0x0033E798u   /* 36 x 16 bytes: the songs' ids */
#define PL_COUNT   0x0033E9DCu
#define PL_LENGTH  0x0033EC40u   /* float: the song's length (s) */
#define PL_HOLD    0x0033EC90u   /* u8: the index is not moved on at a song's end */
#define PL_INDEX   0x0033EC94u
#define AUDIO_MODE 0x00340374u   /* 1 menus, 2 fight */
#define PL_STATE   0x00340398u   /* 1 playing, 2 ending */
#define PL_LOADING 0x003407B4u   /* 1, 2: a song description is loading */

static volatile int s_request;
static int s_wait;

static unsigned long long now_ms(void)
{
#if defined(_WIN32)
    return GetTickCount64();
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000ull + (unsigned long long)(t.tv_nsec / 1000000);
#endif
}

void next_song_request(const char *from)
{
    s_request = 1;
    fprintf(stderr, "[MUSIC] next song asked (%s)\n", from ? from : "?");
}

/* ---- the note -------------------------------------------------------------------------------- */

static char s_note[2][96];
static volatile unsigned s_note_seq;

void next_song_note_set(const char *title)
{
    unsigned next = s_note_seq + 1;
    if (title && *title) snprintf(s_note[next & 1], sizeof s_note[0], "Now playing: %s", title);
    else snprintf(s_note[next & 1], sizeof s_note[0], "Next song");
    s_note_seq = next;
    fprintf(stderr, "[MUSIC] next song: %s\n", s_note[next & 1]);
}

int next_song_toast(char *out, int size, unsigned long long now)
{
    static unsigned seen;
    static unsigned long long until;
    unsigned seq = s_note_seq;
    if (seq != seen) {
        seen = seq;
        until = now + 3500;
    }
    if (!seq || now >= until) return 0;
    if (out && size > 0) snprintf(out, (size_t)size, "%s", s_note[seq & 1]);
    return 1;
}

/* ---- the titles: djv2.sod's Song table ------------------------------------------------------- */
/* Layout as pc_songs.c reads it (docs/research/custom-songs.md 5a): "SODB", schema at +12, tables at
 * +16, strings at +20; the schema key of a field is hash(table) ^ hash(field) (djb2 with the NUL). */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static uint32_t name_hash(const char *s)
{
    uint32_t h = 5381;
    do { h = h * 33 + (uint8_t)*s; } while (*s++);
    return h;
}

typedef struct { const uint8_t *p; size_t n; } Sod;

static int in_range(const Sod *s, uint32_t off, uint32_t len) { return off <= s->n && len <= s->n - off; }
static uint32_t word(const Sod *s, uint32_t off) { return in_range(s, off, 4) ? rd32(s->p + off) : 0; }

static uint32_t lookup(const Sod *s, uint32_t index, uint32_t key)
{
    uint32_t at = index + 4, count, stride, i;
    if (!in_range(s, at, 8)) return 0;
    count = word(s, at);
    stride = word(s, at + 4);
    if (!stride || stride > 256 || count > 0x100000u || !in_range(s, at + 8, count * 4 * (stride + 1))) return 0;
    for (i = 0; i < count; i++)
        if (word(s, at + 8 + i * 4) == key) return at + 8 + count * 4 + i * stride * 4;
    return 0;
}

static int field(const Sod *s, uint32_t schema, uint32_t table_hash, const char *name, uint32_t *w)
{
    uint32_t v = lookup(s, schema, table_hash ^ name_hash(name));
    if (!v) return 0;
    *w = word(s, v);
    return 1;
}

static void string_at(const Sod *s, uint32_t strings, uint32_t nstr, uint32_t rec, int have, uint32_t w,
                      char *out, size_t size)
{
    uint32_t idx = have ? word(s, rec + w * 4) : 0xFFFFFFFFu, at;
    size_t k = 0;
    out[0] = 0;
    if (idx >= nstr) return;
    at = word(s, strings + 12 + idx * 8);
    if (!at) return;
    while (k + 1 < size && at + k < s->n && s->p[at + k]) {
        char c = (char)s->p[at + k];
        out[k++] = (c >= 32 && c < 127) ? c : '?';
    }
    out[k] = 0;
}

int next_song_sod_titles(const uint8_t *p, size_t n, NextSongTitle *out, int max)
{
    Sod s;
    uint32_t schema, tables, strings, nstr, thash, hv, header, group0, count, members, sub, list, i;
    uint32_t we = 0, wt = 0, wa = 0;
    int he, ht, ha;
    s.p = p;
    s.n = n;
    if (!p || n < 24 || memcmp(p, "SODB", 4)) return -1;
    schema = word(&s, 12);
    tables = word(&s, 16);
    strings = word(&s, 20);
    nstr = word(&s, strings + 4);
    thash = name_hash("Song");
    hv = lookup(&s, tables, thash);
    he = field(&s, schema, thash, "Entity", &we);
    ht = field(&s, schema, thash, "Title", &wt);
    ha = field(&s, schema, thash, "Artist", &wa);
    if (!hv || !he || nstr > 0x100000u || !in_range(&s, strings + 8, nstr * 8)) return -1;
    header = word(&s, hv);
    group0 = word(&s, header + 4);
    count = word(&s, header + 12);
    members = word(&s, group0);
    if (!members || members > 4096) return -1;
    sub = word(&s, group0 + 8 + members * 4);
    if (word(&s, sub + 4) != count || word(&s, sub + 8) != 1 || count > 4096) return -1;
    list = sub + 12 + count * 4;
    if (!in_range(&s, list, count * 4)) return -1;
    for (i = 0; i < count && (int)i < max; i++) {
        uint32_t rec = word(&s, list + i * 4);
        string_at(&s, strings, nstr, rec, he, we, out[i].entity, sizeof out[i].entity);
        string_at(&s, strings, nstr, rec, ht, wt, out[i].title, sizeof out[i].title);
        string_at(&s, strings, nstr, rec, ha, wa, out[i].artist, sizeof out[i].artist);
    }
    return (int)(count < (uint32_t)max ? count : (uint32_t)max);
}

#define MAX_TITLES 64
static NextSongTitle s_titles[MAX_TITLES];
static int s_title_count = -2;      /* -2 not read yet, -1 none */

static void load_titles(void)
{
    int overridden = 0;
    FILE *f;
    long size;
    uint8_t *buf;
    s_title_count = -1;
    f = netplay_open_fight_file("assets\\djv2.sod", &overridden);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0 && size < 64L * 1024 * 1024 && (buf = (uint8_t *)malloc((size_t)size))) {
        size_t got = fread(buf, 1, (size_t)size, f);
        s_title_count = next_song_sod_titles(buf, got, s_titles, MAX_TITLES);
        free(buf);
    }
    fclose(f);
    fprintf(stderr, "[MUSIC] next song: %d song titles from the database\n", s_title_count);
}

static int same_id(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y) return 0;
        if (!x) return 1;
    }
}

const char *next_song_title_for(const char *entity)
{
    static char out[132];
    int i;
    if (s_title_count == -2) load_titles();
    if (!entity || !*entity) return "";
    for (i = 0; i < s_title_count; i++) {
        if (!same_id(s_titles[i].entity, entity)) continue;
        if (!s_titles[i].title[0]) return "";
        if (s_titles[i].artist[0]) snprintf(out, sizeof out, "%s - %s", s_titles[i].title, s_titles[i].artist);
        else snprintf(out, sizeof out, "%s", s_titles[i].title);
        return out;
    }
    return "";
}

/* ---- the step -------------------------------------------------------------------------------- */

/* Test aid: RECOMP_TEST_NEXT_SONG="40,55,70" asks for the next song that many seconds after the first
 * real step (scripted runs press no keys). */
static void test_schedule(void)
{
    static int parsed, n, done;
    static unsigned long long t0;
    static double at[16];
    if (!parsed) {
        const char *e = getenv("RECOMP_TEST_NEXT_SONG");
        parsed = 1;
        t0 = now_ms();
        while (e && *e && n < 16) {
            at[n++] = atof(e) * 1000.0;
            e = strchr(e, ',');
            if (e) e++;
        }
    }
    if (done < n && (double)(now_ms() - t0) >= at[done]) {
        done++;
        next_song_request("test");
    }
}

void next_song_game_tick(int rerun)
{
    uint32_t mode, count, state, loading, idx, next;
    char id[17];
    int k;
    if (rerun) return;      /* never inside a rollback re-run step: the request waits */
    test_schedule();
    if (!s_request) return;
    /* One of the player's own songs is the music: its player goes on to the next in their list. */
    if (custom_music_want_mute()) {
        s_request = 0;
        s_wait = 0;
        custom_music_skip();
        return;
    }
    mode = MEM32(AUDIO_MODE);
    count = MEM32(PL_COUNT);
    state = MEM32(PL_STATE);
    loading = MEM32(PL_LOADING);
    if (!MEM8(PL_ON) || !count || count > 36 || (mode != 1 && mode != 2)) {
        s_request = 0;
        s_wait = 0;
        fprintf(stderr, "[MUSIC] next song: no music playlist playing (mode %u, %u songs)\n", mode, count);
        return;
    }
    if (state != 1 || loading == 1 || loading == 2) {         /* a song is ending or loading: wait */
        if (++s_wait > 300) {
            s_request = 0;
            s_wait = 0;
            fprintf(stderr, "[MUSIC] next song: the music stayed busy, dropped\n");
        }
        return;
    }
    s_request = 0;
    s_wait = 0;
    idx = MEM32(PL_INDEX) % count;
    next = (idx + 1) % count;
    /* The machine moves the index on by one at a song's end, unless "hold" is set: then we do. */
    if (MEM8(PL_HOLD)) MEM32(PL_INDEX) = next;
    else MEM32(PL_INDEX) = idx;
    MEMF(PL_TIMER) = MEMF(PL_LENGTH) + 1.0f;
    MEM32(PL_STATE) = 2;
    for (k = 0; k < 16; k++) id[k] = (char)MEM8(PL_NAMES + 16 * next + (uint32_t)k);
    id[16] = 0;
    fprintf(stderr, "[MUSIC] next song: playlist %u songs (%s), #%u -> #%u %s\n", count, mode == 2 ? "fight" : "menus",
            idx, next, id);
    next_song_note_set(next_song_title_for(id));
}

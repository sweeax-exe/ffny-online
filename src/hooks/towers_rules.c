/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Towers: the pure rules. See towers_rules.h. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "towers_rules.h"

uint32_t towers_next32(uint32_t *state)
{
    uint32_t z = (*state += 0x9E3779B9u);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return x < y ? -1 : x > y;
}

uint32_t towers_roster_hash(const int *ids, int n)
{
    static int sorted[TOWER_MAX_ROSTER];
    uint32_t h = 2166136261u;
    int i, k;
    if (n > TOWER_MAX_ROSTER) n = TOWER_MAX_ROSTER;
    if (n < 0) n = 0;
    memcpy(sorted, ids, (size_t)n * sizeof(int));
    qsort(sorted, (size_t)n, sizeof(int), cmp_int);
    for (i = 0; i < n; i++)
        for (k = 0; k < 4; k++) h = (h ^ (uint8_t)((uint32_t)sorted[i] >> (8 * k))) * 16777619u;
    return h;
}

/* sorted, without duplicates and without `drop` (-1: none); returns the count */
static int clean_list(int *out, const int *in, int n, int max, int drop)
{
    int i, m = 0;
    for (i = 0; i < n && m < max; i++) if (in[i] >= 0 && in[i] != drop) out[m++] = in[i];
    qsort(out, (size_t)m, sizeof(int), cmp_int);
    for (i = 0, n = 0; i < m; i++) if (!n || out[n - 1] != out[i]) out[n++] = out[i];
    return n;
}

int towers_new_run(TowerRun *run, int tower, uint32_t seed, int fighter, int skin, const int *pool, int pool_n,
                   const int *venues, int venue_n)
{
    uint32_t st = seed;
    int n, i, v[TOWER_MAX_VENUES], vn;
    memset(run, 0, sizeof *run);
    run->tower = tower;
    run->seed = seed;
    run->fighter = fighter;
    run->skin = skin;
    n = clean_list(run->roster, pool, pool_n, TOWER_MAX_ROSTER, fighter);
    vn = clean_list(v, venues, venue_n, TOWER_MAX_VENUES, -1);
    for (i = 0; i < vn; i++)
        if (tower != TOWER_UPTOWN || v[i] < TOWER_FIRST_COMMUNITY_VENUE) run->venues[run->venue_n++] = v[i];
    run->roster_hash = towers_roster_hash(run->roster, n);
    for (i = n - 1; i > 0; i--) {       /* Fisher-Yates over the list sorted by ID */
        int j = (int)(towers_next32(&st) % (uint32_t)(i + 1)), t = run->roster[i];
        run->roster[i] = run->roster[j];
        run->roster[j] = t;
    }
    run->floors = n;
    run->floor = 1;
    run->health_pct = 100;
    if (!n || !run->venue_n) { run->floors = 0; return 0; }
    return n;
}

int towers_opponent(const TowerRun *run, int floor)
{
    if (!run || floor < 1 || floor > run->floors) return -1;
    return run->roster[floor - 1];
}

static int venue_index(const TowerRun *run, int floor)
{
    uint32_t st = run->seed ^ ((uint32_t)floor * 0x7FEB352Du) ^ 0xA5A5F00Du;
    return (int)(towers_next32(&st) % (uint32_t)run->venue_n);
}

int towers_venue(const TowerRun *run, int floor)
{
    int i;
    if (!run || run->venue_n <= 0 || floor < 1) return -1;
    i = venue_index(run, floor);
    /* never the same venue on two floors in a row (the previous floor's own draw, unadjusted) */
    if (run->venue_n > 1 && floor > 1 && i == venue_index(run, floor - 1)) i = (i + 1) % run->venue_n;
    return run->venues[i];
}

int towers_is_boss(int tower, int floor, int floors)
{
    if (floor < 1 || floor > floors) return 0;
    if (tower == TOWER_UPTOWN) return floor == floors;
    if (tower == TOWER_FIVE_BOROUGHS) return floor % TOWER_CHAPTER == 0;
    return 0;
}

int towers_is_save_point(int tower, int beaten, int floors, int chapter)
{
    if (beaten < 1) return 0;
    if (beaten >= floors) return 1;
    if (tower == TOWER_FIVE_BOROUGHS) return beaten % (chapter > 0 ? chapter : TOWER_CHAPTER) == 0;
    return 1;
}

void towers_floor_rules(int tower, int floor, int floors, TowerFloorRules *out)
{
    float p, t;
    int span = floors;
    memset(out, 0, sizeof *out);
    out->cpu_damage = out->cpu_health = 1.0f;
    if (tower == TOWER_FIVE_BOROUGHS && span > 100) span = 100;
    if (span < 1) span = 1;
    p = (float)floor / (float)span;
    if (p > 1.0f) p = 1.0f;
    if (p < 0.15f) {
        out->cpu_level = 0;
    } else if (p < 0.40f) {
        out->cpu_level = 1;
    } else if (p < 0.70f) {
        t = (p - 0.40f) / 0.30f;
        out->cpu_level = 2;
        out->cpu_damage = 1.0f + 0.10f * t;
        out->cpu_momentum = 0.25f * t;
    } else {
        t = (p - 0.70f) / 0.30f;
        out->cpu_level = 2;
        out->cpu_damage = 1.10f + 0.20f * t;
        out->cpu_momentum = 0.25f + 0.25f * t;
        out->cpu_health = 1.0f + 0.15f * t;
    }
    if (towers_is_boss(tower, floor, floors)) {
        out->boss = 1;
        out->cpu_level = 2;
        out->cpu_damage += 0.10f;
        out->cpu_momentum += 0.25f;
        out->cpu_health *= 1.25f;
    }
}

int towers_health_after_win(int rule, int refill, int end_pct)
{
    int h = end_pct < 1 ? 1 : end_pct > 100 ? 100 : end_pct;
    if (rule == TOWER_HEALTH_CARRY) return h;
    if (rule == TOWER_HEALTH_REFILL) {
        h += refill < 0 ? 0 : refill;
        return h > 100 ? 100 : h;
    }
    return 100;
}

const char *towers_name(int tower)
{
    return tower == TOWER_UPTOWN ? "UPTOWN" : tower == TOWER_FIVE_BOROUGHS ? "FIVE BOROUGHS" :
           tower == TOWER_WEEKLY ? "THE WEEKLY" : "TOWER";
}

/* ---------------------------------------------------------------- the run file */

#define PUT(...) do { if (n < size) n += snprintf(out + n, (size_t)(size - n), __VA_ARGS__); } while (0)

static int put_list(char *out, int n, int size, const char *key, const int *v, int count)
{
    int i;
    PUT("\"%s\":[", key);
    for (i = 0; i < count; i++) PUT("%s%d", i ? "," : "", v[i]);
    PUT("]");
    return n;
}

int towers_file_write(const TowerRun *const runs[TOWER_COUNT], const int best[TOWER_COUNT], char *out, int size)
{
    int n = 0, t, first = 1;
    if (size <= 0) return 0;
    out[0] = 0;
    PUT("{\"version\":1,\"best\":[%d,%d,%d],\"runs\":[", best[0], best[1], best[2]);
    for (t = 0; t < TOWER_COUNT; t++) {
        const TowerRun *r = runs[t];
        if (!r || r->floors <= 0) continue;
        PUT("%s\n{\"tower\":%d,\"seed\":%u,\"fighter\":%d,\"skin\":%d,\"floors\":%d,\"floor\":%d,\"hash\":%u,"
            "\"health\":%d,\"climb_ms\":%u,\"interrupted\":%d,\"in_fight\":%d,\"draws\":%d,\"skipped\":%d,"
            "\"started\":%lld,",
            first ? "" : ",", r->tower, r->seed, r->fighter, r->skin, r->floors, r->floor, r->roster_hash, r->health_pct,
            r->climb_ms, r->interrupted, r->in_fight, r->draws, r->skipped, r->started);
        n = put_list(out, n, size, "venues", r->venues, r->venue_n);
        PUT(",");
        n = put_list(out, n, size, "roster", r->roster, r->floors);
        PUT("}");
        first = 0;
    }
    PUT("]}\n");
    return n < size ? n : 0;
}

/* The value after "key": inside [from, to), or NULL. */
static const char *key_at(const char *from, const char *to, const char *key)
{
    char want[40];
    const char *p;
    snprintf(want, sizeof want, "\"%s\":", key);
    p = strstr(from, want);
    return p && p < to ? p + strlen(want) : NULL;
}

static int read_ll(const char *from, const char *to, const char *key, long long *v)
{
    const char *p = key_at(from, to, key);
    char *end;
    if (!p) return 0;
    *v = strtoll(p, &end, 10);
    return end != p;
}

static int read_int(const char *from, const char *to, const char *key, int *v)
{
    long long x;
    if (!read_ll(from, to, key, &x) || x < -2147483647LL - 1 || x > 2147483647LL) return 0;
    *v = (int)x;
    return 1;
}

static int read_u32(const char *from, const char *to, const char *key, uint32_t *v)
{
    long long x;
    if (!read_ll(from, to, key, &x) || x < 0 || x > 0xFFFFFFFFLL) return 0;
    *v = (uint32_t)x;
    return 1;
}

/* A list of numbers; returns the count, -1 when absent or damaged. */
static int read_list(const char *from, const char *to, const char *key, int *v, int max)
{
    const char *p = key_at(from, to, key);
    int n = 0;
    if (!p || *p != '[') return -1;
    p++;
    while (p < to && *p != ']') {
        char *end;
        long x = strtol(p, &end, 10);
        if (end == p || n >= max) return -1;
        v[n++] = (int)x;
        p = end;
        if (*p == ',') p++;
    }
    return p < to && *p == ']' ? n : -1;
}

int towers_file_read(const char *text, TowerRun runs[TOWER_COUNT], int have[TOWER_COUNT], int best[TOWER_COUNT])
{
    const char *end, *p, *list;
    int v[TOWER_COUNT], t;
    for (t = 0; t < TOWER_COUNT; t++) { have[t] = 0; best[t] = 0; }
    if (!text || strncmp(text, "{\"version\":1", 12)) return 0;
    end = text + strlen(text);
    if (read_list(text, end, "best", v, TOWER_COUNT) == TOWER_COUNT)
        for (t = 0; t < TOWER_COUNT; t++) best[t] = v[t] < 0 ? 0 : v[t];
    list = key_at(text, end, "runs");
    if (!list || *list != '[') return 1;
    for (p = strchr(list, '{'); p; p = strchr(p + 1, '{')) {
        const char *close = strchr(p, '}');
        TowerRun r;
        int tower = 0, ok;
        long long started = 0;
        if (!close) break;
        memset(&r, 0, sizeof r);
        ok = read_int(p, close, "tower", &tower) && tower >= 1 && tower <= TOWER_COUNT &&
             read_u32(p, close, "seed", &r.seed) && read_int(p, close, "fighter", &r.fighter) &&
             read_int(p, close, "skin", &r.skin) && read_int(p, close, "floors", &r.floors) &&
             read_int(p, close, "floor", &r.floor) && read_u32(p, close, "hash", &r.roster_hash) &&
             read_int(p, close, "health", &r.health_pct);
        if (ok) {
            r.tower = tower;
            read_u32(p, close, "climb_ms", &r.climb_ms);
            read_int(p, close, "interrupted", &r.interrupted);
            read_int(p, close, "in_fight", &r.in_fight);
            read_int(p, close, "draws", &r.draws);
            read_int(p, close, "skipped", &r.skipped);
            if (read_ll(p, close, "started", &started)) r.started = started;
            r.venue_n = read_list(p, close, "venues", r.venues, TOWER_MAX_VENUES);
            ok = r.venue_n > 0 && r.floors > 0 && r.floors <= TOWER_MAX_ROSTER &&
                 read_list(p, close, "roster", r.roster, TOWER_MAX_ROSTER) == r.floors &&
                 r.floor >= 1 && r.floor <= r.floors && r.health_pct >= 1 && r.health_pct <= 100 &&
                 towers_roster_hash(r.roster, r.floors) == r.roster_hash;
        }
        if (ok) {
            runs[tower - 1] = r;
            have[tower - 1] = 1;
        }
        p = close;
    }
    return 1;
}

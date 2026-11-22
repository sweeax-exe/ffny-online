/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Towers rewards: the plain part (tower_rewards.h). */
#include "tower_rewards.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const struct { const char *id; int kind; } k_ids[] = {
    { "tw-floor25", TRW_FLOOR25 }, { "tw-floor50", TRW_FLOOR50 }, { "tw-uptown", TRW_UPTOWN },
    { "tw-untouchable", TRW_UNTOUCHABLE }, { "tw-weekly", TRW_WEEKLY }, { "tw-boroughs", TRW_BOROUGHS },
};

int tower_reward_kind(const char *id, int *tower, int *year, int *month)
{
    size_t i;
    int t = 0, y = 0, m = 0, n = 0;
    if (tower) *tower = 0;
    if (year) *year = 0;
    if (month) *month = 0;
    if (id && !strncmp(id, "fw-", 3)) {        /* build 108: fw-winner|top-<dmob|crow>-<yyyy>-<mm> */
        char side[8] = "";
        int top = !strncmp(id, "fw-top-", 7), win = !strncmp(id, "fw-winner-", 10);
        if (!strcmp(id, "fw-winner-season")) return TRW_FW_DMOB;      /* the Locker's locked row */
        if (!top && !win) return TRW_NONE;
        if (sscanf(id + (top ? 7 : 10), "%4[a-z]-%4d-%2d%n", side, &y, &m, &n) != 3 || id[(top ? 7 : 10) + n] != 0 || y < 2026 ||
            m < 1 || m > 12 || (strcmp(side, "dmob") && strcmp(side, "crow")))
            return TRW_NONE;
        if (year) *year = y;
        if (month) *month = m;
        if (tower) *tower = !strcmp(side, "dmob") ? 1 : 2;     /* the side: 1 D-Mob, 2 Crow */
        return top ? (!strcmp(side, "dmob") ? TRW_FW_TOP_DMOB : TRW_FW_TOP_CROW) : (!strcmp(side, "dmob") ? TRW_FW_DMOB : TRW_FW_CROW);
    }
    if (!id || strncmp(id, "tw-", 3)) return TRW_NONE;
    for (i = 0; i < sizeof(k_ids) / sizeof(k_ids[0]); i++)
        if (!strcmp(id, k_ids[i].id)) return k_ids[i].kind;
    /* tw-legend-<tower>-<yyyy>-<mm>; "tw-legend-month" is the Locker's locked row (the frame shows as a legend) */
    if (!strcmp(id, "tw-legend-month")) return TRW_LEGEND;
    if (sscanf(id, "tw-legend-%d-%4d-%2d%n", &t, &y, &m, &n) == 3 && id[n] == 0 && (t == 1 || t == 2) && y >= 2026 &&
        m >= 1 && m <= 12) {
        if (tower) *tower = t;
        if (year) *year = y;
        if (month) *month = m;
        return TRW_LEGEND;
    }
    return TRW_NONE;
}

int tower_reward_rarity(int kind)
{
    switch (kind) {
    case TRW_FLOOR25: case TRW_FLOOR50: return TRW_RARE;
    case TRW_UPTOWN: case TRW_UNTOUCHABLE: case TRW_WEEKLY: return TRW_EPIC;
    case TRW_BOROUGHS: return TRW_LEGENDARY;
    case TRW_LEGEND: return TRW_MYTHIC;
    case TRW_FW_DMOB: case TRW_FW_CROW: return TRW_LEGENDARY;
    case TRW_FW_TOP_DMOB: case TRW_FW_TOP_CROW: return TRW_MYTHIC;
    default: return 0;
    }
}

const char *tower_reward_rarity_name(int rarity)
{
    static const char *const names[] = { "", "RARE", "EPIC", "LEGENDARY", "MYTHIC" };
    return rarity >= 1 && rarity <= 4 ? names[rarity] : "";
}

unsigned tower_reward_effects(int kind)
{
    switch (kind) {
    case TRW_UPTOWN: return TRW_FX_SWEEP | TRW_FX_SHINE;
    case TRW_UNTOUCHABLE: return TRW_FX_GLINT;
    case TRW_WEEKLY: return TRW_FX_GLOW | TRW_FX_FLICKER;
    case TRW_BOROUGHS: return TRW_FX_PULSE | TRW_FX_SWEEP | TRW_FX_SHINE;
    case TRW_LEGEND: return TRW_FX_EMBERS | TRW_FX_PULSE | TRW_FX_SWEEP | TRW_FX_SHINE;
    case TRW_FW_DMOB: case TRW_FW_CROW: return TRW_FX_PULSE | TRW_FX_SWEEP | TRW_FX_SHINE;     /* the side's glow breathes */
    case TRW_FW_TOP_DMOB: case TRW_FW_TOP_CROW: return TRW_FX_EMBERS | TRW_FX_PULSE | TRW_FX_SWEEP | TRW_FX_SHINE;
    default: return 0;      /* RARE: the metal itself, no motion */
    }
}

int tower_reward_effect_count(int kind)
{
    unsigned fx = tower_reward_effects(kind);
    int n = 0;
    for (; fx; fx &= fx - 1) n++;
    return n;
}

int tower_reward_ribbon(const char *id, char *out, int size)
{
    static const char *const months[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    int tower, year, month, n;
    if (!out || size <= 0) return 0;
    out[0] = 0;
    {
        int k = tower_reward_kind(id, &tower, &year, &month);
        if (k >= TRW_FW_DMOB && k <= TRW_FW_TOP_CROW) {
            const char *team = tower == 1 ? "D-MOB'S TEAM" : "CROW'S TEAM";
            if (!year) n = snprintf(out, (size_t)size, "THE SEASON'S WINNING SIDE");
            else if (k == TRW_FW_DMOB || k == TRW_FW_CROW)
                n = snprintf(out, (size_t)size, "%s WON \xC2\xB7 %s %d", team, months[month - 1], year);
            else n = snprintf(out, (size_t)size, "#1 OF %s \xC2\xB7 %s %d", team, months[month - 1], year);
            return n < 0 ? 0 : n >= size ? size - 1 : n;
        }
    }
    if (tower_reward_kind(id, &tower, &year, &month) != TRW_LEGEND || !tower) return 0;
    n = snprintf(out, (size_t)size, "#1 %s \xC2\xB7 %s %d", tower == 1 ? "UPTOWN" : "FIVE BOROUGHS", months[month - 1], year);
    return n < 0 ? 0 : n >= size ? size - 1 : n;
}

static float frac(float v) { return v - floorf(v); }

/* as the design's CSS: the band crosses in the first 55% of the period, then rests */
float tower_fx_sweep(float t, float period)
{
    float p = frac(t / period);
    return p < 0.55f ? p / 0.55f : -1.0f;
}

float tower_fx_pulse(float t, float period) { return 0.5f - 0.5f * cosf(frac(t / period) * 6.2831853f); }

float tower_fx_flicker(float t)
{
    float p = frac(t / 4.0f);
    return (p >= 0.18f && p < 0.22f) || (p >= 0.62f && p < 0.64f) ? 0.35f : 1.0f;
}

void tower_fx_ember(int i, float t, float *x, float *rise, float *alpha)
{
    static const float xs[TRW_EMBERS] = { 0.13f, 0.31f, 0.53f, 0.71f, 0.88f };
    static const float delay[TRW_EMBERS] = { 0.0f, 1.1f, 2.2f, 0.6f, 1.7f };
    float p;
    if (i < 0 || i >= TRW_EMBERS) i = 0;
    p = frac((t + 3.6f - delay[i]) / 3.6f);        /* one rise in 3.6 s */
    if (x) *x = xs[i] + 0.045f * p;
    if (rise) *rise = p;
    if (alpha) *alpha = p < 0.1f ? p / 0.1f : 1.0f - (p - 0.1f) / 0.9f;
}

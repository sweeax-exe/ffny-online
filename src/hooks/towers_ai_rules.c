/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Towers: the CPU past Hard, the pure rules. See towers_ai_rules.h. */
#include <string.h>
#include "towers_ai_rules.h"
#include "towers_rules.h"

/* Measured, not guessed (open question 1 of the study): 1 is the better defender. The defence actions' weight takes
 * eff = min(100, skill * {1.5, 1.25, 1.0, 0.75, 0.5}[p - 1]) in place of the skill, so p = 1 lets the designed
 * weights rule (as a high skill does) and p = 5 adds the flat prior back. Bot (test_bot.c, aggression 60) against
 * Magic on floor 30 (skill 92, holds and the repeat counter off), 3 fights each (build 101, logs/dj/20261121-*-
 * tower-measure): p = 1: the bot's attacks landed 58.2 %, the CPU took 6.4 damage per bot attack (236.8 a fight);
 * p = 5: 56.1 % landed but 7.9 damage per attack (299.9 a fight, longer fights). A small sample: the difference is
 * modest, and in the direction the table suggests. */
const int TOWER_AI_DEFENCE_BEST = 1;

void towers_ai_floor(int tower, int floor, int floors, TowerAiFloor *out)
{
    towers_ai_floor_ex(tower, floor, floors, 0, out);
}

void towers_ai_floor_ex(int tower, int floor, int floors, int force_boss, TowerAiFloor *out)
{
    float t;
    out->on = 0;
    out->t = 0.0f;
    out->boss = force_boss || towers_is_boss(tower, floor, floors);
    out->defence = TOWER_AI_DEFENCE_BEST;
    out->hold_cap = 0;
    if (floor < TOWER_AI_FIRST_FLOOR && !force_boss) return;
    t = floor < TOWER_AI_FIRST_FLOOR ? 0.0f : (float)(floor - TOWER_AI_FIRST_FLOOR) / (float)(TOWER_AI_RAMP_TOP - TOWER_AI_FIRST_FLOOR);
    if (t > 1.0f) t = 1.0f;
    if (out->boss) t += TOWER_AI_BOSS_EXTRA;
    if (t > 1.0f) t = 1.0f;
    out->on = 1;
    out->t = t;
    out->hold_cap = TOWER_AI_HOLD_BOTTOM - (int)((float)(TOWER_AI_HOLD_BOTTOM - TOWER_AI_HOLD_TOP) * t + 0.5f);
    if (out->boss) out->hold_cap--;
    if (out->hold_cap < TOWER_AI_HOLD_TOP - 1) out->hold_cap = TOWER_AI_HOLD_TOP - 1;
    if (out->hold_cap >= TOWER_AI_HOLD_BOTTOM) out->hold_cap = 0;     /* the bottom: nothing is cut */
}

int towers_ai_hold(int prev, int now, int cap)
{
    if (cap <= 0 || now <= prev || now < TOWER_AI_LONG_HOLD || now <= cap) return now;
    return cap;
}

int towers_ai_skill(const TowerAiFloor *f, int own)
{
    int s;
    if (!f->on || own < 1 || own > 100) return own;
    s = own + (int)((float)(100 - own) * f->t + 0.5f);
    if (f->boss) s += TOWER_AI_BOSS_SKILL;
    return s > 100 ? 100 : s < own ? own : s;
}

/* Each tendency the ramp moves, and the end it moves toward. Distance, attack/defend, strike/grapple, punch/kick,
 * the strong/normal mixes, shove/grapple, sticky grapple, climbing, running attacks and submissions stay the
 * fighter's own: they are its style, not its strength. */
static int target_of(const TowerAiFloor *f, int k)
{
    switch (k) {
    case TAI_COUNTER_ATTEMPT: case TAI_COUNTER_USE:     /* counters over plain blocks, and often */
    case TAI_COMBO: case TAI_BLAZIN:                    /* strings and Blazin moves */
    case TAI_ENV_GRAPPLE: case TAI_ENV_ATTACK: case TAI_WEAPON: case TAI_ENVIRONMENT:   /* walls, objects, weapons */
    case TAI_DISTANCE_USE:
        return 5;
    case TAI_GRAPPLE_DEFENCE: case TAI_STRIKE_DEFENCE:
        return f->defence;
    case TAI_APPEAL:                                    /* taunts: the fewest */
        return 1;
    }
    return 0;
}

int towers_ai_tendency(const TowerAiFloor *f, int k, int own)
{
    int target, v;
    float d;
    if (!f->on || k < 0 || k >= TOWER_AI_TENDENCIES || own < 1 || own > 5) return own;
    target = target_of(f, k);
    if (!target) return own;
    d = (float)(target - own) * f->t;
    v = own + (int)(d < 0.0f ? d - 0.5f : d + 0.5f);
    return v < 1 ? 1 : v > 5 ? 5 : v;
}

int towers_ai_adapt_on(const TowerAiFloor *f, int floor)
{
    return f->on && (f->boss || floor >= TOWER_AI_ADAPT_FLOOR);
}

void towers_ai_adapt_reset(TowerAiAdapt *a)
{
    memset(a, 0, sizeof *a);
}

int towers_ai_adapt_note(TowerAiAdapt *a, uint32_t key, uint32_t step)
{
    int i, at = -1, oldest = 0, k;
    for (i = 0; i < a->n; i++) {
        if (a->m[i].key == key) at = i;
        if (a->m[i].last < a->m[oldest].last) oldest = i;
    }
    if (at < 0) {           /* a new key: a free entry, else the one used longest ago */
        at = a->n < TOWER_AI_ADAPT_MOVES ? a->n++ : oldest;
        memset(&a->m[at], 0, sizeof a->m[at]);
        a->m[at].key = key;
    }
    /* the last uses, newest first */
    for (k = TOWER_AI_REPEAT_COUNT - 1; k > 0; k--) a->m[at].seen[k] = a->m[at].seen[k - 1];
    a->m[at].seen[0] = step;
    if (a->m[at].count < TOWER_AI_REPEAT_COUNT) a->m[at].count++;
    a->m[at].last = step;
    if (a->m[at].count >= TOWER_AI_REPEAT_COUNT &&
        step - a->m[at].seen[TOWER_AI_REPEAT_COUNT - 1] <= TOWER_AI_REPEAT_WINDOW)
        a->m[at].read_until = step + TOWER_AI_READ_STEPS;
    else if (a->m[at].read_until && step <= a->m[at].read_until)
        a->m[at].read_until = step + TOWER_AI_READ_STEPS;     /* still read: its time starts again */
    return a->m[at].read_until && step <= a->m[at].read_until;
}

int towers_ai_adapt_skill(int skill)
{
    int s = skill + (100 - skill + 1) / 2;
    return s > 100 ? 100 : s;
}

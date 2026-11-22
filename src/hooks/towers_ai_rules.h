/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Towers: the CPU past Hard, the pure part (community/tower-ai-study.md). The game's own difficulty word stays at 2
 * (Hard: a 3 would index past the {easy, normal, hard} names and load an undefined, weaker CPU); what makes the late
 * floors harder is written over the CPU slot's loaded AI data each fight step (towers_ai.c):
 *   - the skill (AI_difficulty, 1..100) from the fighter's own Hard value toward 100,
 *   - the tendencies (1..5): counter, combo, Blazin, environment and defence toward their strongest values, taunts
 *     toward the fewest,
 *   - the brain's long output holds (fresh ones of 20 steps and more) cut, down to 5 steps at the top (step 2),
 *   - an attack the climber threw three times in a short window met with the strongest counter/defence values
 *     whenever he throws it again for a while (the adaptive repeat counter, step 3),
 * All by the ABSOLUTE floor (as towers_floor_rules): floors 1..6 keep the game's own data, the ramp runs from floor 7
 * to TOWER_AI_RAMP_TOP, a boss takes the top of its floor's ramp plus TOWER_AI_BOSS_EXTRA. No game or host state:
 * compiled natively by tests/unit/test_towers_ai_rules.py. */
#ifndef DEFJAM_TOWERS_AI_RULES_H
#define DEFJAM_TOWERS_AI_RULES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOWER_AI_FIRST_FLOOR    7       /* the ramp starts with Hard (TOWER_HARD_FLOOR) */
#define TOWER_AI_RAMP_TOP       40      /* ... and is at its top here */
#define TOWER_AI_BOSS_EXTRA     0.25f   /* a boss: this much further up the ramp (at most the top) */
#define TOWER_AI_BOSS_SKILL     10      /* ... and this much more skill (at most 100) */
#define TOWER_AI_TENDENCIES     24
#define TOWER_AI_SETS           4

/* The tendency indexes (the string table at 0x2E2E98, tend*.xml order) the ramp moves. */
enum {
    TAI_DISTANCE = 0, TAI_ATTACK_VS_DEFEND = 1, TAI_COUNTER_ATTEMPT = 8, TAI_ENV_GRAPPLE = 10, TAI_ENV_ATTACK = 11,
    TAI_WEAPON = 12, TAI_COMBO = 15, TAI_GRAPPLE_DEFENCE = 16, TAI_STRIKE_DEFENCE = 17, TAI_DISTANCE_USE = 18,
    TAI_APPEAL = 19, TAI_BLAZIN = 20, TAI_ENVIRONMENT = 22, TAI_COUNTER_USE = 23
};

/* The defence proficiencies' strongest value. The weight code scales the skill by {1.5, 1.25, 1.0, 0.75, 0.5}[p-1]
 * (0x2C9094) for the defence actions; which end defends better was measured (bot against CPU, build 101, the
 * numbers in the commit): see towers_ai_rules.c. */
extern const int TOWER_AI_DEFENCE_BEST;

typedef struct TowerAiFloor {
    int on;             /* 0: the game's own AI data, untouched (floors 1..6) */
    float t;            /* how far up the ramp, 0..1 (a boss's extra included) */
    int boss;
    int defence;        /* the defence proficiencies' target (TOWER_AI_DEFENCE_BEST; tests may set another) */
    int hold_cap;       /* a fresh output hold of TOWER_AI_LONG_HOLD steps or more is cut to this (0: none) */
} TowerAiFloor;

/* The floor's settings: tower and floors only for the boss floors (towers_is_boss). */
void towers_ai_floor(int tower, int floor, int floors, TowerAiFloor *out);
/* Build 108, The Weekly's BOSS RUSH: the same with every floor a boss (force_boss 1), also below the ramp's first floor
 * (the ramp's bottom plus the boss's extra there). force_boss 0 is towers_ai_floor. */
void towers_ai_floor_ex(int tower, int floor, int floors, int force_boss, TowerAiFloor *out);
/* The skill to write for a fighter whose own (loaded Hard) skill is `own`: own -> 100 along t, + the boss's extra. */
int towers_ai_skill(const TowerAiFloor *f, int own);
/* Tendency k's value for a fighter whose own value is `own` (1..5): own -> the strongest value along t, rounded;
 * an index the ramp does not move, or an own value outside 1..5, stays as it is. */
int towers_ai_tendency(const TowerAiFloor *f, int k, int own);

/* ---- the hold cap (step 2) ---------------------------------------------------------------------------------------
 * The brain holds its output while brain+0x34 counts down and thinks again once brain+0x38 does (sub_001B0290); the
 * behaviour code writes them as literals (1, 2, 7, 10, 16, 20, 40). A fresh write (now > the value after our last
 * look) of TOWER_AI_LONG_HOLD steps or more is cut to the floor's cap: TOWER_AI_HOLD_BOTTOM at floor 7 (no cut) down
 * to TOWER_AI_HOLD_TOP at the ramp's top, a boss one step less. The short holds (an attack's own few steps) stay. */
#define TOWER_AI_LONG_HOLD      20
#define TOWER_AI_HOLD_BOTTOM    40
#define TOWER_AI_HOLD_TOP       5
/* One countdown: `prev` its value after our last look, `now` before this step. Returns the value to keep. */
int towers_ai_hold(int prev, int now, int cap);

/* ---- the adaptive repeat counter (step 3) --------------------------------------------------------------------------
 * The game has no memory of the climber's habits; this is ours. One key per attack the climber starts: the attack
 * state and the move word the game set for it, read from his fighter record on the step the state changed (what the
 * CPU sees on that step anyway; never the pad). When one key starts TOWER_AI_REPEAT_COUNT times within
 * TOWER_AI_REPEAT_WINDOW steps it is "read" until TOWER_AI_READ_STEPS after its last use: each time it is thrown in
 * that time (the third one included) the CPU meets that attack with the strongest counter and defence values and a
 * higher skill, and its output hold and think wait cut to TOWER_AI_READ_HOLD steps (towers_ai.c: what a read adds
 * on the top floors, where the values are at their strongest already), until the attack is over. Other attacks
 * keep the floor's values. A new fight starts empty. On from TOWER_AI_ADAPT_FLOOR and on every boss. */
#define TOWER_AI_REPEAT_COUNT   3
#define TOWER_AI_REPEAT_WINDOW  720     /* steps (12 s) */
#define TOWER_AI_READ_STEPS     1200    /* steps (20 s) since its last use */
#define TOWER_AI_ADAPT_MOVES    24
#define TOWER_AI_ADAPT_FLOOR    12
#define TOWER_AI_READ_HOLD      4       /* a read attack: the CPU's hold and think wait cut to this many steps */

typedef struct TowerAiAdapt {
    int n;
    struct { uint32_t key, last, read_until; uint32_t seen[TOWER_AI_REPEAT_COUNT]; int count; } m[TOWER_AI_ADAPT_MOVES];
} TowerAiAdapt;

/* Whether the counter runs on this floor. */
int towers_ai_adapt_on(const TowerAiFloor *f, int floor);
void towers_ai_adapt_reset(TowerAiAdapt *a);
/* An attack with `key` started at `step`: 1 when it is read now (this use made it read, or it was already). */
int towers_ai_adapt_note(TowerAiAdapt *a, uint32_t key, uint32_t step);
/* The skill the CPU meets a read attack with: at least half way from the floor's skill to 100. */
int towers_ai_adapt_skill(int skill);

#ifdef __cplusplus
}
#endif

#endif

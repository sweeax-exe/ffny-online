/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Towers: the CPU past Hard, the pure part (community/tower-ai-study.md). The game's own difficulty word stays at 2
 * (Hard: a 3 would index past the {easy, normal, hard} names and load an undefined, weaker CPU); what makes the late
 * floors harder is written over the CPU slot's loaded AI data each fight step (towers_ai.c):
 *   - the skill (AI_difficulty, 1..100) from the fighter's own Hard value toward 100,
 *   - the tendencies (1..5): counter, combo, Blazin, environment and defence toward their strongest values, taunts
 *     toward the fewest,
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
} TowerAiFloor;

/* The floor's settings: tower and floors only for the boss floors (towers_is_boss). */
void towers_ai_floor(int tower, int floor, int floors, TowerAiFloor *out);
/* The skill to write for a fighter whose own (loaded Hard) skill is `own`: own -> 100 along t, + the boss's extra. */
int towers_ai_skill(const TowerAiFloor *f, int own);
/* Tendency k's value for a fighter whose own value is `own` (1..5): own -> the strongest value along t, rounded;
 * an index the ramp does not move, or an own value outside 1..5, stays as it is. */
int towers_ai_tendency(const TowerAiFloor *f, int k, int own);

#ifdef __cplusplus
}
#endif

#endif

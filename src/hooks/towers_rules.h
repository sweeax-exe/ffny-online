/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Towers (T0, offline): a single-player climb, one CPU fighter per floor. The pure part: the run's seeded order of
 * opponents and venues, the difficulty curve per floor, the health rule between fights and the run file's text.
 * No game or host state: compiled natively by tests/unit/test_towers_rules.py. The game side is towers.c, the
 * screens towers_ui.inc. Design: community/tower-mode-design.md (owner's changes of 2026-11-18: a loss ends the run,
 * no continues; Five Boroughs' chapters are save points only; full health every fight, the health rule a per-tower
 * setting). */
#ifndef DEFJAM_TOWERS_RULES_H
#define DEFJAM_TOWERS_RULES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOWER_UPTOWN        1   /* every stock fighter, standard venues */
#define TOWER_FIVE_BOROUGHS 2   /* stock + the community fighters and venues this PC has, chapters of 10 floors */
#define TOWER_WEEKLY        3   /* the server's weekly event tower (T2 of the design): "coming soon" in T0 */
#define TOWER_COUNT         3

#define TOWER_MAX_ROSTER    600
#define TOWER_MAX_VENUES    64
#define TOWER_CHAPTER       10  /* Five Boroughs: a save point every 10 floors */
#define TOWER_FIRST_COMMUNITY_VENUE 28   /* venue feIDs 28..63 are community venues (ONLINE_NEW_VENUE) */

/* Between fights: full health (the default, owner 2026-11-18), the health the fight ended with, or that plus a
 * refill (percentage points of the full bar). Per tower, from [towers] health<N> / refill<N> in the settings. */
enum { TOWER_HEALTH_FULL = 0, TOWER_HEALTH_CARRY = 1, TOWER_HEALTH_REFILL = 2 };

/* One run. The order of opponents is fixed at the start (seed + roster), so a run resumed after the game closed
 * meets the same fighters on the same floors; the roster and venue lists are kept in the run for that, so packs
 * added or removed later do not change a run under way. */
typedef struct TowerRun {
    int tower;                  /* TOWER_* */
    uint32_t seed;
    int fighter, skin;          /* the climber: Battle-mode ID (99 a created fighter, 1000+ a community one), skin */
    int floors;                 /* how tall this run's tower is: the roster's size */
    int floor;                  /* the floor fought next, 1..floors; floors + 1 once the top is beaten */
    int roster[TOWER_MAX_ROSTER];   /* the opponents in climb order: roster[floor - 1] */
    int venue_n;
    int venues[TOWER_MAX_VENUES];   /* the venue pool, sorted by feID */
    uint32_t roster_hash;       /* FNV-1a over the roster sorted by ID (the same list in any order: the same hash) */
    int health_pct;             /* the climber's health going into the next fight, 1..100 (100 under the full rule) */
    uint32_t climb_ms;          /* time in fights, summed */
    int interrupted;            /* fights the game closed in (resumed on the same floor) */
    int draws;                  /* fights that ended in a draw (fought again) */
    int skipped;                /* floors whose opponent this PC no longer has (counted as passed) */
    long long started;          /* unix seconds */
} TowerRun;

/* What one floor's fight is set to (section 2.3 of the design): the CPU's game level and our own knobs past Hard. */
typedef struct TowerFloorRules {
    int cpu_level;          /* the CPU slot's difficulty word: 0 Easy, 1 Normal, 2 Hard */
    float cpu_damage;       /* damage the CPU deals, as a factor (1.00 .. 1.40) */
    float cpu_momentum;     /* the CPU's momentum at the start, a share of its bar (0 .. 0.75) */
    float cpu_health;       /* the CPU's health, as a factor (1.00 .. 1.44): the damage it takes is divided by it */
    int boss;               /* a boss floor (the top of Uptown, each chapter's 10th floor in Five Boroughs) */
} TowerFloorRules;

/* splitmix32: the generator every seeded choice uses (the server can recompute it later). */
uint32_t towers_next32(uint32_t *state);
/* The roster's hash: FNV-1a 32 over the IDs sorted ascending (4 bytes each, little endian). */
uint32_t towers_roster_hash(const int *ids, int n);
/* A new run of `tower` with seed: the opponents are pool[] (any order, duplicates and the climber dropped), sorted
 * by ID and then shuffled by the seed (Fisher-Yates); the venues are venues[] (Uptown keeps the standard ones,
 * feID < 28), sorted. floor 1, full health. Returns the floor count (0: no opponent or no venue). */
int towers_new_run(TowerRun *run, int tower, uint32_t seed, int fighter, int skin, const int *pool, int pool_n,
                   const int *venues, int venue_n);
/* Floor n's opponent (1..floors), -1 outside the run. */
int towers_opponent(const TowerRun *run, int floor);
/* Floor n's venue: drawn from the pool by the seed and the floor (the same floor, the same venue). -1 with none. */
int towers_venue(const TowerRun *run, int floor);
/* A boss floor: Uptown's last floor; every TOWER_CHAPTER-th floor of Five Boroughs. */
int towers_is_boss(int tower, int floor, int floors);
/* After floor `beaten` was won, the run is written (a save point): every floor in Uptown, at each chapter's end in
 * Five Boroughs (chapter = TOWER_CHAPTER, tests may pass less). The top floor is always one. */
int towers_is_save_point(int tower, int beaten, int floors, int chapter);
/* The floor a run resumes on when it was written after `beaten` floors: the next one. */
/* The curve: p = floor / floors (Five Boroughs: floor / min(floors, 100), endless stays at the top row). */
void towers_floor_rules(int tower, int floor, int floors, TowerFloorRules *out);
/* The climber's health going into the next fight after a win that ended with end_pct (1..100). */
int towers_health_after_win(int rule, int refill, int end_pct);

/* The run file (<data>/towers.json): {"version":1,"best":[b1,b2,b3],"runs":[{...}, ...]}, one saved run per tower
 * at most. best[t-1]: the most floors ever beaten in tower t. Write returns the text's length (0: too small). */
int towers_file_write(const TowerRun *const runs[TOWER_COUNT], const int best[TOWER_COUNT], char *out, int size);
/* Reads what towers_file_write wrote: have[t-1] = 1 for a saved run of tower t (checked: a run whose lists are
 * damaged is dropped). Returns 1 when the text was a run file (also with no runs), 0 otherwise. */
int towers_file_read(const char *text, TowerRun runs[TOWER_COUNT], int have[TOWER_COUNT], int best[TOWER_COUNT]);

/* The tower's names, for logs and screens ("UPTOWN"). */
const char *towers_name(int tower);

#ifdef __cplusplus
}
#endif

#endif

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
#define TOWER_WEEKLY        3   /* build 108: The Weekly, the account service's tower of the week (TowerWeekly below) */
/* Build 106: the Team Tower, two players on one PC (pads 0 and 1, slots 0 and 1) against CPU teams (slots 2 and 3),
 * 2 vs 2 (Game.SetMatchType(1)). Two towers so the rules never mix on a board: standard fighters and venues only, or
 * with the community fighters this PC has. Floor f's opponents are roster[2f-2] and roster[2f-1]; venues: only the
 * standard ones with a 2 vs 2 start table (towers_team_venue_ok), in both. */
#define TOWER_TEAM          4
#define TOWER_TEAM_COMMUNITY 5
#define TOWER_COUNT         5
#define TOWER_PAGE_TOWERS   4   /* the TOWERS page's cards: Uptown, Five Boroughs, the Weekly, the Team Tower (4 or 5) */
#define TOWER_ART_COUNT     3   /* towers with a rendered building (scripts/tower-art.py); the Team Tower is drawn */
#define TOWER_IS_TEAM(t)    ((t) == TOWER_TEAM || (t) == TOWER_TEAM_COMMUNITY)

#define TOWER_MAX_ROSTER    600
#define TOWER_MAX_VENUES    64
#define TOWER_CHAPTER       10  /* Five Boroughs: a save point every 10 floors */
#define TOWER_NORMAL_FLOOR  3   /* build 98: Normal from floor 3, Hard from floor 7; build 101: the ramp to floor 40 */
#define TOWER_HARD_FLOOR    7
#define TOWER_RAMP_TOP      40
#define TOWER_TOP_DAMAGE    0.60f   /* build 101: the CPU's damage at the ramp's top, 1 + this (build 98: 0.50 at 60) */
#define TOWER_TOP_HEALTH    0.75f   /* ... its health (build 98: 0.50 at 60) */
#define TOWER_MAX_MOMENTUM  0.75f /* the CPU's starting momentum, a share of its bar, boss extra included */
#define TOWER_FIRST_COMMUNITY_VENUE 28   /* venue feIDs 28..63 are community venues (ONLINE_NEW_VENUE) */
#define TOWER_RULES_VERSION 2   /* build 102: the harder towers (AI ramp, fixed bosses); T1: what the account service checks a run's reports against (towers.go "rules") */
#define TOWER_MAX_REFIGHTS  1   /* a draw is fought again once per floor; a second draw on that floor is a loss */
#define TOWER_RUN_ID_MAX    40  /* the account service's run id (16 hex digits today) */

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
    int fighter2, skin2;        /* the team towers: player 2's fighter (pad 1, slot 1) */
    int roster_n;               /* the IDs in roster[]: 2 a floor in the team towers; 0: floors (one a floor) */
    int floors;                 /* how tall this run's tower is: the roster's size (team: half of it) */
    int floor;                  /* the floor fought next, 1..floors; floors + 1 once the top is beaten */
    int roster[TOWER_MAX_ROSTER];   /* the opponents in climb order: roster[floor - 1] */
    int venue_n;
    int venues[TOWER_MAX_VENUES];   /* the venue pool, sorted by feID */
    uint32_t roster_hash;       /* FNV-1a over the roster sorted by ID (the same list in any order: the same hash) */
    int health_pct;             /* the climber's health going into the next fight, 1..100 (100 under the full rule) */
    uint32_t climb_ms;          /* time in fights, summed */
    int interrupted;            /* fights the game closed in (resumed on the same floor) */
    int in_fight;               /* the floor whose fight was under way when the file was last written (0: none) */
    int draws;                  /* fights that ended in a draw (fought again) */
    int skipped;                /* floors whose opponent this PC no longer has (counted as passed) */
    long long started;          /* unix seconds */
    /* T1: the account service's side (all 0 / "" for a run that was never on the server) */
    char run_id[TOWER_RUN_ID_MAX + 1];  /* POST /api/towers/start's run; "" an offline run (never on the boards) */
    int board;                  /* the service said this run can go on its boards (board=1) */
    int refights;               /* draws on the floor fought next (TOWER_MAX_REFIGHTS) */
    int net_next;               /* the floor the service expects next: Five Boroughs resumes at its chapter's start,
                                   so the floors below this one are fought again without reports (0: as floor) */
    /* build 108, The Weekly (tower 3): the week's twist (TOWER_TWIST_*), the ISO week ("2026-W47") and its end (unix);
     * a weekly run of another week than the service's is over (the service ended it at the week's end) */
    int twist;
    char week[12];
    long long week_end;
} TowerRun;

/* What one floor's fight is set to (section 2.3 of the design): the CPU's game level and our own knobs past Hard. */
typedef struct TowerFloorRules {
    int cpu_level;          /* the CPU slot's difficulty word: 0 Easy, 1 Normal, 2 Hard */
    float cpu_damage;       /* damage the CPU deals, as a factor (1.00 .. 1.70) */
    float cpu_momentum;     /* the CPU's momentum at the start, a share of its bar (0 .. 0.75) */
    float cpu_health;       /* the CPU's health, as a factor (1.00 .. 2.1875): the damage it takes is divided by it */
    int boss;               /* a boss floor (the top of Uptown, each chapter's 10th floor in Five Boroughs) */
    /* build 108, The Weekly's twists (towers_run_floor_rules; 1 / 0 in every other tower) */
    float hit_scale;        /* double_damage: every hit, both ways, times this (on top of cpu_damage / cpu_health) */
    float max_health;       /* glass_jaw: the climber starts at this share of its health, capped there (the CPU full) */
    int momentum_hold;      /* cpu_momentum: the CPU's momentum meter (+0x364) held full, Blazin' ready */
    int carry_health;       /* low_health: the climber starts with the health the last win ended with */
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
/* Build 106: a new team run (TOWER_TEAM or TOWER_TEAM_COMMUNITY): both players' fighters dropped from the pool, which
 * is sorted, shuffled as towers_new_run's and cut to an even count; floors = half of it. Venues: towers_team_venue_ok.
 * The hash is over the cut roster. Returns the floor count (0: fewer than two opponents or no venue). */
int towers_new_team_run(TowerRun *run, int tower, uint32_t seed, int fighter, int skin, int fighter2, int skin2,
                        const int *pool, int pool_n, const int *venues, int venue_n);
/* A team tower's venue: a standard venue with a 2 vs 2 start table, never The Pit (19) nor the one-on-one-only 17, 24,
 * 26, nor a community venue (a new venue may keep its template's one-on-one table). */
int towers_team_venue_ok(int tower, int fe);
/* The IDs in run->roster (roster_n, or floors). */
int towers_roster_len(const TowerRun *run);
/* Floor n's opponent (1..floors), -1 outside the run; the team towers: the first CPU (slot 2). */
int towers_opponent(const TowerRun *run, int floor);
/* The team towers: floor n's second CPU (slot 3); -1 in the other towers. */
int towers_opponent2(const TowerRun *run, int floor);
/* Floor n's venue: drawn from the pool by the seed and the floor (the same floor, the same venue). -1 with none. */
int towers_venue(const TowerRun *run, int floor);
/* A boss floor: Uptown's last floor; every TOWER_CHAPTER-th floor of Five Boroughs; the team towers: every
 * TOWER_CHAPTER-th floor and the top one. */
int towers_is_boss(int tower, int floor, int floors);
/* Build 101: the bosses are fixed, as in the story (and the towers of fighting games): after the shuffle, Uptown's
 * last floor gets Crow (58, the story's final boss; a climb with Crow himself the strongest other story boss), and
 * Five Boroughs' last chapter's 10th floor too, its earlier chapters' 10th floors the story's bosses in order (D-Mob,
 * Sticky, Blaze, D-Mob 2, Sticky 2, Blaze 2, D-Mob 3, Blaze 3; the chapters past the list keep their drawn boss).
 * Each is swapped in from its own floor, so the roster (and its hash) stays the same set. Only a boss the roster has
 * is placed. Returns the bosses placed. The account service computes the order itself (towers.go TowerOrder) and
 * freezes a run that meets another opponent: towers.c places them only for an offline run, or when the service
 * says it does the same (its start answer's bosses=1).
 * Build 106, the team towers: boss pairs, the top floor Crow + Blaze 3 (else the next of 84, 81, 83, 56, 59, 57, 80
 * the roster has), floors 10, 20 and 30 (below the top) D-Mob + Sticky, Blaze + D-Mob 2, Sticky 2 + Blaze 2 (a member
 * the roster lacks or the top pair took is left as drawn); server TowerPlaceTeamBosses does the same. */
int towers_place_bosses(TowerRun *run);
/* After floor `beaten` was won, the run is written (a save point): every floor in Uptown, at each chapter's end in
 * Five Boroughs (chapter = TOWER_CHAPTER, tests may pass less). The top floor is always one. */
int towers_is_save_point(int tower, int beaten, int floors, int chapter);
/* The floor a run resumes on when it was written after `beaten` floors: the next one. */
/* The curve: p = floor / floors (Five Boroughs: floor / min(floors, 100), endless stays at the top row). */
void towers_floor_rules(int tower, int floor, int floors, TowerFloorRules *out);

/* Build 98 (owner): who may start a NEW climb of the tower. Uptown (and The Weekly, build 108: the service takes stock
 * climbers only on its board, "created") takes standard fighters only (stock IDs 0..98; never
 * the created fighter 99, a new fighter's place (new_place) or a numbered community / own fighter); Five Boroughs any.
 * A saved Uptown run with a created fighter still continues (off the board, as the service says). */
#define TOWER_CREATED_FIGHTER 99
int towers_climber_ok(int tower, int fighter, int new_place);
/* After a draw on a floor that had `refights` draws before it: 1 the floor is fought again (refights + 1 is then the
 * floor's count), 0 the draw counts as a loss (the run ends). */
int towers_draw_refights(int refights);
/* A fight that was under way when the run file was last written (the game closed, crashed or was ended in the
 * fight) is a loss on that floor at the next start: 1 when `run` holds such a fight (run->in_fight), and then the
 * floors it beat (in_fight - 1) through *beaten. Quitting mid-fight never rerolls a floor. */
int towers_pending_is_loss(const TowerRun *run, int *beaten);
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

/* ---------------------------------------------------------------- The Weekly (tower 3, build 108)
 * The account service's tower of the week (server relay/accounts/towers_weekly.go): every ISO week (Monday 00:00 UTC
 * to the next) one tower of 20 floors with one twist, the same for everyone: a fixed seed, a fixed order of opponents
 * (the spare stands in on the floor of the climber's own fighter), a fixed venue list (one with Home Turf), Crow on
 * the top floor. GET /api/towers/weekly?format=text gives it (404 while the service's switch is off: no Weekly). The
 * twists and exactly what the game does for them (weekly rules version TOWER_WEEKLY_RULES, the service's
 * WeeklyRulesVersion; the start sends it, another version climbs off the board): */
#define TOWER_WEEKLY_RULES      2   /* 2 (owner 2026-11-22): GLASS JAW, the climber on half health only */
#define TOWER_WEEKLY_MAX_FLOORS 40
enum {
    TOWER_TWIST_NONE = 0,       /* unknown to this build: the Weekly cannot be climbed (a newer game knows it) */
    TOWER_TWIST_LOW_HEALTH,     /* IRON MAN: from floor 2 the climber starts with the health the last win ended with
                                   (regeneration's ceiling too: no refill); the CPU always starts full */
    TOWER_TWIST_DOUBLE_DAMAGE,  /* HEAVY HANDS: every hit does twice the damage, both ways, on top of the ramp */
    TOWER_TWIST_CPU_MOMENTUM,   /* BLAZIN' HOUR: the CPU's momentum meter held full (Blazin' ready) from 5 s of
                                   fighting on (one given in the walk-in stopped the match: cheats.c max_blazin) */
    TOWER_TWIST_GLASS_JAW,      /* GLASS JAW: the climber fights on half health, never healed above it; the CPU's full */
    TOWER_TWIST_ALL_BOSSES,     /* BOSS RUSH: every floor a boss floor (banner, name tag, boss AI and extras) */
    TOWER_TWIST_ONE_VENUE,      /* HOME TURF: all floors at the week's one venue (the service's list has one) */
    TOWER_TWIST_COUNT
};

typedef struct TowerWeekly {
    int valid;                  /* enabled=1 and a complete tower */
    char week[12];              /* "2026-W47" */
    char name[96];              /* "The Weekly 2026-W47: HEAVY HANDS" */
    char twist_key[24];         /* the service's twist id ("double_damage") */
    char title[32];             /* its title ("HEAVY HANDS") */
    int twist;                  /* TOWER_TWIST_*: 0 a twist this build does not know */
    int rules;                  /* the service's weekly rules version */
    uint32_t seed;
    int floors;
    long long starts, ends, now;    /* unix seconds; now: the service's clock at the answer */
    uint32_t roster_hash;       /* the service's roster= (the order and the spare) */
    int spare;                  /* fights the floor whose opponent is the climber's own fighter (-1: none) */
    int venue_n;
    int venues[TOWER_MAX_VENUES];   /* sorted */
    int opponent[TOWER_WEEKLY_MAX_FLOORS], venue[TOWER_WEEKLY_MAX_FLOORS], level[TOWER_WEEKLY_MAX_FLOORS],
        boss[TOWER_WEEKLY_MAX_FLOORS];  /* floor f: [f - 1], the week's own order (before the spare) */
} TowerWeekly;

/* Reads WeeklyText's key=value lines: 1 when it is a complete tower (enabled=1, the week, floors 1..40 with every
 * floor line in order, venues). */
int towers_weekly_parse(const char *text, TowerWeekly *out);
/* A twist's TOWER_TWIST_* from the service's id (0: unknown) and back ("" for 0). */
int towers_twist_id(const char *key);
const char *towers_twist_key(int twist);
/* The week's twist in plain words for the screens ("Every hit does double damage, yours and the CPU's."). */
const char *towers_twist_text(int twist);
/* A new Weekly run for the climber: the week's seed, its order (the spare on the climber's own floor, never shuffled:
 * the service holds this order), its venues; floor 1, full health, the twist and the week kept. Returns the floors
 * (0: no tower, or a twist or rules version this build cannot play). */
int towers_new_weekly_run(TowerRun *run, const TowerWeekly *w, int fighter, int skin);
/* Floor n of a run is a boss floor (towers_is_boss, and every floor of a BOSS RUSH Weekly). */
int towers_run_is_boss(const TowerRun *run, int floor);
/* A run's floor rules: towers_floor_rules, then the Weekly's twist (all_bosses: the boss extras on every floor). */
void towers_run_floor_rules(const TowerRun *run, int floor, TowerFloorRules *out);
/* The ISO week ("2026-W47") of a unix time (UTC), for the last week's board. */
void towers_iso_week(long long unix_secs, char *out, int size);

#ifdef __cplusplus
}
#endif

#endif

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Towers (T0): the game side. See towers.h and community/tower-mode-design.md. */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "recomp/gen/recomp_types.h"
#include "recomp_settings.h"
#include "towers.h"
#include "automatch.h"
#include "fighter_banks.h"
#include "netplay.h"
#include "new_fighter_places.h"
#include "online_data.h"
#include "pc_settings.h"

#define ACTORS          0x003B92E0u     /* the four fighter records (openworld.c, test_control.c) */
#define STRIDE          0x12E8u
#define F_SLOT          0x28u           /* 0xFFFFFFFF: no fighter in this record */
#define F_HEALTH_NOW    0x350u
#define F_HEALTH_TOP    0x354u
#define F_HEALTH_SHARE  0x35Cu
#define F_MOMENTUM      0x374u
#define F_MOMENTUM_TOP  0x378u
#define G_SLOT_LEVEL    0x003B90A8u     /* each fighter slot's difficulty word (netplay.c cpu_level_apply) */
#define G_PHASE         0x003B8F30u     /* the match's phase: 1 fighting, 2 decided, 3 finished */
#define G_WINNER        0x003B8F40u     /* the winner's slot (phase 3) */
#define G_WINNER_FIRST  0x003B8F3Cu     /* the winner slot the decision writes first (phase 2) */
#define G_MATCH_FLAGS   0x003B9054u     /* bit 0x08: the match is over */
#define G_RESULTS       0x003BE130u     /* the result history: 16-byte records, u16 code at +0xE */
#define G_RESULT_COUNT  0x003BE624u
#define RESULTS_MAX     ((G_RESULT_COUNT - G_RESULTS) / 0x10u)
#define PLAYER_LEVEL    1u              /* the climber's own slot word: Normal (the momentum scale reads slot 0's) */

static CRITICAL_SECTION g_cs;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static TowerRun g_run;                  /* the run under way (or just ended) */
static int g_state = TOWERS_IDLE;
static unsigned long long g_since;
static int g_window_due;
static int g_last_floor, g_last_opp = -1, g_last_venue = -1, g_save_point, g_left, g_new_best, g_best_before;
static TowerFloorRules g_rules;         /* the fight's */
static int g_match_seen, g_decided, g_winner, g_draw, g_end_health;
static unsigned g_step;
static unsigned long long g_fight_t0;
static int g_momentum_done;

/* the file */
static TowerRun g_saved[TOWER_COUNT];
static int g_have[TOWER_COUNT], g_best[TOWER_COUNT], g_loaded;

static BOOL CALLBACK init_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_cs);
    return TRUE;
}
static void lock(void) { InitOnceExecuteOnce(&g_once, init_once, NULL, NULL); EnterCriticalSection(&g_cs); }
static void unlock(void) { LeaveCriticalSection(&g_cs); }

static int env_int(const char *name, int dflt)
{
    const char *v = getenv(name);
    return v && *v ? atoi(v) : dflt;
}

static void set_state(int s)
{
    g_state = s;
    g_since = GetTickCount64();
}

/* ---------------------------------------------------------------- the run file (<data>/towers.json) */

static int file_path(char *out, size_t size)
{
    const char *ini = pc_settings_path(), *slash;
    const char *test = getenv("RECOMP_TOWERS_FILE");
    if (test && *test) { snprintf(out, size, "%s", test); return 1; }
    if (!ini || !*ini) return 0;
    slash = strrchr(ini, '\\');
    if (!slash) slash = strrchr(ini, '/');
    if (!slash) { snprintf(out, size, "towers.json"); return 1; }
    snprintf(out, size, "%.*s" HOST_SEP "towers.json", (int)(slash - ini), ini);
    return 1;
}

static void file_load(void)
{
    char path[1024];
    FILE *f;
    static char text[TOWER_COUNT * (TOWER_MAX_ROSTER * 8 + 2048) + 256];
    size_t n;
    if (g_loaded) return;
    g_loaded = 1;
    memset(g_have, 0, sizeof g_have);
    memset(g_best, 0, sizeof g_best);
    if (!file_path(path, sizeof path) || !(f = fopen(path, "rb"))) return;
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    if (!towers_file_read(text, g_saved, g_have, g_best))
        fprintf(stderr, "[TOWERS] %s is not a run file: ignored\n", path);
    else
        fprintf(stderr, "[TOWERS] run file: best %d / %d, saved runs: Uptown %s, Five Boroughs %s\n", g_best[0],
                g_best[1], g_have[0] ? "yes" : "no", g_have[1] ? "yes" : "no");
}

static void file_store(void)
{
    char path[1024], tmp[1100];
    static char text[TOWER_COUNT * (TOWER_MAX_ROSTER * 8 + 2048) + 256];
    const TowerRun *runs[TOWER_COUNT];
    FILE *f;
    int t, n;
    for (t = 0; t < TOWER_COUNT; t++) runs[t] = g_have[t] ? &g_saved[t] : NULL;
    n = towers_file_write(runs, g_best, text, (int)sizeof text);
    if (!n || !file_path(path, sizeof path)) return;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (!(f = fopen(tmp, "wb"))) { fprintf(stderr, "[TOWERS] cannot write %s\n", tmp); return; }
    fwrite(text, 1, (size_t)n, f);
    fclose(f);
#if defined(_WIN32)
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) remove(tmp);
#else
    if (rename(tmp, path)) remove(tmp);
#endif
}

static void save_run(void)
{
    file_load();
    g_saved[g_run.tower - 1] = g_run;
    g_have[g_run.tower - 1] = 1;
    file_store();
    fprintf(stderr, "[TOWERS] run saved: %s, floor %d of %d\n", towers_name(g_run.tower), g_run.floor, g_run.floors);
}

static void drop_saved(int tower)
{
    file_load();
    if (tower < 1 || tower > TOWER_COUNT) return;
    g_have[tower - 1] = 0;
    file_store();
}

int towers_saved(int tower, TowerRun *out)
{
    int have;
    if (tower < 1 || tower > TOWER_COUNT) return 0;
    lock();
    file_load();
    have = g_have[tower - 1];
    if (have && out) *out = g_saved[tower - 1];
    unlock();
    return have;
}

void towers_forget(int tower)
{
    lock();
    drop_saved(tower);
    unlock();
}

int towers_best(int tower)
{
    int b;
    if (tower < 1 || tower > TOWER_COUNT) return 0;
    lock();
    file_load();
    b = g_best[tower - 1];
    unlock();
    return b;
}

/* ---------------------------------------------------------------- the roster */

static int pool_of(int tower, int *pool, int max)
{
    const OnlineFighter *list;
    int n = online_fighters(&list), i, m = 0;
    for (i = 0; i < n && m < max; i++) pool[m++] = list[i].id;
    if (tower == TOWER_FIVE_BOROUGHS) {
        n = online_new_fighters(&list);
        for (i = 0; i < n && m < max; i++) pool[m++] = list[i].id;
        if (fighter_banks_ready())
            for (i = 0; i < fighter_banks_count() && m < max; i++) {
                const FighterBanksEntry *e = fighter_banks_at(i);
                if (e) pool[m++] = e->id;
            }
    }
    return m;
}

static int venues_of(int *venues, int max)
{
    const OnlineVenue *list;
    int n = online_venues(&list), i, m = 0;
    for (i = 0; i < n && m < max; i++)
        if (list[i].fe_id >= 1 && list[i].fe_id <= 63) venues[m++] = list[i].fe_id;
    return m;
}

int towers_floors_now(int tower)
{
    static int pool[TOWER_MAX_ROSTER];
    int n = pool_of(tower, pool, TOWER_MAX_ROSTER);
    return n > 1 ? n - 1 : 0;      /* the climber's own pick is never an opponent */
}

/* This PC can set the fighter up: a stock fighter's row, a new fighter's place, a community fighter's bank record. */
static int fighter_here(int id)
{
    if (id >= FB_FIRST_LOGICAL) return fighter_banks_find(id) != NULL;
    if (new_fighter_is_place(id)) {
        const OnlineFighter *nf;
        int n = online_new_fighters(&nf), i;
        for (i = 0; i < n; i++) if (nf[i].id == id) return 1;
        return 0;
    }
    return online_fighter_has_row(id);
}

/* ---------------------------------------------------------------- the fights */

static void test_overrides(void)
{
    /* tests: RECOMP_TEST_TOWERS_FLOORS=<n> makes the tower n floors tall (the top within reach of a short test);
     * RECOMP_TEST_TOWERS_OPPONENT=<floor>:<id> puts fighter id on that floor (swapped with its own floor) */
    const char *v = getenv("RECOMP_TEST_TOWERS_OPPONENT");
    int cap = env_int("RECOMP_TEST_TOWERS_FLOORS", 0);
    if (v && strchr(v, ':')) {
        int floor = atoi(v), id = atoi(strchr(v, ':') + 1), i, at = -1;
        if (!strcmp(strchr(v, ':') + 1, "community"))     /* the first community fighter beyond the places */
            id = fighter_banks_ready() && fighter_banks_count() > 0 ? fighter_banks_at(0)->id : -1;
        for (i = 0; i < g_run.floors; i++) if (g_run.roster[i] == id) at = i;
        if (floor >= 1 && floor <= g_run.floors && id >= 0) {
            if (at >= 0) { g_run.roster[at] = g_run.roster[floor - 1]; }
            else if (g_run.floors < TOWER_MAX_ROSTER) { g_run.roster[g_run.floors++] = g_run.roster[floor - 1]; }
            g_run.roster[floor - 1] = id;
            g_run.roster_hash = towers_roster_hash(g_run.roster, g_run.floors);
            fprintf(stderr, "[TOWERS] test: fighter %d on floor %d\n", id, floor);
        }
    }
    if (cap > 0 && cap < g_run.floors) {
        g_run.floors = cap;
        g_run.roster_hash = towers_roster_hash(g_run.roster, g_run.floors);
        fprintf(stderr, "[TOWERS] test: the tower is %d floors tall\n", cap);
    }
}

/* The floor's fight: skip floors whose fighter this PC cannot set up, then ask automatch for it. */
static int arm_fight(void)
{
    AutoMatch am;
    int opp, venue;
    while (g_run.floor <= g_run.floors && !fighter_here(towers_opponent(&g_run, g_run.floor))) {
        fprintf(stderr, "[TOWERS] floor %d: fighter %d is not on this PC: passed\n", g_run.floor,
                towers_opponent(&g_run, g_run.floor));
        g_run.skipped++;
        g_run.floor++;
    }
    if (g_run.floor > g_run.floors) return 0;
    opp = towers_opponent(&g_run, g_run.floor);
    venue = towers_venue(&g_run, g_run.floor);
    memset(&am, 0, sizeof am);
    am.type = 0;
    am.venue = venue;
    am.count = 2;
    am.fighter[0] = g_run.fighter;
    am.pad[0] = 0;
    am.skin[0] = g_run.skin;
    am.fighter[1] = opp;
    am.pad[1] = am.pad[2] = am.pad[3] = -1;
    towers_floor_rules(g_run.tower, g_run.floor, g_run.floors, &g_rules);
    g_match_seen = g_decided = g_draw = 0;
    g_winner = -1;
    g_step = 0;
    g_momentum_done = 0;
    set_state(TOWERS_ARMED);
    automatch_request(&am);
    fprintf(stderr, "[TOWERS] %s floor %d of %d: fighter %d against %d at venue %d (CPU level %d, damage x%.2f, "
            "momentum %.0f%%, health x%.2f%s)\n", towers_name(g_run.tower), g_run.floor, g_run.floors, g_run.fighter,
            opp, venue, g_rules.cpu_level, g_rules.cpu_damage, g_rules.cpu_momentum * 100.0f, g_rules.cpu_health,
            g_rules.boss ? ", boss floor" : "");
    return 1;
}

static uint32_t new_seed(void)
{
    LARGE_INTEGER q;
    uint32_t s;
    int test = env_int("RECOMP_TEST_TOWERS_SEED", -1);
    if (test >= 0) return (uint32_t)test;
    QueryPerformanceCounter(&q);
    s = (uint32_t)q.QuadPart ^ (uint32_t)(q.QuadPart >> 32) ^ (uint32_t)time(NULL) * 2654435761u;
    return towers_next32(&s);
}

int towers_start(int tower, int fighter, int skin)
{
    static int pool[TOWER_MAX_ROSTER];
    int venues[TOWER_MAX_VENUES], n, vn, ok;
    if (netplay_active() || (tower != TOWER_UPTOWN && tower != TOWER_FIVE_BOROUGHS)) return 0;
    n = pool_of(tower, pool, TOWER_MAX_ROSTER);
    vn = venues_of(venues, TOWER_MAX_VENUES);
    lock();
    if (!towers_new_run(&g_run, tower, new_seed(), fighter, skin, pool, n, venues, vn)) {
        unlock();
        fprintf(stderr, "[TOWERS] %s: no run (%d fighters, %d venues read)\n", towers_name(tower), n, vn);
        return 0;
    }
    g_run.started = (long long)time(NULL);
    test_overrides();
    file_load();
    g_best_before = g_best[tower - 1];
    g_new_best = 0;
    g_last_floor = 0;
    g_last_opp = g_last_venue = -1;
    fprintf(stderr, "[TOWERS] run start: %s, seed %08X, fighter %d (skin %d), %d floors, %d venues, roster %08X\n",
            towers_name(tower), g_run.seed, fighter, skin, g_run.floors, g_run.venue_n, g_run.roster_hash);
    save_run();         /* a run is resumable from its first floor */
    ok = arm_fight();
    g_window_due = ok;  /* the window holds the input over the load, the first floor's card in it */
    unlock();
    return ok;
}

int towers_resume(int tower)
{
    int ok;
    if (netplay_active() || tower < 1 || tower > TOWER_COUNT) return 0;
    lock();
    file_load();
    if (!g_have[tower - 1]) { unlock(); return 0; }
    g_run = g_saved[tower - 1];
    if (g_run.in_fight) {
        /* T0 is lenient: the floor is fought again and the run notes it (the boards of T1 decide what it costs) */
        g_run.interrupted++;
        fprintf(stderr, "[TOWERS] the game closed during floor %d's fight: fought again (interrupted %d)\n",
                g_run.in_fight, g_run.interrupted);
        g_run.in_fight = 0;
        g_saved[tower - 1] = g_run;
        file_store();
    }
    g_best_before = g_best[tower - 1];
    g_new_best = 0;
    g_last_floor = 0;
    g_last_opp = g_last_venue = -1;
    fprintf(stderr, "[TOWERS] run resumed: %s, seed %08X, floor %d of %d\n", towers_name(tower), g_run.seed,
            g_run.floor, g_run.floors);
    ok = arm_fight();
    g_window_due = ok;
    unlock();
    return ok;
}

/* The fight is decided: who won, from the phase and result words (as netplay.c reads them). 1 when known. */
static int read_decision(int *winner, int *draw)
{
    uint32_t phase = MEM32(G_PHASE), count = MEM32(G_RESULT_COUNT), code = 0, w = 0xFFFFFFFFu;
    if (count && count <= RESULTS_MAX) code = MEM16(G_RESULTS + (count - 1u) * 0x10u + 0xEu);
    *draw = 0;
    if ((code & 1u) && (code & 4u)) { *draw = 1; *winner = -1; return 1; }
    if (phase == 3u) w = MEM32(G_WINNER);
    else if (phase == 2u || ((code & 1u) && (MEM32(G_MATCH_FLAGS) & 8u))) w = MEM32(G_WINNER_FIRST);
    else return 0;
    if (w > 1u) { *draw = 1; *winner = -1; return 1; }
    *winner = (int)w;
    return 1;
}

void towers_match_start(void)
{
    if (netplay_active()) return;
    lock();
    if (g_state == TOWERS_ARMED || (g_state == TOWERS_FIGHTING && g_decided)) {
        if (g_state == TOWERS_FIGHTING)     /* a second match of the same set-up (a rematch the menu ran) */
            fprintf(stderr, "[TOWERS] the floor's fight runs again\n");
        set_state(TOWERS_FIGHTING);
        g_match_seen = 1;
        g_decided = g_draw = 0;
        g_winner = -1;
        g_step = 0;
        g_momentum_done = 0;
        g_fight_t0 = GetTickCount64();
        /* the CPU's level for this floor and the climber's fixed one, after the set-up copied the words and before
         * the CPU brains load (netplay.c cpu_level_apply) */
        if (g_have[g_run.tower - 1] && g_saved[g_run.tower - 1].seed == g_run.seed) {
            g_saved[g_run.tower - 1].in_fight = g_run.floor;     /* cleared when the fight counts */
            file_store();
        }
        MEM32(G_SLOT_LEVEL) = PLAYER_LEVEL;
        MEM32(G_SLOT_LEVEL + 4u) = (uint32_t)g_rules.cpu_level;
        fprintf(stderr, "[TOWERS] floor %d set up: slot levels %u %u\n", g_run.floor, MEM32(G_SLOT_LEVEL),
                MEM32(G_SLOT_LEVEL + 4u));
    }
    unlock();
}

void towers_step(void)
{
    uint32_t me = ACTORS, cpu = ACTORS + STRIDE;
    if (g_state != TOWERS_FIGHTING || netplay_active() || netplay_rerunning()) return;
    lock();
    if (g_state != TOWERS_FIGHTING) { unlock(); return; }
    g_step++;
    if (g_step == 1 && g_run.health_pct < 100 && MEM32(me + F_SLOT) != 0xFFFFFFFFu) {
        float top = MEMF(me + F_HEALTH_TOP);
        if (top > 0.0f) {
            MEMF(me + F_HEALTH_NOW) = top * (float)g_run.health_pct / 100.0f;
            MEMF(me + F_HEALTH_SHARE) = (float)g_run.health_pct / 100.0f;
            fprintf(stderr, "[TOWERS] the climber starts at %d%% health\n", g_run.health_pct);
        }
    }
    if (!g_momentum_done && g_step <= 60 && MEM32(cpu + F_SLOT) != 0xFFFFFFFFu) {
        float top = MEMF(cpu + F_MOMENTUM_TOP);
        if (top > 0.0f) {
            g_momentum_done = 1;
            if (g_rules.cpu_momentum > 0.0f) {
                MEMF(cpu + F_MOMENTUM) = top * g_rules.cpu_momentum;
                fprintf(stderr, "[TOWERS] the CPU starts with %.0f%% momentum (%.1f of %.1f)\n",
                        g_rules.cpu_momentum * 100.0f, (double)MEMF(cpu + F_MOMENTUM), (double)top);
            }
        }
    }
    if (!g_decided) {
        int w, d;
        if (read_decision(&w, &d)) {
            float top = MEMF(me + F_HEALTH_TOP), now = MEMF(me + F_HEALTH_NOW);
            g_decided = 1;
            g_winner = w;
            g_draw = d;
            g_end_health = top > 0.0f ? (int)(100.0f * now / top + 0.5f) : 100;
            fprintf(stderr, "[TOWERS] floor %d decided at step %u: %s; the climber's health %d%%\n", g_run.floor,
                    g_step, d ? "a draw" : w == 0 ? "the climber won" : "the CPU won", g_end_health);
        }
    }
    unlock();
}

void towers_health_change(void)
{
    uint32_t r;
    float delta;
    if (g_state != TOWERS_FIGHTING || netplay_active()) return;
    r = MEM32(g_esp + 4);
    delta = MEMF(g_esp + 8);
    if (delta >= 0.0f) return;
    if (r == ACTORS && g_rules.cpu_damage != 1.0f) MEMF(g_esp + 8) = delta * g_rules.cpu_damage;
    else if (r == ACTORS + STRIDE && g_rules.cpu_health > 1.0f) MEMF(g_esp + 8) = delta / g_rules.cpu_health;
}

/* Back on a menu after the floor's fight: the result counts. */
static void finish_fight(void)
{
    int w = g_winner, d = g_draw, beaten = g_run.floor;
    unsigned long long ms = GetTickCount64() - g_fight_t0;
    if (!g_decided && read_decision(&w, &d)) g_decided = 1;
    g_last_floor = g_run.floor;
    g_last_opp = towers_opponent(&g_run, g_run.floor);
    g_last_venue = towers_venue(&g_run, g_run.floor);
    g_run.climb_ms += (uint32_t)(ms > 3600000ull ? 3600000ull : ms);
    g_left = !g_decided;
    g_save_point = 0;
    g_run.in_fight = 0;
    file_load();
    if (g_have[g_run.tower - 1]) g_saved[g_run.tower - 1].in_fight = 0;     /* the fight counted */
    if (g_decided && d) {
        g_run.draws++;
        file_store();
        set_state(TOWERS_DRAW);
        fprintf(stderr, "[TOWERS] floor %d: a draw, fought again\n", beaten);
    } else if (g_decided && w == 0) {
        int rule = recomp_settings_get("towers", g_run.tower == 1 ? "health1" : "health2", TOWER_HEALTH_FULL);
        int refill = recomp_settings_get("towers", g_run.tower == 1 ? "refill1" : "refill2", 30);
        int chapter = env_int("RECOMP_TEST_TOWERS_CHAPTER", TOWER_CHAPTER);
        file_load();
        if (beaten > g_best[g_run.tower - 1]) {
            g_best[g_run.tower - 1] = beaten;
            g_new_best = beaten > g_best_before;
        }
        g_run.health_pct = towers_health_after_win(rule, refill, g_end_health);
        g_run.floor++;
        if (g_run.floor > g_run.floors) {
            g_have[g_run.tower - 1] = 0;
            file_store();
            set_state(TOWERS_CLEARED);
            fprintf(stderr, "[TOWERS] %s cleared: %d floors in %.1f min\n", towers_name(g_run.tower), g_run.floors,
                    g_run.climb_ms / 60000.0);
        } else {
            g_save_point = towers_is_save_point(g_run.tower, beaten, g_run.floors, chapter);
            if (g_save_point) save_run();
            else file_store();      /* the best floor */
            set_state(TOWERS_WON);
            fprintf(stderr, "[TOWERS] floor %d won (%.1f s, health %d%%%s); next floor %d\n", beaten, ms / 1000.0,
                    g_end_health, g_save_point ? ", a save point" : "", g_run.floor);
        }
    } else {
        file_load();
        g_have[g_run.tower - 1] = 0;
        file_store();
        set_state(TOWERS_LOST);
        fprintf(stderr, "[TOWERS] floor %d %s: the run ends with %d floors beaten\n", beaten,
                g_left ? "left during the fight" : "lost", beaten - 1);
    }
    g_window_due = 1;
}

static void test_start(void)
{
    /* tests: RECOMP_TEST_TOWERS="<tower>,<fighter>[,<skin>]" starts a run at the first settled main menu,
     * "<tower>,resume" resumes the saved one */
    static int done;
    const char *v = getenv("RECOMP_TEST_TOWERS");
    int tower, fighter = 55, skin = 0;
    if (done || !v || !*v) return;
    done = 1;
    tower = atoi(v);
    if (strstr(v, "resume")) { if (!towers_resume(tower)) fprintf(stderr, "[TOWERS] test: no saved run to resume\n"); return; }
    if (strchr(v, ',')) fighter = atoi(strchr(v, ',') + 1);
    if (strchr(v, ',') && strchr(strchr(v, ',') + 1, ',')) skin = atoi(strchr(strchr(v, ',') + 1, ',') + 1);
    if (!towers_start(tower, fighter, skin)) fprintf(stderr, "[TOWERS] test: the run did not start\n");
}

void towers_on_funccall(const char *text)
{
    int menu;
    if (netplay_active()) return;
    menu = strstr(text, "GetScreenInfo(battle/cmtype") != NULL || strstr(text, "GetScreenInfo(intMain/mainMenu.swf") != NULL;
    if (!menu) return;
    if (strstr(text, "mainMenu") && !automatch_pending()) test_start();
    lock();
    if (g_state == TOWERS_FIGHTING && g_match_seen) finish_fight();
    unlock();
}

int towers_rematch_request(void)
{
    int took = 0;
    if (netplay_active()) return 0;
    lock();
    if (g_state == TOWERS_FIGHTING && g_match_seen) {
        unsigned saved;
        took = 1;
        unlock();
        fprintf(stderr, "[TOWERS] Rematch on the results goes on as No (the tower decides the next fight)\n");
        saved = defjam_fe_call("MemCard.QuerySave( battle/rewardun.swf )");
        fprintf(stderr, "[TOWERS] MemCard.QuerySave -> %08X; Game.Quit()\n", saved);
        defjam_fe_call("Game.Quit()");
        return took;
    }
    unlock();
    return took;
}

/* ---------------------------------------------------------------- the screens' side */

void towers_view(TowersView *out)
{
    memset(out, 0, sizeof *out);
    lock();
    out->state = g_state;
    out->tower = g_run.tower;
    out->floors = g_run.floors;
    out->floor = g_run.floor;
    out->last_floor = g_last_floor;
    out->last_opponent = g_last_opp;
    out->last_venue = g_last_venue;
    out->next_opponent = towers_opponent(&g_run, g_run.floor);
    out->next_venue = g_run.floor <= g_run.floors ? towers_venue(&g_run, g_run.floor) : -1;
    if (g_run.floor <= g_run.floors) towers_floor_rules(g_run.tower, g_run.floor, g_run.floors, &out->next_rules);
    out->save_point = g_save_point;
    out->left_fight = g_left;
    file_load();
    out->best = g_run.tower >= 1 && g_run.tower <= TOWER_COUNT ? g_best[g_run.tower - 1] : 0;
    out->new_best = g_new_best;
    out->seed = g_run.seed;
    out->climb_ms = g_run.climb_ms;
    out->fighter = g_run.fighter;
    out->skin = g_run.skin;
    out->since = g_since;
    unlock();
}

const TowerRun *towers_run(void) { return &g_run; }
int towers_active(void) { return g_state != TOWERS_IDLE; }
int towers_state(void) { return g_state; }

int towers_window_take(void)
{
    int due;
    lock();
    due = g_window_due && (g_state == TOWERS_ARMED || g_state == TOWERS_WON || g_state == TOWERS_DRAW ||
                           g_state == TOWERS_LOST || g_state == TOWERS_CLEARED);
    if (due) g_window_due = 0;
    unlock();
    return due;
}

int towers_climb_on(void)
{
    int ok = 0;
    lock();
    if (g_state == TOWERS_WON || g_state == TOWERS_DRAW) ok = arm_fight();
    if (!ok && g_state != TOWERS_ARMED) set_state(TOWERS_IDLE);
    unlock();
    return ok;
}

void towers_save_quit(void)
{
    lock();
    if (g_state == TOWERS_WON && g_save_point)
        fprintf(stderr, "[TOWERS] save and quit: %s resumes on floor %d\n", towers_name(g_run.tower), g_run.floor);
    set_state(TOWERS_IDLE);
    unlock();
}

void towers_done(void)
{
    lock();
    if (g_state == TOWERS_LOST || g_state == TOWERS_CLEARED) set_state(TOWERS_IDLE);
    unlock();
}

int towers_card_wanted(void)
{
    /* from the request until the match is set up, and a short fade after it */
    if (g_state == TOWERS_ARMED) return 1;
    return g_state == TOWERS_FIGHTING && g_step == 0 && GetTickCount64() - g_since < 600;
}

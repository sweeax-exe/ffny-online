/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Towers (T0, offline): the game side of the climb. A run starts from the hub's TOWERS page; each floor is a One on
 * One against a CPU fighter set up by automatch (as Run the Streets' VS CPU), with the floor's difficulty written at
 * the match set-up (the CPU slot's level word, as netplay.c's cpu_level_apply does online) and our knobs past Hard
 * applied in the fight (the CPU's damage through the health-change hook, its start momentum, its health as the
 * damage it takes). The fight's result is read from the game's own phase and result words; once the game is back on
 * the battle menu after the results, the overlay's TOWERS window (towers_ui.inc) shows the climb or the end of the
 * run. A loss ends the run (no continues). The run is written to <data>/towers.json at its save points. Never
 * online: every hook does nothing while netplay is active. Rules: towers_rules.h. */
#ifndef DEFJAM_TOWERS_H
#define DEFJAM_TOWERS_H

#include "towers_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

enum TowersState {
    TOWERS_IDLE = 0,    /* no run under way in this session */
    TOWERS_ARMED,       /* the floor's fight is asked for; it starts at the next quiet moment of the menu */
    TOWERS_FIGHTING,    /* the floor's fight is set up (and fought, and on its results) */
    TOWERS_WON,         /* back on the menu after a won floor: the climb window */
    TOWERS_DRAW,        /* ... after a draw: the same floor again */
    TOWERS_LOST,        /* ... after a loss (or a fight left): the run is over */
    TOWERS_CLEARED      /* ... after the top floor: the tower is beaten */
};

typedef struct TowersView {
    int state;                  /* TowersState */
    int tower, floors, floor;   /* floor: the one fought next (after a win, the new one) */
    int last_floor, last_opponent, last_venue;  /* the fight just decided (0 / -1 before the first) */
    int next_opponent, next_venue;
    TowerFloorRules next_rules;
    int save_point;             /* WON: the floor just won was a save point (SAVE AND QUIT is offered) */
    int left_fight;             /* LOST: the fight was left (pause menu) rather than lost */
    int best;                   /* the tower's best (floors beaten, ever) */
    int new_best;               /* this run beat the best before it */
    uint32_t seed, climb_ms;
    int fighter, skin;
    unsigned long long since;   /* GetTickCount64 when the state began */
} TowersView;

/* A new run of `tower` (TOWER_UPTOWN or TOWER_FIVE_BOROUGHS) for the climber (Battle-mode ID, skin): any saved run of
 * that tower is replaced. The first floor's fight is asked for at once. 0 when the tower cannot be built (no
 * fighters or venues read from the dump). */
int towers_start(int tower, int fighter, int skin);
/* Resume the saved run of `tower` on its floor. 0 when there is none. */
int towers_resume(int tower);
/* The saved run of `tower` (copied into *out when out != NULL): 1 if there is one. */
int towers_saved(int tower, TowerRun *out);
/* Drop the saved run of `tower` (NEW CLIMB over a saved one). */
void towers_forget(int tower);
int towers_best(int tower);
/* The floors a new run of `tower` would have on this PC now (the roster's size, the climber not counted). */
int towers_floors_now(int tower);

/* The state for the screens; the run under way (or just ended) when run != NULL. */
void towers_view(TowersView *out);
const TowerRun *towers_run(void);
int towers_active(void);                /* a run is under way in this session (any state but IDLE) */
int towers_state(void);                 /* TowersState now */

/* The TOWERS window (towers_ui.inc): 1 once when it is due: the game is back on a menu after a floor's fight, or a
 * run just started or resumed (the window then holds the input over the first load). */
int towers_window_take(void);
/* From the window: climb on (WON or DRAW: the next floor's fight is asked for); save and quit (WON on a save point:
 * the run stays saved, the session's run ends); done (LOST or CLEARED: back to idle). */
int towers_climb_on(void);
void towers_save_quit(void);
void towers_done(void);
/* The draw-only card over the load (the next opponent) is due: a floor's fight is asked for or loading. */
int towers_card_wanted(void);

/* The hooks. */
void towers_match_start(void);          /* sub_001A4D90's entry (openworld.c's ow_match_start) */
void towers_step(void);                 /* each fight step, before the game's own (openworld.c's ow_step) */
void towers_health_change(void);        /* sub_001A50B0's entry: (record, delta) at g_esp+4/+8 (ow_health_change) */
void towers_on_funccall(const char *text);  /* after each front-end call (automatch_on_funccall) */
/* The results' Rematch during a tower fight: the menu goes on as No does (a floor is never fought twice by it).
 * 1 when it took the call. */
int towers_rematch_request(void);

#ifdef __cplusplus
}
#endif

#endif

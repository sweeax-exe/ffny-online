/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The start countdown (build 77): "3 - 2 - 1 - FIGHT!" at the start of every fight, with every
 * fighter's input held at rest until FIGHT (players' reports: an opponent ran in and attacked
 * while the fight was still starting; offline the CPU's first hit came at step 77).
 *
 * The count is in simulation steps from the fight's first step (1 = the first step after the
 * intros), never in wall-clock time: online every PC runs the same steps with the same entries, so
 * every PC holds the same steps. The hold is applied to the pad words the fighters read
 * (netplay_on_pad_read, before sub_001BAA00; the CPU's brain writes the same table), and online
 * also to the entries this PC sends for those steps, so the relay's recording and a spectator see
 * the rest the simulation used.
 *
 * Build 81: online fights only by default. Every online fight (lockstep or rollback, a spectator's, the
 * rollback soak) always has it, whatever the setting, so every PC holds the same steps; an offline fight
 * holds and shows it only with [gameplay] offline_countdown on (default off: the fighters' input is free
 * from step 1 and the game's own grey FIGHT! lettering shows, as in retail; a speedrunner's request).
 * The setting is read once at each fight's setup.
 *
 * RECOMP_START_COUNTDOWN=0 turns it off everywhere (tests, the retail behaviour). The rules here are pure C
 * (tests/unit/test_start_countdown_native.py compiles start_countdown_rules.c natively); the game
 * side is start_countdown.c, the drawing fight_hud_ui.inc. */
#ifndef DEFJAM_START_COUNTDOWN_H
#define DEFJAM_START_COUNTDOWN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define START_COUNTDOWN_STEPS   180u    /* steps 1..180 held: 3 s at 60 steps a second */
#define START_COUNTDOWN_BEAT    60u     /* one number a second: 3 (1..60), 2 (61..120), 1 (121..180) */
#define START_COUNTDOWN_FIGHT   60u     /* "FIGHT!" over the first second of the fight (181..240) */

/* ---- rules (start_countdown_rules.c) ---- */

/* The switch's value (RECOMP_START_COUNTDOWN): unset, empty or anything else on; "0", "off", "false",
 * "no" off. */
int start_countdown_parse(const char *value);
/* 1 when this fight has the countdown: the switch on, and the fight online or [gameplay]
 * offline_countdown on. */
int start_countdown_applies(int enabled, int online, int offline_setting);
/* 1 while the fighters' input is held at rest at this step of the fight (1 = its first step). */
int start_countdown_held_at(int enabled, uint32_t step);
/* What the countdown shows at this step: 3, 2 or 1; 0 for "FIGHT!"; -1 nothing. *t (may be NULL):
 * how far into that number or into "FIGHT!", 0 to 1. */
int start_countdown_shown(int enabled, uint32_t step, float *t);

/* ---- the game side (start_countdown.c) ---- */

int start_countdown_enabled(void);                  /* RECOMP_START_COUNTDOWN, read once; off off-Windows */
void start_countdown_match_start(void);             /* sub_001A4D90: a fight is set up ([gameplay] read) */
/* This fight has the countdown (start_countdown_applies). online: the fight is online now; once online
 * a fight stays so until the next setup. */
int start_countdown_active(int online);
void start_countdown_step(int rerun);               /* sub_00087700: a simulation step (rerun: rollback's again) */
uint32_t start_countdown_offline_step(void);        /* the steps counted since the setup (not re-runs) */
/* sub_001BAA00 (actor = the fighter's record): its pad words at rest while step is held (online: as
 * start_countdown_active). */
void start_countdown_hold(uint32_t actor, uint32_t step, int online);

#ifdef __cplusplus
}
#endif

#endif

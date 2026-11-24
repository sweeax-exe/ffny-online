/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Towers: the CPU past Hard, the game side (rules: towers_ai_rules.h). Called by towers.c only, and only while a tower
 * fight is live offline (towers.c's FIGHTING state, never with netplay active): online rooms with CPUs (Free for
 * All, Team) load the same AI data and run the same brain, and nothing here touches them. Writes only the CPU slot's
 * loaded AI words (the AI manager at 0x3773A4, reloaded from ai.viv at every match start, so nothing carries over);
 * the difficulty word stays the floor's (2 from floor 7). Each fight's numbers go to the log as one
 * "[TOWERS-AI] fight" line (the measurements of build 101 and later tuning). */
#ifndef DEFJAM_TOWERS_AI_H
#define DEFJAM_TOWERS_AI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The floor's fight was set up (towers_match_start): slot 0 the climber, slot 1 the CPU. */
/* Build 108, The Weekly's BOSS RUSH: the next begins take every floor as a boss floor (towers_ai_floor_ex). */
void towers_ai_boss_all(int on);
void towers_ai_begin(int tower, int floor, int floors);
/* Build 106, the team towers: the same ramp on both CPU slots (2 against its diagonal opponent 1, 3 against 0). */
void towers_ai_begin_team(int tower, int floor, int floors);
/* Each fight step before the game's own (towers_step), step 1.. of the fight; decided: the fight is decided. */
void towers_ai_step(unsigned step, int decided);
/* A health change the game is about to apply (towers_health_change): the record and the delta after our scale. */
void towers_ai_health(uint32_t record, float delta);
/* The fight was decided: the numbers go to the log. winner 0 the climber, 1 the CPU, -1 a draw. */
void towers_ai_end(int winner, unsigned steps);
/* Team Tower Online: on, the test switches (RECOMP_TEST_TOWERS_AI, _HOLD, _ADAPT) are ignored (both PCs of a floor must
 * think alike); the contexts' hash for the lockstep checksum (netplay.c, as ow_state_hash). */
void towers_ai_set_online(int on);
uint32_t towers_ai_hash(void);
/* Team Tower Online with rollback: on during a re-run of steps (no log lines); the contexts for a save state (at most
 * towers_ai_state_size() bytes). */
void towers_ai_quiet(int on);
unsigned towers_ai_state_size(void);
void towers_ai_state_save(void *out);
void towers_ai_state_load(const void *in);

#ifdef __cplusplus
}
#endif

#endif

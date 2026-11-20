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
void towers_ai_begin(int tower, int floor, int floors);
/* Each fight step before the game's own (towers_step), step 1.. of the fight; decided: the fight is decided. */
void towers_ai_step(unsigned step, int decided);
/* A health change the game is about to apply (towers_health_change): the record and the delta after our scale. */
void towers_ai_health(uint32_t record, float delta);
/* The fight was decided: the numbers go to the log. winner 0 the climber, 1 the CPU, -1 a draw. */
void towers_ai_end(int winner, unsigned steps);

#ifdef __cplusplus
}
#endif

#endif

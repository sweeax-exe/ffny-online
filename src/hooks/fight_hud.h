/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The fight overlays' view of the game (build 76): the PERFECT banner and the players' name tags
 * (rules in fight_hud_rules.h, drawing in fight_hud_ui.inc). Read-only: the hooks below read the
 * fighters' records and the match's phase words and keep host-side notes; they write no game
 * memory, draw no random numbers and touch no input, so lockstep and rollback see nothing of it. */
#ifndef DEFJAM_FIGHT_HUD_H
#define DEFJAM_FIGHT_HUD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A match is set up (sub_001A4D90_enter, through netplay_on_match_start). */
void fight_hud_match_start(void);
/* A fight simulation step begins (sub_00087700_enter, through netplay_on_step, after netplay's own
 * step work). */
void fight_hud_step(void);

/* What the overlay draws from; any thread. */
typedef struct FightHudView {
    uint32_t round;         /* counts this run's matches: a new value is a new round */
    uint32_t perfect;       /* the winners of a perfect round (bit per slot), 0: none (yet) */
    uint32_t beat;          /* counts the steps as they run: it moves while the fight and its HUD run */
    uint32_t live;          /* 1 while the round is fought (begun, not decided): the game's HUD is up */
    uint32_t active;        /* fighter slots in the fight */
    uint32_t cpu;           /* the CPU's among them */
} FightHudView;
void fight_hud_view(FightHudView *out);

/* netplay.c's result report: the slots of `winners` when the round was a perfect, else 0. */
uint32_t fight_hud_report_perfect(uint32_t winners);

#ifdef __cplusplus
}
#endif

#endif

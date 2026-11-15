/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* New fighters: Character rows of assets/djv2.sod at a new fighter's place (new_fighter_places.h: 100..127 and 20
 * free IDs below 98; a content pack's
 * new-fighter op, scripts/new-fighter.py for tests), each a copy of a stock fighter's row (the same coreID,
 * so the same moves) with its own name and model. docs/research/new-fighters.md. */
#ifndef DEFJAM_NEW_FIGHTERS_H
#define DEFJAM_NEW_FIGHTERS_H

#include <stdint.h>
#include "new_fighter_places.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NEW_FIGHTER_MAX places, new_fighter_place(fe_id): new_fighter_places.h. The match setup reads the fighter
 * byte signed: feID 128 crashed the load (guest address 0xFFFFFFF0, 2026-11-12); 127 fights. */

typedef struct NewFighter {
    int fe_id;
    int template_fe_id;     /* the stock fighter with its coreID (its select cell); -1 if none */
    int core_id;
    char name[32];          /* the row's display name (word 5) */
} NewFighter;

/* The new fighters of the database the game reads (read once). */
int new_fighters_list(const NewFighter **out);
int new_fighter_template(int fe_id);        /* -1 when fe_id is not a new fighter */

/* After a front-end call the game ran (the FuncCall hook, recomp_manual.c), and Game.GetNextFighter's
 * wrapper: the select screens show each new fighter as a cell of its own after the stock cells (the community
 * row), unlocked, by place. */
void new_fighters_after_funccall(const char *text);
void new_fighters_get_next(void);           /* recomp_lookup_manual: Game.GetNextFighter, 0x000480E0 */

/* A new fighter's own gameplay (fighter_gameplay.h; NULL: its template's), and the three match set-up points
 * that apply it: after Game.StartGame (the stat block and Blazin keys of the set-up, dev_unlock.c), the fight's
 * set-up before the records spawn (sub_001A4D90_enter: a style combination's character index) and the
 * Blazins' registration (sub_001A8180_enter: a template-moveset fighter's Blazins in its record). */
struct FighterGameplay;
const struct FighterGameplay *new_fighter_gameplay(int fe_id);
void new_fighters_start_game_done(uint32_t gd);
void new_fighters_fight_setup(void);
void new_fighters_blazins(void);

/* A new fighter's own voice (fighter_voice.h): at the speech set-up's bank queue (sub_0003DB00_enter) the slot's
 * voice number becomes the fighter's own when its row asks for it and the files are there; and a log of the bank
 * lines looked up (sub_001393F0_enter: RECOMP_SPEECH_LOG=1, always for own voices). */
void new_fighters_voice(void);
void new_fighters_speech_line(uint32_t hdr, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif

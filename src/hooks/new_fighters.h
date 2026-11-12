/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* New fighters: Character rows of assets/djv2.sod with a front-end ID of 100..127 (a content pack's
 * new-fighter op, scripts/new-fighter.py for tests), each a copy of a stock fighter's row (the same coreID,
 * so the same moves) with its own name and model. docs/research/new-fighters.md. */
#ifndef DEFJAM_NEW_FIGHTERS_H
#define DEFJAM_NEW_FIGHTERS_H

#ifdef __cplusplus
extern "C" {
#endif

#define NEW_FIGHTER_FIRST 100
#define NEW_FIGHTER_LAST 127    /* the match setup reads the fighter byte signed: feID 128 crashed the load
                                 * (guest address 0xFFFFFFF0, 2026-11-12); 127 fights. docs/research/new-fighters.md */
#define NEW_FIGHTER_MAX (NEW_FIGHTER_LAST - NEW_FIGHTER_FIRST + 1)

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
 * wrapper: the select screens show each new fighter as one more outfit of its template's cell (as Blaze 2/3
 * are Blaze's), unlocked. */
void new_fighters_after_funccall(const char *text);
void new_fighters_get_next(void);           /* recomp_lookup_manual: Game.GetNextFighter, 0x000480E0 */

#ifdef __cplusplus
}
#endif

#endif

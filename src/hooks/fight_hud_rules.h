/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The rules behind the two fight overlays the port draws (src/hooks/fight_hud.c reads the game,
 * src/hooks/fight_hud_ui.inc draws): the PERFECT banner and the players' name tags. Pure C with no
 * Windows or game dependencies (tests/unit/test_fight_hud_native.py compiles it natively).
 *
 *   - PERFECT: a tracker fed one sample per simulation step (each fighter's health, whether the
 *     match is decided and for whom). The winner (a Team match: both of its side) never below the
 *     health it started the round with makes the round a perfect. Rollback runs steps again with
 *     corrected state, under the same step numbers: a sample for a step at or before a recorded
 *     event replaces that event, so a loss or a decision seen only in a mispredicted step is
 *     forgotten again.
 *   - Name tags: where a player's name goes on the fight HUD, in window pixels, for 2 to 4
 *     fighters, 4:3, stretched and widescreen pictures (the HUD at the picture's edges or in a
 *     centred 16:9 box), at any window size. */
#ifndef DEFJAM_FIGHT_HUD_RULES_H
#define DEFJAM_FIGHT_HUD_RULES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIGHT_HUD_SLOTS 4

typedef struct FightPerfect {
    int armed;                              /* the round's first sample is taken */
    uint32_t first_step;                    /* its step */
    float start[FIGHT_HUD_SLOTS];           /* health at that step */
    uint32_t tracked;                       /* slots in the fight at that step (bit per slot) */
    int32_t lost_step[FIGHT_HUD_SLOTS];     /* the first step a slot was seen under its start health; -1 never */
    int32_t decided_step;                   /* the first step the match was seen decided; -1 not yet */
    uint32_t winners;                       /* as seen at that step (0: a draw) */
} FightPerfect;

typedef struct FightSample {
    uint32_t step;                          /* the simulation step; rollback repeats numbers */
    uint32_t active;                        /* slots in the fight (bit per slot) */
    float health[FIGHT_HUD_SLOTS];
    int decided;                            /* the game's own "decided" */
    uint32_t winners;                       /* the winning slots when decided; 0 a draw */
} FightSample;

void fight_perfect_reset(FightPerfect *p);
void fight_perfect_sample(FightPerfect *p, const FightSample *s);
/* The winners when the round is decided and a perfect, else 0. */
uint32_t fight_perfect_mask(const FightPerfect *p);
/* 1 when `health` is under the round's start for `slot` (also for a slot not tracked or a NaN). */
int fight_perfect_under(const FightPerfect *p, int slot, float health);

/* The winning slots of a decision for fighter slot `winner`: its side in a Team match (slots 0 and
 * 1 against 2 and 3), limited to the slots in the fight; 0 when the slot is not one of them. */
uint32_t fight_winner_mask(int winner, int team, uint32_t active);

typedef struct FightPicture {
    float w, h;             /* the window */
    float wide;             /* the widescreen aspect in use, 0: a 4:3 picture */
    int stretch;            /* 4:3 picture stretched to the window ([display] aspect = stretch) */
    int hud_centred;        /* widescreen HUD in a centred 16:9 box (wider pictures only) */
} FightPicture;

typedef struct FightTag {
    float x, y;             /* the text's anchor in window pixels: its top, and its left or right end */
    int right;              /* 1: x is the right end */
    float size;             /* text height in window pixels */
    float width;            /* room for the text in window pixels */
} FightTag;

/* The name tag of fighter `slot` (0..3) in a fight of `fighters` (2..4). 0 when there is none. */
int fight_tag_place(const FightPicture *pic, int fighters, int slot, FightTag *out);

/* A name for a tag: at most `max_chars` characters of `name` (UTF-8 kept whole, ".." when cut),
 * control characters dropped. Returns the number of bytes written (0: no tag). */
int fight_tag_text(const char *name, int max_chars, char *out, int size);

#ifdef __cplusplus
}
#endif

#endif

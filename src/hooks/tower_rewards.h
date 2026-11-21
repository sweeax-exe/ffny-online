/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Towers rewards (build 104): the player-card frames and titles players earn in the Towers (the account service's
 * Locker items tw-*, server relay/accounts/tower_rewards.go). This file is the plain part: which reward an item id
 * is, its rarity, the effects its frame and title show (rarer = more), the Tower Legend's ribbon line and the
 * effects' timing. tower_frames.inc draws them on the player cards (room, watch, the Fight Complex's hill) and in
 * the Locker. Visual only: nothing here reaches a fight, lockstep or rollback; no allocation. */
#ifndef TOWER_REWARDS_H
#define TOWER_REWARDS_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TRW_NONE = 0,
    TRW_FLOOR25,        /* tw-floor25: Floor 25 Survivor, steel, rivets, a "25" plate */
    TRW_FLOOR50,        /* tw-floor50: Floor 50 Survivor, bronze, rivets, a "50" plate */
    TRW_UPTOWN,         /* tw-uptown: Uptown Champion, gold, a skyline crest */
    TRW_UNTOUCHABLE,    /* tw-untouchable: chrome, cut corners, a shield */
    TRW_WEEKLY,         /* tw-weekly: Weekly Conqueror, neon (nothing grants it yet) */
    TRW_BOROUGHS,       /* tw-boroughs: King of the Five Boroughs, platinum, a crown, five borough gems */
    TRW_LEGEND,         /* tw-legend-<tower>-<yyyy>-<mm>: Tower Legend, red lacquer, gold trim, laurel, a ribbon */
    TRW_KINDS
};

enum { TRW_RARE = 1, TRW_EPIC, TRW_LEGENDARY, TRW_MYTHIC };

/* the effects of a frame and its title */
#define TRW_FX_SWEEP   0x01u    /* a soft light band crosses the card */
#define TRW_FX_SHINE   0x02u    /* the title's light runs along it */
#define TRW_FX_GLINT   0x04u    /* a narrow bright chrome glint crosses the card */
#define TRW_FX_GLOW    0x08u    /* the neon tubes breathe */
#define TRW_FX_FLICKER 0x10u    /* the neon (and its title) flickers now and then */
#define TRW_FX_EMBERS  0x20u    /* embers rise over the portrait */
#define TRW_FX_PULSE   0x40u    /* the crown on top pulses */

#define TRW_EMBERS 5

/* The reward an item id is (TRW_NONE: not one); a legend's tower (1 Uptown, 2 Five Boroughs), year and month
 * (1..12) into the pointers (any may be NULL). */
int tower_reward_kind(const char *id, int *tower, int *year, int *month);
int tower_reward_rarity(int kind);
const char *tower_reward_rarity_name(int rarity);      /* "RARE" .. "MYTHIC", "" none */
unsigned tower_reward_effects(int kind);
int tower_reward_effect_count(int kind);
/* The Tower Legend's ribbon: "#1 UPTOWN · NOV 2026" (UTF-8 middle dot); returns its length (0: not a legend). */
int tower_reward_ribbon(const char *id, char *out, int size);

/* timing (t in seconds since any fixed point) */
float tower_fx_sweep(float t, float period);    /* the band's place 0..1 across the card, -1 while it rests */
float tower_fx_pulse(float t, float period);    /* 0..1..0 */
float tower_fx_flicker(float t);                /* the neon's brightness: 1, or 0.35 at the flicker moments */
/* ember i (0..TRW_EMBERS-1): its place over the portrait (x 0..1 from the left, rise 0..1 from the foot) and its
 * alpha 0..1 */
void tower_fx_ember(int i, float t, float *x, float *rise, float *alpha);

#ifdef __cplusplus
}
#endif
#endif

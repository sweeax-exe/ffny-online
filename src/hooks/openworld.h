/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Open-world mode (src/hooks/openworld.c): the Big Arena free-for-all and its follow camera. */
#ifndef DEFJAM_OPENWORLD_H
#define DEFJAM_OPENWORLD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OW_STATE_BYTES 96   /* room for the Big Arena state in each rollback save (netplay.c) */

/* rollback: the Big Arena state goes with every save and comes back with every load */
void ow_state_save(uint8_t out[OW_STATE_BYTES]);
void ow_state_load(const uint8_t in[OW_STATE_BYTES]);
/* the online checksum: 0 while no Big Arena fight runs (the checksum is then unchanged) */
uint32_t ow_state_hash(void);

/* the overlay (fight_hud_ui.inc): the scores of a Big Arena fight, 0 when none runs */
typedef struct OwHudView {
    int on;                 /* a Big Arena fight runs */
    int score_limit;        /* first to this many knockouts wins */
    int score[4], deaths[4];
    uint32_t present;       /* fighter slots in the fight (bit per slot) */
    uint32_t down;          /* slots knocked out and waiting to come back */
    int back_in[4];         /* steps until a down slot comes back */
    uint32_t protected_;    /* slots in their respawn protection */
    int leader;             /* the slot ahead, -1 a tie */
    int ending, winner;     /* the limit is reached: the winner's slot */
    int seconds_left;       /* the time limit's rest, -1 none */
    int local;              /* this PC's slot (offline 0) */
    uint32_t step;          /* the fight's step */
} OwHudView;
int ow_hud_view(OwHudView *out);

/* the pure rules, also unit-tested (tests/unit/test_openworld_rules.py builds openworld_rules.c alone) */
typedef struct OwSpawnCtx {
    float spawn_x[8], spawn_z[8];
    int spawns;
    float live_x[4], live_z[4];
    int lives;
    float safe;             /* a spawn point this far from every living fighter counts as safe */
} OwSpawnCtx;
int ow_rules_pick_spawn(const OwSpawnCtx *c);
int ow_rules_winner(const int score[4], const int deaths[4], uint32_t present);
int ow_rules_next_loser(const int score[4], const int deaths[4], uint32_t present, int winner, uint32_t done);
float ow_rules_yaw_to_centre(float x, float z);

#ifdef __cplusplus
}
#endif

#endif

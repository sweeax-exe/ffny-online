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
    int seconds_left;       /* the time limit's rest, -1 none (no time limit: the rest to the safety end) */
    int steps_left;         /* the same in steps, -1 none */
    int unlimited;          /* Run the Streets without a time limit (the KO target ends it) */
    int time_up;            /* the end came from the clock: TIME!, then the winner, then the losers go down */
    uint32_t end_step;      /* the step the end came at (ending) */
    int local;              /* this PC's slot (offline 0) */
    uint32_t step;          /* the fight's step */
} OwHudView;
int ow_hud_view(OwHudView *out);

/* the pure rules, also unit-tested (tests/unit/test_openworld_rules.py builds openworld_rules.c alone) */
#define OW_SPAWN_MAX 16      /* spawn points of a map (bigarena.json; Knox and Jam has 16) */
typedef struct OwSpawnCtx {
    float spawn_x[OW_SPAWN_MAX], spawn_z[OW_SPAWN_MAX];
    int spawns;
    float live_x[4], live_z[4];
    int lives;
    float safe;             /* a spawn point this far from every living fighter counts as safe */
} OwSpawnCtx;
int ow_rules_pick_spawn(const OwSpawnCtx *c);
int ow_rules_winner(const int score[4], const int deaths[4], uint32_t present);
int ow_rules_next_loser(const int score[4], const int deaths[4], uint32_t present, int winner, uint32_t done);
float ow_rules_yaw_to_centre(float x, float z);
/* "spawns": [[x, z], ...] of a bigarena json (Studio's assets/bigarena.json) -> up to max points; 0 none */
int ow_rules_parse_spawns(const char *json, float *x, float *z, int max);
/* GET /api/features: "run_the_streets" true 1, false 0, missing -1; reason gets "reason" ("" none) */
int ow_rules_features_answer(const char *json, char *reason, int size);
/* Camera-relative controls: the stick (x right, y up as the pad gives it) turned by the camera's heading, where
 * heading 0 is the stock view (looking along -Z, the game's own stick directions) and grows as the camera turns
 * clockwise seen from above; the length is kept, so a full push stays full. */
void ow_rules_stick_turn(float heading_deg, float *x, float *y);
/* The heading of a camera looking from eye to at (x, z), in the same sense: 0 looking along -Z. */
float ow_rules_view_heading(float eye_x, float eye_z, float at_x, float at_z);

/* Run the Streets' round rules (build 87): the time limit (5, 10, 15 minutes or none) and the KO target (5, 10, 20)
 * a room's host or the VS CPU page chose, as one byte: in a mode 0x43 room it is the room settings' rounds byte, which
 * the relay checks and puts in START, so every PC of the fight has the same. 0 is the defaults (10 minutes, first to
 * 10); else OW_RTS_RULES_SET | time index | KO index << 2. Anything else reads as the defaults. Without a time limit
 * the KO target ends the fight; so a fight nobody scores in never runs forever, it still ends after
 * OW_RTS_SAFETY_SECONDS like a time limit (TIME!, the leader wins). */
#define OW_RTS_RULES_SET      0x80
#define OW_RTS_TIMES          4         /* 5, 10, 15 minutes, none */
#define OW_RTS_KOS            3         /* first to 5, 10, 20 */
#define OW_RTS_DEFAULT_TIME   1         /* 10 minutes */
#define OW_RTS_DEFAULT_KOS    1         /* first to 10 */
#define OW_RTS_TIME_NONE      3
#define OW_RTS_SAFETY_SECONDS 1800      /* no time limit: still over after 30 minutes */
int ow_rules_rts_valid(int byte);
int ow_rules_rts_encode(int time_idx, int ko_idx);              /* the defaults are 0 */
void ow_rules_rts_decode(int byte, int *time_idx, int *ko_idx); /* invalid: the defaults */
int ow_rules_rts_seconds(int time_idx);                         /* 300, 600, 900; 0 none */
int ow_rules_rts_kos(int ko_idx);
int ow_rules_rts_time_index(int seconds);                       /* 300/600/900/0 -> index (else the default) */
int ow_rules_rts_ko_index(int kos);
int ow_rules_rts_limit_steps(int byte);                         /* the fight's end in steps (the safety one without a limit) */
int ow_rules_rts_text(int byte, char *out, int size);           /* "10 MIN  -  FIRST TO 10" */

/* Run the Streets (rts.c, openworld.c): the overlays and the voice read these. */
int ow_rts_active(void);    /* a Run the Streets fight runs on this PC (offline or online) */
/* sub_001BAA00's entry after netplay_on_pad_read: every fighter at rest once the clock ended the fight (TIME!) */
void ow_pad_hold(uint32_t actor);

#ifdef __cplusplus
}
#endif

#endif

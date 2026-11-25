/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Illegal-tech detection in the fight (stage 1: silent; docs/research/illegal-tech.md). The rules are in
 * illegal_tech_rules.c; this reads the fighter records for them at the combo log's three hooks (combo_log.h)
 * and keeps their state where rollback and the lockstep checksum see it. It runs in every fight and changes
 * no game memory; its detections go out only with the match reports of ranked and tournament set games
 * (or any online match with the test switch RECOMP_ILLEGAL_TECH=test). */
#ifndef DEFJAM_ILLEGAL_TECH_H
#define DEFJAM_ILLEGAL_TECH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* room for the detector's state in each rollback save (netplay.c); illegal_tech.c checks it fits */
#define ILLEGAL_TECH_STATE_BYTES 1536

/* The combo log's hooks (combo_log.c) */
void illegal_tech_match_start(void);
void illegal_tech_hit(uint32_t attacker, uint32_t defender, uint32_t flags);
void illegal_tech_step(void);

/* Rollback (netplay.c snap_save / rollback_to) */
void illegal_tech_state_save(uint8_t out[ILLEGAL_TECH_STATE_BYTES]);
void illegal_tech_state_load(const uint8_t in[ILLEGAL_TECH_STATE_BYTES]);
/* The lockstep checksum's word for the detector (never 0) */
uint32_t illegal_tech_hash(void);

/* netplay.c at a START: whether this match's detections go out with its reports (a ranked or tournament
 * set game; any online match with the test switch). */
void illegal_tech_set_reported(int ranked_set);
int illegal_tech_reported(void);
/* RECOMP_ILLEGAL_TECH=test: report in casual online matches too (tests); =off: report nothing (never the
 * detection itself, which every PC of a match runs alike); =sensitive (tests, on every PC of the match: it changes
 * the detector's state): as test, and the first block cancel, restick and chain stun of a combo already count.
 * Returns 0 normal, 1 test, 2 off, 3 sensitive. */
int illegal_tech_test_switch(void);
/* Appends ,"it":{...} to a match report at n when this match's detections are reported; returns the new length. */
int illegal_tech_json(char *json, int size, int n);
/* Detections so far in this match (tests, the log) */
unsigned illegal_tech_count(void);

#ifdef __cplusplus
}
#endif
#endif

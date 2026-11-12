/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Milestone progress of a profile's fighter (build 79, owner request 2026-11-12): players thought the
 * milestones were broken because the game never shows how far along they are.
 *
 * What the game has (read-only here; docs: community/punch-kick-milestone-study.md,
 * community/toughness-milestone-study.md):
 *   - Stat milestones: counters in the profile (+0x68..+0x8E), fed after every finished offline match
 *     by the reward pass (guest 0x00064ED0) for the profile linked to the slot's pad, turned into bonus
 *     stat points (+0x114..+0x128) by 0x00014C20 (the thresholds below are its immediates).
 *   - The ID card's four honours (Game.GetMilestone, 0x00013920; the Battle user-ID screen,
 *     options/battleID, plays a badge for each): base stats total 250, three styles, all 39 moves,
 *     Story complete (+0x150).
 * Nothing here writes guest memory or the save: the counters are read from a copy of the profile.
 *
 * The rules (milestones_rules.c) are pure C over a byte copy of a profile slot, so
 * tests/unit/test_milestones_native.py compiles them natively. The game side (milestones.cpp) finds
 * the profile, copies it and makes the toast after a match; the drawing is milestones_ui.inc. */
#ifndef DEFJAM_MILESTONES_H
#define DEFJAM_MILESTONES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MS_PROFILE_BYTES    0x4D90u     /* what the rules read of a profile slot (the moves at +0x4D8B) */
#define MS_STAT_ROWS        15          /* the stat milestones; the four ID card honours follow */
#define MS_MAX_ROWS         19

enum MilestoneGroup {
    MS_UPPER, MS_LOWER, MS_SPEED, MS_TOUGHNESS, MS_HEALTH, MS_CHARISMA, MS_BADGE, MS_GROUPS
};

typedef struct MilestoneRow {
    int group;              /* MilestoneGroup */
    const char *what;       /* what counts, for the player */
    const char *reward;     /* what reaching it gives */
    uint32_t count, target; /* progress */
    uint32_t count2, target2;   /* a second condition that must be met too (target2 0: none) */
    int reached;
    int sure;               /* 1: what the counter counts is read in the code; 0: inferred */
    int note;               /* MS_NOTE_* */
} MilestoneRow;

enum { MS_NOTE_NONE, MS_NOTE_REPLACES, MS_NOTE_TOUGHNESS_FIX, MS_NOTE_TOUGHNESS_RETAIL };

/* The group's heading ("UPPER BODY", ..., "ID CARD"). */
const char *milestones_group_name(int group);

/* The rows for a profile slot's bytes (p[0] = the slot's first byte, size how many are valid). The
 * honours need MS_PROFILE_BYTES; with fewer only the stat rows come. toughness_fix: [gameplay]
 * toughness_fix is on (the multi-fighter wins add up; off, the retail game keeps only the last match).
 * Returns the number of rows. */
int milestones_rows(const uint8_t *p, uint32_t size, int toughness_fix, MilestoneRow *out, int max);

/* How many of the rows are reached. */
int milestones_reached(const MilestoneRow *rows, int n);

/* 0..1: how far a row is (the smaller part when it has two conditions). */
float milestones_fraction(const MilestoneRow *row);

/* "3,420": a count with thousands separators. */
void milestones_count_text(uint32_t value, char *out, int size);

/* "3,420 / 5,000" (and " + 80 / 100" for a second condition). */
void milestones_progress_text(const MilestoneRow *row, char *out, int size);

/* After a match: the stat rows that are reached now and were not before. Writes the toast's text
 * ("MILESTONE: Punches landed 100 - +1 Upper Body", "+ 1 more") and returns how many (0: none, the
 * text is empty). */
int milestones_newly_reached(const MilestoneRow *before, int nb, const MilestoneRow *after, int na,
                             char *text, int size);

/* ---- the game side (milestones.cpp) ---- */

/* The Story's current profile (save block +0x5AC7C), -1 none. */
int milestones_current_profile(void);
/* Copy profile slot `index` (0..15) into buf (MS_PROFILE_BYTES); name gets its user ID. 0 when the
 * slot is empty or the game data is not up. */
int milestones_read_profile(int index, uint8_t *buf, char *name, int name_size);
/* From the FuncCall hook: the Battle user-ID screen asks Game.GetMilestone(id, n) for the ID it shows. */
void milestones_on_funccall(const char *text);
/* The user ID the Battle user-ID screen showed last (-1 none). */
int milestones_battle_profile(void);
/* Around the reward pass of one slot (toughness_fix.c): the profile it handles (0 none). */
void milestones_before_rewards(uint32_t profile);
void milestones_after_rewards(uint32_t profile);
/* The toast's text after a match that reached a milestone; 1 while it is to be shown. */
int milestones_toast(char *out, int size);

#ifdef __cplusplus
}
#endif

#endif

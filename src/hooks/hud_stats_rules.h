/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 98's two display additions, the rules only (pure C, no Windows or game dependencies;
 * tests/unit/test_hud_stats_native.py compiles them natively):
 *
 *   - The room streak: each player's wins in a row in the current room (its matches and their rematches), from
 *     the room matches' decisions this PC saw (netplay.c, at the same point it reports the result). A win adds
 *     one, a loss or a draw ends it, a match a player sat out changes nothing; a new room starts over. Every PC
 *     of a match sees the same decisions, so every PC shows the same numbers. The fight HUD shows it after the
 *     player's name tag from HUD_STREAK_SHOWN wins: "SlawPro (4 WINS)".
 *   - The recent form: "LAST 10: 7W 3L" on the versus screen's player cards, from the account service's
 *     GET /api/players/recent?format=text (relay/accounts/recent.go). */
#ifndef DEFJAM_HUD_STATS_RULES_H
#define DEFJAM_HUD_STATS_RULES_H

#ifdef __cplusplus
extern "C" {
#endif

#define HUD_STREAK_PLAYERS 16
#define HUD_STREAK_NAME 25
#define HUD_STREAK_SHOWN 2      /* a single win is no streak yet */

typedef struct HudStreaks {
    char room[40];                                  /* the room the counts are for ("" none yet) */
    char name[HUD_STREAK_PLAYERS][HUD_STREAK_NAME]; /* "" a free entry */
    int wins[HUD_STREAK_PLAYERS];
    unsigned age[HUD_STREAK_PLAYERS];               /* the decision that last touched the entry */
    unsigned decisions;
} HudStreaks;

void hud_streaks_reset(HudStreaks *t);
/* A room match decided: the players' names by position (NULL or "" for a CPU), `winners` a bit per position
 * (0: a draw). `room` names the room; another room than the last one's starts the counts over. */
void hud_streaks_result(HudStreaks *t, const char *room, const char *const *names, int count, unsigned winners);
/* `name`'s wins in a row in `room` (0: none, another room, a CPU). */
int hud_streaks_get(const HudStreaks *t, const char *room, const char *name);
/* The HUD's text for a streak: "(4 WINS)", "" under HUD_STREAK_SHOWN. Returns the bytes written. */
int hud_streak_label(int wins, char *out, int size);

typedef struct HudRecent {
    char name[HUD_STREAK_NAME];
    int wins, losses, draws, counted;
} HudRecent;
/* The service's text answer: up to `max` lines "recent=name|w|l|d|counted" into out; *n gets GET's n (10 when
 * absent). Returns how many were read; malformed lines are skipped. */
int hud_recent_parse(const char *text, HudRecent *out, int max, int *n);
/* The card's line: "LAST 10: 7W 3L" (" 1D" when there were draws; "LAST 3: ..." for a shorter history), "" for
 * nothing counted. Returns the bytes written. */
int hud_recent_label(const HudRecent *r, int n, char *out, int size);

#ifdef __cplusplus
}
#endif

#endif

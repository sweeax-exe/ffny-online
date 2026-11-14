/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 83: the season countdown and the hub fighter search, as pure C (tests/unit/test_season_search_native.py
 * compiles season_search.c natively).
 *
 * The countdown: "SEASON 1 ENDS IN 48 DAYS", hours on the last day ("SEASON 1 ENDS IN 5 HOURS"), "SEASON 1 ENDS
 * TODAY" in its last hour (and once the end has passed, until the service rolls the season over). The end is the
 * account service's current season end (the stats text's season= line, the same value as GET /api/ranked/seasons'
 * now.end); a season without an end shows nothing. The Turf War's days left use the same wording.
 *
 * The fighter search (online_screen.cpp draw_fighters): a name matches a query when the query's letters and digits
 * appear in it in a row, case-insensitive, blanks and punctuation ignored ("dmob" finds "D-Mob"). LT/RT jump to the
 * next / previous first letter in alphabetical order (the grid's first fighter with that letter). */
#ifndef DEFJAM_SEASON_SEARCH_H
#define DEFJAM_SEASON_SEARCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* The countdown for `label` ("SEASON 1", "TURF WAR") ending at unix second `end`, seen at `now`; 0 (and "") without
 * an end (end <= 0), else the text's length. */
int season_countdown_text(const char *label, long long end, long long now, char *out, int size);

/* 1 when `name` matches `query` (an empty query matches every name). */
int fighter_name_matches(const char *name, const char *query);

/* The index to jump to from `cur` by first letter: dir > 0 the next letter, dir < 0 the previous one (wrapping
 * around the alphabet); the first name in grid order with that letter. Names without a letter or digit are skipped.
 * `cur` when no other letter exists. */
int fighter_letter_jump(const char *const *names, int n, int cur, int dir);

#ifdef __cplusplus
}
#endif

#endif

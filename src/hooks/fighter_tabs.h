/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 90: CHOOSE FIGHTER's tabs and fighting-style filter (online_screen.cpp draw_fighters), as pure C
 * (tests/unit/test_fighter_tabs_native.py compiles fighter_tabs.c natively).
 *
 * Tabs: FFNY (the game's own fighters), COMMUNITY (new fighters' places, the community shelf and MORE) and CREATED
 * (this player's Story-mode fighters). LT/RT (keyboard Z/C) go to the previous / next tab that has cards and is
 * allowed (no wraparound past the ends; an "Original fighters only" search or a STANDARD ONLY room leaves COMMUNITY
 * out). The tab last chosen is remembered ([online] fighter_tab); where it is not offered the grid shows FFNY.
 *
 * Style filter: Y cycles All -> each style the game data has (fighter.xml's codes, online_data.c) -> All. A card
 * matches when its style names the filter's, letters only and case-insensitive: "Street fighting" (fighter.xml)
 * and "KICKBOXING / STREETFIGHTING" (a created fighter's two styles) both match "Street fighting". */
#ifndef DEFJAM_FIGHTER_TABS_H
#define DEFJAM_FIGHTER_TABS_H

#ifdef __cplusplus
extern "C" {
#endif

enum FighterTab { FTAB_FFNY = 0, FTAB_COMMUNITY = 1, FTAB_CREATED = 2, FTAB_COUNT = 3 };

/* The tab from `cur` in direction dir (< 0 previous, > 0 next) among those with offered[t] != 0; `cur` when there
 * is none that way. */
int fighter_tab_step(int cur, int dir, const int offered[FTAB_COUNT]);

/* The tab the grid shows: `saved` when offered, else FFNY when offered, else the first offered one (0 if none). */
int fighter_tab_shown(int saved, const int offered[FTAB_COUNT]);

/* 1 when `style` matches `filter` (an empty or NULL filter matches every style; an empty style matches no filter). */
int fighter_style_matches(const char *style, const char *filter);

/* The style filter after `cur` (0 All, 1..count the styles): Y's cycle. */
int fighter_style_next(int cur, int count);

#ifdef __cplusplus
}
#endif

#endif

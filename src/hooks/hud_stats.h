/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 98's room streaks on the fight HUD and recent form on the versus screen (rules in hud_stats_rules.h).
 * Display only: nothing here touches a match, its inputs or its timing. Any thread. */
#ifndef DEFJAM_HUD_STATS_H
#define DEFJAM_HUD_STATS_H

#include "hud_stats_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

/* netplay.c: a room match this PC played was decided (once per match). names by position, "" a CPU. */
void hud_stats_result(const char *room, const char *const *names, int count, unsigned winners);
/* The fight HUD: `name`'s wins in a row in `room` (0 none). */
int hud_stats_streak(const char *room, const char *name);

/* netplay.c at a room START: ask the account service for the players' recent form, in the background (one
 * request, a few seconds at most; the load never waits for it). Names "" are CPUs. */
void hud_stats_recent_request(const char *const *names, int count);
/* The versus screen: the card's line for `name` ("LAST 10: 7W 3L"), 0 when nothing (yet) arrived. */
int hud_stats_recent_label(const char *name, char *out, int size);

#ifdef __cplusplus
}
#endif

#endif

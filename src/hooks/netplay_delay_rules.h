/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The online input delay (build 76): the number of steps, from the players' pings, and the short line the
 * fight and the room show for it. Pure functions, no game memory: tests/unit/test_netplay_delay_native.py
 * compiles them on their own. netplay.c's agreed_delay is netdelay_steps, so the line says what the
 * match plays with. */
#ifndef DEFJAM_NETPLAY_DELAY_RULES_H
#define DEFJAM_NETPLAY_DELAY_RULES_H

#ifdef __cplusplus
extern "C" {
#endif

#define NETDELAY_MIN     2
#define NETDELAY_MAX     10
#define NETDELAY_RB_LESS 4      /* every PC rolls back: the delay is this much shorter */
#define NETDELAY_LOCKSTEP_MARGIN 1   /* build 76.1: lockstep's extra step (build 75's slack); so up to 11 */

/* The delay in 1/60 s steps for a match: `a_ms` and `b_ms` the two highest of the players' pings to the
 * relay (negative: none, read as 0), `rollback` 1 when every PC rolls back, `multi_margin` the extra step
 * of a lockstep match of three or four players (0 or 1). */
int netdelay_steps(int a_ms, int b_ms, int rollback, int multi_margin);

/* The slowest path between two players: half of each of the two highest pings (negative: 0). */
int netdelay_path_ms(int a_ms, int b_ms);

/* The two highest of `n` pings (negative ones count as 0) into *a >= *b. */
void netdelay_top2(const int *pings, int n, int *a, int *b);

/* The line: "Rollback · 3 frames" or "No rollback (path 140 ms) · 10 frames" (UTF-8 middle dot); with
 * `estimate` (the room, before START) the frames read "about N frames". Returns the length. */
int netdelay_line(char *out, int size, int rollback, int frames, int path_ms, int estimate);

#ifdef __cplusplus
}
#endif

#endif

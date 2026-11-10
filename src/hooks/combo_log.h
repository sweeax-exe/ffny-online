/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Per-hit combo log for tests and mods, RECOMP_COMBO_LOG=1 (src/hooks/combo_log.c,
 * docs/research/combo-log.md). Read-only: it writes [COMBO] lines to the run log and
 * changes nothing in the game. Off by default; when off each hook is one test of a flag. */
#ifndef DEFJAM_COMBO_LOG_H
#define DEFJAM_COMBO_LOG_H
#include <stdint.h>

extern int g_combo_log_on;      /* -1 not read yet, 0 off, 1 on */
int combo_log_init(void);

/* sub_001C6CD0_enter: one hit (cdecl attacker, defender, hit flags), before its body runs */
void combo_log_hit(uint32_t attacker, uint32_t defender, uint32_t flags);
/* sub_00087700_enter: a fight simulation step begins */
void combo_log_step(void);
/* sub_001A4D90_enter: a match is set up */
void combo_log_match_start(void);

static inline int combo_log_on(void)
{
    return g_combo_log_on > 0 || (g_combo_log_on < 0 && combo_log_init());
}
#endif

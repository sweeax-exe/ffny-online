/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* [gameplay] toughness_fix: the first Toughness milestone counts (toughness_fix.c). */
#ifndef DEFJAM_TOUGHNESS_FIX_H
#define DEFJAM_TOUGHNESS_FIX_H

#ifdef __cplusplus
extern "C" {
#endif

/* Around the post-match reward pass of one slot (guest 0x00064ED0, src/recomp_manual.c). */
void toughness_fix_before_rewards(int slot);
void toughness_fix_after_rewards(void);

#ifdef __cplusplus
}
#endif

#endif

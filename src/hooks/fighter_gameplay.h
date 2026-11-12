/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* A new fighter's own gameplay (stats, fighting styles, Blazins), read from two words of its Character row in
 * assets/djv2.sod that the game never reads (docs/research/new-fighter-gameplay.md). Pure functions: every PC
 * decodes the same signed database the same way, so a lockstep or rollback match starts from the same values.
 *
 * Row encoding, version 1 (setup/ffnycontent/newfighter.py, gameplay_words(), writes it):
 *   word 11  bits 0..27  stat k (k = 0..6) in bits 4k..4k+3, each 1..15 (Grapple 0..15), in the order of the
 *                        per-slot stat block GS+0x2B0 (Upper, Lower, Speed, Toughness, Health, Grapple, Charisma)
 *            bits 28..29 version (1)
 *            bits 30..31 moveset bits 4..5
 *   word 3   bits 0..27  Blazin key k (k = 0..3) in bits 7k..7k+6, each a Blazin id 0..81 (SOD table 6 word 4)
 *            bits 28..31 moveset bits 0..3
 *   moveset  0 = the template's own moves and styles; 1..55 = a style combination, the created fighters'
 *            archetype (fighter.xml id, = character index) 85 + moveset, 86..140.
 * A stock row has word 11 = 50 and word 3 = 0: not version 1, so no gameplay of its own. */
#ifndef DEFJAM_FIGHTER_GAMEPLAY_H
#define DEFJAM_FIGHTER_GAMEPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FGP_STATS 7
#define FGP_BLAZINS 4
#define FGP_STAT_MIN 1
#define FGP_STAT_MAX 15
#define FGP_STAT_GRAPPLE 5              /* the sixth dword: 0 for every stock and created fighter; 0..15 */
#define FGP_BLAZIN_MAX 81
#define FGP_MOVESET_MAX 55
#define FGP_ARCHETYPE_BASE 85           /* character index = FGP_ARCHETYPE_BASE + moveset */
#define FGP_VERSION 1
/* The largest stat sum of a stock fighter (all seven stats as the game decodes them, measured 2026-11-12 with
 * RECOMP_NF_STATS_DUMP=1; docs/research/new-fighter-gameplay.md). The server and the installer check the same. */
#define FGP_STAT_BUDGET 60

typedef struct FighterGameplay {
    int moveset;                        /* 0 the template's moves; 1..55 a style combination */
    int stats[FGP_STATS];               /* 1..15 */
    int blazins[FGP_BLAZINS];           /* Blazin ids 0..81, keys 0..3 */
} FighterGameplay;

/* 1 and *out filled when the two row words hold valid version-1 gameplay, else 0 (the fighter keeps its
 * template's gameplay). Valid: version 1, every stat 1..15 (Grapple 0..15), the sum at most FGP_STAT_BUDGET,
 * Blazins 0..81 (repeats allowed: a stock fighter has its own Blazin in all four keys), moveset 0..55. Anything
 * else is refused as a whole, the same on every PC. */
int fighter_gameplay_decode(uint32_t word3, uint32_t word11, FighterGameplay *out);

/* The two words for a gameplay (the installer's encoding, for tests). */
void fighter_gameplay_encode(const FighterGameplay *g, uint32_t *word3, uint32_t *word11);

/* The character index of a moveset's archetype (86..140), or -1 for the template's moves. */
int fighter_gameplay_char_index(const FighterGameplay *g);

#ifdef __cplusplus
}
#endif

#endif

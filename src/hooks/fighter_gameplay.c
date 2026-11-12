/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* A new fighter's own gameplay from its Character row: fighter_gameplay.h. No game state, no host state: the
 * same two words give the same answer on every PC. */
#include "fighter_gameplay.h"

int fighter_gameplay_decode(uint32_t word3, uint32_t word11, FighterGameplay *out)
{
    FighterGameplay g;
    int k, sum = 0;
    if (((word11 >> 28) & 3u) != FGP_VERSION) return 0;
    g.moveset = (int)((word3 >> 28) & 0xFu) | (int)(((word11 >> 30) & 3u) << 4);
    if (g.moveset > FGP_MOVESET_MAX) return 0;
    for (k = 0; k < FGP_STATS; k++) {
        g.stats[k] = (int)((word11 >> (4 * k)) & 0xFu);
        if (g.stats[k] < (k == FGP_STAT_GRAPPLE ? 0 : FGP_STAT_MIN) || g.stats[k] > FGP_STAT_MAX) return 0;
        sum += g.stats[k];
    }
    if (sum > FGP_STAT_BUDGET) return 0;
    for (k = 0; k < FGP_BLAZINS; k++) {
        g.blazins[k] = (int)((word3 >> (7 * k)) & 0x7Fu);
        if (g.blazins[k] > FGP_BLAZIN_MAX) return 0;     /* a stock fighter has its one Blazin in all four keys */
    }
    if (out) *out = g;
    return 1;
}

void fighter_gameplay_encode(const FighterGameplay *g, uint32_t *word3, uint32_t *word11)
{
    uint32_t w3 = 0, w11 = (uint32_t)FGP_VERSION << 28;
    int k;
    for (k = 0; k < FGP_STATS; k++) w11 |= ((uint32_t)g->stats[k] & 0xFu) << (4 * k);
    w11 |= (((uint32_t)g->moveset >> 4) & 3u) << 30;
    for (k = 0; k < FGP_BLAZINS; k++) w3 |= ((uint32_t)g->blazins[k] & 0x7Fu) << (7 * k);
    w3 |= ((uint32_t)g->moveset & 0xFu) << 28;
    *word3 = w3;
    *word11 = w11;
}

int fighter_gameplay_char_index(const FighterGameplay *g)
{
    return g->moveset ? FGP_ARCHETYPE_BASE + g->moveset : -1;
}

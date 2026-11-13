/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Big Arena's pure rules (openworld.c calls them inside the fight step, so they must give the same answer on every
 * PC: no clock, no host state, ties broken by slot). Unit-tested natively (tests/unit/test_openworld_rules.py). */
#include <math.h>
#include "openworld.h"

/* Where a knocked-out fighter comes back: the spawn point nearest to the fight among those at least c->safe from
 * every living fighter (back into the action quickly, never on top of someone); when none is that far, the one
 * farthest from its nearest living fighter. Ties go to the lower index. No living fighter: point 0. */
int ow_rules_pick_spawn(const OwSpawnCtx *c)
{
    int i, j, best_safe = -1, best_far = 0;
    float best_safe_d = 0.0f, best_far_d = -1.0f;
    if (c->spawns <= 0) return -1;
    if (c->lives <= 0) return 0;
    for (i = 0; i < c->spawns && i < 8; i++) {
        float nearest = 1e30f;
        for (j = 0; j < c->lives && j < 4; j++) {
            float dx = c->spawn_x[i] - c->live_x[j], dz = c->spawn_z[i] - c->live_z[j];
            float d = sqrtf(dx * dx + dz * dz);
            if (d < nearest) nearest = d;
        }
        if (nearest >= c->safe && (best_safe < 0 || nearest < best_safe_d)) { best_safe = i; best_safe_d = nearest; }
        if (nearest > best_far_d) { best_far = i; best_far_d = nearest; }
    }
    return best_safe >= 0 ? best_safe : best_far;
}

/* The winner: the most knockouts; then the fewest times knocked out; then the lower slot. -1 with no slot. */
int ow_rules_winner(const int score[4], const int deaths[4], uint32_t present)
{
    int s, w = -1;
    for (s = 0; s < 4; s++) {
        if (!(present & (1u << s))) continue;
        if (w < 0 || score[s] > score[w] || (score[s] == score[w] && deaths[s] < deaths[w])) w = s;
    }
    return w;
}

/* At the end the losers go down one after another, the last placed first: the fewest knockouts, then the most
 * times knocked out, then the higher slot. done: slots already down. -1 when every loser is. */
int ow_rules_next_loser(const int score[4], const int deaths[4], uint32_t present, int winner, uint32_t done)
{
    int s, n = -1;
    for (s = 3; s >= 0; s--) {
        if (!(present & (1u << s)) || s == winner || (done & (1u << s))) continue;
        if (n < 0 || score[s] < score[n] || (score[s] == score[n] && deaths[s] > deaths[n])) n = s;
    }
    return n;
}

/* The facing (the record's +0x54, degrees; forward is (sin, cos)) of a fighter at x,z looking at the centre. */
float ow_rules_yaw_to_centre(float x, float z)
{
    if (x == 0.0f && z == 0.0f) return 0.0f;
    return atan2f(-x, -z) * 57.2957795f;
}

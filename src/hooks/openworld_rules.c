/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Big Arena's pure rules (openworld.c calls them inside the fight step, so they must give the same answer on every
 * PC: no clock, no host state, ties broken by slot). Unit-tested natively (tests/unit/test_openworld_rules.py). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
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
    for (i = 0; i < c->spawns && i < OW_SPAWN_MAX; i++) {
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

int ow_rules_parse_spawns(const char *json, float *x, float *z, int max)
{
    const char *p = json ? strstr(json, "\"spawns\"") : NULL;
    int n = 0, depth = 0;
    if (!p || max <= 0) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    for (; *p; p++) {
        if (*p == '[') {
            depth++;
            if (depth == 2) {           /* one point: [x, z] */
                char *end;
                double a = strtod(p + 1, &end), b;
                const char *q = end;
                while (*q == ' ' || *q == ',' || *q == '\t' || *q == '\r' || *q == '\n') q++;
                b = strtod(q, &end);
                if (end == q || n >= max) continue;
                if (fabs(a) < 1e5 && fabs(b) < 1e5) { x[n] = (float)a; z[n] = (float)b; n++; }
            }
        } else if (*p == ']') {
            if (--depth <= 0) break;
        }
    }
    return n;
}

int ow_rules_features_answer(const char *json, char *reason, int size)
{
    const char *k = json ? strstr(json, "\"run_the_streets\"") : NULL, *r;
    int yes;
    if (reason && size > 0) reason[0] = 0;
    if (!k) return -1;
    k = strchr(k + 17, ':');
    if (!k) return -1;
    k++;
    while (*k == ' ' || *k == '\t') k++;
    if (!strncmp(k, "true", 4)) yes = 1;
    else if (!strncmp(k, "false", 5)) yes = 0;
    else return -1;
    if (reason && size > 0) {
        r = strstr(json, "\"reason\"");
        if (r && (r = strchr(r + 8, ':')) != NULL && (r = strchr(r, '"')) != NULL) {
            int n = 0;
            r++;
            while (*r && *r != '"' && n < size - 1) reason[n++] = *r++;
            reason[n] = 0;
        }
    }
    return yes;
}

void ow_rules_stick_turn(float heading_deg, float *x, float *y)
{
    float a = heading_deg * 0.0174532925f, c = cosf(a), s = sinf(a), nx, ny;
    /* the stock view: up = -Z, right = +X. Seen from above, a camera turned clockwise by a: its up is the stock
     * up turned clockwise, so the stick vector turns the same way (clockwise = toward the stock right). */
    nx = *x * c + *y * s;
    ny = -*x * s + *y * c;
    *x = nx;
    *y = ny;
}

float ow_rules_view_heading(float eye_x, float eye_z, float at_x, float at_z)
{
    float dx = at_x - eye_x, dz = at_z - eye_z;
    if (dx == 0.0f && dz == 0.0f) return 0.0f;
    /* -Z is 0; +X (to the stock right) is 90: clockwise seen from above with +X right and -Z up */
    return atan2f(dx, -dz) * 57.2957795f;
}

/* Free roam (Run the Streets): the shortest turn from a to b in degrees, -180..180. */
float ow_rules_angle_diff(float a, float b)
{
    float d = fmodf(b - a, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

/* Free roam: the fighter a roaming fighter at x,z facing yaw (degrees; forward is (sin, cos)) locks on to. Among the
 * candidates (bit per slot in cand) within range and cone degrees of the facing, the best by angle off the facing
 * plus a little for distance; else the nearest within near_. Ties go to the lower slot. -1: none. */
int ow_rules_roam_pick(float x, float z, float yaw, const float cx[4], const float cz[4], uint32_t cand, float range,
                       float cone, float near_)
{
    int s, best = -1, close = -1;
    float best_score = 1e30f, close_d = 1e30f;
    for (s = 0; s < 4; s++) {
        float dx, dz, d, off, score;
        if (!(cand & (1u << s))) continue;
        dx = cx[s] - x;
        dz = cz[s] - z;
        d = sqrtf(dx * dx + dz * dz);
        if (d <= near_ && d < close_d) { close = s; close_d = d; }
        if (d > range) continue;
        off = d > 0.001f ? fabsf(ow_rules_angle_diff(yaw, atan2f(dx, dz) * 57.2957795f)) : 0.0f;
        if (off > cone) continue;
        score = off + 30.0f * d / (range > 0.0f ? range : 1.0f);
        if (score < best_score) { best = s; best_score = score; }
    }
    return best >= 0 ? best : close;
}

/* Free roam: the world heading (the record's +0x54 sense) the game's eight-way direction nibble walks to. The
 * walker (sub_001CAC40) maps the nibble through sub_001BAB90 to an index into the table at 0x2EF510
 * (180, 135, 90, 45, 0, -45, -90, -135): up 180 (-Z, away from the north camera), right 90, down 0, left -90.
 * Returns 0 and leaves *yaw for no direction (or both of a pair pressed). */
int ow_rules_nibble_heading(uint32_t nibble, float *yaw)
{
    static const float table[8] = { 180.0f, 135.0f, 90.0f, 45.0f, 0.0f, -45.0f, -90.0f, -135.0f };
    int idx;
    switch (nibble & 0xFu) {
    case 1: idx = 0; break;
    case 9: idx = 1; break;
    case 8: idx = 2; break;
    case 10: idx = 3; break;
    case 2: idx = 4; break;
    case 6: idx = 5; break;
    case 4: idx = 6; break;
    case 5: idx = 7; break;
    default: return 0;
    }
    *yaw = table[idx];
    return 1;
}

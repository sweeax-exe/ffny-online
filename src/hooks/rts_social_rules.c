/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Run the Streets' social layer, the pure part (rts_social_rules.h). */
#include "rts_social_rules.h"

#include <math.h>

float rts_voice_gain(float dist)
{
    float full = RTS_VOICE_FULL_M * RTS_UNITS_PER_M, silent = RTS_VOICE_SILENT_M * RTS_UNITS_PER_M, t;
    if (!(dist > full)) return 1.0f;            /* NaN, negative and near: full */
    if (dist >= silent) return 0.0f;
    t = 1.0f - (dist - full) / (silent - full);
    return t * t;
}

float rts_voice_pan(float lx, float lz, float sx, float sz, float fx, float fz)
{
    float dx = sx - lx, dz = sz - lz, d = sqrtf(dx * dx + dz * dz), fl = sqrtf(fx * fx + fz * fz), pan, near_k;
    if (!(d > 1e-3f) || !(fl > 1e-6f)) return 0.0f;
    /* the camera's right on the ground: looking along -Z, right is +X (the picture's x) */
    pan = (dx * (-fz / fl) + dz * (fx / fl)) / d;
    near_k = d / RTS_UNITS_PER_M;               /* within 1 m: the middle, growing to the full pan at 2 m */
    near_k = near_k <= 1.0f ? 0.0f : near_k >= 2.0f ? 1.0f : near_k - 1.0f;
    pan *= near_k * 0.8f;
    return pan > 0.8f ? 0.8f : pan < -0.8f ? -0.8f : pan;
}

void rts_voice_balance(float pan, float *left, float *right)
{
    if (!(pan == pan)) pan = 0.0f;
    if (pan > 1.0f) pan = 1.0f;
    if (pan < -1.0f) pan = -1.0f;
    *left = pan > 0.0f ? 1.0f - pan : 1.0f;
    *right = pan < 0.0f ? 1.0f + pan : 1.0f;
}

static void sub3(const float a[3], const float b[3], float o[3]) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
static int norm3(float v[3])
{
    float l = sqrtf(dot3(v, v));
    if (!(l > 1e-6f)) return 0;
    v[0] /= l; v[1] /= l; v[2] /= l;
    return 1;
}

int rts_project(const RtsView *v, const float p[3], float *nx, float *ny)
{
    /* D3D's right-handed look-at (the game's view): z back toward the eye, x right, y up */
    static const float up[3] = { 0.0f, 1.0f, 0.0f };
    float zx[3], xx[3], yx[3], d[3], vx, vy, vz, m00, m11, half;
    *nx = *ny = 0.0f;
    if (!v || !(v->fov_deg > 1.0f && v->fov_deg < 179.0f) || !(v->aspect > 0.1f && v->aspect < 10.0f)) return -1;
    sub3(v->eye, v->at, zx);
    if (!norm3(zx)) return -1;
    cross3(up, zx, xx);
    if (!norm3(xx)) return -1;
    cross3(zx, xx, yx);
    sub3(p, v->eye, d);
    vx = dot3(d, xx);
    vy = dot3(d, yx);
    vz = dot3(d, zx);               /* negative in front */
    half = v->fov_deg * 3.14159265f / 360.0f;
    m00 = 1.0f / tanf(half);
    m11 = m00 * v->aspect;
    if (vz < -1.0f) {
        *nx = m00 * vx / -vz;
        *ny = m11 * vy / -vz;
        return 1;
    }
    /* behind (or at) the eye: the direction it lies in, for the marker at the edge */
    {
        float l = sqrtf(vx * vx + vy * vy);
        if (l < 1e-4f) { vx = 0.0f; vy = -1.0f; l = 1.0f; }
        *nx = vx / l * 4.0f;
        *ny = vy / l * 4.0f;
        if (*ny > 0.0f) *ny = -*ny;     /* behind the camera reads as "down there", never up in the sky */
    }
    return 0;
}

int rts_clamp_to_box(float *x, float *y, float x0, float y0, float x1, float y1, int behind)
{
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, dx = *x - cx, dy = *y - cy, k = 1.0f;
    float hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
    if (!(hx > 0.0f && hy > 0.0f)) return 0;
    if (!behind && *x >= x0 && *x <= x1 && *y >= y0 && *y <= y1) return 0;
    if (fabsf(dx) < 1e-4f && fabsf(dy) < 1e-4f) dy = 1.0f;
    if (fabsf(dx) * hy > fabsf(dy) * hx) k = hx / fabsf(dx);
    else k = hy / fabsf(dy);
    *x = cx + dx * k;
    *y = cy + dy * k;
    return 1;
}

float rts_bubble_alpha(unsigned age_ms)
{
    if (age_ms >= RTS_BUBBLE_MS) return 0.0f;
    if (age_ms < 150u) return (float)age_ms / 150.0f;
    if (age_ms > RTS_BUBBLE_MS - 800u) return (float)(RTS_BUBBLE_MS - age_ms) / 800.0f;
    return 1.0f;
}

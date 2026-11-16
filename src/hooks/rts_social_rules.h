/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Run the Streets' social layer, the pure part (rts_social.c reads the game, rts_social_ui.inc draws, voice.c mixes):
 * where a point of the world is on the screen (the chat bubbles and the speaker icons over the fighters), the bubble's
 * fade, and proximity voice's gain and pan from the distance and direction between a talker and the listener.
 * No Windows or game dependencies: tests/unit/test_rts_social.py compiles it natively. */
#ifndef DEFJAM_RTS_SOCIAL_RULES_H
#define DEFJAM_RTS_SOCIAL_RULES_H

#ifdef __cplusplus
extern "C" {
#endif

/* The map is 1170 units for 100 m: one metre is about 11.7 units. */
#define RTS_UNITS_PER_M     11.7f
#define RTS_VOICE_FULL_M    5.0f        /* full volume up to here */
#define RTS_VOICE_SILENT_M  28.0f       /* silent from here on */

/* Proximity voice: the gain (0..1) of a talker `dist` world units away. 1 up to RTS_VOICE_FULL_M, then falling
 * smoothly (squared, so the far half is quiet already) to 0 at RTS_VOICE_SILENT_M. NaN or negative: 1. */
float rts_voice_gain(float dist);

/* The stereo pan (-1 left .. 1 right) of a talker at (sx, sz) heard at (lx, lz) with the camera looking along
 * (fx, fz) (target minus eye, on the ground; need not be unit). Within 1 m it is the middle; never past +-0.8, so
 * the far ear still hears a little. No direction (fx = fz = 0): the middle. */
float rts_voice_pan(float lx, float lz, float sx, float sz, float fx, float fz);
/* The two channels' gains for a pan: 1 and 1 in the middle (as a mono voice today), the far side down to 0. */
void rts_voice_balance(float pan, float *left, float *right);

/* The render camera and its lens: eye and target in world units, the horizontal field of view in degrees and the
 * picture's aspect (width / height) as the game's projection has them (m00 = cot(fov / 2), m11 = m00 * aspect). */
typedef struct RtsView {
    float eye[3], at[3];
    float fov_deg, aspect;
} RtsView;

/* Projects a world point: *nx, *ny in -1..1 across the picture (x right, y up). Returns 1 in front of the camera,
 * 0 behind it (then *nx, *ny point the way it lies, for the edge marker) and -1 for an unusable view. */
int rts_project(const RtsView *v, const float p[3], float *nx, float *ny);

/* A screen point clamped into the box (x0, y0)..(x1, y1) along the line from the box's middle; behind the camera
 * it always goes to the edge. Returns 1 when it was moved (the point is off the screen). */
int rts_clamp_to_box(float *x, float *y, float x0, float y0, float x1, float y1, int behind);

/* A chat bubble's opacity `age_ms` after its line arrived: up within 150 ms, full until 4.2 s, gone at 5 s. */
#define RTS_BUBBLE_MS 5000
float rts_bubble_alpha(unsigned age_ms);

#ifdef __cplusplus
}
#endif

#endif

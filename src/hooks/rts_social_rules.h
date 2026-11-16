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

/* The map is 1170 units for 100 m: one metre is about 11.7 units. A fighter's head is about 13 units over its
 * position (measured against the picture, build 86): the bubbles' tails end just over the head. */
#define RTS_HEAD_UNITS      15.0f
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
 * picture's aspect (width / height) as the game's projection has them (m00 = cot(fov / 2), m11 = m00 * aspect), and
 * the up vector the view is built with (D3D's right-handed look-at). */
typedef struct RtsView {
    float eye[3], at[3];
    float fov_deg, aspect;
    float up[3];            /* the camera's up (the game's third look-at vector); all 0: world up (+Y) */
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

/* The fight chat's keyboard and mouse while the online screens are shut (pc_ui_window_message). Before build 86.1 the
 * window's messages reached the overlay only with the online screens open, so in a fight ENTER never opened the
 * Run the Streets dock (the pad's button, polled, did). `chat`: RTS_CHAT_NONE, RTS_CHAT_BAR (a spectator's bar) or
 * RTS_CHAT_DOCK (the Run the Streets dock); `typing`: the chat has the input now; `vk` the key (0x0D ENTER);
 * `in_chat`: a click lands on the chat's chip or window. Returns RTS_ROUTE_UI (the overlay sees it) and/or
 * RTS_ROUTE_KEEP (the game does not). Releases and moves always reach the game, so nothing it saw pressed stays
 * pressed; ENTER is the dock's (it opens and sends; as the game's START it would pause); while typing every press is
 * the chat's, so the fighter stands still; a click on the chat is the chat's (MOUSE1 punches). ALT+ENTER (a system
 * key) stays the full-screen chord. */
enum { RTS_CHAT_NONE = 0, RTS_CHAT_BAR = 1, RTS_CHAT_DOCK = 2 };
enum { RTS_MSG_OTHER = 0, RTS_MSG_KEYDOWN, RTS_MSG_SYSKEYDOWN, RTS_MSG_KEYUP, RTS_MSG_CHAR, RTS_MSG_MOUSEDOWN,
       RTS_MSG_MOUSEUP, RTS_MSG_MOUSEMOVE };
enum { RTS_ROUTE_UI = 1, RTS_ROUTE_KEEP = 2 };
/* (inline: pc_ui.cpp calls it from the window procedure, and the launcher app builds pc_ui.cpp without this file) */
static inline int rts_chat_route(int chat, int typing, int kind, int vk, int in_chat)
{
    if (chat != RTS_CHAT_BAR && chat != RTS_CHAT_DOCK) return 0;
    switch (kind) {
    case RTS_MSG_KEYDOWN:
        if (typing) return RTS_ROUTE_UI | RTS_ROUTE_KEEP;
        return RTS_ROUTE_UI | (chat == RTS_CHAT_DOCK && vk == 0x0D ? RTS_ROUTE_KEEP : 0);
    case RTS_MSG_CHAR:
        return RTS_ROUTE_UI | (typing ? RTS_ROUTE_KEEP : 0);
    case RTS_MSG_MOUSEDOWN:
        return RTS_ROUTE_UI | (typing || in_chat ? RTS_ROUTE_KEEP : 0);
    case RTS_MSG_SYSKEYDOWN: case RTS_MSG_KEYUP: case RTS_MSG_MOUSEUP: case RTS_MSG_MOUSEMOVE:
        return RTS_ROUTE_UI;
    default:
        return 0;
    }
}

#ifdef __cplusplus
}
#endif

#endif

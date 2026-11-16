/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Run the Streets' social layer, the game's side (rts_social.h). */
#include "host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "netplay.h"
#include "openworld.h"
#include "rts_social.h"
#include "rts_social_rules.h"
#include "patches/widescreen.h"
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define ACTORS      0x003B92E0u
#define STRIDE      0x12E8u
#define RCAM        0x002DBF28u     /* the render camera: eye +0x10, target +0x20 */
#define VIEWS       0x002FB148u     /* the view list; [4] the scene viewport (sub_000443D0) */
#define MATCH_STATE (0x00349760u + 0x2D820u)    /* 5 and 8: the fight's steps run (7 a special move's camera) */

static unsigned long long now_ms(void)
{
#if defined(_WIN32)
    return GetTickCount64();
#else
    return 0;
#endif
}

static int env_on(const char *name)
{
    const char *v = getenv(name);
    return v && *v && *v != '0';
}

/* two copies: the game thread fills the one not shown, then shows it */
static RtsSocialView s_buf[2];
static volatile int s_shown;
static int s_lens_logged;
static float s_lens_last;

/* The scene's lens: the viewport the render camera is applied to (sub_000443D0: slot 4 of the view list at
 * 0x2FB148) holds the camera's horizontal field of view at 4:3 (+0x0C); the widescreen patch widens it (Hor+) for the
 * full-screen view exactly so (ws_view_params), and the picture's aspect is then the canvas's, else 4:3. */
static void scene_lens(float *fov, float *aspect)
{
    uint32_t vp = MEM32(VIEWS + 4u * 4u);
    float f = vp >= 0x10000u ? MEMF(vp + 0x0Cu) : 0.0f, a = ws_aspect();
    if (!(f > 1.0f && f < 179.0f)) f = 30.0f;
    if (a > 0.0f) {
        f = ws_widen_fov(f, WS_DESIGN_ASPECT, a, ws_fov_cap());
        *aspect = a;
    } else {
        *aspect = WS_DESIGN_ASPECT;
    }
    *fov = f;
    if (s_lens_logged < 6 && fabsf(f - s_lens_last) > 0.5f) {
        s_lens_logged++;
        fprintf(stderr, "[RTSSOCIAL] scene lens: fov %.2f, aspect %.4f (viewport %08X, its fov %.2f)\n", (double)f,
                (double)*aspect, vp, (double)(vp >= 0x10000u ? MEMF(vp + 0x0Cu) : 0.0f));
    }
    s_lens_last = f;
}

int rts_social_on(void)
{
    static int test = -1;
    OwHudView hv;
    if (ow_rts_active()) return 1;
    if (test < 0) test = env_on("RECOMP_RTS_SOCIAL_TEST");
    return test && ow_hud_view(&hv) && hv.on;
}

void rts_social_camera(void)
{
    RtsSocialView *v;
    uint32_t st;
    int s, i, w;
    if (!rts_social_on()) {
        if (s_buf[s_shown].on) s_buf[s_shown].on = 0;
        return;
    }
    w = s_shown ^ 1;
    v = &s_buf[w];
    memset(v, 0, sizeof *v);
    st = MEM32(MATCH_STATE);
    v->on = st == 5 || st == 7 || st == 8;
    for (i = 0; i < 3; i++) { v->eye[i] = MEMF(RCAM + 0x10u + 4u * i); v->at[i] = MEMF(RCAM + 0x20u + 4u * i); }
    for (i = 0; i < 3; i++) v->up[i] = MEMF(RCAM + 0x30u + 4u * i);
    scene_lens(&v->fov_deg, &v->aspect);
    for (s = 0; s < 4; s++) {
        uint32_t r = ACTORS + (uint32_t)s * STRIDE;
        if (MEM32(r + 0x28u) == 0xFFFFFFFFu) continue;
        v->present |= 1u << s;
        v->pos[s][0] = MEMF(r + 0x44u);
        v->pos[s][1] = MEMF(r + 0x48u);
        v->pos[s][2] = MEMF(r + 0x4Cu);
    }
    v->local = netplay_local_slot();
    if (v->local < 0 && !netplay_active()) v->local = 0;     /* offline: pad 0's fighter */
    if (v->local >= 4 || (v->local >= 0 && !(v->present & (1u << v->local)))) v->local = -1;
    v->at_ms = now_ms();
    s_shown = w;
}

int rts_social_view(RtsSocialView *out)
{
    *out = s_buf[s_shown];
    if (!out->on || !out->at_ms) return 0;
    if (now_ms() - out->at_ms > 500u) return 0;     /* the fight is gone (no frame set the camera since) */
    return 1;
}

int rts_social_voice_stereo(void)
{
    return netplay_rts_room() || env_on("RECOMP_RTS_SOCIAL_TEST");
}

int rts_social_voice(int room_slot, float *gain, float *pan)
{
    static int logging = -1;
    static unsigned long long logged_at[NETPLAY_ROOM_SLOTS];
    RtsSocialView v;
    const float *lis;
    float dx, dz, dist, fx, fz;
    int f;
    *gain = 1.0f;
    *pan = 0.0f;
    if (room_slot < 0 || room_slot >= NETPLAY_ROOM_SLOTS || !rts_social_view(&v)) return 0;
    f = netplay_fighter_of_room_slot(room_slot);
    if (f < 0 || f >= 4 || !(v.present & (1u << f)) || f == v.local) return 0;
    lis = v.local >= 0 ? v.pos[v.local] : v.at;     /* a spectator hears from where the camera looks */
    dx = v.pos[f][0] - lis[0];
    dz = v.pos[f][2] - lis[2];
    dist = sqrtf(dx * dx + dz * dz);
    fx = v.at[0] - v.eye[0];
    fz = v.at[2] - v.eye[2];
    *gain = rts_voice_gain(dist);
    *pan = rts_voice_pan(lis[0], lis[2], v.pos[f][0], v.pos[f][2], fx, fz);
    if (logging < 0) logging = env_on("RECOMP_RTS_VOICE_LOG");
    if (logging) {
        unsigned long long t = now_ms();
        if (t - logged_at[room_slot] >= 2000u) {
            logged_at[room_slot] = t;
            fprintf(stderr, "[RTSVOICE] slot %d fighter %d dist %.0f (%.1f m) gain %.2f pan %.2f listener %s\n", room_slot, f,
                    (double)dist, (double)(dist / RTS_UNITS_PER_M), (double)*gain, (double)*pan,
                    v.local >= 0 ? "fighter" : "camera");
        }
    }
    return 1;
}

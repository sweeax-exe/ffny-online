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
#include "fight_hud.h"
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

/* The scene's lens: the view the render camera is applied to (sub_000443D0: slot 4 of the view list at 0x2FB148)
 * has its viewport at +0x34, which holds the camera's horizontal field of view at 4:3 (+0x0C, set by sub_000190C0);
 * the widescreen patch widens it (Hor+) for the full-screen view exactly so (ws_view_params), and the picture's
 * aspect is then the canvas's, else 4:3. Checked against the projection the viewport builds (its device view +0x40:
 * m00 = cot(fov / 2) 2.532, m11 = m00 * aspect 4.501 at fov 43.1 and 16:9, build 86 test run). */
static void scene_lens(float *fov, float *aspect)
{
    uint32_t view = MEM32(VIEWS + 4u * 4u), vp = view >= 0x10000u ? MEM32(view + 0x34u) : 0u;
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

/* RECOMP_RTS_CAM_LOG=1: the camera, the lens and where the local fighter's feet and head land, every 3 s */
static void cam_log(const RtsSocialView *v)
{
    static int on = -1;
    static unsigned long long at;
    RtsView rv;
    float feet[3], head[3], fx = 0, fy = 0, hx = 0, hy = 0;
    int s = v->local >= 0 ? v->local : 0;
    if (on < 0) on = env_on("RECOMP_RTS_CAM_LOG");
    if (!on || !v->on || v->at_ms - at < 3000u) return;
    at = v->at_ms;
    memcpy(rv.eye, v->eye, sizeof rv.eye);
    memcpy(rv.at, v->at, sizeof rv.at);
    memcpy(rv.up, v->up, sizeof rv.up);
    rv.fov_deg = v->fov_deg;
    rv.aspect = v->aspect;
    memcpy(feet, v->pos[s], sizeof feet);
    memcpy(head, v->pos[s], sizeof head);
    head[1] += RTS_HEAD_UNITS;
    rts_project(&rv, feet, &fx, &fy);
    rts_project(&rv, head, &hx, &hy);
    fprintf(stderr, "[RTSCAM] eye %.1f,%.1f,%.1f at %.1f,%.1f,%.1f fov %.2f aspect %.3f | fighter %d feet %.3f,%.3f head %.3f,%.3f\n",
            (double)v->eye[0], (double)v->eye[1], (double)v->eye[2], (double)v->at[0], (double)v->at[1], (double)v->at[2],
            (double)v->fov_deg, (double)v->aspect, s, (double)fx, (double)fy, (double)hx, (double)hy);
}

/* The minimap's walls: the collision planes (0x3BF1B8 base, 0x3BF1C0 count, 0x48 bytes each: four x/y/z corners at
 * +0x04, the normal at +0x34; the .cli layout of ffnystudio/collision.py), each upright one as the segment between
 * its two corners farthest apart on x/z. Copied on the game thread when the planes change; the render thread reads
 * the count last written (the segments before it are complete). Reading only. */
#define WALLS_MAX 512
static float s_walls[WALLS_MAX][4];
static volatile LONG s_walls_n;
static uint32_t s_walls_base, s_walls_count;

static void walls_copy(void)
{
    uint32_t base = MEM32(0x003BF1B8u), n = MEM32(0x003BF1C0u), i;
    int k = 0, a, b;
    if (base == s_walls_base && n == s_walls_count) return;
    s_walls_base = base;
    s_walls_count = n;
    InterlockedExchange(&s_walls_n, 0);
    if (!base || !n || n > 100000u) return;
    for (i = 0; i < n && k < WALLS_MAX; i++) {
        uint32_t p = base + i * 0x48u;
        float best = -1.0f;
        int ba = 0, bb = 1;
        if (fabsf(MEMF(p + 0x38u)) >= 0.7f) continue;      /* a floor or a ceiling */
        for (a = 0; a < 4; a++)
            for (b = a + 1; b < 4; b++) {
                float dx = MEMF(p + 4u + 12u * (uint32_t)a) - MEMF(p + 4u + 12u * (uint32_t)b);
                float dz = MEMF(p + 12u + 12u * (uint32_t)a) - MEMF(p + 12u + 12u * (uint32_t)b);
                if (dx * dx + dz * dz > best) { best = dx * dx + dz * dz; ba = a; bb = b; }
            }
        if (!(best > 1.0f && best < 1e10f)) continue;
        s_walls[k][0] = MEMF(p + 4u + 12u * (uint32_t)ba);
        s_walls[k][1] = MEMF(p + 12u + 12u * (uint32_t)ba);
        s_walls[k][2] = MEMF(p + 4u + 12u * (uint32_t)bb);
        s_walls[k][3] = MEMF(p + 12u + 12u * (uint32_t)bb);
        k++;
    }
    InterlockedExchange(&s_walls_n, k);
}

int rts_social_walls(float (*seg)[4], int max)
{
    int n = (int)s_walls_n, i;
    if (n > max) n = max;
    for (i = 0; i < n; i++) memcpy(seg[i], s_walls[i], sizeof seg[i]);
    return n;
}

/* Build 91: when each fighter last was in combat (GetTickCount64; 0 none). Written by the hit hook and by the grab
 * poll below, both on the game thread; host memory the fight never reads, so rollback re-runs only refresh it. */
static unsigned long long s_combat_ms[4];

void rts_social_combat(uint32_t attacker, uint32_t defender)
{
    unsigned long long t;
    if (attacker < ACTORS || defender < ACTORS || !rts_social_on()) return;
    t = now_ms();
    if (attacker < ACTORS + 4u * STRIDE && (attacker - ACTORS) % STRIDE == 0) s_combat_ms[(attacker - ACTORS) / STRIDE] = t;
    if (defender < ACTORS + 4u * STRIDE && (defender - ACTORS) % STRIDE == 0) s_combat_ms[(defender - ACTORS) / STRIDE] = t;
}

void rts_social_camera(void)
{
    RtsSocialView *v;
    uint32_t st;
    int s, i, w;
    if (!rts_social_on()) {
        if (s_buf[s_shown].on) s_buf[s_shown].on = 0;
        memset(s_combat_ms, 0, sizeof s_combat_ms);
        return;
    }
    w = s_shown ^ 1;
    v = &s_buf[w];
    memset(v, 0, sizeof *v);
    st = MEM32(MATCH_STATE);
    {   /* the fight runs and its HUD is up: not the intros, the knockout's end or the results */
        FightHudView hv;
        fight_hud_view(&hv);
        v->on = (st == 5 || st == 7 || st == 8) && hv.live;
    }
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
        v->yaw[s] = MEMF(r + 0x54u);
        if (MEM8(r + 0x937u) & 1u) v->cpu |= 1u << s;
        {   /* a grab starting, holding or held (0x1E, 0x34, 0x35: combo-log.md, match-end-state.md) is combat too */
            uint32_t st = MEM32(r + 0x30u);
            if (st == 0x1Eu || st == 0x34u || st == 0x35u) s_combat_ms[s] = now_ms();
        }
        v->combat_ms[s] = s_combat_ms[s];
    }
    walls_copy();
    v->local = netplay_local_slot();
    if (v->local < 0 && !netplay_active()) v->local = 0;     /* offline: pad 0's fighter */
    if (v->local >= 4 || (v->local >= 0 && !(v->present & (1u << v->local)))) v->local = -1;
    v->roam_mode = OW_ROAM_FREE;
    v->lock_target = -1;
    if (v->local >= 0) ow_roam_view(v->local, &v->roam_mode, &v->lock_target);
    if (v->lock_target >= 0 && !(v->present & (1u << v->lock_target))) v->lock_target = -1;
    v->at_ms = now_ms();
    cam_log(v);
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

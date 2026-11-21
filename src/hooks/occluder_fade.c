/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Camera occluder fade for new (Studio-made) venues. VISUAL ONLY.
 *
 * Geometry of an imported map (a house, a roof, a ceiling) can stand between the fight camera and the
 * fighters and cover the screen. Each frame this takes the segment from the camera's eye to the fighters'
 * chest height and draws every venue batch whose bounding box the segment crosses at alpha 0.25, eased in
 * and out over 0.15 s. The floor and batches wholly below knee height are never faded.
 *
 * Where: the push-buffer executor's GPU vertex-program draw (tools/xboxrecomp nv2a_pb_exec.c) asks
 * nv2a_set_draw_filter's function for an alpha per batch, on the executor (render) thread, after the guest
 * has built the batch. Nothing here writes guest memory or any state the simulation, the rollback or the
 * checksums read; guest memory is only read (camera and fighter positions, as camera_watch.c does).
 *
 * Which batches: a venue's main model is drawn at the identity transform, so a batch whose vertex-program
 * constants hold the camera's view-projection matrix (the game's EAGL::ViewPort::gpViewProjectionMatrix,
 * guest 0x3A5260) has world-space vertices. Their box is cached per batch (keyed by a hash of its
 * counts and sampled vertices) the first time it draws. Stock venues (front-end ID below 28) are untouched.
 *
 * [graphics] occluder_fade = true (default) / false; RECOMP_OCCLUDER_FADE=0 forces it off;
 * RECOMP_OCCLUDER_TRACE=1 logs what it picks. */
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "recomp/gen/recomp_types.h"
#include "../../tools/xboxrecomp/src/nv2a/nv2a_pgraph_d3d11.h"

int recomp_settings_get(const char *section, const char *key, int def);

#define CAM_DIRECTOR  0x003772A4u
#define ACTORS        0x003B92E0u
#define STRIDE        0x12E8u
#define VIEWPROJ      0x003A5260u

#define FADE_ALPHA    0.25f
#define FADE_SECONDS  0.15f
#define CHEST         5.0f     /* above a fighter's feet (venue units) */
#define KNEE          1.5f     /* a batch wholly below this is floor */
#define MAXB          512

typedef struct { uint32_t key; float lo[3], hi[3]; float alpha; unsigned seen; int skip; } Box;

static Box s_box[MAXB];
static int s_on = -1, s_active;
static unsigned s_frame, s_nowms;
static float s_eye[3], s_foc[3];
static unsigned s_last_ms;
static float s_dt = 1.0f;
static int s_trace;
static volatile int s_venue_fe;     /* Game.ChooseVenue's front-end ID, noted from the front end's call text */

/* Guest thread: only remembers the venue the menus chose; no guest memory is touched. */
void defjam_occluder_note_call(const char *text)
{
    if (!strncmp(text, "Game.ChooseVenue(", 17)) s_venue_fe = atoi(text + 17);
}

static unsigned now_ms(void);
#ifdef _WIN32
#include <windows.h>
static unsigned now_ms(void) { return (unsigned)GetTickCount64(); }
#else
#include <time.h>
static unsigned now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (unsigned)(t.tv_sec * 1000u + t.tv_nsec / 1000000u); }
#endif

/* Slab test: does the segment a..b cross the box? */
static int seg_box(const float *a, const float *b, const float *lo, const float *hi)
{
    float t0 = 0.0f, t1 = 1.0f;
    int i;
    for (i = 0; i < 3; i++) {
        float d = b[i] - a[i];
        if (fabsf(d) < 1e-6f) {
            if (a[i] < lo[i] || a[i] > hi[i]) return 0;
        } else {
            float ta = (lo[i] - a[i]) / d, tb = (hi[i] - a[i]) / d, tmp;
            if (ta > tb) { tmp = ta; ta = tb; tb = tmp; }
            if (ta > t0) t0 = ta;
            if (tb < t1) t1 = tb;
            if (t0 > t1) return 0;
        }
    }
    return 1;
}

/* Called by the host at the end of every presented frame (render thread). */
void defjam_occluder_frame_end(void)
{
    int n = 0, i;
    float sum[3] = { 0, 0, 0 };
    s_frame++;
    if ((s_frame & 31u) == 1u) {
        const char *e = getenv("RECOMP_OCCLUDER_FADE");
        s_on = recomp_settings_get("graphics", "occluder_fade", 1) != 0;
        if (e && *e == '0') s_on = 0;
        s_trace = getenv("RECOMP_OCCLUDER_TRACE") != NULL;
    }
    s_nowms = now_ms();
    s_dt = s_last_ms ? (s_nowms - s_last_ms) / 1000.0f : 1.0f;
    s_last_ms = s_nowms;
    s_active = 0;
    if (s_on <= 0) return;
    {
        if (getenv("RECOMP_OCCLUDER_ANY") == NULL && s_venue_fe < 28) return;   /* stock venues untouched */
    }
    for (i = 0; i < 4; i++) {
        uint32_t f = ACTORS + (uint32_t)i * STRIDE;
        if (MEM32(f + 0x28u) == 0xFFFFFFFFu || (MEM16(f + 0x932u) & 0x200u)) continue;
        sum[0] += MEMF(f + 0x44u); sum[1] += MEMF(f + 0x48u); sum[2] += MEMF(f + 0x4Cu);
        n++;
    }
    if (!n) return;
    s_foc[0] = sum[0] / n; s_foc[1] = sum[1] / n + CHEST; s_foc[2] = sum[2] / n;
    s_eye[0] = MEMF(CAM_DIRECTOR + 4u); s_eye[1] = MEMF(CAM_DIRECTOR + 8u); s_eye[2] = MEMF(CAM_DIRECTOR + 0xCu);
    if (!(fabsf(s_eye[0]) < 1e5f && fabsf(s_eye[1]) < 1e5f && fabsf(s_eye[2]) < 1e5f)) return;
    s_active = 1;
    if (s_trace && (s_frame % 120u) == 0)
        fprintf(stderr, "[OCCLUDER] venue %u eye %.1f,%.1f,%.1f focus %.1f,%.1f,%.1f\n", s_venue_fe,
                s_eye[0], s_eye[1], s_eye[2], s_foc[0], s_foc[1], s_foc[2]);
}

static int is_viewproj(const float *c)
{
    /* The batch's composite matrix is the camera's view-projection (either layout): find 16 floats
     * in the first 32 constants that equal it or its transpose. */
    float vp[16];
    int o, i, j;
    for (i = 0; i < 16; i++) vp[i] = MEMF(VIEWPROJ + 4u * (uint32_t)i);
    for (o = 0; o + 16 <= 32 * 4; o += 4) {
        int same = 1, tr = 1;
        for (i = 0; i < 4 && (same || tr); i++)
            for (j = 0; j < 4; j++) {
                float v = c[o + i * 4 + j];
                if (fabsf(v - vp[i * 4 + j]) > 1e-3f * (1.0f + fabsf(v))) same = 0;
                if (fabsf(v - vp[j * 4 + i]) > 1e-3f * (1.0f + fabsf(v))) tr = 0;
            }
        if (same || tr) return 1;
    }
    return 0;
}

float defjam_occluder_filter(const float *v, uint32_t nv, uint32_t used, const uint16_t *idx, uint32_t nidx,
                             const float *consts)
{
    uint32_t per = 0, k, key = 2166136261u;
    Box *b = NULL;
    int i, hit;
    if (!s_active || !(used & 1u) || nv < 8 || !consts) return 1.0f;
    for (k = 0; k < 16; k++) per += (used >> k) & 1u;
    key = (key ^ nv) * 16777619u; key = (key ^ nidx) * 16777619u; key = (key ^ used) * 16777619u;
    for (k = 0; k < 8; k++) {
        const float *p = v + (size_t)((uint64_t)nv * k / 8) * per * 4;
        uint32_t u; memcpy(&u, p, 4); key = (key ^ u) * 16777619u;
        memcpy(&u, p + 1, 4); key = (key ^ u) * 16777619u;
        memcpy(&u, p + 2, 4); key = (key ^ u) * 16777619u;
    }
    if (!key) key = 1;
    for (i = 0; i < MAXB; i++) {
        Box *c = &s_box[(key + (uint32_t)i) % MAXB];
        if (c->key == key) { b = c; break; }
        if (!c->key) { b = c; break; }
    }
    if (!b) { memset(s_box, 0, sizeof s_box); return 1.0f; }  /* table full (menu batches change every frame): start over */
    if (b->key != key) {                                    /* first sight: the box and the verdict */
        memset(b, 0, sizeof *b);
        b->key = key;
        b->alpha = 1.0f;
        b->skip = !is_viewproj(consts);
        if (!b->skip) {
            b->lo[0] = b->lo[1] = b->lo[2] = 1e30f; b->hi[0] = b->hi[1] = b->hi[2] = -1e30f;
            for (k = 0; k < nv; k++) {
                const float *p = v + (size_t)k * per * 4;
                int a;
                for (a = 0; a < 3; a++) {
                    if (p[a] < b->lo[a]) b->lo[a] = p[a];
                    if (p[a] > b->hi[a]) b->hi[a] = p[a];
                }
            }
            if (b->hi[1] < KNEE) b->skip = 1;               /* the floor, or low enough */
        }
        if (s_trace)
            fprintf(stderr, "[OCCLUDER] batch %08X: %u verts, %s, box %.1f,%.1f,%.1f .. %.1f,%.1f,%.1f\n", key, nv,
                    b->skip ? "skipped" : "candidate", b->lo[0], b->lo[1], b->lo[2], b->hi[0], b->hi[1], b->hi[2]);
    }
    if (b->skip) return 1.0f;
    {
        /* Ease by wall time, once per frame per batch. */
        float step = s_dt / FADE_SECONDS * (1.0f - FADE_ALPHA);
        if (b->seen != s_frame) {
            float goal;
            b->seen = s_frame;
            /* A batch whose box holds the fighters (merged ground and walls) is never the thing in the way. */
            hit = seg_box(s_eye, s_foc, b->lo, b->hi) && !(s_foc[0] > b->lo[0] - 1.0f && s_foc[0] < b->hi[0] + 1.0f &&
                  s_foc[1] > b->lo[1] - 1.0f && s_foc[1] < b->hi[1] + 1.0f &&
                  s_foc[2] > b->lo[2] - 1.0f && s_foc[2] < b->hi[2] + 1.0f);
            goal = hit ? FADE_ALPHA : 1.0f;
            if (step <= 0.0f || step > 1.0f) step = 1.0f;
            if (b->alpha > goal) b->alpha = b->alpha - step < goal ? goal : b->alpha - step;
            else b->alpha = b->alpha + step > goal ? goal : b->alpha + step;
        }
    }
    return b->alpha >= 0.999f ? 1.0f : b->alpha;
}

void defjam_occluder_install(void)
{
    nv2a_set_draw_filter(defjam_occluder_filter);
}

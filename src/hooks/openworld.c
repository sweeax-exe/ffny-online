/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Open-world mode, phase 0: measurements and the first prototypes (community/open-world-phase0-results.md).
 * Everything here is off unless its RECOMP_OW_* switch is set.
 *
 * RECOMP_OW_PROF=1          collision and step cost. Wraps the fight step sub_00087700, the per-fighter plane
 *                           sweep sub_001AC490 (called from sub_001AD110) and counts the plane test sub_001ABFA0.
 *                           Every 600 normal steps: "[OWPROF] planes N | step avg/max us | sweeps/step, sweep
 *                           us/step | tests/step | re-run steps ...". Re-run steps (rollback, RECOMP_ROLLBACK_SOAK)
 *                           are counted apart (netplay_rerunning()).
 * RECOMP_OW_TRACK=<steps>   every <steps> fight steps one line per fighter: position, yaw, health, +0x932 flags,
 *                           state (+0x30), cached floor height (+0x3A4), primary opponent slot.
 * RECOMP_OW_TELEPORT=list   tests only, never online: "<step>@<slot>=<x>,<z>" puts a fighter at x,z at that fight
 *                           step; "<step>@all=<dx>,<dz>" moves every fighter by dx,dz; "<step>@spread=<d>" puts the
 *                           fighters on a square of side d around the mean of their positions. ';' separates entries.
 * RECOMP_OW_KO=<step>:<slot> tests, never online: that fighter is knocked out at that fight step (zero health, the
 *                           knockout state 0x18 through sub_001B5420, as test_telemetry.c's ko_step).
 * RECOMP_OW_REVIVE=<steps>  tests, never online: a fighter that has been "ended" (+0x932 bit 0x200) for <steps>
 *                           steps stands up again: health from sub_001A4F30 (the match set-up's health init), the
 *                           defeat bits cleared, state 1 (free) through sub_001B5420, put at a spawn point.
 * RECOMP_OW_FOLLOWCAM=mode[,dist,height[,slot]]
 *                           a per-PC follow camera on the local fighter (online: netplay_local_slot(), offline: the
 *                           given slot, default 0). mode "fixed": the stock heading (looks along -Z), the eye dist
 *                           behind (+Z) and height above the fighter; mode "behind": behind the fighter's smoothed
 *                           facing. Host side only: after the game's camera wrapper sub_000AC590 has written the
 *                           render camera 0x2DBF28 (eye +0x10, target +0x20) and applied it (sub_000443D0), this
 *                           writes its own eye and target there and applies it again. The camera director
 *                           (0x3772A4), which the next step reads, is never touched; the smoothing state is host
 *                           memory and does not advance in re-run steps.
 */
#include "host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "guest_call.h"
#include "patches/online_guard.h"

#define ACTORS      0x003B92E0u
#define STRIDE      0x12E8u
#define PLANES_PTR  0x003BF1B8u
#define PLANES_N    0x003BF1C0u
#define RCAM        0x002DBF28u     /* the render camera: eye +0x10, target +0x20 (sub_0002ACA0 / sub_0002ACC0) */

static int env_int(const char *name, int dflt)
{
    const char *v = getenv(name);
    return v && *v ? atoi(v) : dflt;
}

static uint32_t rec(int slot) { return ACTORS + (uint32_t)slot * STRIDE; }
static int present(int slot) { return MEM32(rec(slot) + 0x28u) != 0xFFFFFFFFu; }

static double qpc_us(void)
{
    static double inv = 0;
    LARGE_INTEGER t;
    if (inv == 0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); inv = 1e6 / (double)f.QuadPart; }
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * inv;
}

/* ---------------------------------------------------------------- profiling */
static int s_prof = -1;
static struct {
    unsigned steps, rsteps, sweeps, rsweeps;
    unsigned long long tests, rtests;
    double step_us, step_max, rstep_us, rstep_max, sweep_us, rsweep_us;
    unsigned contacts, contacts_hi, contact_max;    /* plane contacts (+0xAB0/AB4/AB8) seen after normal steps */
} P;

/* The plane contacts the sweep left in a fighter (+0xAB0/+0xAB4/+0xAB8 hold plane pointers, wall-stagger.md):
 * counted by index, so a run shows whether planes past index 255 (whose flags byte no longer holds the index)
 * are hit at all. */
static void contacts_count(void)
{
    uint32_t base = MEM32(PLANES_PTR), n = MEM32(PLANES_N), s, k;
    if (!base || !n) return;
    for (s = 0; s < 4; s++) {
        if (!present((int)s)) continue;
        for (k = 0; k < 3; k++) {
            uint32_t q = MEM32(rec((int)s) + 0xAB0u + 4u * k), idx;
            if (q < base || q >= base + n * 0x48u || (q - base) % 0x48u) continue;
            idx = (q - base) / 0x48u;
            P.contacts++;
            if (idx >= 256) P.contacts_hi++;
            if (idx > P.contact_max) P.contact_max = idx;
        }
    }
}
static long long s_steps_total;         /* normal steps since start (for the track/teleport step) */
static long long s_fight_step;          /* normal steps of the current fight */
static double s_last_step_t;

static int prof_on(void)
{
    if (s_prof < 0) s_prof = env_int("RECOMP_OW_PROF", 0);
    return s_prof > 0;
}

static void prof_report(void)
{
    double n = P.steps ? (double)P.steps : 1.0, r = P.rsteps ? (double)P.rsteps : 1.0;
    fprintf(stderr, "[OWPROF] planes %u | %u steps: step %.1f us avg, %.1f max | sweeps %.2f/step, sweep %.1f us/step "
            "(%.3f us each) | plane tests %.0f/step | re-run %u steps: step %.1f us avg, %.1f max, sweep %.1f us/step, "
            "tests %.0f/step\n",
            MEM32(PLANES_N), P.steps, P.step_us / n, P.step_max, P.sweeps / n, P.sweep_us / n,
            P.sweeps ? P.sweep_us / P.sweeps : 0.0, (double)P.tests / n, P.rsteps, P.rstep_us / r, P.rstep_max,
            P.rsweep_us / r, (double)P.rtests / r, P.contacts, P.contacts_hi, P.contact_max);
    memset(&P, 0, sizeof P);
}

/* ---------------------------------------------------------------- tracking */
static void track_line(void)
{
    char line[1200];
    int n = 0, s;
    n += snprintf(line + n, sizeof line - (size_t)n, "[OWTRACK] step %lld planes %u floor0 %.2f |", s_fight_step,
                  MEM32(PLANES_N), (double)MEMF(0x003BF1CCu));
    for (s = 0; s < 4 && n > 0 && n < (int)sizeof line; s++) {
        uint32_t r = rec(s), t;
        int ts = -1;
        if (!present(s)) continue;
        t = MEM32(r);
        if (t >= ACTORS && t < ACTORS + 4 * STRIDE) ts = (int)((t - ACTORS) / STRIDE);
        n += snprintf(line + n, sizeof line - (size_t)n,
                      " %d: %.2f,%.2f,%.2f yaw %.1f hp %.1f/%.1f fl %04X st %u floor %.2f tgt %d;", s,
                      (double)MEMF(r + 0x44u), (double)MEMF(r + 0x48u), (double)MEMF(r + 0x4Cu), (double)MEMF(r + 0x54u),
                      (double)MEMF(r + 0x350u), (double)MEMF(r + 0x354u), MEM16(r + 0x932u), MEM32(r + 0x30u),
                      (double)MEMF(r + 0x3A4u), ts);
    }
    fprintf(stderr, "%s\n", line);
}

/* ---------------------------------------------------------------- teleport (tests) */
static void teleport_step(void)
{
    static const char *list = (const char *)1;
    const char *p;
    if (list == (const char *)1) list = getenv("RECOMP_OW_TELEPORT");
    if (!list || ffny_online_locked()) return;
    for (p = list; p && *p; p = strchr(p, ';') ? strchr(p, ';') + 1 : NULL) {
        long long at = atoll(p);
        const char *who = strchr(p, '@'), *eq = strchr(p, '=');
        int s;
        if (!who || !eq || at != s_fight_step) continue;
        who++;
        if (!strncmp(who, "all", 3)) {
            float dx = 0, dz = 0;
            sscanf(eq + 1, "%f,%f", &dx, &dz);
            for (s = 0; s < 4; s++)
                if (present(s)) { MEMF(rec(s) + 0x44u) += dx; MEMF(rec(s) + 0x4Cu) += dz; }
            fprintf(stderr, "[OWTELE] step %lld: every fighter moved by %.1f,%.1f\n", s_fight_step, dx, dz);
        } else if (!strncmp(who, "spread", 6)) {
            float d = (float)atof(eq + 1), mx = 0, mz = 0;
            int k = 0;
            static const float sx[4] = { -0.5f, 0.5f, -0.5f, 0.5f }, sz[4] = { -0.5f, -0.5f, 0.5f, 0.5f };
            for (s = 0; s < 4; s++) if (present(s)) { mx += MEMF(rec(s) + 0x44u); mz += MEMF(rec(s) + 0x4Cu); k++; }
            if (!k) continue;
            mx /= (float)k; mz /= (float)k;
            for (s = 0; s < 4; s++)
                if (present(s)) { MEMF(rec(s) + 0x44u) = mx + sx[s] * d; MEMF(rec(s) + 0x4Cu) = mz + sz[s] * d; }
            fprintf(stderr, "[OWTELE] step %lld: fighters spread on a square of side %.1f around %.1f,%.1f\n",
                    s_fight_step, d, mx, mz);
        } else {
            float x = 0, z = 0;
            s = atoi(who);
            if (s < 0 || s > 3 || !present(s)) continue;
            sscanf(eq + 1, "%f,%f", &x, &z);
            MEMF(rec(s) + 0x44u) = x;
            MEMF(rec(s) + 0x4Cu) = z;
            fprintf(stderr, "[OWTELE] step %lld: slot %d put at %.1f,%.1f\n", s_fight_step, s, x, z);
        }
    }
}

/* ---------------------------------------------------------------- knockout and revive (tests) */
extern void sub_001B5420(void);     /* (record, state): the state set and restarted */
extern void sub_001A4F30(void);     /* (record): health, max health, ratio, momentum as at match set-up */

static void call2(void (*fn)(void), int argc, uint32_t a0, uint32_t a1)
{
    uint32_t eax_ = g_eax, ecx_ = g_ecx, edx_ = g_edx, ebx_ = g_ebx, esi_ = g_esi, edi_ = g_edi,
             ebp_ = g_ebp, sp = g_esp;
    double fp[8];
    int fp_top = g_fp_top;
    uint16_t fp_cw = g_fp_control_word;
    memcpy(fp, g_fp_stack, sizeof(fp));
    g_esp = (sp - 0x80u) & ~3u;
    if (argc > 1) PUSH32(g_esp, a1);
    PUSH32(g_esp, a0);
    PUSH32(g_esp, 0u);
    fn();
    g_eax = eax_; g_ecx = ecx_; g_edx = edx_; g_ebx = ebx_; g_esi = esi_; g_edi = edi_; g_ebp = ebp_;
    g_esp = sp;
    memcpy(g_fp_stack, fp, sizeof(fp));
    g_fp_top = fp_top;
    g_fp_control_word = fp_cw;
}

static void ko_revive_step(void)
{
    static int parsed, ko_slot = -1, revive = 0;
    static long long ko_at;
    static long long ended_since[4];
    static unsigned revived;
    int s;
    if (!parsed) {
        const char *v = getenv("RECOMP_OW_KO");
        parsed = 1;
        if (v && strchr(v, ':')) { ko_at = atoll(v); ko_slot = atoi(strchr(v, ':') + 1); }
        revive = env_int("RECOMP_OW_REVIVE", 0);
    }
    if (ffny_online_locked()) return;
    if (s_fight_step == 1) memset(ended_since, 0, sizeof ended_since);
    if (ko_slot >= 0 && s_fight_step == ko_at && present(ko_slot)) {
        uint32_t r = rec(ko_slot);
        MEMF(r + 0x350u) = 0.0f;
        MEMF(r + 0x35Cu) = 0.0f;
        MEM8(r + 0x943u) = 1;
        call2(sub_001B5420, 2, r, 0x18u);
        fprintf(stderr, "[OWKO] step %lld: slot %d knocked out (state %u, flags %04X)\n", s_fight_step, ko_slot,
                MEM32(r + 0x30u), MEM16(r + 0x932u));
    }
    if (revive <= 0) return;
    for (s = 0; s < 4; s++) {
        uint32_t r = rec(s);
        if (!present(s)) continue;
        if (!(MEM16(r + 0x932u) & 0x200u)) { ended_since[s] = 0; continue; }
        if (!ended_since[s]) {
            ended_since[s] = s_fight_step;
            fprintf(stderr, "[OWREVIVE] step %lld: slot %d ended (flags %04X, state %u, hp %.1f)\n", s_fight_step, s,
                    MEM16(r + 0x932u), MEM32(r + 0x30u), (double)MEMF(r + 0x350u));
        }
        if (s_fight_step - ended_since[s] < revive) continue;
        {
            static const float spawn[4][2] = { { -40.f, -20.f }, { 40.f, -20.f }, { -40.f, 25.f }, { 40.f, 25.f } };
            uint16_t before = MEM16(r + 0x932u);
            MEM16(r + 0x932u) = (uint16_t)(before & ~(0x01u | 0x04u | 0x10u | 0x20u | 0x80u | 0x200u | 0x400u));
            MEM8(r + 0x943u) = 0;
            call2(sub_001A4F30, 1, r, 0);
            call2(sub_001B5420, 2, r, 1u);
            MEMF(r + 0x44u) = spawn[s][0];
            MEMF(r + 0x4Cu) = spawn[s][1];
            ended_since[s] = 0;
            revived++;
            fprintf(stderr, "[OWREVIVE] step %lld: slot %d revived (#%u): flags %04X->%04X, hp %.1f/%.1f, state %u\n",
                    s_fight_step, s, revived, before, MEM16(r + 0x932u), (double)MEMF(r + 0x350u),
                    (double)MEMF(r + 0x354u), MEM32(r + 0x30u));
        }
    }
}

/* ---------------------------------------------------------------- the wrapped functions */
void ow_step(void (*gen)(void))
{
    static int track = -1;
    int re = netplay_rerunning();
    double t0, t1;
    if (track < 0) track = env_int("RECOMP_OW_TRACK", 0);
    if (!re) {
        double now = qpc_us();
        if (now - s_last_step_t > 1e6) s_fight_step = 0;     /* a gap of a second: a new fight */
        s_last_step_t = now;
        s_fight_step++;
        s_steps_total++;
        teleport_step();
        ko_revive_step();
    }
    if (!prof_on()) {
        gen();
    } else {
        t0 = qpc_us();
        gen();
        t1 = qpc_us() - t0;
        if (!re) contacts_count();
        if (re) { P.rsteps++; P.rstep_us += t1; if (t1 > P.rstep_max) P.rstep_max = t1; }
        else { P.steps++; P.step_us += t1; if (t1 > P.step_max) P.step_max = t1; }
        if (!re && P.steps >= 600) prof_report();
    }
    if (!re && track > 0 && (s_fight_step == 1 || s_fight_step % track == 0)) track_line();
}

void ow_sweep(void (*gen)(void))
{
    double t0;
    if (!prof_on()) { gen(); return; }
    t0 = qpc_us();
    gen();
    if (netplay_rerunning()) { P.rsweeps++; P.rsweep_us += qpc_us() - t0; }
    else { P.sweeps++; P.sweep_us += qpc_us() - t0; }
}

void ow_plane_test(void (*gen)(void))
{
    if (s_prof > 0) { if (netplay_rerunning()) P.rtests++; else P.tests++; }
    gen();
}

/* ---------------------------------------------------------------- follow camera */
extern void sub_000443D0(void);
static struct { int on, behind, slot; float dist, height; int have; float eye[3], at[3], yaw; } C = { -1 };

static void followcam_init(void)
{
    const char *v = getenv("RECOMP_OW_FOLLOWCAM");
    C.on = 0;
    if (!v || !*v || *v == '0') return;
    C.on = 1;
    C.behind = !strncmp(v, "behind", 6);
    C.dist = C.behind ? 90.0f : 140.0f;
    C.height = C.behind ? 45.0f : 90.0f;
    C.slot = 0;
    {
        const char *c = strchr(v, ',');
        if (c) sscanf(c + 1, "%f,%f,%d", &C.dist, &C.height, &C.slot);
    }
    fprintf(stderr, "[OWCAM] follow camera %s, distance %.0f, height %.0f, offline slot %d (render camera only)\n",
            C.behind ? "behind" : "fixed", C.dist, C.height, C.slot);
}

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

void ow_camera(void (*gen)(void))
{
    int slot, i;
    uint32_t r;
    float at[3], eye[3];
    gen();
    if (C.on < 0) followcam_init();
    if (!C.on) return;
    slot = netplay_local_slot();
    if (slot < 0 || slot > 3) slot = C.slot;
    if (!present(slot)) return;
    r = rec(slot);
    at[0] = MEMF(r + 0x44u); at[1] = MEMF(r + 0x48u) + 16.0f; at[2] = MEMF(r + 0x4Cu);
    if (!netplay_rerunning()) {         /* host smoothing: only once per real step */
        float yaw = MEMF(r + 0x54u);
        if (!C.have) { C.yaw = yaw; }
        else {
            float d = fmodf(yaw - C.yaw + 540.0f, 360.0f) - 180.0f;
            C.yaw += d * 0.04f;
        }
        if (C.behind) {
            float rad = C.yaw * 3.14159265f / 180.0f;
            eye[0] = at[0] - sinf(rad) * C.dist;
            eye[2] = at[2] - cosf(rad) * C.dist;
        } else {
            eye[0] = at[0];
            eye[2] = at[2] + C.dist;
        }
        eye[1] = at[1] + C.height;
        if (!C.have) {
            memcpy(C.eye, eye, sizeof eye); memcpy(C.at, at, sizeof at); C.have = 1;
        } else {
            for (i = 0; i < 3; i++) { C.eye[i] = lerpf(C.eye[i], eye[i], 0.12f); C.at[i] = lerpf(C.at[i], at[i], 0.2f); }
        }
    }
    for (i = 0; i < 3; i++) { MEMF(RCAM + 0x10u + 4u * i) = C.eye[i]; MEMF(RCAM + 0x20u + 4u * i) = C.at[i]; }
    MEMF(RCAM + 0x1Cu) = 1.0f;
    MEMF(RCAM + 0x2Cu) = 1.0f;
    guest_call(sub_000443D0, 0, 0, NULL);
}

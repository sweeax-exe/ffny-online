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
 * RECOMP_OW_PUPPET=<slot>[,out] tests, never online: a host-driven slot (puppet_step).
 * RECOMP_OW_FOLLOWCAM=mode[,dist,height[,slot]]
 *                           a per-PC follow camera on the local fighter (online: netplay_local_slot(), offline: the
 *                           given slot, default 0). mode "fixed": the stock heading (looks along -Z), the eye dist
 *                           behind (+Z) and height above the fighter; mode "behind": behind the fighter's smoothed
 *                           facing. Host side only: after the game's camera wrapper sub_000AC590 has written the
 *                           render camera 0x2DBF28 (eye +0x10, target +0x20) and applied it (sub_000443D0), this
 *                           writes its own eye and target there and applies it again. The camera director
 *                           (0x3772A4), which the next step reads, is never touched; the smoothing state is host
 *                           memory and does not advance in re-run steps.
 *
 * Big Arena (phase 1, milestone 1; community/open-world-phase0-results.md section 8): a free-for-all on a big map
 * where a knocked-out fighter comes back, the last fighter who hit a fighter gets the knockout, and the first to
 * the score limit (or the leader at the time limit) wins. [openworld] big_arena turns it on for offline fights
 * (not Story, not Team); online it stays off until the relay carries it in START (milestone 2), except in tests
 * with RECOMP_OW_ONLINE_TEST=1 on every copy. Everything the simulation sees is decided inside the fight step from
 * guest memory and the state below (BA), which rollback saves and loads with each save (ow_state_save/load) and
 * the online checksum folds in (ow_state_hash), so every PC computes the same. See bigarena_* below.
 */
#include "host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "guest_call.h"
#include "patches/online_guard.h"
#include "patches/cheats.h"
#include "recomp_settings.h"
#include "openworld.h"
#include "netplay.h"
#include "rts.h"
#include "rts_social.h"

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
    n += snprintf(line + n, sizeof line - (size_t)n, "[OWTRACK] step %lld planes %u arena kind %u floor0 %.2f |", s_fight_step,
                  MEM32(PLANES_N), MEM32(0x003B9084u), (double)MEMF(0x003BF1CCu));
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

/* Offline test runs with any RECOMP_OW_* switch: the three first-time help cards (weapon, BLAZIN', KO;
 * docs/research/fight-tips.md) pause the fight until A is released, which no test presses. They are marked as seen
 * in the loaded save data (GS = MEM32(0x2FB148 + 0x60), save data MEM32(GS + 4), bytes +0x60..+0x62) at the first
 * step of a fight. Never online (netplay drops the cards itself). */
static void help_cards_seen(void)
{
    static int on = -1;
    uint32_t gs, sd;
    if (on < 0) {
        on = getenv("RECOMP_OW_PROF") || getenv("RECOMP_OW_TRACK") || getenv("RECOMP_OW_TELEPORT") ||
             getenv("RECOMP_OW_KO") || getenv("RECOMP_OW_FOLLOWCAM") || getenv("RECOMP_OW_PUPPET") ||
             getenv("RECOMP_OW_BIGARENA") || getenv("RECOMP_OW_TEST_DAMAGE") ||
             (getenv("RECOMP_TEST_HELP_CARDS_SEEN") && *getenv("RECOMP_TEST_HELP_CARDS_SEEN") == '1');
    }
    if (!on || ffny_online_locked()) return;
    gs = MEM32(0x2FB148u + 4u * 0x18u);
    if (!gs) return;
    sd = MEM32(gs + 4u);
    if (!sd) return;
    if (MEM8(sd + 0x60u) && MEM8(sd + 0x61u) && MEM8(sd + 0x62u)) return;
    MEM8(sd + 0x60u) = MEM8(sd + 0x61u) = MEM8(sd + 0x62u) = 1;
    fprintf(stderr, "[OWTEST] first-time help cards marked as seen (tests)\n");
}

/* RECOMP_OW_PUPPET=<slot>[,<mode>] tests, never online: that fighter becomes a "puppet" from the given fight step 200
 * on: every step our code writes its position (a circle of radius 70 around the origin, 0.6 units a step) and
 * facing, and mode "out" also sets the "ended" bit +0x932 0x200 (not targetable or hittable, wall-stagger.md /
 * the RE study), so the run shows what a host-driven slot looks like and what the bit does to the others. */
static void puppet_step(void)
{
    static int parsed, slot = -1, out;
    uint32_t r;
    float a;
    if (!parsed) {
        const char *v = getenv("RECOMP_OW_PUPPET");
        parsed = 1;
        if (v) { slot = atoi(v); out = strstr(v, "out") != NULL; }
    }
    if (slot < 0 || slot > 3 || ffny_online_locked() || !present(slot) || s_fight_step < 200) return;
    r = rec(slot);
    a = (float)(s_fight_step - 200) * 0.6f / 70.0f;
    MEMF(r + 0x44u) = 70.0f * cosf(a);
    MEMF(r + 0x4Cu) = 70.0f * sinf(a);
    MEMF(r + 0x54u) = -a * 57.29578f;           /* tangent, in the game's degrees */
    if (out) MEM16(r + 0x932u) |= 0x200u;
    if (s_fight_step == 200 || s_fight_step % 300 == 0)
        fprintf(stderr, "[OWPUPPET] step %lld: slot %d at %.1f,%.1f state %u flags %04X hp %.1f\n", s_fight_step, slot,
                (double)MEMF(r + 0x44u), (double)MEMF(r + 0x4Cu), MEM32(r + 0x30u), MEM16(r + 0x932u),
                (double)MEMF(r + 0x350u));
}

extern void sub_000443D0(void);
static struct { int on, behind, slot; float dist, height; int have; float eye[3], at[3], yaw; } C = { -1 };

/* ---------------------------------------------------------------- Big Arena (phase 1) */
#define MATCH_STATE 0x00349760u + 0x2D820u     /* the match's state: 5 and 8 fight, 7 the special-move camera */
#define HUD_PLAYERS 0x0039C1C8u                 /* the fight HUD's per-player objects; +0x1104 latches "KO" */
#define RESULT_COUNT 0x003BE624u                /* the result recorder's record count (3 records at 0x3BE130) */
#define WINNER_A    0x003B8F3Cu                 /* the decisive knockout's winner slot (sub_001A7140) */
#define WINNER_B    0x003B8F40u
#define ENDED       0x0200u                     /* +0x932: out of the fight (sub_001A7140) */
#define DEFEAT_BITS (0x01u | 0x04u | 0x10u | 0x20u | 0x80u | 0x200u | 0x400u)
#define ARENA_PROTECT 120u                      /* steps of respawn protection (2 s) */
#define ARENA_LOSER_GAP 20u                     /* steps between the losers going down at the end */

/* The spawn ring of the Studio "Big Arena" template (studio: ffnystudio/mapbasics.py BIGARENA_SPAWNS, the same
 * eight points): radius 260 around the centre (22 m), inside its walls at +-585. */
static const float k_spawn[8][2] = {
    { 0.0f, -260.0f }, { 183.85f, -183.85f }, { 260.0f, 0.0f }, { 183.85f, 183.85f },
    { 0.0f, 260.0f }, { -183.85f, 183.85f }, { -260.0f, 0.0f }, { -183.85f, -183.85f },
};

/* The simulation's state of a Big Arena fight. Saved and loaded with every rollback save, folded into the online
 * checksum; only changed inside the fight step (ow_step before the body, the hit and health hooks within it). */
typedef struct {
    uint32_t on;                    /* this fight is a Big Arena */
    uint32_t step;                  /* steps since the fight's first, re-runs included */
    int8_t last_hit[4];             /* who hit this slot last (-1 nobody since it came back) */
    uint8_t score[4], deaths[4];
    uint8_t down[4];                /* counted as knocked out (ENDED seen) */
    uint32_t down_at[4];
    uint32_t protect_until[4];
    uint8_t ending, winner, losers_done, pad;
    uint32_t next_loser_at;
    /* the fight's rules, set at its start (from START online, the settings offline) */
    uint16_t score_limit, respawn_steps;
    uint32_t time_limit_steps;
    float spawn_scale;              /* the spawn ring for this venue's walls (1: the Big Arena template's) */
    uint32_t blazin;                /* BLAZIN' moves allowed (else the meter stops short of full) */
    uint32_t rts;                   /* a Run the Streets fight (offline from its page, online from its room) */
    uint32_t spawn_hash;            /* the map's spawn points (SP, set at the set-up from the content every PC shares) */
} BigArena;
static BigArena BA;
typedef char ow_state_fits[sizeof(BigArena) <= OW_STATE_BYTES ? 1 : -1];

/* The map's spawn points for this fight (rts_spawns: the pack's bigarena json or the official map's own), read at
 * the set-up from the content files, which the room's content print makes the same on every PC; their hash is in
 * BA, so a PC with other points shows in the checksum. 0 points: the ring (k_spawn) scaled to the walls. */
static struct { int n; float x[OW_SPAWN_MAX], z[OW_SPAWN_MAX]; } SP;
static struct { int camera; int camera_set; } BAcfg;    /* host only: the follow camera (0 stock, 1 follow, 2 behind) */
static const char *const k_ba_camera[] = { "stock", "follow", "behind", NULL };

void ow_state_save(uint8_t out[OW_STATE_BYTES]) { memset(out, 0, OW_STATE_BYTES); memcpy(out, &BA, sizeof BA); }
void ow_state_load(const uint8_t in[OW_STATE_BYTES]) { memcpy(&BA, in, sizeof BA); }

uint32_t ow_state_hash(void)
{
    const uint8_t *p = (const uint8_t *)&BA;
    uint32_t h = 2166136261u;
    size_t i;
    if (!BA.on) return 0;
    for (i = 0; i < sizeof BA; i++) h = (h ^ p[i]) * 16777619u;
    return h ? h : 1;
}

static int ba_env_on(const char *name)
{
    const char *v = getenv(name);
    return v && *v && *v != '0';
}

/* The match set-up (sub_001A4D90's entry): is this fight a Big Arena, and its rules.
 * Run the Streets: offline from its page (rts_offline_start, the rules from [openworld]), online from a Run the
 * Streets room (the room's mode, which every PC of the fight has from ROOM; fixed rules, never a PC's own settings).
 * Otherwise [openworld] big_arena (tests and the Studio template), offline only (RECOMP_OW_ONLINE_TEST=1: online). */
void ow_match_start(void)
{
    int want = recomp_settings_get("openworld", "big_arena", 0) != 0;
    int online = netplay_active(), rts = 0, i;
    memset(&BA, 0, sizeof BA);
    memset(BA.last_hit, -1, sizeof BA.last_hit);
    SP.n = 0;
    if (online) {
        rts = netplay_rts_match();
        if (rts) want = 1;
        else if (want && !ba_env_on("RECOMP_OW_ONLINE_TEST")) {
            fprintf(stderr, "[BIGARENA] online fight: Big Arena stays off (only in Run the Streets rooms)\n");
            want = 0;
        }
    } else if (rts_offline_fight_starts()) {
        rts = want = 1;
    }
    if (want && cheats_in_story()) want = rts = 0;
    if (want && (MEM8(0x003B907Eu) & 2u)) {
        fprintf(stderr, "[BIGARENA] a team fight: Big Arena stays off (free for all only)\n");
        want = rts = 0;
    }
    BAcfg.camera = recomp_settings_get("openworld", "camera", 1);
    BAcfg.camera_set = 1;
    C.have = 0;
    if (!want) return;
    /* Run the Streets keeps the follow camera looking north (-Z): the map's north faces are left undrawn for it (the
     * game draws faces from both sides), and with that heading the stick is already camera-relative (up = away from
     * the camera). The behind camera and its turned stick stay for the other Big Arena fights. */
    if (rts) BAcfg.camera = 1;
    BA.on = 1;
    BA.rts = (uint32_t)rts;
    if (online && rts) {            /* the same rules on every PC: none of a PC's own settings */
        BA.score_limit = 5;
        BA.respawn_steps = 180;
        BA.time_limit_steps = 60u * 300u;
    } else {
        BA.score_limit = (uint16_t)recomp_settings_get("openworld", "score_limit", 5);
        BA.respawn_steps = (uint16_t)(60 * recomp_settings_get("openworld", "respawn_seconds", 3));
        BA.time_limit_steps = 60u * (uint32_t)recomp_settings_get("openworld", "time_limit", 300);
    }
    BA.winner = 0xFF;
    /* BLAZIN': on in Run the Streets (the Studio-venue BLAZIN' crash was fixed in build 83, re-tested for build 86);
     * the [openworld] blazin setting for the other Big Arena fights */
    BA.blazin = rts ? 1u : recomp_settings_get("openworld", "blazin", 0) != 0;
    if (rts) {
        RtsMap m;
        if (rts_map(&m)) SP.n = rts_spawns(&m, SP.x, SP.z, OW_SPAWN_MAX);
        {
            uint32_t h = 2166136261u;
            const uint8_t *p = (const uint8_t *)SP.x;
            for (i = 0; i < (int)sizeof SP.x; i++) h = (h ^ p[i]) * 16777619u;
            p = (const uint8_t *)SP.z;
            for (i = 0; i < (int)sizeof SP.z; i++) h = (h ^ p[i]) * 16777619u;
            BA.spawn_hash = h ^ (uint32_t)SP.n;
        }
        fprintf(stderr, "[RTS] Run the Streets fight%s on %s (feID %d): %d spawn points (hash %08X)\n",
                online ? " (online)" : "", m.code[0] ? m.code : "?", m.fe_id, SP.n, BA.spawn_hash);
    }
    fprintf(stderr, "[BIGARENA] on: first to %u knockouts, back after %u steps, time limit %u steps, camera %s, "
            "BLAZIN' %s%s\n", BA.score_limit, BA.respawn_steps, BA.time_limit_steps, k_ba_camera[BAcfg.camera % 3],
            BA.blazin ? "on" : "off", online ? (rts ? " (Run the Streets room)" : " (online test)") : "");
}

int ow_rts_active(void) { return BA.on && BA.rts; }

static uint32_t ba_present(void)
{
    uint32_t m = 0;
    int s;
    for (s = 0; s < 4; s++) if (present(s)) m |= 1u << s;
    return m;
}

static void ba_scores(int score[4], int deaths[4])
{
    int s;
    for (s = 0; s < 4; s++) { score[s] = BA.score[s]; deaths[s] = BA.deaths[s]; }
}

/* A knocked-out fighter stands up again: the phase-0 revive (health as at the set-up, the defeat bits cleared,
 * free state), put at a spawn point and facing the centre, protected for ARENA_PROTECT steps, and the fight HUD's
 * "KO" latch for its player cleared (it is set only by a recorded result, which Big Arena skips, but a time-up
 * result could have set it). */
static void ba_revive(int s, int pick_spawn)
{
    uint32_t r = rec(s), h;
    uint16_t before = MEM16(r + 0x932u);
    MEM16(r + 0x932u) = (uint16_t)(before & ~DEFEAT_BITS);
    MEM8(r + 0x943u) = 0;
    call2(sub_001A4F30, 1, r, 0);
    call2(sub_001B5420, 2, r, 1u);
    if (pick_spawn) {
        OwSpawnCtx c;
        int k, i;
        float sc = BA.spawn_scale > 0.0f ? BA.spawn_scale : 1.0f;
        memset(&c, 0, sizeof c);
        if (SP.n > 0) {             /* the map's own points (streets), at their place: no ring scale */
            for (i = 0; i < SP.n; i++) { c.spawn_x[i] = SP.x[i]; c.spawn_z[i] = SP.z[i]; }
            c.spawns = SP.n;
            c.safe = 140.0f;
        } else {
            for (i = 0; i < 8; i++) { c.spawn_x[i] = k_spawn[i][0] * sc; c.spawn_z[i] = k_spawn[i][1] * sc; }
            c.spawns = 8;
            c.safe = 140.0f * sc;
        }
        for (k = 0; k < 4; k++) {
            if (k == s || !present(k) || (MEM16(rec(k) + 0x932u) & ENDED)) continue;
            c.live_x[c.lives] = MEMF(rec(k) + 0x44u);
            c.live_z[c.lives] = MEMF(rec(k) + 0x4Cu);
            c.lives++;
        }
        i = ow_rules_pick_spawn(&c);
        if (i >= 0) {
            MEMF(r + 0x44u) = c.spawn_x[i];
            MEMF(r + 0x48u) = 0.0f;
            MEMF(r + 0x4Cu) = c.spawn_z[i];
            MEMF(r + 0x54u) = ow_rules_yaw_to_centre(c.spawn_x[i], c.spawn_z[i]);
        }
    }
    h = MEM32(HUD_PLAYERS + 4u * (uint32_t)s);
    if (h >= 0x10000u && h < 0x84000000u) { MEM8(h + 0x1104u) = 0; MEMF(h + 0x1108u) = 0.0f; }
    BA.down[s] = 0;
    BA.last_hit[s] = -1;
    BA.protect_until[s] = BA.step + ARENA_PROTECT;
    if (!netplay_rerunning())
        fprintf(stderr, "[BIGARENA] step %u: slot %d back at %.0f,%.0f (flags %04X->%04X, hp %.1f, state %u)\n",
                BA.step, s, (double)MEMF(r + 0x44u), (double)MEMF(r + 0x4Cu), before, MEM16(r + 0x932u),
                (double)MEMF(r + 0x350u), MEM32(r + 0x30u));
}

/* The end: the winner is decided; the losers go down one after another (the game's own knockout, as
 * test_telemetry.c's ko_step), so the game's last knockout, slow motion, results and summary run as in any
 * free-for-all, with our winner (ow_result_record). */
static void ba_end_step(void)
{
    int score[4], deaths[4], s;
    uint32_t pres = ba_present(), done = 0;
    if (BA.winner < 4 && (MEM16(rec(BA.winner) + 0x932u) & ENDED)) ba_revive(BA.winner, 0);
    if (BA.step < BA.next_loser_at) return;
    for (s = 0; s < 4; s++)
        if ((pres & (1u << s)) && ((MEM16(rec(s) + 0x932u) & ENDED) || MEMF(rec(s) + 0x350u) <= 0.0f)) done |= 1u << s;
    ba_scores(score, deaths);
    s = ow_rules_next_loser(score, deaths, pres, BA.winner, done);
    if (s < 0) return;
    {
        uint32_t r = rec(s);
        MEMF(r + 0x350u) = 0.0f;
        MEMF(r + 0x35Cu) = 0.0f;
        MEM8(r + 0x943u) = 1;
        call2(sub_001B5420, 2, r, 0x18u);
    }
    BA.losers_done++;
    BA.next_loser_at = BA.step + ARENA_LOSER_GAP;
    if (!netplay_rerunning())
        fprintf(stderr, "[BIGARENA] step %u: slot %d goes down (end, %u knockouts)\n", BA.step, s, BA.score[s]);
}

/* The spawn ring's size for this venue, at the fight's first step: the walls' extent (the corners of every upright
 * collision plane, +0x04 four x/y/z corners, +0x34 the normal; the .cli layout of ffnystudio/collision.py), the
 * ring at 0.6 of the smaller half-extent, at most the template's 260 and at least 25. The Big Arena template
 * (walls at +-585) keeps 260; a retail ring (+-35..60) gets 25..36, so nobody comes back outside its walls. The
 * same planes on every PC, so the same ring. */
static float ba_spawn_scale(void)
{
    uint32_t base = MEM32(PLANES_PTR), n = MEM32(PLANES_N), i, k;
    float hx = 0.0f, hz = 0.0f, r;
    if (!base || !n || n > 100000u) return 1.0f;
    for (i = 0; i < n; i++) {
        uint32_t p = base + i * 0x48u;
        if (fabsf(MEMF(p + 0x38u)) >= 0.7f) continue;          /* a floor or a ceiling */
        for (k = 0; k < 4; k++) {
            float x = fabsf(MEMF(p + 4u + 12u * k)), z = fabsf(MEMF(p + 12u + 12u * k));
            if (x < 1e5f && x > hx) hx = x;
            if (z < 1e5f && z > hz) hz = z;
        }
    }
    if (hx <= 0.0f || hz <= 0.0f) return 1.0f;
    r = 0.6f * (hx < hz ? hx : hz);
    if (r > 260.0f) r = 260.0f;
    if (r < 25.0f) r = 25.0f;
    return r / 260.0f;
}

/* Once per step before the generated body, in normal and re-run steps alike. */
static void bigarena_step(void)
{
    int s, score[4], deaths[4];
    uint32_t pres;
    if (!BA.on) return;
    BA.step++;
    if (BA.step == 1) {
        BA.spawn_scale = ba_spawn_scale();
        if (!netplay_rerunning())
            fprintf(stderr, "[BIGARENA] %u collision planes: spawn ring radius %.0f\n", MEM32(PLANES_N),
                    (double)(260.0f * BA.spawn_scale));
    }
    pres = ba_present();
    /* BLAZIN' off (the default for now): every fight that ran a BLAZIN' on a Studio-built venue crashed in the
     * heap allocator after the fight (sub_00029DE0+0x1E3; 7 of 7 in phase 0, and run 20261113-222856-offline on
     * the 5-plane Big Arena template, so not the plane count). The meter (+0x364, full at 98..100 sets the ready
     * bit +0x93E 0x01, docs/research/dizzy.md) stops at 97 until that is fixed. */
    if (!BA.blazin)
        for (s = 0; s < 4; s++)
            if ((pres & (1u << s)) && MEMF(rec(s) + 0x364u) > 97.0f) {
                MEMF(rec(s) + 0x364u) = 97.0f;
                MEM8(rec(s) + 0x93Eu) &= (uint8_t)~1u;
            }
    /* knockouts: a fighter that is out (ENDED) is counted once; the last fighter who hit it scores */
    for (s = 0; s < 4; s++) {
        uint32_t r = rec(s);
        if (!(pres & (1u << s))) continue;
        if ((MEM16(r + 0x932u) & ENDED) && !BA.down[s]) {
            int by = BA.last_hit[s];
            BA.down[s] = 1;
            BA.down_at[s] = BA.step;
            if (BA.ending) continue;            /* the end's own knockouts score nothing */
            if (BA.deaths[s] < 255) BA.deaths[s]++;
            if (by >= 0 && by < 4 && by != s && BA.score[by] < 255) BA.score[by]++;
            if (!netplay_rerunning())
                fprintf(stderr, "[BIGARENA] step %u: slot %d knocked out by %d | scores %u %u %u %u\n", BA.step, s, by,
                        BA.score[0], BA.score[1], BA.score[2], BA.score[3]);
        }
    }
    ba_scores(score, deaths);
    if (!BA.ending) {
        int w = ow_rules_winner(score, deaths, pres), limit = 0;
        if (w >= 0 && BA.score_limit && score[w] >= BA.score_limit) limit = 1;
        else if (BA.time_limit_steps && BA.step >= BA.time_limit_steps) limit = 2;
        if (limit && w >= 0) {
            BA.ending = 1;
            BA.winner = (uint8_t)w;
            BA.next_loser_at = BA.step + 30u;     /* half a second for the winning blow to land */
            if (!netplay_rerunning())
                fprintf(stderr, "[BIGARENA] step %u: %s, slot %d wins | scores %u %u %u %u | knocked out %u %u %u %u\n",
                        BA.step, limit == 1 ? "score limit" : "time limit", w, BA.score[0], BA.score[1], BA.score[2],
                        BA.score[3], BA.deaths[0], BA.deaths[1], BA.deaths[2], BA.deaths[3]);
        }
    }
    if (BA.ending) { ba_end_step(); return; }
    for (s = 0; s < 4; s++)
        if ((pres & (1u << s)) && BA.down[s] && BA.step - BA.down_at[s] >= BA.respawn_steps &&
            (MEM16(rec(s) + 0x932u) & ENDED))
            ba_revive(s, 1);
}

static int slot_of_record(uint32_t r)
{
    if (r < ACTORS || r >= ACTORS + 4u * STRIDE || (r - ACTORS) % STRIDE) return -1;
    return (int)((r - ACTORS) / STRIDE);
}

/* sub_001A5DD0's entry (attacker, defender): the knockout credit. */
void ow_hit(uint32_t attacker, uint32_t defender)
{
    int a, d;
    if (!BA.on) return;
    a = slot_of_record(attacker);
    d = slot_of_record(defender);
    if (a >= 0 && d >= 0 && a != d) BA.last_hit[d] = (int8_t)a;
}

/* sub_001A50B0's entry (record, delta, ...): no damage in the respawn protection; tests offline:
 * RECOMP_OW_TEST_DAMAGE=<x> multiplies damage, so a test reaches the score limit sooner. */
void ow_health_change(void)
{
    static float mult = -1.0f;
    int s;
    float delta;
    if (!BA.on) return;
    s = slot_of_record(MEM32(g_esp + 4));
    if (s < 0) return;
    delta = MEMF(g_esp + 8);
    if (delta >= 0.0f) return;
    if (!BA.ending && BA.step < BA.protect_until[s]) { MEMF(g_esp + 8) = 0.0f; return; }
    if (mult < 0.0f) { const char *v = getenv("RECOMP_OW_TEST_DAMAGE"); mult = v && *v ? (float)atof(v) : 0.0f; }
    if (mult > 0.0f && !ffny_online_locked()) MEMF(g_esp + 8) = delta * mult;
}

/* sub_001A6750 (the match-end decision, eax 1 = over): a Big Arena goes on while a fighter is down, until the
 * end (ba_end_step) has put the losers down. */
void ow_match_end_check(void (*gen)(void))
{
    if (!BA.on || BA.ending) { gen(); return; }
    g_eax = 0;
    g_esp += 4;     /* ret */
}

/* sub_001A6A80 (cdecl winners list, losers list, code; lists of slots ending in -1): a knockout before the end is
 * not recorded (the recorder holds 3 results and latches the HUD's "KO"; a fighter that comes back must not count
 * as out). At the end every result names our winner. A time-up result of the game's own clock passes as it is
 * and ends the respawns. */
void ow_result_record(void (*gen)(void))
{
    uint32_t winners, code;
    if (!BA.on) { gen(); return; }
    winners = MEM32(g_esp + 4);
    code = MEM32(g_esp + 12);
    if (code & 0x40u) {         /* the game's time limit */
        BA.ending = 1;
        gen();
        return;
    }
    if (!BA.ending) {
        if (!netplay_rerunning())
            fprintf(stderr, "[BIGARENA] step %u: knockout not recorded (code %04X)\n", BA.step, code & 0xFFFFu);
        g_eax = 0;
        g_esp += 4;
        return;
    }
    if (BA.winner < 4 && winners) {
        MEM32(winners) = BA.winner;
        MEM32(winners + 4) = 0xFFFFFFFFu;
        if (code & 1u) { MEM32(WINNER_A) = BA.winner; MEM32(WINNER_B) = BA.winner; }
    }
    if (MEM32(RESULT_COUNT) >= 3) {     /* never past the 3 records (the stats block follows) */
        g_eax = 0;
        g_esp += 4;
        return;
    }
    gen();
}

int ow_hud_view(OwHudView *out)
{
    int s, score[4], deaths[4];
    memset(out, 0, sizeof *out);
    if (!BA.on) return 0;
    out->on = 1;
    out->score_limit = BA.score_limit;
    out->present = ba_present();
    for (s = 0; s < 4; s++) {
        out->score[s] = BA.score[s];
        out->deaths[s] = BA.deaths[s];
        if (BA.down[s] && !BA.ending) {
            int left = (int)BA.respawn_steps - (int)(BA.step - BA.down_at[s]);
            out->down |= 1u << s;
            out->back_in[s] = left > 0 ? left : 0;
        }
        if (BA.step < BA.protect_until[s]) out->protected_ |= 1u << s;
    }
    ba_scores(score, deaths);
    out->leader = ow_rules_winner(score, deaths, out->present);
    for (s = 0; s < 4 && out->leader >= 0; s++)
        if (s != out->leader && (out->present & (1u << s)) && score[s] == score[out->leader]) out->leader = -1;
    out->ending = BA.ending;
    out->winner = BA.winner < 4 ? BA.winner : -1;
    out->seconds_left = BA.time_limit_steps ? (int)((BA.time_limit_steps > BA.step ? BA.time_limit_steps - BA.step : 0) / 60u) : -1;
    s = netplay_local_slot();
    out->local = s >= 0 && s < 4 ? s : 0;
    out->step = BA.step;
    return 1;
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
        if (s_fight_step == 1) help_cards_seen();
        teleport_step();
        if (!BA.on) ko_revive_step();
        puppet_step();
    }
    bigarena_step();
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

/* Big Arena's camera setting ([openworld] camera: stock, follow (default), behind) when no RECOMP_OW_FOLLOWCAM test
 * switch says otherwise: the follow camera runs in a Big Arena fight only, and only while the fight's own steps run
 * (match state 5 or 8): the intro, the special-move camera of a BLAZIN' (state 7), the knockout and the results keep
 * the game's own cameras. */
static int followcam_wanted(void)
{
    uint32_t st;
    if (C.on < 0) followcam_init();
    if (!C.on) {
        if (!BA.on || !BAcfg.camera_set || BAcfg.camera <= 0) return 0;
        C.behind = BAcfg.camera == 2;
        C.dist = C.behind ? 90.0f : 140.0f;
        C.height = C.behind ? 45.0f : 90.0f;
        C.slot = 0;
    }
    st = MEM32(MATCH_STATE);
    if (st != 5 && st != 8) { C.have = 0; return 0; }
    return 1;
}

void ow_camera(void (*gen)(void))
{
    int slot, i;
    uint32_t r;
    float at[3], eye[3];
    gen();
    if (!followcam_wanted()) { rts_social_camera(); return; }
    slot = netplay_local_slot();
    if (slot < 0 || slot > 3) slot = C.slot;
    if (!present(slot)) { rts_social_camera(); return; }
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
        if (!C.have) {      /* from where the game's camera is: a swoop in, not a cut */
            for (i = 0; i < 3; i++) { C.eye[i] = MEMF(RCAM + 0x10u + 4u * i); C.at[i] = MEMF(RCAM + 0x20u + 4u * i); }
            if (!(fabsf(C.eye[0]) < 1e6f && fabsf(C.at[0]) < 1e6f)) { memcpy(C.eye, eye, sizeof eye); memcpy(C.at, at, sizeof at); }
            C.have = 1;
        } else {
            for (i = 0; i < 3; i++) { C.eye[i] = lerpf(C.eye[i], eye[i], 0.12f); C.at[i] = lerpf(C.at[i], at[i], 0.2f); }
        }
    }
    for (i = 0; i < 3; i++) { MEMF(RCAM + 0x10u + 4u * i) = C.eye[i]; MEMF(RCAM + 0x20u + 4u * i) = C.at[i]; }
    MEMF(RCAM + 0x1Cu) = 1.0f;
    MEMF(RCAM + 0x2Cu) = 1.0f;
    guest_call(sub_000443D0, 0, 0, NULL);
    rts_social_camera();    /* the social layer's copy of the final camera (bubbles, proximity voice) */
}

/* ---------------------------------------------------------------- match state (diagnostics) */
/* From the match update's entry hook (sub_0008BE60, this = the match at 0x349760): every change of its state
 * (+0x2D820; 5 and 8 run the fight step), with the fight step, so a fight that stops stepping shows where.
 * RECOMP_OW_PROF or RECOMP_OW_TRACK turns it on. */
void ow_match_update(uint32_t match)
{
    static int on = -1;
    static uint32_t last = 0xFFFFFFFFu;
    static double since;
    uint32_t st = MEM32(match + 0x2D820u);
    if (on < 0) on = env_int("RECOMP_OW_PROF", 0) > 0 || env_int("RECOMP_OW_TRACK", 0) > 0;
    if (!on) return;
    if (st != last) {
        double now = qpc_us();
        fprintf(stderr, "[OWSTATE] match state %u -> %u at fight step %lld (%.1f s in the old state)\n", last, st,
                s_fight_step, last == 0xFFFFFFFFu ? 0.0 : (now - since) / 1e6);
        last = st;
        since = now;
    }
}

/* ---------------------------------------------------------------- camera-relative controls */
/* The "follow" camera keeps the stock heading (it looks along -Z like the game's own camera), so the stick already
 * moves the fighter away from the camera when pushed up: nothing to turn. The "behind" camera turns with the
 * fighter; there the local player's left stick is turned by the camera's heading before the step reads it
 * (sub_001BAA00's entry, before netplay.c takes the poll as this PC's entry), so the entry every PC simulates is
 * the turned one and the fight stays the same everywhere. Only the stick: the D-pad keeps the stock directions.
 * Tests: RECOMP_OW_STICK_TURN=<degrees> turns by a fixed heading in any Big Arena fight (to measure). */
#define PAD_TABLE_OW 0x003B8F48u
void ow_pad_read(uint32_t actor)
{
    static float last_x[4], last_y[4];
    static int fixed = -1000;
    int slot, local;
    uint32_t base;
    float x, y, heading;
    if (!BA.on || netplay_rerunning()) return;
    if (actor < ACTORS || actor >= ACTORS + 4u * STRIDE || (actor - ACTORS) % STRIDE) return;
    slot = (int)((actor - ACTORS) / STRIDE);
    local = netplay_local_slot();
    if (local < 0) local = netplay_active() ? -1 : 0;
    if (slot != local) return;
    if (fixed == -1000) { const char *v = getenv("RECOMP_OW_STICK_TURN"); fixed = v && *v ? atoi(v) : -999; }
    if (fixed != -999 && !ffny_online_locked()) heading = (float)fixed;
    else if (C.have && C.behind) heading = ow_rules_view_heading(C.eye[0], C.eye[2], C.at[0], C.at[2]);
    else return;
    base = PAD_TABLE_OW + (uint32_t)slot * 0x20u;
    x = MEMF(base + 8u);
    y = MEMF(base + 12u);
    if (x == last_x[slot] && y == last_y[slot]) return;     /* turned already (a second read of the same poll) */
    ow_rules_stick_turn(heading, &x, &y);
    MEMF(base + 8u) = x;
    MEMF(base + 12u) = y;
    last_x[slot] = x;
    last_y[slot] = y;
}

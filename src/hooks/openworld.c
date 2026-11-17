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
 *                           fighters on a square of side d around the mean of their positions; "<step>@<slot>=@<other>,
 *                           <dx>,<dz>" puts it next to another fighter. ';' separates entries.
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
        } else if (eq[1] == '@') {     /* "<step>@<slot>=@<other>,<dx>,<dz>": next to another fighter */
            float dx = 0, dz = 0;
            int o = -1;
            s = atoi(who);
            sscanf(eq + 2, "%d,%f,%f", &o, &dx, &dz);
            if (s < 0 || s > 3 || o < 0 || o > 3 || !present(s) || !present(o)) continue;
            MEMF(rec(s) + 0x44u) = MEMF(rec(o) + 0x44u) + dx;
            MEMF(rec(s) + 0x4Cu) = MEMF(rec(o) + 0x4Cu) + dz;
            fprintf(stderr, "[OWTELE] step %lld: slot %d put next to slot %d (%+.1f,%+.1f)\n", s_fight_step, s, o, dx, dz);
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
#define ARENA_TIME_HOLD 180u                    /* TIME!: 3 s of TIME! and the winner, everyone held, before the losers go down */
#define END_SCORE 1u                            /* BA.end_kind: the KO target was reached */
#define END_TIME  2u                            /* BA.end_kind: the clock ran out (or the safety end without a limit) */

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
    /* build 87: Run the Streets' round rules (the room's, from START online; the VS CPU page's offline) and how the
     * fight ended; checksummed with the rest, so PCs that disagree on the rules show at once */
    uint8_t rules;                  /* the rules byte (ow_rules_rts_*), 0 the defaults */
    uint8_t end_kind;               /* END_SCORE, END_TIME; 0 not over */
    uint8_t unlimited;              /* no time limit: time_limit_steps is the safety end */
    uint8_t pad2;
    uint32_t end_step;              /* the step the end came at */
    /* free roam (ow_roam_input): a human fighter out of combat walks free and faces where it goes */
    uint8_t roam_on, roam_pad[3];
    uint16_t roam_steps;            /* calm steps (no fight button, no hit) before an engaged fighter walks free */
    uint16_t roam_speed;            /* walking speed while free, percent of the game's */
    uint8_t roam_mode[4];           /* OW_ROAM_FREE, _ENGAGED, _LOCKED */
    int8_t roam_pending[4];         /* a hit while free: engage this attacker at the slot's next input (-1 none) */
    uint32_t roam_last[4];          /* the step of the slot's last fight button or hit */
    uint8_t roam_turn[4];           /* steps left of the turn toward a target just locked on to */
    /* build 88: CPU fighters that stand still too long take another target (cpu_idle_step) */
    int16_t cpu_ax[4], cpu_az[4];   /* where the CPU stood when its stillness began (whole units) */
    uint16_t cpu_still[4];          /* steps it has stood there, idle or blocking */
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
    if (online && rts) {            /* the same rules on every PC: the room's, from START; none of a PC's own settings */
        int t, k;
        BA.rules = (uint8_t)netplay_rts_rules();
        ow_rules_rts_decode(BA.rules, &t, &k);
        BA.score_limit = (uint16_t)ow_rules_rts_kos(k);
        BA.respawn_steps = 180;
        BA.time_limit_steps = (uint32_t)ow_rules_rts_limit_steps(BA.rules);
        BA.unlimited = t == OW_RTS_TIME_NONE;
    } else if (rts) {               /* VS CPU: the page's choice (rts.c); tests may shorten the clock */
        int t, k, test_secs = env_int("RECOMP_RTS_TEST_SECONDS", 0);
        BA.rules = (uint8_t)rts_offline_rules();
        ow_rules_rts_decode(BA.rules, &t, &k);
        BA.score_limit = (uint16_t)ow_rules_rts_kos(k);
        BA.respawn_steps = 180;
        BA.time_limit_steps = (uint32_t)ow_rules_rts_limit_steps(BA.rules);
        BA.unlimited = t == OW_RTS_TIME_NONE;
        if (test_secs > 0 && !ffny_online_locked()) {
            BA.time_limit_steps = 60u * (uint32_t)test_secs;
            fprintf(stderr, "[RTS] RECOMP_RTS_TEST_SECONDS (tests): the clock ends after %d s\n", test_secs);
        }
    } else {
        BA.score_limit = (uint16_t)recomp_settings_get("openworld", "score_limit", 5);
        BA.respawn_steps = (uint16_t)(60 * recomp_settings_get("openworld", "respawn_seconds", 3));
        BA.time_limit_steps = 60u * (uint32_t)recomp_settings_get("openworld", "time_limit", 300);
    }
    BA.winner = 0xFF;
    /* free roam: the same on every PC online (on, 3 s, the game's speed); offline [openworld] free_roam,
     * roam_seconds and roam_speed */
    memset(BA.roam_pending, -1, sizeof BA.roam_pending);
    if (online) {
        BA.roam_on = 1;
        BA.roam_steps = 180;
        BA.roam_speed = 100;
    } else {
        BA.roam_on = recomp_settings_get("openworld", "free_roam", 1) != 0;
        BA.roam_steps = (uint16_t)(60 * recomp_settings_get("openworld", "roam_seconds", 3));
        BA.roam_speed = (uint16_t)recomp_settings_get("openworld", "roam_speed", 100);
    }
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
    fprintf(stderr, "[BIGARENA] on: first to %u knockouts, back after %u steps, time limit %u steps%s, camera %s, "
            "BLAZIN' %s, free roam %s (after %u calm steps, speed %u%%)%s\n", BA.score_limit, BA.respawn_steps,
            BA.time_limit_steps, BA.unlimited ? " (no time limit: the safety end)" : "", k_ba_camera[BAcfg.camera % 3],
            BA.blazin ? "on" : "off", BA.roam_on ? "on" : "off", BA.roam_steps, BA.roam_speed,
            online ? (rts ? " (Run the Streets room)" : " (online test)") : "");
    if (rts) {
        char rt[48];
        ow_rules_rts_text(BA.rules, rt, (int)sizeof rt);
        fprintf(stderr, "[RTS] round rules %02X: %s\n", BA.rules, rt);
    }
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
            float yaw = ow_rules_yaw_to_centre(c.spawn_x[i], c.spawn_z[i]);
            /* at the height its body lay at (0 on the Big Arena template, 1.76 on Knox and Jam's streets): the game
             * moves the height only with some animations, and a fighter that really comes back far from the others
             * walked a long way sunk to 0. (The cached floor height +0x3A4 is no help: it reads 0 on parts of a map.) */
            float floor_y = MEMF(r + 0x48u);
            if (!(floor_y > -10000.0f && floor_y < 10000.0f)) floor_y = 0.0f;
            MEMF(r + 0x44u) = c.spawn_x[i];
            MEMF(r + 0x48u) = floor_y;
            MEMF(r + 0x4Cu) = c.spawn_z[i];
            MEMF(r + 0x54u) = yaw;
            /* build 87: and the animation's own place. The record keeps where the running animation has carried
             * the body (+0xC4 x, +0xCC z, +0xD4 yaw: the record's position plus the animation's travel, sub_001A76A0
             * every step), and the next animation starts from there (sub_001A85A0 with flags 4 and 8 copies them to
             * +0x44, +0x4C and +0x54). The first animation after the revive (flags 0xE, in the same step) put the
             * fighter back where its knocked-out body lay, so nobody ever came back at a spawn point (builds 86 and
             * before; seen in the free-roam tests: "back at 260,0", then tracked where it fell). */
            MEMF(r + 0xC4u) = c.spawn_x[i];
            MEMF(r + 0xC8u) = floor_y;
            MEMF(r + 0xCCu) = c.spawn_z[i];
            MEMF(r + 0xD4u) = yaw;
        }
    }
    h = MEM32(HUD_PLAYERS + 4u * (uint32_t)s);
    if (h >= 0x10000u && h < 0x84000000u) { MEM8(h + 0x1104u) = 0; MEMF(h + 0x1108u) = 0.0f; }
    BA.down[s] = 0;
    BA.last_hit[s] = -1;
    BA.roam_mode[s] = OW_ROAM_FREE;     /* back on the street: free until it fights */
    BA.roam_pending[s] = -1;
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

static int slot_of_record(uint32_t r);
static void cpu_idle_step(uint32_t pres);

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
            BA.end_kind = (uint8_t)(limit == 1 ? END_SCORE : END_TIME);
            BA.end_step = BA.step;
            /* the KO target: half a second for the winning blow to land; the clock: TIME! and the winner first,
             * everyone held at rest and unhurt (ow_pad_hold, ow_health_change), then the losers go down */
            BA.next_loser_at = BA.step + (limit == 1 ? 30u : ARENA_TIME_HOLD);
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
    cpu_idle_step(pres);
}

/* CPU fighters that stop fighting (build 88). On a big map two CPUs that target each other can stand face to face,
 * idle or blocking, for minutes (run logs/dj/20261117-100442-offline: slots 1 and 3, 13 units apart, states 1/2,
 * the whole rest of a 3-minute round), and only a hit from someone else (the game's retarget on a hit) wakes them.
 * The game changes a target only when it is down (sub_001C9610) or on the target buttons (sub_001C99B0, which the AI
 * presses itself in a small ring, where everyone is in reach). Here: a CPU that has stood within CPU_IDLE_RADIUS of
 * one place in states 1 (stand/walk) or 2 (block) for CPU_IDLE_STEPS takes the nearest other standing fighter as its
 * target (+0x00, which its AI follows); the same rule with nobody else keeps the target. Decided from the records and
 * BA inside the step, so every PC does the same; BA is saved and checksummed. */
#define CPU_IDLE_STEPS  240         /* 4 s */
#define CPU_IDLE_RADIUS 6           /* units */
static void cpu_idle_step(uint32_t pres)
{
    int s, k;
    float x[4], z[4];
    for (k = 0; k < 4; k++) { x[k] = MEMF(rec(k) + 0x44u); z[k] = MEMF(rec(k) + 0x4Cu); }
    for (s = 0; s < 4; s++) {
        uint32_t r = rec(s), st, cand = 0;
        int cur, c, busy;
        if (!(pres & (1u << s)) || !(MEM8(r + 0x937u) & 1u)) continue;
        st = MEM32(r + 0x30u);
        busy = (st != 1u && st != 2u) || (MEM16(r + 0x932u) & (ENDED | 0x400u)) != 0;
        if (!ow_rules_cpu_idle_step(&BA.cpu_ax[s], &BA.cpu_az[s], &BA.cpu_still[s], x[s], z[s], busy,
                                    CPU_IDLE_RADIUS, CPU_IDLE_STEPS))
            continue;
        for (k = 0; k < 4; k++) {
            uint16_t fl;
            if (k == s || !(pres & (1u << k))) continue;
            fl = MEM16(rec(k) + 0x932u);
            if (!(fl & (ENDED | 0x400u))) cand |= 1u << k;
        }
        cur = slot_of_record(MEM32(r));
        c = ow_rules_cpu_retarget(s, cur, x, z, cand);
        if (c < 0) continue;
        MEM32(r) = rec(c);
        if (!netplay_rerunning())
            fprintf(stderr, "[BIGARENA] step %u: CPU slot %d stood still %d s (state %u): target %d -> %d\n", BA.step, s,
                    CPU_IDLE_STEPS / 60, st, cur, c);
    }
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
    rts_social_combat(attacker, defender);      /* build 91: the minimap's combat pings (host memory only) */
    if (!BA.on) return;
    a = slot_of_record(attacker);
    d = slot_of_record(defender);
    if (a >= 0 && d >= 0 && a != d) {
        BA.last_hit[d] = (int8_t)a;
        if (BA.roam_on) {           /* a hit is a fight for both: the calm starts again, a free one engages */
            BA.roam_last[a] = BA.roam_last[d] = BA.step;
            if (BA.roam_mode[d] == OW_ROAM_FREE) BA.roam_pending[d] = (int8_t)a;
            if (BA.roam_mode[a] == OW_ROAM_FREE) BA.roam_pending[a] = (int8_t)d;
        }
    }
}

/* ---------------------------------------------------------------- free roam */
/* In the stock game a fighter always faces its target (record +0x00): the per-step cache sub_001A4A30 writes before
 * the state handlers (+0x838 the angle, +0x834 the distance, from the pair tables 0x3C04A8 / 0x3C04E8 that
 * sub_001B3EA0 fills), the walker sub_001CAC40 turns +0x54 toward +0x838 (sub_001B3E10) and moves in the stick's
 * world direction (the nibble's angle from the table at 0x2EF510), choosing forward, strafe or back by the difference
 * to the latched target angle +0x3EC. Free roam rewrites that cache for a free human fighter at sub_001BA800's entry
 * (after the cache, before the handlers): the angle becomes the stick's direction (or the fighter's own facing when
 * the stick rests) and the distance far, so it turns to where it walks and walks forward. Its target stays on the
 * nearest standing fighter, so the game's own retarget (sub_001C9610, when the target is down) never turns it.
 *
 *   free -> engaged: a fight button (0x30F0) with a fighter within ROAM_ENGAGE_RANGE (that one becomes the target),
 *                    or a hit given or taken (the other one becomes the target);
 *   free/engaged -> locked: White or Black (the game's target switch, sub_001C99B0) or a right-stick flick (its
 *                    gesture, sub_001C9DF0 -> state 0x55): the fighter best in front within ROAM_PICK_RANGE, else the
 *                    nearest within ROAM_PICK_NEAR; the press goes on as a flick so the fighter gestures at it, and it turns to face it. Nobody
 *                    there: the press is dropped. While locked White/Black switch as in the game;
 *   engaged -> free: roam_steps without a fight button, a hit or a standing fighter within ROAM_CALM_NEAR;
 *   locked -> free:  White and Black together, or the target farther than ROAM_LOCK_DROP;
 *   locked -> engaged: the target is knocked out.
 * CPU fighters keep the game's own targeting. Everything is read from the fight's records and inputs and kept in BA
 * (saved, loaded and checksummed with it), so every PC computes the same. */
#define ROAM_ACTIONS        0x30F0u     /* the face buttons and triggers as the handlers see them (+0x9F0) */
#define ROAM_SELECT         0x0300u     /* White, Black: the target switch */
#define ROAM_FLICK          0x20000u    /* the right stick pushed (newly) */
#define ROAM_PICK_RANGE     300.0f      /* ~26 m (11.7 units a metre) */
#define ROAM_PICK_CONE      60.0f
#define ROAM_PICK_NEAR      140.0f      /* ~12 m all around */
#define ROAM_ENGAGE_RANGE   70.0f       /* ~6 m: a fight button engages a fighter this close, in front */
#define ROAM_ENGAGE_NEAR    45.0f       /* ~4 m all around */
#define ROAM_LOCK_DROP      470.0f      /* ~40 m */
#define ROAM_CALM_NEAR      45.0f       /* ~4 m: a standing fighter this close keeps an engaged one engaged */
#define ROAM_TURN_STEPS     30          /* the turn to a target just locked on to: at most this many steps */
#define ROAM_TURN_RATE      12.0f       /* degrees a step */
#define PAIR_DIST           0x003C04E8u
#define PAIR_ANGLE          0x003C04A8u

static const char *const k_roam_name[3] = { "free", "engaged", "locked" };

static int target_slot(uint32_t r) { return slot_of_record(MEM32(r)); }

/* Fighters a slot can take as its target: in the fight, not itself, not out; with standing_only also not down. */
static uint32_t roam_candidates(int s, int standing_only)
{
    uint32_t m = 0;
    int k;
    for (k = 0; k < 4; k++) {
        uint16_t fl;
        if (k == s || !present(k)) continue;
        fl = MEM16(rec(k) + 0x932u);
        if (fl & ENDED) continue;
        if (standing_only && (fl & 0x400u)) continue;
        m |= 1u << k;
    }
    return m;
}

static int roam_pick(int s, float range, float cone, float near_, int standing_only)
{
    float cx[4], cz[4];
    uint32_t r = rec(s);
    int k;
    for (k = 0; k < 4; k++) { cx[k] = MEMF(rec(k) + 0x44u); cz[k] = MEMF(rec(k) + 0x4Cu); }
    return ow_rules_roam_pick(MEMF(r + 0x44u), MEMF(r + 0x4Cu), MEMF(r + 0x54u), cx, cz,
                              roam_candidates(s, standing_only), range, cone, near_);
}

/* The slot's target becomes c, and this step's cache with it (as sub_001A4A30 wrote it: self * 4 + target). */
static void roam_set_target(uint32_t r, int s, int c)
{
    uint32_t i = (uint32_t)s * 4u + (uint32_t)c;
    MEM32(r) = rec(c);
    MEMF(r + 0x834u) = MEMF(PAIR_DIST + 4u * i);
    MEMF(r + 0x838u) = MEMF(PAIR_ANGLE + 4u * i);
}

static void roam_log(int s, int from, int to, const char *why, int target)
{
    if (netplay_rerunning() || from == to) return;
    fprintf(stderr, "[OWROAM] step %u: slot %d %s -> %s (%s), target %d, at %.0f,%.0f yaw %.0f\n", BA.step, s,
            k_roam_name[from % 3], k_roam_name[to % 3], why, target, (double)MEMF(rec(s) + 0x44u),
            (double)MEMF(rec(s) + 0x4Cu), (double)MEMF(rec(s) + 0x54u));
}

void ow_roam_input(uint32_t r)
{
    int s = slot_of_record(r), c, t;
    uint32_t pressed, held;
    uint8_t m, m0;
    const char *why = "";
    float yaw;
    if (!BA.on || !BA.roam_on || s < 0 || !present(s)) return;
    if (MEM8(r + 0x937u) & 1u) return;                         /* a CPU: the game's own targeting */
    if (BA.ending || (MEM16(r + 0x932u) & ENDED)) return;      /* the end, or out until it comes back */
    m = m0 = BA.roam_mode[s];
    pressed = MEM32(r + 0x9F0u);
    held = MEM32(r + 0x9E0u);
    c = BA.roam_pending[s];
    BA.roam_pending[s] = -1;
    if (c >= 0 && m == OW_ROAM_FREE && (roam_candidates(s, 0) & (1u << c))) {
        roam_set_target(r, s, c);
        m = OW_ROAM_ENGAGED;
        why = "hit";
    }
    if ((held & ROAM_SELECT) == ROAM_SELECT && (pressed & ROAM_SELECT)) {
        if (m != OW_ROAM_FREE) { m = OW_ROAM_FREE; why = "White+Black"; }
        pressed &= ~(ROAM_SELECT | ROAM_FLICK);
    } else if ((pressed & (ROAM_SELECT | ROAM_FLICK)) && m != OW_ROAM_LOCKED) {
        c = roam_pick(s, ROAM_PICK_RANGE, ROAM_PICK_CONE, ROAM_PICK_NEAR, 0);
        if (c >= 0) {
            roam_set_target(r, s, c);
            m = OW_ROAM_LOCKED;
            why = (pressed & ROAM_SELECT) ? "target button" : "right stick";
            BA.roam_last[s] = BA.step;
            BA.roam_turn[s] = ROAM_TURN_STEPS;
            pressed = (pressed & ~ROAM_SELECT) | ROAM_FLICK;   /* the game's own gesture at it (state 0x55) */
        } else {
            pressed &= ~(ROAM_SELECT | ROAM_FLICK);           /* nobody near: no turn toward a far fighter */
        }
    }
    if (pressed & ROAM_ACTIONS) {
        BA.roam_last[s] = BA.step;
        if (m == OW_ROAM_FREE) {
            c = roam_pick(s, ROAM_ENGAGE_RANGE, 90.0f, ROAM_ENGAGE_NEAR, 0);
            if (c >= 0) { roam_set_target(r, s, c); m = OW_ROAM_ENGAGED; why = "fight button"; }
        }
    }
    if (m == OW_ROAM_ENGAGED) {
        /* someone standing right next to it is a fight too, hits or not (a CPU that blocks and circles) */
        c = roam_pick(s, 0.0f, 0.0f, ROAM_CALM_NEAR, 1);
        if (c >= 0) BA.roam_last[s] = BA.step;
        else if (BA.step - BA.roam_last[s] >= BA.roam_steps) { m = OW_ROAM_FREE; why = "calm"; }
    }
    if (m == OW_ROAM_LOCKED) {
        t = target_slot(r);
        if (t < 0 || t == s || !present(t) || (MEM16(rec(t) + 0x932u) & ENDED)) {
            m = OW_ROAM_ENGAGED;
            BA.roam_last[s] = BA.step;
            why = "target out";
        } else if (MEMF(PAIR_DIST + 4u * ((uint32_t)s * 4u + (uint32_t)t)) > ROAM_LOCK_DROP) {
            m = OW_ROAM_FREE;
            why = "target far";
        }
    }
    MEM32(r + 0x9F0u) = pressed;
    BA.roam_mode[s] = m;
    roam_log(s, m0, m, why, target_slot(r));
    if (m != OW_ROAM_FREE) {
        /* just locked on: face the target (the game turns an idle fighter only when it is more than 80 degrees
         * off, and its gesture state 0x55 does not turn), up to ROAM_TURN_RATE degrees a step, while it stands or
         * gestures and the stick rests */
        uint32_t st = MEM32(r + 0x30u);
        t = target_slot(r);
        if (BA.roam_turn[s] && t >= 0 && (st == 1u || st == 0x55u) && !(held & 0xFu)) {
            float want = MEMF(PAIR_ANGLE + 4u * ((uint32_t)s * 4u + (uint32_t)t));
            float d = ow_rules_angle_diff(MEMF(r + 0x54u), want);
            if (d > ROAM_TURN_RATE) d = ROAM_TURN_RATE;
            if (d < -ROAM_TURN_RATE) d = -ROAM_TURN_RATE;
            MEMF(r + 0x54u) = MEMF(r + 0x54u) + d;
            BA.roam_turn[s]--;
            if (d > -0.5f && d < 0.5f) BA.roam_turn[s] = 0;
        } else {
            BA.roam_turn[s] = 0;
        }
        return;
    }
    BA.roam_turn[s] = 0;
    /* free and standing: the game's idle turn toward the target (state 1, sub_001CAC40 without a direction: more
     * than 80 degrees off -> +0x910 bits 0/1, +0x81C the target's angle) is held off by its own cool-down, bit 1
     * with the timer +0x90C (cleared when the timer passes 10 steps, 0x29F7B0), so once the fighter fights again the
     * game turns it after that short wait. A turn already started ends where the fighter faces. */
    if (MEM8(r + 0x910u) & 1u) MEMF(r + 0x81Cu) = MEMF(r + 0x54u);
    MEM8(r + 0x910u) = (uint8_t)(MEM8(r + 0x910u) | 2u);
    MEMF(r + 0x90Cu) = 0.0f;
    /* free: the target on the nearest standing fighter (quiet for the game's retarget), the cache on the stick */
    t = target_slot(r);
    c = roam_pick(s, 0.0f, 0.0f, 1e9f, 1);
    if (c >= 0 && c != t) MEM32(r) = rec(c);
    MEMF(r + 0x834u) = 1000.0f;
    MEMF(r + 0x838u) = ow_rules_nibble_heading(held, &yaw) ? yaw : MEMF(r + 0x54u);
}

/* sub_001CAC40's entry (cdecl record, held bits, speed): a free fighter walks at roam_speed percent. */
void ow_roam_walk(void)
{
    uint32_t r = MEM32(g_esp + 4);
    int s = slot_of_record(r);
    if (!BA.on || !BA.roam_on || s < 0 || BA.roam_speed == 100 || BA.roam_mode[s] != OW_ROAM_FREE) return;
    if (MEM8(r + 0x937u) & 1u) return;
    MEMF(g_esp + 12) = MEMF(g_esp + 12) * ((float)BA.roam_speed / 100.0f);
}

int ow_roam_view(int slot, int *mode, int *target)
{
    *mode = OW_ROAM_FREE;
    *target = -1;
    if (!BA.on || !BA.roam_on || slot < 0 || slot > 3 || !present(slot)) return 0;
    *mode = BA.roam_mode[slot];
    *target = *mode == OW_ROAM_FREE ? -1 : target_slot(rec(slot));
    return 1;
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
    if (BA.ending && BA.end_kind == END_TIME) { MEMF(g_esp + 8) = 0.0f; return; }    /* TIME!: nobody is hurt any more */
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
    out->steps_left = BA.time_limit_steps ? (int)(BA.time_limit_steps > BA.step ? BA.time_limit_steps - BA.step : 0) : -1;
    out->seconds_left = out->steps_left >= 0 ? (out->steps_left + 59) / 60 : -1;     /* 0:00 only once it is up */
    out->unlimited = BA.unlimited;
    out->time_up = BA.ending && BA.end_kind == END_TIME;
    out->end_step = BA.end_step;
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

/* sub_001BAA00's entry, after netplay.c put this step's entry in place (and the start countdown's hold): once the
 * clock has ended a Big Arena fight (TIME!), every fighter's pad words are at rest, the CPU's too, so nobody lands a
 * blow while TIME! and the winner show. Decided from BA, the same on every PC at the same step. */
void ow_pad_hold(uint32_t actor)
{
    uint32_t slot, pad, base, i;
    if (!BA.on || !BA.ending || BA.end_kind != END_TIME) return;
    if (actor < ACTORS || actor >= ACTORS + 4u * STRIDE || (actor - ACTORS) % STRIDE) return;
    slot = MEM32(actor + 0x28u);
    if (slot >= 8u) return;
    pad = MEM32(0x003B9098u + slot * 4u);       /* the pad entry of the controller slot (start_countdown.c) */
    if (pad >= 4u) return;
    base = PAD_TABLE_OW + pad * 0x20u;
    for (i = 0; i < 7u; i++) MEM32(base + i * 4u) = 0;
}

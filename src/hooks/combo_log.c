/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Combo log, RECOMP_COMBO_LOG=1: one [COMBO] line per hit and per state change that matters,
 * so play-testers and mods can replay legal and illegal combos and a detector can be designed
 * from real data later (docs/research/combo-log.md). Diagnostic only: it reads the fighter
 * records, keeps its own per-victim combo counters and writes to the run log. It changes no
 * game memory, sends nothing anywhere and has no penalties. Off by default.
 *
 * Sources. The hit is the entry of sub_001C6CD0 (cdecl attacker, defender, hit flags), which
 * copies the attacker's move block (+0xF50, 0x68 bytes) to the defender's +0xFB8 and dispatches
 * the reaction. The fight step is the entry of sub_00087700. A hit's "after" state is read at
 * the next step (or at the next hit on the same victim, whichever comes first).
 *
 * The game has no combo counter, so this keeps one per victim: a combo opens on a hit that is
 * not blocked and closes when the victim is back in a free stance (state 1), blocks (2, 3),
 * strikes or grabs on its own, is knocked out (0x18) or the match is set up again. Steps run again by rollback are skipped
 * (netplay_rerunning), so online the log follows the steps as first run.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "host.h"
#include "recomp/gen/recomp_types.h"
#include "combo_log.h"
#include "netplay.h"

#define ACTORS      0x003B92E0u
#define STRIDE      0x12E8u
#define F_STATE     0x30u
#define F_PHASE     0x34u
#define F_METER     0x374u
#define F_REHIT     0x384u      /* pinned re-hit counter, reset when a pin begins */
#define F_STEP      0x40Cu      /* the attacker's strike chain step (1..4) */
#define F_FLAGS_927 0x927u      /* bit 2: stunned (the meter was full) */
#define F_MOVE      0xF50u      /* the current move block; its first word is logged */

#define ST_FREE     0x01u
#define ST_BLOCK    0x02u       /* holding block */
#define ST_BLOCKED  0x03u       /* a hit taken on the block (live: 2 > 3 > 2) */
#define ST_KO       0x18u
#define ST_GRAB     0x1Eu
#define LINE_CAP    400000u     /* lines per run, then a single notice */

int g_combo_log_on = -1;

static struct victim {
    int open;               /* a combo is running on this fighter */
    unsigned idx;           /* combos taken by this fighter so far (the open one included) */
    unsigned hits, bc, ws, stuns, dizzies;
    int attacker;           /* slot of the last hit's attacker */
    uint32_t move;          /* the last hit's move word */
    uint32_t last_hit_step, left_wall_step;
    int pending;            /* a hit waiting for its "after" state */
    uint32_t shown_step, shown_from, shown_to;   /* the change a hit line just printed */
    struct { int a; uint32_t move, flags, before, astate, cstep, rehit; float meter; uint32_t step; } p;
} V[4];

static struct fighter {
    uint32_t state, phase;
    int stun, seen;
    int blocked_mid;        /* as an attacker: entered block while its combo was open */
} F[4];

static uint32_t s_step;
static unsigned s_lines;

int combo_log_init(void)
{
    const char *v = getenv("RECOMP_COMBO_LOG");
    int i;
    g_combo_log_on = v && v[0] == '1' && v[1] == 0;
    for (i = 0; i < 4; i++) V[i].attacker = -1;
    if (g_combo_log_on)
        fprintf(stderr, "[COMBO] on: one line per hit and per victim state change (docs/research/combo-log.md)\n");
    return g_combo_log_on;
}

static int line_ok(void)
{
    if (s_lines < LINE_CAP) { s_lines++; return 1; }
    if (s_lines++ == LINE_CAP) fprintf(stderr, "[COMBO] line cap %u reached, no more lines this run\n", LINE_CAP);
    return 0;
}

static int slot_of(uint32_t rec)
{
    if (rec < ACTORS || rec >= ACTORS + 4u * STRIDE || (rec - ACTORS) % STRIDE) return -1;
    return (int)((rec - ACTORS) / STRIDE);
}
static uint32_t rec(int slot) { return ACTORS + (uint32_t)slot * STRIDE; }
static uint32_t st(int slot) { return MEM32(rec(slot) + F_STATE); }
static int stun_flag(int slot) { return (MEM8(rec(slot) + F_FLAGS_927) & 4u) != 0; }
static float meter(int slot) { float m = MEMF(rec(slot) + F_METER); return isfinite(m) ? m : -1.0f; }

static int is_wall(uint32_t s) { return s == 0x37u || s == 0x14u || s == 0x1Bu || s == 0x08u || s == 0x60u; }
static int is_dizzy(uint32_t s) { return s == 0x16u; }      /* not seen live yet; 0x15 is a knockdown */
static int is_blocking(uint32_t s) { return s == ST_BLOCK || s == ST_BLOCKED; }
/* the victim acts on its own again: a strike or a grab (attack states 0x45..0x4B, 0x6C..0x70) */
static int is_acting(uint32_t s) { return (s >= 0x45u && s <= 0x4Bu) || (s >= 0x6Cu && s <= 0x70u) || s == ST_GRAB; }

static void close_combo(int v, const char *why)
{
    struct victim *c = &V[v];
    if (!c->open) return;
    c->open = 0;
    if (line_ok())
        fprintf(stderr, "[COMBO] s=%u end v=%d a=%d c=%u hits=%u bc=%u ws=%u stuns=%u dz=%u why=%s\n",
                s_step, v, c->attacker, c->idx, c->hits, c->bc, c->ws, c->stuns, c->dizzies, why);
    if (c->attacker >= 0) F[c->attacker].blocked_mid = 0;
}

/* The pending hit on v gets its "after" state now and goes into the combo bookkeeping. */
static void flush(int v)
{
    struct victim *c = &V[v];
    uint32_t after;
    int blocked, a;
    if (!c->pending) return;
    c->pending = 0;
    after = st(v);
    a = c->p.a;
    c->shown_step = s_step; c->shown_from = c->p.before; c->shown_to = after;
    blocked = is_blocking(c->p.before) || after == ST_BLOCKED;
    if (blocked) {
        close_combo(v, "block");
        if (line_ok())
            fprintf(stderr, "[COMBO] s=%u hit a=%d v=%d mv=%08X ast=%02X cs=%u fl=%X vst=%02X>%02X blk=1\n",
                    c->p.step, a, v, c->p.move, c->p.astate, c->p.cstep, c->p.flags, c->p.before, after);
        return;
    }
    if (!c->open || c->attacker != a) {
        if (c->open) close_combo(v, "new_attacker");
        c->open = 1;
        c->idx++;
        c->hits = c->bc = c->stuns = c->dizzies = 0;
        c->ws = is_wall(c->p.before) ? 1u : 0u;
        F[a].blocked_mid = 0;
    }
    c->hits++;
    c->attacker = a;
    c->move = c->p.move;
    c->last_hit_step = c->p.step;
    if (F[a].blocked_mid) {
        F[a].blocked_mid = 0;
        c->bc++;
        if (line_ok())
            fprintf(stderr, "[COMBO] s=%u bcancel a=%d v=%d c=%u h=%u n=%u mv=%08X\n",
                    c->p.step, a, v, c->idx, c->hits, c->bc, c->p.move);
    }
    if (line_ok())
        fprintf(stderr, "[COMBO] s=%u hit a=%d v=%d mv=%08X ast=%02X cs=%u fl=%X vst=%02X>%02X wall=%d stun=%d dz=%d"
                        " rh=%u m=%.2f c=%u h=%u bc=%u ws=%u\n",
                c->p.step, a, v, c->p.move, c->p.astate, c->p.cstep, c->p.flags, c->p.before, after,
                is_wall(after), stun_flag(v), is_dizzy(after), MEM32(rec(v) + F_REHIT), (double)meter(v),
                c->idx, c->hits, c->bc, c->ws);
}

void combo_log_hit(uint32_t attacker, uint32_t defender, uint32_t flags)
{
    int a = slot_of(attacker), v = slot_of(defender);
    struct victim *c;
    if (a < 0 || v < 0 || a == v || netplay_rerunning()) return;
    if ((MEM16(defender + 0x91Eu) >> 8) & 0xEu) return;     /* the body returns at once: no hit */
    c = &V[v];
    flush(v);
    c->pending = 1;
    c->p.a = a;
    c->p.move = MEM32(attacker + F_MOVE);
    c->p.flags = flags;
    c->p.before = MEM32(defender + F_STATE);
    c->p.astate = MEM32(attacker + F_STATE);
    c->p.cstep = MEM32(attacker + F_STEP);
    c->p.rehit = MEM32(defender + F_REHIT);
    c->p.meter = meter(v);
    c->p.step = s_step;
}

/* The fighter that a grab by a is aimed at: the victim of its open combo, or of its most recent hit. */
static int in_fight(int slot)      /* the fight camera's slot table (0x340D70, stride 0xC8) */
{
    uint32_t cam = 0x340D70u + (uint32_t)slot * 0xC8u;
    return MEM32(cam) != 0 && (MEM8(cam + 0xC0u) & 8u) == 0;
}

static int grab_target(int a)
{
    int v, best = -1;
    float bd = 0, ax = MEMF(rec(a) + 0x44u), az = MEMF(rec(a) + 0x4Cu);
    for (v = 0; v < 4; v++)
        if (v != a && V[v].attacker == a && V[v].idx && (best < 0 || V[v].last_hit_step > V[best].last_hit_step))
            best = v;
    if (best >= 0) return best;
    for (v = 0; v < 4; v++) {      /* no hit yet: the nearest other fighter */
        float dx, dz, d;
        if (v == a || !in_fight(v)) continue;
        dx = MEMF(rec(v) + 0x44u) - ax; dz = MEMF(rec(v) + 0x4Cu) - az; d = dx * dx + dz * dz;
        if (isfinite(d) && (best < 0 || d < bd)) { best = v; bd = d; }
    }
    return best;
}

void combo_log_step(void)
{
    int i;
    if (!combo_log_on() || netplay_rerunning()) return;
    s_step++;
    for (i = 0; i < 4; i++) flush(i);
    for (i = 0; i < 4; i++) {
        struct fighter *f = &F[i];
        struct victim *c = &V[i];
        uint32_t s = st(i), ph = MEM32(rec(i) + F_PHASE);
        int sf = stun_flag(i);
        if (!f->seen) { f->seen = 1; f->state = s; f->phase = ph; f->stun = sf; continue; }
        if (c->open && (s != f->state || sf != f->stun)) {
            const char *tag = "";
            int plain, dup;
            if (is_wall(s) && !is_wall(f->state)) { tag = " wall+"; c->ws++; }
            else if (!is_wall(s) && is_wall(f->state)) { tag = " wall-"; c->left_wall_step = s_step; }
            plain = !tag[0] && sf == f->stun && is_dizzy(s) == is_dizzy(f->state);
            dup = plain && c->shown_step == s_step && c->shown_from == f->state && c->shown_to == s;
            if (!dup && line_ok())
                fprintf(stderr, "[COMBO] s=%u st v=%d %02X>%02X ph=%u%s%s%s wall=%d stun=%d dz=%d rh=%u m=%.2f c=%u h=%u ws=%u lastmv=%08X\n",
                        s_step, i, f->state, s, ph, tag,
                        sf && !f->stun ? " stun+" : (!sf && f->stun ? " stun-" : ""),
                        is_dizzy(s) && !is_dizzy(f->state) ? " dizzy+" : (!is_dizzy(s) && is_dizzy(f->state) ? " dizzy-" : ""),
                        is_wall(s), sf, is_dizzy(s), MEM32(rec(i) + F_REHIT), (double)meter(i),
                        c->idx, c->hits, c->ws, c->move);
            if (sf && !f->stun) c->stuns++;
            if (is_dizzy(s) && !is_dizzy(f->state)) c->dizzies++;
        } else if (!c->open && is_wall(f->state) && !is_wall(s)) {
            c->left_wall_step = s_step;
        }
        if (c->open && !c->pending) {
            if (s == ST_FREE) close_combo(i, "free");
            else if (is_blocking(s)) close_combo(i, "block");
            else if (s == ST_KO) close_combo(i, "ko");
            else if (is_acting(s)) close_combo(i, "acts");
        }
        /* attacker side: block mid-combo (a block cancel when it hits again) and grabs */
        if (s != f->state) {
            int v = grab_target(i);
            if (s == ST_BLOCK && v >= 0 && V[v].open) {
                F[i].blocked_mid = 1;
                if (line_ok()) fprintf(stderr, "[COMBO] s=%u ablock a=%d v=%d from=%02X c=%u h=%u vst=%02X\n",
                        s_step, i, v, f->state, V[v].idx, V[v].hits, st(v));
            }
            if (s == ST_GRAB && line_ok()) {
                uint32_t vs = v >= 0 ? st(v) : 0xFFu;
                fprintf(stderr, "[COMBO] s=%u grab a=%d v=%d vst=%02X wall=%d stun=%d dz=%d open=%d c=%u h=%u ws=%u offwall=%d sincehit=%d\n",
                        s_step, i, v, vs, v >= 0 && is_wall(vs), v >= 0 && stun_flag(v), v >= 0 && is_dizzy(vs),
                        v >= 0 && V[v].open, v >= 0 ? V[v].idx : 0u, v >= 0 ? V[v].hits : 0u, v >= 0 ? V[v].ws : 0u,
                        v >= 0 && V[v].left_wall_step ? (int)(s_step - V[v].left_wall_step) : -1,
                        v >= 0 ? (int)(s_step - V[v].last_hit_step) : -1);
            }
        }
        f->state = s; f->phase = ph; f->stun = sf;
    }
}

void combo_log_match_start(void)
{
    int i;
    if (!combo_log_on()) return;
    for (i = 0; i < 4; i++) {
        V[i].pending = 0;
        close_combo(i, "match");
    }
    memset(V, 0, sizeof V);
    memset(F, 0, sizeof F);
    for (i = 0; i < 4; i++) V[i].attacker = -1;
    if (line_ok()) fprintf(stderr, "[COMBO] s=%u match\n", s_step);
    s_step = 0;
}

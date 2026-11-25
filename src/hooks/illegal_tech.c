/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Illegal-tech detection, the game side (illegal_tech.h; the rules: illegal_tech_rules.c).
 *
 * Determinism. The detector reads only the records of the fighters in the match (a record whose +0x28 names
 * its own slot), as each fight step begins and as each hit lands: the same words the lockstep checksum covers
 * or that follow from them. It runs in every run of a step, also the ones rollback runs again; its state is a
 * plain block (ItState) that each rollback save copies and each load puts back, and the checksum hashes it, so
 * both PCs of a match list the same detections at the same steps. Whether they are reported (ranked and
 * tournament set games) is decided per match and never changes what the detector does.
 *
 * Logs. [ILLEGAL-TECH] lines name each detection when a step first runs, and the match's list when it is
 * reported. RECOMP_ILLEGAL_TECH_TRACE=1 (tests, research) also writes [ITRACE] lines: every hit, and every
 * change of a fighter's state, phase or stun flag, with the fields the rules read.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host.h"
#include "recomp/gen/recomp_types.h"
#include "illegal_tech.h"
#include "illegal_tech_rules.h"
#include "netplay.h"

#define ACTORS      0x003B92E0u
#define STRIDE      0x12E8u
#define MANAGERS    0x002FB148u     /* the game's registry; slot 0x18 the game data (new_fighters.c) */
#define GD_FIGHTER  0x1AAAu         /* + 5 * slot: the set-up's fighter byte (fighter.xml's ID) */
#define F_SLOT      0x28u
#define F_STATE     0x30u
#define F_PHASE     0x34u
#define F_REHIT     0x384u
#define F_BEARING   0x918u
#define F_FLAGS_91E 0x91Eu
#define F_FLAGS_927 0x927u
#define F_MOVE      0xF50u
#define F_GOT_MOVE  0xFB8u

typedef char it_state_fits[sizeof(ItState) <= ILLEGAL_TECH_STATE_BYTES ? 1 : -1];
typedef char it_state_words[sizeof(ItState) % 4 == 0 ? 1 : -1];

static ItState S;
static uint32_t s_setup_fighter[IT_SLOTS];     /* the set-up's fighter bytes at the match's set-up (host only) */
static int s_reset_done;
static int s_reported;
static int s_switch = -1;      /* 0 normal, 1 test (report in casual too), 2 off (report nothing) */
static int s_trace = -1;
static uint32_t s_trace_state[IT_SLOTS], s_trace_phase[IT_SLOTS], s_trace_stun[IT_SLOTS];

int illegal_tech_test_switch(void)
{
    if (s_switch < 0) {
        const char *v = getenv("RECOMP_ILLEGAL_TECH");
        s_switch = v && !strcmp(v, "test") ? 1 : v && !strcmp(v, "off") ? 2 : v && !strcmp(v, "sensitive") ? 3 : 0;
        if (s_switch == 1 || s_switch == 3)
            fprintf(stderr, "[ILLEGAL-TECH] test switch: detections are reported in every online match%s\n",
                    s_switch == 3 ? "; sensitive: the first block cancel, any wall stick and any stun count (every PC of the "
                    "match must have it)" : "");
    }
    return s_switch;
}

static int trace_on(void)
{
    if (s_trace < 0) s_trace = getenv("RECOMP_ILLEGAL_TECH_TRACE") && getenv("RECOMP_ILLEGAL_TECH_TRACE")[0] == '1';
    return s_trace;
}

static uint32_t rec(int slot) { return ACTORS + (uint32_t)slot * STRIDE; }

static int slot_of(uint32_t r)
{
    if (r < ACTORS || r >= ACTORS + IT_SLOTS * STRIDE || (r - ACTORS) % STRIDE) return -1;
    return (int)((r - ACTORS) / STRIDE);
}

static void observe(int slot, ItObs *o)
{
    uint32_t r = rec(slot);
    memset(o, 0, sizeof(*o));
    o->present = (int32_t)MEM32(r + F_SLOT) == slot;
    if (!o->present) return;
    o->state = MEM32(r + F_STATE);
    o->phase = MEM32(r + F_PHASE);
    o->stun = (MEM8(r + F_FLAGS_927) & 4u) ? 1u : 0u;
    /* the fighter byte goes into the state (and so the checksum) only once the slot is seen fighting: an unused
     * slot's byte is whatever the PC's last set-up left there */
    if (S.fighter[slot] == 0xFFFFFFFFu) S.fighter[slot] = s_setup_fighter[slot];
    o->character = S.fighter[slot];
    o->rehit = MEM32(r + F_REHIT);
    o->move = MEM32(r + F_MOVE);
    o->got_move = MEM32(r + F_GOT_MOVE);
    o->bearing = MEM32(r + F_BEARING);
}

static void log_detections(unsigned from)
{
    unsigned i;
    for (i = from; i < S.det_n; i++) {
        const ItDet *d = &S.det[i];
        fprintf(stderr, "[ILLEGAL-TECH] s=%u %s a=%u v=%u af=%d vf=%d mv=%08X c=%u%s\n", d->step, it_rule_id(d->rule),
                d->attacker, d->victim, (int)d->a_char, (int)d->v_char, d->move, d->count,
                s_reported ? "" : " (not reported: not a ranked or tournament game)");
    }
}

void illegal_tech_match_start(void)
{
    uint32_t gd = MEM32(MANAGERS + 0x18u * 4u);
    int i;
    it_reset(&S);
    S.sensitive = illegal_tech_test_switch() == 3;
    for (i = 0; i < IT_SLOTS; i++) s_setup_fighter[i] = gd ? MEM8(gd + GD_FIGHTER + 5u * (uint32_t)i) : 0xFFFFFFFEu;
    s_reset_done = 1;
    memset(s_trace_state, 0xFF, sizeof(s_trace_state));
    if (trace_on())
        fprintf(stderr, "[ITRACE] match fighters %d %d %d %d\n", (int)s_setup_fighter[0], (int)s_setup_fighter[1],
                (int)s_setup_fighter[2], (int)s_setup_fighter[3]);
}

void illegal_tech_hit(uint32_t attacker, uint32_t defender, uint32_t flags)
{
    int a = slot_of(attacker), v = slot_of(defender);
    ItObs ao, vo;
    if (!s_reset_done || a < 0 || v < 0 || a == v) return;
    if ((MEM16(defender + F_FLAGS_91E) >> 8) & 0xEu) return;      /* the hit's body returns at once: no hit */
    observe(a, &ao);
    observe(v, &vo);
    if (!ao.present || !vo.present) return;
    it_hit(&S, a, v, flags, &ao, &vo);
    if (trace_on() && !netplay_rerunning())
        fprintf(stderr, "[ITRACE] s=%u hit a=%d v=%d fl=%X amv=%08X ast=%02X vst=%02X vbear=%X rh=%u stun=%u\n", S.step, a, v,
                flags, ao.move, ao.state, vo.state, vo.bearing, vo.rehit, vo.stun);
}

void illegal_tech_step(void)
{
    ItObs obs[IT_SLOTS];
    unsigned before = S.det_n;
    int i, found;
    if (!s_reset_done) return;
    for (i = 0; i < IT_SLOTS; i++) observe(i, &obs[i]);
    found = it_step(&S, obs);
    if (netplay_rerunning()) return;
    if (trace_on())
        for (i = 0; i < IT_SLOTS; i++) {
            const ItObs *o = &obs[i];
            if (!o->present) continue;
            if (o->state != s_trace_state[i] || o->phase != s_trace_phase[i] || o->stun != s_trace_stun[i])
                fprintf(stderr, "[ITRACE] s=%u f=%d st=%02X>%02X ph=%u stun=%u rh=%u bear=%X mv=%08X got=%08X ch=%u"
                        " open=%u att=%d bc=%u sticks=%u offwall=%u dz=%u\n", S.step, i,
                        s_trace_state[i] & 0xFFu, o->state, o->phase, o->stun, o->rehit, o->bearing, o->move,
                        o->got_move, o->character, S.v[i].open, (int)S.v[i].attacker, S.v[i].bc, S.v[i].sticks,
                        S.v[i].offwall, S.v[i].dizzy_offwall);
            s_trace_state[i] = o->state; s_trace_phase[i] = o->phase; s_trace_stun[i] = o->stun;
        }
    if (found && S.det_n > before) log_detections(before);
    else if (found) fprintf(stderr, "[ILLEGAL-TECH] s=%u a detection past the first %u (%u in all)\n", S.step, IT_DET_MAX,
                            S.det_total);
}

void illegal_tech_state_save(uint8_t out[ILLEGAL_TECH_STATE_BYTES])
{
    memcpy(out, &S, sizeof(S));
}

void illegal_tech_state_load(const uint8_t in[ILLEGAL_TECH_STATE_BYTES])
{
    memcpy(&S, in, sizeof(S));
}

uint32_t illegal_tech_hash(void) { return it_hash(&S); }

void illegal_tech_set_reported(int ranked_set)
{
    int sw = illegal_tech_test_switch();
    s_reported = sw == 2 ? 0 : sw == 1 || sw == 3 ? 1 : ranked_set != 0;
}

int illegal_tech_reported(void) { return s_reported; }

unsigned illegal_tech_count(void) { return S.det_total; }

int illegal_tech_json(char *json, int size, int n)
{
    if (!s_reported) return n;
    if (S.det_total) {
        static uint32_t told_step = 0xFFFFFFFFu;
        if (told_step != S.step) {
            told_step = S.step;
            fprintf(stderr, "[ILLEGAL-TECH] reported with the match: %u detection(s)\n", S.det_total);
        }
    }
    return it_json(&S, json, size, n, 1);
}

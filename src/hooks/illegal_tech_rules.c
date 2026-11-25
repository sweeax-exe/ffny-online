/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Illegal-tech detection, the rules (illegal_tech_rules.h, docs/research/illegal-tech.md). Pure C. */
#include <stdio.h>
#include <string.h>
#include "illegal_tech_rules.h"

/* Fighters (the set-up's fighter byte: fighter.xml's IDs) */
#define CH_ICE_T    8u
#define CH_COMP     46u
#define CH_BLAZE1   56u
#define CH_BLAZE2   81u
#define CH_BLAZE3   82u

/* How long after leaving a wall (or after the last step it was seen dizzy since) the victim still counts as coming
 * off it, in fight steps (60 a second), unless it is down, free or on the wall again before. */
#define OFFWALL_STEPS 150u
/* Move words (+0xF50) from here up are grapples (0x274B a hold, 0x276A the wall grab, 0x2743 a reversal) and heavy
 * hits (0x2710, 0x2714); below are the strike strings (0x36..0x6C). */
#define IT_MOVE_SPECIAL 0x2700u
/* A block cancel: the attacker's next hit lands within this many steps of its block. */
#define BC_WINDOW     40u

static const char *const k_rule_id[IT_RULE_COUNT] = { "", "infinite", "wall_redaze", "wall_grab", "chain_stun",
                                                      "restick" };

const char *it_rule_id(uint32_t rule) { return rule < IT_RULE_COUNT ? k_rule_id[rule] : ""; }

int it_chain_stun_exempt(uint32_t ch)
{
    return ch == CH_BLAZE1 || ch == CH_BLAZE2 || ch == CH_BLAZE3 || ch == CH_COMP;
}

/* pinned against a wall, fence, rope or corner: the states a re-hit bumps +0x384 in (wall-stagger.md, Gate C);
 * live: 0x37 (a stagger into the cage or a wall, 0x11 > 0x37; a throw out of a hold, 0x36 > 0x37) */
int it_is_pinned(uint32_t s) { return s == 0x37u || s == 0x14u || s == 0x1Bu || s == 0x60u; }
/* on the wall: pinned, or grabbed there (live: the grabber 0x1E > 0x67 and the pinned victim 0x37 > 0x68; at 0x60
 * walls 0x1E > 0x61 and 0x60 > 0x62) */
int it_is_on_wall(uint32_t s) { return it_is_pinned(s) || s == 0x68u || s == 0x62u; }
/* on the ground: knocked down, hit while down, getting up */
int it_is_down(uint32_t s) { return s == 0x15u || s == 0x0Fu || s == 0x1Au || s == 0x19u; }
/* the victim of a grab (live): caught 0x20, held 0x35, thrown 0x22, picked up off the ground 0x33, grabbed on the
 * wall 0x68 */
int it_is_held(uint32_t s)
{
    return s == 0x20u || s == 0x35u || s == 0x22u || s == 0x33u || s == 0x68u || s == 0x62u;
}
/* the grabber (live): the grab 0x1E, caught 0x1F, holding 0x34, throwing 0x21, picking up 0x32, on the wall 0x67 */
int it_is_holding(uint32_t s)
{
    return s == IT_ST_GRAB || s == 0x1Fu || s == 0x34u || s == 0x21u || s == 0x32u || s == 0x67u || s == 0x61u;
}
/* the long dizzy: the stun bit (+0x927 bit 2), whatever the state (live: a pinned victim's third strike, 0x37 > 0x10
 * with the bit set; it clears on the ground, 0x15 phase 11; state 0x16 is its standing form) */
static int is_dizzy(const ItObs *o) { return o->stun != 0; }
static int is_blocking(uint32_t s) { return s == IT_ST_BLOCK || s == IT_ST_BLOCKED; }
/* a standing hit reaction: flinch (0x0E), staggers (0x10, 0x11, 0x12) */
static int is_reeling(uint32_t s) { return s == 0x0Eu || (s >= 0x10u && s <= 0x12u); }
/* the fighter acts on its own: a strike (0x45..0x50: live, the strings and their variants), a heavy hit (0x6C..0x70),
 * a grab of its own, a grab reversal (live: the striker 0x45 > 0x30 while the grabber goes 0x1E > 0x31) */
static int is_acting(uint32_t s)
{
    return (s >= 0x45u && s <= 0x50u) || (s >= 0x6Cu && s <= 0x70u) || s == IT_ST_GRAB || s == 0x30u;
}

void it_reset(ItState *s)
{
    int i;
    memset(s, 0, sizeof(*s));
    s->version = IT_VERSION;
    for (i = 0; i < IT_SLOTS; i++) {
        s->v[i].attacker = 0xFFFFFFFFu;
        s->fighter[i] = 0xFFFFFFFFu;
    }
}

static int detect(ItState *s, uint32_t rule, int a, int v, const ItObs obs[IT_SLOTS], uint32_t move, uint32_t count)
{
    ItVictim *c = &s->v[v];
    ItDet *d;
    if (c->reported & (1u << rule)) return 0;     /* once per rule and combo */
    c->reported |= 1u << rule;
    s->det_total++;
    if (s->det_n >= IT_DET_MAX) return 1;
    d = &s->det[s->det_n++];
    d->step = s->step;
    d->rule = rule;
    d->attacker = (uint32_t)a;
    d->victim = (uint32_t)v;
    d->a_char = obs[a].character;
    d->v_char = obs[v].character;
    d->move = move;
    d->count = count;
    return 1;
}

static void close_combo(ItState *s, int v)
{
    ItVictim *c = &s->v[v];
    if (!c->open) return;
    c->open = 0;
    if (c->attacker < IT_SLOTS) s->f[c->attacker].blocked_mid = 0;
    c->offwall = c->dizzy_offwall = 0;
}

static void open_combo(ItState *s, int v, int a, uint32_t before)
{
    ItVictim *c = &s->v[v];
    uint32_t idx = c->idx + 1;
    if (c->open) close_combo(s, v);
    memset(c, 0, (size_t)((char *)&c->pending - (char *)c));     /* the combo's fields, not the pending hit */
    c->open = 1;
    c->attacker = (uint32_t)a;
    c->idx = idx;
    c->sticks = it_is_pinned(before) ? 1u : 0u;
    s->f[a].blocked_mid = 0;
}

void it_hit(ItState *s, int a, int v, uint32_t flags, const ItObs *ao, const ItObs *vo)
{
    ItVictim *c;
    if (a < 0 || v < 0 || a >= IT_SLOTS || v >= IT_SLOTS || a == v) return;
    c = &s->v[v];
    /* a second hit on v before the next step replaces the first: both have the same "after" state */
    c->pending = 1;
    c->p_attacker = (uint32_t)a;
    c->p_move = ao->move;
    c->p_flags = flags;
    c->p_before = vo->state;
    c->p_step = s->step;
}

/* The pending hit on v takes its "after" state (v as this step begins) and goes into the combo. Returns how many
 * detections it added. */
static int flush(ItState *s, int v, const ItObs obs[IT_SLOTS])
{
    ItVictim *c = &s->v[v];
    const ItObs *o = &obs[v];
    int a = (int)c->p_attacker, found = 0;
    uint32_t after = o->state;
    if (!c->pending) return 0;
    c->pending = 0;
    if (a < 0 || a >= IT_SLOTS || !obs[a].present) return 0;
    if (is_blocking(c->p_before) || after == IT_ST_BLOCKED) {     /* blocked: no combo */
        close_combo(s, v);
        return 0;
    }
    if (!c->open || c->attacker != (uint32_t)a) open_combo(s, v, a, c->p_before);
    c->hits++;
    c->last_move = c->cause_move = c->p_move;
    c->last_hit_step = c->cause_step = c->p_step;
    if (s->f[a].blocked_mid) {      /* rule 1: the attacker blocked mid-combo and hit again, at once */
        s->f[a].blocked_mid = 0;
        /* Ice-T's back submission loop is allowed (The Brick Layer, 2026-11-07): his cancels on a victim's back
         * do not count */
        if (c->p_step - s->f[a].block_step <= BC_WINDOW &&
            !(obs[a].character == CH_ICE_T && (o->bearing & IT_BEARING_BACK)))
            c->bc++;
    }
    if (c->bc >= 2u - (s->sensitive ? 1u : 0u)) found += detect(s, IT_RULE_INFINITE, a, v, obs, c->p_move, c->bc);
    /* rule 2: the victim came off the wall in the long dizzy, and the hit keeps it standing in the dizzy (a hit that
     * puts it down, or back on the wall, is not one) */
    if (c->offwall && c->dizzy_offwall && !is_blocking(c->p_before) && !it_is_on_wall(c->p_before) && is_dizzy(o) &&
        !it_is_down(after) && !it_is_on_wall(after))
        found += detect(s, IT_RULE_WALL_REDAZE, a, v, obs, c->p_move, 1);
    return found;
}

int it_step(ItState *s, const ItObs obs[IT_SLOTS])
{
    int i, found = 0;
    s->step++;
    for (i = 0; i < IT_SLOTS; i++)
        if (obs[i].present) found += flush(s, i, obs);
    for (i = 0; i < IT_SLOTS; i++) {
        ItFighterMem *f = &s->f[i];
        ItVictim *c = &s->v[i];
        const ItObs *o = &obs[i];
        uint32_t st = o->state, was;
        int j;
        if (!o->present) { f->seen = 0; c->open = 0; c->pending = 0; continue; }
        if (!f->seen) { f->seen = 1; f->state = st; f->phase = o->phase; f->stun = o->stun; continue; }
        was = f->state;
        /* the victim is grabbed: a combo opens on it (or goes on) with the grabber as the attacker */
        if (it_is_held(st) && !it_is_held(was)) {
            int holder = -1;
            for (j = 0; j < IT_SLOTS; j++)
                if (j != i && obs[j].present && it_is_holding(obs[j].state) &&
                    (holder < 0 || (c->open && c->attacker == (uint32_t)j)))
                    holder = j;
            if (holder >= 0) {
                /* rule 3: grabbed coming off the wall (a grab of a victim still on the wall, 0x37 > 0x68, is fine) */
                if (c->open && c->offwall && c->attacker == (uint32_t)holder && !it_is_on_wall(was))
                    found += detect(s, IT_RULE_WALL_GRAB, holder, i, obs, obs[holder].move, 1);
                if (!c->open || c->attacker != (uint32_t)holder) open_combo(s, i, holder, was);
            }
        }
        if (c->open) {
            int a = (int)c->attacker;
            /* the attacker's grapple moves touch the victim too (a throw, a slam): the cause of what follows */
            if (a >= 0 && a < IT_SLOTS && obs[a].present && it_is_holding(obs[a].state) && obs[a].move) {
                c->cause_move = obs[a].move;
                c->cause_step = s->step;
            }
            /* walls: a stick, a restick (rule 5), coming off */
            if (it_is_pinned(st) && !it_is_on_wall(was)) {
                c->sticks++;
                c->offwall = c->dizzy_offwall = 0;
                if (c->sticks >= 3u - (s->sensitive ? 1u : 0u))
                    found += detect(s, IT_RULE_RESTICK, a, i, obs, c->cause_move, c->sticks - 1);
            } else if (!it_is_on_wall(st) && it_is_on_wall(was) && !it_is_down(st)) {
                c->offwall = 1;
                c->offwall_step = s->step;
                c->dizzy_offwall = 0;
            }
            if (c->offwall && is_dizzy(o)) {
                c->dizzy_offwall = 1;               /* coming off the wall in the dizzy: fine by itself */
                c->offwall_step = s->step;          /* and still coming off it while dizzy */
            }
            /* the long dizzy begins (the stun bit rises) */
            if (is_dizzy(o) && !f->stun && !c->offwall) {
                if (c->cause_move >= IT_MOVE_SPECIAL) {
                    /* rule 4: stunned by a move that stunned it before in this combo (a grapple or a heavy hit:
                     * the strike strings' moves, 0x36..0x6C, are no chain stuns) */
                    uint32_t mv = c->cause_move, k;
                    int exempt = it_chain_stun_exempt(obs[a].character);
                    for (k = 0; k < c->stun_n && c->stun_moves[k] != mv; k++) {}
                    if (k < c->stun_n) {
                        c->stun_uses[k]++;
                        if (!exempt) found += detect(s, IT_RULE_CHAIN_STUN, a, i, obs, mv, c->stun_uses[k]);
                    } else if (c->stun_n < IT_STUN_MAX) {
                        c->stun_moves[c->stun_n] = mv;
                        c->stun_uses[c->stun_n++] = 1;
                        if (s->sensitive && !exempt) found += detect(s, IT_RULE_CHAIN_STUN, a, i, obs, mv, 1);
                    }
                }
            }
            if (c->offwall) {
                if (it_is_down(st) || s->step - c->offwall_step > OFFWALL_STEPS) c->offwall = c->dizzy_offwall = 0;
            }
            /* the combo ends when the victim is back on its own */
            if (!c->pending && (st == IT_ST_FREE || is_blocking(st) || st == IT_ST_KO || is_acting(st)))
                close_combo(s, i);
        }
        /* attacker side: block while its combo is open and the victim still reels standing from the last hit (a
         * block cancel when it hits again within BC_WINDOW steps); a block while the victim is down or getting up
         * is waiting, not a cancel */
        if (st != was && st == IT_ST_BLOCK) {
            for (j = 0; j < IT_SLOTS; j++)
                if (j != i && s->v[j].open && s->v[j].attacker == (uint32_t)i && obs[j].present &&
                    is_reeling(obs[j].state)) {
                    f->blocked_mid = 1;
                    f->block_step = s->step;
                }
        }
        f->state = st; f->phase = o->phase; f->stun = o->stun;
    }
    return found;
}

uint32_t it_hash(const ItState *s)
{
    const uint32_t *w = (const uint32_t *)s;
    uint32_t h = 2166136261u;
    size_t i, n = sizeof(*s) / 4;
    for (i = 0; i < n; i++) h = (h ^ w[i]) * 16777619u;
    return h ? h : 1u;
}

int it_json(const ItState *s, char *json, int size, int n, int always)
{
    uint32_t i;
    int m;
    if (n < 0 || n >= size) return n;
    if (!s->det_total && !always) return n;
    m = snprintf(json + n, (size_t)(size - n), ",\"it\":{\"v\":%u,\"n\":%u,\"d\":[", IT_VERSION, s->det_total);
    if (m < 0 || n + m >= size - 2) return n;
    m += n;
    for (i = 0; i < s->det_n; i++) {
        const ItDet *d = &s->det[i];
        int k = snprintf(json + m, (size_t)(size - m), "%s{\"s\":%u,\"r\":\"%s\",\"a\":%u,\"v\":%u,\"af\":%d,\"vf\":%d,"
                         "\"mv\":\"%08X\",\"c\":%u}", i ? "," : "", d->step, it_rule_id(d->rule), d->attacker, d->victim,
                         (int)d->a_char, (int)d->v_char, d->move, d->count);
        if (k < 0 || m + k >= size - 3) break;
        m += k;
    }
    if (m + 3 > size) return n;
    json[m++] = ']';
    json[m++] = '}';
    json[m] = 0;
    return m;
}

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Illegal-tech detection, the rules (docs/research/illegal-tech.md). Pure C: no guest memory, no Windows, no
 * floating-point state of its own, so tests/unit/test_illegal_tech_native.py builds it alone and feeds it
 * fighter states step by step.
 *
 * The community's tournament rules (ranked and tournaments only; casual stays free):
 *   1 infinite        a second block cancel in one combo (a front loop may be used once)
 *   2 wall redaze     hitting a victim coming off the wall back into the long dizzy
 *   3 wall grab       grabbing a victim coming off the wall
 *   4 chain stun      the same chain stun twice in one combo (the victim never back to neutral);
 *                     exceptions Blaze and Comp
 *   5 two resticks    a second wall restick in one combo (the first stick and one restick are fine)
 *
 * The detector sees what both PCs of a match see: the fighter records as each fight step begins and each hit
 * as it lands (src/hooks/illegal_tech.c reads them). Everything it keeps is in ItState, a plain block of 32-bit
 * words: the rollback save states copy it, the lockstep checksum hashes it, and a step run again from a load
 * comes to the same verdict at the same step. It decides nothing about the match: it only lists detections.
 */
#ifndef DEFJAM_ILLEGAL_TECH_RULES_H
#define DEFJAM_ILLEGAL_TECH_RULES_H

#include <stdint.h>

#define IT_SLOTS        4
#define IT_DET_MAX      16      /* detections kept per match (the total counts on) */
#define IT_STUN_MAX     8       /* chain stuns remembered per combo */
#define IT_VERSION      1u

enum {
    IT_RULE_INFINITE = 1,
    IT_RULE_WALL_REDAZE = 2,
    IT_RULE_WALL_GRAB = 3,
    IT_RULE_CHAIN_STUN = 4,
    IT_RULE_RESTICK = 5,
    IT_RULE_COUNT
};

/* Fighter states (fighter +0x30). Live: the [ITRACE] bot fights of 2026-11-25 (docs/research/illegal-tech.md);
 * static: docs/research/wall-stagger.md and the state table (sub_001B54D0) */
#define IT_ST_FREE      0x01u
#define IT_ST_BLOCK     0x02u
#define IT_ST_BLOCKED   0x03u
/* The long dizzy is the stun bit +0x927 bit 2, set by the damage routine sub_001A5DD0 once the stun meter +0x374
 * reaches +0x378; state 0x16 (handler sub_001B50D0, which clears the bit and the meter) is its standing form, and a
 * stunned fighter also staggers (0x10) or falls (0x15) with the bit set. */
#define IT_ST_DIZZY     0x16u
#define IT_ST_KO        0x18u
#define IT_ST_GRAB      0x1Eu   /* the attacker's grab start */
#define IT_BEARING_BACK 0xC0u   /* +0x918: the hit came from behind */

/* One fighter, read as a fight step begins (or as a hit lands). */
typedef struct ItObs {
    uint32_t present;       /* the slot fights (its record names its own slot) */
    uint32_t state;         /* +0x30 */
    uint32_t phase;         /* +0x34 */
    uint32_t stun;          /* +0x927 bit 2: stunned (the long dizzy) */
    uint32_t character;     /* the slot's fighter (ItState.fighter: the set-up's fighter byte, fighter.xml's ID) */
    uint32_t rehit;         /* +0x384: pinned re-hit counter */
    uint32_t move;          /* +0xF50: the fighter's current move word (strikes 0x36..0x6C, grapples 0x27xx) */
    uint32_t got_move;      /* +0xFB8: the move word of the last hit taken (copied by the hit) */
    uint32_t bearing;       /* +0x918: the last hit's relative bearing bits (sub_001C5E90: 8 front, 0x20 and 0x10
                             * further round, 0x40 / 0x80 from behind, by the side) */
} ItObs;

typedef struct ItDet {
    uint32_t step, rule, attacker, victim;
    uint32_t a_char, v_char;
    uint32_t move;          /* the move that broke the rule (the cancel's hit, the redaze hit, the grab, the stun) */
    uint32_t count;         /* block cancels, resticks, the chain stun's uses so far */
} ItDet;

typedef struct ItVictim {
    uint32_t open;          /* a combo is running on this fighter */
    uint32_t attacker;      /* its attacker's slot */
    uint32_t idx;           /* combos taken so far (the open one included) */
    uint32_t hits, bc, sticks;
    uint32_t offwall;       /* came off a wall in this combo and not yet down, free or pinned again */
    uint32_t offwall_step;  /* when, or the last step it was seen dizzy since */
    uint32_t dizzy_offwall; /* in the long dizzy since it came off the wall */
    uint32_t cause_move;    /* the attacker's last move that touched the victim: a hit's, or a grapple's */
    uint32_t cause_step;
    uint32_t stun_n;
    uint32_t stun_moves[IT_STUN_MAX];
    uint32_t stun_uses[IT_STUN_MAX];
    uint32_t reported;      /* bit per rule already listed for this combo */
    uint32_t last_move, last_hit_step;
    uint32_t pending;       /* a hit waiting for its "after" state (the next step) */
    uint32_t p_attacker, p_move, p_flags, p_before, p_step;
} ItVictim;

typedef struct ItFighterMem {
    uint32_t seen, state, phase, stun;
    uint32_t blocked_mid;   /* as an attacker: blocked while its combo was open (a block cancel on its next hit) */
    uint32_t block_step;    /* when */
} ItFighterMem;

typedef struct ItState {
    uint32_t version;
    uint32_t step;          /* fight steps since the match was set up */
    uint32_t det_n;         /* detections kept (at most IT_DET_MAX) */
    uint32_t det_total;     /* detections in all */
    uint32_t fighter[IT_SLOTS];     /* each slot's fighter (the set-up's fighter byte; 0xFFFFFFFF none) */
    uint32_t sensitive;     /* tests (RECOMP_ILLEGAL_TECH=sensitive on every PC): the first block cancel, any wall stick and
                             * any stun already count, so ordinary bot fights produce detections to compare */
    ItVictim v[IT_SLOTS];
    ItFighterMem f[IT_SLOTS];
    ItDet det[IT_DET_MAX];
} ItState;

/* A match is set up. */
void it_reset(ItState *s);
/* A hit lands (the entry of the per-hit function): attacker and victim slots, the hit-class flags, both fighters
 * as they are before the hit's body runs. */
void it_hit(ItState *s, int attacker, int victim, uint32_t flags, const ItObs *a, const ItObs *v);
/* A fight step begins: every slot as it is now. Returns how many detections this step added. */
int it_step(ItState *s, const ItObs obs[IT_SLOTS]);
/* The state's hash for the lockstep checksum (FNV-1a over its words; never 0). */
uint32_t it_hash(const ItState *s);

/* The rules' names (1..5), short and stable: they go into the match reports. */
const char *it_rule_id(uint32_t rule);
/* Exceptions by fighter (+0x2C character index): Ice-T's back loop, Blaze's and Comp's chain stuns. */
int it_chain_stun_exempt(uint32_t character);

/* State classes (exposed for the tests and the trace) */
int it_is_pinned(uint32_t state);       /* against a wall, fence, rope or corner */
int it_is_on_wall(uint32_t state);      /* pinned, or grabbed there (0x68) */
int it_is_down(uint32_t state);
int it_is_held(uint32_t state);         /* the victim of a grab */
int it_is_holding(uint32_t state);      /* the grabber */

/* The detections as JSON: ,"it":{"v":1,"n":total,"d":[...]} appended to json at n; returns the new length
 * (n unchanged when there is no room or nothing to say and `always` is 0). */
int it_json(const ItState *s, char *json, int size, int n, int always);

#endif

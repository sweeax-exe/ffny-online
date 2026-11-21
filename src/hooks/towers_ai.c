/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Towers: the CPU past Hard, the game side. See towers_ai.h and community/tower-ai-study.md.
 *
 * Where the words are (the study's section 1, read again in sub_001B1CA0): the AI manager at 0x3773A4 holds one
 * block of 0x890 bytes per fighter slot from 0x3773A8: four tendency sets of 24 ints (+0x60 each), and the skill
 * (AI_difficulty) at +0x7CC. The brain reads both at decision time (the per-action weight takes the skill from
 * +0x7CC and the defence proficiency from the set in use), so a word written before the step's AI pass is the one it
 * decides with. The loader zeroes and refills the block at each match start; the first step that finds a loaded
 * block takes the fighter's own values from it, and every step after writes the floor's values again (a restart of
 * the fight reloads them).
 *
 * The adaptive repeat counter (step 3) reads only the climber's fighter record on the step his state changed (the
 * attack state, the move word at +0xF50): what the CPU's brain sees on that step anyway, never the pad or its queue.
 * RECOMP_TEST_TOWERS_ADAPT=0 turns it off.
 *
 * Gating: towers.c calls in only during a tower fight offline; the CPU slot must be flagged as one (0x3B9088) and the
 * climber's must not, or nothing is written. Tests: RECOMP_TEST_TOWERS_AI=off (only the numbers, the game's own
 * data), def=<1..5> (the defence proficiencies' target), skill=<1..100> (the skill, every floor from 7). */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "towers_ai.h"
#include "towers_ai_rules.h"
#include "towers_rules.h"

#define AI_SLOT_BLOCK(s)    (0x003773A8u + 0x890u * (uint32_t)(s))
#define AI_TEND(s, set, k)  (AI_SLOT_BLOCK(s) + 0x60u * (uint32_t)(set) + 4u * (uint32_t)(k))
#define AI_SKILL(s)         (AI_SLOT_BLOCK(s) + 0x7CCu)
#define AI_SLOT_PTR(s)      (0x003C0494u + 4u * (uint32_t)(s))     /* what sub_001B1CA0 takes the block from */
#define G_CPU_FLAG(s)       (0x003B9088u + 4u * (uint32_t)(s))
#define ACTORS              0x003B92E0u
#define STRIDE              0x12E8u
#define F_SLOT              0x28u
#define F_STATE             0x30u
#define F_MOVE              0xF50u      /* the attacker's move word (combo-log.md) */
#define BRAIN(s)            (0x003BF990u + 0xB8u * (uint32_t)(s))
#define B_HOLD              0x34u       /* the output held while > 0 */
#define B_THINK             0x38u       /* the think cooldown */
/* Build 106: everything below is per CPU slot (one context; two in the team towers), A the one being worked on. */
#define CPU                 (A->cpu)
#define ME                  (A->me)

/* the fight's numbers */
#define ATTACK_WINDOW 40u
struct AiStats {
    unsigned attacks, landed, blocked, countered, cpu_attacks;
    unsigned hits_on_cpu, hits_on_me;
    float dmg_on_cpu, dmg_on_me;
    unsigned cpu_changes, cpu_longest, cpu_run;
    uint32_t me_state, cpu_state;
    /* the climber's attack under way: since step `at`, until the window or the next one */
    unsigned at;
    int open, saw_block, cpu_hurt, me_hurt;
};

typedef struct AiCtx {
    int cpu, me;                        /* the CPU's slot and the human it reads (its primary opponent) */
    TowerAiFloor f;
    int tower, floor, on, loaded, mode_off, skill_test;
    int own_skill, skill;
    int own[TOWER_AI_SETS][TOWER_AI_TENDENCIES], want[TOWER_AI_SETS][TOWER_AI_TENDENCIES];
    unsigned writes;
    int hold_prev[2], hold_seen;
    unsigned hold_cuts;
    TowerAiAdapt adapt;
    int adapt_on, boost;                /* the counter runs; the climber's attack under way is a read one */
    unsigned reads, boost_writes, read_cuts;
    struct AiStats s;
    unsigned now;
} AiCtx;
static AiCtx g_ctx[2], *A = &g_ctx[0];
static int g_nctx = 1;
#define g_f (A->f)
#define g_tower (A->tower)
#define g_floor (A->floor)
#define g_on (A->on)
#define g_loaded (A->loaded)
#define g_mode_off (A->mode_off)
#define g_skill_test (A->skill_test)
#define g_own_skill (A->own_skill)
#define g_skill (A->skill)
#define g_own (A->own)
#define g_want (A->want)
#define g_writes (A->writes)
#define g_hold_prev (A->hold_prev)
#define g_hold_seen (A->hold_seen)
#define g_hold_cuts (A->hold_cuts)
#define g_adapt (A->adapt)
#define g_adapt_on (A->adapt_on)
#define g_boost (A->boost)
#define g_reads (A->reads)
#define g_boost_writes (A->boost_writes)
#define g_read_cuts (A->read_cuts)
#define g_s (A->s)
#define g_now (A->now)

static int attack_state(uint32_t st)
{
    return (st >= 0x45u && st <= 0x4Bu) || st == 0x6Cu || st == 0x6Eu || st == 0x1Eu;
}

static void close_attack(void)
{
    g_boost = 0;
    if (!g_s.open) return;
    if (g_s.cpu_hurt) g_s.landed++;
    else if (g_s.me_hurt) g_s.countered++;
    else if (g_s.saw_block) g_s.blocked++;
    g_s.open = 0;
}

static void ai_begin_one(int tower, int floor, int floors)
{
    const char *test = getenv("RECOMP_TEST_TOWERS_AI");
    g_tower = tower;
    g_floor = floor;
    towers_ai_floor(tower, floor, floors, &g_f);
    g_mode_off = test && !strcmp(test, "off");
    g_skill_test = 0;
    if (test && !strncmp(test, "def=", 4)) g_f.defence = atoi(test + 4) < 1 ? 1 : atoi(test + 4) > 5 ? 5 : atoi(test + 4);
    if (test && !strncmp(test, "skill=", 6)) g_skill_test = atoi(test + 6);
    {   /* tests: RECOMP_TEST_TOWERS_HOLD=<cap> (0: none) for every floor from 7 */
        const char *h = getenv("RECOMP_TEST_TOWERS_HOLD");
        if (h && *h && g_f.on) g_f.hold_cap = atoi(h);
    }
    g_hold_seen = 0;
    g_hold_cuts = 0;
    g_on = g_f.on && !g_mode_off;
    towers_ai_adapt_reset(&g_adapt);
    g_adapt_on = g_on && towers_ai_adapt_on(&g_f, floor) && !(getenv("RECOMP_TEST_TOWERS_ADAPT") &&
                                                               *getenv("RECOMP_TEST_TOWERS_ADAPT") == '0');
    g_boost = 0;
    g_reads = g_boost_writes = g_read_cuts = 0;
    g_loaded = 0;
    g_writes = 0;
    memset(&g_s, 0, sizeof g_s);
    g_s.me_state = g_s.cpu_state = 0xFFFFFFFFu;
}

/* The CPU slot's block is loaded: a skill in 1..100 and every tendency of every set in 1..5. */
static int block_loaded(void)
{
    int set, k, sk = (int)MEM32(AI_SKILL(CPU));
    if (sk < 1 || sk > 100) return 0;
    for (set = 0; set < TOWER_AI_SETS; set++)
        for (k = 0; k < TOWER_AI_TENDENCIES; k++) {
            int v = (int)MEM32(AI_TEND(CPU, set, k));
            if (v < 1 || v > 5) return 0;
        }
    return 1;
}

static void take_own(void)
{
    int set, k;
    char line[200];
    int n = 0;
    g_own_skill = (int)MEM32(AI_SKILL(CPU));
    for (set = 0; set < TOWER_AI_SETS; set++)
        for (k = 0; k < TOWER_AI_TENDENCIES; k++) {
            g_own[set][k] = (int)MEM32(AI_TEND(CPU, set, k));
            g_want[set][k] = towers_ai_tendency(&g_f, k, g_own[set][k]);
        }
    g_skill = towers_ai_skill(&g_f, g_own_skill);
    if (g_skill_test >= 1 && g_skill_test <= 100 && g_f.on) g_skill = g_skill_test;
    for (k = 0; k < TOWER_AI_TENDENCIES && n < (int)sizeof line - 8; k++)
        n += snprintf(line + n, sizeof line - (size_t)n, "%s%d>%d", k ? " " : "", g_own[0][k], g_want[0][k]);
    fprintf(stderr, "[TOWERS-AI] floor %d: CPU %d AI block at %08X (brain's pointer %08X): skill %d -> %d, ramp %.2f%s, "
            "defence target %d%s; set 0 tendencies %s\n", g_floor, CPU, AI_SLOT_BLOCK(CPU), MEM32(AI_SLOT_PTR(CPU)), g_own_skill,
            g_on ? g_skill : g_own_skill, g_f.t, g_f.boss ? " (boss)" : "", g_f.defence,
            g_mode_off ? ", test: the game's own data" : !g_f.on ? ", the game's own data (below floor 7)" : "", line);
}

/* The floor's values; while a read attack is under way (step 3) the counter and defence words at their strongest
 * and the skill half way up to 100 on top. */
static void write_floor(void)
{
    int set, k, skill = g_boost ? towers_ai_adapt_skill(g_skill) : g_skill;
    if ((int)MEM32(AI_SKILL(CPU)) != skill) { MEM32(AI_SKILL(CPU)) = (uint32_t)skill; g_writes++; }
    for (set = 0; set < TOWER_AI_SETS; set++)
        for (k = 0; k < TOWER_AI_TENDENCIES; k++) {
            int v = g_want[set][k];
            if (g_boost && g_own[set][k] >= 1 && g_own[set][k] <= 5) {
                if (k == TAI_COUNTER_ATTEMPT || k == TAI_COUNTER_USE) v = 5;
                else if (k == TAI_GRAPPLE_DEFENCE || k == TAI_STRIKE_DEFENCE) v = g_f.defence;
            }
            if ((int)MEM32(AI_TEND(CPU, set, k)) != v) {
                MEM32(AI_TEND(CPU, set, k)) = (uint32_t)v;
                g_writes++;
                if (g_boost) g_boost_writes++;
            }
        }
}

/* The step's numbers; 1 when the climber started an attack on this step (its key in *key: the attack state and the
 * move word the game set for it, read from his record, as the CPU sees it). */
static int count_step(int decided, uint32_t *key)
{
    uint32_t me = ACTORS + STRIDE * ME, cpu = ACTORS + STRIDE * CPU;
    uint32_t ms = MEM32(me + F_STATE), cs = MEM32(cpu + F_STATE);
    int began = 0;
    if (decided) return 0;
    if (cs != g_s.cpu_state) {
        if (g_s.cpu_state != 0xFFFFFFFFu) g_s.cpu_changes++;
        if (attack_state(cs) && !attack_state(g_s.cpu_state)) g_s.cpu_attacks++;
        g_s.cpu_state = cs;
        g_s.cpu_run = 0;
    }
    if (++g_s.cpu_run > g_s.cpu_longest) g_s.cpu_longest = g_s.cpu_run;
    if (g_s.open && g_now - g_s.at > ATTACK_WINDOW) close_attack();
    if (ms != g_s.me_state) {
        if (attack_state(ms)) {         /* a new attack (the next hit of a string too) */
            close_attack();
            began = 1;
            *key = ms << 16 | (MEM32(me + F_MOVE) & 0xFFFFu);
            g_s.attacks++;
            g_s.open = 1;
            g_s.at = g_now;
            g_s.saw_block = g_s.cpu_hurt = g_s.me_hurt = 0;
        }
        g_s.me_state = ms;
    }
    if (g_boost && !attack_state(ms)) g_boost = 0;      /* the read attack is over */
    if (g_s.open && (cs == 2u || cs == 3u)) g_s.saw_block = 1;
    return began;
}

/* Step 2: the brain's fresh long holds cut to the floor's cap (towers_ai_hold), before the step's AI pass. */
static void cut_holds(void)
{
    static const uint32_t off[2] = { B_HOLD, B_THINK };
    int i;
    for (i = 0; i < 2; i++) {
        int now = (int)MEM32(BRAIN(CPU) + off[i]), keep;
        if (now < 0 || now > 10000) { g_hold_prev[i] = now; continue; }      /* not a countdown: leave it */
        keep = g_hold_seen ? towers_ai_hold(g_hold_prev[i], now, g_f.hold_cap) : now;
        if (keep != now) {
            MEM32(BRAIN(CPU) + off[i]) = (uint32_t)keep;
            if (++g_hold_cuts <= 3)
                fprintf(stderr, "[TOWERS-AI] floor %d: the CPU's %s of %d steps cut to %d\n", g_floor, i ? "think wait" : "hold",
                        now, keep);
        }
        g_hold_prev[i] = keep;
    }
    g_hold_seen = 1;
}

/* A read attack starts (step 3): the CPU's output hold and think wait, if longer, are cut to TOWER_AI_READ_HOLD so it
 * answers the attack it has seen too often within a few steps (at the top floors the tendencies are at their
 * strongest already, so this is what the read adds there). Never below TOWER_AI_READ_HOLD steps: no frame-perfect
 * answer (the study's "broken" list), and only for read attacks. */
static void read_cut(void)
{
    static const uint32_t off[2] = { B_HOLD, B_THINK };
    int i, cut = 0;
    for (i = 0; i < 2; i++) {
        int now = (int)MEM32(BRAIN(CPU) + off[i]);
        if (now > TOWER_AI_READ_HOLD && now <= 10000) {
            MEM32(BRAIN(CPU) + off[i]) = (uint32_t)TOWER_AI_READ_HOLD;
            g_hold_prev[i] = TOWER_AI_READ_HOLD;
            cut = 1;
        }
    }
    g_read_cuts += (unsigned)cut;
}

static void ai_step_one(unsigned step, int decided)
{
    uint32_t key = 0;
    int began;
    g_now = step;
    if (MEM32(ACTORS + STRIDE * CPU + F_SLOT) == 0xFFFFFFFFu) return;
    began = count_step(decided, &key);
    if (!g_loaded) {
        if (!block_loaded()) return;
        g_loaded = 1;
        take_own();
    }
    /* the CPU slot must be a CPU, the climber's not (online rooms never get here: towers.c) */
    if (!g_on || !MEM32(G_CPU_FLAG(CPU)) || MEM32(G_CPU_FLAG(ME))) return;
    if (began && g_adapt_on && towers_ai_adapt_note(&g_adapt, key, step)) {
        if (++g_reads <= 3)
            fprintf(stderr, "[TOWERS-AI] floor %d: the climber's attack %08X is read (step %u)\n", g_floor, key, step);
        g_boost = 1;
        read_cut();
    }
    write_floor();
    if (g_f.hold_cap > 0 && !decided) cut_holds();
}

static void ai_health_one(uint32_t record, float delta)
{
    if (delta >= 0.0f) return;
    if (record == ACTORS + STRIDE * CPU) {
        g_s.hits_on_cpu++;
        g_s.dmg_on_cpu -= delta;
        if (g_s.open) g_s.cpu_hurt = 1;
    } else if (record == ACTORS + STRIDE * ME) {
        g_s.hits_on_me++;
        g_s.dmg_on_me -= delta;
        if (g_s.open && !g_s.cpu_hurt) g_s.me_hurt = 1;
    }
}

static void ai_end_one(int winner, unsigned steps)
{
    close_attack();
    fprintf(stderr, "[TOWERS-AI] fight {\"tower\":%d,\"floor\":%d,\"boss\":%d,\"mode\":\"%s\",\"skill\":%d,\"own_skill\":%d,"
            "\"defence\":%d,\"winner\":%d,\"steps\":%u,\"attacks\":%u,\"landed\":%u,\"blocked\":%u,\"countered\":%u,"
            "\"hits_on_cpu\":%u,\"dmg_on_cpu\":%.1f,\"hits_on_me\":%u,\"dmg_on_me\":%.1f,\"cpu_attacks\":%u,"
            "\"cpu_changes_per_s\":%.2f,\"cpu_longest\":%u,\"writes\":%u,\"hold_cap\":%d,\"hold_cuts\":%u,\"adapt\":%d,\"reads\":%u,\"read_writes\":%u,\"read_cuts\":%u}\n",
            g_tower, g_floor, g_f.boss, g_mode_off ? "off" : g_on ? "ramp" : "stock", g_on ? g_skill : g_own_skill,
            g_own_skill, g_f.defence, winner, steps, g_s.attacks, g_s.landed, g_s.blocked, g_s.countered,
            g_s.hits_on_cpu, (double)g_s.dmg_on_cpu, g_s.hits_on_me, (double)g_s.dmg_on_me, g_s.cpu_attacks,
            steps ? (double)g_s.cpu_changes * 60.0 / (double)steps : 0.0, g_s.cpu_longest, g_writes, g_on ? g_f.hold_cap : 0,
            g_hold_cuts, g_adapt_on, g_reads, g_boost_writes, g_read_cuts);
}

/* ---------------------------------------------------------------- the public side: each CPU slot's context */

void towers_ai_begin(int tower, int floor, int floors)
{
    g_nctx = 1;
    g_ctx[0].cpu = 1;
    g_ctx[0].me = 0;
    A = &g_ctx[0];
    ai_begin_one(tower, floor, floors);
}

void towers_ai_begin_team(int tower, int floor, int floors)
{
    int i;
    g_nctx = 2;
    g_ctx[0].cpu = 2;                   /* team: slot s's primary opponent is s ^ 3 */
    g_ctx[0].me = 1;
    g_ctx[1].cpu = 3;
    g_ctx[1].me = 0;
    for (i = 0; i < 2; i++) { A = &g_ctx[i]; ai_begin_one(tower, floor, floors); }
}

void towers_ai_step(unsigned step, int decided)
{
    int i;
    for (i = 0; i < g_nctx; i++) { A = &g_ctx[i]; ai_step_one(step, decided); }
}

void towers_ai_health(uint32_t record, float delta)
{
    int i;
    for (i = 0; i < g_nctx; i++) { A = &g_ctx[i]; ai_health_one(record, delta); }
}

void towers_ai_end(int winner, unsigned steps)
{
    int i;
    for (i = 0; i < g_nctx; i++) { A = &g_ctx[i]; ai_end_one(winner, steps); }
}

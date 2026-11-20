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
#define CPU                 1
#define ME                  0

static TowerAiFloor g_f;
static int g_tower, g_floor, g_on, g_loaded, g_mode_off, g_skill_test;
static int g_own_skill, g_skill;
static int g_own[TOWER_AI_SETS][TOWER_AI_TENDENCIES], g_want[TOWER_AI_SETS][TOWER_AI_TENDENCIES];
static unsigned g_writes;

/* the fight's numbers */
#define ATTACK_WINDOW 40u
static struct {
    unsigned attacks, landed, blocked, countered, cpu_attacks;
    unsigned hits_on_cpu, hits_on_me;
    float dmg_on_cpu, dmg_on_me;
    unsigned cpu_changes, cpu_longest, cpu_run;
    uint32_t me_state, cpu_state;
    /* the climber's attack under way: since step `at`, until the window or the next one */
    unsigned at;
    int open, saw_block, cpu_hurt, me_hurt;
} g_s;
static unsigned g_now;

static int attack_state(uint32_t st)
{
    return (st >= 0x45u && st <= 0x4Bu) || st == 0x6Cu || st == 0x6Eu || st == 0x1Eu;
}

static void close_attack(void)
{
    if (!g_s.open) return;
    if (g_s.cpu_hurt) g_s.landed++;
    else if (g_s.me_hurt) g_s.countered++;
    else if (g_s.saw_block) g_s.blocked++;
    g_s.open = 0;
}

void towers_ai_begin(int tower, int floor, int floors)
{
    const char *test = getenv("RECOMP_TEST_TOWERS_AI");
    g_tower = tower;
    g_floor = floor;
    towers_ai_floor(tower, floor, floors, &g_f);
    g_mode_off = test && !strcmp(test, "off");
    g_skill_test = 0;
    if (test && !strncmp(test, "def=", 4)) g_f.defence = atoi(test + 4) < 1 ? 1 : atoi(test + 4) > 5 ? 5 : atoi(test + 4);
    if (test && !strncmp(test, "skill=", 6)) g_skill_test = atoi(test + 6);
    g_on = g_f.on && !g_mode_off;
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
    fprintf(stderr, "[TOWERS-AI] floor %d: CPU AI block at %08X (brain's pointer %08X): skill %d -> %d, ramp %.2f%s, "
            "defence target %d%s; set 0 tendencies %s\n", g_floor, AI_SLOT_BLOCK(CPU), MEM32(AI_SLOT_PTR(CPU)), g_own_skill,
            g_on ? g_skill : g_own_skill, g_f.t, g_f.boss ? " (boss)" : "", g_f.defence,
            g_mode_off ? ", test: the game's own data" : !g_f.on ? ", the game's own data (below floor 7)" : "", line);
}

static void write_floor(void)
{
    int set, k;
    if ((int)MEM32(AI_SKILL(CPU)) != g_skill) { MEM32(AI_SKILL(CPU)) = (uint32_t)g_skill; g_writes++; }
    for (set = 0; set < TOWER_AI_SETS; set++)
        for (k = 0; k < TOWER_AI_TENDENCIES; k++)
            if ((int)MEM32(AI_TEND(CPU, set, k)) != g_want[set][k]) {
                MEM32(AI_TEND(CPU, set, k)) = (uint32_t)g_want[set][k];
                g_writes++;
            }
}

static void count_step(int decided)
{
    uint32_t me = ACTORS + STRIDE * ME, cpu = ACTORS + STRIDE * CPU;
    uint32_t ms = MEM32(me + F_STATE), cs = MEM32(cpu + F_STATE);
    if (decided) return;
    if (cs != g_s.cpu_state) {
        if (g_s.cpu_state != 0xFFFFFFFFu) g_s.cpu_changes++;
        if (attack_state(cs) && !attack_state(g_s.cpu_state)) g_s.cpu_attacks++;
        g_s.cpu_state = cs;
        g_s.cpu_run = 0;
    }
    if (++g_s.cpu_run > g_s.cpu_longest) g_s.cpu_longest = g_s.cpu_run;
    if (g_s.open && g_now - g_s.at > ATTACK_WINDOW) close_attack();
    if (ms != g_s.me_state) {
        if (attack_state(ms) && (!attack_state(g_s.me_state) || ms != g_s.me_state)) {
            close_attack();
            g_s.attacks++;
            g_s.open = 1;
            g_s.at = g_now;
            g_s.saw_block = g_s.cpu_hurt = g_s.me_hurt = 0;
        }
        g_s.me_state = ms;
    }
    if (g_s.open && (cs == 2u || cs == 3u)) g_s.saw_block = 1;
}

void towers_ai_step(unsigned step, int decided)
{
    g_now = step;
    if (MEM32(ACTORS + STRIDE * CPU + F_SLOT) == 0xFFFFFFFFu) return;
    count_step(decided);
    if (!g_loaded) {
        if (!block_loaded()) return;
        g_loaded = 1;
        take_own();
    }
    /* the CPU slot must be a CPU, the climber's not (online rooms never get here: towers.c) */
    if (!g_on || !MEM32(G_CPU_FLAG(CPU)) || MEM32(G_CPU_FLAG(ME))) return;
    write_floor();
}

void towers_ai_health(uint32_t record, float delta)
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

void towers_ai_end(int winner, unsigned steps)
{
    close_attack();
    fprintf(stderr, "[TOWERS-AI] fight {\"tower\":%d,\"floor\":%d,\"boss\":%d,\"mode\":\"%s\",\"skill\":%d,\"own_skill\":%d,"
            "\"defence\":%d,\"winner\":%d,\"steps\":%u,\"attacks\":%u,\"landed\":%u,\"blocked\":%u,\"countered\":%u,"
            "\"hits_on_cpu\":%u,\"dmg_on_cpu\":%.1f,\"hits_on_me\":%u,\"dmg_on_me\":%.1f,\"cpu_attacks\":%u,"
            "\"cpu_changes_per_s\":%.2f,\"cpu_longest\":%u,\"writes\":%u}\n",
            g_tower, g_floor, g_f.boss, g_mode_off ? "off" : g_on ? "ramp" : "stock", g_on ? g_skill : g_own_skill,
            g_own_skill, g_f.defence, winner, steps, g_s.attacks, g_s.landed, g_s.blocked, g_s.countered,
            g_s.hits_on_cpu, (double)g_s.dmg_on_cpu, g_s.hits_on_me, (double)g_s.dmg_on_me, g_s.cpu_attacks,
            steps ? (double)g_s.cpu_changes * 60.0 / (double)steps : 0.0, g_s.cpu_longest, g_writes);
}

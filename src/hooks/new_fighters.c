/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* New fighters as extra outfits of a stock fighter's select cell. See new_fighters.h and
 * docs/research/new-fighters.md.
 *
 * The select screens (battle/chooseF2/3/4) build their grid from Game.ResetFighterList() and then one
 * Game.GetNextFighter() string per outfit, "CLOTH=<outfits of the cell>&FE_ID=<id>&LOCK=<1 unlocked>&U=..",
 * until "NO_RETURN". ResetFighterList (sub_00047BC0) lists the Character rows whose word 13 is 1, sorts them,
 * and groups Blaze 2/3, Sticky 2 and D-Mob 2/3 under their cells by hard-coded IDs. A new row (feID 100..127)
 * comes out as a cell of its own, locked (it is not in the profile's 80-entry fighter set), with the grid
 * thumbnail of frame feID, which is empty.
 *
 * So right after the game's ResetFighterList this reads the whole list once (the game's own strings, in its
 * order) and answers the screen's GetNextFighter calls from a copy in which each new fighter's string moved
 * behind its template's outfits, every string of that cell says one outfit more, and the new outfit is
 * unlocked (LOCK=1). Nothing is saved and no profile changes: an unlock is the answer's only. */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "new_fighters.h"
#include "fighter_gameplay.h"
#include "fighter_voice.h"

unsigned int defjam_fe_call(const char *command);                   /* recomp_manual.c */
FILE *netplay_open_fight_file(const char *name, int *overridden);   /* netplay.c: as the print reads it */

#define CHARACTER_TABLE 0x4CE5B1B2u
#define ROW_WORDS 26
#define W_BLAZINS 3         /* gameplay words (fighter_gameplay.h); the game reads neither */
#define W_DISPLAY 5
#define W_FE_ID 8
#define W_CORE_ID 10
#define W_GAMEPLAY 11
#define W_MAIN 13

static NewFighter g_new[NEW_FIGHTER_MAX];
static uint32_t g_words[NEW_FIGHTER_MAX][ROW_WORDS];    /* its words (stats, style bits) */
static FighterGameplay g_gp[NEW_FIGHTER_MAX];           /* its own gameplay, when g_gp_ok */
static int g_gp_ok[NEW_FIGHTER_MAX];
static int g_new_count = -1;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* The string of index i in the database (its pool text), or "" (a hash-only string). */
static const char *sod_string(const uint8_t *d, size_t size, uint32_t i)
{
    uint32_t strt = rd32(d + 0x14), n, off;
    if (strt + 8 > size) return "";
    n = rd32(d + strt + 4);
    if (i >= n || strt + 8 + 8 * (size_t)i + 8 > size) return "";
    off = rd32(d + strt + 8 + 8 * i + 4);
    if (!off || off >= size || !memchr(d + off, 0, size - off)) return "";
    return (const char *)d + off;
}

static void load(void)
{
    struct { uint32_t fe, core, main; } stock[160];
    int nstock = 0, i, j;
    FILE *f;
    long n;
    uint8_t *d = NULL;
    size_t size = 0;
    int overridden = 0;
    g_new_count = 0;
    f = netplay_open_fight_file("assets\\djv2.sod", &overridden);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0x20 && n < 64L * 1024 * 1024 && (d = (uint8_t *)malloc((size_t)n + 1)) != NULL)
        size = fread(d, 1, (size_t)n, f);
    fclose(f);
    if (!d) return;
    if (size > 0x20 && !memcmp(d, "SODB", 4)) {
        uint32_t tables = rd32(d + 0x10), nt, t;
        nt = tables + 12 <= size ? rd32(d + tables + 4) : 0;
        for (t = 0; t < nt && tables + 12 + 8 * (size_t)nt <= size; t++) {
            uint32_t off, ncls, block, count, stride, r;
            if (rd32(d + tables + 12 + 4 * t) != CHARACTER_TABLE) continue;
            off = rd32(d + tables + 12 + 4 * nt + 4 * t);
            if (off + 0x18 > size) break;
            ncls = rd32(d + off + 0x10);
            if (!ncls || off + 0x18 + 8 * (size_t)ncls > size) break;
            block = rd32(d + off + 0x18 + 4 * ncls);
            if (block + 12 > size) break;
            count = rd32(d + block + 4);
            stride = rd32(d + block + 8);
            if (!stride || block + 12 + 4 * (size_t)count * (1 + stride) > size) break;
            for (r = 0; r < count; r++) {
                uint32_t row = rd32(d + block + 12 + 4 * count + 4 * r * stride), fe, core;
                if (row + 4 * ROW_WORDS > size) continue;
                fe = rd32(d + row + 4 * W_FE_ID);
                core = rd32(d + row + 4 * W_CORE_ID);
                if (fe >= NEW_FIGHTER_FIRST && fe <= NEW_FIGHTER_LAST) {
                    NewFighter *nf;
                    if (g_new_count >= NEW_FIGHTER_MAX) continue;
                    nf = &g_new[g_new_count++];
                    nf->fe_id = (int)fe;
                    nf->core_id = (int)core;
                    nf->template_fe_id = -1;
                    for (i = 0; i < ROW_WORDS; i++) g_words[g_new_count - 1][i] = rd32(d + row + 4 * (uint32_t)i);
                    snprintf(nf->name, sizeof(nf->name), "%s", sod_string(d, size, rd32(d + row + 4 * W_DISPLAY)));
                    if (!nf->name[0]) snprintf(nf->name, sizeof(nf->name), "Fighter %u", fe);
                } else if (nstock < (int)(sizeof(stock) / sizeof(stock[0]))) {
                    stock[nstock].fe = fe;
                    stock[nstock].core = core;
                    stock[nstock].main = rd32(d + row + 4 * W_MAIN);
                    nstock++;
                }
            }
            break;
        }
    }
    free(d);
    /* the template: the stock fighter with a cell of its own (word 13 = 1) and the same coreID */
    for (i = 0; i < g_new_count; i++)
        for (j = 0; j < nstock; j++)
            if (stock[j].main == 1 && (int)stock[j].core == g_new[i].core_id && stock[j].fe != 98) {
                g_new[i].template_fe_id = (int)stock[j].fe;
                break;
            }
    for (i = 0; i < g_new_count; i++) {
        const FighterGameplay *g = &g_gp[i];
        g_gp_ok[i] = fighter_gameplay_decode(g_words[i][W_BLAZINS], g_words[i][W_GAMEPLAY], &g_gp[i]);
        fprintf(stderr, "[NEWFIGHTER] feID %d \"%s\": coreID %d, an outfit of feID %d\n", g_new[i].fe_id,
                g_new[i].name, g_new[i].core_id, g_new[i].template_fe_id);
        if (g_gp_ok[i])
            fprintf(stderr, "[NEWFIGHTER] feID %d gameplay: stats %d %d %d %d %d %d %d, moves %s%d, Blazins %d %d %d %d\n",
                    g_new[i].fe_id, g->stats[0], g->stats[1], g->stats[2], g->stats[3], g->stats[4], g->stats[5],
                    g->stats[6], g->moveset ? "archetype " : "template's", g->moveset ? FGP_ARCHETYPE_BASE + g->moveset : 0,
                    g->blazins[0], g->blazins[1], g->blazins[2], g->blazins[3]);
        else if ((g_words[i][W_GAMEPLAY] >> 28) != 0 || g_words[i][W_BLAZINS] != 0)
            fprintf(stderr, "[NEWFIGHTER] feID %d: gameplay words %08x %08x refused (out of range); the template's "
                    "gameplay\n", g_new[i].fe_id, g_words[i][W_BLAZINS], g_words[i][W_GAMEPLAY]);
    }
}

const FighterGameplay *new_fighter_gameplay(int fe_id)
{
    int i;
    if (g_new_count < 0) load();
    for (i = 0; i < g_new_count; i++)
        if (g_new[i].fe_id == fe_id) return g_gp_ok[i] ? &g_gp[i] : NULL;
    return NULL;
}

static int new_fighter_core(int fe_id)
{
    int i;
    if (g_new_count < 0) load();
    for (i = 0; i < g_new_count; i++)
        if (g_new[i].fe_id == fe_id) return g_new[i].core_id;
    return -1;
}

/* ---- the gameplay at match set-up ----
 *
 * The created-fighter path's three override points, written for a new fighter's slot the way the game's
 * created-fighter branch writes them (docs/research/new-fighter-gameplay.md):
 *   1. after Game.StartGame: the slot's stat block GS+0x2B0+0x1C*slot (7 dwords) with its flag GS+0x2AC+slot,
 *      and the four Blazin keys GS+0xE0/+0xD8/+0xE4/+0xDC+16*slot (keys 0..3) and their copy 0x40 above;
 *   2. the fight's set-up (sub_001A4D90, before the records spawn): a style combination's character index in
 *      0x3B90B8+4*slot, so the record's styles, moves (WWAZ), animation bank and Blazin rule follow it;
 *   3. the Blazins' registration (sub_001A8180, after the spawn): a template-moveset fighter's four Blazins into
 *      its record (the game does that for character index 86 and above only).
 * Every value comes from the fighter byte of the set-up and the database every PC shares (in the fight-file
 * print), and is written before the first fight step, so lockstep and rollback start from the same state on
 * every PC. The template's coreID stays in the slot (GS+0x168: speech row, intro clip, AI file). */
#define MANAGERS        0x002FB148u
#define GAMEDATA_SLOT   0x18u
#define GD_FIGHTER      0x1AAAu         /* + 5 * slot: the fighter byte (unsigned) */
#define GD_CORE         0x168u          /* + 4 * slot */
#define GD_STATS_ON     0x2ACu          /* + slot, byte */
#define GD_STATS        0x2B0u          /* + 0x1C * slot, 7 dwords */
#define GD_BLAZIN_KEY0  0xE0u           /* + 16 * slot; key 1 at 0xD8, key 2 at 0xE4, key 3 at 0xDC */
#define GD_BLAZIN_COPY  0x40u           /* the game's second copy of each key */
#define CHAR_INDEX      0x003B90B8u     /* + 4 * slot (sub_000871F0: setup core - 1) */
#define SLOT_CORE       0x003B9278u     /* + 4 * slot: the set-up's coreID as the AI file picker reads it */
#define RECORDS         0x003B92E0u     /* the four fighter records, 0x12E8 bytes each */
#define RECORD_SIZE     0x12E8u
#define REC_SLOT        0x28u
#define REC_CHAR_INDEX  0x2Cu
#define REC_STYLES      0xAE4u          /* three dwords, 7 = none */
#define REC_STATS       0xB34u          /* seven dwords */
#define REC_BLAZINS     0xBB8u          /* four dwords: keys 1, 3, 0, 2 */
#define REC_MAX_HEALTH  0x354u
#define CP_FIRST_INDEX  86              /* sub_001A8180 applies the set-up's Blazins from here on */

static uint32_t game_data(void) { return MEM32(MANAGERS + GAMEDATA_SLOT * 4); }

/* The gameplay of slot s's new fighter (NULL for any other fighter, or one without its own). */
static const FighterGameplay *slot_gameplay(uint32_t gd, int s, int *fe_out)
{
    int fe;
    if (gd < 0x10000u) return NULL;
    fe = (int)MEM8(gd + GD_FIGHTER + 5u * (uint32_t)s);
    if (fe_out) *fe_out = fe;
    if (fe < NEW_FIGHTER_FIRST || fe > NEW_FIGHTER_LAST) return NULL;
    return new_fighter_gameplay(fe);
}

void new_fighters_start_game_done(uint32_t gd)
{
    static const uint32_t key_at[FGP_BLAZINS] = { 0xE0u, 0xD8u, 0xE4u, 0xDCu };
    int s, k, fe;
    for (s = 0; s < 4; s++) {
        const FighterGameplay *g = slot_gameplay(gd, s, &fe);
        uint32_t core;
        if (!g) continue;
        core = MEM32(gd + GD_CORE + 4u * (uint32_t)s);
        if ((int)core != new_fighter_core(fe)) {
            /* not the row's slot as the game set it up (a Story or created-fighter slot): leave it */
            fprintf(stderr, "[NEWFIGHTER] slot %d feID %d: the slot holds coreID %u, not the row's; gameplay left\n",
                    s, fe, core);
            continue;
        }
        for (k = 0; k < FGP_STATS; k++)
            MEM32(gd + GD_STATS + 0x1Cu * (uint32_t)s + 4u * (uint32_t)k) = (uint32_t)g->stats[k];
        MEM8(gd + GD_STATS_ON + (uint32_t)s) = 1;
        for (k = 0; k < FGP_BLAZINS; k++) {
            MEM32(gd + key_at[k] + 16u * (uint32_t)s) = (uint32_t)g->blazins[k];
            MEM32(gd + key_at[k] + GD_BLAZIN_COPY + 16u * (uint32_t)s) = (uint32_t)g->blazins[k];
        }
        fprintf(stderr, "[NEWFIGHTER] slot %d feID %d: own stats and Blazins set (moves: %s)\n", s, fe,
                g->moveset ? "a style combination" : "the template's");
    }
}

/* RECOMP_NF_STATS_DUMP=1: every character index's decoded parameters (styles, the seven stats, Blazins) in
 * the log once, with the game's own decoder (sub_001A3AD0: out, WPAR), into the last fighter record, which
 * sub_001A4D90 clears right after this. A measurement for docs/research/new-fighter-gameplay.md. */
extern void sub_001A3AD0(void);
static void dump_stock_stats(void)
{
    static int done;
    const char *e = getenv("RECOMP_NF_STATS_DUMP");
    uint32_t out = RECORDS + 3u * RECORD_SIZE + REC_STYLES, sp = g_esp;
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx, ebx = g_ebx, esi = g_esi, edi = g_edi, ebp = g_ebp;
    double fp[8];
    int fp_top = g_fp_top, idx, k;
    uint16_t fp_cw = g_fp_control_word;
    if (done || !e || !*e || !strcmp(e, "0")) return;
    done = 1;
    memcpy(fp, g_fp_stack, sizeof(fp));
    for (idx = 0; idx < 141; idx++) {
        uint32_t wpar = MEM32(0x003B864Cu + 16u * (uint32_t)idx);
        if (wpar < 0x10000u) {
            fprintf(stderr, "[NFSTATS] index %d: no WPAR\n", idx);
            continue;
        }
        for (k = 0; k < 232; k += 4) MEM32(out + (uint32_t)k) = 0;
        g_esp = (sp - 0x200u) & ~3u;
        PUSH32(g_esp, wpar);
        PUSH32(g_esp, out);
        PUSH32(g_esp, 0u);
        sub_001A3AD0();
        g_esp = sp;
        fprintf(stderr, "[NFSTATS] index %d core %d: styles %d %d %d stats", idx, idx + 1, (int)MEM32(out),
                (int)MEM32(out + 4), (int)MEM32(out + 8));
        for (k = 0; k < 7; k++) fprintf(stderr, " %d", (int)MEM32(out + (REC_STATS - REC_STYLES) + 4u * (uint32_t)k));
        fprintf(stderr, " blazins");
        for (k = 0; k < 4; k++) fprintf(stderr, " %d", (int)MEM32(out + (REC_BLAZINS - REC_STYLES) + 4u * (uint32_t)k));
        fprintf(stderr, "\n");
    }
    for (k = 0; k < 232; k += 4) MEM32(out + (uint32_t)k) = 0;
    memcpy(g_fp_stack, fp, sizeof(fp));
    g_fp_top = fp_top;
    g_fp_control_word = fp_cw;
    g_eax = eax; g_ecx = ecx; g_edx = edx; g_ebx = ebx; g_esi = esi; g_edi = edi; g_ebp = ebp;
}

void new_fighters_fight_setup(void)
{
    uint32_t gd = game_data();
    int s, fe;
    dump_stock_stats();
    for (s = 0; s < 4; s++) {
        const FighterGameplay *g = slot_gameplay(gd, s, &fe);
        uint32_t at = CHAR_INDEX + 4u * (uint32_t)s, was = MEM32(at);
        if (!g) continue;
        if ((int)was != new_fighter_core(fe) - 1) {
            fprintf(stderr, "[NEWFIGHTER] slot %d feID %d: character index %u is not the template's; left\n", s,
                    fe, was);
            continue;
        }
        if (g->moveset)
            MEM32(at) = (uint32_t)fighter_gameplay_char_index(g);
        fprintf(stderr, "[NEWFIGHTER] slot %d feID %d: character index %u -> %u (set-up core for the AI %u)\n", s,
                fe, was, MEM32(at), MEM32(SLOT_CORE + 4u * (uint32_t)s));
    }
}

void new_fighters_blazins(void)
{
    uint32_t gd = game_data();
    uint32_t r;
    for (r = 0; r < 4; r++) {
        uint32_t rec = RECORDS + r * RECORD_SIZE, slot = MEM32(rec + REC_SLOT), idx = MEM32(rec + REC_CHAR_INDEX);
        const FighterGameplay *g;
        int fe = -1;
        if (slot > 3u) continue;
        g = slot_gameplay(gd, (int)slot, &fe);
        if (g && idx < CP_FIRST_INDEX) {
            MEM32(rec + REC_BLAZINS + 0) = (uint32_t)g->blazins[1];
            MEM32(rec + REC_BLAZINS + 4) = (uint32_t)g->blazins[3];
            MEM32(rec + REC_BLAZINS + 8) = (uint32_t)g->blazins[0];
            MEM32(rec + REC_BLAZINS + 12) = (uint32_t)g->blazins[2];
        }
        if (g && idx >= CP_FIRST_INDEX)     /* the game's body copies these (keys 1, 3, 0, 2) into the record */
            fprintf(stderr, "[NEWFIGHTER] record %u: index %u, the set-up's Blazins %d %d %d %d go in as the game "
                    "does for a created fighter\n", r, idx, (int)MEM32(0x003B9288u + 16u * slot),
                    (int)MEM32(0x003B9288u + 16u * slot + 4), (int)MEM32(0x003B9288u + 16u * slot + 8),
                    (int)MEM32(0x003B9288u + 16u * slot + 12));
        if (g || getenv("RECOMP_NF_STATS_DUMP"))
            fprintf(stderr, "[NEWFIGHTER] record %u (slot %u, fighter %d): index %u styles %d %d %d stats %d %d %d %d "
                    "%d %d %d Blazins %d %d %d %d%s, max health %.1f\n", r, slot, fe, idx, (int)MEM32(rec + REC_STYLES),
                    (int)MEM32(rec + REC_STYLES + 4), (int)MEM32(rec + REC_STYLES + 8), (int)MEM32(rec + REC_STATS),
                    (int)MEM32(rec + REC_STATS + 4), (int)MEM32(rec + REC_STATS + 8), (int)MEM32(rec + REC_STATS + 12),
                    (int)MEM32(rec + REC_STATS + 16), (int)MEM32(rec + REC_STATS + 20), (int)MEM32(rec + REC_STATS + 24),
                    (int)MEM32(rec + REC_BLAZINS), (int)MEM32(rec + REC_BLAZINS + 4), (int)MEM32(rec + REC_BLAZINS + 8),
                    (int)MEM32(rec + REC_BLAZINS + 12), g && idx < CP_FIRST_INDEX ? " (set here)" : "",
                    (double)MEMF(rec + REC_MAX_HEALTH));
    }
}

/* ---- the voice at match load (fighter_voice.h) ----
 *
 * sub_0003E170 (the speech set-up of a match) fills one 0x64-byte row per slot at 0x3403A4: +0x00 the slot's
 * coreID (-1 empty), +0x10 the voice number (stock: 0x2DC5D0[coreID]; a created fighter: 0x28E48C[voice]); then
 * it queues the slot's banks with sub_0003DB00 (whose entry runs this), and later sub_00038440 / 0x3C52B stream
 * the intro / extro line by the same field. A new fighter whose row word 21 holds its own voice number gets
 * that number here when the speech archive the game opens has all its files; else it keeps the template's. */
#define W_SPEECH        21
#define SPEECH_ROWS     0x003403A4u     /* + 0x64 * slot */
#define SPEECH_ROW_SIZE 0x64u
#define SPEECH_VOICE    0x10u

static int g_voice_present[NEW_FIGHTER_MAX];   /* 0 unknown, 1 all files there, -1 not */

static int voice_files_present(int voice)
{
    int idx = voice - FV_FIRST_VOICE, overridden = 0;
    FILE *f;
    uint8_t head[16], *dir;
    uint32_t hsize;
    if (idx < 0 || idx >= NEW_FIGHTER_MAX) return 0;
    if (g_voice_present[idx]) return g_voice_present[idx] > 0;
    g_voice_present[idx] = -1;
    f = netplay_open_fight_file(FV_SPEECH_ARCHIVE, &overridden);
    if (!f) return 0;
    if (fread(head, 1, 16, f) == 16) {
        hsize = (uint32_t)head[12] << 24 | (uint32_t)head[13] << 16 | (uint32_t)head[14] << 8 | head[15];
        if (hsize >= 16 && hsize < 16u * 1024 * 1024 && (dir = (uint8_t *)malloc(hsize)) != NULL) {
            memcpy(dir, head, 16);
            if (fread(dir + 16, 1, hsize - 16, f) == hsize - 16 && fighter_voice_entries_present(dir, hsize, voice))
                g_voice_present[idx] = 1;
            free(dir);
        }
    }
    fclose(f);
    fprintf(stderr, "[VOICE] voice %03d: files %s in %s%s\n", voice, g_voice_present[idx] > 0 ? "all present" :
            "missing", FV_SPEECH_ARCHIVE, overridden ? " (pack copy)" : "");
    return g_voice_present[idx] > 0;
}

/* The own voice number of new fighter fe_id when its row asks for one (word 21 = 40 + feID - 100), else -1. */
static int new_fighter_voice(int fe_id)
{
    int i, n = fighter_voice_number(fe_id);
    if (n < 0) return -1;
    if (g_new_count < 0) load();
    for (i = 0; i < g_new_count; i++)
        if (g_new[i].fe_id == fe_id) return (int)g_words[i][W_SPEECH] == n ? n : -1;
    return -1;
}

void new_fighters_voice(void)
{
    static uint32_t s_logged[4];
    uint32_t gd = game_data();
    int s, fe, voice;
    if (gd < 0x10000u) return;
    for (s = 0; s < 4; s++) {
        uint32_t row = SPEECH_ROWS + SPEECH_ROW_SIZE * (uint32_t)s, was = MEM32(row + SPEECH_VOICE);
        fe = (int)MEM8(gd + GD_FIGHTER + 5u * (uint32_t)s);
        if (fe < NEW_FIGHTER_FIRST || fe > NEW_FIGHTER_LAST || MEM32(row) == 0xFFFFFFFFu) continue;
        voice = new_fighter_voice(fe);
        if (voice < 0 || (int)was == voice) continue;
        if ((int)MEM32(row) != new_fighter_core(fe)) {
            /* not the row's slot as the stock path set it up (a created fighter's voice): leave it */
            fprintf(stderr, "[VOICE] slot %d feID %d: speech row core %u is not the fighter's; voice left\n", s, fe,
                    MEM32(row));
            continue;
        }
        if (!voice_files_present(voice)) {
            if (s_logged[s] != (uint32_t)(voice << 16 | was))
                fprintf(stderr, "[VOICE] slot %d feID %d: own voice %03d has no files; the template's voice %03u\n",
                        s, fe, voice, was);
            s_logged[s] = (uint32_t)(voice << 16 | was);
            continue;
        }
        MEM32(row + SPEECH_VOICE) = (uint32_t)voice;
        fprintf(stderr, "[VOICE] slot %d feID %d: own voice %03d (the template's was %03u)\n", s, fe, voice, was);
    }
}

/* RECOMP_SPEECH_LOG=1: every bank line the game looks up (sub_001393F0: cdecl (bank header, entry index, ...);
 * the header holds the bank's family and voice number), and always for an own voice (40..99): which line plays
 * when. A measurement for docs/research/new-fighter-voice.md. */
void new_fighters_speech_line(uint32_t hdr, uint32_t index)
{
    static int s_all = -1;
    uint32_t family, voice;
    if (hdr < 0x10000u) return;
    if (s_all < 0) s_all = getenv("RECOMP_SPEECH_LOG") && *getenv("RECOMP_SPEECH_LOG") == '1';
    family = MEM16(hdr);
    voice = MEM16(hdr + 2);
    if (s_all || (voice >= FV_FIRST_VOICE && voice < 100))
        fprintf(stderr, "[SPEECH] line: %s voice %03u entry %u of %u (fields %u %u %u)\n",
                family <= 1 ? "Character" : (family & 0xFFFEu) == 16624u ? "Taunts" : (family & 0xFFFEu) == 40962u ?
                "Enviro" : "bank", voice, index, MEM8(hdr + 5),
                index < MEM8(hdr + 5) ? MEM8(hdr + 12 + ((MEM8(hdr + 4) & 0x7Fu) + 2u) * index + 2) : 0u,
                index < MEM8(hdr + 5) ? MEM8(hdr + 12 + ((MEM8(hdr + 4) & 0x7Fu) + 2u) * index + 3) : 0u,
                index < MEM8(hdr + 5) && (MEM8(hdr + 4) & 0x7Fu) > 2 ?
                    MEM8(hdr + 12 + ((MEM8(hdr + 4) & 0x7Fu) + 2u) * index + 4) : 0u);
}

int new_fighters_list(const NewFighter **out)
{
    if (g_new_count < 0) load();
    if (out) *out = g_new;
    return g_new_count;
}

int new_fighter_template(int fe_id)
{
    int i;
    if (g_new_count < 0) load();
    for (i = 0; i < g_new_count; i++)
        if (g_new[i].fe_id == fe_id) return g_new[i].template_fe_id;
    return -1;
}

/* ---- the select screens' fighter list ---- */

#define LIST_MAX 256
#define TEXT_MAX 160
static char g_list[LIST_MAX][TEXT_MAX];
static int g_list_n, g_list_pos, g_serving;
static uint32_t g_self;

static int field(const char *s, const char *key)
{
    char k[16];
    const char *p;
    snprintf(k, sizeof(k), "%s=", key);
    p = strstr(s, k);
    while (p && p != s && p[-1] != '&') p = strstr(p + 1, k);
    return p ? atoi(p + strlen(k)) : -1;
}

/* s with key's value replaced (the first one). */
static void set_field(char *s, const char *key, int value)
{
    char k[16], out[TEXT_MAX], *p, *end;
    snprintf(k, sizeof(k), "%s=", key);
    p = strstr(s, k);
    while (p && p != s && p[-1] != '&') p = strstr(p + 1, k);
    if (!p) return;
    end = p + strlen(k);
    while (*end >= '0' && *end <= '9') end++;
    snprintf(out, sizeof(out), "%.*s%s%d%s", (int)(p - s), s, k, value, end);
    snprintf(s, TEXT_MAX, "%s", out);
}

static void read_guest(uint32_t p, char *out, int size)
{
    int k;
    for (k = 0; p && k < size - 1; k++) {
        char c = (char)MEM8(p + (uint32_t)k);
        if (!c) break;
        out[k] = c;
    }
    out[p ? k : 0] = 0;
}

static void rebuild_list(void)
{
    char raw[LIST_MAX][TEXT_MAX];
    int n = 0, i, j, k, moved = 0;
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx, ebx = g_ebx, esi = g_esi, edi = g_edi, ebp = g_ebp;
    double fp[8];
    int fp_top = g_fp_top;
    uint16_t fp_cw = g_fp_control_word;
    memcpy(fp, g_fp_stack, sizeof(fp));
    g_serving = 0;
    for (i = 0; i < LIST_MAX; i++) {
        uint32_t p = defjam_fe_call("Game.GetNextFighter()");
        read_guest(p, raw[n], TEXT_MAX);
        if (!raw[n][0] || !strcmp(raw[n], "NO_RETURN")) break;
        n++;
    }
    g_eax = eax; g_ecx = ecx; g_edx = edx; g_ebx = ebx; g_esi = esi; g_edi = edi; g_ebp = ebp;
    memcpy(g_fp_stack, fp, sizeof(fp));
    g_fp_top = fp_top;
    g_fp_control_word = fp_cw;
    if (g_self >= 0x10000u) MEM32(g_self + 0x18u) = 0;     /* the game's cursor back to the start */
    if (!n) return;
    /* the copy: the stock strings in order, each new fighter's behind its template's outfits */
    g_list_n = 0;
    for (i = 0; i < n && g_list_n < LIST_MAX; i++) {
        int fe = field(raw[i], "FE_ID");
        if (new_fighter_template(fe) >= 0) continue;     /* placed with its template below */
        snprintf(g_list[g_list_n], TEXT_MAX, "%s", raw[i]);
        if (fe >= NEW_FIGHTER_FIRST && fe <= NEW_FIGHTER_LAST)
            set_field(g_list[g_list_n], "LOCK", 1);       /* no template here: a cell of its own, unlocked */
        g_list_n++;
    }
    for (k = 0; k < g_new_count; k++) {
        int tpl = g_new[k].template_fe_id, at = -1, cloth, first;
        char add[TEXT_MAX] = "";
        if (tpl < 0) continue;
        for (i = 0; i < n; i++)
            if (field(raw[i], "FE_ID") == g_new[k].fe_id) snprintf(add, sizeof(add), "%s", raw[i]);
        for (i = 0; i < g_list_n; i++)
            if (field(g_list[i], "FE_ID") == tpl) { at = i; break; }
        if (at < 0 || g_list_n >= LIST_MAX) continue;
        if (!add[0]) {
            /* not in the game's own list (it always is today; kept for a list hook): the template's string
             * with this row's ID and stat bars (U, L, S, T, H, C = words 17, 16, 22, 25, 12, 2) */
            const uint32_t *w = g_words[k];
            snprintf(add, sizeof(add), "%s", g_list[at]);
            set_field(add, "FE_ID", g_new[k].fe_id);
            set_field(add, "U", (int)w[17]);
            set_field(add, "L", (int)w[16]);
            set_field(add, "S", (int)w[22]);    /* the first S= is the speed bar, a later one a style */
            set_field(add, "T", (int)w[25]);
            set_field(add, "H", (int)w[12]);
            set_field(add, "C", (int)w[2]);
        }
        cloth = field(g_list[at], "CLOTH");
        first = at;
        if (cloth < 1) cloth = 1;
        /* the cell's outfits: the template's string and the cloth-1 after it that say the same count */
        for (j = at + 1; j < g_list_n && j < first + cloth && field(g_list[j], "CLOTH") == cloth; j++) {}
        memmove(g_list[j + 1], g_list[j], (size_t)(g_list_n - j) * TEXT_MAX);
        g_list_n++;
        snprintf(g_list[j], TEXT_MAX, "%s", add);
        set_field(g_list[j], "LOCK", 1);
        for (i = first; i <= j; i++) set_field(g_list[i], "CLOTH", cloth + 1);
        moved++;
    }
    /* a new fighter whose template has no cell in this list (none, or not offered here) keeps its own, unlocked */
    for (k = 0; k < g_new_count; k++) {
        int found = 0;
        for (i = 0; i < g_list_n; i++) if (field(g_list[i], "FE_ID") == g_new[k].fe_id) found = 1;
        for (i = 0; i < n && !found && g_list_n < LIST_MAX; i++)
            if (field(raw[i], "FE_ID") == g_new[k].fe_id) {
                snprintf(g_list[g_list_n], TEXT_MAX, "%s", raw[i]);
                set_field(g_list[g_list_n++], "LOCK", 1);
                found = 1;
            }
    }
    g_list_pos = 0;
    g_serving = 1;
    {
        static int told;
        if (told++ < 6)
            fprintf(stderr, "[NEWFIGHTER] fighter list: %d strings from the game, %d answered, %d new fighters as outfits\n",
                    n, g_list_n, moved);
        if (told < 3)
            for (i = 0; i < g_list_n; i++)
                for (k = 0; k < g_new_count; k++)
                    if (field(g_list[i], "FE_ID") == g_new[k].fe_id || field(g_list[i], "FE_ID") == g_new[k].template_fe_id)
                        fprintf(stderr, "[NEWFIGHTER]   %d: %s\n", i, g_list[i]);
    }
}

/* Game.GetNextFighter (sub_000480E0, thiscall on the fighter list, reached through the command table's
 * vtable): the list's cursor at +0x18, its entry count at +4, the answer buffer at +0x10 (the string the
 * FuncCall dispatcher hands the movie; 0 when the list is done, which the movie gets as NO_RETURN). The
 * game's own call runs (its cursor moves on as always), then the buffer gets this module's string. */
extern void sub_000480E0(void);

void new_fighters_get_next(void)
{
    uint32_t self = g_ecx, buf;
    const char *answer;
    size_t k, len;
    sub_000480E0();
    if (self >= 0x10000u) g_self = self;
    if (!g_serving || self < 0x10000u) return;
    buf = MEM32(self + 0x10u);
    if (g_list_pos >= g_list_n || buf < 0x10000u) {
        g_eax = 0;
        return;
    }
    answer = g_list[g_list_pos++];
    len = strlen(answer);
    for (k = 0; k <= len; k++) MEM8(buf + (uint32_t)k) = (uint8_t)answer[k];
    g_eax = buf;
}

void new_fighters_after_funccall(const char *text)
{
    if (strcmp(text, "Game.ResetFighterList()")) return;
    if (new_fighters_list(NULL) <= 0) return;
    rebuild_list();
}

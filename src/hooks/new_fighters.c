/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* New fighters as select cells of their own (the community row). See new_fighters.h and
 * docs/research/new-fighters.md.
 *
 * The select screens (battle/chooseF2/3/4) build their grid from Game.ResetFighterList() and then one
 * Game.GetNextFighter() string per outfit, "CLOTH=<outfits of the cell>&FE_ID=<id>&LOCK=<1 unlocked>&U=..",
 * until "NO_RETURN". ResetFighterList (sub_00047BC0) lists the Character rows whose word 13 is 1, sorts them,
 * and groups Blaze 2/3, Sticky 2 and D-Mob 2/3 under their cells by hard-coded IDs (80..84; it skips 98). A new
 * row (at a new fighter's place, new_fighter_places.h: 100..127, then 20 free IDs below 98)
 * comes out as a cell of its own, locked (it is not in the profile's 80-entry fighter set), with the grid
 * thumbnail of frame feID, which is empty.
 *
 * So right after the game's ResetFighterList this reads the whole list once (the game's own strings, in its
 * order) and answers the screen's GetNextFighter calls from a copy in which the stock strings keep their order and
 * every new fighter follows as a cell of its own, by place: the last row(s) of the grid, unlocked (LOCK=1), one
 * outfit (CLOTH=1). Its grid thumbnail, portrait and name art are its own files (the pack's pictures, painted by
 * the launcher; the grid picture through the chooseF2/3/4 patch of scripts/apt-patch.py). The template is the
 * gameplay basis only. Nothing is saved and no profile changes: an unlock is the answer's only. */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "new_fighters.h"
#include "fighter_gameplay.h"
#include "fighter_voice.h"
#include "fighter_skins.h"
#include "fighter_banks.h"
#include "guest_window.h"
#include "unlock_list.h"

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
#define W_STYLE_BITS 18     /* the select screen's styles: bit 0 K, 1 B, 2 M, 3 W, 4 S */

static NewFighter g_new[NEW_FIGHTER_MAX];
static uint32_t g_words[NEW_FIGHTER_MAX][ROW_WORDS];    /* its words (stats, style bits) */
static FighterGameplay g_gp[NEW_FIGHTER_MAX];           /* its own gameplay, when g_gp_ok */
static int g_gp_ok[NEW_FIGHTER_MAX];
static int g_new_count = -1;
static unsigned char g_new_row[256];                   /* Character rows (the id's low bits) of new fighters */
static int g_style_bits[256];                           /* build 90: per feID its row's style bits, -1 none */

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
    memset(g_new_row, 0, sizeof(g_new_row));
    for (i = 0; i < 256; i++) g_style_bits[i] = -1;
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
                /* the fighter's own cell (word 13 = 1) wins over an outfit row with the same feID */
                if (fe < 256 && (g_style_bits[fe] < 0 || rd32(d + row + 4 * W_MAIN) == 1))
                    g_style_bits[fe] = (int)(rd32(d + row + 4 * W_STYLE_BITS) & 0x1Fu);
                if (new_fighter_is_place((int)fe) && fighter_banks_index((int)fe) >= 0 &&
                    !strncmp(sod_string(d, size, rd32(d + row)), "FFNYBank", 8)) {
                    if (r < sizeof(g_new_row)) g_new_row[r] = 1;  /* not a stock fighter: out of the Unlock shop too */
                    continue;   /* a bank row (fighter_banks.h): its fighter is bound per match */
                } else if (new_fighter_is_place((int)fe)) {
                    NewFighter *nf;
                    if (r < sizeof(g_new_row)) g_new_row[r] = 1;
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
        fprintf(stderr, "[NEWFIGHTER] feID %d \"%s\": coreID %d, template feID %d\n", g_new[i].fe_id,
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

/* The gameplay of slot s's new fighter (NULL for any other fighter, or one without its own). A bank (124..127,
 * fighter_banks.h) has the gameplay of the fighter bound to it. */
static FighterGameplay g_bank_gp[FB_BANKS];
static const FighterGameplay *slot_gameplay(uint32_t gd, int s, int *fe_out)
{
    int fe, b;
    if (gd < 0x10000u) return NULL;
    fe = (int)MEM8(gd + GD_FIGHTER + 5u * (uint32_t)s);
    if (fe_out) *fe_out = fe;
    if (!new_fighter_is_place(fe)) return NULL;
    if ((b = fighter_banks_index(fe)) >= 0 && fighter_banks_ready()) {
        const FighterBanksEntry *e = fighter_banks_bound(b);
        if (!e || !fighter_gameplay_decode(e->words[W_BLAZINS], e->words[W_GAMEPLAY], &g_bank_gp[b])) return NULL;
        return &g_bank_gp[b];
    }
    return new_fighter_gameplay(fe);
}

/* The coreID a new fighter's row gives the slot (a bank's: its bound fighter's), -1 if none. */
static int slot_fighter_core(int fe)
{
    int b = fighter_banks_index(fe);
    if (b >= 0 && fighter_banks_ready()) {
        const FighterBanksEntry *e = fighter_banks_bound(b);
        return e ? e->core_id : -1;
    }
    return new_fighter_core(fe);
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
        if ((int)core != slot_fighter_core(fe)) {
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
        if ((int)was != slot_fighter_core(fe) - 1) {
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
static uint8_t *g_speech_dir;                   /* the speech archive's directory as the host file has it */
static size_t g_speech_dir_n;
static int g_speech_dir_state;                  /* 0 not read, 1 read, -1 none */
static int g_speech_dir_over;

/* The directory of the speech archive the game opens (read once: content changes only between runs). */
static const uint8_t *speech_dir(size_t *n)
{
    FILE *f;
    uint8_t head[16];
    uint32_t hsize;
    if (g_speech_dir_state) {
        *n = g_speech_dir_n;
        return g_speech_dir_state > 0 ? g_speech_dir : NULL;
    }
    g_speech_dir_state = -1;
    *n = 0;
    f = netplay_open_fight_file(FV_SPEECH_ARCHIVE, &g_speech_dir_over);
    if (!f) return NULL;
    if (fread(head, 1, 16, f) == 16) {
        hsize = (uint32_t)head[12] << 24 | (uint32_t)head[13] << 16 | (uint32_t)head[14] << 8 | head[15];
        if (hsize >= 16 && hsize < 16u * 1024 * 1024 && (g_speech_dir = (uint8_t *)malloc(hsize)) != NULL) {
            memcpy(g_speech_dir, head, 16);
            if (fread(g_speech_dir + 16, 1, hsize - 16, f) == hsize - 16) {
                g_speech_dir_n = hsize;
                g_speech_dir_state = 1;
            } else {
                free(g_speech_dir);
                g_speech_dir = NULL;
            }
        }
    }
    fclose(f);
    *n = g_speech_dir_n;
    return g_speech_dir_state > 0 ? g_speech_dir : NULL;
}

static int voice_files_present(int voice)
{
    int idx = voice - FV_FIRST_VOICE;
    size_t n;
    const uint8_t *dir;
    if (idx < 0 || idx >= NEW_FIGHTER_MAX) return 0;
    if (g_voice_present[idx]) return g_voice_present[idx] > 0;
    g_voice_present[idx] = -1;
    dir = speech_dir(&n);
    if (dir && fighter_voice_entries_present(dir, n, voice)) g_voice_present[idx] = 1;
    fprintf(stderr, "[VOICE] voice %03d: files %s in %s%s\n", voice, g_voice_present[idx] > 0 ? "all present" :
            "missing", FV_SPEECH_ARCHIVE, g_speech_dir_over ? " (pack copy)" : "");
    return g_voice_present[idx] > 0;
}

/* ---- own voices of numbered fighters through their bank (fighter_voice.h) ----
 *
 * The game keeps the speech archive's directory in memory as the file has it (measured 2026-11-18: one copy, in
 * the game's heap, byte for byte the file's; the speech and stream loaders find their entries there). A bank's
 * voice entries are pointed at its bound fighter's files there before the banks are queued; the directory is not
 * read by the fight's simulation, and every PC of a match binds the same fighters (START) to the same banks. */
extern uint32_t xbox_ContiguousBlockSize(uint32_t va);
static uint32_t g_guest_dir;                    /* the guest's copy of the directory, 0 not found yet */
static int g_guest_dir_scans;

/* Whether the guest's bytes at va are the directory, the bank voices' records aside (we change those). */
static int guest_dir_is(uint32_t va, const uint8_t *dir, size_t n)
{
    static long skip[4 * FV_FILES];
    static int nskip = -1;
    const uint8_t *g = (const uint8_t *)XBOX_PTR(va);
    size_t at = 0;
    int k;
    if (nskip < 0) {
        char name[64];
        int b, i;
        nskip = 0;
        for (b = 0; b < 4; b++)
            for (i = 0; i < FV_FILES; i++) {
                long p;
                fighter_voice_entry_name(fighter_voice_bank_voice(b), i, name, sizeof name);
                if ((p = fighter_voice_dir_find(dir, n, name, NULL, NULL)) >= 0) skip[nskip++] = p;
            }
        /* in file order, for the walk below */
        for (b = 1; b < nskip; b++)
            for (k = b; k > 0 && skip[k - 1] > skip[k]; k--) { long t = skip[k]; skip[k] = skip[k - 1]; skip[k - 1] = t; }
    }
    if (memcmp(g, dir, 16)) return 0;
    for (k = 0; k <= nskip; k++) {
        size_t end = k < nskip ? (size_t)skip[k] : n;
        if (end > at && memcmp(g + at, dir + at, end - at)) return 0;
        at = end + 8;
    }
    return 1;
}

/* The guest address of the directory: the cached one if it still holds it, else a search of the contiguous
 * window's live blocks for it (the game reads it at start-up). 0 when it is not there. */
static uint32_t guest_dir(const uint8_t *dir, size_t n)
{
    uint32_t va = GUEST_WINDOW_LO + 0x1000u, found = 0, copies = 0;
    if (g_guest_dir && guest_dir_is(g_guest_dir, dir, n)) return g_guest_dir;
    g_guest_dir = 0;
    if (g_guest_dir_scans++ >= 8) return 0;     /* not a copy we can find: the template's voice */
    while (va < GUEST_WINDOW_HI) {
        uint32_t s = xbox_ContiguousBlockSize(va), end, p;
        if (!s) { va += 0x1000u; continue; }
        end = va + s;
        if (end > GUEST_WINDOW_HI || end < va) end = GUEST_WINDOW_HI;
        for (p = va; p + n <= end; p++) {
            const uint8_t *h = (const uint8_t *)XBOX_PTR(p);
            if (h[0] != 'B' || h[1] != 'I' || h[2] != 'G' || h[3] != dir[3]) continue;
            if (guest_dir_is(p, dir, n)) {
                if (!found) found = p;
                copies++;
            }
        }
        va = (end + 0xFFFu) & ~0xFFFu;
    }
    fprintf(stderr, "[VOICE] the speech archive's directory in the game's memory: %s%08X (%u cop%s, %u bytes)\n",
            found ? "" : "not found ", found, copies, copies == 1 ? "y" : "ies", (unsigned)n);
    return g_guest_dir = found;
}

static void put_be32(uint32_t va, uint32_t v)
{
    MEM8(va) = (uint8_t)(v >> 24);
    MEM8(va + 1) = (uint8_t)(v >> 16);
    MEM8(va + 2) = (uint8_t)(v >> 8);
    MEM8(va + 3) = (uint8_t)v;
}

/* Bank b's voice entries pointed at numbered fighter id's own files. Returns the bank voice, or -1 (the fighter
 * has none, or the directory is not there: the slot keeps the template's voice). */
static int bank_voice_bind(int b, int id)
{
    long pos[FV_FILES];
    uint32_t off[FV_FILES], size[FV_FILES], g;
    size_t n;
    const uint8_t *dir = speech_dir(&n);
    int voice = fighter_voice_bank_voice(b), i;
    if (!dir || voice < 0 || !fighter_voice_numbered_plan(dir, n, voice, id, pos, off, size)) return -1;
    if (!(g = guest_dir(dir, n))) return -1;
    for (i = 0; i < FV_FILES; i++) {
        put_be32(g + (uint32_t)pos[i], off[i]);
        put_be32(g + (uint32_t)pos[i] + 4, size[i]);
    }
    return voice;
}

/* The own voice number of new fighter fe_id when its row asks for one (word 21 = 40 + its place index), else -1. */
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
        if (MEM32(row) == 0xFFFFFFFFu) continue;
        if (fighter_banks_index(fe) >= 0) {
            /* a bank: its bound numbered fighter's own voice, through the bank's voice number */
            const FighterBanksEntry *e = fighter_banks_for_fe(fe);
            int b = fighter_banks_index(fe);
            if (!e || (int)MEM32(row) != e->core_id) continue;
            voice = bank_voice_bind(b, e->id);
            if (voice < 0) {
                if (s_logged[s] != (uint32_t)e->id)
                    fprintf(stderr, "[VOICE] slot %d bank %d (fighter %d %s): no own voice; the template's voice %03u\n",
                            s, fe, e->id, e->name, was);
                s_logged[s] = (uint32_t)e->id;
                continue;
            }
            MEM32(row + SPEECH_VOICE) = (uint32_t)voice;
            s_logged[s] = (uint32_t)e->id;
            fprintf(stderr, "[VOICE] slot %d bank %d (fighter %d %s): own voice as %03d (the template's was %03u)\n",
                    s, fe, e->id, e->name, voice, was);
            continue;
        }
        if (!new_fighter_is_place(fe)) continue;
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

int new_fighters_style_bits(int fe_id)
{
    if (g_new_count < 0) load();
    return fe_id >= 0 && fe_id < 256 ? g_style_bits[fe_id] : -1;
}

int new_fighter_row(uint32_t row)
{
    if (g_new_count < 0) load();
    return row < sizeof(g_new_row) && g_new_row[row];
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
#define SHELF_MAX 30            /* community fighters beyond the places in the grid (the shelf); the rest by MORE */
#define SHELF_TEST_MAX 160      /* RECOMP_TEST_SHELF's limit (LIST_MAX: 75 stock + 160 + MORE fit) */
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

/* A cell of its own for front-end ID fe_id made from its template's string (tpl) with the row words' stat bars
 * (U, L, S, T, H, C = words 17, 16, 22, 25, 12, 2), one outfit, unlocked. 0 when the template has no string. */
static int cell_from_template(char raw[][TEXT_MAX], int n, int tpl, int fe_id, const uint32_t *w, char *add)
{
    int i;
    add[0] = 0;
    for (i = 0; i < n; i++)
        if (field(raw[i], "FE_ID") == tpl) { snprintf(add, TEXT_MAX, "%s", raw[i]); break; }
    if (!add[0]) return 0;
    set_field(add, "FE_ID", fe_id);
    set_field(add, "U", (int)w[17]);
    set_field(add, "L", (int)w[16]);
    set_field(add, "S", (int)w[22]);    /* the first S= is the speed bar, a later one a style */
    set_field(add, "T", (int)w[25]);
    set_field(add, "H", (int)w[12]);
    set_field(add, "C", (int)w[2]);
    set_field(add, "CLOTH", 1);
    set_field(add, "LOCK", 1);
    return 1;
}

/* ---- the shelf: community fighters beyond the places (fighter_banks.h) ----
 *
 * After the place fighters the grid shows up to SHELF_MAX of them, each a cell of its own with its logical ID
 * (1000 and up; its pictures are composed under that ID): the ones picked last in this session first, then the
 * rest in ID order. One more cell, MORE (NF_MORE_ID), opens the paged list of all of them (the community window,
 * online_screen.cpp) for the slot that picked it. Nothing is saved. */
static int g_recent[SHELF_MAX], g_recent_n;

void new_fighters_shelf_played(int id)
{
    int i, k;
    if (id < FB_FIRST_LOGICAL || id == NF_MORE_ID) return;
    for (i = 0; i < g_recent_n && g_recent[i] != id; i++) {}
    if (i == g_recent_n && g_recent_n < SHELF_MAX) g_recent_n++;
    if (i >= SHELF_MAX) i = SHELF_MAX - 1;
    for (k = i; k > 0; k--) g_recent[k] = g_recent[k - 1];
    g_recent[0] = id;
}

int new_fighters_shelf(int *ids, int max)
{
    int n = 0, i, k, count = fighter_banks_count();
    for (i = 0; i < g_recent_n && n < max; i++)
        if (fighter_banks_find(g_recent[i])) ids[n++] = g_recent[i];
    for (i = 0; i < count && n < max; i++) {
        const FighterBanksEntry *e = fighter_banks_at(i);
        for (k = 0; k < n && ids[k] != e->id; k++) {}
        if (k == n) ids[n++] = e->id;
    }
    return n;
}

static void rebuild_list(void)
{
    char raw[LIST_MAX][TEXT_MAX];
    int n = 0, i, k, place, fe_place, moved = 0;
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
    /* the copy: the stock strings in their order, then every new fighter as a cell of its own (the community
     * row at the end of the grid), by place, unlocked, one outfit; its template is its gameplay basis only */
    g_list_n = 0;
    for (i = 0; i < n && g_list_n < LIST_MAX; i++) {
        int fe = field(raw[i], "FE_ID");
        if (new_fighter_is_place(fe) || fe >= FB_FIRST_LOGICAL) continue;     /* placed below; never a bank's fighter */
        snprintf(g_list[g_list_n++], TEXT_MAX, "%s", raw[i]);
    }
    for (place = 0; place < NEW_FIGHTER_MAX && g_list_n < LIST_MAX; place++) {     /* 100..127, then the low IDs */
        char add[TEXT_MAX] = "";
        fe_place = new_fighter_place_id(place);
        for (k = 0; k < g_new_count && g_new[k].fe_id != fe_place; k++) {}
        if (k >= g_new_count) continue;
        for (i = 0; i < n; i++)
            if (field(raw[i], "FE_ID") == fe_place) { snprintf(add, sizeof(add), "%s", raw[i]); break; }
        if (!add[0]) {
            /* not in the game's own list (it always is today; kept for a list hook): its template's string
             * with this row's ID and stat bars */
            if (!cell_from_template(raw, n, g_new[k].template_fe_id, fe_place, g_words[k], add)) continue;
        }
        set_field(add, "CLOTH", 1);
        set_field(add, "LOCK", 1);
        snprintf(g_list[g_list_n++], TEXT_MAX, "%s", add);
        moved++;
    }
    /* the shelf, then MORE (fighter_banks.h) */
    {
        static int shelf_max = -1;
        int shelf[SHELF_TEST_MAX], m, j;
        if (shelf_max < 0) {
            /* RECOMP_TEST_SHELF=<n> (tests: the menu's capacity with more cells), else SHELF_MAX */
            const char *e = getenv("RECOMP_TEST_SHELF");
            shelf_max = e && atoi(e) > 0 ? (atoi(e) < SHELF_TEST_MAX ? atoi(e) : SHELF_TEST_MAX) : SHELF_MAX;
        }
        m = fighter_banks_count() ? new_fighters_shelf(shelf, shelf_max) : 0;
        for (j = 0; j < m && g_list_n < LIST_MAX - 1; j++) {
            const FighterBanksEntry *e = fighter_banks_find(shelf[j]);
            char add[TEXT_MAX];
            if (e && cell_from_template(raw, n, e->template_fe_id, e->id, e->words, add))
                snprintf(g_list[g_list_n++], TEXT_MAX, "%s", add);
        }
        if (fighter_banks_count() > m) {
            char add[TEXT_MAX];
            const FighterBanksEntry *e = fighter_banks_at(0);
            if (cell_from_template(raw, n, e->template_fe_id, NF_MORE_ID, e->words, add))
                snprintf(g_list[g_list_n++], TEXT_MAX, "%s", add);
        }
        if (m) {
            static int told_shelf;
            if (told_shelf++ < 6)
                fprintf(stderr, "[BANKS] select grid: %d of %d community fighters beyond the places on the shelf%s\n",
                        m, fighter_banks_count(), fighter_banks_count() > m ? ", the rest under MORE" : "");
        }
    }
    g_list_pos = 0;
    g_serving = 1;
    {
        static int told;
        if (told++ < 6)
            fprintf(stderr, "[NEWFIGHTER] fighter list: %d strings from the game, %d answered, %d new fighters in cells of their own\n",
                    n, g_list_n, moved);
        if (told < 3)
            for (i = 0; i < g_list_n; i++)
                for (k = 0; k < g_new_count; k++)
                    if (field(g_list[i], "FE_ID") == g_new[k].fe_id)
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
    fighter_banks_call_done(text);         /* the end of a select list's build (fighter_banks.h) */
    fighter_skins_after_funccall(text);    /* build 85: a pick of a fighter with skins: the skin window */
    if (strcmp(text, "Game.ResetFighterList()")) return;
    if (new_fighters_list(NULL) <= 0 && fighter_banks_count() <= 0) return;
    rebuild_list();
}

/* ---- the Unlock Rewards screen (battle/unlockchar) ----
 *
 * Its handlers write their answer into the front end's result text at 0x341768, 0x600 bytes, the 0x341D68 object
 * and the call machinery's own state right after it (sub_0005F7B0 clears it, sub_0005F810 appends "%s&%s=%d" with
 * no bound). Unlock.GetCharacterList (sub_00054420) lists every Character row but the Hero's,
 * "Count=74&BE=<row id>&FE=<feID>&...", about 19 bytes a fighter: the 74 stock fighters take 1405, so about six
 * new fighters more ran past the end. Then the screen got no list at all (its ActionScript stops after the call, no
 * Unlock.GetCharCostInfo follows): every cell kept the grid's placeholder picture and the panel its authoring text
 * (the bald fighter, COST 100, YOU HAVE 100), and nothing could be bought (trollizas, build 85, 2026-11-16;
 * logs/dj/20261116-170145-unlockc). A new fighter is unlocked by its own select cell (LOCK=1, above) and has no
 * picture in the screen's thumball.swf, so it has no place in this shop: its entries are taken out of the answer
 * and whatever ran past the buffer is put back. Unlock.GetGlobalCharUnlockInfo (sub_000546B0) counts the same
 * rows and the fighter set's ids; it gives "UNLOCKED n / total" for the stock fighters only. */
#define FE_RESULT 0x00341768u
#define CHARACTER_TABLE_INDEX 7u       /* an id is table << 24 | row; 0x800000 is the element's own flag */

extern void sub_00054420(void);
extern void sub_000546B0(void);

static size_t fe_result_read(char *out, size_t cap)
{
    size_t n = 0;
    while (n + 1 < cap && (out[n] = (char)MEM8(FE_RESULT + (uint32_t)n)) != 0) n++;
    out[n] = 0;
    return n;
}

static void fe_result_write(const char *text)
{
    size_t k, n = strlen(text);
    for (k = 0; k <= n; k++) MEM8(FE_RESULT + (uint32_t)k) = (uint8_t)text[k];
}

void new_fighters_unlock_list(void)
{
    static uint8_t spill[FE_RESULT_SPILL];
    static int told;
    char text[FE_RESULT_SIZE + FE_RESULT_SPILL], out[FE_RESULT_SIZE];
    size_t len, k, over;
    int kept, left_out;
    for (k = 0; k < FE_RESULT_SPILL; k++) spill[k] = MEM8(FE_RESULT + FE_RESULT_SIZE + (uint32_t)k);
    sub_00054420();
    if (g_eax != FE_RESULT) return;
    len = fe_result_read(text, sizeof(text));
    over = len + 1 > FE_RESULT_SIZE ? len + 1 - FE_RESULT_SIZE : 0;
    if (!over && new_fighters_list(NULL) <= 0 && fighter_banks_count() <= 0) return;   /* banks: 124..127 rows */
    kept = unlock_list_filter(text, out, sizeof(out), &left_out);
    if (left_out || over) {
        fe_result_write(out);
        for (k = 0; k < over && k < FE_RESULT_SPILL; k++) MEM8(FE_RESULT + FE_RESULT_SIZE + (uint32_t)k) = spill[k];
        if (told++ < 4)
            fprintf(stderr, "[UNLOCK] Unlock Fighters: %d fighters listed, %d new fighter(s) left out (the game's list "
                            "was %u bytes, the result holds %u%s)\n", kept, left_out, (unsigned)len + 1,
                    FE_RESULT_SIZE, over ? "; what ran past it was put back" : "");
    }
}

void new_fighters_unlock_info(void)
{
    uint32_t self = g_ecx, set, ids, count, i;
    int unlocked, total, new_rows = 0, new_ids = 0;
    char text[96];
    sub_000546B0();
    if (g_eax != FE_RESULT || (new_fighters_list(NULL) <= 0 && fighter_banks_count() <= 0) || self < 0x10000u) return;
    fe_result_read(text, sizeof(text));
    if (sscanf(text, "unlockedChars=%d&total=%d", &unlocked, &total) != 2) return;
    for (i = 0; i < sizeof(g_new_row); i++) new_rows += g_new_row[i];
    set = MEM32(MEM32(self + 0xC0Cu) + 4u) + 4u + 0x1Cu;    /* as the handler reads it: {cap, count, saved, ids} */
    count = MEM32(set + 4u);
    ids = MEM32(set + 0xCu);
    for (i = 0; ids && i < count && i < 256u; i++) {
        uint32_t id = MEM32(ids + 4u * i);
        if (id >> 24 == CHARACTER_TABLE_INDEX && new_fighter_row(id & 0x7FFFFFu)) new_ids++;
    }
    if (!new_rows && !new_ids) return;
    snprintf(text, sizeof(text), "unlockedChars=%d&total=%d", unlocked - new_ids < 0 ? 0 : unlocked - new_ids,
             total - new_rows);
    fe_result_write(text);
}

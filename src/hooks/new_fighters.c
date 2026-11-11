/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* New fighters as extra outfits of a stock fighter's select cell. See new_fighters.h and
 * docs/research/new-fighters.md.
 *
 * The select screens (battle/chooseF2/3/4) build their grid from Game.ResetFighterList() and then one
 * Game.GetNextFighter() string per outfit, "CLOTH=<outfits of the cell>&FE_ID=<id>&LOCK=<1 unlocked>&U=..",
 * until "NO_RETURN". ResetFighterList (sub_00047BC0) lists the Character rows whose word 13 is 1, sorts them,
 * and groups Blaze 2/3, Sticky 2 and D-Mob 2/3 under their cells by hard-coded IDs. A new row (feID 100..254)
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

unsigned int defjam_fe_call(const char *command);                   /* recomp_manual.c */
FILE *netplay_open_fight_file(const char *name, int *overridden);   /* netplay.c: as the print reads it */

#define CHARACTER_TABLE 0x4CE5B1B2u
#define ROW_WORDS 26
#define W_DISPLAY 5
#define W_FE_ID 8
#define W_CORE_ID 10
#define W_MAIN 13

static NewFighter g_new[NEW_FIGHTER_MAX];
static uint32_t g_row_off[NEW_FIGHTER_MAX];             /* the row's file offset in djv2.sod */
static uint32_t g_words[NEW_FIGHTER_MAX][ROW_WORDS];    /* its words (stats, style bits) */
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
                    g_row_off[g_new_count - 1] = row;
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
    for (i = 0; i < g_new_count; i++)
        fprintf(stderr, "[NEWFIGHTER] feID %d \"%s\": coreID %d, an outfit of feID %d\n", g_new[i].fe_id,
                g_new[i].name, g_new[i].core_id, g_new[i].template_fe_id);
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
            /* hidden from the game's own list (new_fighters_reset_list): the template's string with this row's
             * ID and stat bars (U, L, S, T, H, C = words 17, 16, 22, 25, 12, 2); the styles are the template's */
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

/* ---- more new fighters than the game's own list holds ----
 *
 * ResetFighterList keeps its entries in a global array of 128 (0x3A07B0, no bound check; the next global is
 * 0x3A09B0), 74 of them stock. So the game never lists the new rows: while it runs, each new row with a template
 * says feID 98 in the loaded database, the E3 Hero's, which the list skips, and gets its own back right after.
 * The select screens then see the new fighters only through this module's strings, and the feID can be any
 * byte from 100 up (the grid thumbnail is the template cell's). The rows are in the database as the game loaded
 * it (it keeps the file as read and adds its base to the offsets): found once by the first new row's words. */
static uint32_t g_base;     /* guest address of the loaded djv2.sod, 0: not found (yet) */

static uint32_t find_loaded_database(void)
{
#if defined(_WIN32)
    uintptr_t lo = XBOX_PTR(0), hi = XBOX_PTR(0x04000000u), p = lo;
    MEMORY_BASIC_INFORMATION mi;
    while (p < hi && VirtualQuery((LPCVOID)p, &mi, sizeof(mi))) {
        uintptr_t start = (uintptr_t)mi.BaseAddress, end = start + mi.RegionSize;
        if (mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && end > start) {
            uintptr_t q;
            /* the first new row's words (string indices and numbers only, so as in the file) */
            for (q = start < lo ? lo : start; q + 4 * ROW_WORDS <= end && q + 4 * ROW_WORDS <= hi; q += 4)
                if (*(const uint32_t *)q == g_words[0][0] && !memcmp((const void *)q, g_words[0], 4 * ROW_WORDS)) {
                    uint32_t row = (uint32_t)(q - lo), a = row - g_row_off[0], i;
                    /* the other rows must be where the file has them, from the same base */
                    for (i = 1; i < (uint32_t)g_new_count; i++)
                        if (MEM32(a + g_row_off[i] + 4u * W_FE_ID) != g_words[i][W_FE_ID]) break;
                    if (i == (uint32_t)g_new_count) return a;
                }
        }
        p = end;
    }
#endif
    return 0;
}

extern void sub_00047BC0(void);

void new_fighters_reset_list(void)
{
    int k;
    if (new_fighters_list(NULL) <= 0) { sub_00047BC0(); return; }
    if (!g_base) {
        static int tries;       /* a 64 MB scan: a few times at most (not found: the game lists the rows) */
        if (tries < 4) {
            tries++;
            g_base = find_loaded_database();
            fprintf(stderr, "[NEWFIGHTER] the loaded database is at %08X\n", g_base);
        }
    }
    if (g_base && memcmp((const void *)XBOX_PTR(g_base + g_row_off[0]), g_words[0], 4 * ROW_WORDS))
        g_base = 0;         /* reloaded elsewhere: found again next time */
    if (g_base)
        for (k = 0; k < g_new_count; k++)
            if (g_new[k].template_fe_id >= 0) MEM32(g_base + g_row_off[k] + 4u * W_FE_ID) = 98u;
    sub_00047BC0();
    if (g_base)
        for (k = 0; k < g_new_count; k++) MEM32(g_base + g_row_off[k] + 4u * W_FE_ID) = (uint32_t)g_new[k].fe_id;
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

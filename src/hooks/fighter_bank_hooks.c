/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The game side of the fighter banks (fighter_banks.h): the side file the game reads with its database, the
 * slots' bindings, the set-up command and the record lookup. */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "fighter_banks.h"
#include "../patches/online_guard.h"
#include "new_fighters.h"

FILE *netplay_open_fight_file(const char *name, int *overridden);   /* netplay.c: as the print reads it */
extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

static FighterBanksEntry *g_fb;
static int g_fb_n = -1;
static uint32_t g_bank_off[FB_BANKS];
static int g_ready;
static int g_bound[FB_BANKS] = { -1, -1, -1, -1 };
static uint32_t g_checked_base;     /* the loaded database whose bank rows were checked */
static volatile int g_window_req, g_window_slot;
static int g_listing;               /* Game.ResetFighterList runs: the bank rows read as themselves */
static volatile int g_online_lineup;    /* build 86: online, the match being set up (automatch) names community
                                         * fighters: START carried their numbers (CAPS_UNLIMITED), every PC of the
                                         * match binds the same banks; kept until the next set-up */

void fighter_banks_online_lineup(int on)
{
    if (on != g_online_lineup)
        fprintf(stderr, "[BANKS] online lineup %s\n", on ? "with community fighters: banks bind as offline"
                                                          : "without community fighters");
    g_online_lineup = on;
}

/* A bank may not be bound or redirected now: this PC is online and the match's lineup is not one START gave
 * community fighters (fighter_banks_online_lineup). */
static int locked(void) { return ffny_online_locked() && !g_online_lineup; }

/* RECOMP_BANKS_OFF=answers,movies (tests): leave that part out. */
static int part_off(const char *part)
{
    const char *e = getenv("RECOMP_BANKS_OFF");
    return e && strstr(e, part) != NULL;
}

static uint8_t *read_all(const char *name, size_t *size, int *overridden)
{
    FILE *f = netplay_open_fight_file(name, overridden);
    long n;
    uint8_t *d = NULL;
    *size = 0;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0 && n < 64L * 1024 * 1024 && (d = (uint8_t *)malloc((size_t)n)) != NULL) {
        *size = fread(d, 1, (size_t)n, f);
        if (*size != (size_t)n) { free(d); d = NULL; *size = 0; }
    }
    fclose(f);
    return d;
}

static void load(void)
{
    size_t side_size = 0, sod_size = 0;
    int over_side = 0, over_sod = 0;
    uint8_t *side, *sod;
    char why[160] = "";
    g_fb_n = 0;
    side = read_all(FB_SIDE_FILE, &side_size, &over_side);
    if (!side) return;      /* no community fighters beyond the places: nothing to do */
    sod = read_all("assets\\djv2.sod", &sod_size, &over_sod);
    g_ready = fighter_banks_parse(side, side_size, sod, sod_size, &g_fb, &g_fb_n, g_bank_off, why, sizeof why);
    if (!g_ready) g_fb_n = 0;
    fprintf(stderr, "[BANKS] %s: %s%s (database %u bytes)\n", FB_SIDE_FILE,
            g_ready ? "" : "REFUSED, ", g_ready ? "loaded" : why, (unsigned)sod_size);
    if (g_ready)
        fprintf(stderr, "[BANKS] %d community fighters beyond the places (IDs %d..%d), banks 124..127 at %08X %08X "
                "%08X %08X\n", g_fb_n, g_fb_n ? g_fb[0].id : 0, g_fb_n ? g_fb[g_fb_n - 1].id : 0, g_bank_off[0],
                g_bank_off[1], g_bank_off[2], g_bank_off[3]);
    free(side);
    free(sod);
}

int fighter_banks_ready(void)
{
    if (g_fb_n < 0) load();
    return g_ready;
}

int fighter_banks_count(void)
{
    return fighter_banks_ready() ? g_fb_n : 0;
}

const FighterBanksEntry *fighter_banks_at(int i)
{
    return fighter_banks_ready() && i >= 0 && i < g_fb_n ? &g_fb[i] : NULL;
}

static int find_index(int id)
{
    int lo = 0, hi;
    if (!fighter_banks_ready()) return -1;
    hi = g_fb_n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (g_fb[mid].id == id) return mid;
        if (g_fb[mid].id < id) lo = mid + 1; else hi = mid;
    }
    return -1;
}

const FighterBanksEntry *fighter_banks_find(int id)
{
    int i = find_index(id);
    return i >= 0 ? &g_fb[i] : NULL;
}

const FighterBanksEntry *fighter_banks_bound(int slot)
{
    if (!fighter_banks_ready() || slot < 0 || slot >= FB_BANKS || g_bound[slot] < 0) return NULL;
    return &g_fb[g_bound[slot]];
}

const FighterBanksEntry *fighter_banks_for_fe(int fe_id)
{
    int b = fighter_banks_index(fe_id);
    if (!fighter_banks_ready()) return NULL;
    if (b >= 0) return fighter_banks_bound(b);
    return fe_id >= FB_FIRST_LOGICAL ? fighter_banks_find(fe_id) : NULL;
}

int fighter_banks_before_call(const char *command, char *out, size_t out_size)
{
    int slot = -1, id = -1, idx, replacement;
    const FighterBanksEntry *e;
    /* the select list is built from the rows themselves: a bound bank must not come out as its fighter there
     * (until the next command, or fighter_banks_call_done) */
    g_listing = !strcmp(command, "Game.ResetFighterList()");
    if (strncmp(command, "Controller.ControllerSetup(", 27)) return 0;
    fighter_banks_rewrite_setup(command, -1, out, out_size, &slot, &id);
    if (slot < 0 || slot >= FB_BANKS) return 0;
    if (id < FB_FIRST_LOGICAL) {
        if (g_bound[slot] >= 0 && fighter_banks_index(id) != slot) {
            fprintf(stderr, "[BANKS] slot %d: feID %d, bank %d released (was %d \"%s\")\n", slot, id,
                    FB_FIRST_BANK + slot, g_fb[g_bound[slot]].id, g_fb[g_bound[slot]].name);
            g_bound[slot] = -1;
        }
        return 0;
    }
    idx = find_index(id);
    if (id == FB_MORE_ID && g_ready && g_fb_n > 0 && !ffny_online_locked()) {
        /* MORE: the slot holds the first fighter not on the shelf until the community window chooses */
        int shelf[32], m = new_fighters_shelf(shelf, 30), i, k;
        for (i = 0, idx = 0; i < g_fb_n; i++) {
            for (k = 0; k < m && shelf[k] != g_fb[i].id; k++) {}
            if (k == m) { idx = i; break; }
        }
        g_window_slot = slot;
        g_window_req = 1;
        fprintf(stderr, "[BANKS] slot %d: MORE, the community window opens (%d fighters)\n", slot, g_fb_n);
        id = g_fb[idx].id;
    }
    e = idx >= 0 ? &g_fb[idx] : NULL;
    if (!e) {
        replacement = 55;
        fprintf(stderr, "[BANKS] slot %d: no community fighter %d here (%s); the slot plays Doc\n", slot, id,
                g_ready ? "not installed" : "no fighter side file");
    } else if (locked()) {
        replacement = e->template_fe_id;
        fprintf(stderr, "[BANKS] slot %d: community fighter %d \"%s\" is offline only; online the slot plays its "
                "template feID %d\n", slot, id, e->name, replacement);
        g_bound[slot] = -1;
    } else {
        replacement = FB_FIRST_BANK + slot;
        if (g_bound[slot] != idx)
            fprintf(stderr, "[BANKS] slot %d: community fighter %d \"%s\" (model %s, coreID %d, template %d) bound to "
                    "bank %d%s\n", slot, id, e->name, e->model, e->core_id, e->template_fe_id, replacement,
                    g_bound[slot] >= 0 ? " (rebound)" : "");
        g_bound[slot] = idx;
        new_fighters_shelf_played(e->id);
    }
    return fighter_banks_rewrite_setup(command, replacement, out, out_size, NULL, NULL);
}

uint32_t fighter_banks_record(uint32_t database, uint32_t record, uint32_t caller)
{
    static unsigned told[FB_BANKS];
    uint32_t base;
    int k;
    if (!g_ready || record < 0x10000u || database < 0x10000u) return record;
    base = MEM32(database);
    if (record < base || record - base > 64u * 1024u * 1024u) return record;
    for (k = 0; k < FB_BANKS; k++) {
        const FighterBanksEntry *e;
        if (g_bound[k] < 0 || record - base != g_bank_off[k]) continue;
        if (locked() || g_listing) return record;
        e = &g_fb[g_bound[k]];
        if (g_checked_base != base) {
            /* the loaded file is the file (the game adds its base to every offset): check it is ours */
            int i, ok = MEM32(base) == 0x42444F53u;
            for (i = 0; ok && i < FB_BANKS; i++) ok = MEM32(base + g_bank_off[i] + 32u) == (uint32_t)(FB_FIRST_BANK + i);
            if (!ok) {
                fprintf(stderr, "[BANKS] the database at %08X is not the one the side file belongs to; no redirect\n", base);
                return record;
            }
            g_checked_base = base;
        }
        if (MEM32(base + e->offset + 32u) != (uint32_t)e->id || MEM32(base + e->offset + 40u) != (uint32_t)e->core_id) {
            fprintf(stderr, "[BANKS] record of %d changed in memory; bank %d left as it is\n", e->id, FB_FIRST_BANK + k);
            return record;
        }
        if (told[k] < 6 || (told[k] & (told[k] - 1)) == 0)
            fprintf(stderr, "[BANKS] bank %d row -> record of %d \"%s\" (lookup %u, caller %08X)\n", FB_FIRST_BANK + k,
                    e->id, e->name, told[k] + 1, caller);
        told[k]++;
        return base + e->offset;
    }
    return record;
}

int fighter_banks_take_window(int *slot)
{
    if (!g_window_req) return 0;
    g_window_req = 0;
    if (slot) *slot = g_window_slot;
    return 1;
}

int fighter_banks_bind(int slot, int id)
{
    int idx = find_index(id);
    if (slot < 0 || slot >= FB_BANKS || idx < 0 || ffny_online_locked()) return 0;
    if (g_bound[slot] != idx)
        fprintf(stderr, "[BANKS] slot %d: community fighter %d \"%s\" bound to bank %d (community window)\n", slot,
                id, g_fb[idx].name, FB_FIRST_BANK + slot);
    g_bound[slot] = idx;
    new_fighters_shelf_played(id);
    return 1;
}

int fighter_banks_after_call(const char *command, const char *answer, char *out, size_t out_size)
{
    /* The game's answers keep the bank's ID. Answered with a fighter's own ID (1000 and up) the screens stopped:
     * Game.GetMatchSummary(n) right after the summary (logs/dj/20261116-110206-offline) and Game.GetMatchInfo() on
     * the loading screen (oldMLoad, 20261116-115258-unl-sel7: its AS takes the ID as an index). Their pictures are
     * the bank's (the composer gives the banks placeholder pictures), mapped to the fighter's by the movie loader
     * (fighter_banks_movie_load). */
    (void)command; (void)answer; (void)out; (void)out_size;
    return 0;
}

/* ---- the FuncCall hook's two calls (recomp_manual.c funccall_hook: cdecl, the command at g_esp + 4) ---- */

static uint32_t guest_buffer(uint32_t *slot, uint32_t size)
{
    if (!*slot) *slot = xbox_HeapAlloc(size, 16);
    return *slot;
}

static void put_guest(uint32_t buf, const char *text)
{
    uint32_t i;
    for (i = 0; text[i]; i++) MEM8(buf + i) = (uint8_t)text[i];
    MEM8(buf + i) = 0;
}

void fighter_banks_funccall_before(char *text, size_t text_size)
{
    static uint32_t command_buf;
    char bound[192];
    uint32_t buf;
    if (!fighter_banks_before_call(text, bound, sizeof bound) || !(buf = guest_buffer(&command_buf, 256))) return;
    fprintf(stderr, "[FUNCCALL] %s (as %s)\n", text, bound);
    put_guest(buf, bound);
    MEM32(g_esp + 4) = buf;
    snprintf(text, text_size, "%s", bound);
}

void fighter_banks_funccall_after(const char *text)
{
    static uint32_t answer_buf;
    char answer[600], out[600];
    uint32_t a = g_eax, i, buf;
    if (a < 0x10000u || !g_ready || strcmp(text, "Game.GetMatchInfo()"))
        return;
    for (i = 0; i < sizeof answer - 1 && MEM8(a + i); i++) answer[i] = (char)MEM8(a + i);
    answer[i] = 0;
    if (!fighter_banks_after_call(text, answer, out, sizeof out) || !(buf = guest_buffer(&answer_buf, 640))) return;
    put_guest(buf, out);
    g_eax = buf;
}

/* One guest copy per rewritten name, kept for the session (the loader may keep the name it was given). */
static uint32_t name_copy(const char *text)
{
    static struct { char text[96]; uint32_t buf; } cache[64];
    static int used;
    int i;
    for (i = 0; i < used; i++) if (!strcmp(cache[i].text, text)) return cache[i].buf;
    if (used >= 64 || strlen(text) >= sizeof cache[0].text) return 0;
    if (!(cache[used].buf = xbox_HeapAlloc(96, 16))) return 0;
    snprintf(cache[used].text, sizeof cache[used].text, "%s", text);
    put_guest(cache[used].buf, text);
    return cache[used++].buf;
}

void fighter_banks_movie_load(uint32_t name_arg)
{
    static unsigned told;
    char name[160], out[192];
    int map[FB_BANKS], k, any = 0;
    uint32_t p, i, buf;
    if (!g_ready || locked() || part_off("movies")) return;
    for (k = 0; k < FB_BANKS; k++) {
        map[k] = g_bound[k] >= 0 ? g_fb[g_bound[k]].id : -1;
        any |= map[k] >= 0;
    }
    if (!any || (p = MEM32(name_arg)) < 0x10000u) return;
    for (i = 0; i < sizeof name - 1 && MEM8(p + i); i++) name[i] = (char)MEM8(p + i);
    name[i] = 0;
    {
        static int all = -1;
        static unsigned shown;
        if (all < 0) all = getenv("RECOMP_BANKS_LOG_MOVIES") != NULL;
        if (all && shown++ < 200) fprintf(stderr, "[BANKS] movie load: %s\n", name);
    }
    if (!fighter_banks_picture_name(name, map, out, sizeof out) || !(buf = name_copy(out))) return;
    if (told++ < 40) fprintf(stderr, "[BANKS] picture %s loads as %s\n", name, out);
    MEM32(name_arg) = buf;
}

void fighter_banks_call_done(const char *text)
{
    (void)text;
    g_listing = 0;
}

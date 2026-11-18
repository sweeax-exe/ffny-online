/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Community skins: an extra outfit of a stock fighter, without a front-end ID of its own. See fighter_skins.h. */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "fighter_skins.h"
#include "pc_content.h"
#include "guest_call.h"
#include "automatch.h"
#include "netplay.h"

#if defined(_WIN32)
#define SKIN_SEP "\\"
#else
#define SKIN_SEP "/"
#endif

static SRWLOCK g_lock = SRWLOCK_INIT;
static FighterSkin g_skins[PC_SKIN_MAX];
static int g_count = -1;

static const char *data_root(void)
{
    const char *d = getenv("DEFJAM_DATA");
    return d && *d ? d : NULL;
}

void fighter_skins_refresh(void)
{
    static PcContentSkin raw[PC_SKIN_MAX];
    FighterSkin got[PC_SKIN_MAX];
    int n = pc_content_skins(raw, PC_SKIN_MAX), i, k = 0;
    const char *root = data_root();
    for (i = 0; i < n; i++) {
        int j, index = 1;
        FILE *f;
        if (raw[i].template_fe_id < 0 || raw[i].template_fe_id >= 98) continue;
        for (j = 0; j < k; j++) if (got[j].template_fe_id == raw[i].template_fe_id) index++;
        if (index > FIGHTER_SKINS_PER) continue;
        memset(&got[k], 0, sizeof got[k]);
        snprintf(got[k].id, sizeof got[k].id, "%s", raw[i].id);
        got[k].template_fe_id = raw[i].template_fe_id;
        got[k].index = index;
        snprintf(got[k].model, sizeof got[k].model, "%s", raw[i].model);
        snprintf(got[k].name, sizeof got[k].name, "%s", raw[i].name);
        if (root) {
            snprintf(got[k].picture, sizeof got[k].picture, "%s" SKIN_SEP "online-art" SKIN_SEP "skins" SKIN_SEP "%s.png",
                     root, raw[i].id);
            if ((f = fopen(got[k].picture, "rb")) != NULL) fclose(f);
            else got[k].picture[0] = 0;
        }
        k++;
    }
    AcquireSRWLockExclusive(&g_lock);
    if (g_count != k || memcmp(g_skins, got, sizeof(got[0]) * (size_t)k)) {
        for (i = 0; i < k; i++)
            fprintf(stderr, "[SKINS] skin %d of feID %d: %s \"%s\" (%s)%s\n", got[i].index, got[i].template_fe_id,
                    got[i].model, got[i].name, got[i].id, got[i].picture[0] ? "" : ", no picture");
    }
    memcpy(g_skins, got, sizeof(got[0]) * (size_t)k);
    g_count = k;
    ReleaseSRWLockExclusive(&g_lock);
}

static void ensure(void)
{
    int n;
    AcquireSRWLockShared(&g_lock);
    n = g_count;
    ReleaseSRWLockShared(&g_lock);
    if (n < 0) fighter_skins_refresh();
}

int fighter_skins_count(int fe_id)
{
    int i, n = 0;
    ensure();
    AcquireSRWLockShared(&g_lock);
    for (i = 0; i < g_count; i++) if (g_skins[i].template_fe_id == fe_id) n++;
    ReleaseSRWLockShared(&g_lock);
    return n;
}

int fighter_skin_get(int fe_id, int index, FighterSkin *out)
{
    int i, found = 0;
    ensure();
    AcquireSRWLockShared(&g_lock);
    for (i = 0; i < g_count && !found; i++)
        if (g_skins[i].template_fe_id == fe_id && g_skins[i].index == index) {
            if (out) *out = g_skins[i];
            found = 1;
        }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

/* ---- the last skin per fighter: <data>/skin-choices.ini, "fighter.<feID>=<pack id>" ---- */

static int choices_path(char *out, size_t size)
{
    const char *root = data_root();
    if (!root) return 0;
    snprintf(out, size, "%s" SKIN_SEP "skin-choices.ini", root);
    return 1;
}

int fighter_skins_remembered(int fe_id)
{
    char path[1024], line[256], key[32];
    FILE *f;
    int index = 0, i;
    if (fighter_skins_count(fe_id) <= 0 || !choices_path(path, sizeof path) || !(f = fopen(path, "rb"))) return 0;
    snprintf(key, sizeof key, "fighter.%d=", fe_id);
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
        if (strncmp(line, key, strlen(key))) continue;
        index = 0;     /* the last line for the fighter wins; a pack that is gone is Default */
        for (i = 1; i <= FIGHTER_SKINS_PER; i++) {
            FighterSkin s;
            if (fighter_skin_get(fe_id, i, &s) && !strcmp(s.id, line + strlen(key))) index = i;
        }
    }
    fclose(f);
    return index;
}

void fighter_skins_remember(int fe_id, int index)
{
    char path[1024], tmp[1100], line[256], key[32];
    FighterSkin s;
    FILE *f, *o;
    if (fe_id < 0 || fe_id >= 98 || !choices_path(path, sizeof path)) return;
    if (index && !fighter_skin_get(fe_id, index, &s)) index = 0;
    snprintf(key, sizeof key, "fighter.%d=", fe_id);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (!(o = fopen(tmp, "wb"))) return;
    if ((f = fopen(path, "rb")) != NULL) {
        while (fgets(line, sizeof line, f))
            if (strncmp(line, key, strlen(key)) && strchr(line, '=')) fputs(line, o);
        fclose(f);
    } else {
        fputs("; FFNY Online: the skin each fighter was played with last (the skin window opens on it)\n", o);
    }
    fprintf(o, "%s%s\n", key, index ? s.id : "");
    fclose(o);
#if defined(_WIN32)
    MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING);
#else
    rename(tmp, path);
#endif
    fprintf(stderr, "[SKINS] feID %d: %s kept as the last one\n", fe_id, index ? s.name : "Default");
}

/* ---- the match's looks ---- */

static int g_slot_fe[4] = { -1, -1, -1, -1 }, g_slot_skin[4];

void fighter_skins_set_slot(int slot, int fe_id, int skin)
{
    if (slot < 0 || slot > 3) return;
    if (skin < 0 || skin > FIGHTER_SKINS_PER) skin = 0;
    if (g_slot_skin[slot] != skin || (skin && g_slot_fe[slot] != fe_id))
        fprintf(stderr, "[SKINS] slot %d: feID %d, skin %d\n", slot, fe_id, skin);
    g_slot_fe[slot] = fe_id;
    g_slot_skin[slot] = skin;
}

void fighter_skins_clear_slots(void)
{
    int s;
    for (s = 0; s < 4; s++) g_slot_fe[s] = -1, g_slot_skin[s] = 0;
}

int fighter_skins_slot(int slot) { return slot >= 0 && slot < 4 ? g_slot_skin[slot] : 0; }

/* Game.StartGame copies each slot's Character record into the match set-up (sub_00020270, the copies at
 * GS+0x168 + 4 * slot onwards): the model name (row word 14, field 0xC38D6E96) as a string pointer at
 * GS+0x208 + 4 * slot, which the match load reads (sub_0008B440: set-up + 0x148 + 4 * slot, the set-up
 * being GS+0xC0) to load the model, its camera banks and its IntroExtro clips. A skin's slot gets a pointer to
 * the skin's model name instead, in a small block of the game's own heap taken at this start (a new block each
 * time, 64 bytes a match with skins, never written again: no later owner of a block the game may have given back
 * is touched). Everything else of the slot is the template's. */
#define GD_FIGHTER   0x1AAAu         /* + 5 * slot: the fighter byte */
#define GD_MODEL     0x208u          /* + 4 * slot: the model name's pointer */
#define TAG_RUNMODULE 0x0029D4B4u    /* an allocation tag of the game's own (dev_module.c) */
extern void sub_00029680(void);      /* the game's allocator: cdecl (size, tag, flags) */

static int guest_text(uint32_t p, char *out, int size)
{
    int k;
    if (p < 0x10000u) { out[0] = 0; return 0; }
    for (k = 0; k < size - 1; k++) {
        char c = (char)MEM8(p + (uint32_t)k);
        if (!c) break;
        out[k] = c;
    }
    out[k] = 0;
    return k;
}

void fighter_skins_start_game_done(uint32_t gd)
{
    uint32_t names = 0;                 /* 4 x 16 bytes of guest memory: this start's skin model names */
    int s;
    if (gd < 0x10000u) return;
    for (s = 0; s < 4; s++) {
        FighterSkin skin;
        char was[24];
        int fe = (int)MEM8(gd + GD_FIGHTER + 5u * (uint32_t)s), k;
        uint32_t at;
        if (!g_slot_skin[s]) continue;
        if (fe != g_slot_fe[s] || !fighter_skin_get(fe, g_slot_skin[s], &skin)) {
            fprintf(stderr, "[SKINS] slot %d: feID %d has no skin %d here; its own look\n", s, fe, g_slot_skin[s]);
            continue;
        }
        guest_text(MEM32(gd + GD_MODEL + 4u * (uint32_t)s), was, sizeof was);
        if (strncmp(was, "V2IP_", 5) || strlen(was) != strlen(skin.model)) {
            fprintf(stderr, "[SKINS] slot %d: the set-up's model is \"%s\", not a stock model; its own look\n", s, was);
            continue;
        }
        if (!names) {
            uint32_t args[3];
            args[0] = 64; args[1] = TAG_RUNMODULE; args[2] = 0;
            names = guest_call(sub_00029680, 0, 3, args);
            if (names < 0x10000u) {
                fprintf(stderr, "[SKINS] no guest memory for the skins' model names; the stock looks\n");
                return;
            }
        }
        at = names + 16u * (uint32_t)s;
        for (k = 0; k < 16; k++) MEM8(at + (uint32_t)k) = (uint8_t)(k < (int)strlen(skin.model) ? skin.model[k] : 0);
        MEM32(gd + GD_MODEL + 4u * (uint32_t)s) = at;
        fprintf(stderr, "[SKINS] slot %d feID %d: skin %d \"%s\", model %s for %s\n", s, fe, skin.index, skin.name,
                skin.model, was);
    }
}

/* ---- the select screens ---- */

static volatile LONG g_window_req;
static int g_window_slot, g_window_fe;

/* The Team screen (chsTeam4): Controller.ControllerSelect(thumb, pad, fe + 1, position), one a player (a community
 * fighter's already as its bank, fighter_bank_hooks.c). With the fourth the game rewrites the four set-up entries in
 * position order (sub_00055A20), so slot = position from then on: the looks follow their players there. */
static struct { int pos, src; } g_picks[4];
static int g_pick_n;

static void team_pick(const char *text)
{
    int thumb, pad, fe, pos, k, src = -1, used = 0, fe2[4], skin2[4];
    if (sscanf(text + 28, " %d , %d , %d , %d )%n", &thumb, &pad, &fe, &pos, &used) != 4 || !used) return;
    fe--;
    if (thumb == -1) { if (g_pick_n > 0) g_pick_n--; return; }
    if (thumb < 0) { g_pick_n = 0; return; }
    if (g_pick_n >= 4) g_pick_n = 0;
    /* the pick's set-up slot: its own thumb if that slot holds the fighter, else the first other one not taken */
    if (thumb <= 3 && g_slot_fe[thumb] == fe) src = thumb;
    for (k = 0; src < 0 && k < 4; k++) {
        int j;
        if (g_slot_fe[k] != fe) continue;
        for (j = 0; j < g_pick_n && g_picks[j].src != k; j++) {}
        if (j == g_pick_n) src = k;
    }
    g_picks[g_pick_n].pos = pos;
    g_picks[g_pick_n].src = src;
    if (++g_pick_n < 4) return;
    g_pick_n = 0;
    for (k = 0; k < 4; k++) fe2[k] = -1, skin2[k] = 0;
    for (k = 0; k < 4; k++) {
        int p = g_picks[k].pos, s = g_picks[k].src;
        if (p < 0 || p > 3) continue;
        if (s >= 0) fe2[p] = g_slot_fe[s], skin2[p] = g_slot_skin[s];
    }
    for (k = 0; k < 4; k++) {
        if (skin2[k] != g_slot_skin[k] || fe2[k] != g_slot_fe[k])
            fprintf(stderr, "[SKINS] team order: slot %d now feID %d, skin %d\n", k, fe2[k], skin2[k]);
        g_slot_fe[k] = fe2[k];
        g_slot_skin[k] = skin2[k];
    }
}

void fighter_skins_after_funccall(const char *text)
{
    int slot, pad, fe, outfit;
    if (!strncmp(text, "Controller.ControllerSelect(", 28)) {
        team_pick(text);
        return;
    }
    if (strncmp(text, "Controller.ControllerSetup(", 27)) return;
    if (!strncmp(text, "Controller.ControllerSetup(-1,", 30)) g_pick_n = 0;
    if (sscanf(text + 27, "%d,%d,%d,%d", &slot, &pad, &fe, &outfit) != 4) return;
    if (slot < 0) {
        fighter_skins_clear_slots();
        return;
    }
    if (slot > 3) return;
    fighter_skins_set_slot(slot, fe, 0);
    /* offline, a player's pick (a pad, not the CPU) of a fighter with skins: the skin window */
    if (pad < 0 || netplay_active() || automatch_pending()) return;
    fighter_skins_refresh();
    if (fighter_skins_count(fe) <= 0) return;
    g_window_slot = slot;
    g_window_fe = fe;
    InterlockedExchange(&g_window_req, 1);
    fprintf(stderr, "[SKINS] slot %d picked feID %d: %d skin(s), the skin window opens\n", slot, fe,
            fighter_skins_count(fe));
}

int fighter_skins_take_window(int *slot, int *fe_id)
{
    if (!InterlockedExchange(&g_window_req, 0)) return 0;
    if (slot) *slot = g_window_slot;
    if (fe_id) *fe_id = g_window_fe;
    return 1;
}

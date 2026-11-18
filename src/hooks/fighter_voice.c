/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* A new fighter's own voice: fighter_voice.h. No game state, no host state. */
#include "fighter_voice.h"
#include <stdio.h>
#include <string.h>

static const char *const k_entry[FV_FILES] = {
    "speech\\Character\\Def_Jam_Characters\\%03d.hdr", "speech\\Character\\Def_Jam_Characters\\%03d.dat",
    "speech\\Taunts\\Def_Jam_Characters\\%03d.hdr", "speech\\Taunts\\Def_Jam_Characters\\%03d.dat",
    "speech\\Enviro\\Def_Jam_Characters\\%03d.hdr", "speech\\Enviro\\Def_Jam_Characters\\%03d.dat",
    "speech\\streams\\intro\\%03d.asf", "speech\\streams\\extro\\%03d.asf",
};

int fighter_voice_number(int fe_id)
{
    int place = new_fighter_place(fe_id);
    return place < 0 ? -1 : FV_FIRST_VOICE + place;
}

void fighter_voice_entry_name(int voice, int i, char *out, size_t n)
{
    if (!n) return;
    if (i < 0 || i >= FV_FILES) { out[0] = 0; return; }
    snprintf(out, n, k_entry[i], voice);
}

static int fold(int c)
{
    if (c == '/') return '\\';
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

int fighter_voice_entries_present(const uint8_t *dir, size_t n, int voice)
{
    char want[FV_FILES][64];
    int found[FV_FILES] = {0}, i, all = 1;
    uint32_t count, k;
    size_t pos = 16;
    if (!dir || n < 16 || dir[0] != 'B' || dir[1] != 'I' || dir[2] != 'G') return 0;
    if (voice < 0 || voice > 999) return 0;
    for (i = 0; i < FV_FILES; i++) fighter_voice_entry_name(voice, i, want[i], sizeof(want[i]));
    count = be32(dir + 8);
    for (k = 0; k < count; k++) {
        uint32_t size;
        size_t name;
        if (pos + 9 > n) return 0;
        size = be32(dir + pos + 4);
        name = pos + 8;
        pos = name;
        while (pos < n && dir[pos]) pos++;
        if (pos >= n) return 0;
        for (i = 0; i < FV_FILES; i++) {
            const char *w = want[i];
            size_t j = 0;
            if (found[i]) continue;
            while (w[j] && name + j < pos && fold(dir[name + j]) == fold((unsigned char)w[j])) j++;
            if (!w[j] && name + j == pos && size > 0) found[i] = 1;
        }
        pos++;
    }
    for (i = 0; i < FV_FILES; i++) all &= found[i];
    return all;
}

/* ---- own voices of numbered fighters (fighter_voice.h) ---- */

int fighter_voice_bank_voice(int bank)
{
    return bank >= 0 && bank < 4 ? FV_BANK_FIRST_VOICE + bank : -1;
}

void fighter_voice_numbered_entry_name(int id, int voice, int i, char *out, size_t n)
{
    char base[64], *leaf;
    if (!n) return;
    out[0] = 0;
    if (i < 0 || i >= FV_FILES || id < 0) return;
    fighter_voice_entry_name(voice, i, base, sizeof base);
    leaf = strrchr(base, '\\');
    if (!leaf) return;
    *leaf++ = 0;
    if (i < 6 && !(i & 1))      /* a bank header: one per bank voice */
        snprintf(out, n, "%s\\L%d_%03d.hdr", base, id, voice);
    else
        snprintf(out, n, "%s\\L%d.%s", base, id, i < 6 ? "dat" : "asf");
}

long fighter_voice_dir_find(const uint8_t *dir, size_t n, const char *name, uint32_t *off, uint32_t *size)
{
    uint32_t count, k;
    size_t pos = 16;
    if (!dir || !name || n < 16 || dir[0] != 'B' || dir[1] != 'I' || dir[2] != 'G') return -1;
    count = be32(dir + 8);
    for (k = 0; k < count; k++) {
        size_t rec = pos, nm = pos + 8, j = 0;
        if (pos + 9 > n) return -1;
        pos = nm;
        while (pos < n && dir[pos]) pos++;
        if (pos >= n) return -1;
        while (name[j] && nm + j < pos && fold(dir[nm + j]) == fold((unsigned char)name[j])) j++;
        if (!name[j] && nm + j == pos) {
            if (off) *off = be32(dir + rec);
            if (size) *size = be32(dir + rec + 4);
            return (long)rec;
        }
        pos++;
    }
    return -1;
}

int fighter_voice_numbered_plan(const uint8_t *dir, size_t n, int voice, int id, long pos[FV_FILES],
                                uint32_t off[FV_FILES], uint32_t size[FV_FILES])
{
    char name[96];
    int i;
    if (voice < FV_BANK_FIRST_VOICE || voice >= FV_BANK_FIRST_VOICE + 4 || id < 1000) return 0;
    for (i = 0; i < FV_FILES; i++) {
        uint32_t o = 0, s = 0;
        fighter_voice_entry_name(voice, i, name, sizeof name);
        pos[i] = fighter_voice_dir_find(dir, n, name, &o, &s);
        if (pos[i] < 0 || !s) return 0;
        fighter_voice_numbered_entry_name(id, voice, i, name, sizeof name);
        if (fighter_voice_dir_find(dir, n, name, &off[i], &size[i]) < 0 || !size[i]) return 0;
    }
    return 1;
}

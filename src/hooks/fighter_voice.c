/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* A new fighter's own voice: fighter_voice.h. No game state, no host state. */
#include "fighter_voice.h"
#include <stdio.h>

static const char *const k_entry[FV_FILES] = {
    "speech\\Character\\Def_Jam_Characters\\%03d.hdr", "speech\\Character\\Def_Jam_Characters\\%03d.dat",
    "speech\\Taunts\\Def_Jam_Characters\\%03d.hdr", "speech\\Taunts\\Def_Jam_Characters\\%03d.dat",
    "speech\\Enviro\\Def_Jam_Characters\\%03d.hdr", "speech\\Enviro\\Def_Jam_Characters\\%03d.dat",
    "speech\\streams\\intro\\%03d.asf", "speech\\streams\\extro\\%03d.asf",
};

int fighter_voice_number(int fe_id)
{
    if (fe_id < FV_FIRST_FE_ID || fe_id > FV_LAST_FE_ID) return -1;
    return FV_FIRST_VOICE + (fe_id - FV_FIRST_FE_ID);
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

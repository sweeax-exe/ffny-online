/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The pure parts of the fighter banks (fighter_banks.h): the side file and the set-up command. No game or host
 * state; the game side is fighter_bank_hooks.c. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "fighter_banks.h"

#define CHARACTER_TABLE 0x4CE5B1B2u
#define W_FE_ID 8
#define W_CORE_ID 10
#define HEADER_SIZE 52u
#define ROW_SIZE 72u
#define MAX_COUNT 16384u

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 0x811C9DC5u;
    size_t i;
    for (i = 0; i < n; i++) h = (h ^ p[i]) * 0x01000193u;
    return h;
}

static int fail(char *why, size_t why_size, const char *text)
{
    if (why && why_size) snprintf(why, why_size, "%s", text);
    return 0;
}

/* The row offsets of the Character table's first class block (row ID order), as setup/ffnycontent/newfighter.py
 * characters() reads them; 0 when the table is not there. */
static uint32_t character_rows(const uint8_t *d, size_t size, uint32_t *count_out)
{
    uint32_t tables, nt, t;
    if (size < 0x20 || memcmp(d, "SODB", 4)) return 0;
    tables = rd32(d + 0x10);
    if ((size_t)tables + 12 > size) return 0;
    nt = rd32(d + tables + 4);
    if ((size_t)tables + 12 + 8 * (size_t)nt > size) return 0;
    for (t = 0; t < nt; t++) {
        uint32_t off, ncls, block, count, stride;
        if (rd32(d + tables + 12 + 4 * t) != CHARACTER_TABLE) continue;
        off = rd32(d + tables + 12 + 4 * nt + 4 * t);
        if ((size_t)off + 0x18 > size) return 0;
        ncls = rd32(d + off + 0x10);
        if (!ncls || (size_t)off + 0x18 + 8 * (size_t)ncls > size) return 0;
        block = rd32(d + off + 0x18 + 4 * ncls);
        if ((size_t)block + 12 > size) return 0;
        count = rd32(d + block + 4);
        stride = rd32(d + block + 8);
        if (!stride || (size_t)block + 12 + 4 * (size_t)count * (1 + stride) > size) return 0;
        *count_out = count;
        return block;
    }
    return 0;
}

int fighter_banks_parse(const uint8_t *side, size_t side_size, const uint8_t *sod, size_t sod_size,
                        FighterBanksEntry **out, int *count, uint32_t bank_offset[FB_BANKS], char *why, size_t why_size)
{
    uint32_t n, i, k, block, rows = 0;
    FighterBanksEntry *e;
    *out = NULL;
    *count = 0;
    if (!side || side_size < HEADER_SIZE || memcmp(side, "FFLF", 4)) return fail(why, why_size, "not a fighter side file");
    n = rd32(side + 8);
    if (rd32(side + 4) != 1 || n > MAX_COUNT || side_size != HEADER_SIZE + (size_t)n * ROW_SIZE)
        return fail(why, why_size, "a side file of another version or size");
    if (!sod || rd32(side + 12) != sod_size || rd32(side + 16) != fnv1a(sod, sod_size))
        return fail(why, why_size, "the side file belongs to another database (size or hash)");
    block = character_rows(sod, sod_size, &rows);
    if (!block) return fail(why, why_size, "the database has no Character table");
    for (k = 0; k < FB_BANKS; k++) {
        uint32_t fe = rd32(side + 20 + 4 * k), off = rd32(side + 36 + 4 * k), r, found = 0;
        if (fe != FB_FIRST_BANK + k || off % 4 || (size_t)off + 4 * FB_ROW_WORDS > sod_size)
            return fail(why, why_size, "a bank row is out of place");
        for (r = 0; r < rows; r++)
            if (rd32(sod + block + 12 + 4 * rows + 4 * r * rd32(sod + block + 8)) == off) found = 1;
        if (!found || rd32(sod + off + 4 * W_FE_ID) != fe) return fail(why, why_size, "a bank row is not in the table");
        bank_offset[k] = off;
    }
    e = n ? (FighterBanksEntry *)calloc(n, sizeof *e) : NULL;
    if (n && !e) return fail(why, why_size, "out of memory");
    for (i = 0; i < n; i++) {
        const uint8_t *p = side + HEADER_SIZE + (size_t)i * ROW_SIZE;
        uint32_t id = rd32(p), off = rd32(p + 4);
        if (id < FB_FIRST_LOGICAL || id > 0x7FFFFFFFu || (i && (int)id <= e[i - 1].id) || off % 4 ||
            (size_t)off + 4 * FB_ROW_WORDS > sod_size || rd32(sod + off + 4 * W_FE_ID) != id ||
            rd32(sod + off + 4 * W_CORE_ID) != rd32(p + 12)) {
            free(e);
            return fail(why, why_size, "a record does not match the database");
        }
        e[i].id = (int)id;
        e[i].offset = off;
        e[i].template_fe_id = (int)rd32(p + 8);
        e[i].core_id = (int)rd32(p + 12);
        memcpy(e[i].name, p + 24, 31);
        e[i].name[31] = 0;
        memcpy(e[i].model, p + 56, 15);
        e[i].model[15] = 0;
        for (k = 0; k < FB_ROW_WORDS; k++) e[i].words[k] = rd32(sod + off + 4 * k);
    }
    *out = e;
    *count = (int)n;
    return 1;
}

int fighter_banks_rewrite_setup(const char *command, int replacement, char *out, size_t out_size, int *slot, int *id)
{
    static const char head[] = "Controller.ControllerSetup(";
    int s, pad, fe, outfit, used = 0;
    if (slot) *slot = -1;
    if (id) *id = -1;
    if (!command || strncmp(command, head, sizeof head - 1)) return 0;
    if (sscanf(command + sizeof head - 1, " %d , %d , %d , %d )%n", &s, &pad, &fe, &outfit, &used) != 4 || !used)
        return 0;
    if (slot) *slot = s;
    if (id) *id = fe;
    if (fe < FB_FIRST_LOGICAL || s < 0 || s >= FB_BANKS || replacement < 0) return 0;
    snprintf(out, out_size, "%s%d,%d,%d,%d)", head, s, pad, replacement, outfit);
    return 1;
}

/* Picture families of a fighter (setup/ffnycontent/newfighter.py PICTURE_FAMILIES): the name's prefix and whether
 * its number is the front-end ID + 1. */
static const struct { const char *prefix; int plus; } k_families[] = {
    { "p_cf_", 1 }, { "p_ml_", 1 }, { "p_ct_", 1 },
    { "l_cf_", 0 }, { "l_ml_", 0 }, { "i_ml_", 0 }, { "l_ms_", 0 }, { "t_ms_", 0 }, { "t_ct_", 0 }, { "t_co_", 0 },
    { "thumb", 0 },
};

int fighter_banks_picture_name(const char *name, const int *bank_to_id, char *out, size_t out_size)
{
    const char *base = name, *p, *num;
    size_t i, pre = 0;
    int plus = -1, n, b;
    char *end;
    for (p = name; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    for (i = 0; i < sizeof k_families / sizeof k_families[0]; i++) {
        size_t k = strlen(k_families[i].prefix), j;
        for (j = 0; j < k && base[j] && tolower((unsigned char)base[j]) == k_families[i].prefix[j]; j++) {}
        if (j == k) { pre = k; plus = k_families[i].plus; break; }
    }
    if (plus < 0) {             /* portrait/<ID + 1>_<players>p */
        if (!isdigit((unsigned char)base[0])) return 0;
        plus = 1;
    }
    num = base + pre;
    if (!isdigit((unsigned char)*num)) return 0;
    n = (int)strtol(num, &end, 10);
    b = n - plus - FB_FIRST_BANK;
    if (b < 0 || b >= FB_BANKS || bank_to_id[b] < FB_FIRST_LOGICAL) return 0;
    if (*end && *end != '_' && *end != '.' && !isalpha((unsigned char)*end)) return 0;
    if (snprintf(out, out_size, "%.*s%d%s", (int)(num - name), name, bank_to_id[b] + plus, end) >= (int)out_size)
        return 0;
    return 1;
}

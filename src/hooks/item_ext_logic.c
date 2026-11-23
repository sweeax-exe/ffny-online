/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* New items without a new save layout: see item_ext_logic.h. Standard C only. */
#include "item_ext_logic.h"

#include <stdlib.h>
#include <string.h>

/* djv2.sod as src/hooks/save_unlock.cpp reads it: names are hashes (h = 0x2B5A5; h = (h + byte) * 33). */
#define SOD_CLOTHING        0x912016DDu
#define SOD_JEWELRY         0x426B8667u
#define SOD_CLOTHING_LIST   0xAC3E66A4u     /* a clothing row: one item per colour (sub_000151C0) */
#define SOD_JEWELRY_LIST    0xC2E12E98u     /* a jewellery row: one per entry, one when empty (sub_000153D0) */

typedef struct Sod { const uint8_t *b; size_t n; } Sod;

static int sod_u(const Sod *s, uint64_t o, uint32_t *v)
{
    if (o > s->n || s->n - o < 4) return 0;
    *v = (uint32_t)s->b[o] | (uint32_t)s->b[o + 1] << 8 | (uint32_t)s->b[o + 2] << 16 | (uint32_t)s->b[o + 3] << 24;
    return 1;
}

static int sod_ok(const Sod *s, uint64_t o, uint64_t len)
{
    return o <= s->n && len <= s->n - o;
}

/* The word of a field in a row of `table` (the schema maps table hash ^ field hash to it); -1 when absent. */
static int sod_field(const Sod *s, uint32_t table, uint32_t field)
{
    uint32_t schema, n, i, h, w;
    if (!sod_u(s, 12, &schema) || !sod_u(s, (uint64_t)schema + 4, &n) || !sod_ok(s, (uint64_t)schema + 12, 12ull * n))
        return -1;
    for (i = 0; i < n; i++) {
        sod_u(s, (uint64_t)schema + 12 + 4ull * i, &h);
        if (h == (table ^ field)) {
            sod_u(s, (uint64_t)schema + 12 + 4ull * n + 8ull * i, &w);
            return w < 64 ? (int)w : -1;
        }
    }
    return -1;
}

int item_ext_base_parse(const uint8_t *sod, size_t size, ItemExtBase *out)
{
    Sod s;
    uint32_t data, n, stride, t;
    memset(out, 0, sizeof(*out));
    if (!sod || size < 24 || memcmp(sod, "SODB", 4)) return 0;
    s.b = sod;
    s.n = size;
    if (!sod_u(&s, 4, &t) || t != 2) return 0;
    if (!sod_u(&s, 16, &data) || !sod_u(&s, (uint64_t)data + 4, &n) || !sod_u(&s, (uint64_t)data + 8, &stride) ||
        stride != 1 || n == 0 || n > ITEM_EXT_TABLES || !sod_ok(&s, (uint64_t)data + 12, 8ull * n))
        return 0;
    for (t = 0; t < n; t++) {
        uint32_t hash, table, classes, block, count, rstride, r;
        int word = -1;
        sod_u(&s, (uint64_t)data + 12 + 4ull * t, &hash);
        sod_u(&s, (uint64_t)data + 12 + 4ull * n + 4ull * t, &table);
        out->hash[t] = hash;
        if (!sod_u(&s, (uint64_t)table + 0x10, &classes) || classes == 0 || classes > 64 ||
            !sod_u(&s, (uint64_t)table + 0x18 + 4ull * classes, &block) || !sod_u(&s, (uint64_t)block + 4, &count) ||
            !sod_u(&s, (uint64_t)block + 8, &rstride) || !rstride || count > 100000 ||
            !sod_ok(&s, (uint64_t)block + 12 + 4ull * count, 4ull * count * rstride))
            continue;                       /* a table the game would not read either: no rows */
        out->rows[t] = count;
        if (hash == SOD_CLOTHING) word = sod_field(&s, SOD_CLOTHING, SOD_CLOTHING_LIST);
        else if (hash == SOD_JEWELRY) word = sod_field(&s, SOD_JEWELRY, SOD_JEWELRY_LIST);
        if (word < 0 || !count) continue;
        out->colours[t] = (uint8_t *)calloc(count, 1);
        if (!out->colours[t]) { item_ext_base_free(out); return 0; }
        for (r = 0; r < count; r++) {
            uint32_t row, list, len = 0;
            sod_u(&s, (uint64_t)block + 12 + 4ull * count + 4ull * r * rstride, &row);
            if (sod_u(&s, (uint64_t)row + 4ull * (uint32_t)word, &list) && list) sod_u(&s, list, &len);
            if (hash == SOD_JEWELRY && len == 0) len = 1;
            out->colours[t][r] = (uint8_t)(len > 255 ? 255 : len);
        }
    }
    out->tables = (int)n;
    return 1;
}

void item_ext_base_free(ItemExtBase *b)
{
    int t;
    for (t = 0; t < ITEM_EXT_TABLES; t++) free(b->colours[t]);
    memset(b, 0, sizeof(*b));
}

int item_ext_is_retail(const ItemExtBase *b, uint32_t id)
{
    uint32_t table = id >> 24, colour = (id >> 16) & 0xFFu, row = id & 0xFFFFu;
    if (!b || !b->tables) return 1;
    if (table >= (uint32_t)b->tables) return 0;
    if (row >= b->rows[table]) return 0;
    if (b->colours[table]) return colour < b->colours[table][row];
    return colour == 0;
}

uint32_t item_ext_partition(const ItemExtBase *b, uint32_t *ids, uint32_t n)
{
    uint32_t i, r = 0, k = 0, *tail;
    for (i = 0; i < n; i++) r += (uint32_t)item_ext_is_retail(b, ids[i]);
    if (r == n) return n;
    tail = (uint32_t *)malloc(sizeof(uint32_t) * (n - r));
    if (!tail) return n;                    /* cannot happen in practice; leaves the game's order */
    for (i = 0, r = 0; i < n; i++) {
        if (item_ext_is_retail(b, ids[i])) ids[r++] = ids[i];
        else tail[k++] = ids[i];
    }
    memcpy(ids + r, tail, sizeof(uint32_t) * k);
    free(tail);
    return r;
}

uint32_t item_ext_count_retail(const ItemExtBase *b, const uint32_t *ids, uint32_t n)
{
    uint32_t i, r = 0;
    for (i = 0; i < n; i++) r += (uint32_t)item_ext_is_retail(b, ids[i]);
    return r;
}

static int32_t find_run(const uint32_t *ids, uint32_t lo, uint32_t hi, uint32_t id)
{
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (ids[mid] == id) return (int32_t)mid;
        if (ids[mid] < id) lo = mid + 1;
        else hi = mid;
    }
    return -1;
}

int32_t item_ext_find(const uint32_t *ids, uint32_t n, uint32_t retail, uint32_t id)
{
    int32_t at;
    if (retail > n) retail = n;
    at = find_run(ids, 0, retail, id);
    return at >= 0 ? at : find_run(ids, retail, n, id);
}

void item_ext_layout_offsets(const uint32_t retail[6], uint32_t offsets[6])
{
    uint32_t k, at = 0;
    for (k = 0; k < 6; k++) {
        offsets[k] = at;
        at += retail[k] / 4 + 1;
    }
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* New items without a new save layout: the item containers' methods (item_ext.h, item_ext_logic.h). */
#include "item_ext.h"
#include "item_ext_logic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp/gen/recomp_types.h"

extern void sub_00016FA0(void);     /* the containers' sort (vtable +4): qsort of the items by ID */

#define OFF_ITEMS   0x4u
#define OFF_COUNT   0x8u
#define OFF_STATES  0xCu            /* + slot * 4 */
#define OFF_CURRENT 0x4Cu           /* -> the current slot's index */
#define OFF_MASK    0x50u
#define SLOTS       16u
#define SLOT_STATES 0x4DC4u         /* the first state array in a profile slot (sub_000148B0) */

/* profile order of the six containers (offsets from the first, save record +0x14E4) */
static const uint32_t kProfileOrder[6] = {0x0, 0x58, 0x108, 0xB0, 0x160, 0x1B8};

static int g_enabled = -1;
static int g_base_loaded;
static ItemExtBase g_base;

static int enabled(void)
{
    if (g_enabled < 0) {
        const char *v = getenv("RECOMP_ITEM_EXT");
        g_enabled = !(v && !strcmp(v, "0"));
        if (!g_enabled) fprintf(stderr, "[ITEMS] RECOMP_ITEM_EXT=0: the game's own item containers\n");
    }
    return g_enabled;
}

/* The dump's database (never the redirected or composed copy): what counts as retail. */
static const ItemExtBase *base(void)
{
    if (!g_base_loaded) {
        FILE *f = fopen("game\\assets\\djv2.sod", "rb");
        uint8_t *d = NULL;
        long n = 0;
        g_base_loaded = 1;
        if (f) {
            fseek(f, 0, SEEK_END);
            n = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (n > 24 && n < 64L * 1024 * 1024 && (d = (uint8_t *)malloc((size_t)n)) != NULL &&
                fread(d, 1, (size_t)n, f) != (size_t)n) {
                free(d);
                d = NULL;
            }
            fclose(f);
        }
        if (d && item_ext_base_parse(d, (size_t)n, &g_base))
            fprintf(stderr, "[ITEMS] the dump's database: %d tables (new rows and colours keep the retail save layout)\n",
                    g_base.tables);
        else
            fprintf(stderr, "[ITEMS] the dump's database could not be read: every item counts as retail\n");
        free(d);
        fflush(stderr);
    }
    return &g_base;
}

/* ---- the retail run of each container ---------------------------------------------------------------- */

typedef struct Cached { uint32_t self, items, count, retail; } Cached;
static Cached g_cache[16];

static const uint32_t *ids_of(uint32_t self, uint32_t *count)
{
    uint32_t items = MEM32(self + OFF_ITEMS);
    *count = MEM32(self + OFF_COUNT);
    if (!items || *count > 100000u) { *count = 0; return NULL; }
    return (const uint32_t *)XBOX_PTR(items);
}

static uint32_t retail_of(uint32_t self)
{
    uint32_t count, items = MEM32(self + OFF_ITEMS), k, free_at = 16;
    const uint32_t *ids = ids_of(self, &count);
    if (!ids) return count;
    for (k = 0; k < 16; k++) {
        if (g_cache[k].self == self) {
            if (g_cache[k].items == items && g_cache[k].count == count) return g_cache[k].retail;
            free_at = k;
            break;
        }
        if (!g_cache[k].self && free_at == 16) free_at = k;
    }
    k = item_ext_count_retail(base(), ids, count);
    if (free_at < 16) {
        g_cache[free_at].self = self;
        g_cache[free_at].items = items;
        g_cache[free_at].count = count;
        g_cache[free_at].retail = k;
    }
    return k;
}

uint32_t item_ext_retail_count(uint32_t container)
{
    uint32_t count = MEM32(container + OFF_COUNT);
    return enabled() ? retail_of(container) : count;
}

uint32_t item_ext_new_count(uint32_t container)
{
    return MEM32(container + OFF_COUNT) - item_ext_retail_count(container);
}

/* The position of `id`, -1 when absent; *is_new when it is past the retail run. */
static int32_t find(uint32_t self, uint32_t id, int *is_new)
{
    uint32_t count, retail = retail_of(self);
    const uint32_t *ids = ids_of(self, &count);
    int32_t at = ids ? item_ext_find(ids, count, retail, id) : -1;
    *is_new = at >= 0 && (uint32_t)at >= retail;
    return at;
}

static uint32_t states_of(uint32_t self)
{
    uint32_t slot = MEM32(MEM32(self + OFF_CURRENT));
    return MEM32(self + OFF_STATES + slot * 4u);
}

static uint32_t state_at(uint32_t self, int32_t at)
{
    return (uint32_t)(MEM8(states_of(self) + (uint32_t)at / 4u) >> (((uint32_t)at & 3u) * 2u)) & MEM8(self + OFF_MASK);
}

/* ---- the methods (thiscall; the callee pops its arguments) -------------------------------------------- */

static void m_sort(void)                /* +4: () */
{
    uint32_t self = g_ecx, count, retail, total;
    uint32_t *ids;
    sub_00016FA0();                     /* pops the return address */
    ids = (uint32_t *)ids_of(self, &count);
    if (!ids) return;
    total = count;
    retail = item_ext_partition(base(), ids, count);
    if (retail != total) {
        fprintf(stderr, "[ITEMS] container %08X: %u items, %u retail, %u new (after the retail ones, owned)\n",
                self, total, retail, total - retail);
        fflush(stderr);
    }
    (void)retail_of(self);              /* cache it */
}

static void m_get(void)                 /* +8: (id) -> state, -1 when absent */
{
    uint32_t self = g_ecx, id = MEM32(g_esp + 4);
    int is_new;
    int32_t at = find(self, id, &is_new);
    g_eax = at < 0 ? 0xFFFFFFFFu : is_new ? (ITEM_EXT_NEW_STATE & MEM8(self + OFF_MASK)) : state_at(self, at);
    g_esp += 8;
}

static void m_set(void)                 /* +0xC: (id, state) */
{
    uint32_t self = g_ecx, id = MEM32(g_esp + 4), v = MEM8(g_esp + 8);
    int is_new;
    int32_t at = find(self, id, &is_new);
    if (at >= 0 && !is_new) {
        uint32_t p = states_of(self) + (uint32_t)at / 4u, sh = ((uint32_t)at & 3u) * 2u;
        MEM8(p) = (uint8_t)(MEM8(p) & ~(MEM8(self + OFF_MASK) << sh));
        MEM8(p) = (uint8_t)(MEM8(p) | (v << sh));
    }
    g_esp += 12;
}

static void m_or_row(void)              /* +0x10: (row id, -, state): every colour of the row */
{
    uint32_t self = g_ecx, row = MEM32(g_esp + 4), v = MEM8(g_esp + 0xC), c;
    int is_new;
    for (c = 0; c < 256u; c++) {
        int32_t at = find(self, c << 16 | row, &is_new);
        if (at < 0) break;
        if (!is_new) {
            uint32_t p = states_of(self) + (uint32_t)at / 4u;
            MEM8(p) = (uint8_t)(MEM8(p) | (v << (((uint32_t)at & 3u) * 2u)));
        }
    }
    g_esp += 16;
}

static void m_get_row(void)             /* +0x18: (row id) -> OR of its colours' states */
{
    uint32_t self = g_ecx, row = MEM32(g_esp + 4), c, acc = 0;
    int is_new;
    for (c = 0; c < 256u; c++) {
        int32_t at = find(self, c << 16 | row, &is_new);
        if (at < 0) break;
        acc |= is_new ? (ITEM_EXT_NEW_STATE & MEM8(self + OFF_MASK)) : state_at(self, at);
    }
    g_eax = acc & 0xFFu;
    g_esp += 8;
}

static void m_or(void)                  /* +0x1C: (id, state) */
{
    uint32_t self = g_ecx, id = MEM32(g_esp + 4), v = MEM8(g_esp + 8);
    int is_new;
    int32_t at = find(self, id, &is_new);
    if (at >= 0 && !is_new) {
        uint32_t p = states_of(self) + (uint32_t)at / 4u;
        MEM8(p) = (uint8_t)(MEM8(p) | (v << (((uint32_t)at & 3u) * 2u)));
    }
    g_esp += 12;
}

static void m_clear(void)               /* +0x20: (id, bits) */
{
    uint32_t self = g_ecx, id = MEM32(g_esp + 4), v = MEM8(g_esp + 8);
    int is_new;
    int32_t at = find(self, id, &is_new);
    if (at >= 0 && !is_new) {
        uint32_t p = states_of(self) + (uint32_t)at / 4u;
        MEM8(p) = (uint8_t)(MEM8(p) & ~(v << (((uint32_t)at & 3u) * 2u)));
    }
    g_esp += 12;
}

int item_ext_set_all(void)              /* +0x14: (state): every retail item */
{
    uint32_t self = g_ecx, v = MEM8(g_esp + 4), retail, i, p, mask;
    if (!enabled()) return 0;
    retail = retail_of(self);
    p = states_of(self);
    mask = MEM8(self + OFF_MASK);
    for (i = 0; i < retail; i++) {
        uint32_t b = p + i / 4u, sh = (i & 3u) * 2u;
        MEM8(b) = (uint8_t)(MEM8(b) & ~(mask << sh));
        MEM8(b) = (uint8_t)(MEM8(b) | (v << sh));
    }
    g_esp += 8;
    return 1;
}

item_ext_fn item_ext_lookup(uint32_t va)
{
    if (va < 0x16FA0u || va > 0x174D0u || !enabled()) return NULL;
    switch (va) {
    case 0x00016FA0u: return m_sort;
    case 0x00017260u: return m_get;
    case 0x000173A0u: return m_set;
    case 0x00017410u: return m_or_row;
    case 0x00017310u: return m_get_row;
    case 0x00017480u: return m_or;
    case 0x000174D0u: return m_clear;
    default: return NULL;
    }
}

void item_ext_layout(uint32_t slot, uint32_t index, uint32_t containers)
{
    uint32_t retail[6], offsets[6], k, moved = 0;
    static uint32_t s_told;
    if (!enabled() || index >= SLOTS || !containers) return;
    for (k = 0; k < 6; k++) {
        uint32_t c = containers + kProfileOrder[k];
        retail[k] = retail_of(c);
        moved |= retail[k] != MEM32(c + OFF_COUNT);
    }
    if (!moved) return;                 /* nothing new: the game's own layout is the retail one */
    item_ext_layout_offsets(retail, offsets);
    for (k = 0; k < 6; k++)
        MEM32(containers + kProfileOrder[k] + OFF_STATES + index * 4u) = slot + SLOT_STATES + offsets[k];
    if (!(s_told >> index & 1u)) {
        s_told |= 1u << index;
        fprintf(stderr, "[ITEMS] profile slot %u: item states laid out for the retail counts %u %u %u %u %u %u\n",
                index, retail[0], retail[1], retail[2], retail[3], retail[4], retail[5]);
        fflush(stderr);
    }
}

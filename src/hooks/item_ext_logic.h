/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* New items without a new save layout: the pure part of item_ext.h (no guest memory; tests/unit/test_item_ext.py
 * compiles it on its own).
 *
 * The game keeps one 2-bit state per item (0 locked, 1 unlocked, 2/3 owned) in each profile, in six arrays
 * one after another from slot +0x4DC4 (hair, tattoo, jewellery, clothing, Blazin, style), each items / 4 + 1
 * bytes, and an item's state sits at its position in the container's item array, which the game sorts by
 * ID (docs/research/unlock-everything.md). An item ID is table << 24 | colour << 16 | row. So any row or
 * colour added to the database (a clothing pack, an official new item) moves the states of every item whose
 * ID sorts after it, and from four new items on the arrays after clothing move too
 * (community/clothing-packs-test.md: Blazin 65/16/1 read as 70/10/2).
 *
 * The rule here: an item is RETAIL when the dump's own database (game/assets/djv2.sod, never changed) has it:
 * its row is below the dump's row count of that table and its colour below the dump's colour count of that
 * row. Every other item is NEW. The containers keep the retail items first, sorted by ID as the game sorts
 * them, then the new ones, sorted by ID; the arrays are sized from the retail counts. A retail item's state
 * is therefore where the retail game keeps it, whatever was added, and a profile file is byte for byte one
 * the retail layout reads. New items have no bits in the profile: they count as unlocked and owned
 * (ITEM_EXT_NEW_STATE), and writes to them are dropped. */
#ifndef ITEM_EXT_LOGIC_H
#define ITEM_EXT_LOGIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ITEM_EXT_TABLES 64
/* What a new item reads as: unlocked and owned (bit 0 Unlocked, bit 1 Owned; save_unlock's "own" writes 3 too).
 * Bit 0 matters: the Wardrobe's item list (sub_0004E650, vtable 0x294D54 +4) takes an item only when get() has
 * bit 0 set, so with 2 the Eyepatch counted as owned but never showed in Wardrobe > Accessories > Eyewear. */
#define ITEM_EXT_NEW_STATE 3u

typedef struct ItemExtBase {
    int tables;                             /* tables read; 0 = no dump database: every item is retail */
    uint32_t hash[ITEM_EXT_TABLES];         /* each table's name hash */
    uint32_t rows[ITEM_EXT_TABLES];         /* the dump's rows per table index */
    uint8_t *colours[ITEM_EXT_TABLES];      /* per row, its items (colours, jewellery entries); NULL: one */
} ItemExtBase;

/* The dump's djv2.sod ("SODB" version 2). 1 when read; 0 leaves `out` empty (every item retail). */
int item_ext_base_parse(const uint8_t *sod, size_t size, ItemExtBase *out);
void item_ext_base_free(ItemExtBase *b);
/* 1 when the dump's database has the item (above). */
int item_ext_is_retail(const ItemExtBase *b, uint32_t id);
/* `ids` sorted by ID (the game's own sort): the retail ones first, then the new ones, each run keeping its
 * order. Returns the number of retail items. */
uint32_t item_ext_partition(const ItemExtBase *b, uint32_t *ids, uint32_t n);
/* How many of `ids` are retail (an unsorted or already partitioned array). */
uint32_t item_ext_count_retail(const ItemExtBase *b, const uint32_t *ids, uint32_t n);
/* The position of `id` in an array partitioned as above (two sorted runs, [0, retail) and [retail, n)), or
 * -1. */
int32_t item_ext_find(const uint32_t *ids, uint32_t n, uint32_t retail, uint32_t id);
/* The six state arrays of a profile slot, in profile order (hair, tattoo, jewellery, clothing, Blazin,
 * style), from their retail counts: offsets from the first one (slot +0x4DC4). */
void item_ext_layout_offsets(const uint32_t retail[6], uint32_t offsets[6]);

#ifdef __cplusplus
}
#endif

#endif

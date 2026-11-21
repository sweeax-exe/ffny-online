/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* New items (rows or colours the dump's database does not have: an official new item such as the Eyepatch,
 * a clothing pack) without moving any state in a profile save. The rule and why: item_ext_logic.h.
 *
 * The six item containers at save record +0x14E4 (sub_00015890: hair, tattoo, clothing +0xB0, jewellery
 * +0x108, Blazin +0x160, style +0x1B8; 0x58 bytes each: vtable, item array +4, count +8, per profile slot a
 * state pointer +0xC + slot * 4, the current slot's index at *(+0x4C), the state mask byte +0x50) share one
 * vtable of methods (0x286250..): +4 sort (0x16FA0, qsort by ID), +8 get (0x17260), +0xC set (0x173A0), +0x10
 * OR into every colour of a row (0x17410), +0x14 every item to one state (0x168E0), +0x18 OR of a row's colours
 * (0x17310), +0x1C OR (0x17480), +0x20 clear (0x174D0). They find an item with the binary search 0x172C0 and use
 * its position as the state index. Only these methods and the slot layout (sub_000148B0) touch the states.
 *
 * Here: the sort puts the new items after the retail ones, every method finds an item in the two runs, a new
 * item reads as owned and is never written, and the slot layout is made from the retail counts. With no new
 * item in the database every method does exactly what the game's does. RECOMP_ITEM_EXT=0 leaves the game's
 * own methods in place (tests: the shifted layout as before). */
#ifndef ITEM_EXT_H
#define ITEM_EXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*item_ext_fn)(void);
/* recomp_lookup_manual: the container methods above (NULL for every other address, or when off). */
item_ext_fn item_ext_lookup(uint32_t va);
/* After sub_000148B0 laid out slot `index` (slot record `slot`, containers at `containers`): the six state
 * pointers again from the retail counts. */
void item_ext_layout(uint32_t slot, uint32_t index, uint32_t containers);
/* sub_000168E0 (thiscall (container), arg (state), ret 4): 1 when done here, 0 to run the game's. */
int item_ext_set_all(void);
/* The retail items of a container (its count when it holds no new one). */
uint32_t item_ext_retail_count(uint32_t container);
/* The new items of a container (0 when none). */
uint32_t item_ext_new_count(uint32_t container);

#ifdef __cplusplus
}
#endif

#endif

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Community fighters beyond the 48 places (offline, phase 1 of community/unlimited-fighters-study.md).
 *
 * The database (assets/djv2.sod) holds four bank rows, front-end IDs 124..127, one per player slot, and one
 * unindexed record per logical fighter (a 26-word Character row image no class block or row list points at);
 * our side file assets/ffny_fighters.bin lists the records (setup/ffnycontent/unlimited.py). The menus offer a
 * logical fighter by its own front-end ID, 1000 and up. When a slot is set up with one
 * (Controller.ControllerSetup(slot, pad, id, outfit)) the command reaches the game with the slot's bank instead
 * (124 + slot) and the slot's bank is bound to the fighter; the database's record lookup (sub_000BE760) then
 * answers the bank row's lookups with the bound record, so the match reads that fighter's name, model, coreID,
 * stats and gameplay words. Bank s is only ever slot s's, so the same fighter in two slots (a mirror match) uses
 * two banks. The binding is a pure function of the lineup: each slot's last set-up.
 *
 * Offline only: while this PC is online a logical ID is refused (the slot gets the fighter's template, the stock
 * fighter it was made from) and no bank is redirected. */
#ifndef DEFJAM_FIGHTER_BANKS_H
#define DEFJAM_FIGHTER_BANKS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FB_BANKS 4
#define FB_FIRST_BANK 124
#define FB_FIRST_LOGICAL 1000
#define FB_SIDE_FILE "assets\\ffny_fighters.bin"
#define FB_ROW_WORDS 26
#define FB_MORE_ID 999999          /* the select grid's MORE cell: the paged list of every one (no fighter) */
#define NF_MORE_ID FB_MORE_ID

typedef struct FighterBanksEntry {
    int id;                         /* the logical front-end ID, 1000 and up */
    uint32_t offset;                /* the record's offset in the database */
    int template_fe_id;             /* the stock fighter it was made from */
    int core_id;
    char name[32];                  /* display name */
    char model[16];
    uint32_t words[FB_ROW_WORDS];   /* the record as the database has it */
} FighterBanksEntry;

/* Pure parts (no game or host state; tested natively, tests/unit/test_unlimited_fighters.py). */

/* The side file checked against its database (size, FNV-1a 32, the bank rows' feIDs, every record's ID and
 * coreID). Fills *count entries into a malloc'd array (*out, the caller frees) and the four bank row offsets.
 * Returns 1, or 0 with a reason in why. */
int fighter_banks_parse(const uint8_t *side, size_t side_size, const uint8_t *sod, size_t sod_size,
                        FighterBanksEntry **out, int *count, uint32_t bank_offset[FB_BANKS], char *why, size_t why_size);

/* The bank index (0..3) of front-end ID fe_id, or -1. */
static __inline int fighter_banks_index(int fe_id)
{
    return fe_id >= FB_FIRST_BANK && fe_id < FB_FIRST_BANK + FB_BANKS ? fe_id - FB_FIRST_BANK : -1;
}

/* "Controller.ControllerSetup(slot,pad,id,outfit)" with id a logical fighter's: the command with the slot's bank
 * (bank != 0) or the fighter's template (bank == 0) in its place, into out. Returns 1 when it rewrote, 0 when the
 * command is another (or names no logical fighter); *slot and *id say what it named. */
int fighter_banks_rewrite_setup(const char *command, int replacement, char *out, size_t out_size, int *slot, int *id);

/* text ("k=v&k=v...") with the value of each listed key that equals from[m] replaced by to[m], into out.
 * Returns 1 when something changed (out holds the result), else 0. */
int fighter_banks_rewrite_fields(const char *text, const char *const *keys, int nkeys, const int *from, const int *to,
                                 int nmap, char *out, size_t out_size);

/* A fighter picture's name ("portrait/p_ML_125_2p.swf", "thumbs/thumb124off") that names a bank (its ID, or ID + 1
 * for the families that number so) with bank_to_id[bank] the fighter bound to it (FB_FIRST_LOGICAL and up), under
 * that fighter's ID instead, into out. 1 when it rewrote. */
int fighter_banks_picture_name(const char *name, const int *bank_to_id, char *out, size_t out_size);

/* The game side. */

int fighter_banks_ready(void);                          /* 1 when the side file and its banks loaded and checked */
int fighter_banks_count(void);
const FighterBanksEntry *fighter_banks_at(int i);       /* in ID order */
const FighterBanksEntry *fighter_banks_find(int id);    /* NULL: no such logical fighter */
const FighterBanksEntry *fighter_banks_bound(int slot); /* the fighter bank `slot` holds, or NULL */
/* The fighter a front-end ID stands for: a bank's bound fighter, a logical ID's own, else NULL. */
const FighterBanksEntry *fighter_banks_for_fe(int fe_id);

/* A front-end command before the game runs it (the FuncCall hook and defjam_fe_call): a ControllerSetup that names
 * a logical fighter becomes one with the slot's bank (offline) or the fighter's template (online, refused), and the
 * bank is bound. Returns 1 and the command to run in out, else 0 (run it as it is). */
int fighter_banks_before_call(const char *command, char *out, size_t out_size);

/* The record lookup's result (sub_000BE760: thiscall on the database, out[0] the record): the bound fighter's
 * record for a bound bank's row, else record. caller: the lookup's return address, for the log. */
uint32_t fighter_banks_record(uint32_t database, uint32_t record, uint32_t caller);

/* A front-end answer after the game made it: Game.GetMatchInfo() ("0=<fighter>&1=..") names a bound bank by its
 * fighter's own ID, so the loading screen loads that fighter's pictures. Game.GetMatchSummary keeps the bank's ID
 * (the result screen stops with a fighter's own; its pictures come from fighter_banks_movie_load). Returns 1 with
 * the new answer in out. */
int fighter_banks_after_call(const char *command, const char *answer, char *out, size_t out_size);

/* The FuncCall hook (recomp_manual.c): before the game runs a command (text is changed to the command it runs,
 * the guest argument re-pointed at a copy) and after it answered (g_eax re-pointed at a rewritten answer). */
void fighter_banks_funccall_before(char *text, size_t text_size);
void fighter_banks_funccall_after(const char *text);

/* The APT movie loader (sub_001810B0: archive, name, &apt, &const), name at guest address name_arg: a picture of a
 * bound bank (".._124_.." names) is loaded under its fighter's own ID (the argument re-pointed at a copy). */
void fighter_banks_movie_load(uint32_t name_arg);

/* MORE on a select screen bound the slot to a first fighter and asks for the community window (the paged list,
 * online_screen.cpp): 1 once per request, with the slot. */
int fighter_banks_take_window(int *slot);
/* The window's choice: bank `slot` holds fighter id from now on (offline; 0 when it cannot). */
int fighter_banks_bind(int slot, int id);

#ifdef __cplusplus
}
#endif

#endif

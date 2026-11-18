/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 95: the fighters an online match may load (the fighter-255 crash of builds 93/94,
 * community/crash-hoodiebatman.md). Pure rules, compiled natively by tests/unit/test_netplay_lineup_native.py.
 *
 * A Battle-mode match loads each fighter's Character row from the game's database. A fighter without one (255, the
 * relay's "no fighter", or any ID the database lacks) crashed the load on every PC (sub_001BBC10+0x8B,
 * docs/research/crash-001B2480.md section 11). Builds 93/94 sent 255 in READY for a community fighter by number:
 * the hub's [online] fighter setting held 0..255 and saved 1008 as 255.
 *
 *   - READY never carries a fighter byte outside 0..254: netlineup_ready_fighter gives Doc (55) for anything else
 *     that is no fighter number.
 *   - START: a fighter no PC has a row for (255, an unknown ID) plays Doc on every PC at once (the stock rows are
 *     the same everywhere). A fighter only some PCs may lack (a community fighter by number, a new fighter's
 *     place) sets this PC's bit in the lineup mask; every PC sends its mask in P_RTT (byte 18) and, at the sync
 *     point, every PC plays the template (a number) or Doc (a place) for each position any PC lacks, so the
 *     lineup stays the same on every PC (netlineup_agree). */
#ifndef DEFJAM_NETPLAY_LINEUP_RULES_H
#define DEFJAM_NETPLAY_LINEUP_RULES_H

#ifdef __cplusplus
extern "C" {
#endif

#define NETLINEUP_DOC 55            /* the fighter every PC has (SafeFighter on the relay) */
#define NETLINEUP_CREATED 99        /* a created fighter: automatch writes its record, Doc without one */
#define NETLINEUP_FIRST_NUMBER 1000 /* community fighters by number (fighter_banks.h FB_FIRST_LOGICAL) */
#define NETLINEUP_MORE 999999       /* the select grid's MORE cell (FB_MORE_ID), never a fighter */

/* A fighter's kind for the load, as this PC answers it (callbacks: netlineup_kind). */
enum {
    NETLINEUP_NONE = 0,     /* no row on any PC (255, an ID the database lacks): Doc everywhere, at once */
    NETLINEUP_STOCK,        /* a row every PC has (the database's own fighters, a created fighter) */
    NETLINEUP_HERE,         /* a row only some PCs may have (a number, a new fighter's place), and this PC has it */
    NETLINEUP_MISSING       /* the same, and this PC lacks it: the mask bit */
};

int netlineup_is_number(int fighter);

/* The fighter byte READY may carry for `fighter` (not a number): itself in 0..254, else Doc. */
int netlineup_ready_fighter(int fighter);

/* The kind of `fighter`: stock(id) says whether the database has a row for a front-end ID (0..255), place(id) whether
 * it is a new fighter's place, have(id) whether this PC has that place's or number's row. */
int netlineup_kind(int fighter, int (*stock)(int), int (*place)(int), int (*have)(int));

/* The positions (bit i) of fighter[0..count) this PC lacks (NETLINEUP_MISSING); fighters of kind NETLINEUP_NONE are
 * changed to Doc in place (told: their count goes to *fixed when not NULL). */
unsigned netlineup_mask(int *fighter, int count, int (*stock)(int), int (*place)(int), int (*have)(int), int *fixed);

/* The lineup every PC plays: for each position in `missing` (every PC's mask OR'ed), a number plays its template
 * (template_fe[i], START's byte) when the database has it, else Doc; a place plays Doc. Returns the positions
 * changed. */
int netlineup_agree(int *fighter, const int *template_fe, int count, unsigned missing, int (*stock)(int));

#ifdef __cplusplus
}
#endif

#endif

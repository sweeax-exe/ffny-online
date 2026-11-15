/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The places of new fighters: the front-end IDs a content pack's new fighter may have (the account service gives
 * each accepted one its place, the signed catalogue carries it as fighter_fe_id). 48, in the order the service
 * gives them: 100..127 first (builds 79..83 knew only these), then the 20 IDs below 98 that no Character row of
 * the retail database has and nothing else uses (docs/research/new-fighters.md section 5):
 *
 *   5 15 29 37 39 42 49 53 63 66 79 86 87 88 89 90 94 95 96 97
 *
 * Left out of the free IDs below 98: 40, 41, 52 and 78 (the fighter.xml IDs of Teck, Santos, O.E and Erick
 * Sermon, which the hub and the relay translate to 85, 93, 92 and 91), 98 (the E3 Hero row) and 99 (a created
 * fighter's byte). 128 and up crash the match load (the fighter byte is read signed). The place's index (0..47)
 * numbers a fighter's own voice (fighter_voice.h) and orders the select grid's community row.
 *
 * Pure (no game or host state); the same list is in setup/ffnycontent/newfighter.py (PLACES), FFNY Studio's copy
 * of it, the relay (NewFighterPlaces) and the account service (NewFighterPlaces); tests/unit/
 * test_new_fighter_places.py compares them. */
#ifndef DEFJAM_NEW_FIGHTER_PLACES_H
#define DEFJAM_NEW_FIGHTER_PLACES_H

#define NEW_FIGHTER_MAX 48
#define NEW_FIGHTER_HIGH 28     /* places 0..27 are 100..127, the ones every build since 79 shows */

/* The front-end ID of place index i (0..NEW_FIGHTER_MAX-1), or -1. */
static __inline int new_fighter_place_id(int i)
{
    static const unsigned char k_places[NEW_FIGHTER_MAX] = {
        100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
        120, 121, 122, 123, 124, 125, 126, 127,
        5, 15, 29, 37, 39, 42, 49, 53, 63, 66, 79, 86, 87, 88, 89, 90, 94, 95, 96, 97,
    };
    return i >= 0 && i < NEW_FIGHTER_MAX ? (int)k_places[i] : -1;
}

/* The place index (0..NEW_FIGHTER_MAX-1) of front-end ID fe_id, or -1 when it is no new fighter's place. */
static __inline int new_fighter_place(int fe_id)
{
    int i;
    if (fe_id < 0 || fe_id > 127) return -1;
    if (fe_id >= 100) return fe_id - 100;
    for (i = NEW_FIGHTER_HIGH; i < NEW_FIGHTER_MAX; i++)
        if (new_fighter_place_id(i) == fe_id) return i;
    return -1;
}

/* 1 when fe_id is a new fighter's place. */
static __inline int new_fighter_is_place(int fe_id) { return new_fighter_place(fe_id) >= 0; }

#endif

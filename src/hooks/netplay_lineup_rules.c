/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 95: the fighters an online match may load (netplay_lineup_rules.h). */
#include "netplay_lineup_rules.h"

int netlineup_is_number(int fighter) { return fighter >= NETLINEUP_FIRST_NUMBER && fighter != NETLINEUP_MORE; }

int netlineup_ready_fighter(int fighter)
{
    return fighter >= 0 && fighter < 0xFF ? fighter : NETLINEUP_DOC;
}

int netlineup_kind(int fighter, int (*stock)(int), int (*place)(int), int (*have)(int))
{
    if (netlineup_is_number(fighter)) return have && have(fighter) ? NETLINEUP_HERE : NETLINEUP_MISSING;
    if (fighter < 0 || fighter > 0xFF) return NETLINEUP_NONE;
    if (fighter == NETLINEUP_CREATED || (stock && stock(fighter))) return NETLINEUP_STOCK;
    if (place && place(fighter)) return have && have(fighter) ? NETLINEUP_HERE : NETLINEUP_MISSING;
    return NETLINEUP_NONE;
}

unsigned netlineup_mask(int *fighter, int count, int (*stock)(int), int (*place)(int), int (*have)(int), int *fixed)
{
    unsigned mask = 0;
    int i, n = 0;
    for (i = 0; i < count && i < 8; i++) {
        int k = netlineup_kind(fighter[i], stock, place, have);
        if (k == NETLINEUP_NONE) {
            fighter[i] = NETLINEUP_DOC;
            n++;
        } else if (k == NETLINEUP_MISSING) {
            mask |= 1u << i;
        }
    }
    if (fixed) *fixed = n;
    return mask;
}

int netlineup_agree(int *fighter, const int *template_fe, int count, unsigned missing, int (*stock)(int))
{
    int i, n = 0;
    for (i = 0; i < count && i < 8; i++) {
        int to;
        if (!(missing & (1u << i))) continue;
        to = NETLINEUP_DOC;
        if (netlineup_is_number(fighter[i]) && template_fe && template_fe[i] >= 0 && template_fe[i] < 0xFF &&
            stock && stock(template_fe[i]))
            to = template_fe[i];
        if (fighter[i] != to) n++;
        fighter[i] = to;
    }
    return n;
}

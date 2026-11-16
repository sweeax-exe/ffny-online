/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The Unlock Rewards screen's fighter list without new fighters (new_fighters.c, new_fighters_unlock_list).
 * Unlock.GetCharacterList answers "Count=N&BE=<row id>&FE=<feID>&..." in the front end's result text, 0x600 bytes;
 * this keeps the pairs of stock fighters, in their order, within that size. Pure: tests/unit/
 * test_unlock_list_native.py builds it as it is. */
#ifndef DEFJAM_UNLOCK_LIST_H
#define DEFJAM_UNLOCK_LIST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "new_fighter_places.h"

#define FE_RESULT_SIZE 0x600u          /* 0x341768 .. 0x341D68, where the front end's own objects start */
#define FE_RESULT_SPILL 0x1000u        /* 48 new fighters write about 0x3A0 past the end */

/* Writes the filtered list to out (cap bytes, at most FE_RESULT_SIZE). Returns the pairs kept; *left_out: the pairs
 * taken out (a new fighter's, or one that would not fit). */
static int unlock_list_filter(const char *in, char *out, size_t cap, int *left_out)
{
    const char *p = strchr(in, '&');
    char pairs[FE_RESULT_SIZE];
    size_t used = 0, room = cap < sizeof(pairs) ? cap : sizeof(pairs);
    int kept = 0;
    *left_out = 0;
    pairs[0] = 0;
    while (p && !strncmp(p, "&BE=", 4)) {
        const char *fe = strstr(p + 4, "&FE="), *next;
        size_t len;
        if (!fe) break;
        next = strchr(fe + 4, '&');
        len = (size_t)((next ? next : fe + 4 + strlen(fe + 4)) - p);
        if (new_fighter_is_place(atoi(fe + 4)) || used + len + 16 >= room) {
            ++*left_out;                /* a new fighter's; or it would not fit: never past the buffer again */
        } else {
            memcpy(pairs + used, p, len);
            used += len;
            pairs[used] = 0;
            kept++;
        }
        p = next;
    }
    snprintf(out, cap, "Count=%d%s", kept, pairs);
    return kept;
}

#endif

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Previous song: the history of what played (song_history.h). */
#include "song_history.h"

#include <stdio.h>
#include <string.h>

void song_hist_reset(SongHistory *h)
{
    memset(h, 0, sizeof *h);
    h->pos = -1;
}

static int same(const SongHistEntry *e, int kind, const char *id)
{
    return e->kind == kind && !strncmp(e->id, id, sizeof e->id - 1);
}

int song_hist_played(SongHistory *h, int kind, const char *id, unsigned long long now)
{
    if (!id || !*id) return 0;
    if (h->pos >= 0 && same(&h->e[h->pos], kind, id)) {
        if (h->expect) {            /* the song a back asked for has started (again) */
            h->e[h->pos].started = now;
            h->expect = 0;
        }
        return 0;
    }
    /* After a back, the song that played until then is still heard for a moment: not a new song. */
    if (h->expect && now - h->expect_at < SONG_HIST_WAIT_MS) return 0;
    h->expect = 0;
    h->n = h->pos + 1;              /* the entries after the place go */
    if (h->n >= SONG_HIST_MAX) {    /* full: the oldest goes */
        memmove(&h->e[0], &h->e[1], sizeof h->e[0] * (SONG_HIST_MAX - 1));
        h->n = SONG_HIST_MAX - 1;
    }
    SongHistEntry *e = &h->e[h->n];
    e->kind = kind;
    snprintf(e->id, sizeof e->id, "%s", id);
    e->started = now;
    h->pos = h->n++;
    return 1;
}

const SongHistEntry *song_hist_current(const SongHistory *h)
{
    return h->pos >= 0 && h->pos < h->n ? &h->e[h->pos] : NULL;
}

int song_hist_back(SongHistory *h, unsigned long long now, int (*playable)(void *ctx, const SongHistEntry *e),
                   void *ctx)
{
    int recent = h->backs && now - h->last_back < SONG_HIST_BACK_MS;
    int i, to = -1;
    if (h->pos >= 0 && now - h->e[h->pos].started < SONG_HIST_BACK_MS) recent = 1;
    h->backs++;
    h->last_back = now;
    if (recent)
        for (i = h->pos - 1; i >= 0 && to < 0; i--)
            if (!playable || playable(ctx, &h->e[i])) to = i;
    if (to >= 0) h->pos = to;
    if (h->pos >= 0) {
        h->e[h->pos].started = now;
        h->expect = 1;
        h->expect_at = now;
    }
    return to;
}

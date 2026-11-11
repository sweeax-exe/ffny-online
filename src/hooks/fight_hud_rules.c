/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* See fight_hud_rules.h. */
#include <string.h>
#include "fight_hud_rules.h"

/* ---- PERFECT ------------------------------------------------------------------------------- */

void fight_perfect_reset(FightPerfect *p)
{
    int i;
    memset(p, 0, sizeof(*p));
    for (i = 0; i < FIGHT_HUD_SLOTS; i++) p->lost_step[i] = -1;
    p->decided_step = -1;
}

static int under(float start, float health)
{
    float eps = start > 1.0f ? start * 1e-4f : 1e-4f;
    return !(health >= start - eps);        /* a NaN counts as under: never a perfect on bad data */
}

int fight_perfect_under(const FightPerfect *p, int slot, float health)
{
    if (!p->armed || slot < 0 || slot >= FIGHT_HUD_SLOTS || !(p->tracked & (1u << slot))) return 1;
    return under(p->start[slot], health);
}

void fight_perfect_sample(FightPerfect *p, const FightSample *s)
{
    int i;
    int32_t step = (int32_t)(s->step & 0x7FFFFFFFu);
    if (!p->armed || s->step <= p->first_step) {
        /* the round's first step (again, when rollback went back to it): the start values */
        fight_perfect_reset(p);
        p->armed = 1;
        p->first_step = s->step;
        p->tracked = s->active & ((1u << FIGHT_HUD_SLOTS) - 1u);
        for (i = 0; i < FIGHT_HUD_SLOTS; i++) p->start[i] = s->health[i];
    }
    for (i = 0; i < FIGHT_HUD_SLOTS; i++) {
        if (!(p->tracked & (1u << i))) continue;
        if (under(p->start[i], s->health[i])) {
            if (p->lost_step[i] < 0 || step < p->lost_step[i]) p->lost_step[i] = step;
        } else if (p->lost_step[i] >= step) {
            p->lost_step[i] = -1;           /* that step, run again, shows no loss */
        }
    }
    if (s->decided) {
        if (p->decided_step < 0 || step <= p->decided_step) {
            p->decided_step = step;
            p->winners = s->winners;
        }
    } else if (p->decided_step >= step) {
        p->decided_step = -1;               /* that step, run again, is not decided */
        p->winners = 0;
    }
}

uint32_t fight_perfect_mask(const FightPerfect *p)
{
    int i;
    if (!p->armed || p->decided_step < 0 || !p->winners) return 0;
    if (p->winners & ~p->tracked) return 0;
    for (i = 0; i < FIGHT_HUD_SLOTS; i++)
        if ((p->winners & (1u << i)) && p->lost_step[i] >= 0) return 0;
    return p->winners;
}

uint32_t fight_winner_mask(int winner, int team, uint32_t active)
{
    uint32_t m;
    if (winner < 0 || winner >= FIGHT_HUD_SLOTS || !(active & (1u << winner))) return 0;
    m = team ? 3u << (winner & 2) : 1u << winner;
    return m & active;
}

/* ---- name tags ----------------------------------------------------------------------------- */

/* The game's HUD in its 640x480 layout: the fighter's name sits over the health bar, P1 and P3 on
 * the left, P2 and P4 on the right; with three or four fighters the HUD has a second, bottom row,
 * mirrored (the score over the bar, the name under it). The tag goes on the free line over the row,
 * at the bar's outer end. */
#define TAG_LEFT_X      53.0f       /* the left bars' outer end */
#define TAG_RIGHT_X     589.0f      /* the right bars' outer end */
#define TAG_TOP_Y       24.0f       /* the top row: over the name (y 37) */
#define TAG_BOTTOM_Y    360.0f      /* the bottom row (score, bar, name from y 375 down): over the score */
#define TAG_SIZE        11.0f
#define TAG_WIDTH       214.0f      /* the bar's length */

int fight_tag_place(const FightPicture *pic, int fighters, int slot, FightTag *out)
{
    float px = 0, pw, ph, s, sx, left, right;
    int bottom, on_right;
    if (!pic || !out || pic->w <= 0 || pic->h <= 0) return 0;
    if (fighters < 2) fighters = 2;
    if (fighters > FIGHT_HUD_SLOTS) fighters = FIGHT_HUD_SLOTS;
    if (slot < 0 || slot >= fighters) return 0;
    pw = pic->w;
    ph = pic->h;
    if (pic->wide > 0.0f || !pic->stretch) {
        /* the picture keeps its shape: bars at the sides or above and below */
        float a = pic->wide > 0.0f ? pic->wide : 4.0f / 3.0f;
        if (pw > ph * a) { float w = ph * a; px = (pw - w) / 2; pw = w; }
        else ph = pw / a;
    }
    s = ph / 480.0f;                /* HUD elements keep their 4:3 size in a wide picture */
    sx = s;
    left = px;
    right = px + pw;
    if (pic->wide <= 0.0f) {
        sx = pw / 640.0f;           /* 4:3, or stretched to the window */
    } else if (pic->hud_centred && pic->wide > 16.0f / 9.0f) {
        float box = ph * 16.0f / 9.0f;
        left = px + (pw - box) / 2;
        right = left + box;
    }
    bottom = slot >= 2;
    on_right = slot & 1;
    out->right = on_right;
    out->x = on_right ? right - (640.0f - TAG_RIGHT_X) * sx : left + TAG_LEFT_X * sx;
    out->y = (pic->h - ph) / 2 + (bottom ? TAG_BOTTOM_Y : TAG_TOP_Y) * s;
    out->size = TAG_SIZE * s;
    out->width = TAG_WIDTH * sx;
    return 1;
}

int fight_tag_text(const char *name, int max_chars, char *out, int size)
{
    int n = 0, chars = 0, cut = 0;
    const unsigned char *p = (const unsigned char *)name;
    if (!out || size <= 0) return 0;
    out[0] = 0;
    if (!name || max_chars < 3) return 0;
    while (*p == ' ') p++;
    while (*p) {
        int len = *p < 0x80 ? 1 : (*p >> 5) == 6 ? 2 : (*p >> 4) == 14 ? 3 : (*p >> 3) == 30 ? 4 : 0, k;
        if (!len) { p++; continue; }                        /* a stray byte */
        for (k = 1; k < len; k++)
            if ((p[k] & 0xC0) != 0x80) break;
        if (k < len) { p++; continue; }                     /* a cut sequence */
        if (*p < 0x20 || *p == 0x7F) { p++; continue; }     /* control characters */
        if (chars >= max_chars) { cut = 1; break; }
        if (n + len + 3 >= size) { cut = 1; break; }
        memcpy(out + n, p, (size_t)len);
        n += len;
        chars++;
        p += len;
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    if (cut && n + 2 < size) { out[n++] = '.'; out[n++] = '.'; }
    out[n] = 0;
    return n;
}

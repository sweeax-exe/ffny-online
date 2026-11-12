/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Milestone progress: the rules, pure C over a copy of a profile slot (milestones.h).
 *
 * The thresholds are the immediates of the bonus-stat recompute, guest 0x00014C20 (0x14F22..0x1501C),
 * and of Game.GetMilestone's test, 0x00013920. The counters are fed by the reward pass, 0x00064ED0, from
 * the match statistics (0x3BE160 + slot * 0x130, dword n at +4n):
 *
 *   +0x68 u32  punches landed (stat 37)        >= 100 +1, >= 5000 +2 Upper Body (not both)
 *   +0x6C u32  kicks landed (stat 38)          >= 100 +1, >= 5000 +2 Lower Body
 *   +0x70 u16  stat 19 (one strike type's hits)            >= 250  +1 Speed
 *   +0x72 u16  stat 45 (a time spent moving)               >= 250  +1 Speed
 *   +0x74 u16  wins of matches with more than two fighters >= 10   +1 Toughness (toughness_fix.c)
 *   +0x76 u16  stat 61 (health % regained after the low-health flag) >= 25 +1 Toughness
 *   +0x80 u16  matches ended with full health (stat 57 == 100)      >= 5   +1 Health
 *   +0x78 u32  stat 28 >= 500 and +0x7C u32 stat 27 >= 100           +1 Health
 *   +0x84 u32  stat 9                                      >= 25   +1 Charisma
 *   +0x88 u32  stats 10..13 (grapple moves)                >= 250  +1 Charisma
 *   +0x8C u16  matches with stat 63 > 0                    >= 5    +1 Charisma
 *   +0x82 u16  stat 5 (knockouts by the surroundings)      >= 5    +1 Charisma
 *   +0x8E u8   a Story win at the Window (mode 2, arena 0x11)       +1 Charisma
 *
 * What stats 19, 45, 61, 28, 27, 9, 10..13, 63 and 5 count is inferred from their call sites, not
 * confirmed (sure = 0): the panel marks those rows so a player does not take the words as exact. */
#include <stdio.h>
#include <string.h>
#include "milestones.h"

static uint32_t rd32(const uint8_t *p, uint32_t size, uint32_t off)
{
    if (off + 4u > size) return 0;
    return (uint32_t)p[off] | (uint32_t)p[off + 1] << 8 | (uint32_t)p[off + 2] << 16 | (uint32_t)p[off + 3] << 24;
}

static uint32_t rd16(const uint8_t *p, uint32_t size, uint32_t off)
{
    if (off + 2u > size) return 0;
    return (uint32_t)p[off] | (uint32_t)p[off + 1] << 8;
}

static uint32_t rd8(const uint8_t *p, uint32_t size, uint32_t off)
{
    return off < size ? p[off] : 0;
}

const char *milestones_group_name(int group)
{
    static const char *const names[MS_GROUPS] = { "UPPER BODY", "LOWER BODY", "SPEED", "TOUGHNESS", "HEALTH",
                                                  "CHARISMA", "ID CARD" };
    return group >= 0 && group < MS_GROUPS ? names[group] : "";
}

static void put(MilestoneRow *out, int max, int *n, int group, const char *what, const char *reward, uint32_t count,
                uint32_t target, int sure, int note)
{
    MilestoneRow *r;
    if (*n >= max) return;
    r = &out[(*n)++];
    memset(r, 0, sizeof(*r));
    r->group = group;
    r->what = what;
    r->reward = reward;
    r->count = count;
    r->target = target;
    r->sure = sure;
    r->note = note;
    r->reached = count >= target;
}

int milestones_rows(const uint8_t *p, uint32_t size, int toughness_fix, MilestoneRow *out, int max)
{
    int n = 0;
    uint32_t punches, kicks, i, total, styles, moves;
    if (!p || !out || max <= 0) return 0;
    punches = rd32(p, size, 0x68);
    kicks = rd32(p, size, 0x6C);
    put(out, max, &n, MS_UPPER, "Punches landed", "+1 Upper Body", punches, 100, 1, MS_NOTE_NONE);
    put(out, max, &n, MS_UPPER, "Punches landed", "+2 Upper Body", punches, 5000, 1, MS_NOTE_REPLACES);
    put(out, max, &n, MS_LOWER, "Kicks landed", "+1 Lower Body", kicks, 100, 1, MS_NOTE_NONE);
    put(out, max, &n, MS_LOWER, "Kicks landed", "+2 Lower Body", kicks, 5000, 1, MS_NOTE_REPLACES);
    put(out, max, &n, MS_SPEED, "Running attacks landed", "+1 Speed", rd16(p, size, 0x70), 250, 1, MS_NOTE_NONE);
    put(out, max, &n, MS_SPEED, "Time running", "+1 Speed", rd16(p, size, 0x72), 250, 1, MS_NOTE_NONE);
    put(out, max, &n, MS_TOUGHNESS, "Wins in fights with 3+ fighters", "+1 Toughness", rd16(p, size, 0x74), 10, 1,
        toughness_fix ? MS_NOTE_TOUGHNESS_FIX : MS_NOTE_TOUGHNESS_RETAIL);
    put(out, max, &n, MS_TOUGHNESS, "Big comeback", "+1 Toughness", rd16(p, size, 0x76), 25, 1,
        MS_NOTE_COMEBACK);
    put(out, max, &n, MS_HEALTH, "Fights ended at full health", "+1 Health", rd16(p, size, 0x80), 5, 1, MS_NOTE_NONE);
    put(out, max, &n, MS_HEALTH, "Counters and blocks", "+1 Health", rd32(p, size, 0x7C), 100, 1, MS_NOTE_NONE);
    if (n > 0 && out[n - 1].group == MS_HEALTH && out[n - 1].target == 100) {
        MilestoneRow *r = &out[n - 1];
        r->count2 = rd32(p, size, 0x78);
        r->target2 = 500;
        r->reached = r->count >= r->target && r->count2 >= r->target2;
    }
    put(out, max, &n, MS_CHARISMA, "Crowd grab attacks", "+1 Charisma", rd32(p, size, 0x84), 25, 1, MS_NOTE_NONE);
    put(out, max, &n, MS_CHARISMA, "Wall grabs landed", "+1 Charisma", rd32(p, size, 0x88), 250, 1, MS_NOTE_NONE);
    put(out, max, &n, MS_CHARISMA, "Fights finished", "+1 Charisma", rd16(p, size, 0x8C), 5, 1,
        MS_NOTE_NONE);
    put(out, max, &n, MS_CHARISMA, "Blazin moves done", "+1 Charisma", rd16(p, size, 0x82), 5, 1,
        MS_NOTE_NONE);
    put(out, max, &n, MS_CHARISMA, "Win a Story fight at the Window", "+1 Charisma", rd8(p, size, 0x8E) ? 1u : 0u, 1, 0,
        MS_NOTE_NONE);
    if (size < MS_PROFILE_BYTES) return n;
    /* Game.GetMilestone 0: the six base stats, each counted 0..50, together >= 250 */
    for (total = 0, i = 0; i < 6; i++) {
        int32_t v = (int32_t)rd32(p, size, 0xFC + 4 * i);
        total += v < 0 ? 0u : v > 50 ? 50u : (uint32_t)v;
    }
    put(out, max, &n, MS_BADGE, "Base stats total", "ID card badge", total, 250, 1, MS_NOTE_NONE);
    for (styles = 0, i = 0; i < 3; i++)
        if (rd8(p, size, 0x140 + i) != 0xFF) styles++;
    put(out, max, &n, MS_BADGE, "Fighting styles learned", "ID card badge", styles, 3, 1, MS_NOTE_NONE);
    for (moves = 0, i = 0; i < 39; i++)
        if (rd8(p, size, 0x4D8B + (i >> 3)) & (1u << (i & 7))) moves++;
    put(out, max, &n, MS_BADGE, "Moves learned", "ID card badge", moves, 39, 1, MS_NOTE_NONE);
    put(out, max, &n, MS_BADGE, "Story completed", "ID card badge", rd8(p, size, 0x150) ? 1u : 0u, 1, 1, MS_NOTE_NONE);
    return n;
}

int milestones_reached(const MilestoneRow *rows, int n)
{
    int i, k = 0;
    for (i = 0; rows && i < n; i++) k += rows[i].reached != 0;
    return k;
}

float milestones_fraction(const MilestoneRow *row)
{
    float a, b;
    if (!row || !row->target) return 0.0f;
    a = row->count >= row->target ? 1.0f : (float)row->count / (float)row->target;
    if (!row->target2) return a;
    b = row->count2 >= row->target2 ? 1.0f : (float)row->count2 / (float)row->target2;
    return a < b ? a : b;
}

void milestones_count_text(uint32_t value, char *out, int size)
{
    char digits[16], buf[24];
    int len, i, j = 0;
    if (!out || size <= 0) return;
    len = snprintf(digits, sizeof(digits), "%u", (unsigned)value);
    for (i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0) buf[j++] = ',';
        buf[j++] = digits[i];
    }
    buf[j] = 0;
    snprintf(out, (size_t)size, "%s", buf);
}

void milestones_progress_text(const MilestoneRow *row, char *out, int size)
{
    char a[24], b[24], c[24], d[24];
    if (!out || size <= 0) return;
    if (!row) { out[0] = 0; return; }
    /* a count past the target shows as the target reached: 5,000 / 5,000, not 7,318 / 5,000 (the number
     * itself stays in the row's own text) */
    milestones_count_text(row->count, a, sizeof(a));
    milestones_count_text(row->target, b, sizeof(b));
    if (!row->target2) {
        if (row->note == MS_NOTE_COMEBACK)          /* health won back, in percent (Big comeback) */
            snprintf(out, (size_t)size, "%s%% / %s%%", a, b);
        else
            snprintf(out, (size_t)size, "%s / %s", a, b);
        return;
    }
    milestones_count_text(row->count2, c, sizeof(c));
    milestones_count_text(row->target2, d, sizeof(d));
    snprintf(out, (size_t)size, "%s / %s  +  %s / %s", a, b, c, d);
}

int milestones_newly_reached(const MilestoneRow *before, int nb, const MilestoneRow *after, int na,
                             char *text, int size)
{
    int i, k = 0, first = -1;
    if (text && size > 0) text[0] = 0;
    if (!after) return 0;
    for (i = 0; i < na && i < MS_STAT_ROWS; i++) {
        int was = before && i < nb ? before[i].reached : 0;
        if (after[i].group == MS_BADGE || !after[i].reached || was) continue;
        if (first < 0) first = i;
        k++;
    }
    if (k && text && size > 0) {
        char t[24];
        milestones_count_text(after[first].target, t, sizeof(t));
        if (k == 1)
            snprintf(text, (size_t)size, "MILESTONE: %s %s - %s", after[first].what, t, after[first].reward);
        else
            snprintf(text, (size_t)size, "MILESTONE: %s %s - %s  (+ %d more)", after[first].what, t,
                     after[first].reward, k - 1);
    }
    return k;
}

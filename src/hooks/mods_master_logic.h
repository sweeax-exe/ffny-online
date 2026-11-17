/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 90, "Turn all mods off" (the launcher's Content page): one button that starts the game as the original and one
 * that brings back exactly what was on. The rules, without the game (header only; the unit test builds a driver that
 * includes it: tests/unit/test_mods_master.py). The glue that reads and writes the settings is in
 * pc_launcher_content.inc.
 *
 * What counts as a mod: FFNY Online's community content in offline play ([content] community_offline), the player's own
 * packs (FFNY Studio's "Use in my game", switched off through [content] off), HD texture packs ([textures] enabled),
 * the community graphics pack ([graphics] enabled: ReShade / DLSS) and a language pack ([language] pack: a translation
 * is a mod; the game's own English or built-in French is not, so [language] game stays). Online keeps working: the
 * community content follows the online prompt's restart, the others do not matter for a match.
 *
 * The state before is kept in [content] mods_backup as text ("v1|c=1|t=1|g=0|lp=<pack id>|lo=<ids>"); empty means the
 * master switch was not used. */
#ifndef DEFJAM_MODS_MASTER_LOGIC_H
#define DEFJAM_MODS_MASTER_LOGIC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODS_TEXT 1024

typedef struct ModsState {
    int community;                  /* [content] community_offline: community content in offline play */
    int textures;                   /* [textures] enabled */
    int graphics;                   /* [graphics] enabled */
    char language_pack[96];         /* [language] pack ("" the game's own) */
    char content_off[MODS_TEXT];    /* [content] off: comma-separated ids switched off (community replacements, own packs) */
} ModsState;

/* ", "-separated ids: 1 when id is one of them. */
static inline int mods_list_has(const char *list, const char *id)
{
    size_t n = strlen(id);
    const char *p;
    if (!n) return 0;
    for (p = list; *p;) {
        while (*p == ',' || *p == ' ') p++;
        if (!strncmp(p, id, n) && (p[n] == ',' || p[n] == ' ' || !p[n])) return 1;
        while (*p && *p != ',') p++;
    }
    return 0;
}

/* Every own pack of local_ids (comma-separated) added to the switched-off list in out (no double entries); 0 when it
 * does not fit. */
static inline int mods_off_with(const char *list, const char *local_ids, char *out, size_t size)
{
    char item[128];
    const char *p;
    size_t o;
    if (snprintf(out, size, "%s", list) >= (int)size) return 0;
    o = strlen(out);
    for (p = local_ids; *p;) {
        size_t k = 0;
        while (*p == ',' || *p == ' ') p++;
        while (*p && *p != ',' && k < sizeof item - 1) item[k++] = *p++;
        item[k] = 0;
        if (!k || mods_list_has(out, item)) continue;
        if (o + k + 2 > size) return 0;
        o += (size_t)snprintf(out + o, size - o, "%s%s", o ? "," : "", item);
    }
    return 1;
}

/* The state with every mod off, from the current one and the own packs' ids; 0 when the list does not fit. */
static inline int mods_all_off(const ModsState *cur, const char *local_ids, ModsState *out)
{
    *out = *cur;
    out->community = 0;
    out->textures = 0;
    out->graphics = 0;
    out->language_pack[0] = 0;
    return mods_off_with(cur->content_off, local_ids, out->content_off, sizeof out->content_off);
}

/* 1 when every mod is off in s (every own pack of local_ids switched off, no language pack). */
static inline int mods_is_all_off(const ModsState *s, const char *local_ids)
{
    char item[128];
    const char *p;
    if (s->community || s->textures || s->graphics || s->language_pack[0]) return 0;
    for (p = local_ids; *p;) {
        size_t k = 0;
        while (*p == ',' || *p == ' ') p++;
        while (*p && *p != ',' && k < sizeof item - 1) item[k++] = *p++;
        item[k] = 0;
        if (k && !mods_list_has(s->content_off, item)) return 0;
    }
    return 1;
}

/* The text kept in [content] mods_backup; 0 when it does not fit. Text with '|' in it is refused (no id has one). */
static inline int mods_backup_encode(const ModsState *s, char *out, size_t size)
{
    if (strchr(s->language_pack, '|') || strchr(s->content_off, '|')) return 0;
    return snprintf(out, size, "v1|c=%d|t=%d|g=%d|lp=%s|lo=%s", s->community != 0, s->textures != 0, s->graphics != 0,
                    s->language_pack, s->content_off) < (int)size;
}

/* The state in a mods_backup text; 0 when it is not one (empty, another version, cut). */
static inline int mods_backup_decode(const char *text, ModsState *out)
{
    char copy[MODS_TEXT + 160], *field, *next;
    int seen = 0;
    memset(out, 0, sizeof *out);
    if (!text || strlen(text) >= sizeof copy) return 0;
    memcpy(copy, text, strlen(text) + 1);
    field = copy;
    next = strchr(field, '|');
    if (next) *next++ = 0;
    if (strcmp(field, "v1")) return 0;
    for (field = next; field; field = next) {
        next = strchr(field, '|');
        if (next) *next++ = 0;
        if (!strncmp(field, "c=", 2)) { out->community = atoi(field + 2) != 0; seen |= 1; }
        else if (!strncmp(field, "t=", 2)) { out->textures = atoi(field + 2) != 0; seen |= 2; }
        else if (!strncmp(field, "g=", 2)) { out->graphics = atoi(field + 2) != 0; seen |= 4; }
        else if (!strncmp(field, "lp=", 3)) { if (snprintf(out->language_pack, sizeof out->language_pack, "%s", field + 3) >= (int)sizeof out->language_pack) return 0; seen |= 8; }
        else if (!strncmp(field, "lo=", 3)) { if (snprintf(out->content_off, sizeof out->content_off, "%s", field + 3) >= (int)sizeof out->content_off) return 0; seen |= 16; }
    }
    return seen == 31;
}

#endif

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 90, "Turn all mods off" (the launcher's Content page): one button that starts the game as the original and one
 * that brings back what was on. The rules, without the game (header only; the unit test builds a driver that
 * includes it: tests/unit/test_mods_master.py). The glue that reads and writes the settings is in
 * pc_launcher_content.inc.
 *
 * What counts as a mod: FFNY Online's community content in offline play ([content] community_offline), the player's own
 * packs (FFNY Studio's "Use in my game", switched off one by one on the Content page), HD texture packs
 * ([textures] enabled), the community graphics pack ([graphics] enabled: ReShade / DLSS) and a language pack
 * ([language] pack: a translation is a mod; the game's own English or built-in French is not, so [language] game
 * stays). Online keeps working: the community content follows the online prompt's restart, the others do not matter
 * for a match.
 *
 * Build 94: a setting holds 1023 characters, which is about 60 pack ids, so the own packs that are off (own_off:
 * <data>/mod-manager/own-off.txt, any number) are kept apart from the community replacements that are off
 * (content_off: [content] off), and the state before is kept in a file too (mod-manager/mods-backup.txt) as text
 * ("v2|c=1|t=1|g=0|lp=<pack id>|lo=<community ids off>|po=<own ids off>"; v1, kept in a setting by builds 90 to 93,
 * has the own ids inside lo and is still read). No file means the master switch was not used.
 *
 * "Back on" restores a backup exactly. Without one (it is gone, or it cannot be read) it turns the community content
 * and every own pack on and leaves HD textures, the graphics pack and the language mod as they are. */
#ifndef DEFJAM_MODS_MASTER_LOGIC_H
#define DEFJAM_MODS_MASTER_LOGIC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODS_TEXT 1024                  /* a setting's text, terminator included */
#define MODS_OWN_TEXT 49152             /* the own packs' ids (512 packs, 80 characters each, at most) */
#define MODS_BACKUP_TEXT (MODS_TEXT + MODS_OWN_TEXT + 256)

typedef struct ModsState {
    int community;                  /* [content] community_offline: community content in offline play */
    int textures;                   /* [textures] enabled */
    int graphics;                   /* [graphics] enabled */
    char language_pack[96];         /* [language] pack ("" the game's own) */
    char content_off[MODS_TEXT];    /* [content] off: comma-separated ids of community replacements switched off */
    char own_off[MODS_OWN_TEXT];    /* the own packs switched off, comma-separated (own-off.txt) */
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

/* The next id of a comma-separated list into item (trimmed); the position after it, NULL at the end. */
static inline const char *mods_next_id(const char *p, char *item, size_t size)
{
    size_t k = 0;
    while (*p == ',' || *p == ' ') p++;
    if (!*p) return NULL;
    while (*p && *p != ',' && k < size - 1) item[k++] = *p++;
    while (*p && *p != ',') p++;
    while (k && item[k - 1] == ' ') k--;
    item[k] = 0;
    return p;
}

/* Every id of ids (comma-separated) added to list (no double entries); 0 when it does not fit. */
static inline int mods_list_add(char *list, size_t size, const char *ids)
{
    char item[128];
    const char *p = ids;
    size_t o = strlen(list);
    while ((p = mods_next_id(p, item, sizeof item)) != NULL) {
        size_t k = strlen(item);
        if (!k || mods_list_has(list, item)) continue;
        if (o + k + 2 > size) return 0;
        o += (size_t)snprintf(list + o, size - o, "%s%s", o ? "," : "", item);
    }
    return 1;
}

/* list without the ids of ids (comma-separated). */
static inline void mods_list_remove(char *list, const char *ids)
{
    char item[128], *copy = (char *)malloc(strlen(list) + 1);
    const char *p = list;
    size_t o = 0;
    if (!copy) return;
    while ((p = mods_next_id(p, item, sizeof item)) != NULL) {
        size_t k = strlen(item);
        if (!k || mods_list_has(ids, item)) continue;
        if (o) copy[o++] = ',';
        memcpy(copy + o, item, k);
        o += k;
    }
    copy[o] = 0;
    memcpy(list, copy, o + 1);
    free(copy);
}

/* A state read from the settings (build 90 to 93 kept an own pack's switch in [content] off): the ids of local_ids
 * (the own packs, comma-separated) that are in content_off move to own_off. 0 when own_off has no room. */
static inline int mods_normalise(ModsState *s, const char *local_ids)
{
    char item[128], *mine = (char *)malloc(strlen(s->content_off) + 1);
    const char *p = s->content_off;
    int ok;
    if (!mine) return 0;
    mine[0] = 0;
    while ((p = mods_next_id(p, item, sizeof item)) != NULL)
        if (item[0] && mods_list_has(local_ids, item) && !mods_list_add(mine, strlen(s->content_off) + 1, item)) { free(mine); return 0; }
    ok = mods_list_add(s->own_off, sizeof s->own_off, mine);
    if (ok) mods_list_remove(s->content_off, mine);
    free(mine);
    return ok;
}

/* The state with every mod off, from the current one and the own packs' ids; 0 when the list does not fit. */
static inline int mods_all_off(const ModsState *cur, const char *local_ids, ModsState *out)
{
    if (out != cur) *out = *cur;
    out->community = 0;
    out->textures = 0;
    out->graphics = 0;
    out->language_pack[0] = 0;
    return mods_list_add(out->own_off, sizeof out->own_off, local_ids);
}

/* The state with the community content and every own pack on; HD textures, the graphics pack and the language mod as
 * they are (the button's answer when there is no backup to bring back). */
static inline void mods_back_on_default(const ModsState *cur, const char *local_ids, ModsState *out)
{
    if (out != cur) *out = *cur;
    out->community = 1;
    mods_list_remove(out->own_off, local_ids);
    mods_list_remove(out->content_off, local_ids);
}

/* 1 when every mod is off in s (every own pack of local_ids switched off, no language pack). */
static inline int mods_is_all_off(const ModsState *s, const char *local_ids)
{
    char item[128];
    const char *p = local_ids;
    if (s->community || s->textures || s->graphics || s->language_pack[0]) return 0;
    while ((p = mods_next_id(p, item, sizeof item)) != NULL)
        if (item[0] && !mods_list_has(s->own_off, item) && !mods_list_has(s->content_off, item)) return 0;
    return 1;
}

/* How many of the own packs of local_ids are on in s, and how many there are. */
static inline void mods_own_count(const ModsState *s, const char *local_ids, int *on, int *total)
{
    char item[128];
    const char *p = local_ids;
    *on = *total = 0;
    while ((p = mods_next_id(p, item, sizeof item)) != NULL) {
        if (!item[0]) continue;
        (*total)++;
        if (!mods_list_has(s->own_off, item) && !mods_list_has(s->content_off, item)) (*on)++;
    }
}

/* The text kept in mods-backup.txt; 0 when it does not fit. Text with '|' in it is refused (no id has one). */
static inline int mods_backup_encode(const ModsState *s, char *out, size_t size)
{
    if (strchr(s->language_pack, '|') || strchr(s->content_off, '|') || strchr(s->own_off, '|')) return 0;
    return snprintf(out, size, "v2|c=%d|t=%d|g=%d|lp=%s|lo=%s|po=%s", s->community != 0, s->textures != 0, s->graphics != 0,
                    s->language_pack, s->content_off, s->own_off) < (int)size;
}

/* The state in a backup text (v1 or v2); 0 when it is not one (empty, another version, cut). A v1 backup has the own
 * packs inside lo: the caller normalises it (mods_normalise). */
static inline int mods_backup_decode(const char *text, ModsState *out)
{
    char *copy, *field, *next;
    int seen = 0, v2, ok = 1;
    memset(out, 0, sizeof *out);
    if (!text || strlen(text) >= MODS_BACKUP_TEXT) return 0;
    copy = (char *)malloc(strlen(text) + 1);
    if (!copy) return 0;
    memcpy(copy, text, strlen(text) + 1);
    field = copy;
    next = strchr(field, '|');
    if (next) *next++ = 0;
    v2 = !strcmp(field, "v2");
    if (!v2 && strcmp(field, "v1")) { free(copy); return 0; }
    for (field = next; ok && field; field = next) {
        next = strchr(field, '|');
        if (next) *next++ = 0;
        if (!strncmp(field, "c=", 2)) { out->community = atoi(field + 2) != 0; seen |= 1; }
        else if (!strncmp(field, "t=", 2)) { out->textures = atoi(field + 2) != 0; seen |= 2; }
        else if (!strncmp(field, "g=", 2)) { out->graphics = atoi(field + 2) != 0; seen |= 4; }
        else if (!strncmp(field, "lp=", 3)) { ok = snprintf(out->language_pack, sizeof out->language_pack, "%s", field + 3) < (int)sizeof out->language_pack; seen |= 8; }
        else if (!strncmp(field, "lo=", 3)) { ok = snprintf(out->content_off, sizeof out->content_off, "%s", field + 3) < (int)sizeof out->content_off; seen |= 16; }
        else if (v2 && !strncmp(field, "po=", 3)) { ok = snprintf(out->own_off, sizeof out->own_off, "%s", field + 3) < (int)sizeof out->own_off; seen |= 32; }
    }
    free(copy);
    return ok && seen == (v2 ? 63 : 31);
}

#endif

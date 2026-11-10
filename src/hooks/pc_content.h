/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* FFNY Online's community content: the packs the launcher syncs from our signed catalogue
 * (setup/mods.py sync, docs/content-packs.md).
 *
 * The sync writes <data>/mod-manager/content.ini: one section per installed catalogue pack with
 * its kind ("content": new fighters, clothing, venues, always on; "replacement": retextures and
 * language packs, on unless the player switched them off), type, version, content hash and its
 * place in the catalogue's fixed order. The player's switches are settings.ini [content] off
 * (a comma-separated list of pack ids). */
#ifndef PC_CONTENT_H
#define PC_CONTENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_CONTENT_MAX 64

typedef struct PcContentPack {
    char id[96];
    char name[160];
    char authors[256];
    char kind[16];          /* "content" | "replacement" */
    char type[32];          /* "language", "fighter-texture", "clothing", ... */
    char version[40];
    char language[16];      /* language packs: the ISO 639 code */
    char content_sha256[72];
    char description[512];
    int order;              /* the catalogue's install order (1 = first) */
    long long bytes;
    int installed;          /* its folder is in <data>/mods */
    int off;                /* a replacement the player switched off */
    int composed;           /* fighter-body, clothing, venue: install.json, composed by the launcher */
    int applied;            /* composed: content-files/files.ini lists it in applied */
} PcContentPack;

/* The installed catalogue packs in catalogue order (re-read on every call; content.ini is small).
 * Returns the count (at most max). */
int pc_content_list(PcContentPack *out, int max);

/* The settings.ini switch for a replacement pack ([content] off). Content packs cannot be off. */
int pc_content_is_off(const char *id);
void pc_content_set_off(const char *id, int off);

/* The online content fingerprint: FNV-1a over "id\0version\0content_sha256\n" of every installed
 * pack of kind "content", in catalogue order; 0 with none (rooms then match as before). A composed
 * pack counts only when the launcher's composition applied it (files.ini). Replacements and language
 * packs never count. RECOMP_TEST_CONTENT_PRINT=<hex> overrides it (tests). */
uint32_t pc_content_print(void);

/* At game start, after the launcher and before pc_language_start: the synced texture packs
 * (kind content, and replacements that are on) go beneath the player's own texture packs in
 * RECOMP_TEXTURE_PACKS, in catalogue order; the files the launcher composed for fighter-body,
 * clothing and venue packs (<data>/mod-manager/content-files, listed in its files.ini) are served
 * through the game-file redirect, ahead of <data>/override. */
void pc_content_start(const char *data_root);

/* A language pack id is a synced pack the player switched off (pc_language.c then skips it). */
int pc_content_language_blocked(const char *id);

/* pc_language_start, once its pack is loaded: the composed files the launcher built on top of that
 * pack's translation (content-files/lang/<id>/<path>, files.ini `lang=<id>|<path>`) are served from
 * now on in place of the plain composed copies, and their game paths are written to `paths` (at
 * most `max`): the language pack leaves those to the composed copies instead of rebuilding them.
 * Returns the count; an empty id (the pack was not used after all) goes back to the plain copies. */
int pc_content_language_files(const char *id, char (*paths)[256], int max);

/* RECOMP_OFFLINE_ONLY=1 (Studio's Test in a fight with an unreviewed pack): online is refused. */
int pc_content_offline_only(void);

#ifdef __cplusplus
}
#endif

#endif

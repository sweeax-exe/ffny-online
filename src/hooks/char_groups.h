/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 96: CHARACTER GROUPS (a community idea, owner approved). Community fighters that are the same character
 * ("Lil Wayne" and "Weezy", three "Spider-Man" versions) are one card in CHOOSE FIGHTER's COMMUNITY tab; picking it
 * opens a version window (the skin window's grid, skins_ui.inc's look): each version's picture, its version name,
 * creator and credits. The last version picked per group is remembered (<data>/character-choices.ini). Ungrouped
 * fighters stay single cards, the name search finds groups and versions, RANDOM draws among every version.
 *
 * The group comes from the signed catalogue (index-v7.json, the mod keys "character" and "version_name"; the account
 * service folds case, spacing and fancy letters and keeps a group's first accepted spelling) through content.ini
 * (character=, version_name=; setup/mods.py). It is host-side UI only: the chosen concrete fighter (its place or
 * number) is what is sent online, as before, so no protocol change and nothing that touches determinism.
 *
 * The pure parts (char_groups.c; tests/unit/test_char_groups_native.py compiles them natively) and the game side
 * (char_groups_game.c: content.ini's groups cached, the remembered choice, the test catalogue). */
#ifndef DEFJAM_CHAR_GROUPS_H
#define DEFJAM_CHAR_GROUPS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CG_NAME 64          /* a character or version name, with its NUL (the catalogue's are at most 40 characters) */
#define CG_MAX 512          /* grouped fighters (pc_content's PC_CONTENT_MAX) */

/* ---- pure parts ---- */

/* The key two spellings of one group share: ASCII letters lower-cased, digits, and any non-ASCII byte (UTF-8 letters,
 * which the account service already folded); spaces, punctuation and control characters dropped. "" for none. */
void char_group_key(const char *name, char *out, size_t size);

/* leader[i]: the index of the first of the n keys equal to keys[i], or -1 for an empty key (ungrouped). count[i]: the
 * size of i's group (1 when ungrouped). Returns the number of groups of two or more. */
int char_groups_assign(const char *const *keys, int n, int *leader, int *count);

/* The member a group's card shows, of `count` members (their catalogue orders): `remembered` (an index into the
 * members, -1 none) when valid, else the newest (the highest order; the first of equal ones). -1 when count < 1. */
int char_group_face(const int *orders, int count, int remembered);

/* The version name a member shows: its version_name, else the pack's name, else the fighter's name, else "?". */
const char *char_group_version_label(const char *version_name, const char *pack_name, const char *fighter_name);

/* One line of character-choices.ini ("group.<key>=<pack id>"): 1 and the key and pack id when it is one. */
int char_groups_parse_choice(const char *line, char *key, size_t key_size, char *pack, size_t pack_size);

/* ---- the game side (char_groups_game.c) ---- */

typedef struct CharGroupFighter {
    int fe_id;                  /* the fighter's place (100..127, a low place) or number (1000 and up) */
    char id[96];                /* its pack */
    char name[160];             /* the pack's title */
    char authors[256];          /* its creator(s) */
    char credits[512];          /* who made the work it uses (" | " between) */
    char character[CG_NAME];    /* the group's name as shown */
    char version_name[CG_NAME]; /* "" the pack's title */
    char key[CG_NAME];          /* char_group_key(character) */
    int order;                  /* the catalogue's order (higher: accepted later) */
} CharGroupFighter;

/* The installed new fighters that are in a group (content.ini character=, not skins), in catalogue order; cached,
 * read again at most every 3 s. RECOMP_TEST_CHAR_GROUPS=<feID>:<character>[:<version>][;...] (tests) sets or adds
 * groups over what content.ini says, for the installed fighter of each feID. */
int char_groups_fighters(const CharGroupFighter **out);
/* The grouped fighter of feID fe_id, or NULL (ungrouped or not installed). */
const CharGroupFighter *char_groups_find(int fe_id);
/* The pack id last picked from group `key` ("" none) and remembering one. */
void char_groups_remembered(const char *key, char *pack, size_t size);
void char_groups_remember(const char *key, const char *pack);

#ifdef __cplusplus
}
#endif

#endif

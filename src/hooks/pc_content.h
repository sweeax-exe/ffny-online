/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* FFNY Online's community content: the packs the launcher syncs from our signed catalogue
 * (setup/mods.py sync, docs/content-packs.md).
 *
 * The sync writes <data>/mod-manager/content.ini: one section per installed catalogue pack with
 * its kind ("content": new fighters, clothing, venues, always on; "replacement": retextures and
 * language packs, on unless the player switched them off), type, version, content hash and its
 * place in the catalogue's fixed order. The player's switches are settings.ini [content] off
 * (a comma-separated list of pack ids). The mods of a pack of several mods ("Pack (N mods)") are
 * sections of their own like any single pack, with bundle= and bundle_name= naming their pack;
 * the launcher installs and removes them together, and everything here (the switches, the online
 * print) still counts them one by one. */
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
    char bundle[96];        /* one mod of a pack of several mods: the pack's id ("" for a single pack) */
    char bundle_name[160];  /* ... and its title */
    int order;              /* the catalogue's install order (1 = first) */
    long long bytes;
    int installed;          /* its folder is in <data>/mods */
    int off;                /* a replacement the player switched off */
    int composed;           /* fighter-body, clothing, venue: install.json, composed by the launcher */
    int applied;            /* composed: content-files/files.ini lists it in applied */
    int fighter_fe_id;      /* a new fighter's front-end ID (its place, new_fighter_places.h), 0 none */
    int venue_fe_id;        /* build 82: a new venue's front-end ID (28..63), 0 none */
    char reviewed[16];      /* build 82: the day the team accepted this version, YYYY-MM-DD ("" unknown) */
    int local;              /* the player's own pack (FFNY Studio's "Use in my game", local.json): switchable,
                               never in the online print */
    int online_ok;          /* local: it only changes how things look (textures), so it may stay on online */
    int template_fe_id;     /* build 83: a new fighter's template (its spot on the select screen), 0 none */
    char template_name[48]; /* ... and that fighter's name ("D-Mob"), "" unknown */
    int skin;               /* build 85: a new fighter the team accepted as a skin (fighter_mode=skin) */
    int skin_of;            /* ... the stock fighter it is an outfit of (its front-end ID), -1 none */
    int official;           /* build 86: an official map of the FFNY team (catalogue kind "official-map"), always on */
    int rts_map;            /* ... a Run the Streets map (openworld rts.c loads it) */
    char venue_code[8];     /* ... its venue's code ("28A": V2BG_28A), "" unknown */
} PcContentPack;

/* Build 85, skins: a new-fighter pack the team accepted as an extra outfit of a stock fighter (same gameplay, new
 * look; no place, no Character row). The launcher composed its model files and lists it in content-files/files.ini
 * ("skin=<pack id>|<template feID>|<model>|<name>"), in catalogue order: the skins of one template are numbered
 * from 1 in that order (0 is the template's own look), the same on every PC with the same content print. */
#define PC_SKIN_MAX 64
typedef struct PcContentSkin {
    char id[96];            /* the pack id */
    int template_fe_id;
    char model[16];         /* V2IP_7xxA */
    char name[48];          /* the name in the game */
} PcContentSkin;
int pc_content_skins(PcContentSkin *out, int max);

/* The installed catalogue packs in catalogue order (re-read on every call; content.ini is small).
 * Returns the count (at most max). */
int pc_content_list(PcContentPack *out, int max);
/* Build 86: a small file of an installed pack's folder (<data>/mods/<id>/<name>, e.g. an official map's
 * bigarena.json) read into out (NUL-terminated); the bytes read, 0 when missing, -1 when larger than size - 1. */
int pc_content_pack_file(const char *id, const char *name, char *out, int size);

/* The settings.ini switch for a replacement pack or one of the player's own packs ([content] off). Community
 * content packs cannot be off. */
int pc_content_is_off(const char *id);
void pc_content_set_off(const char *id, int off);

/* The online content fingerprint: FNV-1a over "id\0version\0content_sha256\n" of every installed
 * pack of kind "content", in catalogue order; 0 with none (rooms then match as before). A composed
 * pack counts only when the launcher's composition applied it (files.ini). Replacements and language
 * packs never count. RECOMP_TEST_CONTENT_PRINT=<hex> overrides it (tests). */
uint32_t pc_content_print(void);

/* Build 90, community content in offline play (settings.ini [content] community_offline, default on; the
 * launcher's Content page and Basics). Off, a run leaves FFNY Online's synced community packs out: pc_content_list
 * and the composed files then hold only the player's own packs and the language packs, the print is 0, and
 * online is refused (netplay.c) until the game restarts with the content on (the online prompt's restart:
 * --content-update, for that run only). pc_content_session_online(1): this run was started for online. The game
 * decides once at its start (pc_content_decide, from pc_content_log_header or pc_content_start); the launcher
 * never does, so its Content page lists everything. */
void pc_content_session_online(int forced);
void pc_content_decide(void);
int pc_content_community_off(void);
int pc_content_online_allowed(void);

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

/* The player's own packs that keep this PC offline: on, installed, and not only textures (new clothing,
 * venues, fighters, venue props or collision: other PCs would not have them). Returns the count; names gets
 * their names (", " between; may be NULL). Online is refused while it is not 0 (PC_CONTENT_LOCAL_LINE). */
int pc_content_local_offline(char *names, int size);
#define PC_CONTENT_LOCAL_LINE "Your own packs are on: they work offline only. Turn them off in the launcher (Content) to play online."


/* Build 82: the host path of a game file the launcher composed and this run serves (content-files, e.g.
 * "venue.xml" with a new venue in it), so host code reads what the game reads. 0 when the file is not served. */
int pc_content_file(const char *game_path, char *out, int size);

/* Build 82, new community content is out: the count of packs a sync would install, update or remove (the
 * game's own `mods.py check` wrote <data>/mod-manager/news.ini), 0 none or not known; names gets their names
 * (" | " between, may be NULL). RECOMP_TEST_CONTENT_NEWS=<n> fakes n (tests and screenshots). */
int pc_content_news(char *names, int size);

/* Build 83: why a PC can lack a pack everyone else has. The launcher's sync writes the last outcome to
 * <data>/mod-manager/sync-result.ini and a failed `mods.py check` writes error= into news.ini (setup/mods.py);
 * the game logs both at its start ("[CONTENT] last sync ok, revision 5, 1 pack" / "[CONTENT] last sync FAILED:
 * <reason>"), puts them into the F9 report and tells the player in the online hub. */
typedef struct PcContentSync {
    char result[16];        /* "ok" | "failed" | "cancelled"; "" no sync-result.ini (a launcher before build 83) */
    char kind[16];          /* "clock" | "network" | "disk" | "catalogue" | "pack" ("" when ok) */
    char reason[320];       /* plain words */
    char time[24];          /* UTC, YYYY-MM-DDTHH:MM:SSZ */
    char ids[512];          /* the synced packs, comma-separated */
    char skipped[256];      /* packs this launcher cannot take yet (a newer build) */
    int revision;           /* the catalogue revision content.ini was written for */
    int packs;
} PcContentSync;
typedef struct PcContentNews {
    int known;              /* news.ini was read */
    int revision;           /* the catalogue revision the last good check saw, 0 none */
    int pending;            /* pack changes a sync would make */
    char checked[24];       /* UTC of the last check, good or failed */
    char kind[16];          /* the last check failed: as PcContentSync.kind; "" it worked */
    char error[320];
} PcContentNews;
/* 1 when sync-result.ini was read. RECOMP_TEST_CONTENT_SYNC=<reason> fakes a failed sync (screenshots). */
int pc_content_sync_result(PcContentSync *out);
/* 1 when news.ini was read. */
int pc_content_news_state(PcContentNews *out);
/* The catalogue revision content.ini was written for (0 none). */
int pc_content_revision(void);
/* The hub's plain line for a content problem, from these three: a failed sync (unless a later good check this
 * session saw nothing to change), else a failed check. 0 and "" when there is none. */
int pc_content_problem_text(const PcContentSync *sync, int have_sync, const PcContentNews *news, int installed,
                            char *out, int size);
/* The same from the files (cheap enough for every few seconds). */
int pc_content_problem(char *out, int size);
/* Game start and F9: the content state as log lines ("[CONTENT] ..." / "[REPORT] content: ..."). */
void pc_content_log_header(void);
void pc_content_report_line(void);
#define PC_CONTENT_FIX_LINE "Close the game and start it from the launcher, or check your internet and clock."
#define PC_CONTENT_LOAD_LINE "Close the game and start it from the launcher; if it stays, press F9 to send a report."
#define PC_CONTENT_CLOCK_LINE "Set the date and time to automatic in Windows, then start the game from the launcher."
/* The room list is empty while many are online on other content (the relay's STATUS Same, build 83). */
#define PC_CONTENT_SPLIT_LINE "Players with different community content can't see your rooms: close the game and start it from the launcher to update"

/* Build 82, the hub's NEW badge: a new fighter (its place, new_fighter_places.h) or new venue (feID 28..63) from a pack the
 * team accepted less than PC_CONTENT_NEW_DAYS days ago, which this player has not picked yet. picked: the player
 * picked it (READY with the fighter, a vote for the venue, a match on it); its badge goes. RECOMP_TEST_CONTENT_NEW=
 * all shows every new fighter and venue as new (screenshots). */
#define PC_CONTENT_NEW_DAYS 14
int pc_content_new_fighter(int fe_id);
int pc_content_new_venue(int fe_id);
void pc_content_picked_fighter(int fe_id);
void pc_content_picked_venue(int fe_id);
/* The days since a YYYY-MM-DD date, from today's date (UTC) given as days since 1970-01-01; -1 unreadable. */
int pc_content_days_since(const char *date, long today);

#ifdef __cplusplus
}
#endif

#endif

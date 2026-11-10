/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Discord Rich Presence, the parts without Windows or the game (discord_rpc.c): the IPC frames, the
 * JSON Discord takes, the text rules, and what each moment of the game shows. Plain C, so a unit test
 * builds it alone (tests/unit/test_discord_rpc_native.py). The pipe and its thread are in
 * discord_ipc.c, what the game is doing is read in discord_presence.c.
 *
 * Discord's local IPC (no SDK): a frame is a little-endian u32 opcode, a little-endian u32 length
 * and that many bytes of JSON. Opcode 0 HANDSHAKE {"v":1,"client_id":"..."}, 1 FRAME (commands and
 * their answers), 2 CLOSE, 3 PING (answered with 4 PONG, the same payload). */
#ifndef DEFJAM_DISCORD_RPC_H
#define DEFJAM_DISCORD_RPC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The FFNY Online application in Discord's Developer Portal: its name is what Discord shows after
 * "Playing", the large image is one of its Rich Presence art assets. Public, no secret involved. */
#define DRPC_CLIENT_ID      "1562214133242400993"
#define DRPC_LARGE_IMAGE    "ffny_logo"
#define DRPC_LARGE_TEXT     "FFNY Online"
#define DRPC_BUTTON1_LABEL  "Join the Discord"
#define DRPC_BUTTON1_URL    "https://discord.gg/ffnyonline"
#define DRPC_BUTTON2_LABEL  "Website"
#define DRPC_BUTTON2_URL    "https://play.defjam.online"

enum { DRPC_OP_HANDSHAKE = 0, DRPC_OP_FRAME = 1, DRPC_OP_CLOSE = 2, DRPC_OP_PING = 3, DRPC_OP_PONG = 4 };

#define DRPC_HEADER 8
#define DRPC_TEXT_MAX 128           /* Discord's limit for details, state and the image texts */
#define DRPC_TEXT_MIN 2             /* and its minimum: a shorter text is left out */
#define DRPC_KEY_MAX 32             /* an asset key */

/* One activity as it goes to Discord. An empty text is left out; start 0 means no timer. */
typedef struct DrpcActivity {
    char details[DRPC_TEXT_MAX + 1];
    char state[DRPC_TEXT_MAX + 1];
    char large_image[DRPC_KEY_MAX + 1];
    char large_text[DRPC_TEXT_MAX + 1];
    char small_image[DRPC_KEY_MAX + 1];     /* later: a per-mode picture; empty for now */
    char small_text[DRPC_TEXT_MAX + 1];
    long long start;                        /* unix seconds, the "elapsed" timer */
    int buttons;                            /* 1: the two buttons above */
} DrpcActivity;

/* Writes the 8-byte header for `len` bytes of payload into out. */
void drpc_put_header(uint8_t *out, uint32_t op, uint32_t len);
/* Reads one; returns 0 if fewer than 8 bytes were given. */
int drpc_get_header(const uint8_t *in, int have, uint32_t *op, uint32_t *len);
/* A whole frame (header and payload) into out; returns its length, or 0 if it does not fit. */
int drpc_frame(uint8_t *out, int cap, uint32_t op, const char *payload, int len);

/* `in` made fit for Discord into out (at most max bytes and size-1): control characters become
 * spaces, broken UTF-8 becomes '?', runs of blanks become one, the ends are trimmed, and a text
 * too long is cut at a character boundary. Returns the length. */
int drpc_clean(char *out, int size, const char *in, int max);
/* `in` as a JSON string body (without the quotes) appended at out+*pos; returns 0 if it did not fit. */
int drpc_json_escape(char *out, int size, int *pos, const char *in);

/* {"v":1,"client_id":"<id>"} */
int drpc_handshake_json(char *out, int size, const char *client_id);
/* {"cmd":"SET_ACTIVITY","args":{"pid":..,"activity":{..}},"nonce":".."}; activity NULL clears
 * the presence (no "activity"). Returns the length, 0 if it did not fit. */
int drpc_set_activity_json(char *out, int size, unsigned long pid, const DrpcActivity *activity, unsigned nonce);
/* 1 when two activities would show the same. */
int drpc_same(const DrpcActivity *a, const DrpcActivity *b);

/* ---- what the game shows (the composer) ----------------------------------------------------- */

enum DrpcWhere {
    DRPC_IN_LAUNCHER = 0,
    DRPC_IN_MENUS,          /* the game, offline: see `offline` */
    DRPC_IN_LOBBY,          /* the online hub, no room */
    DRPC_IN_ROOM,           /* a room of the hub, between matches */
    DRPC_IN_MATCH           /* a room's match (versus screen, loading, the fight, its results) */
};
enum DrpcOffline { DRPC_OFF_MENUS = 0, DRPC_OFF_MAIN, DRPC_OFF_STORY, DRPC_OFF_BATTLE };
enum DrpcFormat { DRPC_ONE_ON_ONE = 0, DRPC_TEAM, DRPC_ALL_VS_ALL };

#define DRPC_NAME 33

/* What discord_presence.c reads from the game; plain values only. */
typedef struct DrpcGameState {
    int where;                  /* DRPC_IN_* */
    int offline;                /* DRPC_OFF_*, for DRPC_IN_MENUS */
    int offline_fight;          /* 1 while an offline fight runs */
    char server[DRPC_NAME];     /* "Europe", "US East", "US West"; "" unknown */
    int ranked_search;          /* lobby: searching for a ranked match */
    /* the room */
    char mode[DRPC_NAME];       /* "One on One", "Team 2v2", "King of the Hill", ... */
    int players, max;
    int koth;                   /* King of the Hill room */
    int koth_king;              /* this player is the king */
    int koth_streak;            /* the king's wins in a row */
    /* the match */
    int format;                 /* DRPC_ONE_ON_ONE, DRPC_TEAM, DRPC_ALL_VS_ALL */
    char venue[49];
    int count, you;             /* fighters in the match, this player's position */
    char names[4][DRPC_NAME];   /* each position's player name; "" for a CPU */
    char fighters[4][DRPC_NAME];/* each position's fighter */
    int fighting;               /* the fight itself runs (not the versus screen or the results) */
} DrpcGameState;

/* The details and state lines and the image text for `g` (start and buttons are left as they are). */
void drpc_compose(const DrpcGameState *g, DrpcActivity *out);

#ifdef __cplusplus
}
#endif

#endif

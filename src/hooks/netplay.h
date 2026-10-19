/* Online play through a relay server (docs/netplay-protocol.md, docs/netplay.md).
 *
 * Two copies of the port, each with its own dump, play one match: the host is
 * player 1, the guest player 2, on both PCs. In the menus each side sees the
 * other's controller live, as a second pad. In a fight the two simulations run
 * in lockstep: every simulation step takes both players' pad-table entries from
 * a buffer both sides fill identically, a few steps ahead (the input delay), and
 * every random generator is seeded from the session, not the clock.
 *
 * Windows only for now; elsewhere every call does nothing. */
#ifndef DEFJAM_NETPLAY_H
#define DEFJAM_NETPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum NetplayState {
    NETPLAY_OFF = 0,        /* not started, or cancelled */
    NETPLAY_CONNECTING,     /* waiting for the server's WELCOME */
    NETPLAY_WAITING,        /* in a room, no second player yet (host) */
    NETPLAY_SYNCING,        /* both present, agreeing on the session */
    NETPLAY_CONNECTED,      /* ready: play */
    NETPLAY_FAILED          /* see netplay_status */
};

/* Start as the host (create a room) or join `room` (6 characters). `server` is
 * "host" or "host:port" (default port 27777); `name` is shown to the other
 * player. Any earlier session is ended first. Returns 0 if it could start. */
int netplay_host(const char *server, const char *name, int input_delay);
int netplay_join(const char *server, const char *name, const char *room);

/* End the session (BYE to the server) and give the controllers back. */
void netplay_stop(void);

enum NetplayState netplay_state(void);

/* A line for the launcher or overlay: the room code, the other player, the ping,
 * or what went wrong. */
void netplay_status(char *text, int size);

/* The room code once in a room ("" before). */
void netplay_room(char *code, int size);

/* 1 while a session is connected and the game should run in lockstep. */
int netplay_active(void);

/* Tests: start a session from RECOMP_NETPLAY=host|join, RECOMP_NETPLAY_SERVER and
 * RECOMP_NETPLAY_CODE_FILE, if they are set. Call once at start-up. */
void netplay_autostart(void);

/* ---- hooks from the game (src/recomp_manual.c) ------------------------------ */
void netplay_on_match_start(void);                  /* sub_001A4D90: a match is set up      */
void netplay_on_pad_pass(void);                     /* sub_001AD380: pad entries just read */
void netplay_on_step(void);                         /* sub_00087700: one simulation step    */
void netplay_on_pad_read(uint32_t actor);           /* sub_001BAA00: a fighter reads its pad */
/* Seed arguments on the stack at `esp` (after the return address); `which`
 * names the call site so the two sides derive the same value for it. */
void netplay_on_seed(uint32_t esp, int words, uint32_t which);

#ifdef __cplusplus
}
#endif

#endif

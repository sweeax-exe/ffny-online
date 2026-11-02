/* Discord Rich Presence: the connection to the Discord app on this PC (discord_ipc.c).
 *
 * One background thread owns the named pipe (\\.\pipe\discord-ipc-0 .. -9). It asks a provider
 * four times a second what to show; it connects lazily, tries again every `retry_ms` while Discord
 * is not running, reconnects after a broken pipe, answers Discord's pings, and sends the newest
 * activity at once on a new connection and then at most every `interval_ms` (Discord accepts about
 * one update per 15 s; anything in between is coalesced). Off: the presence is cleared and the pipe
 * closed. Nothing here ever blocks the game or the launcher: the provider must be cheap and
 * thread-safe. Windows only; elsewhere start returns 0 and the rest do nothing. */
#ifndef DEFJAM_DISCORD_IPC_H
#define DEFJAM_DISCORD_IPC_H

#include "discord_rpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fills *out and returns 1 to show it, or returns 0 for off (clear and disconnect). Called on the
 * presence thread. */
typedef int (*DipcProvider)(DrpcActivity *out, void *user);

typedef struct DipcConfig {
    const char *client_id;      /* NULL: DRPC_CLIENT_ID */
    const char *pipe_prefix;    /* NULL: "\\\\.\\pipe\\discord-ipc-"; tests name their own fake */
    int interval_ms;            /* 0: 15000 */
    int retry_ms;               /* 0: 15000 */
    int tick_ms;                /* 0: 250 */
} DipcConfig;

/* Starts the thread (once; a second call only replaces the provider). Returns 1 when it runs. */
int dipc_start(const DipcConfig *config, DipcProvider provider, void *user);
/* Ask the provider again now instead of at the next tick. */
void dipc_wake(void);
/* Clears the presence, closes the pipe and ends the thread (waits at most about 1.5 s). */
void dipc_stop(void);
/* 1 while connected to Discord (for the log and tests). */
int dipc_connected(void);

#ifdef __cplusplus
}
#endif

#endif

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* FFNY Online account: sign-in with Discord through the project's account service
 * (server/relay/accounts). Optional; online play works without it. Windows only for now. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum AccountState { ACCOUNT_OUT, ACCOUNT_CHECKING, ACCOUNT_WAITING, ACCOUNT_IN, ACCOUNT_FAILED };

void account_init(void);            /* loads the saved session (no network at start-up)         */
void account_refresh(void);         /* checks the saved session with the service, once         */
void account_sign_in(void);         /* opens the browser on Discord and waits for the result   */
void account_cancel(void);          /* stops waiting for the browser                           */
void account_sign_out(void);
void account_delete(void);          /* removes the account on the server, then signs out       */
enum AccountState account_state(void);

/* The name the game shows for this player: the Discord display name in the characters the
 * game's fonts have (1..24), or the username. 0 when not signed in. */
int account_game_name(char *out, int size);
/* "Masa (@masa)" while signed in; the reason after a failure; "" otherwise. */
void account_text(char *out, int size);
const char *account_privacy_url(void);

/* Match results and statistics. A finished online match's result goes to the service (signed in
 * only; json as POST /api/matches takes it); it counts once the other players' games agree, and
 * the statistics are fetched again a moment later. */
void account_report_match(const char *json);
/* From the crash handler: the same POST, synchronous on a thread of its own, waited for at most
 * timeout_ms, once per run. Copies json first and allocates nothing of ours. Returns the HTTP
 * status, 0 when no answer came in time, -1 when not sent (not signed in, already sent). */
int account_report_crash(const char *json, unsigned timeout_ms);
void account_fetch_stats(void);                 /* the statistics and the leaderboard, in the background */
void account_fetch_stats_page(int page);        /* the same with that leaderboard page (10 places, 0 = top 10) */
int account_stats_busy(void);
/* The statistics in the service's text form ("key=value" lines, newest fetch); 0 if none yet. */
int account_stats(char *out, int size);
/* Ranked play: a one-time ticket for the relay's ranked queue (32 hex digits and a 0), from the
 * service. Blocks for the request (call it off the game's threads); 0 when not signed in or the
 * service could not be reached. */
int account_ranked_ticket(char out[33]);
/* A bug report (bug_report.c): POST /api/report with this body, naming the account if signed in.
 * Blocks; returns the HTTP status (201: out holds the report's id), 0 if unreachable. */
int account_send_bug_report(const char *ctype, const void *body, int len, char *out, int size);
/* The note added after an online match to that report: POST /api/report/note (bug_report.c). */
int account_send_report_note(const char *ctype, const void *body, int len, char *out, int size);
/* The social features (social.cpp): one request to the account service with this session's token.
 * method "GET"/"POST"/..., path with its query (UTF-8), json the body or NULL; the answer goes to buf.
 * Blocks (off the game's threads only). receive_ms > 0: how long the answer may take (the long-poll).
 * live (may be NULL): while the request runs it holds the request's handle, so account_cancel_call
 * from another thread can end it; set it to NULL before each request. Returns the HTTP status, 0 when
 * unreachable or canceled, -1 when not signed in. A 401 signs out, as the other requests do. */
int account_call(const char *method, const char *path, const char *json, char *buf, int size, int receive_ms,
                 void *volatile *live);
void account_cancel_call(void *volatile *live);
/* A public file of the account service (the social features' avatars, GET /api/avatar/N.png): no
 * session token is sent. Blocks (off the game's threads only); *len gets the bytes read (a 200 only;
 * an answer of size - 1 bytes or more is cut). Returns the HTTP status, 0 when unreachable. */
int account_fetch(const char *path, void *buf, int size, int *len);
/* A large file for this account (FFNY Studio's zip, GET /api/studio/download/<file>): the same session
 * token and client name as account_call, from byte `from` (a Range request when not 0). Each piece of the
 * body of a 200 or 206 goes to sink(ctx, data, n); a sink returning 0 ends the request. *partial is 1 for
 * a 206 (the body starts at `from`) and is set before the first sink call. Blocks (off the game's threads
 * only). Returns the HTTP status, 0 when unreachable, cut off or ended by the sink, -1 when not signed in. */
int account_download(const char *path, unsigned long long from, int (*sink)(void *ctx, const char *data, unsigned n),
                     void *ctx, int *partial);
/* 1 when RECOMP_ACCOUNT_SERVER names a test account service on this PC (127.0.0.1 or localhost). */
int account_test_server_local(void);
/* The launcher's Change name dialog: the service's answer to POST /api/me/name ({"account": ...})
 * becomes the shown and saved name. */
void account_take_renamed(const char *json);

#ifdef __cplusplus
}
#endif

/* FFNY Online account: sign-in with Discord through the project's account service
 * (netplay/relay/accounts). Optional; online play works without it. Windows only for now. */
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

#ifdef __cplusplus
}
#endif

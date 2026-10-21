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
void account_fetch_stats(void);                 /* the statistics and the leaderboard, in the background */
int account_stats_busy(void);
/* The statistics in the service's text form ("key=value" lines, newest fetch); 0 if none yet. */
int account_stats(char *out, int size);

#ifdef __cplusplus
}
#endif

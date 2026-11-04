/* The hang watch (hang_watch.c): a host thread that logs a [HANG] block when the game stops moving. */
#ifndef DEFJAM_HANG_WATCH_H
#define DEFJAM_HANG_WATCH_H

#ifdef __cplusplus
extern "C" {
#endif

void hang_watch_start(void);            /* once, before the game runs */
void hang_watch_note_present(void);     /* a picture went out */
void hang_watch_note_update(void);      /* a game update starts (on the game thread) */

#ifdef __cplusplus
}
#endif

#endif

#ifndef DEFJAM_CINEMA_TIMING_H
#define DEFJAM_CINEMA_TIMING_H

/* Keep the original cinema renderer and its housekeeping, without allowing
 * SC04's minimum-step fallback to invent time on a zero-update frame. */
void defjam_cinema_render(void (*original)(void));

#endif

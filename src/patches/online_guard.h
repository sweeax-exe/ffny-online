/* FFNY Online: what single-player patches may not do while this PC is online.
 *
 * Upstream's cheats (src/patches/cheats.c) and the developer switches (src/patches/dev_unlock.c)
 * change a fighter's health, meter, size, the match's fighters and venue, or the profile. Online
 * fights run in lockstep (or rollback) on every PC of a room from the same inputs, so a change on
 * one PC desynchronises the match, and it would be cheating besides. While a session is open --
 * from connecting through the hub, rooms, matches and spectating, until it ends -- they do nothing.
 *
 * netplay_state() and netplay_active() are plain reads, cheap enough for every simulation step. */
#ifndef DEFJAM_ONLINE_GUARD_H
#define DEFJAM_ONLINE_GUARD_H

#include "hooks/netplay.h"

static inline int ffny_online_locked(void)
{
    enum NetplayState s = netplay_state();
    return netplay_active() || (s != NETPLAY_OFF && s != NETPLAY_FAILED);
}

#endif

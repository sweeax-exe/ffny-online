/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Who this PC muted, for voice and for text, for the whole game session.
 *
 * A mute used to live in two places that did not know each other: the VOICE CHAT page's per-player volume
 * (by relay slot, forgotten with the room) and the chat window's MUTE IN ROOM (by name, text only), so a
 * player who muted someone in the chat window still heard them, and one who turned a volume to 0 got them
 * back with the next room. Both now write here, and voice and text both read here.
 *
 * A player is known by their account when the relay named one (their chat lines carry it) and by their name
 * (the room's list); an entry matches either, and learns the other as soon as it is seen (a player muted by
 * name before they spoke is muted by account from their first line). The slot a player has in a room is not
 * a key: a slot changes with every room and is handed to the next player who joins. Pure logic, no Windows. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define VMUTE_NAME 33

/* The volume in percent (0..200) this PC plays that player at: 100 when nothing was set, 0 when muted. */
int vmute_percent(const char *name, unsigned account);

/* Sets it (clamped); 100 forgets the player. An entry found by the account or the name takes both. */
void vmute_set(const char *name, unsigned account, int percent);

/* Mute (0 %) or unmute (back to 100 %). */
void vmute_mute(const char *name, unsigned account, int on);

/* Shorthand: vmute_percent(...) == 0. */
int vmute_is_muted(const char *name, unsigned account);

/* A player was seen with this name and account (a chat line, the room's list): entries that knew one of them
 * learn the other. Cheap; call it as often as lines arrive. */
void vmute_learn(const char *name, unsigned account);

/* The list, for the screens: entry i's name ("" when only the account is known), account and percent; 0 past the end. */
int vmute_entry(int i, char *name, int size, unsigned *account, int *percent);

/* Forgets everything (tests; a sign-out). */
void vmute_clear(void);

#ifdef __cplusplus
}
#endif

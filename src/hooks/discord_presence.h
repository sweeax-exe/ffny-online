/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Discord Rich Presence for FFNY Online (discord_presence.c): "Playing FFNY Online" in the player's
 * Discord profile, with what they are doing: the launcher, the game's menus, Story or Battle mode,
 * the online lobby, a room, an online fight (mode, venue, fighter, opponents) or King of the Hill.
 *
 * The setting [online] discord_status (on by default; RECOMP_DISCORD_PRESENCE overrides it) turns
 * it on and off at once. Test copies (RECOMP_SETTINGS or RECOMP_PAD_SCRIPT set) leave Discord alone
 * unless RECOMP_DISCORD_PRESENCE is set; RECOMP_DISCORD_PIPE names another pipe (a test's fake
 * Discord, "\\.\pipe\name-" + 0..9), RECOMP_DISCORD_INTERVAL_MS and RECOMP_DISCORD_RETRY_MS replace
 * the 15 s update and retry periods. */
#ifndef DEFJAM_DISCORD_PRESENCE_H
#define DEFJAM_DISCORD_PRESENCE_H

#ifdef __cplusplus
extern "C" {
#endif

/* The launcher shows (pc_settings.c, before pc_launcher_run). */
void discord_presence_launcher(void);
/* The game runs in this process (end of pc_settings_init). */
void discord_presence_game(void);
/* Clear the presence and close the pipe: before the launcher hands over to the game's own process,
 * or when it closes. */
void discord_presence_end(void);
/* Every front-end call, on the game thread (recomp_manual.c funccall_hook): the screen names. */
void discord_presence_on_funccall(const char *text);

#ifdef __cplusplus
}
#endif

#endif

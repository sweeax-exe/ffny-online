// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// The Saves page's glue: where the save area and its backups are, whether the game is running on
// them, the automatic backups, and the folder / zip picker. The page is pc_launcher_saves.inc
// (drawn by pc_ui.cpp); the work is save_manager.cpp.
#pragma once

#ifdef __cplusplus
#include <cstdint>
#include <string>
#include "save_manager.h"

namespace pcsaves
{
saves::fs::path save_dir();         // <data>/save
saves::fs::path backup_dir();       // <data>/save-backups
int64_t now();
// Another process runs the game on this save area (or a copy of this install runs at all).
bool game_running();
// The folder, .zip or PS2 save (.psu) picker. Done when pick_poll returns true; `path` is "" when
// cancelled.
enum class Pick { FOLDER, ZIP, PS2 };
void pick_start(Pick what, void *window);
bool pick_poll(std::string &path);
} // namespace pcsaves

extern "C" {
#endif

/* The game starts in this process (pc_settings.c, after the launcher): marks the save area as in
 * use for as long as the process lives, then backs the saves up when no backup was made today.
 * RECOMP_SAVES_BACKUP=0 turns the backup off. */
void saves_game_starts(void);
/* Before an update is installed or the setup is opened for one (the launcher). */
void saves_backup_before_update(void);
/* The game is about to delete a folder (XAPI's tree delete, src/recomp_manual.c): when it is a
 * profile folder under UDATA/45410049, a copy goes to <data>/save-backups/quarantine first. */
void saves_game_deletes(const char *guest_path);

#ifdef __cplusplus
}
#endif

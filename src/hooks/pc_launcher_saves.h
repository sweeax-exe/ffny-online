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
// The folder or .zip picker. Done when pick_poll returns true; `path` is "" when cancelled.
void pick_start(bool zip, void *window);
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

#ifdef __cplusplus
}
#endif

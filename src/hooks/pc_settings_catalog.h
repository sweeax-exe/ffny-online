/* Every player-facing setting and the file they live in, with nothing of the running game: the
 * game (pc_settings.c) and the app (src/app) register the same table, so a setting the launcher
 * shows means the same thing in both. See pc_settings_catalog.c. */

#ifndef DEFJAM_PC_SETTINGS_CATALOG_H
#define DEFJAM_PC_SETTINGS_CATALOG_H

#include <stddef.h>
#include "recomp_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Room for the whole catalogue: the display table plus the input (64), launcher/overlay (16)
 * and debug (16) tables. */
#define PC_SETTINGS_MAX 128

/* A four-fighter match holds about 2 GiB of replacements. At 512 MiB the cache evicted and
 * re-decoded dozens of textures every two seconds in a fight on the Steam Deck. */
#define PC_TEXTURE_CACHE_MB 2048

/* Fill `out` with every setting, in the order the file is written. Returns the count. */
size_t pc_settings_catalog(RecompSetting *out, size_t max);

/* Register the catalogue and read `path` ("" for defaults only, nothing written). Logs what it
 * read, and writes a complete, commented file on first run and after an upgrade adds a setting.
 * Returns what recomp_settings_load returned (0 for defaults only). pc_display, pc_display_set,
 * pc_settings_path and pc_settings_save (pc_settings.h) then read and write that file. */
int pc_settings_open(const char *path);

#ifdef __cplusplus
}
#endif

#endif

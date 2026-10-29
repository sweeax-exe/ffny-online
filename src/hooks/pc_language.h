/* Language packs: translated text, subtitles, fonts and menu art (docs/language-packs.md). */

#ifndef DEFJAM_PC_LANGUAGE_H
#define DEFJAM_PC_LANGUAGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register [language] pack and story_subtitles. Returns how many were added. */
struct RecompSetting;
size_t pc_language_settings(struct RecompSetting *out, size_t max);

/* After the launcher: load the chosen pack from <data_root>/languages, rebuild its
 * files from the dump in game_dir (once; kept in <data_root>/cache/language) and
 * serve them in place of the originals. A pack that cannot be used is reported
 * and the game runs in its own language. data_root may be NULL. */
void pc_language_start(const char *data_root, const char *game_dir);

/* Apply the pack's change to default.xbe's data sections to the loaded image,
 * before the memory layout maps it. Nothing when no pack changes the XBE. */
void pc_language_patch_xbe(void *xbe, size_t size);

/* Whether every Story cutscene should show subtitles: [language] story_subtitles,
 * or with "auto" what the active pack asks for (src/patches/subtitles.c). */
int pc_language_all_subtitles(void);

/* Installed packs for the launcher: folder and display name; returns the count
 * (up to max). Packs that fail to parse are listed with their problem. */
typedef struct PcLanguagePack {
    char folder[64];
    char name[96];
    char problem[96];
} PcLanguagePack;
int pc_language_list(const char *data_root, PcLanguagePack *out, int max);

#ifdef __cplusplus
}
#endif

#endif

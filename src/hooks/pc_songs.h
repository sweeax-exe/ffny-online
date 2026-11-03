/* Songs: restored and player-supplied music (docs/research/custom-songs.md). */

#ifndef DEFJAM_PC_SONGS_H
#define DEFJAM_PC_SONGS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register the [songs] settings. Returns how many were added. */
struct RecompSetting;
size_t pc_songs_settings(struct RecompSetting *out, size_t max);

/* After pc_language_start: add the enabled extra songs to a copy of assets/djv2.sod (built on a
 * language pack's copy when one is served), keep it in <data_root>/cache/songs and serve it in
 * place of the original. Reports and plays the game's own soundtrack when that is not possible. */
void pc_songs_start(const char *data_root, const char *game_dir);

/* The extra songs in effect after pc_songs_start: count, and each one's folder under music/. */
int pc_songs_count(void);
const char *pc_songs_entity(int i);

/* The database's string hash (djb2 over the bytes and the terminating NUL). */
uint32_t pc_songs_name_hash(const char *s);

/* Append rows to a table of a SODB database (exposed for the native fixture). Returns 1 and a
 * malloc'd image on success; rows whose name already exists are skipped (*added counts the rest). */
int pc_songs_sod_add_rows(const uint8_t *in, size_t n, const char *name, const char *const *field_names,
                          int nfields, const char *const *values, int count, uint8_t **out, size_t *out_n,
                          int *added);

#ifdef __cplusplus
}
#endif

#endif

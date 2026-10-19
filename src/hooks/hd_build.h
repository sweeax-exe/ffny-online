/* Making the HD texture packs from the launcher, for a player who left them out at
 * install time: the same local Lanczos 4x upscale the setup runs (scripts/build-hd-pack.py),
 * from the player's own dump, with its progress for a bar. Windows only for now. */
#ifndef DEFJAM_HD_BUILD_H
#define DEFJAM_HD_BUILD_H

#ifdef __cplusplus
extern "C" {
#endif

enum HdBuildState { HD_BUILD_IDLE, HD_BUILD_RUNNING, HD_BUILD_DONE, HD_BUILD_FAILED };

/* Start the upscale in the background. Returns 0 if it started; otherwise the
 * reason is in hd_build_status. */
int hd_build_start(void);

/* Stop a running upscale (its finished images are kept and reused next time). */
void hd_build_cancel(void);

/* The state, images done of total (0 of 0 while it prepares), and a line for the
 * screen. When DONE, `packs` holds the pack names to enable, ';'-separated. */
enum HdBuildState hd_build_status(int *done, int *total, char *text, int text_size,
                                  char *packs, int packs_size);

/* Free space on the data folder's drive in GiB, or -1 when unknown. */
double hd_build_free_gib(void);

#ifdef __cplusplus
}
#endif

#endif

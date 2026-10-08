/* The launcher and the in-game overlay: one settings screen (pc_ui.cpp), two ways to reach it. */

#ifndef DEFJAM_PC_UI_H
#define DEFJAM_PC_UI_H

#include <stddef.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#include "recomp_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- settings and the decision to show the launcher (pc_ui_settings.c) ------ */

/* Fill `out` with the [launcher] and [ui] settings. Returns the count. */
size_t pc_ui_settings(RecompSetting *out, size_t max);

/* Whether to show the launcher before the game starts. It is shown on every
 * launch unless the player turned "skip" on, with these exceptions, in order:
 * a scripted or test run (RECOMP_SETTINGS=none, a pad script, RECOMP_NO_LAUNCHER)
 * never shows it; `--no-launcher` on the command line skips it; `--launcher`, or
 * Shift held at start, shows it even when it is skipped. */
int pc_launcher_wanted(int skip_setting, int force_flag, int no_flag, int shift_held,
                       int test_run);

#if defined(_WIN32)
/* ---- the in-game overlay (pc_ui.cpp) ------------------------------------------ */

/* The window the overlay draws into and takes input from. */
void pc_ui_set_window(HWND window);

/* The present step's overlay hook (toolkit d3d8_present_set_overlay). */
void pc_ui_overlay(void *device, void *context, void *window_rtv, unsigned width, unsigned height,
                   void *user);

/* Offer a window message to the overlay, on the window's thread. Returns non-zero
 * when the overlay used it (the overlay key, a key being captured for a binding)
 * and the message should go no further. */
int pc_ui_window_message(HWND window, UINT msg, WPARAM wp, LPARAM lp);

int pc_ui_overlay_open(void);

/* Read the overlay's hotkeys and size again after a setting changed. */
void pc_ui_apply_settings(void);

/* ---- the launcher (pc_launcher.cpp) ----------------------------------------------- */

/* Show the launcher and run it until the player plays or quits. Returns 1 to go
 * on and start the game, 0 to quit. */
int pc_launcher_run(void);

#else
/* The overlay and the launcher draw through Direct3D 11 and take Win32 window
 * messages. Off Windows there is neither yet: the settings file and the
 * environment are the way in, and the game starts at once. */
static inline int  pc_ui_overlay_open(void)   { return 0; }
static inline void pc_ui_apply_settings(void) { }
#endif

#ifdef __cplusplus
}
#endif

#endif

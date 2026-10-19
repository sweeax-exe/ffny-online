/* The launcher's updater. A signed manifest on the update server names the newest build
 * and its setup; when it is newer than this installation, the launcher offers it, and on
 * yes downloads the setup, checks it against the manifest's SHA-256 and starts it in its
 * update mode (setup/main.cpp --update), which rebuilds the game from the installed game
 * files. The game itself is never downloaded: every PC builds its own from its own dump.
 * See docs/updates.md. Windows only. */
#ifndef DEFJAM_UPDATER_H
#define DEFJAM_UPDATER_H

#ifdef __cplusplus
extern "C" {
#endif

enum UpdaterState {
    UPDATER_IDLE,           /* not checked yet */
    UPDATER_CHECKING,
    UPDATER_CURRENT,        /* this is the newest build */
    UPDATER_AVAILABLE,      /* a newer build: updater_info */
    UPDATER_DOWNLOADING,    /* updater_progress */
    UPDATER_INSTALLING,     /* the setup builds the new version in the background: updater_progress */
    UPDATER_INSTALLED,      /* done: updater_restart starts it */
    UPDATER_STARTED,        /* (older flow) the setup window runs: close the launcher */
    UPDATER_FAILED          /* updater_message says why; the game still plays */
};

/* Start the newly installed version (its stable launcher) and let this one close. */
void updater_restart(void);

/* Look for an update in the background (once per call; the launcher calls it at start). */
void updater_check(void);

enum UpdaterState updater_state(void);

/* This build's number (config/remaster-version.txt), or 0 if unknown. */
int updater_local_build(void);

/* The offered build: its number, version text and release notes. */
void updater_info(int *build, char *version, int version_size, char *notes, int notes_size);

/* 1 when this copy can update itself (it was installed by setup); 0 for a development
 * checkout, which updates from its source instead. */
int updater_can_install(void);

/* Download the offered setup and start it. */
void updater_install(void);

/* Download progress, 0..1. */
double updater_progress(void);

/* A line for the screen: what happened or went wrong. */
void updater_message(char *text, int size);

#ifdef __cplusplus
}
#endif

#endif

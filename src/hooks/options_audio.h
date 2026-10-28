/* The game's own audio options (Options > Audio and the pause menu's sliders) after a restart.
 * The volumes are saved with the global "Options" save and read back at start, but nothing in the
 * game hands the loaded values to the sound system, and both menus show the sound system's live
 * volume, so every start came back at the defaults. src/hooks/options_audio.c applies them. */
#ifndef DEFJAM_OPTIONS_AUDIO_H
#define DEFJAM_OPTIONS_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

/* From the FuncCall hook (src/recomp_manual.c), on the game thread: `before` ahead of the front
 * end's call, `after` once it returned. `text` is the call, e.g. "Option.SetMusicVolume(40)". */
void options_audio_before_funccall(const char *text);
void options_audio_after_funccall(const char *text);

/* The decision, apart from guest memory (tests/unit/test_options_audio_native.py). `now` are the
 * three stored volumes (music, effects, announcer), `seen` what was stored at the last look (-1:
 * not yet looked), `apply` = whether a change found now is one to hand to the sound system.
 * Writes the volumes to apply (or -1) to `out`, updates `seen`; returns how many to apply. */
int options_audio_decide(const unsigned char now[3], int seen[3], int apply, int out[3]);

#ifdef __cplusplus
}
#endif

#endif

/* Custom music: the player's own songs as the game's soundtrack.
 *
 * Design (why it is safe online):
 *  - The songs never enter the game. A host thread (custom_music.cpp) decodes them (music_decode.c) and
 *    plays them on an audio voice of its own, outside the guest's memory and outside the emulated APU,
 *    so nothing the rollback saves or restores, nothing the lockstep checksum covers and no guest RNG or
 *    timing is involved.
 *  - The game's own music keeps running exactly as before (the director, its streams and the sound
 *    engine's memory are untouched); while one of the player's songs plays, it is only made silent, by
 *    setting its music players' volume to 0 with the engine's own per-player call (sub_00132BA0, the step
 *    the MUSIC slider ends in; custom_music_guest.c). That is the state a player who sets MUSIC to 0 is
 *    in, which online play already handles; the call
 *    is made on the game thread at the clean point between two game updates (sub_0002C530_enter), never
 *    in a rollback re-run step, and allocates nothing.
 *  - Our volume follows the game's: the music volume the game last asked for (slider, ducking and its
 *    fades; [0x33EA18], written only by the game's two music volume setters).
 *  - The game's music events say when music plays: a stream opened (sub_0001D5B0) starts or resumes our
 *    song, a stream stopped (sub_0001D7C0) pauses it, the director's track change (sub_0003BE50) is
 *    where "mix" hands back from the game's track to the next song of ours.
 * Settings: [music] mode (auto, mine, mix, original) and shuffle; RECOMP_MUSIC_DIR points at another
 * folder (tests), RECOMP_MUSIC_GAIN (percent, default 50) sets how loud the songs are against the game. */
#ifndef DEFJAM_CUSTOM_MUSIC_H
#define DEFJAM_CUSTOM_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* After the launcher, as the game starts: reads the settings and the folder; starts nothing when there
 * is nothing to play. `data_root` may be NULL. */
void custom_music_game_starts(const char *data_root);

/* Game-thread events (src/recomp_manual.c). `rerun` is 1 inside a rollback re-run step. */
void custom_music_on_update(int rerun, int in_fight);
void custom_music_stream_open(int rerun);
void custom_music_stream_stop(int rerun);
void custom_music_track_change(int rerun);

/* custom_music.cpp to custom_music_guest.c: whether the game's music should be silent now, and the
 * music volume the game asked for (0..100). */
int custom_music_active(void);
int custom_music_want_mute(void);
void custom_music_set_game_volume(int volume);
void custom_music_set_in_fight(int in_fight);
void custom_music_note(int playing);        /* the game's stream opened (1) or stopped (0) */
void custom_music_note_track_change(void);

#ifdef __cplusplus
}
#endif

#endif

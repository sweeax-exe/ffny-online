/* Song settings shared by the game, app and native fixtures. */
#include <string.h>
#include "recomp_settings.h"
#include "pc_songs.h"

size_t pc_songs_settings(RecompSetting *out, size_t max)
{
    size_t n = 0;
    if (max < 7) return 0;
    memset(out, 0, sizeof(*out) * 7);
#define ADD(...) do { RecompSetting s_ = { __VA_ARGS__ }; out[n++] = s_; } while (0)
    ADD("songs", "restored", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, "RECOMP_SONGS_RESTORED", RECOMP_SETTING_RESTART,
        "Restored songs: music cut from the release whose audio is still on the disc (\"Holler If Ya Hear Me\", "
        "2Pac) in the Beatbox and the playlist, like the other songs.");
    ADD("songs", "custom", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, "RECOMP_SONGS_CUSTOM", RECOMP_SETTING_RESTART,
        "Your own songs: music files (MP3, FLAC, Ogg, WAV) in the songs folder of the data folder and in the "
        "folders below are converted once and play in the Beatbox, the menus and fights like the game's songs.");
    ADD("songs", "folders", RECOMP_SETTING_STRING, 0, 0, 0, NULL, "RECOMP_SONGS_FOLDERS", RECOMP_SETTING_RESTART,
        "More folders of your own songs, separated by semicolons (the songs folder in the data folder is always read).");
    ADD("songs", "skip_pad", RECOMP_SETTING_STRING, 0, 0, 0, NULL, NULL, RECOMP_SETTING_RESTART,
        "Pad buttons that skip to the next song, in the menus and in fights: pressed together, it acts when the last "
        "one goes down. The game also sees those buttons. Alternatives are separated by commas; button names as in "
        "[gamepad]. Empty for none.", "right_stick_click+dpad_right");
    ADD("songs", "restart_pad", RECOMP_SETTING_STRING, 0, 0, 0, NULL, NULL, RECOMP_SETTING_RESTART,
        "Pad buttons that play the song again from its start. Empty for none.", "right_stick_click+dpad_left");
    ADD("songs", "skip_key", RECOMP_SETTING_STRING, 0, 0, 0, NULL, NULL, RECOMP_SETTING_RESTART,
        "The key that skips to the next song. Empty for none.", "F8");
    ADD("songs", "restart_key", RECOMP_SETTING_STRING, 0, 0, 0, NULL, NULL, RECOMP_SETTING_RESTART,
        "The key that plays the song again from its start. Empty for none.", "F7");
#undef ADD
    return n;
}

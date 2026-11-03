/* Song settings shared by the game, app and native fixtures. */
#include <string.h>
#include "recomp_settings.h"
#include "pc_songs.h"

size_t pc_songs_settings(RecompSetting *out, size_t max)
{
    size_t n = 0;
    if (max < 1) return 0;
    memset(out, 0, sizeof(*out) * 1);
#define ADD(...) do { RecompSetting s_ = { __VA_ARGS__ }; out[n++] = s_; } while (0)
    ADD("songs", "restored", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, "RECOMP_SONGS_RESTORED", RECOMP_SETTING_RESTART,
        "Restored songs: music cut from the release whose audio is still on the disc (\"Holler If Ya Hear Me\", "
        "2Pac) in the Beatbox and the playlist, like the other songs.");
#undef ADD
    return n;
}

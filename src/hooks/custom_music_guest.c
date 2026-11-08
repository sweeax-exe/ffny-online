/* Custom music, the game-thread side (custom_music.h): reads the music volume the game asked for and
 * silences or restores the game's own music at its music players.
 *
 * The game's music plays on the sound engine's music players: the main one, handle [0x331D08]
 * (0x11000001), and the overlay, handle [0x2DBC40] (0x11000004), -1 when closed. The game's own music
 * volume call (sub_0001D040 -> sub_00133240) is gated on an engine byte and hands the volume to each
 * player with sub_00132BA0(player, volume 0..100), which keeps it at player+4 and sets the player's
 * mixer voice. sub_001335B0(handle) gives the player for a handle (0 when there is none).
 *
 * While one of the player's songs is the music, each open music player is set to 0 with that same call,
 * and the volume it had is kept here; when the game raises it meanwhile (a new track opens with the
 * engine's default, a fade), that is kept and it is set to 0 again at the next update. Back to the game's
 * turn, each player gets the kept volume. The players live in the sound engine's memory, which a rollback
 * load leaves alone; the calls are made at the clean point between two game updates, never in a re-run
 * step, and allocate nothing. Nothing of the game state, its timing or its random numbers is touched. */
#include "custom_music.h"
#include <stdio.h>
#include <stdlib.h>
#include "guest_call.h"

#define MUSIC_ASKED     0x0033EA18u     /* int: the music volume the game last asked for (slider and ducking) */
#define MATCH_PHASE     0x003B8F30u     /* int: 0 start, 1 fighting, 2 decided, 3 finished */
#define PLAYERS         2
static const uint32_t k_handle_at[PLAYERS] = { 0x00331D08u, 0x002DBC40u };   /* main, overlay */

void sub_001335B0(void);                /* (handle) -> player, 0 when none; cdecl */
void sub_00132BA0(void);                /* (player, volume 0..100): the player's volume; cdecl */

static int g_audio_up;                  /* a music stream has opened */
static int g_muted;                     /* players set to 0 by us */
static uint32_t g_player[PLAYERS];      /* the player we set to 0 ... */
static int g_kept[PLAYERS];             /* ... and the volume it had */
static unsigned g_mutes, g_restores, g_updates;
static int g_last_asked = -1;

static uint32_t player_of(int i)
{
    uint32_t h = MEM32(k_handle_at[i]);
    if (h == 0xFFFFFFFFu || h == 0)
        return 0;
    return guest_call(sub_001335B0, g_ecx, 1, &h);
}

static void player_volume(uint32_t player, int volume)
{
    uint32_t args[2];
    args[0] = player;
    args[1] = (uint32_t)volume;
    guest_call(sub_00132BA0, g_ecx, 2, args);
}

static int asked(void)
{
    int v = (int)MEM32(MUSIC_ASKED);
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

/* The save's options load behind the Press Start screen: until the main menu, the MUSIC volume the
 * game asks for is its default, not the player's slider, and the intro and title keep their own sound. */
static int g_options_ready;
void custom_music_options_ready(void) { g_options_ready = 1; }

void custom_music_on_update(int rerun, int in_fight)
{
    int want, v, i, zeroed = 0;
    if (rerun || !g_audio_up)
        return;
    g_updates++;
    v = asked();
    if (v != g_last_asked) {
        if (g_last_asked < 0 || (v % 10) == 0 || v == 0)
            fprintf(stderr, "[MUSIC] game music volume %d\n", v);
        g_last_asked = v;
    }
    custom_music_note_phase((int)MEM32(MATCH_PHASE));    /* read only: a new fight, a new song */
    if (in_fight)
        g_options_ready = 1;
    custom_music_set_game_volume(g_options_ready ? v : 0);
    custom_music_set_in_fight(in_fight);
    if (!g_options_ready)
        return;
    want = custom_music_active() && custom_music_want_mute();
    if (want) {
        for (i = 0; i < PLAYERS; i++) {
            uint32_t p = player_of(i);
            int now;
            if (!p)
                continue;
            now = (int)(int8_t)MEM8(p + 4u);
            if (now == 0 && p == g_player[i])
                continue;
            if (p != g_player[i] || now != 0) {
                g_player[i] = p;
                g_kept[i] = now;
            }
            if (now != 0) {
                player_volume(p, 0);
                zeroed++;
            }
        }
        if (zeroed) {
            g_mutes++;
            if (!g_muted || g_mutes <= 20 || g_mutes % 100 == 0)
                fprintf(stderr, "[MUSIC] the game's own music silenced (#%u at update %u, %d player(s), kept %d/%d, "
                        "asked %d)%s\n", g_mutes, g_updates, zeroed, g_kept[0], g_kept[1], v,
                        in_fight ? " in a fight" : "");
            g_muted = 1;
        }
    } else if (g_muted) {
        for (i = 0; i < PLAYERS; i++) {
            uint32_t p = player_of(i);
            if (p && p == g_player[i] && MEM8(p + 4u) == 0 && g_kept[i] > 0)
                player_volume(p, g_kept[i]);
            g_player[i] = 0;
        }
        g_muted = 0;
        g_restores++;
        fprintf(stderr, "[MUSIC] the game's own music back (silenced %u times, restored %u)\n", g_mutes, g_restores);
    }
    if (getenv("RECOMP_MUSIC_TRACE") && g_updates % 300 == 1) {
        uint32_t p0 = player_of(0), p1 = player_of(1);
        fprintf(stderr, "[MUSIC] trace: handles %08X/%08X players %08X/%08X volumes %d/%d engine %u\n",
                MEM32(k_handle_at[0]), MEM32(k_handle_at[1]), p0, p1, p0 ? (int8_t)MEM8(p0 + 4u) : -1,
                p1 ? (int8_t)MEM8(p1 + 4u) : -1, MEM8(0x3AD934u));
    }
    fflush(stderr);
}

void custom_music_stream_open(int rerun)
{
    if (rerun) return;
    g_audio_up = 1;
    custom_music_note(1);
}

void custom_music_stream_stop(int rerun)
{
    if (rerun) return;
    custom_music_note(0);
}

void custom_music_track_change(int rerun)
{
    if (rerun) return;
    custom_music_note_track_change();
}

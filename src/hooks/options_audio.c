/* The game's audio options survive a restart.
 *
 * Players reported that the volumes they set came back at the defaults on every start. The game
 * keeps music, sound effects and announcer volume as one byte each (0..100) in its global data,
 * MEM32(component 0x18 + 4) + 4 + 0x53..0x55: Option.SetMusicVolume and friends (handlers
 * 0x488E0, 0x489C0, 0x48AA0) and the pause menu's ACCEPT CHANGES (sub_000CD020) write them there
 * and hand them to the sound system. They are saved with the global "Options" save (file offset
 * 0x93..0x95) and the save is read back at start (MemCard.BootupLoad; sub_00014540 copies it into
 * the global data and re-applies the vibration setting), but no code hands the loaded volumes to
 * the sound system, which starts at its own defaults (sub_0003C6E0: music 0.85). Option.Get*Volume
 * and the pause menu read the sound system, not the stored bytes, so both menus showed the
 * defaults too, and the next save wrote them back over the player's values.
 *
 * Here, on the front end's calls: when the stored volumes change by something other than an
 * Option.* call (the load, after the first MemCard load call), they are applied with the game's
 * own Option.Set*Volume handlers. A change made by an Option.* call or by the pause menu is
 * already live; re-applying it would only repeat the same value. The handlers also mark the
 * global data as changed (flag +0x1AC3, which makes the next options/beatbox/extras save write
 * it); that flag is put back, since nothing changed from the save's point of view. */
#include "options_audio.h"
#include <stdio.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"

#define COMPONENTS      0x002FB148u     /* the game's component table, MEM32(table + 4 * id) */
#define GLOBAL_DATA     0x18u           /* the component that owns the global save's data */
#define GLOBAL_DIRTY    0x1AC3u         /* byte in it: the options changed since the last save */
#define VOLUMES         0x57u           /* MEM32(data + 4) + 0x57: music, effects, announcer */

unsigned int defjam_fe_call(const char *command);      /* src/recomp_manual.c */

static int g_seen[3] = { -1, -1, -1 };
static int g_armed;                                     /* a memory-card load has been asked for */

int options_audio_decide(const unsigned char now[3], int seen[3], int apply, int out[3])
{
    int i, n = 0;
    for (i = 0; i < 3; i++) {
        out[i] = -1;
        if (seen[i] == (int)now[i])
            continue;
        if (apply && seen[i] >= 0 && now[i] <= 100) {
            out[i] = now[i];
            n++;
        }
        seen[i] = now[i];
    }
    return n;
}

static void look(int apply)
{
    static const char *const setters[3] = {
        "Option.SetMusicVolume(%d)", "Option.SetSFXVolume(%d)", "Option.SetAnnouncerVolume(%d)" };
    uint32_t data = MEM32(COMPONENTS + 4u * GLOBAL_DATA), options;
    unsigned char now[3];
    int out[3], i;
    uint8_t dirty;

    if (!data || !(options = MEM32(data + 4u)))
        return;
    for (i = 0; i < 3; i++)
        now[i] = MEM8(options + VOLUMES + (uint32_t)i);
    if (!options_audio_decide(now, g_seen, apply, out))
        return;
    dirty = MEM8(data + GLOBAL_DIRTY);
    for (i = 0; i < 3; i++) {
        char command[48];
        if (out[i] < 0)
            continue;
        snprintf(command, sizeof(command), setters[i], out[i]);
        defjam_fe_call(command);
    }
    MEM8(data + GLOBAL_DIRTY) = dirty;
    fprintf(stderr, "[OPTIONS] saved volumes applied: music %d, effects %d, announcer %d\n",
            now[0], now[1], now[2]);
    fflush(stderr);
}

void options_audio_before_funccall(const char *text)
{
    (void)text;
    look(g_armed);
}

void options_audio_after_funccall(const char *text)
{
    if (!strncmp(text, "MemCard.", 8) && strstr(text, "Load"))
        g_armed = 1;
    look(g_armed && strncmp(text, "Option.", 7) != 0);
}

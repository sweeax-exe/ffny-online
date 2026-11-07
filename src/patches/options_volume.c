/*
 * The volumes set in Options come back after a restart.
 *
 * Option.SetMusicVolume / SetSFXVolume / SetAnnouncerVolume (sub_000488E0, sub_000489C0,
 * sub_00048AA0) store the value, 0-100, as a byte of the profile manager B (B + 0x53 music,
 * + 0x54 effects, + 0x55 announcer), hand it to the mixer (sub_00036940, sub_000334E0,
 * sub_00033550: cdecl, one float, 0-100) when the audio system is up (MEM32(0x2FB148 + 3 * 4),
 * as the setters test it through sub_00012CF0), and mark the options for saving. The bytes are
 * part of the options save (B + 0x4C .. B + 0x14DC, the "Options" file), and the load
 * (sub_00014540) copies them back; but nothing hands them to the mixer, which keeps its start-up
 * 0.85. The Options screen then reads the mixer (Option.Get*: 85), and the next save writes 85
 * over what the player chose. So after a load, the saved bytes go to the mixer the way the
 * setters send them. RECOMP_OPTIONS_VOLUME=0 leaves the game as it was.
 */
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp/gen/recomp_types.h"
#include "hooks/guest_call.h"
#include "options_volume.h"

extern void sub_00036940(void);         /* music volume to the mixer */
extern void sub_000334E0(void);         /* effects */
extern void sub_00033550(void);         /* announcer */

#define AUDIO_UP 0x2FB154u              /* 0x2FB148 + 3 * 4: what sub_00012CF0(3) reads */

void options_volume_after_load(uint32_t manager)
{
    const char *e = getenv("RECOMP_OPTIONS_VOLUME");
    if ((e && *e == '0') || !manager || !MEM32(AUDIO_UP)) return;
    void (*apply[3])(void) = { sub_00036940, sub_000334E0, sub_00033550 };
    static const char *const name[3] = { "music", "effects", "announcer" };
    static const uint32_t live[3] = { 0x34037Cu, 0x33EA54u, 0x33EC20u };   /* the mixer's own copies */
    for (int i = 0; i < 3; i++) {
        uint8_t v = MEM8(manager + 0x53 + i);
        if (v > 100) continue;                                  /* not a saved value */
        float f = (float)v;
        uint32_t bits;
        memcpy(&bits, &f, sizeof bits);
        guest_call(apply[i], 0, 1, &bits);
        fprintf(stderr, "[OPTIONS] %s volume %u from the save (mixer %.2f)\n", name[i], v, (double)MEMF(live[i]));
    }
}

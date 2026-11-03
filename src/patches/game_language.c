/* The game's own French ([language] game = french).
 *
 * USA guest 0x00044FC0 (thiscall, ecx = the localisation object, one argument)
 * stores its argument as the game's language ([ecx+0x3C], the index
 * src/patches/subtitles.c reads) and loads that language's string table from
 * assets/misc.viv: 1 cmusen.loc (US English), 2 cmuken.loc (UK English), 3
 * cmfren.loc (French; 2,825 of its 3,068 strings translated). The boot call at
 * 0x0001212F always passes 1. With index 3 the menus, the memory-card messages
 * (mcfr.loc) and the Story subtitles are French, and the game shows subtitles in
 * every Story cutscene by itself; the voices and the menu icon art stay English.
 * Only the boot call is changed (return address 0x00012134); the wrapper is in
 * src/recomp_manual.c. Off by default, and never while a language mod is on.
 */
#include <stdint.h>
#include <stdio.h>
#include "hooks/pc_language.h"

#define GAME_LANGUAGE_FRENCH 3u

uint32_t defjam_game_language_at_boot(uint32_t asked)
{
    if (!pc_language_french())
        return asked;
    fprintf(stderr, "[LANG] the game's own French (language %u in place of %u)\n", GAME_LANGUAGE_FRENCH, asked);
    return GAME_LANGUAGE_FRENCH;
}

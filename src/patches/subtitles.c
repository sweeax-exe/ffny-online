/* Subtitles in every Story cutscene ([language] story_subtitles, a language pack's
 * story_subtitles=all).
 *
 * USA guest 0x000DA8A0 (thiscall, ecx = the cutscene's subtitle state, one
 * argument: the NIS name) loads a cutscene's subtitle data into [ecx+4] and then
 * decides whether to show it: [ecx+8] = [ecx+4] only when the language index
 * ([0x3412A8]+0x3C) is 3 (French) or the cutscene is assets/nis_sc00.bin, the
 * opening scene. So an English game subtitles the first cutscene and no other,
 * and a translation that keeps the USA language slot would too. ZARF's console
 * patch makes the name compare always succeed (count 0 at 0x000DA901); this does
 * the same after the generated body returns: the function writes [ecx+8] nowhere
 * else, and the subtitle data is already loaded. Off by default; English players
 * can choose it as well (issue #44). The wrapper is in src/recomp_manual.c.
 */
#include <stdint.h>
#include <stdio.h>
#include "recomp/gen/recomp_types.h"
#include "hooks/pc_language.h"

void defjam_subtitles_after_nis_setup(uint32_t state)
{
    static int logged;
    if (!state || !pc_language_all_subtitles())
        return;
    if (MEM32(state + 8) == 0 && MEM32(state + 4) != 0) {
        MEM32(state + 8) = MEM32(state + 4);
        if (logged++ < 8)
            fprintf(stderr, "[LANG] cutscene subtitles shown (story_subtitles)\n");
    }
}

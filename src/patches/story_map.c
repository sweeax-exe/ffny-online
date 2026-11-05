/* #162: Battle's cleaned map copies erase an inset that Story uses for shops.
 * Replay Story's central map pixels at offset zero; keep Battle's cleaned copies
 * and every exterior extension. See docs/research/story-map-shops-widescreen.md.
 */
#include <string.h>
#include "cheats.h"
#include "story_map.h"
#include "texture_pack.h"

static int replay_map_source(const char *id, int offset, void *user)
{
    (void)user;
    if (offset || !id || !cheats_in_story())
        return 0;
    return !strcmp(id, "7b4754dfd82c09030171a6198634968b504062529df77cbc943be5235ca19927") ||
           !strcmp(id, "a1f9480b6262480bbc67ea60b4461949304fc8c474fd99ca5df7880319dfcc8a");
}

void ws_story_map_init(void)
{
    extern void nv2a_ws_set_extend_tag(float y_offset);
    texture_pack_set_wide_replay_filter(replay_map_source, NULL);
    nv2a_ws_set_extend_tag((float)WS_STORY_MAP_TEXT_TAG);
}

int ws_story_map_tag_text(uint8_t *apt, size_t bytes)
{
    /* map.apt's shopText PlaceObject: its matrix has no relocated pointers.
     * Keep x and the live/localized text intact; only tag its y coordinate. */
    static const float matrix[] = {1, 0, 0, 1, 60};
    float y;
    if (!apt || bytes < 0xADDu || memcmp(apt, "Apt Data", 8) ||
        memcmp(apt + 0x3CC, matrix, sizeof matrix) ||
        memcmp(apt + 0xAD4, "shopText", 9))
        return -1;
    memcpy(&y, apt + 0x3E0, sizeof y);
    if (y == 432.9f + WS_STORY_MAP_TEXT_TAG)
        return 0;
    if (y != 432.9f)
        return -1;
    y += WS_STORY_MAP_TEXT_TAG;
    memcpy(apt + 0x3E0, &y, sizeof y);
    return 1;
}

/* #162 / PR #190: shared map artwork and foreground clipping.
 * Battle's cleaned map copies erase an inset that Story uses for shops.
 * Replay Story's central map pixels at offset zero; keep Battle's cleaned copies
 * and every exterior extension. See docs/research/story-map-shops-widescreen.md.
 */
#include <string.h>
#include "cheats.h"
#include "story_map.h"
#include "texture_pack.h"

/* Toolkit executor (nv2a_pb_exec.c); its header is D3D11-only. */
void nv2a_ws_set_extend_tag(float y_offset);

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

int ws_map_tag_pins(uint8_t *apt, size_t bytes)
{
    /* chsVenue's shared pin widget (sprite 132). Its four visual children
     * carry the label/paper, locked pin and live venue icon. Tag their local
     * translations, not the parent pin positions read by findPins/moveToVenue.
     * These children are not positioned by ActionScript after placement. */
    static const struct {
        size_t at, name_at;
        uint32_t depth, character;
        const char *name;
        float x, y;
    } children[] = {
        {0x75DC, 0xF600, 1, 131, "blackPaper", -30, -13},
        {0x761C, 0xF60C, 3, 130, "greyPin",       0,   0},
        {0x765C, 0xF614, 5, 131, "paper",       -30, -13},
        {0x769C, 0xF61C, 7, 131, "pin",           0,   0}
    };
    float ys[4];
    size_t i;
    int changed = 0;
    if (!apt || bytes < 0xF620u || memcmp(apt, "Apt Data", 8))
        return -1;
    /* Validate every child before changing any, including on a partial tag.
     * This window contains no relocated pointers. Names are checked in place. */
    for (i = 0; i < 4; i++) {
        uint32_t header[] = {3, 0x26, children[i].depth, children[i].character};
        float matrix[] = {1, 0, 0, 1, children[i].x};
        size_t at = children[i].at;
        if (memcmp(apt + at, header, sizeof header) ||
            memcmp(apt + at + 16, matrix, sizeof matrix) ||
            memcmp(apt + children[i].name_at, children[i].name, strlen(children[i].name) + 1))
            return -1;
        memcpy(&ys[i], apt + at + 36, sizeof ys[i]);
        if (ys[i] != children[i].y && ys[i] != children[i].y + WS_STORY_MAP_TEXT_TAG)
            return -1;
    }
    for (i = 0; i < 4; i++) {
        if (ys[i] == children[i].y) {
            ys[i] += WS_STORY_MAP_TEXT_TAG;
            memcpy(apt + children[i].at + 36, &ys[i], sizeof ys[i]);
            changed++;
        }
    }
    return changed;
}

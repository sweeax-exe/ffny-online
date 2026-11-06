#ifndef DEFJAM_STORY_MAP_H
#define DEFJAM_STORY_MAP_H
#include <stddef.h>
#include <stdint.h>

#define WS_STORY_MAP_TEXT_TAG 16384

/* Register the Story map's side-replay policy before rendering starts. */
void ws_story_map_init(void);
/* Called only for Story's map movie in widescreen, before its timeline runs.
 * Return 1 when tagged, 0 when already tagged, -1 for an unknown layout. */
int ws_story_map_tag_text(uint8_t *apt, size_t bytes);
/* Shared Story/Battle pin visuals, without changing map navigation positions.
 * Return the number tagged, 0 when already tagged, -1 for an unknown layout. */
int ws_map_tag_pins(uint8_t *apt, size_t bytes);

#endif

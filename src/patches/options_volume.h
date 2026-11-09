/* The saved music, effects and announcer volumes applied after a load (options_volume.c). */
#ifndef DEFJAM_OPTIONS_VOLUME_H
#define DEFJAM_OPTIONS_VOLUME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void options_volume_after_load(uint32_t manager);   /* after sub_00014540: the options save copied into B */

#ifdef __cplusplus
}
#endif

#endif

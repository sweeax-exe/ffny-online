#ifndef DEFJAM_INPUT_CONSUMPTION_H
#define DEFJAM_INPUT_CONSUMPTION_H
#include <stdint.h>
void input_consumption_start(void);
int input_consumption_on(void);
void input_consumption_xdk_end(uint32_t handle, uint32_t state, uint32_t status);
void input_consumption_fighter(uint32_t actor);
#endif

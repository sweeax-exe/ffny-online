#ifndef DEFJAM_INPUT_LATE_H
#define DEFJAM_INPUT_LATE_H
#include <stdint.h>
void input_late_start(void);
int input_late_on(void);
void input_late_lock(void);
void input_late_unlock(void);
void input_late_capture(uint32_t handle, uint32_t state, uint32_t status);
void input_late_validate(unsigned port);
uint32_t input_late_raw(void);
void input_late_refresh(uint32_t device, uint32_t report);
#endif

#ifndef DEFJAM_INPUT_LATENCY_H
#define DEFJAM_INPUT_LATENCY_H
#include <stdint.h>
void input_latency_start(void);
int input_latency_on(void);
void input_latency_step(void);
void input_latency_present(int before);
void input_latency_guest(int slot, const uint32_t words[12], int after);
void input_latency_xdk_begin(uint32_t handle);
void input_latency_pad_cache(uint32_t base, int after, uint32_t writer);
/* Read-only guest probes, implemented beside the calibrated fighter fields. */
void defjam_input_latency_actor(uint32_t actor, int after);
#endif

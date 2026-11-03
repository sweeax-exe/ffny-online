/* Existing telemetry fixtures run without host tracing. */
#include "input_latency.h"
void input_consumption_fighter(uint32_t actor) { (void)actor; }
int input_latency_on(void) { return 0; }
void input_latency_step(void) {}
void input_latency_guest(int slot, const uint32_t words[12], int after)
{ (void)slot; (void)words; (void)after; }

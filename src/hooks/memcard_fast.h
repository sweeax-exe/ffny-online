/* Faster save loading: the memory-card driver's tick run more than once a frame while it reads
 * (src/hooks/memcard_fast.c). */
#ifndef DEFJAM_MEMCARD_FAST_H
#define DEFJAM_MEMCARD_FAST_H

int memcard_fast_on(void);
void memcard_fast_tick(void (*tick)(void));
void memcard_fast_sleep(void (*sleep_fn)(void));

#endif

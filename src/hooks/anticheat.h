/*
 * The facts a signed-in game adds to its online match reports for the anti-cheat (build 37):
 * whether the whole game stood still during the fight (a frozen or suspended process, not the
 * network), how many copies of the game run on the PC, known cheat and freeze tools that run, and a
 * hashed fingerprint of the PC. The account service and the team judge them; the game only reports.
 */
#ifndef DEFJAM_ANTICHEAT_H
#define DEFJAM_ANTICHEAT_H

#include <stdint.h>

/* A fight starts (its "start" report goes out): counters back to zero, the PC looked at once. */
void ac_match_begin(void);
/* From the online thread, every pass of its loop: a pass that comes much later than the one before
 * means the whole process stood still (the online thread waits at most 20 ms a pass). */
void ac_net_tick(uint64_t now_ms);
/* Appends ,"ac":{...} for the running fight to json at n and returns the new length; the PC is
 * looked at again first, so a tool started during the fight is seen. */
int ac_json(char *json, int size, int n);

#endif

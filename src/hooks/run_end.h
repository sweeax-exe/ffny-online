/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* How a run of the game ended (build 110). A game that crashes, is closed with the window's X or is killed looks the
 * same to the relay, and a crash whose report the player does not send leaves nothing: one player's game ended seven
 * times in the middle of a fight and nobody could say whether it crashed (freeze report, 2026-11-23).
 *
 * The game's process keeps a small file beside its logs, logs\run-end.txt, one line per fact:
 *     begin build=109 cl=194435207 at=<unix time>
 *     closed | quit | update                      the window's X (or Alt+F4) | the game's EXIT | closed for UPDATE NOW
 *     hung | moved                                the hang watch: no picture for seconds | it went on
 *     crash code=C0000005 at=module.dll+1DA93 from=1A045      a fault, and the exe's frame nearest to it
 *     exit code=0x... at=<unix time>              by the launcher that waited for the game's process
 * The next launcher start reads it, deletes it and, signed in, sends what the lines say with the build number
 * (account_send_run_end: POST /api/report/end). No path, no name, nothing of the fight or the log; the log itself
 * still goes only when the player presses Send crash report. Not signed in: deleted unsent. Windows only. */
#ifndef DEFJAM_RUN_END_H
#define DEFJAM_RUN_END_H

#include <stddef.h>

/* The compiler this exe was built by (cl's _MSC_FULL_VER, 194435207 = 19.44.35207; 0 another one): setup compiles the
 * game on each player's PC with the newest Visual Studio there, and an address in a report names the right function only
 * with a build of the same release by the same compiler (docs/agent/triage.md). Also in each log's [BUILD] line. */
#if defined(_MSC_FULL_VER)
#define RUN_END_CL ((long)_MSC_FULL_VER)
#else
#define RUN_END_CL 0L
#endif

#ifdef __cplusplus
extern "C" {
#endif

void run_end_begin(int build);              /* once, in the process that runs the game */
void run_end_note(const char *how);         /* "closed", "quit", "update", "hung", "moved" */
void run_end_quit(void);                    /* the EXIT in the game: the close that follows is a quit */
void run_end_closing(void);                 /* the window's close */
void run_end_exit_code(unsigned long code); /* from the process that waited for the game */
void run_end_send(void);                    /* the launcher's start, once: in the background */
#if defined(_WIN32)
struct _EXCEPTION_POINTERS;
void run_end_crash(struct _EXCEPTION_POINTERS *ep);     /* from the crash handler, after the report is on disk */
#endif

/* What the lines of a run-end file say (tests/unit/test_run_end_native.py). */
typedef struct RunEnd {
    int build;
    long cl;                /* RUN_END_CL of the exe that ran */
    long long at;           /* unix time it ended as far as known: the exit line's, else the begin line's; 0 unknown */
    char end[12];           /* "quit", "closed", "update", "crash", "exit" (a code with no other sign), "unknown" */
    int hung;               /* it stood still when it ended */
    char code[12];          /* the exit code, else the fault's, 8 hex digits; "" unknown */
    char where[80];         /* a crash: module+offset */
    char from[20];          /* a crash: the exe's frame, offset alone */
} RunEnd;
int run_end_parse(const char *text, RunEnd *out);      /* 1 when the file holds a run */
int run_end_json(const RunEnd *r, long long now, char *out, size_t size);      /* its length, 0 when it does not fit */

#ifdef __cplusplus
}
#endif

#endif

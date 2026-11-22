/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Team Tower Online (community/team-tower-online-design.md): two signed-in players on two PCs climb Team Tower 4
 * together. The relay runs the climb in a tower room (NETPLAY_MODE_TOWER, server relay/tower.go) and the account
 * service deals every floor; this game only shows the climb (towers_online_ui.inc) and plays each floor as a lockstep
 * Team match (the players in slots 0 and 1, the floor's CPUs in 2 and 3, towers.c's online floor).
 *
 * On the wire (netplay.c): TOWER (0x3D) after every ROOM of a tower room; TCHOICE (0x3E: hello with this game's
 * Towers rules version on entering the room, the host's resume / new climb); START's tower trailer (the floor and its
 * CPUs); RESULT's tower trailer (the steps, the sides' health; winner 0xFE: this PC saw the floor desync). */
#ifndef DEFJAM_TOWERS_ONLINE_H
#define DEFJAM_TOWERS_ONLINE_H

#include <stdint.h>
#include "towers_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TOWER states and flags (relay/protocol_tower.go) */
enum {
    TOWERNET_SELECT = 0,    /* fighter select before the climb; READY */
    TOWERNET_STARTING,      /* the service deals the run or the floor */
    TOWERNET_PLAYING,       /* a floor's fight runs */
    TOWERNET_SUMMARY,       /* between floors: floor = the next one, last_* the one just fought; READY = continue */
    TOWERNET_WAITING,       /* the partner is away (secs: until the climb is saved) */
    TOWERNET_OVER,          /* the climb ended or was saved (end); the host may start a new one */
    TOWERNET_REFUSED
};
#define TOWERNET_FLAG_SAVED   1
#define TOWERNET_FLAG_RESUME  2
#define TOWERNET_FLAG_BOARD   4
enum { TOWERNET_HOW_NONE = 0, TOWERNET_HOW_WIN, TOWERNET_HOW_LOSS, TOWERNET_HOW_DRAW, TOWERNET_HOW_VOID, TOWERNET_HOW_QUIT,
       TOWERNET_HOW_NORESULT };
#define TOWERNET_CHOICE_HELLO  0
#define TOWERNET_CHOICE_RESUME 1
#define TOWERNET_CHOICE_NEW    2
#define TOWERNET_WINNER_VOID   0xFE

typedef struct TowerNetView {
    int valid;              /* a TOWER for the room this PC is in */
    int state, flags, tower, floor, floors, beaten, opp[2], venue, secs, saved_floor, last_floor, last_how, voids;
    char end[24], why[100];
    unsigned long long at;  /* GetTickCount64 when it came (secs counts from then) */
} TowerNetView;

/* netplay.c: a TOWER body (after the header) for the room `room` this PC is in; the room was left (clears it). */
void towers_online_on_tower(const char *room, const uint8_t *body, int len);
void towers_online_room_left(void);
/* The latest TOWER of this PC's room (0: none). */
int towers_online_view_net(TowerNetView *out);

/* The floor of the running netplay match (towers.c). START's trailer (on = 0: a START without one); the CPU of
 * position 2 or 3; whether the match is a tower floor. */
void towers_online_floor(int on, int rules, int tower, int floor, int floors, int opp1, int opp2);
int towers_online_match(void);
int towers_online_cpu(int pos, int *fighter);
/* The floor's host state for the lockstep checksum (0 while no tower floor runs). */
uint32_t towers_online_hash(void);
/* The floor's decision for RESULT: the side that won (0 the players, 1 the CPUs, -1 a draw), the sides' health %,
 * the step it was decided at. 0 while undecided. */
int towers_online_result(int *side, int *health, int *cpu_health, unsigned *steps);

typedef struct TowersOnlineFloor {
    int on, tower, floor, floors, opp[2], fighting;
    TowerFloorRules rules;
} TowersOnlineFloor;
void towers_online_view(TowersOnlineFloor *out);

#ifdef __cplusplus
}
#endif

#endif

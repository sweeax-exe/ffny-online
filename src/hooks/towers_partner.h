/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The Team Tower's player 2 sign-in (build 108; server relay/accounts/pair.go). Player 1 is signed in on this PC;
 * player 2 signs in with their own FFNY account on their phone:
 *
 *   1. towers_partner_begin: a random secret, its SHA-256 to POST /auth/pair/start (player 1's session): a short
 *      code (K7Q-2MX) and the page (<service>/pair). The screen shows both.
 *   2. Player 2 opens the page on the phone, types the code, signs in with Discord or a username and password.
 *   3. A worker polls POST /auth/pair/poll with the secret every two seconds: player 2's name and a partner token.
 *   4. The screen shows the name; player 2 confirms on their own controller (towers_partner_confirm), or says "not
 *      me" (towers_partner_reject: the token is dropped on the service too).
 *   5. The team start sends the partner token (towers.c net_start); the service checks it belongs to player 1.
 *
 * The partner token is no session (it signs nobody in, only lets player 1's tower starts record player 2) and it
 * lives in memory only: never written to a file or a log. GUEST stays: nobody has to sign in. */
#ifndef TOWERS_PARTNER_H
#define TOWERS_PARTNER_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PARTNER_NONE = 0,       /* player 2 is a guest */
    PARTNER_STARTING,       /* asking the service for a code */
    PARTNER_WAITING,        /* the code is on the screen: waiting for player 2's sign-in on the phone */
    PARTNER_CONFIRM,        /* player 2 signed in: the name waits for player 2's confirm */
    PARTNER_SIGNED,         /* player 2 rides along with their own account */
    PARTNER_FAILED          /* the sign-in could not go on: why */
};

typedef struct PartnerView {
    int state;
    char code[16];          /* "K7Q-2MX" */
    char url[160];          /* "https://.../pair" */
    char name[64];          /* player 2's account name (CONFIRM, SIGNED) */
    char why[160];          /* FAILED: the reason; else "" */
    int secs_left;          /* WAITING: the code's time left */
} PartnerView;

/* Starts a sign-in (any open one, and a confirmed partner, end first). 0 when player 1 is not signed in. */
int towers_partner_begin(void);
void towers_partner_cancel(void);       /* stops the sign-in under way (back to the guest, or the partner kept) */
int towers_partner_confirm(void);       /* CONFIRM -> SIGNED; 1 when it did */
void towers_partner_reject(void);       /* CONFIRM -> NONE: "not me" (the token ends on the service) */
void towers_partner_clear(void);        /* SIGNED -> NONE: player 2 leaves (the token ends on the service) */
void towers_partner_view(PartnerView *out);
/* The partner token for a team start into out (0: a guest); its name into name (may be NULL). */
int towers_partner_token(char *out, int size, char *name, int name_size);
/* The service's answer to a team start: the partner it recorded ("" a guest). A token sent but not recorded (it
 * ran out, the service restarted) makes player 2 a guest again and is remembered for the screen's note. */
void towers_partner_started(int sent, const char *recorded);
/* 1 once after a partner was dropped by towers_partner_started (the start card says so). */
int towers_partner_dropped(void);

#ifdef __cplusplus
}
#endif
#endif

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Sign-in without Discord (username and password): the pure parts, tested natively
 * (tests/unit/test_account_password_native.py). The account service decides (server
 * relay/accounts/password.go); these checks only spare a round trip for the obvious mistakes. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define ACCOUNT_PW_LOGIN_MIN 3
#define ACCOUNT_PW_LOGIN_MAX 16
#define ACCOUNT_PW_MIN 8
#define ACCOUNT_PW_MAX 128

/* The JSON body of the password routes: {"username":..,"password":..[,"code":..][,"pc":..]}; code and pc may
 * be NULL or "". Quotes, backslashes and control characters are escaped. Returns the length, -1 when it does
 * not fit. */
int account_pw_json(char *out, int size, const char *user, const char *pass, const char *code, const char *pc);
/* The body of a password change ({"old":..,"password":..}) or of adding a login ({"username":..,"password":..}). */
int account_pw_change_json(char *out, int size, const char *old, const char *pass);

/* Local checks before sending: 0 and why="" when fine, else 1 and a plain-English reason. repeat may be NULL
 * (sign-in: only that both are filled in). */
int account_pw_check_login(const char *user, char *why, int size);
int account_pw_check_new(const char *user, const char *pass, const char *repeat, char *why, int size);

/* "key":true in a flat JSON answer (1), "key":false or missing (0). */
int account_pw_flag(const char *json, const char *key);

/* The PC fingerprint as the match reports carry it (32 lowercase hex digits), from its 16 bytes. */
void account_pw_hex16(const unsigned char in[16], char out[33]);

#ifdef __cplusplus
}
#endif

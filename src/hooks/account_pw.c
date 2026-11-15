/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Sign-in without Discord: the pure parts (account_pw.h). */
#include "account_pw.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Appends a JSON string (quoted, escaped) at *n; 0 when it does not fit. */
static int put_str(char *out, int size, int *n, const char *s)
{
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    if (*n + 2 >= size) return 0;
    out[(*n)++] = '"';
    for (; *p; p++) {
        char esc[8];
        int len;
        if (*p == '"' || *p == '\\') { esc[0] = '\\'; esc[1] = (char)*p; len = 2; }
        else if (*p < 0x20 || *p == 0x7F) len = snprintf(esc, sizeof(esc), "\\u%04x", *p);
        else { esc[0] = (char)*p; len = 1; }
        if (*n + len + 2 >= size) return 0;
        memcpy(out + *n, esc, (size_t)len);
        *n += len;
    }
    out[(*n)++] = '"';
    out[*n] = 0;
    return 1;
}

static int put_raw(char *out, int size, int *n, const char *s)
{
    int len = (int)strlen(s);
    if (*n + len + 1 >= size) return 0;
    memcpy(out + *n, s, (size_t)len + 1);
    *n += len;
    return 1;
}

int account_pw_json(char *out, int size, const char *user, const char *pass, const char *code, const char *pc)
{
    int n = 0;
    if (size < 4) return -1;
    out[0] = 0;
    if (!put_raw(out, size, &n, "{\"username\":") || !put_str(out, size, &n, user) ||
        !put_raw(out, size, &n, ",\"password\":") || !put_str(out, size, &n, pass))
        return -1;
    if (code && *code && (!put_raw(out, size, &n, ",\"code\":") || !put_str(out, size, &n, code))) return -1;
    if (pc && *pc && (!put_raw(out, size, &n, ",\"pc\":") || !put_str(out, size, &n, pc))) return -1;
    if (!put_raw(out, size, &n, "}")) return -1;
    return n;
}

int account_pw_change_json(char *out, int size, const char *old, const char *pass)
{
    int n = 0;
    if (size < 4) return -1;
    out[0] = 0;
    if (!put_raw(out, size, &n, "{\"old\":") || !put_str(out, size, &n, old) ||
        !put_raw(out, size, &n, ",\"password\":") || !put_str(out, size, &n, pass) || !put_raw(out, size, &n, "}"))
        return -1;
    return n;
}

static int fail(char *why, int size, const char *text)
{
    if (why && size > 0) snprintf(why, (size_t)size, "%s", text);
    return 1;
}

int account_pw_check_login(const char *user, char *why, int size)
{
    size_t len = user ? strlen(user) : 0, i;
    int alnum = 0;
    if (why && size > 0) why[0] = 0;
    if (len < ACCOUNT_PW_LOGIN_MIN || len > ACCOUNT_PW_LOGIN_MAX)
        return fail(why, size, "Use 3 to 16 letters, digits, dots, dashes or underscores for your username.");
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)user[i];
        if (isalnum(c) && c < 0x80) alnum++;
        else if (c != '.' && c != '_' && c != '-')
            return fail(why, size, "Use 3 to 16 letters, digits, dots, dashes or underscores for your username.");
    }
    if (alnum < 2) return fail(why, size, "Use at least two letters or digits in your username.");
    return 0;
}

/* Is needle in hay, ignoring case (ASCII)? */
static int contains_nocase(const char *hay, const char *needle)
{
    size_t h = strlen(hay), n = strlen(needle), i, j;
    if (!n || n > h) return 0;
    for (i = 0; i + n <= h; i++) {
        for (j = 0; j < n && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j]); j++) {}
        if (j == n) return 1;
    }
    return 0;
}

int account_pw_check_new(const char *user, const char *pass, const char *repeat, char *why, int size)
{
    size_t len = pass ? strlen(pass) : 0;
    if (why && size > 0) why[0] = 0;
    if (!repeat) {     /* sign-in */
        if (!user || !*user || !len) return fail(why, size, "Enter your username and your password.");
        return 0;
    }
    if (account_pw_check_login(user, why, size)) return 1;
    if (len < ACCOUNT_PW_MIN) return fail(why, size, "Use at least 8 characters for your password.");
    if (len > ACCOUNT_PW_MAX) return fail(why, size, "Use at most 128 characters for your password.");
    if (strcmp(pass, repeat)) return fail(why, size, "The two passwords are not the same.");
    if (contains_nocase(pass, user)) return fail(why, size, "Your password must not contain your username.");
    return 0;
}

int account_pw_flag(const char *json, const char *key)
{
    char pat[64];
    const char *p;
    if (!json || !key) return 0;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    return !strncmp(p, "true", 4);
}

void account_pw_hex16(const unsigned char in[16], char out[33])
{
    static const char hex[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 16; i++) {
        out[2 * i] = hex[in[i] >> 4];
        out[2 * i + 1] = hex[in[i] & 15];
    }
    out[32] = 0;
}

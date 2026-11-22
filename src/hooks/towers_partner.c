/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The Team Tower's player 2 sign-in (towers_partner.h). */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "account.h"
#include "towers_partner.h"

#define POLL_MS 2000

static CRITICAL_SECTION g_pcs;
static INIT_ONCE g_ponce = INIT_ONCE_STATIC_INIT;
static PartnerView g_v;                 /* what the screen shows */
static char g_pending[96];              /* CONFIRM: player 2's partner token, waiting for the confirm */
static char g_token[96];                /* SIGNED: the partner token the team start sends */
static volatile LONG g_gen;             /* each begin / cancel: an older worker stops */
static unsigned long long g_deadline;   /* WAITING: the code runs out then */
static int g_dropped;

/* zeroes a secret so the compiler keeps the stores */
static void wipe(void *p, size_t n)
{
    volatile unsigned char *b = (volatile unsigned char *)p;
    while (n--) *b++ = 0;
}

static BOOL CALLBACK init_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_pcs);
    return TRUE;
}
static void plock(void) { InitOnceExecuteOnce(&g_ponce, init_once, NULL, NULL); EnterCriticalSection(&g_pcs); }
static void punlock(void) { LeaveCriticalSection(&g_pcs); }

/* key=value line `key` of a text answer into out; 1 when present */
static int kv(const char *text, const char *key, char *out, int size)
{
    size_t k = strlen(key);
    const char *p = text;
    while (p && *p) {
        if (!strncmp(p, key, k) && p[k] == '=') {
            int n = 0;
            p += k + 1;
            while (*p && *p != '\n' && *p != '\r' && n < size - 1) out[n++] = *p++;
            out[n] = 0;
            return 1;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
    if (size > 0) out[0] = 0;
    return 0;
}

static int token_ok(const char *t)
{
    size_t i, n = strlen(t);
    if (n != 64) return 0;
    for (i = 0; i < n; i++)
        if (!((t[i] >= '0' && t[i] <= '9') || (t[i] >= 'a' && t[i] <= 'f'))) return 0;
    return 1;
}

static int env_on(const char *name)
{
    const char *v = getenv(name);
    return v && *v && *v != '0';
}

/* the partner token ends on the service ("not me", player 2 leaves, a new player 2): off the caller's thread */
static DWORD WINAPI end_thread(LPVOID arg)
{
    char *token = (char *)arg, body[128], reply[256];
    int status;
    snprintf(body, sizeof body, "{\"token\":\"%s\"}", token);
    status = account_call("POST", "/auth/pair/end?format=text", body, reply, (int)sizeof reply, 0, NULL);
    fprintf(stderr, "[TOWERS] partner sign-in: player 2's token ended: HTTP %d\n", status);
    wipe(body, sizeof body);
    wipe(token, 96);
    free(token);
    return 0;
}

static void end_token(char *t)      /* t: g_pending or g_token, cleared here; called with the lock held */
{
    char *copy;
    if (!t[0]) return;
    copy = (char *)malloc(96);
    if (copy) {
        memcpy(copy, t, 96);
        HANDLE h = CreateThread(NULL, 0, end_thread, copy, 0, NULL);
        if (h) CloseHandle(h);
        else { wipe(copy, 96); free(copy); }
    }
    wipe(t, 96);
}

static void fail_locked(LONG gen, const char *why)
{
    if (gen != g_gen) return;
    g_v.state = PARTNER_FAILED;
    snprintf(g_v.why, sizeof g_v.why, "%s", why && *why ? why : "The sign-in could not go on.");
    fprintf(stderr, "[TOWERS] partner sign-in failed: %s\n", g_v.why);
}

static int still(LONG gen) { return gen == g_gen; }

static DWORD WINAPI pair_thread(LPVOID arg)
{
    LONG gen = (LONG)(intptr_t)arg;
    char secret[65], login[65], body[160], reply[2048], v[160];
    int status, expires, misses = 0;
    if (!account_pair_secret(secret, login)) {
        plock(); fail_locked(gen, "Windows could not start the sign-in."); punlock();
        return 0;
    }
    snprintf(body, sizeof body, "{\"login\":\"%s\"}", login);
    status = account_call("POST", "/auth/pair/start?format=text", body, reply, (int)sizeof reply, 0, NULL);
    plock();
    if (!still(gen)) { punlock(); wipe(secret, sizeof secret); return 0; }
    if (status != 200 || !kv(reply, "code", g_v.code, (int)sizeof g_v.code) || !kv(reply, "url", g_v.url, (int)sizeof g_v.url)) {
        kv(reply, "error", v, (int)sizeof v);
        fail_locked(gen, status == 0 ? "The FFNY server could not be reached." : status < 0 ? "Player 1 is not signed in." : v);
        punlock();
        wipe(secret, sizeof secret);
        return 0;
    }
    expires = kv(reply, "expires", v, (int)sizeof v) ? atoi(v) : 600;
    if (expires < 60 || expires > 3600) expires = 600;
    g_deadline = GetTickCount64() + (unsigned long long)expires * 1000ull;
    g_v.state = PARTNER_WAITING;
    fprintf(stderr, "[TOWERS] partner sign-in: code %s on %s (player 2 opens it on a phone), %d s\n", g_v.code, g_v.url, expires);
    punlock();
    snprintf(body, sizeof body, "{\"secret\":\"%s\"}", secret);
    for (;;) {
        int k;
        for (k = 0; k < POLL_MS / 250; k++) {
            Sleep(250);
            if (!still(gen)) goto out;
        }
        if (GetTickCount64() > g_deadline) {
            plock(); fail_locked(gen, "The code ran out. Press Y to get a new one."); punlock();
            break;
        }
        status = account_call("POST", "/auth/pair/poll?format=text", body, reply, (int)sizeof reply, 0, NULL);
        plock();
        if (!still(gen)) { punlock(); break; }
        if (status == 202) { misses = 0; punlock(); continue; }
        if (status == 0 && ++misses < 5) { punlock(); continue; }      /* the service for a moment out of reach */
        if (status == 200 && kv(reply, "token", g_pending, (int)sizeof g_pending) && token_ok(g_pending)) {
            kv(reply, "name", g_v.name, (int)sizeof g_v.name);
            if (!g_v.name[0]) snprintf(g_v.name, sizeof g_v.name, "PLAYER 2");
            g_v.state = PARTNER_CONFIRM;
            fprintf(stderr, "[TOWERS] partner sign-in: player 2 is %s: waiting for player 2's confirm\n", g_v.name);
            if (env_on("RECOMP_TEST_TOWERS_P2_CONFIRM")) {     /* tests: player 2's A at once */
                memcpy(g_token, g_pending, sizeof g_token);
                wipe(g_pending, sizeof g_pending);
                g_v.state = PARTNER_SIGNED;
                fprintf(stderr, "[TOWERS] partner sign-in: player 2 confirmed (%s)\n", g_v.name);
            }
            punlock();
            break;
        }
        wipe(g_pending, sizeof g_pending);
        kv(reply, "error", v, (int)sizeof v);
        fail_locked(gen, status == 0 ? "The FFNY server could not be reached." : v);
        punlock();
        break;
    }
out:
    wipe(body, sizeof body);
    wipe(secret, sizeof secret);
    wipe(reply, sizeof reply);
    return 0;
}

int towers_partner_begin(void)
{
    LONG gen;
    HANDLE h;
    if (account_state() != ACCOUNT_IN) return 0;
    plock();
    gen = ++g_gen;
    end_token(g_pending);
    end_token(g_token);      /* a new player 2 (or the same again): the last token ends */
    memset(&g_v, 0, sizeof g_v);
    g_v.state = PARTNER_STARTING;
    punlock();
    fprintf(stderr, "[TOWERS] partner sign-in: started\n");
    h = CreateThread(NULL, 0, pair_thread, (LPVOID)(intptr_t)gen, 0, NULL);
    if (!h) {
        plock(); fail_locked(gen, "The sign-in could not start."); punlock();
        return 0;
    }
    CloseHandle(h);
    return 1;
}

void towers_partner_cancel(void)
{
    plock();
    ++g_gen;
    end_token(g_pending);
    if (g_v.state != PARTNER_SIGNED) {
        int signed_before = g_token[0] != 0;
        char name[64];
        snprintf(name, sizeof name, "%s", g_v.name);
        memset(&g_v, 0, sizeof g_v);
        g_v.state = signed_before ? PARTNER_SIGNED : PARTNER_NONE;
        if (signed_before) snprintf(g_v.name, sizeof g_v.name, "%s", name);
    }
    punlock();
}

int towers_partner_confirm(void)
{
    int ok = 0;
    plock();
    if (g_v.state == PARTNER_CONFIRM && g_pending[0]) {
        memcpy(g_token, g_pending, sizeof g_token);
        wipe(g_pending, sizeof g_pending);
        g_v.state = PARTNER_SIGNED;
        g_v.code[0] = g_v.url[0] = 0;
        ok = 1;
        fprintf(stderr, "[TOWERS] partner sign-in: player 2 confirmed (%s)\n", g_v.name);
    }
    punlock();
    return ok;
}

void towers_partner_reject(void)
{
    plock();
    if (g_v.state == PARTNER_CONFIRM) {
        fprintf(stderr, "[TOWERS] partner sign-in: player 2 said not me (%s): a guest\n", g_v.name);
        end_token(g_pending);
        memset(&g_v, 0, sizeof g_v);
    }
    punlock();
}

void towers_partner_clear(void)
{
    plock();
    ++g_gen;
    end_token(g_pending);
    end_token(g_token);
    memset(&g_v, 0, sizeof g_v);
    punlock();
}

void towers_partner_view(PartnerView *out)
{
    plock();
    *out = g_v;
    if (g_v.state == PARTNER_WAITING) {
        unsigned long long now = GetTickCount64();
        out->secs_left = g_deadline > now ? (int)((g_deadline - now) / 1000ull) : 0;
    }
    punlock();
}

int towers_partner_token(char *out, int size, char *name, int name_size)
{
    int ok = 0;
    plock();
    if (g_v.state == PARTNER_SIGNED && g_token[0] && (int)strlen(g_token) < size) {
        snprintf(out, (size_t)size, "%s", g_token);
        if (name && name_size > 0) snprintf(name, (size_t)name_size, "%s", g_v.name);
        ok = 1;
    } else {
        if (size > 0) out[0] = 0;
        if (name && name_size > 0) name[0] = 0;
    }
    punlock();
    return ok;
}

void towers_partner_started(int sent, const char *recorded)
{
    plock();
    if (sent && (!recorded || !recorded[0]) && g_v.state == PARTNER_SIGNED) {
        fprintf(stderr, "[TOWERS] partner sign-in: the service did not take player 2's token (it ran out?): a guest\n");
        wipe(g_token, sizeof g_token);
        memset(&g_v, 0, sizeof g_v);
        g_dropped = 1;
    }
    punlock();
}

int towers_partner_dropped(void)
{
    int d;
    plock();
    d = g_dropped;
    g_dropped = 0;
    punlock();
    return d;
}

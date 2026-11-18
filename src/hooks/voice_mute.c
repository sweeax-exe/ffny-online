/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The session's mutes and per-player volumes (voice_mute.h). */
#include "voice_mute.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
static SRWLOCK s_lock = SRWLOCK_INIT;           /* the UI, the netplay thread and the voice thread all come here */
#define LOCK() AcquireSRWLockExclusive(&s_lock)
#define UNLOCK() ReleaseSRWLockExclusive(&s_lock)
#else
#include <pthread.h>
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&s_lock)
#define UNLOCK() pthread_mutex_unlock(&s_lock)
#endif

#define MAX_ENTRIES 128

typedef struct {
    char name[VMUTE_NAME];
    unsigned account;
    int percent;
    int used;
} Entry;

static Entry s_e[MAX_ENTRIES];

static int same_name(const char *a, const char *b)
{
    if (!a || !b || !*a || !*b) return 0;
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return !*a && !*b;
}

static int find(const char *name, unsigned account)
{
    int i;
    for (i = 0; i < MAX_ENTRIES; i++) {
        if (!s_e[i].used) continue;
        if (account && s_e[i].account) {        /* two known accounts decide: the same, or two players whatever their names */
            if (account == s_e[i].account) return i;
            continue;
        }
        if (same_name(s_e[i].name, name)) return i;
    }
    return -1;
}

static void learn_into(Entry *e, const char *name, unsigned account)
{
    if (account && !e->account) e->account = account;
    /* the account is the authority: a changed name follows it; a name alone is only filled in when empty */
    if (name && *name && (!e->name[0] || (account && e->account == account))) snprintf(e->name, sizeof(e->name), "%s", name);
}

int vmute_percent(const char *name, unsigned account)
{
    int i, p;
    LOCK();
    i = find(name, account);
    p = i < 0 ? 100 : s_e[i].percent;
    UNLOCK();
    return p;
}

int vmute_is_muted(const char *name, unsigned account) { return vmute_percent(name, account) == 0; }

void vmute_set(const char *name, unsigned account, int percent)
{
    int i, free_i = -1, k;
    if (percent < 0) percent = 0;
    if (percent > 200) percent = 200;
    LOCK();
    i = find(name, account);
    if (i < 0) {
        if (percent == 100 || ((!name || !*name) && !account)) { UNLOCK(); return; }
        for (k = 0; k < MAX_ENTRIES; k++) if (!s_e[k].used) { free_i = k; break; }
        if (free_i < 0) { UNLOCK(); return; }
        memset(&s_e[free_i], 0, sizeof(s_e[free_i]));
        s_e[free_i].used = 1;
        i = free_i;
    }
    learn_into(&s_e[i], name, account);
    s_e[i].percent = percent;
    if (percent == 100) memset(&s_e[i], 0, sizeof(s_e[i]));
    UNLOCK();
}

void vmute_mute(const char *name, unsigned account, int on) { vmute_set(name, account, on ? 0 : 100); }

void vmute_learn(const char *name, unsigned account)
{
    int i;
    LOCK();
    i = find(name, account);
    if (i >= 0) learn_into(&s_e[i], name, account);
    UNLOCK();
}

int vmute_entry(int i, char *name, int size, unsigned *account, int *percent)
{
    int k, n = 0, found = 0;
    LOCK();
    for (k = 0; k < MAX_ENTRIES && !found; k++) {
        if (!s_e[k].used) continue;
        if (n++ != i) continue;
        if (name && size > 0) snprintf(name, (size_t)size, "%s", s_e[k].name);
        if (account) *account = s_e[k].account;
        if (percent) *percent = s_e[k].percent;
        found = 1;
    }
    UNLOCK();
    return found;
}

void vmute_clear(void)
{
    LOCK();
    memset(s_e, 0, sizeof(s_e));
    UNLOCK();
}

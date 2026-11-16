/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Run the Streets: access, map and the offline fight. See rts.h and docs/run-the-streets.md. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include "rts.h"
#include "account.h"
#include "automatch.h"
#include "online_data.h"
#include "openworld.h"
#include "pc_content.h"

/* ---------------------------------------------------------------- access (GET /api/features) */

static volatile long s_access = -1;         /* -1 not known, 0 no, 1 yes */
static char s_reason[32];
static unsigned long long s_asked_at;       /* GetTickCount64 of the last answer */
static int s_asking;
static int s_last_state = -1;

static unsigned long long now_ms(void)
{
#if defined(_WIN32)
    return GetTickCount64();
#else
    return 0;
#endif
}

static int test_access(void)
{
    const char *v = getenv("RECOMP_RTS_ACCESS");
    if (!v || !*v) return -1;
    return *v != '0';
}

#if defined(_WIN32)
static DWORD WINAPI ask_thread(LPVOID unused)
{
    char buf[1024], why[32];
    int st, yes;
    (void)unused;
    buf[0] = 0;
    st = account_call("GET", "/api/features", NULL, buf, (int)sizeof buf, 0, NULL);
    yes = st == 200 ? ow_rules_features_answer(buf, why, (int)sizeof why) : -1;
    if (yes >= 0) {
        snprintf(s_reason, sizeof s_reason, "%s", why);
        InterlockedExchange(&s_access, yes);
    } else {
        /* signed out (401/-1), no answer or an older service (404): hidden; asked again in a minute */
        snprintf(s_reason, sizeof s_reason, "%s", st == 404 ? "no-service" : st <= 0 ? "no-answer" : "refused");
        InterlockedExchange(&s_access, 0);
    }
    fprintf(stderr, "[RTS] feature access: %s (%s, status %d)\n", s_access == 1 ? "yes" : "no", s_reason, st);
    s_asked_at = now_ms();
    s_asking = 0;
    return 0;
}
#endif

int rts_access(void)
{
    int t = test_access(), st;
    if (t >= 0) {
        if (!s_reason[0]) snprintf(s_reason, sizeof s_reason, "test");
        return t;
    }
    st = (int)account_state();
    if (st != ACCOUNT_IN) {
        if (s_last_state == ACCOUNT_IN) { s_access = -1; s_reason[0] = 0; }   /* signed out: forget the answer */
        s_last_state = st;
        return 0;
    }
    if (s_last_state != ACCOUNT_IN) { s_access = -1; s_asked_at = 0; }        /* signed in (again): ask anew */
    s_last_state = st;
#if defined(_WIN32)
    if (!s_asking) {
        unsigned long long age = now_ms() - s_asked_at;
        if (s_access < 0 || (s_access == 1 && age > 300000ull) || (s_access == 0 && age > 60000ull)) {
            HANDLE h;
            s_asking = 1;
            h = CreateThread(NULL, 0, ask_thread, NULL, 0, NULL);
            if (h) CloseHandle(h);
            else s_asking = 0;
        }
    }
#endif
    return s_access == 1;
}

const char *rts_access_reason(void) { return s_reason; }

/* ---------------------------------------------------------------- the map */

int rts_map(RtsMap *out)
{
    static PcContentPack packs[PC_CONTENT_MAX];
    const char *v = getenv("RECOMP_RTS_VENUE");
    int n, i, best = -1;
    memset(out, 0, sizeof *out);
    if (v && *v) {
        const char *c = strchr(v, ',');
        out->fe_id = atoi(v);
        if (c) snprintf(out->code, sizeof out->code, "%s", c + 1);
        else snprintf(out->code, sizeof out->code, "%02dA", out->fe_id);
        snprintf(out->id, sizeof out->id, "test-venue-%d", out->fe_id);
        snprintf(out->name, sizeof out->name, "%s", online_venue_name(out->fe_id));
        return out->fe_id >= 1 && out->fe_id <= 63;
    }
    n = pc_content_list(packs, PC_CONTENT_MAX);
    for (i = 0; i < n; i++) {
        const PcContentPack *p = &packs[i];
        if (!p->official || !p->rts_map || !p->composed || !p->applied || p->venue_fe_id < 28 || p->venue_fe_id > 63)
            continue;
        if (best < 0 || p->order < packs[best].order) best = i;
    }
    if (best < 0) return 0;
    out->fe_id = packs[best].venue_fe_id;
    if (packs[best].venue_code[0]) snprintf(out->code, sizeof out->code, "%s", packs[best].venue_code);
    else snprintf(out->code, sizeof out->code, "%02dA", out->fe_id);
    snprintf(out->id, sizeof out->id, "%s", packs[best].id);
    snprintf(out->name, sizeof out->name, "%s", packs[best].name);
    return 1;
}

/* Knox and Jam 0.1.0's sixteen street spawns (community/nyc-map/stats.json): the eight crossings of the 3 x 3 block
 * grid around the park and eight on the outer streets, 17 to 38 m from the middle. */
static const float k_knox_spawns[16][2] = {
    { -204.8f, -204.8f }, { -204.8f, 204.8f }, { 204.8f, -204.8f }, { 204.8f, 204.8f },
    { 0.0f, -204.8f }, { 0.0f, 204.8f }, { -204.8f, 0.0f }, { 204.8f, 0.0f },
    { -204.8f, -444.6f }, { 204.8f, -444.6f }, { -204.8f, 444.6f }, { 204.8f, 444.6f },
    { -444.6f, -204.8f }, { 444.6f, -204.8f }, { -444.6f, 204.8f }, { 444.6f, 204.8f },
};

static int spawns_from_file(const char *game_path, const RtsMap *m, float *x, float *z, int max)
{
    char host[1400], *buf;
    FILE *f;
    long len;
    int n = 0;
    if (!pc_content_file(game_path, host, (int)sizeof host)) return 0;
    if (!(f = fopen(host, "rb"))) return 0;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len > 0 && len < 65536 && (buf = (char *)malloc((size_t)len + 1)) != NULL) {
        if (fread(buf, 1, (size_t)len, f) == (size_t)len) {
            char want[24];
            buf[len] = 0;
            snprintf(want, sizeof want, "V2BG_%s", m->code);
            /* the shared name (Studio's assets/bigarena.json) counts only when it names this venue */
            if (strstr(game_path, "bigarena.json") == NULL || strstr(buf, want))
                n = ow_rules_parse_spawns(buf, x, z, max);
        }
        free(buf);
    }
    fclose(f);
    return n;
}

int rts_spawns(const RtsMap *m, float *x, float *z, int max)
{
    char path[64];
    int n, i;
    if (!m || !m->fe_id) return 0;
    snprintf(path, sizeof path, "assets/bigarena_%s.json", m->code);
    if ((n = spawns_from_file(path, m, x, z, max)) > 0) return n;
    if ((n = spawns_from_file("assets/bigarena.json", m, x, z, max)) > 0) return n;
    if (strstr(m->id, "knox-and-jam") || getenv("RECOMP_RTS_KNOX_SPAWNS")) {
        for (i = 0; i < 16 && i < max; i++) { x[i] = k_knox_spawns[i][0]; z[i] = k_knox_spawns[i][1]; }
        return i;
    }
    return 0;
}

/* ---------------------------------------------------------------- the offline fight */

static volatile long s_armed, s_started;

int rts_offline_start(int fighter, int skin, int cpus, const int *cpu_fighters)
{
    RtsMap m;
    AutoMatch am;
    int i;
    if (!rts_access() || !rts_map(&m)) return 0;
    if (cpus < 1) cpus = 1;
    if (cpus > 3) cpus = 3;
    memset(&am, 0, sizeof am);
    am.type = 3;                /* Free for All (also with two: Big Arena's rules decide the end) */
    am.venue = m.fe_id;
    am.count = 1 + cpus;
    am.fighter[0] = fighter;
    am.pad[0] = 0;
    am.skin[0] = skin;
    for (i = 1; i <= cpus; i++) {
        int f = cpu_fighters && cpu_fighters[i - 1] >= 0 ? cpu_fighters[i - 1] : online_random_fighter();
        am.fighter[i] = f;
        am.pad[i] = -1;
    }
    for (; i < 4; i++) am.pad[i] = -1;
    s_armed = 1;
    s_started = 0;
    automatch_request(&am);
    fprintf(stderr, "[RTS] offline fight on %s (%s, feID %d): fighter %d against %d CPU\n", m.name, m.code, m.fe_id,
            fighter, cpus);
    return 1;
}

int rts_offline_armed(void) { return s_armed != 0; }

int rts_offline_fight_starts(void)
{
    /* tests: RECOMP_RTS_TEST_FIGHT=1 (with RECOMP_RTS_ACCESS=1) makes every offline fight on the map a Run the
     * Streets one, so a RECOMP_AUTOMATCH test fight on its venue plays by its rules without the menus */
    if (!s_armed && getenv("RECOMP_RTS_TEST_FIGHT") && *getenv("RECOMP_RTS_TEST_FIGHT") == '1' && rts_access()) {
        RtsMap m;
        if (rts_map(&m)) return 1;
    }
    if (!s_armed) return 0;
    s_started = 1;
    return 1;
}

void rts_main_menu(void)
{
    /* the request itself waits on the main menu: only once a Run the Streets fight ran does the menu end it */
    if (!s_armed || !s_started || automatch_pending()) return;
    fprintf(stderr, "[RTS] back at the main menu: the next offline fights are normal ones\n");
    s_armed = s_started = 0;
}

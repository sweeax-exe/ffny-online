/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Towers (T0): the game side. See towers.h and community/tower-mode-design.md. */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if !defined(_WIN32)
#include <sys/stat.h>
#endif
#include "recomp/gen/recomp_types.h"
#include "recomp_settings.h"
#include "towers.h"
#include "account.h"
#include "anticheat.h"
#include "automatch.h"
#include "fighter_banks.h"
#include "netplay.h"
#include "new_fighter_places.h"
#include "online_data.h"
#include "pc_settings.h"

#define ACTORS          0x003B92E0u     /* the four fighter records (openworld.c, test_control.c) */
#define STRIDE          0x12E8u
#define F_SLOT          0x28u           /* 0xFFFFFFFF: no fighter in this record */
#define F_HEALTH_NOW    0x350u
#define F_HEALTH_TOP    0x354u
#define F_HEALTH_SHARE  0x35Cu
#define F_MOMENTUM      0x374u
#define F_MOMENTUM_TOP  0x378u
#define G_SLOT_LEVEL    0x003B90A8u     /* each fighter slot's difficulty word (netplay.c cpu_level_apply) */
#define G_PHASE         0x003B8F30u     /* the match's phase: 1 fighting, 2 decided, 3 finished */
#define G_WINNER        0x003B8F40u     /* the winner's slot (phase 3) */
#define G_WINNER_FIRST  0x003B8F3Cu     /* the winner slot the decision writes first (phase 2) */
#define G_MATCH_FLAGS   0x003B9054u     /* bit 0x08: the match is over */
#define G_RESULTS       0x003BE130u     /* the result history: 16-byte records, u16 code at +0xE */
#define G_RESULT_COUNT  0x003BE624u
#define RESULTS_MAX     ((G_RESULT_COUNT - G_RESULTS) / 0x10u)
#define PLAYER_LEVEL    1u              /* the climber's own slot word: Normal (the momentum scale reads slot 0's) */

static CRITICAL_SECTION g_cs;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static TowerRun g_run;                  /* the run under way (or just ended) */
static int g_state = TOWERS_IDLE;
static unsigned long long g_since;
static int g_window_due;
static int g_last_floor, g_last_opp = -1, g_last_venue = -1, g_save_point, g_left, g_new_best, g_best_before;
static TowerFloorRules g_rules;         /* the fight's */
static int g_match_seen, g_decided, g_winner, g_draw, g_end_health;
static unsigned g_step;
static unsigned long long g_fight_t0;
static int g_momentum_done;
static int g_draw_loss;                 /* the run ended on a second draw on its floor */
static int g_cpu_end_health;            /* the CPU's health % when the fight was decided (the report) */

/* the file */
static TowerRun g_saved[TOWER_COUNT];
static int g_have[TOWER_COUNT], g_best[TOWER_COUNT], g_loaded;
static int g_resolved_tower, g_resolved_floor;  /* a fight left open at the last close, ended at this start */

static void net_left_open(const TowerRun *run, int floor);     /* below: the report of such a fight */
static void net_end(const TowerRun *run, const char *why);     /* ... and of a run given up */

/* ---------------------------------------------------------------- the fight's input log (T1)
 * The climber's pad bits each fight step (the fighter record's held bits, as test_control.c reads them), run-length
 * coded as (bits u32, steps u16) pairs. Kept for the floor just fought; the account service asks for it (log=1)
 * only when the run is in a top 10, and deletes it after its retention window (privacy page). */
#define F_PAD_HELD      0x9E0u
#define INPUT_LOG_MAX   8000            /* pairs: 48 KiB */
static unsigned char g_log[INPUT_LOG_MAX * 6];
static int g_log_pairs;
static uint32_t g_log_bits;
static unsigned g_log_count;

static void log_flush(void)
{
    unsigned char *p;
    if (!g_log_count || g_log_pairs >= INPUT_LOG_MAX) return;
    p = g_log + g_log_pairs * 6;
    p[0] = (unsigned char)g_log_bits; p[1] = (unsigned char)(g_log_bits >> 8);
    p[2] = (unsigned char)(g_log_bits >> 16); p[3] = (unsigned char)(g_log_bits >> 24);
    p[4] = (unsigned char)g_log_count; p[5] = (unsigned char)(g_log_count >> 8);
    g_log_pairs++;
    g_log_count = 0;
}

static void log_step(uint32_t bits)
{
    if (g_log_count && (bits != g_log_bits || g_log_count == 0xFFFFu)) log_flush();
    g_log_bits = bits;
    g_log_count++;
}

/* ---------------------------------------------------------------- the anti-cheat clock (T1)
 * Offline nobody runs the online thread whose passes notice a game that stood still (anticheat.c ac_net_tick): during
 * a tower fight a small thread of our own ticks it every 20 ms, so a suspended or frozen game shows in the report. */
static volatile LONG g_ac_on;
static HANDLE g_ac_thread;

static DWORD WINAPI ac_ticker(LPVOID unused)
{
    (void)unused;
    while (InterlockedCompareExchange(&g_ac_on, 1, 1)) {
        ac_net_tick(GetTickCount64());
        Sleep(20);
    }
    return 0;
}

static void ac_start(void)
{
    if (InterlockedExchange(&g_ac_on, 1)) return;
    ac_match_begin();
    ac_net_tick(GetTickCount64());
    if (g_ac_thread) { WaitForSingleObject(g_ac_thread, 1000); CloseHandle(g_ac_thread); }
    g_ac_thread = CreateThread(NULL, 0, ac_ticker, NULL, 0, NULL);
}

static void ac_stop(void) { InterlockedExchange(&g_ac_on, 0); }

static CRITICAL_SECTION g_qcs;          /* the account service's queue (below) */

static BOOL CALLBACK init_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_cs);
    InitializeCriticalSection(&g_qcs);
    return TRUE;
}
static void lock(void) { InitOnceExecuteOnce(&g_once, init_once, NULL, NULL); EnterCriticalSection(&g_cs); }
static void unlock(void) { LeaveCriticalSection(&g_cs); }

static int env_int(const char *name, int dflt)
{
    const char *v = getenv(name);
    return v && *v ? atoi(v) : dflt;
}

static void set_state(int s)
{
    g_state = s;
    g_since = GetTickCount64();
}

/* ---------------------------------------------------------------- the run file (<data>/towers.json) */

/* The data folder: DEFJAM_DATA, else the settings file's folder (pc_ui_actions.cpp folder_path does the same). */
static int data_dir(char *out, size_t size)
{
    const char *env = getenv("DEFJAM_DATA"), *ini = pc_settings_path(), *slash;
    if (env && *env) { snprintf(out, size, "%s", env); return 1; }
    if (!ini || !*ini) return 0;
    slash = strrchr(ini, '\\');
    if (!slash) slash = strrchr(ini, '/');
    if (!slash) { snprintf(out, size, "."); return 1; }
    snprintf(out, size, "%.*s", (int)(slash - ini), ini);
    return 1;
}

/* T1 (build 97): <data>/save/ffny/towers.json, inside the save area the launcher's cloud saves take (cloud_saves.cpp);
 * T0 kept it at <data>/towers.json (old: 1 gives that path, read once and moved). */
static int file_path_at(char *out, size_t size, int old)
{
    char data[1024];
    const char *test = getenv("RECOMP_TOWERS_FILE");
    if (test && *test) { if (old) return 0; snprintf(out, size, "%s", test); return 1; }
    if (!data_dir(data, sizeof data)) return 0;
    if (old) snprintf(out, size, "%s" HOST_SEP "towers.json", data);
    else snprintf(out, size, "%s" HOST_SEP "save" HOST_SEP "ffny" HOST_SEP "towers.json", data);
    return 1;
}

static int file_path(char *out, size_t size) { return file_path_at(out, size, 0); }

static void make_parent(const char *path)
{
    char dir[1024];
    char *slash;
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, HOST_SEP_CHAR);
    if (!slash) return;
    *slash = 0;
#if defined(_WIN32)
    CreateDirectoryA(dir, NULL);
#else
    mkdir(dir, 0755);
#endif
}

static void file_store(void);

static void file_load(void)
{
    char path[1024], old[1024];
    FILE *f;
    static char text[TOWER_COUNT * (TOWER_MAX_ROSTER * 8 + 2048) + 256];
    size_t n;
    int moved = 0, t;
    if (g_loaded) return;
    g_loaded = 1;
    memset(g_have, 0, sizeof g_have);
    memset(g_best, 0, sizeof g_best);
    if (!file_path(path, sizeof path)) return;
    if (!(f = fopen(path, "rb")) && file_path_at(old, sizeof old, 1) && (f = fopen(old, "rb")) != NULL) moved = 1;
    if (!f) return;
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    if (!towers_file_read(text, g_saved, g_have, g_best)) {
        fprintf(stderr, "[TOWERS] %s is not a run file: ignored\n", moved ? old : path);
        return;
    }
    fprintf(stderr, "[TOWERS] run file%s: best %d / %d, saved runs: Uptown %s, Five Boroughs %s\n",
            moved ? " (moved into the save area)" : "", g_best[0], g_best[1], g_have[0] ? "yes" : "no",
            g_have[1] ? "yes" : "no");
    /* the anti-quit rule (T1, owner 2026-11-19): a fight that was under way when the file was last written means the
     * game closed, crashed or was ended in it: that run ends on that floor now, as a loss */
    for (t = 0; t < TOWER_COUNT; t++) {
        int beaten;
        if (!g_have[t] || !towers_pending_is_loss(&g_saved[t], &beaten)) continue;
        fprintf(stderr, "[TOWERS] %s: the game closed during floor %d's fight: a loss, the run ends with %d floors beaten\n",
                towers_name(t + 1), g_saved[t].in_fight, beaten);
        g_resolved_tower = t + 1;
        g_resolved_floor = g_saved[t].in_fight;
        if (beaten > g_best[t]) g_best[t] = beaten;
        net_left_open(&g_saved[t], g_saved[t].in_fight);
        g_have[t] = 0;
        moved = 1;
    }
    if (moved) {
        file_store();
        if (file_path_at(old, sizeof old, 1)) remove(old);
    }
}

int towers_resolved_take(int *tower, int *floor)
{
    int got;
    lock();
    file_load();
    got = g_resolved_tower;
    if (got) {
        if (tower) *tower = g_resolved_tower;
        if (floor) *floor = g_resolved_floor;
        g_resolved_tower = 0;
    }
    unlock();
    return got != 0;
}

static void file_store(void)
{
    char path[1024], tmp[1100];
    static char text[TOWER_COUNT * (TOWER_MAX_ROSTER * 8 + 2048) + 256];
    const TowerRun *runs[TOWER_COUNT];
    FILE *f;
    int t, n;
    for (t = 0; t < TOWER_COUNT; t++) runs[t] = g_have[t] ? &g_saved[t] : NULL;
    n = towers_file_write(runs, g_best, text, (int)sizeof text);
    if (!n || !file_path(path, sizeof path)) return;
    make_parent(path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (!(f = fopen(tmp, "wb"))) { fprintf(stderr, "[TOWERS] cannot write %s\n", tmp); return; }
    fwrite(text, 1, (size_t)n, f);
    fclose(f);
#if defined(_WIN32)
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) remove(tmp);
#else
    if (rename(tmp, path)) remove(tmp);
#endif
}

static void save_run(void)
{
    file_load();
    g_saved[g_run.tower - 1] = g_run;
    g_have[g_run.tower - 1] = 1;
    file_store();
    fprintf(stderr, "[TOWERS] run saved: %s, floor %d of %d\n", towers_name(g_run.tower), g_run.floor, g_run.floors);
}

static void drop_saved(int tower)
{
    file_load();
    if (tower < 1 || tower > TOWER_COUNT) return;
    g_have[tower - 1] = 0;
    file_store();
}

int towers_saved(int tower, TowerRun *out)
{
    int have;
    if (tower < 1 || tower > TOWER_COUNT) return 0;
    lock();
    file_load();
    have = g_have[tower - 1];
    if (have && out) *out = g_saved[tower - 1];
    unlock();
    return have;
}

void towers_forget(int tower)
{
    lock();
    file_load();
    if (tower >= 1 && tower <= TOWER_COUNT && g_have[tower - 1]) net_end(&g_saved[tower - 1], "new");
    drop_saved(tower);
    unlock();
}

int towers_best(int tower)
{
    int b;
    if (tower < 1 || tower > TOWER_COUNT) return 0;
    lock();
    file_load();
    b = g_best[tower - 1];
    unlock();
    return b;
}

/* ---------------------------------------------------------------- the roster */

static int pool_of(int tower, int *pool, int max)
{
    const OnlineFighter *list;
    int n = online_fighters(&list), i, m = 0;
    for (i = 0; i < n && m < max; i++) pool[m++] = list[i].id;
    if (tower == TOWER_FIVE_BOROUGHS) {
        n = online_new_fighters(&list);
        for (i = 0; i < n && m < max; i++) pool[m++] = list[i].id;
        if (fighter_banks_ready())
            for (i = 0; i < fighter_banks_count() && m < max; i++) {
                const FighterBanksEntry *e = fighter_banks_at(i);
                if (e) pool[m++] = e->id;
            }
    }
    return m;
}

static int venues_of(int *venues, int max)
{
    const OnlineVenue *list;
    int n = online_venues(&list), i, m = 0;
    for (i = 0; i < n && m < max; i++)
        if (list[i].fe_id >= 1 && list[i].fe_id <= 63) venues[m++] = list[i].fe_id;
    return m;
}

int towers_floors_now(int tower)
{
    static int pool[TOWER_MAX_ROSTER];
    int n = pool_of(tower, pool, TOWER_MAX_ROSTER);
    return n > 1 ? n - 1 : 0;      /* the climber's own pick is never an opponent */
}

/* This PC can set the fighter up: a stock fighter's row, a new fighter's place, a community fighter's bank record. */
static int fighter_here(int id)
{
    if (id >= FB_FIRST_LOGICAL) return fighter_banks_find(id) != NULL;
    if (new_fighter_is_place(id)) {
        const OnlineFighter *nf;
        int n = online_new_fighters(&nf), i;
        for (i = 0; i < n; i++) if (nf[i].id == id) return 1;
        return 0;
    }
    return online_fighter_has_row(id);
}

/* ---------------------------------------------------------------- the account service (T1)
 * Server relay/accounts/towers.go: POST /api/towers/start (the seed and the run's id), /api/towers/fight per floor
 * ("start" when the fight is set up, then "result"), /api/towers/end, /api/towers/inputs (the input log, asked for
 * with log=1 only for a top-10 run); ?format=text answers key=value lines. One background thread sends the reports
 * in order; a report that finds the service unreachable is tried again a few times, then dropped (the service then
 * stops counting the run at the gap: "order"). Off while signed out or with RECOMP_TOWERS_OFFLINE=1 (tests). */

#define NET_QUEUE 32
typedef struct NetItem {
    char path[40];
    char run[TOWER_RUN_ID_MAX + 1];
    char *json;
    int floor, result;                  /* result: a fight's result (its reply may ask for the input log) */
    unsigned char *log;                 /* the floor's input log (result only), log_n bytes */
    int log_n;
} NetItem;
static NetItem g_q[NET_QUEUE];
static int g_q_head, g_q_n;
static HANDLE g_q_thread;
static volatile LONG g_q_busy;          /* a request is under way (tests wait for an empty queue) */
static char g_net_why[24];              /* the service's last reason for the run under way ("" none) */

static int net_on(void)
{
    const char *off = getenv("RECOMP_TOWERS_OFFLINE");
    return account_state() == ACCOUNT_IN && !(off && *off && *off != '0');
}

/* key=value line `key` of a text answer into out; 1 when present. */
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

static int kv_int(const char *text, const char *key)
{
    char v[24];
    return kv(text, key, v, sizeof v) ? atoi(v) : 0;
}

/* The service stopped counting this run (out of order, closed, switched off): the run goes on offline. */
static void net_drop_run(const char *run, const char *why)
{
    int t;
    lock();
    if (g_run.run_id[0] && !strcmp(g_run.run_id, run)) {
        snprintf(g_net_why, sizeof g_net_why, "%s", why && *why ? why : "offline");
        g_run.board = 0;
    }
    for (t = 0; t < TOWER_COUNT; t++)
        if (g_have[t] && !strcmp(g_saved[t].run_id, run)) g_saved[t].board = 0;
    unlock();
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void net_send_inputs(const NetItem *it)
{
    int n = 0, i, size = it->log_n * 4 / 3 + 256, status;
    char *json = (char *)malloc((size_t)size), reply[256];
    if (!json) return;
    n += snprintf(json, (size_t)size, "{\"run\":\"%s\",\"floor\":%d,\"data\":\"", it->run, it->floor);
    for (i = 0; i < it->log_n && n < size - 8; i += 3) {
        uint32_t v = (uint32_t)it->log[i] << 16 | (i + 1 < it->log_n ? (uint32_t)it->log[i + 1] << 8 : 0) |
                     (i + 2 < it->log_n ? it->log[i + 2] : 0);
        json[n++] = B64[(v >> 18) & 63];
        json[n++] = B64[(v >> 12) & 63];
        json[n++] = i + 1 < it->log_n ? B64[(v >> 6) & 63] : '=';
        json[n++] = i + 2 < it->log_n ? B64[v & 63] : '=';
    }
    n += snprintf(json + n, (size_t)(size - n), "\"}");
    status = account_call("POST", "/api/towers/inputs?format=text", json, reply, (int)sizeof reply, 8000, NULL);
    fprintf(stderr, "[TOWERS] input log of floor %d (%d bytes): HTTP %d\n", it->floor, it->log_n, status);
    free(json);
}

static DWORD WINAPI net_thread(LPVOID unused)
{
    (void)unused;
    for (;;) {
        NetItem it;
        char reply[1024];
        int status = 0, tries;
        EnterCriticalSection(&g_qcs);
        if (!g_q_n) {
            LeaveCriticalSection(&g_qcs);
            Sleep(50);
            continue;
        }
        it = g_q[g_q_head];
        InterlockedExchange(&g_q_busy, 1);
        LeaveCriticalSection(&g_qcs);
        for (tries = 0; tries < 5; tries++) {
            static const DWORD wait_ms[] = { 1000, 3000, 8000, 15000 };
            reply[0] = 0;
            status = account_call("POST", it.path, it.json, reply, (int)sizeof reply, 8000, NULL);
            reply[sizeof reply - 1] = 0;
            if (status != 0 && status < 500) break;
            if (tries < 4) Sleep(wait_ms[tries]);
        }
        {
            char why[24];
            kv(reply, "why", why, sizeof why);
            fprintf(stderr, "[TOWERS] service %s floor %d: HTTP %d%s%s%s%s\n", it.path, it.floor, status,
                    kv_int(reply, "ended") ? ", run ended" : "", kv_int(reply, "held") ? ", held" : "",
                    why[0] ? ", why " : "", why);
            if (status == 409 || status == 404 || (status == 200 && !strcmp(why, "order")))
                net_drop_run(it.run, status == 404 ? "offline" : why);
            if (status == 200 && it.result && kv_int(reply, "log") && it.log && it.log_n > 0) net_send_inputs(&it);
        }
        EnterCriticalSection(&g_qcs);
        g_q_head = (g_q_head + 1) % NET_QUEUE;
        g_q_n--;
        if (status == 404 || status == 401) {      /* switched off or signed out: the rest of this run's reports too */
            while (g_q_n && !strcmp(g_q[g_q_head].run, it.run)) {
                free(g_q[g_q_head].json);
                free(g_q[g_q_head].log);
                g_q_head = (g_q_head + 1) % NET_QUEUE;
                g_q_n--;
            }
        }
        InterlockedExchange(&g_q_busy, 0);
        LeaveCriticalSection(&g_qcs);
        free(it.json);
        free(it.log);
    }
    return 0;
}

static void net_enqueue(const char *path, const char *run, int floor, const char *json, int result, int with_log)
{
    NetItem *it;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_qcs);
    if (g_q_n >= NET_QUEUE) {
        LeaveCriticalSection(&g_qcs);
        fprintf(stderr, "[TOWERS] service queue full: report of floor %d dropped\n", floor);
        return;
    }
    it = &g_q[(g_q_head + g_q_n) % NET_QUEUE];
    memset(it, 0, sizeof *it);
    snprintf(it->path, sizeof it->path, "%s", path);
    snprintf(it->run, sizeof it->run, "%s", run);
    if ((it->json = (char *)malloc(strlen(json) + 1)) != NULL) memcpy(it->json, json, strlen(json) + 1);
    it->floor = floor;
    it->result = result;
    if (with_log && g_log_pairs > 0 && (it->log = (unsigned char *)malloc((size_t)g_log_pairs * 6)) != NULL) {
        memcpy(it->log, g_log, (size_t)g_log_pairs * 6);
        it->log_n = g_log_pairs * 6;
    }
    g_q_n++;
    if (!g_q_thread) g_q_thread = CreateThread(NULL, 0, net_thread, NULL, 0, NULL);
    LeaveCriticalSection(&g_qcs);
}

/* A floor's fight report: event "start", or "result" with result win/loss/draw/left and the fight's numbers. */
static void net_fight(const TowerRun *run, int floor, const char *event, const char *result, unsigned steps, int me_pct,
                      int cpu_pct, unsigned ms)
{
    char json[3072];
    int n;
    if (!run->run_id[0]) return;
    if (floor < run->net_next) {    /* a floor of a resumed chapter the service already counted: fought again quietly */
        if (result && strcmp(result, "win") && strcmp(result, "draw")) net_end(run, "retire");
        return;
    }
    n = snprintf(json, sizeof json, "{\"run\":\"%s\",\"floor\":%d,\"event\":\"%s\",\"opponent\":%d,\"venue\":%d", run->run_id,
                 floor, event, towers_opponent(run, floor), towers_venue(run, floor));
    if (result) {
        n += snprintf(json + n, sizeof json - (size_t)n, ",\"result\":\"%s\",\"fight\":{\"steps\":%u,\"health\":[%d,%d]},\"ms\":%u",
                      result, steps, me_pct, cpu_pct, ms);
        n = ac_json(json, (int)sizeof json - 2, n);
        n = netplay_files_json(json, (int)sizeof json - 2, n);
    }
    if (n < (int)sizeof json - 1) json[n++] = '}';
    json[n < (int)sizeof json ? n : (int)sizeof json - 1] = 0;
    net_enqueue("/api/towers/fight?format=text", run->run_id, floor, json, result != NULL, result && !strcmp(result, "win"));
}

static void net_left_open(const TowerRun *run, int floor)
{
    net_fight(run, floor, "result", "left", 0, -1, -1, 0);
}

static void net_end(const TowerRun *run, const char *why)
{
    char json[128];
    if (!run->run_id[0]) return;
    snprintf(json, sizeof json, "{\"run\":\"%s\",\"why\":\"%s\"}", run->run_id, why);
    net_enqueue("/api/towers/end?format=text", run->run_id, run->floor, json, 0, 0);
}

/* POST /api/towers/start for a new run (blocks, no lock held): 1 with the seed and the run's id, board and why. */
static int net_start(int tower, int fighter, int skin, const int *pool, int pool_n, const int *venues, int venue_n,
                     uint32_t *seed, char *run, int *board, char *why, int why_size)
{
    static char json[TOWER_MAX_ROSTER * 8 + TOWER_MAX_VENUES * 4 + 256];
    char reply[1024], v[48];
    int n, i, status;
    if (!net_on()) return 0;
    n = snprintf(json, sizeof json, "{\"tower\":%d,\"fighter\":%d,\"skin\":%d,\"rules\":%d,\"pool\":[", tower, fighter, skin,
                 TOWER_RULES_VERSION);
    for (i = 0; i < pool_n && n < (int)sizeof json - 16; i++) n += snprintf(json + n, sizeof json - (size_t)n, "%s%d", i ? "," : "", pool[i]);
    n += snprintf(json + n, sizeof json - (size_t)n, "],\"venues\":[");
    for (i = 0; i < venue_n && n < (int)sizeof json - 16; i++) n += snprintf(json + n, sizeof json - (size_t)n, "%s%d", i ? "," : "", venues[i]);
    snprintf(json + n, sizeof json - (size_t)n, "]}");
    reply[0] = 0;
    status = account_call("POST", "/api/towers/start?format=text", json, reply, (int)sizeof reply, 8000, NULL);
    reply[sizeof reply - 1] = 0;
    if (status != 200 || !kv(reply, "run", run, TOWER_RUN_ID_MAX + 1) || !run[0] || !kv(reply, "seed", v, sizeof v)) {
        fprintf(stderr, "[TOWERS] service: no run (HTTP %d): an offline climb\n", status);
        run[0] = 0;
        return 0;
    }
    *seed = (uint32_t)strtoul(v, NULL, 10);
    *board = kv_int(reply, "board");
    kv(reply, "why", why, why_size);
    fprintf(stderr, "[TOWERS] service: run %s, seed %08X, %d floors, roster %s, %s%s\n", run, *seed, kv_int(reply, "floors"),
            kv(reply, "roster", v, sizeof v) ? v : "?", *board ? "on the board" : "off the board: ", *board ? "" : why);
    return 1;
}

void towers_net_text(char *out, int size)
{
    static const struct { const char *why, *text; } k_why[] = {
        { "created", "YOUR OWN FIGHTER: NOT ON THE UPTOWN BOARD" }, { "packs", "UNOFFICIAL PACKS: NOT ON THE BOARD" },
        { "rules", "OLD RULES: NOT ON THE BOARD" }, { "roster", "CHANGED ROSTER: NOT ON THE BOARD" },
        { "order", "THE SERVER LOST TRACK: NOT ON THE BOARD" }, { "closed", "THE SERVER CLOSED THIS CLIMB" },
        { "offline", "OFFLINE: NOT ON THE BOARD" },
    };
    size_t k;
    lock();
    if (g_run.floors <= 0) out[0] = 0;
    else if (!g_run.run_id[0]) snprintf(out, (size_t)size, "OFFLINE: NOT ON THE BOARD");
    else if (g_run.board) snprintf(out, (size_t)size, "ON THE BOARD");
    else {
        const char *why = g_net_why[0] ? g_net_why : "";
        snprintf(out, (size_t)size, "NOT ON THE BOARD");
        for (k = 0; k < sizeof k_why / sizeof k_why[0]; k++)
            if (!strcmp(why, k_why[k].why)) snprintf(out, (size_t)size, "%s", k_why[k].text);
    }
    unlock();
}

/* ---------------------------------------------------------------- the fights */

static void test_overrides(void)
{
    /* tests: RECOMP_TEST_TOWERS_FLOORS=<n> makes the tower n floors tall (the top within reach of a short test);
     * RECOMP_TEST_TOWERS_OPPONENT=<floor>:<id> puts fighter id on that floor (swapped with its own floor) */
    const char *v = getenv("RECOMP_TEST_TOWERS_OPPONENT");
    int cap = env_int("RECOMP_TEST_TOWERS_FLOORS", 0);
    if (v && strchr(v, ':')) {
        int floor = atoi(v), id = atoi(strchr(v, ':') + 1), i, at = -1;
        if (!strcmp(strchr(v, ':') + 1, "community"))     /* the first community fighter beyond the places */
            id = fighter_banks_ready() && fighter_banks_count() > 0 ? fighter_banks_at(0)->id : -1;
        for (i = 0; i < g_run.floors; i++) if (g_run.roster[i] == id) at = i;
        if (floor >= 1 && floor <= g_run.floors && id >= 0) {
            if (at >= 0) { g_run.roster[at] = g_run.roster[floor - 1]; }
            else if (g_run.floors < TOWER_MAX_ROSTER) { g_run.roster[g_run.floors++] = g_run.roster[floor - 1]; }
            g_run.roster[floor - 1] = id;
            g_run.roster_hash = towers_roster_hash(g_run.roster, g_run.floors);
            fprintf(stderr, "[TOWERS] test: fighter %d on floor %d\n", id, floor);
        }
    }
    if (cap > 0 && cap < g_run.floors) {
        g_run.floors = cap;
        g_run.roster_hash = towers_roster_hash(g_run.roster, g_run.floors);
        fprintf(stderr, "[TOWERS] test: the tower is %d floors tall\n", cap);
    }
}

/* The floor's fight: skip floors whose fighter this PC cannot set up, then ask automatch for it. */
static int arm_fight(void)
{
    AutoMatch am;
    int opp, venue;
    while (g_run.floor <= g_run.floors && !fighter_here(towers_opponent(&g_run, g_run.floor))) {
        fprintf(stderr, "[TOWERS] floor %d: fighter %d is not on this PC: passed\n", g_run.floor,
                towers_opponent(&g_run, g_run.floor));
        g_run.skipped++;
        g_run.floor++;
    }
    if (g_run.floor > g_run.floors) return 0;
    opp = towers_opponent(&g_run, g_run.floor);
    venue = towers_venue(&g_run, g_run.floor);
    memset(&am, 0, sizeof am);
    am.type = 0;
    am.venue = venue;
    am.count = 2;
    am.fighter[0] = g_run.fighter;
    am.pad[0] = 0;
    am.skin[0] = g_run.skin;
    am.fighter[1] = opp;
    am.pad[1] = am.pad[2] = am.pad[3] = -1;
    towers_floor_rules(g_run.tower, g_run.floor, g_run.floors, &g_rules);
    g_match_seen = g_decided = g_draw = 0;
    g_winner = -1;
    g_step = 0;
    g_momentum_done = 0;
    set_state(TOWERS_ARMED);
    automatch_request(&am);
    fprintf(stderr, "[TOWERS] %s floor %d of %d: fighter %d against %d at venue %d (CPU level %d, damage x%.2f, "
            "momentum %.0f%%, health x%.2f%s)\n", towers_name(g_run.tower), g_run.floor, g_run.floors, g_run.fighter,
            opp, venue, g_rules.cpu_level, g_rules.cpu_damage, g_rules.cpu_momentum * 100.0f, g_rules.cpu_health,
            g_rules.boss ? ", boss floor" : "");
    return 1;
}

static uint32_t new_seed(void)
{
    LARGE_INTEGER q;
    uint32_t s;
    int test = env_int("RECOMP_TEST_TOWERS_SEED", -1);
    if (test >= 0) return (uint32_t)test;
    QueryPerformanceCounter(&q);
    s = (uint32_t)q.QuadPart ^ (uint32_t)(q.QuadPart >> 32) ^ (uint32_t)time(NULL) * 2654435761u;
    return towers_next32(&s);
}

int towers_start(int tower, int fighter, int skin)
{
    static int pool[TOWER_MAX_ROSTER];
    int venues[TOWER_MAX_VENUES], n, vn, ok, board = 0, online;
    uint32_t seed = 0;
    char run[TOWER_RUN_ID_MAX + 1] = "", why[24] = "";
    if (netplay_active() || (tower != TOWER_UPTOWN && tower != TOWER_FIVE_BOROUGHS)) return 0;
    n = pool_of(tower, pool, TOWER_MAX_ROSTER);
    vn = venues_of(venues, TOWER_MAX_VENUES);
    /* T1: signed in, the service deals the seed (no lock held while it answers) */
    online = n > 0 && vn > 0 && net_start(tower, fighter, skin, pool, n, venues, vn, &seed, run, &board, why, (int)sizeof why);
    lock();
    {   /* a saved run of this tower is replaced: the service ends it too (it also does at a new start) */
        file_load();
        if (g_have[tower - 1] && g_saved[tower - 1].run_id[0] && strcmp(g_saved[tower - 1].run_id, run))
            net_end(&g_saved[tower - 1], "new");
    }
    if (!towers_new_run(&g_run, tower, online ? seed : new_seed(), fighter, skin, pool, n, venues, vn)) {
        unlock();
        fprintf(stderr, "[TOWERS] %s: no run (%d fighters, %d venues read)\n", towers_name(tower), n, vn);
        return 0;
    }
    g_run.started = (long long)time(NULL);
    if (online) {
        snprintf(g_run.run_id, sizeof g_run.run_id, "%s", run);
        g_run.board = board;
        snprintf(g_net_why, sizeof g_net_why, "%s", why);
    } else
        g_net_why[0] = 0;
    test_overrides();
    g_best_before = g_best[tower - 1];
    g_new_best = 0;
    g_last_floor = 0;
    g_last_opp = g_last_venue = -1;
    g_draw_loss = 0;
    fprintf(stderr, "[TOWERS] run start: %s, seed %08X, fighter %d (skin %d), %d floors, %d venues, roster %08X, %s\n",
            towers_name(tower), g_run.seed, fighter, skin, g_run.floors, g_run.venue_n, g_run.roster_hash,
            online ? (board ? "on the service, board" : "on the service, off the board") : "offline");
    save_run();         /* a run is resumable from its first floor */
    ok = arm_fight();
    g_window_due = ok;  /* the window holds the input over the load, the first floor's card in it */
    unlock();
    return ok;
}

int towers_resume(int tower)
{
    int ok;
    if (netplay_active() || tower < 1 || tower > TOWER_COUNT) return 0;
    lock();
    file_load();
    if (!g_have[tower - 1]) { unlock(); return 0; }
    g_run = g_saved[tower - 1];
    /* (a fight left open, in_fight, already ended this run when the file was read: file_load) */
    g_net_why[0] = 0;
    g_draw_loss = 0;
    g_best_before = g_best[tower - 1];
    g_new_best = 0;
    g_last_floor = 0;
    g_last_opp = g_last_venue = -1;
    fprintf(stderr, "[TOWERS] run resumed: %s, seed %08X, floor %d of %d (%s%s)\n", towers_name(tower), g_run.seed,
            g_run.floor, g_run.floors, g_run.run_id[0] ? "run " : "offline", g_run.run_id);
    ok = arm_fight();
    g_window_due = ok;
    unlock();
    return ok;
}

/* The fight is decided: who won, from the phase and result words (as netplay.c reads them). 1 when known. */
static int read_decision(int *winner, int *draw)
{
    uint32_t phase = MEM32(G_PHASE), count = MEM32(G_RESULT_COUNT), code = 0, w = 0xFFFFFFFFu;
    if (count && count <= RESULTS_MAX) code = MEM16(G_RESULTS + (count - 1u) * 0x10u + 0xEu);
    *draw = 0;
    if ((code & 1u) && (code & 4u)) { *draw = 1; *winner = -1; return 1; }
    if (phase == 3u) w = MEM32(G_WINNER);
    else if (phase == 2u || ((code & 1u) && (MEM32(G_MATCH_FLAGS) & 8u))) w = MEM32(G_WINNER_FIRST);
    else return 0;
    if (w > 1u) { *draw = 1; *winner = -1; return 1; }
    *winner = (int)w;
    return 1;
}

void towers_match_start(void)
{
    if (netplay_active()) return;
    lock();
    if (g_state == TOWERS_ARMED || (g_state == TOWERS_FIGHTING && g_decided)) {
        if (g_state == TOWERS_FIGHTING)     /* a second match of the same set-up (a rematch the menu ran) */
            fprintf(stderr, "[TOWERS] the floor's fight runs again\n");
        set_state(TOWERS_FIGHTING);
        g_match_seen = 1;
        g_decided = g_draw = 0;
        g_winner = -1;
        g_step = 0;
        g_momentum_done = 0;
        g_fight_t0 = GetTickCount64();
        g_log_pairs = 0;
        g_log_count = 0;
        ac_start();
        net_fight(&g_run, g_run.floor, "start", NULL, 0, 0, 0, 0);
        /* the CPU's level for this floor and the climber's fixed one, after the set-up copied the words and before
         * the CPU brains load (netplay.c cpu_level_apply) */
        file_load();
        if (!g_have[g_run.tower - 1] || g_saved[g_run.tower - 1].seed != g_run.seed) {
            /* the anti-quit rule needs the run in the file: a run whose copy is gone (another start) is put back */
            g_saved[g_run.tower - 1] = g_run;
            g_saved[g_run.tower - 1].floor = g_run.floor;
            g_have[g_run.tower - 1] = 1;
        }
        g_saved[g_run.tower - 1].in_fight = g_run.floor;     /* cleared when the fight counts */
        file_store();
        MEM32(G_SLOT_LEVEL) = PLAYER_LEVEL;
        MEM32(G_SLOT_LEVEL + 4u) = (uint32_t)g_rules.cpu_level;
        fprintf(stderr, "[TOWERS] floor %d set up: slot levels %u %u\n", g_run.floor, MEM32(G_SLOT_LEVEL),
                MEM32(G_SLOT_LEVEL + 4u));
    }
    unlock();
}

void towers_step(void)
{
    uint32_t me = ACTORS, cpu = ACTORS + STRIDE;
    if (g_state != TOWERS_FIGHTING || netplay_active() || netplay_rerunning()) return;
    lock();
    if (g_state != TOWERS_FIGHTING) { unlock(); return; }
    g_step++;
    if (!g_decided && MEM32(me + F_SLOT) != 0xFFFFFFFFu) log_step(MEM32(me + F_PAD_HELD) & 0xFFFFFu);
    {   /* tests: RECOMP_TEST_TOWERS_EXIT_IN_FIGHT=<floor> ends the game 4 s into that floor's fight (a closed or crashed
         * game: the next start must count it as a loss) */
        static int exit_floor = -2;
        if (exit_floor == -2) exit_floor = env_int("RECOMP_TEST_TOWERS_EXIT_IN_FIGHT", -1);
        if (exit_floor == g_run.floor && g_step == 240) {
            fprintf(stderr, "[TOWERS] test: the game ends in floor %d's fight\n", g_run.floor);
            fflush(stderr);
            _exit(3);
        }
    }
    if (g_step == 1 && g_run.health_pct < 100 && MEM32(me + F_SLOT) != 0xFFFFFFFFu) {
        float top = MEMF(me + F_HEALTH_TOP);
        if (top > 0.0f) {
            MEMF(me + F_HEALTH_NOW) = top * (float)g_run.health_pct / 100.0f;
            MEMF(me + F_HEALTH_SHARE) = (float)g_run.health_pct / 100.0f;
            fprintf(stderr, "[TOWERS] the climber starts at %d%% health\n", g_run.health_pct);
        }
    }
    if (!g_momentum_done && g_step <= 60 && MEM32(cpu + F_SLOT) != 0xFFFFFFFFu) {
        float top = MEMF(cpu + F_MOMENTUM_TOP);
        if (top > 0.0f) {
            g_momentum_done = 1;
            if (g_rules.cpu_momentum > 0.0f) {
                MEMF(cpu + F_MOMENTUM) = top * g_rules.cpu_momentum;
                fprintf(stderr, "[TOWERS] the CPU starts with %.0f%% momentum (%.1f of %.1f)\n",
                        g_rules.cpu_momentum * 100.0f, (double)MEMF(cpu + F_MOMENTUM), (double)top);
            }
        }
    }
    if (!g_decided) {
        int w, d;
        if (read_decision(&w, &d)) {
            float top = MEMF(me + F_HEALTH_TOP), now = MEMF(me + F_HEALTH_NOW);
            float ctop = MEMF(cpu + F_HEALTH_TOP), cnow = MEMF(cpu + F_HEALTH_NOW);
            g_decided = 1;
            g_winner = w;
            g_draw = d;
            g_end_health = top > 0.0f ? (int)(100.0f * now / top + 0.5f) : 100;
            g_cpu_end_health = ctop > 0.0f && cnow == cnow ? (int)(100.0f * cnow / ctop + 0.5f) : -1;
            if (g_cpu_end_health > 100) g_cpu_end_health = 100;
            if (g_cpu_end_health < -1) g_cpu_end_health = 0;
            log_flush();
            fprintf(stderr, "[TOWERS] floor %d decided at step %u: %s; the climber's health %d%%\n", g_run.floor,
                    g_step, d ? "a draw" : w == 0 ? "the climber won" : "the CPU won", g_end_health);
        }
    }
    unlock();
}

void towers_health_change(void)
{
    uint32_t r;
    float delta;
    if (g_state != TOWERS_FIGHTING || netplay_active()) return;
    r = MEM32(g_esp + 4);
    delta = MEMF(g_esp + 8);
    if (delta >= 0.0f) return;
    if (r == ACTORS && g_rules.cpu_damage != 1.0f) MEMF(g_esp + 8) = delta * g_rules.cpu_damage;
    else if (r == ACTORS + STRIDE && g_rules.cpu_health > 1.0f) MEMF(g_esp + 8) = delta / g_rules.cpu_health;
}

/* Back on a menu after the floor's fight: the result counts. */
static void finish_fight(void)
{
    int w = g_winner, d = g_draw, beaten = g_run.floor;
    unsigned long long ms = GetTickCount64() - g_fight_t0;
    if (!g_decided && read_decision(&w, &d)) g_decided = 1;
    g_last_floor = g_run.floor;
    g_last_opp = towers_opponent(&g_run, g_run.floor);
    g_last_venue = towers_venue(&g_run, g_run.floor);
    g_run.climb_ms += (uint32_t)(ms > 3600000ull ? 3600000ull : ms);
    g_left = !g_decided;
    g_save_point = 0;
    g_run.in_fight = 0;
    ac_stop();
    log_flush();
    file_load();
    if (g_have[g_run.tower - 1]) g_saved[g_run.tower - 1].in_fight = 0;     /* the fight counted */
    {   /* T1: the floor's report (the climber's health and the CPU's, by the decision; the fight's steps and time) */
        const char *result = !g_decided ? "left" : d ? "draw" : w == 0 ? "win" : "loss";
        net_fight(&g_run, beaten, "result", result, g_step, g_decided ? g_end_health : -1, g_decided ? g_cpu_end_health : -1,
                  (unsigned)(ms > 3600000ull ? 3600000ull : ms));
    }
    g_draw_loss = g_decided && d && !towers_draw_refights(g_run.refights);
    if (g_decided && d && !g_draw_loss) {
        g_run.draws++;
        g_run.refights++;
        if (g_have[g_run.tower - 1] && g_saved[g_run.tower - 1].seed == g_run.seed) g_saved[g_run.tower - 1].refights = g_run.refights;
        file_store();
        set_state(TOWERS_DRAW);
        fprintf(stderr, "[TOWERS] floor %d: a draw, fought again (refight %d of %d)\n", beaten, g_run.refights, TOWER_MAX_REFIGHTS);
    } else if (g_decided && !d && w == 0) {
        int rule = recomp_settings_get("towers", g_run.tower == 1 ? "health1" : "health2", TOWER_HEALTH_FULL);
        int refill = recomp_settings_get("towers", g_run.tower == 1 ? "refill1" : "refill2", 30);
        int chapter = env_int("RECOMP_TEST_TOWERS_CHAPTER", TOWER_CHAPTER);
        file_load();
        if (beaten > g_best[g_run.tower - 1]) {
            g_best[g_run.tower - 1] = beaten;
            g_new_best = beaten > g_best_before;
        }
        g_run.health_pct = towers_health_after_win(rule, refill, g_end_health);
        g_run.floor++;
        g_run.refights = 0;
        if (g_run.floor > g_run.net_next) g_run.net_next = g_run.floor;
        /* the service's next floor goes in the file at every win, also between save points (Five Boroughs resumes at
         * its chapter's start and fights those floors again without reports) */
        if (g_have[g_run.tower - 1] && g_saved[g_run.tower - 1].seed == g_run.seed) g_saved[g_run.tower - 1].net_next = g_run.net_next;
        if (g_run.floor > g_run.floors) {
            g_have[g_run.tower - 1] = 0;
            file_store();
            set_state(TOWERS_CLEARED);
            fprintf(stderr, "[TOWERS] %s cleared: %d floors in %.1f min\n", towers_name(g_run.tower), g_run.floors,
                    g_run.climb_ms / 60000.0);
        } else {
            g_save_point = towers_is_save_point(g_run.tower, beaten, g_run.floors, chapter);
            if (g_save_point) save_run();
            else file_store();      /* the best floor */
            set_state(TOWERS_WON);
            fprintf(stderr, "[TOWERS] floor %d won (%.1f s, health %d%%%s); next floor %d\n", beaten, ms / 1000.0,
                    g_end_health, g_save_point ? ", a save point" : "", g_run.floor);
        }
    } else {
        file_load();
        g_have[g_run.tower - 1] = 0;
        file_store();
        set_state(TOWERS_LOST);
        fprintf(stderr, "[TOWERS] floor %d %s: the run ends with %d floors beaten\n", beaten,
                g_left ? "left during the fight" : g_draw_loss ? "drawn a second time (a loss)" : "lost", beaten - 1);
    }
    g_window_due = 1;
}

/* The climb's sound: one of the front end's own (Audio.Play(n), the call its menus make), played at the next front-end
 * call on the game's thread. */
static volatile LONG g_sfx_due;         /* sound + 1, 0 none */
void towers_play_sfx(int id)
{
    if (id >= 0 && id < 1000) InterlockedExchange(&g_sfx_due, (LONG)id + 1);
}

static void test_start(void)
{
    /* tests: RECOMP_TEST_TOWERS="<tower>,<fighter>[,<skin>]" starts a run at the first settled main menu,
     * "<tower>,resume" resumes the saved one */
    static int done;
    const char *v = getenv("RECOMP_TEST_TOWERS");
    int tower, fighter = 55, skin = 0;
    if (done || !v || !*v) return;
    done = 1;
    tower = atoi(v);
    if (strstr(v, "resume")) { if (!towers_resume(tower)) fprintf(stderr, "[TOWERS] test: no saved run to resume\n"); return; }
    if (strchr(v, ',')) fighter = atoi(strchr(v, ',') + 1);
    if (strchr(v, ',') && strchr(strchr(v, ',') + 1, ',')) skin = atoi(strchr(strchr(v, ',') + 1, ',') + 1);
    if (!towers_start(tower, fighter, skin)) fprintf(stderr, "[TOWERS] test: the run did not start\n");
}

void towers_on_funccall(const char *text)
{
    int menu;
    static int in_sfx;
    if (netplay_active()) return;
    if (!in_sfx && g_sfx_due) {     /* the climb's sound (towers_play_sfx), on the game's thread like every FE call */
        char call[32];
        LONG id = InterlockedExchange(&g_sfx_due, 0);
        if (id > 0) {
            in_sfx = 1;
            snprintf(call, sizeof call, "Audio.Play(%ld)", (long)(id - 1));
            defjam_fe_call(call);
            in_sfx = 0;
        }
    }
    if (in_sfx) return;
    menu = strstr(text, "GetScreenInfo(battle/cmtype") != NULL || strstr(text, "GetScreenInfo(intMain/mainMenu.swf") != NULL;
    if (!menu) return;
    if (strstr(text, "mainMenu") && !automatch_pending()) test_start();
    lock();
    if (g_state == TOWERS_FIGHTING && g_match_seen) finish_fight();
    unlock();
}

int towers_rematch_request(void)
{
    int took = 0;
    if (netplay_active()) return 0;
    lock();
    if (g_state == TOWERS_FIGHTING && g_match_seen) {
        unsigned saved;
        took = 1;
        unlock();
        fprintf(stderr, "[TOWERS] Rematch on the results goes on as No (the tower decides the next fight)\n");
        saved = defjam_fe_call("MemCard.QuerySave( battle/rewardun.swf )");
        fprintf(stderr, "[TOWERS] MemCard.QuerySave -> %08X; Game.Quit()\n", saved);
        defjam_fe_call("Game.Quit()");
        return took;
    }
    unlock();
    return took;
}

/* ---------------------------------------------------------------- the screens' side */

void towers_view(TowersView *out)
{
    memset(out, 0, sizeof *out);
    lock();
    out->state = g_state;
    out->tower = g_run.tower;
    out->floors = g_run.floors;
    out->floor = g_run.floor;
    out->last_floor = g_last_floor;
    out->last_opponent = g_last_opp;
    out->last_venue = g_last_venue;
    out->next_opponent = towers_opponent(&g_run, g_run.floor);
    out->next_venue = g_run.floor <= g_run.floors ? towers_venue(&g_run, g_run.floor) : -1;
    if (g_run.floor <= g_run.floors) towers_floor_rules(g_run.tower, g_run.floor, g_run.floors, &out->next_rules);
    out->save_point = g_save_point;
    out->left_fight = g_left;
    file_load();
    out->best = g_run.tower >= 1 && g_run.tower <= TOWER_COUNT ? g_best[g_run.tower - 1] : 0;
    out->new_best = g_new_best;
    out->seed = g_run.seed;
    out->climb_ms = g_run.climb_ms;
    out->fighter = g_run.fighter;
    out->skin = g_run.skin;
    out->since = g_since;
    out->online = g_run.run_id[0] != 0;
    out->board = out->online && g_run.board;
    snprintf(out->why, sizeof out->why, "%s", g_net_why);
    out->draw_loss = g_draw_loss;
    unlock();
}

const TowerRun *towers_run(void) { return &g_run; }
int towers_active(void) { return g_state != TOWERS_IDLE; }
int towers_state(void) { return g_state; }

int towers_window_take(void)
{
    int due;
    lock();
    due = g_window_due && (g_state == TOWERS_ARMED || g_state == TOWERS_WON || g_state == TOWERS_DRAW ||
                           g_state == TOWERS_LOST || g_state == TOWERS_CLEARED);
    if (due) g_window_due = 0;
    unlock();
    return due;
}

int towers_climb_on(void)
{
    int ok = 0;
    lock();
    if (g_state == TOWERS_WON || g_state == TOWERS_DRAW) ok = arm_fight();
    if (!ok && g_state != TOWERS_ARMED) set_state(TOWERS_IDLE);
    unlock();
    return ok;
}

void towers_save_quit(void)
{
    lock();
    if (g_state == TOWERS_WON && g_save_point)
        fprintf(stderr, "[TOWERS] save and quit: %s resumes on floor %d\n", towers_name(g_run.tower), g_run.floor);
    set_state(TOWERS_IDLE);
    unlock();
}

void towers_done(void)
{
    lock();
    if (g_state == TOWERS_LOST || g_state == TOWERS_CLEARED) set_state(TOWERS_IDLE);
    unlock();
}

int towers_card_wanted(void)
{
    /* from the request until the match is set up, and a short fade after it */
    if (g_state == TOWERS_ARMED) return 1;
    return g_state == TOWERS_FIGHTING && g_step == 0 && GetTickCount64() - g_since < 600;
}

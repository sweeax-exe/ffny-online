/*
 * Anti-cheat facts for the online match reports (anticheat.h). Windows only, like online play.
 *
 * Freezes: the online thread passes its loop at least every 20 ms (select() timeout). When a pass
 * comes more than AC_FREEZE_MS after the one before, the whole process stood still: suspended
 * (Resource Monitor, Process Explorer, PsSuspend: the freeze exploit of October 2026), stopped in
 * a debugger, or the PC slept. A network cut does not do that, the thread keeps passing.
 *
 * Tools: the names of running processes are compared with a short list of known cheat, memory,
 * debugger, lag-switch and process-suspend tools. Only the names that match are reported.
 *
 * Fingerprint: SHA-256 of Windows' MachineGuid with a fixed prefix, first 16 bytes. The account
 * service keys it again (HMAC) before keeping it, so the stored value says nothing outside it.
 */
#define _CRT_SECURE_NO_WARNINGS
#include "anticheat.h"
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#pragma comment(lib, "bcrypt.lib")

#define AC_FREEZE_MS 2000u      /* the online thread never waits this long on its own */
#define AC_TOOLS_MAX 8

/* Lower case, compared with the start of the process name (so cheatengine-x86_64.exe matches). */
static const char *const k_tools[] = {
    "cheatengine", "cheat engine", "ce.exe", "artmoney", "wemod", "x64dbg", "x32dbg", "ollydbg", "windbg",
    "ida.exe", "ida64.exe", "processhacker", "systeminformer", "procexp", "pssuspend", "resmon.exe",
    "clumsy.exe", "netlimiter", "nlclientapp", "wpe", "sandboxie", "speedhack", "autohotkey",
};

static SRWLOCK g_lock = SRWLOCK_INIT;
static uint64_t g_last_tick;
static int g_armed;
static uint32_t g_froze_ms, g_freezes, g_longest_ms;
static char g_tools[AC_TOOLS_MAX][40];
static int g_ntools;
static int g_instances;
static int g_games;
static HANDLE g_game_mark;
static char g_hwid[33];

/* Games running on the PC. The launcher is defjam_recomp.exe too and stays in the background while
 * it runs the game, so counting processes counted every player twice (build 37, MrRenoldz and
 * marcelo). Each game in a fight holds one of these named mutexes; the launcher never takes one,
 * and Windows drops them with their process. Called with g_lock held. */
#define AC_GAME_MARKS 16
static void count_games(void)
{
    wchar_t name[48];
    int n, games = 0;
    if (!g_game_mark)
        for (n = 0; n < AC_GAME_MARKS && !g_game_mark; n++) {
            HANDLE h;
            swprintf(name, 48, L"Local\\FFNY-game-%d", n);
            h = CreateMutexW(NULL, FALSE, name);
            if (h && GetLastError() == ERROR_ALREADY_EXISTS) CloseHandle(h);
            else g_game_mark = h;
        }
    for (n = 0; n < AC_GAME_MARKS; n++) {
        HANDLE h;
        swprintf(name, 48, L"Local\\FFNY-game-%d", n);
        h = OpenMutexW(SYNCHRONIZE, FALSE, name);
        if (h) { games++; CloseHandle(h); }
    }
    if (games > g_games) g_games = games;
}

static void lower(char *s)
{
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
}

static void add_tool(const char *name)
{
    int i;
    for (i = 0; i < g_ntools; i++)
        if (!strcmp(g_tools[i], name)) return;
    if (g_ntools < AC_TOOLS_MAX) snprintf(g_tools[g_ntools++], sizeof(g_tools[0]), "%s", name);
}

/* The running processes: copies of the game, and known tools. Called with g_lock held. */
static void scan_processes(void)
{
    PROCESSENTRY32W pe;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    int copies = 0;
    unsigned i;
    if (snap == INVALID_HANDLE_VALUE) return;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            char name[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, name, (int)sizeof(name), NULL, NULL);
            lower(name);
            if (!strcmp(name, "defjam_recomp.exe")) copies++;
            for (i = 0; i < sizeof(k_tools) / sizeof(k_tools[0]); i++)
                if (!strncmp(name, k_tools[i], strlen(k_tools[i]))) add_tool(name);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (copies > g_instances) g_instances = copies;
}

static void make_hwid(void)
{
    wchar_t guid[64];
    DWORD size = sizeof(guid);
    char text[96];
    uint8_t digest[32];
    BCRYPT_ALG_HANDLE alg;
    int i;
    if (g_hwid[0]) return;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, NULL, guid, &size) != ERROR_SUCCESS)
        return;
    snprintf(text, sizeof(text), "ffny-pc:%ls", guid);
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) return;
    if (BCryptHash(alg, NULL, 0, (PUCHAR)text, (ULONG)strlen(text), digest, sizeof(digest)) == 0)
        for (i = 0; i < 16; i++) snprintf(g_hwid + 2 * i, 3, "%02x", digest[i]);
    BCryptCloseAlgorithmProvider(alg, 0);
}

void ac_match_begin(void)
{
    AcquireSRWLockExclusive(&g_lock);
    g_froze_ms = g_freezes = g_longest_ms = 0;
    g_ntools = g_instances = g_games = 0;
    g_armed = 1;
    g_last_tick = 0;
    scan_processes();
    count_games();
    make_hwid();
    ReleaseSRWLockExclusive(&g_lock);
    fprintf(stderr, "[ANTICHEAT] match armed: %d game%s running (%d process%s), %d known tool%s\n", g_games,
            g_games == 1 ? "" : "s", g_instances, g_instances == 1 ? "" : "es", g_ntools, g_ntools == 1 ? "" : "s");
}

void ac_net_tick(uint64_t now_ms)
{
    uint64_t gap;
    if (!g_armed) {
        g_last_tick = now_ms;
        return;
    }
    gap = g_last_tick ? now_ms - g_last_tick : 0;
    g_last_tick = now_ms;
    if (gap < AC_FREEZE_MS) return;
    AcquireSRWLockExclusive(&g_lock);
    g_freezes++;
    g_froze_ms += (uint32_t)gap;
    if (gap > g_longest_ms) g_longest_ms = (uint32_t)gap;
    ReleaseSRWLockExclusive(&g_lock);
    fprintf(stderr, "[ANTICHEAT] the whole game stood still for %llu ms during the fight\n", (unsigned long long)gap);
}

static int put_name(char *json, int size, int n, const char *s)
{
    if (n < size) json[n++] = '"';
    for (; *s && n < size - 2; s++)
        if (*s != '"' && *s != '\\' && (unsigned char)*s >= 0x20) json[n++] = *s;
    if (n < size) json[n++] = '"';
    return n;
}

int ac_pc_id(uint8_t out[16])
{
    int i, ok;
    AcquireSRWLockExclusive(&g_lock);
    make_hwid();
    ok = g_hwid[0] != 0;
    for (i = 0; ok && i < 16; i++) {
        unsigned v = 0;
        if (sscanf(g_hwid + 2 * i, "%2x", &v) != 1) ok = 0;
        out[i] = (uint8_t)v;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return ok;
}

int ac_json(char *json, int size, int n)
{
    int i;
    AcquireSRWLockExclusive(&g_lock);
    scan_processes();
    count_games();
    n += snprintf(json + n, (size_t)(size - n), ",\"ac\":{\"froze_ms\":%u,\"freezes\":%u,\"longest_ms\":%u,\"copies\":%d,\"game_copies\":%d,\"pc\":\"%s\",\"tools\":[",
                  g_froze_ms, g_freezes, g_longest_ms, g_instances, g_games, g_hwid);
    for (i = 0; i < g_ntools && n < size - 60; i++) {
        if (i) json[n++] = ',';
        n = put_name(json, size, n, g_tools[i]);
    }
    n += snprintf(json + n, (size_t)(size - n), "]}");
    ReleaseSRWLockExclusive(&g_lock);
    return n < size ? n : size - 1;
}

#else

void ac_match_begin(void) {}
void ac_net_tick(uint64_t now_ms) { (void)now_ms; }
int ac_json(char *json, int size, int n) { (void)json; (void)size; return n; }
int ac_pc_id(uint8_t out[16]) { (void)out; return 0; }

#endif

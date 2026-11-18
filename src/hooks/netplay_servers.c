/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The online relays and the choice between them; see netplay_servers.h. */
#define _CRT_SECURE_NO_WARNINGS
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include "host.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "netplay.h"
#include "netplay_servers.h"
#include "recomp_settings.h"
#include "pc_settings.h"
#include "rts.h"

/* The relays: the table in netplay_servers_rules.c (netservers_def). */
#define k_relays(i) (*netservers_def(i))

static const char *setting_server(char *buf, int size)
{
    return recomp_settings_get_text("online", "server_debug", buf, (size_t)size, "auto");   /* build 91: hidden */
}

/* The setting's text (netservers_parse_choice): auto, eu, na, sg, all; the old "both" is all. */
enum NetServerChoice netservers_choice(void)
{
    char buf[128];
    return netservers_parse_choice(setting_server(buf, sizeof(buf)));
}

const char *netservers_name(int index)
{
    return index >= 0 && index < NETSERVER_COUNT ? k_relays(index).name : "custom";
}

const char *netservers_short(int index)
{
    return index >= 0 && index < NETSERVER_COUNT ? k_relays(index).short_name : "custom";
}

#if defined(_WIN32)

#define STATUS_TYPE   0x24
#define STATUS_LEN    26
#define PROBES        3
#define PROBE_GAP_MS  300
#define LATE_MS       1500      /* after the last probe: how long to wait for answers */
#define ROUND_EVERY   10000     /* a new round when asked and the last one is this old */
#define DECIDE_MS     2500      /* Automatic decides with what it has after this */

static CRITICAL_SECTION g_cs;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static struct {
    NetServerInfo s[NETSERVER_COUNT];
    HANDLE thread, wake;
    volatile LONG running;          /* a round is under way */
    ULONGLONG round_started, round_done;
    int rounds;                     /* finished rounds */
    int pick;                       /* Automatic's choice for this session, -1 not yet */
    int override;                   /* a switch made this session, -1 none */
    ULONGLONG first_ask;            /* Automatic was first asked for its choice */
} P;

static BOOL CALLBACK init_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    WSADATA wsa;
    int i;
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_cs);
    WSAStartup(MAKEWORD(2, 2), &wsa);
    P.pick = P.override = -1;
    /* build 91: a test that points any relay elsewhere (RECOMP_NETPLAY_SERVER_EU etc.) never reaches a live relay
     * it did not name, so a relay added to the table later needs no change to the test scripts: 127.0.0.1:9 */
    const char *test_any = NULL;
    for (i = 0; i < NETSERVER_COUNT && !test_any; i++) {
        const char *e = getenv(k_relays(i).env);
        if (e && *e) test_any = e;
    }
    for (i = 0; i < NETSERVER_COUNT; i++) {
        const char *env = getenv(k_relays(i).env);
        if (test_any && !(env && *env)) env = "127.0.0.1:9";
        P.s[i].id = k_relays(i).id;
        P.s[i].name = k_relays(i).name;
        P.s[i].short_name = k_relays(i).short_name;
        snprintf(P.s[i].address, sizeof(P.s[i].address), "%s", env && *env ? env : k_relays(i).address);
        P.s[i].ping_ms = -1;
        if (env && *env) fprintf(stderr, "[SERVERS] tests: %s is %s\n", k_relays(i).name, P.s[i].address);
    }
    P.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    return TRUE;
}

static void init(void) { InitOnceExecuteOnce(&g_once, init_once, NULL, NULL); }

static double now_ms(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static void split(const char *address, char *host, int hsize, char *port, int psize)
{
    const char *colon = strrchr(address, ':');
    if (colon && colon != address) {
        snprintf(host, (size_t)hsize, "%.*s", (int)(colon - address), address);
        snprintf(port, (size_t)psize, "%s", colon + 1);
    } else {
        snprintf(host, (size_t)hsize, "%s", address);
        snprintf(port, (size_t)psize, "27777");
    }
}

/* The relay's IPv4 address; the project's names fall back to their known address when DNS fails. */
static int resolve(int i, const char *address, struct sockaddr_in *out)
{
    char host[128], port[16];
    struct addrinfo hints, *res = NULL;
    split(address, host, sizeof(host), port, sizeof(port));
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &res) || !res) {
        char known[128], kport[16];
        res = NULL;
        split(k_relays(i).address, known, sizeof(known), kport, sizeof(kport));
        if (_stricmp(host, known) || !k_relays(i).fallback_ip[0] ||   /* build 91: "" while not known yet */
            getaddrinfo(k_relays(i).fallback_ip, port, &hints, &res) || !res) return -1;
    }
    memcpy(out, res->ai_addr, sizeof(*out));
    freeaddrinfo(res);
    return 0;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* A round: PROBES STATUS requests to each relay, PROBE_GAP_MS apart; the best round trip counts. */
static void run_round(void)
{
    struct sockaddr_in addr[NETSERVER_COUNT];
    int have[NETSERVER_COUNT], answers[NETSERVER_COUNT], best[NETSERVER_COUNT], i, k;
    double sent[NETSERVER_COUNT][PROBES], last_send = 0;
    uint32_t base;
    char address[NETSERVER_COUNT][128];
    SOCKET s;
    u_long nonblocking = 1;
    uint8_t print[8];
    netplay_build_fingerprint(print);
    EnterCriticalSection(&g_cs);
    for (i = 0; i < NETSERVER_COUNT; i++) memcpy(address[i], P.s[i].address, sizeof(address[i]));
    LeaveCriticalSection(&g_cs);
    for (i = 0; i < NETSERVER_COUNT; i++) {
        have[i] = resolve(i, address[i], &addr[i]) == 0;
        answers[i] = 0;
        best[i] = -1;
    }
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return;
    ioctlsocket(s, FIONBIO, &nonblocking);
    base = (uint32_t)GetTickCount() * 2654435761u ^ (uint32_t)GetCurrentProcessId() << 8;
    base &= ~0xFFu;
    for (k = 0; k <= PROBES; k++) {
        double until;
        if (k < PROBES) {
            for (i = 0; i < NETSERVER_COUNT; i++) {
                uint8_t b[STATUS_LEN];
                uint32_t nonce = base | (uint32_t)(i << 4 | k);
                if (!have[i]) continue;
                memset(b, 0, sizeof(b));
                b[0] = 'D'; b[1] = 'J'; b[2] = 'N'; b[3] = 'P'; b[4] = 1; b[5] = STATUS_TYPE;
                b[6] = (uint8_t)nonce; b[7] = (uint8_t)(nonce >> 8); b[8] = (uint8_t)(nonce >> 16); b[9] = (uint8_t)(nonce >> 24);
                memcpy(b + 10, print, 8);   /* waiting and idle count this build's players */
                sent[i][k] = last_send = now_ms();
                sendto(s, (const char *)b, STATUS_LEN, 0, (const struct sockaddr *)&addr[i], sizeof(addr[i]));
            }
            until = last_send + PROBE_GAP_MS;
        } else {
            until = last_send + LATE_MS;
        }
        while (now_ms() < until) {
            fd_set rd;
            struct timeval tv = { 0, 20000 };
            uint8_t b[256];
            int n, done = 1;
            FD_ZERO(&rd);
            FD_SET(s, &rd);
            n = select(0, &rd, NULL, NULL, &tv) > 0 ? recv(s, (char *)b, sizeof(b), 0) : 0;
            if (n >= STATUS_LEN && !memcmp(b, "DJNP", 4) && b[4] == 1 && b[5] == STATUS_TYPE) {
                uint32_t nonce = rd32(b + 6);
                int who = (int)((nonce >> 4) & 0xF), probe = (int)(nonce & 0xF);
                if ((nonce & ~0xFFu) == base && who < NETSERVER_COUNT && probe < PROBES && probe <= k) {
                    int rtt = (int)(now_ms() - sent[who][probe] + 0.5);
                    answers[who]++;
                    if (best[who] < 0 || rtt < best[who]) best[who] = rtt;
                    EnterCriticalSection(&g_cs);
                    P.s[who].state = NETSERVER_UP;
                    P.s[who].ping_ms = best[who];
                    P.s[who].version = b[10];
                    P.s[who].flags = b[11];
                    P.s[who].online = rd16(b + 12);
                    P.s[who].rooms = rd16(b + 14);
                    P.s[who].waiting = rd16(b + 16);
                    P.s[who].idle = rd16(b + 18);
                    /* build 83 relays: the last two bytes (zero before) count this fingerprint's players */
                    P.s[who].same = (b[11] & NETSERVER_SAME) ? rd16(b + 24) : -1;
                    LeaveCriticalSection(&g_cs);
                }
            }
            if (k < PROBES) continue;
            for (i = 0; i < NETSERVER_COUNT; i++) if (have[i] && answers[i] < PROBES) done = 0;
            if (done) break;
        }
    }
    closesocket(s);
    EnterCriticalSection(&g_cs);
    for (i = 0; i < NETSERVER_COUNT; i++) {
        if (!answers[i]) { P.s[i].state = NETSERVER_OFFLINE; P.s[i].ping_ms = -1; }
    }
    P.rounds++;
    P.round_done = GetTickCount64();
    if (P.rounds == 1) {
        fprintf(stderr, "[SERVERS]");
        for (i = 0; i < NETSERVER_COUNT; i++)
            fprintf(stderr, "%s %s %s", i ? "," : "", P.s[i].name, P.s[i].state == NETSERVER_UP ? "up" : "no answer");
        fprintf(stderr, "\n");
    }
    for (i = 0; i < NETSERVER_COUNT; i++)
        if (P.s[i].state == NETSERVER_UP && P.rounds == 1)
            fprintf(stderr, "[SERVERS] %s: %d ms, %d online, %d on this build and content, %d rooms, %d waiting (%d/%d answers)\n",
                    P.s[i].name, P.s[i].ping_ms, P.s[i].online, P.s[i].same, P.s[i].rooms, P.s[i].waiting, answers[i], PROBES);
    LeaveCriticalSection(&g_cs);
}

static DWORD WINAPI probe_thread(LPVOID unused)
{
    (void)unused;
    {
        typedef HRESULT (WINAPI *SetThreadDescriptionFn)(HANDLE, PCWSTR);
        SetThreadDescriptionFn name = (SetThreadDescriptionFn)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                                    "SetThreadDescription");
        if (name) name(GetCurrentThread(), L"relay status");
    }
    for (;;) {
        WaitForSingleObject(P.wake, INFINITE);
        P.round_started = GetTickCount64();
        run_round();
        InterlockedExchange(&P.running, 0);
    }
}

void netservers_probe(void)
{
    ULONGLONG now;
    init();
    now = GetTickCount64();
    if (P.running || (P.rounds && now - P.round_done < ROUND_EVERY)) return;
    EnterCriticalSection(&g_cs);
    if (!P.thread) {
        P.thread = CreateThread(NULL, 0, probe_thread, NULL, 0, NULL);
        if (!P.thread) { LeaveCriticalSection(&g_cs); return; }
    }
    if (!InterlockedExchange(&P.running, 1)) {
        P.round_started = now;
        SetEvent(P.wake);
    }
    LeaveCriticalSection(&g_cs);
}

int netservers_probing(void)
{
    int i, up = 0;
    init();
    EnterCriticalSection(&g_cs);
    for (i = 0; i < NETSERVER_COUNT; i++) if (P.s[i].state == NETSERVER_UP) up = 1;
    LeaveCriticalSection(&g_cs);
    return P.running && !up;
}

int netservers_index_of(const char *address)
{
    char host[128], port[16];
    int i;
    if (!address || !*address) return -1;
    init();
    split(address, host, sizeof(host), port, sizeof(port));
    for (i = 0; i < NETSERVER_COUNT; i++) {
        char h[128], p[16];
        split(P.s[i].address, h, sizeof(h), p, sizeof(p));
        if (!_stricmp(h, host) && !strcmp(p, port)) return i;
    }
    return -1;
}

int netservers_get(NetServerInfo *out, int max)
{
    int i, n = max < NETSERVER_COUNT ? max : NETSERVER_COUNT, connected = -1, rtt = -1, online = -1, rooms = 0;
    char address[128];
    init();
    /* the relay this game is in the lobby of answers PING even when it is older than STATUS: its
     * ping and count stand in for the STATUS answer it does not give */
    if (netplay_lobby_address(address, sizeof(address))) {
        connected = netservers_index_of(address);
        rtt = netplay_rtt();
        netplay_lobby_players(NULL, 0, &online, &rooms);
    }
    EnterCriticalSection(&g_cs);
    for (i = 0; i < n; i++) out[i] = P.s[i];
    LeaveCriticalSection(&g_cs);
    if (connected >= 0 && connected < n && out[connected].state != NETSERVER_UP && rtt >= 0) {
        out[connected].state = NETSERVER_UP;
        out[connected].ping_ms = rtt;
        out[connected].online = online < 0 ? 0 : online;
        out[connected].rooms = rooms;
    }
    return n;
}

void netservers_set_choice(enum NetServerChoice c)
{
    const char *text = netservers_choice_text(c);
    init();
    if (!text) return;
    recomp_settings_set_text("online", "server_debug", text);
    pc_settings_save();
    EnterCriticalSection(&g_cs);
    P.override = netservers_choice_relay(c);
    if (c == NETCHOICE_AUTO || c == NETCHOICE_ALL) P.pick = -1;    /* decided again, by the pings at hand */
    LeaveCriticalSection(&g_cs);
    fprintf(stderr, "[SERVERS] choice: %s\n", text);
}

void netservers_use(int index)
{
    init();
    if (index < 0 || index >= NETSERVER_COUNT) return;
    EnterCriticalSection(&g_cs);
    P.override = index;
    LeaveCriticalSection(&g_cs);
    fprintf(stderr, "[SERVERS] this session: %s\n", k_relays(index).name);
}

/* Automatic: the answering relay with the lowest ping; none answering: Europe. */
static int best_relay(void)
{
    int i, best = -1;
    for (i = 0; i < NETSERVER_COUNT; i++)
        if (P.s[i].state == NETSERVER_UP && P.s[i].ping_ms >= 0 && (best < 0 || P.s[i].ping_ms < P.s[best].ping_ms))
            best = i;
    return best < 0 ? NETSERVER_EU : best;
}

int netservers_current(void)
{
    enum NetServerChoice c = netservers_choice();
    int r, kick = 0;
    init();
    EnterCriticalSection(&g_cs);
    if (P.override >= 0) r = P.override;
    else if (netservers_choice_relay(c) >= 0) r = netservers_choice_relay(c);
    else if (c == NETCHOICE_CUSTOM) r = -2;
    else {
        if (P.pick < 0) {
            int all = 1, i;
            for (i = 0; i < NETSERVER_COUNT; i++) if (P.s[i].state != NETSERVER_UP) all = 0;
            ULONGLONG now = GetTickCount64();
            if (!P.first_ask) P.first_ask = now;
            /* decided with what is known after DECIDE_MS, even if no round could run at all */
            if (all || P.rounds > 0 || now - P.first_ask > DECIDE_MS + 1000 ||
                (P.running && now - P.round_started > DECIDE_MS)) {
                P.pick = best_relay();
                fprintf(stderr, "[SERVERS] automatic: %s (", P.s[P.pick].name);
                for (i = 0; i < NETSERVER_COUNT; i++)
                    fprintf(stderr, "%s%s %d ms", i ? ", " : "", P.s[i].short_name, P.s[i].ping_ms);
                fprintf(stderr, ")\n");
            } else if (!P.running) {
                kick = 1;
            }
        }
        r = P.pick;
    }
    LeaveCriticalSection(&g_cs);
    if (kick) netservers_probe();
    return r;
}

int netservers_reconsider(void)
{
    int cur = netservers_current();
    enum NetServerChoice c = netservers_choice();
    if (cur < 0 || (c != NETCHOICE_AUTO && c != NETCHOICE_ALL)) return cur;
    EnterCriticalSection(&g_cs);
    if (P.override < 0 && P.pick >= 0) {
        int other = -1, i;
        for (i = 0; i < NETSERVER_COUNT; i++)     /* the answering relay with the lowest ping besides the pick */
            if (i != P.pick && P.s[i].state == NETSERVER_UP && (other < 0 || P.s[i].ping_ms < P.s[other].ping_ms)) other = i;
        const NetServerInfo *a = &P.s[P.pick], *b = &P.s[other < 0 ? P.pick : other];
        int better = other >= 0 && (a->state == NETSERVER_OFFLINE ||
                     (a->state == NETSERVER_UP && b->ping_ms + 40 <= a->ping_ms && b->ping_ms * 10 <= a->ping_ms * 7));
        if (better) {
            fprintf(stderr, "[SERVERS] automatic: %s now (%d ms) instead of %s (%s)\n", b->name, b->ping_ms, a->name,
                    a->state == NETSERVER_OFFLINE ? "no answer" : "slower");
            P.pick = other;
        }
        cur = P.pick;
    }
    LeaveCriticalSection(&g_cs);
    return cur;
}

const char *netservers_address(char *buf, int size)
{
    int r = netservers_current();
    if (r == -1) return NULL;
    if (r == -2) return setting_server(buf, size);
    EnterCriticalSection(&g_cs);
    snprintf(buf, (size_t)size, "%s", P.s[r].address);
    LeaveCriticalSection(&g_cs);
    return buf;
}

const char *netservers_address_wait(char *buf, int size, int wait_ms)
{
    const char *a;
    ULONGLONG until = GetTickCount64() + (ULONGLONG)wait_ms;
    netservers_probe();
    while (!(a = netservers_address(buf, size)) && GetTickCount64() < until) Sleep(50);
    return a ? a : (snprintf(buf, (size_t)size, "%s", k_relays(NETSERVER_EU).address), buf);
}

int netservers_reconnect(const char *name)
{
    char want[128], have[128];
    const char *a = netservers_address(want, sizeof(want));
    if (!a) return -1;
    if (netplay_in_lobby() && netplay_lobby_address(have, sizeof(have)) && !_stricmp(have, a)) return 0;
    fprintf(stderr, "[SERVERS] connecting the hub to %s\n", a);
    netplay_lobby(a, name && *name ? name : "Player");     /* ends the old session first (BYE) */
    return 1;
}

/* ---- Every relay's rooms (build 31; build 38: three), and moving between relays ---------------- */

#define ROOMLIST_TYPE   0x25
#define ROOMLIST_EVERY  3000    /* ms between two asks of one relay */
#define ROOMLIST_WAIT   1500    /* how long an ask waits for its answer */
#define ROOMLIST_FRESH  10000   /* an older answer is not used */
#define QUICK_LISTED_S  8       /* build 91: how long a quick room waits for its own relay to list it with its pings */
#define LISTED_MAX      30
#define MOVE_WAIT       15000   /* a join or quick match on another relay gives up after this */

static struct {
    HANDLE thread, wake;
    volatile LONG busy;
    int want[NETSERVER_COUNT];
    ULONGLONG asked[NETSERVER_COUNT], got[NETSERVER_COUNT];
    int answered[NETSERVER_COUNT];      /* answered ROOMLIST at least once (a relay of build 31 or later) */
    int count[NETSERVER_COUNT], total[NETSERVER_COUNT];
    NetRoomInfo rooms[NETSERVER_COUNT][LISTED_MAX];
    int age[NETSERVER_COUNT][LISTED_MAX];
    int pings[NETSERVER_COUNT][LISTED_MAX][NETSERVER_COUNT];   /* build 90: a quick room's player's pings, 0 none */
    int logged[NETSERVER_COUNT];        /* the room count last logged plus one, 0 none yet */
    unsigned version;
} R;

/* F0: every relay's rings (RINGS, rings_round below) */
#define RINGS_FLOOR     3000    /* ms between two asks of one relay (the relay answers 2 a second per IP) */
#define RINGS_WAIT      1500    /* how long an ask waits for its answer */
#define RINGS_WAIT_SILENT 400   /* ... for relays that never answered RINGS (no F0) */
#define RINGS_FRESH     12000   /* an older answer is not shown */
#define RINGS_KNOWN     64      /* ring rooms remembered this session (the report's `ring`, the hill screen's banner) */

#define RG_CUSTOM       NETSERVER_COUNT /* the slot of a hub on a custom address (tests, a private relay) */
#define RG_SLOTS        (NETSERVER_COUNT + 1)

static struct {
    int want[RG_SLOTS];
    ULONGLONG asked[RG_SLOTS], got[RG_SLOTS];
    int answered[RG_SLOTS];             /* answered RINGS at least once (a relay with F0) */
    int count[RG_SLOTS];
    NetRingCard cards[RG_SLOTS][RINGS_MAX];
    int logged[RG_SLOTS], logged_silent[RG_SLOTS];
    char custom[128];                   /* RG_CUSTOM's address: the hub's, when it is not one of the table's */
    int rounds;                         /* finished rounds, every relay's answer in or timed out */
    unsigned version;
    struct { int server; NetRingCard card; } known[RINGS_KNOWN];
    int known_n;
} RG;

static void rings_round(void);

static struct {         /* a join or quick match waiting for the hub to reach its relay */
    int kind;           /* 0 none, 1 join, 2 quick, 3 ranked (a MOVE's reservation in code), 4 watch (spectate) */
    int server, mode, max;
    char code[7];
    ULONGLONG since;
} A;

/* One ask of every wanted relay: a ROOMLIST padded to a full datagram (the relay answers with no more
 * bytes than it was sent) for this build's public rooms, King of the Hill included. */
static void list_round(void)
{
    struct sockaddr_in addr[NETSERVER_COUNT];
    int want[NETSERVER_COUNT], i, open = 0;
    uint32_t nonce[NETSERVER_COUNT];
    char address[NETSERVER_COUNT][128];
    uint8_t print[8], req[1200];
    double until;
    SOCKET s;
    u_long nonblocking = 1;
    netplay_build_fingerprint(print);
    EnterCriticalSection(&g_cs);
    for (i = 0; i < NETSERVER_COUNT; i++) {
        memcpy(address[i], P.s[i].address, sizeof(address[i]));
        want[i] = R.want[i];
        R.want[i] = 0;
    }
    LeaveCriticalSection(&g_cs);
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return;
    ioctlsocket(s, FIONBIO, &nonblocking);
    for (i = 0; i < NETSERVER_COUNT; i++) {
        if (!want[i] || resolve(i, address[i], &addr[i])) { want[i] = 0; continue; }
        nonce[i] = ((uint32_t)GetTickCount() * 2654435761u) ^ ((uint32_t)GetCurrentProcessId() << 4) ^ (uint32_t)(i + 1);
        memset(req, 0, sizeof(req));
        req[0] = 'D'; req[1] = 'J'; req[2] = 'N'; req[3] = 'P'; req[4] = 1; req[5] = ROOMLIST_TYPE;
        req[6] = (uint8_t)nonce[i]; req[7] = (uint8_t)(nonce[i] >> 8); req[8] = (uint8_t)(nonce[i] >> 16);
        req[9] = (uint8_t)(nonce[i] >> 24);
        memcpy(req + 10, print, 8);     /* rooms of this build only: the others cannot be joined */
        req[18] = 0xFF;                 /* every match type */
        req[19] = 1;                    /* caps: King of the Hill rooms too */
        req[21] = 1;                    /* caps bit 16 (room rules): build 82 relays add each room's rules bits */
        req[22] = 4 | 16;               /* caps bit 26 (build 90, relay ping_sum.go): then each room's player's pings;
                                         * bit 28 (build 91): to the relays beyond the first three too */
        if (rts_access()) req[22] |= 1; /* caps bit 24 (build 91): Run the Streets rooms too, for a player who has the
                                         * mode; the relay it is joined on checks the access itself */
        sendto(s, (const char *)req, (int)sizeof(req), 0, (const struct sockaddr *)&addr[i], sizeof(addr[i]));
        open++;
    }
    until = now_ms() + ROOMLIST_WAIT;
    while (open > 0 && now_ms() < until) {
        fd_set rd;
        struct timeval tv = { 0, 20000 };
        uint8_t b[1500];
        int n, who = -1, count, at, k, bad = 0;
        NetRoomInfo list[LISTED_MAX];
        int ages[LISTED_MAX];
        int pings[LISTED_MAX][NETSERVER_COUNT];
        FD_ZERO(&rd);
        FD_SET(s, &rd);
        n = select(0, &rd, NULL, NULL, &tv) > 0 ? recv(s, (char *)b, sizeof(b), 0) : 0;
        if (n < 13 || memcmp(b, "DJNP", 4) || b[4] != 1 || b[5] != ROOMLIST_TYPE) continue;
        for (i = 0; i < NETSERVER_COUNT; i++) if (want[i] && rd32(b + 6) == nonce[i]) who = i;
        if (who < 0) continue;
        count = b[12];
        if (count > LISTED_MAX) continue;
        for (k = 0, at = 13; k < count && !bad; k++) {
            int len;
            if (at + 7 > n) { bad = 1; break; }
            memcpy(list[k].code, b + at, 6);
            list[k].code[6] = 0;
            len = b[at + 6];
            at += 7;
            if (at + len + 6 > n) { bad = 1; break; }
            snprintf(list[k].name, sizeof(list[k].name), "%.*s", len > 24 ? 24 : len, (const char *)(b + at));
            at += len;
            list[k].mode = b[at];
            list[k].players = b[at + 1];
            list[k].max = b[at + 2];
            list[k].phase = b[at + 3];
            ages[k] = rd16(b + at + 4);
            list[k].rules = -1;
            at += 6;
        }
        if (bad) continue;
        if (at + count <= n)            /* build 82 relays: each room's FIGHTER RULES bits (the STANDARD tag) */
            for (k = 0; k < count; k++) list[k].rules = b[at + k] & NETPLAY_RULES_MASK;
        memset(pings, 0, sizeof(pings));
        if (at + count + 2 * NETSERVER_LEGACY * count <= n) { /* build 90 relays with the ping-sum switch on */
            int p = at + count, j;
            for (k = 0; k < count; k++)
                for (j = 0; j < NETSERVER_LEGACY; j++, p += 2) pings[k][j] = rd16(b + p);
            /* build 91 relays: the pings to the relays beyond the first three (netservers_get_more_pings) */
            if (netservers_get_more_pings(b + p, n - p, count, &pings[0][0]) < 0)
                fprintf(stderr, "[SERVERS] %s: a short ROOMLIST pings trailer\n", P.s[who].name);
        }
        want[who] = 0;
        open--;
        EnterCriticalSection(&g_cs);
        memcpy(R.rooms[who], list, sizeof(list[0]) * (size_t)count);
        memcpy(R.age[who], ages, sizeof(ages[0]) * (size_t)count);
        memcpy(R.pings[who], pings, sizeof(pings[0]) * (size_t)count);
        R.count[who] = count;
        R.total[who] = rd16(b + 10);
        R.got[who] = GetTickCount64();
        R.answered[who] = 1;
        R.version++;
        if (R.logged[who] != count + 1) {
            R.logged[who] = count + 1;
            fprintf(stderr, "[SERVERS] rooms: %s lists %d room%s", P.s[who].name, count, count == 1 ? "" : "s");
            for (k = 0; k < count && k < 4; k++)
                fprintf(stderr, "%s %s (mode %d, %d/%d, phase %d, %d s)", k ? "," : ":", list[k].code, list[k].mode,
                        list[k].players, list[k].max, list[k].phase, ages[k]);
            fprintf(stderr, "\n");
        }
        LeaveCriticalSection(&g_cs);
    }
    closesocket(s);
}

/* ---- Every relay's players (build 47, PLAYERLIST 0x33: WHO without a session) ------------------ */

#define PLAYERLIST_TYPE  0x33
#define PLAYERLIST_EVERY 5000   /* ms between two asks of one relay (the relay answers 2 a second per IP) */
#define PLAYERLIST_FRESH 15000  /* an older answer is not used */
#define PLAYERS_MAX      40     /* the relay's MaxWho */

static struct {
    int want[NETSERVER_COUNT];
    ULONGLONG asked[NETSERVER_COUNT], got[NETSERVER_COUNT];
    int count[NETSERVER_COUNT], total[NETSERVER_COUNT], online[NETSERVER_COUNT];
    NetPlayer list[NETSERVER_COUNT][PLAYERS_MAX];
    int logged[NETSERVER_COUNT];        /* the player count last logged plus one, 0 none yet */
} PL;

/* One ask of every wanted relay: a PLAYERLIST padded to a full datagram (the relay answers with no
 * more bytes than it was sent) for this build's players, names and states only. */
static void players_round(void)
{
    struct sockaddr_in addr[NETSERVER_COUNT];
    int want[NETSERVER_COUNT], i, open = 0;
    uint32_t nonce[NETSERVER_COUNT];
    char address[NETSERVER_COUNT][128];
    uint8_t print[8], req[1200];
    double until;
    SOCKET s;
    u_long nonblocking = 1;
    netplay_build_fingerprint(print);
    EnterCriticalSection(&g_cs);
    for (i = 0; i < NETSERVER_COUNT; i++) {
        memcpy(address[i], P.s[i].address, sizeof(address[i]));
        want[i] = PL.want[i];
        PL.want[i] = 0;
        if (want[i]) open++;
    }
    LeaveCriticalSection(&g_cs);
    if (!open) return;
    open = 0;
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return;
    ioctlsocket(s, FIONBIO, &nonblocking);
    for (i = 0; i < NETSERVER_COUNT; i++) {
        if (!want[i] || resolve(i, address[i], &addr[i])) { want[i] = 0; continue; }
        nonce[i] = ((uint32_t)GetTickCount() * 2246822519u) ^ ((uint32_t)GetCurrentProcessId() << 5) ^ (uint32_t)(i + 7);
        memset(req, 0, sizeof(req));
        req[0] = 'D'; req[1] = 'J'; req[2] = 'N'; req[3] = 'P'; req[4] = 1; req[5] = PLAYERLIST_TYPE;
        req[6] = (uint8_t)nonce[i]; req[7] = (uint8_t)(nonce[i] >> 8); req[8] = (uint8_t)(nonce[i] >> 16);
        req[9] = (uint8_t)(nonce[i] >> 24);
        memcpy(req + 10, print, 8);     /* this build's players, as WHO names them */
        /* build 96: the flags word after the build (not CAPS bits; an older relay took zeros only there):
         * the listed players' rooms, and Run the Streets rooms named too for a player who has the mode */
        req[18] = (uint8_t)(NETPLAY_PLAYERLIST_ROOMS | (rts_access() ? NETPLAY_PLAYERLIST_RTS : 0u));
        sendto(s, (const char *)req, (int)sizeof(req), 0, (const struct sockaddr *)&addr[i], sizeof(addr[i]));
        open++;
    }
    until = now_ms() + ROOMLIST_WAIT;
    while (open > 0 && now_ms() < until) {
        fd_set rd;
        struct timeval tv = { 0, 20000 };
        uint8_t b[1500];
        int n, who = -1, count, at, k, bad = 0;
        NetPlayer list[PLAYERS_MAX];
        FD_ZERO(&rd);
        FD_SET(s, &rd);
        n = select(0, &rd, NULL, NULL, &tv) > 0 ? recv(s, (char *)b, sizeof(b), 0) : 0;
        if (n < 15 || memcmp(b, "DJNP", 4) || b[4] != 1 || b[5] != PLAYERLIST_TYPE) continue;
        for (i = 0; i < NETSERVER_COUNT; i++) if (want[i] && rd32(b + 6) == nonce[i]) who = i;
        if (who < 0) continue;
        count = b[14];
        if (count > PLAYERS_MAX) continue;
        for (k = 0, at = 15; k < count && !bad; k++) {
            int len;
            if (at + 1 > n) { bad = 1; break; }
            len = b[at++];
            if (at + len + 1 > n) { bad = 1; break; }
            memset(&list[k], 0, sizeof(list[k]));
            snprintf(list[k].name, sizeof(list[k].name), "%.*s", len > 24 ? 24 : len, (const char *)(b + at));
            at += len;
            list[k].state = b[at] <= 2 ? b[at] : 0;
            at++;
        }
        if (bad) continue;
        if (at < n && netplay_who_rooms(b + at, n - at, list, count) < 0)     /* build 96 relays: their rooms */
            fprintf(stderr, "[SERVERS] %s: a short PLAYERLIST rooms trailer\n", P.s[who].name);
        want[who] = 0;
        open--;
        EnterCriticalSection(&g_cs);
        memcpy(PL.list[who], list, sizeof(list[0]) * (size_t)count);
        PL.count[who] = count;
        PL.online[who] = rd16(b + 10);
        PL.total[who] = rd16(b + 12);
        PL.got[who] = GetTickCount64();
        if (PL.logged[who] != count + 1) {
            PL.logged[who] = count + 1;
            fprintf(stderr, "[SERVERS] players: %s lists %d of %d (%d connected)\n", P.s[who].name, count,
                    PL.total[who], PL.online[who]);
        }
        LeaveCriticalSection(&g_cs);
    }
    closesocket(s);
}

static DWORD WINAPI list_thread(LPVOID unused)
{
    (void)unused;
    {
        typedef HRESULT (WINAPI *SetThreadDescriptionFn)(HANDLE, PCWSTR);
        SetThreadDescriptionFn name = (SetThreadDescriptionFn)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                                    "SetThreadDescription");
        if (name) name(GetCurrentThread(), L"relay rooms");
    }
    for (;;) {
        WaitForSingleObject(R.wake, INFINITE);
        list_round();
        players_round();
        rings_round();
        InterlockedExchange(&R.busy, 0);
    }
}

/* F0: the list thread runs RINGS too; woken whether a round is under way or not (the event stays set: one more pass) */
static void wake_list_thread(void)
{
    EnterCriticalSection(&g_cs);
    if (!R.thread) {
        R.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
        R.thread = R.wake ? CreateThread(NULL, 0, list_thread, NULL, 0, NULL) : NULL;
    }
    if (R.thread) {
        InterlockedExchange(&R.busy, 1);
        SetEvent(R.wake);
    }
    LeaveCriticalSection(&g_cs);
}

int netservers_connected(void)
{
    char address[128];
    int i;
    if (!netplay_lobby_address(address, sizeof(address))) return -1;
    i = netservers_index_of(address);
    return i >= 0 ? i : -2;
}

int netservers_best(void)
{
    NetServerInfo s[NETSERVER_COUNT];
    int i, best = -1, n = netservers_get(s, NETSERVER_COUNT);
    for (i = 0; i < n; i++)
        if (s[i].state == NETSERVER_UP && s[i].ping_ms >= 0 && (best < 0 || s[i].ping_ms < s[best].ping_ms)) best = i;
    return best;
}

int netservers_home(void)
{
    enum NetServerChoice c = netservers_choice();
    int best;
    if (c == NETCHOICE_CUSTOM) return -1;
    if (netservers_choice_relay(c) >= 0) return netservers_choice_relay(c);
    best = netservers_best();
    return best >= 0 ? best : netservers_current();
}

unsigned netservers_up(void)
{
    NetServerInfo s[NETSERVER_COUNT];
    int i, n = netservers_get(s, NETSERVER_COUNT);
    unsigned up = 0;
    for (i = 0; i < n; i++) if (s[i].state == NETSERVER_UP) up |= 1u << i;
    return up;
}

/* Asks the relays in `mask` for their rooms (ROOMLIST) when their last ask is 3 s old. */
static void fetch_rooms(unsigned mask)
{
    ULONGLONG now;
    int i, any = 0;
    init();
    if (R.busy) return;
    now = GetTickCount64();
    EnterCriticalSection(&g_cs);
    for (i = 0; i < NETSERVER_COUNT; i++)
        if ((mask >> i & 1u) && P.s[i].state != NETSERVER_OFFLINE && now - R.asked[i] >= ROOMLIST_EVERY) {
            R.want[i] = 1;
            R.asked[i] = now;
            any = 1;
        }
    if (any && !R.thread) {
        R.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
        R.thread = R.wake ? CreateThread(NULL, 0, list_thread, NULL, 0, NULL) : NULL;
    }
    if (any && R.thread) {
        InterlockedExchange(&R.busy, 1);
        SetEvent(R.wake);
    }
    LeaveCriticalSection(&g_cs);
}

/* build 91: every relay's rooms whatever the choice (the relay is where a match runs, not whom one meets) */
static int global_lists(void) { return netservers_choice() != NETCHOICE_CUSTOM; }

void netservers_fetch_rooms(void)
{
    if (global_lists()) fetch_rooms((1u << NETSERVER_COUNT) - 1);
}

int netservers_rooms(NetServerRoom *out, int max, unsigned *version)
{
    NetServerInfo sv[NETSERVER_COUNT];
    NetRoomInfo own[32];
    int ping[NETSERVER_COUNT], order[NETSERVER_COUNT], cur = netservers_connected(), own_n = 0, n = 0, i, j, k;
    int all = global_lists(), rtt = netplay_rtt();
    unsigned v = 0;
    ULONGLONG now = GetTickCount64();
    init();
    netservers_get(sv, NETSERVER_COUNT);
    if (cur != -1) own_n = netplay_lobby_rooms(own, 32, &v);
    for (i = 0; i < NETSERVER_COUNT; i++) {
        ping[i] = sv[i].state == NETSERVER_UP ? sv[i].ping_ms : -1;
        if (i == cur && rtt >= 0) ping[i] = rtt;
        order[i] = i;
    }
    /* the relays by ping, the unknown last (a stable insertion sort of three) */
    for (i = 1; i < NETSERVER_COUNT; i++)
        for (j = i; j > 0; j--) {
            int a = ping[order[j - 1]], b = ping[order[j]];
            if (!(b >= 0 && (a < 0 || b < a))) break;
            k = order[j]; order[j] = order[j - 1]; order[j - 1] = k;
        }
    EnterCriticalSection(&g_cs);
    if (cur == -2)
        for (k = 0; k < own_n && n < max; k++) {
            out[n].room = own[k]; out[n].server = -2; out[n].ping_ms = rtt; out[n].age_s = -1; n++;
        }
    for (j = 0; j < NETSERVER_COUNT; j++) {
        int r = order[j], fresh = R.got[r] && now - R.got[r] < ROOMLIST_FRESH;
        if (r == cur) {
            for (k = 0; k < own_n && n < max; k++) {
                int m;
                out[n].room = own[k]; out[n].server = r; out[n].ping_ms = ping[r]; out[n].age_s = -1;
                for (m = 0; fresh && m < R.count[r]; m++)   /* its age, when this relay answered ROOMLIST too */
                    if (!strcmp(R.rooms[r][m].code, own[k].code)) out[n].age_s = R.age[r][m] + (int)((now - R.got[r]) / 1000);
                n++;
            }
        } else if (all && fresh) {
            for (k = 0; k < R.count[r] && n < max; k++) {
                out[n].room = R.rooms[r][k]; out[n].server = r; out[n].ping_ms = ping[r];
                out[n].age_s = R.age[r][k] + (int)((now - R.got[r]) / 1000);
                n++;
            }
        }
    }
    if (version) *version = v * 2654435761u + R.version;
    LeaveCriticalSection(&g_cs);
    return n;
}

/* Asks the relays in `mask` for their players (PLAYERLIST) when their last ask is 5 s old. */
static void fetch_players(unsigned mask)
{
    ULONGLONG now;
    int i, any = 0;
    init();
    if (R.busy) return;
    now = GetTickCount64();
    EnterCriticalSection(&g_cs);
    for (i = 0; i < NETSERVER_COUNT; i++)
        if ((mask >> i & 1u) && P.s[i].state != NETSERVER_OFFLINE && now - PL.asked[i] >= PLAYERLIST_EVERY) {
            PL.want[i] = 1;
            PL.asked[i] = now;
            any = 1;
        }
    if (any && !R.thread) {
        R.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
        R.thread = R.wake ? CreateThread(NULL, 0, list_thread, NULL, 0, NULL) : NULL;
    }
    if (any && R.thread) {
        InterlockedExchange(&R.busy, 1);
        SetEvent(R.wake);
    }
    LeaveCriticalSection(&g_cs);
}

void netservers_fetch_players(void)
{
    int cur;
    if (!global_lists()) return;
    cur = netservers_connected();
    fetch_players(((1u << NETSERVER_COUNT) - 1) & ~(cur >= 0 ? 1u << cur : 0u));    /* the hub's own: WHO */
}

void netservers_fetch_players_all(void)
{
    int cur = netservers_connected();
    fetch_players(((1u << NETSERVER_COUNT) - 1) & ~(cur >= 0 ? 1u << cur : 0u));
}

int netservers_players(NetServerPlayer *out, int max, int *online)
{
    NetServerInfo sv[NETSERVER_COUNT];
    NetPlayer own[PLAYERS_MAX];
    int order[NETSERVER_COUNT], ping[NETSERVER_COUNT], cur = netservers_connected(), own_online = -1, own_n, n = 0;
    int heard = 0, total = 0, i, j, k, rtt = netplay_rtt();
    ULONGLONG now = GetTickCount64();
    init();
    netservers_get(sv, NETSERVER_COUNT);
    own_n = netplay_lobby_players(own, PLAYERS_MAX, &own_online, NULL);
    for (i = 0; i < NETSERVER_COUNT; i++) {
        ping[i] = sv[i].state == NETSERVER_UP ? sv[i].ping_ms : -1;
        if (i == cur && rtt >= 0) ping[i] = rtt;
        order[i] = i;
    }
    /* the hub's relay first, then the others by ping (as the rooms) */
    for (i = 1; i < NETSERVER_COUNT; i++)
        for (j = i; j > 0; j--) {
            int a = order[j - 1], b = order[j];
            int before = b == cur || (a != cur && ping[b] >= 0 && (ping[a] < 0 || ping[b] < ping[a]));
            if (!before) break;
            order[j] = a; order[j - 1] = b;
        }
    EnterCriticalSection(&g_cs);
    for (j = 0; j < NETSERVER_COUNT; j++) {
        int r = order[j];
        if (r == cur) {
            if (own_n < 0) continue;
            heard = 1;
            total += own_online > 0 ? own_online : 0;
            for (k = 0; k < own_n && n < max; k++) { out[n].player = own[k]; out[n].server = r; n++; }
        } else if (PL.got[r] && now - PL.got[r] < PLAYERLIST_FRESH) {
            heard = 1;
            total += PL.online[r];
            for (k = 0; k < PL.count[r] && n < max; k++) { out[n].player = PL.list[r][k]; out[n].server = r; n++; }
        }
    }
    LeaveCriticalSection(&g_cs);
    if (online) *online = heard ? total : -1;
    return heard ? n : -1;
}

/* Sends the pending join or quick match when the hub is in its relay's lobby. */
int netservers_tick(int *mode)
{
    NetRoom r;
    int kind, cur;
    if (!A.kind) return 0;
    init();
    cur = netservers_connected();
    EnterCriticalSection(&g_cs);
    kind = A.kind;
    if (kind && GetTickCount64() - A.since > MOVE_WAIT) {
        fprintf(stderr, "[SERVERS] move: the move to %s did not reach its lobby; the %s is dropped\n",
                P.s[A.server].name, kind == 1 ? "join" : kind == 4 ? "watch" : kind == 3 ? "ranked set" : "quick match");
        A.kind = 0;
        LeaveCriticalSection(&g_cs);
        return -1;
    }
    LeaveCriticalSection(&g_cs);
    if (!kind || netplay_state() != NETPLAY_LOBBY || !netplay_in_lobby() || cur != A.server) return 0;
    netplay_lobby_room(&r);
    /* build 91: every move waits for the relay's CAPS answer (it comes within a round trip, or counts as none after
     * the retries): a QUICK before it carries no pings, and the room it opens is listed without them */
    if (r.in_room || netplay_lobby_caps() < 0) return 0;
    EnterCriticalSection(&g_cs);
    kind = A.kind;
    A.kind = 0;
    LeaveCriticalSection(&g_cs);
    if (kind == 1) {
        fprintf(stderr, "[SERVERS] move: joining room %s on %s\n", A.code, P.s[A.server].name);
        netplay_lobby_join(A.code);
    } else if (kind == 2) {
        fprintf(stderr, "[SERVERS] move: quick match (mode %d) on %s\n", A.mode, P.s[A.server].name);
        netplay_lobby_quick(A.mode, A.max);
    } else if (kind == 3) {
        fprintf(stderr, "[SERVERS] move: ranked set %s on %s\n", A.code, P.s[A.server].name);
        netplay_lobby_ranked_again(A.code);
    } else if (kind == 4 && !(netplay_lobby_caps() & NETPLAY_CAP_SPECTATE)) {
        fprintf(stderr, "[SERVERS] move: %s does not take spectators; the watch is dropped\n", P.s[A.server].name);
        return -4;
    } else if (kind == 4) {
        fprintf(stderr, "[SERVERS] move: watching room %s on %s\n", A.code, P.s[A.server].name);
        netplay_lobby_spectate(A.code);
    }
    if (mode) *mode = A.mode;
    return kind;
}

int netservers_moving(void) { return A.kind != 0; }

static void move_to(int kind, int server, const char *code, int mode, int max, const char *name)
{
    init();
    EnterCriticalSection(&g_cs);
    A.kind = kind;
    A.server = server;
    A.mode = mode;
    A.max = max;
    snprintf(A.code, sizeof(A.code), "%s", code ? code : "");
    A.since = GetTickCount64();
    LeaveCriticalSection(&g_cs);
    if (netservers_connected() != server) {
        fprintf(stderr, "[SERVERS] move: moving the hub to %s for a %s\n", P.s[server].name,
                kind == 1 ? "join" : kind == 3 ? "ranked set" : kind == 4 ? "watch" : "quick match");
        netservers_use(server);
        netservers_reconnect(name);
    }
    netservers_tick(NULL);      /* at once when the hub is already there */
}

void netservers_join_on(int server, const char *code, int mode, const char *name)
{
    if (server < 0 || server >= NETSERVER_COUNT) { netplay_lobby_join(code); return; }
    move_to(1, server, code, mode, 0, name);
}

void netservers_spectate_on(int server, const char *code, const char *name)
{
    if (server < 0 || server >= NETSERVER_COUNT) { netplay_lobby_spectate(code); return; }
    move_to(4, server, code, 0, 0, name);
}

void netservers_quick_on(int server, int mode, int max, const char *name)
{
    if (server < 0 || server >= NETSERVER_COUNT) { netplay_lobby_quick(mode, max); return; }
    move_to(2, server, NULL, mode, max, name);
}

int netservers_ranked_move(const char *name)
{
    NetQueueExt q;
    int to;
    if (netplay_lobby_queue(NULL, NULL, NULL, 0) != NETQUEUE_MOVE || !netplay_lobby_queue_ext(&q) || !q.code[0]) return 0;
    to = netservers_index_of(q.addr);
    if (to < 0 && netservers_connected() == -2) {
        /* a custom address (tests): one relay plays both sides, so a move never comes; refuse */
        fprintf(stderr, "[SERVERS] move: ranked set %s on %s, not one of the relays\n", q.code, q.addr);
        netplay_lobby_ranked_moved(0);
        return -1;
    }
    if (to < 0) {
        fprintf(stderr, "[SERVERS] move: ranked set %s on an unknown relay %s\n", q.code, q.addr);
        netplay_lobby_ranked_moved(0);
        return -1;
    }
    netplay_lobby_ranked_moved(1);
    move_to(3, to, q.code, 0, 0, name);
    return 1;
}

#define MOVE_FRESH      6000    /* a quick match moves only on a room list this fresh */

int netservers_quick_move(int mode, int search_s, int alone_s, const char *code, int *waiting, int *ping,
                          unsigned *scope_out)
{
    NetServerInfo sv[NETSERVER_COUNT];
    int cur = netservers_connected(), my_age = alone_s, best = -1, best_n = 0, o, k, std_room, sum_on = 0, by_sum = 0;
    int mine[NETSERVER_COUNT] = { 0 };
    int original = netplay_original(mode);
    int listed = 0, caps = netplay_lobby_caps();
    unsigned up = 0, scope, soon;
    enum NetServerChoice c = netservers_choice();
    ULONGLONG now;
    if (waiting) *waiting = 0;
    if (ping) *ping = -1;
    if (scope_out) *scope_out = 0;
    if (c == NETCHOICE_CUSTOM || cur < 0 || NETPLAY_IS_HILL(mode)) return -1;
    netservers_get(sv, NETSERVER_COUNT);
    for (o = 0; o < NETSERVER_COUNT; o++) if (sv[o].state == NETSERVER_UP) up |= 1u << o;
    scope = netservers_scope(c, cur, search_s, up);
    if (scope_out) *scope_out = scope;
    /* the lists of the relays searched now and in 3 s (this room's own for its age), every 3 s */
    soon = netservers_scope(c, cur, search_s + 3, up);
    fetch_rooms(soon | (1u << cur));
    if (scope == (1u << cur)) return -1;
    now = GetTickCount64();
    EnterCriticalSection(&g_cs);
    /* this room's age as its own relay counts it: the clock the other player's view of it uses */
    if (code && R.got[cur] && now - R.got[cur] < ROOMLIST_FRESH)
        for (k = 0; k < R.count[cur]; k++)
            if (!strcmp(R.rooms[cur][k].code, code)) {
                listed = 1;
                my_age = R.age[cur][k] + (int)((now - R.got[cur]) / 1000);
                /* build 90: the pings this room's relay lists for it, the numbers the other player's game reads */
                if (R.rooms[cur][k].players == 1) {
                    memcpy(mine, R.pings[cur][k], sizeof(mine));
                    sum_on = 1;
                }
            }
    /* build 91: a relay that lists pings (CAPS bit 26) has not listed this room yet (its list is asked every 3 s):
     * wait for it, up to QUICK_LISTED_S, instead of moving by the age rule to wherever somebody waits; with the
     * room's pings at hand the pool below picks the relay with the smallest ping sum */
    if (code && *code && !listed && caps > 0 && ((unsigned)caps & (1u << 26)) && alone_s < QUICK_LISTED_S) {
        LeaveCriticalSection(&g_cs);
        return -1;
    }
    /* Run the Streets rooms hold up to four and are joined while they wait: the age rule below (the younger room's
     * player moves to the older room), not the pool of pairs */
    if (sum_on && mode != NETPLAY_MODE_RTS) {   /* build 91: the global pool (netservers_pool): every relay's waiting players, by ping sum */
        static NetQuickSeat seats[QUICK_POOL_MAX];
        int n = 0, me = -1, known = 0, j, mate = -1, to;
        for (j = 0; j < NETSERVER_COUNT; j++) known |= mine[j] > 0;
        for (o = 0; o < NETSERVER_COUNT && known; o++) {
            if (!(scope >> o & 1u) || !R.got[o] || now - R.got[o] >= MOVE_FRESH) continue;
            for (k = 0; k < R.count[o] && n < QUICK_POOL_MAX; k++) {
                const NetRoomInfo *x = &R.rooms[o][k];
                int any = 0;
                if (x->mode != mode || x->phase != 0 || x->players != 1 || x->max < 2) continue;
                if (strncmp(x->name, "Quick: ", 7)) continue;
                std_room = x->rules >= 0 && (x->rules & NETPLAY_RULES_STANDARD) != 0;
                if (std_room != (original != 0)) continue;      /* each fighter choice is its own pool */
                for (j = 0; j < NETSERVER_COUNT; j++) any |= R.pings[o][k][j] > 0;
                if (!any) continue;                             /* a room without pings is not in the pool */
                seats[n].relay = o;
                seats[n].age = R.age[o][k] + (int)((now - R.got[o]) / 1000);
                memcpy(seats[n].pings, R.pings[o][k], sizeof(seats[n].pings));
                snprintf(seats[n].code, sizeof(seats[n].code), "%s", x->code);
                if (o == cur && code && !strcmp(x->code, code)) me = n;
                n++;
            }
        }
        if (me >= 0) {
            /* build 94: only an answer the other game reaches too (netservers_pool_steady); near a limit step, wait */
            to = netservers_pool_steady(seats, n, me, &mate);
            LeaveCriticalSection(&g_cs);
            if (to < 0 || mate < 0 || to == cur) return -1;    /* nobody within the limit yet, not steady, or they come here */
            if (waiting) *waiting = 1;
            if (ping) *ping = sv[to].ping_ms;
            fprintf(stderr, "[SERVERS] quick: pool of %d, searching %d s (room age %d s) on %s; paired with %s on %s, "
                    "playing on %s (%d ms): moving there\n", n, search_s, my_age, sv[cur].name, seats[mate].code,
                    sv[seats[mate].relay].name, sv[to].name, sv[to].ping_ms);
            return to;
        }
    }
    for (o = 0; o < NETSERVER_COUNT; o++) {
        int n = 0;
        if (o == cur || !(scope >> o & 1u)) continue;
        if (R.answered[o]) {
            if (!R.got[o] || now - R.got[o] >= MOVE_FRESH) continue;
            for (k = 0; k < R.count[o]; k++) {
                const NetRoomInfo *x = &R.rooms[o][k];
                int age = R.age[o][k] + (int)((now - R.got[o]) / 1000);
                if (x->mode != mode || x->phase != 0 || x->players < 1 || x->players >= x->max) continue;
                if (strncmp(x->name, "Quick: ", 7)) continue;   /* the relay quick-joins only quick rooms */
                /* build 86 (server relay/original.go): an original-only search meets original-only rooms alone; an
                 * all-fighters one goes to an original-only room whatever its age, since that room's player never
                 * moves to an all-fighters room (so the two never cross) */
                std_room = x->rules >= 0 && (x->rules & NETPLAY_RULES_STANDARD) != 0;
                if (original && !std_room) continue;
                if (!original && std_room) { n += x->players; continue; }
                {   /* build 90: the smaller ping sum when both rooms list their player's pings; else the ages */
                    int sm = sum_on && x->players == 1 ? netservers_sum_move(cur, my_age, mine, o, age, R.pings[o][k]) : -1;
                    if (sm >= 0) {
                        if (sm) { n += x->players; by_sum = 1; }
                    } else if (netservers_should_move(cur, my_age, o, age)) {
                        n += x->players;
                    }
                }
            }
        } else if (sv[o].waiting > 0 && alone_s >= QUICK_LAST_S) {
            n = sv[o].waiting;      /* a relay before build 31: its STATUS, a little later */
        }
        /* the most players waiting; between equals the lower ping */
        if (n > best_n || (n && n == best_n && sv[o].ping_ms >= 0 && sv[o].ping_ms < sv[best].ping_ms)) { best = o; best_n = n; }
    }
    LeaveCriticalSection(&g_cs);
    if (best >= 0) {
        if (waiting) *waiting = best_n;
        if (ping) *ping = sv[best].ping_ms;
        fprintf(stderr, "[SERVERS] quick: searching %d s, alone %d s (room age %d s) on %s; %d waiting on %s (%d ms): "
                "moving there%s\n", search_s, alone_s, my_age, sv[cur].name, best_n, sv[best].name, sv[best].ping_ms,
                by_sum ? " (the smaller ping sum)" : "");
    }
    return best;
}

/* ---- F0: every relay's rings (RINGS 0x46, community/fight-complex-f0-protocol.md section 2) ------- */

/* A host:port's IPv4 address, without the table's fallbacks (RG_CUSTOM). */
static int resolve_plain(const char *address, struct sockaddr_in *out)
{
    char host[128], port[16];
    struct addrinfo hints, *res = NULL;
    split(address, host, sizeof(host), port, sizeof(port));
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &res) || !res) return -1;
    memcpy(out, res->ai_addr, sizeof(*out));
    freeaddrinfo(res);
    return 0;
}

static const char *slot_name(int i) { return i == RG_CUSTOM ? "the hub's relay" : P.s[i].name; }

/* One ask of every wanted relay: a RINGS padded to a full datagram for this build's ring instances. A relay
 * without F0 drops it: no answer within RINGS_WAIT is "no rings there" (the round still counts as done). */
static void rings_round(void)
{
    struct sockaddr_in addr[RG_SLOTS];
    int want[RG_SLOTS], i, open = 0, any = 0;
    uint32_t nonce[RG_SLOTS], caps = netplay_client_caps();
    char address[RG_SLOTS][128];
    uint8_t print[8], req[RINGS_REQ_LEN];
    double until, short_until;
    SOCKET s;
    u_long nonblocking = 1;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < RG_SLOTS; i++) {
        memcpy(address[i], i == RG_CUSTOM ? RG.custom : P.s[i].address, sizeof(address[i]));
        want[i] = RG.want[i];
        RG.want[i] = 0;
        any |= want[i];
    }
    LeaveCriticalSection(&g_cs);
    if (!any) return;
    netplay_build_fingerprint(print);
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return;
    ioctlsocket(s, FIONBIO, &nonblocking);
    for (i = 0; i < RG_SLOTS; i++) {
        int len;
        if (!want[i] || (i == RG_CUSTOM ? resolve_plain(address[i], &addr[i]) : resolve(i, address[i], &addr[i]))) {
            want[i] = 0;
            continue;
        }
        nonce[i] = ((uint32_t)GetTickCount() * 2654435789u) ^ ((uint32_t)GetCurrentProcessId() << 6) ^ (uint32_t)(i + 11);
        len = rings_build_request(req, (int)sizeof(req), nonce[i], print, caps);
        sendto(s, (const char *)req, len, 0, (const struct sockaddr *)&addr[i], sizeof(addr[i]));
        open++;
    }
    until = now_ms() + RINGS_WAIT;
    short_until = now_ms() + RINGS_WAIT_SILENT;
    while (open > 0 && now_ms() < until) {
        fd_set rd;
        struct timeval tv = { 0, 20000 };
        uint8_t b[1500];
        int n, who = -1, count, k, waiting = 0;
        uint32_t got_nonce = 0;
        NetRingCard list[RINGS_MAX];
        /* the relays that never answered RINGS (no F0, after the first two rounds) are asked but not waited for past
         * RINGS_WAIT_SILENT: the list thread runs ROOMLIST too and must not idle on old relays */
        EnterCriticalSection(&g_cs);
        for (i = 0; i < RG_SLOTS; i++) if (want[i] && (RG.answered[i] || RG.rounds < 2)) waiting++;
        LeaveCriticalSection(&g_cs);
        if (!waiting && now_ms() >= short_until) break;
        FD_ZERO(&rd);
        FD_SET(s, &rd);
        n = select(0, &rd, NULL, NULL, &tv) > 0 ? recv(s, (char *)b, sizeof(b), 0) : 0;
        if (n <= 0) continue;
        count = rings_parse_answer(b, n, &got_nonce, list, RINGS_MAX);
        if (count < 0) continue;
        for (i = 0; i < RG_SLOTS; i++) if (want[i] && got_nonce == nonce[i]) who = i;
        if (who < 0) continue;
        want[who] = 0;
        open--;
        EnterCriticalSection(&g_cs);
        memcpy(RG.cards[who], list, sizeof(list[0]) * (size_t)count);
        RG.count[who] = count;
        RG.got[who] = GetTickCount64();
        RG.answered[who] = 1;
        RG.version++;
        for (k = 0; k < count; k++) {   /* every ring room seen this session, for the report and the hill screen */
            int j, at = -1;
            for (j = 0; j < RG.known_n; j++)
                if (RG.known[j].server == (who == RG_CUSTOM ? -2 : who) && !strcmp(RG.known[j].card.code, list[k].code)) at = j;
            if (at < 0) at = RG.known_n < RINGS_KNOWN ? RG.known_n++ : (int)(RG.version % RINGS_KNOWN);
            RG.known[at].server = who == RG_CUSTOM ? -2 : who;
            RG.known[at].card = list[k];
        }
        if (RG.logged[who] != count + 1) {
            RG.logged[who] = count + 1;
            fprintf(stderr, "[SERVERS] rings: %s lists %d ring%s", slot_name(who), count, count == 1 ? "" : "s");
            for (k = 0; k < count && k < 4; k++)
                fprintf(stderr, "%s %s \"%s\" (type %d, ch %d, flags %d, %d in, %d line, %d watching, phase %d, king \"%s\" x%d)",
                        k ? "," : ":", list[k].code, list[k].name, list[k].type, list[k].channel, list[k].flags,
                        list[k].members, list[k].line, list[k].watching, list[k].phase, list[k].king, list[k].king_streak);
            fprintf(stderr, "\n");
        }
        LeaveCriticalSection(&g_cs);
    }
    closesocket(s);
    EnterCriticalSection(&g_cs);
    for (i = 0; i < RG_SLOTS; i++)
        if (want[i] && RG.logged_silent[i] != 1) {
            RG.logged_silent[i] = 1;
            fprintf(stderr, "[SERVERS] rings: %s did not answer RINGS (no rings there)\n", slot_name(i));
        } else if (!want[i]) RG.logged_silent[i] = 0;
    RG.rounds++;
    RG.version++;
    LeaveCriticalSection(&g_cs);
}

void netservers_fetch_rings(int every_ms)
{
    ULONGLONG now;
    int i, any = 0;
    char custom[128];
    /* a hub on a custom address (tests, a private relay): that relay too (asked before g_cs: netplay's own lock) */
    int is_custom = netservers_connected() == -2 && netplay_lobby_address(custom, sizeof(custom));
    init();
    if (every_ms < RINGS_FLOOR) every_ms = RINGS_FLOOR;
    now = GetTickCount64();
    EnterCriticalSection(&g_cs);
    if (is_custom) {
        if (strcmp(RG.custom, custom)) { RG.asked[RG_CUSTOM] = 0; RG.got[RG_CUSTOM] = 0; RG.answered[RG_CUSTOM] = 0; }
        snprintf(RG.custom, sizeof(RG.custom), "%s", custom);
        if (!RG.asked[RG_CUSTOM] || now - RG.asked[RG_CUSTOM] >= (ULONGLONG)every_ms) {
            RG.want[RG_CUSTOM] = 1;
            RG.asked[RG_CUSTOM] = now;
            any = 1;
        }
    } else RG.got[RG_CUSTOM] = 0;
    for (i = 0; i < NETSERVER_COUNT; i++)
        if (P.s[i].state != NETSERVER_OFFLINE && (!RG.asked[i] || now - RG.asked[i] >= (ULONGLONG)every_ms)) {
            RG.want[i] = 1;
            RG.asked[i] = now;
            any = 1;
        }
    LeaveCriticalSection(&g_cs);
    if (any) wake_list_thread();
}

int netservers_rings(NetRing *out, int max, int *rounds)
{
    NetServerInfo sv[NETSERVER_COUNT];
    int order[NETSERVER_COUNT], i, j, n = 0;
    ULONGLONG now = GetTickCount64();
    init();
    netservers_get(sv, NETSERVER_COUNT);
    for (i = 0; i < NETSERVER_COUNT; i++) order[i] = i;
    for (i = 1; i < NETSERVER_COUNT; i++)       /* the relay with the lowest ping first */
        for (j = i; j > 0; j--) {
            int a = sv[order[j - 1]].ping_ms, b = sv[order[j]].ping_ms;
            if ((a < 0 && b >= 0) || (a >= 0 && b >= 0 && b < a)) { int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
        }
    EnterCriticalSection(&g_cs);
    if (rounds) *rounds = RG.rounds;
    if (RG.got[RG_CUSTOM] && now - RG.got[RG_CUSTOM] <= RINGS_FRESH) {
        int k;
        for (k = 0; k < RG.count[RG_CUSTOM] && n < max; k++, n++) {
            out[n].card = RG.cards[RG_CUSTOM][k];
            out[n].server = -2;
            out[n].ping_ms = -1;
        }
    }
    for (j = 0; j < NETSERVER_COUNT; j++) {
        int r = order[j], k;
        if (!RG.got[r] || now - RG.got[r] > RINGS_FRESH) continue;
        for (k = 0; k < RG.count[r] && n < max; k++, n++) {
            out[n].card = RG.cards[r][k];
            out[n].server = r;
            out[n].ping_ms = sv[r].state == NETSERVER_UP ? sv[r].ping_ms : -1;
        }
    }
    LeaveCriticalSection(&g_cs);
    return n;
}

int netservers_ring_of(int server, const char *code, NetRingCard *out)
{
    int j, found = 0;
    init();
    EnterCriticalSection(&g_cs);
    if (!code || !code[0]) {    /* any ring seen this session */
        found = RG.known_n > 0;
        LeaveCriticalSection(&g_cs);
        return found;
    }
    for (j = 0; j < RG.known_n && found < 2; j++)      /* the hub's relay's ring by that code; else any relay's */
        if (!strncmp(RG.known[j].card.code, code, 6) && (RG.known[j].server == server || !found)) {
            if (out) *out = RG.known[j].card;
            found = RG.known[j].server == server ? 2 : 1;
        }
    LeaveCriticalSection(&g_cs);
    return found != 0;
}

#else   /* other systems: no online play yet */

int netservers_reconnect(const char *name) { (void)name; return 0; }
int netservers_connected(void) { return -1; }
int netservers_best(void) { return -1; }
void netservers_fetch_rooms(void) {}
void netservers_fetch_players(void) {}
int netservers_players(NetServerPlayer *out, int max, int *online) { (void)out; (void)max; if (online) *online = -1; return -1; }
int netservers_rooms(NetServerRoom *out, int max, unsigned *version) { (void)out; (void)max; if (version) *version = 0; return 0; }
void netservers_join_on(int server, const char *code, int mode, const char *name) { (void)server; (void)code; (void)mode; (void)name; }
void netservers_quick_on(int server, int mode, int max, const char *name) { (void)server; (void)mode; (void)max; (void)name; }
int netservers_tick(int *mode) { (void)mode; return 0; }
int netservers_ranked_move(const char *name) { (void)name; return 0; }
int netservers_moving(void) { return 0; }
int netservers_quick_move(int mode, int search_s, int alone_s, const char *code, int *waiting, int *ping,
                          unsigned *scope)
{
    (void)mode; (void)search_s; (void)alone_s; (void)code;
    if (waiting) *waiting = 0;
    if (ping) *ping = -1;
    if (scope) *scope = 0;
    return -1;
}
int netservers_home(void) { return -1; }
unsigned netservers_up(void) { return 0; }
void netservers_fetch_rings(int every_ms) { (void)every_ms; }
int netservers_rings(NetRing *out, int max, int *rounds) { (void)out; (void)max; if (rounds) *rounds = 1; return 0; }
int netservers_ring_of(int server, const char *code, NetRingCard *out) { (void)server; (void)code; (void)out; return 0; }

void netservers_probe(void) {}
int netservers_get(NetServerInfo *out, int max) { (void)out; (void)max; return 0; }
int netservers_probing(void) { return 0; }
void netservers_set_choice(enum NetServerChoice c) { (void)c; }
void netservers_use(int index) { (void)index; }
int netservers_current(void) { return NETSERVER_EU; }
int netservers_reconsider(void) { return NETSERVER_EU; }
const char *netservers_address(char *buf, int size) { snprintf(buf, (size_t)size, "%s", k_relays(0).address); return buf; }
const char *netservers_address_wait(char *buf, int size, int wait_ms) { (void)wait_ms; return netservers_address(buf, size); }
int netservers_index_of(const char *address) { (void)address; return -1; }

#endif

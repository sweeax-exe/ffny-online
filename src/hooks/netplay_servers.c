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

static const struct {
    const char *id, *name, *short_name, *address, *fallback_ip, *env;
} k_relays[NETSERVER_COUNT] = {
    { "eu", "Europe", "EU", "play.defjam.online:27777", "49.12.78.32", "RECOMP_NETPLAY_SERVER_EU" },
    { "na", "North America", "NA", "na.defjam.online:27777", "5.161.206.134", "RECOMP_NETPLAY_SERVER_NA" },
};

/* The setting's text: auto, eu, na; the project's two names count as their relay, and the old
 * default (play.defjam.online, written by every build before 30) counts as auto. */
static enum NetServerChoice parse_choice(const char *s)
{
    char low[128];
    size_t i;
    while (*s == ' ') s++;
    for (i = 0; s[i] && i < sizeof(low) - 1; i++) low[i] = (char)tolower((unsigned char)s[i]);
    low[i] = 0;
    while (i && low[i - 1] == ' ') low[--i] = 0;
    if (!low[0] || !strcmp(low, "auto") || !strcmp(low, "automatic") || !strcmp(low, "play.defjam.online") ||
        !strcmp(low, "play.defjam.online:27777"))
        return NETCHOICE_AUTO;
    if (!strcmp(low, "eu") || !strcmp(low, "europe")) return NETCHOICE_EU;
    if (!strcmp(low, "na") || !strcmp(low, "north_america") || !strcmp(low, "us") || !strcmp(low, "na.defjam.online") ||
        !strcmp(low, "na.defjam.online:27777"))
        return NETCHOICE_NA;
    return NETCHOICE_CUSTOM;
}

static const char *setting_server(char *buf, int size)
{
    return recomp_settings_get_text("online", "server", buf, (size_t)size, "auto");
}

enum NetServerChoice netservers_choice(void)
{
    char buf[128];
    return parse_choice(setting_server(buf, sizeof(buf)));
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
    for (i = 0; i < NETSERVER_COUNT; i++) {
        const char *env = getenv(k_relays[i].env);
        P.s[i].id = k_relays[i].id;
        P.s[i].name = k_relays[i].name;
        P.s[i].short_name = k_relays[i].short_name;
        snprintf(P.s[i].address, sizeof(P.s[i].address), "%s", env && *env ? env : k_relays[i].address);
        P.s[i].ping_ms = -1;
        if (env && *env) fprintf(stderr, "[SERVERS] tests: %s is %s\n", k_relays[i].name, P.s[i].address);
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
        split(k_relays[i].address, known, sizeof(known), kport, sizeof(kport));
        if (_stricmp(host, known) || getaddrinfo(k_relays[i].fallback_ip, port, &hints, &res) || !res) return -1;
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
    if (P.rounds == 1)
        fprintf(stderr, "[SERVERS] %s %s, %s %s\n",
                P.s[0].name, P.s[0].state == NETSERVER_UP ? "up" : "no answer",
                P.s[1].name, P.s[1].state == NETSERVER_UP ? "up" : "no answer");
    for (i = 0; i < NETSERVER_COUNT; i++)
        if (P.s[i].state == NETSERVER_UP && P.rounds == 1)
            fprintf(stderr, "[SERVERS] %s: %d ms, %d online, %d rooms, %d waiting (%d/%d answers)\n",
                    P.s[i].name, P.s[i].ping_ms, P.s[i].online, P.s[i].rooms, P.s[i].waiting, answers[i], PROBES);
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
    static const char *const text[] = { "auto", "eu", "na" };
    init();
    if (c < NETCHOICE_AUTO || c > NETCHOICE_NA) return;
    recomp_settings_set_text("online", "server", text[c]);
    pc_settings_save();
    EnterCriticalSection(&g_cs);
    P.override = c == NETCHOICE_AUTO ? -1 : (c == NETCHOICE_EU ? NETSERVER_EU : NETSERVER_NA);
    if (c == NETCHOICE_AUTO) P.pick = -1;   /* decided again, by the pings at hand */
    LeaveCriticalSection(&g_cs);
    fprintf(stderr, "[SERVERS] choice: %s\n", text[c]);
}

void netservers_use(int index)
{
    init();
    if (index < 0 || index >= NETSERVER_COUNT) return;
    EnterCriticalSection(&g_cs);
    P.override = index;
    LeaveCriticalSection(&g_cs);
    fprintf(stderr, "[SERVERS] this session: %s\n", k_relays[index].name);
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
    else if (c == NETCHOICE_EU) r = NETSERVER_EU;
    else if (c == NETCHOICE_NA) r = NETSERVER_NA;
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
                fprintf(stderr, "[SERVERS] automatic: %s (%s %d ms, %s %d ms)\n", P.s[P.pick].name,
                        P.s[0].short_name, P.s[0].ping_ms, P.s[1].short_name, P.s[1].ping_ms);
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
    if (cur < 0 || netservers_choice() != NETCHOICE_AUTO) return cur;
    EnterCriticalSection(&g_cs);
    if (P.override < 0 && P.pick >= 0) {
        int other = 1 - P.pick;
        const NetServerInfo *a = &P.s[P.pick], *b = &P.s[other];
        int better = b->state == NETSERVER_UP && (a->state == NETSERVER_OFFLINE ||
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
    return a ? a : (snprintf(buf, (size_t)size, "%s", k_relays[NETSERVER_EU].address), buf);
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

#else   /* other systems: no online play yet */

int netservers_reconnect(const char *name) { (void)name; return 0; }

void netservers_probe(void) {}
int netservers_get(NetServerInfo *out, int max) { (void)out; (void)max; return 0; }
int netservers_probing(void) { return 0; }
void netservers_set_choice(enum NetServerChoice c) { (void)c; }
void netservers_use(int index) { (void)index; }
int netservers_current(void) { return NETSERVER_EU; }
int netservers_reconsider(void) { return NETSERVER_EU; }
const char *netservers_address(char *buf, int size) { snprintf(buf, (size_t)size, "%s", k_relays[0].address); return buf; }
const char *netservers_address_wait(char *buf, int size, int wait_ms) { (void)wait_ms; return netservers_address(buf, size); }
int netservers_index_of(const char *address) { (void)address; return -1; }

#endif

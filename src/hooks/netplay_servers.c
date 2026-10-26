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
    if (!strcmp(low, "both") || !strcmp(low, "both servers") || !strcmp(low, "both_servers"))
        return NETCHOICE_BOTH;
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
    const char *text = c == NETCHOICE_AUTO ? "auto" : c == NETCHOICE_EU ? "eu" : c == NETCHOICE_NA ? "na" :
                       c == NETCHOICE_BOTH ? "both" : NULL;
    init();
    if (!text) return;
    recomp_settings_set_text("online", "server", text);
    pc_settings_save();
    EnterCriticalSection(&g_cs);
    P.override = c == NETCHOICE_EU ? NETSERVER_EU : c == NETCHOICE_NA ? NETSERVER_NA : -1;
    if (c == NETCHOICE_AUTO || c == NETCHOICE_BOTH) P.pick = -1;   /* decided again, by the pings at hand */
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
    enum NetServerChoice c = netservers_choice();
    if (cur < 0 || (c != NETCHOICE_AUTO && c != NETCHOICE_BOTH)) return cur;
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

/* ---- Both servers (build 31): the other relays' rooms, and moving between relays ----------------- */

#define ROOMLIST_TYPE   0x25
#define ROOMLIST_EVERY  3000    /* ms between two asks of one relay */
#define ROOMLIST_WAIT   1500    /* how long an ask waits for its answer */
#define ROOMLIST_FRESH  10000   /* an older answer is not used */
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
    int logged[NETSERVER_COUNT];        /* the room count last logged plus one, 0 none yet */
    unsigned version;
} R;

static struct {         /* a join or quick match waiting for the hub to reach its relay */
    int kind;           /* 0 none, 1 join, 2 quick */
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
            at += 6;
        }
        if (bad) continue;
        want[who] = 0;
        open--;
        EnterCriticalSection(&g_cs);
        memcpy(R.rooms[who], list, sizeof(list[0]) * (size_t)count);
        memcpy(R.age[who], ages, sizeof(ages[0]) * (size_t)count);
        R.count[who] = count;
        R.total[who] = rd16(b + 10);
        R.got[who] = GetTickCount64();
        R.answered[who] = 1;
        R.version++;
        if (R.logged[who] != count + 1) {
            R.logged[who] = count + 1;
            fprintf(stderr, "[SERVERS] both: %s lists %d room%s", P.s[who].name, count, count == 1 ? "" : "s");
            for (k = 0; k < count && k < 4; k++)
                fprintf(stderr, "%s %s (mode %d, %d/%d, phase %d, %d s)", k ? "," : ":", list[k].code, list[k].mode,
                        list[k].players, list[k].max, list[k].phase, ages[k]);
            fprintf(stderr, "\n");
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
        InterlockedExchange(&R.busy, 0);
    }
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

void netservers_fetch_rooms(void)
{
    ULONGLONG now;
    int i, any = 0;
    init();
    if (netservers_choice() != NETCHOICE_BOTH || R.busy) return;
    now = GetTickCount64();
    EnterCriticalSection(&g_cs);
    for (i = 0; i < NETSERVER_COUNT; i++)
        if (P.s[i].state != NETSERVER_OFFLINE && now - R.asked[i] >= ROOMLIST_EVERY) {
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

int netservers_rooms(NetServerRoom *out, int max, unsigned *version)
{
    NetServerInfo sv[NETSERVER_COUNT];
    NetRoomInfo own[32];
    int ping[NETSERVER_COUNT], order[NETSERVER_COUNT], cur = netservers_connected(), own_n = 0, n = 0, i, j, k;
    int both = netservers_choice() == NETCHOICE_BOTH, rtt = netplay_rtt();
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
    /* the relays by ping, the unknown last (a stable insertion sort of two or three) */
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
        } else if (both && fresh) {
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
        fprintf(stderr, "[SERVERS] both: the move to %s did not reach its lobby; the %s is dropped\n",
                P.s[A.server].name, kind == 1 ? "join" : "quick match");
        A.kind = 0;
        LeaveCriticalSection(&g_cs);
        return -1;
    }
    LeaveCriticalSection(&g_cs);
    if (!kind || netplay_state() != NETPLAY_LOBBY || !netplay_in_lobby() || cur != A.server) return 0;
    netplay_lobby_room(&r);
    if (r.in_room || (NETPLAY_IS_KOTH(A.mode) && netplay_lobby_caps() < 0)) return 0;
    EnterCriticalSection(&g_cs);
    kind = A.kind;
    A.kind = 0;
    LeaveCriticalSection(&g_cs);
    if (kind == 1) {
        fprintf(stderr, "[SERVERS] both: joining room %s on %s\n", A.code, P.s[A.server].name);
        netplay_lobby_join(A.code);
    } else if (kind == 2) {
        fprintf(stderr, "[SERVERS] both: quick match (mode %d) on %s\n", A.mode, P.s[A.server].name);
        netplay_lobby_quick(A.mode, A.max);
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
        fprintf(stderr, "[SERVERS] both: moving the hub to %s for a %s\n", P.s[server].name, kind == 1 ? "join" : "quick match");
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

void netservers_quick_on(int server, int mode, int max, const char *name)
{
    if (server < 0 || server >= NETSERVER_COUNT) { netplay_lobby_quick(mode, max); return; }
    move_to(2, server, NULL, mode, max, name);
}

int netservers_quick_move(int mode, int alone_s, const char *code, int *waiting, int *ping)
{
    NetServerInfo sv[NETSERVER_COUNT];
    int cur = netservers_connected(), my_age = alone_s, best = -1, best_n = 0, o, k;
    ULONGLONG now;
    if (waiting) *waiting = 0;
    if (ping) *ping = -1;
    if (netservers_choice() != NETCHOICE_BOTH || cur < 0 || NETPLAY_IS_KOTH(mode)) return -1;
    netservers_fetch_rooms();
    if (alone_s < 20) return -1;
    netservers_get(sv, NETSERVER_COUNT);
    now = GetTickCount64();
    EnterCriticalSection(&g_cs);
    /* this room's age as its own relay counts it: the clock the other player's view of it uses */
    if (code && R.got[cur] && now - R.got[cur] < ROOMLIST_FRESH)
        for (k = 0; k < R.count[cur]; k++)
            if (!strcmp(R.rooms[cur][k].code, code)) my_age = R.age[cur][k] + (int)((now - R.got[cur]) / 1000);
    for (o = 0; o < NETSERVER_COUNT; o++) {
        int n = 0;
        if (o == cur || sv[o].state != NETSERVER_UP) continue;
        if (R.answered[o]) {
            if (!R.got[o] || now - R.got[o] >= ROOMLIST_FRESH) continue;
            for (k = 0; k < R.count[o]; k++) {
                const NetRoomInfo *x = &R.rooms[o][k];
                int age = R.age[o][k] + (int)((now - R.got[o]) / 1000);
                if (x->mode != mode || x->phase != 0 || x->players < 1 || x->players >= x->max) continue;
                /* The younger room's player moves after 20 s alone (within 2 s of each other, the one
                 * on North America moves to Europe); the other one only once that room has waited 35 s,
                 * so a player there who cannot move (not on Both) is still met, and two players on Both
                 * never move at once and swap. */
                int mover = age > my_age + 2 || (age >= my_age - 2 && cur > o);
                if (mover || age >= 35) n += x->players;
            }
        } else if (sv[o].waiting > 0 && alone_s >= 35) {
            n = sv[o].waiting;      /* a relay before build 31: its STATUS, a little later */
        }
        if (n > best_n) { best = o; best_n = n; }
    }
    LeaveCriticalSection(&g_cs);
    if (best >= 0) {
        if (waiting) *waiting = best_n;
        if (ping) *ping = sv[best].ping_ms;
        fprintf(stderr, "[SERVERS] both: alone %d s (room age %d s); %d waiting on %s (%d ms): moving there\n", alone_s,
                my_age, best_n, sv[best].name, sv[best].ping_ms);
    }
    return best;
}

#else   /* other systems: no online play yet */

int netservers_reconnect(const char *name) { (void)name; return 0; }
int netservers_connected(void) { return -1; }
int netservers_best(void) { return -1; }
void netservers_fetch_rooms(void) {}
int netservers_rooms(NetServerRoom *out, int max, unsigned *version) { (void)out; (void)max; if (version) *version = 0; return 0; }
void netservers_join_on(int server, const char *code, int mode, const char *name) { (void)server; (void)code; (void)mode; (void)name; }
void netservers_quick_on(int server, int mode, int max, const char *name) { (void)server; (void)mode; (void)max; (void)name; }
int netservers_tick(int *mode) { (void)mode; return 0; }
int netservers_moving(void) { return 0; }
int netservers_quick_move(int mode, int alone_s, const char *code, int *waiting, int *ping)
{
    (void)mode; (void)alone_s; (void)code;
    if (waiting) *waiting = 0;
    if (ping) *ping = -1;
    return -1;
}

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

/* Online play through a relay server. See netplay.h, docs/netplay.md and the wire
 * format in docs/netplay-protocol.md.
 *
 * Threads. A network thread owns the socket: it talks to the relay, keeps the
 * session alive, streams this PC's controller and stores what arrives. The game
 * thread calls the hooks below from the title's own functions; a fight step waits
 * there until the other player's input for that step has arrived. One critical
 * section covers the shared state.
 *
 * Lockstep. Fight step s uses, for both players, the pad-table entry (all 32
 * bytes: buttons, a second word, both sticks) that the player's PC captured
 * `delay` steps earlier. Both PCs therefore feed the same entries to the same
 * steps, whatever their frame timing, and the first `delay` steps of a match get
 * an empty entry on both. The title polls the pad once per pass and may run
 * several steps a pass; the last word of an entry (always 0 from the poller) is
 * set to a marker when an entry is injected, so a reader can tell a fresh poll
 * from what it injected itself. */
#define _CRT_SECURE_NO_WARNINGS
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif
#include "host.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "netplay.h"

#if !defined(_WIN32)

int netplay_host(const char *s, const char *n, int d) { (void)s; (void)n; (void)d; return -1; }
int netplay_join(const char *s, const char *n, const char *r) { (void)s; (void)n; (void)r; return -1; }
void netplay_stop(void) {}
enum NetplayState netplay_state(void) { return NETPLAY_OFF; }
void netplay_status(char *t, int size) { if (size > 0) snprintf(t, (size_t)size, "Online play is Windows only for now."); }
void netplay_room(char *c, int size) { if (size > 0) c[0] = 0; }
void netplay_autostart(void) {}
int netplay_active(void) { return 0; }
void netplay_on_match_start(void) {}
void netplay_on_pad_pass(void) {}
void netplay_on_step(void) {}
void netplay_on_pad_read(uint32_t a) { (void)a; }
void netplay_on_seed(uint32_t e, int w, uint32_t x) { (void)e; (void)w; (void)x; }

#else

#include "input_host.h"

#ifndef DEFJAM_VERSION
#define DEFJAM_VERSION "dev"
#endif

#define DEFAULT_PORT  27777
#define PROTO         1
#define T_HELLO       0x01
#define T_WELCOME     0x02
#define T_PEER        0x03
#define T_DATA        0x04
#define T_PING        0x05
#define T_PONG        0x06
#define T_BYE         0x07
#define T_ERROR       0x08

/* Payload types inside DATA (ours; the relay never reads them). */
#define P_GAMEHELLO   1     /* host -> guest: u32 session seed, u8 input delay, u8 lockstep version */
#define P_GAMEACK     2     /* guest -> host */
#define P_LIVEPAD     3     /* u32 sequence, 18 bytes XBOX_GAMEPAD */
#define P_STEPS       4     /* u32 match, u32 last step, u8 count, count x 32 bytes */
#define P_HASH        5     /* u32 match, u32 step, u32 hash */
#define LOCKSTEP_VERSION 1

#define PAD_TABLE     0x003B8F48u
#define ACTORS        0x003B92E0u
#define STRIDE        0x12E8u
#define ENTRY         32
#define MARK          0x594C504Eu   /* "NPLY" in the entry's last word */
#define RING          512           /* steps of input kept per player */
#define REDUNDANT     8             /* steps repeated in every STEPS packet */
#define HASH_EVERY    60
#define STALL_LIMIT_MS 10000        /* mid-fight: the other player has gone */
#define START_LIMIT_MS 90000        /* the first steps: the other PC may still be loading the venue */

static CRITICAL_SECTION g_cs;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static struct {
    volatile LONG state;            /* enum NetplayState */
    volatile LONG stop;
    HANDLE thread;
    SOCKET sock;
    struct sockaddr_storage server;
    int server_len;
    char server_text[128];
    int creating;                   /* 1 host, 0 guest */
    char room[7];
    char name[25], peer_name[25];
    uint8_t build[8];
    uint8_t token[8];
    int have_token;
    int slot;                       /* 0 host = player 1, 1 guest = player 2 */
    char status[192];
    /* session */
    uint32_t session;
    int delay;
    int acked;
    /* controllers */
    XBOX_GAMEPAD remote_pad;
    uint32_t remote_seq, local_seq;
    DWORD remote_packet;
    XBOX_GAMEPAD last_sent;
    ULONGLONG last_pad_sent;
    /* link */
    ULONGLONG last_rx, last_ping, hello_sent, started;
    uint32_t ping_nonce;
    int rtt_ms;
    /* lockstep (step numbers count from 1 at each match start) */
    volatile LONG lockstep;         /* connected and not abandoned */
    uint32_t match, step;
    uint8_t latest_local[ENTRY];
    uint8_t in[2][RING][ENTRY];
    uint32_t in_tag[2][RING];       /* step + 1 (0 = empty) */
    uint32_t in_match[2][RING];
    HANDLE arrived;
    uint32_t local_hash[16][3], remote_hash[16][3];   /* match, step, hash */
    int desync;
    uint32_t desync_step;
    struct { uint32_t which, count; } seeds[32];
} N;

static BOOL CALLBACK init_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    WSADATA wsa;
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_cs);
    WSAStartup(MAKEWORD(2, 2), &wsa);
    N.sock = INVALID_SOCKET;
    N.arrived = CreateEventW(NULL, FALSE, FALSE, NULL);
    return TRUE;
}

static void set_status(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    EnterCriticalSection(&g_cs);
    vsnprintf(N.status, sizeof(N.status), format, args);
    LeaveCriticalSection(&g_cs);
    va_end(args);
    fprintf(stderr, "[NETPLAY] %s\n", N.status);
    fflush(stderr);
}

static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* ---- wire helpers ------------------------------------------------------------ */

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static int header(uint8_t *b, uint8_t type)
{
    b[0] = 'D'; b[1] = 'J'; b[2] = 'N'; b[3] = 'P'; b[4] = PROTO; b[5] = type;
    return 6;
}

static void send_raw(const uint8_t *b, int len)
{
    if (N.sock != INVALID_SOCKET)
        sendto(N.sock, (const char *)b, len, 0, (const struct sockaddr *)&N.server, N.server_len);
}

static void send_hello(void)
{
    uint8_t b[64];
    int n = header(b, T_HELLO), len = (int)strlen(N.name);
    b[n++] = N.creating ? 0 : 1;
    memset(b + n, 0, 6);
    if (!N.creating) memcpy(b + n, N.room, 6);
    n += 6;
    memcpy(b + n, N.build, 8); n += 8;
    b[n++] = (uint8_t)len;
    memcpy(b + n, N.name, (size_t)len); n += len;
    send_raw(b, n);
}

static void send_token_only(uint8_t type, uint32_t extra, int with_extra)
{
    uint8_t b[32];
    int n = header(b, type);
    memcpy(b + n, N.token, 8); n += 8;
    if (with_extra) { put32(b + n, extra); n += 4; }
    send_raw(b, n);
}

static void send_data(const uint8_t *payload, int len)
{
    uint8_t b[1200];
    int n = header(b, T_DATA);
    if (!N.have_token || len <= 0 || len > 1150) return;
    memcpy(b + n, N.token, 8); n += 8;
    memcpy(b + n, payload, (size_t)len); n += len;
    send_raw(b, n);
}

/* ---- the remote controller, as the title's second pad -------------------------- */

static DWORD remote_state(XBOX_INPUT_STATE *state)
{
    if (N.state != NETPLAY_CONNECTED) return ERROR_DEVICE_NOT_CONNECTED;
    EnterCriticalSection(&g_cs);
    state->dwPacketNumber = N.remote_packet;
    state->Gamepad = N.remote_pad;
    LeaveCriticalSection(&g_cs);
    return ERROR_SUCCESS;
}

static void become_connected(void)
{
    N.match = N.step = 0;
    memset(N.in_tag, 0, sizeof(N.in_tag));
    memset(N.seeds, 0, sizeof(N.seeds));
    N.desync = 0;
    InterlockedExchange(&N.state, NETPLAY_CONNECTED);
    InterlockedExchange(&N.lockstep, 1);
    xbox_HostInputSetNetplay(1, N.slot, 1 - N.slot, remote_state);
    set_status("Connected to %s. You are player %d. Both choose Versus; player 1 picks first.",
               N.peer_name[0] ? N.peer_name : "the other player", N.slot + 1);
}

static void lose_peer(const char *why)
{
    InterlockedExchange(&N.lockstep, 0);
    xbox_HostInputSetNetplay(0, 0, 0, NULL);
    SetEvent(N.arrived);
    if (N.creating && !N.stop) {
        InterlockedExchange(&N.state, NETPLAY_WAITING);
        N.acked = 0;
        set_status("%s Room %s is open again; waiting for a player.", why, N.room);
    } else {
        InterlockedExchange(&N.state, NETPLAY_FAILED);
        set_status("%s", why);
    }
}

/* ---- what arrives -------------------------------------------------------------- */

static void store_steps(const uint8_t *p, int len)
{
    uint32_t match, last;
    int count, i, remote = 1 - N.slot;
    if (len < 9) return;
    match = get32(p); last = get32(p + 4); count = p[8];
    if (len < 9 + count * ENTRY) return;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < count; i++) {
        uint32_t s = last - (uint32_t)(count - 1 - i);
        uint32_t k = s % RING;
        if (s == 0) continue;
        memcpy(N.in[remote][k], p + 9 + i * ENTRY, ENTRY);
        N.in_tag[remote][k] = s + 1;
        N.in_match[remote][k] = match;
    }
    LeaveCriticalSection(&g_cs);
    SetEvent(N.arrived);
}

static unsigned g_hash_matches;     /* checksums compared and equal, this session */

/* Compare every checksum both sides have for the same match and step, once each. */
static void check_hashes(void)
{
    int i, j;
    for (i = 0; i < 16; i++)
        for (j = 0; j < 16; j++) {
            if (!N.local_hash[i][1] || N.local_hash[i][0] != N.remote_hash[j][0] ||
                N.local_hash[i][1] != N.remote_hash[j][1])
                continue;
            if (N.local_hash[i][2] != N.remote_hash[j][2]) {
                if (!N.desync) {
                    N.desync = 1;
                    N.desync_step = N.local_hash[i][1];
                    fprintf(stderr, "[NETPLAY] desync in match %u at step %u (local %08X, remote %08X)\n",
                            N.local_hash[i][0], N.local_hash[i][1], N.local_hash[i][2], N.remote_hash[j][2]);
                }
            } else if (++g_hash_matches % 10 == 0) {
                fprintf(stderr, "[NETPLAY] in step: %u checksums compared and equal (match %u, step %u)\n",
                        g_hash_matches, N.local_hash[i][0], N.local_hash[i][1]);
            }
            N.remote_hash[j][1] = 0;    /* compared */
        }
}

static void on_data(const uint8_t *p, int len)
{
    if (len < 1) return;
    switch (p[0]) {
    case P_GAMEHELLO:
        if (!N.creating && len >= 7 && p[6] == LOCKSTEP_VERSION) {
            uint8_t ack = P_GAMEACK;
            N.session = get32(p + 1);
            N.delay = p[5] >= 1 && p[5] <= 12 ? p[5] : 3;
            send_data(&ack, 1);
            if (N.state != NETPLAY_CONNECTED) become_connected();
        }
        break;
    case P_GAMEACK:
        if (N.creating && N.state == NETPLAY_SYNCING) {
            N.acked = 1;
            become_connected();
        }
        break;
    case P_LIVEPAD:
        if (len >= 5 + (int)sizeof(XBOX_GAMEPAD)) {
            uint32_t seq = get32(p + 1);
            EnterCriticalSection(&g_cs);
            if ((int32_t)(seq - N.remote_seq) > 0) {
                static int seen, pressed;
                N.remote_seq = seq;
                memcpy(&N.remote_pad, p + 5, sizeof(XBOX_GAMEPAD));
                N.remote_packet++;
                if (!seen++)
                    fprintf(stderr, "[NETPLAY] the other player's controller is arriving (player %d)\n", 2 - N.slot);
                if (!pressed && (N.remote_pad.wButtons || N.remote_pad.bAnalogButtons[0] > 32)) {
                    pressed = 1;
                    fprintf(stderr, "[NETPLAY] first press from the other player's controller (buttons %04X)\n",
                            N.remote_pad.wButtons);
                }
            }
            LeaveCriticalSection(&g_cs);
        }
        break;
    case P_STEPS:
        store_steps(p + 1, len - 1);
        break;
    case P_HASH:
        if (len >= 13) {
            EnterCriticalSection(&g_cs);
            memmove(N.remote_hash[1], N.remote_hash[0], sizeof(N.remote_hash[0]) * 15);
            N.remote_hash[0][0] = get32(p + 1);
            N.remote_hash[0][1] = get32(p + 5);
            N.remote_hash[0][2] = get32(p + 9);
            check_hashes();
            LeaveCriticalSection(&g_cs);
        }
        break;
    }
}

static void on_packet(const uint8_t *b, int len)
{
    if (len < 6 || memcmp(b, "DJNP", 4) || b[4] != PROTO) return;
    N.last_rx = GetTickCount64();
    switch (b[5]) {
    case T_WELCOME:
        if (len >= 6 + 8 + 1 + 6) {
            memcpy(N.token, b + 6, 8);
            N.have_token = 1;
            N.slot = b[14] ? 1 : 0;
            memcpy(N.room, b + 15, 6);
            N.room[6] = 0;
            if (N.state == NETPLAY_CONNECTING) {
                InterlockedExchange(&N.state, NETPLAY_WAITING);
                if (N.creating)
                    set_status("Room %s is open. Tell the other player this code; waiting for them to join.", N.room);
                else
                    set_status("In room %s; waiting for the host.", N.room);
            }
        }
        break;
    case T_PEER:
        if (len >= 8) {
            int n = b[7];
            if (n == 0) {
                if (N.state == NETPLAY_CONNECTED || N.state == NETPLAY_SYNCING)
                    lose_peer("The other player left.");
            } else if (len >= 8 + n && N.state == NETPLAY_WAITING) {
                memcpy(N.peer_name, b + 8, (size_t)(n < 24 ? n : 24));
                N.peer_name[n < 24 ? n : 24] = 0;
                InterlockedExchange(&N.state, NETPLAY_SYNCING);
                if (N.creating) {
                    LARGE_INTEGER t;
                    QueryPerformanceCounter(&t);
                    N.session = mix32((uint32_t)t.QuadPart ^ (uint32_t)GetCurrentProcessId() ^ 0x9E3779B9u);
                    N.acked = 0;
                    N.hello_sent = 0;
                }
                set_status("%s joined; agreeing on the session...", N.peer_name);
            }
        }
        break;
    case T_DATA:
        if (len >= 7) on_data(b + 7, len - 7);
        break;
    case T_PONG:
        if (len >= 10 && get32(b + 6) == N.ping_nonce)
            N.rtt_ms = (int)(GetTickCount64() - N.last_ping);
        break;
    case T_ERROR:
        if (len >= 7) {
            static const char *const why[] = { "", "No room with that code.", "That room is full.",
                "The other player has a different version of the game.", "That name is not allowed.",
                "The server is full.", "Too many attempts; wait a minute.", "The server lost this session." };
            int code = b[6];
            InterlockedExchange(&N.lockstep, 0);
            InterlockedExchange(&N.state, NETPLAY_FAILED);
            set_status("%s", code >= 1 && code <= 7 ? why[code] : "The server refused the connection.");
        }
        break;
    }
}

/* ---- the network thread -------------------------------------------------------- */

static void send_live_pad(ULONGLONG now)
{
    XBOX_INPUT_STATE s;
    uint8_t p[5 + sizeof(XBOX_GAMEPAD)];
    memset(&s, 0, sizeof(s));
    xbox_HostInputGetLocalState(&s);
    if (!memcmp(&s.Gamepad, &N.last_sent, sizeof(XBOX_GAMEPAD)) && now - N.last_pad_sent < 100) return;
    N.last_sent = s.Gamepad;
    N.last_pad_sent = now;
    p[0] = P_LIVEPAD;
    put32(p + 1, ++N.local_seq);
    memcpy(p + 5, &s.Gamepad, sizeof(XBOX_GAMEPAD));
    send_data(p, (int)sizeof(p));
}

static DWORD WINAPI net_thread(LPVOID unused)
{
    uint8_t b[1500];
    (void)unused;
    N.started = N.last_rx = GetTickCount64();
    while (!N.stop) {
        ULONGLONG now = GetTickCount64();
        int got;
        if (N.state == NETPLAY_CONNECTING) {
            if (now - N.hello_sent >= 500) { send_hello(); N.hello_sent = now; }
            if (now - N.started > 10000) {
                InterlockedExchange(&N.state, NETPLAY_FAILED);
                set_status("No answer from the server %s. Check the address and that it is running.", N.server_text);
            }
        }
        if (N.have_token && N.state != NETPLAY_FAILED) {
            if (now - N.last_ping >= 1000) {
                N.ping_nonce++;
                N.last_ping = now;
                send_token_only(T_PING, N.ping_nonce, 1);
            }
            if (now - N.last_rx > 15000) {
                InterlockedExchange(&N.lockstep, 0);
                xbox_HostInputSetNetplay(0, 0, 0, NULL);
                InterlockedExchange(&N.state, NETPLAY_FAILED);
                set_status("The connection to the server was lost.");
            }
        }
        if (N.state == NETPLAY_SYNCING && N.creating && now - N.hello_sent >= 300) {
            uint8_t p[7];
            p[0] = P_GAMEHELLO;
            put32(p + 1, N.session);
            p[5] = (uint8_t)N.delay;
            p[6] = LOCKSTEP_VERSION;
            send_data(p, 7);
            N.hello_sent = now;
        }
        if (N.state == NETPLAY_CONNECTED || N.state == NETPLAY_SYNCING)
            send_live_pad(now);
        got = recv(N.sock, (char *)b, sizeof(b), 0);
        if (got > 0)
            on_packet(b, got);
    }
    if (N.have_token)
        send_token_only(T_BYE, 0, 0);
    return 0;
}

static int resolve(const char *server)
{
    char host[128], port[16];
    const char *colon = strrchr(server, ':');
    struct addrinfo hints, *res = NULL;
    if (colon && colon != server) {
        snprintf(host, sizeof(host), "%.*s", (int)(colon - server), server);
        snprintf(port, sizeof(port), "%s", colon + 1);
    } else {
        snprintf(host, sizeof(host), "%s", server);
        snprintf(port, sizeof(port), "%d", DEFAULT_PORT);
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &res) || !res) return -1;
    memcpy(&N.server, res->ai_addr, res->ai_addrlen);
    N.server_len = (int)res->ai_addrlen;
    freeaddrinfo(res);
    return 0;
}

static int start(const char *server, const char *name, int creating, const char *room, int delay)
{
    DWORD timeout = 20;
    uint32_t h1 = 2166136261u, h2;
    const char *v;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    netplay_stop();
    if (!server || !*server) { InterlockedExchange(&N.state, NETPLAY_FAILED); set_status("Enter the server address first."); return -1; }
    snprintf(N.server_text, sizeof(N.server_text), "%s", server);
    snprintf(N.name, sizeof(N.name), "%s", name && *name ? name : "Player");
    N.creating = creating;
    memset(N.room, 0, sizeof(N.room));
    if (!creating) {
        int i;
        for (i = 0; i < 6 && room && room[i]; i++) N.room[i] = (char)toupper((unsigned char)room[i]);
        if (i != 6) { InterlockedExchange(&N.state, NETPLAY_FAILED); set_status("A room code has 6 characters."); return -1; }
    }
    N.delay = delay >= 1 && delay <= 12 ? delay : 3;
    /* The build's fingerprint: the version and the lockstep rules. */
    for (v = DEFJAM_VERSION; *v; v++) h1 = (h1 ^ (uint8_t)*v) * 16777619u;
    h2 = mix32(h1 ^ LOCKSTEP_VERSION);
    put32(N.build, h1); put32(N.build + 4, h2);
    N.have_token = 0;
    N.peer_name[0] = 0;
    N.rtt_ms = -1;
    N.remote_seq = N.local_seq = 0;
    memset(&N.remote_pad, 0, sizeof(N.remote_pad));
    if (resolve(server)) { InterlockedExchange(&N.state, NETPLAY_FAILED); set_status("Cannot find the server %s.", server); return -1; }
    N.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (N.sock == INVALID_SOCKET) { InterlockedExchange(&N.state, NETPLAY_FAILED); set_status("No network socket."); return -1; }
    setsockopt(N.sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
    N.stop = 0;
    N.hello_sent = 0;
    InterlockedExchange(&N.state, NETPLAY_CONNECTING);
    set_status("Connecting to %s...", server);
    N.thread = CreateThread(NULL, 0, net_thread, NULL, 0, NULL);
    if (N.thread) SetThreadPriority(N.thread, THREAD_PRIORITY_ABOVE_NORMAL);
    return N.thread ? 0 : -1;
}

int netplay_host(const char *server, const char *name, int input_delay)
{
    return start(server, name, 1, NULL, input_delay);
}

int netplay_join(const char *server, const char *name, const char *room)
{
    return start(server, name, 0, room, 3);
}

void netplay_stop(void)
{
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    InterlockedExchange(&N.lockstep, 0);
    xbox_HostInputSetNetplay(0, 0, 0, NULL);
    if (N.thread) {
        N.stop = 1;
        WaitForSingleObject(N.thread, 2000);
        CloseHandle(N.thread);
        N.thread = NULL;
    }
    if (N.sock != INVALID_SOCKET) { closesocket(N.sock); N.sock = INVALID_SOCKET; }
    N.have_token = 0;
    InterlockedExchange(&N.state, NETPLAY_OFF);
    SetEvent(N.arrived);
}

enum NetplayState netplay_state(void) { return (enum NetplayState)N.state; }

void netplay_status(char *text, int size)
{
    if (size <= 0) return;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    if (N.state == NETPLAY_CONNECTED && N.rtt_ms >= 0)
        snprintf(text, (size_t)size, "%s  Ping %d ms, input delay %d steps.%s", N.status, N.rtt_ms, N.delay,
                 N.desync ? "  The games went out of step in the last fight." : "");
    else if (N.state == NETPLAY_OFF)
        snprintf(text, (size_t)size, "Not connected.");
    else
        snprintf(text, (size_t)size, "%s", N.status);
    LeaveCriticalSection(&g_cs);
}

void netplay_room(char *code, int size)
{
    if (size > 0) snprintf(code, (size_t)size, "%s", N.room);
}

/* Unattended sessions for tests: RECOMP_NETPLAY=host or join, RECOMP_NETPLAY_SERVER, and
 * RECOMP_NETPLAY_CODE_FILE, where the host writes its room code once it has one and the
 * guest waits for it (up to a minute). */
static DWORD WINAPI autostart_thread(LPVOID unused)
{
    const char *mode = getenv("RECOMP_NETPLAY"), *server = getenv("RECOMP_NETPLAY_SERVER");
    const char *file = getenv("RECOMP_NETPLAY_CODE_FILE");
    int i;
    (void)unused;
    if (!strcmp(mode, "host")) {
        netplay_host(server, "Test host", 3);
        for (i = 0; i < 600 && !N.room[0] && N.state != NETPLAY_FAILED; i++) Sleep(100);
        if (N.room[0] && file && *file) {
            FILE *f = fopen(file, "w");
            if (f) { fputs(N.room, f); fclose(f); }
        }
    } else {
        char code[16] = "";
        for (i = 0; i < 600 && file && *file; i++) {
            FILE *f = fopen(file, "r");
            if (f) {
                if (fgets(code, sizeof(code), f) && strlen(code) >= 6) { fclose(f); break; }
                fclose(f);
            }
            Sleep(100);
        }
        code[6] = 0;
        netplay_join(server, "Test guest", code);
    }
    return 0;
}

void netplay_autostart(void)
{
    const char *mode = getenv("RECOMP_NETPLAY"), *server = getenv("RECOMP_NETPLAY_SERVER");
    HANDLE t;
    if (!mode || !server || (strcmp(mode, "host") && strcmp(mode, "join"))) return;
    t = CreateThread(NULL, 0, autostart_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

int netplay_active(void) { return N.lockstep != 0; }

/* ---- the game's hooks ------------------------------------------------------------ */

static uint32_t fighter_hash(void)
{
    static const uint32_t fields[] = { 0x30, 0x44, 0x48, 0x4C, 0x54, 0x350 };
    uint32_t h = 2166136261u;
    int slot;
    unsigned i;
    for (slot = 0; slot < 2; slot++)
        for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
            h = (h ^ MEM32(ACTORS + (uint32_t)slot * STRIDE + fields[i])) * 16777619u;
    return h;
}

void netplay_on_match_start(void)
{
    if (!N.lockstep) return;
    EnterCriticalSection(&g_cs);
    N.match++;
    N.step = 0;
    memset(N.latest_local, 0, sizeof(N.latest_local));
    N.desync = 0;
    LeaveCriticalSection(&g_cs);
    fprintf(stderr, "[NETPLAY] match %u starts in lockstep (delay %d)\n", N.match, N.delay);
}

void netplay_on_pad_pass(void) {}

static void send_steps(uint32_t last)
{
    uint8_t p[1 + 9 + REDUNDANT * ENTRY];
    uint32_t first = last >= REDUNDANT ? last - REDUNDANT + 1 : 1, s;
    int count = 0, local = N.slot;
    p[0] = P_STEPS;
    put32(p + 1, N.match);
    put32(p + 5, last);
    EnterCriticalSection(&g_cs);
    for (s = first; s <= last; s++) {
        uint32_t k = s % RING;
        if (N.in_tag[local][k] == s + 1)
            memcpy(p + 10 + count * ENTRY, N.in[local][k], ENTRY);
        else
            memset(p + 10 + count * ENTRY, 0, ENTRY);
        count++;
    }
    LeaveCriticalSection(&g_cs);
    p[9] = (uint8_t)count;
    send_data(p, 10 + count * ENTRY);
}

void netplay_on_step(void)
{
    uint32_t s, target, k;
    int remote = 1 - N.slot;
    ULONGLONG waited_since, last_send;
    if (!N.lockstep) return;
    s = ++N.step;
    target = s + (uint32_t)N.delay;
    k = target % RING;
    EnterCriticalSection(&g_cs);
    memcpy(N.in[N.slot][k], N.latest_local, ENTRY);
    N.in_tag[N.slot][k] = target + 1;
    N.in_match[N.slot][k] = N.match;
    LeaveCriticalSection(&g_cs);
    send_steps(target);

    if (s % HASH_EVERY == 0) {
        uint8_t p[13];
        uint32_t h = fighter_hash();
        EnterCriticalSection(&g_cs);
        memmove(N.local_hash[1], N.local_hash[0], sizeof(N.local_hash[0]) * 15);
        N.local_hash[0][0] = N.match; N.local_hash[0][1] = s; N.local_hash[0][2] = h;
        check_hashes();
        LeaveCriticalSection(&g_cs);
        p[0] = P_HASH; put32(p + 1, N.match); put32(p + 5, s); put32(p + 9, h);
        send_data(p, 13);
    }

    /* Wait for the other player's entry for this step. */
    if (s <= (uint32_t)N.delay) return;
    k = s % RING;
    waited_since = last_send = GetTickCount64();
    for (;;) {
        int ready;
        EnterCriticalSection(&g_cs);
        ready = N.in_tag[remote][k] == s + 1 && N.in_match[remote][k] == N.match;
        LeaveCriticalSection(&g_cs);
        if (ready || !N.lockstep) break;
        WaitForSingleObject(N.arrived, 4);
        {
            ULONGLONG now = GetTickCount64();
            if (now - last_send >= 50) { send_steps(target); last_send = now; }   /* the peer may be waiting for ours */
            if (now - waited_since > (s <= (uint32_t)N.delay + 1 ? START_LIMIT_MS : STALL_LIMIT_MS)) {
                InterlockedExchange(&N.lockstep, 0);
                set_status("The other player stopped answering during the fight; continuing offline.");
                break;
            }
        }
    }
}

void netplay_on_pad_read(uint32_t actor)
{
    uint32_t slot, base, s;
    int i;
    uint8_t entry[ENTRY];
    if (!N.lockstep) return;
    if (actor < ACTORS || (actor - ACTORS) % STRIDE) return;
    slot = (actor - ACTORS) / STRIDE;
    if (slot > 1) return;
    base = PAD_TABLE + slot * 0x20u;
    /* This PC's player: a fresh poll (no marker) is the input to send next. */
    if ((int)slot == N.slot && MEM32(base + 28) != MARK) {
        EnterCriticalSection(&g_cs);
        for (i = 0; i < ENTRY; i++) N.latest_local[i] = MEM8(base + (uint32_t)i);
        N.latest_local[28] = N.latest_local[29] = N.latest_local[30] = N.latest_local[31] = 0;
        LeaveCriticalSection(&g_cs);
    }
    s = N.step;
    memset(entry, 0, sizeof(entry));
    if (s > (uint32_t)N.delay) {
        uint32_t k = s % RING;
        EnterCriticalSection(&g_cs);
        if (N.in_tag[slot][k] == s + 1)
            memcpy(entry, N.in[slot][k], ENTRY);
        LeaveCriticalSection(&g_cs);
    }
    for (i = 0; i < 7; i++)
        MEM32(base + (uint32_t)i * 4) = get32(entry + i * 4);
    MEM32(base + 28) = MARK;
}

void netplay_on_seed(uint32_t esp, int words, uint32_t which)
{
    int i, slot = -1;
    uint32_t count;
    if (!N.lockstep) return;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < 32; i++)
        if (N.seeds[i].which == which || !N.seeds[i].which) { slot = i; break; }
    if (slot < 0) slot = (int)(which % 32);
    N.seeds[slot].which = which;
    count = N.seeds[slot].count++;
    LeaveCriticalSection(&g_cs);
    for (i = 0; i < words; i++) {
        uint32_t v = mix32(N.session ^ mix32(which * 0x9E3779B9u + count * 0x85EBCA6Bu + (uint32_t)i));
        if (i == 1) v |= 1;     /* an additive term stays odd */
        MEM32(esp + 4 + 4 * (uint32_t)i) = v;
    }
}

#endif

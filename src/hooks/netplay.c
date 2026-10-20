/* Online play through a relay server. See netplay.h, docs/netplay.md and the wire
 * format in docs/netplay-protocol.md.
 *
 * Threads. A network thread owns the socket: it talks to the relay, keeps the
 * session alive, streams this PC's controller and stores what arrives. The game
 * thread calls the hooks below from the title's own functions; a fight step waits
 * there until every other player's input for that step has arrived. One critical
 * section covers the shared state.
 *
 * Two ways in. netplay_host/netplay_join make a two-player room by code; both
 * players then pick the match in the menus. netplay_lobby enters the hub: rooms,
 * quick match and the venue vote run on the server, and its START names the
 * match, which automatch.c sets up on every PC from the main menu.
 *
 * Lockstep. Fight step s uses, for every player, the pad-table entry (all 32
 * bytes: buttons, a second word, both sticks) that the player's PC captured
 * `delay` steps earlier. All PCs therefore feed the same entries to the same
 * steps, whatever their frame timing, and the first `delay` steps of a match get
 * an empty entry on all. The title polls the pad once per pass and may run
 * several steps a pass; the last word of an entry (always 0 from the poller) is
 * set to a marker when an entry is injected, so a reader can tell a fresh poll
 * from what it injected itself. A player is a position 0..players-1, the game's
 * player number; in a hub room positions follow the room slots in order. */
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
#include "automatch.h"
#include "online_data.h"

#if !defined(_WIN32)

int netplay_host(const char *s, const char *n, int d) { (void)s; (void)n; (void)d; return -1; }
int netplay_join(const char *s, const char *n, const char *r) { (void)s; (void)n; (void)r; return -1; }
void netplay_stop(void) {}
enum NetplayState netplay_state(void) { return NETPLAY_OFF; }
void netplay_status(char *t, int size) { if (size > 0) snprintf(t, (size_t)size, "Online play is Windows only for now."); }
void netplay_room(char *c, int size) { if (size > 0) c[0] = 0; }
void netplay_autostart(void) {}
int netplay_active(void) { return 0; }
int netplay_lobby(const char *s, const char *n) { (void)s; (void)n; return -1; }
void netplay_lobby_list(int m) { (void)m; }
int netplay_lobby_rooms(NetRoomInfo *o, int m, unsigned *v) { (void)o; (void)m; if (v) *v = 0; return 0; }
void netplay_lobby_create(const NetRoomSettings *s) { (void)s; }
void netplay_lobby_join(const char *c) { (void)c; }
void netplay_lobby_quick(int m, int x) { (void)m; (void)x; }
void netplay_lobby_settings(const NetRoomSettings *s) { (void)s; }
void netplay_lobby_ready(int r, int f) { (void)r; (void)f; }
void netplay_lobby_vote(int c) { (void)c; }
void netplay_lobby_leave(void) {}
void netplay_lobby_room(NetRoom *o) { memset(o, 0, sizeof(*o)); }
int netplay_in_lobby(void) { return 0; }
void netplay_sync_point(void) {}
void netplay_on_main_menu(void) {}
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
/* the lobby extension */
#define T_LIST        0x10
#define T_ROOMS       0x11
#define T_CREATE      0x12
#define T_JOIN        0x13
#define T_QUICK       0x14
#define T_SETTINGS    0x15
#define T_READY       0x16
#define T_VOTE        0x17
#define T_LEAVE       0x18
#define T_ROOM        0x19
#define T_START       0x1A
#define T_OVER        0x1B

/* DATA payloads between the game copies (opaque to the relay) */
#define P_GAMEHELLO   1     /* host -> guest: u32 session seed, u8 input delay, u8 lockstep version */
#define P_GAMEACK     2     /* guest -> host */
#define P_LIVEPAD     3     /* u32 sequence, 18 bytes XBOX_GAMEPAD */
#define P_STEPS       4     /* u32 match, u32 last step, u8 count, count x 32 bytes */
#define P_HASH        5     /* u32 match, u32 step, u32 hash */
#define P_RTT         6     /* hub: u32 START seed, u16 this PC's ping to the relay in ms */
#define LOCKSTEP_VERSION 2

#define MAXP          NETPLAY_MAX_PLAYERS
#define PAD_TABLE     0x003B8F48u
#define ACTORS        0x003B92E0u
#define STRIDE        0x12E8u
#define ENTRY         32
#define MARK          0x594C504Eu   /* "NPLY" in the entry's last word */
#define RING          512           /* steps of input kept per player */
#define REDUNDANT     8             /* steps repeated in every STEPS packet */
#define HASH_EVERY    60
#define STALL_LIMIT_MS 10000        /* mid-fight: a player has gone */
#define START_LIMIT_MS 90000        /* the first steps: another PC may still be loading the venue */
#define LOBBY_DELAY   3             /* hub matches: until the delay is agreed (P_RTT) */
#define DELAY_MIN     2
#define DELAY_MAX     10
#define RTT_WAIT_MS   5000          /* at the sync point: how long to wait for every player's ping */
#define RETRY_MS      400           /* lobby requests are repeated until the server answers */
#define RETRIES       8

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
    int creating;                   /* code rooms: 1 host, 0 guest */
    int lobby;                      /* 1: the hub */
    char room[7];
    char name[25], peer_name[25];
    uint8_t build[8];
    uint8_t token[8];
    int have_token;
    int slot;                       /* this client's slot in its room */
    char status[192];
    /* session */
    uint32_t session;
    int delay;
    int acked;
    int players, pos;               /* players in the match, this PC's position */
    int fighters;                   /* in the fight, with the CPU ones */
    int pos_of_slot[MAXP];          /* room slot -> position, -1 none */
    /* controllers */
    XBOX_GAMEPAD remote_pad[MAXP];
    uint32_t remote_seq[MAXP], local_seq;
    DWORD remote_packet[MAXP];
    XBOX_GAMEPAD last_sent;
    ULONGLONG last_pad_sent;
    /* link */
    ULONGLONG last_rx, last_ping, hello_sent, started;
    uint32_t ping_nonce;
    int rtt_ms;
    int rtt_recent[5], rtt_next;    /* the last pings, for a jitter margin */
    /* lockstep (step numbers count from 1 at each match start) */
    volatile LONG lockstep;         /* connected and not abandoned */
    uint32_t match, match_count, step;
    uint8_t latest_local[ENTRY];
    uint8_t in[MAXP][RING][ENTRY];
    uint32_t in_tag[MAXP][RING];    /* step + 1 (0 = empty) */
    uint32_t in_match[MAXP][RING];
    HANDLE arrived;
    uint32_t local_hash[16][3], remote_hash[16][3];   /* match, step, hash */
    int desync;
    uint32_t desync_step;
    struct { uint32_t which, count; } seeds[32];
    /* the hub */
    NetRoom roomv;
    NetRoomInfo rooms[32];
    int room_count;
    unsigned rooms_version;
    uint8_t pend[300];              /* the request waiting for an answer */
    int pend_len, pend_tries;
    ULONGLONG pend_sent;
    int match_live;                 /* START received, OVER not yet sent */
    int match_seen;                 /* the game has started that match */
    int over_pending;
    int start_this_play;            /* a START arrived since the room entered phase 2 */
    int peer_rtt[MAXP];             /* each position's ping as it reported it for this START, -1 not yet */
    int delay_fixed;                /* the match's delay is agreed */
    ULONGLONG last_rtt_sent;
    ULONGLONG last_recover;
    uint32_t start_seed;
    int start_slots[MAXP];
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
    b[n++] = N.lobby ? 2 : N.creating ? 0 : 1;
    memset(b + n, 0, 6);
    if (!N.lobby && !N.creating) memcpy(b + n, N.room, 6);
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

/* A hub request: header, token, then `body`. Repeated every RETRY_MS until the server answers
 * (the relay treats a repeat as the same request). A new request replaces a waiting one. */
static void request(uint8_t type, const uint8_t *body, int len)
{
    uint8_t b[sizeof(N.pend)];
    int n;
    if (!N.lobby || !N.have_token || len < 0 || len > (int)sizeof(b) - 14) return;
    n = header(b, type);
    memcpy(b + n, N.token, 8); n += 8;
    if (len) memcpy(b + n, body, (size_t)len);
    n += len;
    EnterCriticalSection(&g_cs);
    memcpy(N.pend, b, (size_t)n);
    N.pend_len = n;
    N.pend_tries = 1;
    N.pend_sent = GetTickCount64();
    N.roomv.error[0] = 0;
    LeaveCriticalSection(&g_cs);
    send_raw(b, n);
}

static void answered(void)
{
    N.pend_len = 0;
}

/* ---- the other players' controllers, as the title's other pads ---------------- */

static DWORD remote_slot_state(int slot, XBOX_INPUT_STATE *state)
{
    if (N.state != NETPLAY_CONNECTED || slot < 0 || slot >= N.players || slot == N.pos)
        return ERROR_DEVICE_NOT_CONNECTED;
    EnterCriticalSection(&g_cs);
    state->dwPacketNumber = N.remote_packet[slot];
    state->Gamepad = N.remote_pad[slot];
    LeaveCriticalSection(&g_cs);
    return ERROR_SUCCESS;
}

static void assign_input(int on)
{
    unsigned mask = 0;
    int i;
    if (!on) { xbox_HostInputSetNetplay(0, 0, 0, NULL); return; }
    for (i = 0; i < N.players; i++)
        if (i != N.pos) mask |= 1u << i;
    xbox_HostInputSetNetplayMulti(1, N.pos, mask, remote_slot_state);
}

static void reset_remote_pads(void)
{
    EnterCriticalSection(&g_cs);
    memset(N.remote_pad, 0, sizeof(N.remote_pad));
    memset(N.remote_seq, 0, sizeof(N.remote_seq));
    LeaveCriticalSection(&g_cs);
}

static void become_connected(void)
{
    if (!N.lobby) {
        N.match_count = N.step = 0;
        memset(N.in_tag, 0, sizeof(N.in_tag));
    }
    memset(N.seeds, 0, sizeof(N.seeds));
    N.desync = 0;
    InterlockedExchange(&N.state, NETPLAY_CONNECTED);
    InterlockedExchange(&N.lockstep, 1);
    assign_input(1);
    if (N.lobby)
        set_status("Match found: %d players, you are player %d. It starts by itself.", N.players, N.pos + 1);
    else
        set_status("Connected to %s. You are player %d. Both choose Versus; player 1 picks first.",
                   N.peer_name[0] ? N.peer_name : "the other player", N.pos + 1);
}

static void lose_peer(const char *why)
{
    InterlockedExchange(&N.lockstep, 0);
    assign_input(0);
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

/* The hub match is over for this PC: tell the server and go back to the room. */
static void end_hub_match(const char *why)
{
    InterlockedExchange(&N.lockstep, 0);
    assign_input(0);
    SetEvent(N.arrived);
    N.match_live = N.match_seen = 0;
    N.over_pending = 1;
    request(T_OVER, NULL, 0);
    InterlockedExchange(&N.state, NETPLAY_LOBBY);
    set_status("%s", why);
}

/* ---- what arrives -------------------------------------------------------------- */

static void store_steps(int from, const uint8_t *p, int len)
{
    uint32_t match, last;
    int count, i;
    if (len < 9 || from < 0 || from >= MAXP) return;
    match = get32(p); last = get32(p + 4); count = p[8];
    if (len < 9 + count * ENTRY) return;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < count; i++) {
        uint32_t s = last - (uint32_t)(count - 1 - i);
        uint32_t k = s % RING;
        if (s == 0) continue;
        memcpy(N.in[from][k], p + 9 + i * ENTRY, ENTRY);
        N.in_tag[from][k] = s + 1;
        N.in_match[from][k] = match;
    }
    LeaveCriticalSection(&g_cs);
    SetEvent(N.arrived);
}

static unsigned g_hash_matches;     /* checksums compared and equal, this session */

/* Compare every checksum this PC and another have for the same match and step, once each. */
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
                    fprintf(stderr, "[NETPLAY] desync in match %08X at step %u (local %08X, remote %08X)\n",
                            N.local_hash[i][0], N.local_hash[i][1], N.local_hash[i][2], N.remote_hash[j][2]);
                }
            } else if (++g_hash_matches % 10 == 0) {
                fprintf(stderr, "[NETPLAY] in step: %u checksums compared and equal (match %08X, step %u)\n",
                        g_hash_matches, N.local_hash[i][0], N.local_hash[i][1]);
            }
            N.remote_hash[j][1] = 0;    /* compared */
        }
}

/* Hub: this PC's ping for the current START, so every PC derives the same input delay. */
static void send_rtt(void)
{
    uint8_t p[7];
    int rtt = N.peer_rtt[N.pos] < 0 ? 0 : N.peer_rtt[N.pos];
    p[0] = P_RTT;
    put32(p + 1, N.start_seed);
    p[5] = (uint8_t)rtt;
    p[6] = (uint8_t)(rtt >> 8);
    send_data(p, 7);
    N.last_rtt_sent = GetTickCount64();
}

/* The delay every PC derives from the same pings: the slowest path between two players goes
 * through the relay, about half of each one's round trip, plus a step of margin. */
static int agreed_delay(void)
{
    int a = 0, b = 0, i, d;
    for (i = 0; i < N.players; i++) {
        int r = N.peer_rtt[i] < 0 ? 0 : N.peer_rtt[i];
        if (r > a) { b = a; a = r; } else if (r > b) b = r;
    }
    d = ((a + b) / 2 * 3 + 49) / 50 + 1;      /* ms to 1/60 s steps, rounded up, + 1 */
    return d < DELAY_MIN ? DELAY_MIN : d > DELAY_MAX ? DELAY_MAX : d;
}

static void on_data(int from, const uint8_t *p, int len)
{
    if (len < 1 || from < 0 || from >= MAXP || from == N.pos) return;
    switch (p[0]) {
    case P_GAMEHELLO:
        if (!N.lobby && !N.creating && len >= 7 && p[6] == LOCKSTEP_VERSION) {
            uint8_t ack = P_GAMEACK;
            N.session = get32(p + 1);
            N.delay = p[5] >= 1 && p[5] <= 12 ? p[5] : 3;
            send_data(&ack, 1);
            if (N.state != NETPLAY_CONNECTED) become_connected();
        }
        break;
    case P_GAMEACK:
        if (!N.lobby && N.creating && N.state == NETPLAY_SYNCING) {
            N.acked = 1;
            become_connected();
        }
        break;
    case P_LIVEPAD:
        if (len >= 5 + (int)sizeof(XBOX_GAMEPAD)) {
            uint32_t seq = get32(p + 1);
            EnterCriticalSection(&g_cs);
            if ((int32_t)(seq - N.remote_seq[from]) > 0) {
                static int seen, pressed;
                N.remote_seq[from] = seq;
                memcpy(&N.remote_pad[from], p + 5, sizeof(XBOX_GAMEPAD));
                N.remote_packet[from]++;
                if (!seen++)
                    fprintf(stderr, "[NETPLAY] the other player's controller is arriving (player %d)\n", from + 1);
                if (!pressed && (N.remote_pad[from].wButtons || N.remote_pad[from].bAnalogButtons[0] > 32)) {
                    pressed = 1;
                    fprintf(stderr, "[NETPLAY] first press from the other player's controller (buttons %04X)\n",
                            N.remote_pad[from].wButtons);
                }
            }
            LeaveCriticalSection(&g_cs);
        }
        break;
    case P_STEPS:
        store_steps(from, p + 1, len - 1);
        break;
    case P_RTT:
        if (N.lobby && N.match_live && len >= 7 && get32(p + 1) == N.start_seed) {
            int had = N.peer_rtt[from];
            N.peer_rtt[from] = p[5] | (p[6] << 8);
            SetEvent(N.arrived);
            if (N.delay_fixed && had < 0) send_rtt();   /* it may still be waiting for ours */
        }
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

/* ---- the hub's messages -------------------------------------------------------- */

typedef struct { const uint8_t *p; int n, i, bad; } Reader;

static int rd8(Reader *r)
{
    if (r->i >= r->n) { r->bad = 1; return 0; }
    return r->p[r->i++];
}

static void rdbytes(Reader *r, void *out, int count)
{
    if (r->i + count > r->n) { r->bad = 1; memset(out, 0, (size_t)count); return; }
    memcpy(out, r->p + r->i, (size_t)count);
    r->i += count;
}

static void rdstr(Reader *r, char *out, int size)
{
    int len = rd8(r), keep = len < size - 1 ? len : size - 1;
    if (r->i + len > r->n) { r->bad = 1; out[0] = 0; return; }
    memcpy(out, r->p + r->i, (size_t)keep);
    out[keep] = 0;
    r->i += len;
}

static void rdsettings(Reader *r, NetRoomSettings *s)
{
    memset(s, 0, sizeof(*s));
    rdstr(r, s->name, sizeof(s->name));
    s->is_public = rd8(r);
    s->mode = rd8(r);
    s->max = rd8(r);
    s->rounds = rd8(r);
    s->pool_count = rd8(r);
    if (s->pool_count > 32) { r->bad = 1; s->pool_count = 0; return; }
    rdbytes(r, s->pool, s->pool_count);
}

static int put_settings(uint8_t *b, const NetRoomSettings *s)
{
    int n = 0, len = (int)strlen(s->name), pool = s->pool_count;
    if (len > 24) len = 24;
    if (len == 0) { b[n++] = 4; memcpy(b + n, "Room", 4); n += 4; }
    else { b[n++] = (uint8_t)len; memcpy(b + n, s->name, (size_t)len); n += len; }
    b[n++] = s->is_public ? 1 : 0;
    b[n++] = (uint8_t)s->mode;
    b[n++] = (uint8_t)(s->max < 2 ? 2 : s->max > MAXP ? MAXP : s->max);
    b[n++] = (uint8_t)s->rounds;
    if (pool < 1) pool = 0;
    if (pool > 32) pool = 32;
    b[n++] = (uint8_t)pool;
    memcpy(b + n, s->pool, (size_t)pool); n += pool;
    return n;
}

static void on_rooms(const uint8_t *b, int len)
{
    Reader r = { b, len, 0, 0 };
    NetRoomInfo list[32];
    int count = rd8(&r), i;
    if (count > 32) count = 32;
    for (i = 0; i < count && !r.bad; i++) {
        rdbytes(&r, list[i].code, 6);
        list[i].code[6] = 0;
        rdstr(&r, list[i].name, sizeof(list[i].name));
        list[i].mode = rd8(&r);
        list[i].players = rd8(&r);
        list[i].max = rd8(&r);
        list[i].phase = rd8(&r);
    }
    if (r.bad) return;
    EnterCriticalSection(&g_cs);
    memcpy(N.rooms, list, sizeof(list[0]) * (size_t)count);
    N.room_count = count;
    N.rooms_version++;
    if (N.pend_len && (N.pend[5] == T_LIST || N.pend[5] == T_LEAVE)) answered();
    LeaveCriticalSection(&g_cs);
}

static void on_room(const uint8_t *b, int len)
{
    Reader r = { b, len, 0, 0 };
    NetRoom v;
    int i, count, gone = 0;
    memset(&v, 0, sizeof(v));
    rdbytes(&r, v.code, 6);
    v.code[6] = 0;
    v.you = rd8(&r);
    v.host = rd8(&r);
    v.phase = rd8(&r);
    rdsettings(&r, &v.settings);
    v.candidate_count = rd8(&r);
    if (v.candidate_count > 3) return;
    rdbytes(&r, v.candidates, v.candidate_count);
    for (i = 0; i < v.candidate_count; i++) v.votes[i] = rd8(&r);
    v.ms_left = rd8(&r);
    v.ms_left |= rd8(&r) << 8;
    count = rd8(&r);
    if (count > MAXP) return;
    v.player_count = count;
    for (i = 0; i < count; i++) {
        v.players[i].slot = rd8(&r);
        rdstr(&r, v.players[i].name, sizeof(v.players[i].name));
        v.players[i].ready = rd8(&r);
        v.players[i].fighter = rd8(&r);
        v.players[i].voted = rd8(&r);
        if (v.players[i].voted == 0xFF) v.players[i].voted = -1;
    }
    if (r.bad) return;
    v.in_room = 1;
    v.received = GetTickCount64();
    EnterCriticalSection(&g_cs);
    memcpy(v.error, N.roomv.error, sizeof(v.error));
    v.version = N.roomv.version + 1;
    N.roomv = v;
    N.slot = v.you;
    memcpy(N.room, v.code, 7);
    if (N.pend_len && N.pend[5] != T_LIST && N.pend[5] != T_LEAVE) answered();
    if (v.phase != 2) N.over_pending = N.start_this_play = 0;
    /* A player of the running match has left: the others cannot go on in lockstep. */
    if (N.match_live) {
        int k, j;
        for (k = 0; k < N.players; k++) {
            int found = 0;
            for (j = 0; j < count; j++) if (v.players[j].slot == N.start_slots[k]) found = 1;
            if (!found) gone = 1;
        }
    }
    LeaveCriticalSection(&g_cs);
    if (gone && N.lockstep) {
        InterlockedExchange(&N.lockstep, 0);
        assign_input(0);
        SetEvent(N.arrived);
        set_status("A player left the match; it finishes offline.");
    }
}

static void on_start(const uint8_t *b, int len)
{
    Reader r = { b, len, 0, 0 };
    AutoMatch m;
    uint32_t seed;
    int count, i, j, slots[MAXP], fighters[MAXP], pos = -1;
    if (len < 4) return;
    seed = get32(b);
    r.i = 4;
    memset(&m, 0, sizeof(m));
    m.type = rd8(&r);
    m.rules = rd8(&r);
    m.venue = rd8(&r);
    count = rd8(&r);
    if (count < 2 || count > MAXP) return;
    for (i = 0; i < count; i++) { slots[i] = rd8(&r); fighters[i] = rd8(&r); }
    if (r.bad) return;
    if (N.start_seed && seed == N.start_seed) { N.start_this_play = 1; return; }     /* a repeat */
    /* positions follow the room slots in order */
    for (i = 1; i < count; i++)
        for (j = i; j > 0 && slots[j - 1] > slots[j]; j--) {
            int t = slots[j]; slots[j] = slots[j - 1]; slots[j - 1] = t;
            t = fighters[j]; fighters[j] = fighters[j - 1]; fighters[j - 1] = t;
        }
    for (i = 0; i < MAXP; i++) N.pos_of_slot[i] = -1;
    for (i = 0; i < count; i++) {
        if (slots[i] < MAXP) N.pos_of_slot[slots[i]] = i;
        if (slots[i] == N.slot) pos = i;
        N.start_slots[i] = slots[i];
        m.fighter[i] = fighters[i];
        m.pad[i] = i;
    }
    if (pos < 0) return;
    m.count = count;
    {   /* the mode's free places go to CPU fighters, the same on every PC */
        const OnlineMode *mode = online_mode(m.type);
        const OnlineFighter *roster;
        int roster_count = online_fighters(&roster);
        for (i = count; mode && i < mode->fighters && i < MAXP; i++) {
            m.pad[i] = -1;
            m.fighter[i] = roster_count ? roster[mix32(seed ^ (0xC0FFEEu + (uint32_t)i)) % (uint32_t)roster_count].id : 55;
            m.count = i + 1;
        }
    }
    EnterCriticalSection(&g_cs);
    N.start_seed = seed;
    N.session = seed;
    N.players = count;
    N.fighters = m.count;
    N.pos = pos;
    N.delay = LOBBY_DELAY;
    N.delay_fixed = 0;
    for (i = 0; i < MAXP; i++) N.peer_rtt[i] = -1;
    {   /* this PC's ping as of now, with the worst of the last few as the jitter margin */
        int k, worst = N.rtt_ms > 0 ? N.rtt_ms : 0;
        for (k = 0; k < 5; k++) if (N.rtt_recent[k] > worst) worst = N.rtt_recent[k];
        N.peer_rtt[pos] = worst > 2000 ? 2000 : worst;
    }
    N.match_count = 0;
    N.match_live = 1;
    N.match_seen = 0;
    N.over_pending = 0;
    N.start_this_play = 1;
    LeaveCriticalSection(&g_cs);
    reset_remote_pads();
    fprintf(stderr, "[NETPLAY] START: seed %08X, type %d, rules %d, venue %d, %d players, this PC is player %d\n",
            seed, m.type, m.rules, m.venue, count, pos + 1);
    become_connected();
    automatch_request(&m);
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
            if (N.lobby) {
                if (N.state == NETPLAY_CONNECTING) {
                    InterlockedExchange(&N.state, NETPLAY_LOBBY);
                    set_status("In the lobby of %s.", N.server_text);
                    netplay_lobby_list(0xFF);
                }
                break;
            }
            N.slot = b[14] ? 1 : 0;
            N.pos = N.slot;
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
        if (len >= 8 && !N.lobby) {
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
        if (len >= 7) {
            int from = N.lobby ? (b[6] < MAXP ? N.pos_of_slot[b[6]] : -1) : (b[6] ? 1 : 0);
            on_data(from, b + 7, len - 7);
        }
        break;
    case T_PONG:
        if (len >= 10 && get32(b + 6) == N.ping_nonce)
        {
            N.rtt_ms = (int)(GetTickCount64() - N.last_ping);
            N.rtt_recent[N.rtt_next] = N.rtt_ms;
            N.rtt_next = (N.rtt_next + 1) % 5;
        }
        break;
    case T_ROOMS:
        if (N.lobby) on_rooms(b + 6, len - 6);
        break;
    case T_ROOM:
        if (N.lobby) on_room(b + 6, len - 6);
        break;
    case T_START:
        if (N.lobby) on_start(b + 6, len - 6);
        break;
    case T_ERROR:
        if (len >= 7) {
            static const char *const why[] = { "", "No room with that code.", "That room is full.",
                "The other player has a different version of the game.", "That name is not allowed.",
                "The server is full.", "Too many attempts; wait a minute.", "The server lost this session.",
                "Only the room's host can do that, and not while voting or playing.",
                "Not possible here: not in a room, or already in one." };
            int code = b[6];
            const char *text = code >= 1 && code <= 9 ? why[code] : "The server refused the request.";
            if (N.lobby && (code == 1 || code == 2 || code == 3 || code == 8 || code == 9)) {
                /* refused, but the session goes on */
                EnterCriticalSection(&g_cs);
                answered();
                snprintf(N.roomv.error, sizeof(N.roomv.error), "%s", text);
                N.roomv.version++;
                LeaveCriticalSection(&g_cs);
                fprintf(stderr, "[NETPLAY] refused: %s\n", text);
                break;
            }
            if (N.lobby && code == 7) {             /* the server restarted: enter again */
                N.have_token = 0;
                N.started = GetTickCount64();
                memset(&N.roomv, 0, sizeof(N.roomv));
                InterlockedExchange(&N.state, NETPLAY_CONNECTING);
                set_status("Reconnecting to %s...", N.server_text);
                break;
            }
            InterlockedExchange(&N.lockstep, 0);
            InterlockedExchange(&N.state, NETPLAY_FAILED);
            set_status("%s", text);
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
                if (N.lobby)
                    set_status("No answer from the server %s. It may be down, or an older server without the "
                               "online hub (play by code still works there).", N.server_text);
                else
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
                assign_input(0);
                InterlockedExchange(&N.state, NETPLAY_FAILED);
                set_status("The connection to the server was lost.");
            }
        }
        if (N.pend_len && now - N.pend_sent >= RETRY_MS) {
            uint8_t again[sizeof(N.pend)];
            int n = 0;
            EnterCriticalSection(&g_cs);
            if (N.pend_len && N.pend_tries >= RETRIES) {
                answered();
                snprintf(N.roomv.error, sizeof(N.roomv.error), "The server did not answer; try again.");
                N.roomv.version++;
            } else if (N.pend_len) {
                memcpy(again, N.pend, (size_t)N.pend_len);
                n = N.pend_len;
                N.pend_tries++;
                N.pend_sent = now;
            }
            LeaveCriticalSection(&g_cs);
            if (n) send_raw(again, n);
        }
        /* Hub: our ping for this START, until the match is under way. */
        if (N.lobby && N.match_live && !N.match_seen && now - N.last_rtt_sent >= 100)
            send_rtt();
        /* The room is playing but this PC never got its START: a VOTE makes the server send it again. */
        if (N.lobby && N.roomv.in_room && N.roomv.phase == 2 && !N.start_this_play && !N.over_pending &&
            now - N.last_recover >= 500) {
            uint8_t vb[6 + 8 + 1];
            int n = header(vb, T_VOTE);
            memcpy(vb + n, N.token, 8); n += 8;
            vb[n++] = 0;
            send_raw(vb, n);
            N.last_recover = now;
        }
        if (!N.lobby && N.state == NETPLAY_SYNCING && N.creating && now - N.hello_sent >= 300) {
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

static int start(const char *server, const char *name, int creating, const char *room, int delay, int lobby)
{
    DWORD timeout = 20;
    uint32_t h1 = 2166136261u, h2;
    const char *v;
    int i;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    netplay_stop();
    if (!server || !*server) { InterlockedExchange(&N.state, NETPLAY_FAILED); set_status("Enter the server address first."); return -1; }
    snprintf(N.server_text, sizeof(N.server_text), "%s", server);
    snprintf(N.name, sizeof(N.name), "%s", name && *name ? name : "Player");
    N.creating = creating;
    N.lobby = lobby;
    memset(N.room, 0, sizeof(N.room));
    if (!lobby && !creating) {
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
    memset(N.rtt_recent, 0, sizeof(N.rtt_recent));
    N.local_seq = 0;
    N.players = N.fighters = 2;
    N.pos = 0;
    for (i = 0; i < MAXP; i++) N.pos_of_slot[i] = i < 2 ? i : -1;
    reset_remote_pads();
    EnterCriticalSection(&g_cs);
    memset(N.in_tag, 0, sizeof(N.in_tag));
    memset(&N.roomv, 0, sizeof(N.roomv));
    N.room_count = 0;
    N.pend_len = 0;
    N.match_live = N.match_seen = N.over_pending = N.start_this_play = 0;
    N.start_seed = 0;
    N.match_count = 0;
    LeaveCriticalSection(&g_cs);
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
    return start(server, name, 1, NULL, input_delay, 0);
}

int netplay_join(const char *server, const char *name, const char *room)
{
    return start(server, name, 0, room, 3, 0);
}

int netplay_lobby(const char *server, const char *name)
{
    return start(server, name, 0, NULL, LOBBY_DELAY, 1);
}

void netplay_stop(void)
{
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    InterlockedExchange(&N.lockstep, 0);
    assign_input(0);
    if (N.thread) {
        N.stop = 1;
        WaitForSingleObject(N.thread, 2000);
        CloseHandle(N.thread);
        N.thread = NULL;
    }
    if (N.sock != INVALID_SOCKET) { closesocket(N.sock); N.sock = INVALID_SOCKET; }
    N.have_token = 0;
    N.lobby = 0;
    N.match_live = N.match_seen = 0;
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
    else if (N.state == NETPLAY_LOBBY && N.rtt_ms >= 0)
        snprintf(text, (size_t)size, "%s  Ping %d ms.", N.status, N.rtt_ms);
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

/* ---- the hub, for the launcher and overlay ------------------------------------ */

int netplay_in_lobby(void) { return N.lobby; }

void netplay_lobby_list(int mode)
{
    uint8_t m = (uint8_t)mode;
    request(T_LIST, &m, 1);
}

int netplay_lobby_rooms(NetRoomInfo *out, int max, unsigned *version)
{
    int n;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    n = N.room_count < max ? N.room_count : max;
    if (n > 0) memcpy(out, N.rooms, sizeof(out[0]) * (size_t)n);
    if (version) *version = N.rooms_version;
    LeaveCriticalSection(&g_cs);
    return n;
}

void netplay_lobby_create(const NetRoomSettings *settings)
{
    uint8_t b[64];
    request(T_CREATE, b, put_settings(b, settings));
}

void netplay_lobby_join(const char *code)
{
    uint8_t b[6];
    int i;
    for (i = 0; i < 6; i++) b[i] = (uint8_t)toupper((unsigned char)(code && code[i] ? code[i] : 0));
    request(T_JOIN, b, 6);
}

void netplay_lobby_quick(int mode, int max)
{
    uint8_t b[2] = { (uint8_t)mode, (uint8_t)(max < 2 ? 2 : max > MAXP ? MAXP : max) };
    request(T_QUICK, b, 2);
}

void netplay_lobby_settings(const NetRoomSettings *settings)
{
    uint8_t b[64];
    request(T_SETTINGS, b, put_settings(b, settings));
}

void netplay_lobby_ready(int ready, int fighter)
{
    uint8_t b[2] = { (uint8_t)(ready ? 1 : 0), (uint8_t)fighter };
    request(T_READY, b, 2);
}

void netplay_lobby_vote(int choice)
{
    uint8_t b = (uint8_t)choice;
    request(T_VOTE, &b, 1);
}

void netplay_lobby_leave(void)
{
    EnterCriticalSection(&g_cs);
    memset(&N.roomv, 0, sizeof(N.roomv));
    N.roomv.version = (unsigned)GetTickCount64();
    N.room[0] = 0;
    LeaveCriticalSection(&g_cs);
    request(T_LEAVE, NULL, 0);
}

void netplay_lobby_room(NetRoom *out)
{
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    *out = N.roomv;
    LeaveCriticalSection(&g_cs);
}

void netplay_sync_point(void)
{
    if (!N.lockstep) return;
    if (N.lobby && N.match_live && !N.delay_fixed) {
        /* Every player's ping for this START, then the delay they all derive from them. */
        ULONGLONG t0 = GetTickCount64();
        int known, i;
        for (;;) {
            known = 1;
            for (i = 0; i < N.players; i++) if (N.peer_rtt[i] < 0) known = 0;
            if (known || GetTickCount64() - t0 > RTT_WAIT_MS || !N.lockstep) break;
            WaitForSingleObject(N.arrived, 20);
        }
        N.delay = agreed_delay();
        N.delay_fixed = 1;
        send_rtt();
        fprintf(stderr, "[NETPLAY] input delay %d steps (pings %d/%d/%d/%d ms%s)\n", N.delay,
                N.peer_rtt[0], N.peer_rtt[1], N.peer_rtt[2], N.peer_rtt[3], known ? "" : ", some missing");
    }
    EnterCriticalSection(&g_cs);
    memset(N.seeds, 0, sizeof(N.seeds));
    LeaveCriticalSection(&g_cs);
    fprintf(stderr, "[NETPLAY] sync point: setting up the room's match\n");
}

void netplay_on_main_menu(void)
{
    if (N.lobby && N.match_live && N.match_seen)
        end_hub_match("Match over; back in the room.");
}

/* Unattended sessions for tests: RECOMP_NETPLAY=host or join, RECOMP_NETPLAY_SERVER, and
 * RECOMP_NETPLAY_CODE_FILE, where the host writes its room code once it has one and the
 * guest waits for it (up to a minute). RECOMP_NETPLAY=quick enters the hub, quick-matches
 * (RECOMP_NETPLAY_QUICK="mode,players", default "0,2") and readies with RECOMP_NETPLAY_FIGHTER
 * (default 55), voting for the first venue; =lobby only
 * enters the hub. */
static DWORD WINAPI autostart_thread(LPVOID unused)
{
    const char *mode = getenv("RECOMP_NETPLAY"), *server = getenv("RECOMP_NETPLAY_SERVER");
    const char *file = getenv("RECOMP_NETPLAY_CODE_FILE");
    int i;
    (void)unused;
    if (!strcmp(mode, "lobby")) {
        netplay_lobby(server, "Test player");
        return 0;
    }
    if (!strcmp(mode, "quick")) {
        const char *f = getenv("RECOMP_NETPLAY_FIGHTER");
        int fighter = f && *f ? atoi(f) : 55, readied = 0;
        unsigned voted_in = 0;
        netplay_lobby(server, "Test player");
        for (i = 0; i < 600 && N.state == NETPLAY_CONNECTING; i++) Sleep(100);
        {   /* RECOMP_NETPLAY_QUICK="mode,players", default "0,2" (One on One) */
            const char *q = getenv("RECOMP_NETPLAY_QUICK");
            int qmode = 0, qmax = 2;
            if (q && *q) sscanf(q, "%d,%d", &qmode, &qmax);
            netplay_lobby_quick(qmode, qmax);
        }
        while (!N.stop && N.state != NETPLAY_FAILED && N.state != NETPLAY_OFF) {
            NetRoom r;
            netplay_lobby_room(&r);
            if (r.in_room && r.phase == 0 && !readied && !N.match_live) {
                netplay_lobby_ready(1, fighter);
                readied = 1;
            }
            if (r.in_room && r.phase == 1 && voted_in != r.version) {
                int me;
                for (me = 0; me < r.player_count; me++)
                    if (r.players[me].slot == r.you && r.players[me].voted < 0) { netplay_lobby_vote(0); break; }
                voted_in = r.version;
            }
            if (r.phase == 2) readied = 0;
            Sleep(100);
        }
        return 0;
    }
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
    static LONG once;
    HANDLE t;
    if (InterlockedExchange(&once, 1)) return;
    if (!mode || !server || (strcmp(mode, "host") && strcmp(mode, "join") && strcmp(mode, "quick") && strcmp(mode, "lobby"))) return;
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
    for (slot = 0; slot < (N.fighters > N.players ? N.fighters : N.players); slot++)
        for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
            h = (h ^ MEM32(ACTORS + (uint32_t)slot * STRIDE + fields[i])) * 16777619u;
    return h;
}

void netplay_on_match_start(void)
{
    if (!N.lockstep) return;
    EnterCriticalSection(&g_cs);
    N.match_count++;
    N.match = mix32(N.session ^ (N.match_count * 0x9E3779B9u));   /* the same on every PC, new per match */
    N.step = 0;
    memset(N.latest_local, 0, sizeof(N.latest_local));
    N.desync = 0;
    if (N.match_live) N.match_seen = 1;
    LeaveCriticalSection(&g_cs);
    fprintf(stderr, "[NETPLAY] match %08X starts in lockstep (%d players, delay %d)\n", N.match, N.players, N.delay);
}

void netplay_on_pad_pass(void) {}

static void send_steps(uint32_t last)
{
    uint8_t p[1 + 9 + REDUNDANT * ENTRY];
    uint32_t first = last >= REDUNDANT ? last - REDUNDANT + 1 : 1, s;
    int count = 0, local = N.pos;
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
    ULONGLONG waited_since, last_send;
    if (!N.lockstep) return;
    s = ++N.step;
    target = s + (uint32_t)N.delay;
    k = target % RING;
    EnterCriticalSection(&g_cs);
    memcpy(N.in[N.pos][k], N.latest_local, ENTRY);
    N.in_tag[N.pos][k] = target + 1;
    N.in_match[N.pos][k] = N.match;
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

    /* Wait for every other player's entry for this step. */
    if (s <= (uint32_t)N.delay) return;
    k = s % RING;
    waited_since = last_send = GetTickCount64();
    for (;;) {
        int ready = 1, i;
        EnterCriticalSection(&g_cs);
        for (i = 0; i < N.players; i++)
            if (i != N.pos && !(N.in_tag[i][k] == s + 1 && N.in_match[i][k] == N.match)) ready = 0;
        LeaveCriticalSection(&g_cs);
        if (ready || !N.lockstep) break;
        WaitForSingleObject(N.arrived, 4);
        {
            ULONGLONG now = GetTickCount64();
            if (now - last_send >= 50) { send_steps(target); last_send = now; }   /* the others may be waiting for ours */
            if (now - waited_since > (s <= (uint32_t)N.delay + 1 ? START_LIMIT_MS : STALL_LIMIT_MS)) {
                InterlockedExchange(&N.lockstep, 0);
                set_status("A player stopped answering during the fight; continuing offline.");
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
    if ((int)slot >= N.players) return;
    base = PAD_TABLE + slot * 0x20u;
    /* This PC's player: a fresh poll (no marker) is the input to send next. */
    if ((int)slot == N.pos && MEM32(base + 28) != MARK) {
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
        if (N.in_tag[slot][k] == s + 1 && N.in_match[slot][k] == N.match)
            memcpy(entry, N.in[slot][k], ENTRY);
        LeaveCriticalSection(&g_cs);
    }
    {   /* RECOMP_NETPLAY_PADLOG=n: the first n reads, polled and injected, with the slot's bindings */
        static int budget = -1;
        if (budget < 0) budget = getenv("RECOMP_NETPLAY_PADLOG") ? atoi(getenv("RECOMP_NETPLAY_PADLOG")) : 0;
        if (budget > 0 && (s % 30 == 5 || s < 8)) {
            uint32_t bind = 0x003B90C8u + slot * 0x18u;
            budget--;
            fprintf(stderr, "[PADLOG] step %u slot %u polled %08X %08X %08X w7 %08X | injected %08X %08X %08X | "
                    "mask %08X bind %08X %08X %08X %08X %08X %08X\n", s, slot,
                    MEM32(base), MEM32(base + 4), MEM32(base + 8), MEM32(base + 28),
                    get32(entry), get32(entry + 4), get32(entry + 8), MEM32(0x003B8FC8u + slot * 4u),
                    MEM32(bind), MEM32(bind + 4), MEM32(bind + 8), MEM32(bind + 12), MEM32(bind + 16), MEM32(bind + 20));
        }
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

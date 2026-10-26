/* The online relays and the choice between them (build 30): Europe (play.defjam.online) and North
 * America (na.defjam.online), both port 27777.
 *
 * Each relay answers STATUS (docs/netplay-protocol.md, "Relay status") without a session: a
 * background thread asks both every 15 s while the online screens or the launcher's settings show,
 * and keeps each one's round trip and player counts. The setting [online] server is auto, eu or
 * na (auto: the relay with the lower ping, decided once per session); anything else is a custom
 * address (tests, a private relay) used as it is. Accounts, statistics and updates stay on the
 * Europe server whatever is chosen.
 *
 * Tests: RECOMP_NETPLAY_SERVER_EU / RECOMP_NETPLAY_SERVER_NA replace the two relays' addresses
 * (e.g. two local relays, one started with -test-lag). */
#ifndef DEFJAM_NETPLAY_SERVERS_H
#define DEFJAM_NETPLAY_SERVERS_H

#ifdef __cplusplus
extern "C" {
#endif

#define NETSERVER_EU    0
#define NETSERVER_NA    1
#define NETSERVER_COUNT 2

/* What [online] server says. */
enum NetServerChoice { NETCHOICE_AUTO = 0, NETCHOICE_EU, NETCHOICE_NA, NETCHOICE_CUSTOM };

enum NetServerState {
    NETSERVER_UNKNOWN = 0,  /* not asked yet, or the first answers are still on their way */
    NETSERVER_UP,           /* answered STATUS: ping and counts are valid */
    NETSERVER_OFFLINE       /* no answer to the last round (down, blocked, or a relay older than STATUS) */
};

/* STATUS flags (the relay's answer) */
#define NETSERVER_RANKED   1
#define NETSERVER_KOTH     2
#define NETSERVER_ROLLBACK 4

typedef struct NetServerInfo {
    const char *id;         /* "eu", "na" */
    const char *name;       /* "Europe", "North America" */
    const char *short_name; /* "EU", "NA" */
    char address[128];      /* host:port */
    int state;              /* NETSERVER_* */
    int ping_ms;            /* the best round trip of the last round, -1 unknown */
    int online, rooms;      /* everyone connected, open rooms */
    int waiting, idle;      /* this build's players waiting in public rooms / in the lobby */
    int flags;              /* NETSERVER_RANKED | ... */
    int version;            /* protocol version byte, 0 unknown */
} NetServerInfo;

/* Ask both relays now if the last round is older than 10 s (cheap to call every frame). */
void netservers_probe(void);
/* Copies the relays' state; returns NETSERVER_COUNT. */
int netservers_get(NetServerInfo *out, int max);
/* 1 while a round runs and nothing has come back yet from any relay. */
int netservers_probing(void);

enum NetServerChoice netservers_choice(void);
/* Stores the choice ("auto", "eu", "na") and drops any switch made this session. */
void netservers_set_choice(enum NetServerChoice c);
/* This session only, beating the setting: the quick-match hint's "search there", the ranked switch. */
void netservers_use(int index);

/* The relay to connect to now (NETSERVER_EU / _NA), -1 while Automatic still waits for the first
 * answers (at most about 2.5 s after the first probe), -2 for a custom address. The automatic
 * choice is made once and kept for the session. */
int netservers_current(void);
/* Before a new connection (not in a room or match): Automatic moves to the other relay when it is
 * clearly better (40 ms and 30 % lower) or the current one is offline. Returns the relay to use. */
int netservers_reconsider(void);
/* The address for netplay_lobby(): the chosen relay's, or the custom one. NULL while undecided. */
const char *netservers_address(char *buf, int size);
/* Tests (RECOMP_NETPLAY=quick with RECOMP_NETPLAY_SERVER=auto|eu|na): like netservers_address,
 * but waits up to wait_ms for Automatic's first answers. */
const char *netservers_address_wait(char *buf, int size, int wait_ms);
/* Moves the hub session to the relay chosen now (after netservers_set_choice or netservers_use):
 * nothing when it is already there; otherwise the old session ends (BYE, which also leaves its
 * room) and the hub connects to the other relay as `name`. Returns 1 when it reconnected, 0 when
 * nothing had to change, -1 while Automatic is undecided. */
int netservers_reconnect(const char *name);
/* The relay `address` is (by its host and port), -1 if neither. */
int netservers_index_of(const char *address);

#ifdef __cplusplus
}
#endif

#endif

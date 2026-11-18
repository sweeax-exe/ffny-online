/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The server choice and the staged quick-match rules; see netplay_servers_rules.h. */
#include "netplay_servers_rules.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* The relays, in the wire's order (netplay_servers_rules.h): a new relay is a new last row. */
/* Build 91: US East (New Jersey), Miami and Asia (Singapore) run on new machines with new host names. The fallback
 * address is used only when the name does not resolve ("" = none: the name must resolve).
 *
 * Adding a relay (e.g. Dallas, "dfw"): one row at the END of this table (never in between: the index is the wire's
 * order), NETSERVER_COUNT + 1 and a NETSERVER_<ID> index in netplay_servers_rules.h, and the same name at the end of
 * the server's RelayNames (relay/relays.go). Choices, the picker, auto/all, quick-match widening, the pings trailers
 * and the chat regions follow the table; tests set RECOMP_NETPLAY_SERVER_<ID> (tools/defjam-mcp/djlib/machine.py,
 * scripts/test-*-launcher.py, scripts/netplay-hub-test.ps1). */
static const NetRelayDef k_relays[] = {
    { "eu", "Europe", "EU", "play.defjam.online:27777", "49.12.78.32", "RECOMP_NETPLAY_SERVER_EU", "eu",
      "europe" },
    /* build 38: the North American relay of builds 30..37 is US East; its id stays "na". Build 91: New Jersey on
     * nj (Ashburn's na.defjam.online closes with build 90; its saved host name still means "na") */
    { "na", "US East", "US EAST", "nj.defjam.online:27777", "208.167.239.167", "RECOMP_NETPLAY_SERVER_NA",
      "us-east", "north_america|north america|northamerica|us|east|us_east|us east|useast|new jersey|newjersey|nj|"
      "na.defjam.online|na.defjam.online:27777" },
    /* build 89: Singapore takes US West's slot; build 91: a new machine, sg2 */
    { "sg", "Asia (Singapore)", "ASIA", "sg2.defjam.online:27777", "139.180.129.88", "RECOMP_NETPLAY_SERVER_SG",
      "sg", "asia|singapore|sg.defjam.online|sg.defjam.online:27777" },
    /* build 91 */
    { "sa", "South America", "S.AMERICA", "sa.defjam.online:27777", "216.238.122.210", "RECOMP_NETPLAY_SERVER_SA",
      "sa", "southamerica|south america|south_america|south-america|brazil|brasil|saopaulo|sao paulo|sao_paulo" },
    /* build 91: US South and the Caribbean */
    { "mia", "US South (Miami)", "MIAMI", "na2.defjam.online:27777", "140.82.31.133",
      "RECOMP_NETPLAY_SERVER_MIA", "mia", "miami|us south|us_south|ussouth|caribbean|florida" },
    /* build 91: Dallas and Los Angeles (US West again; a saved "west" of builds 38..88 means it) */
    { "dfw", "US Central (Dallas)", "DALLAS", "dfw.defjam.online:27777", "45.76.237.224",
      "RECOMP_NETPLAY_SERVER_DFW", "dfw", "dallas|texas|us central|us_central|uscentral|central" },
    { "lax", "US West (Los Angeles)", "LA", "lax.defjam.online:27777", "45.32.95.226",
      "RECOMP_NETPLAY_SERVER_LAX", "lax", "los angeles|losangeles|la|california|west|us_west|us west|uswest|"
      "west.defjam.online|west.defjam.online:27777" },
};

typedef char netservers_table_matches_count[sizeof(k_relays) / sizeof(k_relays[0]) == NETSERVER_COUNT &&
                                            NETSERVER_COUNT <= NETSERVER_MAX && NETSERVER_COUNT >= NETSERVER_LEGACY ? 1 : -1];

const NetRelayDef *netservers_def(int index)
{
    return index >= 0 && index < NETSERVER_COUNT ? &k_relays[index] : NULL;
}

static int same_text(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return !*a && !*b;
}

int netservers_index_of_id(const char *id)
{
    int i;
    if (!id) return -1;
    for (i = 0; i < NETSERVER_COUNT; i++)
        if (same_text(id, k_relays[i].id)) return i;
    return -1;
}

int netservers_index_of_social(const char *region)
{
    int i;
    if (!region) return -1;
    for (i = 0; i < NETSERVER_COUNT; i++)
        if (!strcmp(region, k_relays[i].social)) return i;
    return -1;
}

/* The host part of a table address ("play.defjam.online"), in out. */
static void host_of(const char *address, char *out, size_t size)
{
    const char *colon = strrchr(address, ':');
    size_t n = colon ? (size_t)(colon - address) : strlen(address);
    if (n >= size) n = size - 1;
    memcpy(out, address, n);
    out[n] = 0;
}

const char *netservers_known_ip(const char *host)
{
    char h[128];
    int i;
    if (!host) return NULL;
    for (i = 0; i < NETSERVER_COUNT; i++) {
        host_of(k_relays[i].address, h, sizeof(h));
        if (same_text(host, h)) return k_relays[i].fallback_ip[0] ? k_relays[i].fallback_ip : NULL;
    }
    return NULL;
}

/* Whether `low` is one of the '|'-separated words. */
static int is_word(const char *words, const char *low)
{
    size_t n = strlen(low);
    while (words && *words) {
        const char *bar = strchr(words, '|');
        size_t len = bar ? (size_t)(bar - words) : strlen(words);
        if (len == n && !memcmp(words, low, n)) return 1;
        if (!bar) break;
        words = bar + 1;
    }
    return 0;
}

enum NetServerChoice netservers_parse_choice(const char *s)
{
    static const struct { const char *text; enum NetServerChoice c; } k_names[] = {
        { "", NETCHOICE_AUTO }, { "auto", NETCHOICE_AUTO }, { "automatic", NETCHOICE_AUTO },
        /* the first server's name, saved by every build before 30: now the nearest */
        { "play.defjam.online", NETCHOICE_AUTO }, { "play.defjam.online:27777", NETCHOICE_AUTO },
        /* US West (builds 38..88, Hillsboro) closed in build 89; build 91's Los Angeles takes its words (lax) */
        /* "both" (builds 31..37) is every relay now */
        { "all", NETCHOICE_ALL }, { "all servers", NETCHOICE_ALL }, { "all_servers", NETCHOICE_ALL },
        { "both", NETCHOICE_ALL }, { "both servers", NETCHOICE_ALL }, { "both_servers", NETCHOICE_ALL },
    };
    char low[128], host[128];
    size_t i;
    int r;
    if (!s) return NETCHOICE_AUTO;
    while (*s == ' ') s++;
    for (i = 0; s[i] && i < sizeof(low) - 1; i++) low[i] = (char)tolower((unsigned char)s[i]);
    low[i] = 0;
    while (i && low[i - 1] == ' ') low[--i] = 0;
    for (i = 0; i < sizeof(k_names) / sizeof(k_names[0]); i++)
        if (!strcmp(low, k_names[i].text)) return k_names[i].c;
    for (r = 0; r < NETSERVER_COUNT; r++) {
        host_of(k_relays[r].address, host, sizeof(host));
        if (!strcmp(low, k_relays[r].id) || is_word(k_relays[r].words, low) || !strcmp(low, host) ||
            same_text(low, k_relays[r].address))
            return netservers_relay_choice(r);
    }
    return NETCHOICE_CUSTOM;
}

const char *netservers_choice_text(enum NetServerChoice c)
{
    int r = netservers_choice_relay(c);
    if (r >= 0) return k_relays[r].id;
    return c == NETCHOICE_AUTO ? "auto" : c == NETCHOICE_ALL ? "all" : NULL;
}

int netservers_choice_relay(enum NetServerChoice c)
{
    int r = c == NETCHOICE_EU ? 0 : c == NETCHOICE_NA ? 1 : c == NETCHOICE_SG ? 2 :
            c >= NETCHOICE_RELAY3 && c < NETCHOICE_END ? NETSERVER_LEGACY + (int)(c - NETCHOICE_RELAY3) : -1;
    return r < NETSERVER_COUNT ? r : -1;
}

enum NetServerChoice netservers_relay_choice(int index)
{
    if (index == 1) return NETCHOICE_NA;
    if (index == 2) return NETCHOICE_SG;
    if (index >= NETSERVER_LEGACY && index < NETSERVER_COUNT)
        return (enum NetServerChoice)(NETCHOICE_RELAY3 + (index - NETSERVER_LEGACY));
    return NETCHOICE_EU;
}

int netservers_put_more_pings(unsigned char *out, const int *pings, int count)
{
    int i, n = 0, extra = count - NETSERVER_LEGACY;
    if (extra < 0) extra = 0;
    if (extra > NETSERVER_MAX - NETSERVER_LEGACY) extra = NETSERVER_MAX - NETSERVER_LEGACY;
    out[n++] = (unsigned char)extra;
    for (i = 0; i < extra; i++) {
        int p = pings ? pings[NETSERVER_LEGACY + i] : 0;
        if (p < 0) p = 0;
        if (p > 0xFFFF) p = 0xFFFF;
        out[n++] = (unsigned char)p;
        out[n++] = (unsigned char)(p >> 8);
    }
    return n;
}

int netservers_get_more_pings(const unsigned char *b, int len, int rooms, int *pings)
{
    int extra, k, j, at = 1;
    if (!b || len <= 0) return 0;
    extra = b[0];
    if (rooms < 0 || 1 + 2 * extra * rooms > len) return -1;
    for (k = 0; k < rooms; k++)
        for (j = 0; j < extra; j++, at += 2)
            if (NETSERVER_LEGACY + j < NETSERVER_COUNT)
                pings[k * NETSERVER_COUNT + NETSERVER_LEGACY + j] = b[at] | b[at + 1] << 8;
    return at;
}

int netservers_partner(int relay)
{
    (void)relay;   /* build 89: no two relays are close to each other any more */
    return -1;
}

unsigned netservers_scope(enum NetServerChoice c, int cur, int search_s, unsigned up)
{
    unsigned mask;
    (void)search_s;
    if (c == NETCHOICE_CUSTOM || cur < 0 || cur >= NETSERVER_COUNT) return 0;
    mask = 1u << cur;
    up &= (1u << NETSERVER_COUNT) - 1;
    return mask | up;   /* build 91: one global pool from the first second, whatever the choice */
}

int netservers_sum_move(int my_relay, int my_age, const int *mine, int their_relay, int their_age, const int *theirs)
{
    int here, there, here_max, there_max;
    if (!mine || !theirs || my_relay == their_relay || my_relay < 0 || my_relay >= NETSERVER_COUNT || their_relay < 0 ||
        their_relay >= NETSERVER_COUNT)
        return -1;
    if ((my_relay >= NETSERVER_LEGACY || their_relay >= NETSERVER_LEGACY) && mine[my_relay] > 0 && theirs[their_relay] > 0) {
        /* build 91: a relay builds before 91 do not know; who has no ping to the other's relay cannot go */
        if (mine[their_relay] <= 0 && theirs[my_relay] > 0) return 0;
        if (theirs[my_relay] <= 0 && mine[their_relay] > 0) return 1;
    }
    if (mine[my_relay] <= 0 || mine[their_relay] <= 0 || theirs[my_relay] <= 0 || theirs[their_relay] <= 0) return -1;
    here = mine[my_relay] + theirs[my_relay];
    there = mine[their_relay] + theirs[their_relay];
    here_max = mine[my_relay] > theirs[my_relay] ? mine[my_relay] : theirs[my_relay];
    there_max = mine[their_relay] > theirs[their_relay] ? mine[their_relay] : theirs[their_relay];
    if (there < here || (there == here && there_max < here_max)) return 1;
    if (here < there || (here == there && here_max < there_max))
        return their_age >= QUICK_ALL_S + QUICK_SUM_WAIT_S && my_age >= QUICK_SUM_WAIT_S ? -1 : 0;
    return -1;
}

int netservers_pool_limit(int age_s)
{
    return age_s >= 40 ? 0 : age_s >= 20 ? 250 : age_s >= 10 ? 180 : 120;
}

int netservers_pair_relay(const NetQuickSeat *a, const NetQuickSeat *b, int limit, int *sum, int *worse)
{
    int r, best = -1, best_sum = 0, best_worse = 0;
    for (r = 0; r < NETSERVER_COUNT; r++) {
        int pa = a->pings[r], pb = b->pings[r], s, w;
        if (pa <= 0 || pb <= 0) continue;
        s = pa + pb;
        w = pa > pb ? pa : pb;
        if (limit > 0 && w > limit) continue;
        if (best < 0 || s < best_sum || (s == best_sum && w < best_worse)) { best = r; best_sum = s; best_worse = w; }
    }
    if (sum) *sum = best_sum;
    if (worse) *worse = best_worse;
    return best;
}

/* the pair key's codes, the smaller first */
static void pair_codes(const NetQuickSeat *a, const NetQuickSeat *b, const char **lo, const char **hi)
{
    if (strcmp(a->code, b->code) <= 0) { *lo = a->code; *hi = b->code; } else { *lo = b->code; *hi = a->code; }
}

/* the pool with every wait `shift` seconds longer (0 at least): netservers_pool_steady's views */
static void pool_at(const NetQuickSeat *seats, int n, int *partner, int *relay, int shift)
{
    int i, j;
    if (n > QUICK_POOL_MAX) n = QUICK_POOL_MAX;
    for (i = 0; i < n; i++) partner[i] = relay[i] = -1;
    for (;;) {      /* the best free pair, then again: at most n / 2 rounds of n * n */
        int bi = -1, bj = -1, br = -1, bs = 0, bw = 0;
        const char *blo = "", *bhi = "";
        for (i = 0; i < n; i++) {
            if (partner[i] >= 0) continue;
            for (j = i + 1; j < n; j++) {
                int s, w, r, age;
                const char *lo, *hi;
                if (partner[j] >= 0 || seats[i].relay == seats[j].relay) continue;
                age = (seats[i].age > seats[j].age ? seats[i].age : seats[j].age) + shift;
                if (age < 0) age = 0;
                r = netservers_pair_relay(&seats[i], &seats[j], netservers_pool_limit(age), &s, &w);
                if (r < 0) continue;
                pair_codes(&seats[i], &seats[j], &lo, &hi);
                if (bi < 0 || s < bs || (s == bs && (w < bw || (w == bw && (strcmp(lo, blo) < 0 ||
                    (!strcmp(lo, blo) && strcmp(hi, bhi) < 0)))))) {
                    bi = i; bj = j; br = r; bs = s; bw = w; blo = lo; bhi = hi;
                }
            }
        }
        if (bi < 0) return;
        partner[bi] = bj; partner[bj] = bi;
        relay[bi] = relay[bj] = br;
    }
}

void netservers_pool(const NetQuickSeat *seats, int n, int *partner, int *relay)
{
    pool_at(seats, n, partner, relay, 0);
}

int netservers_pool_steady(const NetQuickSeat *seats, int n, int me, int *partner_out)
{
    int partner[3][QUICK_POOL_MAX], relay[3][QUICK_POOL_MAX], k;
    if (partner_out) *partner_out = -1;
    if (n > QUICK_POOL_MAX) n = QUICK_POOL_MAX;
    if (me < 0 || me >= n) return -1;
    for (k = 0; k < 3; k++) pool_at(seats, n, partner[k], relay[k], (k - 1) * QUICK_POOL_SLACK_S);
    for (k = 0; k < 3; k += 2)
        if (partner[k][me] != partner[1][me] || relay[k][me] != relay[1][me]) return -2;
    if (partner_out) *partner_out = partner[1][me];
    return relay[1][me];
}

int netservers_should_move(int my_relay, int my_age, int their_relay, int their_age)
{
    if (my_relay == their_relay) return 0;
    if (their_age > my_age + 2 || (their_age >= my_age - 2 && my_relay > their_relay)) return 1;
    return their_age >= QUICK_LAST_S;
}

/* ---- what players are shown for a relay (build 84) ------------------------------------------- */

const char *netservers_known_name(const char *address)
{
    /* hosts no longer in the table: the first EU machine, US West (builds 38..88) */
    static const struct { const char *host, *name; } k_retired[] = {
        { "142.132.178.17", "Europe" }, { "west.defjam.online", "US West" }, { "5.78.214.178", "US West" },
        /* the machines of builds 30..90 */
        { "na.defjam.online", "US East" }, { "5.161.206.134", "US East" },
        { "sg.defjam.online", "Asia (Singapore)" }, { "5.223.88.150", "Asia (Singapore)" },
    };
    char host[128], h[128];
    const char *colon, *port = NULL;
    size_t i, n;
    if (!address) return NULL;
    while (*address == ' ') address++;
    colon = strrchr(address, ':');
    n = colon ? (size_t)(colon - address) : strlen(address);
    if (colon) port = colon + 1;
    while (n && address[n - 1] == ' ') n--;
    if (!n || n >= sizeof(host)) return NULL;
    if (port && strcmp(port, "27777")) return NULL;
    for (i = 0; i < n; i++) host[i] = (char)tolower((unsigned char)address[i]);
    host[n] = 0;
    for (i = 0; i < NETSERVER_COUNT; i++) {
        host_of(k_relays[i].address, h, sizeof(h));
        if (!strcmp(host, h) || (k_relays[i].fallback_ip[0] && !strcmp(host, k_relays[i].fallback_ip)))
            return k_relays[i].name;
    }
    for (i = 0; i < sizeof(k_retired) / sizeof(k_retired[0]); i++)
        if (!strcmp(host, k_retired[i].host)) return k_retired[i].name;
    return NULL;
}

int netservers_display(const char *address, char *out, int size)
{
    const char *name = netservers_known_name(address);
    if (size <= 0) return 0;
    if (name) snprintf(out, (size_t)size, "the %s server", name);
    else snprintf(out, (size_t)size, "%s", address ? address : "");
    return name != NULL;
}

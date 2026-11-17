/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The server choice and the staged quick-match rules; see netplay_servers_rules.h. */
#include "netplay_servers_rules.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum NetServerChoice netservers_parse_choice(const char *s)
{
    static const struct { const char *text; enum NetServerChoice c; } k_names[] = {
        { "", NETCHOICE_AUTO }, { "auto", NETCHOICE_AUTO }, { "automatic", NETCHOICE_AUTO },
        /* the first server's name, saved by every build before 30: now the nearest */
        { "play.defjam.online", NETCHOICE_AUTO }, { "play.defjam.online:27777", NETCHOICE_AUTO },
        { "eu", NETCHOICE_EU }, { "europe", NETCHOICE_EU },
        { "na", NETCHOICE_NA }, { "north_america", NETCHOICE_NA }, { "north america", NETCHOICE_NA },
        { "us", NETCHOICE_NA }, { "east", NETCHOICE_NA }, { "us_east", NETCHOICE_NA }, { "us east", NETCHOICE_NA },
        { "na.defjam.online", NETCHOICE_NA }, { "na.defjam.online:27777", NETCHOICE_NA },
        { "sg", NETCHOICE_WEST }, { "asia", NETCHOICE_WEST }, { "singapore", NETCHOICE_WEST },
        { "sg.defjam.online", NETCHOICE_WEST }, { "sg.defjam.online:27777", NETCHOICE_WEST },
        /* US West (builds 38..88) closed in build 89: the saved choice falls back to Automatic */
        { "west", NETCHOICE_AUTO }, { "us_west", NETCHOICE_AUTO }, { "us west", NETCHOICE_AUTO },
        { "west.defjam.online", NETCHOICE_AUTO }, { "west.defjam.online:27777", NETCHOICE_AUTO },
        /* "both" (builds 31..37) is every relay now */
        { "all", NETCHOICE_ALL }, { "all servers", NETCHOICE_ALL }, { "all_servers", NETCHOICE_ALL },
        { "both", NETCHOICE_ALL }, { "both servers", NETCHOICE_ALL }, { "both_servers", NETCHOICE_ALL },
    };
    char low[128];
    size_t i;
    if (!s) return NETCHOICE_AUTO;
    while (*s == ' ') s++;
    for (i = 0; s[i] && i < sizeof(low) - 1; i++) low[i] = (char)tolower((unsigned char)s[i]);
    low[i] = 0;
    while (i && low[i - 1] == ' ') low[--i] = 0;
    for (i = 0; i < sizeof(k_names) / sizeof(k_names[0]); i++)
        if (!strcmp(low, k_names[i].text)) return k_names[i].c;
    return NETCHOICE_CUSTOM;
}

const char *netservers_choice_text(enum NetServerChoice c)
{
    switch (c) {
    case NETCHOICE_AUTO: return "auto";
    case NETCHOICE_EU: return "eu";
    case NETCHOICE_NA: return "na";
    case NETCHOICE_WEST: return "sg";
    case NETCHOICE_ALL: return "all";
    default: return NULL;
    }
}

int netservers_choice_relay(enum NetServerChoice c)
{
    return c == NETCHOICE_EU ? NETSERVER_EU : c == NETCHOICE_NA ? NETSERVER_NA : c == NETCHOICE_WEST ? NETSERVER_WEST : -1;
}

enum NetServerChoice netservers_relay_choice(int index)
{
    return index == NETSERVER_NA ? NETCHOICE_NA : index == NETSERVER_WEST ? NETCHOICE_WEST : NETCHOICE_EU;
}

int netservers_partner(int relay)
{
    (void)relay;   /* build 89: no two relays are close to each other any more */
    return -1;
}

unsigned netservers_scope(enum NetServerChoice c, int cur, int search_s, unsigned up)
{
    unsigned mask;
    int p;
    if (c == NETCHOICE_CUSTOM || cur < 0 || cur >= NETSERVER_COUNT) return 0;
    mask = 1u << cur;
    up &= (1u << NETSERVER_COUNT) - 1;
    if (search_s >= QUICK_ALL_S) return mask | up;
    p = netservers_partner(cur);
    if (netservers_choice_relay(c) < 0 && search_s >= QUICK_NEAR_S && p >= 0 && (up >> p & 1u)) mask |= 1u << p;
    return mask;
}

int netservers_sum_move(int my_relay, int my_age, const int *mine, int their_relay, int their_age, const int *theirs)
{
    int here, there, here_max, there_max;
    if (!mine || !theirs || my_relay == their_relay || my_relay < 0 || my_relay >= NETSERVER_COUNT || their_relay < 0 ||
        their_relay >= NETSERVER_COUNT)
        return -1;
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

int netservers_should_move(int my_relay, int my_age, int their_relay, int their_age)
{
    if (my_relay == their_relay) return 0;
    if (their_age > my_age + 2 || (their_age >= my_age - 2 && my_relay > their_relay)) return 1;
    return their_age >= QUICK_LAST_S;
}

/* ---- what players are shown for a relay (build 84) ------------------------------------------- */

const char *netservers_known_name(const char *address)
{
    static const struct { const char *host, *name; } k_hosts[] = {
        { "play.defjam.online", "Europe" }, { "49.12.78.32", "Europe" }, { "142.132.178.17", "Europe" },
        { "na.defjam.online", "US East" }, { "5.161.206.134", "US East" },
        { "sg.defjam.online", "Asia (Singapore)" }, { "5.223.88.150", "Asia (Singapore)" },
        { "west.defjam.online", "US West" }, { "5.78.214.178", "US West" },
    };
    char host[128];
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
    for (i = 0; i < sizeof(k_hosts) / sizeof(k_hosts[0]); i++)
        if (!strcmp(host, k_hosts[i].host)) return k_hosts[i].name;
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

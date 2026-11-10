/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The server choice and the staged quick-match rules; see netplay_servers_rules.h. */
#include "netplay_servers_rules.h"

#include <ctype.h>
#include <stddef.h>
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
        { "west", NETCHOICE_WEST }, { "us_west", NETCHOICE_WEST }, { "us west", NETCHOICE_WEST },
        { "west.defjam.online", NETCHOICE_WEST }, { "west.defjam.online:27777", NETCHOICE_WEST },
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
    case NETCHOICE_WEST: return "west";
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
    return relay == NETSERVER_NA ? NETSERVER_WEST : relay == NETSERVER_WEST ? NETSERVER_NA : -1;
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

int netservers_should_move(int my_relay, int my_age, int their_relay, int their_age)
{
    if (my_relay == their_relay) return 0;
    if (their_age > my_age + 2 || (their_age >= my_age - 2 && my_relay > their_relay)) return 1;
    return their_age >= QUICK_LAST_S;
}

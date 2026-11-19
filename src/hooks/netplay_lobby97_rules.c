/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Build 97's lobby features without sockets or screens (netplay_lobby97_rules.h). */
#include <stdio.h>
#include <string.h>

#include "netplay_lobby97_rules.h"
#include "netplay_servers_rules.h"

int netlobby_max_ping_ms(int choice)
{
    static const int ms[NETLOBBY_MAX_PING_CHOICES] = { 0, 100, 150, 200 };
    return choice > 0 && choice < NETLOBBY_MAX_PING_CHOICES ? ms[choice] : 0;
}

static void put16(uint8_t *p, unsigned v)
{
    if (v > 0xFFFFu) v = 0xFFFFu;
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

int netlobby_active_body(uint8_t *out, unsigned idle_s, int max_ping_ms, unsigned search_s)
{
    put16(out, idle_s);
    put16(out + 2, max_ping_ms > 0 ? (unsigned)max_ping_ms : 0u);
    put16(out + 4, search_s);
    return NETLOBBY_ACTIVE_LEN;
}

int netlobby_search_line(char *out, int size, int own_max_ms, int search_s, const char *where)
{
    int limit = netservers_pool_limit(search_s < 0 ? 0 : search_s), n;
    if (size <= 0) return 0;
    if (!where || !*where) where = "every server";
    if (own_max_ms > 0)     /* the player's own limit: never widened, so that is what the search waits for */
        n = snprintf(out, (size_t)size, "Waiting for a player under %d ms on %s...", own_max_ms, where);
    else if (limit > 0)
        n = snprintf(out, (size_t)size, "Looking for a close player (under %d ms) on %s...", limit, where);
    else
        n = snprintf(out, (size_t)size, "Waiting for another player on %s...", where);
    return n < 0 ? 0 : n >= size ? size - 1 : n;
}

int netlobby_afk_line(char *out, int size, int left_s)
{
    int n;
    if (size <= 0) return 0;
    if (left_s < 1) left_s = 1;
    n = snprintf(out, (size_t)size, "Press any button: you leave the room in %d s.", left_s);
    return n < 0 ? 0 : n >= size ? size - 1 : n;
}

int netlobby_kick_minutes_left(unsigned long long since_ms)
{
    unsigned long long block = (unsigned long long)NETLOBBY_KICK_BLOCK_S * 1000u;
    unsigned long long left = since_ms >= block ? 0 : block - since_ms;
    int m = (int)((left + 59999u) / 60000u);
    return m < 1 ? 1 : m;
}

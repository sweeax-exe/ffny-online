/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Discord Rich Presence: what the game is doing, read through the online code's own accessors and
 * the front end's screen names, handed to the presence thread (discord_ipc.c). See
 * discord_presence.h. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "recomp_settings.h"
#include "discord_presence.h"
#include "discord_ipc.h"
#include "discord_rpc.h"
#include "netplay.h"
#include "netplay_servers.h"
#include "online_data.h"

enum { PHASE_NONE = 0, PHASE_LAUNCHER, PHASE_GAME };

static volatile int g_phase;
static volatile int g_offline = DRPC_OFF_MENUS;     /* written by the game thread only */
/* The front end has made a call: the guest's memory is mapped and netplay_in_fight (which reads
 * it) is safe. The presence starts before the game is loaded. */
static volatile int g_guest_up;
static int g_test_copy;                             /* a test run: Discord is left alone */
static long long g_launcher_start;

/* The presence thread's own state (only it reads and writes these). */
static char g_last_details[DRPC_TEXT_MAX + 1];
static long long g_details_start;
static int g_match_ms = -1, g_match_live;
static long long g_fight_start;

static int prefix(const char *text, const char *start)
{
    for (; *start; text++, start++) {
        char a = *text, b = *start;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

void discord_presence_on_funccall(const char *text)
{
    const char *screen;
    g_guest_up = 1;
    if (!text || strncmp(text, "Control.GetScreenInfo(", 22)) return;
    screen = text + 22;
    if (prefix(screen, "intMain/mainMenu")) g_offline = DRPC_OFF_MAIN;
    else if (prefix(screen, "story/")) g_offline = DRPC_OFF_STORY;
    else if (prefix(screen, "battle/")) g_offline = DRPC_OFF_BATTLE;
    /* options, high scores, the title...: "In the menus", unless Story or Battle mode is under way
     * (their own options screens keep the mode) */
    else if (g_offline == DRPC_OFF_MAIN) g_offline = DRPC_OFF_MENUS;
}

static int enabled(void)
{
    if (g_test_copy) return 0;
    return recomp_settings_get("online", "discord_status", 1) != 0;
}

static void copy_name(char *out, const char *in)
{
    drpc_clean(out, DRPC_NAME, in ? in : "", DRPC_NAME - 1);
}

static const char *mode_name(int type, int room_mode)
{
    const OnlineMode *m;
    if (NETPLAY_IS_KOTH(room_mode)) return "King of the Hill";
    if (NETPLAY_IS_FFAROT(room_mode)) return "FFA Rotation";
    if (type == ONLINE_MODE_TEAM) return "Team 2v2";
    m = online_mode(type);
    return m ? m->name : "Online";
}

static void read_game(DrpcGameState *g)
{
    enum NetplayState st = netplay_state();
    if (netplay_in_lobby() && st != NETPLAY_OFF && st != NETPLAY_FAILED) {
        NetVersus m;
        NetRoom *r = (NetRoom *)calloc(1, sizeof(NetRoom));
        int server = netservers_connected();
        copy_name(g->server, server >= 0 ? netservers_name(server) : server == -2 ? "Custom server" : "");
        if (r) netplay_lobby_room(r);
        if (r && r->in_room) {
            g->players = r->player_count;
            g->max = r->settings.max;
            g->koth = NETPLAY_IS_KOTH(r->settings.mode);
            copy_name(g->mode, mode_name(r->settings.mode, r->settings.mode));
            if (g->koth && r->hill.valid) {
                g->koth_king = r->hill.king >= 0 && r->hill.king == r->you;
                g->koth_streak = r->hill.streak;
            }
        }
        if (netplay_match_info(&m)) {
            int i;
            g->where = DRPC_IN_MATCH;
            g->koth = NETPLAY_IS_KOTH(m.room_mode);
            if (g->koth) g->koth_king = m.king >= 0 && m.king == m.you;
            copy_name(g->mode, mode_name(m.type, m.room_mode));
            drpc_clean(g->venue, sizeof(g->venue), m.venue > 0 ? online_venue_name(m.venue) : "", sizeof(g->venue) - 1);
            g->format = m.type == ONLINE_MODE_TEAM && m.count == 4 ? DRPC_TEAM : m.count > 2 ? DRPC_ALL_VS_ALL : DRPC_ONE_ON_ONE;
            g->count = m.count;
            g->you = m.you;
            for (i = 0; i < m.count && i < 4; i++) {
                copy_name(g->names[i], m.cpu[i] ? "" : m.name[i]);
                if (m.fighter[i] == NETPLAY_CREATED_FIGHTER)
                    copy_name(g->fighters[i], m.cp_name[i][0] ? m.cp_name[i] : "a created fighter");
                else
                    copy_name(g->fighters[i], online_fighter_name(m.fighter[i]));
            }
            g->fighting = g_guest_up && netplay_in_fight();
            /* the fight's timer: from the first step of this START's fight */
            if (!g_match_live || m.ms < g_match_ms) g_fight_start = 0;
            g_match_live = 1;
            g_match_ms = m.ms;
            if (g->fighting && !g_fight_start) g_fight_start = (long long)time(NULL);
        } else {
            g_match_live = 0;
            g->where = r && r->in_room ? DRPC_IN_ROOM : DRPC_IN_LOBBY;
            if (g->where == DRPC_IN_LOBBY) {
                int q = netplay_lobby_queue(NULL, NULL, NULL, 0);
                g->ranked_search = q == NETQUEUE_SEARCHING || q == NETQUEUE_CHECKING || q == NETQUEUE_TICKET;
            }
        }
        free(r);
        return;
    }
    g_match_live = 0;
    g->where = DRPC_IN_MENUS;
    g->offline = g_offline;
    g->offline_fight = g_guest_up && netplay_in_fight();
}

static int provider(DrpcActivity *out, void *user)
{
    DrpcGameState g;
    (void)user;
    if (!enabled() || g_phase == PHASE_NONE) return 0;
    memset(&g, 0, sizeof(g));
    if (g_phase == PHASE_LAUNCHER) g.where = DRPC_IN_LAUNCHER;
    else read_game(&g);
    drpc_compose(&g, out);
    out->buttons = 1;
    if (strcmp(out->details, g_last_details)) {
        snprintf(g_last_details, sizeof(g_last_details), "%s", out->details);
        g_details_start = (long long)time(NULL);
    }
    if (g.where == DRPC_IN_LAUNCHER) out->start = g_launcher_start;
    else if (g.where == DRPC_IN_MATCH) out->start = g_fight_start;     /* 0 until the fight starts */
    else out->start = g_details_start;
    return 1;
}

static int env_int(const char *name)
{
    const char *v = getenv(name);
    return v && *v ? atoi(v) : 0;
}

static void start(int phase)
{
    DipcConfig config;
    const char *settings = getenv("RECOMP_SETTINGS");
    g_test_copy = !getenv("RECOMP_DISCORD_PRESENCE") &&
                  ((settings && *settings) || getenv("RECOMP_PAD_SCRIPT") != NULL);
    memset(&config, 0, sizeof(config));
    config.pipe_prefix = getenv("RECOMP_DISCORD_PIPE");
    if (config.pipe_prefix && !*config.pipe_prefix) config.pipe_prefix = NULL;
    config.interval_ms = env_int("RECOMP_DISCORD_INTERVAL_MS");
    config.retry_ms = env_int("RECOMP_DISCORD_RETRY_MS");
    g_phase = phase;
    if (g_test_copy) {
        fprintf(stderr, "[DISCORD] a test run: no Discord status (RECOMP_DISCORD_PRESENCE=1 for one)\n");
        fflush(stderr);
        return;
    }
    dipc_start(&config, provider, NULL);
}

void discord_presence_launcher(void)
{
    g_launcher_start = (long long)time(NULL);
    start(PHASE_LAUNCHER);
}

void discord_presence_game(void)
{
    g_last_details[0] = 0;
    start(PHASE_GAME);
}

void discord_presence_end(void)
{
    g_phase = PHASE_NONE;
    dipc_stop();
}

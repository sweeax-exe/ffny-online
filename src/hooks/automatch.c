/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Starting a match without the menus. See automatch.h and docs/research/online-match-setup.md.
 *
 * The front end hands the game a match through FuncCall strings (Game.SetMatchType,
 * Controller.ControllerSetup, Game.ChooseVenue, Game.StartGame, ...). This replays the sequence a
 * two-player Versus run logs, with the requested values, on the game thread (the FuncCall hook),
 * once the main menu, or the battle match-type screen a match's summary leads back to, has
 * settled: no MemCard call for a while, which is when the boot-time save checks are done.
 *
 * Tests: RECOMP_AUTOMATCH="type=0,venue=1,f0=55,p0=0,f1=22,p1=-1" (pads: -1 is the CPU).
 * RECOMP_FE_RESULTS=1 prints what the roster, venue and match-type queries return. */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "automatch.h"
#include "netplay.h"
#include "online_screen.h"
#include "created_fighter.h"
#include "custom_music.h"
#include "discord_presence.h"

extern void xbox_NoteAnchorEvent(const char *text);

/* The idle main menu makes only a handful of FuncCalls (its music and sounds), so the sequence
 * runs at the menu music's start, or after a few calls with no MemCard call in between. */
#define SETTLE_CALLS 4

static AutoMatch g_req;
/* Tests only ("u0=2" in RECOMP_AUTOMATCH): the profile slot linked to a slot's pad, plus one; 0 is
 * none, as every online match and every request without the key has it. A Battle match with a
 * user ID chosen for the pad, without the menus: the post-match reward pass then feeds that
 * profile (src/patches/toughness_fix.c). */
static int g_test_user[4];
static volatile LONG g_armed;
static int g_on_menu, g_quiet, g_ticks, g_running, g_env_read, g_results = -1;

/* The patched main menu sends Online.Tick() every frame (scripts/apt-patch.py): this many
 * ticks with no MemCard call in between count as settled too, so a match agreed while the
 * menu sits idle still starts. */
#define SETTLE_TICKS 45

/* battle/cmtype (where a match's summary leads) sets itself up after its music:
 * Audio.PlayMusic(), Game.ResetMatchTypeList(), Game.ResetMatchData(). A sequence run at the
 * music had its setup wiped after StartGame: each slot's controller number back to -99, so that
 * match's summary took no controller and the player stood on the results screen for ever (King
 * of the Hill's deferred match every time, 2026-10-26). On cmtype the sequence waits for the
 * screen's ResetMatchData: armed by then, it runs right after that call, as it ran after the music
 * before; armed later, it settles as on the main menu. The ticks (patched cmtype, apt-patch
 * version 4) start it anyway if the reset never comes. Without the patch (a dump apt-patch does
 * not recognise; about 2 in 114 player logs) the idle screen makes no calls at all, so a match
 * armed before the reset must not wait for calls or ticks after it. One armed after the reset
 * still needs the ticks there (or the player's own menu sounds), as before build 32. */
#define CMTYPE_FALLBACK_TICKS (8 * SETTLE_TICKS)
static int g_cmtype, g_cmtype_reset;

static int settled(const char *text)
{
    if (g_cmtype)
        return (g_cmtype_reset && (g_quiet >= SETTLE_CALLS || g_ticks >= SETTLE_TICKS ||
                                   !strcmp(text, "Game.ResetMatchData()"))) ||
               g_ticks >= CMTYPE_FALLBACK_TICKS;
    return g_quiet >= SETTLE_CALLS || g_ticks >= SETTLE_TICKS || !strcmp(text, "Audio.PlayMusic()");
}

void automatch_request(const AutoMatch *m)
{
    g_req = *m;
    memset(g_test_user, 0, sizeof(g_test_user));
    if (g_req.count < 2) g_req.count = 2;
    if (g_req.count > 4) g_req.count = 4;
    InterlockedExchange(&g_armed, 1);
    fprintf(stderr, "[AUTOMATCH] armed: type %d, venue %d, %d fighters\n", g_req.type, g_req.venue, g_req.count);
}

int automatch_pending(void) { return g_armed || g_running; }

/* "type=0,venue=1,f0=55,p0=0,f1=22,p1=-1" (pads: -1 is the CPU) -> a request. */
int automatch_request_text(const char *e)
{
    AutoMatch m;
    char key[8];
    int value, n = 0, user[4] = { 0, 0, 0, 0 };
    if (!e || !*e) return 0;
    memset(&m, 0, sizeof(m));
    m.venue = 1;
    m.fighter[0] = 55; m.fighter[1] = 22; m.pad[0] = 0; m.pad[1] = -1;
    while (*e) {
        if (sscanf(e, "%7[a-z0-9]=%d%n", key, &value, &n) == 2) {
            if (!strcmp(key, "type")) m.type = value;
            else if (!strcmp(key, "rules")) m.rules = value;
            else if (!strcmp(key, "venue")) m.venue = value;
            else if (key[0] == 'f' && key[1] >= '0' && key[1] <= '3') m.fighter[key[1] - '0'] = value;
            else if (key[0] == 'p' && key[1] >= '0' && key[1] <= '3') m.pad[key[1] - '0'] = value;
            else if (key[0] == 'u' && key[1] >= '0' && key[1] <= '3' && value >= 0 && value < 16) user[key[1] - '0'] = value + 1;
            if (key[0] == 'f' && key[1] >= '0' && key[1] <= '3' && key[1] - '0' + 1 > m.count) m.count = key[1] - '0' + 1;
        }
        e += n ? n : 1;
        n = 0;
        while (*e == ',' || *e == ' ') e++;
    }
    if (m.count < 2) m.count = 2;
    automatch_request(&m);
    memcpy(g_test_user, user, sizeof(g_test_user));
    return 1;
}

static void read_env(void)
{
    g_env_read = 1;
    automatch_request_text(getenv("RECOMP_AUTOMATCH"));
}

/* Tests (src/hooks/test_control.c): front-end commands to run on the game thread at the next call. */
static char g_fe_queue[4][160];
static volatile LONG g_fe_count;
static CRITICAL_SECTION g_fe_cs;
static INIT_ONCE g_fe_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK fe_init(PINIT_ONCE o, PVOID p, PVOID *c) { (void)o; (void)p; (void)c; InitializeCriticalSection(&g_fe_cs); return TRUE; }

int automatch_queue_fe(const char *command)
{
    int ok = 0;
    InitOnceExecuteOnce(&g_fe_once, fe_init, NULL, NULL);
    EnterCriticalSection(&g_fe_cs);
    if (g_fe_count < 4) { snprintf(g_fe_queue[g_fe_count], sizeof(g_fe_queue[0]), "%s", command); g_fe_count++; ok = 1; }
    LeaveCriticalSection(&g_fe_cs);
    return ok;
}

static void run_fe_queue(void)
{
    char cmds[4][160];
    int n, i;
    if (!g_fe_count) return;
    EnterCriticalSection(&g_fe_cs);
    n = g_fe_count;
    memcpy(cmds, g_fe_queue, sizeof(cmds));
    g_fe_count = 0;
    LeaveCriticalSection(&g_fe_cs);
    for (i = 0; i < n; i++) {
        unsigned int r = defjam_fe_call(cmds[i]);
        fprintf(stderr, "[CONTROL] %s -> 0x%08X\n", cmds[i], r);
    }
}

static void call(const char *format, ...)
{
    char command[160];
    va_list args;
    unsigned int result;
    va_start(args, format);
    vsnprintf(command, sizeof(command), format, args);
    va_end(args);
    if (!netplay_hill_watch_quiet())    /* a pad script can wait for these as for the front end's own (not for a */
        xbox_NoteAnchorEvent(command);  /* hill fight only watched, build 70) */
    result = defjam_fe_call(command);
    fprintf(stderr, "[AUTOMATCH] %s -> %s\n", command, result ? "ok" : "0");
}

/* Online matches are fought on Hard, the same on every PC: the reported difference (players lost
 * health much faster online) came from each PC's own Options setting, which also changes the
 * fight, so different settings would part the lockstep. The player's own setting comes back on the
 * main menu once the online session is over. */
#define ONLINE_DIFFICULTY 2
static int g_saved_difficulty = -1;

/* A front-end call's answer as a number: a small value as it is, or the text it points to
 * ("2", or a name); -1 if it says nothing usable. */
static int fe_number(const char *command)
{
    unsigned int p = defjam_fe_call(command), k;
    char text[16];
    if (p < 16) return (int)p;
    for (k = 0; k < sizeof(text) - 1; k++) {
        char c = (char)MEM8(p + k);
        if (!c) break;
        text[k] = (char)tolower((unsigned char)c);
    }
    text[k] = 0;
    if (text[0] >= '0' && text[0] <= '9') return atoi(text);
    if (strstr(text, "easy")) return 0;
    if (strstr(text, "med") || strstr(text, "norm")) return 1;
    if (strstr(text, "hard")) return 2;
    return -1;
}

static void difficulty_for_online(void)
{
    /* RECOMP_ONLINE_DIFFICULTY=keep or 0..2: tests compare the settings */
    const char *e = getenv("RECOMP_ONLINE_DIFFICULTY");
    int want = e && *e >= '0' && *e <= '2' ? *e - '0' : ONLINE_DIFFICULTY;
    if (e && !strcmp(e, "keep")) return;
    if (g_saved_difficulty < 0) {
        g_saved_difficulty = fe_number("Option.GetDifficulty()");
        fprintf(stderr, "[AUTOMATCH] the player's difficulty is %d; online matches use %d\n",
                g_saved_difficulty, want);
    }
    call("Option.SetDifficulty(%d)", want);
}

static void difficulty_back(void)
{
    if (g_saved_difficulty < 0) return;
    call("Option.SetDifficulty(%d)", g_saved_difficulty);
    g_saved_difficulty = -1;
}

static void run_sequence(void)
{
    const AutoMatch *m = &g_req;
    int i, humans = 0;
    g_running = 1;
    InterlockedExchange(&g_armed, 0);
    for (i = 0; i < m->count; i++) if (m->pad[i] >= 0) humans++;
    netplay_sync_point();
    if (netplay_match_abandoned()) {     /* the other player left while this PC got here */
        g_running = 0;
        return;
    }
    difficulty_for_online();
    call("Game.StartBattleMode()");
    call("Game.ResetMatchTypeList()");
    call("Game.ResetMatchData()");
    call("Game.SetMatchType(%d)", m->type);
    call("Game.SetMatchRules(%d)", m->rules);
    call("Controller.ControllerBind(-1)");
    for (i = 0; i < m->count; i++) if (m->pad[i] >= 0) call("Controller.ControllerBind(%d)", m->pad[i]);
    for (i = 0; i < m->count; i++) if (m->pad[i] >= 0) call("MemCard.SetBMCurrentUserIndex(%d,%d)", m->pad[i], g_test_user[i] - 1);
    call("Battle.LoadCreatePlayerData(%d)", humans);
    call("Controller.ControllerSetup(-1,0,0,0)");
    /* As the game's own versus screens do: each slot's controller number (setup entry byte 4), which
     * the match summary reads; left at its reset value it showed "CPU" over both online players.
     * Nothing in the fight reads it (docs/research/summary-cpu-tag.md). */
    for (i = 0; i < m->count; i++) call("Controller.ControllerReturn(%d)", i);
    call("Game.ResetFighterList()");
    created_fighter_begin();
    for (i = 0; i < m->count; i++) {
        int fighter = m->fighter[i];
        if (fighter == CP_FIGHTER_ID) {
            /* a created fighter: the record START named (online), or this player's own; every PC
             * of the match writes the same checked record. Without one the slot plays Doc. */
            uint8_t rec[CP_REC_LEN];
            int have = netplay_active() ? netplay_created_record(i, rec) : cp_local_record(rec);
            fighter = have && m->pad[i] >= 0 ? created_fighter_inject(i, m->pad[i], rec) : -1;
            if (fighter < 0) fighter = 55;
        } else {
            int test = created_fighter_test_slot(i, m->pad[i]);     /* RECOMP_TEST_CP only */
            if (test >= 0) fighter = test;
        }
        call("Controller.ControllerSetup(%d,%d,%d,%d)", i, m->pad[i], fighter, i);
    }
    call("Game.ResetVenueList(0)");
    call("Game.ChooseVenue(%d)", m->venue);
    call("Controller.GetCustomControlMap()");
    call("Game.GetMatchInfo()");
    call("Game.StartGame()");
    g_running = 0;
    fprintf(stderr, "[AUTOMATCH] sequence done\n");
    fflush(stderr);
}

/* RECOMP_FE_PROBE=1: once the main menu has settled, list for every match type what the venue
 * screen would offer (Game.ResetVenueList / GetNextVenue), and the match types and the roster
 * once, as [FE-PROBE] lines. For working out which venues each mode allows; a probe run only. */
static void probe_list(const char *reset, const char *next, const char *tag, int limit)
{
    char first[200] = "";
    int i;
    defjam_fe_call(reset);
    for (i = 0; i < limit; i++) {
        unsigned int p = defjam_fe_call(next), k;
        char out[200];
        for (k = 0; p && k < sizeof(out) - 1; k++) {
            char c = (char)MEM8(p + k);
            if (!c) break;
            out[k] = c;
        }
        out[p ? k : 0] = 0;
        if (!out[0] || !strcmp(out, first)) break;
        if (!i) strcpy(first, out);
        fprintf(stderr, "[FE-PROBE] %s %s\n", tag, out);
    }
}

static void run_probe(void)
{
    int t;
    char cmd[64], tag[32];
    g_running = 1;
    defjam_fe_call("Game.StartBattleMode()");
    probe_list("Game.ResetMatchTypeList()", "Game.GetNextMatchType()", "matchtype", 20);
    probe_list("Game.ResetFighterList()", "Game.GetNextFighter()", "fighter", 200);
    for (t = 0; t <= 12; t++) {
        snprintf(cmd, sizeof(cmd), "Game.SetMatchType(%d)", t);
        defjam_fe_call(cmd);
        snprintf(tag, sizeof(tag), "venue[type %d]", t);
        probe_list("Game.ResetVenueList(0)", "Game.GetNextVenue()", tag, 60);
    }
    defjam_fe_call("Game.SetMatchType(0)");
    g_running = 0;
    fprintf(stderr, "[FE-PROBE] done\n");
    fflush(stderr);
}

static void print_result(const char *text)
{
    unsigned int p = g_eax, i;
    char out[400];
    if (g_results < 0) g_results = getenv("RECOMP_FE_RESULTS") != NULL;
    if (!g_results || !p) return;
    if (strncmp(text, "Game.GetNext", 12) && strncmp(text, "Game.GetMatchInfo", 17) && strncmp(text, "Game.GetMatchType", 17) &&
        strncmp(text, "Game.GetMatchSummary", 20))
        return;
    for (i = 0; i < sizeof(out) - 1; i++) {
        char c = (char)MEM8(p + i);
        if (!c) break;
        out[i] = c;
    }
    out[i] = 0;
    fprintf(stderr, "[FE-RESULT] %s = %s\n", text, out);
}

void test_control_note_funccall(const char *text);     /* src/hooks/test_control.c */

/* Before the front end runs a call: 1 when it is to be held (online, a rematch that the other
 * players have not chosen yet). */
int automatch_hold_funccall(const char *text)
{
    if (strstr(text, "intMain/mainMenu.swf"))
        custom_music_options_ready();
    /* The front end's own calls that set up a match, while the room's match is agreed, set up or
     * fought: the menu under the match (the battle match-type screen a summary returns to) still takes
     * presses, and an A pressed there during the load went on to the controller screen, which cleared
     * every controller binding (Controller.ControllerBind(-1)) and looked for a memory card while the
     * venue loaded; that game crashed in the load (four-copy Team test, 2026-10-25). The match is
     * automatch's: these calls are dropped from its sequence until the game is back in the room.
     * Not while the room's match only waits for the menu to settle (armed): the match-type screen's
     * own Game.ResetMatchData() is what automatch waits for, and a King of the Hill king called
     * straight after his fight gets the next START while still on the results; with the screen's
     * setup dropped automatch never started that fight (three-copy hill test, 2026-11-01). */
    static const char *const setup[] = { "Controller.ControllerBind(", "Controller.ControllerSetup(",
        "Controller.ControllerRemove(", "Controller.ControllerSelect(", "Game.SetMatchType(", "Game.SetMatchRules(",
        "Game.ChooseVenue(", "Game.StartGame(", "Game.StartBattleMode(", "Game.ResetMatchData(",
        "Battle.LoadCreatePlayerData(", "MemCard.ChooseMCDevice(", "MemCard.SetBMCurrentUserIndex(" };
    unsigned i;
    /* Game.Quit() leaves the results for the match-type screen (screen 9). An A held for a few frames
     * on the results' last page called it once a frame, and each call requested screen 9 again while
     * the first was still loading: the screen manager's left-open race (docs/research/screen-swap-freeze.md)
     * then froze the game on that page for good (four-copy Team test, 2026-11-01: three requests, no
     * frame after them). A Game.Quit() while a screen loads (the manager, registry slot 0x17, in a
     * state other than 0) is dropped; the first one goes on. */
    if (!strcmp(text, "Game.Quit()")) {
        uint32_t sm = MEM32(0x002FB148u + 0x17u * 4u);
        if (sm >= 0x10000u && MEM32(sm + 0x24u) != 0) {
            static unsigned dropped;
            if (dropped++ < 20)
                fprintf(stderr, "[SCREEN] Game.Quit() while screen %08X loads (state %u): dropped (#%u)\n",
                        MEM32(sm + 0x1Cu), MEM32(sm + 0x24u), dropped);
            return 1;
        }
    }
    created_fighter_before_funccall(text);      /* unlinks created fighters before the reward step */
    netplay_spectator_funccall(text);           /* spectator mode: the summary goes by by itself */
    netplay_results_funccall(text);             /* the results' No: the others hear it at once */
    if (!strcmp(text, "Game.Rematch()")) return netplay_rematch_request();
    if (g_armed || (!netplay_match_locked() && !g_running)) return 0;
    for (i = 0; i < sizeof(setup) / sizeof(setup[0]); i++)
        if (!strncmp(text, setup[i], strlen(setup[i]))) {
            static unsigned told;
            if (told++ < 20) fprintf(stderr, "[AUTOMATCH] the room's match is on: the menu's %s is dropped\n", text);
            return 1;
        }
    return 0;
}

void recorder_event_text(const char *kind, const char *key, const char *text);     /* recorder.h */

/* A front-end command as if the menu had called it: logged, and through the same hooks before and
 * after as the FuncCall hook in recomp_manual.c (held calls, created fighters' links, this file's
 * sequence). Online, a Rematch that the others accepted while this PC's was held starts with it
 * (netplay.c, netplay_rematch_tick). Registers and the x87 state are put back. */
unsigned int defjam_menu_call(const char *text)
{
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx, ebx = g_ebx, esi = g_esi, edi = g_edi, ebp = g_ebp, esp = g_esp;
    double fp[8];
    int fp_top = g_fp_top;
    uint16_t fp_cw = g_fp_control_word;
    unsigned int r = 0;
    memcpy(fp, g_fp_stack, sizeof(fp));
    fprintf(stderr, "[FUNCCALL] %s (for the menu)\n", text);
    recorder_event_text("fe", "text", text);
    if (!netplay_hill_watch_quiet()) xbox_NoteAnchorEvent(text);
    if (!automatch_hold_funccall(text)) {
        r = defjam_fe_call(text);
        automatch_on_funccall(text);
    }
    g_eax = eax; g_ecx = ecx; g_edx = edx; g_ebx = ebx; g_esi = esi; g_edi = edi; g_ebp = ebp; g_esp = esp;
    memcpy(g_fp_stack, fp, sizeof(fp));
    g_fp_top = fp_top;
    g_fp_control_word = fp_cw;
    return r;
}

void automatch_on_funccall(const char *text)
{
    test_control_note_funccall(text);
    discord_presence_on_funccall(text);     /* the screen, for the Discord status */
    run_fe_queue();
    unsigned int keep = g_eax;
    if (!g_env_read) read_env();
    online_menu_on_funccall(text);
    print_result(text);
    if (strstr(text, "GetScreenInfo(intMain/mainMenu.swf") || strstr(text, "GetScreenInfo(battle/cmtype"))
        netplay_note_front_end();                   /* a fight quit from its pause menu is over */
    if (g_running) return;
    /* the main menu, or the battle match-type screen the summary of a match returns to */
    if (strstr(text, "intMain/mainMenu.swf") || strstr(text, "GetScreenInfo(battle/cmtype")) {
        g_on_menu = 1;
        g_quiet = g_ticks = 0;
        g_cmtype = strstr(text, "GetScreenInfo(battle/cmtype") != NULL;
        g_cmtype_reset = 0;
        if (!g_armed) netplay_on_main_menu();       /* an online match has ended */
        if (!g_armed && strstr(text, "intMain/mainMenu.swf") && !netplay_in_lobby())
            difficulty_back();                      /* the online session is over */
    }
    else if (strstr(text, "GetScreenInfo(")) g_on_menu = g_cmtype = 0;
    if (g_cmtype && !strcmp(text, "Game.ResetMatchData()")) {
        g_cmtype_reset = 1;                         /* the screen's own setup is done */
        g_quiet = g_ticks = 0;
    }
    if (!strncmp(text, "MemCard.", 8)) g_quiet = g_ticks = 0;
    else if (!strcmp(text, "Online.Tick()")) { if (g_on_menu) g_ticks++; }
    else if (g_on_menu) g_quiet++;
    {
        static int probed;
        if (!probed && g_on_menu && settled(text) && getenv("RECOMP_FE_PROBE")) {
            probed = 1;
            run_probe();
        }
    }
    if (g_armed && g_on_menu && settled(text) && !created_fighter_test_hold()) {
        g_on_menu = 0;
        run_sequence();
    }
    g_eax = keep;           /* the front end gets its own call's answer back */
}

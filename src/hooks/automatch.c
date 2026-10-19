/* Starting a match without the menus. See automatch.h and docs/research/online-match-setup.md.
 *
 * The front end hands the game a match through FuncCall strings (Game.SetMatchType,
 * Controller.ControllerSetup, Game.ChooseVenue, Game.StartGame, ...). This replays the sequence a
 * two-player Versus run logs, with the requested values, on the game thread (the FuncCall hook),
 * once the main menu has settled: no MemCard call for a while, which is when the boot-time save
 * checks are done.
 *
 * Tests: RECOMP_AUTOMATCH="type=0,venue=1,f0=55,p0=0,f1=22,p1=-1" (pads: -1 is the CPU).
 * RECOMP_FE_RESULTS=1 prints what the roster, venue and match-type queries return. */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "automatch.h"

/* The idle main menu makes only a handful of FuncCalls (its music and sounds), so the sequence
 * runs at the menu music's start, or after a few calls with no MemCard call in between. */
#define SETTLE_CALLS 4

static AutoMatch g_req;
static volatile LONG g_armed;
static int g_on_menu, g_quiet, g_running, g_env_read, g_results = -1;

void automatch_request(const AutoMatch *m)
{
    g_req = *m;
    if (g_req.count < 2) g_req.count = 2;
    if (g_req.count > 4) g_req.count = 4;
    InterlockedExchange(&g_armed, 1);
    fprintf(stderr, "[AUTOMATCH] armed: type %d, venue %d, %d fighters\n", g_req.type, g_req.venue, g_req.count);
}

int automatch_pending(void) { return g_armed || g_running; }

static void read_env(void)
{
    const char *e = getenv("RECOMP_AUTOMATCH");
    AutoMatch m;
    char key[8];
    int value, n = 0;
    g_env_read = 1;
    if (!e || !*e) return;
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
            if (key[0] == 'f' && key[1] >= '0' && key[1] <= '3' && key[1] - '0' + 1 > m.count) m.count = key[1] - '0' + 1;
        }
        e += n ? n : 1;
        n = 0;
        while (*e == ',' || *e == ' ') e++;
    }
    if (m.count < 2) m.count = 2;
    automatch_request(&m);
}

static void call(const char *format, ...)
{
    char command[160];
    va_list args;
    unsigned int result;
    va_start(args, format);
    vsnprintf(command, sizeof(command), format, args);
    va_end(args);
    result = defjam_fe_call(command);
    fprintf(stderr, "[AUTOMATCH] %s -> %s\n", command, result ? "ok" : "0");
}

static void run_sequence(void)
{
    const AutoMatch *m = &g_req;
    int i, humans = 0;
    g_running = 1;
    InterlockedExchange(&g_armed, 0);
    for (i = 0; i < m->count; i++) if (m->pad[i] >= 0) humans++;
    call("Game.StartBattleMode()");
    call("Game.ResetMatchTypeList()");
    call("Game.ResetMatchData()");
    call("Game.SetMatchType(%d)", m->type);
    call("Game.SetMatchRules(%d)", m->rules);
    call("Controller.ControllerBind(-1)");
    for (i = 0; i < m->count; i++) if (m->pad[i] >= 0) call("Controller.ControllerBind(%d)", m->pad[i]);
    for (i = 0; i < m->count; i++) if (m->pad[i] >= 0) call("MemCard.SetBMCurrentUserIndex(%d,-1)", m->pad[i]);
    call("Battle.LoadCreatePlayerData(%d)", humans);
    call("Controller.ControllerSetup(-1,0,0,0)");
    call("Game.ResetFighterList()");
    for (i = 0; i < m->count; i++) call("Controller.ControllerSetup(%d,%d,%d,%d)", i, m->pad[i], m->fighter[i], i);
    call("Game.ResetVenueList(0)");
    call("Game.ChooseVenue(%d)", m->venue);
    call("Controller.GetCustomControlMap()");
    call("Game.GetMatchInfo()");
    call("Game.StartGame()");
    g_running = 0;
    fprintf(stderr, "[AUTOMATCH] sequence done\n");
    fflush(stderr);
}

static void print_result(const char *text)
{
    unsigned int p = g_eax, i;
    char out[400];
    if (g_results < 0) g_results = getenv("RECOMP_FE_RESULTS") != NULL;
    if (!g_results || !p) return;
    if (strncmp(text, "Game.GetNext", 12) && strncmp(text, "Game.GetMatchInfo", 17) && strncmp(text, "Game.GetMatchType", 17))
        return;
    for (i = 0; i < sizeof(out) - 1; i++) {
        char c = (char)MEM8(p + i);
        if (!c) break;
        out[i] = c;
    }
    out[i] = 0;
    fprintf(stderr, "[FE-RESULT] %s = %s\n", text, out);
}

void automatch_on_funccall(const char *text)
{
    unsigned int keep = g_eax;
    if (!g_env_read) read_env();
    print_result(text);
    if (g_running) return;
    if (strstr(text, "intMain/mainMenu.swf")) { g_on_menu = 1; g_quiet = 0; }
    else if (strstr(text, "GetScreenInfo(") && !strstr(text, "mainMenu")) g_on_menu = 0;
    if (!strncmp(text, "MemCard.", 8)) g_quiet = 0;
    else if (g_on_menu) g_quiet++;
    if (g_armed && g_on_menu && (g_quiet >= SETTLE_CALLS || !strcmp(text, "Audio.PlayMusic()"))) {
        g_on_menu = 0;
        run_sequence();
    }
    g_eax = keep;           /* the front end gets its own call's answer back */
}

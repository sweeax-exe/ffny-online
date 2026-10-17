/*
 * Front-end command injection: call the game the way its own menus do.
 *
 * Every screen's ActionScript reaches the game through FuncCall: a string such as
 * "Game.StartStoryMode()" or "Control.GetScreenInfo(story/crib.swf)" handed to
 * the handler at guest 0x00068BA0, which dispatches it by module and method
 * (recomp_manual.c, funccall_hook). Anything a menu can ask for, a test can ask
 * for too, by handing the same handler a string of its own -- on the same thread,
 * at a moment the front end itself was making a call:
 *
 *     RECOMP_FE_INJECT=<anchor>[#N]=><command>[;<command>...][|<anchor>=>...]
 *
 * After the Nth front-end call containing <anchor> (case-insensitive, N defaults
 * to 1) has run, each <command> is dispatched in turn, once. For example
 *
 *     RECOMP_FE_INJECT="getscreeninfo(story/crib#1=>Story.SomeMethod(arg)"
 *
 * `python scripts/funccall-catalog.py` lists every command string the front
 * end's own movies contain, which is where to look for the one that starts a
 * scene (docs/research/scenario-launching.md). Each dispatch is logged as
 * [FE-INJECT] and as a recorder event. A test control, off unless set.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fe_inject.h"
#include "guest_call.h"
#include "recorder.h"

extern void sub_00068BA0(void);

#define RULES 8
static FeInjectRule s_rules[RULES];
static int s_count = -1;

static void lower(char *out, size_t size, const char *in, size_t n)
{
    size_t i;
    if (n >= size) n = size - 1;
    for (i = 0; i < n; i++) out[i] = (char)tolower((unsigned char)in[i]);
    out[n] = 0;
}

int fe_inject_parse(const char *spec, FeInjectRule *rules, int max)
{
    int count = 0;
    while (spec && *spec && count < max) {
        const char *end = strchr(spec, '|');
        const char *arrow = strstr(spec, "=>");
        size_t len = end ? (size_t)(end - spec) : strlen(spec);
        FeInjectRule *r = &rules[count];
        memset(r, 0, sizeof *r);
        if (arrow && arrow < spec + len) {
            const char *hash = NULL, *p;
            for (p = spec; p < arrow; p++)
                if (*p == '#') hash = p;
            lower(r->anchor, sizeof r->anchor, spec, (size_t)((hash ? hash : arrow) - spec));
            r->want = hash ? atoi(hash + 1) : 1;
            if (r->want < 1) r->want = 1;
            {
                size_t n = (size_t)(spec + len - (arrow + 2));
                if (n >= sizeof r->commands) n = sizeof r->commands - 1;
                memcpy(r->commands, arrow + 2, n);
                r->commands[n] = 0;
            }
            if (r->anchor[0] && r->commands[0]) count++;
        }
        spec = end ? end + 1 : spec + len;
    }
    return count;
}

/* Hand `command` to the front end's handler, as a menu would. The string goes on
 * the guest stack, below everything live, for the length of the call. */
static void dispatch(const char *command)
{
    GuestRegs saved;
    size_t n = strlen(command) + 1, i;
    uint32_t text, args[1];

    guest_regs_save(&saved);
    g_esp -= (uint32_t)((n + 3) & ~(size_t)3);
    text = g_esp;
    for (i = 0; i < n; i++) MEM8(text + (uint32_t)i) = (uint8_t)command[i];
    args[0] = text;
    guest_call(sub_00068BA0, 0, 1, args);
    guest_regs_restore(&saved);
}

void fe_inject_after_call(const char *text)
{
    char low[192];
    int i;
    if (s_count < 0) {
        const char *spec = getenv("RECOMP_FE_INJECT");
        s_count = spec && *spec ? fe_inject_parse(spec, s_rules, RULES) : 0;
        if (s_count)
            fprintf(stderr, "[FE-INJECT] %d rule(s) armed\n", s_count);
    }
    if (!s_count || !text) return;
    lower(low, sizeof low, text, strlen(text));
    for (i = 0; i < s_count; i++) {
        FeInjectRule *r = &s_rules[i];
        char commands[sizeof r->commands], *cmd, *next;
        if (r->fired || !strstr(low, r->anchor) || ++r->seen < r->want) continue;
        r->fired = 1;
        memcpy(commands, r->commands, sizeof commands);
        for (cmd = commands; cmd && *cmd; cmd = next) {
            next = strchr(cmd, ';');
            if (next) *next++ = 0;
            while (*cmd == ' ') cmd++;
            if (!*cmd) continue;
            fprintf(stderr, "[FE-INJECT] after \"%s\" #%d: %s\n", r->anchor, r->want, cmd);
            fflush(stderr);
            recorder_event_text("fe_inject", "command", cmd);
            dispatch(cmd);
        }
    }
}

/* The Story scene loader (guest 0x00073FD0, thiscall, the scene's name as its first
 * argument): it opens assets/nis_<name>.bin and assets/cinema/<name>.viv. The names
 * it is given point into the image's table of scenes at 0x0028CA28 ("sc00", "sc01",
 * ... "sc07a", ... "sc21", then the "gf" set), one every 8 bytes. Every scene is
 * logged by name ([CINEMA], and a recorder `cinema` event). With
 * RECOMP_TEST_CINEMA_SCENE=<name> set, the first scene the game loads is that one
 * instead: the argument is pointed at that name's table entry, so nothing is
 * written and only a scene the game knows is taken. Start Story with a profile that
 * has no Story progress and the prologue's place plays the chosen scene. A test
 * control, off unless set. */
#define CINEMA_SCENE_TABLE 0x0028CA28u
#define CINEMA_SCENE_SLOTS 64

static void guest_text(uint32_t va, char *out, size_t size)
{
    size_t n = 0;
    while (va && n + 1 < size && MEM8(va + (uint32_t)n)) {
        out[n] = (char)MEM8(va + (uint32_t)n);
        n++;
    }
    out[n] = 0;
}

/* The table entry named `want`, or 0; `list` gets every name, for the log. */
static uint32_t cinema_scene_entry(const char *want, char *list, size_t size)
{
    uint32_t i, found = 0;
    size_t len = 0;
    list[0] = 0;
    for (i = 0; i < CINEMA_SCENE_SLOTS; i++) {
        char name[9];
        size_t k;
        guest_text(CINEMA_SCENE_TABLE + i * 8u, name, sizeof name);
        for (k = 0; name[k] && (isalnum((unsigned char)name[k]) || name[k] == '_'); k++) {}
        if (k < 3 || name[k]) break;            /* past the table */
        if (!found && !strcmp(name, want)) found = CINEMA_SCENE_TABLE + i * 8u;
        if (len + k + 2 < size) len += (size_t)snprintf(list + len, size - len, "%s%s", len ? " " : "", name);
    }
    return found;
}

void fe_cinema_scene_enter(void)
{
    static int armed = -1;
    static char want[16];
    char old[32], list[512];
    uint32_t name = MEM32(g_esp + 4), entry;

    if (armed < 0) {
        const char *s = getenv("RECOMP_TEST_CINEMA_SCENE");
        armed = s && *s && strlen(s) < sizeof want;
        if (armed) snprintf(want, sizeof want, "%s", s);
    }
    if (!name) return;
    guest_text(name, old, sizeof old);
    fprintf(stderr, "[CINEMA] scene %s\n", old);
    recorder_event_text("cinema", "scene", old);
    if (armed != 1) return;
    armed = 2;                                  /* once: the scenes after it play as the story has them */
    entry = cinema_scene_entry(want, list, sizeof list);
    if (!entry) {
        fprintf(stderr, "[CINEMA] RECOMP_TEST_CINEMA_SCENE=%s is not a scene of this game; known: %s\n", want, list);
        return;
    }
    MEM32(g_esp + 4) = entry;
    fprintf(stderr, "[CINEMA] %s replaced by %s (RECOMP_TEST_CINEMA_SCENE)\n", old, want);
    recorder_event_text("cinema", "replaced_by", want);
}

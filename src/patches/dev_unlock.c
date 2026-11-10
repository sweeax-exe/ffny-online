/* Developer switches the retail image still carries, behind environment variables.
 *
 *   RECOMP_UNLOCK_ALL=1  every fighter and venue, 10,000 points (below)
 *   RECOMP_DEV_DEBUG=1   the in-fight debug overlay: what the "Debug" branch of the
 *                        Extras code handler (guest 0x00049EB5) does, which is to call
 *                        sub_000B4CE0(1), a setter for the byte at 0x0039C1DA. sub_000B5AD0
 *                        reads it every frame and, in a fight, draws a debug label
 *                        (for example "N4007 A") under each fighter's health bar.
 *   RECOMP_START_FIGHTERS=a,b[,c,d]  the fighter ids (fighter.xml <ID>) a match starts with, by slot
 *   RECOMP_START_VENUE=n             the venue, as the script id Game.ChooseVenue carries
 *   RECOMP_START_LOG=1               log what Game.StartGame is about to be given
 *   RECOMP_UNLOCK_REPORT=1           log what the current profile has unlocked, per screen (read only)
 *                        The last three act at Game.StartGame (the end of this file).
 *
 * Developer unlock (RECOMP_UNLOCK_ALL=1): every fighter and venue, 10,000 points.
 *
 * The retail image still carries the developers' "unlock everything" routine,
 * guest 0x000158F0. It walks the character table (key 0x4CE5B1B2) and the
 * venue table (key 0xD7DCA628) and inserts each id into the profile's unlocked
 * sets, which is exactly what BuyCharacter and BuyVenue do one id at a time,
 * then sets the reward points (profile + 0x50) to 10,000 and tells the profile
 * owner it changed. Its only caller is the Extras code-entry handler's case 5
 * (the "BIGBANG" branch), and the retail code classifier never produces case 5,
 * so no pad input reaches it. docs/research/unlock-everything.md has the trail.
 *
 * This file calls it from the front end's own call stream. The front end logs
 * each call its screens make into the game; once the main menu has started, the
 * routine runs on every screen change, so a profile loaded later (Story, the
 * user-ID screen) is unlocked before the next screen can look at it. It only
 * inserts ids that are missing, so repeating it costs nothing and changes
 * nothing but the points, which are held at 10,000 until the profile is saved
 * differently.
 *
 * It changes the profile in memory and asks the game to save it: run it on a
 * disposable save (the harness and the scenario suite already do), not on a
 * real one. Off unless asked for, so the unmodified path stays testable.
 *
 * FFNY Online: none of this acts while this PC is online (src/patches/online_guard.h): no unlock,
 * no debug overlay (its flag is cleared), and Game.StartGame keeps the match the room set up.
 */
#include "host.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "cheats.h"
#include "online_guard.h"

extern void sub_000158F0(void);

#define DEBUG_FLAG 0x0039C1DAu

static int s_enabled = -1;
static int s_debug = -1;
static int s_started;
static unsigned long s_runs;
#define GAMEDATA_SLOT_REPORT 0x18u

int defjam_unlock_all_enabled(void)
{
    if (s_enabled < 0) {
        const char *v = getenv("RECOMP_UNLOCK_ALL");
        s_enabled = v && *v && strcmp(v, "0") != 0;
        if (s_enabled)
            fprintf(stderr, "[UNLOCK] RECOMP_UNLOCK_ALL: every fighter and venue, 10000 points, "
                            "on each screen change after the main menu starts\n");
    }
    return s_enabled;
}

static int debug_enabled(void)
{
    if (s_debug < 0) {
        const char *v = getenv("RECOMP_DEV_DEBUG");
        s_debug = v && *v && strcmp(v, "0") != 0;
        if (s_debug)
            fprintf(stderr, "[UNLOCK] RECOMP_DEV_DEBUG: in-fight debug overlay on\n");
    }
    return s_debug;
}

static void run_unlock(void)
{
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx;

    /* A call needs a return address; the callee pops it and keeps the callee-saved
     * registers (ebx, esi, edi, ebp) as the x86 original did. */
    g_esp -= 4;
    MEM32(g_esp) = 0;
    sub_000158F0();
    g_eax = eax;
    g_ecx = ecx;
    g_edx = edx;
    s_runs++;
    if (s_runs == 1 || s_runs == 10 || s_runs == 100)
        fprintf(stderr, "[UNLOCK] sub_000158F0 run %lu\n", s_runs);
}

/* RECOMP_UNLOCK_REPORT=1 (tests, read only): on each screen change, what the current profile has
 * unlocked as the game holds it: per item kind (the six containers at save record +0x14E4, the
 * launcher's Unlock all, src/hooks/save_unlock.h) the item count, where its 2-bit states sit in the
 * profile slot, and how many are locked / unlocked / owned; and the fighter and venue set sizes. */
static void unlock_report(const char *text)
{
    static const uint32_t at[6] = {0x0, 0x58, 0x108, 0xB0, 0x160, 0x1B8};   /* profile order */
    static const char *const name[6] = {"hair", "tattoo", "jewel", "cloth", "blazin", "style"};
    uint32_t gd = MEM32(0x002FB148u + GAMEDATA_SLOT_REPORT * 4u), p, idx, slot, k;
    char line[512];
    int n;
    const char *screen = text + 22;
    if (!gd || !(p = MEM32(gd + 4)))
        return;
    idx = MEM32(p + 0x5AC7Cu);
    n = snprintf(line, sizeof line, "[UNLOCK-REPORT] %.40s profile %d fighters %u venues %u", screen, (int)idx,
                 MEM32(p + 0x1268u), MEM32(p + 0x1264u));
    if (idx < 16) {
        slot = p + 0x177Cu + idx * 0x5950u;
        n += snprintf(line + n, sizeof line - n, " cash %u", MEM32(slot + 0x20u));
        for (k = 0; k < 6 && n < (int)sizeof line - 64; k++) {
            uint32_t c = p + 0x14E4u + at[k], count = MEM32(c + 8), states = MEM32(c + 0xC + idx * 4), i;
            unsigned lk = 0, un = 0, ow = 0;
            if (!states || count > 4096) continue;
            for (i = 0; i < count; i++) {
                unsigned v = (MEM8(states + i / 4) >> (i % 4 * 2)) & 3;
                if (!v) lk++; else if (v == 1) un++; else ow++;
            }
            n += snprintf(line + n, sizeof line - n, " %s %u@+0x%X %u/%u/%u", name[k], count,
                          (unsigned)(states - slot), lk, un, ow);
        }
    }
    fprintf(stderr, "%s\n", line);
    fflush(stderr);
}

/* Called with the text of each front-end call, before the game handles it. */
void defjam_unlock_all_on_funccall(const char *text)
{
    int unlock = defjam_unlock_all_enabled();
    int debug = debug_enabled();
    static int report = -1;

    if (report < 0) {
        const char *v = getenv("RECOMP_UNLOCK_REPORT");
        report = v && *v && strcmp(v, "0") != 0;
    }
    if (report && strncmp(text, "Control.GetScreenInfo(", 22) == 0)
        unlock_report(text);

    if (!unlock && !debug)
        return;
    if (ffny_online_locked()) {
        if (debug && MEM8(DEBUG_FLAG))
            MEM8(DEBUG_FLAG) = 0;
        return;
    }
    if (!s_started) {
        /* The managers sub_000158F0 reads exist once the front end is up.
         * skip_intro bypasses bootLoad's Game.StartMainMenu(0); the actual
         * main-menu screen is also a safe activation boundary. */
        if (strstr(text, "Game.StartMainMenu")) {
            s_started = 1;
            return;
        }
        if (!strstr(text, "Control.GetScreenInfo(intMain/mainMenu."))
            return;
        s_started = 1;  /* Apply at the first menu too, before it can use the profile. */
    }
    if (debug)
        MEM8(DEBUG_FLAG) = 1;
    if (unlock && strncmp(text, "Control.GetScreenInfo(", 22) == 0)
        run_unlock();
}


/* ---- Game.StartGame, with the match set-up under control --------------------------------
 *
 * Game.StartGame is the script method at guest 0x00046E40. It is registered by name and reached
 * through a function pointer, so recomp_lookup_manual() can hand back a wrapper. The wrapper
 * logs, and optionally overrides, what the match is about to be made from; Story fights start
 * here too, with the created hero as fighter id 99.
 */
extern recomp_func_t recomp_lookup(uint32_t xbox_va);

#define GAMEDATA_SLOT 0x18u
#define MANAGERS      0x002FB148u

static int s_start_log = -1;

static void start_game_wrapper(void)
{
    uint32_t game = g_ecx;                                     /* the Game script object */
    uint32_t gd = MEM32(MANAGERS + GAMEDATA_SLOT * 4);         /* the game-data object */
    recomp_func_t real = recomp_lookup(0x00046E40u);
    const char *v = getenv("RECOMP_START_VENUE");
    const char *f = getenv("RECOMP_START_FIGHTERS");
    unsigned i;

    if (ffny_online_locked()) {     /* the room's match as automatch set it up (online_guard.h) */
        if (s_start_log && ((v && *v) || (f && *f)))
            fprintf(stderr, "[START] online: RECOMP_START_VENUE/RECOMP_START_FIGHTERS ignored\n");
        v = f = NULL;
    }
    /* The venue: the script id Game.ChooseVenue stored (0x2DD978 maps it to the venue). */
    if (v && *v)
        MEM32(game + 0xCA4) = (uint32_t)strtol(v, NULL, 0);
    /* The fighters: the id of each slot, in the per-slot records at gd+0x1AA9 (five bytes
     * a slot; the id is the second byte, what Controller.ControllerSetup stores). */
    for (i = 0; f && *f && i < 4; i++) {
        char *end;
        long id = strtol(f, &end, 0);
        if (end == f)
            break;
        MEM8(gd + 0x1AAAu + i * 5) = (uint8_t)id;
        f = (*end == ',') ? end + 1 : end;
    }
    if (!ffny_online_locked())
        cheats_start_game(gd);     /* [cheats] story_hero (src/patches/cheats.c) */
    if (s_start_log) {
        fprintf(stderr, "[START] venue(script)=%d fighters(gd+1aaa step 5):", (int)MEM32(game + 0xCA4));
        for (i = 0; i < 4; i++)
            fprintf(stderr, " %d", (int)(int8_t)MEM8(gd + 0x1AAAu + i * 5));
        fprintf(stderr, "\n");
        fflush(stderr);
    }
    if (real)
        real();
    cheats_start_game_done(gd);
}

/* Always wrapped: the Story hero cheat can be turned on in the overlay at any time. Without a
 * cheat or one of the variables above the wrapper only calls the game's own method. */
recomp_func_t defjam_start_game_hook(void)
{
    if (s_start_log < 0) {
        const char *a = getenv("RECOMP_START_LOG"), *b = getenv("RECOMP_START_VENUE"),
                   *c = getenv("RECOMP_START_FIGHTERS");
        s_start_log = (a && *a && strcmp(a, "0")) || (b && *b) || (c && *c);
    }
    return start_game_wrapper;
}

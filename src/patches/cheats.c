/* Cheats (issue #71, PR #97; docs/research/cheat-menu.md).
 *
 * Fight cheats are [cheats] settings that apply as they change and live only in memory:
 *
 *   infinite_health  the chosen fighters lose no health: sub_001A50B0(record, delta, flags, extra),
 *                    the one routine every hit and every regeneration goes through, gets a delta of
 *                    zero when it would take health away (the hit reaction still plays), and every
 *                    simulation step puts health and its recoverable ceiling (+0x358) back at the
 *                    maximum (+0x354), which also covers the direct writers that bypass it.
 *   max_blazin       the chosen fighters' momentum meter is held full and Blazin' ready, through the
 *                    game's own momentum gain sub_001A5190 (which, when the meter fills, sets meter
 *                    +0x364 = 99, timer +0x368 = 1800, state +0x93E bit 0 and calls sub_000B0130(slot,
 *                    1) for the HUD and sound). The player still starts it the usual way; while it is
 *                    ready or running (bits 0, 1) nothing is touched. It starts after five seconds of
 *                    fighting: a computer fighter given it during the walk-in stopped the match.
 *   no_regen         health does not come back by itself: regeneration's call (from 0x001A5807)
 *                    gets a delta of zero.
 *   fighter_size     giant or tiny fighters: the scale of each fighter's model. Collision and reach
 *                    do not follow; it is a picture change only.
 *   story_hero       Story fights played as another fighter, with that fighter's model and moves: the
 *                    match setup's slot for the created hero is rewritten at Game.StartGame (below).
 *
 * Story cheats change the Story profile the game has loaded, which the game saves as a raw image of
 * the slot, so they are permanent once it saves (the overlay says so before it applies them). They
 * are one-shot requests from the overlay, applied on the game's thread:
 *
 *   cash             slot +0x20 (GetPlayerCash sub_0004CA30 reads it, sub_00013640 spends it);
 *   development pts  slot +0x28 (GetPlayerDevPts sub_0004CA70);
 *   fighting styles  slot +0x140 main, +0x141 and +0x142 hybrids (0xFF none), +0x143 the style id
 *                    plus one, then sub_00014C20 on the slot: what the gym's Learn Style purchase
 *                    (sub_0004EB60, from 0x0004ED51) does after it writes a style byte.
 *
 * The slot is P + MEM32(P + 0x5AC7C) * 0x5950 + 0x177C, P = MEM32(game data + 4), game data =
 * MEM32(0x2FB148 + 0x18 * 4). It counts as Story from Game.StartStoryMode until the front end is back
 * at its own menus. Requests are applied on the game's thread, at the module manager's per-frame update
 * (sub_00077040) or the next front-end call, whichever comes first.
 *
 * Off unless asked for. The harness runs with RECOMP_SETTINGS=none, so every cheat is off there
 * unless a test sets its RECOMP_CHEAT_* variable. For tests and investigation:
 *   RECOMP_CHEAT_LOG=1        fighters' health, meter and Blazin' state every 60 steps, Story events
 *                             (2: every 10 steps)
 *   RECOMP_CHEAT_PROBE=1      also the Story slot's fields at each screen and the match setup's slots
 *   RECOMP_CHEAT_STORY_TEST=cash,devpts,styles=M/H1/H2   the overlay's Story buttons, pressed at the first
 *                             crib screen
 */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp/gen/recomp_types.h"
#include "hooks/guest_call.h"
#include "cheats.h"

#define MANAGERS       0x002FB148u
#define GAMEDATA_SLOT  0x18u
#define PROFILE_SLOTS  16u
#define SLOT_STRIDE    0x5950u
#define SLOT_BASE      0x177Cu
#define SLOT_INDEX     0x5AC7Cu
#define S_CASH         0x20u
#define S_DEVPTS       0x28u
#define S_STYLES       0x140u
#define S_STYLE_ID     0x143u
#define STYLE_TABLE    0x002EBF6Fu       /* GameCoreFightStyleToID's byte table */

#define ACTORS         0x003B92E0u
#define STRIDE         0x12E8u
#define SLOTS          4
#define F_SLOT         0x28u
#define F_HEALTH       0x350u
#define F_HEALTH_MAX   0x354u
#define F_HEALTH_TANK  0x358u
#define F_METER        0x364u
#define F_BLAZIN_TIME  0x368u
#define F_CONTROL      0x937u            /* bit 0: CPU */
#define F_BLAZIN       0x93Eu            /* bit 0 ready, bit 1 active */
#define METER_READY    0x42C60000u       /* 99.0 */
#define BLAZIN_TIME    0x44E10000u       /* 1800.0 */
#define G_PHASE        0x003B8F30u       /* 0 starting, 1 fighting, 2 decided, 3 finished */
#define BLAZIN_AFTER   300               /* fighting steps (five seconds) before max_blazin starts */
#define REGEN_RETURN   0x001A5807u       /* sub_001A5730's call to sub_001A50B0 that gives health back */

#define FIGHT_MODELS   0x00375120u       /* CLogic (0x349760) + 0x2B9C0: each slot's model, sub_00086F20 */
#define MODEL_FLAGS    0xABB4u           /* bit 0: built from parts, drawn through MODEL_PARTS */
#define MODEL_PARTS    0x9D0u
#define NODE_SCALE     0x2E0u            /* a scene node's scale vec3, read by sub_00025180 */

extern void sub_000B0130(void);          /* Blazin' ready: HUD and sound (slot, 1) */
extern void sub_001A5190(void);          /* momentum gain (record, amount) */
extern void sub_00014C20(void);          /* thiscall on a profile slot, after a style change */

static const char *const k_targets[] = { "off", "player 1", "every player", "everyone", NULL };
static const char *const k_sizes[] = { "normal", "giant", "tiny", NULL };

static volatile int s_health, s_blazin, s_no_regen, s_size, s_hero = -1;
static int s_log = -1, s_probe = -1, s_verbose;
static volatile int s_in_story;

int cheats_in_story(void) { return s_in_story != 0; }
static long long s_steps, s_fighting;     /* simulation steps since the match started; of them, fighting */

static volatile int s_request, s_req_main, s_req_h1, s_req_h2;
static void probe_setup(const char *when);
static char s_status[160];

static int env_on(const char *name)
{
    const char *v = getenv(name);
    return v && *v && strcmp(v, "0") != 0;
}

void cheats_apply_settings(void)
{
    static int logged = -1;
    int key;
    s_health = recomp_settings_get("cheats", "infinite_health", 0);
    s_blazin = recomp_settings_get("cheats", "max_blazin", 0);
    s_no_regen = recomp_settings_get("cheats", "no_regen", 0);
    s_size = recomp_settings_get("cheats", "fighter_size", 0);
    s_hero = recomp_settings_get("cheats", "story_hero", -1);
    key = s_health | s_blazin << 4 | s_no_regen << 8 | s_size << 12 | (s_hero + 1) << 16;
    if (key != logged) {
        logged = key;
        fprintf(stderr, "[CHEAT] infinite_health=%s max_blazin=%s no_regen=%d fighter_size=%s story_hero=%d\n",
                k_targets[s_health & 3], k_targets[s_blazin & 3], s_no_regen, k_sizes[s_size % 3], s_hero);
    }
}

/* ---- fights ------------------------------------------------------------------------------------ */

static uint32_t record(int slot) { return ACTORS + (uint32_t)slot * STRIDE; }
static int active(int slot) { return MEM32(record(slot) + F_SLOT) != 0xFFFFFFFFu; }
static int is_cpu(int slot) { return (MEM8(record(slot) + F_CONTROL) & 1) != 0; }

static float getf(uint32_t va)
{
    uint32_t bits = MEM32(va);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static void setf(uint32_t va, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof bits);
    MEM32(va) = bits;
}

/* A guest pointer worth following: the image and its data, or the contiguous heap at 0x80000000. */
static int guest_ptr(uint32_t v)
{
    return (v >= 0x10000u && v < 0x08000000u) || (v >= 0x80000000u && v < 0x88000000u);
}

/* Whether a fight cheat set to `who` applies to the fighter in `slot`. */
static int targets(int who, int slot)
{
    int i;
    if (who == CHEAT_NOBODY || slot < 0 || slot >= SLOTS || !active(slot))
        return 0;
    if (who == CHEAT_EVERYONE)
        return 1;
    if (is_cpu(slot))
        return 0;
    if (who == CHEAT_PLAYERS)
        return 1;
    for (i = 0; i < slot; i++)          /* player 1: the first fighter not played by the computer */
        if (active(i) && !is_cpu(i))
            return 0;
    return 1;
}

static int slot_of(uint32_t r)
{
    if (r < ACTORS || r >= ACTORS + SLOTS * STRIDE || (r - ACTORS) % STRIDE)
        return -1;
    return (int)((r - ACTORS) / STRIDE);
}

static int log_enabled(void)
{
    if (s_log < 0) {
        s_probe = env_on("RECOMP_CHEAT_PROBE");
        s_log = s_probe || env_on("RECOMP_CHEAT_LOG");
        s_verbose = getenv("RECOMP_CHEAT_LOG") && atoi(getenv("RECOMP_CHEAT_LOG")) >= 2;
    }
    return s_log;
}

/* The model object the fight draws for a slot (sub_000441C0 in a fight), or 0. */
static uint32_t fight_model(int slot)
{
    uint32_t m = MEM32(FIGHT_MODELS + (uint32_t)slot * 4u);
    return guest_ptr(m) ? m : 0;
}

static void apply_size(void)
{
    static int applied;
    float scale = s_size == CHEAT_SIZE_GIANT ? 1.4f : s_size == CHEAT_SIZE_TINY ? 0.6f : 1.0f;
    int slot;
    if (s_size == CHEAT_SIZE_NORMAL && !applied)
        return;
    for (slot = 0; slot < SLOTS; slot++) {
        uint32_t m = fight_model(slot), node;
        if (!active(slot) || !m)
            continue;
        /* sub_0009E4A0 draws a model through its own node at +0x30, or, for a model built from parts
         * (the Story's created hero: bit 0 of +0xABB4), through the node +0x9D0 points at. */
        node = (MEM32(m + MODEL_FLAGS) & 1) && guest_ptr(MEM32(m + MODEL_PARTS)) ? MEM32(m + MODEL_PARTS) : m + 0x30u;
        setf(node + NODE_SCALE, scale);
        setf(node + NODE_SCALE + 4, scale);
        setf(node + NODE_SCALE + 8, scale);
    }
    applied = s_size != CHEAT_SIZE_NORMAL;
}

void cheats_fight_step(void)
{
    int slot;
    s_steps++;
    if (MEM32(G_PHASE) == 1)
        s_fighting++;
    for (slot = 0; slot < SLOTS; slot++) {
        uint32_t r = record(slot);
        if (targets(s_health, slot) && getf(r + F_HEALTH) > 0.0f) {
            MEM32(r + F_HEALTH_TANK) = MEM32(r + F_HEALTH_MAX);
            MEM32(r + F_HEALTH) = MEM32(r + F_HEALTH_MAX);
        }
        /* Only once the fight has run five seconds: the computer starts a Blazin' it has as soon as it
         * can, and one started during the walk-in stopped the match there for good. */
        if (s_fighting >= BLAZIN_AFTER && targets(s_blazin, slot) && !(MEM8(r + F_BLAZIN) & 3)) {
            /* Momentum through the game's own gain, sub_001A5190(record, amount): it fills the meter,
             * sets the ready state and tells the HUD, as an earned meter does. If a scale in it left
             * the meter short, the same writes it makes when the meter fills. */
            uint32_t args[2] = { r, 0x447A0000u /* 1000.0 */ };
            static unsigned granted;
            guest_call(sub_001A5190, g_ecx, 2, args);
            if (log_enabled() && granted++ < 8)
                fprintf(stderr, "[CHEAT] Blazin' ready for slot %d through the gain: %s\n", slot,
                        MEM8(r + F_BLAZIN) & 1 ? "yes" : "no, set directly");
            if (!(MEM8(r + F_BLAZIN) & 1)) {
                uint32_t ready[2] = { (uint32_t)slot, 1 };
                MEM32(r + F_METER) = METER_READY;
                MEM32(r + F_BLAZIN_TIME) = BLAZIN_TIME;
                MEM8(r + F_BLAZIN) |= 1;
                guest_call(sub_000B0130, g_ecx, 2, ready);
            }
        }
    }
    apply_size();
    if (log_enabled() && (s_steps % 60 == 1 || (s_verbose && s_steps % 10 == 0))) {
        char line[512];
        size_t len = (size_t)snprintf(line, sizeof line, "[CHEAT] step %lld phase %u", s_steps, MEM32(G_PHASE));
        for (slot = 0; slot < SLOTS && len < sizeof line; slot++) {
            uint32_t r = record(slot);
            if (!active(slot))
                continue;
            len += (size_t)snprintf(line + len, sizeof line - len,
                                    " | slot %d cpu %d health %.1f/%.1f tank %.1f meter %.1f blazin %u", slot,
                                    is_cpu(slot), getf(r + F_HEALTH), getf(r + F_HEALTH_MAX),
                                    getf(r + F_HEALTH_TANK), getf(r + F_METER), MEM8(r + F_BLAZIN));
        }
        fprintf(stderr, "%s\n", line);
    }
}

void cheats_health_change(void)
{
    uint32_t r = MEM32(g_esp + 4);
    float delta = getf(g_esp + 8);
    int slot = slot_of(r);
    if (slot < 0)
        return;
    if (delta < 0.0f && targets(s_health, slot))
        setf(g_esp + 8, 0.0f);
    else if (delta > 0.0f && s_no_regen && MEM32(g_esp) == REGEN_RETURN)
        setf(g_esp + 8, 0.0f);
}

/* ---- Story ------------------------------------------------------------------------------------- */

static uint32_t story_slot(void)
{
    uint32_t gd = MEM32(MANAGERS + GAMEDATA_SLOT * 4u), p, i;
    if (!s_in_story || !guest_ptr(gd))
        return 0;
    p = MEM32(gd + 4);
    if (!guest_ptr(p))
        return 0;
    i = MEM32(p + SLOT_INDEX);
    return i < PROFILE_SLOTS ? p + SLOT_BASE + i * SLOT_STRIDE : 0;
}

uint32_t cheats_style_table_index(int main_style, int hybrid1, int hybrid2)
{
    /* sub_001A4700: byte [0x2EBF6F + hybrid2 + 6 * (hybrid1 + 6 * main)], hybrids -1 when empty. */
    return (uint32_t)(hybrid2 + 6 * (hybrid1 + 6 * main_style));
}

static int style_ok(int s, int hybrid) { return (s >= 0 && s < CHEAT_STYLE_COUNT) || (hybrid && s == CHEAT_STYLE_NONE); }

int cheats_story_info(CheatStory *out)
{
    uint32_t s = story_slot();
    memset(out, 0, sizeof *out);
    if (!s)
        return 0;
    out->valid = 1;
    out->cash = MEM32(s + S_CASH);
    out->devpts = MEM32(s + S_DEVPTS);
    out->style[0] = MEM8(s + S_STYLES);
    out->style[1] = MEM8(s + S_STYLES + 1);
    out->style[2] = MEM8(s + S_STYLES + 2);
    return 1;
}

void cheats_story_request(int action, int main_style, int hybrid1, int hybrid2)
{
    s_req_main = main_style;
    s_req_h1 = hybrid1;
    s_req_h2 = hybrid2;
    snprintf(s_status, sizeof s_status, "Waiting for the game...");
    s_request = action;
}

const char *cheats_story_status(void) { return s_status; }

static void probe_story(const char *when)
{
    uint32_t s = story_slot(), gd = MEM32(MANAGERS + GAMEDATA_SLOT * 4u);
    int m, a, b;
    if (!s) {
        fprintf(stderr, "[CHEAT-PROBE] %s no story slot: story %d gd 0x%08X p 0x%08X index 0x%08X\n", when,
                s_in_story, gd, gd ? MEM32(gd + 4) : 0, gd && MEM32(gd + 4) ? MEM32(MEM32(gd + 4) + SLOT_INDEX) : 0);
        return;
    }
    m = MEM8(s + S_STYLES), a = MEM8(s + S_STYLES + 1), b = MEM8(s + S_STYLES + 2);
    fprintf(stderr, "[CHEAT-PROBE] %s story slot 0x%08X cash %u devpts %u styles %u %u %u id %u table %u "
                    "stats %u %u %u %u %u %u\n",
            when, s, MEM32(s + S_CASH), MEM32(s + S_DEVPTS), m, a, b, MEM8(s + S_STYLE_ID),
            m < CHEAT_STYLE_COUNT ? MEM8(STYLE_TABLE + cheats_style_table_index(m, a == 0xFF ? -1 : a,
                                                                                b == 0xFF ? -1 : b)) : 0xFFFFu,
            MEM32(s + 0xFC), MEM32(s + 0x100), MEM32(s + 0x104), MEM32(s + 0x108), MEM32(s + 0x10C),
            MEM32(s + 0x110));
}

static void story_apply(void)
{
    int action = s_request, m = s_req_main, a = s_req_h1, b = s_req_h2;
    uint32_t s;
    if (!action)
        return;
    s_request = 0;
    s = story_slot();
    if (!s) {
        snprintf(s_status, sizeof s_status, "Not applied: no Story profile is loaded.");
        return;
    }
    switch (action) {
    case CHEAT_STORY_CASH:
        MEM32(s + S_CASH) = CHEAT_MAX_CASH;
        snprintf(s_status, sizeof s_status, "Cash set to $%u.", CHEAT_MAX_CASH);
        break;
    case CHEAT_STORY_DEVPTS:
        MEM32(s + S_DEVPTS) = CHEAT_MAX_DEVPTS;
        snprintf(s_status, sizeof s_status, "Development points set to %u.", CHEAT_MAX_DEVPTS);
        break;
    case CHEAT_STORY_STYLES: {
        uint32_t id, args = 0;
        if (!style_ok(m, 0) || !style_ok(a, 1) || !style_ok(b, 1) || (a != CHEAT_STYLE_NONE && a == m) ||
            (b != CHEAT_STYLE_NONE && (b == m || b == a)) || (a == CHEAT_STYLE_NONE && b != CHEAT_STYLE_NONE)) {
            snprintf(s_status, sizeof s_status, "Not applied: choose different styles, the first hybrid before the second.");
            return;
        }
        MEM8(s + S_STYLES) = (uint8_t)m;
        MEM8(s + S_STYLES + 1) = (uint8_t)a;
        MEM8(s + S_STYLES + 2) = (uint8_t)b;
        id = MEM8(STYLE_TABLE + cheats_style_table_index(m, a == CHEAT_STYLE_NONE ? -1 : a,
                                                         b == CHEAT_STYLE_NONE ? -1 : b));
        MEM8(s + S_STYLE_ID) = (uint8_t)(id + 1);
        guest_call(sub_00014C20, s, 0, &args);
        snprintf(s_status, sizeof s_status, "Fighting styles changed.");
        break;
    }
    default:
        return;
    }
    fprintf(stderr, "[CHEAT] Story: %s\n", s_status);
    if (log_enabled())
        probe_story("applied");
}

void cheats_game_tick(void) { story_apply(); }

/* RECOMP_CHEAT_STORY_TEST=cash,devpts,styles=M/H1/H2: the overlay's Story buttons, pressed by a test at
 * the first crib screen (styles by number, 255 for an empty hybrid). A test control; off unless set. */
static void story_test(const char *text)
{
    static int done;
    const char *v = getenv("RECOMP_CHEAT_STORY_TEST");
    if (done || !v || !*v || !strstr(text, "GetScreenInfo(story/crib"))
        return;
    done = 1;
    if (strstr(v, "cash")) {
        cheats_story_request(CHEAT_STORY_CASH, 0, 0, 0);
        story_apply();
    }
    if (strstr(v, "devpts")) {
        cheats_story_request(CHEAT_STORY_DEVPTS, 0, 0, 0);
        story_apply();
    }
    if (strstr(v, "styles=")) {
        int m = -1, a = -1, b = -1;
        if (sscanf(strstr(v, "styles=") + 7, "%d/%d/%d", &m, &a, &b) == 3) {
            cheats_story_request(CHEAT_STORY_STYLES, m, a, b);
            story_apply();
        }
    }
}

void cheats_on_funccall(const char *text)
{
    static int was_story = -1;
    /* Story runs from Game.StartStoryMode until the front end is back at its own menus (intMain/...).
     * Its map is a battle/ screen and its fights keep no screen, so the screen name alone cannot tell. */
    if (strncmp(text, "Game.StartStoryMode", 19) == 0)
        s_in_story = 1;
    else if (strncmp(text, "Control.GetScreenInfo(intMain/", 30) == 0 || strncmp(text, "Game.StartMainMenu", 18) == 0)
        s_in_story = 0;
    if (log_enabled() && s_in_story != was_story) {
        fprintf(stderr, "[CHEAT] %s Story (%s)\n", s_in_story ? "in" : "out of", text);
        was_story = s_in_story;
    }
    if (s_probe && strncmp(text, "Control.GetScreenInfo(", 22) == 0)
        probe_story(text + 22);
    story_apply();
    story_test(text);
}

void cheats_match_start(void)
{
    int slot;
    s_steps = s_fighting = 0;
    if (!log_enabled())
        return;
    fprintf(stderr, "[CHEAT] match start, story %d:", s_in_story);
    for (slot = 0; slot < SLOTS; slot++)
        fprintf(stderr, " slot %d char %d cpu-flag %u;", slot, (int)MEM32(0x3B90B8u + (uint32_t)slot * 4u),
                MEM32(0x3B9088u + (uint32_t)slot * 4u));
    fprintf(stderr, "\n");
    if (s_probe) {
        probe_story("match");
        probe_setup("match start");
    }
}

/* The Story hero swap, in the match setup (game data + 0xC0, filled by sub_00020270, one slot at a time).
 * In Story the setup is built by sub_000C1240 during Game.LoadNextRunModule, before Game.StartGame, and the
 * created hero's slot is marked by the byte gd+0x1E8+slot (= slot + 1), takes its coreID (gd+0x168+4*slot,
 * the moveset: character index = coreID - 1) from the profile's style template and its model from the
 * profile's parts. Every other slot copies its fighter.xml entry: coreID, model name (gd+0x208+4*slot) and
 * shadow name (gd+0x228+4*slot), pointers into the game's own loaded copy of fighter.xml. At Game.StartGame,
 * before the match is made from the setup (sub_0008BB00: sub_0008B730, sub_000871F0, sub_0008B440), the hero's
 * slot is rewritten the same way for the chosen fighter: its coreID, the addresses of its model and shadow
 * names in that loaded copy, and the mark cleared, so the fight builds that fighter's own model and moves.
 * The hero's name (gd+0x218+4*slot) is kept. The profile is not touched. */
typedef struct HeroEntry { int core; char model[24], shadow[24]; } HeroEntry;
static HeroEntry s_entries[CHEAT_FIGHTER_MAX + 1];
static int s_entries_read;

static void xml_field(const char *block, const char *end, const char *tag, char *out, size_t size)
{
    char open[32], close[32];
    const char *a, *b;
    snprintf(open, sizeof open, "<%s>", tag);
    snprintf(close, sizeof close, "</%s>", tag);
    out[0] = 0;
    a = strstr(block, open);
    if (!a || a > end)
        return;
    a += strlen(open);
    b = strstr(a, close);
    if (!b || b > end || (size_t)(b - a) >= size)
        return;
    memcpy(out, a, (size_t)(b - a));
    out[b - a] = 0;
}

/* game/fighter.xml, the file the game reads, once: each fighter's coreID, model and shadow by <ID>. */
static void read_entries(void)
{
    FILE *f;
    char *text;
    long size;
    const char *at;
    if (s_entries_read)
        return;
    s_entries_read = 1;
    f = fopen("game/fighter.xml", "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = size > 0 && size < (8 << 20) ? (char *)malloc((size_t)size + 1) : NULL;
    if (text && fread(text, 1, (size_t)size, f) == (size_t)size) {
        text[size] = 0;
        for (at = strstr(text, "<fighter>"); at; at = strstr(at + 9, "<fighter>")) {
            const char *end = strstr(at, "</fighter>");
            char id[8], core[8];
            if (!end)
                break;
            xml_field(at, end, "ID", id, sizeof id);
            xml_field(at, end, "coreID", core, sizeof core);
            if (id[0] && atoi(id) >= 0 && atoi(id) <= CHEAT_FIGHTER_MAX) {
                HeroEntry *e = &s_entries[atoi(id)];
                e->core = atoi(core);
                xml_field(at, end, "model", e->model, sizeof e->model);
                xml_field(at, end, "shadow", e->shadow, sizeof e->shadow);
            }
        }
    }
    free(text);
    fclose(f);
}

/* The guest address of `text` as a whole string in [from, from + span), or 0. */
static uint32_t find_string(uint32_t from, uint32_t span, const char *text)
{
    size_t n = strlen(text);
    uint32_t a;
    if (!n)
        return 0;
    for (a = from; a + n + 1 < from + span; a++)
        if (MEM8(a) == (uint8_t)text[0] && MEM8(a - 1) == 0 && MEM8(a + n) == 0) {
            size_t i = 1;
            while (i < n && MEM8(a + (uint32_t)i) == (uint8_t)text[i])
                i++;
            if (i == n)
                return a;
        }
    return 0;
}

void cheats_start_game(uint32_t gd)
{
    int hero = s_hero, slot;
    if (!s_in_story || hero < 0 || hero > CHEAT_FIGHTER_MAX || hero == CHEAT_CREATED_HERO)
        return;
    read_entries();
    for (slot = 0; slot < SLOTS; slot++) {
        const HeroEntry *e = &s_entries[hero];
        uint32_t pool, model, shadow, other = 0;
        int i;
        if (MEM8(gd + 0x1E8u + (uint32_t)slot) != (uint8_t)(slot + 1))
            continue;
        /* The loaded fighter.xml strings: search around another slot's model name, which points into them. */
        for (i = 0; i < SLOTS && !other; i++)
            if (i != slot && guest_ptr(MEM32(gd + 0x208u + (uint32_t)i * 4u)))
                other = MEM32(gd + 0x208u + (uint32_t)i * 4u);
        if (!e->core || !e->model[0] || !other) {
            fprintf(stderr, "[CHEAT] Story hero: fighter %d not available (%s)\n", hero,
                    !e->core ? "not in game/fighter.xml" : "no opponent to find the fighter list from");
            return;
        }
        pool = other > 0x80000000u + 0x40000u ? other - 0x40000u : 0x80000000u;
        model = find_string(pool, 0x80000u, e->model);
        shadow = e->shadow[0] ? find_string(pool, 0x80000u, e->shadow) : 0;
        if (!model) {
            fprintf(stderr, "[CHEAT] Story hero: model %s of fighter %d not found\n", e->model, hero);
            return;
        }
        MEM32(gd + 0x168u + (uint32_t)slot * 4u) = (uint32_t)e->core;
        MEM32(gd + 0x188u + (uint32_t)slot * 4u) = (uint32_t)e->core;   /* coreID + 1000 * outfit: the first */
        MEM32(gd + 0x208u + (uint32_t)slot * 4u) = model;
        if (shadow)
            MEM32(gd + 0x228u + (uint32_t)slot * 4u) = shadow;
        MEM8(gd + 0x1E8u + (uint32_t)slot) = 0;
        fprintf(stderr, "[CHEAT] Story hero: slot %d plays as fighter %d (coreID %d, %s)\n", slot, hero, e->core,
                e->model);
    }
}

void cheats_start_game_done(uint32_t gd)
{
    if (log_enabled() && guest_ptr(gd))
        fprintf(stderr, "[CHEAT] match setup: created %u %u, coreID %d %d\n", MEM8(gd + 0x1E8), MEM8(gd + 0x1E9),
                (int)MEM32(gd + 0x168), (int)MEM32(gd + 0x16C));
    probe_setup("after StartGame");
}

static void probe_setup(const char *when)
{
    uint32_t gd = MEM32(MANAGERS + GAMEDATA_SLOT * 4u);
    if (!s_probe || !guest_ptr(gd))
        return;
    fprintf(stderr, "[CHEAT-PROBE] %s mode %u ids %d %d created %u %u core %d %d model 0x%08X 0x%08X\n", when,
            MEM32(gd + 0x344), (int8_t)MEM8(gd + 0x1AAA), (int8_t)MEM8(gd + 0x1AAF), MEM8(gd + 0x1E8),
            MEM8(gd + 0x1E9), (int)MEM32(gd + 0x168), (int)MEM32(gd + 0x16C), MEM32(gd + 0x208), MEM32(gd + 0x20C));
}

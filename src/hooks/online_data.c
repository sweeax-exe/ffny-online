/* The online hub's lists. See online_data.h. */
#define _CRT_SECURE_NO_WARNINGS
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "online_data.h"
#if defined(_WIN32)
#include <windows.h>
#endif

#define MAX_FIGHTERS 160
#define MAX_VENUES   48

static const OnlineMode k_modes[] = {
    { 0, "One on One", 2 },
    { 2, "3-Way", 3 },
    { 3, "Free for All", 4 },
};

static OnlineFighter g_fighters[MAX_FIGHTERS];
static OnlineVenue g_venues[MAX_VENUES];
static int g_fighter_count, g_venue_count;
#if !defined(_WIN32)
static int g_loaded;
#endif

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *text;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > (8 << 20)) { fclose(f); return NULL; }
    text = (char *)malloc((size_t)size + 1);
    if (text && fread(text, 1, (size_t)size, f) != (size_t)size) { free(text); text = NULL; }
    if (text) text[size] = 0;
    fclose(f);
    return text;
}

/* The text of <tag>...</tag> inside [from, to), trimmed, into out. 0 if absent. */
static int field(const char *from, const char *to, const char *tag, char *out, int size)
{
    char open[32];
    const char *a, *b;
    int n;
    snprintf(open, sizeof(open), "<%s>", tag);
    a = strstr(from, open);
    if (!a || a >= to) return 0;
    a += strlen(open);
    b = strchr(a, '<');
    if (!b || b > to) return 0;
    while (a < b && isspace((unsigned char)*a)) a++;
    while (b > a && isspace((unsigned char)b[-1])) b--;
    n = (int)(b - a) < size - 1 ? (int)(b - a) : size - 1;
    memcpy(out, a, (size_t)n);
    out[n] = 0;
    return 1;
}

static const char *style_name(const char *code)
{
    if (!strcmp(code, "STF")) return "Street fighting";
    if (!strcmp(code, "MAR")) return "Martial arts";
    if (!strcmp(code, "KIC")) return "Kickboxing";
    if (!strcmp(code, "WRE")) return "Wrestling";
    if (!strcmp(code, "SUB")) return "Submissions";
    return NULL;
}

/* Battle mode resolves a fighter by the front-end ID of its record in the compiled database
 * (assets/djv2.sod, the Character table), not by fighter.xml. The two agree for every fighter but
 * four: Teck, Santos, O.E and Eric Sermon have other front-end IDs there, and their fighter.xml
 * IDs exist in no record, so a match set up with those left the slot empty and crashed at the
 * load (docs/research/crash-001B2480.md, section 11). The fighter.xml IDs those four's database
 * IDs have are unused placeholders ("EMPTY", "CP ..."), never offered. */
static const struct { int xml_id, fe_id; } k_battle_ids[] = {
    { 40, 85 },     /* Teck */
    { 41, 93 },     /* Santos */
    { 52, 92 },     /* O.E */
    { 78, 91 },     /* Eric Sermon */
};

/* Every front-end ID that has a Character record in the database (scripts/sod-characters.py lists
 * them from the player's dump; tests/unit/test_battle_roster.py pins this list to it and to the
 * relay's ValidFighters). A fighter.xml entry with a style but no record is not a Battle-mode
 * fighter: Gwen (39) crashed a live Free for All as the CPU that filled a free place (build 31,
 * WrestlerSpecInit with character index -2). */
static const unsigned char k_sod_fighters[] = {
    0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    27, 28, 30, 31, 32, 33, 34, 35, 36, 38, 43, 44, 45, 46, 47, 48, 50, 51, 54, 55, 56, 57, 58,
    59, 60, 61, 62, 64, 65, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 80, 81, 82, 83, 84, 85,
    91, 92, 93, 98,
};

/* The Battle-mode ID of a fighter.xml entry; -1 if Battle mode has no such fighter (a placeholder,
 * or an entry without a database record). */
static int battle_id(int xml_id)
{
    unsigned i;
    int id = xml_id;
    for (i = 0; i < sizeof(k_battle_ids) / sizeof(k_battle_ids[0]); i++) {
        if (k_battle_ids[i].xml_id == xml_id) id = k_battle_ids[i].fe_id;
        if (k_battle_ids[i].fe_id == xml_id) return -1;
    }
    for (i = 0; i < sizeof(k_sod_fighters); i++) if (k_sod_fighters[i] == id) return id;
    return -1;
}

/* "Doc (MAR)" -> name "Doc", style "Martial arts". Entries without a known style are not
 * selectable fighters (placeholders, story figures). */
static void load_fighters(void)
{
    char *text = read_file("game/fighter.xml"), *p;
    if (!text) return;
    for (p = strstr(text, "<fighter>"); p && g_fighter_count < MAX_FIGHTERS; p = strstr(p + 1, "<fighter>")) {
        char *end = strstr(p, "</fighter>"), disp[64], id[16], *paren;
        const char *style;
        OnlineFighter *f;
        int fe_id;
        if (!end) break;
        if (!field(p, end, "dispname", disp, sizeof(disp)) || !field(p, end, "ID", id, sizeof(id))) continue;
        paren = strrchr(disp, '(');
        if (!paren || strlen(paren) != 5 || paren[4] != ')') {
            /* Lauren is a Battle-mode fighter whose entry has no style (players asked for her) */
            if (strcmp(disp, "Lauren")) continue;
            style = "";
        } else {
            paren[4] = 0;
            style = style_name(paren + 1);
            if (!style) continue;
            while (paren > disp && paren[-1] == ' ') paren--;
            *paren = 0;
        }
        fe_id = battle_id(atoi(id));
        if (fe_id < 0) continue;
        f = &g_fighters[g_fighter_count++];
        f->id = fe_id;
        snprintf(f->name, sizeof(f->name), "%s", disp);
        f->style = style;
    }
    free(text);
}

/* The arenas: a "NNx Name" display name with a front-end ID, not cut, not cinema only. */
static void load_venues(void)
{
    char *text = read_file("game/venue.xml"), *p;
    if (!text) return;
    for (p = strstr(text, "<venue>"); p && g_venue_count < MAX_VENUES; p = strstr(p + 1, "<venue>")) {
        char *end = strstr(p, "</venue>"), disp[80], fe[16], *name, *paren;
        int fe_id, i, dup = 0;
        if (!end) break;
        if (!field(p, end, "dispname", disp, sizeof(disp)) || !field(p, end, "feID", fe, sizeof(fe))) continue;
        fe_id = atoi(fe);
        if (fe_id <= 0 || fe_id > 255) continue;
        if (!(isdigit((unsigned char)disp[0]) && isdigit((unsigned char)disp[1]) && isalpha((unsigned char)disp[2]) && disp[3] == ' '))
            continue;
        if (strstr(disp, "CUT") || strstr(disp, "Cinema")) continue;
        for (i = 0; i < g_venue_count; i++) if (g_venues[i].fe_id == fe_id) dup = 1;
        if (dup) continue;
        name = disp + 4;
        paren = strchr(name, '(');
        if (paren) {
            while (paren > name && paren[-1] == ' ') paren--;
            *paren = 0;
        }
        g_venues[g_venue_count].fe_id = fe_id;
        snprintf(g_venues[g_venue_count].name, sizeof(g_venues[0].name), "%s", name);
        g_venue_count++;
    }
    free(text);
}

#if defined(_WIN32)
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK load_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    load_fighters();
    load_venues();
    return TRUE;
}
static void load(void) { InitOnceExecuteOnce(&g_once, load_once, NULL, NULL); }
#else
static void load(void)
{
    if (g_loaded) return;
    g_loaded = 1;
    load_fighters();
    load_venues();
}
#endif

int online_fighters(const OnlineFighter **out) { load(); *out = g_fighters; return g_fighter_count; }
int online_venues(const OnlineVenue **out) { load(); *out = g_venues; return g_venue_count; }
int online_modes(const OnlineMode **out) { *out = k_modes; return (int)(sizeof(k_modes) / sizeof(k_modes[0])); }

int online_fighter_id(int id)
{
    unsigned i;
    for (i = 0; i < sizeof(k_battle_ids) / sizeof(k_battle_ids[0]); i++)
        if (k_battle_ids[i].xml_id == id) return k_battle_ids[i].fe_id;
    return id;
}

const char *online_fighter_name(int id)
{
    static char unknown[4][24];
    static int next;
    int i;
    load();
    for (i = 0; i < g_fighter_count; i++) if (g_fighters[i].id == id) return g_fighters[i].name;
    next = (next + 1) % 4;
    snprintf(unknown[next], sizeof(unknown[0]), "Fighter %d", id);
    return unknown[next];
}

const char *online_venue_name(int fe_id)
{
    static char unknown[4][24];
    static int next;
    int i;
    load();
    for (i = 0; i < g_venue_count; i++) if (g_venues[i].fe_id == fe_id) return g_venues[i].name;
    next = (next + 1) % 4;
    snprintf(unknown[next], sizeof(unknown[0]), "Venue %d", fe_id);
    return unknown[next];
}

const OnlineMode *online_mode(int type)
{
    unsigned i;
    for (i = 0; i < sizeof(k_modes) / sizeof(k_modes[0]); i++) if (k_modes[i].type == type) return &k_modes[i];
    return NULL;
}

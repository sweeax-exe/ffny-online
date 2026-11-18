/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Build 96: character groups, the game side (char_groups.h): content.ini's groups, cached, and the version last
 * picked per group (<data>/character-choices.ini, "group.<key>=<pack id>"). */
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "char_groups.h"
#include "pc_content.h"

#if defined(_WIN32)
#define CG_SEP "\\"
#else
#define CG_SEP "/"
#endif

static CharGroupFighter g_fighters[CG_MAX];
static int g_count;
static unsigned long long g_read;

static unsigned long long cg_now_ms(void)
{
#if defined(_WIN32)
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
#endif
}

/* RECOMP_TEST_CHAR_GROUPS=<feID>:<character>[:<version>][;...]: groups over content.ini's (tests, screenshots). */
static void test_groups(PcContentPack *packs, int n)
{
    const char *t = getenv("RECOMP_TEST_CHAR_GROUPS");
    char item[256];
    if (!t || !*t) return;
    while (*t) {
        size_t k = strcspn(t, ";");
        char *a, *b;
        int fe;
        snprintf(item, sizeof item, "%.*s", (int)(k < sizeof item ? k : sizeof item - 1), t);
        t += k + (t[k] == ';');
        if (!(a = strchr(item, ':'))) continue;
        *a++ = 0;
        fe = atoi(item);
        if ((b = strchr(a, ':')) != NULL) *b++ = 0;
        for (int i = 0; i < n; i++)
            if (packs[i].fighter_fe_id == fe && !packs[i].skin) {
                snprintf(packs[i].character, sizeof packs[i].character, "%s", a);
                snprintf(packs[i].version_name, sizeof packs[i].version_name, "%s", b ? b : "");
            }
    }
}

static void refresh(void)
{
    static PcContentPack *packs;
    int n;
    if (g_read && cg_now_ms() - g_read < 3000) return;
    g_read = cg_now_ms();
    if (!packs && !(packs = (PcContentPack *)malloc(sizeof(PcContentPack) * PC_CONTENT_MAX))) return;
    n = pc_content_list(packs, PC_CONTENT_MAX);
    test_groups(packs, n);
    g_count = 0;
    for (int i = 0; i < n && g_count < CG_MAX; i++) {
        PcContentPack *p = &packs[i];
        CharGroupFighter *f = &g_fighters[g_count];
        if (!p->character[0] || p->skin || p->fighter_fe_id <= 0 || strcmp(p->type, "new-fighter")) continue;
        memset(f, 0, sizeof *f);
        f->fe_id = p->fighter_fe_id;
        f->order = p->order;
        snprintf(f->id, sizeof f->id, "%s", p->id);
        snprintf(f->name, sizeof f->name, "%s", p->name);
        snprintf(f->authors, sizeof f->authors, "%s", p->authors);
        snprintf(f->credits, sizeof f->credits, "%s", p->credits);
        snprintf(f->character, sizeof f->character, "%s", p->character);
        snprintf(f->version_name, sizeof f->version_name, "%s", p->version_name);
        char_group_key(f->character, f->key, sizeof f->key);
        if (f->key[0]) g_count++;
    }
    {
        static int logged = -1;
        if (logged != g_count) {
            logged = g_count;
            fprintf(stderr, "[GROUPS] %d grouped community fighter(s)\n", g_count);
        }
    }
}

int char_groups_fighters(const CharGroupFighter **out)
{
    refresh();
    if (out) *out = g_fighters;
    return g_count;
}

const CharGroupFighter *char_groups_find(int fe_id)
{
    refresh();
    for (int i = 0; i < g_count; i++) if (g_fighters[i].fe_id == fe_id) return &g_fighters[i];
    return NULL;
}

static int choices_path(char *out, size_t size)
{
    const char *d = getenv("DEFJAM_DATA");
    if (!d || !*d) return 0;
    snprintf(out, size, "%s" CG_SEP "character-choices.ini", d);
    return 1;
}

/* The choices, read again at most every 3 s (the grid asks every frame); char_groups_remember updates them at once. */
#define CG_CHOICES 256
static struct { char key[CG_NAME]; char pack[96]; } g_choice[CG_CHOICES];
static int g_choice_n;
static unsigned long long g_choice_read;

static void read_choices(void)
{
    char path[1024], line[256], k[CG_NAME], p[96];
    FILE *f;
    if (g_choice_read && cg_now_ms() - g_choice_read < 3000) return;
    g_choice_read = cg_now_ms();
    g_choice_n = 0;
    if (!choices_path(path, sizeof path) || !(f = fopen(path, "rb"))) return;
    while (fgets(line, sizeof line, f)) {
        int i;
        if (!char_groups_parse_choice(line, k, sizeof k, p, sizeof p)) continue;
        for (i = 0; i < g_choice_n && strcmp(g_choice[i].key, k); i++) {}
        if (i == g_choice_n) {
            if (g_choice_n >= CG_CHOICES) continue;
            g_choice_n++;
        }
        snprintf(g_choice[i].key, sizeof g_choice[i].key, "%s", k);       /* the last line of a group wins */
        snprintf(g_choice[i].pack, sizeof g_choice[i].pack, "%s", p);
    }
    fclose(f);
}

void char_groups_remembered(const char *key, char *pack, size_t size)
{
    if (!size) return;
    pack[0] = 0;
    if (!key || !*key) return;
    read_choices();
    for (int i = 0; i < g_choice_n; i++)
        if (!strcmp(g_choice[i].key, key)) snprintf(pack, size, "%s", g_choice[i].pack);
}

void char_groups_remember(const char *key, const char *pack)
{
    char path[1024], tmp[1100], line[256], k[CG_NAME], p[96];
    FILE *f, *o;
    if (!key || !*key || !pack || !choices_path(path, sizeof path)) return;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (!(o = fopen(tmp, "wb"))) return;
    if ((f = fopen(path, "rb")) != NULL) {
        while (fgets(line, sizeof line, f))
            if (!char_groups_parse_choice(line, k, sizeof k, p, sizeof p) || strcmp(k, key)) {
                if (line[0] == ';' || strchr(line, '=')) fputs(line, o);
            }
        fclose(f);
    } else {
        fputs("; FFNY Online: the version picked last of each character group (its card shows it)\n", o);
    }
    fprintf(o, "group.%s=%s\n", key, pack);
    fclose(o);
    g_choice_read = 0;      /* read again at the next ask */
#if defined(_WIN32)
    MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING);
#else
    rename(tmp, path);
#endif
}

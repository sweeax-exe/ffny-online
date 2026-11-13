/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* FFNY Online's community content: the catalogue packs the launcher synced (pc_content.h,
 * docs/content-packs.md). Read-only here: setup/mods.py owns the files, this file reads its
 * content.ini, the composed files' files.ini and the player's switches, and tells the texture
 * loader, the file system and the netplay which packs are on. */
#define _CRT_SECURE_NO_WARNINGS
#include "pc_content.h"
#include "recomp_settings.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#define HOST_SEP "\\"
#else
#include <sys/stat.h>
#define HOST_SEP "/"
#endif
#if !defined(DEFJAM_APP)
#if defined(_WIN32)
typedef WCHAR content_host_char;
#else
#include "host.h"
typedef char content_host_char;
#endif
#include "netplay.h"
/* The toolkit's game-file redirect (kernel.h, not on this target's include path). */
extern BOOL (*g_xbox_game_file_redirect)(const char *relative, content_host_char *host_path, DWORD size);
#endif

#define CONTENT_FILES_MAX 64

static const char *data_root(void)
{
    const char *d = getenv("DEFJAM_DATA");
    return d && *d ? d : NULL;
}

static int folder_exists(const char *path)
{
#if defined(_WIN32)
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return !stat(path, &st) && S_ISDIR(st.st_mode);
#endif
}

static char *trim(char *s)
{
    char *e;
    while (*s && isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int valid_id(const char *id)
{
    size_t n = strlen(id), i;
    if (!n || n > 80) return 0;
    for (i = 0; i < n; i++)
        if (!(islower((unsigned char)id[i]) || isdigit((unsigned char)id[i]) || id[i] == '-')) return 0;
    return id[0] != '-' && id[n - 1] != '-';
}

/* <data>/mod-manager/content-files/files.ini (setup/content_compose.py): the packs it applied (a
 * comma-separated list) and, with files, the game-relative paths it holds. Returns 0 without one. */
static int read_files_ini(const char *root, char *applied, size_t applied_size,
                          char (*files)[256], int max_files, int *nfiles)
{
    char path[1024], line[1200];
    FILE *f;
    int in_section = 0, ok = 0;
    if (applied_size) applied[0] = 0;
    if (nfiles) *nfiles = 0;
    if (!root) return 0;
    snprintf(path, sizeof path, "%s" HOST_SEP "mod-manager" HOST_SEP "content-files" HOST_SEP "files.ini", root);
    if (!(f = fopen(path, "rb"))) return 0;
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line), *eq;
        if (*s == '[') { in_section = !strcmp(s, "[content-files]"); continue; }
        if (!in_section || !(eq = strchr(s, '='))) continue;
        *eq = 0;
        if (!strcmp(s, "schema")) ok = !strcmp(eq + 1, "1");
        else if (!strcmp(s, "applied")) snprintf(applied, applied_size, "%s", eq + 1);
        else if (!strcmp(s, "file") && files && nfiles && *nfiles < max_files) {
            const char *v = eq + 1;
            size_t n = strlen(v), i;
            int safe = n > 0 && n < 256 && v[0] != '/' && v[0] != '.' && !strstr(v, "..") && !strchr(v, ':');
            for (i = 0; safe && i < n; i++)
                safe = isalnum((unsigned char)v[i]) || v[i] == '_' || v[i] == '-' || v[i] == '.' || v[i] == '/';
            if (safe) snprintf(files[(*nfiles)++], 256, "%s", v);
        }
    }
    fclose(f);
    if (!ok) { if (applied_size) applied[0] = 0; if (nfiles) *nfiles = 0; }
    return ok;
}

static int in_list(const char *list, const char *id)
{
    size_t k = strlen(id);
    const char *q;
    for (q = list; q && *q; q = strchr(q, ',') ? strchr(q, ',') + 1 : NULL)
        if (!strncmp(q, id, k) && (q[k] == ',' || !q[k])) return 1;
    return 0;
}

static int by_order(const void *a, const void *b)
{
    const PcContentPack *x = (const PcContentPack *)a, *y = (const PcContentPack *)b;
    if (x->order != y->order) return x->order < y->order ? -1 : 1;
    return strcmp(x->id, y->id);
}

int pc_content_is_off(const char *id)
{
    char list[RECOMP_SETTING_TEXT_MAX];
    size_t n = strlen(id);
    const char *p;
    recomp_settings_get_text("content", "off", list, sizeof list, "");
    for (p = list; *p;) {
        while (*p == ',' || *p == ' ') p++;
        if (!strncmp(p, id, n) && (p[n] == ',' || p[n] == ' ' || !p[n])) return 1;
        while (*p && *p != ',') p++;
    }
    return 0;
}

void pc_content_set_off(const char *id, int off)
{
    char list[RECOMP_SETTING_TEXT_MAX], out[RECOMP_SETTING_TEXT_MAX] = "", item[128];
    const char *p;
    int o = 0;
    recomp_settings_get_text("content", "off", list, sizeof list, "");
    for (p = list; *p;) {
        int k = 0;
        while (*p == ',' || *p == ' ') p++;
        while (*p && *p != ',' && k < (int)sizeof item - 1) item[k++] = *p++;
        item[k] = 0;
        if (!k || !strcmp(trim(item), id)) continue;
        o += snprintf(out + o, sizeof out - (size_t)o, "%s%s", o ? "," : "", trim(item));
        if (o >= (int)sizeof out) return;
    }
    if (off) o += snprintf(out + o, sizeof out - (size_t)o, "%s%s", o ? "," : "", id);
    if (o >= (int)sizeof out) return;
    recomp_settings_set_text("content", "off", out);
}

int pc_content_list(PcContentPack *out, int max)
{
    char path[1024], line[1200];
    const char *root = data_root();
    FILE *f;
    int n = 0, cur = -1;
    if (!root || max <= 0) return 0;
    snprintf(path, sizeof path, "%s" HOST_SEP "mod-manager" HOST_SEP "content.ini", root);
    if (!(f = fopen(path, "rb"))) return 0;
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line), *eq;
        if (*s == ';' || *s == '#' || !*s) continue;
        if (*s == '[') {
            char *end = strchr(s, ']');
            cur = -1;
            if (!end || strncmp(s, "[pack.", 6)) continue;
            *end = 0;
            if (n >= max || !valid_id(s + 6)) continue;
            cur = n++;
            memset(&out[cur], 0, sizeof out[cur]);
            snprintf(out[cur].id, sizeof out[cur].id, "%s", s + 6);
            continue;
        }
        if (cur < 0 || !(eq = strchr(s, '='))) continue;
        *eq = 0;
        {
            char *k = trim(s), *v = trim(eq + 1);
            PcContentPack *p = &out[cur];
#define CONTENT_TEXT(name) else if (!strcmp(k, #name)) snprintf(p->name, sizeof p->name, "%s", v)
            if (0) {}
            CONTENT_TEXT(name); CONTENT_TEXT(authors); CONTENT_TEXT(kind); CONTENT_TEXT(type); CONTENT_TEXT(version);
            CONTENT_TEXT(language);
            CONTENT_TEXT(content_sha256); CONTENT_TEXT(description);
            CONTENT_TEXT(bundle); CONTENT_TEXT(bundle_name);
            else if (!strcmp(k, "order")) p->order = atoi(v);
            else if (!strcmp(k, "composed")) p->composed = atoi(v) == 1;
            else if (!strcmp(k, "bytes")) p->bytes = atoll(v);
            else if (!strcmp(k, "fighter_fe_id")) p->fighter_fe_id = atoi(v);
            else if (!strcmp(k, "venue_fe_id")) p->venue_fe_id = atoi(v);
            CONTENT_TEXT(reviewed);
#undef CONTENT_TEXT
        }
    }
    fclose(f);
    {   /* keep only packs whose folder is there; the rest are not installed */
        static char applied[4096];
        int i, m = 0;
        read_files_ini(root, applied, sizeof applied, NULL, 0, NULL);
        for (i = 0; i < n; i++) {
            char dir[1024];
            snprintf(dir, sizeof dir, "%s" HOST_SEP "mods" HOST_SEP "%s", root, out[i].id);
            out[i].installed = folder_exists(dir);
            if (strcmp(out[i].kind, "content")) strcpy(out[i].kind, "replacement");
            out[i].off = !strcmp(out[i].kind, "replacement") && pc_content_is_off(out[i].id);
            out[i].applied = out[i].composed && in_list(applied, out[i].id);
            if (out[i].installed) out[m++] = out[i];
        }
        n = m;
    }
    qsort(out, (size_t)n, sizeof *out, by_order);
    return n;
}

uint32_t pc_content_print(void)
{
    static PcContentPack packs[PC_CONTENT_MAX];
    const char *test = getenv("RECOMP_TEST_CONTENT_PRINT");
    uint32_t h = 2166136261u;
    int n, i, any = 0;
    if (test && *test) return (uint32_t)strtoul(test, NULL, 16);
    n = pc_content_list(packs, PC_CONTENT_MAX);
    for (i = 0; i < n; i++) {
        const char *parts[3];
        int k;
        if (strcmp(packs[i].kind, "content")) continue;
        if (packs[i].composed && !packs[i].applied) continue;     /* not in this PC's game files */
        parts[0] = packs[i].id; parts[1] = packs[i].version; parts[2] = packs[i].content_sha256;
        for (k = 0; k < 3; k++) {
            const char *c;
            for (c = parts[k]; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
            h = (h ^ (uint8_t)(k < 2 ? 0 : '\n')) * 16777619u;
        }
        any = 1;
    }
    if (!any) return 0;
    return h ? h : 1;
}

/* ---- build 82: new community content is out; the NEW badge ---- */

int pc_content_news(char *names, int size)
{
    char path[1024], line[1200];
    const char *root = data_root(), *test = getenv("RECOMP_TEST_CONTENT_NEWS");
    FILE *f;
    int pending = 0, in_section = 0;
    if (names && size > 0) names[0] = 0;
    if (test && *test) {
        if (names && size > 0) snprintf(names, (size_t)size, "%s", atoi(test) > 0 ? "Doc Nine | The Block" : "");
        return atoi(test) > 0 ? atoi(test) : 0;
    }
    if (!root) return 0;
    snprintf(path, sizeof path, "%s" HOST_SEP "mod-manager" HOST_SEP "news.ini", root);
    if (!(f = fopen(path, "rb"))) return 0;
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line), *eq;
        if (*s == '[') { in_section = !strcmp(s, "[news]"); continue; }
        if (!in_section || !(eq = strchr(s, '='))) continue;
        *eq = 0;
        if (!strcmp(s, "pending")) pending = atoi(eq + 1);
        else if (!strcmp(s, "names") && names && size > 0) snprintf(names, (size_t)size, "%s", eq + 1);
    }
    fclose(f);
    return pending > 0 ? pending : 0;
}

/* Days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant's days_from_civil). */
static long days_from_civil(int y, int m, int d)
{
    long era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (long)y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int pc_content_days_since(const char *date, long today)
{
    int y, m, d;
    if (!date || strlen(date) != 10 || sscanf(date, "%4d-%2d-%2d", &y, &m, &d) != 3 || date[4] != '-' || date[7] != '-' ||
        y < 2000 || m < 1 || m > 12 || d < 1 || d > 31)
        return -1;
    return (int)(today - days_from_civil(y, m, d));
}

/* The packs with a new fighter or venue that are still new (accepted less than PC_CONTENT_NEW_DAYS ago), read
 * from content.ini at most every five seconds; the picked ones are left out at the lookup. */
typedef struct { int fighter, venue; char id[96]; } NewItem;
static NewItem g_new[PC_CONTENT_MAX];
static int g_new_count;
static unsigned long long g_new_read;

static unsigned long long now_ms(void)
{
#if defined(_WIN32)
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
#endif
}

static void refresh_new(void)
{
    static PcContentPack packs[PC_CONTENT_MAX];
    const char *test = getenv("RECOMP_TEST_CONTENT_NEW");
    int all = test && !strcmp(test, "all"), n, i;
    long today = (long)(time(NULL) / 86400);
    if (g_new_read && now_ms() - g_new_read < 5000) return;
    g_new_read = now_ms();
    g_new_count = 0;
    n = pc_content_list(packs, PC_CONTENT_MAX);
    for (i = 0; i < n && g_new_count < PC_CONTENT_MAX; i++) {
        int age = pc_content_days_since(packs[i].reviewed, today);
        if (!packs[i].fighter_fe_id && !packs[i].venue_fe_id) continue;
        if (!all && (age < 0 || age >= PC_CONTENT_NEW_DAYS)) continue;
        g_new[g_new_count].fighter = packs[i].fighter_fe_id;
        g_new[g_new_count].venue = packs[i].venue_fe_id;
        snprintf(g_new[g_new_count].id, sizeof g_new[0].id, "%s", packs[i].id);
        g_new_count++;
    }
}

static int picked(const char *id)
{
    char list[RECOMP_SETTING_TEXT_MAX];
    if (getenv("RECOMP_TEST_CONTENT_NEW") && !strcmp(getenv("RECOMP_TEST_CONTENT_NEW"), "all")) return 0;
    recomp_settings_get_text("content", "picked", list, sizeof list, "");
    return in_list(list, id);
}

/* The player picked this pack's fighter or venue: [content] picked gets its id (newest first, as many as fit). */
static void set_picked(const char *id)
{
    char list[RECOMP_SETTING_TEXT_MAX], out[RECOMP_SETTING_TEXT_MAX];
    const char *p;
    int o;
    recomp_settings_get_text("content", "picked", list, sizeof list, "");
    if (in_list(list, id)) return;
    o = snprintf(out, sizeof out, "%s", id);
    for (p = list; *p && o < (int)sizeof out;) {
        const char *e = strchr(p, ',');
        size_t k = e ? (size_t)(e - p) : strlen(p);
        if (k && o + 1 + (int)k < (int)sizeof out) o += snprintf(out + o, sizeof out - (size_t)o, ",%.*s", (int)k, p);
        p = e ? e + 1 : p + k;
    }
    recomp_settings_set_text("content", "picked", out);
}

int pc_content_new_fighter(int fe_id)
{
    int i;
    if (fe_id < 100 || fe_id > 127) return 0;
    refresh_new();
    for (i = 0; i < g_new_count; i++) if (g_new[i].fighter == fe_id) return !picked(g_new[i].id);
    return 0;
}

int pc_content_new_venue(int fe_id)
{
    int i;
    if (fe_id < 28 || fe_id > 63) return 0;
    refresh_new();
    for (i = 0; i < g_new_count; i++) if (g_new[i].venue == fe_id) return !picked(g_new[i].id);
    return 0;
}

void pc_content_picked_fighter(int fe_id)
{
    int i;
    if (fe_id < 100 || fe_id > 127) return;
    refresh_new();
    for (i = 0; i < g_new_count; i++) if (g_new[i].fighter == fe_id) set_picked(g_new[i].id);
}

void pc_content_picked_venue(int fe_id)
{
    int i;
    if (fe_id < 28 || fe_id > 63) return;
    refresh_new();
    for (i = 0; i < g_new_count; i++) if (g_new[i].venue == fe_id) set_picked(g_new[i].id);
}

int pc_content_language_blocked(const char *id)
{
    static PcContentPack packs[PC_CONTENT_MAX];
    int n = pc_content_list(packs, PC_CONTENT_MAX), i;
    for (i = 0; i < n; i++)
        if (!strcmp(packs[i].id, id)) return packs[i].off;
    return 0;
}

int pc_content_offline_only(void)
{
    const char *v = getenv("RECOMP_OFFLINE_ONLY");
    return v && *v && strcmp(v, "0");
}

#if !defined(DEFJAM_APP)
/* The composed files: <data>/mod-manager/content-files/<path> for each path files.ini lists; with a
 * language pack in use, <data>/mod-manager/content-files/<sub><path> for the paths the launcher
 * composed on top of that pack's translation (files.ini `lang=<id>|<path>`, pc_content_language_files). */
static char g_files[CONTENT_FILES_MAX][256];
static char g_files_sub[CONTENT_FILES_MAX][160];
static char g_files_dir[1024];
static int g_file_count;
static char g_lang_ids[CONTENT_FILES_MAX][96];
static char g_lang_paths[CONTENT_FILES_MAX][256];
static int g_lang_count;
static BOOL (*g_files_previous)(const char *relative, content_host_char *host_path, DWORD size);

/* files.ini's `lang=<id>|<path>` lines: which composed paths have a copy built with which language pack. */
static void read_lang_lines(const char *root)
{
    char path[1024], line[1200];
    FILE *f;
    int in_section = 0;
    g_lang_count = 0;
    snprintf(path, sizeof path, "%s" HOST_SEP "mod-manager" HOST_SEP "content-files" HOST_SEP "files.ini", root);
    if (!(f = fopen(path, "rb"))) return;
    while (fgets(line, sizeof line, f) && g_lang_count < CONTENT_FILES_MAX) {
        char *s = trim(line), *bar;
        if (*s == '[') { in_section = !strcmp(s, "[content-files]"); continue; }
        if (!in_section || strncmp(s, "lang=", 5) || !(bar = strchr(s + 5, '|'))) continue;
        *bar = 0;
        {   /* the pack id is one folder name (as pc_language.c accepts them), the path a listed file */
            const char *c;
            size_t n = strlen(s + 5);
            int safe = n > 0 && n < sizeof g_lang_ids[0] && s[5] != '.' && !strstr(s + 5, "..");
            for (c = s + 5; safe && *c; c++)
                safe = isalnum((unsigned char)*c) || *c == '_' || *c == '-' || *c == '.';
            if (!safe || !bar[1] || strlen(bar + 1) >= 256 || strstr(bar + 1, "..")) continue;
        }
        snprintf(g_lang_ids[g_lang_count], sizeof g_lang_ids[0], "%s", s + 5);
        snprintf(g_lang_paths[g_lang_count], sizeof g_lang_paths[0], "%s", bar + 1);
        g_lang_count++;
    }
    fclose(f);
}

static int same_game_path(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a == '\\' ? '/' : (char)tolower((unsigned char)*a);
        char y = *b == '\\' ? '/' : (char)tolower((unsigned char)*b);
        if (x != y) return 0;
    }
    return *a == *b;
}

static BOOL content_redirect(const char *relative, content_host_char *host_path, DWORD size)
{
    int i;
    for (i = 0; i < g_file_count; i++) {
        char host[1400];
        char *c;
        if (!same_game_path(relative, g_files[i])) continue;
        if (snprintf(host, sizeof host, "%s" HOST_SEP "%s%s", g_files_dir, g_files_sub[i], g_files[i]) >= (int)sizeof host) break;
#if defined(_WIN32)
        for (c = host; *c; c++) if (*c == '/') *c = '\\';
        if (!MultiByteToWideChar(CP_ACP, 0, host, -1, host_path, (int)size)) return FALSE;
#else
        (void)c;
        if ((size_t)snprintf(host_path, size, "%s", host) >= size) return FALSE;
#endif
        return TRUE;
    }
    return g_files_previous ? g_files_previous(relative, host_path, size) : FALSE;
}

static void serve_composed_files(const char *root)
{
    static char applied[4096];
    int i, n = 0;
    if (!root || !read_files_ini(root, applied, sizeof applied, g_files, CONTENT_FILES_MAX, &n) || !n) return;
    snprintf(g_files_dir, sizeof g_files_dir, "%s" HOST_SEP "mod-manager" HOST_SEP "content-files", root);
    g_file_count = 0;
    for (i = 0; i < n; i++) {
        char host[1400];
        FILE *f;
        if (snprintf(host, sizeof host, "%s" HOST_SEP "%s", g_files_dir, g_files[i]) >= (int)sizeof host) continue;
        if (!(f = fopen(host, "rb"))) { fprintf(stderr, "[CONTENT] composed file missing: %s\n", g_files[i]); continue; }
        fclose(f);
        if (g_file_count != i) memcpy(g_files[g_file_count], g_files[i], sizeof g_files[i]);
        g_files_sub[g_file_count][0] = 0;
        g_file_count++;
    }
    if (!g_file_count) return;
    read_lang_lines(root);
    g_files_previous = g_xbox_game_file_redirect;
    g_xbox_game_file_redirect = content_redirect;
    netplay_data_print_reset();     /* a print made before this (the launcher's online) missed the composed files */
    fprintf(stderr, "[CONTENT] %d composed file(s) served from %s (packs: %s)\n", g_file_count, g_files_dir,
            applied[0] ? applied : "-");
    for (i = 0; i < g_file_count; i++) fprintf(stderr, "[CONTENT]   %s\n", g_files[i]);
}

int pc_content_file(const char *game_path, char *out, int size)
{
    int i;
    if (!game_path || !out || size <= 0) return 0;
    for (i = 0; i < g_file_count; i++) {
        if (!same_game_path(game_path, g_files[i])) continue;
        return snprintf(out, (size_t)size, "%s" HOST_SEP "%s%s", g_files_dir, g_files_sub[i], g_files[i]) < size;
    }
    return 0;
}

int pc_content_language_files(const char *id, char (*paths)[256], int max)
{
    int i, j, n = 0;
    for (i = 0; i < g_file_count; i++) g_files_sub[i][0] = 0;
    if (!id || !*id || !g_file_count) return 0;
    for (i = 0; i < g_file_count; i++) {
        for (j = 0; j < g_lang_count; j++) {
            char host[1400];
            FILE *f;
            if (strcmp(g_lang_ids[j], id) || !same_game_path(g_lang_paths[j], g_files[i])) continue;
            if (snprintf(host, sizeof host, "%s" HOST_SEP "lang" HOST_SEP "%s" HOST_SEP "%s", g_files_dir, id,
                         g_files[i]) >= (int)sizeof host) continue;
            if (!(f = fopen(host, "rb"))) {
                fprintf(stderr, "[CONTENT] composed file missing: lang/%s/%s\n", id, g_files[i]);
                continue;
            }
            fclose(f);
            snprintf(g_files_sub[i], sizeof g_files_sub[i], "lang" HOST_SEP "%s" HOST_SEP, id);
            if (paths && n < max) snprintf(paths[n], 256, "%s", g_files[i]);
            n++;
            break;
        }
    }
    if (n) {
        fprintf(stderr, "[CONTENT] %d composed file(s) built on the language pack %s:", n, id);
        for (i = 0; i < g_file_count; i++) if (g_files_sub[i][0]) fprintf(stderr, " %s", g_files[i]);
        fprintf(stderr, "\n");
    }
    return n > max ? max : n;
}
#else
int pc_content_language_files(const char *id, char (*paths)[256], int max)
{
    (void)id; (void)paths; (void)max;
    return 0;
}

int pc_content_file(const char *game_path, char *out, int size)
{
    (void)game_path; (void)out; (void)size;
    return 0;
}
#endif

void pc_content_start(const char *root)
{
    static PcContentPack packs[PC_CONTENT_MAX];
    char list[4096] = "", *old;
    const char *tex_root = getenv("RECOMP_TEXTURE_ROOT");
    int n, i, o = 0, used = 0;
    (void)root;
    {
        const char *v = getenv("RECOMP_CONTENT");
        if (v && !strcmp(v, "0")) { fprintf(stderr, "[CONTENT] off for this run (RECOMP_CONTENT=0)\n"); return; }
    }
    n = pc_content_list(packs, PC_CONTENT_MAX);
    if (!n) { fprintf(stderr, "[CONTENT] no community packs installed\n"); return; }
#if !defined(DEFJAM_APP)
    /* fighter bodies, clothing, venues: the launcher composed them; a test texture root holds only its own */
    if (!(tex_root && *tex_root)) serve_composed_files(root ? root : data_root());
#endif
    old = getenv("RECOMP_TEXTURE_PACKS");
    for (i = 0; i < n; i++) {
        const PcContentPack *p = &packs[i];
        char manifest[1200];
        FILE *f;
        fprintf(stderr, "[CONTENT] %s %s (%s, %s)%s\n", p->id, p->version, p->kind, p->type,
                p->off ? ": off on this PC" : p->composed && !p->applied ? ": not applied (see the launcher)" : "");
        if (p->off || !strcmp(p->type, "language")) continue;   /* language packs: the Game language setting */
        /* a texture root of its own (Studio's test fight) holds only its own packs */
        if (tex_root && *tex_root) continue;
        if (root) {
            snprintf(manifest, sizeof manifest, "%s" HOST_SEP "mods" HOST_SEP "%s" HOST_SEP "manifest.ini", root, p->id);
            if (!(f = fopen(manifest, "rb"))) continue;
            fclose(f);
        }
        if (old && *old) {     /* listed by hand already */
            size_t k = strlen(p->id);
            const char *q;
            int listed = 0;
            for (q = old; q; q = strchr(q, ';') ? strchr(q, ';') + 1 : NULL)
                if (!strncmp(q, p->id, k) && (q[k] == ';' || !q[k])) listed = 1;
            if (listed) continue;
        }
        o += snprintf(list + o, sizeof list - (size_t)o, "%s%s", o ? ";" : "", p->id);
        if (o >= (int)sizeof list) { fprintf(stderr, "[CONTENT] texture pack list too long\n"); return; }
        used++;
    }
    if (!used) return;
    /* beneath the player's own texture packs (later names win), in catalogue order */
    if (old && *old) {
        char both[8192];
        if (snprintf(both, sizeof both, "%s;%s", list, old) >= (int)sizeof both) return;
#if defined(_WIN32)
        _putenv_s("RECOMP_TEXTURE_PACKS", both);
#else
        setenv("RECOMP_TEXTURE_PACKS", both, 1);
#endif
    } else {
#if defined(_WIN32)
        _putenv_s("RECOMP_TEXTURE_PACKS", list);
#else
        setenv("RECOMP_TEXTURE_PACKS", list, 1);
#endif
    }
    fprintf(stderr, "[CONTENT] %d pack(s) load beneath the texture packs: %s\n", used, list);
}

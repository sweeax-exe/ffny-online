/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* FFNY Online's community content: the catalogue packs the launcher synced (pc_content.h,
 * docs/content-packs.md). Read-only here: setup/mods.py owns the files, this file reads its
 * content.ini, the composed files' files.ini and the player's switches, and tells the texture
 * loader, the file system and the netplay which packs are on. */
#define _CRT_SECURE_NO_WARNINGS
#include "pc_content.h"
#include "content_update_logic.h"
#include "new_fighter_places.h"
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

/* Paths in this file are UTF-8 (build 83). The narrow environment and fopen() use the ANSI code page, which
 * cannot hold a data folder whose path has letters outside it (a Cyrillic or Greek user name on a Western
 * Windows): content.ini was not found there, the launcher's Content page said "No community packs yet" although
 * the sync had installed them, and the game served none of the composed files, so it went online with another
 * content print than everyone else. DEFJAM_DATA is read wide and every file opens wide. */
#if defined(_WIN32)
static FILE *content_fopen(const char *path, const char *mode)
{
    WCHAR w[1400], m[8];
    int i;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, (int)(sizeof w / sizeof w[0]))) return NULL;
    for (i = 0; mode[i] && i < 7; i++) m[i] = (WCHAR)mode[i];
    m[i] = 0;
    return _wfopen(w, m);
}
#else
#define content_fopen fopen
#endif

static const char *data_root(void)
{
#if defined(_WIN32)
    static char utf8[MAX_PATH * 4];
    const WCHAR *w = _wgetenv(L"DEFJAM_DATA");
    if (!w || !*w || !WideCharToMultiByte(CP_UTF8, 0, w, -1, utf8, (int)sizeof utf8, NULL, NULL)) return NULL;
    return utf8;
#else
    const char *d = getenv("DEFJAM_DATA");
    return d && *d ? d : NULL;
#endif
}

/* Build 90, community content in offline play: this run leaves FFNY Online's synced community packs out (the setting
 * [content] community_offline is 0 and the run was not started for online). Decided once, by the game at its start
 * (pc_content_decide); the launcher never decides, so its Content page keeps listing everything. */
static int g_community_off;
static int g_decided;
static int g_session_on;        /* started for online (--content-update): on for this run, whatever the setting */

void pc_content_session_online(int forced) { g_session_on = forced != 0; }

/* A run started for online has --content-update on its command line (UPDATE NOW, or the online prompt's restart). */
static int started_for_online(void)
{
#if defined(_WIN32)
    const char *cmd = GetCommandLineA(), *flag = "--content-update", *p = cmd ? strstr(cmd, flag) : NULL;
    size_t n = strlen(flag);
    while (p) {
        if ((p == cmd || p[-1] == ' ' || p[-1] == 9) && (!p[n] || p[n] == ' ' || p[n] == 9)) return 1;
        p = strstr(p + n, flag);
    }
#endif
    return 0;
}

/* Decided on first use: the game's start (the log header, pc_content_start) or the first online connection, whichever
 * comes first (tests start one before the launcher is done). The launcher's own screens never ask. */
void pc_content_decide(void)
{
    if (g_decided) return;
    g_decided = 1;
    g_session_on |= started_for_online();
    g_community_off = cu_community_off(recomp_settings_get("content", "community_offline", 1), g_session_on);
}

int pc_content_community_off(void) { return g_community_off; }

int pc_content_online_allowed(void)
{
    pc_content_decide();
    return cu_online_allowed(g_community_off);
}

/* The folder the composed files are served from, relative to the data folder: the one composition of the community
 * packs and the player's own, or, with the community content off, the composition of the player's own packs alone
 * (setup/mods.py compose writes both). */
#define COMPOSED_DIR "mod-manager" HOST_SEP "content-files"
#define COMPOSED_OWN_DIR "mod-manager" HOST_SEP "own-packs" HOST_SEP "content-files"
static const char *composed_dir(void) { return g_community_off ? COMPOSED_OWN_DIR : COMPOSED_DIR; }

static int folder_exists(const char *path)
{
#if defined(_WIN32)
    WCHAR w[1400];
    DWORD a = MultiByteToWideChar(CP_UTF8, 0, path, -1, w, (int)(sizeof w / sizeof w[0]))
                  ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;
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
    /* The applied line is as long as the list (build 94: 1200 characters cut it at about 70 packs, and the packs
     * after the cut counted as not applied, the community ones out of the netplay print with them). */
    size_t line_size = applied_size + 1200;
    char path[1024], *line;
    FILE *f;
    int in_section = 0, ok = 0;
    if (applied_size) applied[0] = 0;
    if (nfiles) *nfiles = 0;
    if (!root) return 0;
    snprintf(path, sizeof path, "%s" HOST_SEP "%s" HOST_SEP "files.ini", root, composed_dir());
    if (!(line = (char *)malloc(line_size))) return 0;
    if (!(f = content_fopen(path, "rb"))) { free(line); return 0; }
    while (fgets(line, (int)line_size, f)) {
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
    free(line);
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

/* Build 94: small state files of the launcher in <data>/mod-manager, read and written whole. A setting holds at most
 * 1023 characters (about 60 pack ids); the player's own packs can be several hundred, so which of them are switched
 * off, and what the master switch remembers, live in files. Writing replaces the file in one step. */
static int state_file_path(const char *name, char *path, size_t size)
{
    const char *root = data_root();
    size_t i;
    if (!root || !name || !*name || strlen(name) > 60) return 0;
    for (i = 0; name[i]; i++)
        if (!(isalnum((unsigned char)name[i]) || name[i] == '-' || name[i] == '.')) return 0;
    return snprintf(path, size, "%s" HOST_SEP "mod-manager" HOST_SEP "%s", root, name) < (int)size;
}

int pc_content_state_read(const char *name, char *out, int size)
{
    char path[1400];
    FILE *f;
    long len;
    if (!out || size <= 1) return 0;
    out[0] = 0;
    if (!state_file_path(name, path, sizeof path) || !(f = content_fopen(path, "rb"))) return 0;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || len > size - 1) { fclose(f); return -1; }
    len = (long)fread(out, 1, (size_t)len, f);
    out[len] = 0;
    fclose(f);
    return (int)len;
}

int pc_content_state_write(const char *name, const char *text)
{
    char path[1400], tmp[1420], dir[1400];
    FILE *f;
    size_t n;
    int ok;
    if (!state_file_path(name, path, sizeof path)) return 0;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    snprintf(dir, sizeof dir, "%s" HOST_SEP "mod-manager", data_root());
#if defined(_WIN32)
    {
        WCHAR w[1400], wt[1430];
        if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, (int)(sizeof w / sizeof w[0])) ||
            !MultiByteToWideChar(CP_UTF8, 0, tmp, -1, wt, (int)(sizeof wt / sizeof wt[0]))) return 0;
        if (!text || !*text) { DeleteFileW(w); return 1; }
        if (!folder_exists(dir)) {
            WCHAR wd[1400];
            if (MultiByteToWideChar(CP_UTF8, 0, dir, -1, wd, (int)(sizeof wd / sizeof wd[0]))) CreateDirectoryW(wd, NULL);
        }
        f = _wfopen(wt, L"wb");
        if (!f) return 0;
        n = strlen(text);
        ok = fwrite(text, 1, n, f) == n;
        ok = fclose(f) == 0 && ok;
        if (ok) ok = MoveFileExW(wt, w, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        if (!ok) DeleteFileW(wt);
        return ok;
    }
#else
    if (!text || !*text) { remove(path); return 1; }
    if (!folder_exists(dir)) mkdir(dir, 0777);
    f = fopen(tmp, "wb");
    if (!f) return 0;
    n = strlen(text);
    ok = fwrite(text, 1, n, f) == n;
    ok = fclose(f) == 0 && ok;
    if (ok) ok = rename(tmp, path) == 0;
    if (!ok) remove(tmp);
    return ok;
#endif
}

/* The ids of the player's own packs that are switched off: <data>/mod-manager/own-off.txt, one id per line. Read at
 * most once a second (the Content page asks for every pack on every frame); a write updates the copy at once. */
#define OWN_OFF_FILE "own-off.txt"
#define OWN_OFF_MAX (1 << 17)
static char g_own_text[OWN_OFF_MAX];     /* "\n<id>\n<id>\n" */
static time_t g_own_stamp;
static int g_own_loaded;

static void own_load(void)
{
    char raw[OWN_OFF_MAX];
    time_t now = time(NULL);
    char *p, *e;
    size_t o = 1;
    if (g_own_loaded && now == g_own_stamp) return;
    g_own_loaded = 1;
    g_own_stamp = now;
    g_own_text[0] = '\n';
    g_own_text[1] = 0;
    if (pc_content_state_read(OWN_OFF_FILE, raw, (int)sizeof raw) <= 0) return;
    for (p = raw; *p;) {
        char *line = p;
        for (e = p; *e && *e != '\n'; e++) {}
        p = *e ? e + 1 : e;
        *e = 0;
        line = trim(line);
        if (!valid_id(line) || o + strlen(line) + 2 >= sizeof g_own_text) continue;
        o += (size_t)snprintf(g_own_text + o, sizeof g_own_text - o, "%s\n", line);
    }
}

static int own_has(const char *id)
{
    char key[96];
    if (!valid_id(id)) return 0;
    own_load();
    snprintf(key, sizeof key, "\n%s\n", id);
    return strstr(g_own_text, key) != NULL;
}

static int own_save(void)
{
    int ok = pc_content_state_write(OWN_OFF_FILE, g_own_text[1] ? g_own_text + 1 : "");
    g_own_stamp = time(NULL);
    return ok;
}

int pc_content_own_is_off(const char *id)
{
    return own_has(id) || pc_content_is_off(id);     /* the settings list: builds before 94 kept them there */
}

int pc_content_own_set_off(const char *id, int off)
{
    char key[96];
    char *at;
    if (!valid_id(id)) return 0;
    own_load();
    snprintf(key, sizeof key, "\n%s\n", id);
    at = strstr(g_own_text, key);
    if (at) memmove(at + 1, at + strlen(key), strlen(at + strlen(key)) + 1);       /* drop it */
    if (off) {
        size_t n = strlen(g_own_text);
        if (n + strlen(id) + 2 >= sizeof g_own_text) return 0;
        snprintf(g_own_text + n, sizeof g_own_text - n, "%s\n", id);
    }
    if (pc_content_is_off(id)) pc_content_set_off(id, 0);          /* an old settings entry would switch it off again */
    return own_save();
}

int pc_content_own_off_csv(char *out, int size)
{
    const char *p;
    int o = 0;
    if (!out || size <= 0) return -1;
    out[0] = 0;
    own_load();
    for (p = g_own_text + 1; *p;) {
        const char *e = strchr(p, '\n');
        int k = e ? (int)(e - p) : (int)strlen(p);
        if (!k) break;
        if (o + k + 2 > size) { out[0] = 0; return -1; }
        o += snprintf(out + o, (size_t)(size - o), "%s%.*s", o ? "," : "", k, p);
        p = e ? e + 1 : p + k;
    }
    return o;
}

int pc_content_own_off_put(const char *csv)
{
    const char *p;
    size_t o = 1;
    char *fresh = (char *)malloc(OWN_OFF_MAX);
    int ok;
    if (!fresh) return 0;
    fresh[0] = '\n';
    fresh[1] = 0;
    for (p = csv ? csv : ""; *p;) {
        char item[128];
        size_t k = 0;
        char key[96];
        while (*p == ',' || *p == ' ') p++;
        while (*p && *p != ',' && k < sizeof item - 1) item[k++] = *p++;
        item[k] = 0;
        while (k && item[k - 1] == ' ') item[--k] = 0;
        if (!valid_id(item)) continue;
        snprintf(key, sizeof key, "\n%s\n", item);
        if (strstr(fresh, key)) continue;
        if (o + k + 2 >= OWN_OFF_MAX) { free(fresh); return 0; }
        o += (size_t)snprintf(fresh + o, OWN_OFF_MAX - o, "%s\n", item);
    }
    own_load();
    memcpy(g_own_text, fresh, o + 1);
    free(fresh);
    ok = own_save();
    return ok;
}

int pc_content_pack_file(const char *id, const char *name, char *out, int size)
{
    char path[1400];
    const char *root = data_root();
    FILE *f;
    long len;
    if (!out || size <= 1) return 0;
    out[0] = 0;
    if (!root || !id || !valid_id(id) || !name || strstr(name, "..") || strpbrk(name, "/\\:")) return 0;
    snprintf(path, sizeof path, "%s" HOST_SEP "mods" HOST_SEP "%s" HOST_SEP "%s", root, id, name);
    if (!(f = content_fopen(path, "rb"))) return 0;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || len > size - 1) { fclose(f); return len < 0 ? 0 : -1; }
    len = (long)fread(out, 1, (size_t)len, f);
    out[len] = 0;
    fclose(f);
    return (int)len;
}

#define APPLIED_MAX 32768
static int list_all(PcContentPack *out, int max)
{
    char path[1024], line[1200];
    const char *root = data_root();
    FILE *f;
    int n = 0, cur = -1;
    if (!root || max <= 0) return 0;
    snprintf(path, sizeof path, "%s" HOST_SEP "mod-manager" HOST_SEP "content.ini", root);
    if (!(f = content_fopen(path, "rb"))) return 0;
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
            out[cur].skin_of = -1;
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
            CONTENT_TEXT(content_sha256); CONTENT_TEXT(description); CONTENT_TEXT(credits);
            CONTENT_TEXT(bundle); CONTENT_TEXT(bundle_name);
            else if (!strcmp(k, "order")) p->order = atoi(v);
            else if (!strcmp(k, "composed")) p->composed = atoi(v) == 1;
            else if (!strcmp(k, "bytes")) p->bytes = atoll(v);
            else if (!strcmp(k, "fighter_fe_id")) p->fighter_fe_id = atoi(v);
            else if (!strcmp(k, "venue_fe_id")) p->venue_fe_id = atoi(v);
            else if (!strcmp(k, "local")) p->local = atoi(v) == 1;
            else if (!strcmp(k, "online_ok")) p->online_ok = atoi(v) == 1;
            CONTENT_TEXT(reviewed); CONTENT_TEXT(template_name);
            else if (!strcmp(k, "template_fe_id")) p->template_fe_id = atoi(v);
            else if (!strcmp(k, "fighter_mode")) p->skin = !strcmp(v, "skin");
            else if (!strcmp(k, "skin_of")) p->skin_of = atoi(v);
            else if (!strcmp(k, "official")) p->official = atoi(v) == 1;
            else if (!strcmp(k, "rts_map")) p->rts_map = atoi(v) == 1;
            CONTENT_TEXT(venue_code);
#undef CONTENT_TEXT
        }
    }
    fclose(f);
    {   /* keep only packs whose folder is there; the rest are not installed. Not static: any thread lists (build 94,
         * a shared buffer emptied by another thread's read left packs "not applied" out of the netplay print).
         * On the heap: 32768 characters for hundreds of own packs (fix/mods-master) is too much for a stack. */
        char *applied = (char *)malloc(APPLIED_MAX);
        int i, m = 0;
        if (applied) read_files_ini(root, applied, APPLIED_MAX, NULL, 0, NULL);
        for (i = 0; i < n; i++) {
            char dir[1024];
            snprintf(dir, sizeof dir, "%s" HOST_SEP "mods" HOST_SEP "%s", root, out[i].id);
            out[i].installed = folder_exists(dir);
            if (strcmp(out[i].kind, "content")) strcpy(out[i].kind, "replacement");
            out[i].off = out[i].local ? pc_content_own_is_off(out[i].id)
                                      : !strcmp(out[i].kind, "replacement") && pc_content_is_off(out[i].id);
            out[i].applied = out[i].composed && applied && in_list(applied, out[i].id);
            if (out[i].installed) out[m++] = out[i];
        }
        n = m;
        free(applied);
    }
    qsort(out, (size_t)n, sizeof *out, by_order);
    return n;
}

int pc_content_list(PcContentPack *out, int max)
{
    int n = list_all(out, max), i, m = 0;
    if (!g_community_off) return n;
    /* Build 90: without the community content only the player's own packs and the language packs (the Game language
     * setting; they never clash with a player's own packs) remain: the rest is installed but not used in this run */
    for (i = 0; i < n; i++)
        if (out[i].local || !strcmp(out[i].type, "language")) out[m++] = out[i];
    return m;
}

/* Build 94: every caller below lists into its own buffer (malloc). These ran on several threads at once (the
 * netplay start's build print, its network thread's HELLO, the main thread's [CONTENT] header, the online screen)
 * and shared one static array: a list another thread was rewriting gave the build print of another content set, and
 * the relay then kept that player apart from everyone (a quick match that opened a room beside the other player's). */
static PcContentPack *packs_new(void)
{
    return (PcContentPack *)malloc(sizeof(PcContentPack) * PC_CONTENT_MAX);
}

uint32_t pc_content_print(void)
{
    PcContentPack *packs;
    const char *test = getenv("RECOMP_TEST_CONTENT_PRINT");
    uint32_t h = 2166136261u;
    int n, i, any = 0;
    if (test && *test) return (uint32_t)strtoul(test, NULL, 16);
    if (!(packs = packs_new())) return 0;
    n = pc_content_list(packs, PC_CONTENT_MAX);
    for (i = 0; i < n; i++) {
        const char *parts[3];
        int k;
        if (strcmp(packs[i].kind, "content")) continue;
        if (packs[i].local) continue;       /* the player's own: offline only (pc_content_local_offline) */
        if (packs[i].composed && !packs[i].applied) continue;     /* not in this PC's game files */
        parts[0] = packs[i].id; parts[1] = packs[i].version; parts[2] = packs[i].content_sha256;
        for (k = 0; k < 3; k++) {
            const char *c;
            for (c = parts[k]; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
            h = (h ^ (uint8_t)(k < 2 ? 0 : '\n')) * 16777619u;
        }
        if (packs[i].skin) {    /* build 85: a skin is another look than the same pack as a fighter */
            const char *c;
            for (c = "skin\n"; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
        }
        any = 1;
    }
    free(packs);
    if (!any) return 0;
    return h ? h : 1;
}

/* ---- build 85: skins ---- */

int pc_content_skins(PcContentSkin *out, int max)
{
    PcContentPack *packs;
    char path[1024], line[1200];
    const char *root = data_root();
    FILE *f;
    int n = 0, np, in_section = 0;
    if (!root || max <= 0 || !(packs = packs_new())) return 0;
    np = pc_content_list(packs, PC_CONTENT_MAX);
    snprintf(path, sizeof path, "%s" HOST_SEP "%s" HOST_SEP "files.ini", root, composed_dir());
    if (!(f = fopen(path, "rb"))) { free(packs); return 0; }
    while (fgets(line, sizeof line, f) && n < max) {
        char *s = trim(line), *fields[4], *q;
        int k, i, ok = 0;
        if (*s == '[') { in_section = !strcmp(s, "[content-files]"); continue; }
        if (!in_section || strncmp(s, "skin=", 5)) continue;
        q = s + 5;
        for (k = 0; k < 4; k++) {
            fields[k] = q;
            q = k < 3 ? strchr(q, '|') : NULL;
            if (k < 3 && !q) break;
            if (q) *q++ = 0;
        }
        if (k < 4 || !valid_id(fields[0])) continue;
        /* only a skin of an installed pack the composition applied, which content.ini calls a skin of the same
         * template (a pack the launcher installed by hand never is one) */
        for (i = 0; i < np; i++)
            if (!strcmp(packs[i].id, fields[0]) && packs[i].skin && packs[i].applied &&
                packs[i].skin_of == atoi(fields[1]))
                ok = 1;
        if (!ok || strlen(fields[2]) != 9 || strncmp(fields[2], "V2IP_7", 6)) continue;
        memset(&out[n], 0, sizeof out[n]);
        snprintf(out[n].id, sizeof out[n].id, "%s", fields[0]);
        out[n].template_fe_id = atoi(fields[1]);
        snprintf(out[n].model, sizeof out[n].model, "%s", fields[2]);
        snprintf(out[n].name, sizeof out[n].name, "%s", trim(fields[3]));
        n++;
    }
    fclose(f);
    free(packs);
    return n;
}

/* ---- build 82: new community content is out; the NEW badge ---- */

int pc_content_news(char *names, int size)
{
    char path[1024], line[1200];
    const char *root = data_root(), *test = getenv("RECOMP_TEST_CONTENT_NEWS");
    FILE *f;
    int pending = 0, revision = 0, in_section = 0;
    if (names && size > 0) names[0] = 0;
    if (g_community_off) return 0;      /* build 90: the content is off in this run: nothing to update */
    if (test && *test) {
        if (names && size > 0) snprintf(names, (size_t)size, "%s", atoi(test) > 0 ? "Doc Nine | The Block" : "");
        return atoi(test) > 0 ? atoi(test) : 0;
    }
    if (!root) return 0;
    snprintf(path, sizeof path, "%s" HOST_SEP "mod-manager" HOST_SEP "news.ini", root);
    if (!(f = content_fopen(path, "rb"))) return 0;
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line), *eq;
        if (*s == '[') { in_section = !strcmp(s, "[news]"); continue; }
        if (!in_section || !(eq = strchr(s, '='))) continue;
        *eq = 0;
        if (!strcmp(s, "pending")) pending = atoi(eq + 1);
        else if (!strcmp(s, "revision")) revision = atoi(eq + 1);
        else if (!strcmp(s, "names") && names && size > 0) snprintf(names, (size_t)size, "%s", eq + 1);
    }
    fclose(f);
    /* build 84: news only for a catalogue newer than the one the installed packs were synced for; an older
     * news.ini (a check before the last sync, a copied data folder) says nothing */
    if (pending > 0 && revision <= pc_content_revision()) {
        if (names && size > 0) names[0] = 0;
        return 0;
    }
    return pending > 0 ? pending : 0;
}

/* ---- build 83: the last sync and check (sync-result.ini, news.ini), for the log, F9 and the hub ---- */

/* Calls fn(key, value) for each key of [section] in <data>/mod-manager/<file>; 0 when the file is not there. */
static int read_ini_section(const char *file, const char *section, void (*fn)(void *, const char *, const char *),
                            void *ctx)
{
    char path[1024], line[1400], head[64];
    const char *root = data_root();
    FILE *f;
    int in_section = 0;
    if (!root) return 0;
    snprintf(path, sizeof path, "%s" HOST_SEP "mod-manager" HOST_SEP "%s", root, file);
    snprintf(head, sizeof head, "[%s]", section);
    if (!(f = content_fopen(path, "rb"))) return 0;
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line), *eq;
        if (*s == ';' || *s == '#') continue;
        if (*s == '[') { in_section = !strcmp(s, head); continue; }
        if (!in_section || !(eq = strchr(s, '='))) continue;
        *eq = 0;
        fn(ctx, trim(s), trim(eq + 1));
    }
    fclose(f);
    return 1;
}

#define COPY_FIELD(dst, v) snprintf((dst), sizeof(dst), "%s", (v))

static void sync_field(void *ctx, const char *k, const char *v)
{
    PcContentSync *s = (PcContentSync *)ctx;
    if (!strcmp(k, "result")) COPY_FIELD(s->result, v);
    else if (!strcmp(k, "kind")) COPY_FIELD(s->kind, v);
    else if (!strcmp(k, "reason")) COPY_FIELD(s->reason, v);
    else if (!strcmp(k, "time")) COPY_FIELD(s->time, v);
    else if (!strcmp(k, "ids")) COPY_FIELD(s->ids, v);
    else if (!strcmp(k, "skipped")) COPY_FIELD(s->skipped, v);
    else if (!strcmp(k, "revision")) s->revision = atoi(v);
    else if (!strcmp(k, "packs")) s->packs = atoi(v);
}

int pc_content_sync_result(PcContentSync *out)
{
    const char *test = getenv("RECOMP_TEST_CONTENT_SYNC");
    memset(out, 0, sizeof *out);
    if (test && *test) {
        strcpy(out->result, "failed");
        COPY_FIELD(out->kind, strstr(test, "clock") ? "clock" : "network");
        COPY_FIELD(out->reason, test);
        strcpy(out->time, "2026-11-15T12:00:00Z");
        return 1;
    }
    return read_ini_section("sync-result.ini", "sync", sync_field, out);
}

static void news_field(void *ctx, const char *k, const char *v)
{
    PcContentNews *n = (PcContentNews *)ctx;
    if (!strcmp(k, "revision")) n->revision = atoi(v);
    else if (!strcmp(k, "pending")) n->pending = atoi(v);
    else if (!strcmp(k, "checked")) COPY_FIELD(n->checked, v);
    else if (!strcmp(k, "kind")) COPY_FIELD(n->kind, v);
    else if (!strcmp(k, "error")) COPY_FIELD(n->error, v);
}

int pc_content_news_state(PcContentNews *out)
{
    memset(out, 0, sizeof *out);
    out->known = read_ini_section("news.ini", "news", news_field, out);
    if (!out->error[0]) out->kind[0] = 0;
    return out->known;
}

static void revision_field(void *ctx, const char *k, const char *v)
{
    if (!strcmp(k, "revision")) *(int *)ctx = atoi(v);
}

int pc_content_revision(void)
{
    int revision = 0;
    read_ini_section("content.ini", "content", revision_field, &revision);
    return revision;
}

int pc_content_problem_text(const PcContentSync *sync, int have_sync, const PcContentNews *news, int installed,
                            char *out, int size)
{
    const char *what = NULL, *reason = "", *kind = "";
    if (!out || size <= 0) return 0;
    out[0] = 0;
    if (have_sync && sync && !strcmp(sync->result, "failed")) {
        /* a good check after that sync saw no change to make: the content is current after all (the sync may
         * have failed on its catalogue fetch while the cached catalogue was the newest) */
        int fine = news && news->known && !news->error[0] && news->pending == 0 && news->revision <= installed &&
                   strcmp(news->checked, sync->time) > 0;
        if (!fine) { what = "updated"; reason = sync->reason; kind = sync->kind; }
    }
    if (!what && news && news->known && news->error[0]) { what = "checked"; reason = news->error; kind = news->kind; }
    if (!what) return 0;
    snprintf(out, (size_t)size, "Community content could not be %s%s%s. %s", what, *reason ? ": " : "", reason,
             !strcmp(kind, "clock") ? PC_CONTENT_CLOCK_LINE : PC_CONTENT_FIX_LINE);
    return 1;
}

/* Build 84: the sync installed packs but this run serves none of them (build 83's data folder with letters outside
 * the ANSI code page was one way): set by pc_content_start, "" when all is well. */
static char g_load_problem[400];

int pc_content_problem(char *out, int size)
{
    PcContentSync sync;
    PcContentNews news;
    int have;
    if (g_load_problem[0]) {
        if (out && size > 0)
            snprintf(out, (size_t)size, "Community content is installed but the game could not load it (%s). %s",
                     g_load_problem, PC_CONTENT_LOAD_LINE);
        return 1;
    }
    have = pc_content_sync_result(&sync);
    pc_content_news_state(&news);
    return pc_content_problem_text(&sync, have, &news, pc_content_revision(), out, size);
}

/* The print at the game's start (pc_content_log_header, main thread): the F9 report's thread names it without
 * touching pc_content_print's buffer. The packs do not change while the game runs. */
static uint32_t g_print_at_start;

/* "revision 5, 1 pack (studio-d-mob-2), print F3245300"; any thread (its own pack buffer). */
static void content_state(char *out, size_t size)
{
    PcContentPack *packs = (PcContentPack *)malloc(sizeof(PcContentPack) * PC_CONTENT_MAX);
    char ids[1024] = "";
    int n = packs ? pc_content_list(packs, PC_CONTENT_MAX) : 0, i, o = 0;
    for (i = 0; i < n && o < (int)sizeof ids - 1; i++)
        o += snprintf(ids + o, sizeof ids - (size_t)o, "%s%s%s", i ? ", " : "", packs[i].id,
                      packs[i].composed && !packs[i].applied ? " (not applied)" : "");
    free(packs);
    snprintf(out, size, "revision %d, %d pack%s%s%s%s, print %08X", pc_content_revision(), n, n == 1 ? "" : "s",
             n ? " (" : "", ids, n ? ")" : "", (unsigned)g_print_at_start);
}

static void sync_line(char *out, size_t size, const char *prefix)
{
    PcContentSync s;
    if (!pc_content_sync_result(&s))
        snprintf(out, size, "%sno sync result (a launcher before build 83 or never synced)", prefix);
    else if (!strcmp(s.result, "ok"))
        snprintf(out, size, "%slast sync ok, revision %d, %d pack%s%s%s (%s)", prefix, s.revision, s.packs,
                 s.packs == 1 ? "" : "s", s.skipped[0] ? "; skipped for this build: " : "", s.skipped, s.time);
    else
        snprintf(out, size, "%slast sync %s: %s (%s; revision %d, %d pack%s, %s)", prefix,
                 !strcmp(s.result, "cancelled") ? "CANCELLED (PLAY NOW)" : "FAILED", s.reason[0] ? s.reason : "?",
                 s.kind[0] ? s.kind : "-", s.revision, s.packs, s.packs == 1 ? "" : "s", s.time);
}

/* Build 90: "on", "OFF (setting: ...)" or "on for this run only (restarted for online; the setting is off)". */
static const char *mode_text(void)
{
    if (g_community_off) return "OFF in this run (the setting Community content in offline play is off): the standard "
                                "fighters, venues and looks only; online is refused until a restart with it on";
    if (!recomp_settings_get("content", "community_offline", 1))
        return "on for this run only (the game restarted for online; the saved setting stays off)";
    return "on";
}

void pc_content_log_header(void)
{
    char line[1400], state[1200];
    pc_content_decide();
    g_print_at_start = pc_content_print();
    sync_line(line, sizeof line, "");
    content_state(state, sizeof state);
    fprintf(stderr, "[CONTENT] community content in offline play: %s\n[CONTENT] %s\n[CONTENT] installed: %s\n", mode_text(),
            line, state);
}

void pc_content_report_line(void)
{
    char line[1400], state[1200], problem[512];
    PcContentNews news;
    sync_line(line, sizeof line, "");
    content_state(state, sizeof state);
    pc_content_news_state(&news);
    fprintf(stderr, "[REPORT] community content in offline play: %s\n", mode_text());
    fprintf(stderr, "[REPORT] content: %s; %s; check: %s", state, line,
            !news.known ? "none yet" : news.error[0] ? "FAILED" : "ok");
    if (news.known) fprintf(stderr, " (revision %d, %d pending%s%s, %s)", news.revision, news.pending,
                            news.error[0] ? ", " : "", news.error, news.checked);
    fprintf(stderr, "\n");
    if (pc_content_problem(problem, sizeof problem)) fprintf(stderr, "[REPORT] content problem shown: %s\n", problem);
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
    if (!new_fighter_is_place(fe_id)) return 0;
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
    if (!new_fighter_is_place(fe_id)) return;
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
    PcContentPack *packs = packs_new();
    int n = packs ? pc_content_list(packs, PC_CONTENT_MAX) : 0, i, off = 0;
    for (i = 0; i < n; i++)
        if (!strcmp(packs[i].id, id)) { off = packs[i].off; break; }
    free(packs);
    return off;
}

int pc_content_offline_only(void)
{
    const char *v = getenv("RECOMP_OFFLINE_ONLY");
    return v && *v && strcmp(v, "0");
}

int pc_content_local_offline(char *names, int size)
{
    PcContentPack *packs = packs_new();
    int n = packs ? pc_content_list(packs, PC_CONTENT_MAX) : 0, i, count = 0, o = 0;
    if (names && size > 0) names[0] = 0;
    for (i = 0; i < n; i++) {
        if (!packs[i].local || packs[i].off || packs[i].online_ok) continue;
        if (names && size > 0 && o < size)
            o += snprintf(names + o, (size_t)(size - o), "%s%s", count ? ", " : "", packs[i].name[0] ? packs[i].name : packs[i].id);
        count++;
    }
    free(packs);
    return count;
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
    snprintf(path, sizeof path, "%s" HOST_SEP "%s" HOST_SEP "files.ini", root, composed_dir());
    if (!(f = content_fopen(path, "rb"))) return;
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
        if (!MultiByteToWideChar(CP_UTF8, 0, host, -1, host_path, (int)size)) return FALSE;
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
    static char applied[32768];
    int i, n = 0;
    if (!root || !read_files_ini(root, applied, sizeof applied, g_files, CONTENT_FILES_MAX, &n) || !n) return;
    snprintf(g_files_dir, sizeof g_files_dir, "%s" HOST_SEP "%s", root, composed_dir());
    g_file_count = 0;
    for (i = 0; i < n; i++) {
        char host[1400];
        FILE *f;
        if (snprintf(host, sizeof host, "%s" HOST_SEP "%s", g_files_dir, g_files[i]) >= (int)sizeof host) continue;
        if (!(f = content_fopen(host, "rb"))) { fprintf(stderr, "[CONTENT] composed file missing: %s\n", g_files[i]); continue; }
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
            if (!(f = content_fopen(host, "rb"))) {
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
#if defined(_WIN32)
    if (data_root()) root = data_root();    /* the caller's root is in the ANSI code page; this one is UTF-8 */
#endif
    pc_content_decide();
    {
        const char *v = getenv("RECOMP_CONTENT");
        if (v && !strcmp(v, "0")) { fprintf(stderr, "[CONTENT] off for this run (RECOMP_CONTENT=0)\n"); return; }
    }
    if (g_community_off)
        fprintf(stderr, "[CONTENT] community packs are not used in this run: only the player's own packs and language "
                        "packs (the files come from %s)\n", composed_dir());
    n = pc_content_list(packs, PC_CONTENT_MAX);
    if (!n) {
        PcContentSync last;
        if (g_community_off) return;
        fprintf(stderr, "[CONTENT] no community packs installed\n");
        /* build 84: the launcher's sync says it installed some, yet this run finds none (the data folder) */
        if (!(tex_root && *tex_root) && pc_content_sync_result(&last) && last.packs > 0 &&
            !getenv("RECOMP_TEST_CONTENT_SYNC")) {
            snprintf(g_load_problem, sizeof g_load_problem, "the launcher installed %d pack%s, the game found none",
                     last.packs, last.packs == 1 ? "" : "s");
            fprintf(stderr, "[CONTENT] PROBLEM: the sync installed %d pack(s) (%s) but the game found none under %s: "
                    "it goes online with another content set\n", last.packs, last.ids, root ? root : "(no data folder)");
        }
        return;
    }
#if !defined(DEFJAM_APP)
    /* fighter bodies, clothing, venues: the launcher composed them; a test texture root holds only its own */
    if (!(tex_root && *tex_root)) {
        int composed = 0;
        serve_composed_files(root ? root : data_root());
        for (i = 0; i < n; i++) composed += packs[i].composed && packs[i].applied;
        if (composed && !g_file_count) {    /* build 84: synced and composed, but no community file served */
            snprintf(g_load_problem, sizeof g_load_problem, "%d pack%s ready, 0 community files served", composed,
                     composed == 1 ? "" : "s");
            fprintf(stderr, "[CONTENT] PROBLEM: %d composed pack(s) but the game serves 0 community files from %s: "
                    "it goes online with another content set\n", composed, root ? root : "(no data folder)");
        }
    }
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
            if (!(f = content_fopen(manifest, "rb"))) continue;
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

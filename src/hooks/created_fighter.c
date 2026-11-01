/* Created (Story mode) fighters online. See docs/research/created-fighter-online.md.
 *
 * Battle mode already builds a created fighter for a slot whose ControllerSetup fighter byte is
 * 99: StartGame (sub_00020270, the branch at 0x2096D) finds the profile through the slot's pad
 * (GS+0x1AAB+5*slot -> GS+0x1ABD[pad]), takes the name, the archetype (P+0x143), the move keys
 * and the stats from it, and the match load (sub_0008B730) builds the body and the clothes from
 * the profile's recipe into the pad's CP-Fighter holder. Online, every PC writes the same record
 * (the relay's copy, validated and clamped here the same way on every PC) into a spare profile
 * slot, points the pad at it and sets the slot up with 99.
 *
 * The record (CP_REC_LEN bytes, netplay/relay/protocol_created.go): version, then the payload
 *   +0x000 name (0x20)          profile +0xC
 *   +0x020 profile +0xFC..+0x143 (0x48): base stats, bonus stats, move keys, voice, styles, archetype
 *   +0x068 created flag (1)     profile +0x4D8A
 *   +0x069 recipe (0x98)        profile +0x5564
 *   +0x101 options (0x26)       profile +0x55FC
 * then the CRC-32 of version and payload.
 *
 * The summary's reward step saves the profile linked to each pad: linked, a copy would go to disk
 * as a user ID named after the fighter (measured). So the pads are unlinked before it, and linked
 * again for a rematch. */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "recomp/gen/recomp_types.h"
#include "created_fighter.h"

#define GS_PTR          (0x2FB148u + 4u * 0x18u)    /* game-flow singleton, component 0x18 */
#define GS_PROFILES     4u                          /* MEM32(GS+4) + 0x177C: 16 profiles */
#define PROFILE_BASE    0x177Cu
#define PROFILE_SIZE    0x5950u
#define PROFILE_COUNT   16
#define GS_PAD_PROFILE  0x1ABDu                     /* s8 per pad: profile index, -1 none */
#define GS_HOLDERS      0x1A9Cu                     /* 4 x 0x1C per pad; +1 = look built */
#define SAVE_FILE_SIZE  0x5990u                     /* a user ID's save: 0x80 header + profile 0..0x5910 */
#define SAVE_HEADER     0x80u

/* payload offsets (after the version byte) */
#define R_NAME      0x000
#define R_PROFILE   0x020       /* profile +0xFC */
#define R_BASE      (R_PROFILE + 0x00)
#define R_BONUS     (R_PROFILE + 0x18)
#define R_MOVES     (R_PROFILE + 0x30)
#define R_VOICE     (R_PROFILE + 0x40)
#define R_STYLE     (R_PROFILE + 0x44)
#define R_ARCH      (R_PROFILE + 0x47)
#define R_FLAG      0x068
#define R_RECIPE    0x069
#define R_OPTIONS   0x101

#define MIN_ARCHETYPE   86      /* fighter.xml: CP B__ .. CP MW_, CP_HERO */
#define MAX_ARCHETYPE   141
#define MAX_BASE        50      /* the gym's cost table (0x2DBB70) has ten tiers of five */
#define MAX_BONUS       38      /* with MAX_BASE: the attribute screen's 100 % (stat 30) */
#define DEFAULT_MOVE    0x06000033u     /* a fresh fighter's four move keys */

/* ---- CRC-32 (IEEE) ---- */

static uint32_t crc32_ieee(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void seal(uint8_t rec[CP_REC_LEN])
{
    rec[0] = CP_REC_VERSION;
    wr32(rec + CP_REC_LEN - 4, crc32_ieee(rec, CP_REC_LEN - 4));
}

uint32_t cp_record_crc(const uint8_t rec[CP_REC_LEN]) { return rd32(rec + CP_REC_LEN - 4); }

int cp_record_sealed(const uint8_t rec[CP_REC_LEN])
{
    return rec[0] == CP_REC_VERSION && crc32_ieee(rec, CP_REC_LEN - 4) == cp_record_crc(rec);
}

void cp_record_from_profile(const uint8_t *profile, uint8_t rec[CP_REC_LEN])
{
    uint8_t *p = rec + 1;
    memset(rec, 0, CP_REC_LEN);
    memcpy(p + R_NAME, profile + 0xC, CP_NAME_BYTES);
    memcpy(p + R_PROFILE, profile + 0xFC, 0x48);
    p[R_FLAG] = profile[0x4D8A];
    memcpy(p + R_RECIPE, profile + 0x5564, 0x98);
    memcpy(p + R_OPTIONS, profile + 0x55FC, 0x26);
    seal(rec);
}

/* ---- the game files the check needs: record counts of the database tables, archetype names ---- */

#define SOD_TABLES 64
static int g_sod_loaded, g_sod_tables;
static uint32_t g_sod_count[SOD_TABLES];

static FILE *open_game_file(const char *name)
{
    const char *data = getenv("DEFJAM_DATA");
    char path[MAX_PATH];
    FILE *f = NULL;
    if (data && *data) {        /* a mod's copy, as the game reads it */
        snprintf(path, sizeof(path), "%s\\override\\%s", data, name);
        f = fopen(path, "rb");
    }
    if (!f) {
        snprintf(path, sizeof(path), "game\\%s", name);
        f = fopen(path, "rb");
    }
    return f;
}

static uint8_t *read_game_file(const char *name, size_t *size)
{
    FILE *f = open_game_file(name);
    uint8_t *buf;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 64 * 1024 * 1024 || !(buf = (uint8_t *)malloc((size_t)n + 1))) { fclose(f); return NULL; }
    *size = fread(buf, 1, (size_t)n, f);
    buf[*size] = 0;
    fclose(f);
    return buf;
}

/* djv2.sod (layout as scripts/sod-characters.py reads it): the tables at header +0x10, each with
 * its classes' record blocks; a database key is (table index << 24) | (colour << 16) | row. */
static void load_sod(void)
{
    size_t size = 0, t;
    uint8_t *d;
    if (g_sod_loaded) return;
    g_sod_loaded = 1;
    d = read_game_file("assets\\djv2.sod", &size);
    if (!d) return;
    if (size > 0x20 && !memcmp(d, "SODB", 4)) {
        uint32_t tables = rd32(d + 0x10), nt;
        if (tables + 12 <= size) {
            nt = rd32(d + tables + 4);
            if (nt <= SOD_TABLES && tables + 12 + 8 * (size_t)nt <= size) {
                for (t = 0; t < nt; t++) {
                    uint32_t off = rd32(d + tables + 12 + 4 * nt + 4 * t), ncls, block;
                    if (off + 0x1C > size) break;
                    ncls = rd32(d + off + 0x10);
                    if (!ncls || off + 0x18 + 8 * (size_t)ncls > size) continue;
                    block = rd32(d + off + 0x18 + 4 * ncls);
                    if (block + 12 > size) continue;
                    g_sod_count[t] = rd32(d + block + 4);
                }
                g_sod_tables = (int)nt;
            }
        }
    }
    free(d);
    fprintf(stderr, "[CREATED] database: %d tables\n", g_sod_tables);
}

/* A database key the game can look up: a table that exists and a row it has. The colour byte is
 * kept below 16 (unknown colours are not checked further; see the report). */
static int key_ok(uint32_t key)
{
    uint32_t table = key >> 24, colour = (key >> 16) & 0xFF, row = key & 0xFFFF;
    load_sod();
    return (int)table < g_sod_tables && row < g_sod_count[table] && colour < 16;
}

/* fighter.xml's created archetypes: "CP KB_" -> letters per coreID. The profile's archetype byte
 * (P+0x143) is a coreID, which is the entry's ID + 1 here: a fighter with Martial Arts, Kickboxing
 * and Submissions holds 137, the coreID of "CP MKS" (ID 136); read as an ID it named "CP MKW"
 * (Ryzing, 2026-11-01). */
static char g_arch_letters[MAX_ARCHETYPE + 1][4];
static int g_xml_loaded;

static void load_archetypes(void)
{
    size_t size = 0;
    char *x, *p;
    if (g_xml_loaded) return;
    g_xml_loaded = 1;
    x = (char *)read_game_file("fighter.xml", &size);
    if (!x) return;
    for (p = strstr(x, "<dispname>CP "); p; p = strstr(p + 1, "<dispname>CP ")) {
        const char *id = strstr(p, "<coreID>");
        const char *next = strstr(p + 1, "<dispname>");
        int n;
        if (!id || (next && id > next)) continue;
        n = atoi(id + 8);
        if (n >= MIN_ARCHETYPE && n <= MAX_ARCHETYPE) {
            memcpy(g_arch_letters[n], p + 13, 3);
            g_arch_letters[n][3] = 0;
        }
    }
    free(x);
}

const char *cp_record_style(const uint8_t rec[CP_REC_LEN])
{
    static char out[96];
    int arch = rec[1 + R_ARCH], k;
    load_archetypes();
    out[0] = 0;
    if (arch < MIN_ARCHETYPE || arch > MAX_ARCHETYPE) return out;
    for (k = 0; k < 3 && g_arch_letters[arch][k]; k++) {
        const char *w = NULL;
        switch (g_arch_letters[arch][k]) {
        case 'B': w = "STREETFIGHTING"; break;
        case 'K': w = "KICKBOXING"; break;
        case 'S': w = "SUBMISSIONS"; break;
        case 'W': w = "WRESTLING"; break;
        case 'M': w = "MARTIAL ARTS"; break;
        }
        if (!w) continue;
        if (out[0]) strncat(out, " / ", sizeof(out) - strlen(out) - 1);
        strncat(out, w, sizeof(out) - strlen(out) - 1);
    }
    return out;
}

/* ---- the check every PC makes ---- */

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

int cp_record_sanitize(const uint8_t in[CP_REC_LEN], uint8_t out[CP_REC_LEN])
{
    uint8_t *p;
    int i, n;
    if (!cp_record_sealed(in)) return 0;
    memcpy(out, in, CP_REC_LEN);
    p = out + 1;
    /* name: printable ASCII, 1..CP_NAME_MAX (16) characters, the rest zero (the relay refuses a
     * longer one: netplay/relay/protocol_created.go, ValidRecord) */
    for (n = 0; n < CP_NAME_MAX && p[R_NAME + n]; n++)
        if (p[R_NAME + n] < 0x20 || p[R_NAME + n] > 0x7E) p[R_NAME + n] = '?';
    memset(p + R_NAME + n, 0, CP_NAME_BYTES - n);
    if (!n) return 0;
    if (p[R_ARCH] < MIN_ARCHETYPE || p[R_ARCH] > MAX_ARCHETYPE || p[R_FLAG] != 1) return 0;
    if (p[R_STYLE] > 4) return 0;
    for (i = 1; i < 3; i++) if (p[R_STYLE + i] > 4) p[R_STYLE + i] = 0xFF;
    if (p[R_VOICE] < 1 || p[R_VOICE] > 6) wr32(p + R_VOICE, 1);
    else wr32(p + R_VOICE, p[R_VOICE]);
    for (i = 0; i < 6; i++) {
        uint32_t base = rd32(p + R_BASE + 4 * i);
        int32_t bonus = (int32_t)rd32(p + R_BONUS + 4 * i);
        wr32(p + R_BASE + 4 * i, base > MAX_BASE ? MAX_BASE : base);
        wr32(p + R_BONUS + 4 * i, (uint32_t)clamp32(bonus, 0, MAX_BONUS));
    }
    for (i = 0; i < 4; i++)
        if (!key_ok(rd32(p + R_MOVES + 4 * i))) wr32(p + R_MOVES + 4 * i, DEFAULT_MOVE);
    for (i = 0; i < 38; i++) {
        uint32_t v = rd32(p + R_RECIPE + 4 * i);
        if (i < 12) { if (v > 0xFF) wr32(p + R_RECIPE + 4 * i, 0); }      /* body and face values */
        else if (v != 0xFFFFFFFFu && !key_ok(v)) wr32(p + R_RECIPE + 4 * i, 0xFFFFFFFFu);
    }
    seal(out);
    return 1;
}

void cp_record_name(const uint8_t rec[CP_REC_LEN], char *out, int size)
{
    int n;
    if (size <= 0) return;
    for (n = 0; n < CP_NAME_MAX && n < size - 1 && rec[1 + R_NAME + n]; n++) out[n] = (char)rec[1 + R_NAME + n];
    out[n] = 0;
}

void cp_record_stats(const uint8_t rec[CP_REC_LEN], int out[6])
{
    int i;
    for (i = 0; i < 6; i++) {
        uint32_t base = rd32(rec + 1 + R_BASE + 4 * i);
        int32_t bonus = (int32_t)rd32(rec + 1 + R_BONUS + 4 * i);
        out[i] = (int)((float)(base / 5u) * 1.275f + (float)bonus * 0.45f + 0.5f);
    }
}

/* Every created fighter of the save area, newest first (at most CP_LOCAL_MAX), and the one the
 * player picked: the save the setting names (cp_pick_setting_get), the newest if it is gone. */
static int g_local_state;           /* 0 not read, 1 read */
static CpLocal g_local[CP_LOCAL_MAX];
static ULONGLONG g_local_when[CP_LOCAL_MAX];
static int g_local_count;
static CRITICAL_SECTION g_local_cs;
static INIT_ONCE g_local_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK local_init(PINIT_ONCE o, PVOID p, PVOID *c) { (void)o; (void)p; (void)c; InitializeCriticalSection(&g_local_cs); return TRUE; }

/* ---- online names (created_fighter.h) ---- */

int cp_name_clean(const char *in, char *out, int size)
{
    int n = 0, end = 0;
    if (size <= 0) return 0;
    for (; in && *in && n < CP_NAME_MAX && n < size - 1; in++) {
        unsigned char c = (unsigned char)*in;
        if (c < 0x20 || c > 0x7E || (c == ' ' && !n)) continue;     /* other bytes, leading blanks */
        out[n++] = (char)c;
        if (c != ' ') end = n;
    }
    out[end] = 0;                                                   /* trailing blanks */
    return end;
}

/* The setting's text: '%', ';' and '=' as %XX. */
static void names_escape(const char *s, char *out, size_t size)
{
    size_t n = 0;
    for (; *s && n + 4 < size; s++) {
        if (*s == '%' || *s == ';' || *s == '=') n += (size_t)snprintf(out + n, size - n, "%%%02X", (unsigned char)*s);
        else out[n++] = *s;
    }
    out[n] = 0;
}

static int hexval(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static void names_unescape(const char *s, size_t len, char *out, size_t size)
{
    size_t i, n = 0;
    if (!size) return;
    for (i = 0; i < len && n + 1 < size; i++) {
        if (s[i] == '%' && i + 2 < len && hexval(s[i + 1]) >= 0 && hexval(s[i + 2]) >= 0) {
            out[n++] = (char)(hexval(s[i + 1]) * 16 + hexval(s[i + 2]));
            i += 2;
        } else out[n++] = s[i];
    }
    out[n] = 0;
}

/* Calls `each` for every "key=name" entry of the setting's text, in order, with the entry's own
 * text (start, length) and its decoded key and name; stops when it returns nonzero. */
typedef int (*NameEntryFn)(const char *raw, size_t len, const char *key, const char *name, void *user);
static void names_each(const char *text, NameEntryFn each, void *user)
{
    const char *p = text;
    while (*p) {
        const char *end = strchr(p, ';'), *eq;
        size_t len;
        char key[sizeof(((CpLocal *)0)->key)], name[64];
        if (!end) end = p + strlen(p);
        len = (size_t)(end - p);
        eq = (const char *)memchr(p, '=', len);
        if (eq) {
            names_unescape(p, (size_t)(eq - p), key, sizeof(key));
            names_unescape(eq + 1, (size_t)(end - eq - 1), name, sizeof(name));
            if (each(p, len, key, name, user)) return;
        }
        p = *end ? end + 1 : end;
    }
}

struct NameFind { const char *key; char *out; int size; };
static int name_find(const char *raw, size_t len, const char *key, const char *name, void *user)
{
    struct NameFind *f = (struct NameFind *)user;
    (void)raw; (void)len;
    if (strcmp(key, f->key)) return 0;
    cp_name_clean(name, f->out, f->size);
    return 1;
}

/* The online name the setting keeps for save `key`, cleaned; "" none. */
static void online_name_of(const char *key, char *out, int size)
{
    char text[CP_NAMES_TEXT_MAX];
    struct NameFind f;
    if (size <= 0) return;
    out[0] = 0;
    text[0] = 0;
    cp_names_setting_get(text, (int)sizeof(text));
    f.key = key; f.out = out; f.size = size;
    names_each(text, name_find, &f);
}

/* The setting with save `key`'s entry replaced (removed when `name` is ""); with `only_listed`,
 * entries of saves that are not in the list now are dropped too (to make room). */
struct NameKeep { const char *key; int only_listed; char *out; size_t n, size; int full; };
static int name_keep(const char *raw, size_t len, const char *key, const char *name, void *user)
{
    struct NameKeep *k = (struct NameKeep *)user;
    int i, listed = 0;
    (void)name;
    if (!strcmp(key, k->key)) return 0;
    if (k->only_listed) {
        for (i = 0; i < g_local_count; i++) if (!strcmp(g_local[i].key, key)) listed = 1;
        if (!listed) return 0;
    }
    if (k->n + (k->n ? 1 : 0) + len + 1 > k->size) { k->full = 1; return 1; }
    if (k->n) k->out[k->n++] = ';';
    memcpy(k->out + k->n, raw, len);
    k->n += len;
    k->out[k->n] = 0;
    return 0;
}

static int names_store(const char *key, const char *name)       /* lock held */
{
    char old[CP_NAMES_TEXT_MAX], out[CP_NAMES_TEXT_MAX], ek[2 * sizeof(((CpLocal *)0)->key)], en[3 * CP_NAME_MAX + 1];
    int pass;
    old[0] = 0;
    cp_names_setting_get(old, (int)sizeof(old));
    names_escape(key, ek, sizeof(ek));
    names_escape(name, en, sizeof(en));
    for (pass = 0; pass < 2; pass++) {
        struct NameKeep k;
        k.key = key; k.only_listed = pass; k.out = out; k.n = 0; k.size = sizeof(out); k.full = 0;
        out[0] = 0;
        names_each(old, name_keep, &k);
        if (k.full) continue;
        if (!name[0]) { cp_names_setting_set(out); return 1; }
        if (k.n + (k.n ? 1 : 0) + strlen(ek) + 1 + strlen(en) + 1 > sizeof(out)) continue;
        snprintf(out + k.n, sizeof(out) - k.n, "%s%s=%s", k.n ? ";" : "", ek, en);
        cp_names_setting_set(out);
        return 1;
    }
    fprintf(stderr, "[CREATED] the online names setting is full\n");
    return 0;
}

static void record_set_name(uint8_t rec[CP_REC_LEN], const char *name)
{
    size_t n = strlen(name);
    memset(rec + 1 + R_NAME, 0, CP_NAME_BYTES);
    memcpy(rec + 1 + R_NAME, name, n < CP_NAME_MAX ? n : CP_NAME_MAX);
    seal(rec);
}

/* ---- the player's own ---- */

/* a profile image from a save file (0x5990: header + profile) or a dump (0x5950) */
static int profile_from_file(const char *path, uint8_t *profile, FILETIME *when)
{
    FILE *f = fopen(path, "rb");
    long n;
    size_t got;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    memset(profile, 0, PROFILE_SIZE);
    if (n == SAVE_FILE_SIZE) { fseek(f, SAVE_HEADER, SEEK_SET); got = fread(profile, 1, SAVE_FILE_SIZE - SAVE_HEADER, f); got = got == SAVE_FILE_SIZE - SAVE_HEADER; }
    else if (n == PROFILE_SIZE) got = fread(profile, 1, PROFILE_SIZE, f) == PROFILE_SIZE;
    else got = 0;
    fclose(f);
    if (got && when) {
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (GetFileAttributesExA(path, GetFileExInfoStandard, &a)) *when = a.ftLastWriteTime;
    }
    return (int)got;
}

static ULONGLONG ft64(FILETIME t) { return ((ULONGLONG)t.dwHighDateTime << 32) | t.dwLowDateTime; }

/* One checked record into the list, kept newest first (a tie keeps the order found); past
 * CP_LOCAL_MAX the oldest drops out. `when` is a FILETIME; 0 shows no date. */
static void local_add(const uint8_t rec[CP_REC_LEN], const char *key, ULONGLONG when, int dated)
{
    int at = g_local_count, k;
    CpLocal *e;
    while (at > 0 && g_local_when[at - 1] < when) at--;
    if (at >= CP_LOCAL_MAX) return;
    k = g_local_count < CP_LOCAL_MAX ? g_local_count : CP_LOCAL_MAX - 1;
    for (; k > at; k--) { g_local[k] = g_local[k - 1]; g_local_when[k] = g_local_when[k - 1]; }
    if (g_local_count < CP_LOCAL_MAX) g_local_count++;
    e = &g_local[at];
    memset(e, 0, sizeof(*e));
    memcpy(e->rec, rec, CP_REC_LEN);
    cp_record_name(rec, e->user_id, sizeof(e->user_id));
    snprintf(e->key, sizeof(e->key), "%s", key);
    online_name_of(key, e->online, sizeof(e->online));
    if (e->online[0]) {             /* the online name goes into the record, not into the save */
        uint8_t ok[CP_REC_LEN];
        record_set_name(e->rec, e->online);
        if (cp_record_sanitize(e->rec, ok)) memcpy(e->rec, ok, CP_REC_LEN);
        else { memcpy(e->rec, rec, CP_REC_LEN); e->online[0] = 0; }
    }
    cp_record_name(e->rec, e->name, sizeof(e->name));
    g_local_when[at] = when;
    if (dated) {
        FILETIME ft, lt;
        SYSTEMTIME st;
        ft.dwLowDateTime = (DWORD)when;
        ft.dwHighDateTime = (DWORD)(when >> 32);
        if (FileTimeToLocalFileTime(&ft, &lt) && FileTimeToSystemTime(&lt, &st))
            snprintf(e->saved, sizeof(e->saved), "%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    }
}

/* RECOMP_CP_RECORD (tests): profile dumps or user ID saves separated by ';', newest first. */
static void read_test_records(const char *list)
{
    static uint8_t profile[PROFILE_SIZE];
    char buf[2048], *p, *next;
    int i = 0;
    snprintf(buf, sizeof(buf), "%s", list);
    for (p = buf; p && *p; p = next, i++) {
        uint8_t rec[CP_REC_LEN], ok[CP_REC_LEN];
        int have = 0;
        next = strchr(p, ';');
        if (next) *next++ = 0;
        if (!*p) continue;
        if (profile_from_file(p, profile, NULL)) {
            cp_record_from_profile(profile, rec);
            have = cp_record_sanitize(rec, ok);
        }
        fprintf(stderr, "[CREATED] test record %s: %s\n", p, have ? "usable" : "not usable");
        if (have) local_add(ok, p, (ULONGLONG)(64 - i), 0);     /* the order given */
    }
}

static void read_local(void)
{
    static uint8_t profile[PROFILE_SIZE];
    uint8_t rec[CP_REC_LEN];
    const char *test = getenv("RECOMP_CP_RECORD"), *data = getenv("DEFJAM_DATA");
    char root[MAX_PATH], pattern[MAX_PATH];
    WIN32_FIND_DATAA dir;
    HANDLE h;
    int i;
    g_local_count = 0;
    if (test && *test) { read_test_records(test); goto done; }
    if (data && *data) snprintf(root, sizeof(root), "%s\\save\\UserData\\45410049", data);
    else {
        const char *la = getenv("LOCALAPPDATA");
        snprintf(root, sizeof(root), "%s\\xboxrecomp\\UserData\\45410049", la ? la : ".");
    }
    snprintf(pattern, sizeof(pattern), "%s\\*", root);
    h = FindFirstFileA(pattern, &dir);
    if (h == INVALID_HANDLE_VALUE) goto done;
    do {
        char inner[MAX_PATH];
        WIN32_FIND_DATAA file;
        HANDLE g;
        if (!(dir.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || dir.cFileName[0] == '.') continue;
        snprintf(inner, sizeof(inner), "%s\\%s\\*", root, dir.cFileName);
        g = FindFirstFileA(inner, &file);
        if (g == INVALID_HANDLE_VALUE) continue;
        do {
            char path[MAX_PATH], key[sizeof(g_local[0].key)];
            FILETIME when = { 0, 0 };
            uint8_t ok[CP_REC_LEN];
            if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY || file.nFileSizeLow != SAVE_FILE_SIZE) continue;
            snprintf(path, sizeof(path), "%s\\%s\\%s", root, dir.cFileName, file.cFileName);
            if (!profile_from_file(path, profile, &when) || profile[0x4D8A] != 1) continue;
            cp_record_from_profile(profile, rec);
            if (!cp_record_sanitize(rec, ok)) continue;
            snprintf(key, sizeof(key), "%s/%s", dir.cFileName, file.cFileName);
            local_add(ok, key, ft64(when), 1);
        } while (FindNextFileA(g, &file));
        FindClose(g);
    } while (FindNextFileA(h, &dir));
    FindClose(h);
done:
    for (i = 0; i < g_local_count; i++)
        fprintf(stderr, "[CREATED] the player's fighter %d: %s%s%s (%s), saved %s, record %08X\n", i, g_local[i].name,
                g_local[i].online[0] ? ", user ID " : "", g_local[i].online[0] ? g_local[i].user_id : "", cp_record_style(g_local[i].rec), g_local[i].saved[0] ? g_local[i].saved : "-", cp_record_crc(g_local[i].rec));
    if (!g_local_count) fprintf(stderr, "[CREATED] the player has no created fighter\n");
    g_local_state = 1;
}

/* Takes the list's lock, reading the list first if needed. */
static void local_lock(void)
{
    InitOnceExecuteOnce(&g_local_once, local_init, NULL, NULL);
    EnterCriticalSection(&g_local_cs);
    if (!g_local_state) read_local();
}

/* The picked entry (lock held): RECOMP_CP_PICK (tests), else the save the setting names, else the
 * newest; -1 with no entry. */
static int local_pick(void)
{
    const char *test = getenv("RECOMP_CP_PICK");
    char key[sizeof(g_local[0].key)];
    int i;
    if (!g_local_count) return -1;
    if (test && *test) {
        i = atoi(test);
        return i >= 0 && i < g_local_count ? i : 0;
    }
    key[0] = 0;
    cp_pick_setting_get(key, (int)sizeof(key));
    if (key[0])
        for (i = 0; i < g_local_count; i++)
            if (!strcmp(g_local[i].key, key)) return i;
    return 0;
}

int cp_local_list(CpLocal *out, int max)
{
    int n, count;
    local_lock();
    count = g_local_count;
    n = count < max ? count : max;
    if (out && n > 0) memcpy(out, g_local, sizeof(CpLocal) * (size_t)n);
    LeaveCriticalSection(&g_local_cs);
    return count;
}

int cp_local_pick(void)
{
    int i;
    local_lock();
    i = local_pick();
    LeaveCriticalSection(&g_local_cs);
    return i;
}

int cp_local_set_pick(int index)
{
    char key[sizeof(g_local[0].key)];
    int ok = 0;
    local_lock();
    if (index >= 0 && index < g_local_count) {
        snprintf(key, sizeof(key), "%s", g_local[index].key);
        ok = 1;
    }
    LeaveCriticalSection(&g_local_cs);
    if (ok) {
        cp_pick_setting_set(key);
        fprintf(stderr, "[CREATED] picked fighter %d (%s)\n", index, key);
    }
    return ok;
}

int cp_local_set_name(int index, const char *name)
{
    char clean[CP_NAME_MAX + 1], shown[CP_NAME_MAX + 1] = "";
    int ok = 0;
    cp_name_clean(name, clean, sizeof(clean));
    local_lock();
    if (index >= 0 && index < g_local_count && names_store(g_local[index].key, clean)) {
        CpLocal *e = &g_local[index];
        uint8_t checked[CP_REC_LEN], before[CP_REC_LEN];
        memcpy(before, e->rec, CP_REC_LEN);
        record_set_name(e->rec, clean[0] ? clean : e->user_id);
        if (cp_record_sanitize(e->rec, checked)) {
            memcpy(e->rec, checked, CP_REC_LEN);
            snprintf(e->online, sizeof(e->online), "%s", clean);
        } else memcpy(e->rec, before, CP_REC_LEN);
        cp_record_name(e->rec, e->name, sizeof(e->name));
        snprintf(shown, sizeof(shown), "%s", e->name);
        ok = 1;
    }
    LeaveCriticalSection(&g_local_cs);
    if (ok) fprintf(stderr, "[CREATED] fighter %d plays online as \"%s\"%s\n", index, shown, clean[0] ? "" : " (its user ID)");
    return ok;
}

int cp_local_record(uint8_t rec[CP_REC_LEN])
{
    int i;
    local_lock();
    i = local_pick();
    if (i >= 0 && rec) memcpy(rec, g_local[i].rec, CP_REC_LEN);
    LeaveCriticalSection(&g_local_cs);
    return i >= 0;
}

void created_fighter_reload(void)
{
    InitOnceExecuteOnce(&g_local_once, local_init, NULL, NULL);
    EnterCriticalSection(&g_local_cs);
    g_local_state = 0;
    LeaveCriticalSection(&g_local_cs);
}

/* ---- in a match ---- */

uint32_t created_fighter_profile(int index)
{
    uint32_t gs = MEM32(GS_PTR), base;
    if (!gs || index < 0 || index >= PROFILE_COUNT) return 0;
    base = MEM32(gs + GS_PROFILES);
    return base ? base + PROFILE_BASE + (uint32_t)index * PROFILE_SIZE : 0;
}

static void record_to_profile(const uint8_t rec[CP_REC_LEN], uint32_t dst)
{
    const uint8_t *p = rec + 1;
    memcpy((void *)XBOX_PTR(dst + 0xC), p + R_NAME, CP_NAME_BYTES);
    memcpy((void *)XBOX_PTR(dst + 0xFC), p + R_PROFILE, 0x48);
    MEM8(dst + 0x4D8A) = p[R_FLAG];
    memcpy((void *)XBOX_PTR(dst + 0x5564), p + R_RECIPE, 0x98);
    memcpy((void *)XBOX_PTR(dst + 0x55FC), p + R_OPTIONS, 0x26);
}

static int g_slot_of_pad[4] = { -1, -1, -1, -1 };     /* profile slot each pad plays from */
static int g_linked;                                  /* the pads point at their slots now */
static unsigned g_ours;                               /* profile slots this file wrote */
static unsigned g_used_now;                           /* slots taken in the current sequence */

static void link_pads(int on)
{
    uint32_t gs = MEM32(GS_PTR);
    int pad;
    if (!gs) return;
    for (pad = 0; pad < 4; pad++)
        if (g_slot_of_pad[pad] >= 0) MEM8(gs + GS_PAD_PROFILE + (uint32_t)pad) = on ? (uint8_t)g_slot_of_pad[pad] : 0xFF;
    g_linked = on;
}

void created_fighter_begin(void)
{
    int k;
    for (k = 0; k < 4; k++) g_slot_of_pad[k] = -1;
    g_used_now = 0;
    g_linked = 0;
}

int created_fighter_inject(int slot, int pad, const uint8_t rec[CP_REC_LEN])
{
    uint32_t gs = MEM32(GS_PTR), dst = 0, holder;
    int index;
    char name[CP_NAME_MAX + 1];
    if (!gs || pad < 0 || pad > 3 || !rec) return -1;
    /* a spare profile slot: the highest without a user ID loaded in it (or one written here) */
    for (index = PROFILE_COUNT - 1; index >= 0; index--) {
        uint32_t p = created_fighter_profile(index);
        if (!p || (g_used_now & (1u << index))) continue;
        if (!MEM8(p + 0xC) || (g_ours & (1u << index))) { dst = p; break; }
    }
    if (!dst) {
        fprintf(stderr, "[CREATED] no spare profile slot for pad %d\n", pad);
        return -1;
    }
    record_to_profile(rec, dst);
    g_ours |= 1u << index;
    g_used_now |= 1u << index;
    g_slot_of_pad[pad] = index;
    MEM8(gs + GS_PAD_PROFILE + (uint32_t)pad) = (uint8_t)index;
    g_linked = 1;
    holder = MEM32(gs + GS_HOLDERS);
    if (holder) MEM8(holder + (uint32_t)pad * 0x1Cu + 1) = 0;     /* the match load builds this look */
    cp_record_name(rec, name, sizeof(name));
    fprintf(stderr, "[CREATED] slot %d pad %d plays %s (archetype %d, record %08X) from profile slot %d\n",
            slot, pad, name, rec[1 + R_ARCH], cp_record_crc(rec), index);
    return CP_FIGHTER_ID;
}

/* Every fight step: once the match is finished (phase 3), the pads are unlinked. The game's
 * post-fight reward pass walks each slot's profile (sub_00064ED0 -> sub_00014C20: Story rewards,
 * level-ups of the sixth stat, database lookups of its fields) and crashed in a two-created-fighter
 * match. Not earlier: unlinked as soon as the looks were built (the first version), the fight
 * still reached the profiles through the pads and read low guest memory instead, and about one
 * created-fighter match in six desynced by a few float ulps of the fighters' positions (stock
 * matches never). The phase is simulated state, so every PC unlinks at the same step.
 * Game.Rematch() links them again for the rematch's setup. */
#define G_MATCH_PHASE   0x003B8F30u     /* 0 start, 1 fighting, 2 decided, 3 finished */
void created_fighter_on_step(void)
{
    if (!g_linked || MEM32(G_MATCH_PHASE) < 3u) return;
    link_pads(0);
    fprintf(stderr, "[CREATED] the match is finished: pads unlinked from the records\n");
}

int created_fighter_before_funccall(const char *text)
{
    int any = 0, pad;
    for (pad = 0; pad < 4; pad++) if (g_slot_of_pad[pad] >= 0) any = 1;
    if (!any) return 0;
    if (!strcmp(text, "Game.Rematch()")) {
        if (!g_linked) {
            link_pads(1);
            fprintf(stderr, "[CREATED] rematch: the pads play their records again\n");
        }
        return 0;
    }
    if (g_linked && (!strncmp(text, "Game.UpdateMatchRewardsPoints", 29) || !strncmp(text, "MemCard.Save", 12) ||
                     !strncmp(text, "MemCard.QuerySave", 17) || !strncmp(text, "Game.Quit", 9) ||
                     !strncmp(text, "Game.StartBattleMode", 20))) {
        link_pads(0);
        fprintf(stderr, "[CREATED] pads unlinked from the records before %s\n", text);
    }
    if (!strncmp(text, "Game.Quit", 9) || !strncmp(text, "Game.StartBattleMode", 20))
        for (pad = 0; pad < 4; pad++) g_slot_of_pad[pad] = -1;     /* the match is over for good */
    return 0;
}

/* ---- tests: RECOMP_TEST_CP="slot=1,from=0[,file=<profile dump>][,mod=<n>:<i>:<hex>]" ----
 * At each automatch sequence, slot `slot` plays the record of profile `from` (or of a profile
 * dump or save file, `file`); `mod` changes recipe dword i from the n-th injection on. */

static int g_parsed, g_slot = -1, g_from, g_mod_n, g_mod_i, g_count;
static unsigned g_mod_v;
static char g_file[260];

static void parse(void)
{
    const char *e = getenv("RECOMP_TEST_CP");
    g_parsed = 1;
    if (!e || !*e) return;
    while (*e) {
        int n = 0;
        if (!strncmp(e, "file=", 5)) {
            size_t k = strcspn(e + 5, ",");
            snprintf(g_file, sizeof(g_file), "%.*s", (int)(k < sizeof(g_file) ? k : sizeof(g_file) - 1), e + 5);
            e += 5 + k;
        } else if (sscanf(e, "slot=%d%n", &g_slot, &n) == 1 || sscanf(e, "from=%d%n", &g_from, &n) == 1 ||
                   sscanf(e, "mod=%d:%d:%x%n", &g_mod_n, &g_mod_i, &g_mod_v, &n) == 3) {
            e += n;
        } else e++;
        while (*e == ',' || *e == ' ') e++;
    }
    fprintf(stderr, "[CP-TEST] slot %d: profile %d%s%s, mod #%d recipe[%d]=%08X\n",
            g_slot, g_from, g_file[0] ? " / file " : "", g_file, g_mod_n, g_mod_i, g_mod_v);
}

int created_fighter_test_slot(int slot, int pad)
{
    static uint8_t profile[PROFILE_SIZE];
    uint8_t rec[CP_REC_LEN], ok[CP_REC_LEN];
    if (!g_parsed) parse();
    if (slot != g_slot) return -1;
    if (g_file[0]) {
        if (!profile_from_file(g_file, profile, NULL)) {
            fprintf(stderr, "[CP-TEST] cannot read %s\n", g_file);
            return -1;
        }
    } else {
        uint32_t src = created_fighter_profile(g_from);
        if (!src) return -1;
        memcpy(profile, (const void *)XBOX_PTR(src), PROFILE_SIZE);
    }
    g_count++;
    if (g_mod_n && g_count >= g_mod_n) wr32(profile + 0x5564 + 4 * g_mod_i, g_mod_v);
    cp_record_from_profile(profile, rec);
    if (!cp_record_sanitize(rec, ok)) {
        fprintf(stderr, "[CP-TEST] the record is not playable\n");
        return -1;
    }
    {
        int s[6];
        cp_record_stats(ok, s);
        fprintf(stderr, "[CP-TEST] #%d expected stats: %d %d %d %d %d %d\n", g_count, s[0], s[1], s[2], s[3], s[4], s[5]);
    }
    return created_fighter_inject(slot, pad, ok);
}

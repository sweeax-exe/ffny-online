/*
 * Songs: restored and (later) player-supplied music in the jukebox and the playlist.
 * Research and owner decisions: docs/research/custom-songs.md.
 *
 * The game lists songs from the Song table of assets/djv2.sod (title, artist, album and the
 * folder under music/ in AUDIO/MUSIC0-4.VIV). At start-up this file appends rows for the
 * enabled extra songs to a copy of that database, keeps it in <data>/cache/songs and serves it
 * in place of the original through the toolkit's game-file redirect. It chains to whatever
 * redirect was installed before it (a language pack's), and builds on that pack's copy of
 * djv2.sod when the pack serves one. The dump itself is never written.
 *
 * Slice 1: the one cut song with audio on the disc, music/holler ("Holler If Ya Hear Me",
 * 2Pac), on by default ([songs] restored).
 */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "recomp_settings.h"
#include "pc_songs.h"
#include "song_convert.h"
#include "song_library.h"
#include "../patches/song_cursor.h"

#if !defined(_WIN32)
#include <ctype.h>
#include <dirent.h>
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#else
#include <ctype.h>
#endif

/* The toolkit's game-file redirect (kernel.h, not on this target's include path). */
#if defined(_WIN32)
typedef WCHAR xbox_host_char;
#else
typedef char xbox_host_char;
#endif
extern BOOL (*g_xbox_game_file_redirect)(const char *relative, xbox_host_char *host_path, DWORD size);

#define SONGS_PATH     1024
#define SOD_MAX        (32u << 20)
#define SOD_NAME       "assets/djv2.sod"

/* ---- the extra songs ---------------------------------------------------------------------------- */

typedef struct {
    const char *entity;    /* folder under music/ (at most 15 characters: the playlist's slots are 16 bytes) */
    const char *title, *artist, *album;
} SongInfo;

/* Cut from the release: complete audio and map on the disc (music1.viv), no Song row. Identified
 * by the owner from the audio (docs/research/custom-songs.md section 6). */
static const SongInfo k_restored[] = {
    { "holler", "Holler If Ya Hear Me", "2Pac", "Strictly 4 My N.I.G.G.A.Z." },
};

static SongInfo g_songs[PC_SONGS_MAX_ROWS];
static int g_song_count;

/* ---- small helpers ------------------------------------------------------------------------------- */

typedef struct { uint8_t *p; size_t n, cap; } Buf;

static int buf_reserve(Buf *b, size_t n)
{
    if (n <= b->cap) return 1;
    size_t cap = b->cap ? b->cap : 4096;
    while (cap < n) cap *= 2;
    uint8_t *p = (uint8_t *)realloc(b->p, cap);
    if (!p) return 0;
    b->p = p;
    b->cap = cap;
    return 1;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static char g_error[256];
static int fail(const char *what)
{
    if (!g_error[0]) snprintf(g_error, sizeof g_error, "%s", what);
    return 0;
}

/* The database's own string hash: djb2 over the bytes and the terminating NUL. */
uint32_t pc_songs_name_hash(const char *s)
{
    uint32_t h = 5381;
    do { h = h * 33 + (uint8_t)*s; } while (*s++);
    return h;
}

/* ---- SODB: append rows to one table ------------------------------------------------------------- */
/*
 * Layout (setup-side reader with the same checks: the fighter-mod research, setup/fighter_sod.py):
 *   0 "SODB", 4 version 2, 8 schema fingerprint, 12 schema, 16 tables, 20 strings (offsets).
 *   index: [tag] count stride; with stride != 0, count keys, then count values of max(stride,1) words.
 *   schema: tagged index, key = hash(table) ^ hash(field) (hash(table) alone = the row name),
 *           value = (word offset in the record, type; 1 = string index).
 *   tables: tagged index, key = hash(table), value = table header:
 *           tag, primary group, secondary group, record count; each group is an index of
 *           (member key -> tagged sub-index of record offsets), keyed sub-indexes sorted by value.
 *   strings: "STRT", count, (hash, offset) pairs; records hold string indexes.
 * New strings, records, sub-indexes and the string table are appended; existing records keep
 * their offsets and bytes, and each group's member map is rewritten in place (same size), so the
 * game's pointer-based readers see the rebuilt sub-indexes.
 */

typedef struct {
    Buf out;
    uint32_t nstr;
    uint32_t *str_hash, *str_off;    /* nstr entries, grown as strings are added */
    uint32_t schema;
} Sod;

static int sod_range(const Sod *s, uint32_t off, uint32_t len)
{
    return off <= s->out.n && len <= s->out.n - off;
}

static uint32_t sod_word(const Sod *s, uint32_t off) { return sod_range(s, off, 4) ? rd32(s->out.p + off) : 0; }

/* Find key in an index; returns the address of its value words, or 0. */
static uint32_t sod_lookup(const Sod *s, uint32_t index, int tagged, uint32_t key)
{
    uint32_t at = index + (tagged ? 4 : 0);
    if (!sod_range(s, at, 8)) return 0;
    uint32_t count = sod_word(s, at), stride = sod_word(s, at + 4);
    if (!stride || stride > 256 || !sod_range(s, at + 8, count * 4 * (stride + 1))) return 0;
    for (uint32_t i = 0; i < count; i++)
        if (sod_word(s, at + 8 + i * 4) == key) return at + 8 + count * 4 + i * stride * 4;
    return 0;
}

static uint32_t sod_emit(Sod *s, const void *data, size_t n)
{
    while (s->out.n % 4) {
        if (!buf_reserve(&s->out, s->out.n + 1)) return 0;
        s->out.p[s->out.n++] = 0;
    }
    if (s->out.n + n > SOD_MAX || !buf_reserve(&s->out, s->out.n + n)) return 0;
    uint32_t at = (uint32_t)s->out.n;
    memcpy(s->out.p + at, data, n);
    s->out.n += n;
    return at;
}

static int sod_intern(Sod *s, const char *value, uint32_t *index)
{
    uint32_t h = pc_songs_name_hash(value);
    for (uint32_t i = 0; i < s->nstr; i++) {
        if (s->str_hash[i] != h || !s->str_off[i] || !sod_range(s, s->str_off[i], 1)) continue;
        if (!strncmp((const char *)s->out.p + s->str_off[i], value, s->out.n - s->str_off[i])) { *index = i; return 1; }
    }
    uint32_t at = sod_emit(s, value, strlen(value) + 1);
    if (!at) return fail("database too large");
    uint32_t *hh = (uint32_t *)realloc(s->str_hash, (s->nstr + 1) * sizeof *hh);
    if (hh) s->str_hash = hh;
    uint32_t *oo = (uint32_t *)realloc(s->str_off, (s->nstr + 1) * sizeof *oo);
    if (oo) s->str_off = oo;
    if (!hh || !oo) return fail("out of memory");
    s->str_hash[s->nstr] = h;
    s->str_off[s->nstr] = at;
    *index = s->nstr++;
    return 1;
}

/* Field (word offset, type) of a table, or 0 when the schema has no such field. */
static int sod_field(const Sod *s, uint32_t table_hash, const char *field, uint32_t *word, uint32_t *type)
{
    uint32_t key = field ? table_hash ^ pc_songs_name_hash(field) : table_hash;
    uint32_t v = sod_lookup(s, s->schema, 1, key);
    if (!v) return 0;
    *word = sod_word(s, v);
    *type = sod_word(s, v + 4);
    return 1;
}

typedef struct { uint32_t value, offset; } Pair;

static int pair_cmp(const void *a, const void *b)
{
    const Pair *x = (const Pair *)a, *y = (const Pair *)b;
    if (x->value != y->value) return x->value < y->value ? -1 : 1;
    return x->offset < y->offset ? -1 : x->offset > y->offset;
}

/*
 * Append `count` rows to table `name`. Each row starts as a copy of the table's first record
 * (fields the caller does not name keep the template's values), then gets the named string
 * fields. fields[i*nfields + f] is the value for field_names[f] (NULL field name = the row
 * name). Rows whose row name already exists are skipped (the database already has the song).
 */
int pc_songs_sod_add_rows(const uint8_t *in, size_t n, const char *name, const char *const *field_names,
                          int nfields, const char *const *values, int count, uint8_t **out, size_t *out_n,
                          int *added)
{
    Sod s;
    Pair *pairs = NULL;
    uint32_t *new_rows = NULL;
    int nnew = 0, ok = 0;
    memset(&s, 0, sizeof s);
    g_error[0] = 0;
    *out = NULL;
    *out_n = 0;
    if (added) *added = 0;

    if (n < 24 || n > SOD_MAX || memcmp(in, "SODB", 4) || rd32(in + 4) != 2) return fail("not a version 2 SODB database");
    if (!buf_reserve(&s.out, n)) return fail("out of memory");
    memcpy(s.out.p, in, n);
    s.out.n = n;
    s.schema = sod_word(&s, 12);
    uint32_t tables = sod_word(&s, 16), strings = sod_word(&s, 20);
    if (!sod_range(&s, s.schema, 4) || memcmp(s.out.p + s.schema, "SKMA", 4) || !sod_range(&s, tables, 4) ||
        memcmp(s.out.p + tables, "DATA", 4) || !sod_range(&s, strings, 8) || memcmp(s.out.p + strings, "STRT", 4))
        { fail("database sections not found"); goto done; }

    s.nstr = sod_word(&s, strings + 4);
    if (s.nstr > 1000000 || !sod_range(&s, strings + 8, s.nstr * 8)) { fail("bad string table"); goto done; }
    s.str_hash = (uint32_t *)malloc((s.nstr + 1) * sizeof(uint32_t));
    s.str_off = (uint32_t *)malloc((s.nstr + 1) * sizeof(uint32_t));
    if (!s.str_hash || !s.str_off) { fail("out of memory"); goto done; }
    for (uint32_t i = 0; i < s.nstr; i++) {
        s.str_hash[i] = sod_word(&s, strings + 8 + i * 8);
        s.str_off[i] = sod_word(&s, strings + 12 + i * 8);
    }

    uint32_t thash = pc_songs_name_hash(name);
    uint32_t hv = sod_lookup(&s, tables, 1, thash);
    if (!hv) { fail("table not in the database"); goto done; }
    uint32_t header = sod_word(&s, hv);
    if (!sod_range(&s, header, 16) || sod_word(&s, header) != thash) { fail("bad table header"); goto done; }
    uint32_t rows = sod_word(&s, header + 12);

    /* The table's record offsets (from the primary group's first member) and their extent. */
    uint32_t group0 = sod_word(&s, header + 4);
    if (!sod_range(&s, group0, 8) || !sod_word(&s, group0) || sod_word(&s, group0 + 4) != 1) { fail("bad table index"); goto done; }
    uint32_t members0 = sod_word(&s, group0);
    uint32_t first_sub = sod_word(&s, group0 + 8 + members0 * 4);
    if (!sod_range(&s, first_sub, 12) || sod_word(&s, first_sub + 4) != rows) { fail("bad record index"); goto done; }
    uint32_t sub_stride = sod_word(&s, first_sub + 8);
    uint32_t rec_list = first_sub + 12 + (sub_stride ? rows * 4 : 0);
    if (!rows || !sod_range(&s, rec_list, rows * 4)) { fail("table has no records"); goto done; }
    uint32_t template_off = sod_word(&s, rec_list);

    /* Record size: the highest field word offset this schema gives the table, plus one. */
    uint32_t words = 1, w, t;
    for (int f = 0; f < nfields; f++)
        if (sod_field(&s, thash, field_names[f], &w, &t) && w + 1 > words) words = w + 1;
    {
        /* Any other schema entry of this table may sit further out: take the gap to the next
         * record when the records are laid out contiguously (they are in the retail file). */
        uint32_t next = 0;
        for (uint32_t i = 0; i < rows; i++) {
            uint32_t o = sod_word(&s, rec_list + i * 4);
            if (o > template_off && (!next || o < next)) next = o;
        }
        if (next && next - template_off >= words * 4 && next - template_off <= 4096) words = (next - template_off) / 4;
    }
    if (!sod_range(&s, template_off, words * 4)) { fail("bad template record"); goto done; }

    new_rows = (uint32_t *)calloc((size_t)count + 1, sizeof *new_rows);
    if (!new_rows) { fail("out of memory"); goto done; }
    uint32_t name_word = 0, name_type = 0;
    if (!sod_field(&s, thash, NULL, &name_word, &name_type)) { fail("table has no row name"); goto done; }
    /*
     * The game's record handles are (table rank << 24) | the record's rank in the table's name
     * index, which is sorted by name hash, and profiles save those handles (the jukebox's song
     * keys). So every added row gets a row name whose hash sorts after all the others: the
     * retail rows keep their ranks and the added ones take the ranks after them. The row
     * name is internal; the jukebox and the banner show the Title field.
     */
    uint32_t max_hash = 0;
    for (uint32_t i = 0; i < rows; i++) {
        uint32_t idx = sod_word(&s, sod_word(&s, rec_list + i * 4) + name_word * 4);
        if (idx < s.nstr && s.str_hash[idx] > max_hash) max_hash = s.str_hash[idx];
    }
    for (int r = 0; r < count; r++) {
        const char *base = NULL;
        char row_name[160];
        for (int f = 0; f < nfields; f++) if (!field_names[f]) base = values[r * nfields + f];
        if (!base || strlen(base) > 120) { fail("row without a usable name"); goto done; }
        /* The same song added again (a database that already has it) is left alone. */
        int exists = 0;
        for (uint32_t i = 0; i < rows && !exists; i++) {
            uint32_t idx = sod_word(&s, sod_word(&s, rec_list + i * 4) + name_word * 4);
            exists = idx < s.nstr && s.str_off[idx] && sod_range(&s, s.str_off[idx], 1) &&
                     !strncmp((const char *)s.out.p + s.str_off[idx], base, strlen(base)) &&
                     (s.out.p[s.str_off[idx] + strlen(base)] == 0 || s.out.p[s.str_off[idx] + strlen(base)] == '~');
        }
        if (exists) continue;
        snprintf(row_name, sizeof row_name, "%s", base);
        for (unsigned salt = 1; pc_songs_name_hash(row_name) <= max_hash; salt++) {
            if (salt > 100000000u) { fail("no row name sorts last"); goto done; }
            snprintf(row_name, sizeof row_name, "%s~%u", base, salt);
        }
        /* Only the retail ranks must hold: added rows sort after them, in any order among themselves. */
        uint8_t rec[4096];
        memcpy(rec, s.out.p + template_off, words * 4);
        for (int f = 0; f < nfields; f++) {
            uint32_t idx;
            /* The row name is a string index too, with its own type code (0x02000000). */
            if (!sod_field(&s, thash, field_names[f], &w, &t) || (field_names[f] && t != 1) || w >= words)
                { fail("table has no such text field"); goto done; }
            if (!sod_intern(&s, field_names[f] ? values[r * nfields + f] : row_name, &idx)) goto done;
            wr32(rec + w * 4, idx);
        }
        new_rows[nnew] = sod_emit(&s, rec, words * 4);
        if (!new_rows[nnew]) { fail("database too large"); goto done; }
        nnew++;
    }

    if (nnew) {
        pairs = (Pair *)malloc(((size_t)rows + nnew) * sizeof *pairs);
        if (!pairs) { fail("out of memory"); goto done; }
        for (int g = 0; g < 2; g++) {
            uint32_t group = sod_word(&s, header + 4 + g * 4);
            if (!sod_range(&s, group, 8)) { fail("bad table group"); goto done; }
            uint32_t members = sod_word(&s, group), gstride = sod_word(&s, group + 4);
            if (!members) continue;
            if (gstride != 1 || !sod_range(&s, group + 8, members * 8)) { fail("bad table group"); goto done; }
            for (uint32_t m = 0; m < members; m++) {
                uint32_t key = sod_word(&s, group + 8 + m * 4);
                uint32_t sub = sod_word(&s, group + 8 + members * 4 + m * 4);
                if (!sod_range(&s, sub, 12) || sod_word(&s, sub + 4) != rows) { fail("bad member index"); goto done; }
                uint32_t tag = sod_word(&s, sub), keyed = sod_word(&s, sub + 8);
                uint32_t list = sub + 12 + (keyed ? rows * 4 : 0);
                if (keyed > 1 || !sod_range(&s, list, rows * 4)) { fail("bad member index"); goto done; }
                uint32_t total = rows + (uint32_t)nnew, mw = 0, mt = 0;
                for (uint32_t i = 0; i < rows; i++) pairs[i].offset = sod_word(&s, list + i * 4);
                for (int i = 0; i < nnew; i++) pairs[rows + i].offset = new_rows[i];
                if (keyed) {
                    uint32_t v = sod_lookup(&s, s.schema, 1, key);
                    if (!v) { fail("index key not in schema"); goto done; }
                    mw = sod_word(&s, v);
                    mt = sod_word(&s, v + 4);
                    for (uint32_t i = 0; i < total; i++) {
                        uint32_t val = sod_word(&s, pairs[i].offset + mw * 4);
                        if (key == thash || mt == 1) {
                            if (val >= s.nstr) { fail("bad string reference"); goto done; }
                            val = s.str_hash[val];
                        }
                        pairs[i].value = val;
                    }
                    qsort(pairs, total, sizeof *pairs, pair_cmp);
                }
                /* tag, count, keyed, [keys], record offsets */
                Buf blob = {0};
                if (!buf_reserve(&blob, (3 + (size_t)total * 2) * 4)) { fail("out of memory"); goto done; }
                wr32(blob.p, tag);
                wr32(blob.p + 4, total);
                wr32(blob.p + 8, keyed);
                blob.n = 12;
                if (keyed) for (uint32_t i = 0; i < total; i++, blob.n += 4) wr32(blob.p + blob.n, pairs[i].value);
                for (uint32_t i = 0; i < total; i++, blob.n += 4) wr32(blob.p + blob.n, pairs[i].offset);
                uint32_t at = sod_emit(&s, blob.p, blob.n);
                free(blob.p);
                if (!at) { fail("database too large"); goto done; }
                wr32(s.out.p + group + 8 + members * 4 + m * 4, at);
            }
        }
        wr32(s.out.p + header + 12, rows + (uint32_t)nnew);
    }

    if (nnew) {
        /* The string table, re-emitted with the added strings. */
        Buf blob = {0};
        if (!buf_reserve(&blob, 8 + (size_t)s.nstr * 8)) { fail("out of memory"); goto done; }
        memcpy(blob.p, "STRT", 4);
        wr32(blob.p + 4, s.nstr);
        for (uint32_t i = 0; i < s.nstr; i++) {
            wr32(blob.p + 8 + i * 8, s.str_hash[i]);
            wr32(blob.p + 12 + i * 8, s.str_off[i]);
        }
        uint32_t at = sod_emit(&s, blob.p, 8 + (size_t)s.nstr * 8);
        free(blob.p);
        if (!at) { fail("database too large"); goto done; }
        wr32(s.out.p + 20, at);
    }
    ok = 1;
    if (added) *added = nnew;
    *out = s.out.p;
    *out_n = s.out.n;
    s.out.p = NULL;
done:
    free(s.out.p);
    free(s.str_hash);
    free(s.str_off);
    free(pairs);
    free(new_rows);
    return ok;
}

/* ---- serving ---------------------------------------------------------------------------------- */

static BOOL (*g_previous_redirect)(const char *relative, xbox_host_char *host_path, DWORD size);
static char g_served_host[SONGS_PATH];

static int same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a == '\\' ? '/' : (char)((*a >= 'A' && *a <= 'Z') ? *a + 32 : *a);
        char y = *b == '\\' ? '/' : (char)((*b >= 'A' && *b <= 'Z') ? *b + 32 : *b);
        if (x != y) return 0;
    }
    return *a == *b;
}

/* Custom songs: the player's files, converted into <data>/cache/songs/<id> (song_convert.c). */
typedef struct {
    char id[16];
    char dir[SONGS_PATH];
    SongTags tags;
} Custom;
static Custom g_custom[PC_SONGS_MAX_ROWS];
static int g_ncustom;

static BOOL serve(const char *host, xbox_host_char *host_path, DWORD size)
{
#if defined(_WIN32)
    return MultiByteToWideChar(CP_UTF8, 0, host, -1, host_path, (int)size) != 0;
#else
    return (size_t)snprintf(host_path, size, "%s", host) < size;
#endif
}

static BOOL songs_redirect(const char *relative, xbox_host_char *host_path, DWORD size)
{
    if (g_served_host[0] && same_name(relative, SOD_NAME)) return serve(g_served_host, host_path, size);
    /* music/<id>/<file> of a custom song: the converted file. The game looks for a song's files
     * on the disc as loose files before it looks in AUDIO/MUSIC0-4.VIV. */
    if (g_ncustom && !_strnicmp(relative, "music", 5) && (relative[5] == '/' || relative[5] == '\\')) {
        const char *id = relative + 6, *sep = id;
        while (*sep && *sep != '/' && *sep != '\\') sep++;
        if (*sep && sep - id < 16 && !strchr(sep + 1, '/') && !strchr(sep + 1, '\\')) {
            for (int i = 0; i < g_ncustom; i++)
                if ((size_t)(sep - id) == strlen(g_custom[i].id) && !_strnicmp(id, g_custom[i].id, (size_t)(sep - id))) {
                    char host[SONGS_PATH + 64];
                    snprintf(host, sizeof host, "%s" HOST_SEP "%s", g_custom[i].dir, sep + 1);
                    return serve(host, host_path, size);
                }
        }
    }
    return g_previous_redirect ? g_previous_redirect(relative, host_path, size) : FALSE;
}

static int read_all(const char *path, uint8_t **data, size_t *n)
{
    FILE *f = fopen(path, "rb");
    *data = NULL;
    *n = 0;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) || ftell(f) <= 0 || ftell(f) > (long)SOD_MAX) { fclose(f); return 0; }
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = (uint8_t *)malloc((size_t)len);
    if (*data && fread(*data, 1, (size_t)len, f) == (size_t)len) *n = (size_t)len;
    fclose(f);
    if (!*n) { free(*data); *data = NULL; return 0; }
    return 1;
}

static void make_dirs(const char *path)
{
    char tmp[SONGS_PATH];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        char c = *p;
        *p = 0;
#if defined(_WIN32)
        _mkdir(tmp);
#else
        mkdir(tmp, 0755);
#endif
        *p = c;
    }
}

/* The database the game would otherwise read: a language pack's copy, else the dump's. */
static int source_database(const char *game_dir, char *out, size_t size)
{
    if (g_previous_redirect) {
        xbox_host_char host[SONGS_PATH];
        if (g_previous_redirect(SOD_NAME, host, SONGS_PATH)) {
#if defined(_WIN32)
            return WideCharToMultiByte(CP_UTF8, 0, host, -1, out, (int)size, NULL, NULL) != 0;
#else
            return (size_t)snprintf(out, size, "%s", host) < size;
#endif
        }
    }
    return (size_t)snprintf(out, size, "%s" HOST_SEP "assets" HOST_SEP "djv2.sod", game_dir) < size;
}

int pc_songs_count(void) { return g_song_count; }
const char *pc_songs_entity(int i) { return i >= 0 && i < g_song_count ? g_songs[i].entity : NULL; }

/* ---- which song each record handle names ------------------------------------------------------ */

static char g_rank_entity[PC_SONGS_MAX_ROWS][16];
static char g_rank_id[PC_SONGS_MAX_ROWS][24];
static char g_rank_label[PC_SONGS_MAX_ROWS][128];
static char g_data_root[SONGS_PATH];
static int g_rank_count, g_retail_rows;

/* The Song table's records in name-index order (= the low bits of their handles), as entities.
 * Returns the row count, or -1. */
static int read_rank_table(const uint8_t *p, size_t n, char (*out)[16], int max)
{
    Sod s;
    int rows = -1;
    memset(&s, 0, sizeof s);
    if (n < 24 || memcmp(p, "SODB", 4)) return -1;
    s.out.p = (uint8_t *)p;
    s.out.n = n;
    s.schema = sod_word(&s, 12);
    uint32_t tables = sod_word(&s, 16), strings = sod_word(&s, 20);
    uint32_t nstr = sod_word(&s, strings + 4);
    uint32_t thash = pc_songs_name_hash("Song");
    uint32_t hv = sod_lookup(&s, tables, 1, thash);
    uint32_t w, t;
    if (!hv || !sod_field(&s, thash, "Entity", &w, &t) || !sod_range(&s, strings + 8, nstr * 8)) return -1;
    uint32_t header = sod_word(&s, hv), group0 = sod_word(&s, header + 4), count = sod_word(&s, header + 12);
    uint32_t members = sod_word(&s, group0), sub = sod_word(&s, group0 + 8 + members * 4);
    if (!members || sod_word(&s, sub + 4) != count || sod_word(&s, sub + 8) != 1) return -1;
    uint32_t list = sub + 12 + count * 4;
    if (!sod_range(&s, list, count * 4)) return -1;
    rows = (int)count;
    uint32_t wt = 0, wa = 0, tt;
    int have_title = sod_field(&s, thash, "Title", &wt, &tt), have_artist = sod_field(&s, thash, "Artist", &wa, &tt);
    for (uint32_t i = 0; i < count && (int)i < max; i++) {
        uint32_t rec = sod_word(&s, list + i * 4);
        uint32_t idx = sod_word(&s, rec + w * 4);
        uint32_t at = idx < nstr ? sod_word(&s, strings + 12 + idx * 8) : 0;
        out[i][0] = 0;
        if (at && sod_range(&s, at, 1)) snprintf(out[i], sizeof out[i], "%s", (const char *)p + at);
        if (out == g_rank_entity) {
            /* "Title - Artist" for the launcher's Beatbox list (the state file carries it). */
            const char *title = "", *artist = "";
            uint32_t ti = have_title ? sod_word(&s, rec + wt * 4) : 0xFFFFFFFFu, ai = have_artist ? sod_word(&s, rec + wa * 4) : 0xFFFFFFFFu;
            uint32_t ta = ti < nstr ? sod_word(&s, strings + 12 + ti * 8) : 0, aa = ai < nstr ? sod_word(&s, strings + 12 + ai * 8) : 0;
            if (ta && sod_range(&s, ta, 1)) title = (const char *)p + ta;
            if (aa && sod_range(&s, aa, 1)) artist = (const char *)p + aa;
            snprintf(g_rank_label[i], sizeof g_rank_label[i], "%.60s - %.60s", title, artist);
        }
    }
    return rows;
}

const char *pc_songs_data_root(void) { return g_data_root; }
int pc_songs_retail_rows(void) { return g_retail_rows; }
int pc_songs_rows(void) { return g_rank_count; }
const char *pc_songs_label_for_rank(uint32_t rank)
{
    return rank < (uint32_t)g_rank_count ? g_rank_label[rank] : NULL;
}

const char *pc_songs_id_for_rank(uint32_t rank)
{
    return rank < (uint32_t)g_rank_count ? g_rank_id[rank] : NULL;
}

const char *pc_songs_entity_for_rank(uint32_t rank)
{
    return rank < (uint32_t)g_rank_count ? g_rank_entity[rank] : NULL;
}


/* ---- custom songs: converting the player's files (song_library.c finds them) ----------------- */

static int file_meta(const char *path, long long *size, long long *mtime)
{
#if defined(_WIN32)
    WCHAR w[SONGS_PATH];
    struct _stat64 st;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, SONGS_PATH) || _wstat64(w, &st)) return 0;
#else
    struct stat st;
    if (stat(path, &st)) return 0;
#endif
    *size = (long long)st.st_size;
    *mtime = (long long)st.st_mtime;
    return 1;
}


static void add_custom_songs(const char *data_root, const char *game_dir, int room)
{
    char err[200];
    int nfound, converted = 0, failed = 0, off = 0;
    SongTemplate tmpl[2];
    int have[2] = { 0, 0 };
    g_ncustom = 0;
    song_cursor_clear();
    memset(tmpl, 0, sizeof tmpl);
    if (room <= 0) return;
    SongEntry *found = (SongEntry *)calloc(SONG_LIBRARY_MAX, sizeof *found);
    if (!found) return;
    nfound = song_library_scan(data_root, found, SONG_LIBRARY_MAX);
    if (!nfound) {
        free(found);
        return;
    }
    for (int i = 0; i < nfound && g_ncustom < room; i++) {
        Custom *c = &g_custom[g_ncustom];
        char stamp_path[SONGS_PATH + 32], stamp[SONGS_PATH + 128], old[SONGS_PATH + 128] = "";
        long long size = 0, mtime = 0;
        if (!found[i].on) { off++; continue; }
        int mode = found[i].interactive ? 1 : 0;
        if (!have[mode]) {
            have[mode] = song_template_load(game_dir, mode, &tmpl[mode], err, sizeof err) ? 1 : -1;
            if (have[mode] < 0) fprintf(stderr, "[SONGS] %s songs not used: %s\n", mode ? "interactive" : "straight", err);
        }
        if (have[mode] < 0) { failed++; continue; }
        SongTemplate *tp = &tmpl[mode];
        snprintf(c->id, sizeof c->id, "%s", found[i].id);
        if (!file_meta(found[i].path, &size, &mtime) || !song_read_tags(found[i].path, &c->tags)) continue;
        snprintf(c->dir, sizeof c->dir, "%s" HOST_SEP "cache" HOST_SEP "songs" HOST_SEP "%s", data_root, c->id);
        snprintf(stamp_path, sizeof stamp_path, "%s" HOST_SEP "stamp.txt", c->dir);
        snprintf(stamp, sizeof stamp, "v%d t%08x s%lld m%lld %s %s\n", SONG_CONVERT_VERSION, tp->hash, size, mtime,
                 found[i].interactive ? "interactive" : "straight", found[i].path);
        FILE *f = fopen(stamp_path, "r");
        if (f) {
            if (!fgets(old, sizeof old, f)) old[0] = 0;
            fclose(f);
        }
        if (strcmp(old, stamp)) {
            make_dirs(stamp_path);
            remove(stamp_path);
            fprintf(stderr, "[SONGS] converting %s (%s) ...\n", found[i].path, found[i].interactive ? "interactive" : "straight");
            if (!song_convert(found[i].path, c->dir, tp, mode, err, sizeof err)) {
                fprintf(stderr, "[SONGS] %s not used: %s\n", found[i].path, err);
                failed++;
                continue;
            }
            f = fopen(stamp_path, "w");
            if (f) { fputs(stamp, f); fclose(f); }
            converted++;
        }
        SongInfo s = { c->id, c->tags.title, c->tags.artist, c->tags.album };
        g_songs[g_song_count++] = s;
        song_cursor_register(c->id, c->dir, mode);
        g_ncustom++;
    }
    fprintf(stderr, "[SONGS] custom songs: %d file(s) found, %d in the game (%d converted now), %d turned off%s%s\n",
            nfound, g_ncustom, converted, off, failed ? ", some not usable" : "",
            nfound > g_ncustom + failed + off ? "; the rest past the room the game has" : "");
    song_template_free(&tmpl[0]);
    song_template_free(&tmpl[1]);
    free(found);
}

void pc_songs_start(const char *data_root, const char *game_dir)
{
    char source[SONGS_PATH], cache[SONGS_PATH];
    uint8_t *in = NULL, *out = NULL;
    size_t in_n = 0, out_n = 0;
    int added = 0;

    g_song_count = 0;
    if (recomp_settings_get("songs", "restored", 1))
        for (size_t i = 0; i < sizeof k_restored / sizeof k_restored[0]; i++) g_songs[g_song_count++] = k_restored[i];
    {
        /* Test aid: RECOMP_TEST_SONG_CLONES=N adds N songs that play the disc's own music
         * folders under other titles, to exercise a full song store and playlist. */
        static const char *const folders[] = { "dirty", "walkwithme", "manup", "og", "venu", "comp", "move", "pump" };
        static char titles[PC_SONGS_MAX_ROWS][24];
        const char *clones = getenv("RECOMP_TEST_SONG_CLONES");
        int n = clones ? atoi(clones) : 0;
        for (int i = 0; i < n && g_song_count < PC_SONGS_MAX_ROWS; i++) {
            snprintf(titles[i], sizeof titles[i], "Test Song %d", i + 1);
            SongInfo s = { folders[i % 8], titles[i], "Test Artist", "Test Album" };
            g_songs[g_song_count++] = s;
        }
        /* Test aid: RECOMP_TEST_SONG_ADD="folder|title|artist|album" adds one row for a folder
         * the test put under music/ itself. */
        static char add[256];
        const char *a = getenv("RECOMP_TEST_SONG_ADD");
        if (a && *a && g_song_count < PC_SONGS_MAX_ROWS) {
            char *f[4] = { add, NULL, NULL, NULL };
            snprintf(add, sizeof add, "%s", a);
            for (int k = 1; k < 4; k++) {
                char *bar = strchr(f[k - 1], '|');
                if (!bar) break;
                *bar = 0;
                f[k] = bar + 1;
            }
            if (f[3] && strlen(f[0]) < 16) {
                SongInfo s = { f[0], f[1], f[2], f[3] };
                g_songs[g_song_count++] = s;
            }
        }
    }
    if (data_root && *data_root && recomp_settings_get("songs", "custom", 1))
        add_custom_songs(data_root, game_dir, PC_SONGS_MAX_ROWS - 32 - g_song_count);
    if (!g_song_count) {
        fprintf(stderr, "[SONGS] the game's own soundtrack (no extra songs)\n");
        return;
    }
    if (!data_root || !*data_root) {
        fprintf(stderr, "[SONGS] extra songs need a data folder; playing the game's own soundtrack\n");
        g_song_count = 0;
        return;
    }

    snprintf(g_data_root, sizeof g_data_root, "%s", data_root);
    g_previous_redirect = g_xbox_game_file_redirect;
    g_error[0] = 0;
    if (!source_database(game_dir, source, sizeof source) || !read_all(source, &in, &in_n)) {
        fprintf(stderr, "[SONGS] cannot read %s; playing the game's own soundtrack\n", source);
        g_song_count = 0;
        return;
    }

    /* The game holds PC_SONGS_MAX_ROWS songs (pc_songs.h): the extras beyond that are left out. */
    {
        char (*retail)[16] = (char (*)[16])calloc(PC_SONGS_MAX_ROWS, 16);
        g_retail_rows = retail ? read_rank_table(in, in_n, retail, PC_SONGS_MAX_ROWS) : -1;
        free(retail);
        if (g_retail_rows <= 0 || g_retail_rows >= PC_SONGS_MAX_ROWS) {
            fprintf(stderr, "[SONGS] unexpected song list in %s; playing the game's own soundtrack\n", source);
            free(in);
            g_song_count = 0;
            return;
        }
        if (g_retail_rows + g_song_count > PC_SONGS_MAX_ROWS) {
            fprintf(stderr, "[SONGS] %d extra songs, room for %d: the rest are left out this time\n",
                    g_song_count, PC_SONGS_MAX_ROWS - g_retail_rows);
            g_song_count = PC_SONGS_MAX_ROWS - g_retail_rows;
        }
    }

    {
        static const char *const fields[] = { NULL, "Title", "Artist", "Album", "Entity" };
        const char *values[PC_SONGS_MAX_ROWS * 5];
        for (int i = 0; i < g_song_count; i++) {
            values[i * 5 + 0] = g_songs[i].title;    /* the row name the retail rows use */
            values[i * 5 + 1] = g_songs[i].title;
            values[i * 5 + 2] = g_songs[i].artist;
            values[i * 5 + 3] = g_songs[i].album;
            values[i * 5 + 4] = g_songs[i].entity;
        }
        if (!pc_songs_sod_add_rows(in, in_n, "Song", fields, 5, values, g_song_count, &out, &out_n, &added)) {
            fprintf(stderr, "[SONGS] song list not extended (%s); playing the game's own soundtrack\n", g_error);
            free(in);
            g_song_count = 0;
            return;
        }
    }
    free(in);

    snprintf(cache, sizeof cache, "%s" HOST_SEP "cache" HOST_SEP "songs" HOST_SEP "djv2.sod", data_root);
    {
        /* Rewrite the copy only when it differs: a running game's earlier copy stays valid. */
        uint8_t *old = NULL;
        size_t old_n = 0;
        int same = read_all(cache, &old, &old_n) && old_n == out_n && !memcmp(old, out, out_n);
        free(old);
        if (!same) {
            char part[SONGS_PATH + 8];
            snprintf(part, sizeof part, "%s.part", cache);
            make_dirs(cache);
            FILE *f = fopen(part, "wb");
            int ok = f && fwrite(out, 1, out_n, f) == out_n;
            if (f && fclose(f)) ok = 0;
            remove(cache);
            if (!ok || rename(part, cache)) {
                remove(part);
                fprintf(stderr, "[SONGS] cannot write %s; playing the game's own soundtrack\n", cache);
                free(out);
                g_song_count = 0;
                return;
            }
        }
    }
    g_rank_count = read_rank_table(out, out_n, g_rank_entity, PC_SONGS_MAX_ROWS);
    if (g_rank_count < 0) g_rank_count = 0;
    /* A stable id per row for the PC-side jukebox file: the folder, and "#k" for the k-th
     * row after the first that plays the same folder. */
    for (int i = 0; i < g_rank_count; i++) {
        int k = 0;
        for (int j = 0; j < i; j++) k += !strcmp(g_rank_entity[i], g_rank_entity[j]);
        if (k) snprintf(g_rank_id[i], sizeof g_rank_id[i], "%s#%d", g_rank_entity[i], k);
        else snprintf(g_rank_id[i], sizeof g_rank_id[i], "%s", g_rank_entity[i]);
    }
    free(out);
    snprintf(g_served_host, sizeof g_served_host, "%s", cache);
    g_xbox_game_file_redirect = songs_redirect;
    fprintf(stderr, "[SONGS] %d extra song(s), %d added to the song list; database served from %s\n",
            g_song_count, added, cache);
    for (int i = 0; i < g_song_count; i++)
        fprintf(stderr, "[SONGS]   %s: \"%s\" - %s\n", g_songs[i].entity, g_songs[i].title, g_songs[i].artist);
}

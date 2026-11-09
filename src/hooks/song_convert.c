/*
 * Custom songs: a music file of the player's becomes a song folder the game plays
 * (docs/research/custom-songs.md, "custom songs").
 *
 * A disc song is music/<id>/{tune.mpf, main.mus, loading0.mus, overlay0.mus}: an EA PathFinder
 * map (v4, little-endian), the song cut into segments, each its own EA SCHl stream (EA-XA ADPCM,
 * split channels, 2408-sample blocks), and two banks of short clips (loading screens, fight
 * start). A custom song is built on the plainest disc song, minitrack2 (read from the player's
 * own dump at start-up; no game data is in this source): its map is kept node for node and only
 * its sample table (each segment's place in main.mus and length) is rewritten; its 48-segment
 * full play (intro, body, outro, ending) is filled with the player's song cut into 48 pieces in
 * order, so the song plays through once in the menus and its body loops in fights; the template's
 * other segments get pieces too. The clip banks are the template's own for now.
 */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "song_convert.h"

#if !defined(_WIN32)
#include <strings.h>
#define _stricmp strcasecmp
#endif

#include "../../thirdparty/audio-decoders/dr_mp3.h"
#include "../../thirdparty/audio-decoders/dr_flac.h"
#include "../../thirdparty/audio-decoders/dr_wav.h"
/* stb_vorbis has no header: the declarations used here. */
typedef struct stb_vorbis stb_vorbis;
typedef struct { char *vendor; int comment_list_length; char **comment_list; } stb_vorbis_comment;
extern int stb_vorbis_decode_memory(const unsigned char *mem, int len, int *channels, int *sample_rate, short **output);
extern stb_vorbis *stb_vorbis_open_memory(const unsigned char *data, int len, int *error, const void *alloc);
extern stb_vorbis_comment stb_vorbis_get_comment(stb_vorbis *f);
extern void stb_vorbis_close(stb_vorbis *f);

#define RATE          44100u
#define BLOCK         2408u           /* samples per SCDl block, as the game's own streams */
#define MAX_FILE      (512u << 20)
#define MAX_SECONDS   (20u * 60u)
#define MIN_SECONDS   5u
/* Streams start on 0x80 (the sample table counts in 0x80s). */
#define STREAM_ALIGN  0x80u

static int fail(char *err, size_t errn, const char *msg)
{
    if (err && errn) snprintf(err, errn, "%s", msg);
    return 0;
}

/* ---- files, with UTF-8 paths ---------------------------------------------------------------- */

static FILE *open_utf8(const char *path, const char *mode)
{
#if defined(_WIN32)
    WCHAR wpath[1024], wmode[8];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024)) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 8)) return NULL;
    return _wfopen(wpath, wmode);
#else
    return fopen(path, mode);
#endif
}

static int read_all(const char *path, uint8_t **data, size_t *n, size_t max)
{
    FILE *f = open_utf8(path, "rb");
    *data = NULL;
    *n = 0;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 0; }
    long len = ftell(f);
    if (len <= 0 || (size_t)len > max || fseek(f, 0, SEEK_SET)) { fclose(f); return 0; }
    *data = (uint8_t *)malloc((size_t)len);
    if (*data && fread(*data, 1, (size_t)len, f) == (size_t)len) *n = (size_t)len;
    fclose(f);
    if (!*n) { free(*data); *data = NULL; return 0; }
    return 1;
}

static int write_all(const char *path, const void *data, size_t n)
{
    char part[1100];
    snprintf(part, sizeof part, "%s.part", path);
    FILE *f = open_utf8(part, "wb");
    int ok = f && fwrite(data, 1, n, f) == n;
    if (f && fclose(f)) ok = 0;
    if (!ok) { remove(part); return 0; }
#if defined(_WIN32)
    WCHAR wfrom[1100], wto[1100];
    MultiByteToWideChar(CP_UTF8, 0, part, -1, wfrom, 1100);
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wto, 1100);
    if (!MoveFileExW(wfrom, wto, MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(wfrom); return 0; }
#else
    if (rename(part, path)) { remove(part); return 0; }
#endif
    return 1;
}

static uint32_t rd16le(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32le(const uint8_t *p) { return rd16le(p) | rd16le(p + 2) << 16; }
static uint32_t rd32be(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void wr32le(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* ---- text the game can draw ------------------------------------------------------------------- */

/* One code point -> ASCII: accented Latin letters lose the accent, typographic punctuation becomes
 * its plain form, anything else '?'. */
static char ascii_of(uint32_t c)
{
    static const char *const latin1 =
        "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";   /* U+00C0 .. U+00FF */
    if (c >= 0x20 && c < 0x7F) return (char)c;
    if (c >= 0xC0 && c <= 0xFF) return latin1[c - 0xC0];
    if (c == 0x2018 || c == 0x2019 || c == 0x00B4 || c == 0x2032) return '\'';
    if (c == 0x201C || c == 0x201D) return '"';
    if (c == 0x2013 || c == 0x2014) return '-';
    if (c == 0x2026) return '.';
    if (c == 0xA0 || c == 0x09) return ' ';
    return '?';
}

static void put_cp(char *out, size_t size, size_t *n, uint32_t c)
{
    if (c < 0x20 && c != 0x09) return;
    if (*n + 1 < size) out[(*n)++] = ascii_of(c);
}

static void finish(char *out, size_t n)
{
    out[n] = 0;
    while (n && out[n - 1] == ' ') out[--n] = 0;
    size_t s = 0;
    while (out[s] == ' ') s++;
    if (s) memmove(out, out + s, strlen(out + s) + 1);
}

static void text_utf8(const uint8_t *p, size_t len, char *out, size_t size)
{
    size_t n = 0;
    for (size_t i = 0; i < len && p[i];) {
        uint32_t c = p[i];
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (extra) {
            c &= 0x3F >> extra;
            for (int k = 1; k <= extra && i + k < len; k++) c = c << 6 | (p[i + k] & 0x3F);
        }
        i += (size_t)extra + 1;
        put_cp(out, size, &n, c);
    }
    finish(out, n);
}

static void text_latin1(const uint8_t *p, size_t len, char *out, size_t size)
{
    size_t n = 0;
    for (size_t i = 0; i < len && p[i]; i++) put_cp(out, size, &n, p[i]);
    finish(out, n);
}

static void text_utf16(const uint8_t *p, size_t len, int big, char *out, size_t size)
{
    size_t n = 0, i = 0;
    if (len >= 2 && ((p[0] == 0xFF && p[1] == 0xFE) || (p[0] == 0xFE && p[1] == 0xFF))) { big = p[0] == 0xFE; i = 2; }
    for (; i + 1 < len; i += 2) {
        uint32_t c = big ? (uint32_t)p[i] << 8 | p[i + 1] : (uint32_t)p[i + 1] << 8 | p[i];
        if (!c) break;
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < len) { i += 2; c = '?'; }
        put_cp(out, size, &n, c);
    }
    finish(out, n);
}

/* ---- tags ------------------------------------------------------------------------------------- */

static void id3_text(const uint8_t *p, size_t len, char *out, size_t size)
{
    if (!len) return;
    switch (p[0]) {
    case 0: text_latin1(p + 1, len - 1, out, size); break;
    case 1: text_utf16(p + 1, len - 1, 0, out, size); break;
    case 2: text_utf16(p + 1, len - 1, 1, out, size); break;
    default: text_utf8(p + 1, len - 1, out, size); break;
    }
}

static void read_id3(const uint8_t *d, size_t n, SongTags *t)
{
    if (n >= 10 && !memcmp(d, "ID3", 3) && d[3] >= 2 && d[3] <= 4) {
        int v = d[3];
        size_t size = ((size_t)(d[6] & 0x7F) << 21) | ((size_t)(d[7] & 0x7F) << 14) | ((size_t)(d[8] & 0x7F) << 7) | (d[9] & 0x7F);
        size_t p = 10, end = 10 + size < n ? 10 + size : n;
        if (v >= 3 && (d[5] & 0x40) && p + 4 <= end)                  /* extended header */
            p += v == 4 ? (((size_t)(d[p] & 0x7F) << 21) | ((size_t)(d[p + 1] & 0x7F) << 14) | ((size_t)(d[p + 2] & 0x7F) << 7) | (d[p + 3] & 0x7F))
                        : 4 + rd32be(d + p);
        while (p + (v == 2 ? 6 : 10) <= end) {
            char id[5] = {0};
            size_t fs, hdr = v == 2 ? 6 : 10;
            if (v == 2) { memcpy(id, d + p, 3); fs = (size_t)d[p + 3] << 16 | (size_t)d[p + 4] << 8 | d[p + 5]; }
            else {
                memcpy(id, d + p, 4);
                fs = v == 4 ? (((size_t)(d[p + 4] & 0x7F) << 21) | ((size_t)(d[p + 5] & 0x7F) << 14) | ((size_t)(d[p + 6] & 0x7F) << 7) | (d[p + 7] & 0x7F))
                            : rd32be(d + p + 4);
            }
            if (!id[0] || fs == 0 || p + hdr + fs > end) break;
            const uint8_t *body = d + p + hdr;
            if ((!strcmp(id, "TIT2") || !strcmp(id, "TT2")) && !t->title[0]) id3_text(body, fs, t->title, sizeof t->title);
            if ((!strcmp(id, "TPE1") || !strcmp(id, "TP1")) && !t->artist[0]) id3_text(body, fs, t->artist, sizeof t->artist);
            if ((!strcmp(id, "TALB") || !strcmp(id, "TAL")) && !t->album[0]) id3_text(body, fs, t->album, sizeof t->album);
            p += hdr + fs;
        }
    }
    if (n >= 128 && !memcmp(d + n - 128, "TAG", 3)) {
        const uint8_t *v1 = d + n - 128;
        if (!t->title[0]) text_latin1(v1 + 3, 30, t->title, sizeof t->title);
        if (!t->artist[0]) text_latin1(v1 + 33, 30, t->artist, sizeof t->artist);
        if (!t->album[0]) text_latin1(v1 + 63, 30, t->album, sizeof t->album);
    }
}

static void vorbis_comment(const char *c, size_t len, SongTags *t)
{
    struct { const char *key; char *out; } k[] = { { "TITLE=", t->title }, { "ARTIST=", t->artist }, { "ALBUM=", t->album } };
    for (int i = 0; i < 3; i++) {
        size_t kl = strlen(k[i].key);
        if (len > kl && !k[i].out[0]) {
            int match = 1;
            for (size_t j = 0; j < kl; j++) if (toupper((unsigned char)c[j]) != k[i].key[j]) match = 0;
            if (match) text_utf8((const uint8_t *)c + kl, len - kl, k[i].out, 48);
        }
    }
}

static void flac_meta(void *user, drflac_metadata *m)
{
    if (m->type != DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) return;
    drflac_vorbis_comment_iterator it;
    drflac_uint32 len;
    const char *c;
    drflac_init_vorbis_comment_iterator(&it, m->data.vorbis_comment.commentCount, m->data.vorbis_comment.pComments);
    while ((c = drflac_next_vorbis_comment(&it, &len)) != NULL) vorbis_comment(c, len, (SongTags *)user);
}

static void read_wav_info(const uint8_t *d, size_t n, SongTags *t)
{
    if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return;
    for (size_t p = 12; p + 8 <= n;) {
        uint32_t sz = rd32le(d + p + 4);
        if (!memcmp(d + p, "LIST", 4) && p + 12 <= n && !memcmp(d + p + 8, "INFO", 4)) {
            size_t end = p + 8 + sz < n ? p + 8 + sz : n;
            for (size_t q = p + 12; q + 8 <= end;) {
                uint32_t s2 = rd32le(d + q + 4);
                if (q + 8 + s2 > end) break;
                if (!memcmp(d + q, "INAM", 4) && !t->title[0]) text_utf8(d + q + 8, s2, t->title, sizeof t->title);
                if (!memcmp(d + q, "IART", 4) && !t->artist[0]) text_utf8(d + q + 8, s2, t->artist, sizeof t->artist);
                if (!memcmp(d + q, "IPRD", 4) && !t->album[0]) text_utf8(d + q + 8, s2, t->album, sizeof t->album);
                q += 8 + s2 + (s2 & 1);
            }
        }
        p += 8 + (size_t)sz + (sz & 1);
    }
}

static const char *extension(const char *path)
{
    const char *dot = strrchr(path, '.'), *sep1 = strrchr(path, '/'), *sep2 = strrchr(path, '\\');
    const char *sep = sep1 > sep2 ? sep1 : sep2;
    return dot && (!sep || dot > sep) ? dot + 1 : "";
}

static int ext_is(const char *path, const char *ext)
{
    const char *e = extension(path);
    for (; *e && *ext; e++, ext++) if (tolower((unsigned char)*e) != *ext) return 0;
    return !*e && !*ext;
}

int song_is_music_file(const char *path)
{
    return ext_is(path, "mp3") || ext_is(path, "flac") || ext_is(path, "ogg") || ext_is(path, "wav");
}

/* "Artist - Title" from the file name, the album from the folder. */
static void from_name(const char *path, SongTags *t)
{
    const char *sep1 = strrchr(path, '/'), *sep2 = strrchr(path, '\\');
    const char *base = sep1 > sep2 ? sep1 : sep2;
    char stem[256], folder[256] = "";
    base = base ? base + 1 : path;
    snprintf(stem, sizeof stem, "%s", base);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    if (base > path) {
        size_t len = (size_t)(base - path - 1);
        const char *start = path + len;
        while (start > path && start[-1] != '/' && start[-1] != '\\') start--;
        snprintf(folder, sizeof folder, "%.*s", (int)(path + len - start), start);
    }
    char *dash = strstr(stem, " - ");
    if (!dash) {
        /* "My_Song" or "My-Song-Name": words, not punctuation */
        int spaces = strchr(stem, ' ') != NULL;
        for (char *p = stem; *p; p++)
            if (*p == '_' || (!spaces && *p == '-')) *p = ' ';
    }
    if (dash && !t->artist[0]) {
        *dash = 0;
        text_utf8((const uint8_t *)stem, strlen(stem), t->artist, sizeof t->artist);
        if (!t->title[0]) text_utf8((const uint8_t *)dash + 3, strlen(dash + 3), t->title, sizeof t->title);
    }
    if (!t->title[0]) text_utf8((const uint8_t *)stem, strlen(stem), t->title, sizeof t->title);
    if (!t->artist[0]) snprintf(t->artist, sizeof t->artist, "Unknown Artist");
    if (!t->album[0]) {
        if (folder[0] && strcmp(folder, "songs")) text_utf8((const uint8_t *)folder, strlen(folder), t->album, sizeof t->album);
        else snprintf(t->album, sizeof t->album, "Custom Songs");
    }
}

/* The first 4 MB of the file, and its last 128 bytes after them when the file is longer (tags
 * sit at the start: ID3v2, FLAC metadata, Ogg comments; ID3v1 at the end), so a long list of
 * songs reads quickly. */
static int read_head_tail(const char *path, uint8_t **data, size_t *n)
{
    FILE *f = open_utf8(path, "rb");
    *data = NULL;
    *n = 0;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 0; }
    long len = ftell(f);
    size_t head = len > (4L << 20) ? (4u << 20) : (size_t)(len > 0 ? len : 0);
    size_t tail = (size_t)len > head ? 128 : 0;
    *data = (uint8_t *)malloc(head + tail + 1);
    if (!*data || fseek(f, 0, SEEK_SET) || fread(*data, 1, head, f) != head ||
        (tail && (fseek(f, len - 128, SEEK_SET) || fread(*data + head, 1, tail, f) != tail))) {
        fclose(f);
        free(*data);
        *data = NULL;
        return 0;
    }
    fclose(f);
    *n = head + tail;
    return head > 0;
}

int song_read_tags(const char *path, SongTags *out)
{
    uint8_t *d;
    size_t n;
    memset(out, 0, sizeof *out);
    if (!read_head_tail(path, &d, &n)) return 0;
    if (ext_is(path, "mp3")) read_id3(d, n, out);
    else if (ext_is(path, "flac")) {
        drflac *f = drflac_open_memory_with_metadata(d, n, flac_meta, out, NULL);
        if (f) drflac_close(f);
    } else if (ext_is(path, "ogg")) {
        int e;
        stb_vorbis *v = stb_vorbis_open_memory(d, (int)n, &e, NULL);
        if (v) {
            stb_vorbis_comment c = stb_vorbis_get_comment(v);
            for (int i = 0; i < c.comment_list_length; i++) vorbis_comment(c.comment_list[i], strlen(c.comment_list[i]), out);
            stb_vorbis_close(v);
        }
    } else if (ext_is(path, "wav")) read_wav_info(d, n, out);
    free(d);
    from_name(path, out);
    return 1;
}

/* ---- decoding --------------------------------------------------------------------------------- */

/* Decoded, stereo, 44.1 kHz: frames * 2 int16. */
static int16_t *decode_file(const char *path, size_t *frames, char *err, size_t errn)
{
    uint8_t *d;
    size_t n;
    int16_t *pcm = NULL;
    unsigned ch = 0, rate = 0;
    uint64_t count = 0;
    *frames = 0;
    if (!read_all(path, &d, &n, MAX_FILE)) { fail(err, errn, "cannot read the file"); return NULL; }
    if (ext_is(path, "mp3")) {
        drmp3_config cfg;
        drmp3_uint64 c = 0;
        pcm = drmp3_open_memory_and_read_pcm_frames_s16(d, n, &cfg, &c, NULL);
        ch = cfg.channels; rate = cfg.sampleRate; count = c;
    } else if (ext_is(path, "flac")) {
        drflac_uint64 c = 0;
        pcm = drflac_open_memory_and_read_pcm_frames_s16(d, n, &ch, &rate, &c, NULL);
        count = c;
    } else if (ext_is(path, "wav")) {
        drwav_uint64 c = 0;
        pcm = drwav_open_memory_and_read_pcm_frames_s16(d, n, &ch, &rate, &c, NULL);
        count = c;
    } else if (ext_is(path, "ogg")) {
        int c2 = 0, r2 = 0;
        short *out = NULL;
        int got = stb_vorbis_decode_memory(d, (int)n, &c2, &r2, &out);
        if (got > 0) { pcm = out; ch = (unsigned)c2; rate = (unsigned)r2; count = (uint64_t)got; }
        else free(out);
    }
    free(d);
    if (!pcm || !ch || !rate || !count) { free(pcm); fail(err, errn, "not a music file this game can read"); return NULL; }
    if (count / rate > MAX_SECONDS) count = (uint64_t)MAX_SECONDS * rate;
    if (count < (uint64_t)MIN_SECONDS * rate) { free(pcm); fail(err, errn, "shorter than 5 seconds"); return NULL; }
    /* Stereo at 44.1 kHz (linear interpolation; most files are 44.1 kHz already). */
    size_t out_frames = (size_t)(count * RATE / rate);
    int16_t *st = (int16_t *)malloc(out_frames * 2 * sizeof *st);
    if (!st) { free(pcm); fail(err, errn, "out of memory"); return NULL; }
    for (size_t i = 0; i < out_frames; i++) {
        double pos = (double)i * rate / RATE;
        size_t a = (size_t)pos;
        double f = pos - (double)a;
        size_t b = a + 1 < count ? a + 1 : a;
        for (int c = 0; c < 2; c++) {
            unsigned src = ch == 1 ? 0 : (unsigned)c;
            double v = pcm[a * ch + src] * (1.0 - f) + pcm[b * ch + src] * f;
            st[i * 2 + c] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
        }
    }
    free(pcm);
    *frames = out_frames;
    return st;
}

/* ---- EA-XA ------------------------------------------------------------------------------------ */

static const int32_t k_c1[4] = { 0, 240, 460, 392 }, k_c2[4] = { 0, 0, -208, -220 };

void song_eaxa_frame(const int16_t *x, int n, int32_t *h1, int32_t *h2, uint8_t out[15])
{
    int64_t best = -1;
    int bf = 0, bs = 0;
    uint8_t bn[28] = {0};
    int32_t bh1 = *h1, bh2 = *h2;
    for (int f = 0; f < 4; f++)
        for (int sh = 0; sh <= 12; sh++) {
            int32_t a = *h1, b = *h2;
            int64_t err = 0, q = (int64_t)1 << (20 - sh);
            uint8_t nib[28];
            int i;
            for (i = 0; i < 28; i++) {
                int32_t s = i < n ? x[i] : 0;
                int64_t pred = (int64_t)k_c1[f] * a + (int64_t)k_c2[f] * b;
                int64_t nn = (int64_t)floor(((double)s * 256.0 - (double)pred) / (double)q + 0.5);
                if (nn < -8) nn = -8;
                if (nn > 7) nn = 7;
                int64_t v = (nn * q + pred) >> 8;
                if (v < -32768) v = -32768;
                if (v > 32767) v = 32767;
                err += (v - s) * (v - s);
                if (best >= 0 && err >= best) break;
                nib[i] = (uint8_t)(nn & 15);
                b = a;
                a = (int32_t)v;
            }
            if (i == 28 && (best < 0 || err < best)) {
                best = err; bf = f; bs = sh; memcpy(bn, nib, 28); bh1 = a; bh2 = b;
            }
        }
    out[0] = (uint8_t)(bf << 4 | bs);
    for (int i = 0; i < 14; i++) out[1 + i] = (uint8_t)(bn[2 * i] << 4 | bn[2 * i + 1]);
    *h1 = bh1;
    *h2 = bh2;
}

typedef struct { uint8_t *p; size_t n, cap; } Buf;

static int buf_add(Buf *b, const void *data, size_t n)
{
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 1 << 20;
        while (cap < b->n + n) cap *= 2;
        uint8_t *p = (uint8_t *)realloc(b->p, cap);
        if (!p) return 0;
        b->p = p;
        b->cap = cap;
    }
    if (data) memcpy(b->p + b->n, data, n);
    else memset(b->p + b->n, 0, n);
    b->n += n;
    return 1;
}

/* One SCHl stream of frames stereo samples at pcm: the template's header with the sample count
 * and rate set, SCCl, SCDl blocks (split channels, each with its two history words), SCEl. */
static int write_stream(Buf *out, const SongTemplate *t, const int16_t *pcm, size_t frames)
{
    uint8_t hdr[128];
    memcpy(hdr, t->header, t->header_n);
    for (size_t i = 12; i < t->header_n;) {
        uint8_t tag = hdr[i++];
        if (tag == 0xFF) break;
        if (tag == 0xFC || tag == 0xFD || tag == 0xFE) continue;
        uint8_t len = hdr[i++];
        uint32_t v = tag == 0x85 ? (uint32_t)frames : tag == 0x84 ? RATE : 0;
        if ((tag == 0x85 || tag == 0x84) && len >= 1 && len <= 4)
            for (int k = 0; k < len; k++) hdr[i + k] = (uint8_t)(v >> (8 * (len - 1 - k)));
        i += len;
    }
    uint32_t blocks = (uint32_t)((frames + BLOCK - 1) / BLOCK);
    uint8_t cc[12] = { 'S', 'C', 'C', 'l' };
    wr32le(cc + 4, 12);
    wr32le(cc + 8, blocks);
    if (!buf_add(out, hdr, t->header_n) || !buf_add(out, cc, 12)) return 0;
    int32_t h1[2] = {0, 0}, h2[2] = {0, 0};
    int16_t chan[BLOCK];
    uint8_t frame[15];
    for (size_t s = 0; s < frames; s += BLOCK) {
        uint32_t n = (uint32_t)(frames - s < BLOCK ? frames - s : BLOCK);
        uint32_t fr = (n + 27) / 28, chsize = 4 + fr * 15;
        uint8_t head[20] = { 'S', 'C', 'D', 'l' };
        wr32le(head + 4, 20 + 2 * chsize);
        wr32le(head + 8, n);
        wr32le(head + 12, 0);
        wr32le(head + 16, chsize);
        if (!buf_add(out, head, 20)) return 0;
        for (int c = 0; c < 2; c++) {
            uint8_t hist[4] = { (uint8_t)h1[c], (uint8_t)(h1[c] >> 8), (uint8_t)h2[c], (uint8_t)(h2[c] >> 8) };
            for (uint32_t i = 0; i < n; i++) chan[i] = pcm[(s + i) * 2 + c];
            if (!buf_add(out, hist, 4)) return 0;
            for (uint32_t f = 0; f < fr; f++) {
                int left = (int)(n - f * 28);
                song_eaxa_frame(chan + f * 28, left < 28 ? left : 28, &h1[c], &h2[c], frame);
                if (!buf_add(out, frame, 15)) return 0;
            }
        }
    }
    uint8_t end[8] = { 'S', 'C', 'E', 'l', 8, 0, 0, 0 };
    return buf_add(out, end, 8);
}

/* ---- the template's map ------------------------------------------------------------------------ */

typedef struct {
    uint32_t nodes, samples_table, track0;       /* track 0: samples 0 .. track0-1 */
    uint32_t full[256], nfull;                   /* 1-based sample numbers of the longest play */
} MapInfo;

static int map_info(const uint8_t *d, size_t n, MapInfo *m)
{
    memset(m, 0, sizeof *m);
    if (n < 0x40 || memcmp(d, "xDFP", 4) || d[4] != 4) return 0;
    uint32_t tracks = d[0x0D], events = d[0x0F], routers = d[0x10];
    m->nodes = rd16le(d + 0x12);
    if (!m->nodes || !events || !tracks || 0x20 + m->nodes * 2 > n) return 0;
#define CHECK(o, len) do { if ((size_t)(o) + (len) > n) return 0; } while (0)
    uint32_t so = 0x20, eo = rd16le(d + so + (m->nodes - 1) * 2) * 4;
    CHECK(eo, 8);
    uint32_t sub = (rd32be(d + eo + 4) >> 20) & 0xF;
    so = eo + 0x10 + sub * 4;
    CHECK(so, events * 2);
    eo = rd16le(d + so + (events - 1) * 2) * 4;
    CHECK(eo, 0x10);
    sub = (rd32be(d + eo + 0x0C) >> 8) & 0x3F;
    so = eo + 0x10 + sub * 0x10 + routers * 4;
    CHECK(so, 4);
    uint32_t tracks_table = rd32le(d + so) * 4;
    CHECK(tracks_table, (tracks + 1) * 4);
    m->samples_table = tracks_table + (tracks + 1) * 4;
    uint32_t t1 = rd32le(d + tracks_table + 4) * 4;
    if (t1 < m->samples_table) return 0;
    m->track0 = (t1 - m->samples_table) / 8;
    CHECK(m->samples_table, m->track0 * 8);
    /* The longest walk from a section's start node (sample 0), first route at each node. */
    for (uint32_t start = 0; start < m->nodes; start++) {
        uint32_t o = rd16le(d + 0x20 + start * 2) * 4;
        CHECK(o, 0x14);
        if (rd16le(d + o) != 0) continue;
        uint32_t walk[256], nw = 0, node = start;
        uint8_t seen[2048] = {0};
        while (node < m->nodes && node < 2048 && !seen[node] && nw < 256) {
            seen[node] = 1;
            uint32_t oo = rd16le(d + 0x20 + node * 2) * 4;
            CHECK(oo, 0x14);
            uint32_t seg = rd16le(d + oo);
            if (seg && seg != 0xFFFF && seg != 0xFFFD && seg <= m->track0) walk[nw++] = seg;
            if (!((rd32be(d + oo + 4) >> 20) & 0xF)) break;
            node = rd16le(d + oo + 0x12);
        }
        if (nw > m->nfull) { memcpy(m->full, walk, nw * sizeof walk[0]); m->nfull = nw; }
    }
#undef CHECK
    return m->nfull > 0;
}

/* ---- interactive: lanes ----------------------------------------------------------------------- */
/*
 * The licensed songs' maps (holler's is the template) branch four ways at every segment of their
 * fight sections: the route is picked by a value of the game's (0-0x7F, four ranges) and lands on
 * the same place of the song in one of four lanes (the song; a loop; two other mixes), so the
 * music changes its arrangement with the fight without losing its place. A custom song in this
 * mode fills lane 0 with the song and lanes 1-3 with the same pieces filtered three ways.
 *
 * Mapping: every section's lane-0 chain (first route at each node), longest first; a sample gets
 * the place it first has in a chain (the fight sections enter the same body cycle at different
 * points, so later chains agree), and each chain spreads the whole song over its samples. The
 * other routes of a branching node inherit their first route's place, as lanes 1-3.
 */
typedef struct {
    int chain[256], slot[256], lane[256];          /* per track-0 sample (0-based) */
    int nchains, count[64];                        /* samples given to each chain */
} LaneMap;

static int lane_map(const uint8_t *d, size_t n, const MapInfo *m, LaneMap *lm)
{
    enum { MAXN = 2048 };
    static uint32_t seg[MAXN], routes[MAXN][8];
    static int nroutes[MAXN], walk[MAXN];
    int starts[64], lens[64], nstarts = 0;
    uint32_t N = m->nodes < MAXN ? m->nodes : MAXN;
    memset(lm, 0, sizeof *lm);
    for (int s = 0; s < 256; s++) lm->chain[s] = -1;
    for (uint32_t i = 0; i < N; i++) {
        uint32_t o = rd16le(d + 0x20 + i * 2) * 4;
        if ((size_t)o + 0x14 > n) return 0;
        seg[i] = rd16le(d + o);
        nroutes[i] = (int)((rd32be(d + o + 4) >> 20) & 0xF);
        if (nroutes[i] > 8) nroutes[i] = 8;
        if ((size_t)o + 0x10 + 4 * (size_t)nroutes[i] > n) return 0;
        for (int k = 0; k < nroutes[i]; k++) routes[i][k] = rd16le(d + o + 0x10 + 4 * k + 2);
    }
    /* chains, longest first */
    for (uint32_t s = 0; s < N && nstarts < 64; s++) {
        if (seg[s] != 0) continue;
        static uint8_t seen[MAXN];
        int len = 0;
        memset(seen, 0, N);
        for (uint32_t x = s; x < N && !seen[x];) {
            seen[x] = 1;
            if (seg[x] && seg[x] <= m->track0) len++;
            if (!nroutes[x]) break;
            x = routes[x][0];
        }
        if (len) { starts[nstarts] = (int)s; lens[nstarts++] = len; }
    }
    for (int i = 1; i < nstarts; i++)
        for (int j = i; j > 0 && lens[j - 1] < lens[j]; j--) {
            int a = starts[j]; starts[j] = starts[j - 1]; starts[j - 1] = a;
            a = lens[j]; lens[j] = lens[j - 1]; lens[j - 1] = a;
        }
    for (int c = 0; c < nstarts; c++) {
        static uint8_t seen[MAXN];
        int nw = 0;
        memset(seen, 0, N);
        for (uint32_t x = (uint32_t)starts[c]; x < N && !seen[x];) {
            seen[x] = 1;
            walk[nw++] = (int)x;
            if (!nroutes[x]) break;
            x = routes[x][0];
        }
        for (int k = 0; k < nw; k++) {
            uint32_t s = seg[walk[k]];
            if (!s || s > m->track0 || s > 256 || lm->chain[s - 1] >= 0) continue;
            lm->chain[s - 1] = lm->nchains;
            lm->slot[s - 1] = lm->count[lm->nchains]++;
            lm->lane[s - 1] = 0;
        }
        if (lm->count[lm->nchains]) lm->nchains++;
    }
    /* lanes: the other routes of a branching node take their first route's place */
    for (int changed = 1; changed;) {
        changed = 0;
        for (uint32_t p = 0; p < N; p++) {
            if (nroutes[p] < 2 || routes[p][0] >= N) continue;
            uint32_t s0 = seg[routes[p][0]];
            if (!s0 || s0 > m->track0 || s0 > 256 || lm->chain[s0 - 1] < 0) continue;
            for (int k = 1; k < nroutes[p] && k < 4; k++) {
                uint32_t t = routes[p][k];
                if (t >= N) continue;
                uint32_t s = seg[t];
                if (!s || s > m->track0 || s > 256 || lm->chain[s - 1] >= 0) continue;
                lm->chain[s - 1] = lm->chain[s0 - 1];
                lm->slot[s - 1] = lm->slot[s0 - 1];
                lm->lane[s - 1] = k;
                changed = 1;
            }
        }
    }
    /* anything left: the longest chain's pieces in turn, as lane 0 */
    if (!lm->nchains) return 0;
    for (uint32_t s = 0, next = 0; s < m->track0 && s < 256; s++)
        if (lm->chain[s] < 0) { lm->chain[s] = 0; lm->slot[s] = (int)(next++ % (uint32_t)lm->count[0]); lm->lane[s] = 0; }
    return 1;
}

/* Lanes 1-3: the song filtered three ways (RBJ biquads), the same length and place. */
typedef struct { double b0, b1, b2, a1, a2, z1[2], z2[2]; } Biquad;

static void biquad_set(Biquad *q, int type, double freq, double gain_db)
{
    double w = 2 * 3.14159265358979 * freq / RATE, cw = cos(w), sw = sin(w), A = pow(10, gain_db / 40), Q = 0.707;
    double alpha = sw / (2 * Q), b0, b1, b2, a0, a1, a2;
    if (type == 0) {                                          /* low-pass */
        b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    } else {                                                  /* shelves: 1 low, 2 high */
        double s = type == 1 ? 1 : -1, sq = 2 * sqrt(A) * alpha;
        b0 = A * ((A + 1) - s * (A - 1) * cw + sq);
        b1 = s * 2 * A * ((A - 1) - s * (A + 1) * cw);
        b2 = A * ((A + 1) - s * (A - 1) * cw - sq);
        a0 = (A + 1) + s * (A - 1) * cw + sq;
        a1 = -s * 2 * ((A - 1) + s * (A + 1) * cw);
        a2 = (A + 1) + s * (A - 1) * cw - sq;
    }
    memset(q, 0, sizeof *q);
    q->b0 = b0 / a0; q->b1 = b1 / a0; q->b2 = b2 / a0; q->a1 = a1 / a0; q->a2 = a2 / a0;
}

static double biquad_run(Biquad *q, int c, double x)
{
    double y = q->b0 * x + q->z1[c];
    q->z1[c] = q->b1 * x - q->a1 * y + q->z2[c];
    q->z2[c] = q->b2 * x - q->a2 * y;
    return y;
}

static int16_t *lane_pcm(const int16_t *pcm, size_t frames, int lane)
{
    int16_t *out = (int16_t *)malloc(frames * 2 * sizeof *out);
    Biquad a, b;
    double gain = 1.0;
    if (!out) return NULL;
    if (lane == 1) { biquad_set(&a, 0, 1200, 0); biquad_set(&b, 0, 1200, 0); gain = 1.1; }   /* muffled */
    else if (lane == 2) { biquad_set(&a, 1, 150, 6); biquad_set(&b, 0, 9000, 0); gain = 0.9; } /* heavy */
    else { biquad_set(&a, 2, 3500, 4); biquad_set(&b, 1, 90, 2); gain = 0.95; }              /* bright */
    for (size_t i = 0; i < frames; i++)
        for (int c = 0; c < 2; c++) {
            double v = biquad_run(&b, c, biquad_run(&a, c, pcm[i * 2 + c])) * gain;
            double m = fabs(v) / 32768.0;                               /* unity gain, a soft knee above 0.8 */
            if (m > 0.8) m = 0.8 + 0.2 * tanh((m - 0.8) / 0.2);
            v = (v < 0 ? -m : m) * 32767.0;
            out[i * 2 + c] = (int16_t)v;
        }
    return out;
}

/*
 * The song cut into the menu play's K pieces (m->nfull, on 28-sample frame edges), written once
 * per lane in the song's order: lane 0 the song, lanes 1-3 (interactive) filtered. Every track-0
 * sample of the map then names one of them: straight, the menu play's samples hold pieces 0..K-1
 * in turn and the others take pieces in turn; interactive, a sample's lane and place come from
 * lane_map (its chain's place, scaled to K). out_dir/pieces.txt lists the pieces for the play-time
 * remap (song_plan).
 */
static int convert_pieces(const int16_t *pcm, size_t frames, const SongTemplate *t, const MapInfo *m, int interactive,
                          uint8_t *mpf, Buf *mus, const char *out_dir, char *err, size_t errn)
{
    static uint32_t off[4][256], ms[4][256];
    static LaneMap lm;
    size_t edges[257];
    uint32_t K = m->nfull < 256 ? m->nfull : 256, lanes = interactive ? 4 : 1;
    if (interactive && !lane_map(t->mpf, t->mpf_n, m, &lm)) return fail(err, errn, "the interactive template's map is not one this version reads");
    /* Every piece the same length, as the game's own songs' segments are (the last one padded
     * with silence): P frames, a whole number of 28-sample frames. */
    size_t P = (frames + K - 1) / K;
    P += (28 - P % 28) % 28;
    for (uint32_t k = 0; k <= K; k++) edges[k] = (size_t)k * P;
    int16_t *tail = (int16_t *)calloc(P * 2, sizeof *tail);
    if (!tail) return fail(err, errn, "out of memory");
    for (uint32_t lane = 0; lane < lanes; lane++) {
        int16_t *src = lane ? lane_pcm(pcm, frames, (int)lane) : (int16_t *)pcm;
        if (!src) { free(tail); return fail(err, errn, "out of memory"); }
        for (uint32_t k = 0; k < K; k++) {
            size_t at = mus->n;
            const int16_t *piece = src + edges[k] * 2;
            if (edges[k] + P > frames) {                          /* the last piece: padded */
                size_t have = edges[k] < frames ? frames - edges[k] : 0;
                memset(tail, 0, P * 2 * sizeof *tail);
                if (have) memcpy(tail, src + edges[k] * 2, have * 2 * sizeof *tail);
                piece = tail;
            }
            if (!write_stream(mus, t, piece, P) ||
                !buf_add(mus, NULL, (STREAM_ALIGN - mus->n % STREAM_ALIGN) % STREAM_ALIGN)) {
                if (lane) free(src);
                free(tail);
                return fail(err, errn, "out of memory");
            }
            off[lane][k] = (uint32_t)(at / 0x80);
            ms[lane][k] = (uint32_t)(P * 1000 / RATE);
        }
        if (lane) free(src);
    }
    free(tail);
    /*
     * The sample table: sample i names piece i in file order (the last piece for the samples past
     * the pieces), so consecutive entries step through main.mus exactly as the game's own songs'
     * do. The game sizes its music read buffer once from this table: 3000 ms times the largest
     * (next entry's offset - this one's) / this one's length (sub_001317C0); an entry order that
     * jumps around the file made that tens of megabytes and the reads overran the game's memory.
     * Which piece plays when is the play-time remap's (song_cursor.c), and a sample's lane (which
     * filter the game's route chose) is listed in pieces.txt.
     */
    uint32_t total = K * lanes, lane_of[256];
    const size_t F = RATE - RATE % 28;                            /* a second of silence */
    int16_t *quiet = NULL;
    for (uint32_t i = 0; i < m->track0 && i < 256; i++) {
        lane_of[i] = interactive ? (uint32_t)lm.lane[i] & 3 : 0;
        if (i < total) {
            wr32le(mpf + m->samples_table + i * 8, off[i / K][i % K]);
            wr32le(mpf + m->samples_table + i * 8 + 4, ms[i / K][i % K]);
            continue;
        }
        /* Samples past the pieces (minitrack2 has 64 for 48 pieces) each get a stream of their
         * own after them, so every entry steps forward: the game played nothing at all for a
         * table whose last entries named one piece. The remap gives them pieces when they play. */
        size_t at = mus->n;
        if (!quiet && !(quiet = (int16_t *)calloc(F * 2, sizeof *quiet))) return fail(err, errn, "out of memory");
        if (!write_stream(mus, t, quiet, F) || !buf_add(mus, NULL, (STREAM_ALIGN - mus->n % STREAM_ALIGN) % STREAM_ALIGN)) {
            free(quiet);
            return fail(err, errn, "out of memory");
        }
        wr32le(mpf + m->samples_table + i * 8, (uint32_t)(at / 0x80));
        wr32le(mpf + m->samples_table + i * 8 + 4, (uint32_t)(F * 1000 / RATE));
    }
    free(quiet);
    char file[1100];
    snprintf(file, sizeof file, "%s" HOST_SEP "pieces.txt", out_dir);
    FILE *f = open_utf8(file, "w");
    if (!f) return fail(err, errn, "cannot write the converted song");
    fprintf(f, "pieces %u lanes %u\n", K, lanes);
    for (uint32_t lane = 0; lane < lanes; lane++)
        for (uint32_t k = 0; k < K; k++) fprintf(f, "%u %u %u %u\n", lane, k, off[lane][k], ms[lane][k]);
    for (uint32_t i = 0; i < m->track0 && i < 256; i++) fprintf(f, "lane %u %u\n", i, lane_of[i]);
    int ok = !ferror(f);
    if (fclose(f) || !ok) return fail(err, errn, "cannot write the converted song");
    return 1;
}

/* The play-time order of a converted song (song_convert.h). */
int song_plan(const uint8_t *mpf, size_t n, const char *pieces_txt, SongPlan *p)
{
    MapInfo m;
    unsigned K = 0, lanes = 0;
    int used = 0;
    memset(p, 0, sizeof *p);
    if (!map_info(mpf, n, &m) || sscanf(pieces_txt, "pieces %u lanes %u%n", &K, &lanes, &used) != 2) return 0;
    if (!K || K > 256 || !lanes || lanes > 4) return 0;
    p->table = m.samples_table;
    p->track0 = (int)(m.track0 < 256 ? m.track0 : 256);
    p->pieces = (int)K;
    p->lanes = (int)lanes;
    const char *s = pieces_txt + used;
    unsigned lane, k, o, d;
    int got = 0;
    for (int adv = 0; sscanf(s, " %u %u %u %u%n", &lane, &k, &o, &d, &adv) == 4; s += adv) {
        if (lane < lanes && k < K) { p->off[lane][k] = o; p->ms[lane][k] = d; got++; }
    }
    if (got != (int)(K * lanes) || p->table + (size_t)p->track0 * 8 > n) return 0;
    unsigned i2, l2;
    for (int adv = 0; sscanf(s, " lane %u %u%n", &i2, &l2, &adv) == 2; s += adv)
        if ((int)i2 < p->track0 && l2 < lanes) p->lane[i2] = (uint8_t)l2;
    return 1;
}

/* ---- the template from the dump ----------------------------------------------------------------- */

static int big_find(const char *viv, const char *name, uint8_t **data, size_t *n, size_t limit)
{
    FILE *f = open_utf8(viv, "rb");
    uint8_t h[16];
    int ok = 0;
    *data = NULL;
    *n = 0;
    if (!f) return 0;
    if (fread(h, 1, 16, f) == 16 && (!memcmp(h, "BIGF", 4) || !memcmp(h, "BIG4", 4))) {
        uint32_t count = rd32be(h + 8), hlen = rd32be(h + 12);
        if (hlen > 16 && hlen < (16u << 20)) {
            uint8_t *dir = (uint8_t *)malloc(hlen);
            if (dir && fread(dir, 1, hlen - 16, f) == hlen - 16) {
                size_t p = 0;
                for (uint32_t i = 0; i < count && p + 9 <= hlen - 16; i++) {
                    uint32_t off = rd32be(dir + p), len = rd32be(dir + p + 4);
                    const char *nm = (const char *)dir + p + 8;
                    size_t nl = strnlen(nm, hlen - 16 - p - 8);
                    if (!_stricmp(nm, name) && len && len <= limit) {
                        *data = (uint8_t *)malloc(len);
                        if (*data && !fseek(f, (long)off, SEEK_SET) && fread(*data, 1, len, f) == len) { *n = len; ok = 1; }
                        else { free(*data); *data = NULL; }
                        break;
                    }
                    p += 8 + nl + 1;
                }
            }
            free(dir);
        }
    }
    fclose(f);
    return ok;
}

static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

void song_template_free(SongTemplate *t)
{
    free(t->mpf); free(t->loading); free(t->overlay);
    memset(t, 0, sizeof *t);
}

int song_template_load(const char *game_dir, int interactive, SongTemplate *t, char *err, size_t errn)
{
    /* straight: minitrack2 (a plain loop); interactive: holler (four lanes in its fight sections) */
    const char *song = interactive ? "holler" : "minitrack2";
    char names[4][64];
    snprintf(names[0], sizeof names[0], "music\\%s\\tune.mpf", song);
    snprintf(names[1], sizeof names[1], "music\\%s\\loading0.mus", song);
    snprintf(names[2], sizeof names[2], "music\\%s\\overlay0.mus", song);
    snprintf(names[3], sizeof names[3], "music\\%s\\main.mus", song);
    uint8_t *got[4] = {0};
    size_t len[4] = {0};
    memset(t, 0, sizeof *t);
    for (int v = 0; v < 5; v++) {
        char viv[1024];
        snprintf(viv, sizeof viv, "%s" HOST_SEP "audio" HOST_SEP "music%d.viv", game_dir, v);
        for (int k = 0; k < 4; k++)
            if (!got[k]) big_find(viv, names[k], &got[k], &len[k], k == 3 ? (64u << 20) : (4u << 20));
    }
    if (!got[0] || !got[1] || !got[2] || !got[3] || len[3] < 64 || memcmp(got[3], "SCHl", 4) ||
        rd32le(got[3] + 4) > sizeof t->header || rd32le(got[3] + 4) < 16) {
        for (int k = 0; k < 4; k++) free(got[k]);
        return fail(err, errn, interactive ? "the interactive template song (holler) is not in the dump"
                                           : "the template song (minitrack2) is not in the dump");
    }
    t->mpf = got[0]; t->mpf_n = len[0];
    t->loading = got[1]; t->loading_n = len[1];
    t->overlay = got[2]; t->overlay_n = len[2];
    t->header_n = rd32le(got[3] + 4);
    memcpy(t->header, got[3], t->header_n);
    free(got[3]);
    MapInfo m;
    if (!map_info(t->mpf, t->mpf_n, &m)) { song_template_free(t); return fail(err, errn, "the template's map is not one this version reads"); }
    t->interactive = interactive;
    t->hash = fnv(fnv(fnv(fnv(2166136261u, t->mpf, t->mpf_n), t->loading, t->loading_n), t->overlay, t->overlay_n), t->header, t->header_n);
    return 1;
}

/* ---- a song ------------------------------------------------------------------------------------ */

int song_convert(const char *path, const char *out_dir, const SongTemplate *t, int interactive, char *err, size_t errn)
{
    size_t frames;
    MapInfo m;
    char file[1100];
    int ok = 0;
    if (!map_info(t->mpf, t->mpf_n, &m)) return fail(err, errn, "the template's map is not one this version reads");
    int16_t *pcm = decode_file(path, &frames, err, errn);
    if (!pcm) return 0;
    uint8_t *mpf = (uint8_t *)malloc(t->mpf_n);
    Buf mus = {0};
    if (!mpf) { free(pcm); return fail(err, errn, "out of memory"); }
    memcpy(mpf, t->mpf, t->mpf_n);
    if (interactive != t->interactive) { fail(err, errn, "the template does not match the song's mode"); goto done; }
    if (!convert_pieces(pcm, frames, t, &m, interactive, mpf, &mus, out_dir, err, errn)) goto done;
    snprintf(file, sizeof file, "%s" HOST_SEP "main.mus", out_dir);
    if (!write_all(file, mus.p, mus.n)) { fail(err, errn, "cannot write the converted song"); goto done; }
    snprintf(file, sizeof file, "%s" HOST_SEP "loading0.mus", out_dir);
    if (!write_all(file, t->loading, t->loading_n)) { fail(err, errn, "cannot write the converted song"); goto done; }
    snprintf(file, sizeof file, "%s" HOST_SEP "overlay0.mus", out_dir);
    if (!write_all(file, t->overlay, t->overlay_n)) { fail(err, errn, "cannot write the converted song"); goto done; }
    snprintf(file, sizeof file, "%s" HOST_SEP "tune.mpf", out_dir);          /* last: it marks a whole song */
    if (!write_all(file, mpf, t->mpf_n)) { fail(err, errn, "cannot write the converted song"); goto done; }
    ok = 1;
done:
    free(pcm);
    free(mpf);
    free(mus.p);
    return ok;
}

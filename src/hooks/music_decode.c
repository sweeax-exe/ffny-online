/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Decoding the player's own songs: see music_decode.h.
 *
 * Vendored decoders (thirdparty/audio/, unmodified copies; licences in thirdparty/):
 *   minimp3.h, minimp3_ex.h  lieff/minimp3 ea99364f61c14656440e8d77e9c233ccf3124633   CC0 1.0
 *   stb_vorbis.c             nothings/stb  d14d46f6b44456eb88beac58cba833a4135d4c89   MIT or public domain
 *   dr_flac.h v0.13.4,
 *   dr_wav.h  v0.14.6        mackron/dr_libs 35b5c392cc15456736c440a68dbe073178bbffe0 public domain or MIT-0
 * All four read through our own FILE * (UTF-8 names via _wfopen on Windows). */
#define _CRT_SECURE_NO_WARNINGS
#include "music_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(_MSC_VER)
#pragma warning(push, 0)
#pragma warning(disable : 4244 4245 4456 4457 4701 4703 4996)
#endif
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "../../thirdparty/audio/dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "../../thirdparty/audio/dr_flac.h"
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#include "../../thirdparty/audio/minimp3_ex.h"
#define STB_VORBIS_NO_PUSHDATA_API
#define get_bits stbv_get_bits          /* a static of the same name in minimp3 */
#include "../../thirdparty/audio/stb_vorbis.c"
#undef get_bits
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

struct MusicDecoder {
    int format, rate, channels;
    double seconds;
    FILE *file;
    mp3dec_io_t mp3_io;
    mp3dec_ex_t *mp3;
    stb_vorbis *ogg;
    drflac *flac;
    drwav wav;
    int wav_open;
    int16_t *scratch;           /* source-channel frames before the fold to stereo */
    long scratch_frames;
    int broken;
};

static void say(char *why, size_t size, const char *text)
{
    if (why && size) {
        strncpy(why, text, size - 1);
        why[size - 1] = 0;
    }
}

int music_decode_known_extension(const char *name)
{
    const char *dot = name ? strrchr(name, '.') : NULL;
    char ext[8];
    size_t i;
    if (!dot || strlen(dot) >= sizeof(ext)) return 0;
    for (i = 0; dot[i]; i++) ext[i] = (char)tolower((unsigned char)dot[i]);
    ext[i] = 0;
    return !strcmp(ext, ".mp3") || !strcmp(ext, ".ogg") || !strcmp(ext, ".oga") || !strcmp(ext, ".flac") ||
           !strcmp(ext, ".wav");
}

static FILE *open_utf8(const char *path)
{
#if defined(_WIN32)
    wchar_t wide[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, (int)(sizeof(wide) / sizeof(wide[0])))) return NULL;
    return _wfopen(wide, L"rb");
#else
    return fopen(path, "rb");
#endif
}

static int file_seek(FILE *f, long long offset, int origin)
{
#if defined(_WIN32)
    return _fseeki64(f, offset, origin);
#else
    return fseeko(f, (off_t)offset, origin);
#endif
}

static long long file_tell(FILE *f)
{
#if defined(_WIN32)
    return _ftelli64(f);
#else
    return (long long)ftello(f);
#endif
}

/* ---- I/O callbacks over the FILE * ---- */

static size_t mp3_read(void *buf, size_t size, void *user) { return fread(buf, 1, size, (FILE *)user); }
static int mp3_seek(uint64_t position, void *user) { return file_seek((FILE *)user, (long long)position, SEEK_SET); }

static size_t dr_read(void *user, void *out, size_t bytes) { return fread(out, 1, bytes, (FILE *)user); }
static drwav_bool32 wav_seek(void *user, int offset, drwav_seek_origin origin)
{
    int o = origin == DRWAV_SEEK_SET ? SEEK_SET : origin == DRWAV_SEEK_END ? SEEK_END : SEEK_CUR;
    return file_seek((FILE *)user, offset, o) == 0;
}
static drwav_bool32 wav_tell(void *user, drwav_int64 *cursor)
{
    long long at = file_tell((FILE *)user);
    if (at < 0) return 0;
    *cursor = at;
    return 1;
}
static drflac_bool32 flac_seek(void *user, int offset, drflac_seek_origin origin)
{
    int o = origin == DRFLAC_SEEK_SET ? SEEK_SET : origin == DRFLAC_SEEK_END ? SEEK_END : SEEK_CUR;
    return file_seek((FILE *)user, offset, o) == 0;
}
static drflac_bool32 flac_tell(void *user, drflac_int64 *cursor)
{
    long long at = file_tell((FILE *)user);
    if (at < 0) return 0;
    *cursor = at;
    return 1;
}

static int sniff(FILE *f)
{
    unsigned char h[12] = {0};
    size_t n = fread(h, 1, sizeof(h), f);
    file_seek(f, 0, SEEK_SET);
    if (n >= 4 && !memcmp(h, "OggS", 4)) return MUSIC_FORMAT_OGG;
    if (n >= 4 && !memcmp(h, "fLaC", 4)) return MUSIC_FORMAT_FLAC;
    if (n >= 12 && (!memcmp(h, "RIFF", 4) || !memcmp(h, "RF64", 4)) && !memcmp(h + 8, "WAVE", 4)) return MUSIC_FORMAT_WAV;
    if (n >= 3 && !memcmp(h, "ID3", 3)) return MUSIC_FORMAT_MP3;
    if (n >= 2 && h[0] == 0xFF && (h[1] & 0xE0) == 0xE0) return MUSIC_FORMAT_MP3;
    return MUSIC_FORMAT_NONE;
}

static void close_inner(MusicDecoder *d)
{
    if (d->mp3) { mp3dec_ex_close(d->mp3); free(d->mp3); d->mp3 = NULL; }
    if (d->ogg) { stb_vorbis_close(d->ogg); d->ogg = NULL; }
    if (d->flac) { drflac_close(d->flac); d->flac = NULL; }
    if (d->wav_open) { drwav_uninit(&d->wav); d->wav_open = 0; }
}

static int open_inner(MusicDecoder *d, char *why, size_t why_size)
{
    int err = 0;
    switch (d->format) {
    case MUSIC_FORMAT_MP3:
        d->mp3 = (mp3dec_ex_t *)calloc(1, sizeof(*d->mp3));
        if (!d->mp3) { say(why, why_size, "out of memory"); return 0; }
        d->mp3_io.read = mp3_read;
        d->mp3_io.read_data = d->file;
        d->mp3_io.seek = mp3_seek;
        d->mp3_io.seek_data = d->file;
        if (mp3dec_ex_open_cb(d->mp3, &d->mp3_io, MP3D_SEEK_TO_SAMPLE) || !d->mp3->samples || !d->mp3->info.hz ||
            d->mp3->info.channels < 1) {
            say(why, why_size, "not a playable MP3 (no audio frames found)");
            return 0;
        }
        d->rate = d->mp3->info.hz;
        d->channels = d->mp3->info.channels;
        d->seconds = (double)d->mp3->samples / d->channels / d->rate;
        return 1;
    case MUSIC_FORMAT_OGG: {
        stb_vorbis_info info;
        d->ogg = stb_vorbis_open_file(d->file, 0, &err, NULL);
        if (!d->ogg) { say(why, why_size, "not a playable Ogg Vorbis file (Opus and damaged files are not)"); return 0; }
        info = stb_vorbis_get_info(d->ogg);
        d->rate = (int)info.sample_rate;
        d->channels = info.channels;
        {
            unsigned total = stb_vorbis_stream_length_in_samples(d->ogg);
            d->seconds = total && d->rate ? (double)total / d->rate : -1;
        }
        return 1;
    }
    case MUSIC_FORMAT_FLAC:
        d->flac = drflac_open(dr_read, flac_seek, flac_tell, d->file, NULL);
        if (!d->flac) { say(why, why_size, "not a playable FLAC file"); return 0; }
        d->rate = (int)d->flac->sampleRate;
        d->channels = d->flac->channels;
        d->seconds = d->flac->totalPCMFrameCount && d->rate ? (double)d->flac->totalPCMFrameCount / d->rate : -1;
        return 1;
    case MUSIC_FORMAT_WAV:
        if (!drwav_init(&d->wav, dr_read, wav_seek, wav_tell, d->file, NULL)) {
            say(why, why_size, "not a playable WAV file");
            return 0;
        }
        d->wav_open = 1;
        d->rate = (int)d->wav.sampleRate;
        d->channels = d->wav.channels;
        d->seconds = d->wav.totalPCMFrameCount && d->rate ? (double)d->wav.totalPCMFrameCount / d->rate : -1;
        return 1;
    }
    say(why, why_size, "not a song (MP3, Ogg Vorbis, FLAC or WAV)");
    return 0;
}

/* Source frames of d->channels channels into d->scratch; returns the frames, 0 at the end. */
static long read_inner(MusicDecoder *d, long frames)
{
    switch (d->format) {
    case MUSIC_FORMAT_MP3: {
        size_t got = mp3dec_ex_read(d->mp3, d->scratch, (size_t)frames * d->channels);
        if (!got && d->mp3->last_error) return -1;
        return (long)(got / d->channels);
    }
    case MUSIC_FORMAT_OGG:
        return stb_vorbis_get_samples_short_interleaved(d->ogg, d->channels, d->scratch, (int)(frames * d->channels));
    case MUSIC_FORMAT_FLAC:
        return (long)drflac_read_pcm_frames_s16(d->flac, (drflac_uint64)frames, d->scratch);
    case MUSIC_FORMAT_WAV:
        return (long)drwav_read_pcm_frames_s16(&d->wav, (drwav_uint64)frames, d->scratch);
    }
    return -1;
}

#if defined(_MSC_VER)
/* A damaged file must never take the game down: a fault inside a decoder ends that song instead. */
static int guarded_open(MusicDecoder *d, char *why, size_t why_size)
{
    __try { return open_inner(d, why, why_size); }
    __except (EXCEPTION_EXECUTE_HANDLER) { say(why, why_size, "the decoder failed on this file (damaged?)"); return -1; }
}
static long guarded_read(MusicDecoder *d, long frames)
{
    __try { return read_inner(d, frames); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
#else
static int guarded_open(MusicDecoder *d, char *why, size_t why_size) { return open_inner(d, why, why_size); }
static long guarded_read(MusicDecoder *d, long frames) { return read_inner(d, frames); }
#endif

MusicDecoder *music_decoder_open(const char *path, char *why, size_t why_size)
{
    MusicDecoder *d;
    int opened;
    say(why, why_size, "");
    if (!path || !*path) { say(why, why_size, "no file name"); return NULL; }
    d = (MusicDecoder *)calloc(1, sizeof(*d));
    if (!d) { say(why, why_size, "out of memory"); return NULL; }
    d->file = open_utf8(path);
    if (!d->file) { say(why, why_size, "the file cannot be opened"); free(d); return NULL; }
    d->format = sniff(d->file);
    if (d->format == MUSIC_FORMAT_NONE) {
        /* An MP3 can start with junk or a tag we do not know: minimp3 looks for a run of frames. */
        d->format = MUSIC_FORMAT_MP3;
        opened = guarded_open(d, why, why_size);
        if (opened != 1) say(why, why_size, "not a song (MP3, Ogg Vorbis, FLAC or WAV)");
    } else {
        opened = guarded_open(d, why, why_size);
    }
    if (opened == 1 && (d->rate < 8000 || d->rate > 192000 || d->channels < 1 || d->channels > 8)) {
        say(why, why_size, "unsupported sample rate or channel count");
        opened = 0;
    }
    if (opened != 1) {
        if (opened == 0) close_inner(d);   /* after a fault the decoder's state is not trusted: leak it */
        fclose(d->file);
        free(d);
        return NULL;
    }
    d->scratch_frames = 4096;
    d->scratch = (int16_t *)malloc((size_t)d->scratch_frames * d->channels * sizeof(int16_t));
    if (!d->scratch) { music_decoder_close(d); say(why, why_size, "out of memory"); return NULL; }
    return d;
}

int music_decoder_format(const MusicDecoder *d) { return d ? d->format : 0; }
int music_decoder_rate(const MusicDecoder *d) { return d ? d->rate : 0; }
int music_decoder_channels(const MusicDecoder *d) { return d ? d->channels : 0; }
double music_decoder_seconds(const MusicDecoder *d) { return d ? d->seconds : -1; }

long music_decoder_read(MusicDecoder *d, int16_t *out, long frames)
{
    long done = 0;
    if (!d || d->broken) return -1;
    while (done < frames) {
        long want = frames - done, got, i;
        if (want > d->scratch_frames) want = d->scratch_frames;
        got = guarded_read(d, want);
        if (got < 0) {
            d->broken = got == -2 ? 2 : 1;
            return done ? done : -1;
        }
        if (got == 0) break;
        if (got > want) got = want;
        for (i = 0; i < got; i++) {
            const int16_t *f = d->scratch + (size_t)i * d->channels;
            out[2 * (done + i)] = f[0];
            out[2 * (done + i) + 1] = d->channels > 1 ? f[1] : f[0];
        }
        done += got;
    }
    return done;
}

void music_decoder_close(MusicDecoder *d)
{
    if (!d) return;
    if (d->broken != 2) close_inner(d);
    if (d->file) fclose(d->file);
    free(d->scratch);
    free(d);
}

int music_decode_check(const char *path, double probe_seconds, double *seconds, char *why, size_t why_size)
{
    int16_t buf[2 * 2048];
    double want, got = 0;
    MusicDecoder *d = music_decoder_open(path, why, why_size);
    if (seconds) *seconds = -1;
    if (!d) return 0;
    want = probe_seconds * d->rate;
    while (got < want) {
        long n = music_decoder_read(d, buf, 2048);
        if (n < 0) { say(why, why_size, "damaged audio data"); music_decoder_close(d); return 0; }
        if (n == 0) break;
        got += n;
    }
    if (got == 0) { say(why, why_size, "no audio in the file"); music_decoder_close(d); return 0; }
    if (seconds) *seconds = d->seconds > 0 ? d->seconds : got / d->rate;
    music_decoder_close(d);
    return 1;
}

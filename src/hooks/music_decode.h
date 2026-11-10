/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Decoding the player's own songs (custom music, custom_music.cpp): MP3, Ogg Vorbis, FLAC and WAV
 * through single-header decoders vendored as source under thirdparty/audio/ (minimp3, stb_vorbis,
 * dr_flac, dr_wav; licences in thirdparty/*-LICENSE.txt).
 *
 * Host only: everything here runs on host threads with host memory (malloc), never in the guest's
 * memory or on its threads. Files are recognised by their first bytes, not by the name, and a file
 * that is not a song, is cut short or is damaged gives an error, never a crash: on Windows the
 * decoders run under structured exception handling as a last guard. */
#ifndef DEFJAM_MUSIC_DECODE_H
#define DEFJAM_MUSIC_DECODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MusicDecoder MusicDecoder;

enum { MUSIC_FORMAT_NONE, MUSIC_FORMAT_MP3, MUSIC_FORMAT_OGG, MUSIC_FORMAT_FLAC, MUSIC_FORMAT_WAV };

/* Whether a file name has one of the extensions the scan picks up (.mp3 .ogg .oga .flac .wav). */
int music_decode_known_extension(const char *name);

/* Opens a song by its UTF-8 path. NULL when it cannot be played, with the reason in `why`. */
MusicDecoder *music_decoder_open(const char *path, char *why, size_t why_size);
int music_decoder_format(const MusicDecoder *d);
int music_decoder_rate(const MusicDecoder *d);          /* frames per second of the source */
int music_decoder_channels(const MusicDecoder *d);      /* of the source; reads are always stereo */
double music_decoder_seconds(const MusicDecoder *d);    /* length, or -1 when the file does not say */
/* Up to `frames` stereo frames of 16-bit PCM into `out` (2 * frames values). Returns the frames
 * read, 0 at the end, -1 when the file turned out damaged (the decoder is then unusable). */
long music_decoder_read(MusicDecoder *d, int16_t *out, long frames);
void music_decoder_close(MusicDecoder *d);

/* Opens a song and decodes up to `probe_seconds` of it: 1 when it plays, 0 with the reason. */
int music_decode_check(const char *path, double probe_seconds, double *seconds, char *why, size_t why_size);

#ifdef __cplusplus
}
#endif

#endif

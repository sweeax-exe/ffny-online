"""Custom songs: the EA-XA encoder, tag reading and a whole conversion (src/hooks/song_convert.c).

A native driver built from song_convert.c and the vendored decoders: the encoder is checked by
decoding its frames here (the game's own EA-XA decoder, as docs/research/custom-songs.md
describes it), tags by files written here, and, with the user's own dump, a WAV converted into a
song folder whose map and streams are read back.
"""
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import wave

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "song_convert.h"
int main(int argc, char **argv)
{
    if (!strcmp(argv[1], "encode")) {             /* encode in.raw(int16 mono) out.bin */
        FILE *f = fopen(argv[2], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f) / 2; fseek(f, 0, SEEK_SET);
        int16_t *x = malloc(n * 2); fread(x, 2, n, f); fclose(f);
        FILE *o = fopen(argv[3], "wb"); int32_t h1 = 0, h2 = 0; uint8_t fr[15];
        for (long i = 0; i + 28 <= n; i += 28) { song_eaxa_frame(x + i, 28, &h1, &h2, fr); fwrite(fr, 1, 15, o); }
        fclose(o); return 0;
    }
    if (!strcmp(argv[1], "tags")) {               /* tags file */
        SongTags t;
        if (!song_read_tags(argv[2], &t)) { puts("FAIL"); return 1; }
        printf("%s\n%s\n%s\n", t.title, t.artist, t.album);
        return 0;
    }
    if (!strcmp(argv[1], "convert")) {            /* convert game_dir in out_dir interactive */
        SongTemplate t; char err[200];
        int inter = atoi(argv[5]);
        if (!song_template_load(argv[2], inter, &t, err, sizeof err)) { printf("NOTEMPLATE %s\n", err); return 0; }
        if (!song_convert(argv[3], argv[4], &t, inter, err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
        puts("OK"); return 0;
    }
    if (!strcmp(argv[1], "plan")) {               /* plan tune.mpf pieces.txt: the play-time order */
        static uint8_t d[1 << 20]; static char txt[1 << 16]; static SongPlan p;
        FILE *f = fopen(argv[2], "rb"); size_t n = fread(d, 1, sizeof d, f); fclose(f);
        f = fopen(argv[3], "rb"); size_t tn = fread(txt, 1, sizeof txt - 1, f); fclose(f); txt[tn] = 0;
        if (!song_plan(d, n, txt, &p)) { puts("FAIL"); return 1; }
        printf("pieces %d lanes %d\n", p.pieces, p.lanes);
        for (int i = 0; i < p.track0; i++)        /* each sample: its lane, its entry, its length */
            printf("sample %d %u %u\n", p.lane[i], *(uint32_t *)(d + p.table + i * 8), *(uint32_t *)(d + p.table + i * 8 + 4));
        for (int l = 0; l < p.lanes; l++)
            for (int k = 0; k < p.pieces; k++) printf("piece %d %u %u\n", l, p.off[l][k], p.ms[l][k]);
        return 0;
    }
    return 2;
}
'''


@pytest.fixture(scope='module')
def tool(tmp_path_factory):
    compiler = shutil.which('cl') if os.name == 'nt' else shutil.which('cc')
    if not compiler:
        pytest.skip('native song fixture needs a C compiler')
    folder = tmp_path_factory.mktemp('song-convert-native')
    host = ('#include <windows.h>\n#define HOST_SEP "\\\\"\n' if os.name == 'nt' else
            'typedef int BOOL;\ntypedef unsigned int DWORD;\n#define TRUE 1\n#define FALSE 0\n#define HOST_SEP "/"\n')
    (folder / 'host.h').write_text(host)
    (folder / 'driver.c').write_text(DRIVER)
    exe = folder / ('songs.exe' if os.name == 'nt' else 'songs')
    sources = [folder / 'driver.c', ROOT / 'src/hooks/song_convert.c', ROOT / 'src/hooks/song_decoders.c']
    includes = [folder, ROOT / 'src/hooks']
    if os.name == 'nt':
        cmd = [compiler, '/nologo', '/O2', '/TC', *['/I' + str(p) for p in includes], *map(str, sources), '/Fe:' + str(exe)]
    else:
        cmd = [compiler, '-O2', *['-I' + str(p) for p in includes], *map(str, sources), '-o', str(exe), '-lm']
    result = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


def run(tool, *args):
    r = subprocess.run([str(tool), *map(str, args)], capture_output=True, text=True, timeout=600)
    assert r.returncode == 0, r.stdout + r.stderr
    return r.stdout


TABLE = [0, 240, 460, 392, 0, 0, -208, -220]


def decode(frames, n):
    out, h1, h2 = [], 0, 0
    for f in range(len(frames) // 15):
        b = frames[f * 15:(f + 1) * 15]
        c1, c2, sh = TABLE[b[0] >> 4], TABLE[(b[0] >> 4) + 4], (b[0] & 15) + 8
        for i in range(28):
            nib = (b[1 + i // 2] >> 4) if i % 2 == 0 else (b[1 + i // 2] & 15)
            s = ((nib << 28) & 0xffffffff)
            s = s - (1 << 32) if s & 0x80000000 else s
            v = ((s >> sh) + c1 * h1 + c2 * h2) >> 8
            v = max(-32768, min(32767, v))
            out.append(v); h2, h1 = h1, v
    return out[:n]


def test_encoder_round_trip(tool, tmp_path):
    n = 28 * 400
    x = [int(12000 * math.sin(2 * math.pi * 220 * i / 44100) + 6000 * math.sin(2 * math.pi * 1760 * i / 44100))
         for i in range(n)]
    (tmp_path / 'in.raw').write_bytes(struct.pack(f'<{n}h', *x))
    run(tool, 'encode', tmp_path / 'in.raw', tmp_path / 'out.bin')
    y = decode((tmp_path / 'out.bin').read_bytes(), n)
    signal = sum(v * v for v in x)
    noise = sum((a - b) ** 2 for a, b in zip(x, y))
    assert 10 * math.log10(signal / noise) > 25


def id3v23(frames):
    body = b''
    for fid, text in frames:
        data = b'\x00' + text.encode('latin-1')
        body += fid.encode() + struct.pack('>I', len(data)) + b'\0\0' + data
    size = len(body)
    synch = bytes([(size >> 21) & 0x7f, (size >> 14) & 0x7f, (size >> 7) & 0x7f, size & 0x7f])
    return b'ID3\x03\x00\x00' + synch + body


def test_tags(tool, tmp_path):
    mp3 = tmp_path / 'whatever.mp3'
    mp3.write_bytes(id3v23([('TIT2', 'Caf\xe9 Song'), ('TPE1', 'Some Artist'), ('TALB', 'An Album')]) + b'\xff\xfb' + b'\0' * 400)
    assert run(tool, 'tags', mp3).splitlines() == ['Cafe Song', 'Some Artist', 'An Album']
    v22 = tmp_path / 'old.mp3'
    body = b'TT2' + bytes([0, 0, 6]) + b'\x00Older'
    v22.write_bytes(b'ID3\x02\x00\x00' + bytes([0, 0, 0, len(body)]) + body + b'\0' * 200)
    assert run(tool, 'tags', v22).splitlines()[0] == 'Older'
    v1 = tmp_path / 'v1.mp3'
    tag = b'TAG' + b'One Title'.ljust(30, b'\0') + b'One Artist'.ljust(30, b'\0') + b'One Album'.ljust(30, b'\0') + b'\0' * 35
    v1.write_bytes(b'\xff\xfb' + b'\0' * 400 + tag)
    assert run(tool, 'tags', v1).splitlines() == ['One Title', 'One Artist', 'One Album']
    folder = tmp_path / 'My Mixes'
    folder.mkdir()
    named = folder / 'Big Band - Swing_Time.wav'
    with wave.open(str(named), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050); w.writeframes(b'\0\0' * 100)
    assert run(tool, 'tags', named).splitlines() == ['Swing_Time', 'Big Band', 'My Mixes']
    plain = tmp_path / 'my-demo-song.wav'
    shutil.copy(named, plain)
    assert run(tool, 'tags', plain).splitlines()[:2] == ['my demo song', 'Unknown Artist']


@pytest.mark.parametrize('interactive', [0, 1])
def test_whole_conversion(tool, tmp_path, interactive):
    data = os.environ.get('DEFJAM_DATA') or str(ROOT.parent / 'defjam')
    game = Path(data) / 'extracted'
    if not (game / 'audio' / 'music2.viv').is_file():
        pytest.skip("needs the user's own dump")
    src = tmp_path / 'song.wav'
    rate, secs = 48000, 30
    frames = b''.join(struct.pack('<hh', int(9000 * math.sin(2 * math.pi * 330 * i / rate)),
                                  int(9000 * math.sin(2 * math.pi * 440 * i / rate))) for i in range(rate * secs))
    with wave.open(str(src), 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate); w.writeframes(frames)
    out = tmp_path / 'out'
    out.mkdir()
    assert run(tool, 'convert', game, src, out, interactive).strip() == 'OK'
    mpf = (out / 'tune.mpf').read_bytes()
    mus = (out / 'main.mus').read_bytes()
    assert mpf[:4] == b'xDFP' and mus[:4] == b'SCHl'
    for f in ('loading0.mus', 'overlay0.mus'):
        assert (out / f).read_bytes()[:4] == b'BNKl'
    # every stream the map's sample table points into main.mus is an SCHl at 44.1 kHz
    import re
    starts = [m.start() for m in re.finditer(b'SCHl', mus)]
    assert starts and all(s % 0x80 == 0 for s in starts)
    assert b'\x84\x03\x00\xac\x44' in mus[:0x40]
    # the play-time order (song_plan, used by src/patches/song_cursor.c): every lane holds the whole
    # song in order, its pieces adding up to the song and each as long as lane 0's; every sample of
    # the map names one of the pieces of its lane
    lines = run(tool, 'plan', out / 'tune.mpf', out / 'pieces.txt').splitlines()
    pieces, lanes = int(lines[0].split()[1]), int(lines[0].split()[3])
    assert pieces >= 8 and lanes == (4 if interactive else 1)
    samples = [list(map(int, line.split()[1:])) for line in lines if line.startswith('sample ')]
    assert samples
    if interactive:
        assert len({s[0] for s in samples}) >= 2
    # the game sizes its read buffer from consecutive entries: 3000 ms * max((next - this) * 128 / ms);
    # it must come out at the stream's own rate (about 47-48 bytes per ms at 44.1 kHz), never far above
    rate = max((b[1] - a[1]) * 128 / a[2] for a, b in zip(samples, samples[1:]))
    assert all(b[1] > a[1] for a, b in zip(samples, samples[1:]))
    assert 40 < rate < 50, rate
    table = {}
    for line in lines:
        if line.startswith('piece '):
            _, lane, off, ms = line.split()
            table.setdefault(int(lane), []).append((int(off), int(ms)))
    for lane, rows in table.items():
        assert len(rows) == pieces
        assert [o for o, _ in rows] == sorted(o for o, _ in rows)
        assert abs(sum(ms for _, ms in rows) - secs * 1000) <= pieces + 2
        assert [ms for _, ms in rows] == [ms for _, ms in table[0]]

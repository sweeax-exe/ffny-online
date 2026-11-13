"""Custom music underneath (src/hooks/music_decode.c, src/hooks/music_library.cpp), compiled on its own and
fed only files made here: sine-wave WAV and FLAC, a silent MP3 built frame by frame, and damaged, empty,
mislabelled and random files. No songs, no game data."""
from pathlib import Path
import math
import os
import random
import shutil
import struct
import subprocess
import wave

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "music_library.h"
extern "C" {
#include "music_decode.h"
}
using namespace musiclib;
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    std::string cmd = argv[1];
    if (cmd == "check") {
        double seconds = -1;
        char why[160] = "";
        int ok = music_decode_check(argv[2], 3.0, &seconds, why, sizeof(why));
        if (ok) printf("PLAYS %.2f\n", seconds); else printf("BAD %s\n", why);
        return 0;
    }
    if (cmd == "play") {          // decodes the whole file: frames, rate and the peak
        char why[160] = "";
        MusicDecoder *d = music_decoder_open(argv[2], why, sizeof(why));
        if (!d) { printf("BAD %s\n", why); return 0; }
        short buf[2 * 1000];
        long total = 0, n;
        int peak = 0;
        while ((n = music_decoder_read(d, buf, 1000)) > 0) {
            for (long i = 0; i < 2 * n; i++) peak = std::max(peak, std::abs((int)buf[i]));
            total += n;
        }
        printf("%s %ld %d %d\n", n < 0 ? "DAMAGED" : "END", total, music_decoder_rate(d), peak);
        music_decoder_close(d);
        return 0;
    }
    if (cmd == "scan") {          // scan, index, check the unchecked ones, save the index
        size_t more = 0, limit = argc > 3 ? (size_t)atoi(argv[3]) : kMaxFiles;
        auto songs = scan(from_utf8(argv[2]), limit, kMaxDepth, &more);
        apply_index(from_utf8(argv[2]), songs);
        int checked = 0;
        for (auto &s : songs) if (s.state == UNCHECKED) { check(s); checked++; }
        save_index(from_utf8(argv[2]), songs);
        printf("SONGS %zu MORE %zu CHECKED %d\n", songs.size(), more, checked);
        for (auto &s : songs) printf("%s|%d|%s\n", s.rel.c_str(), s.state, s.why.c_str());
        return 0;
    }
    if (cmd == "add") {
        std::vector<fs::path> files;
        for (int i = 3; i < argc; i++) files.push_back(from_utf8(argv[i]));
        auto r = add(from_utf8(argv[2]), files);
        printf("ADDED %d SKIPPED %d\n", r.added, r.skipped);
        return 0;
    }
    if (cmd == "fights") {        // the match phase per update, "phase*count,..."; prints E/B at each update they fire
        FightWatch w;
        std::string spec = argv[2];
        size_t at = 0, update = 0;
        while (at < spec.size()) {
            size_t end = spec.find(',', at);
            if (end == std::string::npos) end = spec.size();
            std::string item = spec.substr(at, end - at);
            size_t star = item.find('*');
            int phase = atoi(item.substr(0, star).c_str()), count = star == std::string::npos ? 1 : atoi(item.c_str() + star + 1);
            for (int i = 0; i < count; i++, update++) {
                auto e = w.feed(phase);
                if (e != FightWatch::NONE) printf("%c%zu ", e == FightWatch::ENDED ? 'E' : 'B', update);
            }
            at = end + 1;
        }
        printf("\n");
        return 0;
    }
    if (cmd == "playlist") {      // own game shuffle seed draws [avoid-game every k-th draw]: the items, 'g' a game turn
        size_t own = (size_t)atoi(argv[2]), game = (size_t)atoi(argv[3]);
        int draws = atoi(argv[6]), avoid_every = argc > 7 ? atoi(argv[7]) : 0;
        Playlist p;
        p.reset(own, game, atoi(argv[4]) != 0, strtoull(argv[5], nullptr, 10));
        for (int i = 0; i < draws; i++) {
            size_t item = p.next(avoid_every && (i % avoid_every) == avoid_every - 1);
            if (p.is_game(item)) printf("g%zu ", item - own); else printf("%zu ", item);
        }
        printf("\n");
        return 0;
    }
    if (cmd == "firsts") {        // own game first_seed seeds: the first item of `seeds` fresh shuffled lists
        size_t own = (size_t)atoi(argv[2]), game = (size_t)atoi(argv[3]);
        unsigned long long seed0 = strtoull(argv[4], nullptr, 10);
        int seeds = atoi(argv[5]);
        for (int i = 0; i < seeds; i++) {
            Playlist p;
            p.reset(own, game, true, seed0 + (unsigned long long)i * 0x9E3779B97F4A7C15ull);
            size_t item = p.next();
            printf("%zu ", item);
        }
        printf("\n");
        return 0;
    }
    if (cmd == "level") {         // the level gain of a file (decoded whole)
        char why[160] = "";
        MusicDecoder *d = music_decoder_open(argv[2], why, sizeof(why));
        if (!d) { printf("BAD %s\n", why); return 0; }
        LevelMeter m;
        m.rate = music_decoder_rate(d);
        short buf[2 * 1000];
        long n;
        while ((n = music_decoder_read(d, buf, 1000)) > 0) m.feed(buf, n);
        music_decoder_close(d);
        printf("%.4f %.4f\n", m.rms(), m.gain());
        return 0;
    }
    if (cmd == "order") {         // folder [sort]: scan, own order (order.txt), artists, sorted: rel|artist|added
        auto songs = scan(from_utf8(argv[2]));
        apply_order(from_utf8(argv[2]), songs);
        for (auto &s : songs) read_artist(s);
        sort_songs(songs, argc > 3 ? atoi(argv[3]) : SORT_CUSTOM);
        for (auto &s : songs) printf("%s|%s|%lld\n", s.rel.c_str(), s.artist.c_str(), (long long)s.added);
        return 0;
    }
    if (cmd == "move") {          // folder sort rel delta: moves the song in the list as sorted, saves it as the own order
        auto songs = scan(from_utf8(argv[2]));
        apply_order(from_utf8(argv[2]), songs);
        for (auto &s : songs) read_artist(s);
        sort_songs(songs, atoi(argv[3]));
        size_t at = 0;
        while (at < songs.size() && songs[at].rel != argv[4]) at++;
        size_t to = move_song(songs, at, atoi(argv[5]));
        save_order(from_utf8(argv[2]), order_of(songs));
        printf("%zu %zu\n", at, to);
        return 0;
    }
    if (cmd == "artist") {        // path [name rel]
        Song s;
        s.path = from_utf8(argv[2]);
        s.name = argc > 3 ? argv[3] : "";
        s.rel = argc > 4 ? argv[4] : "";
        read_artist(s);
        printf("[%s]\n", s.artist.c_str());
        return 0;
    }
    if (cmd == "continue") {      // own game shuffle after draws: continue_after(after), then `draws` items
        size_t own = (size_t)atoi(argv[2]), game = (size_t)atoi(argv[3]);
        Playlist p;
        p.reset(own, game, atoi(argv[4]) != 0, 1);
        p.continue_after((size_t)atoi(argv[5]));
        for (int i = 0; i < atoi(argv[6]); i++) {
            size_t item = p.next();
            if (p.is_game(item)) printf("g%zu ", item - own); else printf("%zu ", item);
        }
        printf("\n");
        return 0;
    }
    if (cmd == "remove") {
        auto songs = scan(from_utf8(argv[2]));
        for (auto &s : songs) if (s.rel == argv[3]) { std::string why; printf("%s\n", remove(from_utf8(argv[2]), s, &why) ? "REMOVED" : why.c_str()); }
        return 0;
    }
    return 2;
}
'''


@pytest.fixture(scope='module')
def tool(tmp_path_factory):
    folder = tmp_path_factory.mktemp('custom-music-native')
    (folder / 'driver.cpp').write_text(DRIVER, encoding='utf-8')
    include = ROOT / 'src/hooks'
    sources = [str(folder / 'driver.cpp'), str(ROOT / 'src/hooks/music_library.cpp'),
               str(ROOT / 'src/hooks/music_decode.c')]
    compiler = shutil.which('cl')
    if compiler:
        exe = folder / 'music.exe'
        cmd = [compiler, '/nologo', '/std:c++17', '/EHsc', '/O2', '/W3', '/I' + str(include), *sources,
               'shell32.lib', '/Fe:' + str(exe)]
        result = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    else:
        cxx, cc = shutil.which('clang++') or shutil.which('g++'), shutil.which('clang') or shutil.which('gcc')
        if not cxx or not cc:
            pytest.skip('C and C++ compilers required')
        exe = folder / 'music'
        result = subprocess.run([cc, '-c', '-O2', sources[2], '-o', str(folder / 'decode.o')], cwd=folder,
                                capture_output=True, text=True)
        if result.returncode == 0:
            result = subprocess.run([cxx, '-std=c++17', '-O2', '-I' + str(include), sources[0], sources[1],
                                     str(folder / 'decode.o'), '-lm', '-o', str(exe)], cwd=folder, capture_output=True,
                                    text=True)
    assert result.returncode == 0, result.stdout[-4000:] + result.stderr[-4000:]
    return exe


def run(tool, *args):
    env = dict(os.environ, RECOMP_MUSIC_NO_RECYCLE='1')
    result = subprocess.run([str(tool), *map(str, args)], capture_output=True, text=True, timeout=120, env=env)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def sine(seconds, rate=44100, channels=2, freq=440.0):
    frames = int(seconds * rate)
    out = []
    for i in range(frames):
        v = int(12000 * math.sin(2 * math.pi * freq * i / rate))
        out.extend([v] * channels)
    return out


def write_wav(path, seconds=1.0, rate=44100, channels=2):
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack('<%dh' % int(seconds * rate * channels), *sine(seconds, rate, channels)))


def silent_mp3(path, frames=100):
    """MPEG-1 Layer III, 128 kbit/s, 44.1 kHz, stereo: 417-byte frames whose side info and main data are all
    zero, which decode to silence (1152 samples each)."""
    frame = b'\xFF\xFB\x90\x00' + bytes(417 - 4)
    path.write_bytes(frame * frames)


def crc8(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def crc16(data):
    c = 0
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x8005) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def write_flac(path, seconds=1.0, rate=44100):
    """Stereo 16-bit FLAC with VERBATIM subframes, fixed 4096-sample blocks."""
    samples = sine(seconds, rate, 2, 330.0)
    total = len(samples) // 2
    block = 4096
    info = struct.pack('>HH', block, block) + (0).to_bytes(3, 'big') + (0).to_bytes(3, 'big')
    packed = (rate << 44) | (1 << 41) | (15 << 36) | total
    info += packed.to_bytes(8, 'big') + bytes(16)
    out = b'fLaC' + bytes([0x80]) + len(info).to_bytes(3, 'big') + info
    for n, start in enumerate(range(0, total, block)):
        count = min(block, total - start)
        head = bytes([0xFF, 0xF8, (0x7 << 4) | 0x9, (0x1 << 4) | (0x4 << 1)])   # 16-bit-count follows, 44.1k, L/R, 16 bit
        num = n
        utf = bytes([num]) if num < 0x80 else bytes([0xC0 | (num >> 6), 0x80 | (num & 0x3F)])
        head += utf + (count - 1).to_bytes(2, 'big')
        head += bytes([crc8(head)])
        body = b''
        for ch in range(2):
            body += bytes([0x02])                 # VERBATIM, no wasted bits
            body += struct.pack('>%dh' % count, *samples[2 * start + ch:2 * (start + count):2])
        frame = head + body
        frame += crc16(frame).to_bytes(2, 'big')
        out += frame
    path.write_bytes(out)


def test_wav_plays_and_folds_mono_to_stereo(tool, tmp_path):
    write_wav(tmp_path / 'a.wav', 1.0)
    write_wav(tmp_path / 'm.wav', 0.5, rate=22050, channels=1)
    assert run(tool, 'check', tmp_path / 'a.wav').split()[0] == 'PLAYS'
    end, frames, rate, peak = run(tool, 'play', tmp_path / 'a.wav').split()
    assert (end, int(frames), int(rate)) == ('END', 44100, 44100) and int(peak) > 11000
    end, frames, rate, peak = run(tool, 'play', tmp_path / 'm.wav').split()
    assert (end, int(frames), int(rate)) == ('END', 11025, 22050) and int(peak) > 11000


def test_flac_plays(tool, tmp_path):
    write_flac(tmp_path / 'b.flac', 1.0)
    end, frames, rate, peak = run(tool, 'play', tmp_path / 'b.flac').split()
    assert (end, int(frames), int(rate)) == ('END', 44100, 44100) and int(peak) > 11000


def test_mp3_plays(tool, tmp_path):
    silent_mp3(tmp_path / 'c.mp3', 100)
    out = run(tool, 'check', tmp_path / 'c.mp3').split()
    assert out[0] == 'PLAYS' and abs(float(out[1]) - 100 * 1152 / 44100) < 0.1
    end, frames, rate, _ = run(tool, 'play', tmp_path / 'c.mp3').split()
    assert end == 'END' and int(rate) == 44100 and int(frames) > 100000


@pytest.mark.parametrize('name,content', [
    ('empty.mp3', b''),
    ('text.mp3', b'this is not a song\n' * 50),
    ('random.ogg', None),
    ('random.flac', None),
    ('fake.wav', b'RIFF\x10\x00\x00\x00WAVEjunkjunk'),
    ('header-only.flac', b'fLaC\x80\x00\x00\x22' + bytes(34)),
    ('ogg-header.ogg', b'OggS' + bytes(200)),
])
def test_bad_files_are_refused_not_crashed(tool, tmp_path, name, content):
    if content is None:
        rnd = random.Random(name)
        content = (b'OggS' if name.endswith('.ogg') else b'fLaC') + bytes(rnd.getrandbits(8) for _ in range(20000))
    (tmp_path / name).write_bytes(content)
    assert run(tool, 'check', tmp_path / name).startswith('BAD ')
    assert run(tool, 'play', tmp_path / name).split()[0] in ('BAD', 'END', 'DAMAGED')


def test_truncated_and_corrupted_files_do_not_crash(tool, tmp_path):
    write_wav(tmp_path / 'w.wav', 1.0)
    write_flac(tmp_path / 'f.flac', 1.0)
    silent_mp3(tmp_path / 'm.mp3', 50)
    rnd = random.Random(7)
    for src in ('w.wav', 'f.flac', 'm.mp3'):
        data = (tmp_path / src).read_bytes()
        (tmp_path / ('cut-' + src)).write_bytes(data[:len(data) // 3])
        bent = bytearray(data)
        for _ in range(300):
            bent[rnd.randrange(64, len(bent))] = rnd.getrandbits(8)
        (tmp_path / ('bent-' + src)).write_bytes(bytes(bent))
        for prefix in ('cut-', 'bent-'):
            out = run(tool, 'play', tmp_path / (prefix + src)).split()
            assert out[0] in ('BAD', 'END', 'DAMAGED')


def test_scan_index_and_skip(tool, tmp_path):
    music = tmp_path / 'music'
    (music / 'sub' / 'deeper').mkdir(parents=True)
    (music / '.cache').mkdir()
    write_wav(music / 'one.wav', 0.3)
    write_flac(music / 'sub' / 'two.flac', 0.3)
    silent_mp3(music / 'sub' / 'deeper' / 'three.mp3', 20)
    (music / 'broken.mp3').write_bytes(b'nope' * 100)
    (music / 'cover.jpg').write_bytes(b'\xFF\xD8\xFF')
    (music / 'notes.txt').write_text('x')
    write_wav(music / '.cache' / 'hidden.wav', 0.1)
    out = run(tool, 'scan', music).splitlines()
    assert out[0] == 'SONGS 4 MORE 0 CHECKED 4'
    states = {line.split('|')[0]: int(line.split('|')[1]) for line in out[1:]}
    assert states == {'broken.mp3': 2, 'one.wav': 1, 'sub/deeper/three.mp3': 1, 'sub/two.flac': 1}
    # the second scan takes the answers from the index
    assert run(tool, 'scan', music).splitlines()[0] == 'SONGS 4 MORE 0 CHECKED 0'
    # a changed file is checked again
    write_wav(music / 'one.wav', 0.6)
    os.utime(music / 'one.wav', (1, 1))
    assert run(tool, 'scan', music).splitlines()[0] == 'SONGS 4 MORE 0 CHECKED 1'
    # a huge folder: only the first N by name, the rest counted
    assert run(tool, 'scan', music, 2).splitlines()[0].startswith('SONGS 2 MORE 2')


def test_scan_of_a_missing_folder_is_empty(tool, tmp_path):
    assert run(tool, 'scan', tmp_path / 'nowhere').splitlines()[0] == 'SONGS 0 MORE 0 CHECKED 0'


def test_add_copies_and_remove_takes_out(tool, tmp_path):
    src = tmp_path / 'src'
    src.mkdir()
    write_wav(src / 'song.wav', 0.2)
    (src / 'readme.txt').write_text('x')
    music = tmp_path / 'music'
    assert run(tool, 'add', music, src / 'song.wav', src / 'readme.txt').strip() == 'ADDED 1 SKIPPED 1'
    assert (music / 'song.wav').read_bytes() == (src / 'song.wav').read_bytes()
    assert run(tool, 'add', music, src / 'song.wav').strip() == 'ADDED 0 SKIPPED 1'      # already there
    write_wav(src / 'song.wav', 0.4)
    assert run(tool, 'add', music, src).strip() == 'ADDED 1 SKIPPED 0'                    # a folder; renamed
    assert (music / 'song (2).wav').is_file()
    assert run(tool, 'remove', music, 'song (2).wav').strip() == 'REMOVED'
    assert not (music / 'song (2).wav').exists() and (music / 'song.wav').exists()
    assert (music / '.cache' / 'removed' / 'song (2).wav').is_file()


def test_fight_watch_new_fight_after_a_fight_is_over(tool):
    def events(spec):
        return [(e[0], int(e[1:])) for e in run(tool, 'fights', spec).split()]
    # menus, the first fight (no event at its start), KO, results, the rematch's start, the rematch
    assert events('0*100,1*600,2*50,3*200,0*20,1*300') == [('E', 729), ('B', 950)]
    # straight from finished into fighting again (a rotation): ends, then begins at the first fighting update
    assert events('1*600,3*40,1*10') == [('E', 629), ('B', 640)]
    # a KO undone by a rollback a few updates later is no end; a fight that never really ran is none either
    assert events('1*600,2*5,1*600') == []
    assert events('1*20,3*100,1*100') == []
    # a fight left from its pause menu (phase back to 0): over after the settle time, the next starts at fighting
    assert events('1*600,0*100,1*10') == [('E', 629), ('B', 700)]
    # junk before the game sets the phase counts as no fight
    assert events('77*50,0*50') == []


def test_playlist_first_item_is_random_across_songs_and_game_tracks(tool):
    # mix with 3 songs and 3 game turns: the first item is uniform over all six (the player's report: the
    # first was always one of the songs), over 6000 fresh lists
    firsts = [int(x) for x in run(tool, 'firsts', 3, 3, 12345, 6000).split()]
    counts = [firsts.count(i) for i in range(6)]
    assert all(800 < c < 1200 for c in counts), counts
    assert sum(counts[3:]) > 2500                      # the game's own music first about half of the time


def test_playlist_each_item_once_per_round_and_no_immediate_repeat(tool):
    for own, game in ((5, 5), (1, 1), (2, 0), (87, 30), (4, 0)):
        n = own + game
        items = run(tool, 'playlist', own, game, 1, 777 + own, n * 50).split()
        for i in range(0, len(items), n):
            assert sorted(items[i:i + n]) == sorted(set(items[i:i + n])) and len(items[i:i + n]) == n
        for a, b in zip(items, items[1:]):
            assert a != b                              # never the same song (or game turn) twice in a row


def test_playlist_in_order_alternates_songs_and_game_like_before(tool):
    assert run(tool, 'playlist', 3, 2, 0, 1, 10).split() == ['0', 'g0', '1', 'g1', '2', '0', 'g0', '1', 'g1', '2']
    assert run(tool, 'playlist', 3, 0, 0, 1, 6).split() == ['0', '1', '2', '0', '1', '2']


def test_playlist_new_fight_after_the_game_music_gets_a_song(tool):
    # avoid-game on every draw (each one a new fight after the game's music): always a song while any
    # exist, and every song still comes round
    items = run(tool, 'playlist', 4, 4, 1, 99, 40, 1).split()
    assert not any(x.startswith('g') for x in items)
    assert set(items) == {'0', '1', '2', '3'}
    # avoid-game on every second draw: a game turn is never followed by one
    items = run(tool, 'playlist', 3, 3, 1, 7, 600, 2).split()
    assert any(x.startswith('g') for x in items)
    for i in range(1, len(items), 2):
        assert not items[i].startswith('g')
    # only game turns: nothing to avoid with, the game's music it is
    assert run(tool, 'playlist', 0, 2, 1, 5, 3, 1).split()[0].startswith('g')


def test_level_meter_brings_loud_and_quiet_songs_to_one_level(tool, tmp_path):
    # write_wav's sine peaks at 12000 (RMS 0.259, a loud master): turned down to -20 dBFS
    write_wav(tmp_path / 'loud.wav', 3.0)
    rms, gain = map(float, run(tool, 'level', tmp_path / 'loud.wav').split())
    assert abs(rms - 12000 / 32768 / math.sqrt(2)) < 0.01 and abs(gain - 0.1 / rms) < 0.01
    # a quiet one (peak 1200) comes up, silence before it does not count, and the gain stays within 0.1..2
    frames = [0] * (2 * 44100 * 2) + [int(1200 * math.sin(2 * math.pi * 440 * i / 44100)) for i in range(44100 * 3)
                                      for _ in range(2)]
    with wave.open(str(tmp_path / 'quiet.wav'), 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(struct.pack('<%dh' % len(frames), *frames))
    rms, gain = map(float, run(tool, 'level', tmp_path / 'quiet.wav').split())
    assert abs(rms - 1200 / 32768 / math.sqrt(2)) < 0.003 and abs(gain - 2.0) < 1e-6
    # nothing over the gate: unchanged
    with wave.open(str(tmp_path / 'silent.wav'), 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(bytes(4 * 44100))
    assert run(tool, 'level', tmp_path / 'silent.wav').split() == ['0.0000', '1.0000']


def test_playlist_seeds_differ(tool):
    assert run(tool, 'playlist', 20, 20, 1, 1, 40).split() != run(tool, 'playlist', 20, 20, 1, 2, 40).split()



# ---- the order of the songs (build 82: sort and the player's own order) ----

SORT_CUSTOM, SORT_NAME, SORT_ARTIST, SORT_ADDED = range(4)


def rows(out):
    return [line.split('|') for line in out.splitlines() if line]


def id3_mp3(path, artist, frame_id=b'TPE1', encoding=3):
    text = artist.encode('utf-8') if encoding == 3 else b'\xff\xfe' + artist.encode('utf-16-le')
    body = bytes([encoding]) + text
    frames = b'TIT2' + struct.pack('>I', 3) + b'\x00\x00' + b'\x03ab'      # a title first
    frames += frame_id + struct.pack('>I', len(body)) + b'\x00\x00' + body
    size = len(frames) + 20                                                  # and some padding
    syncsafe = bytes([(size >> 21) & 0x7F, (size >> 14) & 0x7F, (size >> 7) & 0x7F, size & 0x7F])
    path.write_bytes(b'ID3\x03\x00\x00' + syncsafe + frames + bytes(20) + b'\xFF\xFB\x90\x00' + bytes(413))


def vorbis_comment(artist):
    vendor = b'test'
    entries = [b'TITLE=x', b'artist=' + artist.encode('utf-8')]
    out = struct.pack('<I', len(vendor)) + vendor + struct.pack('<I', len(entries))
    for e in entries:
        out += struct.pack('<I', len(e)) + e
    return out


def tagged_flac(path, artist):
    info = bytes(34)
    vc = vorbis_comment(artist)
    path.write_bytes(b'fLaC' + bytes([0x00]) + len(info).to_bytes(3, 'big') + info +
                     bytes([0x84]) + len(vc).to_bytes(3, 'big') + vc)


def tagged_ogg(path, artist):
    path.write_bytes(b'OggS' + bytes(60) + b'\x01vorbis' + bytes(20) + b'OggS' + bytes(23) + b'\x03vorbis' +
                     vorbis_comment(artist) + b'\x01')


def tagged_wav(path, artist):
    write_wav(path, 0.1)
    data = bytearray(path.read_bytes())
    iart = artist.encode('utf-8') + b'\x00'
    if len(iart) % 2:
        iart += b'\x00'
    info = b'INFO' + b'IART' + struct.pack('<I', len(iart)) + iart
    data += b'LIST' + struct.pack('<I', len(info)) + info
    data[4:8] = struct.pack('<I', len(data) - 8)
    path.write_bytes(bytes(data))


def test_artist_from_tags_and_from_the_name(tool, tmp_path):
    id3_mp3(tmp_path / 'a.mp3', 'Borough Kings')
    assert run(tool, 'artist', tmp_path / 'a.mp3').strip() == '[Borough Kings]'
    id3_mp3(tmp_path / 'b.mp3', 'Lady Brick', encoding=1)                   # UTF-16 with a BOM
    assert run(tool, 'artist', tmp_path / 'b.mp3').strip() == '[Lady Brick]'
    id3_mp3(tmp_path / 'c.mp3', 'Nine Avenue', frame_id=b'TPE2')            # album artist only
    assert run(tool, 'artist', tmp_path / 'c.mp3').strip() == '[Nine Avenue]'
    tagged_flac(tmp_path / 'd.flac', 'Delancey')
    assert run(tool, 'artist', tmp_path / 'd.flac').strip() == '[Delancey]'
    tagged_ogg(tmp_path / 'e.ogg', 'Quiet Storm')
    assert run(tool, 'artist', tmp_path / 'e.ogg').strip() == '[Quiet Storm]'
    tagged_wav(tmp_path / 'f.wav', 'MC Lockjaw')
    assert run(tool, 'artist', tmp_path / 'f.wav').strip() == '[MC Lockjaw]'
    # no tag: "Artist - Title", else the sub-folder; a track number is no artist
    write_wav(tmp_path / 'g.wav', 0.1)
    assert run(tool, 'artist', tmp_path / 'g.wav', 'Ferry Crew - Night Boat', 'Ferry Crew - Night Boat.wav').strip() == '[Ferry Crew]'
    assert run(tool, 'artist', tmp_path / 'g.wav', '01 - Night Boat', 'Harbor/01 - Night Boat.wav').strip() == '[Harbor]'
    assert run(tool, 'artist', tmp_path / 'g.wav', 'Night Boat', 'Night Boat.wav').strip() == '[]'
    # damaged or foreign files give no artist and do not crash
    (tmp_path / 'h.mp3').write_bytes(b'ID3\x04\x00\x00\x7f\x7f\x7f\x7fTPE1\xff\xff\xff\xff')
    assert run(tool, 'artist', tmp_path / 'h.mp3').strip() == '[]'
    (tmp_path / 'i.flac').write_bytes(b'fLaC\x84\xff\xff\xff\x01')
    assert run(tool, 'artist', tmp_path / 'i.flac').strip() == '[]'


def test_sort_by_name_artist_and_added(tool, tmp_path):
    music = tmp_path / 'music'
    music.mkdir()
    for i, name in enumerate(['charlie', 'Alpha', 'bravo']):
        write_wav(music / (name + '.wav'), 0.1)
        os.utime(music / (name + '.wav'), (1700000000 + i * 100, 1700000000 + i * 100))
    id3_mp3(music / 'zulu.mp3', 'Aardvark')
    os.utime(music / 'zulu.mp3', (1700000500, 1700000500))
    first = rows(run(tool, 'order', music))
    assert [r[0] for r in first] == ['Alpha.wav', 'bravo.wav', 'charlie.wav', 'zulu.mp3']    # the folder's order
    assert (music / 'order.txt').exists()
    # no list before: each song "added" at its file time
    assert {r[0]: int(r[2]) for r in first}['charlie.wav'] == 1700000000
    assert [r[0] for r in rows(run(tool, 'order', music, SORT_NAME))] == ['Alpha.wav', 'bravo.wav', 'charlie.wav', 'zulu.mp3']
    by_artist = rows(run(tool, 'order', music, SORT_ARTIST))
    assert by_artist[0][:2] == ['zulu.mp3', 'Aardvark']                     # songs without an artist last
    assert [r[0] for r in by_artist[1:]] == ['Alpha.wav', 'bravo.wav', 'charlie.wav']
    assert [r[0] for r in rows(run(tool, 'order', music, SORT_ADDED))] == ['zulu.mp3', 'bravo.wav', 'Alpha.wav', 'charlie.wav']
    # a song added later: at the end of the own order, newest by date added
    write_wav(music / 'aaa.wav', 0.1)
    own = rows(run(tool, 'order', music))
    assert [r[0] for r in own] == ['Alpha.wav', 'bravo.wav', 'charlie.wav', 'zulu.mp3', 'aaa.wav']
    assert int(own[-1][2]) > 1700000500
    assert rows(run(tool, 'order', music, SORT_ADDED))[0][0] == 'aaa.wav'


def test_own_order_moves_are_saved_and_survive_changes(tool, tmp_path):
    music = tmp_path / 'music'
    (music / 'sub').mkdir(parents=True)
    for name in ['a.wav', 'b.wav', 'c.wav', 'sub/d.wav']:
        write_wav(music / name, 0.1)
    assert [r[0] for r in rows(run(tool, 'order', music))] == ['a.wav', 'b.wav', 'c.wav', 'sub/d.wav']
    assert run(tool, 'move', music, SORT_CUSTOM, 'c.wav', -2).split() == ['2', '0']
    assert run(tool, 'move', music, SORT_CUSTOM, 'a.wav', 5).split() == ['1', '3']     # clamped at the end
    assert [r[0] for r in rows(run(tool, 'order', music))] == ['c.wav', 'b.wav', 'sub/d.wav', 'a.wav']
    # a move in a sorted list (newest added: here all equal, so the own order) makes that order the own one
    (music / 'b.wav').unlink()
    write_wav(music / 'e.wav', 0.1)
    assert [r[0] for r in rows(run(tool, 'order', music))] == ['c.wav', 'sub/d.wav', 'a.wav', 'e.wav']
    run(tool, 'move', music, SORT_NAME, 'e.wav', -1)                        # by name: a c sub/d e -> a c e sub/d
    assert [r[0] for r in rows(run(tool, 'order', music))] == ['a.wav', 'c.wav', 'e.wav', 'sub/d.wav']
    # the file is plain text the player may edit; unknown and repeated lines do no harm
    text = (music / 'order.txt').read_text(encoding='utf-8')
    (music / 'order.txt').write_text(text + '5\tgone.wav\nbad line\n1\tsub/d.wav\n', encoding='utf-8')
    assert [r[0] for r in rows(run(tool, 'order', music))] == ['a.wav', 'c.wav', 'e.wav', 'sub/d.wav']
    assert 'gone.wav' not in (music / 'order.txt').read_text(encoding='utf-8')
    # an empty or missing folder does not wipe the order
    saved = (music / 'order.txt').read_text(encoding='utf-8')
    for name in ['a.wav', 'c.wav', 'e.wav', 'sub/d.wav']:
        (music / name).rename(tmp_path / name.replace('/', '_'))
    assert run(tool, 'order', music).strip() == ''
    assert (music / 'order.txt').read_text(encoding='utf-8') == saved


def test_playlist_continues_after_the_current_song_in_the_new_order(tool):
    # in order, no game turns: after song 2 come 3, then 0 1 2 3 again
    assert run(tool, 'continue', 4, 0, 0, 2, 6).split() == ['3', '0', '1', '2', '3', '0']
    # in order, mix: the game's turn after song 1, then song 2
    assert run(tool, 'continue', 3, 3, 0, 1, 3).split() == ['g1', '2', 'g2']
    # unknown song (none played yet): from the top
    assert run(tool, 'continue', 3, 0, 0, 3, 3).split() == ['0', '1', '2']
    # shuffled: a fresh round that does not start with the current song
    for seed_song in range(3):
        items = run(tool, 'continue', 3, 0, 1, seed_song, 3).split()
        assert sorted(items) == ['0', '1', '2'] and items[0] != str(seed_song)

"""Songs: the SODB row writer in src/hooks/pc_songs.c (docs/research/custom-songs.md).

A small database is built here in the game's layout, the C writer adds rows through a native
driver, and a reader written here checks the result: retail records and their name-index ranks
unchanged (profiles save rank-derived handles), added rows ranked last, readable fields, and a
second run a no-op. The same checks run on the user's own djv2.sod when the dump is available.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def name_hash(value):
    h = 5381
    for byte in value.encode('ascii') + b'\0':
        h = (h * 33 + byte) & 0xffffffff
    return h


# ---- a minimal SODB writer/reader for the test -------------------------------------------------

def build_database(songs, extra_tables=('Venue',)):
    """songs: list of dicts with name/Album/Artist/Entity/Unknown/Title. Records are 6 words."""
    out = bytearray(b'SODB' + struct.pack('<5I', 2, 0x1234, 0, 0, 0))
    out += b'\0' * 4
    strings = []

    def intern(s):
        if s not in strings:
            strings.append(s)
        return strings.index(s)

    def emit(blob):
        while len(out) % 4:
            out.append(0)
        at = len(out)
        out.extend(blob)
        return at

    def words(*v):
        return struct.pack(f'<{len(v)}I', *v)

    song = name_hash('Song')
    fields = {None: (0, 0x02000000), 'Album': (1, 1), 'Artist': (2, 1), 'Entity': (3, 1), 'Unknown': (4, 0),
              'Title': (5, 1)}
    records = []
    for s in songs:
        rec = [intern(s['name']), intern(s['Album']), intern(s['Artist']), intern(s['Entity']), s['Unknown'],
               intern(s['Title'])]
        records.append(emit(words(*rec)))
    tables = []
    for t in ('Song',) + tuple(extra_tables):
        th = name_hash(t)
        recs = records if t == 'Song' else [emit(words(intern(t + '-row'), 0, 0, 0, 0, 0))]
        header = len(out) + (-len(out) % 4)
        # header: tag, group0, group1, count; group0 inline at header+16 (one member: row name)
        group0 = header + 16
        group1 = group0 + 16
        sub = group1 + 8
        pairs = sorted((name_hash(strings[struct.unpack_from('<I', out, r)[0]]), r) for r in recs)
        blob = words(th, group0, group1, len(recs))
        blob += words(1, 1, th, sub)
        blob += words(0, 0)
        blob += words(th, len(recs), 1, *[k for k, _ in pairs], *[r for _, r in pairs])
        assert emit(blob) == header
        tables.append((th, header))
    tables.sort()
    data_at = emit(b'DATA' + words(len(tables), 1, *[k for k, _ in tables], *[v for _, v in tables]))
    schema = sorted(((song ^ name_hash(f)) if f else song, v) for f, v in fields.items())
    schema_at = emit(b'SKMA' + words(len(schema), 2, *[k for k, _ in schema], *[w for _, v in schema for w in v]))
    ptrs = [emit(s.encode('ascii') + b'\0') for s in strings]
    strt_at = emit(b'STRT' + words(len(strings), *[w for s, p in zip(strings, ptrs) for w in (name_hash(s), p)]))
    struct.pack_into('<3I', out, 12, schema_at, data_at, strt_at)
    return bytes(out)


class Db:
    def __init__(self, data):
        self.d = data
        w = lambda o: struct.unpack_from('<I', data, o)[0]
        self.w = w
        assert data[:4] == b'SODB'
        strt = w(20)
        assert data[strt:strt + 4] == b'STRT'
        n = w(strt + 4)
        self.strings = []
        for i in range(n):
            h, p = w(strt + 8 + i * 8), w(strt + 12 + i * 8)
            if not p:   # the retail table has empty slots
                self.strings.append(None)
                continue
            s = data[p:data.index(b'\0', p)].decode('ascii')
            assert name_hash(s) == h
            self.strings.append(s)
        tables = w(16)
        count, stride = w(tables + 4), w(tables + 8)
        keys = [w(tables + 12 + i * 4) for i in range(count)]
        self.headers = dict(zip(keys, [w(tables + 12 + count * 4 + i * 4) for i in range(count)]))

    def name_index(self, table):
        th = name_hash(table)
        header = self.headers[th]
        assert self.w(header) == th
        count = self.w(header + 12)
        group0 = self.w(header + 4)
        assert group0 == header + 16
        members = self.w(group0)
        sub = self.w(group0 + 8 + members * 4)
        assert self.w(sub + 4) == count and self.w(sub + 8) == 1
        keys = [self.w(sub + 12 + i * 4) for i in range(count)]
        recs = [self.w(sub + 12 + count * 4 + i * 4) for i in range(count)]
        assert keys == sorted(keys)
        return keys, recs

    def row(self, rec):
        return [self.w(rec + i * 4) for i in range(6)]


# ---- native driver -----------------------------------------------------------------------------

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include "pc_songs.h"
int main(int argc, char **argv)
{
    /* driver in.sod out.sod [entity title artist album]... */
    static const char *const fields[] = { 0, "Title", "Artist", "Album", "Entity" };
    const char *values[5 * 64];
    int n = 0, added = 0;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *in = malloc(len);
    if (fread(in, 1, len, f) != (size_t)len) return 2;
    fclose(f);
    for (int a = 3; a + 3 < argc && n < 64; a += 4, n++) {
        values[n * 5 + 0] = argv[a + 1];
        values[n * 5 + 1] = argv[a + 1];
        values[n * 5 + 2] = argv[a + 2];
        values[n * 5 + 3] = argv[a + 3];
        values[n * 5 + 4] = argv[a];
    }
    unsigned char *out; size_t out_n;
    if (!pc_songs_sod_add_rows(in, len, "Song", fields, 5, values, n, &out, &out_n, &added)) { puts("FAIL"); return 1; }
    f = fopen(argv[2], "wb");
    fwrite(out, 1, out_n, f);
    fclose(f);
    printf("added %d\n", added);
    return 0;
}
'''


@pytest.fixture(scope='module')
def writer(tmp_path_factory):
    if not (ROOT / 'tools/xboxrecomp/src/settings/recomp_settings.c').is_file():
        pytest.skip('native songs fixture needs the initialized toolkit submodule')
    compiler = shutil.which('cl') if os.name == 'nt' else shutil.which('cc')
    if not compiler:
        pytest.skip('native songs fixture needs a C compiler')
    folder = tmp_path_factory.mktemp('songs-native')
    if os.name == 'nt':
        host = '#include <windows.h>\n#define HOST_SEP "\\\\"\n'
    else:
        host = ('typedef int BOOL;\ntypedef unsigned int DWORD;\n#define TRUE 1\n#define FALSE 0\n'
                '#define HOST_SEP "/"\n')
    (folder / 'host.h').write_text(host)
    stub = folder / 'stub.c'
    stub.write_text('#include "host.h"\n'
                    '#if defined(_WIN32)\ntypedef WCHAR xbox_host_char;\n#else\ntypedef char xbox_host_char;\n#endif\n'
                    'BOOL (*g_xbox_game_file_redirect)(const char *, xbox_host_char *, DWORD);\n'
                    'void song_cursor_register(const char *id, const char *dir, int interactive) {}\n'
                    'void song_cursor_clear(void) {}\n')
    driver = folder / 'driver.c'
    driver.write_text(DRIVER)
    exe = folder / ('songs.exe' if os.name == 'nt' else 'songs')
    includes = [folder, ROOT / 'src/hooks', ROOT / 'tools/xboxrecomp/src/settings']
    sources = [driver, stub, ROOT / 'src/hooks/pc_songs.c', ROOT / 'src/hooks/pc_songs_settings.c',
               ROOT / 'src/hooks/song_library.c', ROOT / 'src/hooks/song_convert.c', ROOT / 'src/hooks/song_decoders.c',
               ROOT / 'tools/xboxrecomp/src/settings/recomp_settings.c']
    if os.name == 'nt':
        cmd = [compiler, '/nologo', '/O2', '/TC', *['/I' + str(p) for p in includes], *map(str, sources),
               '/Fe:' + str(exe)]
    else:
        cmd = [compiler, '-O2', '-Wall', *['-I' + str(p) for p in includes], *map(str, sources), '-o', str(exe),
               '-pthread', '-lm']
    result = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


HOLLER = ['holler', 'Holler If Ya Hear Me', '2Pac', 'Strictly 4 My N.I.G.G.A.Z.']


def add(writer, tmp, data, *songs):
    src, dst = tmp / 'in.sod', tmp / 'out.sod'
    src.write_bytes(data)
    args = [a for s in songs for a in s]
    result = subprocess.run([str(writer), str(src), str(dst), *args], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return dst.read_bytes(), int(result.stdout.split()[1])


def check_added(before, after, entities):
    old, new = Db(before), Db(after)
    old_keys, old_recs = old.name_index('Song')
    new_keys, new_recs = new.name_index('Song')
    # Retail records keep their bytes and their ranks; added rows take the ranks after them.
    assert new_recs[:len(old_recs)] == old_recs
    for rec in old_recs:
        assert after[rec:rec + 24] == before[rec:rec + 24]
    added = new_recs[len(old_recs):]
    assert sorted(new.strings[new.row(r)[3]] for r in added) == sorted(entities)
    # Other tables are untouched.
    for th, header in old.headers.items():
        if th != name_hash('Song'):
            assert new.headers[th] == header
    return new, added


def songs(n):
    names = ['Make It Hurt', 'Bust', 'Walk with Me', "Let's Get Dirty", 'Zzz Last', 'Aaa First'][:n]
    return [dict(name=x, Album='Album ' + x, Artist='Artist ' + x, Entity='e' + str(i), Unknown=i, Title=x)
            for i, x in enumerate(names)]


def test_added_song_is_last_and_retail_ranks_hold(writer, tmp_path):
    before = build_database(songs(6))
    after, count = add(writer, tmp_path, before, HOLLER)
    assert count == 1
    new, added = check_added(before, after, ['holler'])
    row = new.row(added[0])
    title, artist, album = (new.strings[row[i]] for i in (5, 2, 1))
    assert (title, artist, album) == ('Holler If Ya Hear Me', '2Pac', 'Strictly 4 My N.I.G.G.A.Z.')
    assert new.strings[row[0]].startswith('Holler If Ya Hear Me')
    # The unnamed integer field is copied from the template (the first row in name-index order).
    first = Db(before).name_index('Song')[1][0]
    assert row[4] == Db(before).row(first)[4]


def test_second_run_is_a_no_op(writer, tmp_path):
    once, _ = add(writer, tmp_path, build_database(songs(4)), HOLLER)
    twice, count = add(writer, tmp_path, once, HOLLER)
    assert count == 0 and twice == once


def test_several_songs_rank_after_retail(writer, tmp_path):
    before = build_database(songs(5))
    extra = [['a1', 'Alpha', 'X', 'Y'], ['zz', 'Zulu', 'X', 'Y'], ['m1', 'Mike', 'X', 'Y']]
    after, count = add(writer, tmp_path, before, *extra)
    assert count == 3
    check_added(before, after, ['a1', 'zz', 'm1'])


def test_a_full_store_of_added_songs(writer, tmp_path):
    # 32 retail-like rows + 32 added: every added row still ranks after every retail row.
    names = [f'Retail {i:02d}' for i in range(32)]
    before = build_database([dict(name=x, Album='A', Artist='B', Entity=f'r{i}', Unknown=0, Title=x)
                             for i, x in enumerate(names)])
    extra = [[f'c{i:02d}', f'Custom {i}', 'X', 'Y'] for i in range(32)]
    after, count = add(writer, tmp_path, before, *extra)
    assert count == 32
    check_added(before, after, [e[0] for e in extra])


def test_users_own_database(writer, tmp_path):
    data = os.environ.get('DEFJAM_DATA') or str(ROOT.parent / 'defjam')
    sod = Path(data) / 'extracted/assets/djv2.sod'
    if not sod.is_file():
        pytest.skip('needs the user\'s own dump')
    before = sod.read_bytes()
    after, count = add(writer, tmp_path, before, HOLLER)
    assert count == 1
    new, added = check_added(before, after, ['holler'])
    assert len(new.name_index('Song')[1]) == 33

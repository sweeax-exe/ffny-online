"""Cloud saves underneath (src/hooks/cloud_saves.cpp, build 91), compiled on its own with save_manager.cpp and
fed synthetic profiles only: what goes up (only the save area's save files), the archive and its content hash
(the same definition as the account service, server relay/accounts/cloudsaves.go), the check before a restore,
the restore into an empty and into a used save area (always through a backup), what a restore changes, the
service's list, the sync state, and the decision at a launcher start (upload, skip, restore, conflict)."""
from pathlib import Path
import hashlib
import shutil
import struct
import subprocess
import time
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include "cloud_saves.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace cloud;
static Bytes slurp(const char *p) {
    std::ifstream f(fs::u8path(p), std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(f), {});
}
static std::string arg(const char *s) { return std::string(s) == "-" ? "" : s; }
int main(int argc, char **argv) {
    std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "sha") { std::cout << sha256_hex((const uint8_t *)argv[2], strlen(argv[2])) << "\n"; return 0; }
    if (cmd == "collect" || cmd == "pack") {
        Files files; std::vector<std::string> skipped; std::string error;
        bool ok = collect(fs::u8path(argv[2]), files, &skipped, &error);
        std::cout << "ok=" << ok << "\nerror=" << error << "\n";
        for (auto &s : skipped) std::cout << "skipped " << s << "\n";
        for (auto &kv : files) std::cout << "file " << kv.first << " " << kv.second.size() << "\n";
        std::cout << "profiles=" << count_profiles(files) << "\ngame=" << count_game_profiles(files) << "\n";
        std::cout << "hash=" << (files.empty() ? "" : content_hash(files)) << "\n";
        if (cmd == "pack" && ok) {
            Bytes z = pack(files);
            std::ofstream(fs::u8path(argv[3]), std::ios::binary).write((const char *)z.data(), z.size());
        }
        return ok ? 0 : 1;
    }
    if (cmd == "unpack") {
        Files files; std::string error;
        bool ok = unpack(slurp(argv[2]), files, &error);
        std::cout << "ok=" << ok << "\nerror=" << error << "\n";
        if (ok) std::cout << "hash=" << content_hash(files) << "\nfiles=" << files.size() << "\n";
        return 0;
    }
    if (cmd == "decide") {
        std::cout << decision_name(decide(arg(argv[2]), atoi(argv[3]) != 0, arg(argv[4]), arg(argv[5]))) << "\n";
        return 0;
    }
    if (cmd == "compare") {
        Files here, there; std::string error;
        collect(fs::u8path(argv[2]), here, nullptr, &error);
        if (!unpack(slurp(argv[3]), there, &error)) { std::cout << "error " << error << "\n"; return 1; }
        for (auto &c : compare(here, there)) std::cout << c.what << "|" << c.name << "|" << c.aside << "\n";
        return 0;
    }
    if (cmd == "restore" || cmd == "keep") {
        Files there; std::string error;
        if (!unpack(slurp(argv[2]), there, &error)) { std::cout << "error " << error << "\n"; return 1; }
        if (cmd == "keep") {
            std::string made;
            bool ok = keep_as_backup(there, fs::u8path(argv[3]), atoll(argv[4]), "test", &made, &error);
            std::cout << ok << " " << made << " " << error << "\n";
            return ok ? 0 : 1;
        }
        std::string message;
        bool ok = restore(there, fs::u8path(argv[3]), fs::u8path(argv[4]), atoll(argv[5]), "17 Nov (PC-A)", &message);
        std::cout << ok << " " << message << "\n";
        return ok ? 0 : 1;
    }
    if (cmd == "listing") {
        std::ifstream f(fs::u8path(argv[2]));
        std::string text((std::istreambuf_iterator<char>(f)), {});
        Listing l = parse_listing(text);
        std::cout << "ok=" << l.ok << " account=" << l.account << " n=" << l.versions.size() << "\n";
        for (auto &v : l.versions)
            std::cout << v.id << "|" << v.hash << "|" << v.size << "|" << v.bytes << "|" << v.files << "|" << v.profiles
                      << "|" << v.uploaded << "|" << v.pc << "\n";
        std::cout << "status=" << text_field(text, "status") << "\n";
        return 0;
    }
    if (cmd == "state") {
        SyncState s; s.account = argv[3]; s.hash = arg(argv[4]); s.version = argv[5]; s.when = 1763380800;
        bool ok = write_state(fs::u8path(argv[2]), s);
        SyncState r = read_state(fs::u8path(argv[2]));
        std::cout << ok << " " << r.account << " " << r.hash << " " << r.version << " " << r.when << "\n";
        return 0;
    }
    return 2;
}
'''


@pytest.fixture(scope='module')
def tool(tmp_path_factory):
    folder = tmp_path_factory.mktemp('cloud-saves-native')
    (folder / 'driver.cpp').write_text(DRIVER, encoding='utf-8')
    sources = [ROOT / 'src/hooks/cloud_saves.cpp', ROOT / 'src/hooks/save_manager.cpp']
    if not (ROOT / 'tools/xboxrecomp/src/nv2a/stb/stb_image_write.h').is_file():
        pytest.skip('the toolkit (tools/xboxrecomp) is not checked out')
    include = ROOT / 'src/hooks'
    compiler = shutil.which('cl')
    if compiler:
        exe = folder / 'cloud_saves.exe'
        cmd = [compiler, '/nologo', '/std:c++17', '/EHsc', '/O2', '/W3', '/I' + str(include), str(folder / 'driver.cpp'),
               *map(str, sources), '/Fe:' + str(exe)]
    else:
        compiler = shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            pytest.skip('C++ compiler required')
        exe = folder / 'cloud_saves'
        cmd = [compiler, '-std=c++17', '-O2', '-I' + str(include), str(folder / 'driver.cpp'), *map(str, sources), '-o', str(exe)]
    result = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


def run(tool, *args, check=True):
    result = subprocess.run([str(tool), *map(str, args)], capture_output=True, text=True)
    if check:
        assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def fields(text):
    return dict(l.split('=', 1) for l in text.splitlines() if '=' in l and ' ' not in l.split('=', 1)[0])


def profile(seed):
    """A synthetic 0x5990-byte profile file: the 20CM header and made-up bytes."""
    body = bytearray(hashlib.shake_256(seed.encode()).digest(0x5990))
    body[0:4] = b'20CM'
    return bytes(body)


def meta(name):
    return b'\xff\xfe' + ('Name=' + name + '\r\n').encode('utf-16-le')


def folder_of(name):
    return hashlib.sha1(name.encode()).hexdigest()[:12].upper()


def put_profile(base, name, seed=None):
    folder = base / folder_of(name)
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'SaveMeta.xbx').write_bytes(meta(name))
    (folder / name).write_bytes(profile(seed or name))
    (folder / 'saveimage.xbx').write_bytes(b'XPR0' + bytes(4092))
    return folder


def save_area(root, names=('VY2', 'KID'), aside=(), options=True):
    """root/save with UserData/45410049 (title files, profiles, Options) and aside, plus junk the cloud leaves out."""
    save = root / 'save'
    title = save / 'UserData' / '45410049'
    title.mkdir(parents=True)
    (title / 'TitleMeta.xbx').write_bytes(meta('Def Jam'))
    (title / 'TitleImage.xbx').write_bytes(b'XPR0' + bytes(10236))
    for n in names:
        put_profile(title, n)
    if options:
        o = title / folder_of('Options')
        o.mkdir()
        (o / 'SaveMeta.xbx').write_bytes(meta('Options'))
        (o / 'Options').write_bytes(b'20CM' + bytes(5400))
    for n in aside:
        put_profile(save / 'aside', n)
    return save


def py_hash(files):
    """The content hash as relay/accounts/cloudsaves.go cloudSaveHash defines it."""
    h = hashlib.sha256()
    for name in sorted(files, key=lambda n: n.encode()):
        h.update(name.encode() + b'\0' + struct.pack('<Q', len(files[name])) + files[name])
    return h.hexdigest()


def tree(root):
    return {p.relative_to(root).as_posix(): p.read_bytes() for p in sorted(root.rglob('*')) if p.is_file()}


def write_zip(path, files, method=zipfile.ZIP_DEFLATED):
    with zipfile.ZipFile(path, 'w', method) as zf:
        for n, d in files.items():
            zf.writestr(n, d)


def backups(folder):
    out = []
    for d in sorted(folder.iterdir(), reverse=True) if folder.is_dir() else []:
        if (d / 'backup.txt').is_file():
            info = dict(l.split('=', 1) for l in (d / 'backup.txt').read_text().splitlines() if '=' in l)
            out.append((d.name, info.get('reason'), d))
    return out


def test_sha256(tool):
    for text in ('abc', 'FFNY Online cloud saves ' * 9):
        assert run(tool, 'sha', text).strip() == hashlib.sha256(text.encode()).hexdigest()


HASH_VECTOR = 'e8f5a3a6e7ccda31ea5457dc8d1517b61d8c6799ae43ec9fd66cb265d822e834'


def test_hash_matches_the_service(tool, tmp_path):
    # the same files and constant as server relay/accounts/cloudsaves_test.go TestCloudSaveHashVector
    files = {'UserData/45410049/A/SaveMeta.xbx': b'\xff\xfeN\x00', 'aside/B/P': b'20CMxyz'}
    assert py_hash(files) == HASH_VECTOR
    write_zip(tmp_path / 'v.zip', files)
    assert fields(run(tool, 'unpack', tmp_path / 'v.zip'))['hash'] == HASH_VECTOR


def test_collect_takes_only_save_files(tool, tmp_path):
    save = save_area(tmp_path, aside=['OLDIE'])
    (save / 'Partition1.img').write_bytes(b'\0' * 4096)                      # the game's disk images: never
    title = save / 'UserData' / '45410049'
    (title / folder_of('VY2') / 'desktop.ini').write_text('[x]')            # not a save file: left out
    (title / folder_of('VY2') / 'deep').mkdir()                              # a folder inside a profile
    (title / folder_of('KID') / 'huge.xbx').write_bytes(b'\xff\xfe' + bytes(70000))
    (save / 'aside' / '.moving-X').mkdir()                                   # put_aside's own copy
    (save / 'aside' / '.moving-X' / 'X').write_bytes(profile('X'))
    (save / 'UserData' / 'other.txt').write_text('x')                         # beside the title: not looked at
    out = run(tool, 'collect', save)
    f = fields(out)
    names = [l.split()[1] for l in out.splitlines() if l.startswith('file ')]
    assert f['ok'] == '1' and f['profiles'] == '3' and f['game'] == '2', out
    assert not any('Partition' in n or 'desktop.ini' in n or 'huge' in n or '.moving' in n or 'other' in n for n in names)
    assert 'aside/%s/OLDIE' % folder_of('OLDIE') in names
    assert out.count('skipped ') == 3, out
    want = {p.relative_to(save).as_posix(): p.read_bytes() for p in save.rglob('*') if p.is_file()
            and p.relative_to(save).as_posix() in names}
    assert len(want) == len(names) and f['hash'] == py_hash(want)


def test_pack_is_a_zip_with_the_same_hash(tool, tmp_path):
    save = save_area(tmp_path, aside=['OLDIE'])
    out = run(tool, 'pack', save, tmp_path / 'a.zip')
    with zipfile.ZipFile(tmp_path / 'a.zip') as zf:
        assert zf.testzip() is None
        files = {i.filename: zf.read(i) for i in zf.infolist()}
        assert any(i.compress_type == zipfile.ZIP_DEFLATED for i in zf.infolist())
    assert fields(out)['hash'] == py_hash(files)
    # the same saves give the same archive, byte for byte
    run(tool, 'pack', save, tmp_path / 'b.zip')
    assert (tmp_path / 'a.zip').read_bytes() == (tmp_path / 'b.zip').read_bytes()
    # and it reads back with the same hash
    assert fields(run(tool, 'unpack', tmp_path / 'a.zip')) == {'ok': '1', 'error': '', 'hash': py_hash(files),
                                                               'files': str(len(files))}


def test_unpack_checks_everything(tool, tmp_path):
    good = {'UserData/45410049/TitleMeta.xbx': meta('Def Jam'), 'UserData/45410049/ABC/SaveMeta.xbx': meta('VY2'),
            'UserData/45410049/ABC/VY2': profile('VY2'), 'aside/DEF/KID': profile('KID')}
    write_zip(tmp_path / 'good.zip', good)
    assert fields(run(tool, 'unpack', tmp_path / 'good.zip'))['hash'] == py_hash(good)
    write_zip(tmp_path / 'stored.zip', good, zipfile.ZIP_STORED)
    assert fields(run(tool, 'unpack', tmp_path / 'stored.zip'))['hash'] == py_hash(good)
    bad = {
        'traversal': {'UserData/45410049/../../evil/VY2': profile('x')},
        'absolute': {'/etc/VY2': profile('x')},
        'outside': {'Partition1.img': profile('x')},
        'too deep': {'UserData/45410049/A/B/VY2': profile('x')},
        'not a save file': {'UserData/45410049/ABC/run.exe': b'MZ\x90\0'},
        'fake xbx': {'UserData/45410049/ABC/x.xbx': b'MZ'},
        'too large': {'UserData/45410049/ABC/VY3': b'20CM' + bytes(70000)},
        'dot end': {'UserData/45410049/ABC/VY3.': profile('x')},
        'twice': {'UserData/45410049/abc/VY2': profile('VY2')},
    }
    for why, extra in bad.items():
        write_zip(tmp_path / 'bad.zip', {**good, **extra})
        f = fields(run(tool, 'unpack', tmp_path / 'bad.zip'))
        assert f['ok'] == '0' and f['error'].startswith('The cloud save was not taken'), (why, f)
    write_zip(tmp_path / 'none.zip', {'UserData/45410049/TitleMeta.xbx': meta('Def Jam')})
    assert 'no profile' in fields(run(tool, 'unpack', tmp_path / 'none.zip'))['error']
    # a damaged member (its CRC) and something that is not a zip at all
    raw = bytearray((tmp_path / 'stored.zip').read_bytes())
    at = raw.find(b'20CM')
    raw[at + 100] ^= 0xFF
    (tmp_path / 'crc.zip').write_bytes(bytes(raw))
    assert 'damaged' in fields(run(tool, 'unpack', tmp_path / 'crc.zip'))['error']
    (tmp_path / 'junk.zip').write_bytes(b'not a zip' * 10)
    assert fields(run(tool, 'unpack', tmp_path / 'junk.zip'))['ok'] == '0'
    (tmp_path / 'big.zip').write_bytes(b'\0' * (4 * 1024 * 1024 + 1))
    assert fields(run(tool, 'unpack', tmp_path / 'big.zip'))['ok'] == '0'


@pytest.mark.parametrize('local,profiles,base,cloud,want', [
    ('L', 1, '-', '-', 'upload'),            # nothing in the cloud yet
    ('-', 0, '-', '-', 'nothing'),           # nothing anywhere
    ('L', 0, '-', '-', 'nothing'),           # only title files / Options: never sent
    ('L', 1, 'L', 'L', 'in-sync'),
    ('L', 1, '-', 'L', 'in-sync'),           # a new PC with the same saves
    ('L2', 1, 'L', 'L', 'upload'),           # only this PC changed
    ('L', 1, 'L', 'C', 'cloud-newer'),       # only the cloud changed: ask, never restore silently
    ('L2', 1, 'L', 'C', 'conflict'),         # both changed: ask, keep both
    ('L', 1, '-', 'C', 'conflict'),          # never synced and they differ
    ('-', 0, '-', 'C', 'offer-restore'),     # a new PC: offer the cloud
    ('-', 0, 'L', 'C', 'offer-restore'),     # this PC emptied, the cloud moved on
    ('-', 0, 'C', 'C', 'nothing'),           # "Not now" taken: not asked again
    ('L', 1, 'C', 'C', 'upload'),            # after "Not now": this PC's first saves go up
])
def test_decide(tool, local, profiles, base, cloud, want):
    h = lambda s: '-' if s == '-' else hashlib.sha256(s.encode()).hexdigest()
    assert run(tool, 'decide', h(local), profiles, h(base), h(cloud)).strip() == want


def test_restore_into_an_empty_data_root(tool, tmp_path):
    a = save_area(tmp_path / 'A', aside=['OLDIE'])
    run(tool, 'pack', a, tmp_path / 'cloud.zip')
    b = tmp_path / 'B' / 'save'
    b.mkdir(parents=True)
    out = run(tool, 'restore', tmp_path / 'cloud.zip', b, tmp_path / 'B' / 'save-backups', int(time.time()))
    assert out.startswith('1 Your cloud save from 17 Nov (PC-A) is in place. 1 profile put aside came with it.'), out
    assert tree(b / 'UserData') == tree(a / 'UserData') and tree(b / 'aside') == tree(a / 'aside')
    made = backups(tmp_path / 'B' / 'save-backups')
    assert [r for _, r, _ in made] == ['cloud'], made           # nothing to back up here, the cloud copy is kept
    assert fields(run(tool, 'collect', b))['hash'] == fields(run(tool, 'collect', a))['hash']


def test_restore_over_saves_backs_up_first_and_keeps_both(tool, tmp_path):
    a = save_area(tmp_path / 'A', names=('VY2', 'KID'), aside=['OLDIE'])
    run(tool, 'pack', a, tmp_path / 'cloud.zip')
    b = save_area(tmp_path / 'B', names=('VY2', 'HERE'), aside=['MINE'])
    (b / 'UserData' / '45410049' / folder_of('VY2') / 'VY2').write_bytes(profile('VY2 on B'))
    before = tree(b / 'UserData')
    aside_before = tree(b / 'aside')
    # what it would change: KID new, VY2 replaced, HERE leaves the game, OLDIE comes aside, MINE stays
    changes = run(tool, 'compare', b, tmp_path / 'cloud.zip').splitlines()
    assert changes == ['3|HERE|0', '0|KID|0', '1|VY2|0', '2|Options|0', '2|MINE|1', '0|OLDIE|1'], changes
    out = run(tool, 'restore', tmp_path / 'cloud.zip', b, tmp_path / 'B' / 'save-backups', int(time.time()))
    assert out.startswith('1 '), out
    assert tree(b / 'UserData') == tree(a / 'UserData')
    after_aside = tree(b / 'aside')
    assert all(after_aside[k] == v for k, v in aside_before.items())          # this PC's put aside stays
    assert any(k.endswith('/OLDIE') for k in after_aside)                       # the cloud's comes too
    made = backups(tmp_path / 'B' / 'save-backups')
    reasons = [r for _, r, _ in made]
    assert reasons[:2] == ['restore', 'cloud'], reasons                         # backed up first, the cloud kept
    assert tree(made[0][2] / 'UserData') == before                              # the old saves are all there
    # restoring with the same aside folder present never overwrites it
    (b / 'aside' / folder_of('OLDIE') / 'OLDIE').write_bytes(profile('changed here'))
    run(tool, 'restore', tmp_path / 'cloud.zip', b, tmp_path / 'B' / 'save-backups', int(time.time()) + 2)
    assert (b / 'aside' / folder_of('OLDIE') / 'OLDIE').read_bytes() == profile('changed here')


def test_keep_as_backup(tool, tmp_path):
    a = save_area(tmp_path / 'A')
    run(tool, 'pack', a, tmp_path / 'cloud.zip')
    out = run(tool, 'keep', tmp_path / 'cloud.zip', tmp_path / 'bk', int(time.time())).split()
    made = backups(tmp_path / 'bk')
    assert out[0] == '1' and made[0][0] == out[1] and made[0][1] == 'cloud'
    assert tree(made[0][2] / 'UserData') == tree(a / 'UserData')
    assert 'profiles=2' in (made[0][2] / 'backup.txt').read_text()


def test_listing_and_state(tool, tmp_path):
    h1, h2 = 'a' * 64, 'b' * 64
    (tmp_path / 'list.txt').write_text('account=12\nkeep=5\nmax_size=4194304\nversions=2\n'
                                       'v=v9|%s|2347|45000|8|2|1763380800|DESKTOP-B12x\n'
                                       'v=v8|%s|2300|44000|8|2|1763370000|\n' % (h1, h2))
    out = run(tool, 'listing', tmp_path / 'list.txt').splitlines()
    assert out[0] == 'ok=1 account=12 n=2'
    assert out[1] == 'v9|%s|2347|45000|8|2|1763380800|DESKTOP-B12x' % h1 and out[2].endswith('|1763370000|')
    (tmp_path / 'bad.txt').write_text('error=not signed in\n')
    assert run(tool, 'listing', tmp_path / 'bad.txt').startswith('ok=0')
    (tmp_path / 'up.txt').write_text('status=conflict\nid=v9\n')
    assert 'status=conflict' in run(tool, 'listing', tmp_path / 'up.txt')
    assert run(tool, 'state', tmp_path / 'cloud-saves.txt', '12', h1, 'v9').strip() == '1 12 %s v9 1763380800' % h1
    assert run(tool, 'state', tmp_path / 'cloud-saves.txt', '12', '-', '').split()[:2] == ['1', '12']

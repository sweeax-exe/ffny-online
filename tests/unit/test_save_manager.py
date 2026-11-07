"""The launcher's Saves page underneath (src/hooks/save_manager.cpp), compiled on its own and fed
synthetic profiles only: the re-signing and the import against scripts/save-import.py, the backup
rotation, the daily rule and restore. No game data, no real save area."""
from pathlib import Path
import hashlib
import hmac
import importlib.util
import os
import shutil
import struct
import subprocess
import time
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('save_import', ROOT / 'scripts/save-import.py')
save_import = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(save_import)

DRIVER = r'''
#include "save_manager.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace saves;
static Bytes slurp(const char *p) {
    std::ifstream f(fs::u8path(p), std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(f), {});
}
int main(int argc, char **argv) {
    std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "resign") {
        Bytes in = slurp(argv[2]);
        Bytes out = resign(in, kPortKey);
        std::ofstream(fs::u8path(argv[3]), std::ios::binary).write((const char *)out.data(), out.size());
        std::cout << "fault=" << header_fault(in) << "\nbefore=" << signatures_ok(in, kPortKey)
                  << "\nafter=" << signatures_ok(out, kPortKey) << "\n";
        return 0;
    }
    if (cmd == "folder") { std::cout << folder_name(argv[2]) << "\n"; return 0; }
    if (cmd == "backup") {
        std::string made, error;
        bool ok = make_backup(fs::u8path(argv[2]), fs::u8path(argv[3]), argv[4], atoll(argv[5]), &made, &error,
                              argc > 6 ? argv[6] : "");
        std::cout << (ok ? "made " + made : "error " + error) << "\n";
        return ok ? 0 : 1;
    }
    if (cmd == "backups") {
        for (auto &b : list_backups(fs::u8path(argv[2])))
            std::cout << b.name << " " << b.reason << " " << b.when << " " << b.profiles << "\n";
        return 0;
    }
    if (cmd == "due") { std::cout << daily_due(fs::u8path(argv[2]), atoll(argv[3])) << "\n"; return 0; }
    if (cmd == "restore") {
        std::string message;
        bool ok = restore(fs::u8path(argv[2]), fs::u8path(argv[3]), argv[4], atoll(argv[5]), &message);
        std::cout << ok << " " << message << "\n";
        return ok ? 0 : 1;
    }
    if (cmd == "list") {
        for (auto &e : list_saves(fs::u8path(argv[2])))
            std::cout << e.kind << "|" << e.name << "|" << e.folder << "|" << e.signed_ok << "|" << e.created << "|"
                      << (e.changed > 0) << "\n";
        return 0;
    }
    if (cmd == "import") {
        ImportScan scan = scan_import(fs::u8path(argv[2]), fs::u8path(argv[3]));
        if (!scan.error.empty()) { std::cout << "error " << scan.error << "\n"; return 1; }
        for (auto &c : scan.found)
            std::cout << "found " << c.name << " " << c.folder << " exists=" << c.exists << " sign=" << c.needs_signing << "\n";
        for (auto &s : scan.skipped) std::cout << "skipped " << s << "\n";
        std::set<std::string> replace;
        for (int i = 6; i < argc; i++) replace.insert(argv[i]);
        ImportResult r = apply_import(scan, replace, fs::u8path(argv[3]), fs::u8path(argv[4]), atoll(argv[5]));
        std::cout << "result ok=" << r.ok << " imported=" << r.imported << " replaced=" << r.replaced
                  << " kept=" << r.kept << " backup=" << r.backup << "\nmessage " << r.message << "\n";
        return 0;
    }
    return 2;
}
'''


@pytest.fixture(scope='module')
def tool(tmp_path_factory):
    folder = tmp_path_factory.mktemp('save-manager-native')
    (folder / 'driver.cpp').write_text(DRIVER, encoding='utf-8')
    source = ROOT / 'src/hooks/save_manager.cpp'
    include = ROOT / 'src/hooks'
    compiler = shutil.which('cl')
    if compiler:
        exe = folder / 'save_manager.exe'
        cmd = [compiler, '/nologo', '/std:c++17', '/EHsc', '/O2', '/W3', '/I' + str(include),
               str(folder / 'driver.cpp'), str(source), '/Fe:' + str(exe)]
    else:
        compiler = shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            pytest.skip('C++ compiler required')
        exe = folder / 'save_manager'
        cmd = [compiler, '-std=c++17', '-O2', '-I' + str(include), str(folder / 'driver.cpp'), str(source), '-o', str(exe)]
    result = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


def run(tool, *args, check=True):
    result = subprocess.run([str(tool), *map(str, args)], capture_output=True, text=True)
    if check:
        assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def profile(seed, key=None, created=False):
    """A synthetic 0x5990-byte profile file: the 20CM header, random bytes, signed with `key`
    (a stand-in for another console's key) or not at all."""
    size = 0x5990
    body = bytearray(hashlib.shake_256(seed.encode()).digest(size))
    body[0:4] = b'20CM'
    body[4:16] = struct.pack('<III', size, 0x40, size - 0x8C)
    body[0x80 + 0x4D8A] = 1 if created else 0
    blob = bytes(body)
    return save_import.resign(blob, key) if key else blob


def meta(name):
    return b'\xff\xfe' + ('Name=' + name + '\r\n').encode('utf-16-le')


def put_profile(title_dir, name, blob, image=b'IMG'):
    folder = title_dir / save_import.folder_name(name)
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'SaveMeta.xbx').write_bytes(meta(name))
    (folder / name).write_bytes(blob)
    (folder / 'saveimage.xbx').write_bytes(image)
    return folder


def noon(days=0):
    t = time.localtime()
    base = time.mktime((t.tm_year, t.tm_mon, t.tm_mday, 12, 0, 0, 0, 0, -1))
    return int(base) + days * 86400


def tree(root):
    return {p.relative_to(root).as_posix(): p.read_bytes() for p in sorted(root.rglob('*')) if p.is_file()}


# ---- signing ----

def test_resign_matches_the_python_tool(tool, tmp_path):
    console_key = bytes(range(16))
    blob = profile('VY2', key=console_key)
    src, out = tmp_path / 'in', tmp_path / 'out'
    src.write_bytes(blob)
    text = run(tool, 'resign', src, out)
    assert 'fault=\n' in text and 'before=0' in text and 'after=1' in text
    assert out.read_bytes() == save_import.resign(blob, save_import.PORT_KEY)
    assert all(save_import.signatures_ok(out.read_bytes(), save_import.PORT_KEY))
    # only the three fields change
    changed = [i for i, (a, b) in enumerate(zip(blob, out.read_bytes())) if a != b]
    assert changed and min(changed) >= 0x10 and max(changed) < 0x4C


def test_hmac_matches_the_standard_library(tool, tmp_path):
    # a profile signed natively verifies with Python's hmac (an independent SHA-1)
    blob = profile('Other')
    src, out = tmp_path / 'in', tmp_path / 'out'
    src.write_bytes(blob)
    run(tool, 'resign', src, out)
    signed = out.read_bytes()
    assert signed[0x38:0x4C] == hmac.new(bytes(16), signed[:0x38], hashlib.sha1).digest()


@pytest.mark.parametrize('name', ['VY2', 'Alpha', 'Another profile', 'A' * 32, '~!@#'])
def test_folder_name_matches_the_python_tool(tool, name):
    assert run(tool, 'folder', name).strip() == save_import.folder_name(name)


# ---- backups ----

def test_backup_rotation_keeps_the_newest_ten(tool, tmp_path):
    save, backups = tmp_path / 'save', tmp_path / 'save-backups'
    title = save / 'UserData' / '45410049'
    put_profile(title, 'VY2', profile('VY2', key=bytes(16)))
    backups.mkdir()
    (backups / 'notes').mkdir()                       # not a backup: never touched
    (backups / 'readme.txt').write_text('mine')
    (backups / '2020-01-01').mkdir()
    made = []
    for i in range(12):
        line = run(tool, 'backup', save, backups, 'manual', noon(-20 + i))
        made.append(line.split()[1])
    names = [l.split()[0] for l in run(tool, 'backups', backups).splitlines()]
    assert names == list(reversed(made[2:]))
    assert len(names) == 10
    assert (backups / 'notes').is_dir() and (backups / 'readme.txt').read_text() == 'mine'
    assert (backups / '2020-01-01').is_dir()
    assert (backups / names[0] / 'UserData' / '45410049').is_dir()
    assert 'reason=manual' in (backups / names[0] / 'backup.txt').read_text()


def test_backup_rotation_spares_the_protected_one(tool, tmp_path):
    save, backups = tmp_path / 'save', tmp_path / 'save-backups'
    put_profile(save / 'UserData' / '45410049', 'VY2', profile('VY2', key=bytes(16)))
    oldest = run(tool, 'backup', save, backups, 'start', noon(-30)).split()[1]
    for i in range(10):
        run(tool, 'backup', save, backups, 'manual', noon(-20 + i), oldest)
    names = [l.split()[0] for l in run(tool, 'backups', backups).splitlines()]
    assert oldest in names and len(names) == 11


def test_backups_in_one_second_get_their_own_folders(tool, tmp_path):
    save, backups = tmp_path / 'save', tmp_path / 'save-backups'
    put_profile(save / 'UserData' / '45410049', 'VY2', profile('VY2', key=bytes(16)))
    a = run(tool, 'backup', save, backups, 'manual', noon()).split()[1]
    b = run(tool, 'backup', save, backups, 'manual', noon()).split()[1]
    assert b == a + '-2'
    assert [l.split()[0] for l in run(tool, 'backups', backups).splitlines()] == [b, a]


def test_nothing_to_back_up(tool, tmp_path):
    out = subprocess.run([str(tool), 'backup', str(tmp_path / 'save'), str(tmp_path / 'b'), 'start', str(noon())],
                         capture_output=True, text=True)
    assert out.returncode == 1 and 'no saves to back up yet' in out.stdout
    assert not (tmp_path / 'b').exists() or not any((tmp_path / 'b').iterdir())


def test_daily_backup_once_a_day(tool, tmp_path):
    save, backups = tmp_path / 'save', tmp_path / 'save-backups'
    put_profile(save / 'UserData' / '45410049', 'VY2', profile('VY2', key=bytes(16)))
    assert run(tool, 'due', backups, noon()).strip() == '1'
    run(tool, 'backup', save, backups, 'start', noon())
    assert run(tool, 'due', backups, noon() + 3600).strip() == '0'
    assert run(tool, 'due', backups, noon(1)).strip() == '1'


def test_restore_backs_up_the_current_state_first(tool, tmp_path):
    save, backups = tmp_path / 'save', tmp_path / 'save-backups'
    title = save / 'UserData' / '45410049'
    put_profile(title, 'VY2', profile('VY2', key=bytes(16)))
    before = tree(save / 'UserData')
    name = run(tool, 'backup', save, backups, 'manual', noon(-1)).split()[1]
    put_profile(title, 'VY3', profile('VY3', key=bytes(16)))
    (title / save_import.folder_name('VY2') / 'VY2').write_bytes(profile('VY2 changed', key=bytes(16)))
    after = tree(save / 'UserData')
    text = run(tool, 'restore', save, backups, name, noon())
    assert text.startswith('1 Backup restored from')
    assert tree(save / 'UserData') == before
    rows = run(tool, 'backups', backups).splitlines()
    assert rows[0].split()[1] == 'restore'
    assert tree(backups / rows[0].split()[0] / 'UserData') == after
    assert not (save / 'UserData.restoring').exists() and not (save / 'UserData.replaced').exists()


def test_restore_keeps_the_backup_it_restores_when_ten_exist(tool, tmp_path):
    save, backups = tmp_path / 'save', tmp_path / 'save-backups'
    put_profile(save / 'UserData' / '45410049', 'VY2', profile('VY2', key=bytes(16)))
    names = [run(tool, 'backup', save, backups, 'manual', noon(-20 + i)).split()[1] for i in range(10)]
    run(tool, 'restore', save, backups, names[0], noon())
    left = [l.split()[0] for l in run(tool, 'backups', backups).splitlines()]
    # the restored one stays (the ten newest plus it) until the next backup trims the list
    assert names[0] in left and len(left) == 11
    assert run(tool, 'backup', save, backups, 'manual', noon(1)).startswith('made')
    assert len(run(tool, 'backups', backups).splitlines()) == 10


# ---- listing ----

def test_list_saves(tool, tmp_path):
    save = tmp_path / 'save'
    title = save / 'UserData' / '45410049'
    put_profile(title, 'VY2', profile('VY2', key=bytes(16), created=True))
    put_profile(title, 'XBOX', profile('XBOX', key=bytes(range(16))))
    options = title / save_import.folder_name('Options')
    options.mkdir()
    (options / 'SaveMeta.xbx').write_bytes(meta('Options'))
    (options / 'Options').write_bytes(b'\0' * 5404)
    rows = run(tool, 'list', save).splitlines()
    assert rows == ['0|VY2|%s|1|1|1' % save_import.folder_name('VY2'),
                    '0|XBOX|%s|0|0|1' % save_import.folder_name('XBOX'),
                    '1|Options|%s|0|0|1' % save_import.folder_name('Options')]


# ---- import ----

def xbox_zip(path, profiles, extra=()):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as zf:
        zf.writestr('UDATA/45410049/TitleMeta.xbx', b'TITLE')
        for folder, name, blob in profiles:
            zf.writestr('UDATA/45410049/%s/SaveMeta.xbx' % folder, meta(name))
            zf.writestr('UDATA/45410049/%s/%s' % (folder, name), blob)
            zf.writestr('UDATA/45410049/%s/saveimage.xbx' % folder, b'IMAGE' * 50)
            zf.writestr('UDATA/45410049/%s/datelinfo.xbx' % folder, b'other tool')
        for name, data in extra:
            zf.writestr(name, data)


def test_import_zip_matches_the_python_tool(tool, tmp_path):
    console = bytes(range(16, 32))
    profiles = [('ABCDEF012345', 'VY2', profile('VY2', key=console)), ('OTHERNAME', 'Kid', profile('Kid', key=console))]
    src = tmp_path / 'saves.zip'
    xbox_zip(src, profiles, extra=[('../evil/SaveMeta.xbx', meta('Evil')), ('../evil/Evil', profile('Evil'))])
    native, python = tmp_path / 'native' / 'save', tmp_path / 'python' / 'save'
    text = run(tool, 'import', src, native, tmp_path / 'native' / 'save-backups', noon())
    assert 'result ok=1 imported=2 replaced=0 kept=0 backup=\n' in text
    assert 'message Imported 2 profiles: VY2 and Kid.' in text
    assert save_import.main([str(src), '--save-dir', str(python)]) == 0
    assert tree(native) == tree(python)
    assert not (tmp_path / 'evil').exists() and not (tmp_path / 'native' / 'evil').exists()
    for _, name, _ in profiles:
        blob = (native / 'UserData/45410049' / save_import.folder_name(name) / name).read_bytes()
        assert all(save_import.signatures_ok(blob, save_import.PORT_KEY))


def test_import_folder_matches_the_python_tool(tool, tmp_path):
    console = bytes(range(32, 48))
    src = tmp_path / 'E' / 'UDATA' / '45410049'
    put_profile(src, 'VY2', profile('VY2', key=console), image=b'X' * 4096)
    bad = src / 'BROKEN'
    bad.mkdir()
    (bad / 'SaveMeta.xbx').write_bytes(meta('Broken'))
    (bad / 'Broken').write_bytes(b'not a profile')
    opt = src / 'OPTIONS'
    opt.mkdir()
    (opt / 'SaveMeta.xbx').write_bytes(meta('Options'))
    (opt / 'Options').write_bytes(b'\0' * 5404)
    native, python = tmp_path / 'native' / 'save', tmp_path / 'python' / 'save'
    text = run(tool, 'import', tmp_path / 'E', native, tmp_path / 'native' / 'save-backups', noon())
    assert 'imported=1' in text and 'Skipped 2 folders that are not profiles.' in text
    save_import.main([str(tmp_path / 'E'), '--save-dir', str(python)])
    assert tree(native) == tree(python)


def test_import_never_overwrites_without_asking_and_backs_up_first(tool, tmp_path):
    save, backups = tmp_path / 'save', tmp_path / 'save-backups'
    title = save / 'UserData' / '45410049'
    mine = profile('VY2 mine', key=bytes(16))
    put_profile(title, 'VY2', mine)
    theirs = profile('VY2 theirs', key=bytes(range(16)))
    src = tmp_path / 'saves.zip'
    xbox_zip(src, [('X1', 'VY2', theirs), ('X2', 'NEW', profile('NEW', key=bytes(range(16))))])
    text = run(tool, 'import', src, save, backups, noon(-1))
    assert 'found VY2 %s exists=1' % save_import.folder_name('VY2') in text
    assert 'imported=1 replaced=0 kept=1' in text and 'Kept 1 profile you already had: VY2.' in text
    assert (title / save_import.folder_name('VY2') / 'VY2').read_bytes() == mine
    first = run(tool, 'backups', backups).splitlines()
    assert len(first) == 1 and first[0].split()[1] == 'import'
    # asked, and the player said replace
    text = run(tool, 'import', src, save, backups, noon(), 'VY2', 'NEW')
    assert 'imported=2 replaced=2' in text
    assert (title / save_import.folder_name('VY2') / 'VY2').read_bytes() == save_import.resign(theirs, bytes(16))
    newest = run(tool, 'backups', backups).splitlines()[0].split()[0]
    assert (backups / newest / 'UserData/45410049' / save_import.folder_name('VY2') / 'VY2').read_bytes() == mine


def test_import_of_nothing_says_so(tool, tmp_path):
    (tmp_path / 'empty').mkdir()
    out = subprocess.run([str(tool), 'import', str(tmp_path / 'empty'), str(tmp_path / 'save'), str(tmp_path / 'b'),
                          str(noon())], capture_output=True, text=True)
    assert out.returncode == 1 and 'No Xbox profiles in empty' in out.stdout
    (tmp_path / 'junk.zip').write_bytes(b'PK not really')
    out = subprocess.run([str(tool), 'import', str(tmp_path / 'junk.zip'), str(tmp_path / 'save'), str(tmp_path / 'b'),
                          str(noon())], capture_output=True, text=True)
    assert 'error Could not read junk.zip' in out.stdout


def test_stored_and_large_deflate_members(tool, tmp_path):
    """Both zip methods, and a dynamic-Huffman member with long matches (the profile itself is mostly
    random, so the deflated test above uses few back-references)."""
    blob = bytearray(profile('Repeat', key=bytes(range(16))))
    blob[0x100:0x5000] = (b'fight for ny ' * 2000)[:0x4F00]
    blob = bytes(blob)
    src = tmp_path / 'mixed.zip'
    with zipfile.ZipFile(src, 'w') as zf:
        zf.writestr(zipfile.ZipInfo('A/SaveMeta.xbx'), meta('Repeat'), compress_type=zipfile.ZIP_STORED)
        zf.writestr(zipfile.ZipInfo('A/Repeat'), blob, compress_type=zipfile.ZIP_DEFLATED)
    save = tmp_path / 'save'
    text = run(tool, 'import', src, save, tmp_path / 'b', noon())
    assert 'imported=1' in text
    got = (save / 'UserData/45410049' / save_import.folder_name('Repeat') / 'Repeat').read_bytes()
    assert got == save_import.resign(blob, bytes(16))


# ---- PS2 profiles (hK's converter, ported) ----

PS2_SIZE = 22888
PS2_IDENTITY = b'DJFFNY::User 17 -'


def ps2_profile(name, seed='ps2'):
    """A synthetic PS2 Story profile: the 20CM header with the PS2 sizes, random check bytes, the
    identity block at 28, the fighter's name at 92 and random bytes for the rest."""
    body = bytearray(hashlib.shake_256((seed + name).encode()).digest(PS2_SIZE))
    body[0:4] = b'20CM'
    body[4:16] = struct.pack('<III', PS2_SIZE, 64, PS2_SIZE - 28 - 64)
    body[28:92] = (PS2_IDENTITY + b'1234567890').ljust(64, b'\0')
    body[92:112] = name.encode('ascii').ljust(20, b'\0')
    return bytes(body)


def psu(dirname, files):
    """A .psu export: a folder entry, '.' and '..', then each file's 512-byte entry and its bytes
    padded to 1 KiB."""
    def entry(mode, size, name):
        e = bytearray(512)
        struct.pack_into('<HHI', e, 0, mode, 0, size)
        e[64:64 + len(name)] = name
        return bytes(e)
    out = entry(0x8427, len(files) + 2, dirname) + entry(0x8427, 0, b'.') + entry(0x8427, 0, b'..')
    for name, data in files:
        out += entry(0x8497, len(data), name) + data + bytes(-len(data) % 1024)
    return out


def expected_xbox(ps2):
    """The Xbox layout written out independently from the description in save_manager.h."""
    payload = ps2[92:]
    preview = bytes(2) + bytes((48, 48, 48, 255)) * 4863 + bytes.fromhex('01890081a008')
    identity = (PS2_IDENTITY + b'2030886228').ljust(64, b'\0')
    blob = (b'20CM' + struct.pack('<III', 0x5990, 64, 0x5990 - 140) + bytes(60) + identity + payload[:384]
            + preview + payload[19852:])
    assert len(blob) == 0x5990
    return save_import.resign(blob, bytes(16))


def test_ps2_psu_import(tool, tmp_path):
    blob = ps2_profile('SWEEAX')
    src = tmp_path / 'Def Jam.psu'
    src.write_bytes(psu(b'BESLES-52507DEFJAM', [(b'icon.sys', b'I' * 964), (b'defjams.ico', b'C' * 3000),
                                                   (b'BESLES-52507DEFJAM', blob)]))
    save = tmp_path / 'save'
    text = run(tool, 'import', src, save, tmp_path / 'b', noon())
    folder = save_import.folder_name('SWEEAX')
    assert 'found SWEEAX %s exists=0 sign=0' % folder in text
    assert 'result ok=1 imported=1' in text and 'message Imported 1 profile: SWEEAX.' in text
    out = save / 'UserData/45410049' / folder
    got = (out / 'SWEEAX').read_bytes()
    assert got == expected_xbox(blob)
    assert all(save_import.signatures_ok(got, save_import.PORT_KEY))
    assert got[0x38:0x4C] == hmac.new(bytes(16), got[:0x38], hashlib.sha1).digest()
    assert got[0x80 + 0x4D8A] == 1                       # the created-fighter flag
    assert got[0x8C:0x8C + 6] == b'SWEEAX'               # the fighter's name opens the profile
    assert (out / 'SaveMeta.xbx').read_bytes() == meta('SWEEAX')
    image = (out / 'saveimage.xbx').read_bytes()
    assert len(image) == 4096 and image[:4] == b'XPR0'
    assert run(tool, 'list', save).splitlines() == ['0|SWEEAX|%s|1|1|1' % folder]


def test_ps2_bare_profile_and_replace(tool, tmp_path):
    save, backups = tmp_path / 'save', tmp_path / 'b'
    mine = profile('AMP mine', key=bytes(16))
    put_profile(save / 'UserData' / '45410049', 'AMP', mine)
    blob = ps2_profile('AMP')
    src = tmp_path / 'BASLUS-21004DEFJAM'          # the profile file itself, as a card tool saves it
    src.write_bytes(blob)
    text = run(tool, 'import', src, save, backups, noon(-1))
    assert 'exists=1' in text and 'kept=1' in text
    assert (save / 'UserData/45410049' / save_import.folder_name('AMP') / 'AMP').read_bytes() == mine
    text = run(tool, 'import', src, save, backups, noon(), 'AMP')
    assert 'imported=1 replaced=1' in text
    assert (save / 'UserData/45410049' / save_import.folder_name('AMP') / 'AMP').read_bytes() == expected_xbox(blob)


@pytest.mark.parametrize('file,data,words', [
    ('Mcd001.ps2', b'Sony PS2 Memory Card Format 1.2.0.0' + bytes(5000), ['memory card (.ps2)', '.psu']),
    ('save.max', b'Ps2PowerSave' + bytes(500), ['MAX Drive', '.psu']),
    ('cut.psu', None, ['damaged']),
    ('notone.psu', b'\0' * 2048, ['not a .psu save export']),
])
def test_ps2_files_that_are_refused(tool, tmp_path, file, data, words):
    if data is None:
        whole = psu(b'BESLES-52507DEFJAM', [(b'BESLES-52507DEFJAM', ps2_profile('CUT'))])
        data = whole[:len(whole) - 1024]
    (tmp_path / file).write_bytes(data)
    out = subprocess.run([str(tool), 'import', str(tmp_path / file), str(tmp_path / 'save'), str(tmp_path / 'b'),
                          str(noon())], capture_output=True, text=True)
    assert out.returncode == 1 and out.stdout.startswith('error Could not read ' + file)
    for w in words:
        assert w in out.stdout
    assert not (tmp_path / 'save').exists()


def test_ps2_options_save_and_bad_names(tool, tmp_path):
    options = tmp_path / 'options.psu'
    options.write_bytes(psu(b'BESLES-52507OPTIONS', [(b'icon.sys', b'I' * 964), (b'BESLES-52507OPTIONS', b'\0' * 300)]))
    out = subprocess.run([str(tool), 'import', str(options), str(tmp_path / 'save'), str(tmp_path / 'b'), str(noon())],
                         capture_output=True, text=True)
    assert out.returncode == 1 and 'No Def Jam Story profile in options.psu' in out.stdout
    for name, why in (('CON', 'Windows keeps'), (' lead', 'cannot use'), ('a/b', 'cannot use'), ('', '1 to 20')):
        src = tmp_path / 'bad.psu'
        src.write_bytes(psu(b'BASLUS-21004X', [(b'BASLUS-21004X', ps2_profile(name))]))
        text = run(tool, 'import', src, tmp_path / 'save', tmp_path / 'b', noon(), check=False)
        assert 'skipped BASLUS-21004X' in text and why in text, (name, text)
        assert not (tmp_path / 'save' / 'UserData').exists()

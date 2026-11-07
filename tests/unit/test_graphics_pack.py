"""Enhanced graphics (community): scripts/make-graphics-pack.py, and the launcher's download and install
(src/hooks/graphics_mod.cpp) compiled on its own against a local HTTP server with tiny made-up packs and
a TEST signing key made here. Install, preset switch, re-install into a new version's bin, removal of
exactly the owned files, a bad checksum, a bad signature and an interrupted download. The proxy's two names
and the start guard, with a tiny DLL made here that does nothing. No real pack."""
from pathlib import Path
import hashlib
import http.server
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import threading
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('make_graphics_pack', ROOT / 'scripts/make-graphics-pack.py')
pack = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pack)

PRESETS = pack.PRESET_FILES


def make_zip(path, files):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data in files.items():
            z.writestr(name, data)


TEXTURE = os.urandom(5000)


def sources(tmp, extra_diff=False, preset_tag=b'', base_add=None, base_drop=()):
    shared = {'dxgi.dll': b'MZ fake proxy' * 50, 'reshade-shaders/Shaders/a.fx': b'shader a',
              'reshade-shaders/Textures/t.png': TEXTURE, 'THIRD-PARTY-NOTICES.txt': b'notices'}
    shared.update(base_add or {})
    for name in base_drop:
        del shared[name]
    q = dict(shared, **{n: b'quality ' + preset_tag + n.encode() for n in PRESETS})
    p = dict(shared, **{n: b'performance ' + preset_tag + n.encode() for n in PRESETS})
    if extra_diff:
        p['dxgi.dll'] = b'other'
    make_zip(tmp / 'q.zip', q)
    make_zip(tmp / 'p.zip', p)
    return q, p


def test_script_splits_and_is_reproducible(tmp_path):
    q, p = sources(tmp_path)
    m = pack.build(tmp_path / 'q.zip', tmp_path / 'p.zip', tmp_path / 'out', '2026-11-04.1')
    m2 = pack.build(tmp_path / 'q.zip', tmp_path / 'p.zip', tmp_path / 'out2', '2026-11-04.1')
    assert m == m2
    assert set(m['installs']['base']) == {n for n in q if n not in PRESETS}
    assert m['installs']['quality'] == sorted(PRESETS)
    on_disk = json.loads((tmp_path / 'out' / 'graphics.json').read_text(encoding='utf-8'))
    assert on_disk == m and on_disk['format'] == 1
    for kind, a in m['archives'].items():
        data = (tmp_path / 'out' / a['path']).read_bytes()
        assert len(data) == a['size'] and hashlib.sha256(data).hexdigest() == a['sha256']
        assert a['path'] == f"graphics-{kind}-{a['sha256'][:8]}.zip"
        with zipfile.ZipFile(tmp_path / 'out' / a['path']) as z:
            assert all(i.compress_type == zipfile.ZIP_STORED for i in z.infolist())
    with zipfile.ZipFile(tmp_path / 'out' / m['archives']['performance']['path']) as z:
        assert z.read('ReShade.ini') == p['ReShade.ini']


def test_script_refuses_other_differences(tmp_path):
    sources(tmp_path, extra_diff=True)
    with pytest.raises(SystemExit, match='beyond the preset files'):
        pack.build(tmp_path / 'q.zip', tmp_path / 'p.zip', tmp_path / 'out', 'v')


# ---- the launcher's side ----

DRIVER = r'''
#include <cstdio>
#include <string>
#include <windows.h>
#include "graphics_mod.h"
extern "C" const wchar_t *updater_host(int) { return nullptr; }
extern "C" int updater_port(void) { return 27778; }
int main(int argc, char **argv)
{
    using namespace gfx;
    std::string cmd = argc > 1 ? argv[1] : "", error;
    fs::path data = fs::u8path(argc > 2 ? argv[2] : ".");
    finish_pending(data);
    if (cmd == "tidy") { printf("%s\n", tidy_proxy(data) ? "OK" : "FAIL"); return 0; }
    if (cmd == "launcher-start") { printf("%s\n", guard_note_launcher_start(data) ? "SKIP" : "CLEAN"); return 0; }
    if (cmd == "begin") { printf("%s\n", begin_play(data, fs::u8path(argv[3]), argv[4][0] == '1') ? "MOD" : "PLAIN"); return 0; }
    if (cmd == "tidy-loaded") {         // as the launcher of an older install: the proxy is loaded in this process
        HMODULE dll = LoadLibraryW((fs::u8path(argv[3]) / "dxgi.dll").c_str());
        if (!dll) { printf("NOLOAD\n"); return 0; }
        printf("%s\n", tidy_proxy(data) ? "OK" : "FAIL");
        return 0;
    }
    if (cmd == "child") {               // as the game: the proxy loads at the start, then it runs or fails
        HMODULE dll = LoadLibraryW(L"dxgi.dll");
        wchar_t path[1024] = L"";
        if (dll) GetModuleFileNameW(dll, path, 1024);
        FILE *f = fopen(argv[3], "w");
        fprintf(f, "%ls\n", path);
        fclose(f);
        Sleep(atoi(argv[4]));
        return atoi(argv[5]);
    }
    if (cmd == "play") {                // as the launcher: Play, with the child above (argv: data bin report ms code guard)
        if (!begin_play(data, fs::u8path(argv[3]), true)) { printf("PLAIN\n"); return 0; }
        wchar_t exe[1024];
        GetModuleFileNameW(nullptr, exe, 1024);
        std::wstring line = L"gfx child x " + fs::u8path(argv[4]).wstring() + L" " + fs::u8path(argv[5]).wstring() + L" " + fs::u8path(argv[6]).wstring();
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi;
        if (!CreateProcessW(exe, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { start_failed(data); printf("NOSTART\n"); return 0; }
        watch_play(pi.hProcess, data, (unsigned long)atoi(argv[7]));
        printf("%s\n", WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0 ? "SETTLED-EXITED" : "SETTLED-RUNNING");
        return 0;
    }
    if (cmd == "remove") { bool ok = remove(data, &error); printf("%s %s\n", ok ? "OK" : "FAIL", error.c_str()); return 0; }
    if (cmd == "recover") { recover_update(data); printf("OK\n"); return 0; }
    Manifest m;
    if (!fetch_manifest(m, &error)) { printf("FAIL %s\n", error.c_str()); return 0; }
    bool ok = false;
    if (cmd == "install") ok = install(m, fs::u8path(argv[3]), data, argv[4], nullptr, &error);
    else if (cmd == "switch") ok = switch_preset(m, data, argv[3], nullptr, &error);
    else if (cmd == "update" || cmd == "update-game-running") {
        bool running = cmd != "update", updated = false;
        ok = update(m, data, nullptr, [running] { return !running; }, &error, &updated);
        printf("%s %s %s\n", ok ? "OK" : "FAIL", updated ? "UPDATED" : "SAME", error.c_str());
        return 0;
    }
    printf("%s %s\n", ok ? "OK" : "FAIL", error.c_str());
    return 0;
}
'''


class Handler(http.server.BaseHTTPRequestHandler):
    root = None
    cut = {}            # file name -> bytes to send before dropping the connection (once)
    log = []

    def log_message(self, *a):
        pass

    def do_GET(self):
        name = self.path.split('/')[-1]
        path = self.root / name
        Handler.log.append((self.path, self.headers.get('Range')))
        if not self.path.startswith('/graphics/') or not path.is_file():
            self.send_response(404)
            self.end_headers()
            return
        data = path.read_bytes()
        start = 0
        rng = self.headers.get('Range')
        if rng and rng.startswith('bytes='):
            start = int(rng[6:].split('-')[0])
            self.send_response(206)
            self.send_header('Content-Range', f'bytes {start}-{len(data) - 1}/{len(data)}')
        else:
            self.send_response(200)
        body = data[start:]
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        if name in Handler.cut:
            n = Handler.cut.pop(name)
            self.wfile.write(body[:n])
            self.wfile.flush()
            self.connection.shutdown(2)
            return
        self.wfile.write(body)


@pytest.fixture(scope='module')
def tool(tmp_path_factory):
    if sys.platform != 'win32' or not shutil.which('cl'):
        pytest.skip('Windows with cl required (WinHTTP, BCrypt)')
    folder = tmp_path_factory.mktemp('graphics-native')
    (folder / 'driver.cpp').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'gfx.exe'
    cmd = ['cl', '/nologo', '/std:c++17', '/EHsc', '/O2', '/W3', '/I' + str(ROOT / 'src/hooks'), str(folder / 'driver.cpp'),
           str(ROOT / 'src/hooks/graphics_mod.cpp'), str(ROOT / 'src/hooks/update_sig.c'), '/Fe:' + str(exe)]
    r = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-3000:]
    # a real DLL that does nothing: what loading it, renaming it held and the child start need
    (folder / 'dummy.cpp').write_text('int ffny_dummy_proxy = 1;\n', encoding='utf-8')
    r = subprocess.run(['cl', '/nologo', '/LD', 'dummy.cpp', '/link', '/OUT:dummy.dll'], cwd=folder, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-3000:]
    return exe


@pytest.fixture
def server(tmp_path):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    key = ec.generate_private_key(ec.SECP256R1())
    pem = tmp_path / 'test-key.pem'
    pem.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                      serialization.NoEncryption()))
    nums = key.public_key().public_numbers()
    pub = '%064x%064x' % (nums.x, nums.y)
    src = tmp_path / 'src'
    src.mkdir()
    sources(src)
    out = tmp_path / 'updates' / 'graphics'
    pack.main([str(src / 'q.zip'), str(src / 'p.zip'), '--out', str(out), '--version', 't1', '--sign-test-key', str(pem)])
    Handler.root, Handler.cut, Handler.log = out, {}, []
    httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    env = dict(os.environ, RECOMP_GRAPHICS_SERVER=f'127.0.0.1:{httpd.server_address[1]}', RECOMP_GRAPHICS_TEST_KEY=pub)
    yield {'out': out, 'env': env, 'pem': pem, 'tmp': tmp_path}
    httpd.shutdown()


def run(tool, env, *args):
    r = subprocess.run([str(tool), *map(str, args)], capture_output=True, text=True, env=env, timeout=120)
    assert r.returncode == 0, r.stdout + r.stderr
    return r.stdout.strip()


def files(bin_dir):
    return sorted(p.relative_to(bin_dir).as_posix() for p in bin_dir.rglob('*') if p.is_file())


def test_install_switch_update_remove(tool, server, tmp_path):
    data, v1, v2 = tmp_path / 'data', tmp_path / 'versions/1/bin', tmp_path / 'versions/2/bin'
    v1.mkdir(parents=True)
    v2.mkdir(parents=True)
    (v1 / 'defjam_recomp.exe').write_bytes(b'game')
    (v1 / 'reshade-shaders').mkdir()          # a folder that was there before: kept on removal
    (v1 / 'reshade-shaders' / 'mine.txt').write_text('not ours')
    env = server['env']
    assert run(tool, env, 'install', data, v1, 'performance').startswith('OK')
    assert (v1 / 'ReShade.ini').read_bytes() == b'performance ReShade.ini'
    assert (v1 / 'dxgi.ffny-mod').exists() and not (v1 / 'dxgi.dll').exists() and (v1 / 'reshade-shaders/Shaders/a.fx').exists()
    # the preset switch fetches only the small archive and swaps the 4 files
    Handler.log.clear()
    assert run(tool, env, 'switch', data, 'quality').startswith('OK')
    assert (v1 / 'ReShade.ini').read_bytes() == b'quality ReShade.ini'
    assert not any('base' in path for path, _ in Handler.log)
    # a new version: installed there from the cache, taken out of the old one
    Handler.log.clear()
    assert run(tool, env, 'install', data, v2, 'quality').startswith('OK')
    assert not any('base' in path for path, _ in Handler.log)
    assert files(v1) == ['defjam_recomp.exe', 'reshade-shaders/mine.txt']
    assert (v2 / 'deep-fried-chicken.cfg').read_bytes() == b'quality deep-fried-chicken.cfg'
    # removal: exactly the owned files, and the folders it made
    (v2 / 'user-file.txt').write_text('mine')
    assert run(tool, env, 'remove', data).startswith('OK')
    assert files(v2) == ['user-file.txt']
    assert not (data / 'graphics' / 'installed.txt').exists()


def test_never_overwrites_a_foreign_file(tool, server, tmp_path):
    bin_dir = tmp_path / 'bin'
    bin_dir.mkdir()
    (bin_dir / 'dxgi.dll').write_bytes(b'someone else')
    out = run(tool, server['env'], 'install', tmp_path / 'data', bin_dir, 'performance')
    assert out.startswith('FAIL') and 'dxgi.dll' in out
    assert files(bin_dir) == ['dxgi.dll'] and (bin_dir / 'dxgi.dll').read_bytes() == b'someone else'


def test_bad_signature_is_refused(tool, server, tmp_path):
    sig = server['out'] / 'graphics.json.sig'
    s = sig.read_text()
    sig.write_text(s[:-2] + ('00' if s[-2:] != '00' else '11'))
    bin_dir = tmp_path / 'bin'
    bin_dir.mkdir()
    out = run(tool, server['env'], 'install', tmp_path / 'data', bin_dir, 'performance')
    assert out.startswith('FAIL') and 'not signed' in out and files(bin_dir) == []


def test_test_key_only_from_this_pc(tool, server, tmp_path):
    env = dict(server['env'], RECOMP_GRAPHICS_SERVER='192.0.2.1:9')
    out = run(tool, env, 'install', tmp_path / 'data', tmp_path, 'performance')
    assert out.startswith('FAIL')


def test_bad_hash_is_refused(tool, server, tmp_path):
    m = json.loads((server['out'] / 'graphics.json').read_text(encoding='utf-8'))
    base = server['out'] / m['archives']['base']['path']
    data = bytearray(base.read_bytes())
    data[100] ^= 0xFF                   # same size, other bytes: the signed manifest says otherwise
    base.write_bytes(bytes(data))
    bin_dir = tmp_path / 'bin'
    bin_dir.mkdir()
    out = run(tool, server['env'], 'install', tmp_path / 'data', bin_dir, 'performance')
    assert out.startswith('FAIL') and 'checksum' in out and files(bin_dir) == []
    assert not list((tmp_path / 'data' / 'graphics' / 'cache').glob('*.part'))


def test_interrupted_download_resumes(tool, server, tmp_path):
    m = json.loads((server['out'] / 'graphics.json').read_text(encoding='utf-8'))
    name = m['archives']['base']['path']
    Handler.cut[name] = 3000            # the first answer stops after 3000 bytes
    bin_dir = tmp_path / 'bin'
    bin_dir.mkdir()
    assert run(tool, server['env'], 'install', tmp_path / 'data', bin_dir, 'performance').startswith('OK')
    ranges = [r for path, r in Handler.log if path.endswith(name)]
    assert ranges[0] is None and ranges[-1] == 'bytes=3000-'
    assert (bin_dir / 'dxgi.ffny-mod').read_bytes() == b'MZ fake proxy' * 50


# ---- updates: a newer graphics.json, changed archives only, switched over whole ----

def publish(server, version, **kw):
    """A new pack version on the test server, signed with the same TEST key."""
    src = server['tmp'] / f'src-{version}'
    src.mkdir()
    sources(src, **kw)
    pack.main([str(src / 'q.zip'), str(src / 'p.zip'), '--out', str(server['out']), '--version', version,
               '--sign-test-key', str(server['pem'])])
    return json.loads((server['out'] / 'graphics.json').read_text(encoding='utf-8'))


def snapshot(bin_dir):
    return {f: (bin_dir / f).read_bytes() for f in files(bin_dir)}


def installed(tool, server, tmp_path):
    data, bin_dir = tmp_path / 'data', tmp_path / 'bin'
    bin_dir.mkdir()
    (bin_dir / 'defjam_recomp.exe').write_bytes(b'game')
    assert run(tool, server['env'], 'install', data, bin_dir, 'performance').startswith('OK')
    return data, bin_dir


def record_version(data):
    return next(ln[8:] for ln in (data / 'graphics/installed.txt').read_text().splitlines() if ln.startswith('version '))


def test_update_fetches_only_the_changed_archive(tool, server, tmp_path):
    data, bin_dir = installed(tool, server, tmp_path)
    assert run(tool, server['env'], 'update', data) == 'OK SAME'           # same version: nothing
    m = publish(server, 't2', preset_tag=b'v2 ')
    shutil.rmtree(data / 'graphics/cache')          # the hash compare decides, not a cached copy
    Handler.log.clear()
    assert run(tool, server['env'], 'update', data) == 'OK UPDATED'
    got = [path for path, _ in Handler.log]
    assert any(m['archives']['performance']['path'] in p for p in got)
    assert not any('base' in p or 'quality' in p for p in got)
    assert (bin_dir / 'ReShade.ini').read_bytes() == b'performance v2 ReShade.ini'
    assert (bin_dir / 'dxgi.ffny-mod').read_bytes() == b'MZ fake proxy' * 50
    assert record_version(data) == 't2'
    assert not (bin_dir / 'ffny-graphics-new').exists() and not (bin_dir / 'ffny-graphics-old').exists()
    assert not (data / 'graphics/update-journal.txt').exists()
    assert run(tool, server['env'], 'update', data) == 'OK SAME'


def test_update_of_the_base_adds_and_drops_files(tool, server, tmp_path):
    data, bin_dir = installed(tool, server, tmp_path)
    publish(server, 't2', base_add={'reshade-shaders/New/b.fx': b'shader b'}, base_drop=['reshade-shaders/Textures/t.png'])
    assert run(tool, server['env'], 'update', data) == 'OK UPDATED'
    assert (bin_dir / 'reshade-shaders/New/b.fx').read_bytes() == b'shader b'
    assert not (bin_dir / 'reshade-shaders/Textures/t.png').exists()
    assert (bin_dir / 'ReShade.ini').read_bytes() == b'performance ReShade.ini'
    # Turn off still removes exactly the pack, the new folder too
    assert run(tool, server['env'], 'remove', data).startswith('OK')
    assert files(bin_dir) == ['defjam_recomp.exe'] and not (bin_dir / 'reshade-shaders').exists()


def test_old_record_without_hashes_updates_everything(tool, server, tmp_path):
    data, bin_dir = installed(tool, server, tmp_path)
    rec = data / 'graphics/installed.txt'
    rec.write_text(''.join(ln + '\n' for ln in rec.read_text().splitlines() if 'sha256' not in ln))
    (bin_dir / 'reshade-shaders/Shaders/a.fx').write_bytes(b'stale')
    publish(server, 't2', preset_tag=b'v2 ')
    assert run(tool, server['env'], 'update', data) == 'OK UPDATED'
    # the base counts as changed: unpacked again (from the checked cache, so not downloaded again)
    assert (bin_dir / 'reshade-shaders/Shaders/a.fx').read_bytes() == b'shader a'
    assert (bin_dir / 'ReShade.ini').read_bytes() == b'performance v2 ReShade.ini'


def test_update_with_a_bad_download_keeps_the_old_pack(tool, server, tmp_path):
    data, bin_dir = installed(tool, server, tmp_path)
    before = snapshot(bin_dir)
    m = publish(server, 't2', base_add={'reshade-shaders/New/b.fx': b'shader b'})
    base = server['out'] / m['archives']['base']['path']
    raw = bytearray(base.read_bytes())
    raw[100] ^= 0xFF
    base.write_bytes(bytes(raw))
    out = run(tool, server['env'], 'update', data)
    assert out.startswith('FAIL SAME') and 'checksum' in out
    assert snapshot(bin_dir) == before and record_version(data) == 't1'


def test_no_update_while_the_game_runs(tool, server, tmp_path):
    data, bin_dir = installed(tool, server, tmp_path)
    before = snapshot(bin_dir)
    publish(server, 't2', preset_tag=b'v2 ')
    out = run(tool, server['env'], 'update-game-running', data)
    assert out.startswith('OK SAME') and 'game started' in out
    assert snapshot(bin_dir) == before and record_version(data) == 't1'


def test_a_file_that_cannot_be_replaced_rolls_the_switch_back(tool, server, tmp_path):
    data, bin_dir = installed(tool, server, tmp_path)
    before = snapshot(bin_dir)
    m = publish(server, 't2', preset_tag=b'v2 ')
    last = m['installs']['performance'][-1]          # the earlier preset files are already switched by then
    with open(bin_dir / last, 'rb'):                 # held without delete sharing: it cannot be renamed
        out = run(tool, server['env'], 'update', data)
    assert out.startswith('FAIL SAME') and 'Cannot replace' in out
    assert snapshot(bin_dir) == before and record_version(data) == 't1'
    assert not (data / 'graphics/update-journal.txt').exists()
    assert run(tool, server['env'], 'update', data) == 'OK UPDATED'     # the next try goes through


def test_a_switch_cut_short_is_undone_at_the_next_start(tool, server, tmp_path):
    data, bin_dir = installed(tool, server, tmp_path)
    before = snapshot(bin_dir)
    # as the power cut left it: one file in the backup, the new one in its place, the record still the old one
    (bin_dir / 'ffny-graphics-old').mkdir()
    (bin_dir / 'ReShade.ini').rename(bin_dir / 'ffny-graphics-old/ReShade.ini')
    (bin_dir / 'ReShade.ini').write_bytes(b'new')
    (bin_dir / 'ffny-graphics-new').mkdir()
    (bin_dir / 'ffny-graphics-new/x.ini').write_bytes(b'staged')
    (bin_dir / 'brand-new.fx').write_bytes(b'added')
    (data / 'graphics/update-journal.txt').write_text(f'version t2\nbin {bin_dir}\ns ReShade.ini\na brand-new.fx\n')
    assert run(tool, server['env'], 'recover', data) == 'OK'
    assert snapshot(bin_dir) == before and record_version(data) == 't1'
    assert not (data / 'graphics/update-journal.txt').exists()


# ---- the proxy's two names and the start guard ----

def old_install(tool, tmp_path, proxy=None):
    """An install as builds up to 59 left it: dxgi.dll in bin, under that name in the record."""
    data, bin_dir = tmp_path / 'data', tmp_path / 'bin'
    bin_dir.mkdir()
    (data / 'graphics').mkdir(parents=True)
    (bin_dir / 'dxgi.dll').write_bytes(proxy or b'MZ fake')
    (bin_dir / 'ReShade.ini').write_text('x')
    (data / 'graphics/installed.txt').write_text(f'version t0\npreset performance\nbin {bin_dir}\nfile dxgi.dll\nfile ReShade.ini\n')
    return data, bin_dir


def record_files(data):
    return [ln[5:] for ln in (data / 'graphics/installed.txt').read_text().splitlines() if ln.startswith('file ')]


def test_foreign_dxgi_beside_a_new_install_stays_and_blocks_the_mod(tool, server, tmp_path):
    data, bin_dir = tmp_path / 'data', tmp_path / 'bin'
    bin_dir.mkdir()
    assert run(tool, server['env'], 'install', data, bin_dir, 'performance').startswith('OK')
    (bin_dir / 'dxgi.dll').write_bytes(b'someone else')
    assert run(tool, server['env'], 'begin', data, bin_dir, '1') == 'PLAIN'
    assert run(tool, server['env'], 'remove', data).startswith('OK')
    assert files(bin_dir) == ['dxgi.dll'] and (bin_dir / 'dxgi.dll').read_bytes() == b'someone else'


def test_old_install_is_migrated(tool, tmp_path):
    data, bin_dir = old_install(tool, tmp_path)
    assert run(tool, os.environ, 'tidy', data) == 'OK'
    assert not (bin_dir / 'dxgi.dll').exists() and (bin_dir / 'dxgi.ffny-mod').read_bytes() == b'MZ fake'
    assert 'dxgi.ffny-mod' in record_files(data) and 'dxgi.dll' not in record_files(data)
    assert run(tool, os.environ, 'tidy', data) == 'OK'
    assert run(tool, os.environ, 'remove', data).startswith('OK')
    assert files(bin_dir) == []


def test_migration_with_the_proxy_loaded_in_this_process(tool, tmp_path):
    data, bin_dir = old_install(tool, tmp_path, (tool.parent / 'dummy.dll').read_bytes())
    assert run(tool, os.environ, 'tidy-loaded', data, bin_dir) == 'OK'
    assert not (bin_dir / 'dxgi.dll').exists() and (bin_dir / 'dxgi.ffny-mod').exists()
    assert 'dxgi.ffny-mod' in record_files(data)


def test_a_stopped_start_is_put_right(tool, tmp_path):
    data, bin_dir = old_install(tool, tmp_path)
    assert run(tool, os.environ, 'begin', data, bin_dir, '1') == 'MOD'
    assert (bin_dir / 'dxgi.dll').exists() and not (bin_dir / 'dxgi.ffny-mod').exists()
    assert (data / 'graphics/play-started.txt').exists()
    # the power went: the next start renames it back, and the guard speaks
    assert run(tool, os.environ, 'tidy', data) == 'OK'
    assert not (bin_dir / 'dxgi.dll').exists()
    assert run(tool, os.environ, 'launcher-start', data) == 'SKIP'


def test_off_starts_plain_and_touches_nothing(tool, tmp_path):
    data, bin_dir = old_install(tool, tmp_path)
    assert run(tool, os.environ, 'tidy', data) == 'OK'
    assert run(tool, os.environ, 'begin', data, bin_dir, '0') == 'PLAIN'
    assert (bin_dir / 'dxgi.ffny-mod').exists() and not (bin_dir / 'dxgi.dll').exists()
    assert not (data / 'graphics/play-started.txt').exists()


def play_install(tool, tmp_path):
    data, bin_dir = old_install(tool, tmp_path, (tool.parent / 'dummy.dll').read_bytes())
    exe = bin_dir / 'gfx.exe'
    shutil.copy(tool, exe)
    assert run(exe, os.environ, 'tidy', data) == 'OK'
    return data, bin_dir, exe


def test_play_child_loads_it_and_it_goes_back(tool, tmp_path):
    data, bin_dir, exe = play_install(tool, tmp_path)
    report = tmp_path / 'child.txt'
    # the child runs on past the guard (4 s): settled while it runs, marker cleared, proxy parked meanwhile
    assert run(exe, os.environ, 'play', data, bin_dir, report, 6000, 0, 4000) == 'SETTLED-RUNNING'
    assert report.read_text().strip().lower().endswith('dxgi.dll')
    assert not (bin_dir / 'dxgi.dll').exists() and (bin_dir / 'dxgi.ffny-mod').exists()
    assert not (data / 'graphics/play-started.txt').exists()
    assert run(exe, os.environ, 'launcher-start', data) == 'CLEAN'


def test_guard_after_a_crash_skips_once(tool, tmp_path):
    data, bin_dir, exe = play_install(tool, tmp_path)
    report = tmp_path / 'child.txt'
    assert run(exe, os.environ, 'play', data, bin_dir, report, 100, 3, 60000) == 'SETTLED-EXITED'
    assert not (bin_dir / 'dxgi.dll').exists() and (bin_dir / 'dxgi.ffny-mod').exists()
    assert (data / 'graphics/play-started.txt').exists()
    # the launcher opens: the next Play is without it, also if this launcher closes first
    assert run(exe, os.environ, 'launcher-start', data) == 'SKIP'
    assert run(exe, os.environ, 'launcher-start', data) == 'SKIP'
    assert run(exe, os.environ, 'begin', data, bin_dir, '1') == 'PLAIN'
    assert not (bin_dir / 'dxgi.dll').exists()
    # that one time: the Play after it has the mod again
    assert run(exe, os.environ, 'launcher-start', data) == 'CLEAN'
    assert run(exe, os.environ, 'begin', data, bin_dir, '1') == 'MOD'
    assert (bin_dir / 'dxgi.dll').exists()


def test_a_normal_quick_close_is_not_a_failure(tool, tmp_path):
    data, bin_dir, exe = play_install(tool, tmp_path)
    assert run(exe, os.environ, 'play', data, bin_dir, tmp_path / 'c.txt', 100, 0, 60000) == 'SETTLED-EXITED'
    assert run(exe, os.environ, 'launcher-start', data) == 'CLEAN'

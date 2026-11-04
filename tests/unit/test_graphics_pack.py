"""Enhanced graphics (community): scripts/make-graphics-pack.py, and the launcher's download and install
(src/hooks/graphics_mod.cpp) compiled on its own against a local HTTP server with tiny made-up packs and
a TEST signing key made here. Install, preset switch, re-install into a new version's bin, removal of
exactly the owned files, a bad checksum, a bad signature and an interrupted download. No real pack."""
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


def sources(tmp, extra_diff=False):
    shared = {'dxgi.dll': b'MZ fake proxy' * 50, 'reshade-shaders/Shaders/a.fx': b'shader a',
              'reshade-shaders/Textures/t.png': os.urandom(5000), 'THIRD-PARTY-NOTICES.txt': b'notices'}
    q = dict(shared, **{n: b'quality ' + n.encode() for n in PRESETS})
    p = dict(shared, **{n: b'performance ' + n.encode() for n in PRESETS})
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
#include "graphics_mod.h"
extern "C" const wchar_t *updater_host(int) { return nullptr; }
extern "C" int updater_port(void) { return 27778; }
int main(int argc, char **argv)
{
    using namespace gfx;
    std::string cmd = argc > 1 ? argv[1] : "", error;
    fs::path data = fs::u8path(argc > 2 ? argv[2] : ".");
    finish_pending(data);
    if (cmd == "remove") { bool ok = remove(data, &error); printf("%s %s\n", ok ? "OK" : "FAIL", error.c_str()); return 0; }
    Manifest m;
    if (!fetch_manifest(m, &error)) { printf("FAIL %s\n", error.c_str()); return 0; }
    bool ok = false;
    if (cmd == "install") ok = install(m, fs::u8path(argv[3]), data, argv[4], nullptr, &error);
    else if (cmd == "switch") ok = switch_preset(m, data, argv[3], nullptr, &error);
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
    assert (v1 / 'dxgi.dll').exists() and (v1 / 'reshade-shaders/Shaders/a.fx').exists()
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
    assert (bin_dir / 'dxgi.dll').read_bytes() == b'MZ fake proxy' * 50

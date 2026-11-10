#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""The launcher's FFNY Studio card on Windows, end to end, against a local update server and a fake Studio.

    python tools/defjam-mcp/dj.py lock run -- python scripts/test-studio-launcher.py [--shots DIR] [--keep]

Builds a throw-away root in %TEMP% (data folder, no player's files), a fake FFNYStudio.exe (a small C# program
compiled with the .NET Framework's csc.exe that writes its arguments, command line and working folder to
$FAKE_STUDIO_OUT), packages it like the Studio repository's scripts/package.ps1 (FFNYStudio/FFNYStudio.exe +
_internal/, deflated, plus an incompressible blob so the download takes a moment) and serves studio-latest.json,
its .sig (a throw-away P-256 key, r||s as 128 hex digits) and the zip from a local HTTP server with Range support
and an optional speed limit. Then build/win-x64-release/defjam_recomp.exe --launcher runs several times with
RECOMP_STUDIO_SERVER / RECOMP_STUDIO_TEST_KEY and RECOMP_TEST_STUDIO steps, one picture each (RECOMP_WINDOW_SHOT):

1. get            the card offers "Get FFNY Studio (<size>)"; nothing installed
2. downloading    press, slow server: the picture shows the progress (the launcher quits mid-download; .part stays)
3. installed      press again: the download resumes (Range), is checked and installed; versions/<v>, current.txt
4. open           press: the fake Studio starts with --data-root and --account-file (no --install-dir: a checkout)
5. update         a newer build is published: "Update Studio"
6. updated        press: the new version is installed beside the old one, current.txt switches
7. open-install   DEFJAM_INSTALL_DIR names a folder with installed.ini: --install-dir is passed
8. bad-zip        a newer manifest whose zip does not match its SHA-256: the error line; nothing switched
9. bad-signature  a manifest signed by another key: not believed (the installed version stays, no update offer)
Each step prints one JSON line; the exit code is the number of failed steps.
"""
import argparse
import datetime
import hashlib
import http.server
import json
import os
from pathlib import Path
import shutil
import socketserver
import subprocess
import sys
import tempfile
import threading
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'build/win-x64-release/defjam_recomp.exe'
CSC = Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
SAFE = {
    'RECOMP_ACCOUNT_SERVER': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_EU': '127.0.0.1:9',
    'RECOMP_NETPLAY_SERVER_NA': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_WEST': '127.0.0.1:9',
    'RECOMP_NO_UPDATE_CHECK': '1', 'RECOMP_WINDOW_BACKGROUND': '1',
    'FFNY_CONTENT_URL': 'http://127.0.0.1:9/updates/content/index.json',
}

FAKE_STUDIO = r'''
using System;
using System.IO;
class FakeStudio {
    static string Q(string x) { return "\"" + x.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\""; }
    static void Main(string[] args) {
        string o = Environment.GetEnvironmentVariable("FAKE_STUDIO_OUT");
        if (o == null) return;
        string[] q = Array.ConvertAll(args, Q);
        File.AppendAllText(o, "{\"argv\": [" + string.Join(", ", q) + "], \"cwd\": " + Q(Environment.CurrentDirectory) +
            ", \"cmdline\": " + Q(Environment.CommandLine) + ", \"exe\": " +
            Q(System.Reflection.Assembly.GetEntryAssembly().Location) + "}\n");
    }
}
'''


class Handler(http.server.BaseHTTPRequestHandler):
    folder = None
    rate = 0            # bytes per second, 0: no limit

    def log_message(self, *a):
        pass

    def do_GET(self):
        if not self.path.startswith('/studio/') or '..' in self.path:
            self.send_error(404)
            return
        f = Handler.folder / self.path[len('/studio/'):]
        if not f.is_file():
            self.send_error(404)
            return
        data = f.read_bytes()
        start = 0
        rng = self.headers.get('Range', '')
        if rng.startswith('bytes=') and rng.endswith('-'):
            start = int(rng[6:-1])
        if start:
            self.send_response(206)
            self.send_header('Content-Range', 'bytes %d-%d/%d' % (start, len(data) - 1, len(data)))
        else:
            self.send_response(200)
        body = data[start:]
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        step = 64 * 1024
        try:
            for i in range(0, len(body), step):
                self.wfile.write(body[i:i + step])
                if Handler.rate:
                    time.sleep(step / Handler.rate)
        except (ConnectionError, OSError):
            pass


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def keypair():
    from cryptography.hazmat.primitives.asymmetric import ec
    key = ec.generate_private_key(ec.SECP256R1())
    n = key.public_key().public_numbers()
    return key, '%064x%064x' % (n.x, n.y)


def sign(key, data):
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
    r, s = decode_dss_signature(key.sign(data, ec.ECDSA(hashes.SHA256())))
    return '%064x%064x' % (r, s)


def package(fake_exe, served, version, build, key, blob_mb=6, corrupt=False):
    """FFNYStudio-<version>-windows-x64.zip, studio-latest.json and its .sig, as package.ps1 and updatesign make them."""
    name = 'FFNYStudio-%s-windows-x64.zip' % version
    zpath = served / name
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.write(fake_exe, 'FFNYStudio/FFNYStudio.exe')
        z.writestr('FFNYStudio/_internal/version.txt', 'FFNY Studio %s (build %d), test package\n' % (version, build))
        z.writestr('FFNYStudio/_internal/text/lorem.txt', ('FFNY Studio test package. ' * 4000).encode())
        z.writestr('FFNYStudio/_internal/blob.bin', os.urandom(blob_mb << 20))
    sha = hashlib.sha256(zpath.read_bytes()).hexdigest()
    size = zpath.stat().st_size
    if corrupt:                     # the published checksum stays, the bytes change
        data = bytearray(zpath.read_bytes())
        data[100] ^= 0xFF
        zpath.write_bytes(bytes(data))
    manifest = json.dumps(dict(build=build, version=version, notes='FFNY Studio %s' % version, setup=name,
                               url='https://example.invalid/studio/' + name, sha256=sha, size=size),
                          separators=(',', ':')).encode()
    (served / 'studio-latest.json').write_bytes(manifest)
    (served / 'studio-latest.json.sig').write_text(sign(key, manifest) + '\n')
    return size


def launcher(run, env_base, name, frame, steps='', extra=None):
    shot, log = run / (name + '.bmp'), run / (name + '.log')
    env = dict(env_base, RECOMP_WINDOW_SHOT=str(shot), RECOMP_WINDOW_SHOT_FRAME=str(frame), RECOMP_TEST_STUDIO=steps)
    env.update(extra or {})
    with log.open('w', encoding='utf-8', errors='replace') as out:
        p = subprocess.Popen([str(EXE), '--launcher'], cwd=ROOT, env=env, stdout=out, stderr=subprocess.STDOUT)
        try:
            code = p.wait(timeout=180)
        finally:
            if p.poll() is None:
                p.kill()
                p.wait()
    png = None
    if shot.exists():
        from PIL import Image
        png = shot.with_suffix('.png')
        with Image.open(shot) as picture:
            picture.save(png)
        shot.unlink()
    lines = [l for l in log.read_text(errors='replace').splitlines() if '[STUDIO]' in l]
    return code, lines, png


def current(studio):
    f = studio / 'current.txt'
    if not f.exists():
        return {}
    return dict(l.split(' ', 1) for l in f.read_text().splitlines() if l and not l.startswith('#'))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--shots', type=Path, default=None, help='copy the pictures (PNG) here')
    ap.add_argument('--keep', action='store_true', help='keep the temporary root')
    a = ap.parse_args()
    if not EXE.exists() or not CSC.exists():
        print(json.dumps(dict(step='tools', ok=False, why='no %s' % (EXE if not EXE.exists() else CSC))))
        return 1
    top = Path(tempfile.mkdtemp(prefix='ffny-studio-test-'))
    run, data, served = top / 'run', top / 'data', top / 'served'
    studio = top / 'studio'             # the default: beside the data folder
    for d in (run, data / 'save', served):
        d.mkdir(parents=True)
    (data / 'settings.ini').write_text('[launcher]\ngpu_notice_seen=1\n', encoding='utf-8')
    fake_src, fake_exe = top / 'FakeStudio.cs', top / 'FakeStudio.exe'
    fake_src.write_text(FAKE_STUDIO)
    subprocess.run([str(CSC), '/nologo', '/out:' + str(fake_exe), str(fake_src)], check=True, capture_output=True)
    out_file = top / 'studio-started.jsonl'

    key, public = keypair()
    other, _ = keypair()
    Handler.folder = served
    httpd = Server(('127.0.0.1', 0), Handler)
    port = httpd.server_address[1]
    threading.Thread(target=httpd.serve_forever, daemon=True).start()

    env = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_') and k != 'DEFJAM_INSTALL_DIR'}
    env.update(SAFE)
    env.update(DEFJAM_DATA=str(data), DEFJAM_LOG_DIR=str(data / 'logs'), RECOMP_SETTINGS=str(data / 'settings.ini'),
               RECOMP_STUDIO_SERVER='127.0.0.1:%d' % port, RECOMP_STUDIO_TEST_KEY=public, FAKE_STUDIO_OUT=str(out_file))
    results, shots = [], []

    def step(name, ok, png=None, **info):
        row = dict(step=name, ok=bool(ok), shot=str(png) if png else None, **info)
        results.append(row)
        if png:
            shots.append(png)
        print(json.dumps(row), flush=True)

    def started():
        return [json.loads(l) for l in out_file.read_text().splitlines()] if out_file.exists() else []

    try:
        size1 = package(fake_exe, served, '0.1.0-test.1', 101, key)
        code, log, png = launcher(run, env, '1-get', 300)
        step('get', any('published: 0.1.0-test.1' in l for l in log) and not current(studio), png, size=size1, log=log[-2:])

        Handler.rate = 400 * 1024
        code, log, png = launcher(run, env, '2-downloading', 420, '60:press')
        parts = list((studio / 'cache').glob('*.part')) if (studio / 'cache').exists() else []
        step('downloading', parts and not current(studio), png, part_bytes=parts[0].stat().st_size if parts else 0)

        Handler.rate = 0
        code, log, png = launcher(run, env, '3-installed', 900, '30:press')
        cur = current(studio)
        exe = studio / 'versions' / cur.get('folder', '?') / 'FFNYStudio' / 'FFNYStudio.exe'
        step('installed', cur.get('version') == '0.1.0-test.1' and cur.get('build') == '101' and exe.is_file() and
             not any((studio / 'cache').glob('*')), png, current=cur, log=log[-2:])

        code, log, png = launcher(run, env, '4-open', 300, '60:press')
        time.sleep(2)
        got = started()
        argv = got[-1]['argv'] if got else []
        want = ['--data-root', str(data), '--account-file', str(data / 'account.dat')]
        step('open', argv == want and Path(got[-1]['cwd']).resolve() == exe.parent.resolve(), png, argv=argv,
             cwd=got[-1]['cwd'] if got else None)

        size2 = package(fake_exe, served, '0.1.0-test.2', 102, key)
        code, log, png = launcher(run, env, '5-update', 300)
        step('update-offered', any('build 102' in l for l in log) and current(studio).get('build') == '101', png)

        code, log, png = launcher(run, env, '6-updated', 900, '60:press')
        cur = current(studio)
        versions = sorted(p.name for p in (studio / 'versions').iterdir())
        step('updated', cur.get('build') == '102' and versions == ['0.1.0-test.1', '0.1.0-test.2'], png, current=cur,
             versions=versions, previous=(studio / 'previous.txt').read_text().strip())

        install = top / 'install'
        install.mkdir()
        (install / 'installed.ini').write_text('[install]\ndata = %s\n' % data)
        n = len(started())
        code, log, png = launcher(run, env, '7-open-install', 300, '60:press', {'DEFJAM_INSTALL_DIR': str(install)})
        time.sleep(2)
        got = started()
        argv = got[-1]['argv'] if len(got) > n else []
        step('open-install-dir', argv[:2] == ['--install-dir', str(install)] and '0.1.0-test.2' in got[-1]['exe'], png,
             argv=argv, exe=got[-1]['exe'] if got else None)

        package(fake_exe, served, '0.1.0-test.3', 103, key, corrupt=True)
        code, log, png = launcher(run, env, '8-bad-zip', 600, '60:press')
        step('bad-zip', current(studio).get('build') == '102' and any('did not match' in l for l in log), png, log=log[-2:])

        package(fake_exe, served, '0.1.0-test.4', 104, other)
        code, log, png = launcher(run, env, '9-bad-signature', 300)
        step('bad-signature', current(studio).get('build') == '102' and any('not signed' in l for l in log) and
             not any('published: 0.1.0-test.4' in l for l in log), png, log=log[-1:])
    finally:
        httpd.shutdown()
        if a.shots:
            a.shots.mkdir(parents=True, exist_ok=True)
            for p in shots:
                shutil.copy2(p, a.shots / p.name)
        (a.shots or run).joinpath('receipts.json').write_text(json.dumps(results, indent=1))
        if not a.keep:
            shutil.rmtree(top, ignore_errors=True)
        else:
            print('kept', top)
    failed = sum(not r['ok'] for r in results)
    print(json.dumps(dict(step='summary', ok=failed == 0, failed=failed, steps=len(results))))
    return failed


if __name__ == '__main__':
    sys.exit(main())

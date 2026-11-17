#!/usr/bin/env python3
"""Cloud saves end to end (build 91) on Windows: two made-up "PCs" (two data roots) and a local account service
with a test account, never the live service and never a player's save.

    python tools/defjam-mcp/dj.py lock run -- python scripts/test-cloud-saves.py --server <server repo> [--out DIR]

Builds the account service from the private server repository (relay/cmd/accounts; --accounts-exe takes a built
one instead) and starts it on 127.0.0.1 with -test-accounts and -cloud-saves-dir in the run folder. Then it starts
build/win-x64-release/defjam_recomp.exe as the launcher several times (RECOMP_ACCOUNT_SERVER and
RECOMP_ACCOUNT_TEST_TOKEN sign it in to that service, RECOMP_CLOUD_PC names the PC, RECOMP_WINDOW_SHOT keeps the
window's own picture): PC A uploads at start; PC B, empty, is offered the cloud save and restores it; both change
and B meets a conflict and keeps both; A sees newer saves, looks at the restore confirm and restores; the setting
off sends nothing; Delete my cloud saves empties the cloud. Each step prints one JSON line; receipts.json sums them
up and the pictures are PNGs in the run folder (--shots copies them elsewhere too). Then boot the game itself on PC
B's restored saves, in a dj call of its own (it takes the lock):

    python tools/defjam-mcp/dj.py scenario boot --goto story --fixture <run folder>/B
"""
import argparse
import datetime
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('save_import', ROOT / 'scripts/save-import.py')
save_import = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(save_import)
EXE = ROOT / 'build/win-x64-release/defjam_recomp.exe'
SAFE = {
    'RECOMP_NETPLAY_SERVER_EU': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_NA': '127.0.0.1:9',
    'RECOMP_NETPLAY_SERVER_SG': '127.0.0.1:9', 'RECOMP_NO_UPDATE_CHECK': '1', 'RECOMP_WINDOW_BACKGROUND': '1',
}
OWN = []
SLOW = 4            # every frame number below is multiplied by this
UPLOAD_GAP = 11    # the service takes one upload per account every 10 s (relay/accounts/cloudsaves.go)


def profile(seed):
    """A made-up profile file the game loads: the 20CM header and its sizes, signed with the port's key."""
    size = 0x5990
    body = bytearray(hashlib.shake_256(seed.encode()).digest(size))
    body[0:4] = b'20CM'
    body[4:16] = struct.pack('<III', size, 0x40, size - 0x8C)
    body[0x80 + 0x4D8A] = 0
    return save_import.resign(bytes(body), bytes(16))


def meta(name):
    return b'\xff\xfe' + ('Name=' + name + '\r\n').encode('utf-16-le')


def folder_of(name):
    return save_import.folder_name(name)


def put(base, name, seed=None):
    folder = base / folder_of(name)
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'SaveMeta.xbx').write_bytes(meta(name))
    (folder / name).write_bytes(profile(seed or name))
    (folder / 'saveimage.xbx').write_bytes(b'XPR0' + bytes(4092))
    return folder


def save_tree(save):
    """What the cloud holds of a save area (UserData/45410049 and aside), path -> sha1."""
    out = {}
    for sub in ('UserData', 'aside'):
        for p in sorted((save / sub).rglob('*')) if (save / sub).is_dir() else []:
            if p.is_file():
                out[p.relative_to(save).as_posix()] = hashlib.sha1(p.read_bytes()).hexdigest()
    return out


def backups(data):
    out = []
    folder = data / 'save-backups'
    for d in sorted(folder.iterdir(), reverse=True) if folder.is_dir() else []:
        if (d / 'backup.txt').is_file():
            info = dict(l.split('=', 1) for l in (d / 'backup.txt').read_text().splitlines() if '=' in l)
            out.append((d.name, info.get('reason'), d))
    return out


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


class Service:
    def __init__(self, exe, folder):
        self.port = free_port()
        self.folder = folder
        folder.mkdir(parents=True)
        self.log = (folder / 'accounts.log').open('w')
        (folder / 'discord-secret.txt').write_text('tests-no-discord')     # no Discord sign-in: the test session only
        self.proc = subprocess.Popen([str(exe), '-listen', '127.0.0.1:%d' % self.port, '-data', str(folder / 'accounts.json'),
                                      '-internal-listen', '', '-test-accounts', 'Cloud Tester',
                                      '-cloud-saves-dir', str(folder / 'cloudsaves'), '-public-url', 'http://127.0.0.1',
                                      '-client-id', '0', '-client-secret-file', str(folder / 'discord-secret.txt')],
                                     stdout=self.log, stderr=subprocess.STDOUT)
        OWN.append(self.proc)
        for _ in range(100):
            sessions = folder / 'accounts.json.test-sessions'
            if sessions.is_file() and self.get('/health')[0] == 200:
                self.token = sessions.read_text().split('\t')[1].strip()
                return
            time.sleep(0.1)
        raise SystemExit('the local account service did not start: ' + str(folder / 'accounts.log'))

    def get(self, path):
        req = urllib.request.Request('http://127.0.0.1:%d%s' % (self.port, path))
        if getattr(self, 'token', None):
            req.add_header('Authorization', 'Bearer ' + self.token)
        try:
            with urllib.request.urlopen(req, timeout=5) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()
        except OSError:
            return 0, b''

    def versions(self):
        code, body = self.get('/api/saves?format=text')
        return [l[2:].split('|') for l in body.decode().splitlines() if l.startswith('v=')] if code == 200 else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--out', type=Path, default=ROOT / 'logs' / 'b91-cloud-saves')
    ap.add_argument('--server', type=Path, default=ROOT / 'server', help='the private server repository (relay/cmd/accounts)')
    ap.add_argument('--accounts-exe', type=Path, help='a built account service instead')
    ap.add_argument('--shots', type=Path, help='also copy the pictures here')
    ap.add_argument('--real-save', type=Path, help='a save folder (with UserData) the game made: also up from PC C, '
                    'restored into an empty PC D for a game boot')
    a = ap.parse_args()
    if not EXE.is_file():
        print('not built: ' + str(EXE))
        return 2
    run = (a.out / ('run-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))).resolve()
    run.mkdir(parents=True)
    exe = a.accounts_exe
    if not exe:
        exe = run / 'accounts.exe'
        subprocess.run(['go', 'build', '-o', str(exe), './cmd/accounts'], cwd=a.server / 'relay', check=True)
    svc = Service(exe, run / 'service')

    def root(name):
        data = run / name
        (data / 'logs').mkdir(parents=True)
        (data / 'settings.ini').write_text('[debug]\nskip_intro = 1\n')
        return data

    A, B = root('A'), root('B')
    title_a = A / 'save' / 'UserData' / '45410049'
    title_a.mkdir(parents=True)
    (title_a / 'TitleMeta.xbx').write_bytes(meta('Def Jam'))
    put(title_a, 'VY2')
    put(title_a, 'KID')
    opt = title_a / folder_of('Options')
    opt.mkdir()
    (opt / 'SaveMeta.xbx').write_bytes(meta('Options'))
    (opt / 'Options').write_bytes(b'20CM' + bytes(5400))
    put(A / 'save' / 'aside', 'OLDIE')
    (A / 'save' / 'Partition1.img').write_bytes(bytes(65536))       # stands for the game's disk images: never sent
    (B / 'save').mkdir()

    receipts, faults = [], []

    def step(name, ok, png, detail=''):
        r = dict(step=name, ok=bool(ok), picture=str(png) if png else None, detail=detail)
        receipts.append(r)
        if not ok:
            faults.append(name)
        print(json.dumps(r), flush=True)

    def slow(spec):
        """Frame numbers times SLOW: the requests to the service run beside the frames."""
        return ';'.join('%d:%s' % (int(s.split(':', 1)[0]) * SLOW, s.split(':', 1)[1]) for s in spec.split(';') if s)

    def launcher(data, pc, name, page=None, steps='', prompt='', frame=150):
        steps, prompt, frame = slow(steps), slow(prompt), frame * SLOW
        shot = run / (name + '.bmp')
        log = run / (name + '.log')
        e = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_')}
        e.update(SAFE)
        e.update(DEFJAM_DATA=str(data), DEFJAM_LOG_DIR=str(data / 'logs'), RECOMP_SETTINGS=str(data / 'settings.ini'),
                 RECOMP_ACCOUNT_SERVER='127.0.0.1:%d' % svc.port, RECOMP_ACCOUNT_TEST_TOKEN=svc.token,
                 RECOMP_NETPLAY_NAME='Cloud Tester', RECOMP_CLOUD_PC=pc,
                 # the one-time notices (the cloud question among them) without the 8 s wait for the update check
                 RECOMP_TEST_NOTICE='cloud',
                 RECOMP_WINDOW_SHOT=str(shot), RECOMP_WINDOW_SHOT_FRAME=str(frame))
        if page:
            e['RECOMP_LAUNCHER_PAGE'] = page
        if steps:
            e['RECOMP_TEST_SAVES'] = steps
        if prompt:
            e['RECOMP_TEST_CLOUD_PROMPT'] = prompt
        with log.open('w', encoding='utf-8', errors='replace') as out:
            p = subprocess.Popen([str(EXE), '--launcher'], cwd=ROOT, env=e, stdout=out, stderr=subprocess.STDOUT)
            OWN.append(p)
            try:
                p.wait(timeout=90)
            finally:
                if p.poll() is None:
                    p.kill()
                    p.wait()
        text = log.read_text(errors='replace')
        png = None
        if shot.exists():
            from PIL import Image
            png = shot.with_suffix('.png')
            with Image.open(shot) as picture:
                picture.save(png)
            shot.unlink()
            if a.shots:
                a.shots.mkdir(parents=True, exist_ok=True)
                shutil.copy2(png, a.shots / png.name)
        return text, png

    # 1. PC A: the first start uploads
    text, png = launcher(A, 'PC-A', '01-a-home-upload')
    v = svc.versions()
    step('A uploads at start', png and v and len(v) == 1 and v[0][7] == 'PC-A' and v[0][5] == '3' and
         '[CLOUD] check: 0 cloud version(s)' in text and 'HTTP 201' in text, png, v)
    text, png = launcher(A, 'PC-A', '02-a-cloud-in-sync', page='saves-cloud', frame=120)
    step('A in sync, the cloud view', png and ': in-sync' in text, png)
    first = save_tree(A / 'save')

    # 2. PC B, empty: offered, nothing changes until it is answered; then restored
    text, png = launcher(B, 'PC-B', '03-b-offer-restore', frame=160)
    step('B is offered the cloud save', png and 'start-up question: offer-restore' in text and
         not save_tree(B / 'save'), png)
    text, png = launcher(B, 'PC-B', '04-b-restored', prompt='120:restore', frame=200)
    made = backups(B)
    step('B restores it', png and save_tree(B / 'save') == first and [r for _, r, _ in made] == ['cloud'] and
         'answered: restore (done)' in text, png, [m[:2] for m in made])

    # 3. Both change: A uploads; B meets a conflict and keeps both
    (title_a / folder_of('VY2') / 'VY2').write_bytes(profile('VY2 after a Story fight on A'))
    time.sleep(UPLOAD_GAP)
    text, png = launcher(A, 'PC-A', '05-a-changed-uploads')
    v = svc.versions()
    step('A uploads its change', v and len(v) == 2 and ': upload' in text, png, [x[0] for x in v or []])
    a_second = save_tree(A / 'save')
    title_b = B / 'save' / 'UserData' / '45410049'
    (title_b / folder_of('KID') / 'KID').write_bytes(profile('KID after a fight on B'))
    text, png = launcher(B, 'PC-B', '06-b-conflict', frame=160)
    step('B: a conflict is asked', png and 'start-up question: conflict' in text and len(svc.versions()) == 2, png)
    b_mine = save_tree(B / 'save')
    time.sleep(UPLOAD_GAP)
    text, png = launcher(B, 'PC-B', '07-b-kept-both', prompt='120:keep', frame=220)
    v = svc.versions()
    kept = [d for n, r, d in backups(B) if r == 'cloud']
    newest_cloud_backup = save_tree(kept[0]) if kept else {}
    step('B keeps both', v and len(v) == 3 and v[0][7] == 'PC-B' and save_tree(B / 'save') == b_mine and
         newest_cloud_backup == {k: h for k, h in a_second.items()}, png, [x[0] for x in v or []])

    # 4. A: the cloud is newer: asked; on the Saves page Restore lists what changes and restores after OK
    text, png = launcher(A, 'PC-A', '08-a-cloud-newer', frame=160)
    step('A: newer cloud saves are asked', png and 'start-up question: cloud-newer' in text and save_tree(A / 'save') == a_second, png)
    text, png = launcher(A, 'PC-A', '09-a-restore-confirm', page='saves-cloud', steps='60:cloud-restore:0',
                         prompt='30:later', frame=150)
    step('A: the restore confirm', png and save_tree(A / 'save') == a_second, png)
    text, png = launcher(A, 'PC-A', '10-a-restored', page='saves-cloud', steps='60:cloud-restore:0;120:cloud-restore-ok',
                         prompt='30:later', frame=190)
    made = backups(A)
    step('A restores the newest', png and save_tree(A / 'save') == b_mine and made and made[0][1] == 'restore' and
         save_tree(made[0][2]) == a_second, png, [m[:2] for m in made[:3]])

    # 5. The setting off: nothing goes up
    text, png = launcher(A, 'PC-A', '11-a-cloud-off', page='saves-cloud', steps='30:cloud-off', frame=80)
    ini = (A / 'settings.ini').read_text()
    step('cloud saves off', png and re.search(r'\[cloud\][^\[]*\nsaves *= *(false|0)\s', ini), png)
    put(title_a, 'NEWGUY')
    time.sleep(UPLOAD_GAP)
    text, png = launcher(A, 'PC-A', '12-a-off-no-upload', frame=100)
    step('off: nothing sent', len(svc.versions()) == 3 and 'upload:' not in text, png)

    # 6. On again: the change goes up; then Delete my cloud saves (asks, then empties the cloud)
    text, png = launcher(A, 'PC-A', '13-a-delete-confirm', page='saves-cloud', steps='20:cloud-on;90:cloud-delete', frame=140)
    v = svc.versions()
    step('on again uploads; delete asks', png and v and len(v) == 4 and v[0][7] == 'PC-A', png)
    text, png = launcher(A, 'PC-A', '14-a-deleted', page='saves-cloud', steps='60:cloud-delete;90:cloud-delete-ok', frame=160)
    step('delete my cloud saves', png and svc.versions() == [] and (A / 'save' / 'UserData').is_dir(), png)
    status, _ = svc.get('/api/saves/v1')
    step('a deleted version is gone', status == 404, None, status)

    # 7. --real-save: a profile the game itself made (e.g. the harness's intro route with
    # DEFJAM_HARNESS_SNAPSHOT_SAVE) goes up from PC C and is restored into an empty PC D, for the game boot after
    if a.real_save:
        C, D = root('C'), root('D')
        shutil.copytree(a.real_save / 'UserData', C / 'save' / 'UserData')
        (D / 'save').mkdir()
        time.sleep(UPLOAD_GAP)
        text, png = launcher(C, 'PC-C', '15-c-real-upload')
        v = svc.versions()
        step('C uploads a game-made profile', v and len(v) == 1 and v[0][7] == 'PC-C', png, v)
        text, png = launcher(D, 'PC-D', '16-d-real-restored', prompt='120:restore', frame=200)
        step('D restores it', save_tree(D / 'save') == save_tree(C / 'save') and 'answered: restore (done)' in text, png,
             'boot it: python tools/defjam-mcp/dj.py scenario boot --goto story --fixture ' + str(D))

    (run / 'receipts.json').write_text(json.dumps(receipts, indent=1) + '\n')
    print(json.dumps(dict(result='PASS' if not faults else 'FAIL', faults=faults, folder=str(run))))
    return 0 if not faults else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    finally:
        for p in OWN:
            if p.poll() is None:
                p.kill()

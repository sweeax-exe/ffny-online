#!/usr/bin/env python3
"""The launcher's Saves page on Windows, end to end, on a new data root with synthetic profiles only.

    python tools/defjam-mcp/dj.py lock run -- python scripts/test-saves-launcher.py [--out logs/b47-saves]

Builds a throw-away data folder (<out>/run-<time>/data) with made-up profiles (never a player's
save, never game data), then starts build/win-x64-release/defjam_recomp.exe several times with
the launcher's test switches: RECOMP_LAUNCHER_PAGE=saves opens the page, RECOMP_TEST_SAVES presses
its buttons (Back up now, Import Xbox saves from a made-up zip, Replace, Restore) and
RECOMP_WINDOW_SHOT writes the window's own picture (never the desktop) as a BMP, kept as PNG.
Then it starts the game itself on that data folder (--no-launcher, a test copy beside nothing
else), checks the daily backup and that a launcher refuses to restore while the game runs, and
stops its own game process. Each step prints one JSON line; receipts.json sums them up.

Run it under the dj lock: the last part starts the game.
"""
import argparse
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('save_import', ROOT / 'scripts/save-import.py')
save_import = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(save_import)
EXE = ROOT / 'build/win-x64-release/defjam_recomp.exe'
SAFE = {
    'RECOMP_ACCOUNT_SERVER': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_EU': '127.0.0.1:9',
    'RECOMP_NETPLAY_SERVER_NA': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_WEST': '127.0.0.1:9',
    'RECOMP_NO_UPDATE_CHECK': '1', 'RECOMP_WINDOW_BACKGROUND': '1',
}
OWN = []


def profile(seed, key=None, created=False):
    size = 0x5990
    body = bytearray(hashlib.shake_256(seed.encode()).digest(size))
    body[0:4] = b'20CM'
    body[4:16] = (size).to_bytes(4, 'little') + (0x40).to_bytes(4, 'little') + (size - 0x8C).to_bytes(4, 'little')
    body[0x80 + 0x4D8A] = 1 if created else 0
    blob = bytes(body)
    return save_import.resign(blob, key) if key is not None else blob


def meta(name):
    return b'\xff\xfe' + ('Name=' + name + '\r\n').encode('utf-16-le')


def put(title, name, blob):
    folder = title / save_import.folder_name(name)
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'SaveMeta.xbx').write_bytes(meta(name))
    (folder / name).write_bytes(blob)
    (folder / 'saveimage.xbx').write_bytes(b'\0' * 4096)
    return folder


def tree(root):
    """The files under root and their hashes (only ever UserData: the game adds 5 GB of sparse partition
    images beside it)."""
    return {p.relative_to(root).as_posix(): hashlib.sha1(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob('*')) if p.is_file()}


def backups(data):
    out = []
    folder = data / 'save-backups'
    for d in sorted(folder.iterdir(), reverse=True) if folder.is_dir() else []:
        if d.name == 'quarantine':
            continue
        info = dict(l.split('=', 1) for l in (d / 'backup.txt').read_text().split('\n') if '=' in l)
        out.append((d.name, info.get('reason')))
    return out


def env_for(data, settings, extra):
    e = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_')}
    e.update(SAFE)
    e.update(DEFJAM_DATA=str(data), DEFJAM_LOG_DIR=str(data / 'logs'), RECOMP_SETTINGS=str(settings))
    e.update(extra)
    return e


def launcher(run, data, settings, name, steps='', frame=45, page='saves'):
    """One launcher window: the steps, then its picture at `frame`, then it closes itself."""
    shot = run / (name + '.bmp')
    log = run / (name + '.log')
    extra = {'RECOMP_WINDOW_SHOT': str(shot), 'RECOMP_WINDOW_SHOT_FRAME': str(frame)}
    if page:
        extra['RECOMP_LAUNCHER_PAGE'] = page
    if steps:
        extra['RECOMP_TEST_SAVES'] = steps
    with log.open('w', encoding='utf-8', errors='replace') as out:
        p = subprocess.Popen([str(EXE), '--launcher'], cwd=ROOT, env=env_for(data, settings, extra), stdout=out,
                             stderr=subprocess.STDOUT)
        OWN.append(p)
        try:
            code = p.wait(timeout=60)
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
    return code, text, png


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--out', type=Path, default=ROOT / 'logs' / 'b47-saves')
    a = ap.parse_args()
    if not EXE.is_file():
        print('not built: ' + str(EXE))
        return 2
    run = (a.out / ('run-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))).resolve()
    data = run / 'data'
    title = data / 'save' / 'UserData' / '45410049'
    (data / 'logs').mkdir(parents=True)
    vy2 = put(title, 'VY2', profile('VY2', key=bytes(16), created=True))
    put(title, 'KID', profile('KID', key=bytes(16)))
    put(title, 'XBOX1', profile('XBOX1', key=bytes(range(16))))         # copied from an Xbox, never signed again
    # a profile the game deleted earlier, kept in quarantine (src/hooks/pc_launcher_saves.cpp)
    gone = data / 'save-backups' / 'quarantine' / ('20261101-120000-' + save_import.folder_name('GONE'))
    gone.mkdir(parents=True)
    (gone / 'SaveMeta.xbx').write_bytes(meta('GONE'))
    (gone / 'GONE').write_bytes(profile('GONE', key=bytes(16)))
    (gone / 'saveimage.xbx').write_bytes(b'\0' * 4096)
    options = title / save_import.folder_name('Options')
    options.mkdir()
    (options / 'SaveMeta.xbx').write_bytes(meta('Options'))
    (options / 'Options').write_bytes(b'\0' * 5404)
    settings = data / 'settings.ini'
    settings.write_text('[online]\ncreated_names = %s/VY2=Iron Mike\n[debug]\nskip_intro = 1\n' % vy2.name)
    source = run / 'xbox-saves.zip'
    console = bytes(range(100, 116))
    with zipfile.ZipFile(source, 'w', zipfile.ZIP_DEFLATED) as zf:
        for folder, name in (('5A5A5A5A5A5A', 'NEWGUY'), ('6B6B6B6B6B6B', 'KID')):
            zf.writestr('UDATA/45410049/%s/SaveMeta.xbx' % folder, meta(name))
            zf.writestr('UDATA/45410049/%s/%s' % (folder, name), profile(name + ' from the Xbox', key=console))
            zf.writestr('UDATA/45410049/%s/saveimage.xbx' % folder, b'\1' * 4096)

    receipts, faults = [], []

    def step(name, ok, png, detail=''):
        r = dict(step=name, ok=bool(ok), picture=str(png) if png else None, detail=detail)
        receipts.append(r)
        if not ok:
            faults.append(name)
        print(json.dumps(r), flush=True)

    code, text, png = launcher(run, data, settings, '01-home', page=None)
    step('home', png and '[UI] launcher shown' in text, png)

    code, text, png = launcher(run, data, settings, '02-saves-list')
    step('list', png and code == 0, png)

    before = tree(data / 'save' / 'UserData')
    code, text, png = launcher(run, data, settings, '03-backup-now', steps='10:backup')
    made = backups(data)
    step('backup now', png and len(made) == 1 and made[0][1] == 'manual' and 'Backup made' in text, png, str(made))

    code, text, png = launcher(run, data, settings, '04-import-confirm', steps='10:import:%s' % source)
    step('import asks', png and 'import: 2 profile(s) found' in text and tree(data / 'save' / 'UserData') == before, png)

    code, text, png = launcher(run, data, settings, '05-imported', steps='10:import:%s;20:replace:KID;30:import-ok' % source,
                               frame=60)
    new = (title / save_import.folder_name('NEWGUY') / 'NEWGUY').read_bytes() if (title / save_import.folder_name('NEWGUY')).is_dir() else b''
    kid = (title / save_import.folder_name('KID') / 'KID').read_bytes()
    ok = (new == save_import.resign(profile('NEWGUY from the Xbox', key=console), bytes(16)) and
          kid == save_import.resign(profile('KID from the Xbox', key=console), bytes(16)) and
          backups(data)[0][1] == 'import' and 'Imported 2 profiles (1 replaced)' in text)
    step('import', png and ok, png, [l for l in text.splitlines() if '[SAVES]' in l][-1:])
    after_import = tree(data / 'save' / 'UserData')

    code, text, png = launcher(run, data, settings, '06-restore-confirm', steps='10:restore:1')
    step('restore asks', png and tree(data / 'save' / 'UserData') == after_import, png)

    restored_from = backups(data)[1][0]          # row 1: the backup made by "Back up now"
    code, text, png = launcher(run, data, settings, '07-restored', steps='10:restore:1;20:restore-ok', frame=55)
    ok = (tree(data / 'save' / 'UserData') == before and backups(data)[0][1] == 'restore' and
          tree(data / 'save-backups' / backups(data)[0][0] / 'UserData') == after_import and
          'Backup restored from' in text)
    step('restore', png and ok, png, restored_from)

    code, text, png = launcher(run, data, settings, '07b-bring-back', steps='10:bring:0;20:import-ok', frame=40)
    back = title / save_import.folder_name('GONE') / 'GONE'
    step('bring back a deleted profile', png and back.is_file() and back.read_bytes() == (gone / 'GONE').read_bytes() and
         'Imported 1 profile' in text, png)

    # The game itself on this data folder: the daily backup, then a launcher that must refuse.
    game_env = env_for(data, 'none', {'RECOMP_AUDIO_MUTE': '1'})
    game_log = run / '08-game.log'
    n_before = len(backups(data))
    # a backup from today exists (the ones above): the game makes none
    with game_log.open('w', encoding='utf-8', errors='replace') as out:
        game = subprocess.Popen([str(EXE), '--no-launcher'], cwd=ROOT, env=game_env, stdout=out, stderr=subprocess.STDOUT)
        OWN.append(game)
        t0 = time.time()
        while time.time() - t0 < 60 and '[SAVES] daily backup' not in game_log.read_text(errors='replace'):
            time.sleep(0.25)
        time.sleep(2)
        listed = backups(data)       # a restore always backs up first: no new backup, nothing restored
        try:
            code, text, png = launcher(run, data, settings, '09-refused-while-running', steps='10:restore:0')
        finally:
            game.kill()
            game.wait()
    glog = game_log.read_text(errors='replace')
    step('daily backup skipped (one from today)', 'there is one from today already' in glog and len(backups(data)) == n_before,
         None)
    # (the booting game writes TitleMeta.xbx / TitleImage.xbx itself, so the files are not compared here)
    step('refused while the game runs', png and 'Close the game first' in text and 'Backup restored' not in text and
         backups(data) == listed, png)

    # no backup from today any more (their times moved two days back): the game makes one
    yesterday = int(time.time()) - 2 * 86400
    for d in (data / 'save-backups').iterdir():
        if d.name == 'quarantine':
            continue
        lines = [l for l in (d / 'backup.txt').read_text().split('\n') if l and not l.startswith('when=')]
        (d / 'backup.txt').write_text('when=%d\n' % yesterday + '\n'.join(lines) + '\n')
    game_log = run / '10-game-daily.log'
    with game_log.open('w', encoding='utf-8', errors='replace') as out:
        game = subprocess.Popen([str(EXE), '--no-launcher'], cwd=ROOT, env=game_env, stdout=out, stderr=subprocess.STDOUT)
        OWN.append(game)
        t0 = time.time()
        while time.time() - t0 < 60 and '[SAVES] daily backup' not in game_log.read_text(errors='replace'):
            time.sleep(0.25)
        game.kill()
        game.wait()
    glog = game_log.read_text(errors='replace')
    newest = backups(data)
    step('daily backup at game start', '[SAVES] daily backup made' in glog and newest[0][1] == 'start' and len(newest) == n_before + 1,
         None, newest[0][0])

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

#!/usr/bin/env python3
"""The launcher's community content sync and Content page on Windows, end to end, on a new data root.

    python tools/defjam-mcp/dj.py lock run -- python scripts/test-content-launcher.py \
        --catalogue http://127.0.0.1:<port>/updates/content/index.json --keys <keys.json> [--out logs/content]

Needs a local account service with -content-serve and a test content key (docs/content-packs.md, "Testing");
--keys is a catalogue-keys.json with that test key's public half. Builds a throw-away data folder
(<out>/run-<time>/data, no game data, no player's files) and starts build/win-x64-release/defjam_recomp.exe
with the launcher's test switches:

1. RECOMP_LAUNCHER_PAGE=content: the launcher syncs the catalogue at its start (setup/mods.py sync); the
   Content page's picture is taken once the sync is done (RECOMP_WINDOW_SHOT). Checks content.ini, the pack
   folders and the receipts.
2. A second start: nothing to download ("up to date").
3. RECOMP_LAUNCHER_PAGE=general with a language pack in the catalogue: the Game language list (picture).
4. The player switches a replacement pack off ([content] off): the third start keeps it installed and
   says it is off.
Each step prints one JSON line; receipts.json sums them up. The account service is the caller's.
"""
import argparse
import configparser
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'build/win-x64-release/defjam_recomp.exe'
SAFE = {
    'RECOMP_ACCOUNT_SERVER': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_EU': '127.0.0.1:9',
    'RECOMP_NETPLAY_SERVER_NA': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_WEST': '127.0.0.1:9',
    'RECOMP_NO_UPDATE_CHECK': '1', 'RECOMP_WINDOW_BACKGROUND': '1',
}
OWN = []


def env_for(data, settings, extra, catalogue, keys):
    e = {k: v for k, v in os.environ.items() if not k.startswith(('RECOMP_', 'FFNY_CONTENT'))}
    e.update(SAFE)
    e.update(DEFJAM_DATA=str(data), DEFJAM_LOG_DIR=str(data / 'logs'), RECOMP_SETTINGS=str(settings),
             FFNY_CONTENT_URL=catalogue, FFNY_CONTENT_TEST_KEYS=str(keys), DEFJAM_MOD_PYTHON=sys.executable)
    e.update(extra)
    return e


def launcher(run, data, settings, name, page, catalogue, keys, frame=420):
    """One launcher window: its picture at `frame` (about 7 s, after the sync), then it closes itself."""
    shot, log = run / (name + '.bmp'), run / (name + '.log')
    extra = {'RECOMP_WINDOW_SHOT': str(shot), 'RECOMP_WINDOW_SHOT_FRAME': str(frame), 'RECOMP_LAUNCHER_PAGE': page}
    with log.open('w', encoding='utf-8', errors='replace') as out:
        p = subprocess.Popen([str(EXE), '--launcher'], cwd=ROOT, env=env_for(data, settings, extra, catalogue, keys),
                             stdout=out, stderr=subprocess.STDOUT)
        OWN.append(p)
        try:
            code = p.wait(timeout=120)
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
    return code, log.read_text(errors='replace'), png


def content_ini(data):
    path = data / 'mod-manager' / 'content.ini'
    c = configparser.ConfigParser(interpolation=None)
    if path.exists():
        c.read(path, encoding='utf-8')
    return {s[5:]: dict(c[s]) for s in c.sections() if s.startswith('pack.')}


def set_value(settings, section, key, value):
    """One value in the launcher's settings.ini (it writes every section with its comments)."""
    lines, inside, done = settings.read_text(encoding='utf-8').splitlines(), False, False
    for i, line in enumerate(lines):
        if line.startswith('['):
            inside = line.strip() == '[%s]' % section
        elif inside and line.split('=')[0].strip() == key:
            lines[i], done = '%s = %s' % (key, value), True
    if not done:
        lines += ['[%s]' % section, '%s = %s' % (key, value)]
    settings.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def progress(data):
    path = data / 'mod-manager' / 'progress.ini'
    return path.read_text(encoding='utf-8', errors='replace') if path.exists() else ''


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--catalogue', required=True)
    ap.add_argument('--keys', type=Path, required=True)
    ap.add_argument('--out', type=Path, default=ROOT / 'logs' / 'content-launcher')
    ap.add_argument('--expect', default='es-chanock', help='a pack id the catalogue lists')
    ap.add_argument('--account-server', default='', help='127.0.0.1:<port>: also the Community tab (gallery)')
    ap.add_argument('--token', default='', help='a test session of that service (votes show as yours)')
    a = ap.parse_args()
    if not EXE.exists():
        print(json.dumps(dict(step='build', ok=False, why='no %s' % EXE)))
        return 1
    run = a.out / ('run-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    data = run / 'data'
    (data / 'save').mkdir(parents=True)
    settings = data / 'settings.ini'
    settings.write_text('[launcher]\ngpu_notice_seen=1\n', encoding='utf-8')
    receipts = []

    def step(name, ok, **info):
        row = dict(step=name, ok=bool(ok), **info)
        receipts.append(row)
        print(json.dumps(row), flush=True)

    code, log, png = launcher(run, data, settings, '1-first-sync', 'content', a.catalogue, a.keys)
    packs = content_ini(data)
    step('first-sync', a.expect in packs and (data / 'mods' / a.expect).is_dir(), exit=code,
         packs=sorted(packs), progress=progress(data).strip().splitlines()[1:2], shot=str(png))

    code, log, png = launcher(run, data, settings, '2-again', 'content', a.catalogue, a.keys)
    step('second-sync-no-download', 'up to date' in progress(data), exit=code,
         progress=progress(data).strip().splitlines()[1:2], shot=str(png))

    language = [k for k, v in packs.items() if v.get('type') == 'language']
    if language:      # chosen as the Game language (what the Settings list writes)
        set_value(settings, 'language', 'pack', language[0])
    code, log, png = launcher(run, data, settings, '3-game-language', 'general', a.catalogue, a.keys, frame=90)
    step('game-language-page', png is not None, exit=code, language=language[:1], shot=str(png))

    replacement = [k for k, v in packs.items() if v.get('kind') == 'replacement']
    if replacement:
        set_value(settings, 'content', 'off', replacement[0])
        code, log, png = launcher(run, data, settings, '4-switched-off', 'content', a.catalogue, a.keys)
        step('switched-off-kept', (data / 'mods' / replacement[0]).is_dir() and replacement[0] in content_ini(data),
             exit=code, off=replacement[0], shot=str(png))
    if a.account_server:      # the Community tab: the gallery of the same local account service
        extra = {'RECOMP_ACCOUNT_SERVER': a.account_server, 'RECOMP_CONTENT_TAB': 'community'}
        if a.token:
            extra['RECOMP_ACCOUNT_TEST_TOKEN'] = a.token
        shot, log_path = run / '5-community.bmp', run / '5-community.log'
        extra.update(RECOMP_WINDOW_SHOT=str(shot), RECOMP_WINDOW_SHOT_FRAME='300', RECOMP_LAUNCHER_PAGE='content')
        with log_path.open('w', encoding='utf-8', errors='replace') as out:
            p = subprocess.Popen([str(EXE), '--launcher'], cwd=ROOT, env=env_for(data, settings, extra, a.catalogue, a.keys),
                                 stdout=out, stderr=subprocess.STDOUT)
            OWN.append(p)
            code = p.wait(timeout=120)
        png = None
        if shot.exists():
            from PIL import Image
            png = shot.with_suffix('.png')
            with Image.open(shot) as picture:
                picture.save(png)
            shot.unlink()
        step('community-tab', png is not None, exit=code, shot=str(png))
    (run / 'receipts.json').write_text(json.dumps(receipts, indent=1), encoding='utf-8')
    print(json.dumps(dict(run=str(run), ok=all(r['ok'] for r in receipts))))
    return 0 if all(r['ok'] for r in receipts) else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    finally:
        for p in OWN:
            if p.poll() is None:
                p.kill()

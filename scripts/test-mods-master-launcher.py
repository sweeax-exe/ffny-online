#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""The Content page's "Turn all mods off / Turn mods back on" button on Windows, end to end, on a new data root.

    python tools/defjam-mcp/dj.py lock run -- python scripts/test-mods-master-launcher.py [--packs 60] [--out logs/mods-master]

Builds a throw-away data folder (<out>/run-<time>/data, no game data, no player's files) with --packs own packs
(mods/<id>/local.json + install.json, as FFNY Studio's "Use in my game" writes them) and starts
build/win-x64-release/defjam_recomp.exe --launcher on it with the Content page open (RECOMP_LAUNCHER_PAGE=content)
and RECOMP_TEST_CONTENT, which presses the master button the way a click does (src/hooks/pc_launcher_content.inc):

1. a fresh start: every own pack on; the button turns all mods off (the community content, every own pack): the list
   of switched-off ids is in mod-manager/own-off.txt (far past what a setting holds), the backup in mods-backup.txt
2. the button again: the same state as before (own packs all on again, the community content on), the backup gone
3. the same with half of the packs switched off before the first press: they stay off after "back on" (exactly as it was)
4. all off, backup deleted (an older build, a lost file): the button still brings the community content and every
   own pack back on
5. half of the packs off, then MY PACKS' "Turn all on" (the step "allon"): every own pack on
After each press the launcher composes again (setup/mods.py compose): the helper's status line is in the report. Each
step prints one JSON line (and a picture); the exit code is the number of failed steps.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'build/win-x64-release/defjam_recomp.exe'
SAFE = {
    'RECOMP_ACCOUNT_SERVER': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_EU': '127.0.0.1:9',
    'RECOMP_NETPLAY_SERVER_NA': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_SG': '127.0.0.1:9',
    'RECOMP_NETPLAY_SERVER_SA': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_MIA': '127.0.0.1:9',
    'RECOMP_NO_UPDATE_CHECK': '1', 'RECOMP_WINDOW_BACKGROUND': '1',
}
OWN = []


def make_packs(data, count):
    ids = []
    for i in range(count):
        pack = 'own-roster-fighter-%03d' % i
        folder = data / 'mods' / pack
        (folder / 'deltas').mkdir(parents=True)
        (folder / 'local.json').write_text(json.dumps(dict(schema=1, source='ffny-studio', name='Roster %d' % i,
                                                            kind='replacement', type='venue', version='1.0.0')),
                                           encoding='utf-8')
        (folder / 'install.json').write_text(json.dumps(dict(
            schema=1, type='venue', ops=[], entries=[dict(
                archive='assets/v2bg.viv', base_archive='assets/v2bg.viv', mode='replace', delta='deltas/a.ffdelta',
                original_sha256='0' * 64, result_sha256='1' * 64, result_size=4, entry='V2BG_03A.xsh',
                base_entry='V2BG_03A.xsh')])), encoding='utf-8')
        (folder / 'deltas' / 'a.ffdelta').write_bytes(b'FFDL')
        ids.append(pack)
    # the launcher's content.ini lists them (setup/mods.py writes it at a sync / compose; no sync here)
    lines = ['; test', '[content]', 'revision=1']
    for i, pack in enumerate(ids):
        lines += ['[pack.%s]' % pack, 'name=Roster %d' % i, 'kind=replacement', 'type=venue', 'version=1.0.0',
                  'content_sha256=aa', 'order=%d' % (900 + i), 'local=1', 'online_ok=1', 'composed=1']
    (data / 'mod-manager').mkdir(parents=True, exist_ok=True)
    (data / 'mod-manager' / 'content.ini').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return ids


def launcher(run, data, settings, name, steps, frame):
    shot, log = run / (name + '.bmp'), run / (name + '.log')
    e = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_')}
    e.update(SAFE)
    e.update(DEFJAM_DATA=str(data), DEFJAM_LOG_DIR=str(data / 'logs'), RECOMP_SETTINGS=str(settings),
             DEFJAM_MOD_PYTHON=sys.executable, RECOMP_CONTENT='0', RECOMP_WINDOW_SHOT=str(shot),
             RECOMP_WINDOW_SHOT_FRAME=str(frame), RECOMP_LAUNCHER_PAGE='content', RECOMP_TEST_CONTENT=steps)
    with log.open('w', encoding='utf-8', errors='replace') as out:
        p = subprocess.Popen([str(EXE), '--launcher'], cwd=ROOT, env=e, stdout=out, stderr=subprocess.STDOUT)
        OWN.append(p)
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
    return code, log.read_text(errors='replace'), png


def reports(log):
    out = []
    for m in re.finditer(r'\[CONTENT-TEST\] report community=(\d) own_on=(\d+) own_total=(\d+) backup=(\d)/(\d) '
                         r'all_off=(\d) status=\'(.*)\'', log):
        out.append(dict(community=int(m[1]), own_on=int(m[2]), own_total=int(m[3]), backup=int(m[4]), usable=int(m[5]),
                        all_off=int(m[6]), status=m[7]))
    return out


def composed(report):
    return len(set(re.findall(r'own-roster-fighter-\d+', report['status'])))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--packs', type=int, default=60)
    ap.add_argument('--out', type=Path, default=ROOT / 'logs' / 'mods-master')
    a = ap.parse_args()
    if not EXE.exists():
        print(json.dumps(dict(step='build', ok=False, why='no %s' % EXE)))
        return 1
    run = a.out / ('run-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    fails = 0

    def step(name, ok, **info):
        nonlocal fails
        fails += 0 if ok else 1
        print(json.dumps(dict(step=name, ok=bool(ok), **info)), flush=True)

    def fresh(name, off_ids=()):
        data = run / name / 'data'
        (data / 'save').mkdir(parents=True)
        settings = data / 'settings.ini'
        settings.write_text('[launcher]\ngpu_notice_seen=1\n', encoding='utf-8')
        ids = make_packs(data, a.packs)
        if off_ids:
            (data / 'mod-manager' / 'own-off.txt').write_text('\n'.join(ids[i] for i in off_ids) + '\n', encoding='utf-8')
        return data, settings, ids

    off_steps = '60:report;70:master;80:report;90:report'
    both = '60:report;70:master;80:report;90:master;100:report'

    # 1 + 2: all on -> off -> on again
    data, settings, ids = fresh('1-off-on')
    code, log, png = launcher(run / '1-off-on', data, settings, 'launcher', both, 2400)
    r = reports(log)
    own_off = (data / 'mod-manager' / 'own-off.txt')
    step('listed-all', len(r) == 3 and r[0]['own_total'] == a.packs and r[0]['own_on'] == a.packs and r[0]['community'] == 1,
         exit=code, reports=r[:1])
    step('all-off', len(r) == 3 and r[1]['all_off'] == 1 and r[1]['own_on'] == 0 and r[1]['community'] == 0 and r[1]['backup'] == 1
         and composed(r[1]) == 0,
         report=r[1:2], compose=re.findall(r'\[CONTENT\] compose [^\n]*', log)[:1], shot=str(png))
    # composed again: the helper's last line names every own pack it looked at (the test packs do not fit a real game,
    # so they are listed as "Not applied", which is how the test sees that compose got all of them)
    step('back-on', len(r) == 3 and r[2]['own_on'] == a.packs and r[2]['community'] == 1 and r[2]['backup'] == 0 and not r[2]['all_off']
         and composed(r[2]) == a.packs,
         report=r[2:3], compose=re.findall(r'\[CONTENT\] compose [^\n]*', log), own_off_left=own_off.read_text().split()
         if own_off.exists() else [])

    # 3: half of them were off before the first press: back on restores exactly that
    half = list(range(0, a.packs, 2))
    data, settings, ids = fresh('3-half', half)
    code, log, png = launcher(run / '3-half', data, settings, 'launcher', both, 2400)
    r = reports(log)
    step('half-restored', len(r) == 3 and r[0]['own_on'] == a.packs - len(half) and r[1]['own_on'] == 0
         and r[2]['own_on'] == a.packs - len(half) and r[2]['community'] == 1 and composed(r[2]) == a.packs - len(half),
         exit=code, reports=r, shot=str(png))

    # 4: everything off, no backup: the button turns the community content and every own pack on
    data, settings, ids = fresh('4-nobackup', list(range(a.packs)))
    (settings).write_text('[launcher]\ngpu_notice_seen=1\n[content]\ncommunity_offline = 0\n', encoding='utf-8')
    code, log, png = launcher(run / '4-nobackup', data, settings, 'launcher', '60:report;70:master;80:report;90:report', 2400)
    r = reports(log)
    step('no-backup-back-on', len(r) == 3 and r[0]['all_off'] == 1 and r[0]['backup'] == 0 and r[2]['own_on'] == a.packs
         and r[2]['community'] == 1 and composed(r[2]) == a.packs, exit=code, reports=r, shot=str(png))

    # 5: half of them off, MY PACKS' "Turn all on": every own pack on, composed again
    data, settings, ids = fresh('5-all-on', half)
    code, log, png = launcher(run / '5-all-on', data, settings, 'launcher', '60:report;70:allon;80:report;90:report', 2400)
    r = reports(log)
    step('turn-all-on', len(r) == 3 and r[0]['own_on'] == a.packs - len(half) and r[2]['own_on'] == a.packs
         and r[2]['community'] == 1 and composed(r[2]) == a.packs, exit=code, reports=r[:1] + r[2:], shot=str(png))
    return fails


if __name__ == '__main__':
    try:
        sys.exit(main())
    finally:
        for proc in OWN:
            if proc.poll() is None:
                proc.kill()

#!/usr/bin/env python3
"""Sign in without Discord in the launcher, end to end against a local account service, on a new data root.

    python tools/defjam-mcp/dj.py lock run -- python scripts/test-password-launcher.py --accounts <accounts.exe> [--out DIR]

<accounts.exe> is the account service built from the server repo (go build ./cmd/accounts), started here on
127.0.0.1 with -password (on) or -password-switch <missing file> (off), a fresh accounts file in the run folder.
The launcher (build/win-x64-release/defjam_recomp.exe --launcher) runs with its settings file and account.dat in
the run folder (never a player's), RECOMP_ACCOUNT_SERVER on that service, and writes its own window's picture
(RECOMP_WINDOW_SHOT, never the desktop). Steps, one picture each:

  off            the switch off: the Account card offers Discord only
  card-out       on: the card offers "Without Discord"
  dialog-signin  RECOMP_LAUNCHER_PAGE=password: the dialog with the pad keyboard
  dialog-new     password-new, filled in (RECOMP_TEST_PW_FILL), password shown hidden
  made           the dialog's own button pressed (RECOMP_TEST_PW_SUBMIT): the account is made, account.dat saved
  card-in        a new launcher start: signed in from account.dat, the card's Password button
  dialog-change  password-change (signed in)
  dialog-forgot  signed out again: the forgot page ("ask an admin")
  wrong          a wrong password through the dialog: the service's answer shown

Then the account service's file is checked: the account exists, no Discord id, an argon2id hash, the password
nowhere in it or in the service's log. Prints one JSON line per step and receipts.json.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'build/win-x64-release/defjam_recomp.exe'
SAFE = {
    'RECOMP_NETPLAY_SERVER_EU': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_NA': '127.0.0.1:9',
    'RECOMP_NETPLAY_SERVER_WEST': '127.0.0.1:9', 'RECOMP_NO_UPDATE_CHECK': '1', 'RECOMP_WINDOW_BACKGROUND': '1',
}
USER, PASSWORD = 'Pw_Tester', 'launcher test pass 1'
OWN = []


def free_port(start):
    for port in range(start, start + 200):
        with socket.socket() as s:
            try:
                s.bind(('127.0.0.1', port))
            except OSError:
                continue
        with socket.socket() as s2:
            try:
                s2.bind(('127.0.0.1', port + 1))
            except OSError:
                continue
        return port
    raise OSError('no free port')


def service(exe, run, port, on):
    secret = run / 'discord-secret.txt'
    secret.write_text('tests-no-discord')
    args = [str(exe), '-listen', '127.0.0.1:%d' % port, '-internal-listen', '127.0.0.1:%d' % (port + 1),
            '-data', str(run / 'accounts.json'), '-public-url', 'http://127.0.0.1:%d' % port,
            '-client-id', '0', '-client-secret-file', str(secret), '-official-ua', 'FFNY-Online-Launcher/']
    args += ['-password'] if on else ['-password-switch', str(run / 'password-enabled-missing')]
    log = open(run / ('accounts-%s.log' % ('on' if on else 'off')), 'wb')
    p = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    OWN.append(p)
    for _ in range(100):
        try:
            urllib.request.urlopen('http://127.0.0.1:%d/health' % port, timeout=1).read()
            return p
        except OSError:
            time.sleep(0.1)
    raise RuntimeError('the account service did not start')


def stop(p):
    if p.poll() is None:
        p.terminate()
        try:
            p.wait(5)
        except subprocess.TimeoutExpired:
            p.kill()


def launcher(run, port, name, frame, extra):
    shot = run / (name + '.bmp')
    log = run / (name + '.log')
    env = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_')}
    env.update(SAFE)
    env.update(DEFJAM_DATA=str(run / 'data'), DEFJAM_LOG_DIR=str(run / 'data' / 'logs'),
               RECOMP_SETTINGS=str(run / 'settings.ini'), RECOMP_ACCOUNT_SERVER='127.0.0.1:%d' % port,
               RECOMP_WINDOW_SHOT=str(shot), RECOMP_WINDOW_SHOT_FRAME=str(frame))
    env.update(extra)
    with log.open('w', encoding='utf-8', errors='replace') as out:
        p = subprocess.Popen([str(EXE), '--launcher'], cwd=ROOT, env=env, stdout=out, stderr=subprocess.STDOUT)
        OWN.append(p)
        try:
            code = p.wait(timeout=90)
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
    ap.add_argument('--accounts', type=Path, required=True)
    ap.add_argument('--out', type=Path, default=ROOT / 'logs' / 'pw-launcher')
    ap.add_argument('--pictures', type=Path, help='also copy the pictures here')
    a = ap.parse_args()
    if not EXE.is_file():
        print('not built: ' + str(EXE))
        return 2
    run = (a.out / ('run-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))).resolve()
    (run / 'data' / 'logs').mkdir(parents=True)
    port = free_port(28400)
    receipts = []

    def step(name, frame, extra, check):
        code, text, png = launcher(run, port, name, frame, extra)
        ok, why = check(text, png)
        r = {'step': name, 'ok': bool(ok and png), 'exit': code, 'picture': str(png) if png else None, 'why': why}
        lines = [l for l in text.splitlines() if '[ACCOUNT]' in l]
        r['account_lines'] = lines[-4:]
        print(json.dumps(r))
        receipts.append(r)
        if a.pictures and png:
            a.pictures.mkdir(parents=True, exist_ok=True)
            shutil.copy(png, a.pictures / png.name)
        return text

    try:
        off = service(a.accounts, run, port, False)
        step('off', 90, {}, lambda t, p: ('offered: 0' in t, 'the service says off'))
        stop(off)
        on = service(a.accounts, run, port, True)
        step('card-out', 90, {}, lambda t, p: ('offered: 1' in t, 'offered'))
        step('dialog-signin', 90, {'RECOMP_LAUNCHER_PAGE': 'password'}, lambda t, p: (True, ''))
        step('dialog-new', 90, {'RECOMP_LAUNCHER_PAGE': 'password-new', 'RECOMP_TEST_PW_FILL': USER + '|' + PASSWORD},
             lambda t, p: (True, ''))
        step('made', 130, {'RECOMP_LAUNCHER_PAGE': 'password-new', 'RECOMP_TEST_PW_FILL': USER + '|' + PASSWORD,
                           'RECOMP_TEST_PW_SUBMIT': '1'},
             lambda t, p: ('sign-in without Discord (new account): HTTP 201' in t and (run / 'account.dat').is_file(),
                           'HTTP 201 and account.dat beside settings.ini'))
        step('card-in', 90, {}, lambda t, p: ('saved session checked: HTTP 200' in t, 'the saved session works'))
        step('dialog-change', 90, {'RECOMP_LAUNCHER_PAGE': 'password-change'}, lambda t, p: (True, ''))
        (run / 'account.dat').unlink()
        step('dialog-forgot', 90, {'RECOMP_LAUNCHER_PAGE': 'password-forgot'}, lambda t, p: (True, ''))
        step('wrong', 130, {'RECOMP_LAUNCHER_PAGE': 'password', 'RECOMP_TEST_PW_FILL': USER + '|not the password',
                            'RECOMP_TEST_PW_SUBMIT': '1'},
             lambda t, p: ('sign-in without Discord (sign-in): HTTP 401' in t, 'HTTP 401'))
        stop(on)
        time.sleep(0.5)
        data = json.loads((run / 'accounts.json').read_text())
        accs = [x for x in data['accounts'] if (x.get('pw') or {}).get('login') == USER]
        raw = (run / 'accounts.json').read_text() + (run / 'accounts-on.log').read_text(errors='replace')
        file_ok = len(accs) == 1 and not accs[0].get('discord_id') and accs[0]['pw']['hash'].startswith('$argon2id$') \
            and PASSWORD not in raw
        r = {'step': 'service-file', 'ok': file_ok, 'accounts': len(data['accounts'])}
        print(json.dumps(r))
        receipts.append(r)
    finally:
        for p in OWN:
            if p.poll() is None:
                p.kill()
    (run / 'receipts.json').write_text(json.dumps(receipts, indent=1))
    ok = all(r['ok'] for r in receipts)
    print(json.dumps({'ok': ok, 'run': str(run)}))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())

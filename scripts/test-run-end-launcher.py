#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""Build 110: how the last run ended (src/hooks/run_end.h) and the question before it, end to end on Windows, on
throw-away data folders and a local account service with a test account.

    python tools/defjam-mcp/dj.py lock run -- python scripts/test-run-end-launcher.py --server <server repo> [--out DIR]
        [--shots DIR]

The question (pc_launcher_run_end.inc, answered with RECOMP_TEST_RUN_END) and the setting, each on a fresh data folder:

1. ask-unsent    a run's file from before the answer: deleted unsent, and the launcher closed with the question open
2. ask-again     the same folder: it asks once more; "Not now": counts as no (off); ask-never: no question any more
3. not-now keys  B, then Escape at the next start: off; Enter on the question as it opens is "Not now", never "Yes"
4. no            clicked; the game then plays: no file, nothing sent
5. yes           the pad goes to Yes and presses A; clicked Yes: the setting is on
6. off           a run's file with the setting off: deleted unsent
7. switch        the setting on (the game's run is sent), then off (the game's run is not, no file)
8. signed-out    the setting on, no session: the file deleted unsent; ask and no session: no question

Then what is sent, with the setting on: the game ends one way and the launcher that opens again sends it, a line in
the service's accounts.json.runends.jsonl: closed (WM_CLOSE), killed (TerminateProcess 1) and crash (a thread at 0x10).

Only processes of this build's exe are ended (the machine lock is held). Each step prints one JSON line; the exit code
is the failed steps' number. --shots: the question and the setting as PNGs.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import datetime
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / file)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


cu = load('cu_launcher', 'test-content-update-launcher.py')
cs = load('cloud_saves', 'test-cloud-saves.py')
user32, k32 = ctypes.WinDLL('user32'), ctypes.WinDLL('kernel32')
k32.OpenProcess.restype = wt.HANDLE
k32.CreateRemoteThread.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p,
                                   wt.DWORD, ctypes.c_void_p]
k32.CloseHandle.argtypes = [wt.HANDLE]


def game_window(pid):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(h, _):
        owner, name = wt.DWORD(), ctypes.create_unicode_buffer(64)
        user32.GetWindowThreadProcessId(h, ctypes.byref(owner))
        user32.GetClassNameW(h, name, 64)
        if owner.value == pid and name.value == 'DefJamRecompTranslator':
            found.append(h)
        return True
    user32.EnumWindows(each, 0)
    return found[0] if found else None


def crash(pid):
    h = k32.OpenProcess(0x43A, False, pid)      # create thread, query, VM operation/write/read
    k32.CreateRemoteThread(h, None, 0, ctypes.c_void_p(0x10), None, 0, None)
    k32.CloseHandle(h)


def own_exe():
    return [pid for pid, (_, cmd) in cu.processes().items() if str(cu.EXE).lower() in cmd.lower()]


def lines(svc):
    f = svc.folder / 'accounts.json.runends.jsonl'
    return [json.loads(l) for l in f.read_text().splitlines()] if f.is_file() else []


def settings_of(data):
    out = {}
    for l in (data / 'settings.ini').read_text(encoding='utf-8', errors='replace').splitlines():
        if '=' in l and not l.lstrip().startswith((';', '#')):
            k, v = l.split('=', 1)
            out[k.strip()] = v.split(';')[0].strip()
    return out.get('run_end_reports', 'ask'), out.get('run_end_asked', '0')


PENDING = ROOT / 'logs/run-end.txt'
BEGIN = 'begin build=110 cl=194435207 at=%d\r\n'


class Bench:
    def __init__(self, run, svc):
        self.run, self.svc, self.rows, self.n = run, svc, [], 0

    def data(self, mode=None):
        self.n += 1
        d = self.run / ('data%d' % self.n)
        (d / 'logs').mkdir(parents=True)
        (d / 'settings.ini').write_text('[debug]\nskip_intro = 1\n' + ('[online]\nrun_end_reports = %s\n' % mode if mode else ''))
        return d

    def env(self, data, steps, session=True, **extra):
        e = {'FFNY_CONTENT_URL': 'http://127.0.0.1:9/updates/content/index.json', 'RECOMP_TEST_NOTICE': 'run-end',
             'RECOMP_ACCOUNT_SERVER': '127.0.0.1:%d' % self.svc.port, 'RECOMP_NETPLAY_NAME': 'End Tester'}
        if session:
            e['RECOMP_ACCOUNT_TEST_TOKEN'] = self.svc.token
        if steps:
            e['RECOMP_TEST_RUN_END'] = steps
        e.update(extra)
        return cu.env_for(data, e)

    def row(self, name, ok, **facts):
        self.rows.append(dict(step=name, ok=bool(ok), **facts))
        print(json.dumps(self.rows[-1]), flush=True)

    def clean(self):
        until = time.time() + 20
        while time.time() < until and own_exe():
            for pid in own_exe():
                cu.end(pid, 1)
            time.sleep(1)

    def launcher(self, name, data, steps, session=True, pending=False, **extra):
        """A launcher start that ends by itself (its steps' quit, or the picture); its log's text."""
        log = self.run / (name + '.log')
        PENDING.unlink(missing_ok=True)
        if pending:
            PENDING.write_bytes((BEGIN % int(time.time() - 30) + 'closed\r\n').encode())
        with log.open('w', encoding='utf-8', errors='replace') as out:
            p = subprocess.Popen([str(cu.EXE), '--launcher'], cwd=ROOT, env=self.env(data, steps, session, **extra),
                                 stdout=out, stderr=subprocess.STDOUT)
            try:
                p.wait(timeout=60)
            except subprocess.TimeoutExpired:
                pass
            self.clean()
        return log.read_text(encoding='utf-8', errors='replace')

    def game(self, name, data, steps, how='close', session=True):
        """PLAY, some seconds of the game, its end, the launcher that opens again; what the service got."""
        log, before = self.run / (name + '.log'), len(lines(self.svc))
        PENDING.unlink(missing_ok=True)
        row = dict(reopened=False, got=[], wrote=False)
        with log.open('w', encoding='utf-8', errors='replace') as out:
            p = subprocess.Popen([str(cu.EXE), '--launcher'], cwd=ROOT,
                                 env=self.env(data, steps, session), stdout=out, stderr=subprocess.STDOUT)
            try:
                m = cu.wait_for(log, r'the game runs in its own process \((\d+)\)', 120)
                if not m:
                    row['why'] = 'the launcher did not start the game'
                    return row
                game = int(m.group(1))
                until = time.time() + 60
                while time.time() < until and not game_window(game):
                    time.sleep(0.5)
                time.sleep(5)
                row['wrote'] = PENDING.exists()
                if how == 'close':
                    user32.PostMessageW(game_window(game), 0x10, 0, 0)
                elif how == 'kill':
                    cu.end(game, 1)
                else:
                    crash(game)
                again, _ = cu.children_of(p.pid, 60, lambda cmd: '--launcher-child' not in cmd)
                row['reopened'] = bool(again)
                until = time.time() + (20 if row['wrote'] and session else 6)
                while time.time() < until and len(lines(self.svc)) == before:
                    time.sleep(0.5)
            finally:
                self.clean()
        row.update(got=lines(self.svc)[before:], left=PENDING.exists())
        return row


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--server', type=Path, default=ROOT / 'server', help='the private server repository')
    ap.add_argument('--out', type=Path, default=ROOT / 'logs' / 'b110-run-end')
    ap.add_argument('--shots', type=Path)
    a = ap.parse_args()
    run = (a.out / ('run-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))).resolve()
    run.mkdir(parents=True)
    exe = run / 'accounts.exe'
    subprocess.run(['go', 'build', '-o', str(exe), './cmd/accounts'], cwd=a.server / 'relay', check=True)
    svc = cs.Service(exe, run / 'service')
    b = Bench(run, svc)
    opens = '[RUN-END] the question opens'
    none = lambda: not lines(svc)
    try:
        d = b.data()
        t = b.launcher('ask-unsent', d, '240:quit', pending=True)
        b.row('ask-unsent', opens in t and 'deleted unsent' in t and none() and not PENDING.exists() and
              settings_of(d) == ('ask', '1'), settings=settings_of(d))
        t = b.launcher('ask-again', d, '150:later;300:quit')
        b.row('ask-again', opens + ' (time 2)' in t and 'the question: no' in t and none() and settings_of(d)[0] == 'off',
              settings=settings_of(d))
        t = b.launcher('ask-never', d, '240:quit', pending=True)
        b.row('ask-never', opens not in t and 'deleted unsent' in t and none() and settings_of(d)[0] == 'off')
        d = b.data()
        b.launcher('close-1', d, '240:quit')
        b.launcher('close-2', d, '240:quit')
        t = b.launcher('close-3', d, '240:quit')
        b.row('closed-twice', opens not in t and settings_of(d)[0] == 'off' and none(), settings=settings_of(d))
        d = b.data()
        t = b.launcher('not-now-b', d, '150:b;300:quit')
        first = 'the question: not now' in t and settings_of(d) == ('ask', '1')
        t = b.launcher('not-now-esc', d, '150:esc;300:quit')
        b.row('not-now keys', first and 'the question: not now' in t and settings_of(d)[0] == 'off' and none(),
              settings=settings_of(d))
        d = b.data()
        t = b.launcher('enter', d, '150:enter;300:quit')
        b.row('enter is not yes', 'the question: yes' not in t and settings_of(d) == ('ask', '1') and none(), settings=settings_of(d))
        d = b.data()
        g = b.game('no', d, '150:click-no;300:play')
        b.row('no', g['reopened'] and not g['wrote'] and not g['got'] and settings_of(d)[0] == 'off' and none(), **g)
        d = b.data()
        t = b.launcher('yes-pad', d, '150:left;170:left;190:a;300:quit')
        b.row('yes pad', 'the question: yes' in t and settings_of(d)[0] == 'on' and none(), settings=settings_of(d))
        d = b.data('off')
        t = b.launcher('off', d, '240:quit', pending=True)
        b.row('off', 'deleted unsent' in t and opens not in t and none() and not PENDING.exists())
        d = b.data()
        g = b.game('yes', d, '150:click-yes;300:play')
        b.row('yes', g['reopened'] and g['wrote'] and len(g['got']) == 1 and g['got'][0]['end'] == 'closed' and
              settings_of(d)[0] == 'on' and not g['left'], **g)
        n = len(lines(svc))
        g = b.game('switch-off', d, '150:set-off;300:play')
        off = g['reopened'] and not g['wrote'] and not g['got'] and settings_of(d)[0] == 'off'
        g2 = b.game('switch-on', d, '150:set-on;300:play')
        b.row('switch', off and g2['wrote'] and len(g2['got']) == 1 and len(lines(svc)) == n + 1, off=g, on=g2)
        n = len(lines(svc))
        d = b.data('on')
        t = b.launcher('signed-out', d, '240:quit', session=False, pending=True)
        d2 = b.data()
        t2 = b.launcher('signed-out-ask', d2, '240:quit', session=False)
        b.row('signed-out', 'not sent' in t and opens not in t and not PENDING.exists() and opens not in t2 and
              settings_of(d2) == ('ask', '0') and len(lines(svc)) == n)
        for name, how, want in (('closed', 'close', {'end': 'closed', 'code': '00000000'}),
                                ('killed', 'kill', {'end': 'exit', 'code': '00000001'}),
                                ('crash', 'crash', {'end': 'crash', 'code': 'C0000005'})):
            g = b.game(name, b.data('on'), '200:play', how)
            b.row(name, g['reopened'] and len(g['got']) == 1 and all(g['got'][0].get(k) == v for k, v in want.items()) and
                  (how != 'crash' or g['got'][0].get('at')) and not g['left'], **g)
        if a.shots:
            from PIL import Image
            a.shots.mkdir(parents=True, exist_ok=True)
            for name, steps, extra in (('question', '', {}), ('setting', '1:set-off;show', {'RECOMP_LAUNCHER_PAGE': 'basics'})):
                bmp = run / (name + '.bmp')
                b.launcher('shot-' + name, b.data(), steps, RECOMP_WINDOW_SHOT=str(bmp), RECOMP_WINDOW_SHOT_FRAME='120', **extra)
                with Image.open(bmp) as picture:
                    picture.convert('RGB').save(a.shots / (name + '.png'))
                bmp.unlink()
    finally:
        svc.proc.kill()
        b.clean()
        exe.unlink(missing_ok=True) if svc.proc.wait(10) is not None else None
    (run / 'receipts.json').write_text(json.dumps(b.rows, indent=1), encoding='utf-8')
    print(json.dumps({'dir': str(run), 'failed': sum(1 for r in b.rows if not r['ok'])}))
    return sum(1 for r in b.rows if not r['ok'])


if __name__ == '__main__':
    sys.exit(main())

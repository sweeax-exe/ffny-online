#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""Build 110: how the last run ended (src/hooks/run_end.h), end to end on Windows, on a throw-away data folder and a
local account service with a test account.

    python tools/defjam-mcp/dj.py lock run -- python scripts/test-run-end-launcher.py --server <server repo> [--out DIR]

Each step starts the launcher as "--launcher --content-update" (it presses PLAY itself), lets the game run some
seconds, ends it one way and waits for the launcher that opens again: its message must be in the service's
accounts.json.runends.jsonl.

1. closed   WM_CLOSE to the game's window (the X)
2. killed   TerminateProcess with code 1 (the Task Manager)
3. crash    a thread of the game starts at address 0x10: an access violation the crash handler reports
4. signedout  closed again, with no session: nothing arrives and the file is gone

Only processes this test started are ended. Each step prints one JSON line; the exit code is the failed steps' number.
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


def step(run, data, svc, name, how, want):
    (ROOT / 'logs/run-end.txt').unlink(missing_ok=True)      # another run's
    log, row, before = run / (name + '.log'), {'step': name, 'ok': False}, len(lines(svc))
    extra = {'FFNY_CONTENT_URL': 'http://127.0.0.1:9/updates/content/index.json',
             'RECOMP_ACCOUNT_SERVER': '127.0.0.1:%d' % svc.port}
    if want:
        extra['RECOMP_ACCOUNT_TEST_TOKEN'] = svc.token
    with log.open('w', encoding='utf-8', errors='replace') as out:
        p = subprocess.Popen([str(cu.EXE), '--launcher', '--content-update'], cwd=ROOT, env=cu.env_for(data, extra),
                             stdout=out, stderr=subprocess.STDOUT)
        cu.OWN.append(p.pid)
        try:
            m = cu.wait_for(log, r'the game runs in its own process \((\d+)\)', 120)
            if not m:
                row['why'] = 'the launcher did not start the game'
                return row
            game = int(m.group(1))
            cu.OWN.append(game)
            until = time.time() + 60
            while time.time() < until and not game_window(game):
                time.sleep(0.5)
            time.sleep(5)
            if how == 'close':
                user32.PostMessageW(game_window(game), 0x10, 0, 0)
            elif how == 'kill':
                cu.end(game, 1)
            else:
                crash(game)
            again, _ = cu.children_of(p.pid, 60, lambda cmd: '--launcher-child' not in cmd)
            until = time.time() + 45
            while want and time.time() < until and len(lines(svc)) == before:
                time.sleep(0.5)
            if not want:
                time.sleep(35)          # past the wait for a sign-in
            got = lines(svc)[before:]
            row.update(reopened=bool(again), got=got, file_left=(ROOT / 'logs/run-end.txt').exists())
            row['ok'] = bool(again) and not row['file_left'] and (
                len(got) == 1 and all(got[0].get(k) == v for k, v in want.items()) if want else not got)
            if want and want.get('end') == 'crash':
                row['ok'] = row['ok'] and bool(got[0].get('at'))
        finally:
            until = time.time() + 20        # every process of this build's exe is this test's: it holds the machine lock
            while time.time() < until and own_exe():
                for pid in own_exe():
                    cu.end(pid, 1)
                time.sleep(1)
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--server', type=Path, default=ROOT / 'server', help='the private server repository')
    ap.add_argument('--out', type=Path, default=ROOT / 'logs' / 'b110-run-end')
    a = ap.parse_args()
    run = (a.out / ('run-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))).resolve()
    data, nobody = run / 'data', run / 'nobody'     # the launcher saves the session it checked in its data folder
    for d in (data, nobody):
        (d / 'logs').mkdir(parents=True)
        (d / 'settings.ini').write_text('[debug]\nskip_intro = 1\n')
    exe = run / 'accounts.exe'
    subprocess.run(['go', 'build', '-o', str(exe), './cmd/accounts'], cwd=a.server / 'relay', check=True)
    svc = cs.Service(exe, run / 'service')
    rows = []
    try:
        for s in (('closed', 'close', {'end': 'closed', 'code': '00000000'}),
                  ('killed', 'kill', {'end': 'exit', 'code': '00000001'}),
                  ('crash', 'crash', {'end': 'crash', 'code': 'C0000005'}),
                  ('signedout', 'close', None)):
            rows.append(step(run, data if s[2] else nobody, svc, *s))
            print(json.dumps(rows[-1]), flush=True)
    finally:
        svc.proc.kill()
        for pid in cu.OWN:
            if pid in cu.processes():
                cu.end_tree(pid)
    (run / 'receipts.json').write_text(json.dumps(rows, indent=1), encoding='utf-8')
    print(json.dumps({'dir': str(run)}))
    return sum(1 for r in rows if not r['ok'])


if __name__ == '__main__':
    sys.exit(main())

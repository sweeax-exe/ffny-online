#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""Build 86, UPDATE NOW: the launcher's half of the hand-off on Windows, end to end, on a throw-away data folder.

    python tools/defjam-mcp/dj.py lock run -- python scripts/test-content-update-launcher.py [--out DIR]

The game's half (the popup, the gate, closing with the code) is covered by tests/unit/test_content_update.py and a
dj run with RECOMP_TEST_UPDATE_NOW_DRY. Here build/win-x64-release/defjam_recomp.exe runs as the player's launcher
does (no RECOMP_SETTINGS=none, so the launcher shows), with every server on 127.0.0.1:

1. waiting      "--launcher --content-update" against a catalogue server that answers only after 30 s: the launcher
                pressed PLAY itself and waits for the sync (its line says so); the picture is taken (RECOMP_WINDOW_SHOT)
2. update-code  the same against a catalogue that fails at once: PLAY goes by itself, the game starts with
                --launcher-child and --content-update; the test ends that game with the UPDATE NOW code (0x46465544):
                the launcher waiting for it opens again with "--launcher --content-update"
3. plain-close  the same, the game ended with 0: the launcher opens again with "--launcher" and no --content-update
Only processes this test started are ended (their trees). Each step prints one JSON line; the exit code is the
number of failed steps.
"""
import argparse
import ctypes
import datetime
import http.server
import json
import os
from pathlib import Path
import re
import socketserver
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'build/win-x64-release/defjam_recomp.exe'
CU_EXIT_CODE = 0x46465544
SAFE = {
    'RECOMP_ACCOUNT_SERVER': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_EU': '127.0.0.1:9',
    'RECOMP_NETPLAY_SERVER_NA': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_SG': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_SA': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_MIA': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_DFW': '127.0.0.1:9',
    'RECOMP_NETPLAY_SERVER_LAX': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_MEX': '127.0.0.1:9', 'RECOMP_NETPLAY_SERVER_BOG': '127.0.0.1:9',
    'RECOMP_NETPLAY_SERVER_WAW': '127.0.0.1:9',
    'RECOMP_NO_UPDATE_CHECK': '1', 'RECOMP_WINDOW_BACKGROUND': '1', 'RECOMP_SKIP_INTRO': '1',
}
OWN = []        # pids this test started (directly or through the launcher)


class Slow(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        time.sleep(30)
        self.send_error(503)

    def log_message(self, *a):
        pass


def env_for(data, extra):
    e = {k: v for k, v in os.environ.items() if not k.startswith(('RECOMP_', 'FFNY_CONTENT', 'DEFJAM_'))}
    e.update(SAFE)
    e.update(DEFJAM_DATA=str(data), DEFJAM_LOG_DIR=str(data / 'logs'), RECOMP_SETTINGS=str(data / 'settings.ini'),
             DEFJAM_MOD_PYTHON=sys.executable)
    e.update(extra)
    return e


def processes():
    """{pid: (parent, command line)} of every defjam_recomp.exe."""
    r = subprocess.run(['powershell', '-NoProfile', '-Command',
                        "Get-CimInstance Win32_Process -Filter \"Name='defjam_recomp.exe'\" | "
                        "ForEach-Object { '{0}|{1}|{2}' -f $_.ProcessId,$_.ParentProcessId,$_.CommandLine }"],
                       capture_output=True, text=True)
    out = {}
    for line in r.stdout.splitlines():
        parts = line.split('|', 2)
        if len(parts) == 3 and parts[0].strip().isdigit():
            out[int(parts[0])] = (int(parts[1]), parts[2])
    return out


def end(pid, code=1):
    k = ctypes.windll.kernel32
    h = k.OpenProcess(0x0001, False, pid)       # PROCESS_TERMINATE
    if h:
        k.TerminateProcess(h, code)
        k.CloseHandle(h)


def end_tree(pid):
    subprocess.run(['taskkill', '/PID', str(pid), '/T', '/F'], capture_output=True)


def wait_for(path, pattern, seconds):
    until = time.time() + seconds
    while time.time() < until:
        text = path.read_text(encoding='utf-8', errors='replace') if path.exists() else ''
        m = re.search(pattern, text)
        if m:
            return m
        time.sleep(0.25)
    return None


def children_of(parent, seconds, want=None):
    until = time.time() + seconds
    while time.time() < until:
        for pid, (pp, cmd) in processes().items():
            if pp == parent and (want is None or want(cmd)):
                OWN.append(pid)
                return pid, cmd
        time.sleep(0.25)
    return None, ''


def step_waiting(run, data, port):
    shot, log = run / 'waiting.bmp', run / 'waiting.log'
    extra = {'FFNY_CONTENT_URL': 'http://127.0.0.1:%d/updates/content/index.json' % port,
             'RECOMP_WINDOW_SHOT': str(shot), 'RECOMP_WINDOW_SHOT_FRAME': '150'}
    with log.open('w', encoding='utf-8', errors='replace') as out:
        p = subprocess.Popen([str(EXE), '--launcher', '--content-update'], cwd=ROOT, env=env_for(data, extra),
                             stdout=out, stderr=subprocess.STDOUT)
        OWN.append(p.pid)
        try:
            p.wait(timeout=90)
        except subprocess.TimeoutExpired:
            end_tree(p.pid)
    text = log.read_text(encoding='utf-8', errors='replace')
    ok = ('launcher in UPDATE NOW mode' in text and shot.exists() and 'launcher closed: quit' in text
          and 'the game runs in its own process' not in text)
    return {'step': 'waiting', 'ok': ok, 'shot': str(shot) if shot.exists() else None,
            'auto_play': 'launcher in UPDATE NOW mode' in text}


def step_close(run, data, name, code, want_update):
    log = run / (name + '.log')
    extra = {'FFNY_CONTENT_URL': 'http://127.0.0.1:9/updates/content/index.json'}
    row = {'step': name, 'ok': False}
    with log.open('w', encoding='utf-8', errors='replace') as out:
        p = subprocess.Popen([str(EXE), '--launcher', '--content-update'], cwd=ROOT, env=env_for(data, extra),
                             stdout=out, stderr=subprocess.STDOUT)
        OWN.append(p.pid)
        try:
            m = wait_for(log, r'the game runs in its own process \((\d+)\)', 120)
            if not m:
                row['why'] = 'the launcher did not start the game by itself'
                return row
            game = int(m.group(1))
            OWN.append(game)
            cmd = processes().get(game, (0, ''))[1]
            row['game_cmd'] = cmd.replace(str(EXE), '<exe>')
            row['game_has_flags'] = '--launcher-child' in cmd and '--content-update' in cmd
            time.sleep(3)
            end(game, code)
            again, again_cmd = children_of(p.pid, 30)
            if again:
                row['reopened_cmd'] = again_cmd.replace(str(EXE), '<exe>')
            try:
                row['launcher_exit'] = p.wait(timeout=30)
            except subprocess.TimeoutExpired:
                row['launcher_exit'] = None
            text = log.read_text(encoding='utf-8', errors='replace')
            said = 'the launcher opens again for UPDATE NOW' in text
            has_update = bool(again) and '--content-update' in again_cmd and '--launcher-child' not in again_cmd
            row['ok'] = (row['game_has_flags'] and bool(again) and '--launcher' in again_cmd and
                         has_update == want_update and said == want_update and row['launcher_exit'] == 0)
            if again:
                time.sleep(2)
                end_tree(again)
        finally:
            for pid in list(OWN):
                if pid in processes():
                    end_tree(pid)
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default=None)
    args = ap.parse_args()
    if not EXE.exists():
        print(json.dumps({'ok': False, 'why': 'no %s' % EXE}))
        return 1
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    run = Path(args.out) / ('run-' + stamp) if args.out else Path(tempfile.mkdtemp(prefix='ffny-update-now-'))
    data = run / 'data'
    (data / 'logs').mkdir(parents=True, exist_ok=True)
    server = socketserver.ThreadingTCPServer(('127.0.0.1', 0), Slow)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    rows = []
    try:
        rows.append(step_waiting(run, data, server.server_address[1]))
        rows.append(step_close(run, data, 'update-code', CU_EXIT_CODE, True))
        rows.append(step_close(run, data, 'plain-close', 0, False))
    finally:
        server.shutdown()
        for pid in OWN:
            if pid in processes():
                end_tree(pid)
    for r in rows:
        print(json.dumps(r))
    (run / 'receipts.json').write_text(json.dumps(rows, indent=1), encoding='utf-8')
    print(json.dumps({'dir': str(run)}))
    return sum(1 for r in rows if not r['ok'])


if __name__ == '__main__':
    sys.exit(main())

"""Discord Rich Presence (src/hooks/discord_rpc.c, discord_ipc.c), compiled natively.

The pure part: the IPC frame header, the JSON Discord takes (handshake, SET_ACTIVITY, the clear),
the text rules (control characters, broken UTF-8, Discord's 2..128 limits) and what each moment of
the game shows. The pipe part: the presence thread against a fake Discord, a named-pipe server in
this test that records every frame: it waits while Discord is not running, shakes hands, answers a
PING, sends the newest state at most once per interval, reconnects after a broken pipe, clears and
closes when switched off and on exit. Needs MSVC (`cl` on PATH); skips without it. Windows only."""
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "discord_rpc.h"

static void show(const char *tag, const DrpcGameState *g)
{
    DrpcActivity a;
    memset(&a, 0, sizeof(a));
    drpc_compose(g, &a);
    printf("%s|%s|%s|%s\n", tag, a.details, a.state, a.large_text);
}

static DrpcGameState base(int where)
{
    DrpcGameState g;
    memset(&g, 0, sizeof(g));
    g.where = where;
    snprintf(g.server, sizeof(g.server), "Europe");
    return g;
}

int main(void)
{
    uint8_t frame[64];
    char json[4096], clean[256];
    uint32_t op, len;
    int n;
    DrpcActivity a;
    DrpcGameState g;

    /* frames */
    n = drpc_frame(frame, sizeof(frame), DRPC_OP_FRAME, "{}", 2);
    printf("frame %d %02X%02X%02X%02X %02X%02X%02X%02X %c%c\n", n, frame[0], frame[1], frame[2], frame[3],
           frame[4], frame[5], frame[6], frame[7], frame[8], frame[9]);
    printf("header %d", drpc_get_header(frame, n, &op, &len));
    printf(" op %u len %u short %d\n", op, len, drpc_get_header(frame, 7, &op, &len));
    printf("toolong %d\n", drpc_frame(frame, sizeof(frame), 1, json, 57));
    drpc_put_header(frame, 0x01020304u, 0xA0B0C0D0u);
    printf("le %02X%02X%02X%02X %02X%02X%02X%02X\n", frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], frame[6], frame[7]);

    /* JSON */
    n = drpc_handshake_json(json, sizeof(json), DRPC_CLIENT_ID);
    printf("hello %d %s\n", n, json);
    memset(&a, 0, sizeof(a));
    snprintf(a.details, sizeof(a.details), "One on One \xC2\xB7 The Pit");
    snprintf(a.state, sizeof(a.state), "as \"Quote\\Back\" vs B");
    snprintf(a.large_image, sizeof(a.large_image), "ffny_logo");
    snprintf(a.large_text, sizeof(a.large_text), "FFNY Online");
    a.start = 1762000000LL;
    a.buttons = 1;
    n = drpc_set_activity_json(json, sizeof(json), 4242, &a, 7);
    printf("activity %d %s\n", n, json);
    memset(&a, 0, sizeof(a));
    snprintf(a.details, sizeof(a.details), "x");               /* too short: left out */
    snprintf(a.state, sizeof(a.state), "Main menu");
    printf("short %s\n", drpc_set_activity_json(json, sizeof(json), 1, &a, 1) ? json : "-");
    printf("clear %s\n", drpc_set_activity_json(json, sizeof(json), 4242, NULL, 8) ? json : "-");
    printf("nofit %d\n", drpc_set_activity_json(json, 40, 4242, NULL, 8));

    /* text rules */
    drpc_clean(clean, sizeof(clean), "  A\x01" "B\tC\x7F\n  D  ", 128);
    printf("clean [%s]\n", clean);
    drpc_clean(clean, sizeof(clean), "bad\xFF" "x\xC3(" "y\xE2\x82" "", 128);
    printf("utf8 [%s]\n", clean);
    drpc_clean(clean, sizeof(clean), "\xC3\xA9\xC3\xA9\xC3\xA9", 5);   /* never half a character */
    printf("cut [%s] %d\n", clean, (int)strlen(clean));
    {
        char longtext[400];
        memset(longtext, 'a', sizeof(longtext) - 1);
        longtext[sizeof(longtext) - 1] = 0;
        printf("max %d\n", drpc_clean(clean, sizeof(clean), longtext, DRPC_TEXT_MAX));
    }

    /* the composer */
    g = base(DRPC_IN_LAUNCHER); show("launcher", &g);
    g = base(DRPC_IN_MENUS); g.offline = DRPC_OFF_MAIN; show("main", &g);
    g = base(DRPC_IN_MENUS); g.offline = DRPC_OFF_STORY; show("story", &g);
    g = base(DRPC_IN_MENUS); g.offline = DRPC_OFF_BATTLE; g.offline_fight = 1; show("battlefight", &g);
    g = base(DRPC_IN_MENUS); g.offline = DRPC_OFF_MENUS; show("menus", &g);
    g = base(DRPC_IN_LOBBY); show("lobby", &g);
    g = base(DRPC_IN_LOBBY); g.ranked_search = 1; snprintf(g.server, sizeof(g.server), "US West"); show("ranked", &g);
    g = base(DRPC_IN_LOBBY); g.server[0] = 0; show("lobbynoserver", &g);
    g = base(DRPC_IN_ROOM); snprintf(g.mode, sizeof(g.mode), "Team 2v2"); g.players = 3; g.max = 4;
    snprintf(g.server, sizeof(g.server), "US East"); show("room", &g);
    g = base(DRPC_IN_ROOM); g.koth = 1; g.players = 5; g.max = 10; show("kothwait", &g);
    g = base(DRPC_IN_ROOM); g.koth = 1; g.koth_king = 1; g.koth_streak = 3; g.players = 5; g.max = 10; show("kothking", &g);

    g = base(DRPC_IN_MATCH); snprintf(g.mode, sizeof(g.mode), "One on One"); snprintf(g.venue, sizeof(g.venue), "The Pit");
    g.count = 2; g.you = 1; g.players = 2; g.max = 2;
    snprintf(g.names[0], DRPC_NAME, "Rival"); snprintf(g.names[1], DRPC_NAME, "Me");
    snprintf(g.fighters[1], DRPC_NAME, "Sticky Fingaz");
    show("1v1", &g);
    g.koth = 1; g.koth_king = 1; show("kothfight", &g);

    g = base(DRPC_IN_MATCH); snprintf(g.mode, sizeof(g.mode), "Team 2v2"); snprintf(g.venue, sizeof(g.venue), "Club Murder");
    g.format = DRPC_TEAM; g.count = 4; g.you = 2; g.players = 3; g.max = 4;
    snprintf(g.names[0], DRPC_NAME, "A"); snprintf(g.names[1], DRPC_NAME, "B"); snprintf(g.names[2], DRPC_NAME, "Me");
    g.names[3][0] = 0;  /* a CPU partner */
    show("team", &g);
    g.you = 0; show("team0", &g);

    g = base(DRPC_IN_MATCH); snprintf(g.mode, sizeof(g.mode), "Free for All"); snprintf(g.venue, sizeof(g.venue), "Bar");
    g.format = DRPC_ALL_VS_ALL; g.count = 4; g.you = 0;
    snprintf(g.names[0], DRPC_NAME, "Me"); snprintf(g.names[1], DRPC_NAME, "X"); snprintf(g.names[2], DRPC_NAME, "Y");
    snprintf(g.names[3], DRPC_NAME, "Z\x07");
    show("ffa", &g);

    /* every text stays inside Discord's limit, with long names in every place */
    g = base(DRPC_IN_MATCH); g.format = DRPC_ALL_VS_ALL; g.count = 4; g.you = 0;
    memset(g.mode, 'M', DRPC_NAME - 1); memset(g.venue, 'V', sizeof(g.venue) - 1);
    for (n = 0; n < 4; n++) memset(g.names[n], 'N', DRPC_NAME - 1);
    memset(&a, 0, sizeof(a));
    drpc_compose(&g, &a);
    printf("limits %d %d %d\n", (int)strlen(a.details), (int)strlen(a.state), (int)strlen(a.large_text));
    return 0;
}
'''

IPC_DRIVER = r'''
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "discord_ipc.h"

static const char *g_control;

/* the control file: "off", "quit", or "details|state"; unreadable for a moment while the test
 * replaces it: the last line read */
static int provider(DrpcActivity *out, void *user)
{
    static char last[300] = "";
    char line[300] = "";
    char *bar;
    FILE *f = fopen(g_control, "rb");
    (void)user;
    if (f) { size_t n = fread(line, 1, sizeof(line) - 1, f); line[n] = 0; fclose(f); }
    if (line[0]) memcpy(last, line, sizeof(last));
    else memcpy(line, last, sizeof(line));
    if (!strncmp(line, "off", 3) || !strncmp(line, "quit", 4) || !line[0]) return 0;
    bar = strchr(line, '|');
    if (bar) *bar = 0;
    snprintf(out->details, sizeof(out->details), "%s", line);
    snprintf(out->state, sizeof(out->state), "%s", bar ? bar + 1 : "");
    snprintf(out->large_image, sizeof(out->large_image), "%s", DRPC_LARGE_IMAGE);
    snprintf(out->large_text, sizeof(out->large_text), "%s", DRPC_LARGE_TEXT);
    out->start = 1762000000LL;
    out->buttons = 1;
    return 1;
}

int main(int argc, char **argv)
{
    DipcConfig c;
    char line[16];
    if (argc < 3) return 2;
    g_control = argv[2];
    memset(&c, 0, sizeof(c));
    c.pipe_prefix = argv[1];
    c.interval_ms = 600;
    c.retry_ms = 300;
    c.tick_ms = 20;
    if (!dipc_start(&c, provider, NULL)) return 3;
    for (;;) {             /* until the control file says quit */
        FILE *f = fopen(g_control, "rb");
        line[0] = 0;
        if (f) { size_t n = fread(line, 1, sizeof(line) - 1, f); line[n] = 0; fclose(f); }
        if (!strncmp(line, "quit", 4)) break;
        Sleep(20);
    }
    dipc_stop();
    printf("stopped connected=%d\n", dipc_connected());
    return 0;
}
'''


def compile_exe(folder, name, driver_text, sources):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native Discord presence test needs MSVC')
    driver = folder / (name + '.c')
    driver.write_text(driver_text, encoding='utf-8')
    exe = folder / (name + '.exe')
    r = subprocess.run([compiler, '/nologo', '/W3', '/WX', '/TC', '/utf-8', '/I' + str(HOOKS), str(driver)]
                       + [str(HOOKS / s) for s in sources] + ['/Fe:' + str(exe)],
                       cwd=folder, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    return exe


@pytest.fixture(scope='module')
def output(tmp_path_factory):
    folder = tmp_path_factory.mktemp('drpc')
    exe = compile_exe(folder, 'drpc', DRIVER, ['discord_rpc.c'])
    run = subprocess.run([str(exe)], capture_output=True)
    assert run.returncode == 0, run.stderr
    return run.stdout.decode('utf-8').replace('\r\n', '\n')


def line(out, key):
    m = re.search(r'^%s(.*)$' % re.escape(key), out, re.M)
    assert m, key
    return m.group(1).strip()


def shown(out, tag):
    m = re.search(r'^%s\|(.*)$' % re.escape(tag), out, re.M)
    assert m, tag
    return tuple(m.group(1).split('|'))


def test_frames_are_little_endian_opcode_and_length(output):
    assert line(output, 'frame') == '10 01000000 02000000 {}'
    assert line(output, 'header') == '1 op 1 len 2 short 0'
    assert line(output, 'toolong') == '0'
    assert line(output, 'le') == '04030201 D0C0B0A0'


def test_handshake_and_activity_json(output):
    hello = line(output, 'hello').split(' ', 1)[1]
    assert json.loads(hello) == {'v': 1, 'client_id': '1562214133242400993'}
    n, text = line(output, 'activity').split(' ', 1)
    assert int(n) == len(text.encode('utf-8'))
    msg = json.loads(text)
    assert msg['cmd'] == 'SET_ACTIVITY' and msg['nonce'] == '7'
    assert msg['args']['pid'] == 4242
    act = msg['args']['activity']
    assert act['details'] == 'One on One \u00b7 The Pit'
    assert act['state'] == 'as "Quote\\Back" vs B'
    assert act['timestamps'] == {'start': 1762000000}
    assert act['assets'] == {'large_image': 'ffny_logo', 'large_text': 'FFNY Online'}
    assert act['buttons'] == [{'label': 'Join the Discord', 'url': 'https://discord.gg/ffnyonline'},
                              {'label': 'Website', 'url': 'https://play.defjam.online'}]
    short = json.loads(line(output, 'short'))
    assert short['args']['activity'] == {'state': 'Main menu'}      # a one-character text is left out
    clear = json.loads(line(output, 'clear'))
    assert clear == {'cmd': 'SET_ACTIVITY', 'args': {'pid': 4242}, 'nonce': '8'}
    assert line(output, 'nofit') == '0'


def test_text_rules(output):
    assert line(output, 'clean') == '[A B C D]'
    assert line(output, 'utf8') == '[bad?x?(y??]'
    assert line(output, 'cut') == '[\u00e9\u00e9] 4'
    assert line(output, 'max') == '128'


def test_states(output):
    assert shown(output, 'launcher') == ('In the launcher', '', 'FFNY Online')
    assert shown(output, 'main') == ('Main menu', '', 'FFNY Online')
    assert shown(output, 'story') == ('Story mode', '', 'FFNY Online')
    assert shown(output, 'battlefight') == ('Battle mode', 'In a fight', 'FFNY Online')
    assert shown(output, 'menus') == ('In the menus', '', 'FFNY Online')
    assert shown(output, 'lobby') == ('Online lobby', 'Europe', 'FFNY Online')
    assert shown(output, 'ranked') == ('Online lobby', 'Ranked search \u00b7 US West', 'FFNY Online')
    assert shown(output, 'lobbynoserver') == ('Online lobby', '', 'FFNY Online')
    assert shown(output, 'room') == ('In a room: Team 2v2', 'US East \u00b7 3/4 players', 'FFNY Online')
    assert shown(output, 'kothwait') == ('King of the Hill', 'Waiting in line \u00b7 Europe \u00b7 5/10 players', 'FFNY Online')
    assert shown(output, 'kothking') == ('King of the Hill', 'King, 3 wins in a row \u00b7 Europe \u00b7 5/10 players',
                                         'FFNY Online')


def test_match_states(output):
    assert shown(output, '1v1') == ('One on One \u00b7 The Pit', 'as Sticky Fingaz vs Rival \u00b7 Europe',
                                    'FFNY Online \u00b7 Europe \u00b7 2/2 players')
    assert shown(output, 'kothfight')[:2] == ('King of the Hill \u00b7 The Pit',
                                              'King \u00b7 as Sticky Fingaz vs Rival \u00b7 Europe')
    assert shown(output, 'team') == ('Team 2v2 \u00b7 Club Murder', 'with CPU vs A & B \u00b7 Europe',
                                     'FFNY Online \u00b7 Europe \u00b7 3/4 players')
    assert shown(output, 'team0')[1] == 'with B vs Me & CPU \u00b7 Europe'
    assert shown(output, 'ffa')[:2] == ('Free for All \u00b7 Bar', 'vs X, Y & Z \u00b7 Europe')


def test_limits(output):
    details, state, large = (int(x) for x in line(output, 'limits').split())
    assert 2 <= details <= 128 and 2 <= state <= 128 and 2 <= large <= 128


# ---- the pipe, against a fake Discord ------------------------------------------------------------

if sys.platform == 'win32':
    import ctypes
    from ctypes import wintypes

    K32 = ctypes.WinDLL('kernel32', use_last_error=True)
    K32.CreateNamedPipeW.restype = wintypes.HANDLE
    K32.CreateNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
                                     wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    K32.ConnectNamedPipe.argtypes = [wintypes.HANDLE, ctypes.c_void_p]
    K32.DisconnectNamedPipe.argtypes = [wintypes.HANDLE]
    K32.ReadFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
    K32.WriteFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
    K32.CloseHandle.argtypes = [wintypes.HANDLE]
    K32.OpenThread.restype = wintypes.HANDLE
    K32.OpenThread.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    K32.CancelSynchronousIo.argtypes = [wintypes.HANDLE]
    INVALID = wintypes.HANDLE(-1).value


class FakeDiscord:
    """One pipe instance at `name`; records (time, connection, op, payload) of every frame received
    and 'connect'/'disconnect' events. Answers the handshake with READY, every command with an empty
    result, and sends one PING on the first connection."""

    def __init__(self, name):
        self.name = name
        self.events = []
        self.lock = threading.Lock()
        self.handle = None
        self.stop = False
        self.connection = 0
        self.kicked = False
        self.native_id = None
        self.thread = threading.Thread(target=self.run, daemon=True)

    def start(self):
        self.handle = K32.CreateNamedPipeW(self.name, 3, 0, 1, 65536, 65536, 0, None)   # duplex, byte, wait
        assert self.handle and self.handle != INVALID, ctypes.get_last_error()
        self.thread.start()

    def note(self, *event):
        with self.lock:
            self.events.append((time.monotonic(),) + event)

    def read(self, n):
        buf = ctypes.create_string_buffer(n)
        got = wintypes.DWORD(0)
        data = b''
        while len(data) < n:
            if not K32.ReadFile(self.handle, buf, n - len(data), ctypes.byref(got), None) or got.value == 0:
                return None
            data += buf.raw[:got.value]
        return data

    def send(self, op, payload):
        data = struct.pack('<II', op, len(payload)) + payload
        done = wintypes.DWORD(0)
        K32.WriteFile(self.handle, data, len(data), ctypes.byref(done), None)

    def run(self):
        self.native_id = threading.get_native_id()
        while not self.stop:
            ok = K32.ConnectNamedPipe(self.handle, None)
            if not ok and ctypes.get_last_error() != 535:   # ERROR_PIPE_CONNECTED
                if self.stop:
                    return
                time.sleep(0.01)
                continue
            if self.stop:
                return
            self.connection += 1
            self.note(self.connection, 'connect', None)
            self.kicked = False
            while not self.kicked and not self.stop:
                head = self.read(8)
                if head is None:
                    break
                op, length = struct.unpack('<II', head)
                payload = self.read(length) if length else b''
                if payload is None:
                    break
                self.note(self.connection, op, payload)
                if op == 0:
                    # as Discord sends it: the config and the user first, "evt" at the end
                    ready = {'cmd': 'DISPATCH', 'data': {'v': 1, 'config': {'cdn_host': 'cdn.example', 'environment': 'production'},
                                                         'user': {'id': '1', 'username': 'x' * 600}},
                             'evt': 'READY', 'nonce': None}
                    self.send(1, json.dumps(ready).encode())
                    if self.connection == 1:
                        self.send(3, b'{"ping":1}')
                elif op == 1:
                    nonce = json.loads(payload).get('nonce')
                    self.send(1, json.dumps({'cmd': 'SET_ACTIVITY', 'data': None, 'evt': None, 'nonce': nonce}).encode())
            self.note(self.connection, 'disconnect', None)
            K32.DisconnectNamedPipe(self.handle)

    def interrupt(self, done):
        """The handle is synchronous: another thread's DisconnectNamedPipe or CloseHandle would wait
        for the server thread's blocking ReadFile. Cancel that read instead, until `done`."""
        thread = K32.OpenThread(0x0001, False, self.native_id)     # THREAD_TERMINATE: enough to cancel I/O
        try:
            end = time.monotonic() + 5
            while not done() and time.monotonic() < end:
                K32.CancelSynchronousIo(thread)
                time.sleep(0.01)
        finally:
            K32.CloseHandle(thread)

    def kick(self):
        """Breaks the current connection, as Discord quitting would."""
        connection = self.connection
        self.kicked = True
        self.interrupt(lambda: [e for e in self.frames('disconnect') if e[1] == connection])

    def close(self):
        self.stop = True
        if self.thread.is_alive():
            self.interrupt(lambda: not self.thread.is_alive())
        K32.CloseHandle(self.handle)

    def frames(self, op=None):
        with self.lock:
            return [e for e in self.events if op is None or e[2] == op]

    def activities(self):
        """(time, connection, activity or None for a clear) of every SET_ACTIVITY."""
        out = []
        for t, conn, op, payload in self.frames(1):
            msg = json.loads(payload)
            if msg.get('cmd') == 'SET_ACTIVITY':
                out.append((t, conn, msg['args'].get('activity'), msg))
        return out


def put(path, text):
    """Replaces the control file in one step (the driver reads it every 20 ms)."""
    tmp = path.with_suffix('.tmp')
    tmp.write_text(text)
    for _ in range(200):
        try:
            os.replace(tmp, path)
            return
        except PermissionError:     # the driver has it open this instant
            time.sleep(0.005)
    raise AssertionError('could not replace ' + str(path))


def wait_for(check, timeout=5.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = check()
        if value:
            return value
        time.sleep(0.02)
    return check()


@pytest.mark.skipif(sys.platform != 'win32', reason='named pipes: Windows only')
def test_presence_thread_against_a_fake_discord(tmp_path_factory):
    folder = tmp_path_factory.mktemp('dipc')
    exe = compile_exe(folder, 'dipc', IPC_DRIVER, ['discord_ipc.c', 'discord_rpc.c'])
    prefix = r'\\.\pipe\ffny-test-discord-%d-' % os.getpid()
    control = folder / 'control.txt'
    put(control, 'Main menu|')
    # Discord "runs" on index 2, as when two Discord apps are open: the client tries 0..9
    fake = FakeDiscord(prefix + '2')
    proc = subprocess.Popen([str(exe), prefix, str(control)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        time.sleep(0.5)                 # Discord not running yet: the client keeps trying
        assert proc.poll() is None
        fake.start()

        # handshake, READY, the PING answered, then the first state at once
        hello = wait_for(lambda: fake.frames(0))
        assert hello and json.loads(hello[0][3]) == {'v': 1, 'client_id': '1562214133242400993'}
        pong = wait_for(lambda: fake.frames(4))
        assert pong and pong[0][3] == b'{"ping":1}'
        first = wait_for(lambda: [a for a in fake.activities() if a[2]])
        assert first[0][2]['details'] == 'Main menu' and 'state' not in first[0][2]
        assert first[0][3]['args']['pid'] == proc.pid
        assert first[0][2]['assets']['large_image'] == 'ffny_logo'
        assert len(first[0][2]['buttons']) == 2
        assert first[0][0] - hello[0][0] < 0.5

        # two changes inside one interval: only the newest goes out, no sooner than the interval
        put(control, 'Story mode|In a fight')
        time.sleep(0.1)
        put(control, 'Battle mode|')
        second = wait_for(lambda: [a for a in fake.activities() if a[2] and a[2]['details'] != 'Main menu'])
        time.sleep(0.8)
        sent = [a for a in fake.activities() if a[2]]
        assert [a[2]['details'] for a in sent] == ['Main menu', 'Battle mode']
        assert second[0][0] - first[0][0] >= 0.55

        # a broken pipe: it connects again and sends the newest state at once
        fake.kick()
        again = wait_for(lambda: [a for a in fake.activities() if a[1] == 2 and a[2]])
        assert again and again[0][2]['details'] == 'Battle mode'
        assert len(fake.frames(0)) == 2

        # switched off: the presence is cleared and the pipe closed; on again: back
        put(control, 'off')
        cleared = wait_for(lambda: [a for a in fake.activities() if a[1] == 2 and a[2] is None])
        assert cleared and cleared[0][3]['args'] == {'pid': proc.pid}
        assert wait_for(lambda: [e for e in fake.frames('disconnect') if e[1] == 2])
        put(control, 'Main menu|')
        back = wait_for(lambda: [a for a in fake.activities() if a[1] == 3 and a[2]])
        assert back and back[0][2]['details'] == 'Main menu'

        # the program ends: cleared before the pipe closes
        put(control, 'quit')
        out, err = proc.communicate(timeout=10)
        assert proc.returncode == 0, err
        assert b'stopped connected=0' in out
        assert wait_for(lambda: [a for a in fake.activities() if a[1] == 3 and a[2] is None])
        assert wait_for(lambda: [e for e in fake.frames('disconnect') if e[1] == 3])
        log = err.decode('utf-8', 'replace')
        assert 'Discord is not running' in log
        assert 'connected to Discord (pipe 2)' in log
        assert 'presence switched off (cleared)' in log
    finally:
        if proc.poll() is None:
            proc.kill()
        fake.close()


def test_the_game_wires_it_in():
    settings = (HOOKS / 'pc_settings.c').read_text(encoding='utf-8')
    ui = (HOOKS / 'pc_ui.cpp').read_text(encoding='utf-8')
    automatch = (HOOKS / 'automatch.c').read_text(encoding='utf-8')
    presence = (HOOKS / 'discord_presence.c').read_text(encoding='utf-8')
    # the setting: on by default, with its environment override
    assert re.search(r'\{ "online", "discord_status", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, "RECOMP_DISCORD_PRESENCE", 0,',
                     settings)
    # the launcher shows it and hands over to the game's process without leaving it behind
    assert settings.count('discord_presence_launcher();') == 2
    assert 'discord_presence_end();     /* the game\'s process shows its own */' in settings
    assert 'discord_presence_game();' in settings
    # the launcher's options and the overlay's Online page both offer it
    assert '{"online", "discord_status", "Discord status"}' in ui
    assert ui.count('discord_status_setting();') == 2
    assert 'discord_presence_on_funccall(text);' in automatch     # every front-end call
    # test copies leave the player's Discord alone
    assert 'getenv("RECOMP_DISCORD_PRESENCE")' in presence and 'getenv("RECOMP_SETTINGS")' in presence

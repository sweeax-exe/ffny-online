"""Build 90: the direct connection (P2P), the player's opt-in (docs/research/p2p-optin.md).

Off by default; turning it on shows the IP notice first (launcher and the hub's VOICE CHAT page); CAPS bit 27;
only P_STEPS and P_LIVEPAD go direct, as a second copy beside the relay's; the wire forms (src/hooks/netplay_p2p.c)
compiled natively and checked against the relay's (server relay/p2p.go: request 34 bytes, answer 26)."""
import re
import shutil
import struct
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def _src(rel):
    return (ROOT / rel).read_text(encoding='utf-8')


def test_setting_defaults_off():
    row = re.search(r'\{\s*"online",\s*"p2p_direct",\s*RECOMP_SETTING_BOOL,\s*(\d+),', _src('src/hooks/pc_settings.c'))
    assert row, 'no [online] p2p_direct row in pc_settings.c'
    assert row.group(1) == '0', 'the direct connection must be off by default'
    assert 'IP address' in _src('src/hooks/pc_settings.c').split('"p2p_direct"', 1)[1][:600]


def test_caps_bit_27_and_consent_per_match():
    net = _src('src/hooks/netplay.c')
    assert '#define NETP2P_CAP        (1u << 27)' in _src('src/hooks/netplay_p2p.h')
    assert '| CAPS_DIRECT |' in net
    # the endpoint is asked for only while the player allows it, in a 1v1 match this PC plays
    start = net.split('static void p2p_on_start(', 1)[1].split('\n}\n', 1)[0]
    assert 'count != 2' in start and 'watching' in start and 'p2p_setting()' in start
    # the relay copy always goes out; the direct one only beside it
    send = net.split('static void send_data(const uint8_t *payload, int len)\n{', 1)[1].split('\n}\n', 1)[0]
    assert send.index('send_raw(b, n);') < send.index('p2p_send_data(payload, len);')


def test_notice_before_turning_on():
    ui = _src('src/hooks/pc_ui.cpp')
    assert ui.count('p2p_setting();') >= 2      # Online page and the basics page
    body = ui.split('static void p2p_setting()', 1)[1].split('\n}\n', 1)[0]
    assert 'IP address' in body and 'DDoS' in body and 'BeginPopupModal' in body
    hub = _src('src/hooks/online_screen.cpp')
    voice = hub.split('void draw_voice(', 1)[1].split('\n}\n', 1)[0]
    assert 'S.p2p_ask = true' in voice and 'IP address' in voice and 'DDoS' in voice


DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "netplay_p2p.h"

static void hex(const char *tag, const unsigned char *b, int n)
{
    int i;
    printf("%s|", tag);
    for (i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}

int main(void)
{
    unsigned char b[64], tok[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }, key[8] = { 9, 8, 7, 6, 5, 4, 3, 2 }, bad[8] = { 0 };
    unsigned char ans[26] = { 'D', 'J', 'N', 'P', 1, 0x3A, 0xEF, 0xBE, 0xAD, 0xDE, 1, 1, 203, 0, 113, 9, 0xD7, 0x11,
                              9, 8, 7, 6, 5, 4, 3, 2 };
    const unsigned char *body;
    int n, kind, len;
    NetP2PAnswer a;
    hex("ask", b, netp2p_ask(b, tok, 0xDEADBEEFu));
    printf("answer|%d|", netp2p_answer(ans, 26, &a));
    printf("%08x|%d|%d|%d.%d.%d.%d|%d\n", a.seed, a.status, a.slot, a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.port);
    printf("answer_short|%d\n", netp2p_answer(ans, 25, &a));
    ans[10] = 2;
    printf("answer_wait|%d|%d\n", netp2p_answer(ans, 11, &a), a.status);
    ans[10] = 7;
    printf("answer_bad|%d\n", netp2p_answer(ans, 11, &a));
    len = netp2p_packet(b, (int)sizeof(b), key, 3, (const unsigned char *)"\x04xyz", 4);
    hex("packet", b, len);
    printf("parse|%d|", netp2p_parse(b, len, key, &kind, &body, &n));
    printf("%d|%d|%02x\n", kind, n, body[0]);
    printf("parse_badkey|%d\n", netp2p_parse(b, len, bad, &kind, &body, &n));
    b[5] = 0x04;
    printf("parse_type|%d\n", netp2p_parse(b, len, key, &kind, &body, &n));
    printf("toobig|%d\n", netp2p_packet(b, 16, key, 3, b, 2));
    printf("direct|%d%d%d%d%d\n", netp2p_direct_payload(3), netp2p_direct_payload(4), netp2p_direct_payload(5),
           netp2p_direct_payload(6), netp2p_direct_payload(0x0B));
    (void)memset;
    return 0;
}
'''


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('p2p')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'p2p.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/utf-8', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/netplay_p2p.c'),
                            '/Fe:' + str(exe)], cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    return {line.split('|', 1)[0]: line.split('|', 1)[1] for line in out.splitlines()}


def test_ask_is_the_relays_request(rows):
    want = b'DJNP\x01\x3a' + bytes(range(1, 9)) + struct.pack('<I', 0xDEADBEEF) + bytes(16)
    assert len(want) == 34
    assert rows['ask'] == want.hex()


def test_answer(rows):
    assert rows['answer'] == '1|deadbeef|1|1|203.0.113.9|4567'
    assert rows['answer_short'] == '0'
    assert rows['answer_wait'] == '1|2'
    assert rows['answer_bad'] == '0'


def test_direct_packet(rows):
    assert rows['packet'] == (b'DJNP\x01\x3b' + bytes([9, 8, 7, 6, 5, 4, 3, 2]) + b'\x03\x04xyz').hex()
    assert rows['parse'] == '1|3|4|04'
    assert rows['parse_badkey'] == '0'
    assert rows['parse_type'] == '0'
    assert rows['toobig'] == '0'


def test_only_inputs_and_live_pads_go_direct(rows):
    assert rows['direct'] == '11000'

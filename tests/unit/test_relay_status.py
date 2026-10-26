"""The server choice (build 30): the game's STATUS client (src/hooks/netplay_servers.c) and the relay
(netplay/relay/protocol_status.go) must agree on the message type, the request size, the answer's
layout and the flags; the room ping (P_PINGINFO) must not collide with another payload type; and
the setting, the two relays and the test switches must be where the docs say."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SERVERS_C = (ROOT / 'src/hooks/netplay_servers.c').read_text(encoding='utf-8')
SERVERS_H = (ROOT / 'src/hooks/netplay_servers.h').read_text(encoding='utf-8')
NETPLAY_C = (ROOT / 'src/hooks/netplay.c').read_text(encoding='utf-8')
SETTINGS_C = (ROOT / 'src/hooks/pc_settings.c').read_text(encoding='utf-8')
GO = (ROOT / 'netplay/relay/protocol_status.go').read_text(encoding='utf-8')
GO_BASE = (ROOT / 'netplay/relay/protocol.go').read_text(encoding='utf-8')
HUB_TEST = (ROOT / 'scripts/netplay-hub-test.ps1').read_text(encoding='utf-8')
PROTOCOL_DOC = (ROOT / 'docs/netplay-protocol.md').read_text(encoding='utf-8')


def c_define(text, name):
    m = re.search(r'^#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)u?\b' % re.escape(name), text, re.M)
    assert m, name
    return int(m.group(1), 0)


def go_const(text, name):
    m = re.search(r'^\s*(?:const\s+)?%s\s+(?:\w+\s+)?=\s+(?:\w+\()?(0x[0-9A-Fa-f]+|\d+)' % re.escape(name), text, re.M)
    assert m, name
    return int(m.group(1), 0)


def go_shift(name):
    m = re.search(r'^\s*%s\s+uint8\s+=\s+1\s+<<\s+(\d+)' % re.escape(name), GO, re.M)
    assert m, name
    return 1 << int(m.group(1))


def test_status_type_and_sizes_match_the_relay():
    assert c_define(SERVERS_C, 'STATUS_TYPE') == go_const(GO, 'TypeStatus') == 0x24
    header = go_const(GO_BASE, 'HeaderLen')
    build = go_const(GO_BASE, 'BuildLen')
    reserved = go_const(GO, 'StatusReserved')
    assert c_define(SERVERS_C, 'STATUS_LEN') == header + 4 + build + reserved
    # the answer is never larger than the request (the relay's anti-amplification rule)
    reply = 4 + 1 + 1 + 2 + 2 + 2 + 2 + 4 + 2
    assert reply == 20 and header + reply <= c_define(SERVERS_C, 'STATUS_LEN')


def test_the_client_reads_the_answer_at_the_relays_offsets():
    # nonce u32 at 6, version 10, flags 11, online 12, rooms 14, waiting 16, idle 18
    for field, offset in (('version', 10), ('flags', 11)):
        assert re.search(r'P\.s\[who\]\.%s = b\[%d\]' % (field, offset), SERVERS_C), field
    for field, offset in (('online', 12), ('rooms', 14), ('waiting', 16), ('idle', 18)):
        assert re.search(r'P\.s\[who\]\.%s = rd16\(b \+ %d\)' % (field, offset), SERVERS_C), field
    assert 'rd32(b + 6)' in SERVERS_C
    # the build fingerprint goes after the nonce, so the relay counts this build's players
    assert 'memcpy(b + 10, print, 8)' in SERVERS_C


def test_flags_match_the_relay():
    assert c_define(SERVERS_H, 'NETSERVER_RANKED') == go_shift('StatusRanked')
    assert c_define(SERVERS_H, 'NETSERVER_KOTH') == go_shift('StatusKoth')
    assert c_define(SERVERS_H, 'NETSERVER_ROLLBACK') == go_shift('StatusRollback')


def test_room_ping_payload_is_its_own_type():
    payloads = {name: int(v) for name, v in re.findall(r'^#define\s+(P_\w+)\s+(\d+)\b', NETPLAY_C, re.M)}
    assert payloads['P_PINGINFO'] == 9
    assert list(payloads.values()).count(9) == 1, payloads


def test_relays_setting_and_test_switches():
    assert '"play.defjam.online:27777"' in SERVERS_C and '"na.defjam.online:27777"' in SERVERS_C
    assert 'RECOMP_NETPLAY_SERVER_EU' in SERVERS_C and 'RECOMP_NETPLAY_SERVER_NA' in SERVERS_C
    # [online] server defaults to auto, and RECOMP_NETPLAY_SERVER still overrides it
    m = re.search(r'\{ "online", "server", RECOMP_SETTING_STRING, .*?"RECOMP_NETPLAY_SERVER".*?\}', SETTINGS_C, re.S)
    assert m and m.group(0).rstrip('} ').endswith('"auto"')
    assert 'RECOMP_TEST_SERVER_SWITCH' in NETPLAY_C
    for param in ('NaRelayPort', 'Server', 'EuLagMs', 'NaLagMs', 'SwitchTo'):
        assert re.search(r'\$%s\b' % param, HUB_TEST), param
    assert '0x24' in PROTOCOL_DOC and '`STATUS`' in PROTOCOL_DOC

"""The server choice (build 30): the game's STATUS client (src/hooks/netplay_servers.c) and the relay
(server/relay/protocol_status.go) must agree on the message type, the request size, the answer's
layout and the flags; the room ping (P_PINGINFO) must not collide with another payload type; and
the setting, the two relays and the test switches must be where the docs say."""
import re
from pathlib import Path
import pytest

from server_src import RELAY as RELAY_SRC, SKIP

if RELAY_SRC is None:
    pytest.skip(SKIP, allow_module_level=True)

ROOT = Path(__file__).resolve().parents[2]
RULES_C = (ROOT / 'src/hooks/netplay_servers_rules.c').read_text(encoding='utf-8')
SERVERS_C = (ROOT / 'src/hooks/netplay_servers.c').read_text(encoding='utf-8')
SERVERS_H = (ROOT / 'src/hooks/netplay_servers.h').read_text(encoding='utf-8')
NETPLAY_C = (ROOT / 'src/hooks/netplay.c').read_text(encoding='utf-8')
SETTINGS_C = (ROOT / 'src/hooks/pc_settings.c').read_text(encoding='utf-8')
GO = (RELAY_SRC / 'protocol_status.go').read_text(encoding='utf-8')
GO_BASE = (RELAY_SRC / 'protocol.go').read_text(encoding='utf-8')
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
    # build 91: one table (netplay_servers_rules.c) names the relays, their fallback addresses and test switches
    for row in ('"play.defjam.online:27777", "49.12.78.32", "RECOMP_NETPLAY_SERVER_EU"',
                '"nj.defjam.online:27777", "208.167.239.167", "RECOMP_NETPLAY_SERVER_NA"',
                '"na2.defjam.online:27777", "140.82.31.133",',
                '"sg2.defjam.online:27777", "139.180.129.88", "RECOMP_NETPLAY_SERVER_SG"',
                '"sa.defjam.online:27777", "216.238.122.210", "RECOMP_NETPLAY_SERVER_SA"'):
        assert row in RULES_C, row
    assert 'netservers_def(i)' in SERVERS_C
    # the session's own DNS fallback uses the same table
    assert 'netservers_known_ip(host)' in NETPLAY_C
    # [online] server defaults to auto, and RECOMP_NETPLAY_SERVER still overrides it
    m = re.search(r'\{ "online", "server_debug", RECOMP_SETTING_STRING, .*?"RECOMP_NETPLAY_SERVER".*?\}', SETTINGS_C, re.S)
    assert m and m.group(0).rstrip('} ').endswith('"auto"')
    assert 'RECOMP_TEST_SERVER_SWITCH' in NETPLAY_C
    for param in ('NaRelayPort', 'WestRelayPort', 'SaRelayPort', 'Server', 'EuLagMs', 'NaLagMs', 'WestLagMs', 'SaLagMs',
                  'SwitchTo'):
        assert re.search(r'\$%s\b' % param, HUB_TEST), param
    assert '0x24' in PROTOCOL_DOC and '`STATUS`' in PROTOCOL_DOC


def test_relays_trailer_caps_bit_28():
    """Build 91: CAPS bit 28 carries the pings to the relays beyond the first three; QUICK and RANKED add the
    trailer only to a relay that answered it, and ROOMLIST asks for it with the same bit."""
    assert re.search(r'#define CAPS_RELAYS\s+268435456u', NETPLAY_C)
    assert 'CAPS_PINGSUM | CAPS_DIRECT | CAPS_RELAYS |' in NETPLAY_C     # build 90's bit 27 between them
    assert NETPLAY_C.count('if (relay_has(CAPS_RELAYS))') == 2
    assert 'req[22] = 4 | 16;' in SERVERS_C
    assert 'netservers_get_more_pings(b + p, n - p, count, &pings[0][0])' in SERVERS_C

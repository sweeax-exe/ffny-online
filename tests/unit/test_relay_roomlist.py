"""Both servers (build 31): the game's ROOMLIST client (src/hooks/netplay_servers.c) and the relay
(netplay/relay/protocol_roomlist.go) must agree on the message type, the request's fields and its
padding (the relay never answers with more bytes than it was sent), and the answer's layout; the
setting must accept "both", the move rule must stay the one the docs describe, and the hub test
must offer the switches the Both tests use."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SERVERS_C = (ROOT / 'src/hooks/netplay_servers.c').read_text(encoding='utf-8')
SERVERS_H = (ROOT / 'src/hooks/netplay_servers.h').read_text(encoding='utf-8')
NETPLAY_C = (ROOT / 'src/hooks/netplay.c').read_text(encoding='utf-8')
SETTINGS_C = (ROOT / 'src/hooks/pc_settings.c').read_text(encoding='utf-8')
SCREEN = (ROOT / 'src/hooks/online_screen.cpp').read_text(encoding='utf-8')
PC_UI = (ROOT / 'src/hooks/pc_ui.cpp').read_text(encoding='utf-8')
GO = (ROOT / 'netplay/relay/protocol_roomlist.go').read_text(encoding='utf-8')
GO_BASE = (ROOT / 'netplay/relay/protocol.go').read_text(encoding='utf-8')
GO_LOBBY = (ROOT / 'netplay/relay/protocol_lobby.go').read_text(encoding='utf-8')
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


def test_type_matches_the_relay_and_differs_from_status():
    assert c_define(SERVERS_C, 'ROOMLIST_TYPE') == go_const(GO, 'TypeRoomList') == 0x25
    assert c_define(SERVERS_C, 'STATUS_TYPE') != c_define(SERVERS_C, 'ROOMLIST_TYPE')


def test_request_fields_and_padding():
    header = go_const(GO_BASE, 'HeaderLen')
    build = go_const(GO_BASE, 'BuildLen')
    datagram = go_const(GO_BASE, 'MaxDatagram')
    # nonce at 6, build at 10, mode at 18, caps at 19..22, then zeros up to a full datagram
    assert header + 4 + build + 1 + 4 == 23
    assert 'memcpy(req + 10, print, 8)' in SERVERS_C
    assert 'req[18] = 0xFF' in SERVERS_C and 'req[19] = 1' in SERVERS_C
    assert re.search(r'uint8_t print\[8\], req\[%d\]' % datagram, SERVERS_C)
    assert 'memset(req, 0, sizeof(req))' in SERVERS_C
    assert 'sendto(s, (const char *)req, (int)sizeof(req)' in SERVERS_C


def test_the_client_reads_the_answer_at_the_relays_offsets():
    # nonce u32 at 6, total u16 at 10, count at 12, entries from 13: code 6, name, mode, players, max,
    # phase, age u16
    assert 'rd32(b + 6) == nonce[i]' in SERVERS_C
    assert 'R.total[who] = rd16(b + 10)' in SERVERS_C
    assert 'count = b[12]' in SERVERS_C and 'at = 13' in SERVERS_C
    assert 'ages[k] = rd16(b + at + 4)' in SERVERS_C
    assert go_const(GO_LOBBY, 'MaxListed') == c_define(SERVERS_C, 'LISTED_MAX') == 30


def test_setting_and_launcher_offer_both():
    assert 'NETCHOICE_BOTH' in SERVERS_H
    assert '!strcmp(low, "both")' in SERVERS_C and '"both"' in SERVERS_C
    m = re.search(r'\{ "online", "server", RECOMP_SETTING_STRING, .*?\}', SETTINGS_C, re.S)
    assert m and 'both' in m.group(0)
    assert 'netservers_set_choice(NETCHOICE_BOTH)' in PC_UI and 'netservers_set_choice(NETCHOICE_BOTH)' in SCREEN
    # the picker explains Both in one line
    assert 'Rooms and Quick Match on Europe and North America; matches across the ocean have higher ping.' in SCREEN


def test_move_rule_and_gallery():
    # the younger room's player moves after 20 s; the other only once that room waited 35 s
    assert 'if (alone_s < 20) return -1;' in SERVERS_C
    assert 'int mover = age > my_age + 2 || (age >= my_age - 2 && cur > o);' in SERVERS_C
    assert 'if (mover || age >= 35) n += x->players;' in SERVERS_C
    for page in ('hub-both', 'servers-both', 'rooms-both', 'rooms-both-na', 'room-both-move'):
        assert '"%s"' % page in SCREEN, page


def test_hub_test_switches():
    assert 'RECOMP_TEST_JOIN_LISTED' in NETPLAY_C
    for param in ('Servers', 'JoinListed', 'JoinListedSecs', 'CopyGapSecs'):
        assert re.search(r'\$%s\b' % param, HUB_TEST), param
    assert "'both'" in HUB_TEST
    assert '0x25' in PROTOCOL_DOC and '`ROOMLIST`' in PROTOCOL_DOC

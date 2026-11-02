"""Every relay's rooms (build 31 "both"; build 38 "all", three relays): the game's ROOMLIST client
(src/hooks/netplay_servers.c) and the relay (server/relay/protocol_roomlist.go) must agree on the
message type, the request's fields and its padding (the relay never answers with more bytes than it
was sent), and the answer's layout; the setting must accept "all" and the old "both", the staged
quick-match rule must stay the one the docs describe, and the hub test must offer the switches the
tests use."""
import re
from pathlib import Path
import pytest

from server_src import RELAY as RELAY_SRC, SKIP

if RELAY_SRC is None:
    pytest.skip(SKIP, allow_module_level=True)

ROOT = Path(__file__).resolve().parents[2]
SERVERS_C = (ROOT / 'src/hooks/netplay_servers.c').read_text(encoding='utf-8')
SERVERS_H = (ROOT / 'src/hooks/netplay_servers.h').read_text(encoding='utf-8')
RULES_H = (ROOT / 'src/hooks/netplay_servers_rules.h').read_text(encoding='utf-8')
RULES_C = (ROOT / 'src/hooks/netplay_servers_rules.c').read_text(encoding='utf-8')
NETPLAY_C = (ROOT / 'src/hooks/netplay.c').read_text(encoding='utf-8')
SETTINGS_C = (ROOT / 'src/hooks/pc_settings.c').read_text(encoding='utf-8')
SCREEN = (ROOT / 'src/hooks/online_screen.cpp').read_text(encoding='utf-8')
PC_UI = (ROOT / 'src/hooks/pc_ui.cpp').read_text(encoding='utf-8')
GO = (RELAY_SRC / 'protocol_roomlist.go').read_text(encoding='utf-8')
GO_BASE = (RELAY_SRC / 'protocol.go').read_text(encoding='utf-8')
GO_LOBBY = (RELAY_SRC / 'protocol_lobby.go').read_text(encoding='utf-8')
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


def test_setting_and_launcher_offer_all():
    assert 'NETCHOICE_ALL' in RULES_H and 'NETCHOICE_BOTH' not in SERVERS_H + RULES_H
    # "all" is saved; the "both" of builds 31..37 still reads as all
    assert '{ "all", NETCHOICE_ALL }' in RULES_C and '{ "both", NETCHOICE_ALL }' in RULES_C
    assert 'case NETCHOICE_ALL: return "all";' in RULES_C
    m = re.search(r'\{ "online", "server", RECOMP_SETTING_STRING, .*?\}', SETTINGS_C, re.S)
    assert m and 'all' in m.group(0) and 'west' in m.group(0)
    assert 'netservers_set_choice(NETCHOICE_ALL)' in PC_UI and 'netservers_set_choice(NETCHOICE_ALL)' in SCREEN
    # the picker explains All in one line
    assert 'The rooms of Europe, US East and US West in one list; joining one moves you there.' in SCREEN


def test_staged_search_rule_and_gallery():
    # its own relay; the other US relay from 10 s (Automatic, All); every relay from 30 s (any choice);
    # the older room's side only once the younger room waited 40 s
    assert c_define(RULES_H, 'QUICK_NEAR_S') == 10
    assert c_define(RULES_H, 'QUICK_ALL_S') == 30
    assert c_define(RULES_H, 'QUICK_LAST_S') == 40
    assert 'if (their_age > my_age + 2 || (their_age >= my_age - 2 && my_relay > their_relay)) return 1;' in RULES_C
    assert 'if (netservers_should_move(cur, my_age, o, age)) n += x->players;' in SERVERS_C
    assert 'scope = netservers_scope(c, cur, search_s, up);' in SERVERS_C
    # the screens and the test thread both run it, for every choice but a custom address
    assert 'netservers_quick_move(S.last_quick, search_s, alone_s, r.code, &waiting, &ping, &scope)' in SCREEN
    assert 'netservers_quick_move(qmode, search_s, alone_s, r.code, &waiting, &ping, &scope)' in NETPLAY_C
    assert '"Looking on every server..."' in SCREEN and '"Looking on " + added + " too..."' in SCREEN
    for page in ('hub-both', 'servers-both', 'rooms-both', 'rooms-both-na', 'room-both-move',
                 'room-quick-near', 'room-quick-wide'):
        assert '"%s"' % page in SCREEN, page


def test_hub_test_switches():
    assert 'RECOMP_TEST_JOIN_LISTED' in NETPLAY_C
    for param in ('Servers', 'JoinListed', 'JoinListedSecs', 'CopyGapSecs', 'WestRelayPort', 'WestLagMs'):
        assert re.search(r'\$%s\b' % param, HUB_TEST), param
    assert "'both'" in HUB_TEST and "'all'" in HUB_TEST and "'west'" in HUB_TEST
    assert '0x25' in PROTOCOL_DOC and '`ROOMLIST`' in PROTOCOL_DOC

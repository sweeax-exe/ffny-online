"""Every relay's players (build 47, "All servers"): the game's PLAYERLIST client
(src/hooks/netplay_servers.c) and the relay (server/relay/protocol_playerlist.go) must agree on the
message type, the request (nonce, build, zero padding: the relay never answers with more bytes than
it was sent) and the answer's layout; the players page must tag each player with its server."""
import re
from pathlib import Path
import pytest

from server_src import RELAY as RELAY_SRC, SKIP

if RELAY_SRC is None:
    pytest.skip(SKIP, allow_module_level=True)
if not (RELAY_SRC / 'protocol_playerlist.go').is_file():
    pytest.skip('the relay source predates PLAYERLIST (server build 47)', allow_module_level=True)

ROOT = Path(__file__).resolve().parents[2]
SERVERS_C = (ROOT / 'src/hooks/netplay_servers.c').read_text(encoding='utf-8')
SCREEN = (ROOT / 'src/hooks/online_screen.cpp').read_text(encoding='utf-8')
PROTOCOL_DOC = (ROOT / 'docs/netplay-protocol.md').read_text(encoding='utf-8')
GO = (RELAY_SRC / 'protocol_playerlist.go').read_text(encoding='utf-8')
GO_BASE = (RELAY_SRC / 'protocol.go').read_text(encoding='utf-8')
GO_LOBBY = (RELAY_SRC / 'protocol_lobby.go').read_text(encoding='utf-8')
GO_ALL = ''.join(p.read_text(encoding='utf-8') for p in RELAY_SRC.glob('protocol*.go'))


def c_define(text, name):
    m = re.search(r'^#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)u?\b' % re.escape(name), text, re.M)
    assert m, name
    return int(m.group(1), 0)


def go_const(text, name):
    m = re.search(r'^\s*(?:const\s+)?%s\s+(?:\w+\s+)?=\s+(?:\w+\()?(0x[0-9A-Fa-f]+|\d+)' % re.escape(name), text, re.M)
    assert m, name
    return int(m.group(1), 0)


def test_type_matches_the_relay_and_is_new():
    assert c_define(SERVERS_C, 'PLAYERLIST_TYPE') == go_const(GO, 'TypePlayerList') == 0x33
    types = [int(t, 16) for t in re.findall(r'MsgType = (0x[0-9A-Fa-f]+)', GO_ALL)]
    assert len(types) == len(set(types)), sorted(types)


def test_request_fields_and_padding():
    build = go_const(GO_BASE, 'BuildLen')
    datagram = go_const(GO_BASE, 'MaxDatagram')
    assert 'PlayerListBodyMin  = 4 + BuildLen' in GO and build == 8
    body = SERVERS_C[SERVERS_C.index('static void players_round(void)'):]
    body = body[:body.index('\n}\n')]
    # nonce at 6, build at 10, then zeros up to a full datagram
    assert 'req[5] = PLAYERLIST_TYPE' in body and 'memcpy(req + 10, print, 8)' in body
    assert re.search(r'uint8_t print\[8\], req\[%d\]' % datagram, body)
    assert 'memset(req, 0, sizeof(req))' in body
    assert 'sendto(s, (const char *)req, (int)sizeof(req)' in body


def test_the_client_reads_the_answer_at_the_relays_offsets():
    # nonce u32 at 6, online u16 at 10, total u16 at 12, count at 14, entries from 15: name, state
    body = SERVERS_C[SERVERS_C.index('static void players_round(void)'):]
    assert 'rd32(b + 6) == nonce[i]' in body
    assert 'PL.online[who] = rd16(b + 10)' in body and 'PL.total[who] = rd16(b + 12)' in body
    assert 'count = b[14]' in body and 'at = 15' in body
    assert go_const(GO_LOBBY, 'MaxWho') == c_define(SERVERS_C, 'PLAYERS_MAX') == 40
    # the answer: names and states only
    assert re.search(r'type PlayerListReply struct \{\s*Nonce\s+uint32\s*Online\s+uint16\s*Total\s+uint16\s*List\s+\[\]PlayerInfo', GO)


def test_the_page_tags_every_server_and_counts_the_others():
    page = SCREEN[SCREEN.index('void draw_players('):]
    page = page[:page.index('\n}\n')]
    assert 'all_players(list, 40, &online)' in page and 'short_name' in page
    assert 'more on %s' in page        # one server chosen: the others' STATUS counts
    assert '"players-all"' in SCREEN    # the gallery's All-servers page
    assert '`PLAYERLIST`' in PROTOCOL_DOC and '0x33' in PROTOCOL_DOC

"""Build 82, STANDARD ONLY rooms (community content off) and the hub's NEW badge: the game
(src/hooks/netplay.h, netplay.c, netplay_servers.c, online_data.h, online_screen.cpp, pc_content.c) and the relay
(server/relay/protocol_rules.go, protocol_lobby.go, roomlist.go) agree on the rules bit, the ID ranges and the
rules bytes after ROOMS and ROOMLIST."""
import re
from pathlib import Path

import pytest

from server_src import RELAY as RELAY_SRC, SKIP

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'
NETPLAY_H = (HOOKS / 'netplay.h').read_text(encoding='utf-8')
NETPLAY_C = (HOOKS / 'netplay.c').read_text(encoding='utf-8')
SERVERS_C = (HOOKS / 'netplay_servers.c').read_text(encoding='utf-8')
DATA_H = (HOOKS / 'online_data.h').read_text(encoding='utf-8')
DATA_C = (HOOKS / 'online_data.c').read_text(encoding='utf-8')
SCREEN = (HOOKS / 'online_screen.cpp').read_text(encoding='utf-8')
CONTENT_C = (HOOKS / 'pc_content.c').read_text(encoding='utf-8')
CONTENT_H = (HOOKS / 'pc_content.h').read_text(encoding='utf-8')
LAUNCHER = (HOOKS / 'pc_launcher_content.inc').read_text(encoding='utf-8')
MODS_CPP = (HOOKS / 'pc_mods.cpp').read_text(encoding='utf-8')


def c_define(text, name):
    m = re.search(r'^#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)u?\b' % re.escape(name), text, re.M)
    assert m, name
    return int(m.group(1), 0)


def go_const(text, name):
    m = re.search(r'^\s*(?:const\s+)?%s\s+(?:\w+\s+)?=\s+(0x[0-9A-Fa-f]+|\d+)\b' % re.escape(name), text, re.M)
    assert m, name
    return int(m.group(1), 0)


def test_game_side_rules_bit_and_ranges():
    assert c_define(NETPLAY_H, 'NETPLAY_RULES_STANDARD') == 4 and c_define(NETPLAY_H, 'NETPLAY_RULES_MASK') == 7
    # the encoder and ROOM's decoder take every bit; ROOMS and ROOMLIST read the rules bytes after the list
    assert 's->fighter_rules <= NETPLAY_RULES_MASK ? s->fighter_rules : 0' in NETPLAY_C
    assert 'v.settings.fighter_rules > NETPLAY_RULES_MASK' in NETPLAY_C
    assert 'list[i].rules = rd8(&r) & NETPLAY_RULES_MASK' in NETPLAY_C
    assert 'req[21] = 1;' in SERVERS_C and 'list[k].rules = b[at + k] & NETPLAY_RULES_MASK' in SERVERS_C
    assert '#define ONLINE_NEW_VENUE(fe) ((fe) >= 28 && (fe) <= 63)' in DATA_H
    assert '#define ONLINE_NEW_FIGHTER(id) ((id) >= 100 && (id) <= 127)' in DATA_H
    # the hub's venue list is the venue.xml the game reads (with the composed new venues)
    assert 'pc_content_file("venue.xml", composed, sizeof composed)' in DATA_C
    # ERROR 15 keeps the hub session and says the news line; the hub says it too
    assert 'code == 13 ||\n                            code == 15)' in NETPLAY_C
    assert '"New community content is out: restart FFNY Online to get it"' in NETPLAY_H
    assert 'upper(NETPLAY_CONTENT_NEWS_LINE)' in SCREEN and '"NEW COMMUNITY CONTENT IS OUT"' in SCREEN
    for page in ('create-standard', 'room-standard', 'rooms-standard', 'fighters-standard', 'fighters-new',
                 'vote-new', 'hub-news'):
        assert '"%s"' % page in SCREEN, page


def test_new_badge_and_news_contract():
    assert c_define(CONTENT_H, 'PC_CONTENT_NEW_DAYS') == 14
    assert 'recomp_settings_get_text("content", "picked"' in CONTENT_C
    assert 'mod-manager" HOST_SEP "news.ini"' in CONTENT_C
    assert '"check"' in MODS_CPP and 'BELOW_NORMAL_PRIORITY_CLASS' in MODS_CPP
    # PLAY waits for a running sync (and looks again when the last began over two minutes ago); PLAY NOW skips
    assert 'g_content_play_skip' in LAUNCHER and '2 * 60000' in LAUNCHER


@pytest.mark.skipif(RELAY_SRC is None, reason=SKIP)
def test_relay_agrees():
    rules = (RELAY_SRC / 'protocol_rules.go').read_text(encoding='utf-8')
    lobby = (RELAY_SRC / 'protocol_lobby.go').read_text(encoding='utf-8')
    roomlist = (RELAY_SRC / 'roomlist.go').read_text(encoding='utf-8')
    lobby_go = (RELAY_SRC / 'lobby.go').read_text(encoding='utf-8')
    if 'RulesStandard' not in rules:
        pytest.skip('the relay source predates STANDARD ONLY (server branch feat/content-room-options)')
    assert go_const(rules, 'RulesStandard') == c_define(NETPLAY_H, 'NETPLAY_RULES_STANDARD')
    assert go_const(lobby, 'NewVenueFirst') == 28 and go_const(lobby, 'NewVenueLast') == 63
    assert go_const(lobby, 'NewFighterFirst') == 100 and go_const(lobby, 'NewFighterLast') == 127
    assert 'withRules := m.Caps&CapRoomRules != 0' in roomlist
    assert 'withRules := c.caps&CapRoomRules != 0' in lobby_go

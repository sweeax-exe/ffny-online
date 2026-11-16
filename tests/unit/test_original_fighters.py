"""Build 86, "Original fighters only" or "All fighters (community too)" where a search starts: the game
(src/hooks/netplay.h, netplay.c, netplay_ranked.h, netplay_servers.c, pc_settings.c, online_screen.cpp,
ranked_ui.inc) and the relay (server/relay/original.go) agree on the CAPS bit, QUICK's choice byte after the ping and
RANKED's flag; the choice is remembered per match type; a room for original fighters only shows no community
fighters and no skin window."""
import re
from pathlib import Path

import pytest

from server_src import RELAY as RELAY_SRC, SKIP

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'
NETPLAY_H = (HOOKS / 'netplay.h').read_text(encoding='utf-8')
NETPLAY_C = (HOOKS / 'netplay.c').read_text(encoding='utf-8')
RANKED_H = (HOOKS / 'netplay_ranked.h').read_text(encoding='utf-8')
SERVERS_C = (HOOKS / 'netplay_servers.c').read_text(encoding='utf-8')
SETTINGS_C = (HOOKS / 'pc_settings.c').read_text(encoding='utf-8')
SCREEN = (HOOKS / 'online_screen.cpp').read_text(encoding='utf-8')
RANKED_UI = (HOOKS / 'ranked_ui.inc').read_text(encoding='utf-8')


def c_define(text, name):
    m = re.search(r'^#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)u?\b' % re.escape(name), text, re.M)
    assert m, name
    return int(m.group(1), 0)


def go_const(text, name):
    m = re.search(r'^\s*(?:const\s+)?%s\s+(?:\w+\s+)?=\s+(?:1 << )?(0x[0-9A-Fa-f]+|\d+)\b' % re.escape(name), text, re.M)
    assert m, name
    v = int(m.group(1), 0)
    return 1 << v if '1 << ' in m.group(0) else v


def caps_defines():
    return {n: int(v) for n, v in re.findall(r'^#define\s+(CAPS_\w+)\s+(\d+)u', NETPLAY_C, re.M)}


def test_caps_bit_is_new_and_announced():
    caps = caps_defines()
    assert caps['CAPS_ORIGINAL'] == 1 << 23 == c_define(NETPLAY_H, 'NETPLAY_CAP_ORIGINAL')
    others = [v for n, v in caps.items() if n != 'CAPS_ORIGINAL']
    assert all(v & (1 << 23) == 0 for v in others), 'bit 23 used twice'
    announce = NETPLAY_C[NETPLAY_C.index('put32(cb + n, CAPS_KOTH'):]
    announce = announce[:announce.index('n += 4;')]
    assert 'CAPS_ORIGINAL' in announce


def test_quick_sends_the_choice_after_the_ping():
    body = NETPLAY_C[NETPLAY_C.index('void netplay_lobby_quick(int mode, int max)\n{'):]
    body = body[:body.index('\n}\n')]
    assert 'netplay_original(mode)' in body
    assert 'if (relay_has(CAPS_ORIGINAL))' in body
    # without the ping trailer the ping goes as "unknown" first, so the relay finds the byte where it looks
    assert 'b[2] = b[3] = 0xFF; n = 4;' in body and 'b[n++] = (uint8_t)(original ? 1 : 0);' in body
    # ranked: the flag, only to a relay that took the bit
    assert c_define(RANKED_H, 'NETRANK_FLAG_ORIGINAL') == 4
    assert 'relay_has(CAPS_ORIGINAL) && netplay_original(NETPLAY_ORIGINAL_RANKED)' in NETPLAY_C


def test_choice_is_remembered_per_type():
    assert '"online", "original_fighters", RECOMP_SETTING_INT, 0, 0, 536870911' in SETTINGS_C
    assert 'if (mode == NETPLAY_ORIGINAL_RANKED) return 28;' in NETPLAY_C
    assert 'if (mode >= 0x40 && mode <= 0x42) return 24 + (mode - 0x40);' in NETPLAY_C
    assert 'pc_settings_save()' in NETPLAY_C[NETPLAY_C.index('void netplay_set_original'):]
    # tests beat the setting
    assert 'getenv("RECOMP_TEST_QUICK_ORIGINAL")' in NETPLAY_C


def test_hub_and_ranked_offer_the_choice():
    # QUICK MATCH: LB switches, the door says which, the hint bar names it; the gallery's two pages
    assert 'if (S.hub_sel == 0 && pad.lb && mode_count)' in SCREEN
    assert '"ORIGINAL FIGHTERS ONLY" : "ALL FIGHTERS (COMMUNITY TOO)"' in SCREEN
    assert 'hint(dl, f, hx, 424, "lb", "FIGHTERS")' in SCREEN
    for page in ('hub-original', 'hub-koth-original'):
        assert '"%s"' % page in SCREEN, page
    # ranked: LB too, about the skins (ranked plays the original fighters)
    assert 'if (pad.lb) search_original_toggle(NETPLAY_ORIGINAL_RANKED);' in RANKED_UI
    assert 'hint(dl, f, hx, 410, "lb", "SKINS")' in RANKED_UI


def test_grid_hides_community_in_original_rooms():
    grid = SCREEN[SCREEN.index('const std::vector<OnlineFighter> &grid_fighters()'):]
    grid = grid[:grid.index('\n}\n')]
    hide = grid.index('standard_only(r.settings.fighter_rules)')
    assert 'ONLINE_NEW_FIGHTER(o.id)' in grid[hide:] and hide < grid.index('g_grid_cp.assign')
    assert 'bool no_skins = (rules_room.in_room && standard_only(rules_room.settings.fighter_rules)) ||' in SCREEN
    assert '!no_skins && list[pick].id < 98 && fighter_skins_count' in SCREEN


def test_other_servers_follow_the_choice():
    body = SERVERS_C[SERVERS_C.index('int netservers_quick_move('):]
    body = body[:body.index('\n}\n')]
    assert 'int original = netplay_original(mode);' in body
    assert 'if (original && !std_room) continue;' in body
    assert 'if (!original && std_room) { n += x->players; continue; }' in body


@pytest.mark.skipif(RELAY_SRC is None, reason=SKIP)
def test_relay_agrees():
    f = RELAY_SRC / 'original.go'
    if not f.is_file():
        pytest.skip('the relay source predates the original-fighters choice (server branch feat/standard-preference)')
    go = f.read_text(encoding='utf-8')
    assert go_const(go, 'CapOriginal') == caps_defines()['CAPS_ORIGINAL']
    assert go_const(go, 'QuickAll') == 0 and go_const(go, 'QuickOriginal') == 1
    assert go_const(go, 'RankedFlagOriginal') == c_define(RANKED_H, 'NETRANK_FLAG_ORIGINAL')
    lobby = (RELAY_SRC / 'protocol_lobby.go').read_text(encoding='utf-8')
    assert 'c.pingTrailer(&q.Ping, &q.WithPing)\n\t\tc.prefTrailer(&q)' in lobby.replace('\r\n', '\n')

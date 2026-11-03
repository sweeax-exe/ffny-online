"""Rollback everywhere (build 52): the game (src/hooks/netplay.c, netplay.h) and the relay
(server/relay/rollback_modes.go) must agree on the CAPS bit and the mode mask's bits; the game must
announce the bit, read the mask after the caps word and keep it for the next start; the per-match
fallback must keep its limits; and the protocol doc must say all of it."""
import re
from pathlib import Path
import pytest

from server_src import RELAY as RELAY_SRC, SKIP

if RELAY_SRC is None:
    pytest.skip(SKIP, allow_module_level=True)

ROOT = Path(__file__).resolve().parents[2]
NETPLAY_C = (ROOT / 'src/hooks/netplay.c').read_text(encoding='utf-8')
NETPLAY_H = (ROOT / 'src/hooks/netplay.h').read_text(encoding='utf-8')
ROLLBACK_C = (ROOT / 'src/hooks/rollback.c').read_text(encoding='utf-8')
SETTINGS_C = (ROOT / 'src/hooks/pc_settings.c').read_text(encoding='utf-8')
PROTOCOL_DOC = (ROOT / 'docs/netplay-protocol.md').read_text(encoding='utf-8')
GO = (RELAY_SRC / 'rollback_modes.go').read_text(encoding='utf-8')


def c_define(text, name):
    m = re.search(r'^#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)u?\b' % re.escape(name), text, re.M)
    assert m, name
    return int(m.group(1), 0)


def go_shift(name):
    m = re.search(r'^\s*%s\s+uint32\s+=\s+1\s+<<\s+(\d+)' % re.escape(name), GO, re.M)
    assert m, name
    return 1 << int(m.group(1))


def test_caps_bit_matches_the_relay():
    assert c_define(NETPLAY_C, 'CAPS_RB_MODES') == c_define(NETPLAY_H, 'NETPLAY_CAP_RB_MODES') == go_shift('CapRollbackModes')
    # the game announces it, so the relay sends the mask
    m = re.search(r'put32\(cb \+ n, (CAPS_[A-Z_| \n]+)\)', NETPLAY_C)
    assert m and 'CAPS_RB_MODES' in m.group(1)


def test_mode_bits_match_the_relay():
    for c, go in (('NETPLAY_RB_1V1', 'RbMode1v1'), ('NETPLAY_RB_TEAM', 'RbModeTeam'), ('NETPLAY_RB_FFA', 'RbModeFFA'),
                  ('NETPLAY_RB_KOTH', 'RbModeKoth'), ('NETPLAY_RB_CREW', 'RbModeCrew'), ('NETPLAY_RB_RANKED', 'RbModeRanked')):
        assert c_define(NETPLAY_H, c) == go_shift(go), c
    assert c_define(NETPLAY_H, 'NETPLAY_RB_ALL') == 63
    # the switch file's names, in bit order
    assert '"1v1", "team", "ffa", "koth", "crew", "ranked"' in GO


def test_the_game_reads_and_keeps_the_mask():
    # the mask follows the caps word (header 6 + caps 4)
    assert 'get32(b + 10) & NETPLAY_RB_ALL' in NETPLAY_C and 'len >= 14' in NETPLAY_C
    assert 'rollback_server_modes(' in NETPLAY_C
    assert re.search(r'\{ "online", "rollback_modes", RECOMP_SETTING_INT, 0, 0, 255,', SETTINGS_C)
    # the audio arena at start: the kept answer, whatever the player's setting
    assert 'server_mandates()' in ROLLBACK_C and '"server"' in ROLLBACK_C


def test_the_hill_and_the_fallback_decide_from_shared_flags():
    # the hill's lockstep is lifted only from every player's P_RTT byte 14 bit 1, never from the local mask
    assert 'N.koth_match && !koth && !every_player_must()' in NETPLAY_C
    assert 'N.peer_rb_must[from] = len >= 15 && (p[14] & 2)' in NETPLAY_C
    # the ping limit reads the pings every PC has (P_RTT), the CPU limit stays on this PC
    assert c_define(NETPLAY_C, 'RB_PATH_MAX_MS') == 300
    assert c_define(NETPLAY_C, 'RB_BUDGET_PERCENT') == 50
    assert 'RECOMP_ROLLBACK_BUDGET' in NETPLAY_C
    assert '"This match runs without rollback: %s."' in NETPLAY_C


def test_protocol_doc():
    assert 'bit 9' in PROTOCOL_DOC and 'rollback-everywhere' in PROTOCOL_DOC

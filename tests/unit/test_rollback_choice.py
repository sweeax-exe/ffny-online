"""The player's Rollback: On / Off (build 110): the setting (src/hooks/pc_settings.c, pc_ui.cpp), on by default;
off, this PC's P_RTT byte 7 is 0 (every build since 17 reads it: the match plays in lockstep on every PC, older
builds too) and byte 14 bit 2 says why; the versus screen's line says DELAY (ROLLBACK OFF)."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NETPLAY_C = (ROOT / 'src/hooks/netplay.c').read_text(encoding='utf-8')
NETPLAY_H = (ROOT / 'src/hooks/netplay.h').read_text(encoding='utf-8')
SETTINGS_C = (ROOT / 'src/hooks/pc_settings.c').read_text(encoding='utf-8')
UI_CPP = (ROOT / 'src/hooks/pc_ui.cpp').read_text(encoding='utf-8')
SCREEN_CPP = (ROOT / 'src/hooks/online_screen.cpp').read_text(encoding='utf-8')
PROTOCOL_DOC = (ROOT / 'docs/netplay-protocol.md').read_text(encoding='utf-8')


def body(name):
    m = re.search(r'^static int %s\(void\)\n\{\n(.*?)^\}' % re.escape(name), NETPLAY_C, re.M | re.S)
    assert m, name
    return m.group(1)


def test_the_setting_is_on_by_default_with_on_first():
    assert 'k_rollback_choice[] = { "on", "off", NULL }' in SETTINGS_C
    assert re.search(r'\{ "online", "rollback_choice", RECOMP_SETTING_ENUM, 0, 0, 0, k_rollback_choice,', SETTINGS_C)
    assert '{"online", "rollback_choice", "Rollback"}' in UI_CPP
    assert '"Off: always use input delay: steadier, a bit slower to respond."' in UI_CPP
    # shown on both settings pages (Basics and the Online page)
    assert UI_CPP.count('rollback_choice_setting();') == 2


def test_off_clears_this_pcs_rollback_byte_first():
    b = body('rollback_choice_off')
    assert 'RECOMP_ROLLBACK_PREF' in b and 'rollback_choice' in b and '== 1' in b
    lines = [l.strip() for l in body('this_pc_rolls_back').splitlines() if l.strip()]
    assert lines[0].startswith('if (rollback_choice_off()) return 0;')
    # byte 7 is what every build reads: a match rolls back only when every player's is set
    assert 'p[7] = (uint8_t)N.peer_rb[N.pos];' in NETPLAY_C
    assert 'N.peer_rb[pos] = this_pc_rolls_back();' in NETPLAY_C
    assert 'for (i = 0; i < N.players; i++) if (!N.peer_rb[i]) return 0;' in body('every_player_rolls_back')


def test_byte_14_bit_2_says_why_and_is_reset_per_start():
    assert '| (N.peer_rb_off[N.pos] ? 4 : 0)' in NETPLAY_C
    assert 'N.peer_rb_off[from] = len >= 15 && (p[14] & 4);' in NETPLAY_C
    assert 'N.peer_rb_off[pos] = rollback_choice_off();' in NETPLAY_C
    assert 'for (i = 0; i < MAXP; i++) N.peer_rb_off[i] = 0;' in NETPLAY_C
    # the player who chose Off gets no "without rollback" notice; the other player does
    assert '&& !N.peer_rb_off[N.pos];' in NETPLAY_C
    assert '"%.30s turned rollback off"' in NETPLAY_C


def test_the_versus_line_says_rollback_off():
    assert 'int rb_choice_off;' in NETPLAY_H
    assert 'if (N.peer_rb_off[i]) out->rb_choice_off = 1;' in NETPLAY_C
    assert '"DELAY (ROLLBACK OFF)"' in SCREEN_CPP
    assert '"versus-rboff"' in SCREEN_CPP


def test_the_protocol_doc_says_it():
    assert 'Rollback: On / Off (build 110)' in PROTOCOL_DOC
    assert '`P_RTT` byte 14 bit 2' in PROTOCOL_DOC

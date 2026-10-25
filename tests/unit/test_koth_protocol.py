"""King of the Hill (docs/research/king-of-the-hill.md): the game client (src/hooks/netplay.c, netplay.h)
and the relay (netplay/relay/protocol_koth.go) must agree on the message types, the mode values, the
room size, the line states and the HILL phases. The two are written in different languages, so this
reads both sources and compares the numbers."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NETPLAY_C = (ROOT / 'src/hooks/netplay.c').read_text(encoding='utf-8')
NETPLAY_H = (ROOT / 'src/hooks/netplay.h').read_text(encoding='utf-8')
VOICE_C = (ROOT / 'src/hooks/voice.c').read_text(encoding='utf-8')
GO = (ROOT / 'netplay/relay/protocol_koth.go').read_text(encoding='utf-8')


def c_define(text, name):
    m = re.search(r'^#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)u?\b' % re.escape(name), text, re.M)
    assert m, name
    return int(m.group(1), 0)


def go_const(name):
    m = re.search(r'^\s*%s\s+(?:\w+\s+)?=\s+(?:\w+\()?(0x[0-9A-Fa-f]+|\d+)' % re.escape(name), GO, re.M)
    assert m, name
    return int(m.group(1), 0)


def c_enum(text, enum_name):
    m = re.search(r'enum\s+%s\s*\{([^}]*)\}' % re.escape(enum_name), text)
    assert m, enum_name
    values, next_value = {}, 0
    for item in m.group(1).split(','):
        item = item.strip()
        if not item:
            continue
        name, _, value = item.partition('=')
        if value.strip():
            next_value = int(value.strip(), 0)
        values[name.strip()] = next_value
        next_value += 1
    return values


def test_message_types_match_the_relay():
    assert c_define(NETPLAY_C, 'T_CAPS') == go_const('TypeCaps')
    assert c_define(NETPLAY_C, 'T_HILL') == go_const('TypeHill')
    assert c_define(NETPLAY_C, 'T_RESULT') == go_const('TypeResult')
    assert c_define(NETPLAY_C, 'T_WATCH') == go_const('TypeWatch')


def test_modes_room_size_and_caps_match_the_relay():
    assert c_define(NETPLAY_H, 'NETPLAY_MODE_KOTH') == go_const('ModeKoth')
    assert c_define(NETPLAY_H, 'NETPLAY_MODE_KOTH_RANDOM') == go_const('ModeKothRandom')
    assert c_define(NETPLAY_H, 'NETPLAY_ROOM_SLOTS') == go_const('MaxKothSlots')
    assert c_define(NETPLAY_C, 'CAPS_KOTH') == 1 << 0 == c_define(NETPLAY_H, 'NETPLAY_CAP_KOTH')
    assert c_define(NETPLAY_C, 'WINNER_DRAW') == go_const('WinnerDraw')


def test_hill_phases_line_states_and_results_match_the_relay():
    phases = c_enum(NETPLAY_H, 'NetHillPhase')
    assert [phases[k] for k in ('NETHILL_WAITING', 'NETHILL_CALLING', 'NETHILL_VOTING', 'NETHILL_FIGHTING')] == \
        [go_const(k) for k in ('HillWaiting', 'HillCalling', 'HillVoting', 'HillFighting')]
    states = c_enum(NETPLAY_H, 'NetLineState')
    assert [states[k] for k in ('NETLINE_WAITING', 'NETLINE_CALLED', 'NETLINE_READY', 'NETLINE_FIGHTING', 'NETLINE_AWAY')] == \
        [go_const(k) for k in ('LineWaiting', 'LineCalled', 'LineReady', 'LineFighting', 'LineAway')]
    how = c_enum(NETPLAY_H, 'NetHillHow')
    assert [how[k] for k in ('NETHOW_NONE', 'NETHOW_WON', 'NETHOW_DRAW', 'NETHOW_LEFT', 'NETHOW_VOID')] == \
        [go_const(k) for k in ('HowNone', 'HowWin', 'HowDraw', 'HowLeft', 'HowVoid')]


def test_room_slots_are_apart_from_match_fighters():
    # a fighter in room slot 9 must still be routed (pos_of_slot) and heard (voice)
    assert c_define(NETPLAY_H, 'NETPLAY_MAX_PLAYERS') == 4
    assert re.search(r'pos_of_slot\[ROOMSLOTS\]', NETPLAY_C)
    assert re.search(r'b\[6\] < ROOMSLOTS \? N\.pos_of_slot\[b\[6\]\]', NETPLAY_C)
    assert re.search(r'#define SLOTS\s+NETPLAY_ROOM_SLOTS', VOICE_C)
    init = re.search(r's_player_vol\[SLOTS\] = \{([^}]*)\}', VOICE_C).group(1)
    assert init.count('100') == c_define(NETPLAY_H, 'NETPLAY_ROOM_SLOTS')

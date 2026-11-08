"""King of the Hill (docs/research/king-of-the-hill.md): the game client (src/hooks/netplay.c, netplay.h)
and the relay (server/relay/protocol_koth.go) must agree on the message types, the mode values, the
room size, the line states and the HILL phases. The two are written in different languages, so this
reads both sources and compares the numbers."""
import re
from pathlib import Path
import pytest

from server_src import RELAY as RELAY_SRC, SKIP

if RELAY_SRC is None:
    pytest.skip(SKIP, allow_module_level=True)

ROOT = Path(__file__).resolve().parents[2]
NETPLAY_C = (ROOT / 'src/hooks/netplay.c').read_text(encoding='utf-8')
NETPLAY_H = (ROOT / 'src/hooks/netplay.h').read_text(encoding='utf-8')
VOICE_C = (ROOT / 'src/hooks/voice.c').read_text(encoding='utf-8')
GO = (RELAY_SRC / 'protocol_koth.go').read_text(encoding='utf-8')


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


def test_ffa_rotation_matches_the_relay():
    # build 66: FFA rotation rooms (mode 0x42, up to eight, CAPS bit 13; RESULT may carry the order)
    if 'ModeFFARot' not in GO:
        pytest.skip('the relay source checked out has no FFA rotation yet (server branch feat/ffa-rotation)')
    assert c_define(NETPLAY_H, 'NETPLAY_MODE_FFAROT') == go_const('ModeFFARot')
    assert c_define(NETPLAY_H, 'NETPLAY_FFAROT_SLOTS') == go_const('MaxFFARotSlots')
    assert re.search(r'^\s*CapFFARot uint32 = 1 << 13$', GO, re.M)
    assert c_define(NETPLAY_C, 'CAPS_FFAROT') == 1 << 13 == c_define(NETPLAY_H, 'NETPLAY_CAP_FFAROT')
    assert 'CAPS_TSET | CAPS_FFAROT' in NETPLAY_C                # announced in CAPS
    assert 'N.rot_match = NETPLAY_IS_FFAROT(N.roomv.settings.mode);' in NETPLAY_C
    assert 'rb[n++] = (uint8_t)N.koth_order_n;' in NETPLAY_C      # RESULT's finishing order
    assert '(N.koth_match && !N.rot_match)' in NETPLAY_C          # a leaver: the others fight on


def test_cpu_level_matches_the_relay():
    # build 67: the room's CPU level (CAPS bit 15; 0 Hard, 1 Normal, 2 Easy; 3-Way and Free for All only)
    path = RELAY_SRC / 'protocol_cpu.go'
    if not path.exists():
        pytest.skip('the relay source checked out has no CPU level yet (server branch feat/cpu-level)')
    go = path.read_text(encoding='utf-8')
    def const(name):
        m = re.search(r'^\s*%s\s+(?:\w+\s+)?=\s+(\d+)(?:\s*<<\s*(\d+))?' % name, go, re.M)
        assert m, name
        return int(m.group(1)) << int(m.group(2) or 0)
    assert c_define(NETPLAY_C, 'CAPS_CPULEVEL') == const('CapCPULevel') == c_define(NETPLAY_H, 'NETPLAY_CAP_CPULEVEL')
    assert [c_define(NETPLAY_H, 'NETPLAY_CPU_' + k) for k in ('HARD', 'NORMAL', 'EASY')] ==         [const('CPU' + k) for k in ('Hard', 'Normal', 'Easy')]
    assert 'func cpuFill(mode uint8) bool { return mode == 2 || mode == 3 }' in go
    assert '#define NETPLAY_CPU_FILL(mode) ((mode) == 2 || (mode) == 3)' in NETPLAY_H
    assert "CAPS_SPEC_LATE | CAPS_CPULEVEL |" in NETPLAY_C       # announced in CAPS


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


def test_the_results_screen_never_holds_the_next_fight():
    # build 34: a deferred START is called off once the relay ended that fight (the fight count moved
    # on), Rematch on a hill fight's results does what No does, the king hears who waits, and P_RTT
    # tells the waiting challenger why (byte 12; older builds send and read 12 bytes)
    assert re.search(r'N\.start_deferred && N\.koth_match &&\s*\(h\.fights != N\.koth_fights \|\| '
                     r'\(h\.phase == NETHILL_FIGHTING && h\.seed != N\.start_seed\)\)', NETPLAY_C)
    assert 'end_hub_match("The next fight was called off: back on the hill.")' in NETPLAY_C
    assert 'deferred = N.lobby && N.koth_results && NETPLAY_IS_HILL(N.roomv.settings.mode);' in NETPLAY_C
    assert 'defjam_fe_call("MemCard.QuerySave( battle/rewardun.swf )")' in NETPLAY_C
    assert 'defjam_fe_call("Game.Quit()")' in NETPLAY_C
    assert 'is waiting for you: choose No to start the next fight.' in NETPLAY_C
    assert 'p[12] = N.start_deferred ? 1 : 0;' in NETPLAY_C
    # build 40: byte 13, the agreed input delay, for spectators (docs/research/spectator-mode.md)
    assert 'p[13] = (uint8_t)(N.delay_fixed ? N.delay : 0);' in NETPLAY_C
    # build 45: byte 14, rollback also with a created fighter (rollback-plan-2026-11.md, fifth round)
    # build 52: bit 1, the server makes rollback mandatory for the match's mode on the sender
    assert 'p[14] = (uint8_t)(1 | (N.peer_rb_must[N.pos] ? 2 : 0));' in NETPLAY_C
    # build 54: bytes 15-16, the sender's rollback ping limit for the match (rb_path_limit takes the lowest)
    assert 'p[15] = (uint8_t)N.peer_rb_max[N.pos];' in NETPLAY_C and 'send_data(p, 17);' in NETPLAY_C
    assert 'N.peer_rb_max[from] = len >= 17 ? (p[15] | (p[16] << 8)) : 0;' in NETPLAY_C
    assert 'N.peer_rb_cp[from] = len >= 15 && (p[14] & 1);' in NETPLAY_C
    assert "leave the last fight's results" in NETPLAY_C


def test_late_spectator_type_and_caps_match_the_relay():
    # a spectator that comes while the match runs (docs/research/spectator-mode.md section 5): WATCHLATE
    # and the client's CAPS bit 11, from the relay's protocol_spectate.go
    spec = (RELAY_SRC / 'protocol_spectate.go').read_text(encoding='utf-8')
    m = re.search(r'^\s*TypeWatchLate\s+MsgType\s+=\s+(0x[0-9A-Fa-f]+)', spec, re.M)
    assert m and c_define(NETPLAY_C, 'T_WATCHLATE') == int(m.group(1), 0)
    m = re.search(r'^\s*CapSpectateLate\s+uint32\s+=\s+1\s*<<\s*(\d+)', spec, re.M)
    assert m and c_define(NETPLAY_C, 'CAPS_SPEC_LATE') == 1 << int(m.group(1))
    m = re.search(r'^\s*CapSpectateRematch\s+uint32\s+=\s+1\s*<<\s*(\d+)', spec, re.M)
    assert m and c_define(NETPLAY_C, 'CAPS_SPEC_REMATCH') == 1 << int(m.group(1))


def test_random_venue_matches_the_relay():
    # build 67: RANDOM in the venue vote (CAPS bit 14, VOTE choice and ROOM voted 0xFE)
    rnd = RELAY_SRC / 'random_venue.go'
    if not rnd.exists():
        pytest.skip('the relay source checked out has no RANDOM vote yet (server branch feat/random-pick)')
    go = rnd.read_text(encoding='utf-8')
    m = re.search(r'^\s*CapRandomVenue\s+uint32\s+=\s+1\s*<<\s*(\d+)', go, re.M)
    assert m and c_define(NETPLAY_C, 'CAPS_RANDOM_VENUE') == 1 << int(m.group(1)) == c_define(NETPLAY_H, 'NETPLAY_CAP_RANDOM_VENUE')
    m = re.search(r'^\s*VoteRandom\s+uint8\s+=\s+(0x[0-9A-Fa-f]+)', go, re.M)
    assert m and c_define(NETPLAY_H, 'NETPLAY_VOTE_RANDOM') == int(m.group(1), 0)
    assert 'CAPS_FFAROT | CAPS_RANDOM_VENUE); n += 4;' in NETPLAY_C      # announced in CAPS
    # every other CAPS bit the client knows is a different one
    bits = [c_define(NETPLAY_C, n) for n in re.findall(r'^#define\s+(CAPS_[A-Z_]+)\s+\d+u', NETPLAY_C, re.M)]
    assert len(bits) == len(set(bits))

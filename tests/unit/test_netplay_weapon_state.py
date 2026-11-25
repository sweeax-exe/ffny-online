"""Build 112: the weapon state of an online fight is the same on every PC (src/hooks/netplay.c).

Two inputs outside the simulation placed a weapon a spectator holds: the spectator's size, drawn at load from the
host-timed generator (crowd_reset now sets it from the session), and the model's size, hand pose and "on screen" byte,
which only the crowd's draw wrote (weapon_update now sizes and poses every holder before the manager's update). The
weapons are in the checksum, and Team Tower Online floors roll back again with the CPUs' full weapon and environment
ramp."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'


def netplay():
    return (HOOKS / 'netplay.c').read_text(encoding='utf-8')


def body(src, head):
    start = src.index(head)
    return src[start:src.index('\n}\n', start)]


def test_crowd_reset_sets_each_spectators_size_from_the_session():
    reset = body(netplay(), 'static void crowd_reset(uint32_t arena)')
    assert 'MEM32(m + 0x11Cu) = bits;' in reset
    assert 'mix32(N.session ^ (0xC20E0000u + i)) % 10u' in reset          # (rand() % 10 - 5), as sub_0007E170
    assert 'MEMF(0x00292378u)' in reset and 'MEMF(0x00289478u)' in reset
    assert 'crowd_scale_on()' in reset                                    # RECOMP_CROWD_SCALE=keep for tests


def test_weapon_update_poses_the_holding_spectators_first():
    src = netplay()
    upd = body(src, 'static void weapon_update(void)')
    pose = upd.index('crowd_hold_pose(anim, was)')
    assert pose < upd.index('sub_000BCC50();')
    assert upd.index('MEM8(anim[k] + 0x42u) = was[k];') > upd.index('sub_000BCC50();')   # the draw's byte goes back
    hold = body(src, 'static int crowd_hold_pose(uint32_t *anim, uint8_t *was)')
    assert hold.index('sub_000230A0') < hold.index('sub_000252A0')       # the pose, then the attachments (sub_00084B40)
    assert hold.index('guest_call(sub_00024C20, model, 3, size)') < hold.index('sub_000230A0')   # the size first
    assert 'MEM32(a + 0x3Cu)' in hold                                    # member+0x11C, as sub_000838E0 passes it
    assert 'MEM8(a + 0x42u) = 1;' in hold
    assert 'owner < 4u' in hold                                           # spectators only (owner 4.. = record owner-4)
    assert "RECOMP_NETPLAY_CROWD_HOLD" in src


def test_the_weapon_manager_moves_only_in_updates_with_a_fight_step():
    src = netplay()
    upd = body(src, 'static void weapon_update(void)')
    gate = upd.index('if (N.lobby && !g_stepped && weapon_steps_only())')
    assert gate < upd.index('sub_000BCC50();')
    assert 'g_stepped = 1;' in body(src, 'void netplay_on_step(void)\n{')
    assert 'g_stepped = 0;' in body(src, 'void netplay_on_update(void)\n{')
    assert 'g_stepped = 0;\n        guest_call(sub_000DD930, input, 1, &dt);' in src    # every update a rollback runs again
    assert 'RECOMP_NETPLAY_WEAPON_UPDATES' in src


def test_weapons_are_in_the_checksum_by_default():
    src = netplay()
    m = re.search(r'fold = !\(getenv\("RECOMP_NETPLAY_HASH_WEAPONS"\) && \*getenv\("RECOMP_NETPLAY_HASH_WEAPONS"\) == \'0\'\);', src)
    assert m, 'the weapons fold into the checksum unless RECOMP_NETPLAY_HASH_WEAPONS=0'
    assert 'if (fold) N.hash_at[k] = (N.hash_at[k] ^ w) * 16777619u;' in src


def test_tower_floors_roll_back_again():
    src = netplay()
    rolls = body(src, 'static int every_player_rolls_back(void)\n{')
    assert '*getenv("RECOMP_ROLLBACK_TOWER") == \'0\'' in rolls           # lockstep only on request (tests)
    assert '== \'1\'))' not in rolls.split('N.tower_match', 1)[1].split('\n', 1)[0]
    rules = (HOOKS / 'towers_ai_rules.c').read_text(encoding='utf-8')
    assert 'f->online' not in rules                                        # the weapon/environment ramp online too
    assert 'int online;' not in (HOOKS / 'towers_ai_rules.h').read_text(encoding='utf-8')


def test_spectators_hand_out_no_weapons_outside_the_fight_steps():
    src = netplay()
    crowd = body(src, 'void netplay_crowd_update(void)\n{')
    assert 'int hush = N.lobby && !g_stepped && crowd_offers_in_step();' in crowd
    assert crowd.index('MEM8(CROWD_HUSH) = (uint8_t)(was | 1u);') < crowd.index('sub_00081170();')
    assert '#define CROWD_HUSH 0x003BE0F4u' in src
    assert src.index('static int g_stepped;') < src.index('void netplay_crowd_update(void)\n{')
    reset = body(src, 'static void crowd_reset(uint32_t arena)')
    assert 'guest_call(sub_000BADA0, obj, 0, NULL);' in reset                  # an orphaned spectator weapon goes
    assert 'owner < 4u' in reset
    assert 'RECOMP_NETPLAY_CROWD_OFFERS' in src

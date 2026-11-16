"""Big Arena (open-world phase 1, src/hooks/openworld_rules.c), compiled natively: where a knocked-out fighter comes
back, who wins, the order the losers go down at the end, the facing toward the centre. Plus the wiring that keeps it
deterministic online: the state goes with every rollback save and into the checksum, the mode is offline only
without the test switch, and the hooks it needs exist. Needs MSVC (`cl` on PATH) for the native part."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include "openworld.h"

static const float ring[8][2] = { { 0.0f, -260.0f }, { 183.85f, -183.85f }, { 260.0f, 0.0f }, { 183.85f, 183.85f },
    { 0.0f, 260.0f }, { -183.85f, 183.85f }, { -260.0f, 0.0f }, { -183.85f, -183.85f } };

static int pick(int lives, const float (*live)[2], float safe)
{
    OwSpawnCtx c;
    int i;
    for (i = 0; i < 8; i++) { c.spawn_x[i] = ring[i][0]; c.spawn_z[i] = ring[i][1]; }
    c.spawns = 8;
    c.lives = lives;
    for (i = 0; i < lives; i++) { c.live_x[i] = live[i][0]; c.live_z[i] = live[i][1]; }
    c.safe = safe;
    return ow_rules_pick_spawn(&c);
}

int main(void)
{
    static const float none[1][2] = { { 0, 0 } };
    static const float centre[2][2] = { { 0, 0 }, { 10, 10 } };
    static const float north[1][2] = { { 0, -250 } };
    static const float crowd[4][2] = { { 0, -260 }, { 260, 0 }, { 0, 260 }, { -260, 0 } };
    int a[4] = { 2, 3, 3, 0 }, d[4] = { 1, 2, 1, 0 }, z[4] = { 0, 0, 0, 0 };
    printf("pick|none|%d\n", pick(0, none, 140));
    printf("pick|centre|%d\n", pick(2, centre, 140));
    printf("pick|north|%d\n", pick(1, north, 140));
    printf("pick|crowd|%d\n", pick(4, crowd, 400));
    printf("winner|%d|%d|%d\n", ow_rules_winner(a, d, 0xF), ow_rules_winner(z, z, 0xF), ow_rules_winner(a, d, 0x9));
    printf("loser|%d|%d|%d|%d\n", ow_rules_next_loser(a, d, 0xF, 2, 0), ow_rules_next_loser(a, d, 0xF, 2, 1u << 3),
           ow_rules_next_loser(a, d, 0xF, 2, (1u << 3) | 1u), ow_rules_next_loser(a, d, 0xF, 2, 0xB));
    printf("yaw|%.1f|%.1f|%.1f|%.1f\n", ow_rules_yaw_to_centre(0, -260), ow_rules_yaw_to_centre(260, 0),
           ow_rules_yaw_to_centre(0, 260), ow_rules_yaw_to_centre(0, 0));
    {   /* free roam: who a fighter at the origin facing +z (yaw 0) locks on to */
        static const float cx[4] = { 0.0f, 40.0f, -100.0f, 0.0f }, cz[4] = { 0.0f, 200.0f, 50.0f, -60.0f };
        float y = -1.0f;
        int n[9], k;
        static const unsigned nib[9] = { 1, 9, 8, 10, 2, 6, 4, 5, 3 };
        printf("roam|%d|%d|%d|%d|%.1f|%.1f\n", ow_rules_roam_pick(0, 0, 0, cx, cz, 0xE, 300, 60, 140),
               ow_rules_roam_pick(0, 0, 180, cx, cz, 0xE, 300, 60, 140),
               ow_rules_roam_pick(0, 0, 180, cx, cz, 0x6, 300, 60, 40),
               ow_rules_roam_pick(0, 0, 0, cx, cz, 0x0, 300, 60, 140),
               ow_rules_angle_diff(170, -170), ow_rules_angle_diff(-90, 90));
        for (k = 0; k < 9; k++) { y = -1.0f; n[k] = ow_rules_nibble_heading(nib[k], &y) ? (int)y : 999; }
        printf("nib|%d|%d|%d|%d|%d|%d|%d|%d|%d\n", n[0], n[1], n[2], n[3], n[4], n[5], n[6], n[7], n[8]);
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def out():
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    import tempfile
    folder = Path(tempfile.mkdtemp(prefix='owrules'))
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'owrules.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/openworld_rules.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    text = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    return {tuple(line.split('|')[:2]) if line.startswith('pick') else line.split('|')[0]: line.split('|')
            for line in text.splitlines()}


def test_spawn_pick(out):
    assert out[('pick', 'none')][2] == '0'          # nobody standing: the first point
    # two in the middle, one a little toward +x+z: every point is safe, the nearest to the fight is that diagonal
    assert out[('pick', 'centre')][2] == '3'
    # someone at the north point: the nearest safe point, not the far side (back into the action quickly)
    assert out[('pick', 'north')][2] in ('1', '7')
    assert out[('pick', 'north')][2] == '1'         # tie between 1 and 7: the lower index
    # no point is safe (400): the one farthest from its nearest fighter, a diagonal
    assert out[('pick', 'crowd')][2] in ('1', '3', '5', '7')


def test_free_roam_pick_and_headings(out):
    # facing +z: slot 1 (40, 200) is in the cone; facing -z: slot 3 (0, -60) is right in front;
    # facing -z without slot 3: nobody in the cone, nearest within 40: nobody; no candidates: -1
    assert out['roam'][1:5] == ['1', '3', '-1', '-1']
    assert float(out['roam'][5]) == pytest.approx(20.0) and abs(float(out['roam'][6])) == pytest.approx(180.0)
    # the walker's table (0x2EF510) by nibble: up 180, up-right 135, right 90, ..., up-left -135; up+down none
    assert out['nib'][1:] == ['180', '135', '90', '45', '0', '-45', '-90', '-135', '999']


def test_winner_and_loser_order(out):
    # scores 2 3 3 0, knocked out 1 2 1 0: slots 1 and 2 tie on 3, slot 2 was knocked out less
    assert out['winner'][1:] == ['2', '0', '0']     # all level: slot 0; only slots 0 and 3: slot 0 (2 > 0)
    # the last placed goes down first: slot 3 (0), then slot 0 (2), then slot 1; then nobody
    assert out['loser'][1:] == ['3', '0', '1', '-1']


def test_yaw_to_centre(out):
    # forward is (sin yaw, cos yaw): from the north (z -260) the centre is +z (0); from +x it is -90
    got = [float(v) % 360.0 for v in out['yaw'][1:]]
    assert got == [0.0, 270.0, 180.0, 0.0]


def read(rel):
    return (ROOT / rel).read_text(encoding='utf-8')


def test_state_goes_with_rollback_and_the_checksum():
    net = read('src/hooks/netplay.c')
    assert 'uint8_t ow[OW_STATE_BYTES];' in net
    assert 'ow_state_save(N.snap[slot].ow);' in net and 'ow_state_load(N.snap[slot].ow);' in net
    assert re.search(r'uint32_t ow = ow_state_hash\(\);\s*if \(ow\) N\.hash_at\[k\]', net)
    ow = read('src/hooks/openworld.c')
    assert 'typedef char ow_state_fits[sizeof(BigArena) <= OW_STATE_BYTES ? 1 : -1];' in ow
    # 0 while off: the checksum of every other fight is unchanged
    assert re.search(r'uint32_t ow_state_hash\(void\)\s*\{[^}]*if \(!BA\.on\) return 0;', ow)


def test_state_holds_the_round_rules_and_free_roam_without_holes():
    """Build 87 put two branches' fields in one state: the round rules (rules, end_kind, unlimited, end_step) and free
    roam (roam_*). The checksum runs over every byte of the struct, so the layout may have no padding the compiler
    chose (a hole's bytes are whatever memcpy left), and it has to fit the room each rollback save keeps."""
    ow = read('src/hooks/openworld.c')
    state = re.search(r'typedef struct \{(.*?)\} BigArena;', ow, re.S).group(1)
    state = re.sub(r'/\*.*?\*/', '', state, flags=re.S)
    sizes = {'uint32_t': 4, 'int32_t': 4, 'float': 4, 'uint16_t': 2, 'int16_t': 2, 'uint8_t': 1, 'int8_t': 1}
    offset, names = 0, []
    for decl in filter(None, (d.strip() for d in state.split(';'))):
        kind, rest = decl.split(None, 1)
        size = sizes[kind]
        for item in rest.split(','):
            m = re.fullmatch(r'\s*(\w+)(?:\[(\d+)\])?\s*', item)
            assert offset % size == 0, f'{m.group(1)} at {offset} needs padding before it'
            offset += size * int(m.group(2) or 1)
            names.append(m.group(1))
    assert offset % 4 == 0, 'padding at the end'
    for field in ('rules', 'end_kind', 'unlimited', 'end_step', 'roam_on', 'roam_steps', 'roam_speed', 'roam_mode',
                  'roam_pending', 'roam_last', 'roam_turn', 'spawn_hash'):
        assert names.count(field) == 1, field
    room = int(re.search(r'#define OW_STATE_BYTES (\d+)', read('src/hooks/openworld.h')).group(1))
    assert offset == 132 and offset <= room
    # both sets are zeroed at a fight's start and hashed whole
    assert 'memset(&BA, 0, sizeof BA);' in ow
    assert 'for (i = 0; i < sizeof BA; i++) h = (h ^ p[i]) * 16777619u;' in ow


def test_a_revived_fighter_comes_back_at_its_spawn_point():
    """Build 87: the revive also moves the animation's own place (+0xC4, +0xCC, +0xD4). The first animation after it
    starts from there (sub_001A85A0, flags 4 and 8), which had put every revived fighter back where its body lay."""
    ow = read('src/hooks/openworld.c')
    revive = re.search(r'static void ba_revive\(int s, int pick_spawn\)\n\{.*?\n\}\n', ow, re.S).group(0)
    for line in ('MEMF(r + 0x44u) = c.spawn_x[i];', 'MEMF(r + 0x4Cu) = c.spawn_z[i];', 'MEMF(r + 0x54u) = yaw;',
                 'MEMF(r + 0xC4u) = c.spawn_x[i];', 'MEMF(r + 0xCCu) = c.spawn_z[i];', 'MEMF(r + 0xD4u) = yaw;'):
        assert line in revive, line
    # after the state change (the revive's own calls), so nothing of the knockout is left to copy back
    assert revive.index('call2(sub_001B5420, 2, r, 1u);') < revive.index('MEMF(r + 0xC4u)')


def test_offline_only_and_hooks():
    ow = read('src/hooks/openworld.c')
    assert 'RECOMP_OW_ONLINE_TEST' in ow and 'netplay_active()' in ow and 'cheats_in_story()' in ow
    manual = read('src/recomp_manual.c')
    for hook in ('void sub_001A6750(void) { ow_match_end_check(sub_001A6750_gen); }',
                 'void sub_001A6A80(void) { ow_result_record(sub_001A6A80_gen); }',
                 'ow_hit(MEM32(g_esp + 4), MEM32(g_esp + 8));', 'ow_health_change();', 'ow_match_start();'):
        assert hook in manual
    settings = read('src/hooks/pc_settings.c')
    assert re.search(r'\{\s*"openworld",\s*"big_arena",\s*RECOMP_SETTING_BOOL,\s*0,', settings)
    assert re.search(r'\{\s*"openworld",\s*"camera",\s*RECOMP_SETTING_ENUM,\s*1,', settings)


def test_spawn_ring_matches_studio_template():
    ow = read('src/hooks/openworld.c')
    m = re.search(r'k_spawn\[8\]\[2\] = \{(.*?)\};', ow, re.S)
    pts = [tuple(float(v.rstrip('f')) for v in p.split(',')) for p in re.findall(r'\{\s*([-0-9.f]+,\s*[-0-9.f]+)\s*\}', m.group(1))]
    assert len(pts) == 8
    import math
    for k, (x, z) in enumerate(pts):        # mapbasics.BIGARENA_SPAWNS: radius 260, 45 degrees apart from (0, -260)
        assert x == pytest.approx(260 * math.sin(math.radians(45 * k)), abs=0.01)
        assert z == pytest.approx(-260 * math.cos(math.radians(45 * k)), abs=0.01)

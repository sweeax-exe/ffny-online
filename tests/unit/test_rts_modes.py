"""Build 95, three Run the Streets wishes from the community: the Points round mode (no knockouts out of the round;
src/hooks/openworld_rules.c compiled natively, and its wiring in the checksummed Big Arena state, the room settings,
VS CPU and the relay's CAPS bit 30), this PC's camera options (field of view and distance, render camera only) and
the minimap that always shows this PC's side (crew, team) and everyone else only by their combat pings."""
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include "openworld.h"

int main(void)
{
    int t, k, b;
    char text[48];
    for (t = 0; t < OW_RTS_TIMES; t++)
        for (k = 0; k < OW_RTS_KOS; k++) {
            int tt, kk;
            b = ow_rules_rts_encode_mode(t, k, 1);
            ow_rules_rts_decode(b, &tt, &kk);
            ow_rules_rts_text(b, text, (int)sizeof text);
            printf("pts|%d|%d|%d|%d|%d|%d|%d|%d|%s\n", t, k, b, ow_rules_rts_valid(b), ow_rules_rts_points(b), tt,
                   ow_rules_rts_limit_steps(b), kk, text);
        }
    for (t = 0; t < OW_RTS_TIMES; t++)
        for (k = 0; k < OW_RTS_KOS; k++)
            printf("ko|%d|%d|%d|%d\n", t, k, ow_rules_rts_encode_mode(t, k, 0) == ow_rules_rts_encode(t, k),
                   ow_rules_rts_points(ow_rules_rts_encode_mode(t, k, 0)));
    return 0;
}
'''


@pytest.fixture(scope='module')
def out():
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = Path(tempfile.mkdtemp(prefix='rtsmodes'))
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'rtsmodes.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/openworld_rules.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8').splitlines()


def rows(out, kind):
    return [line.split('|')[1:] for line in out if line.startswith(kind + '|')]


def read(rel):
    return (ROOT / rel).read_text(encoding='utf-8')


def test_points_bytes(out):
    seen = {}
    for t, k, b, valid, points, tt, steps, kk, text in rows(out, 'pts'):
        b = int(b)
        assert valid == '1' and points == '1'
        assert b & 0xF0 == 0x90 and (b >> 2) & 3 == 1          # the KO bits stay the default's
        want_t = 1 if t == '3' else int(t)                      # no time limit is never Points: 10 minutes
        assert int(tt) == want_t and kk == '1'
        assert int(steps) == 60 * [300, 600, 900][want_t]
        assert text == f'{[5, 10, 15][want_t]} MIN  -  POINTS'
        seen[(t, k)] = b
    assert sorted(set(seen.values())) == [0x94, 0x95, 0x96]
    # the knockout mode is exactly the build 87 encoding
    assert all(same == '1' and points == '0' for _, _, same, points in rows(out, 'ko'))


def test_points_mode_is_simulated_state():
    ow = read('src/hooks/openworld.c')
    state = re.search(r'typedef struct \{(.*?)\} BigArena;', ow, re.S).group(1)
    assert 'uint8_t points;' in state                           # saved with rollback, in the checksum
    assert 'typedef char ow_state_fits[sizeof(BigArena) <= OW_STATE_BYTES ? 1 : -1];' in ow
    body = ow[ow.index('static void ba_points_rules(void)'):ow.index('static int ba_env_on')]
    assert 'ow_rules_rts_points(BA.rules)' in body and 'BA.score_limit = 0;' in body
    assert 'BA.respawn_steps = (uint16_t)OW_RTS_POINTS_BACK;' in body
    # online from START's byte, offline from the page: both branches decide it from BA.rules only
    start = ow[ow.index('void ow_match_start(void)'):ow.index('int ow_rts_active(void)')]
    assert start.count('ba_points_rules();') == 2
    assert 'ba_revive(s, !BA.points);' in ow                    # back up where it fell


def test_points_in_the_ui_and_on_the_wire():
    ui = read('src/hooks/online_screen.cpp')
    assert '"ROUND MODE"' in ui and 'case R_RTS_MODE: s.rounds = rts_rules_step(s.rounds, 2, step); break;' in ui
    assert 'bool rts_points_offered() { return gallery_on() || netplay_relay_rts_points(); }' in ui
    assert '"CPU FIGHTERS", "ROUND MODE", "TIME LIMIT", "KO TARGET", ""' in ui
    net = read('src/hooks/netplay.c')
    assert re.search(r'#define CAPS_RTS_POINTS 1073741824u', net)
    assert '| CAPS_RTS_POINTS |' in net
    assert 'NETPLAY_CAP_RTS_POINTS 1073741824u' in read('src/hooks/netplay.h')
    assert '"rts_round_mode"' in read('src/hooks/rts.c')
    assert re.search(r'"openworld",\s*"rts_round_mode",\s*RECOMP_SETTING_ENUM,\s*0,', read('src/hooks/pc_settings.c'))
    hud = read('src/hooks/fight_hud_ui.inc')
    assert 'MOST POINTS WINS' in hud


def test_camera_options_are_render_only():
    ow = read('src/hooks/openworld.c')
    body = ow[ow.index('static void camcfg_read(void)'):ow.index('static int followcam_wanted(void)')]
    assert 'MEM32(' in body and 'MEMF(' not in body and 'BA.' in body and 'BA.on = ' not in body
    assert 'C.dist *= CAMcfg.dist_scale;' in ow
    ws = read('src/patches/widescreen.c')
    assert 'if (ow_render_fov(vp, &of)) MEMF(pfov) = of > g_fov_cap ? g_fov_cap : of;' in ws
    assert 'ow_render_fov(vp, &of)' in read('src/hooks/rts_social.c')    # the overlays' lens follows it
    settings = read('src/hooks/pc_settings.c')
    assert re.search(r'"openworld",\s*"fov",\s*RECOMP_SETTING_INT,\s*0,\s*0,\s*100,', settings)
    assert re.search(r'"openworld",\s*"camera_distance",\s*RECOMP_SETTING_ENUM,\s*1,', settings)
    ui = read('src/hooks/pc_ui.cpp')
    assert 'settings_group("openworld", { "fov", "camera_distance", "minimap_team" });' in ui


def test_minimap_shows_your_side_and_pings_for_the_rest():
    inc = read('src/hooks/rts_social_ui.inc')
    assert 'c.team = recomp_settings_get("openworld", "minimap_team", 1) != 0;' in inc
    assert 'if (c.team && !mate) continue;' in inc
    assert 'nv.type == ONLINE_MODE_TEAM && nv.count == 4' in inc and 'sv.crew_roster' in inc
    # the pings come before the fighters' dots and do not depend on the side
    assert inc.index('if (c.pings) {') < inc.index('RtsSides side = ')
    assert re.search(r'"openworld",\s*"minimap_team",\s*RECOMP_SETTING_BOOL,\s*1,', read('src/hooks/pc_settings.c'))

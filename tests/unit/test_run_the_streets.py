"""Run the Streets (build 86, closed test): the pure rules it adds to src/hooks/openworld_rules.c (the map's spawn
points from its bigarena json, the account service's feature answer, camera-relative sticks), compiled natively; the
launcher's night light for the map (setup/ffnycontent/newvenue.py fighter_light); and the wiring that keeps the mode
hidden without access and deterministic online. Needs MSVC (`cl` on PATH) for the native part."""
import math
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'setup'))

DRIVER = r'''
#include <stdio.h>
#include "openworld.h"

int main(void)
{
    static const char *json = "{\"version\": 1, \"mode\": \"bigarena\", \"size\": [1170, 1170],\n"
        " \"spawns\": [\n  [-204.8, -204.8],\n  [0.0, 204.8],\n  [444.6, -204.8]\n ], \"venue\": \"V2BG_28A\"}";
    float x[16], z[16], sx, sy;
    char why[32];
    int n = ow_rules_parse_spawns(json, x, z, 16), i;
    printf("spawns|%d", n);
    for (i = 0; i < n; i++) printf("|%.1f,%.1f", x[i], z[i]);
    printf("\n");
    printf("spawns-max|%d\n", ow_rules_parse_spawns(json, x, z, 2));
    printf("spawns-none|%d|%d\n", ow_rules_parse_spawns("{\"mode\": \"bigarena\"}", x, z, 16),
           ow_rules_parse_spawns(NULL, x, z, 16));
    printf("feat-yes|%d|%s\n", ow_rules_features_answer("{\"run_the_streets\": true, \"reason\": \"role\"}", why, 32), why);
    printf("feat-no|%d|%s\n", ow_rules_features_answer("{\"run_the_streets\":false,\"reason\":\"no-role\",\"message\":\"x\"}",
                                                         why, 32), why);
    printf("feat-bad|%d|%s\n", ow_rules_features_answer("{\"error\":\"Sign in first.\"}", why, 32), why);
    /* the stick: up (0, 1) under a camera turned 90 degrees clockwise (looking along +X) is the stock right */
    sx = 0.0f; sy = 1.0f; ow_rules_stick_turn(90.0f, &sx, &sy);
    printf("turn90|%.3f|%.3f\n", sx, sy);
    sx = 0.6f; sy = 0.8f; ow_rules_stick_turn(0.0f, &sx, &sy);
    printf("turn0|%.3f|%.3f\n", sx, sy);
    sx = 1.0f; sy = 0.0f; ow_rules_stick_turn(180.0f, &sx, &sy);
    printf("turn180|%.3f|%.3f\n", sx, sy);
    sx = 0.6f; sy = 0.8f; ow_rules_stick_turn(37.0f, &sx, &sy);
    printf("turnlen|%.4f\n", sx * sx + sy * sy);
    printf("heading|%.1f|%.1f|%.1f|%.1f\n", ow_rules_view_heading(0, 100, 0, 0), ow_rules_view_heading(-100, 0, 0, 0),
           ow_rules_view_heading(0, -100, 0, 0), ow_rules_view_heading(100, 0, 0, 0));
    {   /* sixteen spawn points: the farthest-reaching rule still picks from all of them */
        OwSpawnCtx c;
        for (i = 0; i < 16; i++) { c.spawn_x[i] = (float)(i * 10); c.spawn_z[i] = 0.0f; }
        c.spawns = 16; c.lives = 1; c.live_x[0] = 0.0f; c.live_z[0] = 0.0f; c.safe = 1000.0f;
        printf("pick16|%d\n", ow_rules_pick_spawn(&c));
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
    folder = Path(tempfile.mkdtemp(prefix='rtsrules'))
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'rtsrules.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/openworld_rules.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    text = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    return {line.split('|')[0]: line.split('|')[1:] for line in text.splitlines()}


def test_spawns_from_bigarena_json(out):
    assert out['spawns'] == ['3', '-204.8,-204.8', '0.0,204.8', '444.6,-204.8']
    assert out['spawns-max'] == ['2']
    assert out['spawns-none'] == ['0', '0']
    assert out['pick16'] == ['15']          # nothing is safe at 1000: the farthest of all sixteen


def test_feature_answer(out):
    assert out['feat-yes'] == ['1', 'role']
    assert out['feat-no'] == ['0', 'no-role']
    assert out['feat-bad'] == ['-1', '']


def test_camera_relative_stick(out):
    x, y = map(float, out['turn90'])
    assert (x, y) == pytest.approx((1.0, 0.0), abs=1e-3)       # up under a camera looking along +X: the stock right
    assert list(map(float, out['turn0'])) == pytest.approx([0.6, 0.8], abs=1e-3)
    assert list(map(float, out['turn180'])) == pytest.approx([-1.0, 0.0], abs=1e-3)
    assert float(out['turnlen'][0]) == pytest.approx(1.0, abs=1e-3)   # a full push stays full
    # the stock view (eye at +Z looking at the origin) is heading 0; looking along +X is 90, +Z 180, -X -90
    assert [float(v) for v in out['heading']] == pytest.approx([0.0, 90.0, 180.0, -90.0], abs=0.1)


def test_fighter_light_from_a_night_venue():
    from ffnycontent import newvenue
    ent = ('<entitylist>\n\t<entity>\n\t\t<name>V2BG_03A</name>\n\t\t<lighting>\n\t\t\t<setting>\n\t\t\t\t<count>2</count>\n'
           '\t\t\t\t<name>V2BG03A</name>\n\t\t\t\t<name>V2BG03A_nis</name>\n\t\t\t</setting>\n\t\t\t<proxy>PROX03A</proxy>\n'
           '\t\t</lighting>\n\t\t<characterenvmap>C_ENV03A</characterenvmap>\n\t</entity>\n'
           '\t<entity>\n\t\t<name>V2BG_01B</name>\n\t\t<lighting>\n\t\t\t<setting>\n\t\t\t\t<count>2</count>\n'
           '\t\t\t\t<name>V2BG01B</name>\n\t\t\t\t<name>V2BG01B_nis</name>\n\t\t\t</setting>\n\t\t\t<proxy>PROX01B</proxy>\n'
           '\t\t</lighting>\n\t\t<characterenvmap>C_ENV01B</characterenvmap>\n\t</entity>\n</entitylist>\n')
    block = newvenue.entity_block(ent, type('T', (), {'filename': 'V2BG_03A'})(), 'V2BG_28A')
    lit = newvenue.set_fighter_light(block, ent, '01B')
    assert '<name>V2BG01B</name>' in lit and '<name>V2BG01B_nis</name>' in lit and 'V2BG03A' not in lit.split('<proxy>')[0]
    assert '<proxy>PROX03A</proxy>' in lit                  # the shadow proxy stays the template's
    assert '<characterenvmap>C_ENV01B</characterenvmap>' in lit
    assert '<name>V2BG_28A</name>' in lit
    for bad in ('1B', '99Z', None, 'V2BG_01B'):
        with pytest.raises(newvenue.VenueError):
            newvenue.set_fighter_light(block, ent, bad)


def read(rel):
    return (ROOT / rel).read_text(encoding='utf-8')


def test_hidden_without_access():
    ui = read('src/hooks/online_screen.cpp')
    # the hub lists RUN THE STREETS only with access; the page closes itself when the access goes
    assert re.search(r'\(i != HUB_RTS \|\| rts\)', ui)
    assert 'rts = gallery_on() ? G.rts : rts_access() != 0' in ui
    assert re.search(r'if \(!\(gallery_on\(\) \? G\.rts : rts_access\(\)\)\) \{[^}]*S\.page = HUB;', ui)
    rts = read('src/hooks/rts.c')
    # signed out: no access, and the answer is forgotten; the test switch for harness runs
    assert 'if (st != ACCOUNT_IN)' in rts and 'RECOMP_RTS_ACCESS' in rts and '"/api/features"' in rts


def test_online_rules_are_fixed_and_lockstep():
    ow = read('src/hooks/openworld.c')
    # online: the room's mode decides, with the same rules on every PC (no PC's own settings): build 87, the room's
    # round rules from START (test_rts_round_rules.py)
    m = re.search(r'if \(online && rts\) \{(.*?)\} else if \(rts\) \{', ow, re.S)
    assert m and 'recomp_settings_get' not in m.group(1) and 'getenv' not in m.group(1)
    assert 'BA.rules = (uint8_t)netplay_rts_rules();' in m.group(1)
    assert 'rts = netplay_rts_match();' in ow
    assert 'uint32_t spawn_hash;' in ow     # the map's spawn points are in the checksummed state
    net = read('src/hooks/netplay.c')
    assert 'N.rts_match = NETPLAY_IS_RTS(N.roomv.settings.mode);' in net
    assert re.search(r'if \(N\.rts_match && !\(getenv\("RECOMP_ROLLBACK_RTS"\)', net)
    hdr = read('src/hooks/netplay.h')
    assert '#define NETPLAY_MODE_RTS         0x43' in hdr and '#define NETPLAY_CAP_RTS          16777216u' in hdr
    assert 'put32(cb + n, CAPS_KOTH | NETPLAY_CAP_RTS |' in net       # every build-86 client announces it

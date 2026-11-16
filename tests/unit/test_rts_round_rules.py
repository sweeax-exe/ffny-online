"""Run the Streets round rules (build 87): the time limit and the KO target a room's host or the VS CPU page chose,
one byte on the wire (src/hooks/openworld_rules.c, compiled natively), and the wiring that makes every PC of a fight
agree on them (START's rounds byte, the checksummed Big Arena state), plus the round clock, "30 SECONDS LEFT" and
TIME! on the score plate. Needs MSVC (`cl` on PATH) for the native part."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include "openworld.h"

int main(void)
{
    int b, t, k;
    char text[48];
    for (t = 0; t < OW_RTS_TIMES; t++)
        for (k = 0; k < OW_RTS_KOS; k++) {
            int tt, kk;
            b = ow_rules_rts_encode(t, k);
            ow_rules_rts_decode(b, &tt, &kk);
            ow_rules_rts_text(b, text, (int)sizeof text);
            printf("enc|%d|%d|%d|%d|%d|%d|%d|%d|%s\n", t, k, b, ow_rules_rts_valid(b), tt, kk,
                   ow_rules_rts_limit_steps(b), ow_rules_rts_kos(kk), text);
        }
    for (b = 0; b < 256; b++) {
        ow_rules_rts_decode(b, &t, &k);
        printf("dec|%d|%d|%d|%d\n", b, ow_rules_rts_valid(b), t, k);
    }
    printf("bad|%d|%d\n", ow_rules_rts_encode(9, -1), ow_rules_rts_encode(OW_RTS_DEFAULT_TIME, OW_RTS_DEFAULT_KOS));
    printf("idx|%d|%d|%d|%d|%d|%d|%d\n", ow_rules_rts_time_index(300), ow_rules_rts_time_index(900),
           ow_rules_rts_time_index(0), ow_rules_rts_time_index(77), ow_rules_rts_ko_index(5), ow_rules_rts_ko_index(20),
           ow_rules_rts_ko_index(3));
    return 0;
}
'''


@pytest.fixture(scope='module')
def out():
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    import tempfile
    folder = Path(tempfile.mkdtemp(prefix='rtsround'))
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'rtsround.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/openworld_rules.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8').splitlines()


def rows(out, kind):
    return [line.split('|')[1:] for line in out if line.startswith(kind + '|')]


def test_every_choice_round_trips(out):
    enc = rows(out, 'enc')
    assert len(enc) == 12
    seen = set()
    for t, k, b, valid, tt, kk, steps, kos, text in enc:
        assert valid == '1' and (tt, kk) == (t, k)
        seen.add(b)
        assert int(steps) == 60 * ([300, 600, 900, 1800][int(t)])       # no time limit: the 30-minute safety end
        assert int(kos) == [5, 10, 20][int(k)]
        if t == '3':
            assert text == f'NO TIME LIMIT  -  FIRST TO {kos}'
        else:
            assert text == f'{[5, 10, 15][int(t)]} MIN  -  FIRST TO {kos}'
    assert len(seen) == 12                                              # every choice its own byte
    assert dict(((t, k), b) for t, k, b, *_ in enc)[('1', '1')] == '0'  # the defaults are 0 (rooms before build 87)


def test_anything_else_reads_as_the_defaults(out):
    dec = {int(b): (v, t, k) for b, v, t, k in rows(out, 'dec')}
    valid = {b for b, (v, _, _) in dec.items() if v == '1'}
    # 0 and the twelve set bytes (0x85, the defaults spelled out, reads as the defaults too)
    assert valid == {0} | {0x80 | t | (k << 2) for t in range(4) for k in range(3)}
    for b, (v, t, k) in dec.items():
        if v == '0' or b == 0:
            assert (t, k) == ('1', '1')
    assert rows(out, 'bad') == [['0', '0']]
    assert rows(out, 'idx') == [['0', '2', '3', '1', '0', '2', '1']]


def read(rel):
    return (ROOT / rel).read_text(encoding='utf-8')


def test_every_pc_has_the_rooms_rules():
    net = read('src/hooks/netplay.c')
    # START's rounds byte is the RTS room's rules, never the game's own match rules
    assert 'N.rts_rules = N.rts_match && ow_rules_rts_valid(m.rules) ? m.rules : 0;' in net
    assert 'if (N.rts_match) m.rules = N.report_m.rules = 0;' in net
    assert 'int netplay_rts_rules(void) { return N.rts_rules; }' in net
    ow = read('src/hooks/openworld.c')
    # the rules and the end are in the Big Arena state, which goes with every rollback save and into the checksum
    state = re.search(r'typedef struct \{(.*?)\} BigArena;', ow, re.S).group(1)
    for field in ('uint8_t rules;', 'uint8_t end_kind;', 'uint8_t unlimited;', 'uint32_t end_step;', 'uint32_t spawn_hash;'):
        assert field in state
    assert 'typedef char ow_state_fits[sizeof(BigArena) <= OW_STATE_BYTES ? 1 : -1];' in ow
    # the offline test clock never reaches an online fight
    assert re.search(r'test_secs > 0 && !ffny_online_locked\(\)', ow)


def test_time_end_holds_everyone_before_the_losers_go_down():
    ow = read('src/hooks/openworld.c')
    assert 'BA.next_loser_at = BA.step + (limit == 1 ? 30u : ARENA_TIME_HOLD);' in ow
    assert 'if (BA.ending && BA.end_kind == END_TIME) { MEMF(g_esp + 8) = 0.0f; return; }' in ow
    assert re.search(r'void ow_pad_hold\(uint32_t actor\)\s*\{\s*uint32_t[^;]*;\s*if \(!BA\.on \|\| !BA\.ending \|\| '
                     r'BA\.end_kind != END_TIME\) return;', ow)
    manual = read('src/recomp_manual.c')
    assert 'netplay_on_pad_read(MEM32(g_esp + 4)); ow_pad_hold(MEM32(g_esp + 4)); }' in manual


def test_plate_clock_notice_and_time():
    hud = read('src/hooks/fight_hud_ui.inc')
    assert '"30 SECONDS LEFT"' in hud and '"TIME!"' in hud and '"NO LIMIT"' in hud
    assert 'const int FH_NOTICE_STEPS = 30 * 60, FH_NOTICE_SHOW = 3 * 60;' in hud
    # TIME! comes before the winner
    assert hud.index('fh_callout(dl, f, 228, (float)since / 1500.0f, 92, "TIME!");') < hud.index('" WINS"')


def test_only_the_plates_clock_in_a_run_the_streets_fight():
    """Build 87: the game's own clock (top centre, counting up) is left out of a Run the Streets fight, where the
    plate's clock counts down. The wrapper returns as the function does (thiscall, no stack arguments: the return
    address only) and runs the game's own body in every other fight."""
    manual = read('src/recomp_manual.c')
    assert 'extern void sub_000B54E0_gen(void);' in manual
    body = re.search(r'void sub_000B54E0\(void\)\n\{(.*?)\n\}\n', manual, re.S).group(1)
    assert 'if (!keep && ow_rts_active()) { g_esp += 4; return; }' in body
    assert body.rstrip().endswith('sub_000B54E0_gen();')
    assert 'getenv("RECOMP_RTS_GAME_CLOCK")' in body
    # Run the Streets only: not every Big Arena fight, not the online screens
    assert 'int ow_rts_active(void) { return BA.on && BA.rts; }' in read('src/hooks/openworld.c')


def test_room_settings_and_vs_cpu():
    ui = read('src/hooks/online_screen.cpp')
    assert '"TIME LIMIT", "KO TARGET"' in ui
    assert 'case R_RTS_TIME: s.rounds = rts_rules_step(s.rounds, 0, step); break;' in ui
    assert 'case R_RTS_KOS: s.rounds = rts_rules_step(s.rounds, 1, step); break;' in ui
    assert 'if (NETPLAY_IS_RTS(r.settings.mode)) {' in ui       # the room card shows them
    rts = read('src/hooks/rts.c')
    assert '"rts_time_limit"' in rts and '"rts_ko_target"' in rts
    settings = read('src/hooks/pc_settings.c')
    assert re.search(r'\{\s*"openworld",\s*"rts_time_limit",\s*RECOMP_SETTING_INT,\s*10,\s*0,\s*15,', settings)
    assert re.search(r'\{\s*"openworld",\s*"rts_ko_target",\s*RECOMP_SETTING_INT,\s*10,\s*5,\s*20,', settings)

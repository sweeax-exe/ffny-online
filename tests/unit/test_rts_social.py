"""Run the Streets' social layer (src/hooks/rts_social_rules.c), compiled natively: proximity voice's gain and pan,
the world-to-screen projection of the chat bubbles, the edge clamp and the bubble's fade. Plus the wiring that keeps
voice as it was outside Run the Streets and keeps the layer out of the fight's input. Needs MSVC (`cl` on PATH) for
the native part."""
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include "rts_social_rules.h"

int main(void)
{
    static const float d[] = { -5.0f, 0.0f, 58.5f, 100.0f, 150.0f, 200.0f, 250.0f, 300.0f, 327.6f, 400.0f };
    float l, r, nx, ny, x, y;
    int i, k;
    RtsView v = { { 0.0f, 106.0f, 140.0f }, { 0.0f, 16.0f, 0.0f }, 60.0f, 4.0f / 3.0f };
    for (i = 0; i < (int)(sizeof d / sizeof d[0]); i++) printf("gain|%.1f|%.4f\n", d[i], rts_voice_gain(d[i]));
    /* the camera looks along -Z: +X is right */
    printf("pan|right|%.3f\n", rts_voice_pan(0, 0, 100, 0, 0, -1));
    printf("pan|left|%.3f\n", rts_voice_pan(0, 0, -100, 0, 0, -1));
    printf("pan|ahead|%.3f\n", rts_voice_pan(0, 0, 0, -100, 0, -1));
    printf("pan|near|%.3f\n", rts_voice_pan(0, 0, 8, 0, 0, -1));
    printf("pan|turned|%.3f\n", rts_voice_pan(0, 0, 100, 0, 0, 1));     /* looking along +Z: +X is left */
    printf("pan|nodir|%.3f\n", rts_voice_pan(0, 0, 100, 0, 0, 0));
    rts_voice_balance(0.0f, &l, &r);
    printf("bal|mid|%.3f|%.3f\n", l, r);
    rts_voice_balance(0.8f, &l, &r);
    printf("bal|right|%.3f|%.3f\n", l, r);
    rts_voice_balance(-0.5f, &l, &r);
    printf("bal|left|%.3f|%.3f\n", l, r);
    k = rts_project(&v, v.at, &nx, &ny);
    printf("proj|target|%d|%.3f|%.3f\n", k, nx, ny);
    { float p[3] = { 40.0f, 16.0f, 0.0f }; k = rts_project(&v, p, &nx, &ny); printf("proj|right|%d|%.3f|%.3f\n", k, nx, ny); }
    { float p[3] = { 0.0f, 60.0f, 0.0f }; k = rts_project(&v, p, &nx, &ny); printf("proj|up|%d|%.3f|%.3f\n", k, nx, ny); }
    { float p[3] = { 30.0f, 16.0f, 400.0f }; k = rts_project(&v, p, &nx, &ny); printf("proj|behind|%d|%.3f|%.3f\n", k, nx, ny); }
    { RtsView bad = v; bad.fov_deg = 0.0f; float p[3] = { 0, 0, 0 }; printf("proj|bad|%d\n", rts_project(&bad, p, &nx, &ny)); }
    x = 900; y = 240; k = rts_clamp_to_box(&x, &y, 0, 0, 640, 480, 0); printf("clamp|right|%d|%.1f|%.1f\n", k, x, y);
    x = 320; y = 200; k = rts_clamp_to_box(&x, &y, 0, 0, 640, 480, 0); printf("clamp|inside|%d|%.1f|%.1f\n", k, x, y);
    x = 320; y = 200; k = rts_clamp_to_box(&x, &y, 0, 0, 640, 480, 1); printf("clamp|behind|%d|%.1f|%.1f\n", k, x, y);
    printf("alpha|%.2f|%.2f|%.2f|%.2f|%.2f\n", rts_bubble_alpha(0), rts_bubble_alpha(75), rts_bubble_alpha(2000),
           rts_bubble_alpha(4600), rts_bubble_alpha(5000));
    return 0;
}
'''


@pytest.fixture(scope='module')
def out():
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = Path(tempfile.mkdtemp(prefix='rtssocial'))
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'rtssocial.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/rts_social_rules.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    text = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    rows = {}
    for line in text.splitlines():
        parts = line.split('|')
        rows[(parts[0], parts[1])] = parts[2:]
    return rows


def f(v):
    return float(v)


def test_gain_curve(out):
    g = {float(k[1]): f(v[0]) for k, v in out.items() if k[0] == 'gain'}
    assert g[-5.0] == 1.0 and g[0.0] == 1.0 and g[58.5] == 1.0      # up to 5 m (58.5 units): full volume
    assert g[327.6] == 0.0 and g[400.0] == 0.0                      # 28 m (327.6 units) and beyond: silent
    falling = [g[k] for k in (58.5, 100.0, 150.0, 200.0, 250.0, 300.0, 327.6)]
    assert all(a > b for a, b in zip(falling, falling[1:]))         # strictly falling in between
    assert 0.5 < g[100.0] < 0.8 and g[300.0] < 0.02                 # squared: quiet well before the end


def test_pan(out):
    assert f(out[('pan', 'right')][0]) == pytest.approx(0.8)        # never past 0.8: the far ear hears a little
    assert f(out[('pan', 'left')][0]) == pytest.approx(-0.8)
    assert f(out[('pan', 'ahead')][0]) == pytest.approx(0.0)
    assert f(out[('pan', 'near')][0]) == pytest.approx(0.0)         # within 1 m: the middle
    assert f(out[('pan', 'turned')][0]) == pytest.approx(-0.8)      # the camera turned round: the sides swap
    assert f(out[('pan', 'nodir')][0]) == pytest.approx(0.0)


def test_balance_keeps_the_middle_as_mono(out):
    assert out[('bal', 'mid')] == ['1.000', '1.000']                # the middle is today's mono voice
    assert out[('bal', 'right')] == ['0.200', '1.000']
    assert out[('bal', 'left')] == ['1.000', '0.500']


def test_projection(out):
    assert out[('proj', 'target')] == ['1', '0.000', '0.000']       # the camera's target is the picture's middle
    k, x, y = out[('proj', 'right')]
    assert k == '1' and f(x) > 0.1 and abs(f(y)) < 0.2              # +X is right of the middle
    k, x, y = out[('proj', 'up')]
    assert k == '1' and f(y) > 0.1 and abs(f(x)) < 1e-3             # higher is up
    k, x, y = out[('proj', 'behind')]
    assert k == '0' and f(x) > 0 and f(y) <= 0                      # behind: the way it lies, never up in the sky
    assert out[('proj', 'bad')] == ['-1']


def test_clamp_and_fade(out):
    assert out[('clamp', 'right')] == ['1', '640.0', '240.0']
    assert out[('clamp', 'inside')] == ['0', '320.0', '200.0']
    k, x, y = out[('clamp', 'behind')]
    assert k == '1' and (f(y) in (0.0, 480.0) or f(x) in (0.0, 640.0))
    assert out[('alpha', '0.00')] == ['0.50', '1.00', '0.50', '0.00']


def read(rel):
    return (ROOT / rel).read_text(encoding='utf-8')


def test_voice_stays_mono_outside_run_the_streets():
    voice = read('src/hooks/voice.c')
    # mono with the old format unless the room is a Run the Streets one (or the test switch)
    assert 's_stereo = rts_social_voice_stereo();' in voice
    assert 's_stereo ? &k_format_stereo : &k_format' in voice
    assert 'if (!s_stereo) {' in voice
    social = read('src/hooks/rts_social.c')
    assert 'return netplay_rts_room() || env_on("RECOMP_RTS_SOCIAL_TEST");' in social
    # outside a Run the Streets fight a talker is at full volume in the middle
    assert '*gain = 1.0f;\n    *pan = 0.0f;' in social


def test_social_layer_writes_no_game_memory():
    social = read('src/hooks/rts_social.c')
    writes = re.findall(r'MEM(?:8|16|32|F)\([^;]*?\)\s*=[^=]', social)
    assert not writes, writes
    ow = read('src/hooks/openworld.c')
    assert ow.count('rts_social_camera();') == 3        # every way out of the camera wrapper copies the camera


def test_the_dock_holds_input_through_the_fight_chat():
    ui = read('src/hooks/social_ui.inc')
    assert 'if (rts_dock_live()) return rts_dock_draw(dl, display);' in ui
    assert 'if (rts_dock_live()) { rts_dock_send(); return; }' in ui
    screen = read('src/hooks/online_screen.cpp')
    assert 'bool fight_chat_typing() { return FC.typing ||' in screen   # pc_ui.cpp's fight chat hold reads FC.typing

"""The "Button icons" option (ui.button_icons): the setting, the auto choice and the drawn PlayStation glyphs.

The setting and the choice are compiled from src/hooks/pc_ui_settings.c with the real settings registry;
the glyphs come from scripts/online-art.py, which draws them without any game file.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include "pc_ui.h"
int main(int argc, char **argv)
{
    static RecompSetting settings[16];
    size_t n = pc_ui_settings(settings, 16);
    recomp_settings_register(settings, n);
    if (argc > 1 && argv[1][0]) recomp_settings_load(argv[1]);
    printf("setting %d\n", recomp_settings_get("ui", "button_icons", -1));
    for (int i = 2; i < argc; i++)
        printf("pad %s=%d\n", argv[i], pc_ui_pad_is_playstation(argv[i]));
    /* the auto choice: setting x (nothing, keyboard, pad, pad+keyboard) x name x SDL's type */
    for (int setting = 0; setting < 3; setting++)
        for (int what = 0; what < 4; what++)
            for (int sdl = -1; sdl <= 1; sdl++) {
                const char *names[] = { "PS5 Controller", "Xbox Series X Controller" };
                for (int k = 0; k < 2; k++)
                    printf("pick %d %d %d %d=%d\n", setting, what, sdl, k,
                           pc_ui_button_icons_pick(setting, what, names[k], sdl));
            }
    return 0;
}
'''


@pytest.fixture(scope='module')
def tool(tmp_path_factory):
    if not (ROOT / 'tools/xboxrecomp/src/settings/recomp_settings.c').is_file():
        pytest.skip('needs the initialized toolkit submodule')
    compiler = shutil.which('cl') if os.name == 'nt' else shutil.which('cc')
    if not compiler:
        pytest.skip('needs a C compiler')
    folder = tmp_path_factory.mktemp('button-icons')
    driver = folder / 'driver.c'
    driver.write_text(DRIVER)
    exe = folder / ('icons.exe' if os.name == 'nt' else 'icons')
    includes = [ROOT / 'src/hooks', ROOT / 'tools/xboxrecomp/src/settings']
    sources = [driver, ROOT / 'src/hooks/pc_ui_settings.c', ROOT / 'tools/xboxrecomp/src/settings/recomp_settings.c']
    if os.name == 'nt':
        cmd = [compiler, '/nologo', '/O2', '/TC', *['/I' + str(p) for p in includes], *map(str, sources), '/Fe:' + str(exe)]
    else:
        cmd = [compiler, '-O2', *['-I' + str(p) for p in includes], *map(str, sources), '-o', str(exe), '-pthread']
    result = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


def run(tool, ini=None, env_value=None, pads=()):
    env = dict(os.environ)
    env.pop('RECOMP_BUTTON_ICONS', None)
    if env_value is not None:
        env['RECOMP_BUTTON_ICONS'] = env_value
    result = subprocess.run([str(tool), str(ini) if ini else '', *pads], env=env, capture_output=True, text=True,
                            timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    lines = result.stdout.splitlines()
    out = {'setting': int(lines[0].split()[1])}
    for line in lines[1:]:
        kind, rest = line.split(' ', 1)
        key, value = rest.rsplit('=', 1)
        out[(kind, key)] = int(value)
    return out


@pytest.mark.parametrize('file_value,env_value,want', [
    (None, None, 0),                    # auto by default
    ('xbox', None, 1),
    ('playstation', None, 2),
    ('PlayStation', None, 2),
    ('nonsense', None, 0),
    ('xbox', 'playstation', 2),         # the environment wins (tests)
    (None, 'bogus', 0),
])
def test_setting_file_and_environment(tool, tmp_path, file_value, env_value, want):
    ini = tmp_path / 'settings.ini'
    ini.write_text('' if file_value is None else '[ui]\nbutton_icons = ' + file_value + '\n')
    assert run(tool, ini, env_value)['setting'] == want


@pytest.mark.parametrize('name,ps', [
    ('DualSense Wireless Controller', 1),
    ('DualSense Edge Wireless Controller', 1),
    ('PS5 Controller', 1),
    ('PS4 Controller', 1),
    ('PS3 Controller', 1),
    ('Wireless Controller', 1),                     # a DualShock 4's own USB name
    ('Wireless Controller + keyboard', 1),
    ('Sony Interactive Entertainment Controller', 1),
    ('DUALSHOCK 4', 1),
    ('Xbox Wireless Controller', 0),
    ('Xbox Series X Controller', 0),
    ('XInput pad 1', 0),
    ('Nintendo Switch Pro Controller', 0),
    ('Steam Virtual Gamepad', 0),
    ('Keyboard and mouse', 0),
])
def test_pad_names(tool, name, ps):
    assert run(tool, pads=[name])[('pad', name)] == ps


def test_auto_choice(tool):
    out = run(tool)
    for setting in range(3):
        for what in range(4):
            for sdl in (-1, 0, 1):
                for k in (0, 1):            # 0: "PS5 Controller", 1: an Xbox pad's name
                    got = out[('pick', '%d %d %d %d' % (setting, what, sdl, k))]
                    if setting == 1:
                        want = 1
                    elif setting == 2:
                        want = 2
                    elif not what & 1:      # nothing, or the keyboard alone: Xbox
                        want = 1
                    else:                   # a pad: PlayStation by its name or SDL's type
                        want = 2 if (k == 0 or sdl == 1) else 1
                    assert got == want, (setting, what, sdl, k)


def load_online_art():
    sys.path.insert(0, str(ROOT / 'scripts'))
    spec = importlib.util.spec_from_file_location('online_art', ROOT / 'scripts/online-art.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def test_drawn_playstation_glyphs(tmp_path):
    pytest.importorskip('PIL')
    art = load_online_art()
    assert art.VERSION >= 5        # installs made before the glyphs see their art as old
    names = art.write_ps_glyphs(tmp_path)
    assert names == ['ps_cross', 'ps_circle', 'ps_square', 'ps_triangle']
    from PIL import Image
    colours = {}
    for name in names:
        image = Image.open(tmp_path / (name + '.png'))
        assert image.size == (32, 32) and image.mode == 'RGBA'
        alpha = image.getchannel('A')
        # the round badge at the top left, where the dump's Xbox glyphs have theirs (about 21 px)
        x0, y0, x1, y1 = alpha.getbbox()
        assert x0 == 0 and y0 == 0 and 20 <= x1 <= 22 and 20 <= y1 <= 22
        assert alpha.getpixel((31, 31)) == 0 and alpha.getpixel((0, 0)) == 0 and alpha.getpixel((10, 10)) == 255
        # the symbol in its colour on a dark disc
        rgb = image.convert('RGB')
        bright = [rgb.getpixel((x, y)) for x in range(4, 18) for y in range(4, 18) if max(rgb.getpixel((x, y))) > 160]
        assert bright, name
        colours[name] = max(bright, key=max)
    assert colours['ps_cross'][2] > colours['ps_cross'][0]           # blue
    assert colours['ps_circle'][0] > colours['ps_circle'][1]         # red
    assert colours['ps_triangle'][1] > colours['ps_triangle'][0]     # green
    assert colours['ps_square'][0] > colours['ps_square'][1]         # pink

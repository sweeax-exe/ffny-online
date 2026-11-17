"""Build 90, "Turn all mods off" / "Turn mods back on" (src/hooks/mods_master_logic.h, the Content page's master button).

One press saves what was on and turns every mod off (community content in offline play, the player's own packs, HD
textures, the community graphics pack, a language pack); the next press restores exactly the saved state, not
"everything on". Native driver (MSVC); the wiring is checked in the sources.
"""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src/hooks'

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "mods_master_logic.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static ModsState make(int c, int t, int g, const char *lp, const char *off)
{
    ModsState s; memset(&s, 0, sizeof s);
    s.community = c; s.textures = t; s.graphics = g;
    snprintf(s.language_pack, sizeof s.language_pack, "%s", lp);
    snprintf(s.content_off, sizeof s.content_off, "%s", off);
    return s;
}
int main(void)
{
    const char *own = "my-venue,my-fighter,my-textures";
    /* a mixed state: community on, textures off, graphics on, a language mod, one own pack off, one community retexture off */
    ModsState before = make(1, 0, 1, "deutsch", "hd-retex,my-venue"), off, back;
    CHECK(!mods_is_all_off(&before, own));
    CHECK(mods_all_off(&before, own, &off));
    CHECK(!off.community && !off.textures && !off.graphics && !off.language_pack[0]);
    CHECK(mods_is_all_off(&off, own));
    /* the community retexture stays as it was, my-venue is not listed twice, the other own packs are added */
    CHECK(!strcmp(off.content_off, "hd-retex,my-venue,my-fighter,my-textures"));
    /* save and restore: exactly the state before, not everything on */
    char text[MODS_TEXT];
    CHECK(mods_backup_encode(&before, text, sizeof text));
    CHECK(!strcmp(text, "v1|c=1|t=0|g=1|lp=deutsch|lo=hd-retex,my-venue"));
    CHECK(mods_backup_decode(text, &back));
    CHECK(back.community == 1 && back.textures == 0 && back.graphics == 1);
    CHECK(!strcmp(back.language_pack, "deutsch") && !strcmp(back.content_off, "hd-retex,my-venue"));
    /* a second press of "all off" on an all-off state changes nothing; restoring still gives the first state */
    ModsState again;
    CHECK(mods_all_off(&off, own, &again) && !strcmp(again.content_off, off.content_off));
    /* nothing on to begin with: the saved state is all off too, and back on gives the same */
    ModsState none = make(0, 0, 0, "", "my-venue,my-fighter,my-textures");
    CHECK(mods_is_all_off(&none, own));
    CHECK(mods_backup_encode(&none, text, sizeof text) && mods_backup_decode(text, &back));
    CHECK(!back.community && !back.textures && !back.graphics && !back.language_pack[0]);
    /* no own packs at all */
    ModsState plain = make(1, 1, 0, "", "");
    CHECK(!mods_is_all_off(&plain, ""));
    CHECK(mods_all_off(&plain, "", &off) && mods_is_all_off(&off, "") && !off.content_off[0]);
    /* an own pack installed after "all off" is not off: the state says so */
    CHECK(!mods_is_all_off(&off, "new-pack"));
    /* bad backups are refused, not half-read */
    CHECK(!mods_backup_decode("", &back));
    CHECK(!mods_backup_decode("v2|c=1|t=1|g=1|lp=|lo=", &back));
    CHECK(!mods_backup_decode("v1|c=1|t=1", &back));
    CHECK(mods_backup_decode("v1|c=0|t=1|g=0|lp=|lo=", &back) && !back.community && back.textures);
    /* a list that does not fit is refused (the caller leaves the settings alone) */
    ModsState big = make(1, 1, 1, "", "");
    memset(big.content_off, 'a', sizeof big.content_off - 1);
    CHECK(!mods_all_off(&big, "extra-pack", &off));
    printf(fails ? "FAILED %d\n" : "OK\n", fails);
    return fails != 0;
}
'''


@pytest.fixture(scope='module')
def driver(tmp_path_factory):
    compiler = shutil.which('cl')
    if sys.platform != 'win32' or not compiler:
        pytest.skip('the native fixture needs MSVC on Windows')
    out = tmp_path_factory.mktemp('mods_master')
    (out / 'driver.c').write_text(DRIVER, encoding='utf-8')
    built = subprocess.run([compiler, '/nologo', '/W4', '/WX', '/TC', '/I' + str(HOOKS), str(out / 'driver.c'),
                            '/Fe:' + str(out / 'driver.exe')], cwd=out, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return out / 'driver.exe'


def test_save_and_restore_the_previous_state(driver):
    r = subprocess.run([str(driver)], capture_output=True, text=True)
    assert r.returncode == 0 and r.stdout.strip() == 'OK', r.stdout


def src(name):
    return (ROOT / name).read_text(encoding='utf-8')


def test_button_is_wired_to_the_settings():
    c = src('src/hooks/pc_launcher_content.inc')
    assert '##content_mods_master' in c and 'mods_master_press(' in c
    for pair in ('"content", "community_offline"', '"textures", "enabled"', '"graphics", "enabled"',
                 '"language", "pack"', '"content", "off"', '"content", "mods_backup"'):
        assert pair in c
    assert 'Turn all mods off' in c and 'Turn mods back on' in c and 'Mods: OFF' in c
    # the own packs' files follow (compose), as with a pack's own switch
    assert c.index('mods_master_card(ld') < c.index('pcui::mod_start("compose")', c.index('mods_master_card(ld'))
    assert 'ADD("content", "mods_backup"' in src('src/hooks/pc_language_settings.c')

"""Build 90, "Turn all mods off" / "Turn mods back on" (src/hooks/mods_master_logic.h, the Content page's master button).

One press saves what was on and turns every mod off (community content in offline play, the player's own packs, HD
textures, the community graphics pack, a language pack); the next press restores exactly the saved state, not
"everything on". Build 94: the own packs that are off and the backup are files, not 1023-character settings (hundreds
of own packs), and "back on" without a usable backup turns the community content and every own pack on.
Native driver (MSVC); the wiring is checked in the sources.
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
static ModsState *make(int c, int t, int g, const char *lp, const char *off, const char *own)
{
    ModsState *s = (ModsState *)calloc(1, sizeof *s);
    s->community = c; s->textures = t; s->graphics = g;
    snprintf(s->language_pack, sizeof s->language_pack, "%s", lp);
    snprintf(s->content_off, sizeof s->content_off, "%s", off);
    snprintf(s->own_off, sizeof s->own_off, "%s", own);
    return s;
}
int main(void)
{
    const char *own = "my-venue,my-fighter,my-textures";
    ModsState *off = (ModsState *)calloc(1, sizeof *off), *back = (ModsState *)calloc(1, sizeof *back);
    ModsState *again = (ModsState *)calloc(1, sizeof *again), *on = (ModsState *)calloc(1, sizeof *on);
    char *text = (char *)malloc(MODS_BACKUP_TEXT);
    /* a mixed state: community on, textures off, graphics on, a language mod, one own pack off, one community retexture off */
    ModsState *before = make(1, 0, 1, "deutsch", "hd-retex", "my-venue");
    CHECK(!mods_is_all_off(before, own));
    CHECK(mods_all_off(before, own, off));
    CHECK(!off->community && !off->textures && !off->graphics && !off->language_pack[0]);
    CHECK(mods_is_all_off(off, own));
    /* the community retexture stays as it was, my-venue is not listed twice, the other own packs are added */
    CHECK(!strcmp(off->content_off, "hd-retex"));
    CHECK(!strcmp(off->own_off, "my-venue,my-fighter,my-textures"));
    /* save and restore: exactly the state before, not everything on */
    CHECK(mods_backup_encode(before, text, MODS_BACKUP_TEXT));
    CHECK(!strcmp(text, "v2|c=1|t=0|g=1|lp=deutsch|lo=hd-retex|po=my-venue"));
    CHECK(mods_backup_decode(text, back));
    CHECK(back->community == 1 && back->textures == 0 && back->graphics == 1);
    CHECK(!strcmp(back->language_pack, "deutsch") && !strcmp(back->content_off, "hd-retex") && !strcmp(back->own_off, "my-venue"));
    /* a second press of "all off" on an all-off state changes nothing */
    CHECK(mods_all_off(off, own, again) && !strcmp(again->own_off, off->own_off));
    /* nothing on to begin with: the saved state is all off too */
    ModsState *none = make(0, 0, 0, "", "", own);
    CHECK(mods_is_all_off(none, own));
    CHECK(mods_backup_encode(none, text, MODS_BACKUP_TEXT) && mods_backup_decode(text, back));
    CHECK(!back->community && !back->textures && !back->graphics && !back->language_pack[0]);
    /* no own packs at all */
    ModsState *plain = make(1, 1, 0, "", "", "");
    CHECK(!mods_is_all_off(plain, ""));
    CHECK(mods_all_off(plain, "", off) && mods_is_all_off(off, "") && !off->own_off[0]);
    /* an own pack installed after "all off" is not off: the state says so */
    CHECK(!mods_is_all_off(off, "new-pack"));
    /* bad backups are refused, not half-read */
    CHECK(!mods_backup_decode("", back));
    CHECK(!mods_backup_decode("v3|c=1|t=1|g=1|lp=|lo=|po=", back));
    CHECK(!mods_backup_decode("v1|c=1|t=1", back));
    CHECK(!mods_backup_decode("v2|c=1|t=1|g=1|lp=|lo=", back));         /* a v2 text cut before po= */
    CHECK(mods_backup_decode("v1|c=0|t=1|g=0|lp=|lo=", back) && !back->community && back->textures);
    /* a v1 backup (builds 90 to 93) has the own packs inside lo: normalising moves them */
    CHECK(mods_backup_decode("v1|c=1|t=0|g=0|lp=|lo=hd-retex,my-venue", back));
    CHECK(mods_normalise(back, own));
    CHECK(!strcmp(back->content_off, "hd-retex") && !strcmp(back->own_off, "my-venue"));
    /* "back on" with no backup: the community content and every own pack on, textures/graphics/language as they are */
    ModsState *offs = make(0, 0, 1, "", "hd-retex", own);
    mods_back_on_default(offs, own, on);
    CHECK(on->community && !on->textures && on->graphics && !on->own_off[0] && !strcmp(on->content_off, "hd-retex"));
    CHECK(!mods_is_all_off(on, own));
    {   /* an own pack that was also listed in the old settings list comes on too */
        ModsState *legacy = make(0, 0, 0, "", "hd-retex,my-fighter", "my-venue");
        mods_back_on_default(legacy, own, on);
        CHECK(!strcmp(on->content_off, "hd-retex") && !on->own_off[0]);
        free(legacy);
    }
    {   /* counts for the card */
        int n_on, n_total;
        ModsState *some = make(1, 0, 0, "", "", "my-venue,gone-pack");
        mods_own_count(some, own, &n_on, &n_total);
        CHECK(n_total == 3 && n_on == 2);
        free(some);
    }
    {   /* 300 own packs: far past what a setting holds (1023 characters); all off, saved, restored, all on */
        static char ids[MODS_OWN_TEXT], half[MODS_OWN_TEXT];
        char one[64];
        int i, n_on, n_total;
        size_t io = 0, ho = 0;
        for (i = 0; i < 300; i++) {
            int k = snprintf(one, sizeof one, "own-fighter-pack-%03d", i);
            io += (size_t)snprintf(ids + io, sizeof ids - io, "%s%s", io ? "," : "", one);
            if (i % 3 == 0) ho += (size_t)snprintf(half + ho, sizeof half - ho, "%s%s", ho ? "," : "", one);
            (void)k;
        }
        CHECK(strlen(ids) > 6000);
        ModsState *big = make(1, 1, 1, "fr", "hd-retex", half);
        CHECK(mods_all_off(big, ids, off));
        CHECK(mods_is_all_off(off, ids));
        mods_own_count(off, ids, &n_on, &n_total);
        CHECK(n_total == 300 && n_on == 0);
        CHECK(strlen(off->own_off) == strlen(ids));
        CHECK(mods_backup_encode(big, text, MODS_BACKUP_TEXT) && mods_backup_decode(text, back));
        CHECK(back->community && back->textures && back->graphics && !strcmp(back->own_off, half));
        mods_own_count(back, ids, &n_on, &n_total);
        CHECK(n_on == 200);                         /* the packs that were off stay off, the others come back */
        mods_back_on_default(off, ids, on);
        mods_own_count(on, ids, &n_on, &n_total);
        CHECK(n_on == 300 && on->community && !on->language_pack[0]);
        free(big);
    }
    /* a list that does not fit is refused (the caller leaves the settings alone) */
    ModsState *big = make(1, 1, 1, "", "", "");
    memset(big->own_off, 'a', sizeof big->own_off - 1);
    CHECK(!mods_all_off(big, "extra-pack", off));
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
    # build 94: the own packs' switches and the backup are files; the button says what it does
    assert 'pc_content_own_off_put(' in c and 'MODS_BACKUP_FILE' in c and 'mods_back_on_default(' in c
    assert 'const bool back = c.saved || c.all_off;' in c
    assert 'PC_CONTENT_MAX 512' in src('src/hooks/pc_content.h')
    assert 'ADD("content", "mods_backup"' in src('src/hooks/pc_language_settings.c')

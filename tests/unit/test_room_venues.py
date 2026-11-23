"""Build 110: CUSTOM venues in the room settings. src/hooks/netplay_room_venues.c compiled natively (MSVC; skips without
`cl`): the remembered list's text, the checklist's toggles; the wiring in the online screens, the settings and netplay.c."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / 'src' / 'hooks'

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "netplay_room_venues.h"

static void show(const char *what, const unsigned char *ids, int n)
{
    int i;
    printf("%s|", what);
    for (i = 0; i < n; i++) printf("%s%d", i ? "," : "", ids[i]);
    printf("\n");
}

int main(void)
{
    static const char *const texts[] = { "", "1,6,11", " 1, 6 ;11,", "6,6,1,6", "0,256,1000,99999999999,255,-4,x7" };
    static const int order[] = { 1, 2, 4, 6, 11, 14, 21, 28 }, team[] = { 1, 2, 4, 6, 11, 14, 21 };
    unsigned char ids[256], pool[NETROOM_VENUES_MAX];
    int many[40], i, n, r;
    char text[NETROOM_VENUES_SAVED_TEXT], tiny[6];
    for (i = 0; i < 5; i++) {
        n = netroom_venues_parse(texts[i], ids, NETROOM_VENUES_SAVED);
        show("parse", ids, n);
    }
    show("parse-null", ids, netroom_venues_parse(NULL, ids, 8));
    show("parse-max", ids, netroom_venues_parse("1,2,3,4,5", ids, 3));
    for (i = 0; i < 63; i++) ids[i] = (unsigned char)(i + 1);      /* every venue there is: the text and back */
    n = netroom_venues_format(ids, 63, text, sizeof(text));
    memset(ids, 0, sizeof(ids));
    r = netroom_venues_parse(text, ids, NETROOM_VENUES_SAVED);
    printf("round|%d|%d|%d\n", n == (int)strlen(text), r, ids[62]);
    n = netroom_venues_format((const unsigned char *)"\x01\x0b\x15", 3, tiny, sizeof(tiny));   /* only whole ids */
    printf("short|%d|%s\n", n, tiny);
    n = netroom_venues_format(NULL, 0, text, sizeof(text));
    printf("none|%d|%s|\n", n, text);

    n = netroom_venues_parse("28,21,4,99", pool, NETROOM_VENUES_MAX);
    show("fit", ids, netroom_venues_fit(pool, n, order, 8, ids, NETROOM_VENUES_MAX));
    show("fit-team", pool, netroom_venues_fit(pool, n, team, 7, pool, NETROOM_VENUES_MAX));   /* in place */

    n = netroom_venues_parse("4,14,21", pool, NETROOM_VENUES_MAX);
    r = netroom_venues_toggle(pool, &n, 6, order, 8, NETROOM_VENUES_MAX);
    printf("toggle|%d\n", r);
    show("pool", pool, n);
    r = netroom_venues_toggle(pool, &n, 14, order, 8, NETROOM_VENUES_MAX);
    printf("toggle|%d\n", r);
    show("pool", pool, n);
    r = netroom_venues_toggle(pool, &n, 99, order, 8, NETROOM_VENUES_MAX);      /* not this room's */
    printf("toggle|%d\n", r);
    r = netroom_venues_toggle(pool, &n, 1, order, 8, 3);                         /* full */
    printf("toggle|%d\n", r);
    r = netroom_venues_toggle(pool, &n, 21, order, 8, 3);                        /* off while full */
    printf("toggle|%d\n", r);
    show("pool", pool, n);
    for (i = 0; i < 40; i++) many[i] = i + 1;
    n = netroom_venues_all(pool, many, 40, NETROOM_VENUES_MAX);
    printf("all|%d|%d|%d\n", n, pool[0], pool[31]);
    printf("has|%d|%d\n", netroom_venues_has(pool, n, 32), netroom_venues_has(pool, n, 33));

    /* a Team room's checklist keeps the remembered venues it does not show (28), and takes its own ticks */
    n = netroom_venues_parse("28,4,14", ids, NETROOM_VENUES_SAVED);
    show("merge", ids, netroom_venues_merge(ids, n, (const unsigned char *)"\x06\x15", 2, team, 7, NETROOM_VENUES_SAVED));
    return 0;
}
'''


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('room_venues')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'room_venues.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/utf-8', '/I' + str(HOOKS),
                            str(folder / 'driver.c'), str(HOOKS / 'netplay_room_venues.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    return [line.split('|') for line in out.splitlines()]


def by(rows, kind):
    return [r[1:] for r in rows if r[0] == kind]


def _src(rel):
    return (ROOT / rel).read_text(encoding='utf-8')


def test_text(rows):
    assert [r[0] for r in by(rows, 'parse')] == ['', '1,6,11', '1,6,11', '6,1', '255,4,7']
    assert by(rows, 'parse-null') == [['']] and by(rows, 'parse-max') == [['1,2,3']]
    assert by(rows, 'round') == [['1', '63', '63']]
    assert by(rows, 'short') == [['4', '1,11']]
    assert by(rows, 'none') == [['0', '', '']]


def test_fit_keeps_the_rooms_venues_in_the_lists_order(rows):
    assert by(rows, 'fit') == [['4,21,28']]
    assert by(rows, 'fit-team') == [['4,21']]


def test_toggles(rows):
    assert [r[0] for r in by(rows, 'toggle')] == ['1', '0', '-2', '-1', '0']
    assert [r[0] for r in by(rows, 'pool')] == ['4,6,14,21', '4,6,21', '4,6']
    assert by(rows, 'all') == [['32', '1', '32']]
    assert by(rows, 'has') == [['1', '0']]


def test_merge_keeps_what_the_room_cannot_have(rows):
    assert by(rows, 'merge') == [['28,6,21']]


def test_wiring():
    ui = _src('src/hooks/online_screen.cpp')
    # only CREATE A ROOM (and its test) starts on the remembered list: Faction War's rooms come from default_settings
    body = ui[ui.index('void default_settings(NetRoomSettings &s)'):ui.index('void create_page_settings')]
    assert 'room_venues' not in body and 'apply_saved_custom' not in body
    assert ui.count('create_page_settings(') == 3
    assert 'create_page_settings' not in _src('src/hooks/faction_ui.inc') + _src('src/hooks/social_ui.inc')
    # the checklist's B closes the checklist only
    assert 'if (S.venue_pick) { draw_venue_pick(dl, f, pad, s); return; }' in ui
    for page in ('create-custom', 'create-venues', 'room-custom'):
        assert '"%s"' % page in ui
    settings = _src('src/hooks/pc_settings.c')
    assert '"room_venues", RECOMP_SETTING_STRING' in settings and '"room_venues_on", RECOMP_SETTING_BOOL' in settings
    # the list is the Settings block's pool, on the wire since build 13: the same 32 on both sides
    assert 'unsigned char pool[32];' in _src('src/hooks/netplay.h')
    assert '#define NETROOM_VENUES_MAX  32' in _src('src/hooks/netplay_room_venues.h')
    assert 'RECOMP_TEST_CREATE_ROOM' in _src('src/hooks/netplay.c')

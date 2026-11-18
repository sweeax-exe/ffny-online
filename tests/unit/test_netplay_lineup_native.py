"""Build 95: the fighters an online match may load (src/hooks/netplay_lineup_rules.c), compiled natively, and where
the game uses them (the fighter-255 crash of builds 93/94, community/crash-hoodiebatman.md).

Builds 93/94 saved a community fighter's number (1000 and up) in [online] fighter, which held 0..255: 1008 became
255, the room screen's READY sent it, the relay played it (its "no fighter") and START carried 255 to every PC of the
match, which crashed loading a fighter without a database row. Needs MSVC (`cl` on PATH) for the native part."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / "src" / "hooks"

DRIVER = r'''
#include <stdio.h>
#include "netplay_lineup_rules.h"

static int stock(int id) { return id == 0 || id == 22 || id == 30 || id == 55 || id == 64; }
static int place(int id) { return id == 100 || id == 5; }
static int have(int id) { return id == 100 || id == 1000; }   /* this PC: place 100 and number 1000 */

int main(void)
{
    int ids[] = { -1, 0, 30, 39, 99, 100, 5, 254, 255, 256, 999, 1000, 1008, 999999 }, i;
    for (i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); i++)
        printf("ready|%d|%d\n", ids[i], netlineup_ready_fighter(ids[i]));
    for (i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); i++)
        printf("kind|%d|%d\n", ids[i], netlineup_kind(ids[i], stock, place, have));
    {
        int f[4] = { 64, 255, 1008, 5 }, fixed = -1;
        unsigned m = netlineup_mask(f, 4, stock, place, have, &fixed);
        printf("mask|%u|%d|%d %d %d %d\n", m, fixed, f[0], f[1], f[2], f[3]);
    }
    {   /* the other PC lacks number 1000 (position 0); this one place 5 (position 1) */
        int f[3] = { 1000, 5, 1008 }, t[3] = { 30, 55, 255 };
        int n = netlineup_agree(f, t, 3, 1u | 2u | 4u, stock);
        printf("agree|%d|%d %d %d\n", n, f[0], f[1], f[2]);
    }
    {
        int f[2] = { 1000, 22 }, t[2] = { 30, 22 };
        int n = netlineup_agree(f, t, 2, 0u, stock);
        printf("agree0|%d|%d %d\n", n, f[0], f[1]);
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def rows(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('lineup')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    exe = folder / 'lineup.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/W4', '/WX', '/TC', '/utf-8', '/I' + str(HOOKS),
                            str(folder / 'driver.c'), str(HOOKS / 'netplay_lineup_rules.c'), '/Fe:' + str(exe)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    out = subprocess.run([str(exe)], capture_output=True, check=True).stdout.decode('utf-8')
    return {tuple(r.split('|')[:2]): r.split('|')[2:] for r in out.splitlines()}


def test_ready_never_sends_255(rows):
    for fid in (-1, 255, 256, 999, 1000, 1008, 999999):
        assert rows[('ready', str(fid))] == ['55'], fid    # numbers never get here (READY's own path)
    for fid in (0, 30, 99, 100, 254):
        assert rows[('ready', str(fid))] == [str(fid)], fid


def test_kinds(rows):
    none, stock, here, missing = '0', '1', '2', '3'
    want = {-1: none, 0: stock, 30: stock, 39: none, 99: stock, 100: here, 5: missing, 254: none, 255: none,
            256: none, 999: none, 1000: here, 1008: missing, 999999: none}
    for fid, kind in want.items():
        assert rows[('kind', str(fid))] == [kind], fid


def test_mask_and_doc_for_no_row(rows):
    # 255 has no row anywhere: Doc at once (fixed 1); 1008 and place 5 are missing here: bits 2 and 3
    assert rows[('mask', str(4 | 8))] == ['1', '64 55 1008 5']


def test_agree(rows):
    # a missing number plays its template, a missing place Doc, a number whose template has no row Doc
    assert rows[('agree', '3')] == ['30 55 55']
    assert rows[('agree0', '0')] == ['1000 22']


SRC = (HOOKS / "netplay.c").read_text(encoding="utf-8")
SETTINGS = (HOOKS / "pc_settings.c").read_text(encoding="utf-8")
SCREEN = (HOOKS / "online_screen.cpp").read_text(encoding="utf-8")
AUTOMATCH = (HOOKS / "automatch.c").read_text(encoding="utf-8")


def _body(source, head):
    """The text of the C function whose definition starts with ``head`` up to its closing brace at column 0."""
    m = re.search(re.escape(head) + r"[^;{]*\)\n\{", source)     # the definition, not a prototype or a stub
    assert m, head
    return source[m.start():source.index("\n}\n", m.start())]


def test_the_setting_holds_fighter_numbers():
    m = re.search(r'\{ "online", "fighter", RECOMP_SETTING_INT, 55, 0, (\d+),', SETTINGS)
    assert m and int(m.group(1)) >= 999998          # every fighter number (1000 and up) stays as picked


def test_saved_fighter_is_one_this_pc_can_load():
    body = SCREEN[SCREEN.index("static int saved_fighter()"):]
    body = body[:body.index("\n}\n")]
    assert "netplay_fighter_loadable(id)" in body and "return 55;" in body


def test_ready_logs_and_never_ff():
    body = _body(SRC, "void netplay_lobby_ready_skin(")
    assert "netlineup_ready_fighter(fighter) != fighter" in body
    assert '"[NETPLAY] READY: ready %d, fighter byte %d, skin %d, number %d (%d bytes)\\n"' in body
    assert body.index("netlineup_ready_fighter") < body.index("b[1] = (uint8_t)fighter;")


def test_start_lineup_and_p_rtt_mask():
    start = _body(SRC, "static void on_start_as(")
    assert "templ[i] = fighters[i];" in start and "t = templ[j]; templ[j] = templ[j - 1];" in start
    assert start.index("netlineup_mask(fighters, count") < start.index("m.fighter[i] = fighters[i];")
    rtt = SRC[SRC.index("static void send_rtt(void)"):]
    rtt = rtt[:rtt.index("\n}\n")]
    assert "p[18] = (uint8_t)N.lineup_mine;" in rtt and "send_data(p, 19);" in rtt
    assert "N.peer_lineup[from] = len >= 19 ? p[18] : 0;" in SRC
    seq = AUTOMATCH[AUTOMATCH.index("static void run_sequence(void)"):]
    assert seq.index("netplay_sync_point();") < seq.index("netplay_lineup_agree(&g_req);") < \
        seq.index('call("Controller.ControllerSetup(%d,%d,%d,%d)"')

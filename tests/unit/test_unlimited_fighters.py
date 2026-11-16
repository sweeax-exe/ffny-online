"""Community fighters beyond the places (phase 1, offline): the four bank rows and the unindexed records the
composer writes (setup/ffnycontent/unlimited.py, content_compose.finish_logical), and the game's pure side
(src/hooks/fighter_banks.c: the side file check, the set-up command, the answers' fields and the picture names),
compiled natively. The composition tests need the user's own dump ($DEFJAM_DATA/extracted or game/)."""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "setup"))

import content_compose as cc  # noqa: E402
from ffnycontent import newfighter as nf, unlimited  # noqa: E402


def _game():
    for base in (os.environ.get("DEFJAM_DATA"),):
        if base and (Path(base) / "extracted" / "assets" / "djv2.sod").is_file():
            return Path(base) / "extracted"
    if (ROOT / "game" / "assets" / "djv2.sod").is_file():
        return ROOT / "game"
    return None


GAME = _game()
needs_dump = pytest.mark.skipif(GAME is None, reason="needs the user's own dump")


def test_logical_ids():
    assert nf.is_logical(1000) and nf.is_logical(123456)
    assert not nf.is_logical(999) and not nf.is_logical(127) and not nf.is_logical(nf.MORE_ID)
    assert not nf.is_logical(True) and not nf.is_logical("1000")
    assert unlimited.BANKS == (124, 125, 126, 127)
    assert list(nf.PLACE_ORDER[-4:]) == list(unlimited.BANKS)     # the banks are the places given out last


def _sod():
    return (GAME / "assets" / "djv2.sod").read_bytes()


def _stock(sod):
    return [r for r in nf.characters(sod) if r["main"] == 1 and r["fe_id"] < 100 and r["fe_id"] not in nf.OUTFIT_FE_IDS
            and not nf.is_place(r["fe_id"])]


@needs_dump
def test_records_are_unindexed_and_banks_are_four_rows():
    sod = _sod()
    stock = _stock(sod)
    rows_before = len(nf.characters(sod))
    fighters = [{"id": 1000 + i, "template_fe_id": stock[i % len(stock)]["fe_id"], "name": "Syn %d" % (1000 + i),
                 "model": "V2IP_7%02dA" % i} for i in range(130)]
    fighters[3]["edits"] = nf.gameplay_words({"version": 1, "stats": {"upper": 15, "lower": 15, "speed": 12,
                                              "toughness": 6, "health": 6, "grapple": 0, "charisma": 6},
                                              "blazins": [5, 6, 7, 8]})
    out, side = unlimited.add_records(sod, fighters)
    rows = nf.characters(out)
    # the game's table: the stock rows and the four banks only (the roster list, the unlock set, the select list)
    assert len(rows) == rows_before + 4
    assert {r["fe_id"] for r in rows if r["fe_id"] >= 100} == set(unlimited.BANKS)
    assert all(r["name"].startswith("FFNYBank") and len(r["name"]) < 20 for r in rows if r["fe_id"] in unlimited.BANKS)
    # every stock row keeps its place and words
    before = {r["fe_id"]: (r["index"], r["words"]) for r in nf.characters(sod)}
    assert all(before[r["fe_id"]] == (r["index"], r["words"]) for r in rows if r["fe_id"] in before)
    info = unlimited.read_side(side, out)
    assert [f["id"] for f in info["fighters"]] == list(range(1000, 1130))
    from ffnycontent.sod import Sod
    db = Sod(out)
    for f, spec in zip(info["fighters"], fighters):
        w = struct.unpack_from("<26I", out, f["offset"])
        assert w[nf.W_FE_ID] == f["id"] and db.string(w[nf.W_DISPLAY]) == spec["name"]
        assert db.string(w[nf.W_MODEL]) == spec["model"] and len(db.string(w[nf.W_NAME])) < 20
        assert f["template_fe_id"] == spec["template_fe_id"]
    w3 = struct.unpack_from("<26I", out, info["fighters"][3]["offset"])
    assert (w3[nf.W_BLAZINS], w3[nf.W_GAMEPLAY]) != (0, 0)
    # the same inputs give the same bytes (every PC composes the same database)
    assert unlimited.add_records(sod, fighters) == (out, side)
    with pytest.raises(nf.FighterError):
        unlimited.read_side(side, out[:-4] + b"\0\0\0\1")


@needs_dump
def test_a_place_fighter_on_a_bank_refuses_the_banks():
    sod = _sod()
    sod, _, _ = nf.append_character(sod, 55, 124, "Doc X", "Doc X")
    with pytest.raises(nf.FighterError, match="124"):
        unlimited.add_records(sod, [{"id": 1000, "template_fe_id": 55, "name": "Syn"}])
    with pytest.raises(nf.FighterError):
        unlimited.add_records(_sod(), [{"id": nf.MORE_ID, "template_fe_id": 55, "name": "Syn"}])


@needs_dump
def test_composer_writes_banks_records_and_pictures(tmp_path):
    synth = ROOT / "scripts" / "unlimited-synth.py"
    sys.path.insert(0, str(ROOT / "scripts"))
    import importlib.util
    spec = importlib.util.spec_from_file_location("unlimited_synth", synth)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    written = mod.build(GAME, tmp_path, 5, log=lambda *_: None)
    assert unlimited.SIDE_FILE in written and nf.SOD_PATH in written
    sod = (tmp_path / "assets" / "djv2.sod").read_bytes()
    info = unlimited.read_side((tmp_path / "assets" / "ffny_fighters.bin").read_bytes(), sod)
    assert [f["name"] for f in info["fighters"]] == ["Syn %d" % i for i in range(1000, 1005)]
    from ffnycontent import viv
    screens = viv.Archive(str(tmp_path / "screens" / "screens.viv"))
    names = {n.lower() for n in screens.names()}
    assert "screens/thumbs/thumb1004.big" in names and "screens/portrait/p_cf/p_cf_1005_2p.big" in names
    models = viv.Archive(str(tmp_path / "assets" / "v2ip.viv"))
    assert models.has(mod.model_name(1) + ".o")


DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fighter_banks.h"
static unsigned char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); unsigned char *d; long s;
    if (!f) return NULL; fseek(f, 0, SEEK_END); s = ftell(f); fseek(f, 0, SEEK_SET);
    d = malloc(s); *n = fread(d, 1, s, f); fclose(f); return d;
}
int main(int argc, char **argv) {
    char out[256], why[160];
    if (!strcmp(argv[1], "parse")) {
        size_t a, b; unsigned char *side = slurp(argv[2], &a), *sod = slurp(argv[3], &b);
        FighterBanksEntry *e; int n; uint32_t off[4];
        if (!fighter_banks_parse(side, a, sod, b, &e, &n, off, why, sizeof why)) { printf("refused %s\n", why); return 0; }
        printf("ok %d %d %s %d %08X\n", n, e[n - 1].id, e[n - 1].name, e[0].template_fe_id, off[3]);
        return 0;
    }
    if (!strcmp(argv[1], "setup")) {
        int slot, id, r = fighter_banks_rewrite_setup(argv[2], atoi(argv[3]), out, sizeof out, &slot, &id);
        printf("%d %d %d %s\n", r, slot, id, r ? out : "-");
        return 0;
    }
    if (!strcmp(argv[1], "fields")) {
        static const char *keys[] = { "0", "1", "2", "3", "fid" };
        int from[2] = { 124, 125 }, to[2] = { 1007, 1003 };
        int r = fighter_banks_rewrite_fields(argv[2], keys, 5, from, to, 2, out, sizeof out);
        printf("%d %s\n", r, r ? out : "-");
        return 0;
    }
    if (!strcmp(argv[1], "picture")) {
        int map[4] = { 1007, -1, 1003, -1 };
        int r = fighter_banks_picture_name(argv[2], map, out, sizeof out);
        printf("%d %s\n", r, r ? out : "-");
        return 0;
    }
    return 1;
}
'''


@pytest.fixture(scope="module")
def banks_exe(tmp_path_factory):
    compiler = shutil.which("cl")
    if not compiler:
        pytest.skip("needs MSVC (cl on PATH)")
    folder = tmp_path_factory.mktemp("banks")
    (folder / "driver.c").write_text(DRIVER)
    exe = folder / "fb.exe"
    built = subprocess.run([compiler, "/nologo", "/O2", "/TC", "/I" + str(ROOT / "src/hooks"), str(folder / "driver.c"),
                            str(ROOT / "src/hooks/fighter_banks.c"), "/Fe:" + str(exe)], cwd=folder,
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr

    def run(*args):
        return subprocess.run([str(exe)] + [str(a) for a in args], capture_output=True, text=True).stdout.strip()
    return run


def test_c_set_up_command(banks_exe):
    assert banks_exe("setup", "Controller.ControllerSetup(0,0,1007,0)", 124) == \
        "1 0 1007 Controller.ControllerSetup(0,0,124,0)"
    assert banks_exe("setup", "Controller.ControllerSetup(1,-1,1003,1)", 125) == \
        "1 1 1003 Controller.ControllerSetup(1,-1,125,1)"
    assert banks_exe("setup", "Controller.ControllerSetup(1,1,55,1)", 125) == "0 1 55 -"     # a stock fighter
    assert banks_exe("setup", "Controller.ControllerSetup(-1,0,0,0)", 124) == "0 -1 0 -"     # the reset
    assert banks_exe("setup", "Controller.ControllerSetup(4,0,1007,0)", 124) == "0 4 1007 -"   # no fifth bank
    assert banks_exe("setup", "Game.StartGame()", 124) == "0 -1 -1 -"


def test_c_answer_fields(banks_exe):
    assert banks_exe("fields", "0=124&1=125&2=-99&3=-99&venue=1&matchtype=0") == \
        "1 0=1007&1=1003&2=-99&3=-99&venue=1&matchtype=0"
    assert banks_exe("fields", "win=1&isAI=0&fighter=Syn 1007&fid=124&cid=0&total=5850") == \
        "1 win=1&isAI=0&fighter=Syn 1007&fid=1007&cid=0&total=5850"
    assert banks_exe("fields", "0=55&1=22&venue=124&matchtype=0") == "0 -"    # only the fighters' keys
    assert banks_exe("fields", "fid=1240&cid=124") == "0 -"


def test_c_picture_names(banks_exe):
    assert banks_exe("picture", "p_cf_125_2p") == "1 p_cf_1008_2p"          # ID + 1 families
    assert banks_exe("picture", "portrait/p_ML_127_3p.swf") == "1 portrait/p_ML_1004_3p.swf"
    assert banks_exe("picture", "l_CF_124_2p") == "1 l_CF_1007_2p"
    assert banks_exe("picture", "thumbs/thumb126off.swf") == "1 thumbs/thumb1003off.swf"
    assert banks_exe("picture", "portrait/125_2p") == "1 portrait/1008_2p"
    assert banks_exe("picture", "l_CF_125_2p") == "0 -"                     # bank 1 holds nothing
    assert banks_exe("picture", "p_cf_56_2p") == "0 -"
    assert banks_exe("picture", "thumbs/thumb1240.swf") == "0 -"


@needs_dump
def test_c_side_file_check_agrees(banks_exe, tmp_path):
    sod = _sod()
    stock = _stock(sod)
    fighters = [{"id": 1000 + 3 * i, "template_fe_id": stock[i]["fe_id"], "name": "Syn %d" % i} for i in range(7)]
    out, side = unlimited.add_records(sod, fighters)
    (tmp_path / "sod").write_bytes(out)
    (tmp_path / "side").write_bytes(side)
    off = unlimited.read_side(side)["banks"][127]
    assert banks_exe("parse", tmp_path / "side", tmp_path / "sod") == \
        "ok 7 1018 Syn 6 %d %08X" % (stock[0]["fe_id"], off)
    (tmp_path / "sod2").write_bytes(out[:-1] + bytes([out[-1] ^ 1]))
    assert banks_exe("parse", tmp_path / "side", tmp_path / "sod2").startswith("refused")


def test_hooks_are_wired():
    manual = (ROOT / "src/recomp_manual.c").read_text(encoding="utf-8")
    assert "void sub_000BE760(void)" in manual and "fighter_banks_record(" in manual
    assert "fighter_banks_funccall_before(" in manual and "fighter_banks_funccall_after(" in manual
    assert "fighter_banks_movie_load(" in manual
    hooks = (ROOT / "src/hooks/fighter_bank_hooks.c").read_text(encoding="utf-8")
    assert "ffny_online_locked()" in hooks       # offline only


@needs_dump
def test_grid_pictures_by_url_patch():
    """apt-patch version 6 on the user's own chooseF2/3/4: the three picture calls each get the community thumb
    call, the helper is defined, the copy verifies, and only the action item's pointer changes in place."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("apt_patch", ROOT / "scripts" / "apt-patch.py")
    ap = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ap)
    from ffnycontent import viv
    screens = viv.Archive(str(GAME / "screens" / "screens.viv"))
    assert ap.PATCH_VERSION >= 6
    for name in ap.CHOOSEF_ENTRIES:
        new_raw, lines = ap.choosef_patcher(name)(screens.raw(screens.find(name)))
        assert any("grid pictures by URL: 308 bytes inserted" in line for line in lines), lines

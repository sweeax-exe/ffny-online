"""The Unlock Rewards screen's fighter list (src/hooks/unlock_list.h, new_fighters.c): Unlock.GetCharacterList
answers in the front end's 0x600-byte result text, and with more than about six new fighters the game's list ran
past it, so the screen showed only its placeholder (every cell the same picture, the authoring panel; trollizas,
build 85). The filter keeps the stock fighters' pairs in their order, drops every new fighter's place, and its
answer always fits. Needs MSVC (`cl` on PATH) for the native part; the wiring checks run everywhere."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include "unlock_list.h"
int main(int argc, char **argv)
{
    static char in[0x4000], out[FE_RESULT_SIZE];
    int left = -1, kept;
    FILE *f = fopen(argv[1], "rb");
    size_t n = fread(in, 1, sizeof(in) - 1, f);
    (void)argc;
    fclose(f);
    in[n] = 0;
    kept = unlock_list_filter(in, out, sizeof(out), &left);
    printf("%d %d %u\n%s", kept, left, (unsigned)strlen(out), out);
    return 0;
}
'''

# the stock list's shape (BE = 7 << 24 | row, FE = the fighter's front-end ID); 74 fighters, the Hero's row left out
STOCK_FE = [33, 25, 18, 56, 81, 82, 26, 1, 31, 23, 47, 69, 13, 67, 46, 2, 3, 6, 7, 8, 9, 10, 11, 12, 14, 16, 17, 19,
            20, 21, 22, 24, 27, 28, 30, 32, 34, 35, 36, 38, 43, 44, 45, 48, 50, 51, 54, 55, 57, 59, 60, 61, 62, 64,
            65, 68, 70, 71, 72, 73, 74, 75, 76, 77, 80, 83, 84, 85, 91, 92, 93, 4, 0, 58]
PLACES = list(range(100, 128)) + [5, 15, 29, 37, 39, 42, 49, 53, 63, 66, 79, 86, 87, 88, 89, 90, 94, 95, 96, 97]


def game_list(fes):
    return "Count=%d" % len(fes) + "".join("&BE=%d&FE=%d" % ((7 << 24) | (i + 1), fe) for i, fe in enumerate(fes))


@pytest.fixture(scope="module")
def filt(tmp_path_factory):
    cl = shutil.which("cl")
    if not cl:
        pytest.skip("needs MSVC (cl on PATH)")
    d = tmp_path_factory.mktemp("unlock_list")
    (d / "driver.c").write_text(DRIVER)
    exe = d / "unlock_list.exe"
    r = subprocess.run([cl, "/nologo", "/W4", "/WX", "/TC", "/I" + str(ROOT / "src/hooks"), str(d / "driver.c"),
                        "/Fe:" + str(exe)], cwd=d, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr

    def run(text):
        src = d / "in.txt"
        src.write_bytes(text.encode())
        out = subprocess.run([str(exe), str(src)], capture_output=True, text=True, check=True).stdout
        head, _, body = out.partition("\n")
        kept, left, length = map(int, head.split())
        assert length == len(body)
        return kept, left, body
    return run


def test_stock_list_is_kept_as_it_is(filt):
    text = game_list(STOCK_FE)
    assert len(text) < 0x600            # the retail list fits: 74 fighters, about 1.4 KB
    assert filt(text) == (74, 0, text)


def test_new_fighters_are_left_out_and_the_answer_fits(filt):
    fes = STOCK_FE + PLACES             # all 48 places taken: the game's list is about 2.4 KB
    text = game_list(fes)
    assert len(text) > 0x600
    kept, left, out = filt(text)
    assert (kept, left) == (74, 48)
    assert len(out) < 0x600
    pairs = re.findall(r"&BE=(\d+)&FE=(\d+)", out)
    assert [int(fe) for _, fe in pairs] == STOCK_FE          # the game's order, stock only
    assert out.startswith("Count=74&")


def test_a_list_too_long_for_the_result_is_cut_not_overrun(filt):
    text = game_list(STOCK_FE * 2)      # never in the game, but the answer must still fit
    kept, left, out = filt(text)
    assert kept + left == 148 and kept < 148
    assert len(out) < 0x600 and out.startswith("Count=%d&" % kept)


def test_empty_and_odd_answers(filt):
    assert filt("Count=0") == (0, 0, "Count=0")
    assert filt("") == (0, 0, "Count=0")


def test_hooks_are_wired():
    manual = (ROOT / "src/recomp_manual.c").read_text(encoding="utf-8")
    assert re.search(r"xbox_va == 0x00054420u\)[^\n]*\n\s*return new_fighters_unlock_list;", manual)
    assert re.search(r"xbox_va == 0x000546B0u\)[^\n]*\n\s*return new_fighters_unlock_info;", manual)
    assert "id_set_make_room(set," in manual
    hooks = (ROOT / "src/hooks/new_fighters.c").read_text(encoding="utf-8")
    assert '#include "unlock_list.h"' in hooks
    # the handlers' result text and its size, as the game's code has them (sub_0005F7B0 / sub_0005F810, 0x341D68 next)
    assert "#define FE_RESULT 0x00341768u" in hooks
    assert "#define FE_RESULT_SIZE 0x600u" in (ROOT / "src/hooks/unlock_list.h").read_text(encoding="utf-8")

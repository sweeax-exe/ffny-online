"""Previous song: the history of what played (src/hooks/song_history.c), compiled natively.

As on a music player: the first back press plays the current song from its start; a second one within
3 s, or a back in a song's first 3 s, goes to the song before it (and on back from there). Going back
adds nothing; a new song after that drops the entries after the place. Entries that cannot be played
now are passed over; with none, back starts the current song again.
Needs MSVC (`cl` on PATH) on Windows or `cc` elsewhere; skips without it.
"""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "song_history.h"

static SongHistory h;

static int no_b(void *ctx, const SongHistEntry *e) { (void)ctx; return strcmp(e->id, "b") != 0; }

/* commands: p<id>@t (played, game), o<id>@t (played, own), b@t (back), x@t (back, "b" not playable) */
int main(int argc, char **argv)
{
    int i;
    song_hist_reset(&h);
    for (i = 1; i < argc; i++) {
        char id[64] = "";
        unsigned long long t = 0;
        const char *at = strchr(argv[i], '@');
        if (!at) return 2;
        sscanf(at + 1, "%llu", &t);
        memcpy(id, argv[i] + 1, (size_t)(at - argv[i] - 1));
        if (argv[i][0] == 'p' || argv[i][0] == 'o') {
            int added = song_hist_played(&h, argv[i][0] == 'p' ? SONG_HIST_GAME : SONG_HIST_OWN, id, t);
            if (added) printf("+%s ", id);
        } else {
            int to = song_hist_back(&h, t, argv[i][0] == 'x' ? no_b : NULL, NULL);
            const SongHistEntry *e = song_hist_current(&h);
            if (to < 0) printf("restart:%s ", e ? e->id : "-");
            else printf("back:%s%s ", e->kind == SONG_HIST_OWN ? "own:" : "", e->id);
        }
    }
    printf("| n=%d pos=%d\n", h.n, h.pos);
    return 0;
}
'''


@pytest.fixture(scope='module')
def exe(tmp_path_factory):
    compiler = shutil.which('cl') if os.name == 'nt' else shutil.which('cc')
    if not compiler:
        pytest.skip('the native fixture needs a C compiler (MSVC on Windows)')
    folder = tmp_path_factory.mktemp('songhist')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    src = str(ROOT / 'src/hooks/song_history.c')
    if os.name == 'nt':
        out = folder / 'songhist.exe'
        cmd = [compiler, '/nologo', '/O2', '/TC', '/I' + str(ROOT / 'src/hooks'), str(folder / 'driver.c'), src,
               '/Fe:' + str(out)]
    else:
        out = folder / 'songhist'
        cmd = [compiler, '-O2', '-std=c11', '-I' + str(ROOT / 'src/hooks'), str(folder / 'driver.c'), src, '-o', str(out)]
    built = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return out


def run(exe, *args):
    return subprocess.run([str(exe), *args], capture_output=True, text=True, check=True).stdout.strip()


def test_first_back_restarts_the_second_goes_back(exe):
    out = run(exe, 'pa@0', 'pb@60000', 'b@90000', 'pb@90100', 'b@91000')
    assert out == '+a +b restart:b back:a | n=2 pos=0', out


def test_back_in_a_songs_first_seconds_goes_back_at_once(exe):
    assert run(exe, 'pa@0', 'pb@60000', 'b@61500').startswith('+a +b back:a ')


def test_a_late_second_press_restarts_again(exe):
    assert run(exe, 'pa@0', 'pb@60000', 'b@90000', 'b@95000').startswith('+a +b restart:b restart:b ')


def test_back_back_back_walks_the_history(exe):
    out = run(exe, 'pa@0', 'pb@60000', 'pc@120000', 'b@121000', 'b@122000', 'b@123000')
    assert out == '+a +b +c back:b back:a restart:a | n=3 pos=0', out


def test_going_back_adds_nothing_and_a_new_song_drops_the_rest(exe):
    # c still heard for a moment after the back: not a new song; then a plays (the target); d is new
    out = run(exe, 'pa@0', 'pb@60000', 'pc@120000', 'b@121000', 'pc@121100', 'pb@121500', 'pd@200000')
    assert out == '+a +b +c back:b +d | n=3 pos=2', out


def test_unplayable_entries_are_passed_over(exe):
    assert run(exe, 'pa@0', 'pb@60000', 'pc@120000', 'x@121000').startswith('+a +b +c back:a ')
    assert run(exe, 'pb@0', 'pc@60000', 'x@61000').startswith('+b +c restart:c ')


def test_own_and_game_songs_share_the_history(exe):
    out = run(exe, 'pa@0', 'ox.mp3@60000', 'b@61000')
    assert out.startswith('+a +x.mp3 back:a '), out
    assert run(exe, 'ox.mp3@0', 'pa@60000', 'b@61000').startswith('+x.mp3 +a back:own:x.mp3 ')


def test_nothing_played_back_restarts(exe):
    assert run(exe, 'b@1000') == 'restart:- | n=0 pos=-1'


def test_the_history_keeps_the_last_songs(exe):
    args = [f'p{i}@{i * 60000}' for i in range(20)] + ['b@1140500']
    out = run(exe, *args)
    assert out.endswith('back:18 | n=16 pos=14'), out          # 16 kept: 4..19

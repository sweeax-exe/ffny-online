"""Next song (src/hooks/next_song.c), compiled natively with synthetic guest memory.

The [audio] next_song key moves the game's music playlist on the way a song's end does: the playlist
machine's state 2 with the timer past the length, the index left for the machine to move on (or moved
on by us when "hold" is set). It waits while a song is ending or loading, does nothing without a
playlist or inside a rollback re-run step, hands one of the player's own songs to their player, and
puts up "Now playing: <title>" for a few seconds. The Song table of the user's own djv2.sod gives the
titles (skipped without the dump).
Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1) on Windows or `cc` elsewhere; skips without it.
"""
import os
import shutil
import struct
import subprocess
from pathlib import Path

import pytest

from native_hooks_fixtures import STUB_TYPES

ROOT = Path(__file__).resolve().parents[2]
SOD = ROOT / 'game' / 'assets' / 'djv2.sod'

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "next_song.h"

unsigned char memory[0x400000];
uint32_t g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_ebp, g_esp;
double g_fp_stack[8];
int g_fp_top;
uint16_t g_fp_control_word;

static int own_song, skips;
static const char *sod_path;
int custom_music_want_mute(void) { return own_song; }
void custom_music_skip(void) { skips++; }
FILE *netplay_open_fight_file(const char *name, int *overridden) { (void)name; *overridden = 0; return sod_path ? fopen(sod_path, "rb") : NULL; }

static void playlist(uint32_t count, uint32_t index, int hold)
{
    uint32_t i;
    MEM8(0x33E794) = 1;
    for (i = 0; i < count; i++) snprintf((char *)memory + 0x33E798 + 16 * i, 16, "song%u", i);
    MEM32(0x33E9DC) = count;
    MEM32(0x33EC94) = index;
    MEM8(0x33EC90) = (uint8_t)hold;
    MEMF(0x33E790) = 10.0f;
    MEMF(0x33EC40) = 200.0f;
    MEM32(0x340374) = 1;
    MEM32(0x340398) = 1;
    MEM32(0x3407B4) = 0;
}

static void show(void)
{
    char note[128];
    int up = next_song_toast(note, sizeof note, 1000);
    printf("index %u state %u timer_past %d skips %d note %s\n", MEM32(0x33EC94), MEM32(0x340398),
           MEMF(0x33E790) > MEMF(0x33EC40), skips, up ? note : "-");
}

int main(int argc, char **argv)
{
    const char *what = argv[1];
    if (!strcmp(what, "step")) {            /* step count index hold */
        playlist((uint32_t)atoi(argv[2]), (uint32_t)atoi(argv[3]), atoi(argv[4]));
        next_song_request("test");
        next_song_game_tick(0);
        show();
        return 0;
    }
    if (!strcmp(what, "rerun")) {           /* a re-run step leaves it alone; the next real step acts */
        playlist(5, 2, 0);
        next_song_request("test");
        next_song_game_tick(1);
        show();
        next_song_game_tick(0);
        show();
        return 0;
    }
    if (!strcmp(what, "busy")) {            /* a song loading: it waits, then acts */
        playlist(5, 2, 0);
        MEM32(0x3407B4) = 2;
        next_song_request("test");
        next_song_game_tick(0);
        show();
        MEM32(0x3407B4) = 0;
        next_song_game_tick(0);
        show();
        return 0;
    }
    if (!strcmp(what, "none")) {            /* no playlist: dropped, nothing written */
        playlist(5, 2, 0);
        MEM8(0x33E794) = 0;
        next_song_request("test");
        next_song_game_tick(0);
        MEM8(0x33E794) = 1;
        next_song_game_tick(0);
        show();
        return 0;
    }
    if (!strcmp(what, "own")) {             /* one of the player's songs: their player, the game's left alone */
        playlist(5, 2, 0);
        own_song = 1;
        next_song_request("test");
        next_song_game_tick(0);
        show();
        return 0;
    }
    if (!strcmp(what, "toast")) {
        char note[128];
        next_song_note_set("Song - Artist");
        printf("%d %s\n", next_song_toast(note, sizeof note, 5000), note);
        printf("%d\n", next_song_toast(note, sizeof note, 8000));
        printf("%d\n", next_song_toast(note, sizeof note, 8600));
        return 0;
    }
    if (!strcmp(what, "titles")) {          /* titles path id */
        sod_path = argv[2];
        printf("[%s]\n", next_song_title_for(argv[3]));
        return 0;
    }
    if (!strcmp(what, "junk")) {
        unsigned char buf[256];
        NextSongTitle t[4];
        memset(buf, 0xFF, sizeof buf);
        memcpy(buf, "SODB", 4);
        printf("%d %d\n", next_song_sod_titles(buf, sizeof buf, t, 4), next_song_sod_titles(buf, 3, t, 4));
        return 0;
    }
    return 2;
}
'''


@pytest.fixture(scope='module')
def exe(tmp_path_factory):
    compiler = shutil.which('cl') if os.name == 'nt' else shutil.which('cc')
    if not compiler:
        pytest.skip('the native fixture needs a C compiler (MSVC on Windows)')
    folder = tmp_path_factory.mktemp('nextsong')
    (folder / 'recomp/gen').mkdir(parents=True)
    (folder / 'recomp/gen/recomp_types.h').write_text(STUB_TYPES)
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    src = str(ROOT / 'src/hooks/next_song.c')
    if os.name == 'nt':
        out = folder / 'nextsong.exe'
        cmd = [compiler, '/nologo', '/O2', '/TC', '/I' + str(folder), '/I' + str(ROOT / 'src/hooks'),
               str(folder / 'driver.c'), src, '/Fe:' + str(out)]
    else:
        out = folder / 'nextsong'
        cmd = [compiler, '-O2', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-I' + str(folder),
               '-I' + str(ROOT / 'src/hooks'), str(folder / 'driver.c'), src, '-o', str(out)]
    built = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return out


def run(exe, *args):
    e = {k: v for k, v in os.environ.items() if k != 'RECOMP_TEST_NEXT_SONG'}
    return subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, check=True, env=e).stdout


def test_the_song_ends_as_at_its_end_and_the_machine_moves_on(exe):
    out = run(exe, 'step', 5, 2, 0)
    # index left for the machine (it adds one in state 2), timer past the length
    assert out.startswith('index 2 state 2 timer_past 1 skips 0 note Next song'), out


def test_hold_moves_the_index_itself_and_wraps(exe):
    assert run(exe, 'step', 5, 2, 1).startswith('index 3 state 2'), 'hold: we move it on'
    assert run(exe, 'step', 5, 4, 1).startswith('index 0 state 2'), 'wraps at the count'


def test_never_inside_a_rerun_step(exe):
    first, second = run(exe, 'rerun').splitlines()
    assert first.startswith('index 2 state 1 timer_past 0'), first      # re-run: untouched
    assert second.startswith('index 2 state 2 timer_past 1'), second    # the next real step


def test_waits_while_a_song_loads(exe):
    first, second = run(exe, 'busy').splitlines()
    assert 'state 1 timer_past 0' in first and 'state 2 timer_past 1' in second


def test_no_playlist_drops_the_request(exe):
    assert run(exe, 'none').startswith('index 2 state 1 timer_past 0 skips 0')


def test_own_song_goes_to_its_player(exe):
    assert run(exe, 'own').startswith('index 2 state 1 timer_past 0 skips 1')


def test_note_shows_for_a_few_seconds(exe):
    lines = run(exe, 'toast').splitlines()
    assert lines[0] == '1 Now playing: Song - Artist'
    assert lines[1] == '1' and lines[2] == '0'


def test_junk_database_reads_as_none(exe):
    assert run(exe, 'junk').split() == ['-1', '-1']


@pytest.mark.skipif(not SOD.exists(), reason="needs the user's own dump (game/assets/djv2.sod)")
def test_titles_from_the_users_own_database(exe):
    dirty = run(exe, 'titles', SOD, 'dirty').strip()
    assert 'Dirty' in dirty and ' - ' in dirty, dirty           # Redman, "Let's Get Dirty"
    assert run(exe, 'titles', SOD, 'DIRTY').strip() == dirty     # ids compare without case
    assert run(exe, 'titles', SOD, 'nosuchsong').strip() == '[]'

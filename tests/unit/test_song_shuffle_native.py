"""The shuffled music playlist, evenly (src/patches/song_shuffle.c), compiled natively.

The game's shuffled playlist builder (sub_00020D70) puts each song that is on at slot rand() % n and, when
that slot is taken, at the lowest free slot. A model of that rule shows the bias the player saw: with all
32 songs on, the song in Song-table place 16 ("Get It Now") starts the list well under 1% of the time and
the late places never. The fix reorders the builder's list with a Fisher-Yates shuffle: every song starts
the list about 1/n of the time, the list stays the same songs (the game frees them by index), and
RECOMP_SONG_SHUFFLE=retail or a list that is not the builder's is left alone.
Needs MSVC (`cl` on PATH, e.g. after scripts/env.ps1) on Windows or `cc` elsewhere; skips without it.
"""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

from native_hooks_fixtures import STUB_TYPES

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "patches/song_shuffle.h"

unsigned char memory[0x10000];
uint32_t g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_ebp, g_esp;
double g_fp_stack[8];
int g_fp_top;
uint16_t g_fp_control_word;

/* The game's rule (sub_00020D70): song i of n at slot r = rand() % n, else the lowest free slot. */
static void retail(uint32_t *out, uint32_t n, uint64_t *rng)
{
    uint32_t i, j, free_mark = 0xFFFFFFFFu;
    for (i = 0; i < n; i++) out[i] = free_mark;
    for (i = 0; i < n; i++) {
        uint32_t r = (uint32_t)(rand() % (int)n);
        (void)rng;
        if (out[r] == free_mark) { out[r] = i; continue; }
        for (j = 0; j < n; j++) if (out[j] == free_mark) { out[j] = i; break; }
    }
}

int main(int argc, char **argv)
{
    uint64_t rng = 12345;
    if (!strcmp(argv[1], "first")) {           /* first n trials fixed|retail: how often each song starts */
        uint32_t n = (uint32_t)atoi(argv[2]), trials = (uint32_t)atoi(argv[3]), t, i, items[64], seen[64];
        static uint32_t first[64];
        int fixed = !strcmp(argv[4], "fixed");
        for (t = 0; t < trials; t++) {
            if (fixed) {
                for (i = 0; i < n; i++) items[i] = i;
                song_shuffle_permute(items, n, &rng);
            } else {
                retail(items, n, &rng);
            }
            memset(seen, 0, sizeof seen);
            for (i = 0; i < n; i++) {
                if (items[i] >= n || seen[items[i]]++) { printf("bad\n"); return 1; }
            }
            first[items[0]]++;
        }
        for (i = 0; i < n; i++) printf("%u%s", first[i], i + 1 < n ? " " : "\n");
        return 0;
    }
    if (!strcmp(argv[1], "list")) {            /* list n [hole]: the builder's list in guest memory */
        uint32_t n = (uint32_t)atoi(argv[2]), names = 0x1000, count = 0x2000, i;
        for (i = 0; i < n; i++) MEM32(names + 4 * i) = 0x8000 + 0x10 * i;
        MEM32(names + 4 * n) = 0;
        MEM32(count) = n;
        if (argc > 3) MEM32(names + 4 * (uint32_t)atoi(argv[3])) = 0;
        song_shuffle_after_build(names, count);
        song_shuffle_after_build(names, count);
        for (i = 0; i <= n; i++) printf("%X%s", MEM32(names + 4 * i), i < n ? " " : "\n");
        printf("count %u\n", MEM32(count));
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
    folder = tmp_path_factory.mktemp('songshuffle')
    (folder / 'recomp/gen').mkdir(parents=True)
    (folder / 'recomp/gen/recomp_types.h').write_text(STUB_TYPES)
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    src = str(ROOT / 'src/patches/song_shuffle.c')
    if os.name == 'nt':
        out = folder / 'songshuffle.exe'
        cmd = [compiler, '/nologo', '/O2', '/TC', '/I' + str(folder), '/I' + str(ROOT / 'src'),
               '/I' + str(ROOT / 'src/patches'), str(folder / 'driver.c'), src, '/Fe:' + str(out)]
    else:
        out = folder / 'songshuffle'
        cmd = [compiler, '-O2', '-std=c11', '-I' + str(folder), '-I' + str(ROOT / 'src'),
               '-I' + str(ROOT / 'src/patches'), str(folder / 'driver.c'), src, '-o', str(out)]
    built = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return out


def run(exe, *args, env=None):
    e = {k: v for k, v in os.environ.items() if k != 'RECOMP_SONG_SHUFFLE'}
    e.update(env or {})
    return subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, check=True, env=e).stdout


def firsts(exe, n, trials, kind):
    return [int(x) for x in run(exe, 'first', n, trials, kind).split()]


def test_the_games_rule_almost_never_starts_with_a_late_song(exe):
    counts = firsts(exe, 32, 20000, 'retail')
    share = [c / 20000 for c in counts]
    assert share[16] < 0.01                      # "Get It Now": Song-table place 16 of 32
    assert sum(share[20:]) < 0.002               # places 20-31: practically never
    assert max(share[3:8]) > 0.09                # the front goes to the early places


def test_the_fix_starts_with_every_song_equally(exe):
    trials = 64000
    counts = firsts(exe, 32, trials, 'fixed')
    expected = trials / 32
    assert all(abs(c - expected) < expected * 0.15 for c in counts), counts


def test_the_list_keeps_its_songs_and_end(exe):
    names, count = run(exe, 'list', 32).splitlines()
    got = [int(x, 16) for x in names.split()]
    assert got[-1] == 0 and count == 'count 32'
    assert sorted(got[:-1]) == [0x8000 + 0x10 * i for i in range(32)]
    assert got[:-1] != [0x8000 + 0x10 * i for i in range(32)]     # reordered (1 in 32! to stay)


def test_retail_switch_and_foreign_lists_are_left_alone(exe):
    same = ' '.join('%X' % (0x8000 + 0x10 * i) for i in range(16)) + ' 0'
    assert run(exe, 'list', 16, env={'RECOMP_SONG_SHUFFLE': 'retail'}).splitlines()[0] == same
    holed = run(exe, 'list', 16, 5).splitlines()[0].split()
    assert holed[5] == '0' and holed[:5] == ['%X' % (0x8000 + 0x10 * i) for i in range(5)]

"""Run the game-side guard against the retail fallback's observable contract.

In particular, 0/1/0/2 simulation ticks must contribute three ticks of cinema
time, not three ticks plus two synthetic 1/30-second steps. The original body
still runs on every render so cleanup/transition work and guest ABI survive.
"""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define TEST_TLS __declspec(thread)
#else
#include <pthread.h>
#include <sched.h>
#define TEST_TLS __thread
#endif
#include "recomp/gen/recomp_types.h"
#include "cinema_timing.h"
unsigned char memory[0x300000];
TEST_TLS uint32_t g_ecx = 0x1000, g_eax = 0, g_esp = 0x2000;
static const uint32_t player = 0x1000, shots = 0x4000, minimum = 0x29D24C;
static unsigned calls;
static double elapsed;
static int change_registers;
static uint32_t observed_minimum;
static int reenter;

/* Model the original fallback and zero guard, not the corrective predicate.
 * A real generated render also performs bookkeeping around this operation. */
static void original(void)
{
    if (reenter) {
        uint32_t outer_delta = MEM32(player + 0x764);
        reenter = 0;
        MEMF(player + 0x764) = 1.0f / 240;
        defjam_cinema_render(original);
        MEM32(player + 0x764) = outer_delta;
    }
    float delta = MEMF(player + 0x764);
    uint32_t record = MEM32(player + 0xC4) + MEM32(player + 0xCC) * 0xCC;
    calls++; /* Must run even with no simulation update. */
    observed_minimum = MEM32(minimum);
    if ((MEM8(player) & 1) && !MEM8(player + 0x984)) {
        if (delta < MEMF(minimum) && !memcmp(memory + record, "sc04", 4))
            delta = 1.0f / 30.0f;
        if (delta != 0.0f)
            elapsed += delta;
        MEM32(player + 0x764) = 0;
        MEM32(player + 0xD0) = MEM32(player + 0xCC); /* housekeeping */
    }
    if (change_registers) { g_ecx = 0xABC; g_eax = 0xDEF; g_esp += 4; }
}
static void reset(const char *name, float delta)
{
    memset(memory, 0, sizeof memory);
    g_ecx = player; g_eax = 0x123; g_esp = 0x2000;
    MEM8(player) = 1;
    MEM32(player + 0xC4) = shots;
    MEM32(player + 0xC8) = 2;
    MEM32(player + 0xCC) = 1;
    strcpy((char *)memory + shots + 0xCC, name);
    MEMF(player + 0x764) = delta;
    MEM32(minimum) = 0x3C088889;
    calls = 0; elapsed = 0; change_registers = 0; reenter = 0;
}
static void render(void)
{
    defjam_cinema_render(original);
    assert(MEM32(minimum) == 0x3C088889); /* no persistent data patch */
    assert(MEM32(player + 0xD0) == 1); /* zero-time cleanup was not skipped */
}
static void close_to(double want)
{
    assert(fabs(elapsed - want) < 0.000001);
}
static void zero_body(void)
{
    assert(MEM32(minimum) == 0);
#ifdef _WIN32
    Sleep(1);
#else
    sched_yield();
#endif
    assert(MEM32(minimum) == 0);
}
static void positive_body(void) { assert(MEM32(minimum) == 0x3C088889); }
#ifdef _WIN32
static DWORD WINAPI concurrent(void *argument)
#else
static void *concurrent(void *argument)
#endif
{
    int i, positive = argument != NULL;
    g_ecx = positive ? 0x8000 : player;
    for (i = 0; i < 16; i++)
        defjam_cinema_render(positive ? positive_body : zero_body);
    return 0;
}
int main(int argc, char **argv)
{
    int enabled = argc > 1 && argv[1][0] == '1';
    const float tick = 1.0f / 60.0f;
    const int schedule[] = {0, 1, 0, 2};
    int i;
    reset("sc04_03", 0);
    for (i = 0; i < 4; i++) {
        MEMF(player + 0x764) = schedule[i] * tick;
        render();
    }
    assert(calls == 4);
    close_to(3 * (double)tick + (enabled ? 0 : 2.0 / 30));

    reset("sc04_14", -0.0f); render();
    close_to(enabled ? 0 : 1.0 / 30);
    reset("sc04_01", tick); render(); close_to(tick);
    assert(observed_minimum == 0x3C088889);
    /* Preserve the retail positive-small-delta behavior deliberately. */
    reset("sc04_13b", 1.0f / 240); render(); close_to(1.0 / 30);
    assert(observed_minimum == 0x3C088889);
    for (i = 0; i < 3; i++) {
        reset("sc04_14", 0);
        MEM32(player + 0x758) = i;
        render();
        close_to(enabled ? 0 : 1.0 / 30);
        assert(MEM32(player + 0x758) == (uint32_t)i);
    }

    reset("sc03_01", 0); render(); close_to(0);
    assert(observed_minimum == 0x3C088889);
    reset("sc05_01", tick); render(); close_to(tick);
    reset("SC04_01", 0); render(); close_to(0);
    reset("sc04_01", 0); MEM8(player + 0x984) = 1;
    defjam_cinema_render(original); close_to(0);
    assert(observed_minimum == 0x3C088889);
    reset("sc04_01", 0); MEM8(player) = 0;
    defjam_cinema_render(original); close_to(0);
    assert(observed_minimum == 0x3C088889);
    reset("sc04_01", 0); MEM32(player + 0xC8) = 1;
    defjam_cinema_render(original);
    assert(observed_minimum == 0x3C088889); /* invalid index not intercepted */

    reset("sc04_01", 0); change_registers = 1;
    render();
    assert(g_ecx == 0xABC && g_eax == 0xDEF && g_esp == 0x2004);
    /* A cleanup callback can reenter with a small positive delta. It must
     * retain its retail 1/30 fallback, and the outer zero call stays zero. */
    reset("sc04_01", 0); reenter = 1; render();
    assert(calls == 2);
    close_to(enabled ? 1.0 / 30 : 2.0 / 30);
    if (enabled) {
        reset("sc04_01", 0);
        memcpy(memory + 0x8000, memory + player, 0x990);
        MEMF(0x8000 + 0x764) = 1.0f / 240;
#ifdef _WIN32
        HANDLE first = CreateThread(NULL, 0, concurrent, NULL, 0, NULL);
        HANDLE second = CreateThread(NULL, 0, concurrent, (void *)1, 0, NULL);
        assert(first && second);
        assert(WaitForSingleObject(first, 10000) == WAIT_OBJECT_0);
        assert(WaitForSingleObject(second, 10000) == WAIT_OBJECT_0);
        CloseHandle(first); CloseHandle(second);
#else
        pthread_t first, second;
        assert(!pthread_create(&first, NULL, concurrent, NULL));
        assert(!pthread_create(&second, NULL, concurrent, (void *)1));
        pthread_join(first, NULL); pthread_join(second, NULL);
#endif
        assert(MEM32(minimum) == 0x3C088889);
    }
    puts(enabled ? "guard on: timing, scope, housekeeping and ABI pass"
                 : "guard off: retail baseline reproduced");
    return 0;
}
'''


@pytest.fixture(scope='module')
def cinema_tool(tmp_path_factory):
    compiler = shutil.which('cl') if os.name == 'nt' else shutil.which('cc')
    if not compiler:
        pytest.skip('native cinema fixture needs a C compiler')
    folder = tmp_path_factory.mktemp('cinema-native')
    inc = folder / 'recomp/gen'
    inc.mkdir(parents=True)
    (inc / 'recomp_types.h').write_text(
        '#include <stdint.h>\nextern unsigned char memory[];\n'
        '#ifdef _WIN32\n#define TEST_TLS __declspec(thread)\n'
        '#else\n#define TEST_TLS __thread\n#endif\n'
        'extern TEST_TLS uint32_t g_ecx, g_eax, g_esp;\n'
        '#define MEM32(a) (*(uint32_t *)(memory + (a)))\n'
        '#define MEM8(a) (*(uint8_t *)(memory + (a)))\n'
        '#define MEMF(a) (*(float *)(memory + (a)))\n')
    driver = folder / 'driver.c'
    driver.write_text(DRIVER)
    exe = folder / ('cinema.exe' if os.name == 'nt' else 'cinema')
    includes = [folder, ROOT / 'src/patches']
    sources = [driver, ROOT / 'src/patches/cinema_timing.c']
    if os.name == 'nt':
        cmd = [compiler, '/nologo', '/O2', '/TC', *['/I' + str(p) for p in includes],
               *map(str, sources), '/Fe:' + str(exe)]
    else:
        cmd = [compiler, '-O2', '-fno-strict-aliasing', *['-I' + str(p) for p in includes],
               *map(str, sources), '-o', str(exe), '-lm', '-pthread']
    result = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


@pytest.mark.parametrize('env_value,enabled', [(None, True), ('1', True), ('0', False)])
def test_cinema_zero_step_and_scope(cinema_tool, env_value, enabled):
    env = dict(os.environ)
    env.pop('RECOMP_SC04_ZERO_DT_FIX', None)
    if env_value is not None:
        env['RECOMP_SC04_ZERO_DT_FIX'] = env_value
    env.pop('RECOMP_SC04_TIMING_TRACE', None)
    result = subprocess.run([str(cinema_tool), '1' if enabled else '0'], env=env,
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert 'pass' in result.stdout or 'retail baseline reproduced' in result.stdout

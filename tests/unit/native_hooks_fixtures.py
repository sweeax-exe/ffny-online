"""Builds the test hooks (src/hooks/test_observations.c, test_telemetry.c, test_bot.c, recorder.c) natively
against synthetic guest memory, for the *_native tests. Needs MSVC (`cl` on PATH); the tests skip
without it.

The lifted code is not part of the build: a stand-in recomp_types.h maps guest memory to one
array, and STUB_SOURCE provides the few runtime and netplay symbols the hooks call (the guest
registers, the game's set-state routine sub_001B5420, and netplay_local_slot/netplay_rerunning)."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

STUB_TYPES = r'''#include <stdint.h>
extern unsigned char memory[];
#define MEM32(a) (*(uint32_t *)(memory + (a)))
#define MEM16(a) (*(uint16_t *)(memory + (a)))
#define MEM8(a)  (*(uint8_t *)(memory + (a)))
#define MEMF(a)  (*(float *)(memory + (a)))
#define XBOX_PTR(a) ((uintptr_t)(memory + (a)))
#define PUSH32(sp, v) do { (sp) -= 4; MEM32(sp) = (uint32_t)(v); } while (0)
extern uint32_t g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_ebp, g_esp;
extern double g_fp_stack[8];
extern int g_fp_top;
extern uint16_t g_fp_control_word;
'''

# The guest's stack lives at 0x380000.. in the synthetic memory, below the fighter records.
STUB_SOURCE = r'''
#include <stdint.h>
#include <stdlib.h>
#include "recomp/gen/recomp_types.h"
uint32_t g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_ebp, g_esp = 0x380000u;
double g_fp_stack[8];
int g_fp_top;
uint16_t g_fp_control_word;
int stub_set_state_calls;
/* sub_001B5420(record, state): the game's set-state routine, as far as the hooks can see it. */
void sub_001B5420(void)
{
    uint32_t rec = MEM32(g_esp + 4), state = MEM32(g_esp + 8);
    MEM32(rec + 0x30) = state;
    MEM32(rec + 0x34) = 0;
    stub_set_state_calls++;
    g_esp += 4;                         /* ret */
}
int netplay_local_slot(void) { const char *v = getenv("STUB_LOCAL_SLOT"); return v ? atoi(v) : -1; }
int netplay_rerunning(void) { return 0; }
'''

# recorder.c: test_telemetry.c copies its lines into the play-test recorder's events (upstream v0.10).
HOOK_SOURCES = ('src/hooks/test_observations.c', 'src/hooks/test_telemetry.c', 'src/hooks/test_bot.c',
                'src/hooks/recorder.c')


def build(tmp_path_factory, name, driver_text):
    """Compile driver_text with the hooks; returns the executable's path."""
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native hook fixtures need MSVC')
    folder = tmp_path_factory.mktemp(name)
    include = folder / 'recomp/gen'
    include.mkdir(parents=True)
    (include / 'recomp_types.h').write_text(STUB_TYPES)
    (folder / 'stub.c').write_text(STUB_SOURCE)
    driver = folder / 'driver.c'
    driver.write_text(driver_text, encoding='utf-8')
    exe = folder / (name + '.exe')
    result = subprocess.run([compiler, '/nologo', '/O2', '/TC', '/I' + str(folder), '/I' + str(ROOT / 'src/hooks'),
                             '/I' + str(ROOT / 'src'), str(driver), str(folder / 'stub.c')]
                            + [str(ROOT / s) for s in HOOK_SOURCES] + ['/Fe:' + str(exe)],
                            cwd=folder, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe

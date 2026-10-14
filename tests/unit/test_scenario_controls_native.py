"""src/hooks/fe_inject.c and src/hooks/crowd_control.c against a fake guest.

Both call translated game functions from a hook (src/hooks/guest_call.h). Here
the game functions are stand-ins that record what they were handed, so the tests
check what reaches the game -- the command string on the guest stack, `this` and
the arguments of a seeding call, the crowd's gate byte -- and that the hook's
caller gets every register back as it was."""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

TYPES = r'''
#include <stdint.h>
extern unsigned char memory[];
extern uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
extern int g_fp_top;
#define MEM8(a)  (*(volatile uint8_t *)(memory + (a)))
#define MEM32(a) (*(volatile uint32_t *)(memory + (a)))
'''

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "fe_inject.h"

unsigned char memory[0x40000];
uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
int g_fp_top;
int defjam_test_observations_enabled(void) { return 1; }
void defjam_crowd_match_start(void);
void defjam_crowd_update(void);
void defjam_crowd_seed(uint32_t generator, uint32_t stack);
void defjam_crowd_step(void);

/* The front end's handler: reads its one argument, as the real one does, and returns. */
void sub_00068BA0(void)
{
    uint32_t p = MEM32(g_esp + 4);
    printf("handler: %s\n", (const char *)memory + p);
    g_eax = 0x77; g_ecx = 0xDEAD; g_edx = 0xBEEF; g_esi = 1;
    g_esp += 4;
}
/* The seeding routine: thiscall, two arguments, ret 8. */
void sub_000AA0D0(void)
{
    printf("seed: this=%u high=%u low=%u\n", g_ecx, MEM32(g_esp + 4), MEM32(g_esp + 8));
    g_esp += 12;
}

int main(int argc, char **argv)
{
    if (!strcmp(argv[1], "inject")) {
        g_esp = 0x30000; g_eax = 5; g_ecx = 6; g_edx = 7; g_ebx = 8; g_esi = 9; g_edi = 10; g_ebp = 11;
        fe_inject_after_call("Control.GetScreenInfo(intMain/mainMenu.swf)");
        printf("after first: esp=%x eax=%u\n", g_esp, g_eax);
        fe_inject_after_call("Control.GetScreenInfo(story/crib.swf)");
        fe_inject_after_call("Control.GetScreenInfo(Story/Crib.swf)");
        printf("regs: esp=%x eax=%u ecx=%u edx=%u ebx=%u esi=%u edi=%u ebp=%u\n",
               g_esp, g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_ebp);
        fe_inject_after_call("Control.GetScreenInfo(story/crib.swf)");
        return 0;
    }
    /* crowd: set-up seeds one generator, the crowd updates five times, then the first step. */
    g_esp = 0x30000;
    MEM8(0x2E3488 % sizeof memory) = 0;
    defjam_crowd_match_start();
    MEM32(0x30004) = 111; MEM32(0x30008) = 222;
    defjam_crowd_seed(0x1000, 0x30000);
    {
        int i;
        for (i = 1; i <= 5; i++) {
            defjam_crowd_update();
            printf("update %d gate=%u\n", i, MEM8(0x2E3488));
        }
    }
    g_eax = 42;
    defjam_crowd_step();
    printf("after step gate=%u esp=%x eax=%u\n", MEM8(0x2E3488), g_esp, g_eax);
    defjam_crowd_step();                          /* only the first step acts */
    return 0;
}
'''


@pytest.fixture(scope='module')
def tool(tmp_path_factory):
    cc = shutil.which('cl') if os.name == 'nt' else (shutil.which('cc') or shutil.which('gcc') or shutil.which('clang'))
    if not cc:
        pytest.skip('no C compiler')
    folder = tmp_path_factory.mktemp('scenario-controls')
    (folder / 'recomp/gen').mkdir(parents=True)
    # 0x2E3488 is outside the fake memory: the test maps the crowd's gate into it.
    (folder / 'recomp/gen/recomp_types.h').write_text(
        TYPES.replace('(memory + (a))', '(memory + ((a) == 0x002E3488u || (a) == 0x2E3488 ? 0x3F000u : (a)))'),
        encoding='utf-8')
    driver = folder / 'driver.c'
    driver.write_text(DRIVER.replace('MEM8(0x2E3488 % sizeof memory) = 0;', 'MEM8(0x2E3488) = 1;'), encoding='utf-8')
    sources = [str(driver), str(ROOT / 'src/hooks/fe_inject.c'), str(ROOT / 'src/hooks/crowd_control.c'),
               str(ROOT / 'src/hooks/recorder.c')]
    if os.name == 'nt':
        exe = folder / 'controls.exe'
        cmd = [cc, '/nologo', '/TC', '/I' + str(folder), '/I' + str(ROOT / 'src/hooks')] + sources + ['/Fe:' + str(exe)]
    else:
        exe = folder / 'controls'
        cmd = [cc, '-std=gnu11', '-I' + str(folder), '-I' + str(ROOT / 'src/hooks')] + sources + ['-o', str(exe),
                                                                                              '-lpthread', '-lm']
    r = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    return exe


def run(tool, mode, **env):
    clean = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_')}
    r = subprocess.run([str(tool), mode], env=dict(clean, **env), capture_output=True, text=True, timeout=30)
    assert r.returncode == 0, r.stdout + r.stderr
    return r.stdout.splitlines(), r.stderr


def test_injection_dispatches_after_the_nth_anchor_and_restores_registers(tool):
    out, err = run(tool, 'inject',
                   RECOMP_FE_INJECT='getscreeninfo(story/crib#2=>Story.One(a); Story.Two(b)|mainmenu=>Game.X()')
    assert out[0] == 'handler: Game.X()'                    # the second rule, on the first matching call
    assert out[1] == 'after first: esp=30000 eax=5'
    assert out[2:4] == ['handler: Story.One(a)', 'handler: Story.Two(b)']   # case-insensitive, second crib call
    assert out[4] == 'regs: esp=30000 eax=5 ecx=6 edx=7 ebx=8 esi=9 edi=10 ebp=11'
    assert len(out) == 5                                    # each rule fires once
    assert '[FE-INJECT] 2 rule(s) armed' in err


def test_no_injection_without_the_variable(tool):
    out, err = run(tool, 'inject')
    assert not any(line.startswith('handler') for line in out)
    assert 'FE-INJECT' not in err


def test_crowd_hold_and_reseed(tool):
    out, err = run(tool, 'crowd', RECOMP_TEST_CROWD='hold:3,reseed', RECOMP_TEST_RNG_SEED='99')
    gates = [line for line in out if line.startswith('update')]
    assert gates == ['update 1 gate=1', 'update 2 gate=1', 'update 3 gate=0', 'update 4 gate=0', 'update 5 gate=0']
    assert 'seed: this=4096 high=99 low=24301' in out       # base seed, 0x5EED for the first generator
    assert out[-1] == 'after step gate=1 esp=30000 eax=42'  # gate put back, registers as they were
    assert out.count('seed: this=4096 high=99 low=24301') == 1
    assert '"updates_in_window":5,"held":true,"reseeded":1' in err


def test_crowd_controls_are_off_by_default(tool):
    out, err = run(tool, 'crowd')
    assert all(line.endswith('gate=1') for line in out if line.startswith('update'))
    assert not any(line.startswith('seed') for line in out)
    assert 'TEST-CROWD' not in err

"""The check before EAGL's DynamicLoader::Resolve (src/hooks/dynload_guard.c), compiled natively against a
synthetic loaded object: a sound object passes untouched; a relocation entry of text, a symbol of an
unknown type, a callback that is no function, an offset outside its section, a name outside guest
memory or a damaged other loaded object each refuse the load, which is then taken back as a failed
dlopen leaves it (handle[0] = 0, the object out of the list at 0x3A52A8). Resolve's own early returns
(no object, resolved already) are left to it. Report 5519e0 (build 70) is the case. Needs MSVC (`cl` on
PATH, e.g. after scripts/env.ps1); skips without it."""
import shutil
import subprocess
from pathlib import Path

import pytest

from native_hooks_fixtures import STUB_TYPES

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "dynload_guard.h"

unsigned char memory[0x4000000];
uint32_t g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_ebp, g_esp;
double g_fp_stack[8];
int g_fp_top;
uint16_t g_fp_control_word;

int dynload_code_ok(uint32_t va) { return va == 0x25400u; }

#define H 0x100000u     /* the handle */
#define O 0x200000u     /* the loader object */
#define I 0x300000u     /* the image: its ELF header */
#define S 0x310000u     /* section table */
#define D 0x320000u     /* .data, 0x100 bytes */
#define Y 0x330000u     /* symbol table, 3 symbols */
#define R 0x340000u     /* relocations, 3 entries */
#define T 0x350000u     /* string table */
#define P 0x360000u     /* another loaded object */
#define WILD 0x89D53B60u

static void section(int i, uint32_t type, uint32_t addr, uint32_t size, uint32_t link, uint32_t info)
{
    uint32_t s = S + 0x28u * (uint32_t)i;
    MEM32(s + 4) = type; MEM32(s + 0x10) = addr; MEM32(s + 0x14) = size;
    MEM32(s + 0x18) = link; MEM32(s + 0x1C) = info;
}

static void symbol(int i, uint32_t name, uint32_t value, uint8_t info, uint16_t shndx)
{
    uint32_t y = Y + 16u * (uint32_t)i;
    MEM32(y) = name; MEM32(y + 4) = value; MEM8(y + 0xC) = info; MEM16(y + 0xE) = shndx;
}

static void build(void)
{
    memset(memory, 0, sizeof(memory));
    MEM32(I) = 0x464C457Fu; MEM16(I + 0x30) = 4;
    section(1, 1, D, 0x100, 0, 0);
    section(2, 2, Y, 3 * 16, 3, 0);
    section(3, 9, R, 3 * 8, Y, D);
    symbol(1, 0, 8, 0x01, 1);           /* defined, OBJECT, in .data */
    symbol(2, 1, 0, 0x10, 0);           /* undefined "foo" */
    memcpy(memory + T + 1, "foo", 4);
    MEM32(R) = 0x10; MEM32(R + 4) = (1u << 8) | 2;
    MEM32(R + 8) = 0x20; MEM32(R + 12) = (2u << 8) | 2;
    MEM32(R + 16) = 0xFE; MEM32(R + 20) = (1u << 8) | 6;   /* a 16-bit patch at the last two bytes */
    MEM32(O) = P; MEM32(O + 8) = T; MEM32(O + 0x10) = 3; MEM32(O + 0x14) = Y;
    MEM32(O + 0x18) = S; MEM32(O + 0x1C) = I; MEM32(O + 0x428) = H; MEM32(O + 0x42C) = 0x25400u;
    MEM32(P + 4) = O; MEM32(P + 8) = 0x370300u; MEM32(P + 0x10) = 1; MEM32(P + 0x14) = 0x370000u;
    MEM32(P + 0x420) = 0x370100u; MEM32(P + 0x424) = 0x370200u;
    MEM32(0x3A52A8u) = O;
    MEM32(H) = O; MEM32(H + 0x10) = I; MEM32(H + 0x14) = 0x1000;
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "ok";
    int r;
    build();
    if (!strcmp(c, "text")) { MEM32(R + 8) = 0x6E696174u; MEM32(R + 12) = 0x6E696162u; }  /* "tainbain" */
    else if (!strcmp(c, "type9")) MEM8(Y + 16 + 0xC) = 0x09;
    else if (!strcmp(c, "callback")) MEM32(O + 0x42C) = 0xF68u;
    else if (!strcmp(c, "offset")) MEM32(R) = 0x100;
    else if (!strcmp(c, "reltype")) MEM32(R + 4) = (1u << 8) | 0x2A;
    else if (!strcmp(c, "name")) MEM32(O + 8) = WILD;
    else if (!strcmp(c, "relsection")) section(3, 9, WILD, 3 * 8, Y, D);
    else if (!strcmp(c, "count")) section(3, 9, R, 0xFFFFFFF8u, Y, D);
    else if (!strcmp(c, "other")) MEM32(P + 0x14) = WILD;
    else if (!strcmp(c, "object")) MEM32(H) = WILD;
    else if (!strcmp(c, "resolved")) { MEM8(O + 0xC) = 1; MEM32(O + 0x42C) = 0xF68u; }
    else if (!strcmp(c, "none")) MEM32(H) = 0;
    else if (!strcmp(c, "defined")) { symbol(2, 1, 0, 0x10, 1); MEM32(O + 0x42C) = 0xF68u; }  /* no lookup */
    if (!strcmp(c, "image")) {     /* dlopen's log: a section table past the end, then no ELF header */
        MEM32(I + 0x20) = 0xFF0; MEM16(I + 0x2E) = 0x28;
        dynload_guard_image(H, 0x0010B41Cu, 0x0017F208u);
        MEM32(I) = 0x6E69614Du;
        dynload_guard_image(H, 0x0010B41Cu, 0x0017F208u);
        return 0;
    }
    if (!strcmp(c, "ok")) {         /* a sound header: no line */
        MEM32(I + 0x20) = 0x40; MEM16(I + 0x2E) = 0x28;
        dynload_guard_image(H, 0x0010B41Cu, 0x0017F208u);
    }
    r = dynload_guard_check(H, 0x0010B423u, 0x0017F208u);
    printf("result|%d|%08X|%08X|%08X|%u|%u\n", r, MEM32(H), MEM32(0x3A52A8u), MEM32(P + 4),
           dynload_guard_checked(), dynload_guard_refused());
    return 0;
}
'''


@pytest.fixture(scope='module')
def exe(tmp_path_factory):
    compiler = shutil.which('cl')
    if not compiler:
        pytest.skip('the native fixture needs MSVC')
    folder = tmp_path_factory.mktemp('dynload')
    (folder / 'recomp/gen').mkdir(parents=True)
    (folder / 'recomp/gen/recomp_types.h').write_text(STUB_TYPES)
    (folder / 'host.h').write_text('#include <stdint.h>\n')
    (folder / 'driver.c').write_text(DRIVER, encoding='utf-8')
    out = folder / 'dynload.exe'
    built = subprocess.run([compiler, '/nologo', '/O2', '/TC', '/I' + str(folder), '/I' + str(ROOT / 'src/hooks'),
                            str(folder / 'driver.c'), str(ROOT / 'src/hooks/dynload_guard.c'), '/Fe:' + str(out)],
                           cwd=folder, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    return out


def check(exe, case):
    out = subprocess.run([str(exe), case], capture_output=True, text=True, check=True)
    row = [line.split('|') for line in out.stdout.splitlines() if line.startswith('result|')][0]
    return {'refused': row[1] == '1', 'handle': row[2], 'head': row[3], 'next_prev': row[4],
            'checked': int(row[5]), 'count': int(row[6]), 'log': out.stderr}


def test_sound_object_passes_untouched(exe):
    r = check(exe, 'ok')
    assert not r['refused'] and r['handle'] == '00200000' and r['head'] == '00200000'
    assert r['checked'] == 1 and r['count'] == 0 and r['log'] == ''
    r = check(exe, 'defined')       # every symbol defined: the callback is never asked, so not checked
    assert not r['refused']


@pytest.mark.parametrize('case, why', [
    ('text', 'relocation symbol'),          # report 5519e0: an entry of text, its symbol far outside
    ('type9', 'symbol type'),
    ('callback', 'callback'),               # 0xF68 in the report
    ('offset', 'relocation offset'),
    ('reltype', 'relocation type'),
    ('name', 'symbol name'),
    ('relsection', 'relocation section'),
    ('count', 'relocation section'),        # a negative count: Resolve would loop four billion times
    ('other', 'loaded object'),
])
def test_damaged_object_is_taken_back(exe, case, why):
    r = check(exe, case)
    assert r['refused'] and r['count'] == 1
    assert r['handle'] == '00000000'                         # Resolve returns at once
    assert r['head'] == '00360000' and r['next_prev'] == '00000000'     # out of the loaded-object list
    assert 'Resolve refused a damaged object' in r['log'] and why in r['log']
    assert 'caller 0010B423 (from 0017F208)' in r['log'] and 'image 00300000 (4096 bytes)' in r['log']


def test_object_outside_guest_memory_is_taken_back(exe):
    r = check(exe, 'object')
    assert r['refused'] and r['handle'] == '00000000' and r['head'] == '00200000'   # not in the list: kept
    assert 'object, at 89D53B60' in r['log']


def test_resolves_own_early_returns_left_alone(exe):
    for case in ('resolved', 'none'):
        r = check(exe, case)
        assert not r['refused'] and r['log'] == ''
    assert check(exe, 'resolved')['handle'] == '00200000'


def test_dlopen_logs_a_damaged_image(exe):
    out = subprocess.run([str(exe), 'image'], capture_output=True, text=True, check=True).stderr
    lines = [line for line in out.splitlines() if 'dlopen of a damaged image' in line]
    assert len(lines) == 2
    assert 'a section table past the image' in lines[0] and 'no ELF header' in lines[1]
    assert all('caller 0010B41C (from 0017F208), handle 00100000, image 00300000 (4096 bytes)' in x for x in lines)
    assert '|Main' in out       # the dump shows the bytes as text

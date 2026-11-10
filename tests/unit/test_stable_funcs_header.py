"""An override in src/recomp_manual.c must not change recomp_funcs.h (every chunk includes it)."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('pipeline_state_funcs', ROOT / 'scripts/pipeline-state.py')
state = importlib.util.module_from_spec(spec)
spec.loader.exec_module(state)


def lifted(gen, header_lines, chunks, eol='\r\n'):
    """A synthetic lifter output (no game code): a header and chunks of empty functions."""
    gen.mkdir(parents=True)
    head = ['/**', ' * GAME - Recompiled Function Declarations', ' * %d functions, auto-generated' % len(header_lines),
            ' */', '', '#ifndef RECOMP_FUNCS_H', '#define RECOMP_FUNCS_H', '', '#include "recomp_types.h"', '']
    (gen / 'recomp_funcs.h').write_bytes(eol.join(head + header_lines + ['', '#endif /* RECOMP_FUNCS_H */', ''])
                                         .encode())
    for name, funcs in chunks.items():
        body = ['#define RECOMP_GENERATED_CODE', '#include "recomp_funcs.h"', '#include <math.h>', '']
        for f in funcs:
            body += ['void %s(void)' % f, '{', '    sub_00010000();', '}', '']
        (gen / name).write_bytes(eol.join(body).encode())


def test_an_override_changes_only_the_chunk_that_defines_it(tmp_path):
    before, after = tmp_path / 'before', tmp_path / 'after'
    lifted(before, ['void sub_00010000(void);', 'void sub_00020000(void);', 'void sub_00030000(void);'],
           {'recomp_0000.c': ['sub_00010000'], 'recomp_0001.c': ['sub_00020000', 'sub_00030000']})
    # sub_00020000 now has a hand-written wrapper that keeps the generated body as sub_00020000_gen.
    lifted(after, ['void sub_00010000(void);', 'void sub_00020000_gen(void);', 'void sub_00030000(void);', '',
                   '/* Hand-written overrides (defined by the project) */', 'void sub_00020000(void);  /* 0x00020000 */'],
           {'recomp_0000.c': ['sub_00010000'], 'recomp_0001.c': ['sub_00020000_gen', 'sub_00030000']})
    assert state.stabilize_funcs_header(before) and state.stabilize_funcs_header(after)
    for name in ('recomp_funcs.h', 'recomp_0000.c'):
        assert (before / name).read_bytes() == (after / name).read_bytes(), name
    chunk = (after / 'recomp_0001.c').read_bytes()
    assert b'#include "recomp_funcs.h"\r\n/* Generated bodies' in chunk
    assert b'\r\nvoid sub_00020000_gen(void);\r\n#include <math.h>' in chunk
    header = (after / 'recomp_funcs.h').read_bytes()
    assert b'_gen' not in header and b'\n' not in header.replace(b'\r\n', b'')
    assert header.count(b'void sub_00020000(void);') == 1


def test_an_unexpected_header_is_left_alone(tmp_path):
    gen = tmp_path / 'gen'
    lifted(gen, ['void sub_00010000(void);', 'extern int g_something;'], {'recomp_0000.c': ['sub_00010000']}, eol='\n')
    original = (gen / 'recomp_funcs.h').read_bytes()
    assert not state.stabilize_funcs_header(gen)
    assert (gen / 'recomp_funcs.h').read_bytes() == original


def test_slow_functions_compile_unoptimised_only_without_floating_point(tmp_path):
    gen = tmp_path / 'gen'
    lifted(gen, ['void sub_00010000(void);', 'void sub_00020000(void);'],
           {'recomp_0000.c': ['sub_00010000', 'sub_00020000']})
    chunk = gen / 'recomp_0000.c'
    text = chunk.read_bytes().decode()
    # sub_00020000 does x87 work: it must stay at the build's optimisation level.
    chunk.write_bytes(text.replace('void sub_00020000(void)\r\n{\r\n',
                                   'void sub_00020000(void)\r\n{\r\n    fp_push(1.0);\r\n').encode())
    assert state.unoptimize_slow_functions(gen, ('sub_00010000', 'sub_00020000')) == ['sub_00010000']
    out = chunk.read_bytes().decode()
    assert '\r\n#pragma optimize("", off)\r\nvoid sub_00010000(void)\r\n{' in out
    assert '    sub_00010000();\r\n}\r\n#pragma optimize("", on)\r\n' in out
    assert out.count('#pragma optimize') == 2
    # Running it again (an unchanged re-lift) adds nothing.
    assert state.unoptimize_slow_functions(gen, ('sub_00010000',)) == []
    assert chunk.read_bytes().decode() == out


def test_the_post_process_is_a_lift_input():
    assert state.POSTPROCESS['unoptimized'] == list(state.SLOW_COMPILE)

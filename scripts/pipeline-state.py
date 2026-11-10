#!/usr/bin/env python3
"""Hash the inputs/outputs of analysis, lifting and builds; refuse stale runs.

Manifests and generated code are local, ignored artifacts. Lifting publishes a
complete new directory only after the translator succeeds; its previous output
is retained under logs/lift-backups. No game bytes enter a tracked file.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import time

REPO = Path(__file__).resolve().parents[1]
# The game executable in a build folder: an .exe on Windows only.
EXE_NAME = 'defjam_recomp.exe' if os.name == 'nt' else 'defjam_recomp'
ANALYSIS_FILES = (
    'game_files/default_analysis.json',
    'tools/disasm/output/functions.json', 'tools/disasm/output/labels.json',
    'tools/disasm/output/summary.json', 'tools/func_id/output/identified_functions.json',
    'tools/abi_analysis/output/abi_functions.json',
)


def digest(path):
    with open(path, 'rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def hashes(root, paths):
    return {p.relative_to(root).as_posix(): digest(p) for p in sorted(paths)}


def source_files(root, suffixes):
    return [p for p in root.rglob('*') if p.is_file() and p.suffix in suffixes
            and not {'output', '__pycache__', '.git', 'tests'} & set(p.relative_to(root).parts)]


def analysis_inputs(repo, toolkit):
    return {'xbe': digest(toolkit / 'game_files/default.xbe'),
            'seeds': digest(repo / 'config/seed_functions.json'),
            'tools': hashes(toolkit, source_files(toolkit / 'tools', {'.py'}))}


def analysis_outputs(toolkit):
    paths = [toolkit / p for p in ANALYSIS_FILES]
    for p in paths:
        with p.open(encoding='utf-8') as f:
            json.load(f)  # A partial or corrupt output is never a completed stage.
    return hashes(toolkit, paths)


def write_state(path, state):
    state['recorded'] = time.strftime('%Y-%m-%d %H:%M:%S')
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(state, indent=2) + '\n', encoding='utf-8')
    temp.replace(path)


def read_state(path):
    if not path.is_file():
        raise ValueError(f'No completed pipeline state: {path}')
    return json.loads(path.read_text(encoding='utf-8'))


def begin_stage(path, inputs):
    path.unlink(missing_ok=True)
    write_state(path.with_suffix('.pending.json'), {'inputs': inputs})


def finish_stage_inputs(path, inputs):
    started = read_state(path.with_suffix('.pending.json'))
    if started['inputs'] != inputs:
        raise ValueError('Pipeline inputs changed during the stage; output was not certified')
    return inputs


def verify_analysis(repo, toolkit):
    state = read_state(toolkit / 'game_files/defjam-analysis-state.json')
    if state['inputs'] != analysis_inputs(repo, toolkit) or state['outputs'] != analysis_outputs(toolkit):
        raise ValueError('Analysis is stale; run scripts/analyze.ps1')
    return state


def lift_inputs(repo, toolkit):
    state = verify_analysis(repo, toolkit)
    return {'analysis': {k: state[k] for k in ('inputs', 'outputs')},
            'manual': digest(repo / 'src/recomp_manual.c'),
            'postprocess': POSTPROCESS,
            'runtime_templates': hashes(toolkit, source_files(toolkit / 'templates/runtime', {'.h', '.c'}))}


def generated_outputs(gen):
    return hashes(gen, [p for p in gen.iterdir() if p.suffix in {'.c', '.h'}])


def verify_lift(repo, toolkit):
    gen = repo / 'src/recomp/gen'
    state = read_state(gen / 'lift-state.json')
    if state['inputs'] != lift_inputs(repo, toolkit) or state['outputs'] != generated_outputs(gen):
        raise ValueError('Generated code is stale; run scripts/recomp.ps1')
    return state


PROTO_RE = re.compile(r'^void ([A-Za-z_][A-Za-z0-9_]*)\(void\);(?:\s*/\*.*\*/)?$')
HEADER_SHAPE_RE = re.compile(r'^(?:$|/\*|\*| \*|#ifndef RECOMP_FUNCS_H$|#define RECOMP_FUNCS_H$|'
                             r'#include "recomp_types\.h"$|#endif)')
FUNCS_INCLUDE = '#include "recomp_funcs.h"'


def stabilize_funcs_header(gen):
    """Make recomp_funcs.h independent of which functions src/recomp_manual.c overrides.

    Every generated chunk includes recomp_funcs.h. The lifter writes the function count into
    it, renames an overridden function's prototype to sub_X_gen and lists the overrides in a
    section of their own, so adding or dropping one override in recomp_manual.c changed the
    header and Ninja recompiled all ~90 MB of generated C on every player's update (build 68).
    All prototypes are `void name(void);`, so their order and grouping mean nothing to the
    compiler: the header becomes the sorted set of plain names, which an override does not
    change, and each sub_X_gen prototype moves into the chunks that mention it (they changed
    anyway). An unexpected header shape is left as the lifter wrote it."""
    header = gen / 'recomp_funcs.h'
    if not header.is_file():
        return False
    raw = header.read_bytes().decode('utf-8')     # keep the lifter's line endings
    eol = '\r\n' if '\r\n' in raw else '\n'
    names, gen_names = set(), set()
    for line in raw.splitlines():
        m = PROTO_RE.match(line)
        if m:
            (gen_names if m.group(1).endswith('_gen') else names).add(m.group(1))
        elif not HEADER_SHAPE_RE.match(line):
            print(f'recomp_funcs.h kept as generated (unexpected line: {line[:80]!r})')
            return False
    chunks = {}
    for path in sorted(gen.glob('*.c')):
        text = path.read_bytes().decode('utf-8')
        used = sorted(n for n in gen_names if n in text)
        if not used:
            continue
        chunk_eol = '\r\n' if '\r\n' in text else '\n'
        include = FUNCS_INCLUDE + chunk_eol
        if include not in text:
            print(f'recomp_funcs.h kept as generated ({path.name} does not include it)')
            return False
        chunks[path] = (text, used, include, chunk_eol)
    for path, (text, used, include, chunk_eol) in chunks.items():
        decls = ''.join(f'void {n}(void);{chunk_eol}' for n in used)
        note = '/* Generated bodies kept under another name (scripts/pipeline-state.py) */' + chunk_eol
        path.write_bytes(text.replace(include, include + note + decls, 1).encode('utf-8'))
    out = ['/**',
           ' * Recompiled function declarations, auto-generated by tools/recomp and sorted by',
           ' * scripts/pipeline-state.py: an override in src/recomp_manual.c leaves this file,',
           ' * and so every generated chunk that does not define the function, unchanged.',
           ' */', '', '#ifndef RECOMP_FUNCS_H', '#define RECOMP_FUNCS_H', '',
           '#include "recomp_types.h"', '']
    out += [f'void {n}(void);' for n in sorted(names)]
    out += ['', '#endif /* RECOMP_FUNCS_H */', '']
    header.write_bytes(eol.join(out).encode('utf-8'))
    return True


# Lifted functions on which MSVC's /O2 code generation is pathological (cl /Bt+ /d2cgsummary on
# recomp_0012.c, build 68: sub_001BF030 664 s, sub_001C0000 175 s and sub_001C0014 175 s, one
# straight-line run of ~1500 volatile guest stores each; every other function of the chunk took
# under 2 s, so the chunk was 11 of a player's 12 minutes of update). With optimisation off for
# them alone (#pragma optimize) each compiles in 0.2 s. Allowed only for a body with no floating
# point at all: then every result is integer arithmetic, and every guest memory access is a
# volatile one (MEM8/16/32), which the compiler performs exactly and in order at any level, so the
# fight simulation stays bit for bit the same as an /O2 build's (lockstep/rollback across PCs).
SLOW_COMPILE = ('sub_001BF030', 'sub_001C0000', 'sub_001C0014')
FP_MARKERS = ('fp_', 'float', 'double', 'fpu', 'xmm', '__m128', 'math', 'sqrt')   # matched ignoring case
OPT_OFF, OPT_ON = '#pragma optimize("", off)', '#pragma optimize("", on)'


def unoptimize_slow_functions(gen, names=SLOW_COMPILE):
    """Compile the functions in `names` without optimisation (see SLOW_COMPILE). Returns the
    names wrapped; one that uses floating point, or is not found, is left alone."""
    done = []
    for path in sorted(gen.glob('recomp_0*.c')):
        text = path.read_bytes().decode('utf-8')
        eol = '\r\n' if '\r\n' in text else '\n'
        changed = False
        for name in names:
            for fn in (name, name + '_gen'):
                head = f'{eol}void {fn}(void){eol}{{{eol}'
                start = text.find(head)
                if start < 0:
                    continue
                end = text.find(f'{eol}}}{eol}', start + len(head))
                if end < 0:
                    continue
                end += len(eol) + 1
                body = text[start:end]
                if any(m in body.lower() for m in FP_MARKERS):
                    print(f'{fn} uses floating point; left at full optimisation')
                    continue
                if text[:start].endswith(OPT_OFF):
                    continue
                text = (text[:start] + eol + OPT_OFF + body + eol + OPT_ON + text[end:])
                changed = True
                done.append(fn)
        if changed:
            path.write_bytes(text.encode('utf-8'))
    return done


# Part of the lift's inputs: a change to what the two steps above do re-lifts (once).
POSTPROCESS = {'funcs_header': 1, 'unoptimized': list(SLOW_COMPILE), 'fp_markers': list(FP_MARKERS)}


def postprocess(gen):
    stabilize_funcs_header(gen)
    done = unoptimize_slow_functions(gen)
    if done:
        print('Compiled without optimisation (pathological /O2 build time, integer-only): ' + ', '.join(done))


def lift(repo, toolkit, split, extra):
    owned = ('--gen-dir', '--exclude-manual', '--manual-functions', '--split',
             '--disasm-dir', '--func-id-dir', '--abi-dir', '--functions', '--labels',
             '--identified', '--abi', '--output-dir')
    for token in extra:
        option = token.split('=', 1)[0]
        if option == '-o' or (option.startswith('--') and any(p.startswith(option) for p in owned)):
            raise ValueError(f'Lifter option is controlled by the pipeline: {option}')
    inputs = lift_inputs(repo, toolkit)
    gen = repo / 'src/recomp/gen'
    gen.parent.mkdir(parents=True, exist_ok=True)
    staged = Path(tempfile.mkdtemp(prefix='gen-pending-', dir=gen.parent))
    # Retain failed staging output for diagnosis; never replace the working set.
    cmd = [sys.executable, '-m', 'tools.recomp', 'game_files/default.xbe', '--all',
           '--split', str(split), '--gen-dir', str(staged), '--exclude-manual',
           str(repo / 'src/recomp_manual.c'), *extra]
    subprocess.run(cmd, cwd=toolkit, check=True)
    if not (staged / 'recomp_dispatch.c').is_file() or not list(staged.glob('recomp_0*.c')):
        raise ValueError(f'Lifter did not produce a complete set: {staged}')
    if inputs != lift_inputs(repo, toolkit):
        raise ValueError('Lifter inputs changed during translation; output was not published')
    if (gen / '.gitkeep').is_file():
        (staged / '.gitkeep').write_bytes((gen / '.gitkeep').read_bytes())
    postprocess(staged)
    write_state(staged / 'lift-state.json', {'inputs': inputs, 'outputs': generated_outputs(staged),
                                          'split': split, 'extra_args': extra})
    # A generated file that comes out byte for byte as before keeps its old modification time,
    # so the build recompiles only the files that changed (each is several MB of C).
    if gen.is_dir():
        kept = 0
        for new in staged.iterdir():
            old = gen / new.name
            if new.suffix in {'.c', '.h'} and old.is_file() and old.stat().st_size == new.stat().st_size \
                    and digest(old) == digest(new):
                st = old.stat()
                os.utime(new, ns=(st.st_atime_ns, st.st_mtime_ns))
                kept += 1
        print(f'{kept} generated files unchanged; only the others will be recompiled')
    backup = repo / 'logs/lift-backups' / time.strftime('%Y%m%d-%H%M%S')
    if gen.exists():
        backup.parent.mkdir(parents=True, exist_ok=True)
        gen.rename(backup)
    try:
        staged.rename(gen)
    except OSError:
        if backup.exists():
            backup.rename(gen)
        raise
    print(f'Published {len(list(gen.glob("*.c")))} C files; previous output: {backup}')


def build_inputs(repo, toolkit):
    verify_lift(repo, toolkit)
    paths = source_files(repo / 'src', {'.c', '.cpp', '.m', '.h', '.inc', '.hlsl'})
    paths += [repo / 'CMakeLists.txt', repo / 'CMakePresets.json']
    runtime = source_files(toolkit / 'src', {'.c', '.cpp', '.h', '.inc', '.hlsl', '.txt', '.in'})
    runtime += source_files(toolkit / 'include', {'.h'}) + [toolkit / 'CMakeLists.txt']
    return {'game_sources': hashes(repo, paths), 'runtime': hashes(toolkit, runtime),
            'lift': digest(repo / 'src/recomp/gen/lift-state.json')}


def verify_build(repo, preset):
    if preset == 'ci-runtime-only':
        raise ValueError('Runtime-only presets cannot certify a game executable')
    folder = repo / 'build' / preset
    state = read_state(folder / 'build-state.json')
    toolkit = Path(state['toolkit'])
    verify_game_configuration(folder)
    if state['inputs'] != build_inputs(repo, toolkit) or state['exe'] != digest(folder / EXE_NAME):
        raise ValueError(f'Build {preset} is stale; rebuild it (scripts/build.ps1 -Preset {preset}, '
                         f'or scripts/pipeline.py build --preset {preset})')
    return state


def verify_game_configuration(folder):
    cache = (folder / 'CMakeCache.txt').read_text(encoding='utf-8')
    if 'DEFJAM_BUILD_GAME:BOOL=ON' not in cache.splitlines():
        raise ValueError('Game build was disabled in CMake; rebuild with scripts/build.ps1')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('action', choices=['begin-analysis', 'record-analysis', 'verify-lift', 'lift',
                                    'begin-build', 'record-build', 'verify-build'])
    ap.add_argument('--repo', type=Path, default=REPO)
    ap.add_argument('--toolkit', type=Path)
    ap.add_argument('--preset', default='win-x64-debug')
    ap.add_argument('--split', type=int, default=1000)
    args, extra = ap.parse_known_args(argv)
    repo = args.repo.resolve()
    toolkit = (args.toolkit or repo / 'tools/xboxrecomp').resolve()
    try:
        if args.action in ('begin-build', 'record-build', 'verify-build') and args.preset == 'ci-runtime-only':
            raise ValueError('Runtime-only presets cannot certify a game executable')
        if args.action == 'begin-analysis':
            begin_stage(toolkit / 'game_files/defjam-analysis-state.json', analysis_inputs(repo, toolkit))
        elif args.action == 'record-analysis':
            path = toolkit / 'game_files/defjam-analysis-state.json'
            inputs = finish_stage_inputs(path, analysis_inputs(repo, toolkit))
            write_state(path, {'inputs': inputs, 'outputs': analysis_outputs(toolkit)})
        elif args.action == 'verify-lift':
            verify_lift(repo, toolkit)
        elif args.action == 'lift':
            lift(repo, toolkit, args.split, extra[1:] if extra[:1] == ['--'] else extra)
        elif args.action == 'begin-build':
            begin_stage(repo / 'build' / args.preset / 'build-state.json', build_inputs(repo, toolkit))
        elif args.action == 'record-build':
            folder = repo / 'build' / args.preset
            verify_game_configuration(folder)
            inputs = finish_stage_inputs(folder / 'build-state.json', build_inputs(repo, toolkit))
            write_state(folder / 'build-state.json', {'toolkit': str(toolkit),
                        'inputs': inputs, 'exe': digest(folder / EXE_NAME)})
        else:
            verify_build(repo, args.preset)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as ex:
        print(f'Pipeline failed: {ex}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

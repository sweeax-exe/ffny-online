#!/usr/bin/env python3
"""Check that every function src/recomp_manual.c names is still a function the lifter knows.

    python scripts/check-manual-addresses.py [--toolkit tools/xboxrecomp]

recomp_manual.c replaces generated functions (`void sub_XXXXXXXX(void)`), hooks their
entries (`void sub_XXXXXXXX_enter(void)`) and calls generated ones (`extern void
sub_XXXXXXXX(void)`). The lifter's function list comes from the toolkit's discovery,
which a toolkit update can change: v0.16 found 530 functions v0.13 did not and dropped
340. A replacement or an extern whose address is no longer a function start fails at
link time; an entry hook does not fail at all -- the generated code simply never calls
it, and whatever it fed (a scenario's game-state probe, the replay hash) goes quiet.
So every address is checked against the disassembler's function list after a lift.

Exit 0 when every address is a function start, 1 otherwise (each one is named).
"""
import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DEFINITION = re.compile(r'^\s*(?:static\s+)?void\s+sub_([0-9A-Fa-f]{8})(_enter)?\s*\(\s*void\s*\)', re.M)
EXTERN = re.compile(r'\bextern\s+void\s+sub_([0-9A-Fa-f]{8})\s*\(\s*void\s*\)')


COMMENT = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)


def manual_addresses(text):
    """{address: kind} for every definition, entry hook and extern in the source
    (comments, which carry examples, are left out)."""
    text = COMMENT.sub(lambda m: '\n' * m.group(0).count('\n'), text)
    found = {}
    for m in DEFINITION.finditer(text):
        found.setdefault(int(m.group(1), 16), 'entry hook' if m.group(2) else 'replacement')
    for m in EXTERN.finditer(text):
        found.setdefault(int(m.group(1), 16), 'extern')
    return found


def function_starts(path):
    starts = set()
    for f in json.loads(Path(path).read_text(encoding='utf-8')):
        start = f.get('start') if isinstance(f, dict) else f
        starts.add(int(start, 16) if isinstance(start, str) else int(start))
    return starts


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--toolkit', type=Path, default=REPO / 'tools/xboxrecomp')
    ap.add_argument('--manual', type=Path, default=REPO / 'src/recomp_manual.c')
    args = ap.parse_args(argv)
    functions = args.toolkit / 'tools/disasm/output/functions.json'
    if not functions.exists():
        sys.exit(f'{functions} is missing: run "pipeline.py analyze" first')
    starts = function_starts(functions)
    wanted = manual_addresses(args.manual.read_text(encoding='utf-8'))
    missing = sorted(a for a in wanted if a not in starts)
    for a in missing:
        print(f'recomp_manual.c: sub_{a:08X} ({wanted[a]}) is not a function start in this analysis')
    print(f'{len(wanted)} manual addresses checked, {len(missing)} missing')
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())

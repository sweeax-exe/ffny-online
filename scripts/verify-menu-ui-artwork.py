#!/usr/bin/env python3
"""Read-only integrity check for the local approved menu artwork; never exports/installs.

python scripts/verify-menu-ui-artwork.py --art-root /private/path/pilot-v1
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def verify(catalogue, art_root):
    root = Path(art_root).resolve()
    if catalogue.get('schema') != 1:
        raise ValueError('Unsupported artwork catalogue schema')
    expected = {}
    for scope in catalogue['approval_records']:
        for item in scope['files']:
            name, digest = item['path'], item['sha256']
            if not re.fullmatch(r'[0-9a-f]{64}', digest):
                raise ValueError('Invalid SHA-256: ' + name)
            path = Path(name)
            if path.is_absolute() or '..' in path.parts or not (root/path).resolve().is_relative_to(root):
                raise ValueError('Artwork path escapes private root: ' + name)
            if name in expected and expected[name] != digest:
                raise ValueError('Conflicting artwork pins: ' + name)
            expected[name] = digest
    problems = []
    for name, digest in sorted(expected.items()):
        path = root/name
        try:
            with path.open('rb') as stream:
                observed = hashlib.file_digest(stream, 'sha256').hexdigest()
        except OSError as exc:
            problems.append(name + ': ' + str(exc))
            continue
        if observed != digest:
            problems.append(name + ': SHA-256 mismatch')
    return len(expected), problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--art-root', type=Path, required=True)
    parser.add_argument('--catalogue', type=Path, default=Path(__file__).resolve().parents[1]/'config/menu-ui-approved-artwork.json')
    args = parser.parse_args()
    count, problems = verify(json.loads(args.catalogue.read_text()), args.art_root)
    for problem in problems:
        print(problem)
    print(f'{count} approved/conditional file pins; {len(problems)} integrity failures. Not game acceptance.')
    return bool(problems)


if __name__ == '__main__':
    raise SystemExit(main())

#!/usr/bin/env python3
"""Check tracked parent-repository files before CI or a source release.

Does not traverse the toolkit gitlink or ignored local game/build files.
Run: python scripts/check-source-tree.py [--repo PATH]
"""
import argparse
import hashlib
import re
import subprocess
from pathlib import Path, PurePosixPath

BLOCKED_EXTENSIONS = {
    '.xbe', '.iso', '.xiso', '.viv', '.mad', '.xsh', '.7z', '.zip', '.rar',
    '.exe', '.dll', '.pdb', '.obj', '.o', '.lib', '.a', '.so', '.dmp',
    '.bmp', '.png', '.gif', '.jpg', '.jpeg', '.ppm', '.pcm', '.wav', '.mp3', '.mp4', '.log',
}
BLOCKED_ROOTS = {'game', 'assets', 'extracted', 'logs', 'build', 'out', 'save', 'private'}
# D58: the owner explicitly approved these two README visuals for publication.
# Exact hashes prevent the exception from admitting other captures or game data.
APPROVED_README_MEDIA = {
    'docs/media/fight-screenshot.png': '9804400a61948f0d562809cf46f3da09a2aa310f4b1b37f98c167b92c8fad434',
    'docs/media/fight-gameplay.gif': '77012147d4fe73227222c988610dba82510df1abad4f2405f1164935920b4277',
    # 2026-10-09, at the owner's request: the port's own setup wizard and launcher. No game artwork.
    'docs/media/setup-wizard.png': '14133e1592e34308aceeffa35366cf9ed6515c82346e8e4a7b85d2dd11d74bb1',
    'docs/media/launcher.png': 'ffb73dfd833f6bbe4b059b57ed0969eaaad74d2b4e5a3aa7070253a44b2a5dc8',
}


def path_problem(name):
    if name in APPROVED_README_MEDIA:
        return None
    path = PurePosixPath(name)
    if path.parts[0] in BLOCKED_ROOTS:
        return 'local game/build/evidence directory'
    if name.startswith('src/recomp/gen/') and name != 'src/recomp/gen/.gitkeep':
        return 'generated game source'
    if path.suffix.lower() in BLOCKED_EXTENSIONS:
        return 'binary, asset, capture or archive extension'
    return None


def text_problem(data, name):
    if name in APPROVED_README_MEDIA:
        if hashlib.sha256(data).hexdigest() != APPROVED_README_MEDIA[name]:
            return 'approved README media changed; requires owner review and fingerprint update'
        return None
    if b'\0' in data:
        return 'binary content'
    try:
        text = data.decode('utf-8-sig')
    except UnicodeDecodeError:
        return 'non-UTF-8 content'
    if name.endswith('.md'):
        for language, block in re.findall(r'^[ \t]*```([^\n]*)\n(.*?)^[ \t]*```', text, re.M | re.S):
            if language.strip().lower() in {'asm', 'assembly', 'nasm'}:
                return 'disassembly in Markdown; summarize behavior instead'
            if re.search(r'\bsub_[0-9A-Fa-f]{8}\s*\(', block) and re.search(
                    r'\b(?:MEM(?:8|16|32|64)|PUSH32|POP32|eax|esp|_fv)\b', block):
                return 'guest-function body in Markdown; summarize behavior instead'
    return None


def check(repo):
    entries = subprocess.check_output(
        ['git', 'ls-files', '--stage', '-z'], cwd=repo).split(b'\0')
    problems = []
    count = 0
    for entry in entries:
        if not entry:
            continue
        metadata, raw_name = entry.split(b'\t', 1)
        mode, _, stage = metadata.decode().split()
        name = raw_name.decode('utf-8')
        if stage != '0':
            problems.append((name, 'unmerged index entry'))
            continue
        if mode == '160000':
            if name != 'tools/xboxrecomp':
                problems.append((name, 'unexpected submodule'))
            continue
        if mode not in {'100644', '100755'}:
            problems.append((name, 'unsupported file mode'))
            continue
        count += 1
        problem = path_problem(name)
        if not problem:
            try:
                problem = text_problem((repo / name).read_bytes(), name)
            except OSError:
                problem = 'tracked file unavailable'
        if problem:
            problems.append((name, problem))
    return count, problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path,
                        default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    count, problems = check(args.repo)
    for name, reason in problems:
        print(f'{name}: {reason}')
    print(f'{count} tracked source files; {len(problems)} publication-policy failures')
    return bool(problems)


if __name__ == '__main__':
    raise SystemExit(main())

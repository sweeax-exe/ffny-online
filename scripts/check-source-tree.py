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
    'docs/media/setup-wizard.png': '0a9ee25b0e97751972cba83eea0b1b93a26ec12463c91f712d2fd9ef28b4b7ff',
    'docs/media/launcher.png': '50a18097a6d4724dd955937d698a51cdbdab9343f186afe73493791319e9708e',
    # 2026-10-20, FFNY Online's owner, for the README and the release post: the online screens
    # (main menu entry, hub, room, fighter grid, venue vote) and an online fight, captured from the
    # game's own frames; the setup wizard and launcher pictures were retaken for FFNY Online.
    'docs/media/online-menu.png': 'd25b23b9d4fda22438089ff01dcd64b313f16f5df5db47277bf92de39d835639',
    'docs/media/online-hub.png': '520d38027c22bed874e2649b124f2931a362422c275dac99e22244e2eefca0ed',
    'docs/media/online-room.png': 'acc3add318ab278772d445042d6044da1f2dfe0ed48ef8c693cbc8c8aff6676a',
    'docs/media/online-fighters.png': '6ff224a9e64b317be2166c9a086b6c087652e7ea2e906292c415a1ba0b15125f',
    'docs/media/online-vote.png': '2a86d860bf5a202cc68e94ac2cafdd4d2913a0846781fdb17ec353afe6e85279',
    'docs/media/online-fight.png': '17c30e63e490fd10e1f071d2b6a4390f98855936bb809c2450974ad3a8785a88',
}
# D98 (2026-10-10): the owner approved committing the application icons and the Steam artwork
# rendered from the game's own textures (the shield, the title wordmark, the title-screen scene).
# The whole set is pinned by the SHA-256 of branding/SHA256SUMS, which lists every other file in
# branding/ and is written by scripts/make-branding.py. Changing any file therefore needs the
# owner to approve a new pin here.
BRANDING_SUMS = 'branding/SHA256SUMS'
BRANDING_SUMS_SHA256 = 'c8d6048985f1bda2f167f008ec0c278713b6da43eb1f7c46e9560af8f21a1af7'


def approved_branding(repo):
    """{path: sha256} for branding/ when its list still has the approved fingerprint, else {}."""
    try:
        data = (repo / BRANDING_SUMS).read_bytes()
    except OSError:
        return {}
    if hashlib.sha256(data).hexdigest() != BRANDING_SUMS_SHA256:
        return {}
    approved = {}
    for line in data.decode('utf-8').splitlines():
        digest, _, name = line.partition('  ')
        approved['branding/' + name] = digest
    return approved


def path_problem(name, approved=None):
    if name in APPROVED_README_MEDIA or name in (approved or {}):
        return None
    path = PurePosixPath(name)
    if path.parts[0] in BLOCKED_ROOTS:
        return 'local game/build/evidence directory'
    if name.startswith('src/recomp/gen/') and name != 'src/recomp/gen/.gitkeep':
        return 'generated game source'
    if path.suffix.lower() in BLOCKED_EXTENSIONS:
        return 'binary, asset, capture or archive extension'
    return None


def text_problem(data, name, approved=None):
    expected = APPROVED_README_MEDIA.get(name) or (approved or {}).get(name)
    if expected:
        if hashlib.sha256(data).hexdigest() != expected:
            return 'approved media changed; requires owner review and fingerprint update'
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
    approved = approved_branding(repo)
    tracked = set()
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
        tracked.add(name)
        problem = path_problem(name, approved)
        if not problem:
            try:
                problem = text_problem((repo / name).read_bytes(), name, approved)
            except OSError:
                problem = 'tracked file unavailable'
        if problem:
            problems.append((name, problem))
    if BRANDING_SUMS in tracked and not approved:
        problems.append((BRANDING_SUMS, 'branding fingerprint list changed; requires owner review and a new pin'))
    for name in sorted(set(approved) - tracked):
        problems.append((name, 'approved branding file is not tracked'))
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

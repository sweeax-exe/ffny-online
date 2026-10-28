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
    'docs/media/setup-wizard.png': '30ef7c4f396aa2383b234d02cd85c25c7bdf84e3f2b141c4eac986220b5560c7',
    'docs/media/launcher.png': 'fab5f3f75768532324b0e577084420eab80c5db3406af6c98cfdd30ce00e9d25',
    # 2026-10-20, FFNY Online's owner, for the README and the release post: the online screens
    # (main menu entry, hub, room, fighter grid, venue vote) and an online fight, captured from the
    # game's own frames; the setup wizard and launcher pictures were retaken for FFNY Online.
    'docs/media/online-menu.png': 'd25b23b9d4fda22438089ff01dcd64b313f16f5df5db47277bf92de39d835639',
    'docs/media/online-hub.png': '3380e552d96006aed97363e22ac6671fd90cc3185522765fbcfedd32f099ec08',
    'docs/media/online-room.png': 'abac899abc46008ebdf1bb9d3535cd6198ea2f33e776522b4ee37ca4ba09f9e3',
    'docs/media/online-fighters.png': '7bb86b9687aa9f3e972e59074c131e08327378481d0a02532a2fc0c5ef95c706',
    'docs/media/online-vote.png': 'c63d5be4073df649d3b86dd79a5c74c50464d84f5ae5eb3301f4680c17c800e0',
    'docs/media/online-fight.png': '17c30e63e490fd10e1f071d2b6a4390f98855936bb809c2450974ad3a8785a88',
    # 2026-10-21, build 13: MY STATS and PLAYERS ONLINE from the game's own frames (test accounts),
    # and the launcher retaken with the FFNY Online logo.
    'docs/media/online-stats.png': '81324f692b0077c85445a29f2a6f4386b3f42e91f73cd2921478f08cd4cfdb5c',
    'docs/media/online-players.png': '8083b9ea8a68e87f318f9eb627dd7f4e095c597939715cf9c2c210402fe8d3c6',
    # 2026-10-21, FFNY Online's owner: the project's own logo (made for the project, no game artwork
    # and no EA or Def Jam marks), for the launcher, the README, the Discord and the web pages.
    'ffny-branding/logo.png': '33888212e2e025e40bfe2b570f340edadb619368d0a976d1f6e45488f0d3a799',
    'ffny-branding/logo-512.png': '597f0c9d3e02b1a94e665337ccedf9e58b6b0c33ac57a001d003f5711774cb9b',
    'ffny-branding/icon-1024.png': '130658498e88dc1cb5e6f781bb08cb2dfbc9ca131aee09692ec2a976cf04d14b',
    'ffny-branding/icon-256.png': 'f640abfadaf49f3da32b726f795b4300df12aab60ce5fd27650dfdbebfc6536d',
    'ffny-branding/ffny.ico': '884d182c92b5d8269dfeaefcee68e8bbb8ff0e7fba82154cf04f59fb5739e46b',
    # 2026-10-24, FFNY Online's owner, for the build 24 launcher: its background (a close-up from the
    # game's own intro, graded, with a red rim light and smoke) and a blurred copy for the glass panels.
    'ffny-branding/launcher-bg.jpg': '919760df1e6d599ee6b019d2d3c387a74d0615e1786d588e5ab0e26c5ff97225',
    'ffny-branding/launcher-bg-blur.jpg': '483cfd22c413736321f35152470352e9f935e4f55fda88082075af1fc3c8c559',
    # 2026-10-24, chosen by FFNY Online's owner: the Anton display font (SIL Open Font License 1.1, its
    # licence beside it in OFL.txt), from google/fonts, for the launcher's headings and PLAY.
    'ffny-branding/fonts/Anton-Regular.ttf': 'a4ba3a92350ebb031da0cb47630ac49eb265082ca1bc0450442f4a83ab947cab',

    # 2026-10-13: owner requested curated PR22 media from the Mac capture set.
    'docs/media/gameplay-hero.png': 'f0f9f8524913594e7b0d676bbe241c5733f6f4fd4eba2206c4f593638e0814e4',
    'docs/media/gameplay-preview-compact.gif': '88b8c8747d6b3936203191aca261ed0ca85108e2ae4bb914f0b0773495f641ec',
    'docs/media/textures-comparison.png': 'bfb4bfdd75b28268cf2cea5638f1fe6962e9ac2e30f2b275dc800c13a8fd9e8a',
    'docs/media/widescreen-comparison.png': 'dd8b23a051e8ef98516ea6aa349ef450f441e58669efc67a56fb2273dfbeaf67',
    # D144 (2026-10-20): owner requested current Home/Advanced screenshots in README and docs
    # (FFNY Online keeps its own launcher.png above).
    'docs/media/launcher-advanced.png': '2b314e1d96c797a56e475b2d18cae0d502e5a6c944b07af2c8307bbd0dc3060a',
    'docs/media/overlay.png': 'a137a13fa65cbbfc765897aa9d51d3ef84523e5cabd0fe934f6e477f446ca09b',
    'docs/media/hd-16x9-gameplay.mp4': '385b041f57b7ddd61feb99e0ce726f6e4a5856f1e9936c416aca81949ee428f0',
    'docs/media/textures-comparison.mp4': '0659c294460c00fafd69552a2f4fb0c70d161b5d6206e78c902cdd4ca3a03383',
    'docs/media/widescreen-comparison.mp4': '2916fc4efd72b6d109d829157c01479c9ef50d84c5bbb94437d3fd87060f7d2c',
    # D130 (2026-10-17): the owner's own screenshot of EA's developer menu (text only) for docs/developer-menu.md.
    'docs/media/developer-menu.png': '86a3a91ae252ff9692003e3b88ee8bbc68af176ab74bb7fa0db4e4fd5add9077',
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

#!/usr/bin/env python3
"""Check tracked parent-repository files before CI or a source release.

Does not traverse the toolkit gitlink or ignored local game/build files.
Run: python scripts/check-source-tree.py [--repo PATH]
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path, PurePosixPath

BLOCKED_EXTENSIONS = {
    '.xbe', '.iso', '.xiso', '.viv', '.mad', '.xsh', '.7z', '.zip', '.rar',
    '.exe', '.dll', '.pdb', '.obj', '.o', '.lib', '.a', '.so', '.dmp',
    '.bmp', '.png', '.gif', '.jpg', '.jpeg', '.ppm', '.pcm', '.wav', '.mp3', '.mp4', '.log',
}
BLOCKED_ROOTS = {'game', 'assets', 'extracted', 'logs', 'build', 'out', 'save', 'private', 'authoring', 'workspaces', 'candidates'}
# Owner-approved media retained in the current tree (D58 and subsequent decisions).
# The superseded October4 pair was retired in the approved documentation cleanup.
# Exact hashes prevent the exception from admitting other captures or game data.
APPROVED_README_MEDIA = {
    # 2026-10-09, at the owner's request: the port's own setup wizard and launcher. No game artwork.

    # 2026-10-13: owner requested curated PR22 media from the Mac capture set.
    # D144 (2026-10-20): owner requested current Home/Advanced screenshots in README and docs.
    'docs/media/launcher.png': '61be58a7a76224e0ac5f8d42689c715294d323b2f1ed559491f019a93a2ef693',
    'docs/media/launcher-advanced.png': '2b314e1d96c797a56e475b2d18cae0d502e5a6c944b07af2c8307bbd0dc3060a',
    # D130 (2026-10-17): the owner's own screenshot of EA's developer menu (text only) for docs/developer-menu.md.
    'docs/media/developer-menu.png': '86a3a91ae252ff9692003e3b88ee8bbc68af176ab74bb7fa0db4e4fd5add9077',
    # 2026-11-05: the owner approved the project site's before/after pairs and the app's screens, captured on
    # macOS from the v1.0.0 candidate (docs/media/README.md).
    # 2026-11-05: the owner approved the install guide's setup screens, with neutral example paths.
    'docs/media/install-step-1.png': 'e8a2142a5d05b5929afb3521ae0ec68b3fecf8d5f3aa1567fe75b802d36b7a59',
    'docs/media/install-step-3.png': 'eb2f4f9319a32f16e3f25dd67519c7f0ce6bcb31cc3644a31ec68f0cadd8597b',
    'docs/media/install-progress.png': '120963deb850abb186c8b4d4493c1aa8424288c94faf0c22a37bee15614ef965',
    # 2026-11-05: the owner supplied the project site's trailer; this is its silent 720p web copy.
    'docs/media/site-trailer.mp4': '62a09bae660913b6fb849a969fbe86e144c2ab0759bb6093025e1bed1c5b1df8',
    'docs/media/site-trailer-poster.jpg': '8d9f3ddf7f2d392160490a109d5b05f8d9c5062e7064be90d0e00368ceab934d',
    # 2026-11-05: the owner asked for these in the README; composed from the approved compare-* pairs.
    'docs/media/readme-compare-foundation.jpg': 'a9972d7c8d7a7f9cea92ad32294a585bdbc2d774b9979a959d40d460134def19',
    'docs/media/readme-compare-fight.jpg': 'c049c6a25abfbca6bef630b0edeccc658bb770596a284f872ba3d90f14e8e70e',
    'docs/media/readme-compare-menu.jpg': '1804ed0d2747e40d9fb923abc90e1d132ac228321719a619dcdb4ac8280c5a5d',
    'docs/media/compare-cutscene-before.png': '7a1d9a8cfd511e6a507b4a50ff325d3afedebf3d18b0450aa17835193693cd5e',
    'docs/media/compare-cutscene-after.jpg': '7c02c32fcd679bff421db6270b6fc07b1f4080ba09d333a6087eab2f2537d196',
    'docs/media/compare-fight-before.png': 'def518e6e2e8edeed541f8f97555aae63fe91879aa2ce9757042f489ec1a058c',
    'docs/media/compare-fight-after.jpg': 'a039bb965d18ec5bb2cbb34a882121b5855d2cfaeab8106085c70a7539eaf186',
    'docs/media/compare-menu-before.png': 'c77e8071aaab91906b8b63ff815624d63c25f3a6427df2a7e9ffe19cdb66336e',
    'docs/media/compare-menu-after.jpg': '83ddf5179df2a8725a81952376c535e8dadcbc3926f2969e33cc02a07bc5d974',
    'docs/media/compare-map-before.png': '3d07e21f4ee3851a270ad74290b9e80430c7d7f92df4a2bf93cd01753c51f624',
    'docs/media/compare-map-after.jpg': '0971a0995a332f4b75b1d32b69f2f5b731c90b9bbc69f62278ef4815f15540f8',
    'docs/media/app-home.png': '018ca077d45b2cdaee111c4c2a576eafb4bd5f6e9d658845ce6a34bdf83e2cbe',
    'docs/media/app-first-run.png': '72a941c2f8f36650da1ae192754f1c6d425fae87ddffbcf799b6b93fc1f9fb44',
    'docs/media/app-mods-browse.png': '62ce3debd8fa090920cb5b55a6906b7b6268dff4acc71483190513f3ca796ab0',
}
# D98 (2026-10-10): the owner approved committing the application icons and the Steam artwork
# rendered from the game's own textures (the shield, the title wordmark, the title-screen scene).
# The whole set is pinned by the SHA-256 of branding/SHA256SUMS, which lists every other file in
# branding/ and is written by scripts/make-branding.py. Changing any file therefore needs the
# owner to approve a new pin here.
# 2026-11-05: owner approved the HD city/title refresh and retained the original Impact subtitle.
BRANDING_SUMS = 'branding/SHA256SUMS'
BRANDING_SUMS_SHA256 = '0e46ab19f5ec6afcc30574797b95ecd66d0962d19522e95743eaf47d188ddfcb'


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
    if name.startswith('config/menu-ui-') or name.endswith(('.local.json', '.local.md')):
        return 'private authoring record or local machine configuration'
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
    if name.startswith('config/') and name.endswith('.json'):
        try:
            record = json.loads(text)
        except ValueError:
            record = None
        if isinstance(record, dict):
            keys = set(record)
            production_fields = (
                {'approval_records', 'reviewed_sources'},
                {'recovery_path', 'manifest_sha256'},
                {'owner_message', 'approved_records'},
                {'generation_records', 'selected_exports'},
            )
            if any(fields <= keys for fields in production_fields):
                return 'production authoring/recovery ledger belongs in the private workspace'
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

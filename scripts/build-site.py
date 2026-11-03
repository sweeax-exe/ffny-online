#!/usr/bin/env python3
"""Assemble the GitHub Pages site from `site/` and the approved media.

    python scripts/build-site.py [--out build/site] [--releases releases.json]
    python -m http.server --directory build/site 8000      # preview it

`site/` holds only text (HTML, CSS, JS). The pictures the page shows are copied in here from
the files the owner already approved for publication: the README media pinned in
`scripts/check-source-tree.py` (D58, D144) and the branding set pinned by `branding/SHA256SUMS`
(D98). Each copy is checked against its pinned SHA-256 first, so the site cannot publish a file
the tree check would not admit. The game artwork stays the property of its rights holders.

`--releases` copies a GitHub releases listing (`gh api repos/<repo>/releases`) next to the
page; `site/app.js` reads it when the live API is unavailable.
"""
import argparse
import hashlib
import importlib.util
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Published name under media/ -> approved source in the repository.
MEDIA = {
    'hero.png': 'branding/steam/hero.png',
    'skyline.png': 'branding/masters/skyline.png',
    'logo-fan.png': 'branding/fan/steam/logo.png',
    'social-preview.jpg': 'branding/fan/github/social-preview-1280x640.jpg',
    'icon-64.png': 'branding/icon/png/defjam-64.png',
    'icon-256.png': 'branding/icon/png/defjam-256.png',
    'gameplay-hero.png': 'docs/media/gameplay-hero.png',
    'hd-16x9-gameplay.mp4': 'docs/media/hd-16x9-gameplay.mp4',
    'textures-comparison.png': 'docs/media/textures-comparison.png',
    'widescreen-comparison.png': 'docs/media/widescreen-comparison.png',
    'launcher.png': 'docs/media/launcher.png',
    'launcher-advanced.png': 'docs/media/launcher-advanced.png',
    'overlay.png': 'docs/media/overlay.png',
    'developer-menu.png': 'docs/media/developer-menu.png',
}


def load_tree_check(repo):
    spec = importlib.util.spec_from_file_location('site_source_tree', repo / 'scripts/check-source-tree.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def approved_media(repo):
    """{path: sha256} of every file the tree check admits as published media."""
    tree = load_tree_check(repo)
    approved = dict(tree.APPROVED_README_MEDIA)
    approved.update(tree.approved_branding(repo))
    return approved


def build(repo, out, releases=None):
    """Write the site to `out`; returns a list of problems (empty when the build is good)."""
    approved = approved_media(repo)
    problems = []
    for name, source in MEDIA.items():
        expected = approved.get(source)
        if not expected:
            problems.append('%s: not an approved media file' % source)
            continue
        path = repo / source
        if not path.is_file():
            problems.append('%s: missing' % source)
        elif hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            problems.append('%s: does not match its approved fingerprint' % source)
    if problems:
        return problems

    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(repo / 'site', out, ignore=shutil.ignore_patterns('README.md'))
    (out / 'media').mkdir()
    for name, source in MEDIA.items():
        shutil.copyfile(repo / source, out / 'media' / name)
    if releases:
        shutil.copyfile(releases, out / 'releases.json')
    # Serve files as they are; GitHub Pages would otherwise run Jekyll over them.
    (out / '.nojekyll').write_text('', encoding='utf-8')
    return []


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--repo', type=Path, default=ROOT)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/site')
    parser.add_argument('--releases', type=Path, help='GitHub releases JSON to publish as releases.json')
    args = parser.parse_args()
    problems = build(args.repo, args.out, args.releases)
    for problem in problems:
        print(problem, file=sys.stderr)
    if not problems:
        print('site written to %s (%d media files)' % (args.out, len(MEDIA)))
    return 1 if problems else 0


if __name__ == '__main__':
    raise SystemExit(main())

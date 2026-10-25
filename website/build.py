#!/usr/bin/env python3
"""Build the defjam.online start page into website/dist/.

The page itself (index.html, style.css, site.js) lives in website/. The pictures, the logo and the
font are not copied into the repository a second time: this script takes them from docs/media/ and
ffny-branding/ and puts them next to the page in dist/. It copies only files that
scripts/check-source-tree.py lists as owner-approved, and only while their SHA-256 still matches,
so the site can never carry a picture the owner has not approved.

Run: python website/build.py [--out DIR]
Then upload the contents of DIR (default website/dist/) to /var/www/defjam.online on the server.
"""
import argparse
import hashlib
import importlib.util
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

# Page files, written with a short content hash on their CSS/JS references so a new upload is
# never mixed with an old cached copy.
PAGE = ['index.html', 'style.css', 'site.js']

# dist path -> repository path. Every image must be in check-source-tree.py's approved list.
ASSETS = {
    'img/logo-512.png': 'ffny-branding/logo-512.png',
    'img/icon-256.png': 'ffny-branding/icon-256.png',
    'favicon.ico': 'ffny-branding/ffny.ico',
    'img/launcher-bg.jpg': 'ffny-branding/launcher-bg.jpg',
    'img/launcher-bg-blur.jpg': 'ffny-branding/launcher-bg-blur.jpg',
    'media/online-menu.png': 'docs/media/online-menu.png',
    'media/online-hub.png': 'docs/media/online-hub.png',
    'media/online-fighters.png': 'docs/media/online-fighters.png',
    'media/online-vote.png': 'docs/media/online-vote.png',
    'media/online-fight.png': 'docs/media/online-fight.png',
    'media/online-stats.png': 'docs/media/online-stats.png',
    'media/launcher.png': 'docs/media/launcher.png',
    'media/setup-wizard.png': 'docs/media/setup-wizard.png',
    'media/fight-screenshot.png': 'docs/media/fight-screenshot.png',
    'media/fight-gameplay.gif': 'docs/media/fight-gameplay.gif',
    'fonts/Anton-Regular.ttf': 'ffny-branding/fonts/Anton-Regular.ttf',
}
# Text files that travel with an asset (the font's licence must ship with the font).
LICENCES = {
    'fonts/OFL.txt': 'ffny-branding/fonts/OFL.txt',
}

# A local reference in the page: src/href/poster/data-full/content="..." that is not a URL.
LOCAL_REF = re.compile(r'''(?:src|href|poster|data-full)\s*=\s*"([^"#:]+?)(?:\?[^"]*)?"''')
CSS_URL = re.compile(r'''url\(\s*['"]?([^'")#:]+?)(?:\?[^'")]*)?['"]?\s*\)''')


def load_approved():
    spec = importlib.util.spec_from_file_location('website_source_tree', ROOT / 'scripts/check-source-tree.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.APPROVED_README_MEDIA


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def check_assets(approved):
    problems = []
    for dest, src in ASSETS.items():
        expected = approved.get(src)
        if expected is None:
            problems.append(f'{src}: not in the approved media of scripts/check-source-tree.py')
        elif sha256(ROOT / src) != expected:
            problems.append(f'{src}: changed since the owner approved it')
    if not (ROOT / LICENCES['fonts/OFL.txt']).is_file():
        problems.append('ffny-branding/fonts/OFL.txt: the font licence is missing')
    return problems


def local_refs(out):
    """Every local file the built page, stylesheet and script refer to."""
    refs = set()
    html = (out / 'index.html').read_text(encoding='utf-8')
    refs.update(LOCAL_REF.findall(html))
    refs.update(CSS_URL.findall((out / 'style.css').read_text(encoding='utf-8')))
    refs.update(CSS_URL.findall(html))
    return {r.lstrip('./') for r in refs if r and not r.startswith(('//', 'mailto'))}


def build(out):
    approved = load_approved()
    problems = check_assets(approved)
    if problems:
        raise SystemExit('website build stopped:\n  ' + '\n  '.join(problems))
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for dest, src in {**ASSETS, **LICENCES}.items():
        target = out / dest
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / src, target)
    versions = {name: sha256(HERE / name)[:10] for name in ('style.css', 'site.js')}
    for name in PAGE:
        text = (HERE / name).read_text(encoding='utf-8')
        if name == 'index.html':
            for ref, version in versions.items():
                text = text.replace(f'"{ref}"', f'"{ref}?v={version}"')
        (out / name).write_text(text, encoding='utf-8', newline='\n')
    missing = sorted(r for r in local_refs(out) if not (out / r).is_file())
    if missing:
        raise SystemExit('website build stopped: the page refers to missing files: ' + ', '.join(missing))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', type=Path, default=HERE / 'dist')
    args = parser.parse_args()
    out = build(args.out.resolve())
    total = sum(p.stat().st_size for p in out.rglob('*') if p.is_file())
    print(f'built {out} ({sum(1 for p in out.rglob("*") if p.is_file())} files, {total / 1e6:.1f} MB)')
    return 0


if __name__ == '__main__':
    sys.exit(main())

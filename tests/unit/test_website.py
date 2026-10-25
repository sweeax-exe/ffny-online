"""The defjam.online start page (website/): it builds from the approved pictures only, loads nothing
from other sites but the GitHub API, and its download button works without JavaScript."""
import hashlib
import importlib.util
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


site = load('website_build', ROOT / 'website/build.py')
tree = load('website_source_tree', ROOT / 'scripts/check-source-tree.py')

PICTURES = {'.png', '.jpg', '.jpeg', '.gif', '.ico', '.webp', '.svg'}


@pytest.fixture(scope='module')
def dist(tmp_path_factory):
    return site.build(tmp_path_factory.mktemp('website') / 'dist')


def test_every_picture_and_font_in_the_build_is_owner_approved(dist):
    approved = set(tree.APPROVED_README_MEDIA.values())
    for path in dist.rglob('*'):
        if path.suffix.lower() in PICTURES | {'.ttf', '.otf', '.woff', '.woff2'}:
            assert hashlib.sha256(path.read_bytes()).hexdigest() in approved, path


def test_the_font_ships_with_its_licence(dist):
    assert (dist / 'fonts/Anton-Regular.ttf').is_file()
    assert 'SIL Open Font License' in (dist / 'fonts/OFL.txt').read_text(encoding='utf-8')


def test_every_local_reference_resolves(dist):
    refs = site.local_refs(dist)
    assert 'img/logo-512.png' in refs and 'media/online-fight.png' in refs
    assert all((dist / ref).is_file() for ref in refs)


def test_css_and_js_are_versioned_by_content(dist):
    html = (dist / 'index.html').read_text(encoding='utf-8')
    assert re.search(r'href="style\.css\?v=[0-9a-f]{10}"', html)
    assert re.search(r'src="site\.js\?v=[0-9a-f]{10}"', html)


def test_no_external_scripts_styles_or_fonts():
    html = (ROOT / 'website/index.html').read_text(encoding='utf-8')
    css = (ROOT / 'website/style.css').read_text(encoding='utf-8')
    js = (ROOT / 'website/site.js').read_text(encoding='utf-8')
    assert not re.search(r'<script[^>]+src="(?:https?:)?//', html)
    assert not re.search(r'<link[^>]+href="(?:https?:)?//[^"]*"[^>]*rel="(?:stylesheet|preload)"', html)
    assert not re.search(r'<link[^>]+rel="(?:stylesheet|preload)"[^>]+href="(?:https?:)?//', html)
    assert '@import' not in css and not re.search(r'url\(\s*[\'"]?(?:https?:)?//', css)
    assert set(re.findall(r'https://[a-z0-9.-]+', js)) <= {'https://api.github.com', 'https://github.com'}
    assert 'document.cookie' not in js


def test_the_download_button_falls_back_to_the_latest_release_without_js():
    html = (ROOT / 'website/index.html').read_text(encoding='utf-8')
    button = re.search(r'<a class="btn-download" id="dl-main" href="([^"]+)"', html)
    assert button and button.group(1) == 'https://github.com/sweeax-exe/ffny-online/releases/latest'


def test_the_footer_says_it_is_unofficial_and_distributes_no_game_data():
    html = (ROOT / 'website/index.html').read_text(encoding='utf-8')
    assert 'unofficial fan project' in html
    assert 'not\n        affiliated with, endorsed or sponsored by Electronic Arts' in html
    assert 'no game data is distributed' in html


def test_the_build_refuses_a_picture_the_owner_has_not_approved(monkeypatch, tmp_path):
    monkeypatch.setitem(site.ASSETS, 'media/extra.png', 'branding/SHA256SUMS')
    with pytest.raises(SystemExit, match='not in the approved media'):
        site.build(tmp_path / 'dist')
    assert not (tmp_path / 'dist').exists()


def test_the_build_output_is_ignored_by_git():
    assert 'website/dist/' in (ROOT / '.gitignore').read_text(encoding='utf-8').splitlines()

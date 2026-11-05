"""The GitHub Pages site: it builds from site/ plus approved media only, and every media file the
page refers to is one the build publishes."""
import importlib.util
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


site = load('build_site', ROOT / 'scripts/build-site.py')


def test_builds_with_every_media_file(tmp_path):
    out = tmp_path / 'site'
    assert site.build(ROOT, out) == []
    assert (out / 'index.html').is_file()
    assert (out / '.nojekyll').is_file()
    assert not (out / 'README.md').exists()
    assert sorted(p.name for p in (out / 'media').iterdir()) == sorted(site.MEDIA)


def test_page_refers_only_to_published_media():
    text = ''.join((ROOT / 'site' / name).read_text(encoding='utf-8')
                   for name in ('index.html', 'styles.css', 'app.js'))
    referenced = set(re.findall(r'media/([\w.-]+)', text))
    assert referenced, 'expected the page to show media'
    assert referenced <= set(site.MEDIA), referenced - set(site.MEDIA)


def test_every_source_is_approved_media():
    approved = site.approved_media(ROOT)
    assert all(source in approved for source in site.MEDIA.values())


def test_refuses_unapproved_or_changed_media(tmp_path, monkeypatch):
    monkeypatch.setitem(site.MEDIA, 'extra.png', 'docs/media/not-approved.png')
    problems = site.build(ROOT, tmp_path / 'site')
    assert problems == ['docs/media/not-approved.png: not an approved media file']
    assert not (tmp_path / 'site').exists()

    monkeypatch.delitem(site.MEDIA, 'extra.png')
    approved = dict(site.approved_media(ROOT))
    approved['docs/media/overlay.png'] = '0' * 64
    monkeypatch.setattr(site, 'approved_media', lambda repo: approved)
    assert site.build(ROOT, tmp_path / 'site') == [
        'docs/media/overlay.png: does not match its approved fingerprint']


def test_site_folder_is_text_only():
    for path in (ROOT / 'site').rglob('*'):
        if path.is_file():
            assert path.suffix in {'.html', '.css', '.js', '.md'}, path

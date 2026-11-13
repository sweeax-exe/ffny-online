"""Build 82: new community content is out (setup/mods.py check, from the running game) and the content.ini fields the
hub's NEW badge reads (reviewed, venue_fe_id)."""
import configparser
import json
from pathlib import Path
import sys
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_content_sync as sync  # noqa: E402  (the signed catalogue fixtures)
sys.path.pop(0)
mods = sync.mods
setup = sync.setup      # the fixture


def news(manager, raw, sig):
    def fetch(url, limit):
        return sig if url.endswith('.sig') else raw
    with patch.object(mods, 'fetch_small', fetch):
        names = manager.check_news('https://play.defjam.online/updates/content/index-v3.json')
    c = configparser.ConfigParser(interpolation=None)
    c.read(manager.root / 'news.ini', encoding='utf-8')
    return names, c['news']


def test_check_reports_new_and_changed_packs_without_touching_anything(setup, tmp_path):
    manager, key = setup
    a_path, a = sync.base.package(tmp_path, name='first')
    sync.ffny(key, a_path, a, order=1)
    raw, sig = sync.publish(key, [a], 1)
    sync.run_sync(manager, raw, sig, {a['versions'][0]['archive']['sha256']: a_path})
    names, row = news(manager, raw, sig)
    assert names == [] and row['pending'] == '0' and row['revision'] == '1'       # the last sync saw this revision
    state = (manager.root / 'state.json').read_bytes()
    cached = (manager.root / 'catalogue.json').read_bytes()
    b_path, b = sync.base.package(tmp_path, name='second', color='blue')
    sync.ffny(key, b_path, b, kind='content', order=2, type_='clothing')
    b['name'] = 'Blue hat'
    raw2, sig2 = sync.publish(key, [a, b], 2)
    names, row = news(manager, raw2, sig2)
    assert names == ['Blue hat'] and row['pending'] == '1' and row['names'] == 'Blue hat'
    # a check writes only news.ini: the state, the cached catalogue and the packs are as they were
    assert (manager.root / 'state.json').read_bytes() == state
    assert (manager.root / 'catalogue.json').read_bytes() == cached
    assert not (manager.mods / 'second').exists()
    # a removed pack is news too; once a sync took the revision, nothing is
    raw3, sig3 = sync.publish(key, [b], 3)
    names, row = news(manager, raw3, sig3)
    assert sorted(names) == ['Blue hat', 'Paper test'] and row['pending'] == '2'
    sync.run_sync(manager, raw3, sig3, {b['versions'][0]['archive']['sha256']: b_path})
    names, row = news(manager, raw3, sig3)
    assert names == [] and row['pending'] == '0'


def test_check_skips_packs_this_launcher_cannot_install(setup, tmp_path):
    manager, key = setup
    a_path, a = sync.base.package(tmp_path, name='needs-more')
    sync.ffny(key, a_path, a, kind='content', type_='venue')
    a['versions'][0]['capabilities'] = ['textures-v1', 'content-v9']   # a capability this launcher lacks
    raw, sig = sync.publish(key, [a], 1)
    names, row = news(manager, raw, sig)
    assert names == [] and row['pending'] == '0'


def test_check_refuses_a_bad_signature(setup, tmp_path):
    manager, key = setup
    a_path, a = sync.base.package(tmp_path, name='first')
    sync.ffny(key, a_path, a)
    raw, sig = sync.publish(key, [a], 1)
    try:
        news(manager, raw, bytes(64))
    except ValueError as error:
        assert 'signature' in str(error)
    else:
        raise AssertionError('a bad signature was believed')
    assert not (manager.root / 'news.ini').exists()


def test_content_ini_carries_the_review_day_and_a_new_venue(setup, tmp_path):
    manager, key = setup
    a_path, a = sync.base.package(tmp_path, name='venue-pack')
    sync.ffny(key, a_path, a, kind='content', type_='venue')
    a['versions'][0]['reviewed'] = '2026-11-12'
    raw, sig = sync.publish(key, [a], 1)
    sync.run_sync(manager, raw, sig, {a['versions'][0]['archive']['sha256']: a_path})
    # as a composed venue pack would have it: its install.json's new-venue op
    (manager.mods / 'venue-pack' / 'install.json').write_text(json.dumps(
        {'schema': 1, 'type': 'venue', 'entries': [], 'ops': [{'op': 'new-venue', 'fe_id': 31, 'code': '31A'}]}))
    manager.write_content(1)
    text = sync.content_ini(manager)
    assert 'reviewed=2026-11-12' in text and 'venue_fe_id=31' in text and 'composed=1' in text


def test_new_venue_fe_id_reads_only_the_new_venue_range(tmp_path):
    path = tmp_path / 'install.json'
    for ops, want in (([{'op': 'new-venue', 'fe_id': 28}], 28), ([{'op': 'new-venue', 'fe_id': 63}], 63),
                      ([{'op': 'new-venue', 'fe_id': 12}], 0), ([{'op': 'item'}], 0), ([], 0)):
        path.write_text(json.dumps({'ops': ops}))
        assert mods.new_venue_fe_id(path) == want
    path.write_text('not json')
    assert mods.new_venue_fe_id(path) == 0
    assert mods.new_venue_fe_id(tmp_path / 'missing.json') == 0

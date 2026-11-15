"""Build 83: the last community content sync's outcome (setup/mods.py sync writes mod-manager/sync-result.ini) and a
failed news check (mods.py check writes error= into news.ini), in words a player can act on. The game logs both at
the start and in the F9 report and shows them in the online hub (pc_content.c)."""
import configparser
import datetime as dt
from pathlib import Path
import sys
import urllib.error
from unittest.mock import patch

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_content_sync as sync  # noqa: E402  (the signed catalogue fixtures)
sys.path.pop(0)
mods = sync.mods
setup = sync.setup      # the fixture


def result(manager):
    c = configparser.ConfigParser(interpolation=None)
    c.read(manager.root / 'sync-result.ini', encoding='utf-8')
    return c['sync']


def test_a_good_sync_writes_ok_with_revision_and_packs(setup, tmp_path):
    manager, key = setup
    a_path, a = sync.base.package(tmp_path, name='first')
    sync.ffny(key, a_path, a, order=1)
    raw, sig = sync.publish(key, [a], 5)
    sync.run_sync(manager, raw, sig, {a['versions'][0]['archive']['sha256']: a_path})
    row = result(manager)
    assert row['result'] == 'ok' and row['reason'] == '' and row['kind'] == ''
    assert row['revision'] == '5' and row['packs'] == '1' and row['ids'] == a['id']


def test_no_connection_and_no_cache_writes_failed(setup):
    manager, _ = setup

    def fetch(url, limit):
        raise urllib.error.URLError('[Errno 11001] getaddrinfo failed')
    with patch.object(mods, 'fetch_small', fetch), pytest.raises(mods.ModError):
        manager.sync('https://play.defjam.online/updates/content/index-v3.json')
    row = result(manager)
    assert row['result'] == 'failed' and row['kind'] == 'network' and row['revision'] == '0' and row['packs'] == '0'
    assert row['reason'].startswith('no connection to the content server')


def test_a_stale_cache_after_a_failed_refresh_is_a_failure_not_ok(setup, tmp_path):
    """The D-Mob 2 case: the refresh fails, the sync goes on with the cached older catalogue (no new pack)."""
    manager, key = setup
    a_path, a = sync.base.package(tmp_path, name='first')
    sync.ffny(key, a_path, a, order=1)
    raw, sig = sync.publish(key, [a], 3)
    sync.run_sync(manager, raw, sig, {a['versions'][0]['archive']['sha256']: a_path})

    def fetch(url, limit):
        raise urllib.error.URLError('timed out')
    with patch.object(mods, 'fetch_small', fetch):
        manager.sync('https://play.defjam.online/updates/content/index-v3.json')
    row = result(manager)
    assert row['result'] == 'failed' and row['kind'] == 'network' and row['revision'] == '3' and row['packs'] == '1'
    assert 'timed out' in row['detail']


def test_a_clock_far_behind_is_said_plainly(setup, tmp_path):
    manager, key = setup
    a_path, a = sync.base.package(tmp_path, name='first')
    sync.ffny(key, a_path, a, order=1)
    raw, sig = sync.publish(key, [a], 1)
    real = mods.verify_index
    behind = dt.datetime.now(dt.timezone.utc).replace(tzinfo=None) - dt.timedelta(days=3)
    with patch.object(mods, 'verify_index', lambda *a, **k: real(*a, **{**k, 'now': behind})), \
            pytest.raises(mods.ModError):
        sync.run_sync(manager, raw, sig, {})
    row = result(manager)
    assert row['result'] == 'failed' and row['kind'] == 'clock' and 'clock is behind' in row['reason']


def test_plain_reason_words():
    assert mods.plain_reason(['Catalogue: Catalogue issued in the future'], -3 * 86400) == \
        ('clock', 'the PC clock is 72 hours behind')
    assert mods.plain_reason(['Catalogue expired; refresh before installing'], 600)[0] == 'clock'
    assert 'ahead' in mods.plain_reason(['Catalogue expired; refresh before installing'], 4 * 3600)[1]
    assert mods.plain_reason(['studio-d-mob-2: [Errno 28] No space left on device'])[0] == 'disk'
    assert mods.plain_reason(['Catalogue: Catalogue signature is invalid or untrusted'])[0] == 'catalogue'
    assert mods.plain_reason(['studio-cj: no compatible version'])[0] == 'pack'
    now = dt.datetime(2026, 11, 15, 12, 0, tzinfo=dt.timezone.utc)
    assert mods.clock_skew(now, dt.datetime(2026, 11, 15, 10, 0, tzinfo=dt.timezone.utc)) == 7200
    assert mods.clock_skew(now, None) is None or isinstance(mods.clock_skew(now, None), float)


def test_a_cancelled_sync_says_cancelled(setup, tmp_path):
    manager, key = setup
    a_path, a = sync.base.package(tmp_path, name='first')
    sync.ffny(key, a_path, a, order=1)
    raw, sig = sync.publish(key, [a], 2)

    def download(self, archive):
        raise mods.ModError('Operation cancelled; previous installation preserved')
    with patch.object(mods, 'fetch_small', lambda url, limit: sig if url.endswith('.sig') else raw), \
            patch.object(mods.Manager, 'download', download), pytest.raises(mods.ModError):
        manager.sync('https://play.defjam.online/updates/content/index.json')
    assert result(manager)['result'] == 'cancelled'


def test_a_failed_check_writes_its_error_and_keeps_the_last_news(setup, tmp_path):
    manager, key = setup
    a_path, a = sync.base.package(tmp_path, name='first')
    sync.ffny(key, a_path, a, order=1)
    a['name'] = 'D-Mob 2'
    raw, sig = sync.publish(key, [a], 4)
    with patch.object(mods, 'fetch_small', lambda url, limit: sig if url.endswith('.sig') else raw):
        manager.check_news('https://play.defjam.online/updates/content/index-v3.json')
    manager.write_news_failure(urllib.error.URLError('[WinError 10061] connection refused'))
    c = configparser.ConfigParser(interpolation=None)
    c.read(manager.root / 'news.ini', encoding='utf-8')
    row = c['news']
    assert row['pending'] == '1' and row['revision'] == '4' and row['names'] == 'D-Mob 2'
    assert row['kind'] == 'network' and row['error'].startswith('no connection')
    # a good check afterwards has no error
    with patch.object(mods, 'fetch_small', lambda url, limit: sig if url.endswith('.sig') else raw):
        manager.check_news('https://play.defjam.online/updates/content/index-v3.json')
    c = configparser.ConfigParser(interpolation=None)
    c.read(manager.root / 'news.ini', encoding='utf-8')
    assert 'error' not in c['news']


def test_check_command_line_failure_writes_news_error(setup, monkeypatch, capsys):
    manager, _ = setup

    def fetch(url, limit):
        raise urllib.error.URLError('timed out')
    monkeypatch.setattr(mods, 'fetch_small', fetch)
    monkeypatch.setattr(mods.Manager, '__init__', lambda self, *a, **k: setattr(self, '__dict__', manager.__dict__))
    monkeypatch.setattr(sys, 'argv', ['mods.py', 'check', '--data-dir', str(manager.data)])
    assert mods.main() == 1
    c = configparser.ConfigParser(interpolation=None)
    c.read(manager.root / 'news.ini', encoding='utf-8')
    assert c['news']['kind'] == 'network'


def test_a_pack_for_a_newer_launcher_is_skipped_not_failed(setup, tmp_path):
    """CJ needs voice-v1: a launcher without it skips CJ every sync; that is no failure to show the player."""
    manager, key = setup
    a_path, a = sync.base.package(tmp_path, name='first')
    sync.ffny(key, a_path, a, order=1)
    b_path, b = sync.base.package(tmp_path, name='needs-voice', color='blue')
    sync.ffny(key, b_path, b, order=2)
    b['versions'][0]['capabilities'] = ['textures-v1', 'voice-v9']
    raw, sig = sync.publish(key, [a, b], 5)
    sync.run_sync(manager, raw, sig, {a['versions'][0]['archive']['sha256']: a_path})
    row = result(manager)
    assert row['result'] == 'ok' and row['skipped'] == b['id'] and row['packs'] == '1'

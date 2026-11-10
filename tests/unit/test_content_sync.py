"""FFNY Online's content sync (setup/mods.py sync): the signed catalogue installs for everyone, in its order."""
import datetime as dt
import importlib.util
import json
from pathlib import Path
import sys
from unittest.mock import patch

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[1].parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_mod_manager as base  # noqa: E402  (the synthetic packs and the loaded mods module)
sys.path.pop(0)
mods = base.mods


def ffny(key, path, mod, kind='replacement', order=1, type_='fighter-texture', sign_ok=True):
    """The FFNY fields the account service adds: kind, type, order, content hash and pack signature."""
    files = {}
    import zipfile, hashlib
    with zipfile.ZipFile(path) as z:
        for info in z.infolist():
            files[info.filename.split('/', 1)[1]] = hashlib.sha256(z.read(info)).hexdigest()
    version = mod['versions'][0]
    version['content_sha256'] = mods.content_hash(files)
    signed = json.dumps(dict(content_sha256=version['content_sha256'], id=mod['id'], kind=kind,
                             version=version['version']), sort_keys=True, separators=(',', ':')).encode()
    signature = key.sign(signed)
    if not sign_ok:
        signature = bytes(64)
    version['pack_signature'] = signature.hex()
    mod.update(kind=kind, type=type_, order=order)
    return mod


def publish(key, rows, revision):
    now = dt.datetime.now(dt.timezone.utc)
    raw = mods.contract.build(rows, revision, (now - dt.timedelta(minutes=1)).strftime('%Y-%m-%dT%H:%M:%SZ'),
                              (now + dt.timedelta(days=30)).strftime('%Y-%m-%dT%H:%M:%SZ'))
    return raw, key.sign(raw)


@pytest.fixture
def setup(tmp_path):
    key = Ed25519PrivateKey.generate()
    manager = mods.Manager(tmp_path / 'data', '0.13.0', ['textures-v1', 'language-v1'],
                           keys=[key.public_key().public_bytes_raw().hex()])
    return manager, key


def run_sync(manager, raw, sig, archives):
    def fetch(url, limit):
        return sig if url.endswith('.sig') else raw

    def download(self, archive):
        return archives[archive['sha256']]
    with patch.object(mods, 'fetch_small', fetch), patch.object(mods.Manager, 'download', download):
        manager.sync('https://play.defjam.online/updates/content/index.json')


def content_ini(manager):
    return (manager.root / 'content.ini').read_text(encoding='utf-8')


def test_contract_accepts_ffny_fields_and_refuses_bad_ones(setup, tmp_path):
    manager, key = setup
    path, mod = base.package(tmp_path)
    ffny(key, path, mod)
    mods.contract.validate_mod(mod)
    bad = json.loads(json.dumps(mod))
    bad['kind'] = 'other'
    with pytest.raises(ValueError):
        mods.contract.validate_mod(bad)
    bad = json.loads(json.dumps(mod))
    bad['surprise'] = 1
    with pytest.raises(ValueError):
        mods.contract.validate_mod(bad)


def test_sync_installs_in_order_writes_content_ini_and_skips_unchanged(setup, tmp_path):
    manager, key = setup
    a_path, a = base.package(tmp_path, name='zeta-skin')
    b_path, b = base.package(tmp_path, name='alpha-hat', color='blue')
    ffny(key, a_path, a, order=1)
    ffny(key, b_path, b, kind='content', order=2, type_='clothing')
    raw, sig = publish(key, [a, b], 1)
    archives = {a['versions'][0]['archive']['sha256']: a_path, b['versions'][0]['archive']['sha256']: b_path}
    run_sync(manager, raw, sig, archives)
    text = content_ini(manager)
    assert text.index('[pack.zeta-skin]') < text.index('[pack.alpha-hat]')    # catalogue order, not ids
    assert 'kind=content' in text and 'type=clothing' in text
    assert (manager.mods / 'zeta-skin' / 'manifest.ini').is_file()
    assert 'synced: 2 change(s)' in manager.message
    run_sync(manager, raw, sig, {})                 # nothing changed: no download at all
    assert 'up to date: 2 pack(s)' in manager.message


def test_sync_removes_dropped_and_revoked_packs(setup, tmp_path):
    manager, key = setup
    a_path, a = base.package(tmp_path, name='one')
    b_path, b = base.package(tmp_path, name='two', color='blue')
    ffny(key, a_path, a, order=1)
    ffny(key, b_path, b, order=2)
    raw, sig = publish(key, [a, b], 1)
    run_sync(manager, raw, sig, {a['versions'][0]['archive']['sha256']: a_path,
                                 b['versions'][0]['archive']['sha256']: b_path})
    b['revoked_versions'] = ['1.0.0']
    raw, sig = publish(key, [b], 2)                # one dropped, two revoked
    run_sync(manager, raw, sig, {})
    assert not (manager.mods / 'one').exists() and not (manager.mods / 'two').exists()
    assert '[pack.' not in content_ini(manager)


def test_sync_refuses_a_bad_pack_signature_and_keeps_going(setup, tmp_path):
    manager, key = setup
    a_path, a = base.package(tmp_path, name='good')
    b_path, b = base.package(tmp_path, name='forged', color='blue')
    ffny(key, a_path, a, order=1)
    ffny(key, b_path, b, order=2, sign_ok=False)
    raw, sig = publish(key, [a, b], 1)
    run_sync(manager, raw, sig, {a['versions'][0]['archive']['sha256']: a_path,
                                 b['versions'][0]['archive']['sha256']: b_path})
    assert (manager.mods / 'good').is_dir() and not (manager.mods / 'forged').exists()
    assert 'forged: Pack signature is invalid' in manager.message


def test_sync_keeps_a_hand_installed_folder(setup, tmp_path):
    manager, key = setup
    a_path, a = base.package(tmp_path, name='mine')
    ffny(key, a_path, a)
    (manager.mods / 'mine').mkdir()
    raw, sig = publish(key, [a], 1)
    run_sync(manager, raw, sig, {a['versions'][0]['archive']['sha256']: a_path})
    assert 'mine:' in manager.message and not (manager.mods / 'mine' / 'manifest.ini').exists()


def test_catalogue_url_is_ours_and_local_http_only_for_tests():
    assert mods.CATALOGUE_URL == 'https://play.defjam.online/updates/content/index.json'
    assert mods.local_test_url('http://127.0.0.1:8095/updates/content/index.json')
    assert not mods.local_test_url('http://example.com:80/index.json')
    assert not mods.local_test_url('http://127.0.0.1.evil.com:80/x')

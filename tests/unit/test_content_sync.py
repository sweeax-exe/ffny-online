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


def body_package(tmp, game, name='new-body', new=b'modder body'):
    """A fighter-body pack as Studio submits it: install.json and one delta against the player's v2ip.viv."""
    import hashlib, io, zipfile
    sys.path.insert(0, str(ROOT / 'setup'))
    from ffnycontent import delta, viv
    sys.path.pop(0)
    original = b'\x7fELF' + bytes(range(256)) * 2
    (game / 'assets').mkdir(parents=True, exist_ok=True)
    (game / 'assets' / 'v2ip.viv').write_bytes(viv.build_archive([('V2IP_109A.o', viv.refpack_store(original))],
                                                                 align=128))
    result = original + new
    install = {'schema': 1, 'type': 'fighter-body', 'ops': [], 'entries': [{
        'archive': 'assets/v2ip.viv', 'entry': 'V2IP_109A.o', 'mode': 'replace', 'base_archive': 'assets/v2ip.viv',
        'base_entry': 'V2IP_109A.o', 'delta': 'deltas/V2IP_109A.o.ffdelta',
        'original_sha256': hashlib.sha256(original).hexdigest(), 'result_sha256': hashlib.sha256(result).hexdigest(),
        'result_size': len(result)}]}
    entries = {name + '/install.json': json.dumps(install).encode(),
               name + '/deltas/V2IP_109A.o.ffdelta': delta.encode(original, result)}
    path = tmp / (name + '.zip')
    with zipfile.ZipFile(path, 'w') as archive:
        for key, data in entries.items():
            archive.writestr(key, data)
    _, mod = base.package(tmp, name=name)
    mod['versions'][0]['archive'] = dict(root=name, size=path.stat().st_size, sha256=mods.sha(path),
                                         unpacked_size=sum(map(len, entries.values())), file_count=len(entries),
                                         urls=['https://example.invalid/body.zip'])
    mod['versions'][0]['capabilities'] = ['content-v1']
    return path, mod, result


def test_sync_composes_a_fighter_body_pack_and_marks_it_composed(tmp_path, monkeypatch):
    monkeypatch.setenv('RECOMP_SETTINGS', 'none')
    key = Ed25519PrivateKey.generate()
    game = tmp_path / 'game'
    manager = mods.Manager(tmp_path / 'data', '0.13.0', ['textures-v1', 'language-v1', 'content-v1'],
                           keys=[key.public_key().public_bytes_raw().hex()], game_dir=game)
    path, mod, result = body_package(tmp_path, game)
    ffny(key, path, mod, kind='replacement', type_='fighter-body')
    raw, sig = publish(key, [mod], 1)
    run_sync(manager, raw, sig, {mod['versions'][0]['archive']['sha256']: path})
    assert 'composed=1' in content_ini(manager) and 'type=fighter-body' in content_ini(manager)
    files = manager.root / 'content-files'
    assert 'applied=new-body' in (files / 'files.ini').read_text(encoding='utf-8')
    sys.path.insert(0, str(ROOT / 'setup'))
    from ffnycontent import viv
    sys.path.pop(0)
    assert viv.Archive(files / 'assets' / 'v2ip.viv').read('V2IP_109A.o') == result
    # a launcher without content-v1 does not take it
    old = mods.Manager(tmp_path / 'old', '0.13.0', ['textures-v1'], keys=[key.public_key().public_bytes_raw().hex()],
                       game_dir=game)
    run_sync(old, raw, sig, {mod['versions'][0]['archive']['sha256']: path})
    assert not (old.mods / 'new-body').exists() and 'new-body:' in old.message
    # dropped from the catalogue: the pack and the composed files go
    raw, sig = publish(key, [], 2)
    run_sync(manager, raw, sig, {})
    assert not files.exists() and not (manager.mods / 'new-body').exists()


def test_install_json_pack_validation(tmp_path):
    game = tmp_path / 'game'
    path, mod, _ = body_package(tmp_path, game)
    import zipfile
    folder = tmp_path / 'unpacked'
    with zipfile.ZipFile(path) as archive:
        archive.extractall(folder)
    pack = folder / 'new-body'
    assert mods.validate_pack(pack)['capabilities'] == ['content-v1']
    (pack / 'deltas' / 'extra.ffdelta').write_bytes(b'FFDL\x01\x00\x00\x00')
    with pytest.raises(mods.ModError, match='not listed'):
        mods.validate_pack(pack)
    (pack / 'deltas' / 'extra.ffdelta').unlink()
    (pack / 'deltas' / 'V2IP_109A.o.ffdelta').write_bytes(b'nope')
    with pytest.raises(mods.ModError, match='Not an FFNY delta'):
        mods.validate_pack(pack)
    (pack / 'install.json').write_text('{"schema": 1, "type": "fighter-body", "entries": [], "ops": [{"op": "item"}]}')
    with pytest.raises(mods.ModError, match='Invalid install.json'):
        mods.validate_pack(pack)

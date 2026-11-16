"""Build 86: the FFNY team's official maps (kind official-map, index-v5.json): the catalogue contract, the sync
(always on, content.ini official=1, rts_map, venue_fe_id, venue_code) and the composer (never switched off)."""
import hashlib
import io
import json
from pathlib import Path
import sys
import urllib.error
from unittest.mock import patch
import zipfile

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_content_sync as sync  # noqa: E402  (signed catalogue helpers)
import test_content_compose as tcc  # noqa: E402  (synthetic game files)
from test_content_compose import game, data  # noqa: E402,F401  (pytest fixtures)
sys.path.pop(0)
mods = sync.mods
cc = tcc.cc
delta = tcc.delta

MAP_ID = 'official-knox-and-jam'
VENUE_OP = {"op": "new-venue", "template": "V2BG_03A", "template_code": "03A", "template_fe_id": 3,
            "filename": "V2BG_28A", "code": "28A", "fe_id": 28, "title": "Knox and Jam",
            "dispname": "28A Knox and Jam", "drop_objects": True}
BASE_O = b"\x7fELF" + bytes(300)        # test_content_compose's V2BG_03A.o


def install_json(result=b"\x7fELF" + b"knox and jam" * 30):
    rel = 'deltas/0_V2BG_28A.o.ffdelta'
    entry = {"archive": "assets/v2bg.viv", "entry": "V2BG_28A.o", "mode": "add", "base_archive": "assets/v2bg.viv",
             "base_entry": "V2BG_03A.o", "delta": rel, "original_sha256": hashlib.sha256(BASE_O).hexdigest(),
             "result_sha256": hashlib.sha256(result).hexdigest(), "result_size": len(result)}
    info = {"schema": 1, "type": "venue", "entries": [entry], "ops": [VENUE_OP]}
    return info, {rel: delta.encode(BASE_O, result)}


def official_package(tmp, version='0.1.0', rts=True, result=None):
    """The signed archive of an official map as the account service publishes it, and its index-v5.json entry."""
    info, deltas = install_json(result or b"\x7fELF" + (b"knox and jam %s" % version.encode()) * 30)
    entries = {MAP_ID + '/install.json': json.dumps(info).encode()}
    entries.update({MAP_ID + '/' + k: v for k, v in deltas.items()})
    entries[MAP_ID + '/bigarena.json'] = json.dumps({"version": 1, "mode": "bigarena", "size": [1000.0, 1000.0],
                                                     "spawns": [[0.0, -260.0], [260.0, 0.0]]}).encode()
    path = tmp / ('%s-%s.zip' % (MAP_ID, version))
    with zipfile.ZipFile(path, 'w') as z:
        for k, v in entries.items():
            z.writestr(k, v)
    descriptor = dict(root=MAP_ID, size=path.stat().st_size, sha256=mods.sha(path),
                      unpacked_size=sum(map(len, entries.values())), file_count=len(entries),
                      urls=['https://example.invalid/%s.zip' % version])
    mod = dict(id=MAP_ID, name='Knox and Jam', description='The Run the Streets map', authors=['FFNY Online'],
               licence='FFNY licence', homepage='https://example.invalid', **{'class': 'original'},
               revoked_versions=[], versions=[dict(version=version, channel='stable', min_recomp='0.1.0',
               capabilities=['content-v1', 'official-map-v1'], reviewed='2026-11-16', depends=[], conflicts=[],
               load_after=[], archive=descriptor)])
    return path, mod


def entry(key, tmp, version='0.1.0', rts=True, order=1):
    path, mod = official_package(tmp, version, rts)
    sync.ffny(key, path, mod, kind='official-map', order=order, type_='venue')
    mod.update(rts_map=rts, venue_fe_id=28)
    return path, mod


@pytest.fixture
def setup(tmp_path):
    key = Ed25519PrivateKey.generate()
    caps = ['textures-v1', 'content-v1', 'official-map-v1']
    manager = mods.Manager(tmp_path / 'data', '0.13.0', caps, keys=[key.public_key().public_bytes_raw().hex()])
    return manager, key


def run_sync(manager, raw, sig, archives):
    def fetch(url, limit):
        return sig if url.endswith('.sig') else raw

    def download(self, archive):
        return archives[archive['sha256']]
    with patch.object(mods, 'fetch_small', fetch), patch.object(mods.Manager, 'download', download), \
            patch.object(mods.Manager, 'compose', lambda self: {'skipped': []}):
        manager.sync(mods.CATALOGUE_URL)


def section(text, name):
    part = text.split('[pack.%s]' % name, 1)[1].split('\n[', 1)[0]
    return dict(line.split('=', 1) for line in part.strip().splitlines())


def test_contract_takes_official_maps_only_whole(setup, tmp_path):
    _, key = setup
    _, mod = entry(key, tmp_path)
    mods.contract.validate_mod(mod)
    for change in (lambda m: m.pop('venue_fe_id'), lambda m: m.update(venue_fe_id=64),
                   lambda m: m.update(venue_fe_id=True), lambda m: m.update(type='clothing'),
                   lambda m: m.update(id='knox-and-jam'), lambda m: m.update(rts_map='yes'),
                   lambda m: m.update(kind='content'), lambda m: m.update(bundle='x', bundle_name='X')):
        bad = json.loads(json.dumps(mod))
        change(bad)
        with pytest.raises(ValueError):
            mods.contract.validate_mod(bad)
    plain = json.loads(json.dumps(mod))
    plain.pop('rts_map')            # rts_map is optional (false)
    mods.contract.validate_mod(plain)
    other = json.loads(json.dumps(mod))
    other['id'] = 'official-other'
    with pytest.raises(ValueError, match='Duplicate official map venue'):
        sync.publish(key, [mod, other], 1)


def test_sync_installs_the_official_map_always_on(setup, tmp_path):
    manager, key = setup
    path, mod = entry(key, tmp_path)
    raw, sig = sync.publish(key, [mod], 1)
    run_sync(manager, raw, sig, {mod['versions'][0]['archive']['sha256']: path})
    receipt = manager.state()['installed'][MAP_ID]
    assert receipt['kind'] == 'official-map' and receipt['official'] and receipt['rts_map'] and receipt['venue_fe_id'] == 28
    ini = section(sync.content_ini(manager), MAP_ID)
    # kind=content for the game (always on, in the content print); official=1 and the map's venue for Run the Streets
    assert ini['kind'] == 'content' and ini['type'] == 'venue' and ini['official'] == '1' and ini['rts_map'] == '1'
    assert ini['venue_fe_id'] == '28' and ini['venue_code'] == '28A' and ini['composed'] == '1'
    assert ini['bigarena'] == '1' and (manager.mods / MAP_ID / 'bigarena.json').is_file()
    composed = manager.composed_packs()
    assert [(p['id'], p['kind']) for p in composed] == [(MAP_ID, 'official-map')]


def test_an_update_replaces_the_installed_version(setup, tmp_path):
    manager, key = setup
    path, mod = entry(key, tmp_path)
    raw, sig = sync.publish(key, [mod], 1)
    run_sync(manager, raw, sig, {mod['versions'][0]['archive']['sha256']: path})
    path2, mod2 = entry(key, tmp_path, version='0.2.0', rts=False)
    raw, sig = sync.publish(key, [mod2], 2)
    run_sync(manager, raw, sig, {mod2['versions'][0]['archive']['sha256']: path2})
    receipt = manager.state()['installed'][MAP_ID]
    assert receipt['version'] == '0.2.0' and receipt['rts_map'] is False
    assert section(sync.content_ini(manager), MAP_ID)['rts_map'] == '0'
    raw, sig = sync.publish(key, [], 3)                 # withdrawn: it goes like any synced pack
    run_sync(manager, raw, sig, {})
    assert MAP_ID not in manager.state()['installed'] and '[pack.' not in sync.content_ini(manager)


def test_a_launcher_without_the_capability_skips_it(tmp_path):
    key = Ed25519PrivateKey.generate()
    manager = mods.Manager(tmp_path / 'data', '0.13.0', ['textures-v1', 'content-v1'],
                           keys=[key.public_key().public_bytes_raw().hex()])
    path, mod = entry(key, tmp_path)
    raw, sig = sync.publish(key, [mod], 1)
    run_sync(manager, raw, sig, {mod['versions'][0]['archive']['sha256']: path})
    assert MAP_ID not in manager.state()['installed']
    assert 'needs a newer FFNY Online' in manager.message


def test_index_v5_falls_back_to_v4_then_v3(monkeypatch):
    assert mods.CATALOGUE_URL.endswith('/index-v5.json') and mods.CATALOGUE_URL_V4.endswith('/index-v4.json')
    served = {mods.CATALOGUE_URL_V3: b'v3', mods.CATALOGUE_URL_V3 + '.sig': b's3'}

    def fetch(url, limit):
        if url not in served:
            raise urllib.error.HTTPError(url, 404, 'Not Found', None, None)
        return served[url]
    monkeypatch.setattr(mods, 'fetch_small', fetch)
    assert mods.fetch_index(mods.CATALOGUE_URL) == (b'v3', b's3', mods.CATALOGUE_URL_V3)
    served.update({mods.CATALOGUE_URL_V4: b'v4', mods.CATALOGUE_URL_V4 + '.sig': b's4'})
    assert mods.fetch_index(mods.CATALOGUE_URL) == (b'v4', b's4', mods.CATALOGUE_URL_V4)
    served.update({mods.CATALOGUE_URL: b'v5', mods.CATALOGUE_URL + '.sig': b's5'})
    assert mods.fetch_index(mods.CATALOGUE_URL) == (b'v5', b's5', mods.CATALOGUE_URL)


def official_folder(data, kind='official-map', pack_id=MAP_ID, order=1):
    info, deltas = install_json()
    folder = data / 'mods' / pack_id
    (folder / 'deltas').mkdir(parents=True)
    (folder / 'install.json').write_text(json.dumps(info), encoding='utf-8')
    for rel, raw in deltas.items():
        (folder / rel).write_bytes(raw)
    return {"id": pack_id, "version": "0.1.0", "content_sha256": hashlib.sha256(pack_id.encode()).hexdigest(),
            "kind": kind, "order": order, "folder": folder}


def test_compose_an_official_map_into_its_venue_slot(game, data):
    pack = official_folder(data)
    # the player cannot switch it off: [content] off naming it changes nothing
    result = tcc.compose(data, game, [pack], off=[MAP_ID])
    assert result['applied'] == [MAP_ID], result
    venue_xml = tcc.out(data, 'venue.xml').read_text('latin-1')
    assert '<filename>V2BG_28A</filename>' in venue_xml and '<feID>28</feID>' in venue_xml
    rows = {v['fe_id']: v for _, v in tcc.sod.venues(tcc.out(data, 'assets/djv2.sod').read_bytes())}
    assert rows[28]['filename'] == 'V2BG_28A' and rows[28]['display'] == 'Knox and Jam'
    assert tcc.viv.Archive(tcc.out(data, 'assets/v2bg.viv')).read('V2BG_28A.o').startswith(b'\x7fELF' + b'knox and jam')
    # a player's own venue at the same slot comes after the catalogue's packs and is left out
    own = tcc.make_pack(data, 'my-block', 'venue', [], [dict(VENUE_OP, title='My Block', dispname='28A My Block')],
                        order=mods.LOCAL_ORDER)
    result = tcc.compose(data, game, [pack, own])
    assert result['applied'] == [MAP_ID] and result['skipped'][0][0] == 'my-block'


def test_a_run_the_streets_map_is_left_out_of_battles_venue_select(game, data):
    """Build 86: the composer gives an official RTS map's Venue row no match types (Battle's GetNextVenue skips it);
    the row stays (Game.StartGame loads it by feID). A non-RTS official map keeps its template's types."""
    pack = official_folder(data)
    tcc.compose(data, game, [pack])
    plain = {v['fe_id']: v for _, v in tcc.sod.venues(tcc.out(data, 'assets/djv2.sod').read_bytes())}
    assert plain[28]['modes'] == plain[3]['modes'] != 0
    result = tcc.compose(data, game, [dict(pack, rts_map=True)])
    assert result['applied'] == [MAP_ID] and not result.get('unchanged'), result
    rows = {v['fe_id']: v for _, v in tcc.sod.venues(tcc.out(data, 'assets/djv2.sod').read_bytes())}
    assert rows[28]['modes'] == 0 and rows[28]['filename'] == 'V2BG_28A' and rows[3]['modes'] == plain[3]['modes']

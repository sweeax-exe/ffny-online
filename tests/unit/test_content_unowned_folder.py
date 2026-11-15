"""Build 84: a catalogue pack's folder <data>/mods/<id> that state.json does not own no longer blocks the pack.

A player on build 83: "Not synced: studio-d-mob-2: A local folder already uses this mod ID; it will not be
overwritten" (left from a copy by hand, or a sync cut short by the certificate failures). Now:
(a) the exact signed files: adopted; (b) anything else without local.json: moved aside to
mod-manager/replaced/<id>-<UTC>, never deleted, and the signed package installed; (c) the player's own pack
(local.json) is never touched; (d) one blocked pack does not hold up the others, and the Content page line names
the folder."""
import configparser
import json
from pathlib import Path
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_content_sync as sync  # noqa: E402  (the signed catalogue fixtures)
sys.path.pop(0)
mods = sync.mods
setup = sync.setup      # the fixture


def two_packs(key, tmp_path, revision=1):
    a_path, a = sync.base.package(tmp_path, name='studio-d-mob-2')
    sync.ffny(key, a_path, a, order=1)
    b_path, b = sync.base.package(tmp_path, name='studio-cj', color='blue')
    sync.ffny(key, b_path, b, order=2)
    raw, sig = sync.publish(key, [a, b], revision)
    archives = {a['versions'][0]['archive']['sha256']: a_path, b['versions'][0]['archive']['sha256']: b_path}
    return a, b, raw, sig, archives


def message(manager):
    c = configparser.ConfigParser(interpolation=None)
    c.read(manager.root / 'progress.ini', encoding='utf-8')
    return c['progress']['message']


def notes(manager):
    c = configparser.ConfigParser(interpolation=None)
    c.read(manager.root / 'sync-result.ini', encoding='utf-8')
    return c['sync']


def files(folder):
    return {p.relative_to(folder).as_posix(): p.read_bytes() for p in folder.rglob('*') if p.is_file()}


def test_a_an_unowned_folder_with_the_exact_package_is_adopted(setup, tmp_path):
    manager, key = setup
    a, b, raw, sig, archives = two_packs(key, tmp_path)
    sync.run_sync(manager, raw, sig, archives)
    before = files(manager.mods / a['id'])
    # state.json forgets it (an interrupted sync); the folder stays
    state = manager.state()
    state['installed'].pop(a['id'])
    mods.atomic_json(manager.root / 'state.json', state)
    raw2, sig2 = sync.publish(key, [a, b], 2)
    sync.run_sync(manager, raw2, sig2, archives)
    receipt = manager.state()['installed'][a['id']]
    assert receipt['synced'] and receipt['version'] == '1.0.0'
    assert files(manager.mods / a['id']) == before
    assert not (manager.root / 'replaced').exists()
    assert 'already held this exact pack' in message(manager) and notes(manager)['result'] == 'ok'
    assert a['id'] in (manager.root / 'content.ini').read_text(encoding='utf-8')


def test_b_any_other_unowned_folder_is_moved_aside_and_the_signed_pack_installed(setup, tmp_path):
    manager, key = setup
    a, b, raw, sig, archives = two_packs(key, tmp_path)
    old = manager.mods / a['id']
    (old / 'textures').mkdir(parents=True)
    (old / 'textures/test.png').write_bytes(b'an older copy by hand')
    (old / 'notes.txt').write_text('mine', encoding='utf-8')
    sync.run_sync(manager, raw, sig, archives)
    assert manager.state()['installed'][a['id']]['synced']
    moved = list((manager.root / 'replaced').iterdir())
    assert len(moved) == 1 and moved[0].name.startswith(a['id'] + '-') and moved[0].name.endswith('Z')
    assert files(moved[0]) == {'textures/test.png': b'an older copy by hand', 'notes.txt': b'mine'}   # never deleted
    assert (manager.mods / a['id'] / 'manifest.ini').is_file()
    text = message(manager)
    assert 'was moved to' in text and str(moved[0]) in text and 'Not synced' not in text
    assert 'was moved to' in notes(manager)['notes']


def test_c_the_players_own_pack_is_never_touched_and_d_the_others_go_on(setup, tmp_path):
    manager, key = setup
    a, b, raw, sig, archives = two_packs(key, tmp_path)
    own = manager.mods / a['id']
    own.mkdir(parents=True)
    (own / mods.LOCAL_FILE).write_text(json.dumps({'name': 'My D-Mob'}), encoding='utf-8')
    (own / 'manifest.ini').write_text('[pack]\nschema=1\nname=Mine\n', encoding='utf-8')
    before = files(own)
    sync.run_sync(manager, raw, sig, archives)
    assert files(own) == before and not (manager.root / 'replaced').exists()
    assert a['id'] not in manager.state()['installed']
    assert manager.state()['installed'][b['id']]['synced']             # (d) the other pack installed
    text = message(manager)
    assert 'Not synced' in text and str(own) in text and 'your own pack' in text
    assert notes(manager)['result'] == 'failed'


def test_c_a_my_prefixed_own_pack_beside_the_catalogue_is_left_alone(setup, tmp_path):
    manager, key = setup
    a, b, raw, sig, archives = two_packs(key, tmp_path)
    mine = manager.mods / 'my-d-mob'
    mine.mkdir(parents=True)
    (mine / mods.LOCAL_FILE).write_text('{}', encoding='utf-8')
    sync.run_sync(manager, raw, sig, archives)
    assert (mine / mods.LOCAL_FILE).is_file() and not (manager.root / 'replaced').exists()
    assert {a['id'], b['id']} <= set(manager.state()['installed'])


def test_d_a_blocked_pack_names_its_folder(setup, tmp_path):
    """A file where the folder should be: refused with its path, the other pack installs."""
    manager, key = setup
    a, b, raw, sig, archives = two_packs(key, tmp_path)
    manager.mods.mkdir(parents=True, exist_ok=True)
    (manager.mods / a['id']).write_bytes(b'not a folder')
    sync.run_sync(manager, raw, sig, archives)
    assert manager.state()['installed'][b['id']]['synced']
    assert str(manager.mods / a['id']) in message(manager)
    shutil.rmtree(manager.root / 'staging', ignore_errors=True)

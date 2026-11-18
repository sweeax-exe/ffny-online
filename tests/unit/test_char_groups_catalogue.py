"""Build 96: character groups. Community fighters that are the same character come with the mod keys "character" and
"version_name" in the signed catalogue (index-v7.json only; builds 94 and 95 read index-v6.json and refuse unknown keys),
reach content.ini at every sync (character=, version_name=) and are never part of a pack's content hash, signature or
the online content print. The game's hub groups them (src/hooks/char_groups.c, test_char_groups_native.py)."""
import configparser
from pathlib import Path
import sys
from unittest.mock import patch

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_content_sync as sync  # noqa: E402  (the signed catalogue fixtures)
sys.path.pop(0)
mods = sync.mods
setup = sync.setup      # the fixture


def fighter(mod, **kw):
    m = dict(mod, type='new-fighter', kind='content', fighter_fe_id=101)
    m.update(kw)
    return m


def test_contract_takes_groups_on_new_fighters_only(setup, tmp_path):
    manager, key = setup
    path, mod = sync.base.package(tmp_path)
    sync.ffny(key, path, mod)
    mods.contract.validate_mod(fighter(mod, character='Spider-Man'))
    mods.contract.validate_mod(fighter(mod, character='Spider-Man', version_name='Noir'))
    mods.contract.validate_mod(fighter(mod, character='x' * 40))
    for bad in (dict(character=''), dict(character='x' * 41), dict(character='a\nb'), dict(character=5),
                dict(version_name='Noir'), dict(character='Doc', fighter_mode='skin', skin_of=55)):
        with pytest.raises(ValueError):
            m = fighter(mod, **bad)
            if 'skin_of' in bad:
                m.pop('fighter_fe_id')
            mods.contract.validate_mod(m)
    with pytest.raises(ValueError):          # a texture pack has no group
        mods.contract.validate_mod(dict(mod, character='Spider-Man'))


def test_content_ini_carries_the_groups(setup, tmp_path):
    manager, key = setup
    index = {'mods': [{'id': 'spidey-a', 'character': 'Spider-Man', 'version_name': 'Classic', 'versions': []},
                      {'id': 'spidey-b', 'character': 'Spider-Man', 'versions': []}, {'id': 'loner', 'versions': []}]}
    installed = {i: {'synced': True, 'order': n, 'name': i, 'type': 'new-fighter', 'kind': 'content', 'version': '1.0.0'}
                 for n, i in enumerate(('spidey-a', 'spidey-b', 'loner'), 1)}
    with patch.object(mods.Manager, 'trusted', lambda self: index), \
            patch.object(mods.Manager, 'state', lambda self: {'installed': installed}), \
            patch.object(mods.Manager, 'local_packs', lambda self: []):
        assert manager.groups_by_id() == {'spidey-a': ('Spider-Man', 'Classic'), 'spidey-b': ('Spider-Man', '')}
        manager.write_content(3)
    c = configparser.ConfigParser(interpolation=None)
    c.read(manager.root / 'content.ini', encoding='utf-8')
    assert (c['pack.spidey-a']['character'], c['pack.spidey-a']['version_name']) == ('Spider-Man', 'Classic')
    assert (c['pack.spidey-b']['character'], c['pack.spidey-b']['version_name']) == ('Spider-Man', '')
    assert c['pack.loner']['character'] == '' and c['pack.loner']['version_name'] == ''


def test_groups_from_an_old_catalogue_are_none(setup):
    manager, _ = setup
    with patch.object(mods.Manager, 'trusted', lambda self: {'mods': [{'id': 'a'}]}):
        assert manager.groups_by_id() == {}
    with patch.object(mods.Manager, 'trusted', lambda self: (_ for _ in ()).throw(ValueError('none'))):
        assert manager.groups_by_id() == {}


def test_index_v7_falls_back_to_v6():
    assert mods.FALLBACK[mods.CATALOGUE_URL] == mods.CATALOGUE_URL_V6
    assert mods.FALLBACK[mods.CATALOGUE_URL_V6] == mods.CATALOGUE_URL_V5
    assert mods.one_line(' A \n B ') == 'A B' and len(mods.one_line('x' * 99)) == 60

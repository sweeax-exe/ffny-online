"""Release packaging boundary checks; synthetic files, no game/artwork bytes."""
import importlib.util
import json
from pathlib import Path
import pytest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('menu_release',ROOT/'scripts/build-menu-ui-release.py')
release=importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


def test_native_mapping_cannot_accidentally_include_wide_entries(tmp_path):
    manifest=tmp_path/'manifest.ini'
    a,b='a'*64,'b'*64
    manifest.write_text('[textures]\n'+a+'=textures/a.png\n[wide]\n'+b+'=-512:wide/b.png\n')
    assert dict(release.mappings(manifest,'textures'))=={a:'textures/a.png'}


def test_component_files_cannot_escape_the_component(tmp_path):
    root=tmp_path/'component';root.mkdir()
    (root/'native.png').write_bytes(b'synthetic input')
    (tmp_path/'outside.png').write_bytes(b'outside')
    assert release.safe_file(root,'native.png')==root/'native.png'
    with pytest.raises(ValueError,match='escapes root'):
        release.safe_file(root,'../outside.png')


def test_component_symlink_cannot_escape_the_component(tmp_path):
    root=tmp_path/'component';root.mkdir()
    (tmp_path/'outside.png').write_bytes(b'outside')
    try:
        (root/'link.png').symlink_to(tmp_path/'outside.png')
    except OSError as error:
        if getattr(error, 'winerror', None) == 1314:
            pytest.skip('Windows symlink privilege unavailable; traversal checked separately')
        raise
    with pytest.raises(ValueError,match='escapes root'):
        release.safe_file(root,'link.png')


def test_release_is_a_separate_sparse_override_with_exact_registered_scope():
    recipe=json.loads((ROOT/'config/menu-ui-release.json').read_text())
    catalogue=json.loads((ROOT/'config/menu-ui-approved-artwork.json').read_text())
    registered=json.loads((ROOT/recipe['runtime_registrations']).read_text())
    ids=[s['source_id'] for s in catalogue['reviewed_sources']+registered['sources']]
    assert len(ids)==len(set(ids))==recipe['expected_source_ids']==157
    assert recipe['bundles_faithful_hd'] is False
    components=recipe['base_components']+recipe['approved_components']
    assert all(not name.startswith('faithful-hd') for name in components)
    assert set(recipe['component_receipt_sha256'])==set(components)
    assert all(len(pin)==64 for pin in recipe['component_receipt_sha256'].values())
    assert len(recipe['retained_core_sha256'])==23
    assert set(recipe['retained_core_sha256']) <= set(ids)
    assert all(len(pin)==64 for pin in recipe['retained_core_sha256'].values())

"""Synthetic checks for the source-shaped menu pack export table (no artwork needed)."""
import copy
import importlib.util
import json
from pathlib import Path
import pytest

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('menu_pack', REPO/'scripts/export-menu-ui-pack.py')
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
spec_wide = importlib.util.spec_from_file_location('menu_wide', REPO/'scripts/export-menu-ui-wide.py')
wide = importlib.util.module_from_spec(spec_wide)
spec_wide.loader.exec_module(wide)

SID = 'a' * 64


def tables(size=(64, 128), art='x/tile.png', pinned='x/tile.png'):
    catalogue = {'schema': 1, 'approval_records': [{'files': [{'path': pinned, 'sha256': 'b' * 64}]}],
                 'reviewed_sources': [{'source_id': SID, 'aliases': [{'width': 16, 'height': 32}]}]}
    exports = {'schema': 1, 'components': {'menu-test': {'name': 'Test', 'png_mips': 'opacity', 'status': 'test',
               'entries': [{'source_id': SID, 'label': 't', 'art': art, 'crop': None, 'size': list(size)}]}}}
    return exports, catalogue


def test_valid_plan_records_pin_and_scale():
    work = mod.plan(*tables())
    assert work['menu-test'][0]['scale'] == 4 and work['menu-test'][0]['pin'] == 'b' * 64


@pytest.mark.parametrize('size', [(64, 64), (48, 128), (144, 288)])
def test_non_uniform_or_out_of_range_scale_is_rejected(size):
    with pytest.raises(ValueError, match='uniform'):
        mod.plan(*tables(size=size))


def test_unpinned_artwork_is_rejected():
    with pytest.raises(ValueError, match='approved pin'):
        mod.plan(*tables(art='x/other.png'))


def test_duplicate_source_ids_are_rejected():
    exports, catalogue = tables()
    exports['components']['menu-second'] = copy.deepcopy(exports['components']['menu-test'])
    with pytest.raises(ValueError, match='twice'):
        mod.plan(exports, catalogue)


def test_tracked_table_matches_tracked_catalogue():
    exports = json.loads((REPO/'config/menu-ui-source-shaped-exports.json').read_text())
    catalogue = json.loads((REPO/'config/menu-ui-approved-artwork.json').read_text())
    work = mod.plan(exports, catalogue)
    assert {k: len(v) for k, v in work.items()} == {'menu-ui-restored-test-v1': 39, 'menu-ui-mode-conditional-test-v1': 8, 'menu-ui-extras-test-v1': 9, 'menu-ui-phone-test-v1': 2, 'menu-ui-popup-profile-test-v1': 22, 'menu-ui-gym-controls-test-v1': 22}


def test_wide_layer_table_uses_pinned_masters_and_known_anchors():
    layers = json.loads((REPO/'config/menu-ui-wide-layers.json').read_text())
    catalogue = json.loads((REPO/'config/menu-ui-approved-artwork.json').read_text())
    registrations = json.loads((REPO/'config/menu-ui-runtime-registrations.json').read_text())
    pins, dimensions = wide.registered_inputs(catalogue, registrations)
    sources = {sid: size[0] for sid, size in dimensions.items()}
    labels = [layer['label'] for layer in layers['layers']]
    assert len(labels) == len(set(labels))
    for layer in layers['layers']:
        assert layer['anchor'] in sources and layer['master'] in pins, layer['label']
        assert layer.get('sides', 'both') in ('left', 'right', 'both', 'none')
        ua, ub = layer.get('u_range', [0, sources[layer['anchor']]])
        assert 0 <= ua < ub <= sources[layer['anchor']]
    stretch = [s['id'] for s in layers['stretch']]
    assert len(stretch) == len(set(stretch)) and all(len(s) == 64 and int(s, 16) >= 0 for s in stretch)
    # Paper panels are not scenery: none of their sources may carry side slabs.
    papers = {s['source_id'] for s in catalogue['reviewed_sources'] if s['family'] in ('result-papers', 'extras-papers')}
    assert not papers & {layer['anchor'] for layer in layers['layers']}


def test_wide_offsets_use_runtime_integer_grammar_with_fractional_uvs():
    spec = importlib.util.spec_from_file_location('menu_wide', REPO/'scripts/export-menu-ui-wide.py')
    wide = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(wide)
    shifts = wide.slab_shifts(-5, 5, 127.968017578125)
    assert shifts == sorted(set(shifts))
    assert all(isinstance(x, int) and abs(x - i*127.968017578125) <= .5
               for i, x in zip(range(-5, 6), shifts))
    assert wide.slab_shifts(-2, 2, 512) == [-1024, -512, 0, 512, 1024]


def test_engineering_registration_requires_original_approved_parent():
    _, catalogue = tables()
    item = dict(path='derived.png', sha256='c'*64, derived_from='x/tile.png',
                derived_from_sha256='b'*64)
    assert wide.registered_inputs(catalogue, {'files':[item]})[0]['derived.png'] == 'c'*64
    item['derived_from_sha256'] = 'd'*64
    with pytest.raises(ValueError, match='approved parent'):
        wide.registered_inputs(catalogue, {'files':[item]})
    item.update(derived_from_sha256='b'*64, path='x/tile.png')
    with pytest.raises(ValueError, match='replace an approval'):
        wide.registered_inputs(catalogue, {'files':[item]})
    item.update(path='derived.png', derived_from='unknown.png', derived_from_sha256=None)
    with pytest.raises(ValueError, match='approved parent'):
        wide.registered_inputs(catalogue, {'files':[item]})

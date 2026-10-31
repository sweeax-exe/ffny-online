"""Release settings must survive scenario isolation and refuse changed mod bytes."""
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('regression_profile', ROOT / 'scripts/regression_profile.py')
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


def installed(tmp_path):
    root = tmp_path / 'mods'
    state = {'installed': {}}
    for name, version in profile.PACKS.items():
        folder = root / name
        folder.mkdir(parents=True)
        manifest = folder / 'manifest.ini'
        manifest.write_text('[pack]\nschema=1\n[textures]\nid=image.png\n')
        state['installed'][name] = {'version': version, 'folder': name,
            'archive_sha256': 'a' * 64, 'files': {'manifest.ini': profile.digest(manifest)}}
    (tmp_path / 'mod-manager').mkdir()
    (tmp_path / 'mod-manager/state.json').write_text(json.dumps(state))
    return root, state


def test_profile_rejects_changed_installed_bytes(tmp_path):
    root, _ = installed(tmp_path)
    (root / 'hd-fighters/manifest.ini').write_text('changed')
    with pytest.raises(ValueError, match='differs from verified receipt'):
        profile.prepare(root, tmp_path / 'profile.json')


def test_profile_rejects_wrong_requested_version(tmp_path):
    root, state = installed(tmp_path)
    state['installed']['menu-ui-hd-wide']['version'] = '1.3.0'
    (tmp_path / 'mod-manager/state.json').write_text(json.dumps(state))
    with pytest.raises(ValueError, match='1.4.0'):
        profile.prepare(root, tmp_path / 'profile.json')


def test_profile_is_explicit_and_records_both_archive_identities(tmp_path, monkeypatch):
    monkeypatch.setenv(profile.PROFILE_ENV, '')
    root, _ = installed(tmp_path)
    result = profile.prepare(root, tmp_path / 'profile.json')
    assert profile.current() == result
    assert result['environment']['RECOMP_ASPECT'] == '16:9'
    assert result['environment']['RECOMP_TEXTURE_PACKS'] == 'menu-ui-hd-wide;hd-fighters'
    assert {key: value['version'] for key, value in result['packs'].items()} == profile.PACKS
    # The profile is outside the RECOMP_* diagnostics suppressed by scenarios.
    assert not profile.PROFILE_ENV.startswith('RECOMP_')


def test_runtime_must_really_initialize_widescreen_and_texture_packs(tmp_path, monkeypatch):
    root, _ = installed(tmp_path)
    monkeypatch.setenv(profile.PROFILE_ENV, '')
    result = profile.prepare(root, tmp_path / 'profile.json')
    run = SimpleNamespace(identity={'regression_profile': result, 'environment': result['environment'].copy()},
                          text='[FUNCCALL] menu')
    assert len(profile.faults(run)) == 2
    run.text += '\n[WS] widescreen 1.7778\n[TEXPACK] schema=1 entries=2'
    assert profile.faults(run) == []
    run.text = run.text.replace('entries=2', 'entries=1')
    assert 'all verified manifests' in profile.faults(run)[0]
    run.text = run.text.replace('entries=1', 'entries=2')
    run.identity['environment']['RECOMP_ASPECT'] = '4:3'
    assert 'RECOMP_ASPECT differs' in profile.faults(run)[0]


def generated_lanczos(tmp_path, root):
    aliases = json.loads((ROOT / 'config/hd-runtime-aliases.json').read_text())['aliases']
    sources = sorted(set(aliases.values()) | {'b' * 64})
    recipe = 'c' * 64
    corpus_dir = tmp_path / 'corpus'
    corpus_dir.mkdir()
    assets, names = [], []
    for policy in ('opacity', 'channels'):
        name = 'faithful-hd-' + recipe[:12] + '-' + policy
        names.append(name)
        folder = root / name
        (folder / 'textures').mkdir(parents=True)
        entries = []
        for index, source in enumerate(sources):
            if ('opacity' if index % 2 else 'channels') != policy:
                continue
            image = folder / 'textures' / (source + '.png')
            image.write_bytes(('image:' + source).encode())
            assets.append({'id': source, 'upscaled_hash': profile.digest(image)})
            entries.append(source + '=textures/' + source + '.png\n')
            entries.extend(key + '=textures/' + source + '.png\n'
                           for key, target in aliases.items() if target == source)
        (folder / 'manifest.ini').write_text('[pack]\nschema=1\npng_mips=' + policy +
                                            '\n[textures]\n' + ''.join(entries))
        (folder / 'builder.json').write_text(json.dumps(dict(schema=1,
            product='DefJamFaithfulHD', recipe_id=recipe, png_mips=policy)))
    (corpus_dir / 'corpus.json').write_text(json.dumps({'corpus_id': recipe, 'assets': assets}))
    receipt = tmp_path / 'lanczos.json'
    receipt.write_text(json.dumps({'recipe_id': recipe, 'corpus': str(corpus_dir), 'packs': names}))
    return receipt, names


def test_lanczos_profile_verifies_all_images_and_preserves_override_order(tmp_path, monkeypatch):
    monkeypatch.setenv(profile.PROFILE_ENV, '')
    root, _ = installed(tmp_path)
    receipt, names = generated_lanczos(tmp_path, root)
    result = profile.prepare(root, tmp_path / 'profile.json', receipt)
    assert result['environment']['RECOMP_TEXTURE_PACKS'].split(';') == names + list(profile.PACKS)
    assert result['expected_entries'] == 2 + sum(result['packs'][name]['entries'] for name in names)
    image = next((root / names[0] / 'textures').glob('*.png'))
    image.write_bytes(b'changed')
    with pytest.raises(ValueError, match='differs from verified corpus'):
        profile.prepare(root, tmp_path / 'profile.json', receipt)


def test_lanczos_profile_rejects_partial_corpus(tmp_path):
    root, _ = installed(tmp_path)
    receipt, names = generated_lanczos(tmp_path, root)
    manifest = root / names[0] / 'manifest.ini'
    manifest.write_text(manifest.read_text().rsplit('\n', 2)[0] + '\n')
    with pytest.raises(ValueError, match='do not cover the full corpus'):
        profile.prepare(root, tmp_path / 'profile.json', receipt)


def test_lanczos_profile_rejects_different_recipe(tmp_path):
    root, _ = installed(tmp_path)
    receipt, _ = generated_lanczos(tmp_path, root)
    value = json.loads(receipt.read_text())
    value['recipe_id'] = 'd' * 64
    receipt.write_text(json.dumps(value))
    with pytest.raises(ValueError, match='recipe does not match'):
        profile.prepare(root, tmp_path / 'profile.json', receipt)

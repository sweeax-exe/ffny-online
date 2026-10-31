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
        manifest.write_text('[pack]\nschema=1\n')
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
    assert result['environment']['RECOMP_TEXTURE_PACKS'] == 'menu-ui-hd-wide,hd-fighters'
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
    run.text += '\n[WS] widescreen 1.7778\n[TEXPACK] schema=1 entries=1561'
    assert profile.faults(run) == []
    run.identity['environment']['RECOMP_ASPECT'] = '4:3'
    assert 'RECOMP_ASPECT differs' in profile.faults(run)[0]

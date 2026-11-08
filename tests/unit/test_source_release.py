"""Source publication boundaries and version mistakes must stop a release."""
import importlib.util
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


source = load('check-source-tree')
release = load('check-release')
msvc = load('test-toolkit-msvc')


@pytest.mark.parametrize('name', [
    'src/recomp/gen/recomp_dispatch.c', 'docs/reference.png', 'game/data.xml',
    'logs/audio.pcm', 'build/game.exe', 'save/profile.dat', 'payload.zip',
    'docs/media/unapproved.png', 'docs/media/unapproved.gif',
])
def test_distribution_rejects_local_game_and_binary_paths(name):
    assert source.path_problem(name)


@pytest.mark.parametrize('name', ['src/hooks/save_compat.c', 'config/dump-manifest.json',
                                  'src/recomp/gen/.gitkeep', 'patches/xboxrecomp/0001.patch'])
def test_maintained_sources_and_hashes_are_allowed(name):
    assert source.path_problem(name) is None


def test_binary_content_cannot_hide_in_a_text_extension():
    assert source.text_problem(b'header\0payload', 'notes.txt')


@pytest.mark.parametrize('name', sorted(source.APPROVED_README_MEDIA))
def test_only_exact_owner_approved_readme_media_is_allowed(name):
    data = (ROOT / name).read_bytes()
    assert source.path_problem(name) is None
    assert source.text_problem(data, name) is None
    assert source.text_problem(data + b'changed', name)


def test_guest_body_cannot_hide_in_markdown():
    text = b'```c\nvoid sub_00ABCDEF(void) { eax = MEM32(esp); }\n```\n'
    assert source.text_problem(text, 'docs/report.md')


def test_symbolic_hook_documentation_is_allowed():
    assert source.text_problem(b'```c\nvoid sub_XXXXXXXX_enter(void);\n```\n', 'docs/hooks.md') is None


def test_indented_guest_excerpt_is_also_rejected():
    assert source.text_problem(b'  ```c\n  void sub_00ABCDEF(void) { eax = MEM32(esp); }\n  ```\n', 'report.md')


def test_changed_toolkit_fixture_stops_compiler_override(tmp_path):
    helper = tmp_path / 'tools/recomp/test_lifter_result_clobber.py'
    helper.parent.mkdir(parents=True)
    helper.write_text('def _cc(): return "unexpected compiler"\n', encoding='utf-8')
    with pytest.raises(pytest.UsageError, match='Pinned fixture changed'):
        msvc.MSVCFixtures(tmp_path, 'cl').pytest_collection_modifyitems(None, None, [])


def test_release_requires_matching_version_and_notes():
    release.validate_version('v0.1.0', 'project(defjam_recomp VERSION 0.1.0 LANGUAGES C)', True)
    with pytest.raises(ValueError, match='CMake'):
        release.validate_version('v0.2.0', 'project(defjam_recomp VERSION 0.1.0 LANGUAGES C)', True)
    with pytest.raises(ValueError, match='notes|Missing'):
        release.validate_version('v0.1.0', 'project(defjam_recomp VERSION 0.1.0 LANGUAGES C)', False)


def test_beta_tag_matches_the_cmake_version():
    release.validate_version('v0.1.0-beta.1', 'project(defjam_recomp VERSION 0.1.0 LANGUAGES C)', True)
    release.validate_version('v0.1.0-beta.12', 'project(defjam_recomp VERSION 0.1.0 LANGUAGES C)', True)
    with pytest.raises(ValueError, match='CMake'):
        release.validate_version('v0.2.0-beta.1', 'project(defjam_recomp VERSION 0.1.0 LANGUAGES C)', True)


@pytest.mark.parametrize('tag', ['preview', 'v0.1.0-rc1', '../../notes', 'v0.1', 'v0.1.0-beta', 'v0.1.0-beta.0',
                                 'v0.1.0-beta.1/../x', 'v0.1.0-alpha.1'])
def test_release_rejects_invalid_tags(tag):
    with pytest.raises(ValueError, match='vMAJOR'):
        release.validate_version(tag, 'project(defjam_recomp VERSION 0.1.0)', True)


@pytest.mark.parametrize('case', ['valid', 'wrong_checkout', 'outside_main', 'dirty'])
def test_tag_validation_against_a_real_synthetic_repository(tmp_path, case):
    if not shutil.which('git'):
        pytest.skip('Git unavailable')
    (tmp_path / 'scripts').mkdir()
    (tmp_path / 'docs/releases').mkdir(parents=True)
    for name in ['check-release.py', 'check-source-tree.py']:
        shutil.copyfile(ROOT / 'scripts' / name, tmp_path / 'scripts' / name)
    (tmp_path / 'CMakeLists.txt').write_text('project(defjam_recomp VERSION 0.1.0 LANGUAGES C)\n')
    (tmp_path / 'docs/releases/v0.1.0.md').write_text('Synthetic source release\n')

    def git(*args):
        subprocess.run(['git', *args], cwd=tmp_path, check=True, capture_output=True)

    git('init', '-b', 'main')
    git('config', 'user.name', 'Synthetic fixture')
    git('config', 'user.email', 'fixture@example.invalid')
    git('add', '.')
    git('commit', '-m', 'Synthetic sources')
    git('update-ref', 'refs/remotes/origin/main', 'HEAD')
    if case == 'outside_main':
        git('switch', '-c', 'unmerged')
        (tmp_path / 'extra.txt').write_text('Synthetic change\n')
        git('add', '.')
        git('commit', '-m', 'Unmerged source')
    git('tag', 'v0.1.0')
    if case == 'wrong_checkout':
        (tmp_path / 'extra.txt').write_text('Synthetic later change\n')
        git('add', '.')
        git('commit', '-m', 'Later source')
    if case == 'dirty':
        (tmp_path / 'CMakeLists.txt').write_text('project(defjam_recomp VERSION 0.1.0 LANGUAGES C)\n# dirty\n')
    result = subprocess.run([sys.executable, str(tmp_path / 'scripts/check-release.py'), 'v0.1.0'],
                            cwd=tmp_path, capture_output=True, text=True)
    assert (result.returncode == 0) == (case == 'valid'), result.stdout + result.stderr


@pytest.mark.parametrize('name', [
    'config/menu-ui-release.json', 'config/menu-ui-wave01-approval.json',
    'authoring/review.json', 'workspaces/menu/revisions.json', 'candidates/draft.json',
    'project.local.json', 'notes.local.md',
])
def test_authoring_records_and_machine_paths_stay_private(name):
    assert source.path_problem(name)


@pytest.mark.parametrize('name', ['config/hd-runtime-aliases.json', 'config/mods/catalogue-keys.json',
                                 'docs/menu-ui-modpack-handover.md'])
def test_runtime_texture_contracts_and_integration_notes_remain_allowed(name):
    assert source.path_problem(name) is None


@pytest.mark.parametrize('record', [
    b'{"approval_records": [], "reviewed_sources": []}',
    b'{"recovery_path": "/private/example", "manifest_sha256": "abc"}',
    b'{"owner_message": "approved", "approved_records": []}',
    b'{"generation_records": [], "selected_exports": []}',
])
def test_production_ledgers_cannot_hide_under_other_config_names(record):
    assert source.text_problem(record, 'config/custom-pack.json')


def test_public_runtime_alias_config_is_not_an_authoring_ledger():
    assert source.text_problem(b'{"schema": 1, "aliases": []}', 'config/hd-runtime-aliases.json') is None


@pytest.mark.skipif(sys.platform == 'win32' or not shutil.which('bash'), reason='Release workflow runs in Bash on Ubuntu')
@pytest.mark.parametrize('tag,missing', [('v1.1.0', False), ('v1.1.0-beta.2', False), ('v1.1.0', True)])
def test_release_uploads_only_app_assets_and_stops_if_one_is_missing(tmp_path, tag, missing):
    workflow = (ROOT / '.github/workflows/release.yml').read_text()
    block = re.search(r'^        run: \|\n((?:          .*\n|\n)+)', workflow, re.M).group(1)
    script = '\n'.join(line[10:] for line in block.splitlines())
    folder = tmp_path / 'setup-assets'
    folder.mkdir()
    expected = []
    for suffix in ('windows-x64.exe', 'macos-arm64.zip', 'linux-x86_64.AppImage'):
        name = 'DefJamRecompiled-1.1.0-' + suffix
        stem = name.rsplit('.', 1)[0]
        for asset in (name, stem + '.sha256', stem + '.provenance.json'):
            (folder / asset).write_text('synthetic asset')
            expected.append('setup-assets/' + asset)
    # An old local build must never add its setup file to the public release.
    (folder / 'DefJamSetup-1.1.0-windows-x64.exe').write_text('legacy setup')
    if missing:
        (tmp_path / expected[-1]).unlink()
    # Capture the exact arguments handed to gh; this test never contacts GitHub.
    stub = 'gh() { printf "%s\\n" "$@" > published-args; }\n'
    result = subprocess.run(['bash', '-e'], input=stub + script, cwd=tmp_path,
                            env={**os.environ, 'RELEASE_TAG': tag}, capture_output=True, text=True)
    if missing:
        assert result.returncode != 0
        assert 'Missing app release asset' in result.stdout
        assert not (tmp_path / 'published-args').exists()
    else:
        assert result.returncode == 0, result.stderr
        args = (tmp_path / 'published-args').read_text().splitlines()
        assert args[-9:] == expected
        assert not any('DefJamSetup' in arg for arg in args)
        assert ('--prerelease' in args) == ('-beta.' in tag)

"""Installer boundaries and recovery behavior with synthetic, game-free data."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


engine = load('setup_engine', ROOT / 'setup/engine.py')
package = load('setup_package', ROOT / 'scripts/package-setup.py')
assets = load('setup_assets', ROOT / 'scripts/check-setup-assets.py')


def payload(tmp_path):
    folder = tmp_path / 'payload'
    folder.mkdir()
    (folder / 'source/scripts').mkdir(parents=True)
    (folder / 'source/config').mkdir()
    (folder / 'source/src').mkdir()
    (folder / 'source/config/dump-manifest.json').write_text('{}')
    (folder / 'source/scripts/verify-dump.py').write_text(
        "def verify(folder, manifest):\n    from pathlib import Path\n    return [] if (Path(folder)/'default.xbe').read_bytes() == b'synthetic' else ['wrong dump']\n")
    (folder / 'source/src/maintained.c').write_text('/* synthetic source */')
    inventory = {p.relative_to(folder).as_posix(): engine.digest(p) for p in folder.rglob('*') if p.is_file()}
    manifest = dict(schema=1, platform=engine.HOST_PLATFORM, version='0.4.1',
                    source_commit='a'*40, toolkit_commit='b'*40, files=inventory)
    engine.write_json(folder / 'manifest.json', manifest)
    return folder


def installer(tmp_path):
    folder = payload(tmp_path)
    dump = tmp_path / 'dump'
    dump.mkdir()
    (dump / 'default.xbe').write_bytes(b'synthetic')
    (dump / 'other.txt').write_bytes(b'files')
    args = argparse.Namespace(payload=str(folder), install_dir=str(tmp_path/'app'),
        data_dir=str(tmp_path/'data'), dump=str(dump), log=None, status_file=None,
        cancel_file=None, install_prerequisites=False, silent=True, no_shortcuts=True, no_desktop_shortcut=False)
    result = engine.Engine(args)
    result.data.mkdir()
    result.prepare_source()
    return result


@pytest.mark.parametrize('name', ['../escape', '/escape', 'C:/escape', 'a\\b', 'foo/../bar',
                                  'CON', 'nul.txt', 'COM1.log', 'a/b.', 'a//b'])
def test_payload_and_image_names_cannot_escape_or_alias(name):
    with pytest.raises(engine.SetupError):
        engine.safe_relative(name)


def test_payload_tampering_and_extra_files_fail_before_execution(tmp_path):
    folder = payload(tmp_path)
    assert engine.verify_payload(folder)['version'] == '0.4.1'
    extra = folder / 'rogue.py'
    extra.write_text('unexpected')
    with pytest.raises(engine.SetupError, match='Unexpected'):
        engine.verify_payload(folder)
    extra.unlink()
    (folder / 'source/src/maintained.c').write_text('tampered')
    with pytest.raises(engine.SetupError, match='verification'):
        engine.verify_payload(folder)


@pytest.mark.parametrize('data,dump', [('app/data','dump'), ('data','app'), ('data','data/dump')])
def test_nested_destinations_are_refused(tmp_path, data, dump):
    with pytest.raises(engine.SetupError, match='separate'):
        engine.validate_locations(tmp_path/'app', tmp_path/data, tmp_path/dump)


def test_extraction_resume_requires_outputs_and_preserves_user_saves(tmp_path):
    setup = installer(tmp_path)
    saves = setup.data / 'save'
    saves.mkdir()
    (saves / 'profile').write_bytes(b'keep exactly')
    setup.extract()
    setup.extract()
    (setup.data/'extracted/other.txt').write_bytes(b'corrupted')
    with pytest.raises(engine.SetupError, match='differs'):
        setup.extract()
    assert (saves/'profile').read_bytes() == b'keep exactly'
    assert (setup.dump/'other.txt').read_bytes() == b'files'


def test_failed_extraction_never_publishes_a_partial_dump(tmp_path):
    setup = installer(tmp_path)
    (setup.dump/'default.xbe').write_bytes(b'wrong')
    with pytest.raises(engine.SetupError, match='verification'):
        setup.extract()
    assert not (setup.data/'extracted').exists()
    assert not list(setup.data.glob('extract-pending-*'))


def test_resume_repairs_source_without_removing_build_artifacts(tmp_path):
    setup = installer(tmp_path)
    (setup.source/'src/maintained.c').write_text('corrupted')
    generated = setup.source / 'src/recomp/gen/recomp_000.c'
    generated.parent.mkdir(parents=True)
    generated.write_text('/* synthetic generated fixture */')
    setup.prepare_source()
    assert (setup.source/'src/maintained.c').read_text() == '/* synthetic source */'
    assert generated.exists()


def test_process_arguments_are_literal_and_failed_stage_is_not_success(tmp_path):
    setup = installer(tmp_path)
    script = tmp_path / 'path with spaces & quote.py'
    script.write_text('import sys\nprint(sys.argv[1])\nsys.exit(7)\n')
    with pytest.raises(engine.SetupError, match='exit 7'):
        setup.run([sys.executable, script, '$(not a shell); & literal'])


def test_cancellation_prevents_a_new_stage(tmp_path):
    setup = installer(tmp_path)
    cancel = tmp_path/'cancel'
    cancel.touch()
    setup.args.cancel_file = str(cancel)
    with pytest.raises(engine.SetupError) as error:
        setup.run([sys.executable, '-c', 'raise RuntimeError("should not execute")'])
    assert error.value.code == 6


def test_install_lock_excludes_another_process_and_recovers(tmp_path):
    root = tmp_path/'app'
    with engine.install_lock(root):
        with pytest.raises(engine.SetupError) as error:
            with engine.install_lock(root):
                pass
        assert error.value.code == 5
    with engine.install_lock(root):
        pass


def test_dependency_zip_traversal_is_rejected(tmp_path):
    archive = tmp_path/'unsafe.zip'
    with zipfile.ZipFile(archive, 'w') as stream:
        stream.writestr('../escape.txt', 'bad')
    with pytest.raises(ValueError, match='Unsafe'):
        package.safe_unpack(archive, tmp_path/'output')
    assert not (tmp_path/'escape.txt').exists()


@pytest.mark.parametrize('name', ['src/recomp/gen/recomp_00.c', 'game_files/default.xbe',
                                 'notes/hidden.exe', 'logs/foo.txt', 'audio/test.wav'])
def test_release_payload_source_filter_excludes_game_and_binary_files(name):
    assert not package.source_allowed(name, True)


def test_the_website_stays_out_of_the_setup_payload():
    assert not package.source_allowed('website/index.html')
    assert not package.source_allowed('website/build.py')


def test_release_asset_exception_is_exact_and_cannot_admit_the_game():
    names = ['DefJamSetup-0.5.0-windows-x64' + ext for ext in ('.exe', '.sha256', '.provenance.json')]
    assets.validate_asset_names('0.5.0', names)
    with pytest.raises(ValueError):
        assets.validate_asset_names('0.5.0', names + ['defjam_recomp.exe'])


def test_payload_manifest_cannot_disable_file_checks(tmp_path):
    folder = payload(tmp_path)
    manifest = json.loads((folder/'manifest.json').read_text())
    manifest['files'] = {}
    engine.write_json(folder/'manifest.json', manifest)
    with pytest.raises(engine.SetupError, match='Unexpected'):
        engine.verify_payload(folder)


def test_cancel_mid_copy_does_not_publish_or_remove_saves(tmp_path, monkeypatch):
    setup = installer(tmp_path)
    save = setup.data / 'save'
    save.mkdir()
    (save/'profile').write_bytes(b'keep')
    def cancel():
        raise engine.SetupError('cancel', 6)
    monkeypatch.setattr(setup, 'cancelled', cancel)
    with pytest.raises(engine.SetupError):
        setup.extract()
    assert not (setup.data/'extracted').exists()
    assert not list(setup.data.glob('extract-pending-*'))
    assert (save/'profile').read_bytes() == b'keep'


def test_new_release_only_reuses_lift_after_pipeline_validation(tmp_path):
    setup = installer(tmp_path)
    old = setup.install / 'versions/old/source'
    (old / 'src/recomp/gen').mkdir(parents=True)
    (old / 'src/recomp/gen/lift-state.json').write_text('{"synthetic":"uncertified"}')
    (old / 'src/recomp/gen/recomp_00.c').write_text('/* synthetic */')
    engine.write_json(setup.install/'installed.json', {'source': str(old)})
    setup.prepare_source()
    assert (setup.source/'src/recomp/gen/lift-state.json').exists()
    # Cached files never imply successful certification: a real verifier reads
    # inputs/outputs and will reject this deliberately incomplete state.
    assert json.loads((setup.source/'src/recomp/gen/lift-state.json').read_text()) == {'synthetic':'uncertified'}


def test_unreadable_earlier_attempt_is_skipped_not_fatal(tmp_path, monkeypatch):
    setup = installer(tmp_path)
    good = setup.install / 'versions/0.4.0-good/source/src/recomp/gen'
    good.mkdir(parents=True)
    (good / 'lift-state.json').write_text('{"synthetic":"good"}')
    stuck = setup.install / 'versions/0.4.1-stuck-attempt-633ae679/source/src/recomp/gen/lift-state.json'
    stuck.parent.mkdir(parents=True)
    stuck.write_text('{"synthetic":"stuck"}')
    real_is_file = Path.is_file

    def is_file(path, *args, **kwargs):
        # Windows reports a delete-pending file as access denied; Python 3.13 raises it.
        if path == stuck:
            raise PermissionError(13, 'Access is denied', str(path))
        return real_is_file(path, *args, **kwargs)
    monkeypatch.setattr(Path, 'is_file', is_file)
    setup.prepare_source()
    assert json.loads((setup.source/'src/recomp/gen/lift-state.json').read_text()) == {'synthetic':'good'}


def test_failed_reuse_copy_is_discarded_not_fatal(tmp_path, monkeypatch):
    setup = installer(tmp_path)
    old = setup.install / 'versions/old/source'
    (old / 'src/recomp/gen').mkdir(parents=True)
    (old / 'src/recomp/gen/lift-state.json').write_text('{"synthetic":"uncertified"}')
    engine.write_json(setup.install/'installed.json', {'source': str(old)})

    def copytree(src, dst, **kwargs):
        Path(dst).mkdir(parents=True)
        (Path(dst) / 'recomp_00.c').write_text('/* partial */')
        raise PermissionError(13, 'Access is denied', str(src))
    monkeypatch.setattr(engine.shutil, 'copytree', copytree)
    setup.prepare_source()
    assert not (setup.source/'src/recomp/gen').exists()
    assert (setup.source/'src/maintained.c').read_text() == '/* synthetic source */'


def test_update_cannot_switch_the_data_root(tmp_path):
    setup = installer(tmp_path)
    engine.write_json(setup.install/'installed.json', {'source':str(setup.source),
        'data':str(tmp_path/'other-data'), 'executable':str(setup.source/'game.exe')})
    with pytest.raises(engine.SetupError, match='data folder'):
        setup.check_existing_install()


def test_install_refuses_unrelated_nonempty_destination(tmp_path):
    setup = installer(tmp_path)
    (setup.install/'personal.txt').write_bytes(b'keep')
    with pytest.raises(engine.SetupError, match='empty install folder'):
        setup.check_install_destination()
    assert (setup.install/'personal.txt').read_bytes() == b'keep'


def test_owned_failed_install_can_resume_without_a_launch_receipt(tmp_path):
    setup = installer(tmp_path)
    engine.write_json(setup.install/'setup-owner.json', {'schema':1, 'product':'DefJamRecompiled'})
    setup.check_install_destination()
    assert (setup.source/'src/maintained.c').exists()


@pytest.mark.skipif(sys.platform != 'win32', reason='CMD environment expansion')
def test_toolchain_handoff_accepts_spaces_ampersands_and_literal_percent(tmp_path):
    setup = installer(tmp_path)
    batch = tmp_path/'space & %PATH% folder'/'toolchain.cmd'
    batch.parent.mkdir()
    batch.write_text('@echo off\necho TOOLCHAIN_OK\n')
    setup.env['DEFJAM_SETUP_ENV_SCRIPT'] = '"' + str(batch) + '"'
    output = setup.run(['cmd.exe', '/d', '/c', '%DEFJAM_SETUP_ENV_SCRIPT%'], capture=True)
    assert output.strip() == 'TOOLCHAIN_OK'


def test_captured_toolchain_environment_is_not_written_to_logs(tmp_path):
    import io
    setup = installer(tmp_path)
    setup.log = io.StringIO()
    setup.env['SETUP_TEST_CAPTURE'] = 'private-environment-fixture'
    output = setup.run([sys.executable, '-c', 'import os; print(os.environ["SETUP_TEST_CAPTURE"])'],
                       capture=True, log_output=False)
    assert output.strip() == setup.env['SETUP_TEST_CAPTURE']
    assert setup.env['SETUP_TEST_CAPTURE'] not in setup.log.getvalue()


def test_nested_destination_failure_creates_log_before_engine_initialization(tmp_path):
    folder = payload(tmp_path)
    log = tmp_path/'diagnostics/session.log'
    code = engine.main(['--payload', str(folder), '--silent', '--dump', str(tmp_path/'dump'),
        '--install-dir', str(tmp_path/'app'), '--data-dir', str(tmp_path/'app/data'), '--log', str(log)])
    assert code == 2
    assert 'separate, non-nested' in log.read_text()
    assert not (tmp_path/'app').exists()


@pytest.mark.skipif(not engine.IS_WINDOWS, reason='Windows launcher and Start-menu shortcut')
@pytest.mark.parametrize('desktop', [True, False])
def test_desktop_choice_preserves_start_menu_shortcut_and_launch_guidance(tmp_path, monkeypatch, capsys, desktop):
    setup = installer(tmp_path)
    (setup.payload/'DefJamLauncher.exe').write_bytes(b'synthetic launcher')
    setup.args.no_shortcuts = False
    setup.args.no_desktop_shortcut = not desktop
    calls = []
    monkeypatch.setattr(setup, 'run', lambda argv: calls.append((argv, setup.env['DEFJAM_SETUP_DESKTOP_SHORTCUT'])))
    setup.activate()
    assert len(calls) == 1
    assert calls[0][1] == ('1' if desktop else '0')
    assert str(calls[0][0][-1]).endswith('shortcuts.ps1')
    assert str(setup.install/'DefJamLauncher.exe') in capsys.readouterr().out
    assert (setup.install/'installed.ini').exists()


@pytest.mark.skipif(not (ROOT/'tools/xboxrecomp/tools/xiso').is_dir(), reason='requires recursive checkout')
def test_image_traversal_is_rejected_before_any_file_publication(tmp_path):
    setup = installer(tmp_path)
    # Minimal XDVDFS descriptor/directory: malicious name with a real in-bounds body.
    import struct
    image = bytearray(35 * 2048)
    image[32*2048:32*2048+20] = b'MICROSOFT*XBOX*MEDIA'
    struct.pack_into('<II', image, 32*2048+0x14, 33, 2048)
    name = b'../outside'
    struct.pack_into('<HHIIBB', image, 33*2048, 0, 0, 34, 1, 0, len(name))
    image[33*2048+14:33*2048+14+len(name)] = name
    iso = tmp_path/'bad.iso'
    iso.write_bytes(image)
    setup.dump = iso
    # Fixture source needs the exact pinned reader, not a different extractor.
    import shutil
    shutil.copytree(ROOT/'tools/xboxrecomp/tools/xiso', setup.source/'tools/xboxrecomp/tools/xiso')
    with pytest.raises(engine.SetupError, match='Unsafe'):
        setup.extract()
    assert not (setup.data/'extracted').exists()
    assert not (setup.data/'outside').exists()


@pytest.mark.skipif(not (ROOT/'tools/xboxrecomp/tools/xiso').is_dir(), reason='requires recursive checkout')
def test_valid_synthetic_xiso_extracts_and_resumes(tmp_path):
    import shutil
    import struct
    setup = installer(tmp_path)
    image = bytearray(35 * 2048)
    image[32*2048:32*2048+20] = b'MICROSOFT*XBOX*MEDIA'
    struct.pack_into('<II', image, 32*2048+0x14, 33, 2048)
    name, body = b'default.xbe', b'synthetic'
    struct.pack_into('<HHIIBB', image, 33*2048, 0, 0, 34, len(body), 0, len(name))
    image[33*2048+14:33*2048+14+len(name)] = name
    image[34*2048:34*2048+len(body)] = body
    setup.dump = tmp_path/'valid.iso'
    setup.dump.write_bytes(image)
    shutil.copytree(ROOT/'tools/xboxrecomp/tools/xiso', setup.source/'tools/xboxrecomp/tools/xiso')
    setup.extract()
    setup.extract()
    assert (setup.data/'extracted/default.xbe').read_bytes() == body


@pytest.mark.skipif(not (ROOT/'tools/xboxrecomp/tools/xiso').is_dir(), reason='requires recursive checkout')
def test_invalid_disc_image_has_documented_exit_and_no_partial_output(tmp_path):
    import shutil
    setup = installer(tmp_path)
    setup.dump = tmp_path/'wrong.iso'
    setup.dump.write_bytes(b'not a disc')
    shutil.copytree(ROOT/'tools/xboxrecomp/tools/xiso', setup.source/'tools/xboxrecomp/tools/xiso')
    with pytest.raises(engine.SetupError, match='Invalid Xbox disc') as error:
        setup.extract()
    assert error.value.code == 2
    assert not (setup.data/'extracted').exists()
    assert not list(setup.data.glob('extract-pending-*'))


@pytest.mark.skipif(sys.platform != 'win32', reason='Windows lifetime lock')
def test_game_lock_blocks_setup_and_releases_after_close(tmp_path):
    with engine.game_lock(tmp_path):
        with pytest.raises(engine.SetupError) as error:
            with engine.game_lock(tmp_path):
                pass
        assert error.value.code == 5
    with engine.game_lock(tmp_path):
        pass


@pytest.mark.skipif(sys.platform != 'win32', reason='Windows uninstall')
def test_uninstall_preserves_data_and_unrelated_install_files(tmp_path, monkeypatch):
    setup = installer(tmp_path)
    (setup.data/'save').mkdir()
    (setup.data/'save/profile').write_bytes(b'keep')
    (setup.install/'unrelated.txt').write_bytes(b'keep too')
    engine.write_json(setup.install/'installed.json', {'schema':1, 'source':str(setup.source), 'data':str(setup.data)})
    (setup.install/'DefJamLauncher.exe').write_bytes(b'synthetic')
    monkeypatch.setattr(engine.subprocess, 'run', lambda *a, **k: None)
    engine.uninstall(setup.install)
    assert not (setup.install/'versions').exists()
    assert not (setup.install/'DefJamLauncher.exe').exists()
    assert (setup.install/'unrelated.txt').read_bytes() == b'keep too'
    assert (setup.data/'save/profile').read_bytes() == b'keep'


def synthetic_launcher_template(payload):
    template = payload / 'launcher' / engine.LAUNCHER_BUNDLE
    (template / 'Contents/MacOS').mkdir(parents=True)
    (template / 'Contents/MacOS/DefJamLauncher').write_bytes(b'synthetic launcher')
    return template


@pytest.mark.skipif(not engine.IS_MACOS, reason='macOS launcher bundle')
@pytest.mark.parametrize('desktop', [True, False])
def test_macos_launcher_bundle_shortcuts_follow_the_choices_and_stay_in_the_home_folder(
        tmp_path, monkeypatch, capsys, desktop):
    setup = installer(tmp_path)
    home = tmp_path / 'home'
    (home / 'Desktop').mkdir(parents=True)
    monkeypatch.setenv('HOME', str(home))
    synthetic_launcher_template(setup.payload)
    setup.args.no_shortcuts = False
    setup.args.no_desktop_shortcut = not desktop
    calls = []
    monkeypatch.setattr(setup, 'run', lambda argv, **kwargs: calls.append([str(a) for a in argv]))
    setup.activate()
    bundle = setup.install / engine.LAUNCHER_BUNDLE
    assert (bundle / 'Contents/Resources/install-root.txt').read_text() == str(setup.install)
    applications = home / 'Applications' / engine.LAUNCHER_BUNDLE
    assert (applications / 'Contents/Resources/install-root.txt').read_text() == str(setup.install)
    assert any('--make-alias' in call for call in calls) == desktop
    receipt = json.loads((setup.install / 'installed.json').read_text())
    assert str(applications) in receipt['shortcuts']
    assert (str(home / 'Desktop' / 'Def Jam Recompiled') in receipt['shortcuts']) == desktop
    assert str(bundle) in capsys.readouterr().out
    assert not (setup.install / 'installed.ini').exists()


@pytest.mark.skipif(not engine.IS_MACOS, reason='macOS launcher bundle')
def test_macos_no_shortcuts_leaves_the_home_folder_alone(tmp_path, monkeypatch):
    setup = installer(tmp_path)
    home = tmp_path / 'home'
    home.mkdir()
    monkeypatch.setenv('HOME', str(home))
    synthetic_launcher_template(setup.payload)
    monkeypatch.setattr(setup, 'run', lambda argv, **kwargs: None)
    setup.activate()
    assert (setup.install / engine.LAUNCHER_BUNDLE).is_dir()
    assert list(home.iterdir()) == []


@pytest.mark.skipif(not engine.IS_MACOS, reason='macOS launcher bundle')
def test_macos_uninstall_removes_only_recorded_shortcuts_that_still_name_this_install(tmp_path, monkeypatch):
    setup = installer(tmp_path)
    home = tmp_path / 'home'
    (home / 'Desktop').mkdir(parents=True)
    monkeypatch.setenv('HOME', str(home))
    synthetic_launcher_template(setup.payload)
    setup.args.no_shortcuts = False
    monkeypatch.setattr(setup, 'run', lambda argv, **kwargs: Path(argv[-1]).write_bytes(b'alias')
                        if '--make-alias' in [str(a) for a in argv] else None)
    setup.activate()
    mine = home / 'Applications' / engine.LAUNCHER_BUNDLE
    other = tmp_path / 'other-install'
    other.mkdir()
    (mine / 'Contents/Resources/install-root.txt').write_text(str(other))   # now names another install
    (setup.data / 'settings.ini').write_text('keep')
    engine.uninstall(setup.install)
    assert mine.is_dir()                      # not ours any more: left alone
    assert not (home / 'Desktop/Def Jam Recompiled').exists()
    assert not (setup.install / engine.LAUNCHER_BUNDLE).exists()
    assert (setup.data / 'settings.ini').read_text() == 'keep'


@pytest.mark.skipif(engine.IS_WINDOWS or not engine.IS_MACOS, reason='macOS toolchain')
def test_macos_setup_refuses_intel_and_missing_command_line_tools_with_the_documented_exit(tmp_path, monkeypatch):
    setup = installer(tmp_path)
    monkeypatch.setattr(engine.platform, 'machine', lambda: 'x86_64')
    with pytest.raises(engine.SetupError, match='Apple Silicon') as intel:
        setup.toolchain()
    assert intel.value.code == 3
    monkeypatch.setattr(engine.platform, 'machine', lambda: 'arm64')
    monkeypatch.setattr(engine.subprocess, 'run', lambda *a, **k: subprocess.CompletedProcess(a, 2, '', 'no tools'))
    with pytest.raises(engine.SetupError, match='Command Line Tools') as missing:
        setup.toolchain()
    assert missing.value.code == 3
    assert engine.APPLE_COMMAND_LINE_TOOLS_URL in str(missing.value)


@pytest.mark.skipif(engine.IS_WINDOWS, reason='POSIX process groups')
def test_cancellation_stops_the_whole_process_tree(tmp_path):
    import time
    setup = installer(tmp_path)
    marker = tmp_path / 'child.pid'
    cancel = tmp_path / 'cancel'
    setup.args.cancel_file = str(cancel)
    script = f"sleep 300 & echo $! > '{marker}'; wait"

    def raise_cancel():
        while not marker.exists():
            time.sleep(.05)
        cancel.write_text('cancel')

    import threading
    threading.Thread(target=raise_cancel, daemon=True).start()
    with pytest.raises(engine.SetupError) as stopped:
        setup.run(['sh', '-c', script])
    assert stopped.value.code == 6
    pid = int(marker.read_text())
    time.sleep(.3)
    with pytest.raises(ProcessLookupError):
        import os
        os.kill(pid, 0)


def test_hd_textures_are_refused_on_a_host_whose_renderer_cannot_draw_them(tmp_path, monkeypatch):
    monkeypatch.setattr(engine, 'HD_SUPPORTED', False)
    setup = installer(tmp_path)
    setup.args.hd_textures = True
    with pytest.raises(engine.SetupError, match='not supported') as refused:
        setup.execute()
    assert refused.value.code == 2
    assert not (setup.install / 'installed.json').exists()


def test_hd_textures_are_supported_on_windows_macos_and_linux():
    assert engine.HD_SUPPORTED == (engine.IS_WINDOWS or engine.IS_MACOS or engine.IS_LINUX)


def test_status_write_survives_a_wizard_holding_the_file(tmp_path, monkeypatch):
    """The wizard reads status.txt while setup writes it; a refused rename must not stop setup."""
    status = tmp_path / 'status.txt'
    real = engine.os.replace
    refusals = {'left': 3}

    def flaky(src, dst):
        if refusals['left']:
            refusals['left'] -= 1
            raise PermissionError(5, 'Access is denied')
        return real(src, dst)

    monkeypatch.setattr(engine.os, 'replace', flaky)
    monkeypatch.setattr(engine.time, 'sleep', lambda s: None)
    engine.write_status(status, 'Building')
    assert status.read_text(encoding='utf-8') == 'Building'

    def always(src, dst):
        raise PermissionError(5, 'Access is denied')

    monkeypatch.setattr(engine.os, 'replace', always)
    engine.write_status(status, 'Still building')      # no exception: the display misses one update
    engine.write_progress(status, 500)
    assert status.read_text(encoding='utf-8') == 'Building'
    assert not (tmp_path / 'status.pending').exists()


def test_each_stage_writes_its_step_beside_the_status_and_progress_files(tmp_path):
    """The launcher shows "Step n of m" from <status>.stage; status and progress keep their formats."""
    setup = installer(tmp_path)
    status = tmp_path / 'status.txt'
    setup.args.status_file = str(status)
    stage = Path(str(status) + '.stage')
    progress = Path(str(status) + '.progress')
    setup.stage('source')
    assert stage.read_text(encoding='utf-8') == '1 7 source Getting the new version'
    setup.stage('lift')
    assert stage.read_text(encoding='utf-8') == '5 7 lift Translating the game code'
    assert progress.read_text(encoding='ascii') == '170'
    setup.event('Lift in progress (this may take several minutes)')
    assert status.read_text(encoding='utf-8') == 'Lift in progress (this may take several minutes)'
    setup.stage('hd')                       # no HD textures: not a step, the step shown stays
    assert stage.read_text(encoding='utf-8') == '5 7 lift Translating the game code'
    setup.stage('activate')
    assert stage.read_text(encoding='utf-8') == '7 7 activate Finishing up'
    assert not Path(str(status) + '.stage-pending').exists()

    setup.args.hd_textures = True
    setup.stage('hd')
    assert stage.read_text(encoding='utf-8') == '7 8 hd HD textures'
    setup.stage('activate')
    assert stage.read_text(encoding='utf-8') == '8 8 activate Finishing up'
    # every stage the progress bar knows has a name, and the names fit one line on the card
    assert set(engine.STAGE_NAMES) == set(engine.PROGRESS_STAGES)
    assert all(len(name) <= 40 and ' ' not in name[:1] for name in engine.STAGE_NAMES.values())


def test_a_folder_around_the_dump_finds_the_one_image_or_game_folder(tmp_path):
    around = tmp_path / 'around'
    (around / 'notes').mkdir(parents=True)
    image = around / 'game.ISO'
    image.write_bytes(b'image')
    assert engine.locate_dump(around) == image.resolve()
    image.unlink()
    game = around / 'dump' / 'Def Jam'
    game.mkdir(parents=True)
    (game / 'default.xbe').write_bytes(b'synthetic')
    assert engine.locate_dump(around) == game.resolve()
    assert engine.locate_dump(game) == game


def test_a_folder_without_a_dump_says_what_to_choose(tmp_path):
    empty = tmp_path / 'empty'
    empty.mkdir()
    with pytest.raises(engine.SetupError, match='Choose the Xbox disc image'):
        engine.locate_dump(empty)
    (empty / 'a.iso').write_bytes(b'1')
    (empty / 'b.iso').write_bytes(b'2')
    with pytest.raises(engine.SetupError, match='more than one'):
        engine.locate_dump(empty)


def test_old_versions_are_pruned_keeping_this_and_the_previous_one(tmp_path):
    setup = installer(tmp_path)
    versions = setup.install / 'versions'
    old = [versions / ('0.7.2-%012x-%012x' % (i, i)) for i in range(3)]
    for folder in old:
        (folder / 'source').mkdir(parents=True)
    other = versions / 'notes'
    other.mkdir()
    previous = json.dumps({'source': str(old[1] / 'source')}).encode()
    setup.prune_versions(previous)
    assert setup.version_root.exists() and old[1].exists() and other.exists()
    assert not old[0].exists() and not old[2].exists()
    setup.prune_versions(None)
    assert setup.version_root.exists() and not old[1].exists()


def test_a_dangling_game_link_is_replaced(tmp_path):
    setup = installer(tmp_path)
    game = setup.source / 'game'
    gone = tmp_path / 'gone'
    gone.mkdir()
    try:
        game.symlink_to(gone, target_is_directory=True)
    except OSError:
        pytest.skip('symbolic links need privileges here')
    gone.rmdir()
    calls = []
    setup.run = lambda *a, **k: calls.append(a)
    if engine.IS_WINDOWS:
        setup.link_game()
        assert not os.path.lexists(game) and calls
    else:
        (setup.data / 'extracted').mkdir()
        setup.link_game()
        assert game.resolve() == (setup.data / 'extracted').resolve()


def test_generated_files_unchanged_since_the_previous_version_keep_their_time(tmp_path):
    """After a new lift only the generated files that really changed look new to Ninja."""
    old, new = tmp_path / 'old/source', tmp_path / 'new/source'
    for root in (old, new):
        (root / 'src/recomp/gen').mkdir(parents=True)
    for name, before, after in (('recomp_0001.c', b'same', b'same'), ('recomp_0002.c', b'old', b'new!'),
                                ('lift-state.json', b'{}', b'{}')):
        (old / 'src/recomp/gen' / name).write_bytes(before)
        (new / 'src/recomp/gen' / name).write_bytes(after)
        os.utime(old / 'src/recomp/gen' / name, ns=(1_000_000_000, 1_000_000_000))
    setup = engine.Engine.__new__(engine.Engine)
    setup.source, setup.previous_source = new, old
    setup.event = lambda text: None
    setup.keep_generated_times()
    gen = new / 'src/recomp/gen'
    assert (gen / 'recomp_0001.c').stat().st_mtime_ns == 1_000_000_000
    assert (gen / 'recomp_0002.c').stat().st_mtime_ns != 1_000_000_000
    assert (gen / 'lift-state.json').stat().st_mtime_ns != 1_000_000_000


# ---- The Windows wizard's questions: --identify, --find-games, --plan-folder (synthetic images only) ----
import hashlib
import struct


def fake_xbe(title_id=0x45410049, name='Def Jam: Fight for NY', region=1, pad=b''):
    """An XBE header with a certificate: what identification reads, nothing of a game."""
    data = bytearray(0x400)
    data[:4] = b'XBEH'
    struct.pack_into('<I', data, 0x104, 0x10000)
    struct.pack_into('<I', data, 0x118, 0x10000 + 0x200)
    struct.pack_into('<I', data, 0x200, 0x1D0)
    struct.pack_into('<I', data, 0x208, title_id)
    data[0x20C:0x20C + 0x50] = name.encode('utf-16-le')[:0x50].ljust(0x50, b'\0')
    struct.pack_into('<I', data, 0x2A0, region)
    return bytes(data) + pad


def wizard_source(tmp_path, xbe, required=('default.xbe', 'audio'), count=3):
    source = tmp_path / 'source'
    (source / 'config').mkdir(parents=True)
    (source / 'scripts').mkdir()
    (source / 'scripts/verify-dump.py').write_bytes((ROOT / 'scripts/verify-dump.py').read_bytes())
    (source / 'config/dump-manifest.json').write_text(json.dumps({
        'title_id': '0x45410049', 'xbe': {'filename': 'default.xbe', 'size': len(xbe), 'sha256': hashlib.sha256(xbe).hexdigest()},
        'required_paths': list(required), 'expected_file_count': count}))
    return source


def xdvdfs_image(path, files, base=0, cut=0):
    """A tiny Xbox disc image: files is {'name' or 'dir/name': bytes}, one directory level."""
    S = 2048
    sectors, data_sector = {}, 40

    def place(blob):
        nonlocal data_sector
        start = data_sector
        sectors[start] = blob
        data_sector += max(1, (len(blob) + S - 1) // S)
        return start

    def directory(entries):
        out = bytearray()
        for name, (sector, size, is_dir) in entries.items():
            raw = name.encode('ascii')
            record = struct.pack('<HHIIBB', 0, 0, sector, size, 0x10 if is_dir else 0x20, len(raw)) + raw
            out += record + b'\xff' * ((-len(record)) % 4)
        return bytes(out.ljust(S, b'\xff'))

    top, folders = {}, {}
    for name, blob in files.items():
        if '/' in name:
            folder, leaf = name.split('/', 1)
            folders.setdefault(folder, {})[leaf] = blob
        else:
            top[name] = (place(blob), len(blob), False)
    for folder, children in folders.items():
        listing = directory({leaf: (place(blob), len(blob), False) for leaf, blob in children.items()})
        top[folder] = (place(listing), len(listing), True)
    root = directory(top)
    root_sector = 33                                   # the root first, as image tools write it
    sectors[root_sector] = root
    image = bytearray(base + data_sector * S)
    descriptor = engine.XDVDFS_MAGIC + struct.pack('<II', root_sector, len(root))
    image[base + 32 * S:base + 32 * S + len(descriptor)] = descriptor
    for sector, blob in sectors.items():
        image[base + sector * S:base + sector * S + len(blob)] = blob
    path.write_bytes(bytes(image[:len(image) - cut] if cut else image))
    return path


def test_identify_accepts_only_the_manifest_game_complete(tmp_path):
    xbe = fake_xbe()
    source = wizard_source(tmp_path, xbe)
    good = xdvdfs_image(tmp_path / 'good.iso', {'default.xbe': xbe, 'audio/a.bin': b'1', 'b.bin': b'2'}, base=0xFD90)
    verdict = engine.identify_game(good, source)
    assert verdict['ok'] and verdict['kind'] == 'ok' and verdict['message'] == 'Xbox \u00b7 USA \u00b7 complete and undamaged'
    missing = xdvdfs_image(tmp_path / 'missing.iso', {'default.xbe': xbe, 'b.bin': b'2', 'c.bin': b'3'})
    assert engine.identify_game(missing, source)['kind'] == 'incomplete'
    cut = xdvdfs_image(tmp_path / 'cut.iso', {'default.xbe': xbe, 'audio/a.bin': b'1', 'b.bin': b'2' * 5000}, cut=3000)
    verdict = engine.identify_game(cut, source)
    assert verdict['kind'] == 'incomplete' and 'cut short' in verdict['message']
    # Cut inside default.xbe: its certificate still names this game; cut before it, nothing is claimed.
    big = fake_xbe(pad=bytes(20000))
    files = {'default.xbe': big, 'audio/a': b'1'}
    size = xdvdfs_image(tmp_path / 'n.iso', files).stat().st_size
    assert engine.identify_game(xdvdfs_image(tmp_path / 'c1.iso', files, cut=size - 40 * 2048 - 1500), source)['kind'] == 'incomplete'
    assert engine.identify_game(xdvdfs_image(tmp_path / 'c2.iso', files, cut=size - 40 * 2048 - 100), source)['kind'] == 'cut'


@pytest.mark.parametrize('xbe, kind, headline', [
    (fake_xbe(region=4, pad=b'pal'), 'pal', 'THIS IS THE EUROPEAN (PAL) VERSION'),
    (fake_xbe(title_id=0x12345678, name='Def Jam: Fight for NY', region=4), 'pal', 'THIS IS THE EUROPEAN (PAL) VERSION'),
    (fake_xbe(region=2, pad=b'jp'), 'japan', 'THIS IS THE JAPANESE VERSION'),
    (fake_xbe(region=0x80000007, pad=b'mod'), 'changed', "THIS ISN'T THE USA XBOX VERSION"),
    (fake_xbe(title_id=0x4D530004, name='Halo', region=7), 'other-game', 'THIS IS A DIFFERENT XBOX GAME'),
    (b'not an xbe at all', 'unknown', "THIS ISN'T THE USA XBOX VERSION"),
])
def test_identify_names_other_xbox_versions_only_as_far_as_it_can_tell(tmp_path, xbe, kind, headline):
    source = wizard_source(tmp_path, fake_xbe())
    verdict = engine.identify_game(xdvdfs_image(tmp_path / 'x.iso', {'default.xbe': xbe, 'audio/a': b'1'}), source)
    assert (verdict['ok'], verdict['kind'], verdict['headline']) == (False, kind, headline)
    if kind == 'other-game':
        assert '\u201cHalo\u201d' in verdict['message']


def iso9660(path, system=b'PLAYSTATION', cnf=b'BOOT2 = cdrom0:\\SLUS_000.00;1\r\n'):
    S = 2048
    image = bytearray(S * 24)
    pvd = bytearray(S)
    pvd[0], pvd[1:6], pvd[8:8 + 32] = 1, b'CD001', system.ljust(32)
    struct.pack_into('<I', pvd, 156 + 2, 20)
    struct.pack_into('<I', pvd, 156 + 10, S)
    image[16 * S:17 * S] = pvd
    name = b'SYSTEM.CNF;1'
    record = bytearray(33 + len(name) + 1)
    record[0], record[32] = len(record), len(name)
    struct.pack_into('<I', record, 2, 22)
    struct.pack_into('<I', record, 10, len(cnf))
    record[33:33 + len(name)] = name
    image[20 * S:20 * S + len(record)] = record
    image[22 * S:22 * S + len(cnf)] = cnf
    path.write_bytes(bytes(image))
    return path


def test_identify_tells_other_consoles_and_archives_by_their_headers(tmp_path):
    source = wizard_source(tmp_path, fake_xbe())
    assert engine.identify_game(iso9660(tmp_path / 'ps2.iso'), source)['kind'] == 'ps2'
    assert engine.identify_game(iso9660(tmp_path / 'ps1.iso', cnf=b'BOOT = cdrom:\\SLUS_000.00;1'), source)['kind'] == 'playstation'
    assert engine.identify_game(iso9660(tmp_path / 'pc.iso', system=b'WIN32'), source)['kind'] == 'unknown'
    cube = bytearray(0x440)
    cube[0:6], cube[0x1C:0x20] = b'GDJE69', b'\xc2\x33\x9f\x3d'
    cube[0x20:0x20 + 21] = b'Def Jam: Fight for NY'
    (tmp_path / 'gc.iso').write_bytes(bytes(cube))
    assert engine.identify_game(tmp_path / 'gc.iso', source)['kind'] == 'gamecube'
    cube[0x20:0x20 + 21] = b'Some other cube game!'
    (tmp_path / 'gc2.iso').write_bytes(bytes(cube))
    assert engine.identify_game(tmp_path / 'gc2.iso', source)['kind'] == 'gamecube-other'
    wii = bytearray(0x440)
    wii[0x18:0x1C] = b'\x5d\x1c\x9e\xa3'
    (tmp_path / 'wii.iso').write_bytes(bytes(wii))
    assert engine.identify_game(tmp_path / 'wii.iso', source)['kind'] == 'wii'
    (tmp_path / 'game.rvz').write_bytes(b'RVZ\x01' + bytes(100))
    assert engine.identify_game(tmp_path / 'game.rvz', source)['kind'] == 'dolphin'
    with zipfile.ZipFile(tmp_path / 'packed.iso', 'w') as packed:
        packed.writestr('game.iso', b'x')
    verdict = engine.identify_game(tmp_path / 'packed.iso', source)
    assert verdict['kind'] == 'archive' and 'ZIP' in verdict['message']
    (tmp_path / 'random.iso').write_bytes(os.urandom(70000))
    assert engine.identify_game(tmp_path / 'random.iso', source)['kind'] == 'unknown'
    assert engine.identify_game(tmp_path / 'gone.iso', source)['kind'] == 'missing'


def test_identify_folders_like_the_install_does(tmp_path):
    xbe = fake_xbe()
    source = wizard_source(tmp_path, xbe)
    game = tmp_path / 'dumps/extracted'
    (game / 'audio').mkdir(parents=True)
    (game / 'default.xbe').write_bytes(xbe)
    (game / 'audio/a.bin').write_bytes(b'1')
    assert engine.identify_game(game, source)['kind'] == 'incomplete'      # 2 of 3 files
    (game / 'b.bin').write_bytes(b'2')
    assert engine.identify_game(game, source)['ok']
    # The folder around the dump, as players choose it: the one game inside is used.
    verdict = engine.identify_game(tmp_path / 'dumps', source)
    assert verdict['ok'] and Path(verdict['path']) == game.resolve()
    xdvdfs_image(tmp_path / 'dumps/copy.iso', {'default.xbe': xbe, 'audio/a': b'1', 'b': b'2'})
    assert engine.identify_game(tmp_path / 'dumps', source)['kind'] == 'many-games'
    (tmp_path / 'empty').mkdir()
    assert engine.identify_game(tmp_path / 'empty', source)['kind'] == 'no-game-folder'


def test_find_games_lists_this_game_best_first_and_skips_unrelated_images(tmp_path, monkeypatch):
    monkeypatch.setattr(engine, 'MIN_IMAGE_BYTES', 0)
    xbe = fake_xbe()
    source = wizard_source(tmp_path, xbe)
    games = tmp_path / 'Games/deep/er'
    games.mkdir(parents=True)
    xdvdfs_image(games / 'Def Jam (Europe).iso', {'default.xbe': fake_xbe(region=4, pad=b'p'), 'audio/a': b'1'})
    xdvdfs_image(games / 'mine.iso', {'default.xbe': xbe, 'audio/a': b'1', 'b': b'2'})
    xdvdfs_image(games / 'halo.iso', {'default.xbe': fake_xbe(title_id=1, name='Halo', region=7), 'audio/a': b'1'})
    (games / 'notes.txt').write_text('x')
    # An earlier installation's own copy (<data>\extracted) is not offered as a dump.
    copy = tmp_path / 'Games/FFNY Online/data/extracted'
    (copy / 'audio').mkdir(parents=True)
    for name, blob in (('default.xbe', xbe), ('audio/a', b'1'), ('b', b'2')):
        (copy / name).write_bytes(blob)
    (copy.parent / 'extraction-state.json').write_text('{}')
    found = engine.find_games(source, [(tmp_path / 'Games', 3)])
    assert [g['name'] for g in found] == ['mine.iso', 'Def Jam (Europe).iso']
    assert found[0]['ok'] and found[1]['kind'] == 'pal' and found[0]['folder'] == str(games)
    assert engine.find_games(source, [(tmp_path / 'Games', 1)]) == []      # deeper than asked


def test_plan_folder_checks_and_fixes_in_plain_words(tmp_path, monkeypatch):
    monkeypatch.setattr(engine, 'drive_free', lambda path: 500 * 1024**3)
    monkeypatch.setattr(engine, 'suggest_folder', lambda needed: tmp_path / 'Suggested/FFNY Online')
    for key in ('OneDrive', 'OneDriveConsumer', 'OneDriveCommercial'):
        monkeypatch.delenv(key, raising=False)
    for key in ('TEMP', 'TMP'):                         # pytest's folders live there
        monkeypatch.setenv(key, str(tmp_path / 'temp'))
    dump = tmp_path / 'dump.iso'
    dump.write_bytes(b'x' * 1000)
    target = tmp_path / 'Games/FFNY Online'
    plan = engine.plan_folder(target, dump, hd=True)
    assert plan['ok'] and plan['install_dir'] == str(target / 'app') and plan['data_dir'] == str(target / 'data')
    assert [c['id'] for c in plan['checks']] == ['space', 'write', 'place', 'dump']
    assert not target.exists()                                         # nothing is created
    # The game file inside the folder.
    inside = engine.plan_folder(tmp_path, dump)
    assert inside['folder'] == str(tmp_path / 'FFNY Online') and inside['adjusted']   # a busy folder gets its own
    nested = engine.plan_folder(tmp_path / 'Games', tmp_path / 'Games/FFNY Online/x.iso')
    assert nested['ok'] is False and any(c['id'] == 'dump' and not c['ok'] and c['fix'] for c in nested['checks'])
    # Too little space: HD textures are named as the thing to turn off when that is enough.
    monkeypatch.setattr(engine, 'drive_free', lambda path: 20 * 1024**3)
    short = engine.plan_folder(target, dump, hd=True)
    space = next(c for c in short['checks'] if c['id'] == 'space')
    assert not short['ok'] and not space['ok'] and 'HD textures' in space['fix']
    # OneDrive.
    monkeypatch.setattr(engine, 'drive_free', lambda path: 500 * 1024**3)
    monkeypatch.setenv('OneDrive', str(tmp_path / 'OneDrive'))
    cloud = engine.plan_folder(tmp_path / 'OneDrive/Games', dump)
    assert not cloud['ok'] and 'OneDrive' in next(c for c in cloud['checks'] if c['id'] == 'place')['text']


def test_plan_folder_keeps_an_earlier_installation_and_its_data(tmp_path, monkeypatch):
    monkeypatch.setattr(engine, 'drive_free', lambda path: 500 * 1024**3)
    monkeypatch.setattr(engine, 'suggest_folder', lambda needed: tmp_path / 'other')
    folder = tmp_path / 'FFNY Online'
    (folder / 'app').mkdir(parents=True)
    engine.write_json(folder / 'app/installed.json', {'schema': 1, 'data': str(tmp_path / 'saves')})
    for chosen in (folder, folder / 'app'):
        plan = engine.plan_folder(chosen)
        assert plan['existing'] and plan['folder'] == str(folder) and plan['data_dir'] == str(tmp_path / 'saves')
        assert plan['checks'][-1]['id'] == 'existing'


def test_wizard_questions_answer_into_a_file_and_installs_still_need_their_folders(tmp_path):
    xbe = fake_xbe()
    source = wizard_source(tmp_path, xbe)
    image = xdvdfs_image(tmp_path / 'good.iso', {'default.xbe': xbe, 'audio/a': b'1', 'b': b'2'})
    out = tmp_path / 'answer.json'
    assert engine.main(['--payload', str(tmp_path), '--source-root', str(source), '--identify', str(image), '--out', str(out)]) == 0
    assert json.loads(out.read_text(encoding='utf-8'))['ok'] is True
    assert engine.main(['--payload', str(tmp_path), '--dump', str(image), '--data-dir', str(tmp_path / 'd')]) == 2


def test_cmake_version_reads_visual_studio_cmake_and_flags_old_ones():
    # A player's Visual Studio 2019 brought CMake 3.18, which takes "--preset NAME" for a source folder.
    old = engine.cmake_version('cmake version 3.18.20081302-MSVC_2\n\nCMake suite maintained and supported by Kitware')
    assert old == (3, 18) and old < engine.CMAKE_MINIMUM
    assert engine.cmake_version('cmake version 4.3.1-msvc1') >= engine.CMAKE_MINIMUM
    assert engine.cmake_version('cmake version 3.20.21032501-MSVC_2') == engine.CMAKE_MINIMUM
    assert engine.cmake_version('') is None and engine.cmake_version(None) is None


def test_cmake_minimum_matches_the_project():
    root = Path(__file__).resolve().parents[2]
    lists = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
    match = re.search(r'cmake_minimum_required\(VERSION (\d+)\.(\d+)', lists)
    assert (int(match.group(1)), int(match.group(2))) == engine.CMAKE_MINIMUM

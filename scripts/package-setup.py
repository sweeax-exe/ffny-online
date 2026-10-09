#!/usr/bin/env python3
"""Assemble a game-free Windows setup payload from reviewed source and pinned tools.

Requires a recursive checkout, an already built DefJamLauncher, and Python/pip
on the packaging machine only. --development permits a dirty feature checkout;
release workflows must omit it. ZIP/binaries remain ignored local build output.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PYTHON_VERSION = '3.13.5'
PACKAGES = ['pyxbe==1.0.4', 'capstone==5.0.9', 'Pillow==12.3.0', 'numpy==2.5.2']
HD_SOURCES = ('scripts/build-hd-pack.py', 'scripts/hd_assets.py',
              'scripts/hd_corpus.py', 'scripts/hd_corpus.html',
              'config/hd-runtime-aliases.json')


def git(root, *args):
    return subprocess.check_output(['git', '-c', 'safe.directory=' + str(root), *args], cwd=root, text=True).strip()


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def source_allowed(name, toolkit=False):
    """Conservative source inventory; binaries and generated data cannot sneak in."""
    path = PurePosixPath(name)
    if path.suffix.lower() in {'.xbe', '.iso', '.viv', '.mad', '.xsh', '.exe', '.dll', '.obj', '.pdb',
                              '.lib', '.a', '.so', '.pcm', '.wav', '.wma', '.png', '.gif', '.zip', '.7z', '.bmp', '.log'}:
        return False
    if any(p in {'game_files', 'gen', 'output', 'logs', 'build', 'game', 'assets', 'save', 'third_party'} for p in path.parts):
        return False
    if not toolkit and path.parts[0] in {'docs', 'tests', 'patches', '.github', '.agents', '.codex'}:
        return False
    if toolkit and path.parts[0] in {'tests', 'docs', 'examples', 'templates'}:
        # Runtime templates are required by the lifter.
        # Current toolkit includes its format smoke project unconditionally.
        return name.startswith(('templates/runtime/', 'tests/d3d8_smoke/'))
    return True


def copy_sources(root, destination, toolkit=False):
    for name in git(root, 'ls-files').splitlines():
        path = root / name
        if source_allowed(name, toolkit) and path.is_file() and not path.is_symlink():
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)


def download(url, destination, expected=None):
    destination = Path(destination)
    if not destination.is_file() or (expected and digest(destination) != expected):
        pending = destination.with_suffix('.download')
        urllib.request.urlretrieve(url, pending)
        if expected and digest(pending) != expected:
            raise ValueError('Download SHA256 mismatch: ' + url)
        pending.replace(destination)
    return digest(destination)


def safe_unpack(archive, destination):
    """Reject path traversal and links in build dependency archives."""
    destination = Path(destination)
    if str(archive).endswith(('.zip', '.whl')):
        with zipfile.ZipFile(archive) as package:
            for item in package.infolist():
                path = PurePosixPath(item.filename)
                if path.is_absolute() or '..' in path.parts or '\\' in item.filename or ':' in item.filename:
                    raise ValueError('Unsafe ZIP entry')
                if (item.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError('ZIP symlink is unsupported')
            package.extractall(destination)
            for item in package.infolist():
                mode = (item.external_attr >> 16) & 0o777
                if mode and not item.is_dir():
                    (destination / item.filename).chmod(mode)
    else:
        with tarfile.open(archive) as package:
            package.extractall(destination, filter='data')


def stage_macos(args, staging, source, toolkit):
    """Python, CMake, Ninja, the runtime libraries and the locally-made launcher app."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('macos_payload', ROOT / 'scripts/macos_payload.py')
    macos = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(macos)
    cache = args.cache / 'macos'
    cache.mkdir(parents=True, exist_ok=True)
    (staging / 'engine').mkdir()
    shutil.copyfile(ROOT / 'setup/engine.py', staging / 'engine/engine.py')
    if not (args.launcher / 'Contents/MacOS/DefJamLauncher').is_file():
        raise ValueError('--launcher must be the built Def Jam Recompiled.app')
    shutil.copytree(args.launcher, staging / 'launcher' / args.launcher.name, symlinks=True)
    provenance = macos.stage_python(staging, cache)
    tool_provenance, tools = macos.stage_tools(staging, cache)
    provenance += tool_provenance
    prefix = args.runtime or (cache / 'runtime')
    if not (prefix / 'lib/libMoltenVK.dylib').is_file():
        provenance_runtime = macos.build_runtime_prefix(cache, prefix, tools)
        (prefix / 'provenance.json').write_text(json.dumps(provenance_runtime, indent=2), encoding='utf-8')
    provenance += json.loads((prefix / 'provenance.json').read_text(encoding='utf-8')) if (prefix / 'provenance.json').is_file() else []
    shutil.copytree(prefix, staging / 'deps', ignore=shutil.ignore_patterns('provenance.json', 'licenses'))
    shutil.copytree(prefix / 'licenses', staging / 'licenses')
    for old in staging.rglob('.DS_Store'):
        old.unlink()
    return provenance


def stage_windows(args, staging, source, toolkit):
    """Embedded Python, wheels, offline SDL3/ImGui source and the native launcher."""
    (staging / 'engine').mkdir()
    for name in ('engine.py', 'install-prerequisites.ps1', 'shortcuts.ps1'):
        shutil.copyfile(ROOT / 'setup' / name, staging / 'engine' / name)
    shutil.copyfile(args.launcher, staging / 'DefJamLauncher.exe')
    provenance = []
    python_url = f'https://www.python.org/ftp/python/{PYTHON_VERSION}/python-{PYTHON_VERSION}-embed-amd64.zip'
    python_zip = args.cache / ('python-' + PYTHON_VERSION + '.zip')
    # Official runtime download has a checked-in digest in setup/dependencies.json.
    dependencies = json.loads((ROOT / 'setup/dependencies.json').read_text())
    sha = download(python_url, python_zip, dependencies['python_sha256'])
    safe_unpack(python_zip, staging / 'python')
    provenance.append({'url': python_url, 'sha256': sha})
    wheels = args.cache / 'wheels'
    wheels.mkdir(exist_ok=True)
    subprocess.run([sys.executable, '-m', 'pip', 'download', '--only-binary=:all:', '--no-deps',
                    '--platform', 'win_amd64', '--python-version', '313', '--implementation', 'cp', '--abi', 'cp313',
                    '--dest', str(wheels), *PACKAGES], check=True)
    for wheel_name, wheel_sha in dependencies['wheels'].items():
        wheel = wheels / wheel_name
        if not wheel.is_file() or digest(wheel) != wheel_sha:
            raise ValueError('Python dependency checksum mismatch: ' + wheel_name)
        safe_unpack(wheel, staging / 'python/Lib/site-packages')
        provenance.append({'package': wheel_name, 'sha256': wheel_sha})
    # ._pth isolation requires explicitly adding source roots used by -m tools.
    (staging / 'python/python313._pth').write_text(
        'python313.zip\n.\nLib/site-packages\n../source\n../source/scripts\n../source/tools/xboxrecomp\nimport site\n', encoding='utf-8')
    # Users never download SDL/ImGui at configure time. Keep source archive checksums.
    for name, cmake, version_key, sha_key, url_pattern in (
        ('sdl3', toolkit / 'cmake/xbox_sdl3.cmake', 'XBOX_SDL3_VERSION', 'XBOX_SDL3_SHA256',
         'https://github.com/libsdl-org/SDL/releases/download/release-{version}/SDL3-{version}.tar.gz'),
        ('imgui', ROOT / 'cmake/imgui.cmake', 'DEFJAM_IMGUI_VERSION', 'DEFJAM_IMGUI_SHA256',
         'https://github.com/ocornut/imgui/archive/refs/tags/v{version}.tar.gz')):
        text = cmake.read_text()
        dependency_version = re.search(r'set\(' + version_key + r' "([^"]+)"', text).group(1)
        dependency_sha = re.search(r'set\(' + sha_key + r' "([^"]+)"', text).group(1)
        archive = args.cache / (name + '-' + dependency_version + '.tar.gz')
        url = url_pattern.format(version=dependency_version)
        download(url, archive, dependency_sha)
        folder = source / 'setup-deps' / name
        safe_unpack(archive, folder)
        children = list(folder.iterdir())
        if len(children) != 1 or not children[0].is_dir():
            raise ValueError('Unexpected dependency archive layout')
        provenance.append({'url': url, 'sha256': dependency_sha,
                           'source_dir': str(children[0].relative_to(source))})
    return provenance


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['windows-x64', 'macos-arm64'],
                        default='windows-x64' if sys.platform == 'win32' else 'macos-arm64')
    parser.add_argument('--launcher', type=Path, required=True,
                        help='built DefJamLauncher.exe (Windows) or Def Jam Recompiled.app (macOS)')
    parser.add_argument('--runtime', type=Path, help='macOS: prebuilt runtime library prefix (see macos_payload.py)')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/setup-payload.zip')
    parser.add_argument('--cache', type=Path, default=ROOT / 'build/setup-downloads')
    parser.add_argument('--development', action='store_true')
    args = parser.parse_args(argv)
    toolkit = ROOT / 'tools/xboxrecomp'
    commit, pin = git(ROOT, 'rev-parse', 'HEAD'), git(toolkit, 'rev-parse', 'HEAD')
    expected_pin = git(ROOT, 'ls-tree', 'HEAD', 'tools/xboxrecomp').split()[2]
    if (pin != expected_pin and not args.development) or git(toolkit, 'status', '--porcelain', '--untracked-files=no'):
        raise ValueError('Toolkit must match the clean release gitlink')
    if not args.development and git(ROOT, 'status', '--porcelain', '--untracked-files=no'):
        raise ValueError('Production setup must be built from a clean reviewed release checkout')
    version = re.search(r'project\(defjam_recomp VERSION ([\d.]+)', (ROOT / 'CMakeLists.txt').read_text()).group(1)
    args.cache.mkdir(parents=True, exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='setup-payload-', dir=args.output.parent) as temporary:
        staging = Path(temporary)
        source = staging / 'source'
        source.mkdir()
        copy_sources(ROOT, source)
        copy_sources(toolkit, source / 'tools/xboxrecomp', True)
        for name in HD_SOURCES:
            if not (source / name).is_file():
                raise ValueError('Required HD setup source is not tracked: ' + name)
        # Source archives contain no Git metadata and never receive the user's bytes.
        (source / 'src/recomp/gen').mkdir(parents=True, exist_ok=True)
        (source / 'src/recomp/gen/.gitkeep').touch()
        provenance = (stage_macos if args.platform == 'macos-arm64' else stage_windows)(args, staging, source, toolkit)
        (staging / 'dependencies.json').write_text(json.dumps(provenance, indent=2), encoding='utf-8')
        inventory = {p.relative_to(staging).as_posix(): digest(p) for p in sorted(staging.rglob('*')) if p.is_file()}
        manifest = {'schema': 1, 'platform': args.platform, 'version': version,
                    'source_commit': commit, 'toolkit_commit': pin,
                    'development': args.development, 'files': inventory}
        (staging / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED) as package:
            for p in sorted(staging.rglob('*')):
                if p.is_file():
                    package.write(p, p.relative_to(staging).as_posix())
        print(f'{args.output}: {len(inventory)} inventoried files, {args.output.stat().st_size} bytes')
    return 0


if __name__ == '__main__':
    sys.exit(main())

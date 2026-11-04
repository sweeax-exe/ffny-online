#!/usr/bin/env python3
"""Everything the Linux setup payload carries besides the project's own source.

The setup compiles the player's dump on their computer, so the payload brings
the Python that runs the wizard, the engine and the lifter (with Tk for the
wizard, the two wheels the pipeline needs and the two the optional HD texture
upscale needs), the wizard itself, the launcher template and the game's build toolchain
(stage_toolchain): Zig's clang for x86_64 Linux with glibc 2.31 symbols, CMake, Ninja, and SDL3,
shaderc and the Vulkan headers built with that compiler. The game it builds needs only glibc and
the computer's own Vulkan loader. The computer's own compiler and the pinned build container
remain as developer options (engine.py --toolchain). Every download is pinned by URL and SHA-256
or by git commit. Nothing here touches a dump or game data.

Used by package-setup.py (--platform linux-x64).
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = ['pyxbe==1.0.4', 'capstone==5.0.9', 'Pillow==12.3.0', 'numpy==2.5.2', 'cryptography==50.0.2', 'cffi==2.0.0', 'pycparser==2.23']
PYTHON_PRUNE = ('bin/idle3', 'bin/idle3.13', 'bin/pip', 'bin/pip3', 'bin/pip3.13', 'bin/pydoc3', 'bin/pydoc3.13',
                'bin/python3-config', 'bin/python3.13-config', 'bin/python', 'share', 'include', 'lib/pkgconfig')
PYTHON_PRUNE_LIB = ('test', 'idlelib', 'turtledemo', 'ensurepip', 'lib2to3')

_spec = importlib.util.spec_from_file_location('macos_payload', ROOT / 'scripts/macos_payload.py')
_shared = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_shared)
digest, download, unpack, flatten_links = _shared.digest, _shared.download, _shared.unpack, _shared.flatten_links
toolkit_pin = _shared.toolkit_pin


def pins():
    return json.loads((ROOT / 'setup/dependencies-linux.json').read_text(encoding='utf-8'))


def stage_python(staging, cache):
    """python-build-standalone, pruned, with Tk kept for the wizard and the pipeline's wheels added."""
    pin = pins()['python']
    archive = download(pin['url'], cache / 'python.tar.gz', pin['sha256'])
    work = cache / 'unpacked-python'
    unpack(archive, work)
    python = staging / 'python'
    shutil.copytree(work / 'python', python, symlinks=True)
    for name in PYTHON_PRUNE:
        target = python / name
        if target.is_symlink() or target.is_file():
            target.unlink()
        elif target.is_dir():
            shutil.rmtree(target)
    library = next((python / 'lib').glob('python3.*'))
    for name in PYTHON_PRUNE_LIB:
        shutil.rmtree(library / name, ignore_errors=True)
    for folder in list(python.rglob('__pycache__')):
        shutil.rmtree(folder, ignore_errors=True)
    # python3 -> python3.13: keep one real executable.
    real = python / 'bin' / next(p.name for p in (python / 'bin').iterdir() if re.fullmatch(r'python3\.\d+', p.name))
    link = python / 'bin/python3'
    if link.is_symlink():
        link.unlink()
    real.rename(link)
    flatten_links(python)
    site = library / 'site-packages'
    site.mkdir(exist_ok=True)
    (site / 'defjam-setup.pth').write_text('../../../../source\n../../../../source/scripts\n'
                                           '../../../../source/tools/xboxrecomp\n', encoding='utf-8')
    wheels = cache / 'wheels'
    wheels.mkdir(exist_ok=True)
    expected = pins()['wheels']
    if not all((wheels / name).is_file() and digest(wheels / name) == sha for name, sha in expected.items()):
        subprocess.run([sys.executable, '-m', 'pip', 'download', '--only-binary=:all:', '--no-deps', '--dest',
                        str(wheels), '--platform', 'manylinux2014_x86_64', '--platform', 'manylinux_2_17_x86_64',
                        '--platform', 'manylinux_2_28_x86_64',
                        '--python-version', '313', '--implementation', 'cp', '--abi', 'cp313', *PACKAGES], check=True)
    provenance = []
    for name, sha in expected.items():
        wheel = wheels / name
        if not wheel.is_file() or digest(wheel) != sha:
            raise ValueError('Python dependency checksum mismatch: ' + name)
        unpack(wheel, cache / 'unpacked-wheel')
        shutil.copytree(cache / 'unpacked-wheel', site, dirs_exist_ok=True)
        provenance.append({'package': name, 'sha256': sha})
    provenance.append({'url': pin['url'], 'sha256': pin['sha256']})
    return provenance


def stage(staging, cache):
    """The engine, the wizard, the launcher template and Python."""
    (staging / 'engine').mkdir()
    # steam_shortcuts.py: the app's Add to Steam (engine.py --steam), as on Windows.
    for name in ('engine.py', 'releases.py', 'transaction.py', 'steam_shortcuts.py'):
        shutil.copyfile(ROOT / 'setup' / name, staging / 'engine' / name)
    shutil.copyfile(ROOT / 'setup/linux/wizard.py', staging / 'engine/wizard.py')
    (staging / 'launcher').mkdir()
    shutil.copyfile(ROOT / 'setup/linux/launcher.sh', staging / 'launcher/launcher.sh')
    provenance = stage_python(staging, cache)
    provenance += stage_toolchain(staging, cache)
    container = pins()['container']
    provenance.append({'container_image': container['image'], 'archive_date': container['archive_date'],
                       'packages': container['packages'], 'note': 'fetched on the player\'s computer only '
                       'when setup is asked to build in it (--toolchain container)'})
    return provenance


# ---- the game's build toolchain (design §6, D215) ------------------------------------------------

# What of Zig a build for x86_64-linux-gnu reads: its clang and lld (the zig executable), the C
# and C++ runtimes it compiles on first use (compiler_rt, libc++, libc++abi, libunwind, glibc's
# start files and symbol lists) and the Linux headers. The other C libraries, the other targets'
# headers, the documentation and the thread sanitizer are left out (about 160 MB of 411).
ZIG_PRUNE = ('doc', 'lib/docs', 'lib/build-web', 'lib/lldb', 'lib/libtsan', 'lib/init', 'lib/fuzzer.zig',
             'lib/libc/musl', 'lib/libc/mingw', 'lib/libc/wasi', 'lib/libc/darwin', 'lib/libc/freebsd',
             'lib/libc/netbsd')
ZIG_HEADERS = ('generic-glibc', 'x86-linux-gnu', 'x86-linux-any', 'any-linux-any')
# What CMake is given as the compiler, archiver and indexer. -fno-sanitize=all: zig cc turns the
# undefined-behaviour sanitizer on in unoptimized builds, and the lifted code relies on behaviour it
# would trap (the game is compiled with -fno-strict-aliasing -fwrapv -ffp-contract=off).
# The compilers include setup/linux/glibc-symbols.h in everything they compile: the functions Zig's
# own runtime also defines (memcpy, floor, ...) then come from glibc, not from Zig's generic copies.
GLIBC_SYMBOLS = ROOT / 'setup/linux/glibc-symbols.h'
WRAPPERS = {'zig-cc': 'cc {target} -fno-sanitize=all -include "$here/../include/glibc-symbols.h"',
            'zig-c++': 'c++ {target} -fno-sanitize=all -include "$here/../include/glibc-symbols.h"',
            'zig-ar': 'ar', 'zig-ranlib': 'ranlib'}


def git_checkout(pin, folder, sync=False):
    """A shallow clone of one tag, refused unless it is the pinned commit."""
    if not (folder / '.git').is_dir():
        shutil.rmtree(folder, ignore_errors=True)
        subprocess.run(['git', 'clone', '--quiet', '--branch', pin['tag'], '--depth', '1', pin['url'], str(folder)],
                       check=True)
    head = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=folder, capture_output=True, text=True,
                          check=True).stdout.strip()
    if head != pin['commit']:
        raise ValueError(f'{pin["url"]} {pin["tag"]} is {head}, expected {pin["commit"]}')
    if sync:
        # shaderc's DEPS pins glslang, SPIRV-Tools and SPIRV-Headers by commit; this fetches exactly those.
        subprocess.run([sys.executable, 'utils/git-sync-deps'], cwd=folder, check=True)
    return {'url': pin['url'], 'tag': pin['tag'], 'commit': pin['commit']}


def stage_zig(tools, cache, pin, glibc):
    """tools/zig, pruned to x86_64-linux-gnu, and the four wrappers in tools/bin."""
    archive = download(pin['url'], cache / 'zig.tar.xz', pin['sha256'])
    work = cache / 'unpacked-zig'
    unpack(archive, work)
    zig = tools / 'zig'
    shutil.copytree(next(work.iterdir()), zig, symlinks=True)
    for name in ZIG_PRUNE:
        target = zig / name
        if target.is_dir():
            shutil.rmtree(target)
        elif target.exists():
            target.unlink()
    for folder in (zig / 'lib/libc/include').iterdir():
        if folder.name not in ZIG_HEADERS:
            shutil.rmtree(folder)
    flatten_links(zig)
    (tools / 'bin').mkdir(parents=True, exist_ok=True)
    (tools / 'include').mkdir(exist_ok=True)
    shutil.copyfile(GLIBC_SYMBOLS, tools / 'include/glibc-symbols.h')
    target = f'-target x86_64-linux-gnu.{glibc}'
    for name, arguments in WRAPPERS.items():
        wrapper = tools / 'bin' / name
        wrapper.write_text('#!/bin/sh\n# The game\'s build toolchain (setup/dependencies-linux.json, toolchain).\n'
                           'here=$(dirname "$(readlink -f "$0")")\n'
                           'exec "$here/../zig/zig" ' + arguments.format(target=target) + ' "$@"\n', encoding='utf-8')
        wrapper.chmod(0o755)
    return {'url': pin['url'], 'sha256': pin['sha256'], 'pruned': list(ZIG_PRUNE), 'glibc': glibc}


def stage_build_tools(tools, cache, toolchain):
    """CMake (the executable and its modules only) and Ninja, in tools/bin and tools/share."""
    pin = toolchain['cmake']
    archive = download(pin['url'], cache / 'cmake-linux.tar.gz', pin['sha256'])
    work = cache / 'unpacked-cmake'
    unpack(archive, work)
    contents = next(work.iterdir())
    shutil.copy2(contents / 'bin/cmake', tools / 'bin/cmake')
    share = next((contents / 'share').glob('cmake-*'))
    shutil.copytree(share, tools / 'share' / share.name, ignore=shutil.ignore_patterns('Help', '__pycache__'))
    provenance = [{'url': pin['url'], 'sha256': pin['sha256']}]
    pin = toolchain['ninja']
    archive = download(pin['url'], cache / 'ninja-linux.zip', pin['sha256'])
    unpack(archive, cache / 'unpacked-ninja')
    shutil.copy2(cache / 'unpacked-ninja/ninja', tools / 'bin/ninja')
    (tools / 'bin/ninja').chmod(0o755)
    provenance.append({'url': pin['url'], 'sha256': pin['sha256']})
    return provenance, contents / 'doc/cmake/LICENSE.rst'


def stage_toolchain(staging, cache):
    """tools/ (Zig, its wrappers, CMake, Ninja), deps/ (what the game links) and their licences."""
    toolchain = pins()['toolchain']
    tools = staging / 'tools'
    provenance = [stage_zig(tools, cache, toolchain['zig'], toolchain['glibc'])]
    build_tools, cmake_licence = stage_build_tools(tools, cache, toolchain)
    provenance += build_tools
    sources = cache / 'toolchain-sources'
    sources.mkdir(parents=True, exist_ok=True)
    version, sha = toolkit_pin('sdl3')
    url = f'https://github.com/libsdl-org/SDL/releases/download/release-{version}/SDL3-{version}.tar.gz'
    download(url, sources / f'SDL3-{version}.tar.gz', sha)
    provenance.append({'url': url, 'sha256': sha})
    provenance.append(git_checkout(toolchain['shaderc'], sources / 'shaderc', sync=True))
    provenance.append(git_checkout(toolchain['vulkan_headers'], sources / 'vulkan-headers'))
    # The libraries follow from these pins, the image and the script alone, so one build serves every payload.
    key = hashlib.sha256(json.dumps([toolchain, version, sha, pins()['app_build'],
                                     (ROOT / 'scripts/build-toolchain-linux.sh').read_text(encoding='utf-8'),
                                     GLIBC_SYMBOLS.read_text(encoding='utf-8')],
                                    sort_keys=True).encode()).hexdigest()[:16]
    built = cache / ('toolchain-deps-' + key)
    if not (built / 'lib/libshaderc_combined.a').is_file():
        shutil.rmtree(built, ignore_errors=True)
        pending = cache / 'toolchain-deps-pending'
        shutil.rmtree(pending, ignore_errors=True)
        subprocess.run(['bash', str(ROOT / 'scripts/build-toolchain-linux.sh'), str(tools), str(sources),
                        str(pending)], check=True)
        pending.rename(built)
    shutil.copytree(built, staging / 'deps', ignore=shutil.ignore_patterns('licenses'))
    licences = staging / 'licenses'
    licences.mkdir(exist_ok=True)
    for path in (built / 'licenses').iterdir():
        shutil.copyfile(path, licences / path.name)
    zig = tools / 'zig'
    for name, path in (('Zig-MIT.txt', zig / 'LICENSE'), ('libcxx-Apache-2.0-LLVM.txt', zig / 'lib/libcxx/LICENSE.TXT'),
                       ('libcxxabi-Apache-2.0-LLVM.txt', zig / 'lib/libcxxabi/LICENSE.TXT'),
                       ('libunwind-Apache-2.0-LLVM.txt', zig / 'lib/libunwind/LICENSE.TXT'),
                       ('glibc-LICENSES.txt', zig / 'lib/libc/glibc/LICENSES'),
                       ('CMake-BSD-3-Clause.rst', cmake_licence)):
        shutil.copyfile(path, licences / name)
    # Ninja's release archive holds the executable alone. Its licence is Apache-2.0, the same text as shaderc's.
    (licences / 'Ninja-Apache-2.0.txt').write_text(
        'Ninja (https://github.com/ninja-build/ninja) is licensed under the Apache License, Version 2.0:\n\n'
        + (built / 'licenses/shaderc-Apache-2.0.txt').read_text(encoding='utf-8'), encoding='utf-8')
    return provenance

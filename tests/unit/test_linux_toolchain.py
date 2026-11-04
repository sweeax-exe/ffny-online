"""The Linux setup's own build toolchain (design §6, D215): its pins, how the payload stages it,
when setup picks it and what the game's build is told. Synthetic files only: no compiler runs."""
import importlib.util
import io
import os
import json
from pathlib import Path
import re
import tarfile

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


linux_payload = load('toolchain_linux_payload', ROOT / 'scripts/linux_payload.py')
setup_tests = load('toolchain_linux_setup_tests', ROOT / 'tests/unit/test_linux_setup.py')
engine = setup_tests.engine     # the same module the installers below are made from
linux_only = pytest.mark.skipif(not engine.IS_LINUX, reason='Linux setup paths')


def test_the_toolchain_is_pinned_by_hash_and_commit():
    toolchain = json.loads((ROOT / 'setup/dependencies-linux.json').read_text())['toolchain']
    assert toolchain['glibc'] == '2.31'
    assert re.fullmatch(r'https://ziglang\.org/download/[0-9.]+/zig-x86_64-linux-[0-9.]+\.tar\.xz', toolchain['zig']['url'])
    assert toolchain['cmake']['url'].startswith('https://github.com/Kitware/CMake/releases/download/')
    assert toolchain['ninja']['url'].startswith('https://github.com/ninja-build/ninja/releases/download/')
    for name in ('zig', 'cmake', 'ninja'):
        assert re.fullmatch('[0-9a-f]{64}', toolchain[name]['sha256'])
    for name in ('shaderc', 'vulkan_headers'):
        assert re.fullmatch('[0-9a-f]{40}', toolchain[name]['commit']) and toolchain[name]['tag']
    # The same shaderc as the macOS payload's.
    assert toolchain['shaderc'] == json.loads((ROOT / 'setup/dependencies-macos.json').read_text())['shaderc']


def zig_archive(path):
    """A zig release's shape: the executable, the runtimes' sources and other targets' C libraries."""
    names = ['zig', 'LICENSE', 'doc/langref.html', 'lib/std/std.zig', 'lib/compiler_rt.zig', 'lib/libcxx/LICENSE.TXT',
             'lib/libc/glibc/abilists', 'lib/libc/musl/x.c', 'lib/libc/mingw/x.c', 'lib/libc/darwin/x.h',
             'lib/libc/include/generic-glibc/stdio.h', 'lib/libc/include/any-linux-any/linux/types.h',
             'lib/libc/include/x86-linux-any/asm/x.h', 'lib/libc/include/x86-linux-gnu/bits/wordsize.h',
             'lib/libc/include/any-windows-any/windows.h',
             'lib/libc/include/aarch64-macos-none/x.h', 'lib/libtsan/x.cpp', 'lib/fuzzer.zig']
    with tarfile.open(path, 'w:xz') as archive:
        for name in names:
            data = b'#!/bin/sh\n' if name == 'zig' else name.encode()
            info = tarfile.TarInfo('zig-x86_64-linux-0.17.0/' + name)
            info.size, info.mode = len(data), 0o755 if name == 'zig' else 0o644
            archive.addfile(info, io.BytesIO(data))


def test_zig_is_pruned_to_linux_glibc_and_wrapped_for_glibc_2_31(tmp_path, monkeypatch):
    archive = tmp_path / 'zig.tar.xz'
    zig_archive(archive)
    monkeypatch.setattr(linux_payload, 'download', lambda url, destination, sha: archive)
    tools = tmp_path / 'tools'
    pin = {'url': 'https://ziglang.org/download/0.17.0/zig-x86_64-linux-0.17.0.tar.xz', 'sha256': '0' * 64}
    provenance = linux_payload.stage_zig(tools, tmp_path / 'cache', pin, '2.31')
    kept = {p.relative_to(tools / 'zig').as_posix() for p in (tools / 'zig').rglob('*') if p.is_file()}
    assert kept == {'zig', 'LICENSE', 'lib/std/std.zig', 'lib/compiler_rt.zig', 'lib/libcxx/LICENSE.TXT',
                    'lib/libc/glibc/abilists', 'lib/libc/include/generic-glibc/stdio.h',
                    'lib/libc/include/any-linux-any/linux/types.h', 'lib/libc/include/x86-linux-any/asm/x.h',
                    'lib/libc/include/x86-linux-gnu/bits/wordsize.h'}
    # Windows files have no execute bit; the payload is built on Linux.
    assert os.name == 'nt' or (tools / 'zig/zig').stat().st_mode & 0o111
    assert provenance['glibc'] == '2.31' and provenance['url'] == pin['url']
    for name in ('zig-cc', 'zig-c++', 'zig-ar', 'zig-ranlib'):
        wrapper = tools / 'bin' / name
        assert os.name == 'nt' or wrapper.stat().st_mode & 0o111
        text = wrapper.read_text()
        # Found next to itself wherever setup copied it, never by an absolute path.
        assert 'here=$(dirname "$(readlink -f "$0")")' in text and '"$here/../zig/zig" ' in text
        assert str(tmp_path) not in text
    for name in ('zig-cc', 'zig-c++'):
        assert ('-target x86_64-linux-gnu.2.31 -fno-sanitize=all -include "$here/../include/glibc-symbols.h" "$@"'
                in (tools / 'bin' / name).read_text())
    assert (tools / 'include/glibc-symbols.h').read_bytes() == linux_payload.GLIBC_SYMBOLS.read_bytes()


def test_the_glibc_symbol_header_matches_the_pinned_zig():
    """setup/linux/glibc-symbols.h is made for one Zig: a new pin needs scripts/zig-glibc-symbols.py again."""
    toolchain = json.loads((ROOT / 'setup/dependencies-linux.json').read_text())['toolchain']
    zig_version = re.search(r'/download/([0-9.]+)/', toolchain['zig']['url'])[1]
    header = (ROOT / 'setup/linux/glibc-symbols.h').read_text()
    assert f'for Zig {zig_version} and glibc {toolchain["glibc"]}:' in header
    bound = dict(re.findall(r'\.symver (\w+), \1@(GLIBC_[0-9.]+)', header))
    # The ones the game spends its time in, at the versions glibc 2.31 gives them.
    assert bound['memcpy'] == 'GLIBC_2.14' and bound['exp'] == 'GLIBC_2.29' and bound['floor'] == 'GLIBC_2.2.5'
    assert {'memset', 'memmove', 'memcmp', 'strlen', 'fmod', 'sin', 'cos', 'sqrt', 'log'} <= set(bound)
    assert all(tuple(map(int, v[6:].split('.'))) <= (2, 31) for v in bound.values())
    # Assembly sources are preprocessed with it too: they must see nothing.
    assert header.count('#if !defined(__ASSEMBLER__)') == 1


def bundled_payload(setup):
    for name in ('tools/bin/zig-cc', 'tools/bin/zig-c++', 'tools/bin/zig-ar', 'tools/bin/zig-ranlib',
                 'tools/bin/cmake', 'tools/bin/ninja', 'tools/zig/zig', 'deps/lib/libSDL3.a',
                 'deps/lib/libshaderc_combined.a', 'deps/link/libvulkan.so'):
        path = setup.version_root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'x')


def record_runs(setup, monkeypatch):
    calls = []

    def run(argv, capture=False, **kwargs):
        calls.append([str(a) for a in argv])
        return '0.17.0\n' if capture else ''
    monkeypatch.setattr(setup, 'run', run)
    return calls


@linux_only
def test_setup_builds_with_the_payloads_toolchain_by_default(tmp_path, monkeypatch):
    setup = setup_tests.installer(tmp_path)
    bundled_payload(setup)
    calls = record_runs(setup, monkeypatch)
    monkeypatch.setattr(setup, 'host_toolchain_problem', lambda: pytest.fail('the computer was asked'))
    setup.toolchain_linux()
    assert setup.bundled and setup.container_image is None
    assert (setup.version_root / 'toolchain.txt').read_text() == 'bundled:zig-0.17.0'
    # The C++ runtime is compiled once into the version's own cache, and the result must run.
    assert setup.env['ZIG_GLOBAL_CACHE_DIR'] == setup.env['ZIG_LOCAL_CACHE_DIR'] == str(setup.version_root / 'zig-cache')
    assert calls[0][0] == str(setup.version_root / 'tools/bin/zig-c++')
    assert calls[1] == [str(setup.version_root / 'toolchain-check/check')]


@linux_only
def test_without_bundled_tools_auto_uses_the_computer_and_bundled_refuses(tmp_path, monkeypatch):
    setup = setup_tests.installer(tmp_path)
    record_runs(setup, monkeypatch)
    monkeypatch.setattr(setup, 'host_toolchain_problem', lambda: None)
    setup.toolchain_linux()
    assert not setup.bundled and (setup.version_root / 'toolchain.txt').read_text() == 'host'
    setup.args.toolchain = 'bundled'
    with pytest.raises(engine.SetupError) as error:
        setup.toolchain_linux()
    assert error.value.code == 3


@linux_only
def test_host_stays_available_when_the_payload_has_tools(tmp_path, monkeypatch):
    setup = setup_tests.installer(tmp_path, toolchain='host')
    bundled_payload(setup)
    record_runs(setup, monkeypatch)
    monkeypatch.setattr(setup, 'host_toolchain_problem', lambda: None)
    setup.toolchain_linux()
    assert not setup.bundled and (setup.version_root / 'toolchain.txt').read_text() == 'host'


@linux_only
def test_the_bundled_build_reads_nothing_from_the_computer(tmp_path):
    setup = setup_tests.installer(tmp_path)
    bundled_payload(setup)
    overrides = setup.bundled_overrides()
    values = dict(item[2:].split('=', 1) for item in overrides)
    tools, deps = setup.version_root / 'tools', setup.version_root / 'deps'
    assert values['CMAKE_C_COMPILER'] == f'{tools}/bin/zig-cc' and values['CMAKE_CXX_COMPILER'] == f'{tools}/bin/zig-c++'
    assert values['CMAKE_PREFIX_PATH'] == str(deps)
    assert values['CMAKE_FIND_USE_CMAKE_SYSTEM_PATH'] == values['CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH'] == 'OFF'
    # The stub loader is linked against but never found at run time: no run path into the payload.
    assert values['Vulkan_LIBRARY'] == f'{deps}/link/libvulkan.so' and values['CMAKE_SKIP_RPATH'] == 'ON'
    assert values['SHADERC_LIBRARY'] == f'{deps}/lib/libshaderc_combined.a'
    assert not any(value.startswith('/usr') for value in values.values())


@linux_only
def test_switching_to_the_bundled_toolchain_starts_the_build_tree_afresh(tmp_path, monkeypatch):
    setup = setup_tests.installer(tmp_path)
    tree = setup.source / 'build' / engine.PRESET
    tree.mkdir(parents=True)
    setup.record_toolchain('container:localhost/defjam-recompiled-build:be9dd70c0e79')
    bundled_payload(setup)
    record_runs(setup, monkeypatch)
    setup.toolchain_linux()
    assert not tree.exists()


def test_linux_runtime_folders_include_the_toolchain():
    if engine.IS_WINDOWS:
        pytest.skip('Windows keeps its tools outside the payload')
    assert {'tools/', 'deps/'} <= set(engine.RUNTIME_FOLDERS)


def test_the_libraries_are_built_with_every_backend_and_only_headers_from_the_image():
    script = (ROOT / 'scripts/build-toolchain-linux.sh').read_text()
    for option in ('SDL_X11_SHARED', 'SDL_WAYLAND_SHARED', 'SDL_PIPEWIRE_SHARED', 'SDL_PULSEAUDIO_SHARED',
                   'SDL_ALSA_SHARED', 'SDL_LIBUDEV'):
        assert option in script
    assert '-idirafter /usr/include' in script and '-g0' in script
    assert '-Wl,-soname,libvulkan.so.1' in script

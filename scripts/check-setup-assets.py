#!/usr/bin/env python3
"""Validate the narrow game-free setup release exception, never game binaries."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


engine = load('asset_setup_engine', ROOT / 'setup/engine.py')
package = load('asset_setup_package', ROOT / 'scripts/package-setup.py')


def inspect_payload(archive):
    with tempfile.TemporaryDirectory() as temporary:
        folder = Path(temporary)
        package.safe_unpack(archive, folder)
        manifest = engine.verify_payload(folder)
        for name in manifest['files']:
            if name.startswith('source/setup-deps/'):
                continue  # exact, hashed upstream SDL/ImGui source archives
            if name.startswith('source/'):
                relative = name[len('source/'):]
                toolkit = relative.startswith('tools/xboxrecomp/')
                if toolkit:
                    relative = relative[len('tools/xboxrecomp/'):]
                if relative == 'src/recomp/gen/.gitkeep':
                    continue
                if not package.source_allowed(relative, toolkit):
                    raise ValueError('Disallowed setup source input: ' + name)
            elif not name.startswith(('python/', 'engine/')) and name not in {'DefJamLauncher.exe', 'dependencies.json'}:
                raise ValueError('Unexpected setup component: ' + name)
        # Probe the actual embedded runtime without generating new payload files.
        if sys.platform == 'win32':
            import subprocess
            subprocess.run([folder / 'python/python.exe', '-B', '-c',
                'import capstone, xbe, tools.disasm, tools.recomp, tools.xiso; print("Embedded runtime imports OK")'], check=True)
        return manifest


def validate_asset_names(version, names):
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Invalid setup release version')
    stem = f'DefJamSetup-{version}-windows-x64'
    expected = {stem + '.exe', stem + '.sha256', stem + '.provenance.json'}
    if set(names) != expected:
        raise ValueError('Release assets must be exactly the approved Windows setup, checksum and provenance')


def verify_embedded_payload(installer, archive):
    """Inspect resource bytes without executing the installer or trusting its name."""
    if sys.platform != 'win32':
        raise ValueError('Setup PE resource inspection requires Windows')
    import ctypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
    kernel.LoadLibraryExW.restype = ctypes.c_void_p
    kernel.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    kernel.FindResourceW.restype = ctypes.c_void_p
    kernel.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel.LoadResource.restype = ctypes.c_void_p
    kernel.LockResource.argtypes = [ctypes.c_void_p]
    kernel.LockResource.restype = ctypes.c_void_p
    kernel.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel.SizeofResource.restype = ctypes.c_uint32
    kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
    handle = kernel.LoadLibraryExW(str(installer.resolve()), None, 2)  # LOAD_LIBRARY_AS_DATAFILE
    if not handle:
        raise ValueError('Installer is not a readable Windows executable')
    try:
        resource = kernel.FindResourceW(handle, 101, 10)  # RCDATA
        if not resource:
            raise ValueError('Installer payload resource missing')
        size = kernel.SizeofResource(handle, resource)
        pointer = kernel.LockResource(kernel.LoadResource(handle, resource))
        if not pointer or hashlib.sha256(ctypes.string_at(pointer, size)).hexdigest() != engine.digest(archive):
            raise ValueError('Installer resource does not match the inspected payload')
    finally:
        kernel.FreeLibrary(handle)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--payload', type=Path, required=True)
    parser.add_argument('--installer', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--development', action='store_true')
    args = parser.parse_args(argv)
    manifest = inspect_payload(args.payload)
    verify_embedded_payload(args.installer, args.payload)
    if manifest.get('development') and not args.development:
        raise ValueError('A development payload cannot be published')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stem = f'DefJamSetup-{manifest["version"]}-windows-x64'
    import shutil
    exe = args.output_dir / (stem + '.exe')
    shutil.copyfile(args.installer, exe)
    sha = engine.digest(exe)
    (args.output_dir / (stem + '.sha256')).write_text(sha + '  ' + exe.name + '\n', encoding='utf-8')
    (args.output_dir / (stem + '.provenance.json')).write_text(json.dumps(
        {'installer_sha256': sha, 'payload_sha256': engine.digest(args.payload), **manifest}, indent=2) + '\n', encoding='utf-8')
    validate_asset_names(manifest['version'], [p.name for p in args.output_dir.iterdir()])
    print('Verified game-free setup assets:', stem)
    return 0


if __name__ == '__main__':
    sys.exit(main())

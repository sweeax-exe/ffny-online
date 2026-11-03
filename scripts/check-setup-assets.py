#!/usr/bin/env python3
"""Validate the narrow game-free setup release exception, never game binaries.

With --app, also the app that sets up, launches and updates the game (src/app, D215): on macOS
the archive scripts/package-app.py makes, checked file by file against the inspected payload; on
Windows the program DefJamRecompiled.exe, whose built-in payload (resource 101) must be the
inspected payload byte for byte. Published as DefJamRecompiled-<version>-macos-arm64.zip or
-windows-x64.exe with its checksum and a kind: app provenance that setup/releases.py select_app
verifies before the app replaces itself."""
import argparse
import hashlib
import importlib.util
import json
import os
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
releases = load('asset_setup_releases', ROOT / 'setup/releases.py')


def inspect_payload(archive):
    with tempfile.TemporaryDirectory() as temporary:
        folder = Path(temporary)
        package.safe_unpack(archive, folder)
        manifest = engine.verify_payload(folder, platform_of(archive))
        for name in package.HD_SOURCES + package.MOD_SOURCES:
            if 'source/' + name not in manifest['files']:
                raise ValueError('Required HD setup source missing: ' + name)
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
            elif manifest['platform'] == 'linux-x64':
                if not name.startswith(('python/', 'engine/', 'licenses/')) \
                        and name not in {'launcher/launcher.sh', 'dependencies.json'}:
                    raise ValueError('Unexpected setup component: ' + name)
            elif manifest['platform'] == 'macos-arm64':
                if not name.startswith(('python/', 'engine/', 'tools/', 'deps/', 'licenses/')) \
                        and not name.startswith('launcher/Def Jam Recompiled.app/') and name != 'dependencies.json':
                    raise ValueError('Unexpected setup component: ' + name)
            elif not name.startswith(('python/', 'engine/')) and name not in {'DefJamLauncher.exe', 'dependencies.json'}:
                raise ValueError('Unexpected setup component: ' + name)
        # Probe the actual embedded runtime without generating new payload files.
        if manifest['platform'] == 'linux-x64':
            if sys.platform.startswith('linux'):
                import subprocess
                python = folder / 'python/bin/python3'
                python.chmod(0o755)   # safe_unpack keeps no modes; the shipped folder does
                subprocess.run([python, '-B', '-c',
                    'import capstone, xbe, tkinter, PIL, numpy, tools.disasm, tools.recomp, tools.xiso; from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey; print("Bundled runtime imports OK")'],
                    check=True, env={'PATH': '/usr/bin:/bin'})
                subprocess.run([python, '-B', folder / 'source/scripts/build-hd-pack.py', '--help'], check=True,
                               stdout=subprocess.DEVNULL, env={'PATH': '/usr/bin:/bin'})
        elif manifest['platform'] == 'macos-arm64':
            if sys.platform == 'darwin':
                import subprocess
                python = folder / 'python/bin/python3'
                subprocess.run([python, '-B', '-c',
                    'import capstone, xbe, PIL, numpy, tools.disasm, tools.recomp, tools.xiso; from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey; print("Bundled runtime imports OK")'],
                    check=True, env={'PATH': '/usr/bin:/bin'})
                subprocess.run([python, '-B', folder / 'source/scripts/build-hd-pack.py', '--help'], check=True,
                               stdout=subprocess.DEVNULL, env={'PATH': '/usr/bin:/bin'})
                subprocess.run([folder / 'tools/bin/cmake', '--version'], check=True, stdout=subprocess.DEVNULL)
                subprocess.run([folder / 'tools/bin/ninja', '--version'], check=True, stdout=subprocess.DEVNULL)
        elif sys.platform == 'win32':
            import subprocess
            subprocess.run([folder / 'python/python.exe', '-B', '-c',
                'import capstone, xbe, PIL, numpy, tools.disasm, tools.recomp, tools.xiso; from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey; print("Embedded runtime imports OK")'], check=True)
            subprocess.run([folder / 'python/python.exe', '-B',
                            folder / 'source/scripts/build-hd-pack.py', '--help'], check=True,
                           stdout=subprocess.DEVNULL)
            subprocess.run([folder / 'python/python.exe', '-B',
                            folder / 'source/setup/mods.py', '--help'], check=True,
                           stdout=subprocess.DEVNULL)
        return manifest


def platform_of(archive):
    with zipfile.ZipFile(archive) as package_zip:
        return json.loads(package_zip.read('manifest.json'))['platform']


INSTALLER_EXTENSION = {'windows-x64': '.exe', 'macos-arm64': '.dmg', 'linux-x64': '.tar.gz'}
# The files an app bundle may hold besides its payload (scripts/package-app.py).
APP_FILES = {'Contents/Info.plist', 'Contents/MacOS/' + releases.APP_PROGRAM, 'Contents/Resources/defjam.icns',
             'Contents/_CodeSignature/CodeResources'}


APP_PLATFORMS = ('macos-arm64', 'windows-x64')
# What the Windows app may load: Windows' own libraries only (it is linked with the static C runtime
# and a static SDL3), never a Visual C++ runtime, a game library or anything carried beside it.
WINDOWS_SYSTEM_DLLS = {
    'kernel32.dll', 'user32.dll', 'gdi32.dll', 'shell32.dll', 'ole32.dll', 'oleaut32.dll', 'advapi32.dll',
    'imm32.dll', 'setupapi.dll', 'version.dll', 'winmm.dll', 'cfgmgr32.dll', 'hid.dll', 'shlwapi.dll',
    'bcrypt.dll', 'comdlg32.dll', 'uxtheme.dll', 'dwmapi.dll', 'd3d11.dll', 'dxgi.dll', 'd3dcompiler_47.dll',
    'xinput1_4.dll', 'dinput8.dll', 'comctl32.dll', 'shcore.dll', 'winhttp.dll', 'rpcrt4.dll', 'ntdll.dll',
    'combase.dll', 'mfplat.dll', 'mf.dll', 'mfreadwrite.dll', 'powrprof.dll', 'avrt.dll',
}


def pe_imports(data):
    """The DLL names a PE image imports (its import directory), lower case."""
    def u16(at):
        return int.from_bytes(data[at:at + 2], 'little')

    def u32(at):
        if at + 4 > len(data):
            raise ValueError('Not a complete Windows program')
        return int.from_bytes(data[at:at + 4], 'little')
    if data[:2] != b'MZ':
        raise ValueError('Not a Windows program')
    pe = u32(0x3C)
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Not a Windows program')
    sections, optional_size, optional = u16(pe + 6), u16(pe + 20), pe + 24
    directories = optional + (96 if u16(optional) == 0x10B else 112)
    table = optional + optional_size

    def offset(rva):
        for s in range(sections):
            at = table + 40 * s
            size, address, raw = max(u32(at + 8), u32(at + 16)), u32(at + 12), u32(at + 20)
            if address <= rva < address + size:
                return rva - address + raw
        raise ValueError('Import table outside the program file')
    names, rva = [], u32(directories + 8)
    if not rva:
        return names
    at = offset(rva)
    while u32(at + 12):                                     # a descriptor's name; zero ends the table
        start = offset(u32(at + 12))
        names.append(data[start:data.index(b'\0', start)].decode('ascii').lower())
        at += 20
    return names


def inspect_app_windows(program, archive):
    """The Windows app's built-in payload is the inspected payload, byte for byte; it loads only
    Windows' own libraries, and no lifted game code is in it."""
    data = Path(program).read_bytes()
    if releases.pe_resource(data, *releases.PAYLOAD_RESOURCE) != Path(archive).read_bytes():
        raise ValueError('The payload inside the app does not match the inspected payload')
    for name in pe_imports(data):
        if name not in WINDOWS_SYSTEM_DLLS and not name.startswith(('api-ms-win-', 'ext-ms-win-')):
            raise ValueError('The app program loads a library it does not carry: ' + name)
    # Lifted functions are named sub_XXXXXXXX; the payload ZIP itself is compressed source.
    payload = Path(archive).read_bytes()
    rest = data.replace(payload, b'')
    if re.search(rb'sub_[0-9A-Fa-f]{8}', rest) or b'recomp_dispatch' in rest:
        raise ValueError('The app program contains lifted game code')


def app_stem(version, platform):
    return releases.APP_ASSETS[platform].format(v=version).rsplit('.', 1)[0]


def validate_asset_names(version, names, platform='windows-x64', app=False):
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Invalid setup release version')
    stem = f'DefJamSetup-{version}-{platform}'
    extension = INSTALLER_EXTENSION[platform]
    expected = {stem + extension, stem + '.sha256', stem + '.provenance.json'}
    if app:
        if platform not in APP_PLATFORMS:
            raise ValueError('The app is published for macOS and Windows only so far')
        a = app_stem(version, platform)
        expected |= {releases.APP_ASSETS[platform].format(v=version), a + '.sha256', a + '.provenance.json'}
    if set(names) != expected:
        raise ValueError('Release assets must be exactly the approved setup installer and app, their checksums and provenance')


def inspect_app(app_zip, archive, binary_checks=sys.platform == 'darwin'):
    """The app archive holds one bundle: the approved bundle files and, under Resources/payload, exactly
    the inspected payload's files, byte for byte. The program carries no lifted game code and loads
    only the system's libraries and the payload's SDL3."""
    import subprocess
    with zipfile.ZipFile(archive) as payload_zip:
        expected = {name: hashlib.sha256(payload_zip.read(name)).hexdigest()
                    for name in payload_zip.namelist() if not name.endswith('/')}
    with tempfile.TemporaryDirectory() as temporary:
        folder = Path(temporary)
        releases.safe_zip(Path(app_zip).read_bytes(), folder)      # no links, escapes or duplicates
        if sorted(p.name for p in folder.iterdir()) != [releases.APP_BUNDLE]:
            raise ValueError('The app archive must hold only ' + releases.APP_BUNDLE)
        bundle = folder / releases.APP_BUNDLE
        payload, found = {}, set()
        for path in bundle.rglob('*'):
            if not path.is_file():
                continue
            name = path.relative_to(bundle).as_posix()
            if name.startswith('Contents/Resources/payload/'):
                payload[name[len('Contents/Resources/payload/'):]] = engine.digest(path)
            elif name in APP_FILES:
                found.add(name)
            else:
                raise ValueError('Unexpected file in the app: ' + name)
        if payload != expected:
            raise ValueError('The payload inside the app does not match the inspected payload')
        program = bundle / 'Contents/MacOS' / releases.APP_PROGRAM
        if not {'Contents/Info.plist', 'Contents/MacOS/' + releases.APP_PROGRAM} <= found or not os.access(program, os.X_OK):
            raise ValueError('The app has no program or Info.plist')
        if binary_checks:                                         # nm and otool are macOS's
            symbols = subprocess.run(['/usr/bin/nm', '-U', program], check=True, capture_output=True, text=True).stdout
            if re.search(r' _sub_[0-9A-Fa-f]{8}\b', symbols):
                raise ValueError('The app program contains lifted game code')
            loads = subprocess.run(['/usr/bin/otool', '-L', program], check=True, capture_output=True, text=True).stdout
            for line in loads.splitlines()[1:]:
                library = line.split(' (')[0].strip()
                if not library.startswith(('/usr/lib/', '/System/Library/', '@rpath/libSDL3.0.dylib')):
                    raise ValueError('The app program loads a library it does not carry: ' + library)


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


def verify_embedded_payload_macos(image, archive):
    """Mount the disk image read-only and compare the payload inside the app with the inspected one."""
    import subprocess
    with tempfile.TemporaryDirectory() as mountpoint:
        subprocess.run(['/usr/bin/hdiutil', 'attach', '-readonly', '-nobrowse', '-noverify', '-mountpoint', mountpoint,
                        str(image)], check=True, stdout=subprocess.DEVNULL)
        try:
            bundled = Path(mountpoint) / 'Def Jam Setup.app/Contents/Resources/payload.zip'
            if not bundled.is_file() or engine.digest(bundled) != engine.digest(archive):
                raise ValueError('Installer payload does not match the inspected payload')
        finally:
            subprocess.run(['/usr/bin/hdiutil', 'detach', '-force', mountpoint], check=True, stdout=subprocess.DEVNULL)


def verify_embedded_payload_linux(tarball, archive):
    """Every file under payload/ in the tarball is the inspected ZIP's, byte for byte, and nothing else ships."""
    import tarfile
    with zipfile.ZipFile(archive) as package_zip:
        expected = {name: hashlib.sha256(package_zip.read(name)).hexdigest()
                    for name in package_zip.namelist() if not name.endswith('/')}
    found, top = {}, set()
    with tarfile.open(tarball) as image:
        for member in image.getmembers():
            parts = member.name.split('/')
            if member.issym() or member.islnk() or not (member.isfile() or member.isdir()) or '..' in parts:
                raise ValueError('Unexpected entry in the setup archive: ' + member.name)
            if len(parts) > 1:
                top.add(parts[1])
            if member.isfile() and len(parts) > 2 and parts[1] == 'payload':
                found['/'.join(parts[2:])] = hashlib.sha256(image.extractfile(member).read()).hexdigest()
    if found != expected:
        raise ValueError('Installer payload does not match the inspected payload')
    if top - {'payload', 'DefJamSetup.sh', 'READ ME FIRST.txt', 'Licenses'}:
        raise ValueError('Unexpected files in the setup archive: ' + ', '.join(sorted(top)))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--payload', type=Path, required=True)
    parser.add_argument('--installer', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--app', type=Path, help='The app: macOS, the archive from scripts/package-app.py --zip; '
                                                 'Windows, DefJamRecompiled.exe built with its payload')
    parser.add_argument('--development', action='store_true')
    args = parser.parse_args(argv)
    manifest = inspect_payload(args.payload)
    platform = manifest['platform']
    {'macos-arm64': verify_embedded_payload_macos, 'linux-x64': verify_embedded_payload_linux}.get(
        platform, verify_embedded_payload)(args.installer, args.payload)
    if manifest.get('development') and not args.development:
        raise ValueError('A development payload cannot be published')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stem = f'DefJamSetup-{manifest["version"]}-{platform}'
    import shutil
    exe = args.output_dir / (stem + INSTALLER_EXTENSION[platform])
    shutil.copyfile(args.installer, exe)
    sha = engine.digest(exe)
    (args.output_dir / (stem + '.sha256')).write_text(sha + '  ' + exe.name + '\n', encoding='utf-8')
    (args.output_dir / (stem + '.provenance.json')).write_text(json.dumps(
        {'installer_sha256': sha, 'payload_sha256': engine.digest(args.payload), **manifest}, indent=2) + '\n', encoding='utf-8')
    if args.app:
        if platform not in APP_PLATFORMS:
            raise ValueError('The app is published for macOS and Windows only so far')
        if platform == 'windows-x64':
            inspect_app_windows(args.app, args.payload)
        else:
            inspect_app(args.app, args.payload)
        app = args.output_dir / releases.APP_ASSETS[platform].format(v=manifest['version'])
        shutil.copyfile(args.app, app)
        app_sha = engine.digest(app)
        a = app_stem(manifest['version'], platform)
        (args.output_dir / (a + '.sha256')).write_text(app_sha + '  ' + app.name + '\n', encoding='utf-8')
        (args.output_dir / (a + '.provenance.json')).write_text(json.dumps(
            {'kind': 'app', 'app_sha256': app_sha, 'payload_sha256': engine.digest(args.payload), **manifest},
            indent=2) + '\n', encoding='utf-8')
        print('Verified game-free app assets:', a)
    validate_asset_names(manifest['version'], [p.name for p in args.output_dir.iterdir()], platform, app=bool(args.app))
    print('Verified game-free setup assets:', stem)
    return 0


if __name__ == '__main__':
    sys.exit(main())

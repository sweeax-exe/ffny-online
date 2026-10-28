"""Fingerprint-gated scene repair against a fake guest; game bytes stay local."""
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def repair(tmp_path_factory):
    cc = shutil.which("cl") if os.name == "nt" else (shutil.which("cc") or shutil.which("clang"))
    if not cc:
        pytest.skip("native C compiler unavailable")
    folder = tmp_path_factory.mktemp("menu-geometry")
    (folder / "recomp/gen").mkdir(parents=True)
    (folder / "host.h").write_text("")
    (folder / "recomp/gen/recomp_types.h").write_text('''
#include <stdint.h>
extern unsigned char memory[];
#define MEM8(a)  (*(uint8_t *)(memory + (a)))
#define MEM16(a) (*(uint16_t *)(memory + (a)))
#define MEM32(a) (*(uint32_t *)(memory + (a)))
''')
    driver = folder / "driver.c"
    driver.write_text('''
#include <stdio.h>
#include <stdint.h>
#include "menu_geometry.h"
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
unsigned char memory[65536];
int main(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    size_t n = fread(memory + 1024, 1, 32768, stdin);
    menu_geometry_register(0);
    menu_geometry_register(1024);
    menu_geometry_register(1024); /* repeated registrations must be harmless */
    return fwrite(memory + 1024, 1, n, stdout) == n ? 0 : 1;
}
''')
    exe = folder / ("repair.exe" if os.name == "nt" else "repair")
    sources = [str(ROOT / "src/patches/menu_geometry.c"), str(driver)]
    if os.name == "nt":
        command = [cc, "/nologo", "/W4", "/WX", "/D_CRT_SECURE_NO_WARNINGS",
                   "/I" + str(folder), "/I" + str(ROOT / "src/patches"),
                   *sources, "/Fe:" + str(exe)]
    else:
        command = [cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-I", str(folder), "-I", str(ROOT / "src/patches"),
                   *sources, "-o", str(exe)]
    subprocess.run(command, cwd=folder, check=True)

    def run(blob, enabled=True):
        env = dict(os.environ, RECOMP_MENU_TRANSITION_REPAIR="1" if enabled else "0")
        env.pop("RECOMP_MENU_GEOMETRY_TRACE", None)
        return subprocess.run([exe], input=blob, capture_output=True, env=env, check=True)
    return run


def test_other_objects_are_not_changed(repair):
    for blob in (b"", b"other resource", bytes(13089)):
        assert repair(blob).stdout == blob
    # Same section layout is insufficient: the whole object must match.
    blob = bytearray(13089)
    struct.pack_into("<II", blob, 0, 0x464C457F, 0x00010101)
    struct.pack_into("<I", blob, 32, 7452)
    struct.pack_into("<HHH", blob, 46, 40, 6, 3)
    struct.pack_into("<II", blob, 7452 + 5 * 40 + 16, 10852, 2237)
    assert repair(blob).stdout == blob
    assert not repair(blob).stderr


def test_supported_dump_repair_is_exact_and_repeatable(repair):
    """Optional local integration: no game object is stored in the fixtures."""
    data = os.environ.get("DEFJAM_DATA")
    archive = Path(data) / "extracted/screens/screens.viv" if data else None
    if not archive or not archive.is_file():
        pytest.skip("set DEFJAM_DATA for the local transition-object integration check")
    spec = importlib.util.spec_from_file_location("geometry_hd_assets", ROOT / "scripts/hd_assets.py")
    assets = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(assets)
    outer = dict(assets.archive_entries(archive.read_bytes()))
    inner = dict(assets.archive_entries(assets.unpack_refpack(outer["screens/trans/t1.big"])))
    original = inner["t1.o"]
    expected = bytearray(original)
    for i in range(4):
        at = 64 + 740 + i * 6
        x, y = struct.unpack_from("<hh", expected, at)
        struct.pack_into("<hh", expected, at, x - 4096, y - 1656)
    actual = repair(original)
    assert actual.stdout == expected
    assert actual.stderr.count(b"restored first scenery tile") == 1
    assert repair(actual.stdout).stdout == expected
    assert not repair(actual.stdout).stderr
    assert repair(original, enabled=False).stdout == original
    changed = bytearray(original)
    changed[-1] ^= 1
    assert repair(changed).stdout == changed

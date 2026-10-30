"""Language packs: scripts/langpack.py (the format and the authoring tool) and
src/hooks/pc_language.c (what the game does at start-up), checked against each other.

Everything runs on a synthetic dump: BIG archives with a RefPack entry, a nested
archive, a plain file and a minimal XBE. No game data is used.
"""
import hashlib
import importlib.util
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('langpack', ROOT / 'scripts' / 'langpack.py')
lp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lp)


# --- synthetic game files ----------------------------------------------------------------------

def big(entries, magic=b'BIG4', align=16):
    """A BIG archive: header, directory, 8-byte trailer, then aligned entry data."""
    directory = b''.join(struct.pack('>II', 0, 0) + name.encode() + b'\0' for name, _ in entries)
    header = 16 + len(directory) + 8
    out = bytearray(magic + b'\0' * 4 + struct.pack('>II', len(entries), header) + directory + b'L253\0\0\0\0')
    pos = 16
    for name, data in entries:
        out += b'\0' * (-len(out) % align)
        struct.pack_into('>II', out, pos, len(out), len(data))
        pos += 8 + len(name) + 1
        out += data
    struct.pack_into('<I', out, 4, len(out))
    return bytes(out)


def refpack_with_copy():
    """'ABCD' as literals, then a back-reference that repeats it twice: 'ABCDABCDABCD'."""
    return bytes([0x10, 0xFB, 0, 0, 12, 0xE0]) + b'ABCD' + bytes([0x14, 3, 0xFC])


def xbe(data_section):
    """Enough of an XBE for the section table: .text then .data, raw = file offsets."""
    base = 0x10000
    image = bytearray(0x3000)
    struct.pack_into('<I', image, 0x104, base)
    struct.pack_into('<I', image, 0x11C, 2)
    struct.pack_into('<I', image, 0x120, base + 0x200)
    names = 0x300
    image[names:names + 6] = b'.text\0'
    image[names + 8:names + 14] = b'.data\0'
    struct.pack_into('<IIIIII', image, 0x200, 0x16, 0x11000, 0x1000, 0x1000, 0x1000, base + names)
    struct.pack_into('<IIIIII', image, 0x200 + 56, 0x7, 0x12000, 0x1000, 0x2000, 0x1000, base + names + 8)
    image[0x1000:0x1010] = b'\x55\x8b\xec' + b'\x90' * 13     # "code"
    image[0x2000:0x2000 + len(data_section)] = data_section
    return bytes(image)


def make_game(root):
    loc = ('BEST WIN PERCENTAGE\0' * 40).encode('utf-16-le')
    inner = big([('x.xsh', b'SHPX' + bytes(range(256)) * 8), ('y.bin', b'unchanged inner entry')], magic=b'BIGF')
    misc = big([('cmusen.loc', lp.refpack_store(loc)), ('hud.xsh', b'H' * 3000), ('copy.bin', refpack_with_copy())])
    screens = big([('screens/a.big', lp.refpack_store(inner)), ('screens/b.big', b'plain neighbour')])
    files = {
        'assets/misc.viv': misc,
        'screens/screens.viv': screens,
        'gconfig.xml': b'<config lang="en"/>\n' * 20,
        'default.xbe': xbe(b'LOADING' + b'\0' * 64),
    }
    for rel, data in files.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return files


def make_translation(original, modified):
    """The translator's copy: a longer string table, a redrawn texture, a nested entry,
    a whole file and a data-section change in the XBE."""
    shutil.copytree(original, modified)
    misc = (original / 'assets/misc.viv').read_bytes()
    entries = {e[0]: misc[e[1]:e[1] + e[2]] for e in lp.big_entries(misc)}
    loc = ('MEJOR PORCENTAJE DE VICTORIAS\0' * 40).encode('utf-16-le')
    hud = bytearray(entries['hud.xsh'])
    hud[100:140] = b'S' * 40
    (modified / 'assets/misc.viv').write_bytes(lp.big_replace(misc, {
        'cmusen.loc': lp.refpack_store(loc), 'hud.xsh': bytes(hud)}))
    screens = (original / 'screens/screens.viv').read_bytes()
    _, off, size, _ = lp.big_find(lp.big_entries(screens), 'screens/a.big')
    inner = lp.refpack_decompress(screens[off:off + size])
    _, ioff, isize, _ = lp.big_find(lp.big_entries(inner), 'x.xsh')
    xsh = bytearray(inner[ioff:ioff + isize])
    xsh[64:96] = b'\xEE' * 32
    inner = lp.big_replace(inner, {'x.xsh': bytes(xsh)})
    (modified / 'screens/screens.viv').write_bytes(lp.big_replace(screens, {'screens/a.big': lp.refpack_store(inner)}))
    (modified / 'gconfig.xml').write_bytes(b'<config lang="es"/>\n' * 20)
    image = bytearray((original / 'default.xbe').read_bytes())
    image[0x2000:0x2008] = b'CARGANDO'
    (modified / 'default.xbe').write_bytes(bytes(image))


@pytest.fixture()
def dump(tmp_path):
    game = tmp_path / 'game'
    make_game(game)
    make_translation(game, tmp_path / 'modified')
    pack = tmp_path / 'data' / 'mods' / 'xx-test'
    assert lp.main(['make', str(game), str(tmp_path / 'modified'), '--out', str(pack)]) == 0
    return tmp_path, game, pack


# --- the format --------------------------------------------------------------------------------

@pytest.mark.parametrize('seed', range(6))
def test_delta_round_trip(seed):
    rng = random.Random(seed)
    source = bytes(rng.randrange(256) for _ in range(rng.randrange(0, 4000)))
    target = bytearray(source)
    for _ in range(rng.randrange(1, 8)):
        at = rng.randrange(0, len(target) + 1)
        if rng.random() < 0.5:
            target[at:at] = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 300)))
        else:
            del target[at:at + rng.randrange(1, 200)]
    if seed % 2:
        target = bytearray(source)
        for _ in range(20):
            if target:
                target[rng.randrange(len(target))] ^= 0xFF
    delta = lp.delta_make(source, bytes(target))
    assert lp.delta_apply(delta, source) == bytes(target)
    with pytest.raises(lp.PackError):
        lp.delta_apply(delta, source + b'x')


def test_delta_carries_only_changes():
    source = os.urandom(50000)
    target = source[:20000] + b'translated text' + source[20000:]
    copied, added = lp.delta_stats(lp.delta_make(source, target))
    assert copied == len(source) and added == len(b'translated text')


def test_refpack():
    assert lp.refpack_decompress(refpack_with_copy()) == b'ABCDABCDABCD'
    for n in (0, 1, 3, 4, 5, 111, 112, 113, 5000):
        data = os.urandom(n)
        assert lp.refpack_decompress(lp.refpack_store(data)) == data
    # ZARF's encoder pads its last literal block past the stated size.
    padded = bytes([0x10, 0xFB, 0, 0, 5, 0xE1]) + b'ABCDEFGH' + bytes([0xFC])
    assert lp.refpack_decompress(padded) == b'ABCDE'


def test_make_lists_entries_and_apply_rebuilds(dump):
    tmp, game, pack = dump
    _, files = lp.read_pack(pack)
    targets = sorted('|'.join(parts) for parts, _ in files)
    assert targets == ['assets/misc.viv|cmusen.loc', 'assets/misc.viv|hud.xsh', 'default.xbe', 'gconfig.xml',
                       'screens/screens.viv|screens/a.big|x.xsh']
    out = lp.apply_pack(pack, game)
    misc = out['assets/misc.viv']
    entries = {e[0]: e for e in lp.big_entries(misc)}
    loc = lp.unpack_entry(misc[entries['cmusen.loc'][1]:][:entries['cmusen.loc'][2]])
    assert loc.decode('utf-16-le').startswith('MEJOR PORCENTAJE')
    original = (game / 'assets/misc.viv').read_bytes()
    # Untouched entries keep their bytes and their offsets; the archive's size field is updated.
    assert entries['copy.bin'][1:3] == {e[0]: e for e in lp.big_entries(original)}['copy.bin'][1:3]
    assert struct.unpack_from('<I', misc, 4)[0] == len(misc)
    assert out['gconfig.xml'].startswith(b'<config lang="es"/>')
    assert out['default.xbe'][0x2000:0x2008] == b'CARGANDO'


def test_pack_holds_no_unchanged_game_bytes(dump):
    _, _, pack = dump
    added = sum(lp.delta_stats((pack / p).read_bytes())[1] for _, p in lp.read_pack(pack)[1])
    assert added < 3000            # the string table, 40 + 32 texture bytes, a line, a word


def test_make_refuses_code_and_new_entries(tmp_path):
    game = tmp_path / 'game'
    make_game(game)
    shutil.copytree(game, tmp_path / 'code')
    image = bytearray((game / 'default.xbe').read_bytes())
    image[0x1001] = 0x90
    (tmp_path / 'code' / 'default.xbe').write_bytes(bytes(image))
    assert lp.main(['make', str(game), str(tmp_path / 'code'), '--out', str(tmp_path / 'p1')]) == 1
    shutil.copytree(game, tmp_path / 'added')
    (tmp_path / 'added' / 'assets/misc.viv').write_bytes(big([('new.loc', b'x')]))
    assert lp.main(['make', str(game), str(tmp_path / 'added'), '--out', str(tmp_path / 'p2')]) == 1


def test_check_reports_a_different_dump(dump):
    tmp, game, pack = dump
    assert lp.main(['check', str(pack), '--game', str(game)]) == 0
    (game / 'gconfig.xml').write_bytes(b'<config lang="fr"/>\n' * 20)
    assert lp.main(['check', str(pack), '--game', str(game)]) == 1


@pytest.mark.parametrize('line', ['../x.viv = patches/a.djd', 'a.viv = ../a.djd', 'a.viv = patches/a.bin',
                                  '/abs.viv = patches/a.djd', 'a.viv| = patches/a.djd'])
def test_unsafe_targets_are_rejected(tmp_path, line):
    (tmp_path / 'language.ini').write_text('[language]\nschema=1\nname=X\nlanguage=xx\n[files]\n' + line + '\n')
    with pytest.raises(lp.PackError):
        lp.read_pack(tmp_path)


# --- the runtime, against the reference --------------------------------------------------------

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host.h"
#include "recomp_settings.h"
#include "pc_language.h"
#include "recomp/gen/recomp_types.h"
unsigned char memory[0x1000];
#if defined(_WIN32)
BOOL (*g_xbox_game_file_redirect)(const char *relative, wchar_t *host_path, DWORD size);
#else
BOOL (*g_xbox_game_file_redirect)(const char *relative, char *host_path, DWORD size);
#endif
void defjam_subtitles_after_nis_setup(uint32_t state);

int main(int argc, char **argv)
{
    RecompSetting settings[4];
    char path[2048];
    size_t n = pc_language_settings(settings, 4);
    recomp_settings_register(settings, n);
    pc_language_start(argv[1], argv[2]);
    for (int i = 3; i < argc; i++) {
#if defined(_WIN32)
        wchar_t wide[2048];
        int served = g_xbox_game_file_redirect && g_xbox_game_file_redirect(argv[i], wide, 2048) &&
                     WideCharToMultiByte(CP_ACP, 0, wide, -1, path, sizeof path, NULL, NULL);
#else
        int served = g_xbox_game_file_redirect && g_xbox_game_file_redirect(argv[i], path, sizeof path);
#endif
        if (served)
            printf("serve %s %s\n", argv[i], path);
        else
            printf("keep %s\n", argv[i]);
    }
    {
        /* The XBE as main.c has it, patched in memory. */
        char xbe[2048];
        FILE *f;
        snprintf(xbe, sizeof xbe, "%s/default.xbe", argv[2]);
        f = fopen(xbe, "rb");
        if (f) {
            static unsigned char image[1 << 20];
            size_t got = fread(image, 1, sizeof image, f);
            fclose(f);
            pc_language_patch_xbe(image, got);
            printf("xbe %.8s\n", (const char *)image + 0x2000);
        }
    }
    MEM32(0x104) = 0x1234;      /* subtitle data loaded, not yet shown */
    MEM32(0x108) = 0;
    defjam_subtitles_after_nis_setup(0x100);
    printf("subtitles %s\n", MEM32(0x108) == 0x1234 ? "all" : "game");
    return 0;
}
'''


@pytest.fixture(scope='module')
def runtime_tool(tmp_path_factory):
    if not (ROOT / 'tools/xboxrecomp/src/settings/recomp_settings.c').is_file():
        pytest.skip('native language fixture needs the initialized toolkit submodule')
    compiler = shutil.which('cl') if os.name == 'nt' else shutil.which('cc')
    if not compiler:
        pytest.skip('native language fixture needs a C compiler')
    folder = tmp_path_factory.mktemp('langpack-native')
    (folder / 'recomp/gen').mkdir(parents=True)
    (folder / 'recomp/gen/recomp_types.h').write_text(
        '#include <stdint.h>\nextern unsigned char memory[];\n'
        '#define MEM32(a) (*(uint32_t *)(memory + (a)))\n')
    (folder / 'hooks').mkdir()
    shutil.copy(ROOT / 'src/hooks/pc_language.h', folder / 'hooks/pc_language.h')
    if os.name == 'nt':
        host = '#include <windows.h>\n#define HOST_SEP "\\\\"\n'
    else:
        host = ('typedef int BOOL;\ntypedef unsigned int DWORD;\n#define TRUE 1\n#define FALSE 0\n'
                '#define HOST_SEP "/"\n')
    (folder / 'host.h').write_text(host)
    driver = folder / 'driver.c'
    driver.write_text(DRIVER)
    exe = folder / ('language.exe' if os.name == 'nt' else 'language')
    includes = [folder, ROOT / 'src/hooks', ROOT / 'tools/xboxrecomp/src/settings']
    sources = [driver, ROOT / 'src/hooks/pc_language.c', ROOT / 'src/patches/subtitles.c',
               ROOT / 'tools/xboxrecomp/src/settings/recomp_settings.c']
    if os.name == 'nt':
        cmd = [compiler, '/nologo', '/O2', '/TC', *['/I' + str(p) for p in includes], *map(str, sources),
               '/Fe:' + str(exe)]
    else:
        cmd = [compiler, '-O2', '-Wall', *['-I' + str(p) for p in includes], *map(str, sources), '-o', str(exe),
               '-pthread']
    result = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


def run_runtime(tool, data, game, *paths, **env_values):
    env = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_')}
    env['RECOMP_SETTINGS'] = 'none'
    env.update(env_values)
    result = subprocess.run([str(tool), str(data), str(game), *paths], env=env, capture_output=True,
                            text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.splitlines(), result.stderr


def test_runtime_matches_reference(runtime_tool, dump):
    tmp, game, pack = dump
    data = tmp / 'data'
    lines, err = run_runtime(runtime_tool, data, game, 'ASSETS\\MISC.VIV', 'screens/screens.viv',
                             'gconfig.xml', 'assets\\other.viv', 'default.xbe', RECOMP_LANGUAGE_PACK='xx-test')
    assert '(built now)' in err
    served = {line.split()[1]: line.split(None, 2)[2] for line in lines if line.startswith('serve ')}
    assert set(served) == {'ASSETS\\MISC.VIV', 'screens/screens.viv', 'gconfig.xml'}
    assert 'keep assets\\other.viv' in lines and 'keep default.xbe' in lines
    reference = lp.apply_pack(pack, game)
    for asked, rel in (('ASSETS\\MISC.VIV', 'assets/misc.viv'), ('screens/screens.viv', 'screens/screens.viv'),
                       ('gconfig.xml', 'gconfig.xml')):
        assert Path(served[asked]).read_bytes() == reference[rel], rel
    assert 'xbe CARGANDO' in lines
    assert 'subtitles all' in lines            # the pack's default story_subtitles=all
    # The dump is untouched, and a second start reuses the cache.
    assert (game / 'gconfig.xml').read_bytes().startswith(b'<config lang="en"/>')
    lines, err = run_runtime(runtime_tool, data, game, 'gconfig.xml', RECOMP_LANGUAGE_PACK='xx-test')
    assert '(built now)' not in err and lines[0].startswith('serve gconfig.xml')


def test_runtime_without_a_pack_changes_nothing(runtime_tool, dump):
    tmp, game, _ = dump
    lines, err = run_runtime(runtime_tool, tmp / 'data', game, 'assets/misc.viv')
    assert lines == ['keep assets/misc.viv', 'xbe LOADING', 'subtitles game']
    assert "the game's own language" in err
    lines, _ = run_runtime(runtime_tool, tmp / 'data', game, RECOMP_STORY_SUBTITLES='all cutscenes')
    assert lines[-1] == 'subtitles all'
    lines, _ = run_runtime(runtime_tool, tmp / 'data', game, RECOMP_LANGUAGE_PACK='xx-test',
                           RECOMP_STORY_SUBTITLES='as the game does')
    assert lines[-1] == 'subtitles game'


def test_runtime_finds_the_mod_in_the_texture_root(runtime_tool, dump):
    # The language mod sits in the mods folder the launcher and the texture loader use.
    tmp, game, pack = dump
    elsewhere = tmp / 'elsewhere'
    elsewhere.mkdir()
    shutil.move(str(pack), str(elsewhere / 'xx-test'))
    lines, err = run_runtime(runtime_tool, tmp / 'data', game, 'gconfig.xml', RECOMP_LANGUAGE_PACK='xx-test')
    assert lines[0] == 'keep gconfig.xml' and 'not used' in err
    lines, err = run_runtime(runtime_tool, tmp / 'data', game, 'gconfig.xml', RECOMP_LANGUAGE_PACK='xx-test',
                             RECOMP_TEXTURE_ROOT=str(elsewhere))
    assert lines[0].startswith('serve gconfig.xml'), err


def test_runtime_refuses_a_different_dump(runtime_tool, dump):
    tmp, game, _ = dump
    (game / 'gconfig.xml').write_bytes(b'<config lang="fr"/>\n' * 20)
    lines, err = run_runtime(runtime_tool, tmp / 'data', game, 'gconfig.xml', 'assets/misc.viv',
                             RECOMP_LANGUAGE_PACK='xx-test')
    assert lines[:2] == ['keep gconfig.xml', 'keep assets/misc.viv'] and 'xbe LOADING' in lines
    assert 'not used' in err and 'differs from the one the pack was made for' in err


def test_runtime_refuses_xbe_code_changes(runtime_tool, dump):
    tmp, game, pack = dump
    original = (game / 'default.xbe').read_bytes()
    image = bytearray(original)
    image[0x1001] = 0x90
    image[0x2000:0x2008] = b'CARGANDO'
    (pack / 'patches' / 'xbe.djd').write_bytes(lp.delta_make(original, bytes(image)))
    ini = (pack / 'language.ini').read_text().splitlines()
    ini = [line if not line.startswith('default.xbe') else 'default.xbe = patches/xbe.djd' for line in ini]
    (pack / 'language.ini').write_text('\n'.join(ini) + '\n')
    lines, err = run_runtime(runtime_tool, tmp / 'data', game, RECOMP_LANGUAGE_PACK='xx-test')
    assert 'xbe LOADING' in lines and 'outside .rdata/.data' in err

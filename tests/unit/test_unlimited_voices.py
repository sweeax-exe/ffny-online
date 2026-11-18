"""Own voices of community fighters beyond the places (fighter numbers 1000 and up): the files the composer adds
under the fighter's number and the banks' voice entries (setup/ffnycontent/unlimited.py, content_compose
.finish_logical), and the game's pure side (src/hooks/fighter_voice.c: the names, the directory lookup and the
redirect plan), compiled natively. No dump needed: the speech archive is synthetic (test_fighter_voice.py's)."""
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "setup"))
sys.path.insert(0, str(ROOT / "tests" / "unit"))

from ffnycontent import fightervoice as fv, newfighter as nf, unlimited, viv  # noqa: E402
from test_fighter_voice import own_lines, synthetic_speech, trim  # noqa: E402


def test_bank_voices_are_the_bank_places_own():
    assert [unlimited.bank_voice(b) for b in unlimited.BANKS] == [64, 65, 66, 67]
    assert [fv.voice_number(b) for b in unlimited.BANKS] == [64, 65, 66, 67]
    with pytest.raises(nf.FighterError):
        unlimited.bank_voice(123)
    # the speech code's bank flags (0x33EA78, 200 bytes) take 064..067; no stock voice is there
    assert all(v < 200 for v in range(64, 68))


def test_numbered_entry_names():
    names = unlimited.logical_entries(1000)
    assert len(names) == 17 and len(set(n.lower() for n in names)) == 17
    assert "speech\\Taunts\\Def_Jam_Characters\\L1000_065.hdr" in names
    assert "speech\\Taunts\\Def_Jam_Characters\\L1000.dat" in names
    assert "speech\\streams\\intro\\L1000.asf" in names and "speech\\streams\\extro\\L1000.asf" in names
    assert unlimited.logical_entry("speech\\Enviro\\Def_Jam_Characters\\067.hdr", 1234) == \
        "speech\\Enviro\\Def_Jam_Characters\\L1234_067.hdr"
    # no name a voice number could give (%03d of 0..999)
    assert not any(n.rsplit("\\", 1)[1].split(".")[0].isdigit() for n in names)


def _voiced(fe_id, speech, names_extra=()):
    pack, lines = own_lines()
    return dict(unlimited.logical_voice_files(speech.__getitem__, lambda: list(speech) + list(names_extra), 55, 25,
                                              fe_id, lines, pack.__getitem__)), pack


def test_logical_voice_files_are_a_place_voice_under_the_number():
    speech = synthetic_speech()
    got, pack = _voiced(1000, speech)
    assert sorted(got) == sorted(unlimited.logical_entries(1000))
    # the same bytes as a place fighter's own voice at the first bank's number, the headers once per bank voice
    pk, ln = own_lines()
    place = dict(fv.voice_files(speech.__getitem__, lambda: list(speech), 55, 25, 124, ln, pk.__getitem__))
    for bank in fv.BANKS:
        assert got["speech\\%s\\Def_Jam_Characters\\L1000.dat" % bank] == place[fv.bank_entry(bank, 64, "dat")]
        for v in (64, 65, 66, 67):
            h = got["speech\\%s\\Def_Jam_Characters\\L1000_%03d.hdr" % (bank, v)]
            info = fv.parse_hdr(h)
            assert info["voice"] == v and info["family"] == fv.FAMILY[bank]
            assert h[:2] + h[4:] == place[fv.bank_entry(bank, 64, "hdr")][:2] + place[fv.bank_entry(bank, 64, "hdr")][4:]
    assert got["speech\\streams\\intro\\L1000.asf"] == pack["voice/intro-1.asf"]
    assert got["speech\\streams\\extro\\L1000.asf"] == speech[fv.stream_entry("extro", 25)]
    taunts = fv.bank_clips(got["speech\\Taunts\\Def_Jam_Characters\\L1000_066.hdr"],
                           got["speech\\Taunts\\Def_Jam_Characters\\L1000.dat"])
    assert all(trim(c) == pack["voice/taunt-1.sch"] for c in taunts)
    # same inputs, same bytes; the banks' placeholders already there do not get in the way
    again, _ = _voiced(1000, speech, [fv.bank_entry("Taunts", 64, "hdr")])
    assert again == got
    # a number already there is refused, and so is an ID that is no fighter number
    with pytest.raises(fv.VoiceError):
        _voiced(1000, speech, ["speech\\streams\\intro\\L1000.asf"])
    with pytest.raises(fv.VoiceError):
        _voiced(127, speech)


def test_bank_voice_placeholders():
    speech = synthetic_speech()
    out = dict(unlimited.bank_voice_placeholders(speech.__getitem__, lambda: list(speech)))
    assert sorted(out) == sorted(e for b in unlimited.BANKS for e in fv.voice_entries(unlimited.bank_voice(b)))
    for b in unlimited.BANKS:
        v = unlimited.bank_voice(b)
        for bank in fv.BANKS:
            info = fv.parse_hdr(out[fv.bank_entry(bank, v, "hdr")])
            assert info["voice"] == v and info["count"] == fv.parse_hdr(speech[fv.bank_entry(bank, 25, "hdr")])["count"]
            assert all(trim(c) == fv.silence_clip()
                       for c in fv.bank_clips(out[fv.bank_entry(bank, v, "hdr")], out[fv.bank_entry(bank, v, "dat")]))
        assert out[fv.stream_entry("intro", v)] == fv.silence_clip(True)
    # once only
    assert unlimited.bank_voice_placeholders(speech.__getitem__, lambda: list(speech) + list(out)) == []


def test_has_logical_voice():
    speech = synthetic_speech()
    got, _ = _voiced(1001, speech)
    assert unlimited.has_logical_voice(list(got), 1001)
    assert not unlimited.has_logical_voice(list(got)[1:], 1001)
    assert not unlimited.has_logical_voice(list(got), 1000)


def test_newfighter_op_of_a_numbered_fighter_adds_its_voice():
    """apply_op with logical=True puts the own voice under the number and leaves word 21 the template's."""
    src = (ROOT / "setup" / "ffnycontent" / "newfighter.py").read_text(encoding="utf-8")
    assert "unlimited.logical_voice_files(" in src
    assert "phase 1: a bank plays the template's voice" not in src
    compose = (ROOT / "setup" / "content_compose.py").read_text(encoding="utf-8")
    assert "unlimited.bank_voice_placeholders(" in compose and "unlimited.has_logical_voice(" in compose


# -- the game's side (src/hooks/fighter_voice.c) ---------------------------------------------------------------------

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include "fighter_voice.h"
int main(int argc, char **argv)
{
    static unsigned char dir[1 << 22];
    FILE *f = fopen(argv[1], "rb");
    size_t n = fread(dir, 1, sizeof dir, f);
    int i, k;
    char name[96];
    fclose(f);
    for (i = 0; i < 5; i++) printf("bank %d %d\n", i, fighter_voice_bank_voice(i));
    for (i = 0; i < FV_FILES; i++) {
        fighter_voice_numbered_entry_name(1000, 65, i, name, sizeof name);
        printf("name %s\n", name);
    }
    for (k = 2; k + 1 < argc; k += 2) {
        long pos[FV_FILES];
        unsigned off[FV_FILES], size[FV_FILES];
        int voice = atoi(argv[k]), id = atoi(argv[k + 1]);
        int ok = fighter_voice_numbered_plan(dir, n, voice, id, pos, off, size);
        printf("plan %d %d %d", voice, id, ok);
        if (ok) for (i = 0; i < FV_FILES; i++) printf(" %ld:%u:%u", pos[i], off[i], size[i]);
        printf("\n");
    }
    {
        unsigned o = 0, s = 0;
        long p = fighter_voice_dir_find(dir, n, "SPEECH/STREAMS/INTRO/L1000.ASF", &o, &s);
        printf("find %ld %u %u\n", p, o, s);
        printf("find %ld\n", fighter_voice_dir_find(dir, n, "speech\\streams\\intro\\L100.asf", NULL, NULL));
    }
    return 0;
}
'''


@pytest.fixture(scope="module")
def plan_exe(tmp_path_factory):
    compiler = shutil.which("cl")
    if not compiler:
        pytest.skip("needs MSVC (cl on PATH)")
    folder = tmp_path_factory.mktemp("fvn")
    (folder / "driver.c").write_text(DRIVER)
    exe = folder / "fvn.exe"
    built = subprocess.run([compiler, "/nologo", "/O2", "/TC", "/I" + str(ROOT / "src/hooks"), str(folder / "driver.c"),
                            str(ROOT / "src/hooks/fighter_voice.c"), "/Fe:" + str(exe)], cwd=folder,
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr

    def run(archive, args):
        path = folder / "speech.viv"
        path.write_bytes(archive)
        return subprocess.run([str(exe), str(path)] + [str(a) for a in args], capture_output=True, text=True,
                              check=True).stdout.splitlines()
    return run


def _records(archive):
    """{lower name: (record position, offset, size)} of a BIG archive's directory."""
    count, hsize = struct.unpack_from(">I", archive, 8)[0], struct.unpack_from(">I", archive, 12)[0]
    pos, out = 16, {}
    for _ in range(count):
        off, size = struct.unpack_from(">II", archive, pos)
        end = archive.index(b"\0", pos + 8)
        out[archive[pos + 8:end].decode("latin-1").lower()] = (pos, off, size)
        pos = end + 1
    assert pos <= hsize
    return out


def test_c_names_and_plan_agree(plan_exe):
    speech = synthetic_speech()
    j, _ = _voiced(1000, speech)
    k, _ = _voiced(1001, speech)
    holders = unlimited.bank_voice_placeholders(speech.__getitem__, lambda: list(speech))
    archive = viv.build_archive(sorted(speech.items()) + holders + sorted(j.items()) + sorted(k.items()))
    out = plan_exe(archive, [64, 1000, 65, 1001, 67, 1000, 65, 1002, 63, 1000, 64, 999])
    assert out[:5] == ["bank 0 64", "bank 1 65", "bank 2 66", "bank 3 67", "bank 4 -1"]
    names = [line[5:] for line in out[5:13]]
    assert set(names) <= set(unlimited.logical_entries(1000)) and len(set(names)) == 8
    assert names == [unlimited.logical_entry(e, 1000) for e in fv.voice_entries(65)]
    rec = _records(archive)
    for line in out[13:16]:
        _, voice, fid, ok, *parts = line.split()
        assert ok == "1"
        for e, part in zip(fv.voice_entries(int(voice)), parts):
            pos, off, size = map(int, part.split(":"))
            assert pos == rec[e.lower()][0]
            target = rec[unlimited.logical_entry(e, int(fid)).lower()]
            assert (off, size) == target[1:] and size > 0
    assert out[16:19] == ["plan 65 1002 0", "plan 63 1000 0", "plan 64 999 0"]
    intro = rec["speech\\streams\\intro\\l1000.asf"]
    assert out[19] == "find %d %d %d" % intro and out[20] == "find -1"
    # without the placeholders there is no plan
    bare = viv.build_archive(sorted(speech.items()) + sorted(j.items()))
    assert plan_exe(bare, [64, 1000])[13] == "plan 64 1000 0"


def test_game_hook_is_wired():
    src = (ROOT / "src" / "hooks" / "new_fighters.c").read_text(encoding="utf-8")
    assert "bank_voice_bind(" in src and "fighter_banks_for_fe(fe)" in src
    assert "fighter_voice_numbered_plan(" in src and "guest_dir(" in src
    manual = (ROOT / "src" / "recomp_manual.c").read_text(encoding="utf-8")
    assert "void sub_0003DB00_enter(void) { new_fighters_voice(); }" in manual

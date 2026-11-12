"""A new fighter's own voice (setup/ffnycontent/fightervoice.py, the new-fighter op's gameplay voice, the game's
src/hooks/fighter_voice.c): the EA-XA R2 codec round trip, the SCHl clip and bank format, the composition of the
voice's eight files at N = 40 + feID - 100 (the same bytes on every PC), the installer's rules and the game's
file check. The stock-clip round trip and the composition onto the player's speech archive need the user's own
dump ($DEFJAM_DATA/extracted or game/) and are skipped without it."""
import array
import hashlib
import json
import math
import os
import random
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "setup"))

import content_compose  # noqa: E402
import mods  # noqa: E402
from ffnycontent import fightervoice as fv, newfighter as nf, viv  # noqa: E402


def _game():
    for base in (os.environ.get("DEFJAM_DATA"), str(ROOT / "game" / "..")):
        if base and (Path(base) / "extracted" / "audio" / "speech2.viv").is_file():
            return Path(base) / "extracted"
    if (ROOT / "game" / "audio" / "speech2.viv").is_file():
        return ROOT / "game"
    return None


GAME = _game()
needs_dump = pytest.mark.skipif(GAME is None, reason="needs the user's own dump")


def speechlike(seconds, rate, seed=1):
    """A voice-like test signal (no recording of anyone): a gliding harmonic tone with a syllable envelope and a
    little noise, int16."""
    rnd = random.Random(seed)
    out = []
    phase = 0.0
    for i in range(int(seconds * rate)):
        t = i / rate
        f0 = 140 + 40 * math.sin(2 * math.pi * 0.7 * t)
        phase += 2 * math.pi * f0 / rate
        env = max(0.0, math.sin(2 * math.pi * 3.1 * t)) ** 0.5
        v = sum(math.sin(k * phase) / k for k in range(1, 8)) * env * 7000 + rnd.gauss(0, 150)
        out.append(max(-32768, min(32767, int(v))))
    return out


def trim(clip):
    """A clip cut out of a bank without the padding after it."""
    return clip[:fv.parse_clip(clip)["end"]]


def snr(x, y):
    num = sum(a * a for a in x)
    den = sum((a - b) ** 2 for a, b in zip(x, y))
    return float("inf") if den == 0 else 10 * math.log10(num / den)


# -- codec and clips ---------------------------------------------------------------------------------------------

def test_codec_round_trip_on_fresh_audio():
    x = speechlike(1.5, fv.BANK_RATE)
    clip = fv.encode_clip(x)
    y, rate = fv.decode_clip(clip)
    assert rate == fv.BANK_RATE and len(y) == len(x)
    assert snr(x, y) > 22                  # 4-bit ADPCM on fresh audio: about 25-30 dB
    assert y[:28] == array.array("h", x[:28])   # the first frame is plain PCM, as the stock clips start
    assert fv.clip_problems(clip) == []
    # what the encoder made, it makes again bit for bit (the decoder's output is exactly representable)
    assert fv.decode_clip(fv.encode_clip(y))[0] == y


def test_clip_format_bank_and_stream():
    bank = fv.encode_clip(speechlike(0.5, fv.BANK_RATE))
    stream = fv.encode_clip(speechlike(0.5, fv.STREAM_RATE), stream=True)
    b, s = fv.parse_clip(bank), fv.parse_clip(stream)
    assert bank[:4] == b"SCHl" and bank[8:11] == b"PT\x07"
    assert b"\x84" not in bank[12:struct.unpack_from("<I", bank, 4)[0]]       # no rate tag: the 24 kHz default
    assert (b["rate"], s["rate"]) == (fv.BANK_RATE, fv.STREAM_RATE)
    # tag values are read signed by the game: 36000 goes out as 00 8C A0, as in EA's own streams
    assert b"\x84\x03\x00\x8c\xa0" in stream[:40]
    assert fv.parse_clip(fv.encode_clip([1000] * 52438))["samples"] == 52438      # 00 CC D6
    bad = bytearray(stream)
    i = bytes(bad).index(b"\x84\x03\x00\x8c\xa0")
    bad[i:i + 5] = b"\x84\x02\x8c\xa0\xfc"          # the value without its zero byte reads negative
    assert "negative" in fv.clip_problems(bytes(bad), stream=True)[0]
    assert all(n <= fv.BANK_BLOCK for _, n, _ in b["blocks"]) and all(n <= fv.STREAM_BLOCK for _, n, _ in s["blocks"])
    assert bank.endswith(b"SCEl\x08\x00\x00\x00") and b["end"] == len(bank)
    assert fv.clip_problems(stream, stream=True) == []
    assert "plays at 24000 Hz" in fv.clip_problems(bank, stream=True)[0]
    assert fv.clip_problems(bank[:-30]) and fv.clip_problems(b"RIFF....")
    assert "at most" in fv.clip_problems(stream, stream=True, seconds=0.2)[0]
    wav = fv.to_wav(stream)
    assert wav[:4] == b"RIFF" and struct.unpack_from("<I", wav, 24)[0] == fv.STREAM_RATE


@needs_dump
def test_stock_clips_round_trip():
    """decode stock -> encode -> decode: the stock audio comes back (it is the codec's own output, so nearly
    exactly), for a bank line and a stream."""
    arc = viv.Archive(str(GAME / "audio" / "speech2.viv"))
    for name, stream in ((fv.stream_entry("intro", 25), True), (fv.bank_entry("Taunts", 25, "dat"), False)):
        x, rate = fv.decode_clip(arc.raw(name))
        assert rate == (fv.STREAM_RATE if stream else fv.BANK_RATE)
        y, _ = fv.decode_clip(fv.encode_clip(x, stream=stream))
        assert snr(x, y) > 60, name


@needs_dump
def test_stock_bank_layout():
    arc = viv.Archive(str(GAME / "audio" / "speech2.viv"))
    hdr, dat = arc.raw(fv.bank_entry("Character", 25, "hdr")), arc.raw(fv.bank_entry("Character", 25, "dat"))
    info = fv.parse_hdr(hdr)
    assert (info["family"], info["voice"], info["count"], info["unit"]) == (0, 25, 59, 0x100)
    clips = fv.bank_clips(hdr, dat)
    assert len(clips) == 59 and all(c[:4] == b"SCHl" for c in clips)
    assert sorted({f[0] for _, f in info["entries"]}) == [0, 1, 2, 3]     # Blazin, weapon, environment, special
    # rebuilt from its own clips, the bank is the stock bank (offsets, total, fields, trailer)
    h2, d2 = fv.build_bank(hdr, "Character", 25, [trim(c) for c in clips])
    assert h2 == hdr and [trim(c) for c in fv.bank_clips(h2, d2)] == [trim(c) for c in clips]
    for v in range(40, 100):
        assert not any(arc.has(e) for e in fv.voice_entries(v)), "voice %03d is in the dump" % v


# -- composition -----------------------------------------------------------------------------------------------

def synthetic_speech():
    """A small speech archive in the stock layout: voice 025 (Doc) with 3 Character lines (Blazin, weapon,
    special), 2 taunts, 1 crowd line and both streams."""
    files = {}
    tpl = {"Character": (0, [(0,), (1,), (3,)], 2), "Taunts": (16625, [(4, 0, 1), (4, 0, 0)], 3),
           "Enviro": (40962, [(1, 0)], 2)}
    for bank, (family, fields, nf_) in tpl.items():
        es = nf_ + 2
        h = bytearray(struct.pack("<HHBBBB", family, 25, nf_, len(fields), len(fields), 0) + b"\0" * 4)
        for f in fields:
            h += b"\0\0" + bytes(f) + b"\0" * (nf_ - len(f))
        h += b"\0" * (-len(h) % 4) + struct.pack("<%dI" % nf_, *([2] * nf_)) + b"\0" + b"\xff" * len(fields)
        clips = [fv.encode_clip(speechlike(0.3, fv.BANK_RATE, seed=i + 10 * len(bank))) for i in range(len(fields))]
        hdr, dat = fv.build_bank(bytes(h), bank, 25, clips)
        files[fv.bank_entry(bank, 25, "hdr")] = hdr
        files[fv.bank_entry(bank, 25, "dat")] = dat
    for kind in ("intro", "extro"):
        files[fv.stream_entry(kind, 25)] = fv.encode_clip(speechlike(0.4, fv.STREAM_RATE, seed=3), stream=True)
    return files


def own_lines():
    pack = {"voice/intro-1.asf": fv.encode_clip(speechlike(0.6, fv.STREAM_RATE, seed=5), stream=True),
            "voice/taunt-1.sch": fv.encode_clip(speechlike(0.5, fv.BANK_RATE, seed=6)),
            "voice/blazin-1.sch": fv.encode_clip(speechlike(0.7, fv.BANK_RATE, seed=7))}
    lines = {"intro": ["voice/intro-1.asf"], "taunt": ["voice/taunt-1.sch"], "blazin": ["voice/blazin-1.sch"]}
    return pack, lines


def test_voice_files_compose_the_eight_entries():
    speech = synthetic_speech()
    pack, lines = own_lines()
    got = fv.voice_files(speech.__getitem__, lambda: list(speech), 55, 25, 100, lines, pack.__getitem__)
    names = [n for n, _ in got]
    assert names == fv.voice_entries(40)
    files = dict(got)
    # Taunts: the one own taunt takes both entries; the fields stay the template's
    th = fv.parse_hdr(files[fv.bank_entry("Taunts", 40, "hdr")])
    assert (th["voice"], th["family"], th["count"]) == (40, 16625, 2)
    assert [f for _, f in th["entries"]] == [(4, 0, 1), (4, 0, 0)]
    assert all(trim(c) == pack["voice/taunt-1.sch"]
               for c in fv.bank_clips(files[fv.bank_entry("Taunts", 40, "hdr")], files[fv.bank_entry("Taunts", 40, "dat")]))
    # Character: the Blazin entry is the own line, the weapon and special entries keep Doc's (no own lines)
    ch = fv.bank_clips(files[fv.bank_entry("Character", 40, "hdr")], files[fv.bank_entry("Character", 40, "dat")])
    stock = fv.bank_clips(speech[fv.bank_entry("Character", 25, "hdr")], speech[fv.bank_entry("Character", 25, "dat")])
    assert trim(ch[0]) == pack["voice/blazin-1.sch"]
    assert [trim(c) for c in ch[1:]] == [trim(c) for c in stock[1:]]
    # the intro is the own one, the extro the template's
    assert files[fv.stream_entry("intro", 40)] == pack["voice/intro-1.asf"]
    assert files[fv.stream_entry("extro", 40)] == speech[fv.stream_entry("extro", 25)]
    # the same inputs give the same bytes (every PC composes the same files)
    again = fv.voice_files(speech.__getitem__, lambda: list(speech), 55, 25, 100, lines, pack.__getitem__)
    assert hashlib.sha256(repr(again).encode()).digest() == hashlib.sha256(repr(got).encode()).digest()
    # feID 127 -> voice 067; a taken voice is refused
    assert fv.voice_files(speech.__getitem__, lambda: list(speech), 55, 25, 127, lines,
                          pack.__getitem__)[0][0].endswith("067.hdr")
    with pytest.raises(fv.VoiceError):
        fv.voice_files(speech.__getitem__, lambda: list(speech) + [fv.stream_entry("intro", 40)], 55, 25, 100,
                       lines, pack.__getitem__)


def test_template_without_a_voice_uses_docs_layout_and_silence():
    speech = synthetic_speech()
    pack, lines = own_lines()
    got = dict(fv.voice_files(speech.__getitem__, lambda: list(speech), 60, 999, 101, lines, pack.__getitem__))
    assert fv.template_voice(60, 999) is None and fv.template_voice(14, 523) == 506 and fv.template_voice(55, 25) == 25
    ch = fv.bank_clips(got[fv.bank_entry("Character", 41, "hdr")], got[fv.bank_entry("Character", 41, "dat")])
    silent = fv.silence_clip()
    assert trim(ch[1]) == silent and got[fv.stream_entry("extro", 41)] == fv.silence_clip(True)


def test_lines_rules():
    pack, lines = own_lines()
    assert fv.lines_problems(lines, pack.__getitem__) == []
    assert fv.voice_problems({"mode": "template"}) == []
    assert fv.voice_problems({"mode": "own"})
    assert "unknown line group" in fv.lines_problems({"lose": ["voice/a.sch"]})[0]
    assert "one to 7" in fv.lines_problems({"taunt": ["voice/t%d.sch" % i for i in range(8)]})[0]
    assert "voice/<name>.sch" in fv.lines_problems({"taunt": ["voice/intro-1.asf"]})[0]
    assert "voice/<name>.asf" in fv.lines_problems({"intro": ["../x.asf"]})[0]
    assert "not in the pack" in fv.lines_problems({"taunt": ["voice/none.sch"]}, pack.__getitem__)[0]
    long = {"voice/t.sch": fv.encode_clip(speechlike(4.3, fv.BANK_RATE))}
    assert "at most 4.0 s" in fv.lines_problems({"taunt": ["voice/t.sch"]}, long.__getitem__)[0]
    assert fv.lines_problems({}) == ["an own voice needs at least one line"]


def test_gameplay_block_with_a_voice():
    _, lines = own_lines()
    voice = {"mode": "own", "lines": lines}
    only = {"version": 1, "voice": voice}
    assert nf.gameplay_problems(only) == [] and nf.voice_only(only) and nf.gameplay_words(only) == {}
    full = {"version": 1, "stats": dict(zip(nf.STAT_KEYS, (9, 6, 10, 5, 10, 0, 10))), "blazins": [4, 4, 4, 4],
            "voice": voice}
    assert nf.gameplay_problems(full) == [] and 11 in nf.gameplay_words(full)
    assert nf.gameplay_problems({"version": 1, "voice": {"mode": "template"}})
    assert nf.gameplay_problems({"version": 1, "voice": {"mode": "own", "lines": {"x": []}}})
    op = nf.make_op(55, "V2IP_109A", "V2IP_7K3A", "Doc Two", gameplay=only)
    assert nf.op_problems(op) == []


def tags_file(entries):
    keys = sorted(entries, key=lambda k: struct.unpack("<Q", k)[0])
    head, body, table = 8 + 4 * len(keys), b"", []
    for k in keys:
        table.append(head + len(body))
        body += k + entries[k]
    return b"TAGS" + struct.pack("<I", len(keys)) + struct.pack("<%dI" % len(keys), *table) + body


def read_tags(blob):
    n = struct.unpack_from("<I", blob, 4)[0]
    offs = list(struct.unpack_from("<%dI" % n, blob, 8)) + [len(blob)]
    return [(blob[offs[i]:offs[i] + 8], blob[offs[i] + 8:offs[i + 1]]) for i in range(n)]


def test_new_model_gets_the_templates_speech_tags():
    """The intro/victory speech starts from the camera banks' sound tags (audio/animtags.dat, keyed by the bank
    name reversed); a new model's CE_/CI_ banks get copies of the template's."""
    ci, ce = b"\x02\0\0\0" + b"\0\0\x01\0;\0\0\0n\0\x01\0;\0\0\0", b"\x01\0\0\0" + b"\0\0\x01\0<\0\0\0"
    blob = tags_file({b"\0A901_IC": ci, b"\0A901_EC": ce, bytes(4) + b"601A": bytes(8), b"\0AZZZ_IC": b"\0" * 4})
    out = nf.add_anim_tags(blob, "V2IP_109A", "V2IP_7K3A")
    got = dict(read_tags(out))
    assert got[b"\0A3K7_IC"] == ci and got[b"\0A3K7_EC"] == ce and len(got) == 6
    keys = [k for k, _ in read_tags(out)]
    assert keys == sorted(keys, key=lambda k: struct.unpack("<Q", k)[0])
    assert nf.add_anim_tags(out, "V2IP_109A", "V2IP_7K3A") == out          # once only
    assert nf.add_anim_tags(blob, "V2IP_999A", "V2IP_7K3A") == blob        # a template without tags


def test_installer_rules():
    assert ".sch" in mods.ALLOWED and ".asf" in mods.ALLOWED
    assert content_compose.stored(b"SCHl data", "audio/speech2.viv") == b"SCHl data"
    assert viv.is_refpack(content_compose.stored(b"model bytes", "assets/v2ip.viv"))


def test_pack_reader(tmp_path):
    (tmp_path / "voice").mkdir()
    (tmp_path / "voice" / "intro-1.asf").write_bytes(b"SCHl")
    read = content_compose.pack_reader(tmp_path)
    assert read("voice/intro-1.asf") == b"SCHl"
    for bad in ("voice/../install.json", "install.json", "voice/a/b.sch", "voice/missing.sch", "/voice/x.sch"):
        with pytest.raises(content_compose.ComposeError):
            read(bad)


def test_viv_names_with_backslashes():
    """The speech archive's names use backslashes; lookups take either slash."""
    arc = viv.Archive(viv.build_archive([("speech\\streams\\intro\\025.asf", b"SCHl")]))
    assert arc.has("speech/streams/intro/025.asf") and arc.has("speech\\streams\\intro\\025.asf")


# -- the game's file check (src/hooks/fighter_voice.c) -------------------------------------------------------------

DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include "fighter_voice.h"
int main(int argc, char **argv)
{
    static unsigned char dir[1 << 20];
    FILE *f = fopen(argv[1], "rb");
    size_t n = fread(dir, 1, sizeof dir, f);
    int i;
    char name[80];
    fclose(f);
    for (i = 2; i < argc; i++) {
        int fe = atoi(argv[i]), v = fighter_voice_number(fe);
        printf("%d %d", fe, v);
        if (v >= 0) printf(" %d", fighter_voice_entries_present(dir, n, v));
        printf("\n");
    }
    for (i = 0; i < FV_FILES; i++) { fighter_voice_entry_name(40, i, name, sizeof name); printf("%s\n", name); }
    return 0;
}
'''


@pytest.fixture(scope="module")
def checker(tmp_path_factory):
    compiler = shutil.which("cl")
    if not compiler:
        pytest.skip("needs MSVC (cl on PATH)")
    folder = tmp_path_factory.mktemp("fv")
    (folder / "driver.c").write_text(DRIVER)
    exe = folder / "fv.exe"
    built = subprocess.run([compiler, "/nologo", "/O2", "/TC", "/I" + str(ROOT / "src/hooks"), str(folder / "driver.c"),
                            str(ROOT / "src/hooks/fighter_voice.c"), "/Fe:" + str(exe)], cwd=folder,
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr

    def run(archive, fe_ids):
        path = folder / "dir.viv"
        path.write_bytes(archive)
        return subprocess.run([str(exe), str(path)] + [str(x) for x in fe_ids], capture_output=True, text=True,
                              check=True).stdout.splitlines()
    return run


def test_c_file_check_agrees(checker):
    full = [(n.upper() if i % 2 else n.replace("\\", "/"), b"x") for i, n in enumerate(fv.voice_entries(40))]
    some = [(n, b"x") for n in fv.voice_entries(41)[:-1]] + [(fv.voice_entries(41)[-1], b"")]
    out = checker(viv.build_archive([("other.bin", b"y")] + full + some), [99, 100, 101, 102, 127, 128])
    assert out[:6] == ["99 -1", "100 40 1", "101 41 0", "102 42 0", "127 67 0", "128 -1"]
    assert out[6:] == fv.voice_entries(40)

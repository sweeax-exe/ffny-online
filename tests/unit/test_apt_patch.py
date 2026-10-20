"""scripts/apt-patch.py with synthetic data only; the real-data check is skipped when no dump is present."""
import importlib.util
import os
from pathlib import Path
import random
import struct

import pytest

ROOT = Path(__file__).parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ap = load("apt_patch", ROOT / "scripts/apt-patch.py")
refpack = load("refpack_script", ROOT / "scripts/refpack.py")


# ----------------------------------------------------------------- RefPack

@pytest.mark.parametrize("n", list(range(0, 300)) + [4095, 4096, 100000])
def test_literal_refpack_round_trip(n):
    data = bytes(random.Random(n).randrange(256) for _ in range(n))
    packed = ap.refpack_encode_literal(data)
    assert ap.refpack_decode(packed) == data
    assert refpack.decompress(packed) == data          # the repo's decoder agrees
    assert len(packed) <= n + n // 112 + 12


def test_refpack_decode_is_strict():
    packed = ap.refpack_encode_literal(b"abcdefgh")
    with pytest.raises(ap.PatchError):
        ap.refpack_decode(packed[:-1] + b"")[:0] if False else ap.refpack_decode(b"\x10\xfb\x00\x00\x09" + packed[5:])
    with pytest.raises(ap.PatchError):
        ap.refpack_decode(b"BIGF\x00\x00\x00\x00")


def test_literal_header_is_the_short_form_for_small_data():
    packed = ap.refpack_encode_literal(b"x" * 10)
    assert packed[:2] == b"\x10\xfb" and packed[2:5] == b"\x00\x00\x0a"


# ------------------------------------------------------------------ BIG files

def make_big(magic, entries, align=4, hdr_extra=0):
    """A tiny BIG archive: header, directory, 4- or 128-aligned entry data."""
    names = [n for n, _ in entries]
    dir_len = 16 + sum(8 + len(n) + 1 for n in names)
    pos = (dir_len + hdr_extra + align - 1) // align * align
    out = bytearray(pos)
    out[:4] = magic
    struct.pack_into(">II", out, 8, len(entries), dir_len)
    p = 16
    for name, data in entries:
        pos = (pos + align - 1) // align * align
        struct.pack_into(">II", out, p, pos, len(data))
        out[p + 8:p + 8 + len(name) + 1] = name.encode() + b"\0"
        p += 8 + len(name) + 1
        if len(out) < pos:
            out += bytes(pos - len(out))
        out[pos:pos + len(data)] = data
        pos += len(data)
    out += bytes((-len(out)) % align)
    struct.pack_into("<I", out, 4, len(out))
    return bytes(out)


def test_big_records_and_rebuild_bigf():
    entries = [("a.apt", b"A" * 100), ("a.const", b"C" * 33), ("a.o", b"O" * 7)]
    big = make_big(b"BIGF", entries, align=128)
    recs = ap.big_records(big)
    assert [r[0] for r in recs] == ["a.apt", "a.const", "a.o"]
    assert all(r[2] % 128 == 0 for r in recs)
    new = ap.rebuild_bigf(big, {"a.apt": b"B" * 500})
    files = ap.inner_files(new)
    assert files == {"a.apt": b"B" * 500, "a.const": b"C" * 33, "a.o": b"O" * 7}
    assert struct.unpack_from("<I", new, 4)[0] == len(new) and len(new) % 128 == 0
    assert all(r[2] % 128 == 0 for r in ap.big_records(new))
    # The names and the directory size field are untouched.
    assert new[8:16] == big[8:16]


def test_plan_rewrites_one_directory_record_of_a_fake_archive(tmp_path):
    filler = bytes(range(256)) * 20
    inner_old = ap.refpack_encode_literal(b"old payload")
    entries = [("screens/x.big", filler), ("screens/target.big", inner_old), ("screens/z.big", filler[:77])]
    big = make_big(b"BIG4", entries, hdr_extra=8)
    src = tmp_path / "screens.viv"
    src.write_bytes(big + b"\x01\x02\x03")           # odd size on purpose: appended entry is re-aligned
    struct.pack_into("<I", (b := bytearray(src.read_bytes())), 4, len(b))
    src.write_bytes(bytes(b))
    new_raw = ap.refpack_encode_literal(b"patched payload, longer than the old one")

    plan = ap.Plan(str(src), entry_name="screens/target.big", patcher=lambda raw, extras: (new_raw, ["ok"]))
    assert plan.append_at % 4 == 0 and plan.append_at >= plan.size
    dst = tmp_path / "override" / "screens" / "screens.viv"
    ap.write_override(plan, str(dst))
    assert not (tmp_path / "override" / "screens" / "screens.viv.tmp").exists()
    assert ap.compare_override(plan, str(dst)) == []

    out = dst.read_bytes()
    assert struct.unpack_from("<I", out, 4)[0] == len(out)
    recs = {r[0]: r for r in ap.big_records(out)}
    assert out[recs["screens/target.big"][2]:][:recs["screens/target.big"][3]] == new_raw
    for name in ("screens/x.big", "screens/z.big"):
        old = {r[0]: r for r in ap.big_records(big)}[name]
        assert out[recs[name][2]:recs[name][2] + recs[name][3]] == big[old[2]:old[2] + old[3]]
    # Everything but the header total size and the one record is byte-identical.
    changed = [i for i in range(len(big)) if big[i] != out[i]]
    rec_pos = recs["screens/target.big"][1]
    assert all(i < 8 or rec_pos <= i < rec_pos + 8 for i in changed)
    assert src.read_bytes()[:len(big)] != out[:len(big)]   # the source was not touched below
    # A tampered override is reported as stale.
    bad = bytearray(out)
    bad[-1] ^= 0xFF
    dst.write_bytes(bytes(bad))
    assert ap.compare_override(plan, str(dst))


def test_patch_entry_refuses_other_versions():
    with pytest.raises(ap.PatchError, match="refusing"):
        ap.patch_entry(ap.refpack_encode_literal(make_big(b"BIGF", [("mainMenu.apt", b"x" * 40)], 128)))


# ------------------------------------------------------------- bytecode assembly

SLOTS = {"oMenu": 0x22, "getIndex": 0x23, "_level0": 0x14, "_root": 0x15, "FuncCall": 0x16,
         "aMenuArray": 0x27, "PlaySound": 0x25}


def le(v):
    return struct.pack("<I", v)


def test_keyx_insert_known_bytes():
    # 0x13041 is odd on purpose: the VM aligns operands on the absolute address.
    code = ap.asm_keyx_insert(0x13041, SLOTS, 0x12345)
    expected = (bytes([0x59, 0xAE, 0x22, 0xA2, 0x23, 0x52,       # oMenu.getIndex()
                       0xB5, 0x06, 0x49, 0x12,                    # == 6, Not
                       0x9D]) + le(15)                            # if (!eq) skip 15 bytes
                + bytes([0xA1, 0, 0, 0]) + le(0x12345)            # "Online.Open()"
                + bytes([0x5A, 0xAE, 0x14, 0xAF, 0x15, 0xB2, 0x16])   # _level0._root.FuncCall(...)
                + bytes([0x59, 0x17]))                            # filler: whole insertion = 32 bytes
    assert code == expected and len(code) % 4 == 0


def test_select_insert_known_bytes():
    code = ap.asm_select_insert(0x13000, SLOTS, 0x2000, 0x3000)
    expected = (bytes([0xA1, 0, 0, 0]) + le(0x2000)               # "Online.Focus("
                + bytes([0x59, 0xAE, 0x22, 0xA2, 0x23, 0x52])     # oMenu.getIndex()
                + bytes([0x47])                                    # +
                + bytes([0xA1]) + le(0x3000)                      # ")" (operand already aligned)
                + bytes([0x47, 0x5A, 0xAE, 0x14, 0xAF, 0x15, 0xB2, 0x16]))
    assert code == expected and len(code) == 28


def test_tick_action_known_bytes():
    code = ap.asm_tick_action(0x13000, 0x5000, 0x6000)
    expected = (bytes([0x88, 0, 0, 0]) + le(3) + le(0x5000)           # ConstantPool: 3 entries at 0x5000
                + bytes([0xA1, 0, 0, 0]) + le(0x6000)                  # "Online.Tick()"
                + bytes([0x5A, 0xAE, 0x00, 0xAF, 0x01, 0xB2, 0x02, 0x00]))   # _level0._root.FuncCall(...); End
    assert code == expected
    ins = ap.decode(bytes(0x13000) + code, 0x13000)
    assert [i.op for i in ins] == [0x88, 0xA1, 0x5A, 0xAE, 0xAF, 0xB2, 0x00]


def test_tail_known_bytes_and_filler_parities():
    code = ap.asm_tail(0x13000, SLOTS, 0x4000)
    assert code == (bytes([0xAE, 0x27, 0xB5, 0x06, 0xA1, 0, 0, 0]) + le(0x4000) + bytes([0x4F]))
    for start in range(4):                                         # every alignment gives a multiple of 4
        for extra in range(4):
            a = ap.Asm(0x100 + start)
            a.op(*([0x59] * extra))
            a.pad_to_multiple_of_4()
            assert len(a.buf) % 4 == 0


# ---------------------------------------------- body editing on a synthetic function

def synthetic_block():
    """buf with a top-level DefineFunction at 0x100: guard If over two filler pairs, then End."""
    base = 0x100
    body_at = base + 4 + 16 + 8
    a = ap.Asm(body_at)
    a.op(0x59, 0x59, 0x49, 0x12)
    at = a.op_u32(ap.OP_IF, 0)
    after = len(a.buf)
    a.op(0x59, 0x17, 0x59, 0x17)
    a.patch_u32(at, len(a.buf) - after)                           # the If skips both pairs
    a.op(0x59, 0x17)
    body = bytes(a.buf)
    buf = bytearray(base)
    buf.append(ap.OP_DEFFUNC)
    buf += bytes(3)
    buf += struct.pack("<IIII", 0, 0, 0, len(body)) + ap.FUNC_MARKER + body
    buf.append(ap.OP_END)
    return bytes(buf), base, body_at, len(body), body_at + at     # absolute address of the If operand


def test_decode_and_branch_check_of_a_synthetic_function():
    buf, base, body_at, size, _ = synthetic_block()
    ins, nfun = ap.verify_block(buf, base)
    assert nfun == 1 and ins[0].body == (body_at, size) and ins[-1].op == ap.OP_END
    sub = ap.decode(buf, body_at, body_at + size)
    guard = next(i for i in sub if i.op == ap.OP_IF)
    assert guard.target == body_at + size - 2                    # skips to the last pair


def test_inserting_into_a_body_needs_the_size_and_branch_fixups():
    buf, base, body_at, size, if_at = synthetic_block()
    sub = ap.decode(buf, body_at, body_at + size)
    guard = next(i for i in sub if i.op == ap.OP_IF)
    point = guard.nxt                                             # right after the guard
    assert [i.addr for i in ap.branches_crossing(sub, point)] == [guard.addr]
    insert = bytes([0x59, 0x17, 0x59, 0x17])
    size_field = base + 4 + 12
    operand = ap.align(guard.addr + 1, 4)
    block = buf[base:]
    good = ap.apply_edits(block, base, [(size_field, len(insert)), (operand, len(insert))], [(point, insert)])
    new = bytes(buf[:base]) + good
    ins, nfun = ap.verify_block(new, base)
    assert nfun == 1
    nsub = ap.decode(new, body_at, body_at + size + 4)
    nguard = next(i for i in nsub if i.op == ap.OP_IF)
    assert nguard.target == body_at + size + 4 - 2               # still skips exactly the original pairs
    # Without the fixups the jump no longer skips what it used to skip.
    bad = bytes(buf[:base]) + ap.apply_edits(block, base, [], [(point, insert)])
    bsub = ap.decode(bad, body_at, body_at + size)
    bguard = next(i for i in bsub if i.op == ap.OP_IF)
    assert bguard.target != nguard.target


def test_branch_into_the_middle_of_an_instruction_is_rejected():
    buf, base, body_at, size, if_at = synthetic_block()
    b = bytearray(buf)
    struct.pack_into("<I", b, if_at, 0xFFFFFFFE)                  # target = 2 bytes before the operand's end
    with pytest.raises(ap.PatchError):
        ap.verify_block(bytes(b), base)


# ----------------------------------------------------------------- DXT5 and the poster

def make_shpx(images):
    """images: [(name, w, h, bytes of w*h DXT5)] -> a minimal SHPX bank, entries 256-aligned like the game's."""
    n = len(images)
    dir_end = 16 + 8 * n
    out = bytearray(dir_end)
    pos = (dir_end + 0xF0 + 0xFF) // 0x100 * 0x100 - 0x10      # header at ...F0 so data is 256-aligned
    for i, (name, w, h, data) in enumerate(images):
        pos = (pos + 0xFF) // 0x100 * 0x100 + 0xF0 if pos % 0x100 != 0xF0 else pos
        out[16 + 8 * i:20 + 8 * i] = name.ljust(4).encode()
        struct.pack_into("<I", out, 20 + 8 * i, pos)
        if len(out) < pos:
            out += bytes(pos - len(out))
        out += bytes([0x62, 0x20]) + struct.pack("<H", w * h // 256) + struct.pack("<HH", w, h) + bytes(8) + data
        pos = len(out)
    out[:4] = b"SHPX"
    struct.pack_into("<II", out, 4, len(out), n)
    out[12:16] = b"G355"
    return bytes(out)


def solid_block(rgb, a=255):
    return ap.dxt5_encode_block([rgb + (a,)] * 16)


def test_dxt5_blocks_round_trip():
    for rgb in ((0, 0, 0), (255, 255, 255), (123, 200, 40)):
        got = ap.dxt5_decode_block(solid_block(rgb))
        assert all(max(abs(g - e) for g, e in zip(p[:3], rgb)) <= 8 and p[3] == 255 for p in got)
    bw = [(255, 255, 255, 255) if (i // 4 + i) % 2 else (0, 0, 0, 255) for i in range(16)]
    assert [p[:3] for p in ap.dxt5_decode_block(ap.dxt5_encode_block(bw))] == [p[:3] for p in bw]
    ramp = [(i * 16, i * 16, i * 16, 255) for i in range(16)]
    blk = ap.dxt5_encode_block(ramp)
    assert max(abs(p[0] - q[0]) for p, q in zip(ap.dxt5_decode_block(blk), ramp)) <= 44     # four colours for a 16-step ramp
    soft = [(10, 20, 30, i * 16) for i in range(16)]
    assert max(abs(p[3] - q[3]) for p, q in zip(ap.dxt5_decode_block(ap.dxt5_encode_block(soft)), soft)) <= 18
    assert blk[8] * 256 + blk[9] >= 0 and struct.unpack_from("<H", blk, 8)[0] >= struct.unpack_from("<H", blk, 10)[0]


def test_resample_keeps_edges_clean_and_size():
    rows = [[(200.0, 200.0, 200.0, 1.0) if x > 1 else (0.0, 255.0, 0.0, 0.0) for x in range(8)] for y in range(8)]
    out = ap.resample_rgba(rows, (4, 6))
    assert len(out) == 6 and len(out[0]) == 4
    for row in out:
        for p in row:
            assert p[3] == 0.0 or abs(p[1] - 200.0) < 1e-6        # no green bleeds from the transparent side


def synthetic_banks():
    rnd = random.Random(7)
    wall = bytes(rnd.randrange(256) for _ in range(ap.WALL_W * ap.WALL_H))
    # a smooth wall instead of noise so the encode error stays small: use solid blocks of two levels
    blocks = b"".join(solid_block((40 + (i % 7) * 3,) * 3) for i in range(ap.WALL_W // 4 * ap.WALL_H // 4))
    lit = b"".join(solid_block((120 + (i % 5) * 4,) * 3) for i in range(ap.WALL_W // 4 * ap.WALL_H // 4))
    xsh = make_shpx([("14", ap.WALL_W, ap.WALL_H, lit), ("5", ap.WALL_W, ap.WALL_H, blocks)])
    art = []
    for by in range(32):
        for bx in range(32):
            px = [((255, 255, 255, 255) if (bx * 4 + i % 4 + by * 4 + i // 4) % 9 < 4 else (0, 0, 0, 255)) for i in range(16)]
            art.append(ap.dxt5_encode_block(px))
    poster = make_shpx([("15", 128, 128, b"".join(art))])
    return xsh, poster


def test_paint_poster_changes_only_the_poster_blocks():
    xsh, poster = synthetic_banks()
    new, line, intended = ap.paint_poster(xsh, poster, check_hash=False)
    assert len(new) == len(xsh) and ap.shpx_directory(new) == ap.shpx_directory(xsh)
    assert "painted" in line
    assert "poster blocks" in ap.verify_paint(xsh, new, intended)
    x0, y0, w, h = ap.POSTER_RECT
    base, tw, _ = ap.shpx_image(new, ap.WALL_LIT_TEX)
    rows = ap.dxt5_region(new, base, tw, ap.POSTER_RECT)
    lum = [p[0] for r in rows for p in r]
    assert max(lum) > 100 and min(lum) < 40                     # the black-and-white art is in there
    assert new != xsh
    # A change outside the rectangle is caught.
    bad = bytearray(new)
    base5, _, _ = ap.shpx_image(bad, ap.WALL_DIM_TEX)
    bad[base5] ^= 0xFF
    with pytest.raises(ap.PatchError, match="outside"):
        ap.verify_paint(xsh, bytes(bad), intended)
    # A poster that does not match the expected hash is refused by default.
    with pytest.raises(ap.PatchError, match="expected hash"):
        ap.paint_poster(xsh, poster)


# --------------------------------------------------------------- battle/cmtype tick clip

def synthetic_cmtype(monkeypatch):
    """A tiny stand-in for cmtype.apt/.const: a movie with 67 root frames, an empty one-frame sprite
    (character 295) and a frame-0 placement of it named "rules"."""
    names = ["_level0", "_root", "FuncCall", "rules"]
    strings = bytearray()
    entries = []
    base = 0x20 + 8 * len(names)
    for n in names:
        entries.append((1, base + len(strings)))
        strings += n.encode() + b"\0"
    const = bytearray(b"Apt constant file\0\0")
    const += struct.pack("<III", 0x1000, len(names), 0x20)
    for t_, v in entries:
        const += struct.pack("<II", t_, v)
    const += strings
    apt = bytearray(0x3000)
    movie, frames, chars, nchars = 0x1000, 0x1100, 0x1400, 296
    struct.pack_into("<I", const, 0x14, movie)
    struct.pack_into("<2I", apt, movie, 9, 0x09876543)
    struct.pack_into("<I", apt, movie + 8, 67)
    struct.pack_into("<I", apt, movie + 12, frames)
    struct.pack_into("<II", apt, movie + 20, nchars, chars)
    sprite, sprite_frames = 0x1900, 0x1950
    struct.pack_into("<5I", apt, sprite, 5, 0x09876543, 1, sprite_frames, 0)       # one frame ...
    struct.pack_into("<II", apt, sprite_frames, 0, 0)                               # ... with no items
    struct.pack_into("<I", apt, chars + 4 * 295, sprite)
    rules_name = 0x1A00
    apt[rules_name:rules_name + 6] = b"rules\0"
    place, arr = 0x1A40, 0x1A80
    struct.pack_into("<16I", apt, place, 3, 0x26, 111, 295, 0x3F800000, 0, 0, 0x3F800000, 0, 0,
                     0xFFFFFFFF, 0, 0, rules_name, 0xFFFFFFFF, 0)
    struct.pack_into("<I", apt, arr, place)
    struct.pack_into("<II", apt, frames, 1, arr)
    monkeypatch.setattr(ap, "CMTYPE_APT_LEN", len(apt))
    return bytes(apt), bytes(const)


def test_cmtype_tick_clip_on_a_synthetic_movie(monkeypatch):
    apt, const = synthetic_cmtype(monkeypatch)
    new, info = ap.build_cmtype_apt(apt, const)
    assert "tick clip appended" in ap.verify_cmtype_apt(apt, new, info)
    assert struct.unpack_from("<II", new, info["frames"]) == (2, info["array_at"])
    ptrs = struct.unpack_from("<2I", new, info["array_at"])
    assert ptrs[1] == info["place_at"]
    words = struct.unpack_from("<16I", new, info["place_at"])
    assert words[1] == 0x26 | 0x80 and words[2] == ap.CMTYPE_TICK_DEPTH and words[3] == 295
    assert ap.cstr(new, words[13]) == ap.CMTYPE_TICK_NAME
    # Only the frame-0 record changed in the original bytes.
    diff = [i for i in range(len(apt)) if apt[i] != new[i]]
    assert diff and all(info["frames"] <= i < info["frames"] + 8 for i in diff)
    # A depth already in use, or a non-empty template sprite, is refused.
    bad = bytearray(apt)
    struct.pack_into("<I", bad, 0x1A40 + 8, ap.CMTYPE_TICK_DEPTH)
    with pytest.raises(ap.PatchError, match="depth"):
        ap.build_cmtype_apt(bytes(bad), const)
    bad = bytearray(apt)
    struct.pack_into("<I", bad, 0x1950, 1)
    with pytest.raises(ap.PatchError, match="empty"):
        ap.build_cmtype_apt(bytes(bad), const)


def test_cmtype_entry_is_refused_when_it_differs():
    other = make_big(b"BIGF", [("cmtype.apt", b"x" * 40)], 128)
    with pytest.raises(ap.PatchError, match="refusing"):
        ap.patch_cmtype_entry(ap.refpack_encode_literal(other))


def test_plan_patches_two_entries_of_a_fake_archive(tmp_path):
    filler = bytes(range(256)) * 8
    a_new = ap.refpack_encode_literal(b"A" * 333)
    b_new = ap.refpack_encode_literal(b"B" * 77)
    entries = [("screens/a.big", ap.refpack_encode_literal(b"a")), ("screens/x.big", filler),
               ("screens/b.big", ap.refpack_encode_literal(b"b"))]
    big = make_big(b"BIG4", entries)
    src = tmp_path / "screens.viv"
    src.write_bytes(big)
    plan = ap.Plan(str(src), entry_name="screens/a.big", patcher=lambda raw, extras: (a_new, ["a"]),
                   more=[("screens/b.big", lambda raw: (b_new, ["b"]))])
    assert [len(b) for _, b in plan.blobs] == [len(a_new), len(b_new)]
    assert all(at % 4 == 0 for at, _ in plan.blobs) and plan.blobs[1][0] >= plan.blobs[0][0] + len(a_new)
    dst = tmp_path / "o" / "screens.viv"
    ap.write_override(plan, str(dst))
    assert ap.compare_override(plan, str(dst)) == []
    out = dst.read_bytes()
    recs = {r[0]: r for r in ap.big_records(out)}
    assert out[recs["screens/a.big"][2]:][:recs["screens/a.big"][3]] == a_new
    assert out[recs["screens/b.big"][2]:][:recs["screens/b.big"][3]] == b_new
    assert struct.unpack_from("<I", out, 4)[0] == len(out)
    bad = bytearray(out)
    bad[-2] ^= 1
    dst.write_bytes(bytes(bad))
    assert ap.compare_override(plan, str(dst))


# ------------------------------------------------------------ the user's own dump

def data_screens():
    root = os.environ.get("DEFJAM_DATA", "")
    path = Path(root) / "extracted" / "screens" / "screens.viv" if root else None
    return path if path and path.is_file() else None


@pytest.mark.skipif(data_screens() is None, reason="no game data (set DEFJAM_DATA)")
def test_real_archive_plan_verifies_and_leaves_the_source_alone(tmp_path):
    src = data_screens()
    before = (src.stat().st_size, src.stat().st_mtime_ns)
    try:
        plan = ap.Plan(str(src))
    except ap.PatchError as e:                                     # another region or version
        pytest.skip("not the reference dump: %s" % e)
    assert any(l.startswith("verify .apt: 25 function bodies") for l in plan.lines)
    assert any(l.startswith("verify cmtype.apt: tick clip appended") for l in plan.lines)
    assert len(plan.blobs) == 2
    assert plan.final_size == plan.blobs[-1][0] + len(plan.blobs[-1][1])
    assert (src.stat().st_size, src.stat().st_mtime_ns) == before

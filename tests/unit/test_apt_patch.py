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

    plan = ap.Plan(str(src), entry_name="screens/target.big", patcher=lambda raw: (new_raw, ["ok"]))
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
    assert plan.final_size == plan.append_at + len(plan.new_raw)
    assert (src.stat().st_size, src.stat().st_mtime_ns) == before

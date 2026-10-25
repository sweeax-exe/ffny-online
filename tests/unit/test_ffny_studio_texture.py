"""FFNY Studio's Pillow-free SHPX decoder against scripts/hd_assets.py's Pillow path, on synthetic textures."""
import importlib.util
import random
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).parents[2]
sys.path.insert(0, str(ROOT / "tools" / "ffny-studio"))
from ffnystudio import texture  # noqa: E402

spec = importlib.util.spec_from_file_location("hd_assets_ref", ROOT / "scripts" / "hd_assets.py")
hd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hd)


def record(kind, w, h, payload, flags=0, attach=0):
    head = bytearray(16)
    head[0] = kind
    head[1:4] = attach.to_bytes(3, "little")
    struct.pack_into("<HH", head, 4, w, h)
    struct.pack_into("<I", head, 12, flags)
    return bytes(head) + payload


def palette_record(colours):
    head = bytearray(16)
    head[0] = texture.TYPE_PALETTE
    struct.pack_into("<H", head, 4, len(colours) // 4)
    return bytes(head) + colours


def shpx(images):
    """images: [(name, [record bytes, ...])]; the records of one image are laid out back to back."""
    count = len(images)
    out = bytearray(b"SHPX" + bytes(8) + b"G354" + bytes(8 * count))
    for i, (name, parts) in enumerate(images):
        while len(out) % 16:
            out.append(0)
        struct.pack_into("<4sI", out, 16 + 8 * i, name.encode(), len(out))
        for part in parts:
            out += part
    struct.pack_into("<II", out, 4, len(out), count)
    return bytes(out)


def paletted(name, w, h, rng, swizzle, colours=16):
    indices = bytes(rng.randrange(colours) for _ in range(w * h))
    pal = bytes(rng.randrange(256) for _ in range(colours * 4))
    first = record(texture.TYPE_P8, w, h, indices, texture.FLAG_SWIZZLED if swizzle else 0)
    pad = (-len(first)) % 16
    first = record(texture.TYPE_P8, w, h, indices + bytes(pad), texture.FLAG_SWIZZLED if swizzle else 0,
                   attach=len(first) + pad)
    return name, [first, palette_record(pal)]


def dxt_blocks(kind, count, rng):
    out = bytearray()
    for i in range(count):
        if kind == texture.TYPE_DXT1:
            c0, c1 = rng.randrange(65536), rng.randrange(65536)
            if i % 2:                       # alternate the 3-colour + transparent mode
                c0, c1 = min(c0, c1), max(c0, c1)
            out += struct.pack("<HHI", c0, c1, rng.getrandbits(32))
        else:
            if kind == texture.TYPE_DXT5:
                a0, a1 = rng.randrange(256), rng.randrange(256)
                if i % 2:
                    a0, a1 = min(a0, a1), max(a0, a1)
                alpha = bytes((a0, a1)) + rng.getrandbits(48).to_bytes(6, "little")
            else:
                alpha = rng.getrandbits(64).to_bytes(8, "little")
            c0, c1 = rng.randrange(65536), rng.randrange(65536)
            out += alpha + struct.pack("<HHI", min(c0, c1), max(c0, c1), rng.getrandbits(32))
    return bytes(out)


def synthetic():
    rng = random.Random(1234)
    images = [paletted("pal1", 8, 4, rng, swizzle=True), paletted("pal2", 4, 16, rng, swizzle=False),
              paletted("pal3", 16, 16, rng, swizzle=True, colours=256)]
    bgra = bytes(rng.randrange(256) for _ in range(8 * 8 * 4))
    images.append(("bgr1", [record(texture.TYPE_BGRA, 8, 8, bgra, texture.FLAG_SWIZZLED)]))
    images.append(("bgr2", [record(texture.TYPE_BGRA, 2, 4, bgra[:32])]))
    for name, kind, w, h in [("dxt1", texture.TYPE_DXT1, 8, 8), ("dxt3", texture.TYPE_DXT3, 8, 4),
                             ("dxt5", texture.TYPE_DXT5, 8, 8), ("dxtc", texture.TYPE_DXT1, 6, 5)]:
        blocks = ((w + 3) // 4) * ((h + 3) // 4)
        images.append((name, [record(kind, w, h, dxt_blocks(kind, blocks, rng))]))
    return shpx(images)


def test_matches_the_pillow_decoder():
    pytest.importorskip("PIL.Image")
    blob = synthetic()
    ours = {name: (image, error) for name, image, error in texture.shpx_images(blob)}
    seen = 0
    for rec, image in hd.shpx_images(blob):
        assert "error" not in rec, rec
        mine, error = ours[rec["name"]]
        assert not error and (mine.width, mine.height) == image.size
        assert mine.rgba == image.tobytes(), rec["name"]
        seen += 1
    assert seen == len(ours) == 9


def test_known_values_without_pillow():
    blob = shpx([
        ("pal8", [record(texture.TYPE_P8, 2, 1, bytes((1, 0)) + bytes(14), attach=32),
                palette_record(bytes((10, 20, 30, 64, 1, 2, 3, 255)))]),
        ("argb", [record(texture.TYPE_BGRA, 1, 1, bytes((10, 20, 30, 40)))]),
        ("dxt1", [record(texture.TYPE_DXT1, 4, 4, struct.pack("<HHI", 0x001F, 0xF800, 0x00000003))]),
    ])
    images = texture.decode_all(blob)
    assert images["pal8"].pixel(0, 0) == (3, 2, 1, 255) and images["pal8"].pixel(1, 0) == (30, 20, 10, 64)
    assert images["argb"].pixel(0, 0) == (30, 20, 10, 40)
    dxt = images["dxt1"]
    # c0 (blue) > c1 (red) as integers is false: 0x001F < 0xF800, so 3-colour mode; index 3 is transparent.
    assert dxt.pixel(0, 0) == (0, 0, 0, 0) and dxt.pixel(1, 0) == (0, 0, 255, 255)
    assert dxt.has_alpha and images["argb"].has_alpha


def test_errors_are_reported_per_image():
    blob = shpx([("bad1", [record(0x65, 4, 4, bytes(16))]),
                 ("bad2", [record(texture.TYPE_P8, 2, 2, bytes(4))]),
                 ("good", [record(texture.TYPE_BGRA, 1, 1, bytes(4))])])
    results = {name: (image, error) for name, image, error in texture.shpx_images(blob)}
    assert "Unsupported SHPX type 0x65" in results["bad1"][1]
    assert "palette" in results["bad2"][1].lower()
    assert results["good"][0] is not None
    assert list(texture.decode_all(blob)) == ["good"]
    with pytest.raises(ValueError):
        texture.directory(b"SHPX" + bytes(12))


def test_morton_tables_match_the_reference():
    for w, h in [(1, 1), (2, 8), (8, 2), (16, 4), (32, 32)]:
        xs, ys = texture.morton_tables(w, h)
        for y in range(h):
            for x in range(w):
                assert xs[x] | ys[y] == hd.morton_offset(x, y, w, h)


def test_blender_pixels_are_bottom_up_floats():
    image = texture.Image("t", 1, 2, bytes((255, 0, 0, 255, 0, 0, 255, 0)))
    pixels = texture.blender_pixels(image)
    assert list(pixels) == [0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0]

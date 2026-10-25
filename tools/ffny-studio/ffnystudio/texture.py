"""SHPX (``.xsh``) textures decoded without Pillow, for Blender's Python (which has no Pillow).

The same reading as ``scripts/hd_assets.py`` ``shpx_images`` (the reference, which needs Pillow): the
directory, the 8-bit paletted (0x7b) and 32-bit BGRA (0x7d) images, both optionally Morton-swizzled (flag
0x2000), and DXT1/3/5 (0x60/0x61/0x62). The DXT decoder follows Pillow's BCn decoder bit for bit (integer
interpolation, DXT1 transparent black when ``c0 <= c1``), and tests/unit/test_ffny_studio_texture.py checks
every path against the Pillow one on synthetic images.

Output is :class:`Image`: tightly packed 8-bit RGBA rows, top row first. ``blender_pixels`` turns that into
the bottom-up float list ``bpy.types.Image.pixels.foreach_set`` takes. Standard library only.
"""

import struct
from array import array
from dataclasses import dataclass

MAX_DIM = 4096
TYPE_PALETTE = 0x2A
TYPE_P8, TYPE_BGRA = 0x7B, 0x7D
TYPE_DXT1, TYPE_DXT3, TYPE_DXT5 = 0x60, 0x61, 0x62
FLAG_SWIZZLED, FLAG_INDIRECT = 0x2000, 0x1000

_TO_FLOAT = [i / 255.0 for i in range(256)]


@dataclass
class Image:
    name: str
    width: int
    height: int
    rgba: bytes          # width * height * 4 bytes, rows top to bottom
    kind: int = 0

    def pixel(self, x, y):
        o = (y * self.width + x) * 4
        return tuple(self.rgba[o:o + 4])

    @property
    def has_alpha(self):
        return any(a != 255 for a in self.rgba[3::4])


# -- swizzle --------------------------------------------------------------------------------------------------

def morton_tables(w, h):
    """Per-column and per-row parts of the NV2A Morton offset; offset(x, y) = xs[x] | ys[y].

    Bits of x and y interleave (x first) while both dimensions still have bits; then the larger one's bits
    continue alone, as in ``hd_assets.morton_offset``."""
    xs, ys = [0] * w, [0] * h
    target, bit = 0, 0
    while (1 << bit) < w or (1 << bit) < h:
        if (1 << bit) < w:
            for x in range(w):
                xs[x] |= ((x >> bit) & 1) << target
            target += 1
        if (1 << bit) < h:
            for y in range(h):
                ys[y] |= ((y >> bit) & 1) << target
            target += 1
        bit += 1
    return xs, ys


def unswizzle(data, w, h, bpp):
    xs, ys = morton_tables(w, h)
    if (max(xs, default=0) | max(ys, default=0)) * bpp + bpp > len(data):
        raise ValueError("Unsupported nonrectangular swizzle layout")
    out = bytearray(w * h * bpp)
    if bpp == 1:
        for y in range(h):
            row = ys[y]
            out[y * w:(y + 1) * w] = bytes(data[row | xs[x]] for x in range(w))
        return bytes(out)
    for y in range(h):
        row, base = ys[y], y * w * bpp
        for x in range(w):
            s = (row | xs[x]) * bpp
            t = base + x * bpp
            out[t:t + bpp] = data[s:s + bpp]
    return bytes(out)


def bgra_to_rgba(data):
    out = bytearray(data)
    out[0::4], out[2::4] = data[2::4], data[0::4]
    return bytes(out)


# -- DXT (BC1, BC2, BC3) ---------------------------------------------------------------------------------------

def _rgb565(c):
    r = (c & 0xF800) >> 8
    g = (c & 0x07E0) >> 3
    b = (c & 0x001F) << 3
    return (r | r >> 5, g | g >> 6, b | b >> 5)


def _color_block(block, offset, four_colour):
    """16 RGBA tuples of a BC1 colour block. ``four_colour`` forces the c0 > c1 mode (BC2/BC3 colour)."""
    c0, c1, bits = struct.unpack_from("<HHI", block, offset)
    p0, p1 = _rgb565(c0), _rgb565(c1)
    if c0 > c1 or four_colour:
        p2 = tuple((2 * a + b) // 3 for a, b in zip(p0, p1)) + (255,)
        p3 = tuple((a + 2 * b) // 3 for a, b in zip(p0, p1)) + (255,)
    else:
        p2 = tuple((a + b) // 2 for a, b in zip(p0, p1)) + (255,)
        p3 = (0, 0, 0, 0)
    palette = (p0 + (255,), p1 + (255,), p2, p3)
    return [palette[(bits >> (2 * i)) & 3] for i in range(16)]


def _bc3_alpha(block, offset):
    a0, a1 = block[offset], block[offset + 1]
    if a0 > a1:
        lut = [a0, a1] + [((7 - k) * a0 + k * a1) // 7 for k in range(1, 7)]
    else:
        lut = [a0, a1] + [((5 - k) * a0 + k * a1) // 5 for k in range(1, 5)] + [0, 255]
    bits = int.from_bytes(block[offset + 2:offset + 8], "little")
    return [lut[(bits >> (3 * i)) & 7] for i in range(16)]


def _bc2_alpha(block, offset):
    bits = int.from_bytes(block[offset:offset + 8], "little")
    return [((bits >> (4 * i)) & 0xF) * 17 for i in range(16)]


def decode_dxt(data, w, h, kind):
    """DXT1/3/5 blocks -> RGBA bytes (rows top to bottom), cropped to w x h."""
    size = 8 if kind == TYPE_DXT1 else 16
    bw, bh = (w + 3) // 4, (h + 3) // 4
    if len(data) < bw * bh * size:
        raise ValueError("Truncated pixels")
    out = bytearray(w * h * 4)
    for by in range(bh):
        for bx in range(bw):
            o = (by * bw + bx) * size
            if kind == TYPE_DXT1:
                texels = _color_block(data, o, False)
            else:
                colours = _color_block(data, o + 8, True)
                alpha = _bc2_alpha(data, o) if kind == TYPE_DXT3 else _bc3_alpha(data, o)
                texels = [c[:3] + (a,) for c, a in zip(colours, alpha)]
            for i, texel in enumerate(texels):
                x, y = bx * 4 + (i & 3), by * 4 + (i >> 2)
                if x < w and y < h:
                    t = (y * w + x) * 4
                    out[t:t + 4] = bytes(texel)
    return bytes(out)


# -- the SHPX container ---------------------------------------------------------------------------------------

def _palette(blob, offset, size):
    """The BGRA palette attached to a paletted image through its attachment chain."""
    pos, seen = offset, set()
    while pos not in seen:
        seen.add(pos)
        delta = int.from_bytes(blob[pos + 1:pos + 4], "little")
        if not delta:
            break
        pos += delta
        if pos + 16 > size or len(seen) > 64:
            raise ValueError("Invalid attachment chain")
        if blob[pos] == TYPE_PALETTE:
            colors = struct.unpack_from("<H", blob, pos + 4)[0]
            if not 1 <= colors <= 256 or pos + 16 + colors * 4 > size:
                raise ValueError("Invalid palette")
            return blob[pos + 16:pos + 16 + colors * 4]
    raise ValueError("No BGRA palette")


def decode_record(blob, offset, name=""):
    """Decode the image whose header is at ``offset``; None for a palette record. Raises ValueError."""
    size = len(blob)
    if offset + 16 > size:
        raise ValueError("Truncated image header")
    kind = blob[offset]
    if kind == TYPE_PALETTE:
        return None
    w, h = struct.unpack_from("<HH", blob, offset + 4)
    flags = struct.unpack_from("<I", blob, offset + 12)[0]
    if not w or not h or max(w, h) > MAX_DIM:
        raise ValueError("Invalid image dimensions")
    if flags >> 28:
        raise ValueError("Mip-bearing SHPX needs validated layout")
    data_offset = offset + (struct.unpack_from("<I", blob, offset + 16)[0] if flags & FLAG_INDIRECT else 16)
    if data_offset < offset + 16:
        raise ValueError("Invalid indirect image pointer")
    if kind in (TYPE_DXT1, TYPE_DXT3, TYPE_DXT5):
        n = ((w + 3) // 4) * ((h + 3) // 4) * (8 if kind == TYPE_DXT1 else 16)
    elif kind in (TYPE_P8, TYPE_BGRA):
        n = w * h * (1 if kind == TYPE_P8 else 4)
    else:
        raise ValueError("Unsupported SHPX type 0x%02x" % kind)
    if data_offset + n > size:
        raise ValueError("Truncated pixels")
    pixels = blob[data_offset:data_offset + n]
    swizzled = bool(flags & FLAG_SWIZZLED)
    if kind == TYPE_P8:
        palette = _palette(blob, offset, size)
        linear = unswizzle(pixels, w, h, 1) if swizzled else pixels
        colours = len(palette) // 4
        if max(linear) >= colours:
            raise ValueError("Palette index out of bounds")
        lut = [bytes((palette[i * 4 + 2], palette[i * 4 + 1], palette[i * 4], palette[i * 4 + 3]))
               for i in range(colours)]
        rgba = b"".join(lut[i] for i in linear)
    elif kind == TYPE_BGRA:
        rgba = bgra_to_rgba(unswizzle(pixels, w, h, 4) if swizzled else pixels)
    else:
        rgba = decode_dxt(pixels, w, h, kind)
    return Image(name, w, h, rgba, kind)


def directory(blob):
    """[(name, offset)] of an SHPX file."""
    if len(blob) < 16 or blob[:4] != b"SHPX":
        raise ValueError("Invalid SHPX")
    size, count = struct.unpack_from("<II", blob, 4)
    if size != len(blob) or count > 65536 or 16 + count * 8 > size:
        raise ValueError("Invalid SHPX directory bounds")
    out = []
    for index in range(count):
        name, offset = struct.unpack_from("<4sI", blob, 16 + index * 8)
        out.append((name.decode("latin-1"), offset))
    return out


def shpx_images(blob, names=None):
    """Yield (name, Image or None, error) for every texture of an SHPX file; palette records are skipped.

    ``names`` limits decoding to those image names."""
    for name, offset in directory(blob):
        if names is not None and name not in names:
            continue
        try:
            image = decode_record(blob, offset, name)
        except (ValueError, IndexError, struct.error) as error:
            yield name, None, str(error)
            continue
        if image is not None:
            yield name, image, ""


def decode_all(blob):
    """{name: Image} of the images that decode."""
    return {name: image for name, image, _ in shpx_images(blob) if image is not None}


# -- Blender --------------------------------------------------------------------------------------------------

def flip_rows(rgba, w, h):
    stride = w * 4
    return b"".join(rgba[(h - 1 - y) * stride:(h - y) * stride] for y in range(h))


def blender_pixels(image):
    """The float array ``bpy.types.Image.pixels.foreach_set`` wants: RGBA 0..1, bottom row first."""
    return array("f", map(_TO_FLOAT.__getitem__, flip_rows(image.rgba, image.width, image.height)))

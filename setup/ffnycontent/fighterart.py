# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""A new fighter's own menu pictures: the pack's ``art/body.png``, ``art/head.png`` and ``art/name.png`` painted
into the copies of its template's picture families (``newfighter.PICTURE_FAMILIES``).

Each family file is a small BIG with ``.apt``, ``.const``, ``.o`` and ``.xsh``. The ``.xsh`` is an SHPX texture bank
(DXT5, type 0x62; the ``I_ML`` names DXT3, 0x61; no mips, not swizzled); the ``.o`` is an EAGL ELF object that draws
the textures as quads. Per quad the object's ``.data`` has a relocation against a
``__EAGL::TAR:::...SHAPENAME=<tex>,1;...`` symbol at offset r and, at r + 16, a local pointer to a 4x4 float texture
matrix whose translation (m[12], m[13]) is minus the texture's origin in movie units (scale 1): texel (i, j) of
``<tex>`` sits at movie (i - m[12], j - m[13]). What shows is the model's box (``__BBOX:::<model>``, six floats).
``layout`` reads that; the painting fits the pack's art to the box the way the template's own picture sits in it
(same box of opaque pixels, same tint and brightness), so a cool family stays cool and a sepia one sepia.

The texture records keep their place, size and format: only pixel bytes change. All arithmetic is integer, so
every PC composes the same bytes (the composed files are part of the online fight-file print).

One implementation for both sides: FFNY Studio (``ffnystudio/fighterart.py``) and the player's install
(``setup/ffnycontent/fighterart.py`` of the game repository) hold the same text below their first comment lines.
Standard library only.
"""

import re
import struct
import zlib

BODY, HEAD, NAME = "art/body.png", "art/head.png", "art/name.png"
ART_SIZES = {BODY: (256, 512), HEAD: (256, 256), NAME: (512, 64)}
ART_MAX_BYTES = 1 << 20
DXT3, DXT5 = 0x61, 0x62
HUB_SIZE = (94, 193)


class ArtError(ValueError):
    pass


# -- PNG (8-bit, not interlaced) ------------------------------------------------------------------------------------

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def _chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def png_bytes(rgba, w, h, level=9):
    """An 8-bit RGBA PNG (rows top to bottom, Sub filter)."""
    if not 0 < w <= 65535 or not 0 < h <= 65535 or len(rgba) != w * h * 4:
        raise ArtError("RGBA data does not match %dx%d" % (w, h))
    stride = w * 4
    raw = bytearray()
    for y in range(h):
        row = rgba[y * stride:(y + 1) * stride]
        raw.append(1)
        raw += row[:4]
        raw += bytes((row[i] - row[i - 4]) & 0xFF for i in range(4, stride))
    head = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    return (PNG_MAGIC + _chunk(b"IHDR", head) + _chunk(b"IDAT", zlib.compress(bytes(raw), level))
            + _chunk(b"IEND", b""))


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def read_png(data, max_pixels=1 << 22):
    """(width, height, RGBA bytes) of an 8-bit, non-interlaced grey, grey+alpha, RGB or RGBA PNG; ArtError for
    anything else (16-bit, paletted, interlaced, truncated, too large)."""
    if data[:8] != PNG_MAGIC:
        raise ArtError("not a PNG")
    pos, idat, head = 8, bytearray(), None
    while pos + 8 <= len(data):
        length, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + length]
        if len(body) != length:
            raise ArtError("truncated PNG chunk")
        if kind == b"IHDR":
            if length != 13:
                raise ArtError("bad PNG header")
            head = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
        pos += 12 + length
    if head is None:
        raise ArtError("PNG without IHDR")
    w, h, depth, colour, _, _, interlace = head
    channels = {0: 1, 4: 2, 2: 3, 6: 4}.get(colour)
    if depth != 8 or channels is None or interlace:
        raise ArtError("only 8-bit non-interlaced grey/RGB(A) PNGs are read (depth %d, colour type %d)"
                       % (depth, colour))
    if not w or not h or w * h > max_pixels:
        raise ArtError("the PNG is too large (%dx%d)" % (w, h))
    stride = w * channels
    try:
        unpacker = zlib.decompressobj()
        raw = unpacker.decompress(bytes(idat), h * (stride + 1))
    except zlib.error as e:
        raise ArtError("bad PNG data: %s" % e)
    if len(raw) < h * (stride + 1):
        raise ArtError("truncated PNG pixels")
    out, prev, bpp = bytearray(), bytearray(stride), channels
    for y in range(h):
        base = y * (stride + 1)
        kind, row = raw[base], bytearray(raw[base + 1:base + 1 + stride])
        if kind == 1:
            for i in range(bpp, stride):
                row[i] = (row[i] + row[i - bpp]) & 0xFF
        elif kind == 2:
            for i in range(stride):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif kind == 3:
            for i in range(stride):
                row[i] = (row[i] + (((row[i - bpp] if i >= bpp else 0) + prev[i]) >> 1)) & 0xFF
        elif kind == 4:
            for i in range(stride):
                a = row[i - bpp] if i >= bpp else 0
                c = prev[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + _paeth(a, prev[i], c)) & 0xFF
        elif kind != 0:
            raise ArtError("unknown PNG filter %d" % kind)
        out += row
        prev = row
    if channels == 4:
        return w, h, bytes(out)
    rgba = bytearray(b"\xff" * (w * h * 4))
    if channels == 1:
        rgba[0::4] = rgba[1::4] = rgba[2::4] = out
    elif channels == 2:
        rgba[0::4] = rgba[1::4] = rgba[2::4] = out[0::2]
        rgba[3::4] = out[1::2]
    else:
        rgba[0::4], rgba[1::4], rgba[2::4] = out[0::3], out[1::3], out[2::3]
    return w, h, bytes(rgba)


def read_art(data, rel):
    """(w, h, RGBA) of one of the pack's art files, checked: at most ``ART_MAX_BYTES`` and exactly its size."""
    if rel not in ART_SIZES:
        raise ArtError("%s is not an art file" % rel)
    if len(data) > ART_MAX_BYTES:
        raise ArtError("%s is larger than %d bytes" % (rel, ART_MAX_BYTES))
    w, h, rgba = read_png(data)
    if (w, h) != ART_SIZES[rel]:
        raise ArtError("%s must be %dx%d (it is %dx%d)" % ((rel,) + ART_SIZES[rel] + (w, h)))
    return w, h, rgba


# -- the EAGL object: where each texture sits -----------------------------------------------------------------------

SHT_SYMTAB, SHT_REL = 2, 9


def _elf(blob):
    """(.data bytes, [(name, value, shndx)], {data offset: symbol index}, .data's section index) of a 32-bit
    little-endian ELF object."""
    if blob[:4] != b"\x7fELF" or len(blob) < 52 or blob[4] != 1 or blob[5] != 1:
        raise ArtError("not a 32-bit little-endian ELF object")
    shoff, = struct.unpack_from("<I", blob, 32)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", blob, 46)
    if shentsize != 40 or not shnum or shoff + shnum * 40 > len(blob) or shstrndx >= shnum:
        raise ArtError("bad ELF section table")
    heads = [struct.unpack_from("<10I", blob, shoff + i * 40) for i in range(shnum)]

    def body(i):
        _, typ, _, _, offset, size = heads[i][:6]
        if typ in (0, 8):
            return b""
        if offset + size > len(blob):
            raise ArtError("an ELF section runs past the file")
        return blob[offset:offset + size]

    def cstr(table, at):
        end = table.find(b"\0", at)
        if at >= len(table) or end < 0:
            raise ArtError("bad ELF string")
        return table[at:end].decode("latin-1")

    shstr = body(shstrndx)
    names = [cstr(shstr, h[0]) for h in heads]
    if ".data" not in names:
        raise ArtError("the object has no .data")
    data_index = names.index(".data")
    symbols = []
    for i, h in enumerate(heads):
        if h[1] == SHT_SYMTAB:
            if h[6] >= shnum:
                raise ArtError("bad ELF symbol table")
            table, strtab = body(i), body(h[6])
            for pos in range(0, len(table) - 15, 16):
                name_off, value, _, _, _, shndx = struct.unpack_from("<IIIBBH", table, pos)
                symbols.append((cstr(strtab, name_off) if name_off else "", value, shndx))
            break
    relocs = {}
    for i, h in enumerate(heads):
        if h[1] == SHT_REL and h[7] == data_index:
            table = body(i)
            for pos in range(0, len(table) - 7, 8):
                offset, info = struct.unpack_from("<II", table, pos)
                if info >> 8 >= len(symbols):
                    raise ArtError("bad ELF relocation")
                relocs[offset] = info >> 8
    return body(data_index), symbols, relocs, data_index


def _shape(symbol):
    """``__EAGL::TAR:::...SHAPENAME=A003,1;...`` -> ``A003`` (empty for another symbol)."""
    if not symbol.startswith("__EAGL::TAR:::") or "SHAPENAME=" not in symbol:
        return ""
    return symbol.split("SHAPENAME=")[1].split(",")[0].split(";")[0].strip()


def object_layout(o_bytes):
    """[{"model", "box": (x0, y0, x1, y1), "quads": [(texture name, origin x, origin y)]}] of a picture's EAGL
    object, movie units rounded to whole pixels: texel (i, j) of the texture shows at movie (origin x + i,
    origin y + j), clipped to the model's box. Models in the object's symbol order, quads in data order."""
    data, symbols, relocs, data_index = _elf(o_bytes)

    def u32(at):
        if at < 0 or at + 4 > len(data):
            raise ArtError("the object's data is cut short")
        return struct.unpack_from("<I", data, at)[0]

    def local(at):
        """The .data offset the pointer at ``at`` holds; None when it is not a pointer into .data."""
        sym = relocs.get(at)
        if sym is None:
            return None
        name, value, shndx = symbols[sym]
        if shndx != data_index:
            return None
        return u32(at) + (value if name else 0)

    def floats(at, n):
        if at < 0 or at + 4 * n > len(data):
            raise ArtError("the object's data is cut short")
        return struct.unpack_from("<%df" % n, data, at)

    def whole(v):
        if v != v or abs(v) > 1 << 20:
            raise ArtError("the object has an unreadable position")
        return int(v + 0.5) if v >= 0 else -int(-v + 0.5)

    quads = []
    for r in sorted(relocs):
        tex = _shape(symbols[relocs[r]][0])
        if not tex:
            continue
        mvp, matrix = relocs.get(r + 8), local(r + 16)
        if mvp is None or "ModelViewProjection" not in symbols[mvp][0] or matrix is None:
            continue                        # the model's texture list, not a drawn quad
        m = floats(matrix, 16)
        quads.append((r, tex, -whole(m[12]), -whole(m[13])))
    models, boxes = [], {}
    for name, value, shndx in symbols:
        if shndx == data_index and name.startswith("__Model:::"):
            models.append((name[10:], value))
        elif shndx == data_index and name.startswith("__BBOX:::"):
            b = floats(value, 6)
            boxes[name[9:]] = (whole(b[0]), whole(b[1]), whole(b[3]), whole(b[4]))
    if not models:
        raise ArtError("the object has no model")
    # which model draws a quad: the model's instance list (model + 0xCC -> per material a count and pointers)
    owner = []
    for name, off in models:
        count, lod = u32(off + 0x9C), local(off + 0xCC)
        if lod is None or count > 64:
            continue
        p = lod + 4
        for _ in range(count):
            n = u32(p)
            if n > 256:
                raise ArtError("the object has an unreadable model")
            for i in range(n):
                inst = local(p + 4 + 4 * i)
                if inst is not None:
                    owner.append((inst, name))
            p += 4 + 4 * n
    owner.sort()
    out = [{"model": name, "box": boxes.get(name), "quads": []} for name, _ in models]
    by_name = {m["model"]: m for m in out}
    for r, tex, ox, oy in quads:
        mine = [name for inst, name in owner if inst <= r]
        target = by_name[mine[-1]] if mine else out[0]
        target["quads"].append((tex, ox, oy))
    for m in out:
        if m["box"] is None and m["quads"]:
            raise ArtError("model %s has no box" % m["model"])
    return [m for m in out if m["quads"]]


# -- the SHPX texture bank ------------------------------------------------------------------------------------------

def shpx_textures(xsh):
    """{name: (pixel offset, width, height, type)} of the bank's plain DXT3/DXT5 images."""
    if len(xsh) < 16 or xsh[:4] != b"SHPX":
        raise ArtError("not an SHPX bank")
    size, count = struct.unpack_from("<II", xsh, 4)
    if size != len(xsh) or count > 4096 or 16 + count * 8 > size:
        raise ArtError("bad SHPX directory")
    out = {}
    for i in range(count):
        name, off = struct.unpack_from("<4sI", xsh, 16 + i * 8)
        if off + 16 > size:
            raise ArtError("bad SHPX record")
        kind = xsh[off]
        if kind not in (DXT3, DXT5):
            continue
        w, h = struct.unpack_from("<HH", xsh, off + 4)
        flags, = struct.unpack_from("<I", xsh, off + 12)
        if flags & 0xF0002000 or not w or not h or w % 4 or h % 4 or off + 16 + w * h > size:
            continue                        # mips, swizzled or an odd size: not one of the menu pictures
        out[name.decode("latin-1").strip(" \0")] = (off + 16, w, h, kind)
    return out


# -- DXT ------------------------------------------------------------------------------------------------------------

def _rgb565(c):
    r, g, b = (c & 0xF800) >> 8, (c & 0x07E0) >> 3, (c & 0x001F) << 3
    return (r | r >> 5, g | g >> 6, b | b >> 5)


def _pack565(r, g, b):
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def _alpha_table(a0, a1):
    if a0 > a1:
        return [a0, a1] + [((7 - k) * a0 + k * a1) // 7 for k in range(1, 7)]
    return [a0, a1] + [((5 - k) * a0 + k * a1) // 5 for k in range(1, 5)] + [0, 255]


def decode_texture(xsh, at, w, h, kind, rect=None):
    """The RGBA bytes (rows top to bottom) of a DXT3 or DXT5 image whose blocks start at ``at``; with ``rect``
    (x0, y0, x1, y1 in pixels) only the blocks that touch it are decoded (the rest stays zero)."""
    out = bytearray(w * h * 4)
    bw = w // 4
    x0, y0, x1, y1 = rect or (0, 0, w, h)
    for by in range(max(0, y0 // 4), min(h // 4, (y1 + 3) // 4)):
        for bx in range(max(0, x0 // 4), min(bw, (x1 + 3) // 4)):
            o = at + (by * bw + bx) * 16
            c0, c1, bits = struct.unpack_from("<HHI", xsh, o + 8)
            p0, p1 = _rgb565(c0), _rgb565(c1)
            pal = (bytes(p0), bytes(p1), bytes((2 * a + b) // 3 for a, b in zip(p0, p1)),
                   bytes((a + 2 * b) // 3 for a, b in zip(p0, p1)))
            if kind == DXT5:
                abits, tab = int.from_bytes(xsh[o + 2:o + 8], "little"), _alpha_table(xsh[o], xsh[o + 1])
                alpha = [tab[(abits >> (3 * i)) & 7] for i in range(16)]
            else:
                abits = int.from_bytes(xsh[o:o + 8], "little")
                alpha = [((abits >> (4 * i)) & 15) * 17 for i in range(16)]
            for i in range(16):
                t = ((by * 4 + (i >> 2)) * w + bx * 4 + (i & 3)) * 4
                out[t:t + 3] = pal[(bits >> (2 * i)) & 3]
                out[t + 3] = alpha[i]
    return bytes(out)


_Q5 = [(v * 31 + 127) // 255 for v in range(256)]
_Q6 = [(v * 63 + 127) // 255 for v in range(256)]
_ZERO16, _FULL16 = bytes(16), b"\xff" * 16
_SHIFT = [bytes(lo) + bytes(range(256 - lo)) for lo in range(256)]      # v -> max(0, v - lo)
_TABLES = {}


def _index_tables(d):
    """For a value range ``d`` (hi - lo): the byte tables that turn v - lo into a block index: the colour index
    with the high end first, the colour index with the low end first, the DXT5 alpha index (high end first)."""
    t = _TABLES.get(d)
    if t is None:
        q = [min(3, (x * 6 + d) // (2 * d)) for x in range(256)]                # thirds from the low end
        a = [min(7, (max(0, d - x) * 14 + d) // (2 * d)) for x in range(256)]    # sevenths from the high end
        t = _TABLES[d] = (bytes((1, 3, 2, 0)[v] for v in q), bytes((0, 2, 3, 1)[v] for v in q),
                          bytes(0 if v == 0 else 1 if v == 7 else v + 1 for v in a))
    return t


_A4 = bytes((v * 15 + 127) // 255 for v in range(256))


def _pack2(idx):
    x = int.from_bytes(idx, "little")
    x = (x | x >> 6) & 0x000F000F000F000F000F000F000F000F
    x = (x | x >> 12) & 0x000000FF000000FF000000FF000000FF
    x = (x | x >> 24) & 0x000000000000FFFF000000000000FFFF
    return (x | x >> 48) & 0xFFFFFFFF


def _pack3(idx):
    x = int.from_bytes(idx, "little")
    x = (x | x >> 5) & 0x003F003F003F003F003F003F003F003F
    x = (x | x >> 10) & 0x00000FFF00000FFF00000FFF00000FFF
    x = (x | x >> 20) & 0x0000000000FFFFFF0000000000FFFFFF
    return (x | x >> 40) & 0xFFFFFFFFFFFF


def _pack4(idx):
    x = int.from_bytes(idx, "little")
    x = (x | x >> 4) & 0x00FF00FF00FF00FF00FF00FF00FF00FF
    x = (x | x >> 8) & 0x0000FFFF0000FFFF0000FFFF0000FFFF
    x = (x | x >> 16) & 0x00000000FFFFFFFF00000000FFFFFFFF
    return (x | x >> 32) & 0xFFFFFFFFFFFFFFFF


def encode_texture(r, g, b, a, w, h, kind, base=None, rect=None):
    """The DXT3 or DXT5 blocks of four ``w`` x ``h`` byte planes. A block's two colours are the ends of its
    colour box along the channel that varies most (the other channels follow it, or run against it when they do
    in the block), the high colour first; a clear pixel takes no part. With ``base`` (the blocks there now) and
    ``rect`` (x0, y0, x1, y1 in pixels) only the blocks that touch the rectangle are encoded, the others kept."""
    bw = w // 4
    out = bytearray(base) if base is not None else bytearray(w * h)
    x0, y0, x1, y1 = rect or (0, 0, w, h)
    bx0, bx1 = max(0, x0 // 4), min(bw, (x1 + 3) // 4)
    pack = struct.pack
    for by in range(max(0, y0 // 4), min(h // 4, (y1 + 3) // 4)):
        row = by * 4 * w
        strip = a[row:row + 4 * w]
        at = by * bw * 16
        if not any(strip):
            out[at + bx0 * 16:at + bx1 * 16] = bytes((bx1 - bx0) * 16)
            continue
        ra, ga, ba = r[row:row + 4 * w], g[row:row + 4 * w], b[row:row + 4 * w]
        for bx in range(bx0, bx1):
            p0, p1, p2, p3 = bx * 4, bx * 4 + w, bx * 4 + 2 * w, bx * 4 + 3 * w
            al = strip[p0:p0 + 4] + strip[p1:p1 + 4] + strip[p2:p2 + 4] + strip[p3:p3 + 4]
            o = at + bx * 16
            if al == _ZERO16:
                out[o:o + 16] = _ZERO16
                continue
            chans = [ra[p0:p0 + 4] + ra[p1:p1 + 4] + ra[p2:p2 + 4] + ra[p3:p3 + 4],
                     ga[p0:p0 + 4] + ga[p1:p1 + 4] + ga[p2:p2 + 4] + ga[p3:p3 + 4],
                     ba[p0:p0 + 4] + ba[p1:p1 + 4] + ba[p2:p2 + 4] + ba[p3:p3 + 4]]
            la, ha = min(al), max(al)
            if la:
                seen = chans
            else:                           # the clear pixels do not choose the colours
                keep = [i for i in range(16) if al[i]]
                seen = [bytes([c[i] for i in keep]) for c in chans]
            lows, highs = [min(c) for c in seen], [max(c) for c in seen]
            spans = [highs[0] - lows[0], highs[1] - lows[1], highs[2] - lows[2]]
            d = max(spans)
            if not d:
                c0 = c1 = _Q5[lows[0]] << 11 | _Q6[lows[1]] << 5 | _Q5[lows[2]]
                bits = 0
            else:
                main = spans.index(d)
                top, bottom = seen[main].index(highs[main]), seen[main].index(lows[main])
                hi_c, lo_c = list(highs), list(lows)
                for c in range(3):
                    if c != main and seen[c][top] < seen[c][bottom]:
                        hi_c[c], lo_c[c] = lows[c], highs[c]
                c0 = _Q5[hi_c[0]] << 11 | _Q6[hi_c[1]] << 5 | _Q5[hi_c[2]]
                c1 = _Q5[lo_c[0]] << 11 | _Q6[lo_c[1]] << 5 | _Q5[lo_c[2]]
                if c0 == c1:
                    bits = 0
                else:
                    tables = _index_tables(d)
                    idx = chans[main].translate(_SHIFT[lows[main]])
                    if c0 > c1:
                        bits = _pack2(idx.translate(tables[0]))
                    else:
                        c0, c1 = c1, c0
                        bits = _pack2(idx.translate(tables[1]))
            if kind == DXT3:
                head = _pack4(al.translate(_A4)).to_bytes(8, "little")
            elif ha == la:
                head = bytes((ha, la, 0, 0, 0, 0, 0, 0))
            else:
                head = bytes((ha, la)) + _pack3(
                    al.translate(_SHIFT[la]).translate(_index_tables(ha - la)[2])).to_bytes(6, "little")
            out[o:o + 16] = head + pack("<HHI", c0, c1, bits)
    return bytes(out)


# -- byte planes: resampling and tone with whole-row arithmetic -----------------------------------------------------
#
# A row of bytes becomes one big integer with a 16-bit lane per byte; a weighted sum of rows is then a few
# integer operations, and the high byte of each lane is the sum / 256.

_HALF = {}


def _wide(row):
    lanes = bytearray(2 * len(row))
    lanes[0::2] = row
    return int.from_bytes(lanes, "little")


def _half(n):
    v = _HALF.get(n)
    if v is None:
        v = _HALF[n] = int.from_bytes(b"\x80\x00" * n, "little")
    return v


def _taps(sn, dn, num, den, s0):
    """Per destination pixel d of ``dn``: [(source index, weight)], weights in 256ths. Pixel d covers the source
    span [s0 + d * num, s0 + (d + 1) * num) in units of 1 / ``den`` source pixel: the average of that span when
    shrinking, the two nearest source pixels when enlarging. Source pixels outside 0..sn-1 count as clear."""
    out = []
    for d in range(dn):
        lo = s0 + d * num
        if num >= den:
            hi, taps = lo + num, []
            for k in range(lo // den, (hi - 1) // den + 1):
                taps.append([k, (min(hi, (k + 1) * den) - max(lo, k * den)) * 256 // num])
            taps[max(range(len(taps)), key=lambda i: taps[i][1])][1] += 256 - sum(t[1] for t in taps)
        else:
            t = 2 * lo + num - den
            k = t // (2 * den)
            w1 = (t - k * 2 * den) * 256 // (2 * den)
            taps = [[k, 256 - w1], [k + 1, w1]]
        out.append([(k, wgt) for k, wgt in taps if 0 <= k < sn and wgt])
    return out


def _resample(plane, sw, xt, yt):
    """``plane`` (rows of ``sw`` bytes) through the taps of each axis: len(xt) x len(yt) bytes."""
    dw, dh = len(xt), len(yt)
    used = [k for taps in xt for k, _ in taps]
    if not used or not any(yt):
        return bytes(dw * dh)
    c0, c1 = min(used), max(used) + 1
    cw = c1 - c0
    half, rows, mid = _half(cw), {}, []
    for taps in yt:
        if not taps:
            mid.append(bytes(cw))
            continue
        acc = half
        for k, wgt in taps:
            v = rows.get(k)
            if v is None:
                v = rows[k] = _wide(plane[k * sw + c0:k * sw + c1])
            acc += v * wgt
        mid.append(acc.to_bytes(2 * cw, "little")[1::2])
    mid = b"".join(mid)
    half, cols, across = _half(dh), {}, []
    for taps in xt:
        if not taps:
            across.append(bytes(dh))
            continue
        acc = half
        for k, wgt in taps:
            v = cols.get(k)
            if v is None:
                v = cols[k] = _wide(mid[k - c0::cw])
            acc += v * wgt
        across.append(acc.to_bytes(2 * dh, "little")[1::2])
    across = b"".join(across)
    return b"".join(across[y::dh] for y in range(dh))


def _luminance(r, g, b):
    n = len(r)
    return (_wide(r) * 77 + _wide(g) * 150 + _wide(b) * 29 + _half(n)).to_bytes(2 * n, "little")[1::2]


def _mix(x, y, share):
    """x * (256 - share) / 256 + y * share / 256 per byte."""
    n = len(x)
    return (_wide(x) * (256 - share) + _wide(y) * share + _half(n)).to_bytes(2 * n, "little")[1::2]


def _lum(c):
    return (77 * c[0] + 150 * c[1] + 29 * c[2] + 128) >> 8


# -- the pack's art -------------------------------------------------------------------------------------------------

_CLEAR = re.compile(rb"\x00+")
_PARTIAL = re.compile(rb"[^\x00\xff]+")


class Art:
    """One art image: its colour planes multiplied by the alpha (what the resampling averages), the alpha plane,
    the mean colour of what shows and the box of what shows (x0, y0, x1, y1; alpha 32 and up)."""

    def __init__(self, w, h, rgba):
        self.w, self.h = w, h
        self.a = a = rgba[3::4]
        planes = []
        for c in range(3):
            p = bytearray(rgba[c::4])
            for m in _CLEAR.finditer(a):
                p[m.start():m.end()] = bytes(m.end() - m.start())
            for m in _PARTIAL.finditer(a):
                for i in range(m.start(), m.end()):
                    p[i] = (p[i] * a[i] + 127) // 255
            planes.append(bytes(p))
        self.planes = planes
        total = sum(a)
        self.mean = tuple(min(255, sum(p) * 255 // total) for p in planes) if total else (0, 0, 0)
        solid = a.translate(_SOLID)
        ys = [y for y in range(h) if any(solid[y * w:(y + 1) * w])]
        xs = [x for x in range(w) if any(solid[x::w])]
        self.box = (xs[0], ys[0], xs[-1] + 1, ys[-1] + 1) if xs else None


_SOLID = bytes(32) + b"\x01" * 224


def load_art(read):
    """{BODY / HEAD / NAME: Art} of a pack: ``read(path)`` gives one of its files (KeyError or OSError when it has
    none: that picture then stays the template's). ArtError for a file that is not what the art must be."""
    out = {}
    for rel in (BODY, HEAD, NAME):
        try:
            data = read(rel)
        except (KeyError, OSError):
            continue
        out[rel] = Art(*read_art(data, rel))
        if out[rel].box is None:
            raise ArtError("%s is empty" % rel)
    return out


def _window(art, dw, dh, xs, ys):
    """The art through an x and a y mapping ((num, den, s0) each, see ``_taps``) as four dw x dh planes, the
    colour no longer multiplied by the alpha."""
    xt, yt = _taps(art.w, dw, *xs), _taps(art.h, dh, *ys)
    a = _resample(art.a, art.w, xt, yt)
    planes = [bytearray(_resample(p, art.w, xt, yt)) for p in art.planes]
    for m in _PARTIAL.finditer(a):
        for i in range(m.start(), m.end()):
            al = a[i]
            for p in planes:
                p[i] = min(255, (p[i] * 255 + al // 2) // al)
    return [bytes(p) for p in planes] + [a]


def _tall(art, bw, bh, share, centre):
    """The art's top ``share`` thousandths of its height over the whole box height, its middle column at
    ``centre`` thousandths of the box width."""
    num, den = share * art.h, 1000 * bh
    return _window(art, bw, bh, (num, den, art.w * den // 2 - centre * bw * num // 1000), (num, den, 0))


def _crop(art, dw, dh, x0, y0, x1, y1):
    """The art's window (thousandths of its size) as dw x dh planes."""
    return _window(art, dw, dh, ((x1 - x0) * art.w, 1000 * dw, x0 * art.w * dw),
                   ((y1 - y0) * art.h, 1000 * dh, y0 * art.h * dh))


# -- tone -----------------------------------------------------------------------------------------------------------

GAIN_MIN, GAIN_MAX = 102, 640           # the art is made at most 2.5 times darker or brighter (256ths)
COOL = ((40, 46, 51), 0)                # the stock Suspect portrait's tone (the hub portrait without a template)


def _tone(colours):
    """(mean colour, how much of the art's own colour stays: 0..256) of a template picture's opaque colours: a
    picture in one tone (every colour near its brightness times the mean colour: the blue-grey and the sepia
    ones) gives 0, a full-colour one 256."""
    n = len(colours)
    if not n:
        return None
    mean = tuple(sum(c[k] for c in colours) // n for k in range(3))
    ml = max(1, _lum(mean))
    off = 0
    for c in colours:
        l = _lum(c)
        off += abs(c[0] - l * mean[0] // ml) + abs(c[1] - l * mean[1] // ml) + abs(c[2] - l * mean[2] // ml)
    spread = off * 100 // (n * max(ml, 48))
    return mean, max(0, min(256, (spread - 16) * 16))


def _block_tone(xsh, textures):
    """``_tone`` of the wholly opaque blocks' two colours (no pixel is decoded)."""
    colours, seen = [], {}
    for at, w, h, kind in textures:
        for a0, a1, lo, hi, c0, c1 in struct.iter_unpack("<BBHIHH4x", xsh[at:at + w * h]):
            if kind == DXT5:
                if a0 < 248 or (a1 < 248 and (lo or hi)):
                    continue
            elif not (a0 == a1 == 255 and lo == 0xFFFF and hi == 0xFFFFFFFF):
                continue
            for c in (c0, c1):
                v = seen.get(c)
                if v is None:
                    v = seen[c] = _rgb565(c)
                colours.append(v)
    return _tone(colours)


def _grade(planes, art_mean, tone, keep=None):
    """The three colour planes in the template's tone: the brightness times the template's mean colour, mixed
    (``keep`` 256ths, default the tone's own) with the art's own colours; both as bright as the template (the
    mean brightness of the art becomes the template's, within the gain limits)."""
    mean, own = tone
    keep = own if keep is None else keep
    r, g, b = planes[:3]
    ml, al = max(1, _lum(mean)), max(1, _lum(art_mean))
    gain = max(GAIN_MIN, min(GAIN_MAX, ml * 256 // al))
    lum = _luminance(r, g, b) if keep < 256 else None
    full = bytes(min(255, (v * gain + 128) >> 8) for v in range(256))
    out = []
    for c, plane in enumerate((r, g, b)):
        one = None
        if keep < 256:
            one = lum.translate(bytes(min(255, (v * gain * mean[c] + 128 * ml) // (256 * ml)) for v in range(256)))
        if keep:
            one = plane.translate(full) if one is None else _mix(one, plane.translate(full), keep)
        out.append(one)
    return out


# -- the picture families -------------------------------------------------------------------------------------------
#
# What each family shows, by the file's name (``newfighter.PICTURE_FAMILIES``); <n> is the player count of the
# screen it is drawn on. Shares are thousandths.
#
#   p_cf_<id+1>_<n>p   the select portrait: the body from the head down (2 players: to the thighs, 3: to the
#                      knees, 4: whole)
#   p_CT_<id+1>        the team select portrait: the body to the waist
#   <id+1>_<n>p        the loading card: the body to the knees, left of the middle, the name top right
#   p_ML_<id+1>_<n>p   the match loading bust: the head picture over the whole box
#   thumb<id>, thumb<id>off, t_MS_<id>_<n>p, t_CT_<id>, t_CO_<id>
#                      small opaque head pictures: the head over the picture's own background, inside its frame
#   l_CF_, l_MS_       the name, flat and dark;  l_ML_, I_ML_: the name, light with a dark outline
#
# The specs: body and bust (the share of the art's height that fills the box height, the box column of the art's
# middle); card: the same, then the name's right edge and widest width (shares of the box width) and its top and
# height (shares of the box height), in the order right, top, height, width; face (the frame's width in pixels,
# then the head picture's window x0, y0, x1, y1); name: outlined or not.

_FAMILIES = (
    (re.compile(r"^p_cf_\d+_([234])p$", re.I), "body", {"2": (620, 500), "3": (780, 500), "4": (1000, 500)}),
    (re.compile(r"^p_ct_\d+()$", re.I), "body", {"": (500, 500)}),
    (re.compile(r"^\d+_([23])p$", re.I), "card", {"2": (740, 330, 932, 74, 58, 460),
                                                   "3": (740, 330, 925, 59, 43, 460)}),
    (re.compile(r"^p_ml_\d+_([234])p$", re.I), "bust", {"2": (1000, 500), "3": (1000, 500), "4": (1000, 500)}),
    (re.compile(r"^thumb\d+(off)?$", re.I), "face", {None: (2, 220, 40, 780, 600), "off": (2, 220, 40, 780, 600)}),
    (re.compile(r"^t_ms_\d+_[234]p()$", re.I), "face", {"": (0, 120, 20, 880, 780)}),
    (re.compile(r"^t_c[to]_\d+()$", re.I), "face", {"": (3, 120, 20, 880, 780)}),
    (re.compile(r"^l_(?:cf|ms)_\d+_[234]p()$", re.I), "name", {"": False}),
    (re.compile(r"^[li]_ml_\d+_[234]p()$", re.I), "name", {"": True}),
)
NAME_DARK = (44, 40, 38)        # a name's colour when the template's own name gives none
NEW_BADGE = (200, 36, 24)
NEW_INK = (255, 244, 224)
NEW_GLYPHS = ("X..X.XXX.X...X",
              "XX.X.X...X...X",
              "XX.X.XX..X.X.X",
              "X.XX.X...X.X.X",
              "X.XX.X...XX.XX",
              "X..X.XXX.X...X")
NEW_SIZE = (16, 8)              # two rows of four DXT blocks: each block holds the badge's two colours only


def family(name):
    """(kind, spec) of a picture file by its name (a path or a bare name, with or without ``.big``); None for a
    file that is none of the families."""
    stem = name.replace("\\", "/").rsplit("/", 1)[-1]
    if stem.lower().endswith(".big"):
        stem = stem[:-4]
    for pattern, kind, specs in _FAMILIES:
        m = pattern.match(stem)
        if m:
            return kind, specs[m.group(1)], bool(kind == "face" and m.group(1) == "off")
    return None


def layout(big_bytes):
    """Where a picture file's textures show: {"xsh": the texture bank's entry name, "offset" and "size": the
    bank's place in the file, "textures": {name: (pixel offset in the bank, width, height, type)}, "models":
    [{"model", "box": (x0, y0, x1, y1), "quads": [(texture, x, y)]}]}: texel (i, j) of a quad's texture is the
    movie's (x + i, y + j), and the model's box is what shows (its canvas). ArtError when the file is not such a
    picture."""
    from . import viv
    try:
        inner = viv.Archive(big_bytes)
        o = [e for e in inner.entries if e.name.lower().endswith(".o")]
        x = [e for e in inner.entries if e.name.lower().endswith(".xsh")]
        if len(o) != 1 or len(x) != 1:
            raise ArtError("the picture has no single .o and .xsh")
        if viv.is_refpack(inner.raw(x[0])):
            raise ArtError("the picture's textures are packed")
        xsh = inner.raw(x[0])
        textures = shpx_textures(xsh)
        models = object_layout(viv.unwrap(inner.raw(o[0])))
    except (ValueError, struct.error, IndexError, KeyError) as e:
        if isinstance(e, ArtError):
            raise
        raise ArtError("unreadable picture: %s" % e)
    for m in models:
        for tex, _, _ in m["quads"]:
            if tex not in textures:
                raise ArtError("texture %s is not in the picture's bank" % tex)
        if m["box"][2] <= m["box"][0] or m["box"][3] <= m["box"][1]:
            raise ArtError("the picture's box is empty")
    return {"xsh": x[0].name, "offset": x[0].offset, "size": x[0].size, "textures": textures, "models": models}


def canvas(big_bytes, model=0):
    """(width, height, RGBA bytes) of what a picture file shows: its ``model``-th model's box with its textures
    in place (tests, previews)."""
    from . import viv
    lay = layout(big_bytes)
    xsh = viv.Archive(big_bytes).raw(lay["xsh"])
    m = lay["models"][model]
    return _canvas(xsh, lay["textures"], m)


def _canvas(xsh, textures, m, window=None):
    """The model's box as RGBA; ``window`` (x0, y0, x1, y1 inside the box) limits what is decoded."""
    x0, y0, x1, y1 = m["box"]
    bw, bh = x1 - x0, y1 - y0
    wx0, wy0, wx1, wy1 = window or (0, 0, bw, bh)
    out = bytearray(bw * bh * 4)
    for tex, ox, oy in m["quads"]:
        at, w, h, kind = textures[tex]
        cx, cy = ox - x0, oy - y0                       # the texture's corner in the box
        px = decode_texture(xsh, at, w, h, kind, (wx0 - cx, wy0 - cy, wx1 - cx, wy1 - cy))
        for j in range(max(0, wy0 - cy), min(h, wy1 - cy)):
            i0, i1 = max(0, wx0 - cx), min(w, wx1 - cx)
            if i0 < i1:
                at_out = ((j + cy) * bw + i0 + cx) * 4
                out[at_out:at_out + (i1 - i0) * 4] = px[(j * w + i0) * 4:(j * w + i1) * 4]
    return bw, bh, bytes(out)


def _texture_planes(planes, bw, bh, cx, cy, w, h):
    """The part of four box-sized planes under a ``w`` x ``h`` texture whose corner is at (cx, cy) in the box."""
    i0, i1 = max(0, -cx), min(w, bw - cx)
    out = []
    for p in planes:
        rows = []
        for j in range(h):
            y = j + cy
            if 0 <= y < bh and i0 < i1:
                rows.append(bytes(i0) + p[y * bw + i0 + cx:y * bw + i1 + cx] + bytes(w - i1))
            else:
                rows.append(bytes(w))
        out.append(b"".join(rows))
    return out


def _store(xsh, textures, m, planes):
    """The model's box-sized planes into its textures (every block of them)."""
    x0, y0, x1, y1 = m["box"]
    for tex, ox, oy in m["quads"]:
        at, w, h, kind = textures[tex]
        r, g, b, a = _texture_planes(planes, x1 - x0, y1 - y0, ox - x0, oy - y0, w, h)
        xsh[at:at + w * h] = encode_texture(r, g, b, a, w, h, kind)


def _name_planes(art, bw, bh, x0, y0, tw, th):
    """The name's alpha (its lettering's box scaled to tw x th) at (x0, y0) of a bw x bh plane."""
    nx0, ny0, nx1, ny1 = art.box
    small = _resample(art.a, art.w, _taps(art.w, tw, nx1 - nx0, tw, nx0 * tw),
                      _taps(art.h, th, ny1 - ny0, th, ny0 * th))
    rows = []
    for y in range(bh):
        j = y - y0
        if 0 <= j < th:
            i0, i1 = max(0, -x0), min(tw, bw - x0)
            rows.append(bytes(max(0, x0)) + small[j * tw + i0:j * tw + i1] + bytes(bw - max(0, x0) - max(0, i1 - i0))
                        if i0 < i1 else bytes(bw))
        else:
            rows.append(bytes(bw))
    return b"".join(rows)


def _name_size(art, height, width):
    """(width, height) of the lettering at ``height``, made smaller when that is wider than ``width``."""
    nw, nh = art.box[2] - art.box[0], art.box[3] - art.box[1]
    if nw * height <= width * nh:
        return max(1, (nw * height + nh // 2) // nh), max(1, height)
    return max(1, width), max(1, (nh * width + nw // 2) // nw)


def _spread(a, bw, bh, radius):
    """The alpha plane grown by ``radius`` pixels (the outline's shape)."""
    pad = radius
    row_w = bw + 2 * pad
    wide = _wide(b"".join(bytes(pad) + a[y * bw:(y + 1) * bw] + bytes(pad) for y in range(bh)))
    total = 0
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            if dx * dx + dy * dy <= radius * radius + radius:
                shift = 16 * (dy * row_w + dx)
                total += wide << shift if shift >= 0 else wide >> -shift
    n = row_w * bh
    raw = (total & ((1 << (16 * n)) - 1)).to_bytes(2 * n, "little")
    low, high = raw[0::2], raw[1::2].translate(_ANY)
    full = (int.from_bytes(low, "little") | int.from_bytes(high, "little")).to_bytes(n, "little")
    return b"".join(full[y * row_w + pad:y * row_w + pad + bw] for y in range(bh))


_ANY = b"\x00" + b"\xff" * 255


def _paint_name(xsh, textures, m, art, outlined):
    """A name picture: the lettering where the template's own stands (same height, same side)."""
    x0, y0, x1, y1 = m["box"]
    bw, bh = x1 - x0, y1 - y0
    _, _, px = _canvas(xsh, textures, m)
    alpha = px[3::4]
    solid = alpha.translate(_SOLID)
    ys = [y for y in range(bh) if any(solid[y * bw:(y + 1) * bw])]
    xs = [x for x in range(bw) if any(solid[x::bw])]
    if not xs:
        raise ArtError("the template's name picture is empty")
    tx0, ty0, tx1, ty1 = xs[0], ys[0], xs[-1] + 1, ys[-1] + 1
    shown = [(px[i], px[i + 1], px[i + 2]) for i in range(0, len(px), 4) if px[i + 3] >= 128]
    if not shown:
        raise ArtError("the template's name picture is empty")
    mid = sum(_lum(c) for c in shown) // len(shown)
    light = [c for c in shown if _lum(c) >= mid] or shown
    dark = [c for c in shown if _lum(c) < mid] or shown
    mean = lambda cs: tuple(sum(c[k] for c in cs) // len(cs) for k in range(3))
    left, right = tx0, bw - tx1
    margin = max(1, min(left, right))
    radius = max(1, min(6, (ty1 - ty0 + 8) // 16)) if outlined else 0       # 6: the sums of ``_spread`` fit
    tw, th = _name_size(art, ty1 - ty0 - 2 * radius, bw - 2 * margin - 2 * radius)
    if abs(left - right) <= max(4, bw // 16):
        nx = (tx0 + tx1 - tw) // 2
    elif right < left:
        nx = tx1 - radius - tw
    else:
        nx = tx0 + radius
    ny = (ty0 + ty1 - th) // 2
    fill = _name_planes(art, bw, bh, nx, ny, tw, th)
    if outlined:
        lo, hi = mean(dark), mean(light)
        planes = [fill.translate(bytes((lo[c] * (255 - v) + hi[c] * v + 127) // 255 for v in range(256)))
                  for c in range(3)] + [_spread(fill, bw, bh, radius)]
    else:
        ink = mean(shown)
        planes = [bytes([ink[c]]) * (bw * bh) for c in range(3)] + [fill]
    _store(xsh, textures, m, planes)


def _paint_tall(xsh, textures, m, art, spec, name=None):
    """A body or bust picture; with ``name`` (the name art) and a card's spec the name goes top right."""
    x0, y0, x1, y1 = m["box"]
    bw, bh = x1 - x0, y1 - y0
    tone = _block_tone(xsh, [textures[tex] for tex, _, _ in m["quads"]]) or COOL
    planes = _tall(art, bw, bh, spec[0], spec[1])
    planes = _grade(planes, art.mean, tone) + [planes[3]]
    if name is not None and len(spec) > 2:
        right, top = spec[2] * bw // 1000, spec[3] * bh // 1000
        height, width = max(4, spec[4] * bh // 1000), max(4, spec[5] * bw // 1000)
        # the colour of the template's own name: what is opaque in its corner, right of the figure
        rx0 = bw - width
        _, _, px = _canvas(xsh, textures, m, (rx0, top, right, top + height))
        seen = [(px[i], px[i + 1], px[i + 2]) for y in range(top, min(bh, top + height))
                for i in range((y * bw + rx0) * 4, (y * bw + right) * 4, 4) if px[i + 3] >= 128]
        ink = tuple(sum(c[k] for c in seen) // len(seen) for k in range(3)) if len(seen) >= 16 else NAME_DARK
        tw, th = _name_size(name, height, width)
        cover = _name_planes(name, bw, bh, right - tw, top + (height - th) // 2, tw, th)
        out = [bytearray(p) for p in planes]
        for found in _ANYBYTE.finditer(cover):
            for i in range(found.start(), found.end()):
                n = cover[i]
                for c in range(3):
                    out[c][i] = (ink[c] * n + out[c][i] * (255 - n) + 127) // 255
                out[3][i] = out[3][i] + (255 - out[3][i]) * n // 255
        planes = [bytes(p) for p in out]
    _store(xsh, textures, m, planes)


_ANYBYTE = re.compile(rb"[^\x00]+")


def stamp_new(r, g, b, a, w, x=0, y=0):
    """The NEW badge (``NEW_SIZE``, pixel letters) into four bytearray planes of rows ``w`` wide, its corner at
    (x, y): a block-aligned corner keeps each DXT block to the badge's two colours."""
    for j in range(NEW_SIZE[1]):
        for i in range(NEW_SIZE[0]):
            ink = 1 <= j <= len(NEW_GLYPHS) and 1 <= i <= len(NEW_GLYPHS[0]) and NEW_GLYPHS[j - 1][i - 1] == "X"
            c = NEW_INK if ink else NEW_BADGE
            at = (y + j) * w + x + i
            r[at], g[at], b[at], a[at] = c[0], c[1], c[2], 255


def _paint_face(xsh, textures, m, art, spec, one_tone, new):
    """A small opaque head picture: the head over the picture's own background colour inside its frame (the
    frame and what lies outside the box keep the template's bytes)."""
    inset, cx0, cy0, cx1, cy1 = spec
    x0, y0, x1, y1 = m["box"]
    if len(m["quads"]) != 1:
        raise ArtError("a small head picture has one texture")
    tex, ox, oy = m["quads"][0]
    at, w, h, kind = textures[tex]
    fx0, fy0 = x0 - ox + inset, y0 - oy + inset                     # the picture inside the frame, in texels
    fw, fh = x1 - x0 - 2 * inset, y1 - y0 - 2 * inset
    if fx0 < 0 or fy0 < 0 or fx0 + fw > w or fy0 + fh > h or fw < 8 or fh < 8:
        raise ArtError("the small head picture has an unexpected frame")
    px = decode_texture(xsh, at, w, h, kind)
    planes = [bytearray(px[c::4]) for c in range(4)]
    pixel = lambda i, j: (px[(j * w + i) * 4], px[(j * w + i) * 4 + 1], px[(j * w + i) * 4 + 2])
    # the background: the most frequent colour (16 steps a channel) one pixel inside the picture's edge
    ring = [pixel(fx0 + i, fy0 + j) for j in range(1, fh - 1) for i in range(1, fw - 1)
            if j in (1, fh - 2) or i in (1, fw - 2)]
    counts = {}
    for c in ring:
        counts.setdefault((c[0] >> 4, c[1] >> 4, c[2] >> 4), []).append(c)
    most = max(sorted(counts), key=lambda k: len(counts[k]))
    back = tuple(sum(c[k] for c in counts[most]) // len(counts[most]) for k in range(3))
    inside = [pixel(fx0 + i, fy0 + j) for j in range(fh) for i in range(fw)]
    head = [c for c in inside if abs(c[0] - back[0]) + abs(c[1] - back[1]) + abs(c[2] - back[2]) > 40] or inside
    tone = _tone(head)
    face = _crop(art, fw, fh, cx0, cy0, cx1, cy1)
    graded = _grade(face, art.mean, tone, 0 if one_tone else None)
    cover = face[3]
    for j in range(fh):
        row = (fy0 + j) * w + fx0
        for i in range(fw):
            n = cover[j * fw + i]
            for c in range(3):
                planes[c][row + i] = (graded[c][j * fw + i] * n + back[c] * (255 - n) + 127) // 255
            planes[3][row + i] = 255
    rect = [fx0, fy0, fx0 + fw, fy0 + fh]
    if new:
        sx, sy = x0 - ox, y0 - oy
        if sx % 4 or sy % 4 or sx < 0 or sy < 0 or sx + NEW_SIZE[0] > w or sy + NEW_SIZE[1] > h:
            raise ArtError("the thumb has no room for the NEW badge")
        stamp_new(planes[0], planes[1], planes[2], planes[3], w, sx, sy)
        rect = [min(rect[0], sx), min(rect[1], sy), max(rect[2], sx + NEW_SIZE[0]), max(rect[3], sy + NEW_SIZE[1])]
    r, g, b, a = (bytes(p) for p in planes)
    xsh[at:at + w * h] = encode_texture(r, g, b, a, w, h, kind, xsh[at:at + w * h], rect)


def paint_picture(name, big_bytes, art, new=False, strict=False):
    """One picture file (a family copy, ``name`` its file name) with the pack's art (``load_art``) painted in:
    the same archive with the same texture records, only pixel bytes changed. ``new`` stamps NEW on a thumb.
    A file that is none of the families, one whose art the pack does not have, or one that is not built the way
    the families are comes back unchanged (``strict``: the last raises ArtError)."""
    kind = family(name)
    if kind is None:
        return big_bytes
    kind, spec, off = kind
    need = {"body": BODY, "card": BODY, "bust": HEAD, "face": HEAD, "name": NAME}[kind]
    if need not in art:
        return big_bytes
    try:
        lay = layout(big_bytes)
        xsh = bytearray(big_bytes[lay["offset"]:lay["offset"] + lay["size"]])
        for index, m in enumerate(lay["models"]):
            if kind == "name":
                _paint_name(xsh, lay["textures"], m, art[NAME], spec)
            elif kind == "face":
                # the second picture of a file is its "not chosen" state, in one tone like the "off" thumb
                _paint_face(xsh, lay["textures"], m, art[HEAD], spec, off or index > 0,
                            new and name.lower().rsplit("/", 1)[-1].startswith("thumb"))
            else:
                _paint_tall(xsh, lay["textures"], m, art[need], spec, art.get(NAME) if kind == "card" else None)
    except (ValueError, struct.error, IndexError, KeyError, ZeroDivisionError, OverflowError) as e:
        if strict:
            raise e if isinstance(e, ArtError) else ArtError("unreadable picture %s: %s" % (name, e))
        return big_bytes
    return big_bytes[:lay["offset"]] + bytes(xsh) + big_bytes[lay["offset"] + lay["size"]:]


def hub_portrait(body, template=None):
    """The online hub's portrait of a new fighter as PNG bytes (``HUB_SIZE``, the body to the waist like the
    stock ``online-art/fighters/<id>.png``, which is the team select portrait). ``body``: the pack's
    ``art/body.png`` bytes (or its ``Art``); ``template``: the template's ``p_CT`` picture file for its tone
    (left out: the stock Suspect's cool tone)."""
    art = body if isinstance(body, Art) else Art(*read_art(body, BODY))
    tone = COOL
    if template is not None:
        try:
            from . import viv
            lay = layout(template)
            xsh = viv.Archive(template).raw(lay["xsh"])
            tone = _block_tone(xsh, [lay["textures"][t] for m in lay["models"] for t, _, _ in m["quads"]]) or COOL
        except (ArtError, ValueError, struct.error, IndexError, KeyError):
            tone = COOL
    w, h = HUB_SIZE
    planes = _tall(art, w, h, 500, 500)
    r, g, b = _grade(planes, art.mean, tone)
    rgba = bytearray(w * h * 4)
    rgba[0::4], rgba[1::4], rgba[2::4], rgba[3::4] = r, g, b, planes[3]
    return png_bytes(bytes(rgba), w, h)

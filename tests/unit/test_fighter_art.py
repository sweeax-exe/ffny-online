"""A new fighter's own menu pictures (setup/ffnycontent/fighterart.py, used by newfighter.apply_op and
setup/content_compose.py): the pack's art/body.png, art/head.png and art/name.png painted into the copies of the
template's picture families, the NEW stamp on the select thumbs and the online hub's portrait.

The picture files here are made up (an EAGL-shaped ELF object and an SHPX bank with the fields the painter
reads); the tests at the end use the user's own dump ($DEFJAM_DATA) and are skipped without it."""
import datetime
import importlib.util
import json
import os
import struct
import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "setup"))

import content_compose as cc  # noqa: E402
from ffnycontent import fighterart as fa, newfighter as nf, viv  # noqa: E402

TAR = "__EAGL::TAR:::RUNTIME_ALLOC::UID=1;SHAPENAME=%s,1;XBOXEXTOBJ_SetClampU=EAGL::XBOXCM_CLAMP;"
MVP = "__const MATRIX4:::EAGL::ViewPort::gpModelViewProjectionMatrix"


# ------------------------------------------------------------ made-up picture files

def make_object(models):
    """An ELF object laid out like a picture's: per quad the texture symbol, the view matrix symbol and a local
    pointer to the texture matrix (8 bytes apart), a texture list, the model's instance list and its box.
    ``models``: [(name, (x0, y0, x1, y1), [(texture, x, y)])]."""
    data, relocs, strtab, syms, index = bytearray(), [], bytearray(b"\0"), [(0, 0, 0)], {}

    def sym(name, value, shndx):
        if shndx or (name, shndx) not in index:
            off = len(strtab) if name else 0
            if name:
                strtab.extend(name.encode("latin-1") + b"\0")
            syms.append((off, value, shndx))
            index[(name, shndx)] = len(syms) - 1
        return index[(name, shndx)]

    def word(v):
        data.extend(struct.pack("<I", v))
        return len(data) - 4

    def pointer(target):
        relocs.append((word(target), section))

    section, view = sym("", 0, 1), sym(MVP, 0, 0)
    for name, box, quads in models:
        instances = []
        for tex, ox, oy in quads:
            matrix = len(data)
            data.extend(struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -ox, -oy, 0, 1))
            instances.append(word(0))
            word(1)
            relocs.append((word(0), sym(TAR % tex, 0, 0)))
            word(1)
            relocs.append((word(0), view))
            word(1)
            pointer(matrix)
        word(0)                                         # the texture list: a texture symbol that draws nothing
        relocs.append((word(0), sym(TAR % quads[0][0], 0, 0)))
        data.extend(bytes(16))
        lod = word(0)
        word(len(instances))
        for inst in instances:
            pointer(inst)
        bbox = len(data)
        data.extend(struct.pack("<6f", box[0], box[1], 0, box[2], box[3], 0))
        model = len(data)
        data.extend(bytes(0x9C))
        word(1)
        data.extend(bytes(0xCC - 0xA0))
        pointer(lod)
        sym("__BBOX:::" + name, bbox, 1)
        sym("__Model:::" + name, model, 1)
    names = b"\0.data\0.rel.data\0.symtab\0.strtab\0.shstrtab\0"
    bodies = [b"", bytes(data), b"".join(struct.pack("<II", off, s << 8 | 1) for off, s in relocs),
              b"".join(struct.pack("<IIIBBH", n, v, 0, 0, 0, shndx) for n, v, shndx in syms), bytes(strtab), names]
    heads = [(0, 0, 0, 0), (names.index(b".data"), 1, 0, 0), (names.index(b".rel.data"), 9, 3, 1),
             (names.index(b".symtab"), 2, 4, 0), (names.index(b".strtab"), 3, 0, 0),
             (names.index(b".shstrtab"), 3, 0, 0)]
    out = bytearray(52)
    table = []
    for (name, kind, link, info), body in zip(heads, bodies):
        table.append(struct.pack("<10I", name, kind, 0, 0, len(out), len(body), link, info, 4, 0))
        out += body + bytes(-len(body) % 4)
    out[:52] = (b"\x7fELF\x01\x01\x01" + bytes(9)
                + struct.pack("<HHIIIIIHHHHHH", 1, 8, 1, 0, 0, len(out), 0x20924000, 52, 0, 0, 40, len(table), 5))
    return bytes(out) + b"".join(table)


def make_bank(textures):
    """An SHPX bank: {name: (w, h, type, (r, g, b, a) planes)}."""
    head = 16 + 8 * len(textures)
    records, directory, at = [], [], head
    for name, (w, h, kind, planes) in textures.items():
        directory.append(struct.pack("<4sI", name.encode().ljust(4), at))
        records.append(bytes([kind, 0, 0, 0]) + struct.pack("<HHII", w, h, 0, 0)
                       + fa.encode_texture(*planes, w, h, kind))
        at += len(records[-1])
    return b"SHPX" + struct.pack("<II4s", at, len(textures), b"GIMX") + b"".join(directory) + b"".join(records)


def flat(w, h, colour, box=None):
    """Planes of one colour, opaque inside ``box`` (x0, y0, x1, y1; everywhere when None), clear elsewhere."""
    x0, y0, x1, y1 = box or (0, 0, w, h)
    a = b"".join((bytes(x0) + b"\xff" * (x1 - x0) + bytes(w - x1)) if y0 <= y < y1 else bytes(w) for y in range(h))
    return [bytes([colour[c]]) * (w * h) for c in range(3)] + [a]


def over(planes, layer):
    """``layer``'s opaque pixels over ``planes`` (the alpha stays the planes')."""
    return [bytes(q if m else p for p, q, m in zip(planes[c], layer[c], layer[3])) for c in range(3)] + [planes[3]]


def make_picture(stem, textures, models):
    return viv.build_archive([(stem + ".apt", b"APT" + bytes(200)), (stem + ".const", b"CONST" + bytes(27)),
                              (stem + ".o", make_object(models)), (stem + ".xsh", make_bank(textures))])


def entries(big):
    arc = viv.Archive(big)
    return {e.name.rsplit(".", 1)[1]: arc.raw(e) for e in arc.entries}


def records_alike(before, after):
    """The two banks have the same directory and the same record headers (only pixel bytes may differ)."""
    old, new = fa.shpx_textures(before), fa.shpx_textures(after)
    first = min(at for at, _, _, _ in old.values()) - 16
    return (old == new and len(before) == len(after) and before[:first] == after[:first]
            and all(before[at - 16:at] == after[at - 16:at] for at, _, _, _ in old.values()))


def pixels(big, model=0):
    w, h, px = fa.canvas(big, model)
    return w, h, [tuple(px[i:i + 4]) for i in range(0, len(px), 4)]


def mean(colours):
    return tuple(sum(c[k] for c in colours) // max(1, len(colours)) for k in range(3))


def near(p, c, slack=10):
    return max(abs(p[k] - c[k]) for k in range(3)) <= slack


# ------------------------------------------------------------ made-up art

def body_png():
    """256x512: a red top and a blue bottom on a centred 128-wide figure, brighter to the left."""
    rgba = bytearray(256 * 512 * 4)
    for y in range(10, 502):
        for x in range(64, 192):
            shade = 255 - (x - 64)
            c = (shade, shade // 5, shade // 6) if y < 256 else (shade // 6, shade // 4, shade)
            rgba[(y * 256 + x) * 4:(y * 256 + x) * 4 + 4] = bytes(c + (255,))
    return fa.png_bytes(bytes(rgba), 256, 512)


def head_png():
    """256x256: a green disc (the head) over a red block (the shoulders)."""
    rgba = bytearray(256 * 256 * 4)
    for y in range(256):
        for x in range(256):
            if (x - 128) ** 2 + (y - 90) ** 2 <= 70 ** 2:
                c = (60, 200 - abs(x - 128), 70, 255)
            elif y >= 170 and 30 <= x < 226:
                c = (200, 40, 30, 255)
            else:
                continue
            rgba[(y * 256 + x) * 4:(y * 256 + x) * 4 + 4] = bytes(c)
    return fa.png_bytes(bytes(rgba), 256, 256)


def name_png(letters=6):
    """512x64: white blocks for letters, left-aligned, 44 high."""
    rgba = bytearray(512 * 64 * 4)
    for y in range(10, 54):
        for k in range(letters):
            for x in range(4 + k * 40, 4 + k * 40 + 30):
                rgba[(y * 512 + x) * 4:(y * 512 + x) * 4 + 4] = b"\xff\xff\xff\xff"
    return fa.png_bytes(bytes(rgba), 512, 64)


FILES = {fa.BODY: body_png(), fa.HEAD: head_png(), fa.NAME: name_png()}


def reader(files):
    def read(rel):
        if rel not in files:
            raise FileNotFoundError(rel)
        return files[rel]
    return read


ART = fa.load_art(reader(FILES))
SEPIA, COOL = (120, 92, 60), (60, 72, 84)


def striped(w, h):
    """Full-colour planes: red, green and blue bands."""
    band = lambda y: ((200, 40, 40), (40, 180, 60), (50, 60, 200))[(y // 8) % 3]
    return [b"".join(bytes([band(y)[c]]) * w for y in range(h)) for c in range(3)] + [b"\xff" * (w * h)]


def body_picture(colourful=False, stem="p_cf_101_2p"):
    """A select portrait of two textures side by side: box 150x250, textures 64x256 and 128x256."""
    left = striped(64, 256) if colourful else flat(64, 256, SEPIA)
    right = striped(128, 256) if colourful else flat(128, 256, SEPIA)
    return make_picture(stem, {"2": (64, 256, fa.DXT5, left), "1": (128, 256, fa.DXT5, right)},
                        [("5", (0, 0, 150, 250), [("2", 0, 0), ("1", 64, 0)])])


# ------------------------------------------------------------ the pieces

def test_png_round_trip_and_the_art_rules():
    rgba = bytes(range(256)) * 4 * 2
    assert fa.read_png(fa.png_bytes(rgba, 64, 8)) == (64, 8, rgba)
    assert fa.read_art(FILES[fa.BODY], fa.BODY)[:2] == (256, 512)
    with pytest.raises(fa.ArtError, match="must be 256x256"):
        fa.read_art(FILES[fa.BODY], fa.HEAD)
    with pytest.raises(fa.ArtError, match="larger than"):
        fa.read_art(FILES[fa.BODY] + bytes(fa.ART_MAX_BYTES), fa.BODY)
    with pytest.raises(fa.ArtError, match="not a PNG"):
        fa.read_art(b"GIF89a" + bytes(64), fa.NAME)
    interlaced = bytearray(fa.png_bytes(bytes(16), 2, 2))
    interlaced[28] = 1
    with pytest.raises(fa.ArtError):
        fa.read_png(bytes(interlaced))
    with pytest.raises(fa.ArtError, match="empty"):
        fa.load_art(reader({fa.NAME: fa.png_bytes(bytes(512 * 64 * 4), 512, 64)}))
    assert fa.load_art(reader({})) == {}
    assert sorted(fa.load_art(reader({fa.HEAD: FILES[fa.HEAD]}))) == [fa.HEAD]
    assert ART[fa.NAME].box == (4, 10, 234, 54) and ART[fa.BODY].box == (64, 10, 192, 502)


def test_layout_reads_each_model_s_box_and_texture_places():
    obj = make_object([("2", (-20, -30, 44, 34), [("1", -20, -30)]),
                       ("4", (0, 0, 150, 250), [("3", 0, 0), ("7", 128, 0)])])
    assert fa.object_layout(obj) == [
        {"model": "2", "box": (-20, -30, 44, 34), "quads": [("1", -20, -30)]},
        {"model": "4", "box": (0, 0, 150, 250), "quads": [("3", 0, 0), ("7", 128, 0)]}]
    lay = fa.layout(body_picture())
    assert lay["xsh"] == "p_cf_101_2p.xsh" and lay["models"][0]["box"] == (0, 0, 150, 250)
    assert {n: t[1:] for n, t in lay["textures"].items()} == {"2": (64, 256, fa.DXT5), "1": (128, 256, fa.DXT5)}
    with pytest.raises(fa.ArtError):
        fa.object_layout(b"\x7fELF" + bytes(100))
    with pytest.raises(fa.ArtError):
        fa.layout(viv.build_archive([("x.apt", b"1234")]))


def test_families_by_name():
    names = [pattern.replace("_%dp", "_2p") % 101 for pattern, _, _ in nf.PICTURE_FAMILIES]
    assert sorted({fa.family(n)[0] for n in names}) == ["body", "bust", "card", "face", "name"]
    assert ("screens/thumbs/thumb%doff.big", 0, None) in nf.PICTURE_FAMILIES
    assert fa.family("screens/thumbs/thumb100off.big")[2] and not fa.family("thumb100.big")[2]
    assert fa.family("screens/portrait/vp3.big") is None


def test_dxt_blocks_round_trip_closely_and_the_same_every_time():
    w, h = 64, 32
    r = bytes((x * 4) & 255 for y in range(h) for x in range(w))
    g = bytes((y * 8) & 255 for y in range(h) for x in range(w))
    b = bytes(255 - ((x * 4) & 255) for y in range(h) for x in range(w))
    a = bytes(min(255, x * 8) for y in range(h) for x in range(w))
    for kind, slack in ((fa.DXT5, 3), (fa.DXT3, 9)):
        blocks = fa.encode_texture(r, g, b, a, w, h, kind)
        assert len(blocks) == w * h and blocks == fa.encode_texture(r, g, b, a, w, h, kind)
        back = fa.decode_texture(blocks, 0, w, h, kind)
        seen = [i for i in range(w * h) if a[i]]
        assert max(abs(back[i * 4 + c] - p[i]) for i in seen for c, p in enumerate((r, g, b))) <= 17
        assert max(abs(back[i * 4 + 3] - a[i]) for i in range(w * h)) <= slack
    # a kept block is the base's, byte for byte
    base = bytes(range(256)) * (w * h // 256)
    part = fa.encode_texture(r, g, b, a, w, h, fa.DXT5, base, (8, 4, 16, 8))
    changed = [i for i in range(0, w * h, 16) if part[i:i + 16] != base[i:i + 16]]
    assert changed == [(1 * (w // 4) + 2) * 16, (1 * (w // 4) + 3) * 16]


# ------------------------------------------------------------ painting

def test_body_picture_in_one_tone_takes_the_template_s_tone():
    before = body_picture()
    after = fa.paint_picture("screens/portrait/p_CF/p_cf_101_2p.big", before, ART, strict=True)
    assert after != before and len(after) == len(before)
    old, new = entries(before), entries(after)
    assert all(old[k] == new[k] for k in ("apt", "const", "o")) and records_alike(old["xsh"], new["xsh"])
    assert after == fa.paint_picture("screens/portrait/p_CF/p_cf_101_2p.big", before, ART, strict=True)
    w, h, px = pixels(after)
    assert (w, h) == (150, 250)
    assert px[0][3] == 0 and px[w * 100 + 75][3] == 255            # clear beside the figure, opaque on it
    m = mean([p for p in px if p[3] == 255])
    assert m[0] > m[1] > m[2]                                     # sepia like the template
    assert abs(fa._lum(m) - fa._lum(SEPIA)) <= 12                 # and as bright
    top, legs = mean([px[w * 60 + x] for x in range(60, 90)]), mean([px[w * 240 + x] for x in range(60, 90)])
    assert top[0] > top[2] and legs[0] > legs[2]                  # the red top and the blue legs alike
    # the figure spans both textures: painted on both sides of their seam at x = 64, nothing right of the box
    assert all(px[w * 100 + x][3] == 255 for x in range(40, 110))
    right = fa.shpx_textures(new["xsh"])["1"]
    edge = fa.decode_texture(new["xsh"], *right)
    assert not any(edge[(y * 128 + x) * 4 + 3] for y in range(0, 256, 7) for x in range(88, 128))


def test_body_picture_in_full_colour_keeps_the_art_s_colours():
    after = fa.paint_picture("p_cf_101_2p.big", body_picture(colourful=True), ART, strict=True)
    w, h, px = pixels(after)
    top, legs = mean([px[w * 60 + x] for x in range(60, 90)]), mean([px[w * 240 + x] for x in range(60, 90)])
    assert top[0] > top[2] + 40 and legs[2] > legs[0] + 40


def test_pictures_stay_the_template_s_without_their_art_or_when_not_a_picture():
    before = body_picture()
    only_head = fa.load_art(reader({fa.HEAD: FILES[fa.HEAD]}))
    assert fa.paint_picture("p_cf_101_2p.big", before, only_head) == before
    assert fa.paint_picture("screens/portrait/vp3.big", before, ART) == before
    broken = viv.build_archive([("p_cf_101_2p.o", b"\x7fELF" + bytes(80)), ("p_cf_101_2p.xsh", entries(before)["xsh"])])
    assert fa.paint_picture("p_cf_101_2p.big", broken, ART) == broken
    with pytest.raises(fa.ArtError):
        fa.paint_picture("p_cf_101_2p.big", broken, ART, strict=True)


def name_picture(stem, kind, colours):
    """A name picture, box 200x50 in a 256x64 texture: the template's lettering is a bar at x 90..196, y 10..40
    (near the right edge), its left half the first colour and its right half the second."""
    planes = over(flat(256, 64, colours[0], (90, 10, 196, 40)), flat(256, 64, colours[1], (143, 10, 196, 40)))
    return make_picture(stem, {"1": (256, 64, kind, planes)}, [("2", (0, 0, 200, 50), [("1", 0, 0)])])


def test_flat_name_stands_where_the_template_s_does():
    ink = (50, 46, 42)
    before = name_picture("l_CF_100_2p", fa.DXT5, (ink, ink))
    after = fa.paint_picture("screens/portrait/l_CF/l_CF_100_2p.big", before, ART, strict=True)
    assert len(after) == len(before) and records_alike(entries(before)["xsh"], entries(after)["xsh"])
    w, h, px = pixels(after)
    xs = [i % w for i, p in enumerate(px) if p[3] >= 128]
    ys = [i // w for i, p in enumerate(px) if p[3] >= 128]
    assert abs(max(xs) + 1 - 196) <= 1 and abs(min(ys) - 10) <= 1 and abs(max(ys) + 1 - 40) <= 1   # right-aligned
    assert 196 - min(xs) > 106                                    # six letters at that height are wider than the bar
    assert near(mean([p for p in px if p[3] == 255]), ink, 8)
    # a long name is made smaller to fit the box
    long = fa.load_art(reader({fa.NAME: name_png(12)}))
    w, h, px = pixels(fa.paint_picture("l_MS_100_3p.big", name_picture("l_MS_100_3p", fa.DXT5, (SEPIA, SEPIA)), long,
                                       strict=True))
    xs = [i % w for i, p in enumerate(px) if p[3] >= 128]
    ys = [i // w for i, p in enumerate(px) if p[3] >= 128]
    assert min(xs) >= 2 and max(xs) < 198 and max(ys) - min(ys) < 24


@pytest.mark.parametrize("stem,kind", [("l_ML_100_2p", fa.DXT5), ("I_ML_100_2p", fa.DXT3)])
def test_outlined_name_is_light_inside_a_dark_outline(stem, kind):
    light, dark = (230, 210, 170), (40, 30, 24)
    before = name_picture(stem, kind, (dark, light))
    after = fa.paint_picture(stem + ".big", before, ART, strict=True)
    assert records_alike(entries(before)["xsh"], entries(after)["xsh"])
    w, h, px = pixels(after)
    shown = [p for p in px if p[3] >= 200]
    bright = [p for p in shown if fa._lum(p) > 150]
    murky = [p for p in shown if fa._lum(p) < 80]
    assert len(bright) > 500 and len(murky) > 300
    assert near(mean(bright), light, 24)
    # the outline surrounds the lettering: the first opaque pixel of a row through the letters is dark
    first = next(p for p in (px[w * 25 + x] for x in range(w)) if p[3] >= 200)
    assert fa._lum(first) < 110


def face_picture(stem, inset, frame, models=1):
    """Small head pictures: box 40x40 at the corner of 64x64 textures; a frame ``inset`` wide, a black
    background inside it and a square as the template's head (grey-blue; sepia in a second picture)."""
    textures, shapes = {}, []
    for k in range(models):
        planes = over(flat(64, 64, frame), flat(64, 64, (0, 0, 0), (inset, inset, 40 - inset, 40 - inset)))
        planes = over(planes, flat(64, 64, SEPIA if k else COOL, (12, 10, 30, 34)))
        textures[str(2 * k + 1)] = (64, 64, fa.DXT5, planes)
        shapes.append((str(2 * k + 2), (0, 0, 40, 40), [(str(2 * k + 1), 0, 0)]))
    return make_picture(stem, textures, shapes)


def test_small_head_picture_keeps_its_frame_and_background():
    before = face_picture("t_CT_100", 3, (150, 150, 130), models=2)
    after = fa.paint_picture("screens/portrait/t_CT/t_CT_100.big", before, ART, new=True, strict=True)
    old, new = entries(before), entries(after)
    assert records_alike(old["xsh"], new["xsh"]) and old["o"] == new["o"]
    for model in (0, 1):
        w, h, was = pixels(before, model)
        _, _, px = pixels(after, model)
        frame = [i for i in range(w * h) if not (3 <= i % w < 37 and 3 <= i // w < 37)]
        assert all(px[i] == was[i] for i in frame)                 # the frame: the template's bytes (no NEW here)
        assert all(p[3] == 255 for p in px)
        assert px[w * 5 + 5][:3] == (0, 0, 0)                       # the background beside the head
        assert sum(1 for i in range(w * h) if px[i] != was[i]) > 300
    # the second picture (the "not chosen" state) is in one tone, the template's sepia
    _, _, second = pixels(after, 1)
    face = mean([second[40 * y + x] for y in range(12, 20) for x in range(16, 24)])
    assert face[0] > face[1] > face[2]
    # the blocks outside the box are the template's bytes
    at = fa.layout(before)["textures"]["1"][0]
    assert old["xsh"][at + 12 * 16 * 16:at + 4096] == new["xsh"][at + 12 * 16 * 16:at + 4096]


def test_new_stamp_only_on_the_thumbs_and_only_when_new():
    for stem in ("thumb100", "thumb100off"):
        before = face_picture(stem, 2, (0, 0, 0))
        plain = fa.paint_picture("screens/thumbs/%s.big" % stem, before, ART, strict=True)
        stamped = fa.paint_picture("screens/thumbs/%s.big" % stem, before, ART, new=True, strict=True)
        assert plain != stamped and len(plain) == len(stamped) == len(before)
        w, h, px = pixels(stamped)
        _, _, was = pixels(plain)
        badge = [px[w * y + x][:3] for y in range(fa.NEW_SIZE[1]) for x in range(fa.NEW_SIZE[0])]
        assert all(near(p, fa.NEW_BADGE) or near(p, fa.NEW_INK) for p in badge)
        assert sum(near(p, fa.NEW_INK) for p in badge) == sum(row.count("X") for row in fa.NEW_GLYPHS)
        assert near(px[0], fa.NEW_BADGE) and near(px[w + 1], fa.NEW_INK)       # the N's first pixel
        assert not any(near(p[:3], fa.NEW_BADGE) for p in was)
        outside = [i for i in range(w * h) if not (i % w < fa.NEW_SIZE[0] and i // w < fa.NEW_SIZE[1])]
        assert all(px[i] == was[i] for i in outside)               # nothing but the badge differs
    planes = [bytearray(32 * 16) for _ in range(4)]
    fa.stamp_new(*planes, 32, 4, 4)
    assert planes[3].count(255) == fa.NEW_SIZE[0] * fa.NEW_SIZE[1] and planes[0][4 * 32 + 4] == fa.NEW_BADGE[0]


def test_loading_card_has_the_body_and_the_name():
    card = make_picture("101_2p", {"1": (256, 256, fa.DXT5, flat(256, 256, (40, 44, 48), (0, 0, 80, 250)))},
                        [("2", (0, 0, 200, 250), [("1", 0, 0)])])
    with_name = fa.paint_picture("screens/portrait/101_2p.big", card, ART, strict=True)
    body_only = fa.paint_picture("screens/portrait/101_2p.big", card,
                                 fa.load_art(reader({fa.BODY: FILES[fa.BODY]})), strict=True)
    assert len(with_name) == len(body_only) == len(card) and with_name != body_only != card
    w, h, px = pixels(with_name)
    _, _, bare = pixels(body_only)
    corner = [i for i in range(w * h) if i % w >= 120 and i // w < 40]
    assert sum(1 for i in corner if px[i][3] >= 128) > 200 and not any(bare[i][3] for i in corner if i % w >= 150)
    assert all(px[i] == bare[i] for i in range(w * h) if i // w >= 48)         # the name is in the top corner only
    assert sum(1 for p in bare if p[3] == 255) > 5000


def test_bust_takes_the_head_picture():
    bust = make_picture("p_ML_101_3p", {"1": (128, 128, fa.DXT5, flat(128, 128, SEPIA))},
                        [("2", (-60, -120, 60, 0), [("1", -60, -120)])])
    after = fa.paint_picture("p_ML_101_3p.big", bust, ART, strict=True)
    w, h, px = pixels(after)
    assert (w, h) == (120, 120) and px[0][3] == 0 and px[w * 40 + 60][3] == 255
    assert fa.paint_picture("p_ML_101_3p.big", bust, fa.load_art(reader({fa.BODY: FILES[fa.BODY]}))) == bust


def test_hub_portrait():
    png = fa.hub_portrait(FILES[fa.BODY])
    w, h, rgba = fa.read_png(png)
    assert (w, h) == fa.HUB_SIZE == (94, 193) and png == fa.hub_portrait(ART[fa.BODY])
    shown = [tuple(rgba[i:i + 3]) for i in range(0, len(rgba), 4) if rgba[i + 3] == 255]
    m = mean(shown)
    assert len(shown) > 5000 and m[2] > m[0] and rgba[3] == 0      # the stock cool tone, clear beside the figure
    warm = fa.hub_portrait(FILES[fa.BODY], make_picture("p_CT_56", {"1": (128, 256, fa.DXT5, flat(128, 256, SEPIA))},
                                                        [("2", (0, 0, 101, 195), [("1", 0, 0)])]))
    _, _, rgba = fa.read_png(warm)
    m = mean([tuple(rgba[i:i + 3]) for i in range(0, len(rgba), 4) if rgba[i + 3] == 255])
    assert m[0] > m[2]
    with pytest.raises(fa.ArtError):
        fa.hub_portrait(FILES[fa.HEAD])


# ------------------------------------------------------------ the composer's side

def art_pack(folder, files=None, fe_id=None, template=55):
    folder = Path(folder)
    (folder / "art").mkdir(parents=True)
    for rel, data in (FILES if files is None else files).items():
        (folder / rel).write_bytes(data)
    (folder / "install.json").write_text(json.dumps({
        "schema": 1, "type": "new-fighter", "entries": [],
        "ops": [nf.make_op(template, "V2IP_109A", "V2IP_7K3A", "Night Owl", fe_id)]}), encoding="utf-8")
    return folder


def test_pack_reader_gives_the_art_files(tmp_path):
    folder = art_pack(tmp_path / "p", {fa.BODY: FILES[fa.BODY]})
    read = cc.pack_reader(folder)
    assert read(fa.BODY) == FILES[fa.BODY]
    with pytest.raises(FileNotFoundError):
        read(fa.HEAD)
    (folder / "art" / "name.png").write_bytes(bytes(fa.ART_MAX_BYTES + 1))
    with pytest.raises(cc.ComposeError, match="too large"):
        read(fa.NAME)
    for rel in ("art/other.png", "art/../install.json", "install.json", "art/body.PNG"):
        with pytest.raises(cc.ComposeError):
            read(rel)
    assert sorted(fa.load_art(cc.pack_reader(art_pack(tmp_path / "q")))) == sorted(FILES)


def test_new_packs_and_the_inputs_key():
    today = datetime.date(2026, 11, 13)

    def pack(pack_id, reviewed, place=101):
        return {"id": pack_id, "version": "1.0.0", "content_sha256": "0" * 64, "fighter_fe_id": place,
                "reviewed": reviewed}

    packs = [pack("fresh", "2026-11-12"), pack("today", "2026-11-13"), pack("last-day", "2026-10-31"),
             pack("old", "2026-10-30"), pack("undated", ""), pack("odd", "soon"), pack("venue", "2026-11-12", 0),
             pack("ahead", "2026-11-14"), pack("far-ahead", "2026-11-20")]
    assert cc.new_packs(packs, today) == {"fresh", "today", "last-day", "ahead"}
    assert cc.new_packs(packs, datetime.date(2026, 11, 14)) == {"fresh", "today", "ahead"}
    assert cc.new_packs([{"id": "no-date", "fighter_fe_id": 101}], today) == set()
    assert cc.NEW_DAYS == 14 and cc.COMPOSER == 6     # 6: build 85, skins
    plain = cc.inputs_key("game", packs, [])
    assert plain == cc.inputs_key("game", packs, [], (), set()) != cc.inputs_key("game", packs, [], (), {"fresh"})
    assert cc.inputs_key("game", packs, [], (), {"a", "b"}) == cc.inputs_key("game", packs, [], (), ["b", "a"])


def test_hub_portraits_are_written_kept_and_removed(tmp_path):
    data = tmp_path / "data"
    one = {"id": "owl", "folder": art_pack(tmp_path / "owl"), "fighter_fe_id": 101}
    bare = {"id": "bare", "folder": art_pack(tmp_path / "bare", {fa.HEAD: FILES[fa.HEAD]}), "fighter_fe_id": 102}
    bad = {"id": "bad", "folder": art_pack(tmp_path / "bad", {fa.BODY: FILES[fa.HEAD]}), "fighter_fe_id": 103}
    by_hand = {"id": "hand", "folder": art_pack(tmp_path / "hand", fe_id=110), "fighter_fe_id": 0}
    folder = data / "online-art" / "fighters"
    folder.mkdir(parents=True)
    (folder / "60.png").write_bytes(b"stock")
    (folder / "120.png").write_bytes(b"stale")

    def no_template(entry):
        raise KeyError(entry)

    there = cc.hub_portraits(data, [one, bare, bad, by_hand], no_template)
    assert there == [[101, "owl"], [110, "hand"]]
    assert sorted(p.name for p in folder.iterdir()) == ["101.png", "110.png", "60.png"]
    assert (folder / "101.png").read_bytes() == fa.hub_portrait(FILES[fa.BODY])
    assert fa.read_png((folder / "110.png").read_bytes())[:2] == (94, 193)
    # the same inputs again: a portrait that is there is not drawn again, a missing one is
    (folder / "101.png").write_bytes(b"kept")
    (folder / "110.png").unlink()
    assert cc.hub_portraits(data, [one, by_hand], no_template, there) == there
    assert (folder / "101.png").read_bytes() == b"kept" and (folder / "110.png").is_file()
    assert cc.hub_portraits(data, [one, by_hand], no_template)[0] == [101, "owl"]
    assert (folder / "101.png").read_bytes() != b"kept"
    assert cc.hub_portraits(data, [], no_template) == []
    assert sorted(p.name for p in folder.iterdir()) == ["60.png"]
    assert cc.hub_portraits(tmp_path / "none", [], no_template) == [] and not (tmp_path / "none").exists()


def test_online_art_rebuild_keeps_the_new_fighters_portraits(tmp_path):
    spec = importlib.util.spec_from_file_location("online_art", ROOT / "scripts" / "online-art.py")
    art = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(art)
    old, new = tmp_path / "online-art", tmp_path / "online-art.tmp"
    (old / "fighters").mkdir(parents=True)
    (new / "fighters").mkdir(parents=True)
    for name in ("60.png", "99.png", "100.png", "127.png", "128.png"):
        (old / "fighters" / name).write_bytes(name.encode())
    assert art.keep_new_fighters(old, new) == [100, 127]
    assert sorted(p.name for p in (new / "fighters").iterdir()) == ["100.png", "127.png"]
    assert (new / "fighters" / "127.png").read_bytes() == b"127.png"
    assert art.keep_new_fighters(tmp_path / "nowhere", new) == []


# ------------------------------------------------------------ the user's own dump

def _game():
    for base in (os.environ.get("DEFJAM_DATA"), str(ROOT / "game" / "..")):
        if base and (Path(base) / "extracted" / "assets" / "djv2.sod").is_file():
            return Path(base) / "extracted"
    if (ROOT / "game" / "assets" / "djv2.sod").is_file():
        return ROOT / "game"
    return None


GAME = _game()
dump = pytest.mark.skipif(GAME is None, reason="needs the user's own dump")


@dump
@pytest.mark.parametrize("template", [60, 0])
def test_every_family_copy_of_a_stock_fighter_is_painted(template):
    screens = viv.Archive(str(GAME / "screens" / "screens.viv"))
    copies = nf.picture_copies(screens, template, 100)
    assert len(copies) == 28
    started = time.perf_counter()
    painted = [(n, fa.paint_picture(n, data, ART, new=True, strict=True)) for n, data in copies]
    assert time.perf_counter() - started < 5.0
    for (name, before), (_, after) in zip(copies, painted):
        assert len(after) == len(before) and after != before, name
        old, new = entries(before), entries(after)
        assert all(old[k] == new[k] for k in old if k != "xsh"), name
        assert records_alike(old["xsh"], new["xsh"]), name
        assert fa.canvas(after)[2] != fa.canvas(before)[2], name
    assert painted == [(n, fa.paint_picture(n, data, ART, new=True, strict=True)) for n, data in copies]
    plain = dict((n, fa.paint_picture(n, data, ART, strict=True)) for n, data in copies)
    differ = sorted(n.rsplit("/", 1)[1] for n, data in painted if plain[n] != data)
    assert differ == ["thumb100.big", "thumb100off.big"]


@dump
def test_compose_paints_stamps_and_draws_the_hub_portrait(tmp_path):
    import test_new_fighter_pack as packs
    today = datetime.date(2026, 11, 13)

    def run(name, art, reviewed):
        pack = packs.make_pack(tmp_path / name / "pack", "V2IP_7K3A", "Night Owl")
        if art:
            (pack / "art").mkdir()
            for rel, blob in FILES.items():
                (pack / rel).write_bytes(blob)
        record = dict(packs.record(pack, "night-owl", 1, 101), reviewed=reviewed)
        data = tmp_path / name / "data"
        got = cc.compose(data, GAME, [record], off=(), log=lambda t: None, languages=[], today=today)
        assert got["applied"] == ["night-owl"] and not got["skipped"], got
        return data, viv.Archive(str(packs.composed(data, nf.SCREENS_ARCHIVE))), record

    names = [n for n, _ in nf.picture_copies(viv.Archive(str(GAME / "screens" / "screens.viv")), packs.TEMPLATE, 101)]
    _, bare, _ = run("bare", False, "2026-11-12")
    data, old, record = run("old", True, "2026-10-01")
    _, fresh, _ = run("fresh", True, "2026-11-12")
    _, again, _ = run("again", True, "2026-11-12")
    assert len(names) == 28
    for n in names:
        assert len(bare.read(n)) == len(old.read(n)) == len(fresh.read(n)), n
        assert bare.read(n) != old.read(n), n                       # every copy is painted
        assert fresh.read(n) == again.read(n), n                    # the same bytes in another data folder
    stamped = sorted(n.rsplit("/", 1)[1] for n in names if old.read(n) != fresh.read(n))
    assert stamped == ["thumb101.big", "thumb101off.big"]
    hub = data / "online-art" / "fighters" / "101.png"
    assert fa.read_png(hub.read_bytes())[:2] == (94, 193)
    assert not (tmp_path / "bare" / "data" / "online-art").exists()
    # unchanged inputs: nothing is composed again and a lost portrait comes back
    hub.unlink()
    got = cc.compose(data, GAME, [record], off=(), log=lambda t: None, languages=[], today=today)
    assert got["unchanged"] and hub.is_file()
    # the NEW set is an input: a pack that becomes new, and the day its stamp runs out, compose again
    record["reviewed"] = "2026-11-01"
    assert not cc.compose(data, GAME, [record], off=(), log=lambda t: None, languages=[], today=today)["unchanged"]
    assert cc.compose(data, GAME, [record], off=(), log=lambda t: None, languages=[],
                      today=today + datetime.timedelta(days=1))["unchanged"]
    assert not cc.compose(data, GAME, [record], off=(), log=lambda t: None, languages=[],
                          today=today + datetime.timedelta(days=2))["unchanged"]
    cc.compose(data, GAME, [], off=(), log=lambda t: None, languages=[], today=today)
    assert not hub.exists()


@dump
def test_art_of_the_wrong_size_skips_the_pack(tmp_path):
    import test_new_fighter_pack as packs
    pack = packs.make_pack(tmp_path / "pack", "V2IP_7K3A", "Night Owl")
    (pack / "art").mkdir()
    (pack / "art" / "head.png").write_bytes(FILES[fa.BODY])
    got = cc.compose(tmp_path / "data", GAME, [packs.record(pack, "night-owl", 1, 101)], off=(),
                     log=lambda t: None, languages=[])
    assert got["applied"] == [] and "art/head.png must be 256x256" in got["skipped"][0][1]

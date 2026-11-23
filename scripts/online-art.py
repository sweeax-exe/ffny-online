#!/usr/bin/env python3
"""Local-only: the game's own art and fonts for the online screens, from the player's dump.

The online screens (lobby, room, fighter choice, venue vote) are drawn by the port, in the
game's look: its fonts, fighter portraits, venue thumbnails, the ONLINE poster EA left in
the main menu's assets, backgrounds and button glyphs. This reads them from the player's own
`screens/screens.viv` and `fonts/fonts.viv` and writes PNG and TTF files to
`<data>/online-art/`, which only the player's own copy of the port reads. Nothing here or in
the repository contains game bytes: only archive entry names and image names.

    python scripts/online-art.py [--data <DEFJAM_DATA>] [--check | --repair]

Layout of the output: fighters/<id>.png (the choose-character portrait without its padding; `id` is the
Battle-mode ID, the front-end ID of the fighter's record in assets/djv2.sod: the fighter.xml ID except
for Teck 85, Santos 93, O.E 92 and Eric Sermon 91), venues/<feID>.png (venue.xml feID: its photo with
its sign, 256x256) and venues/<feID>_name.png (the gold name banner), ui/<name>.png, fonts/<file>.ttf,
ui/hud_fight.png and ui/hud_grain.png (the fight HUD's FIGHT! lettering from assets/misc.viv and a tile of its
wear, for the start countdown and the PERFECT banner),
and manifest.json. ui/ps_cross.png, ps_circle.png, ps_square.png and ps_triangle.png are not from the
dump: this script draws them (PlayStation-style glyphs for the "Button icons" setting). ranks/em<N>-256.png and
em<N>-64.png are not from the dump either: the port's own rank emblems, rendered by scripts/rank-emblems.py. Needs Pillow (the installed Python has it for the HD textures).
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hd_assets  # noqa: E402

VERSION = 8       # 5: the PlayStation-style glyphs ui/ps_*.png; 6: the rank emblems ranks/*.png;
                  # 7: the fight HUD's FIGHT! lettering and its grain (ui/hud_fight.png, ui/hud_grain.png), TeaChest.ttf
                  # 8: Faction War's two bosses, factions/dmob.png and factions/crow.png

# (output name, screens.viv entry, SHPX image name). See docs/research/frontend-menu-integration.md §6.
UI_IMAGES = [
    ("online_poster", "screens/intMain/support/mainMenuButtons.big", "15"),
    ("online_icon", "screens/intMain/support/mainMenuLogosIcons.big", "17"),
    ("button_a", "screens/xHead.big", "15"),
    ("button_y", "screens/xHead.big", "17"),
    ("button_b", "screens/xHead.big", "19"),
    ("button_x", "screens/xHead.big", "21"),
    ("header_strip", "screens/xHead.big", "7"),
    ("menu_wall", "screens/intMain/mainMenu.big", "5"),
    ("menu_door", "screens/intMain/mainMenu.big", "53"),
    ("door_shutter", "screens/intMain/mainMenu.big", "18"),
    ("paper_sign", "screens/options/options.big", "4"),
    ("paper_edge", "screens/options/options.big", "7"),
    ("selector", "screens/battle/cmtype.big", "10"),
    ("label_red", "screens/options/options.big", "26"),
    ("glow", "screens/intMain/support/mainMenuButtons.big", "77"),
]
MATCH_LOGO = "screens/match/2_type/mTypeLogo_%s_e.big"
# Faction War (build 108): the two crews' bosses as CHOOSE FIGHTER shows them large, the full-length render with the
# name lettering (screens/portrait/<Battle ID + 1>_2p.big, 512x512): D-Mob (59) and Crow (58).
FACTION_RENDERS = [("dmob", 59), ("crow", 58)]
FONTS = ["TradeBld.ttf", "TradeCon.ttf", "SchwartzHand_Bold2.ttf", "FERRO___.TTF",
         "TradeGothicLTStd-BoldExt.ttf", "AachenStd-Bold.ttf", "TeaChest.ttf"]
# The fight HUD's call-outs (assets/misc.viv, hud.xsh): one 512x256 texture "Over" with the words the game
# shows over a fight in white distressed lettering (DOUBLE, OVERTIME, SUBMIT, TOTALED!, KO, OUT, RING,
# FIGHT!). FIGHT! is the last word of its bottom row; the box is cut down to the lettering's own edges.
HUD_ENTRY, HUD_CALLOUTS = "hud.xsh", "Over"
HUD_FIGHT_BOX = (333, 176, 512, 256)


def default_data():
    return os.environ.get("DEFJAM_DATA") or str(Path(__file__).resolve().parents[2] / "data")


def inner_files(entry_blob):
    """The files inside one RefPack-wrapped screen BIGF."""
    return dict(hd_assets.archive_entries(hd_assets.unpack_refpack(entry_blob)))


def images(entry_blob):
    """{SHPX image name: PIL image} of a screen entry's texture bank."""
    out = {}
    for name, data in inner_files(entry_blob).items():
        if name.lower().endswith(".xsh"):
            for record, image in hd_assets.shpx_images(data):
                if image is not None:
                    out[record["name"].strip()] = image
    return out


def trim_padding(image, threshold=3.0):
    """Cut the texture padding off the right and bottom: rows and columns the encoder filled by
    repeating the last real one (they differ from their neighbour by almost nothing)."""
    from PIL import ImageChops, ImageStat
    w, h = image.size
    rgb = image.convert("RGB")

    def column(x):
        return rgb.crop((x, 0, x + 1, h))

    def row(y):
        return rgb.crop((0, y, w, y + 1))

    right = w
    while right > w // 2 and sum(ImageStat.Stat(ImageChops.difference(column(right - 1), column(right - 2))).mean) < threshold:
        right -= 1
    bottom = h
    while bottom > h // 2 and sum(ImageStat.Stat(ImageChops.difference(row(bottom - 1), row(bottom - 2))).mean) < threshold:
        bottom -= 1
    return image.crop((0, 0, right, bottom))


# PlayStation-style button glyphs for the "Button icons" setting (ui.button_icons): drawn here, not
# taken from anywhere, in the round-badge style of the dump's Xbox glyphs (button_a.png etc.: a
# 32x32 texture with a shaded disc of about 21 px at its top left and a black rim). A dark disc with
# the symbol in its classic colour, as on the pad. Mapping: A cross, B circle, X square, Y triangle.
PS_GLYPHS = [("ps_cross", "cross", (110, 168, 238)), ("ps_circle", "circle", (255, 96, 104)),
             ("ps_square", "square", (246, 120, 220)), ("ps_triangle", "triangle", (70, 222, 170))]


def ps_glyph(shape, colour, size=32, scale=8):
    """One 32x32 RGBA glyph: drawn `scale` times larger, then reduced (smooth edges)."""
    from PIL import Image, ImageDraw
    big = size * scale
    cx = cy = 10.5 * scale              # the Xbox badge's centre and radius in the same texture
    r_out, r_rim, r_in = 10.5 * scale, 9.4 * scale, 8.4 * scale
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.ellipse((cx - r_out, cy - r_out, cx + r_out, cy + r_out), fill=(0, 0, 0, 255))
    # a light rim inside the black edge, so the dark disc stands out on the hub's dark walls
    draw.ellipse((cx - r_rim, cy - r_rim, cx + r_rim, cy + r_rim), fill=(150, 150, 160, 255))
    # the disc: lit at the top left, darker towards the bottom right, as the Xbox badges are
    steps = 24
    for i in range(steps):
        t = i / (steps - 1)
        r = r_in * (1 - 0.55 * t)
        ox, oy = cx - 0.22 * r_in * t, cy - 0.26 * r_in * t
        shade = int(40 + 56 * t)
        draw.ellipse((ox - r, oy - r, ox + r, oy + r), fill=(shade, shade, shade + 6, 255))
    stroke = 2.6 * scale
    col = colour + (255,)
    if shape == "cross":
        d = 4.4 * scale
        for a, b in (((-d, -d), (d, d)), ((-d, d), (d, -d))):
            draw.line((cx + a[0], cy + a[1], cx + b[0], cy + b[1]), fill=col, width=int(stroke))
        for x, y in ((-d, -d), (d, d), (-d, d), (d, -d)):     # round line ends
            draw.ellipse((cx + x - stroke / 2, cy + y - stroke / 2, cx + x + stroke / 2, cy + y + stroke / 2), fill=col)
    elif shape == "circle":
        r = 5.0 * scale
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), outline=col, width=int(stroke))
    elif shape == "square":
        h = 4.4 * scale
        draw.rectangle((cx - h, cy - h, cx + h, cy + h), outline=col, width=int(stroke))
    else:   # triangle, its centre of mass on the disc's centre
        h = 5.6 * scale
        top, left, right = (cx, cy - h), (cx - h * 0.95, cy + h * 0.62), (cx + h * 0.95, cy + h * 0.62)
        draw.line((top, left, right, top, left), fill=col, width=int(stroke), joint="curve")
    return image.reduce(scale)          # box average: smooth edges, nothing outside the badge


def write_ps_glyphs(ui_dir):
    """Writes ui/ps_*.png; returns their names."""
    for name, shape, colour in PS_GLYPHS:
        ps_glyph(shape, colour).save(Path(ui_dir) / (name + ".png"))
    return [name for name, _, _ in PS_GLYPHS]


def hud_callouts(misc_viv):
    """The fight HUD's call-out texture (RGBA, 512x256) from assets/misc.viv, or None."""
    for label, blob in hd_assets.walk(Path(misc_viv).read_bytes(), "misc.viv"):
        if label.lower().endswith("::" + HUD_ENTRY) and blob[:4] == b"SHPX":
            for record, image in hd_assets.shpx_images(blob):
                if record["name"].strip() == HUD_CALLOUTS and image is not None and image.size == (512, 256):
                    return image.convert("RGBA")
    return None


def hud_fight(callouts):
    """The game's FIGHT! lettering, cut to its edges, or None when the texture is not the one expected."""
    word = callouts.crop(HUD_FIGHT_BOX)
    box = word.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
    if not box:
        return None
    word = word.crop(box)
    w, h = word.size
    return word if 140 <= w <= 180 and 60 <= h <= 80 else None


def hud_grain(callouts, size=256):
    """A tile of the call-outs' wear: the dark specks and scratches inside their white letters, as
    white with the darkness for alpha, gathered from shifted copies of the texture until the tile is
    covered (the letters leave gaps between them). The port tints it black over its own lettering
    (the countdown's numbers, PERFECT) so they wear the same way, and grey over the plate behind."""
    from PIL import Image, ImageChops, ImageFilter
    w, h = callouts.size
    wrapped = Image.new("RGBA", (w * 2, h * 2))
    for x in (0, w):
        for y in (0, h):
            wrapped.paste(callouts, (x, y))
    # well inside the letters: their shaded edges would draw the letters' outlines into the tile
    inside = wrapped.getchannel("A").point(lambda v: 255 if v > 230 else 0).filter(ImageFilter.MinFilter(9))
    dark = wrapped.convert("L").point(lambda v: max(0, min(255, (205 - v) * 3)))
    grain = Image.new("L", (size, size), 0)
    covered = Image.new("L", (size, size), 0)
    for k in range(48):
        x, y = (k * 197) % w, (k * 89) % h
        part = inside.crop((x, y, x + size, y + size))
        free = ImageChops.multiply(part, ImageChops.invert(covered))
        grain.paste(dark.crop((x, y, x + size, y + size)), (0, 0), free)
        covered = ImageChops.lighter(covered, part)
    tile = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    tile.putalpha(grain)
    return tile


def write_hud_art(data, ui_dir):
    """ui/hud_fight.png and ui/hud_grain.png from the player's assets/misc.viv; returns the names written
    and the ones missing."""
    misc = Path(data) / "extracted" / "assets" / "misc.viv"
    if not misc.is_file():
        return [], ["assets/misc.viv"]
    callouts = hud_callouts(misc)
    if callouts is None:
        return [], ["assets/misc.viv#%s/%s" % (HUD_ENTRY, HUD_CALLOUTS)]
    done, missing = [], []
    fight = hud_fight(callouts)
    if fight is None:
        missing.append("hud_fight")
    else:
        fight.save(Path(ui_dir) / "hud_fight.png")
        done.append("hud_fight")
    hud_grain(callouts).save(Path(ui_dir) / "hud_grain.png")
    done.append("hud_grain")
    return done, missing


def maybe_refpack(blob):
    return hd_assets.unpack_refpack(blob) if blob[:2] in (b"\x10\xfb", b"\x11\xfb", b"\x90\xfb", b"\x91\xfb") else blob


# setup/ffnycontent/newfighter.py PLACES (this script runs without the setup package on its path)
NEW_FIGHTER_PLACES = tuple(range(100, 128)) + (5, 15, 29, 37, 39, 42, 49, 53, 63, 66, 79, 86, 87, 88, 89, 90, 94, 95,
                                               96, 97)


FIRST_LOGICAL = 1000    # setup/ffnycontent/newfighter.py: community fighters beyond the places, by number


def keep_new_fighters(old, new):
    """The community new fighters' hub portraits (fighters/<id>.png: the places of setup/ffnycontent/newfighter.py
    PLACES, 100..127 and 20 low IDs no stock fighter has, and the numbered fighters beyond them, 1000 and up) are
    not from the dump: the launcher's composer (setup/content_compose.py) draws them from the packs' own art, so
    a rebuild carries them over from the folder it replaces (an update re-runs this script; without it every
    community portrait was gone until the next composition drew it again). Returns the IDs kept."""
    kept = []
    source = Path(old) / "fighters"
    if not source.is_dir():
        return kept
    for picture in sorted((p for p in source.glob("*.png") if p.stem.isdigit()), key=lambda p: int(p.stem)):
        if picture.is_symlink() or not picture.is_file():
            continue
        fid = int(picture.stem)
        if fid in NEW_FIGHTER_PLACES or fid >= FIRST_LOGICAL:
            (Path(new) / "fighters").mkdir(parents=True, exist_ok=True)
            shutil.copyfile(picture, Path(new) / "fighters" / picture.name)
            kept.append(fid)
    return kept


def keep_skin_pictures(old, new):
    """skins/<pack id>.png, the picture of each applied community skin, is the composer's too (see
    keep_new_fighters), and nothing here writes the folder: a rebuild carries it over whole. Returns the names kept."""
    kept = []
    source = Path(old) / "skins"
    if not source.is_dir():
        return kept
    for picture in sorted(source.glob("*.png")):
        if picture.is_file() and not picture.is_symlink():
            (Path(new) / "skins").mkdir(parents=True, exist_ok=True)
            shutil.copyfile(picture, Path(new) / "skins" / picture.name)
            kept.append(picture.name)
    return kept


def write_faction_renders(entries, tmp, manifest):
    """factions/<faction>.png: the bosses' large CHOOSE FIGHTER renders (Faction War's picker and page)."""
    manifest.setdefault("factions", [])
    for name, fid in FACTION_RENDERS:
        entry = "screens/portrait/%d_2p.big" % (fid + 1)
        imgs = images(entries[entry]) if entry in entries else None
        if not imgs:
            manifest["missing"].append(entry)
            continue
        (tmp / "factions").mkdir(parents=True, exist_ok=True)
        next(iter(imgs.values())).save(tmp / "factions" / ("%s.png" % name))
        manifest["factions"].append(name)


# ---- the folder's swap and repair (build 110) --------------------------------------------------------
# Builds before 110 removed online-art and then renamed online-art.tmp to it: a file of either folder held open by
# another program (a virus scanner, a viewer, a game still running) left the player with half of the art or none of
# it, the finished new art stranded in online-art.tmp, until the next update. Now the old art stays until the new
# one is in place, and the launcher's start repairs what is missing (setup/mods.py sync runs --repair).

STATUS = "online-art-status.ini"        # <data>: the last build's or repair's outcome; the game logs it (F9 reports)
RETRIES = (0.1, 0.2, 0.4, 0.8, 1.5, 2.0)    # seconds between the tries of a rename another program blocks
PNG_HEAD, PNG_END = b"\x89PNG\r\n\x1a\n", b"\x00\x00\x00\x00IEND\xaeB`\x82"


class ArtError(RuntimeError):
    """The art could not be put in place; what was there before still is."""


def sibling(out, kind):
    """The folders beside online-art: .tmp* (new art) and .old* (the art it replaces), the plain name first."""
    return sorted(p for p in out.parent.glob("%s.%s*" % (out.name, kind))
                  if re.fullmatch(re.escape("%s.%s" % (out.name, kind)) + r"\d?", p.name))


def free_sibling(out, kind):
    """A free name for a new .tmp / .old folder. A leftover .tmp goes first (one that cannot be removed keeps its
    name); an .old may be the only art there is, so it goes only when every name is taken."""
    paths = [out.with_name("%s.%s%s" % (out.name, kind, n)) for n in [""] + list(range(1, 10))]
    for clear in ((True,) if kind == "tmp" else (False, True)):
        for path in paths:
            if clear:
                discard(path)
            if not os.path.lexists(path):
                return path
    raise ArtError("the folders %s.%s* cannot be removed" % (out.name, kind))


def is_link(path):
    """A symlink or a junction (a test root's online-art is a junction to the real one: never touched)."""
    try:
        info = os.lstat(path)
    except OSError:
        return False
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info, "st_file_attributes", 0) & 0x400)


def discard(path):
    """Remove a folder no longer wanted; what another program holds open stays."""
    if not is_link(path):
        shutil.rmtree(path, ignore_errors=True)


def picture_ok(path):
    """A whole PNG: a file a crash or a power cut left empty or zero-filled is not one."""
    with open(path, "rb") as f:
        if f.read(8) != PNG_HEAD:
            return False
        f.seek(0, os.SEEK_END)
        if f.tell() < 8 + len(PNG_END):
            return False
        f.seek(-len(PNG_END), os.SEEK_END)
        return f.read() == PNG_END


def read_manifest(folder):
    """The folder's manifest.json (written last, so a folder with one was finished), or None."""
    try:
        m = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return m if isinstance(m, dict) else None


def faults(folder):
    """What is wrong with an art folder: [] for a complete one of this version, else the first missing files."""
    m = read_manifest(folder)
    if m is None:
        return ["manifest.json"]
    if m.get("version") != VERSION:
        return ["version %s" % m.get("version")]
    names = ["%s/%s.png" % (key, n) for key in ("fighters", "venues", "ui", "modes", "factions") for n in m.get(key, [])]
    names += ["fonts/%s" % n for n in m.get("fonts", [])]
    names += ["ranks/em%d-%d.png" % (n, size) for n in range(1, int(m.get("ranks") or 0) + 1) for size in (256, 64)]
    names += ["towers/tower%d-%d.png" % (n, size) for n in range(1, int(m.get("towers") or 0) + 1) for size in (512, 128)]
    bad = []
    for name in names:
        try:
            ok = picture_ok(folder / name) if name.endswith(".png") else (folder / name).stat().st_size > 0
        except OSError:
            ok = False
        if not ok:
            bad.append(name)
    return bad or ([] if names else ["empty manifest"])


def retry(action, *args):
    for wait in RETRIES:
        try:
            return action(*args)
        except OSError:
            time.sleep(wait)
    return action(*args)


def same_bytes(a, b):
    try:
        return os.path.getsize(a) == os.path.getsize(b) and a.read_bytes() == b.read_bytes()
    except OSError:
        return False


def merge(src, dst):
    """Every file of src into dst, one by one (each whole or not at all), the manifest last; nothing of dst is
    removed. A file that cannot be replaced keeps its old content and is named in the ArtError."""
    failed = []
    files = sorted((p for p in src.rglob("*") if p.is_file()), key=lambda p: (p.name == "manifest.json", str(p)))
    for path in files:
        target = dst / path.relative_to(src)
        if same_bytes(path, target):
            continue
        if path == src / "manifest.json" and failed:
            break                           # not all of the new art: the next start tries again
        part = target.with_name(target.name + ".new")
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            retry(shutil.copyfile, path, part)
            retry(os.replace, part, target)
        except OSError as error:
            failed.append("%s (%s)" % (target.relative_to(dst).as_posix(), reason(error)))
            try:
                part.unlink()
            except OSError:
                pass
            if len(failed) >= 3:
                break                       # the folder as a whole is blocked: not seven seconds for each file
    if failed:
        raise ArtError("another program holds " + ", ".join(failed))


def swap(tmp, out):
    """Put the finished art `tmp` at `out`. The old art is moved aside, not removed, until the new one is there; a
    rename that another program blocks is tried again for five seconds, then the old art goes back and the files
    are replaced one by one."""
    old = None
    if os.path.lexists(out):
        old = free_sibling(out, "old")
        try:
            retry(os.rename, out, old)
        except OSError:
            merge(tmp, out)
            discard(tmp)
            return
    try:
        retry(os.rename, tmp, out)
    except OSError:
        if old is not None and not os.path.lexists(out):
            try:
                retry(os.rename, old, out)
            except OSError:
                merge(old, out)
        merge(tmp, out)
        discard(tmp)
    for path in sibling(out, "old"):
        discard(path)


def reason(error):
    """An exception in one line for the log: its kind, the Windows error number (the text itself is translated)."""
    if isinstance(error, ArtError):
        return str(error)
    number = getattr(error, "winerror", None) or getattr(error, "errno", None)
    name = Path(str(error.filename)).name if getattr(error, "filename", None) else ""
    text = type(error).__name__ + (" %s" % number if number else "") + (" on %s" % name if name else "")
    return text if number else "%s: %s" % (text, error)


def write_status(data, result, step, detail):
    try:
        path = Path(data) / STATUS
        part = path.with_name(path.name + ".new")
        part.write_text("; the online screens' art, written by scripts/online-art.py. Do not edit.\n[art]\ntime=%s\n"
                        "result=%s\nstep=%s\ndetail=%s\n"
                        % (time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), result, step,
                           re.sub(r"\s+", " ", str(detail))[:400]), encoding="utf-8")
        os.replace(part, path)
    except OSError:
        pass


def repair(data, out):
    """The launcher's start: ('current' | 'nothing' | 'repaired', what). Raises when it could not be repaired."""
    if is_link(out):
        return "nothing", "a linked folder"
    bad = faults(out)
    if not bad:
        for path in sibling(out, "tmp") + sibling(out, "old"):
            discard(path)
        return "current", ""
    lost = "%d file(s) missing, %s the first" % (len(bad), bad[0])
    for tmp in sibling(out, "tmp"):         # finished art an update could not put in place
        if is_link(tmp) or faults(tmp):
            continue
        if out.exists():
            keep_new_fighters(out, tmp)
            keep_skin_pictures(out, tmp)
        swap(tmp, out)
        return "repaired", "put the finished art of %s in place (%s)" % (tmp.name, lost)
    have_dump = all((Path(data) / "extracted" / rel).is_file() for rel in ("screens/screens.viv", "fonts/fonts.viv"))
    try:
        if have_dump:
            build(data, out)
            return "repaired", "made again (%s)" % lost
    finally:
        if not os.path.lexists(out):        # no new art: the old one is better than none
            for path in sibling(out, "old"):
                if not is_link(path):
                    retry(os.rename, path, out)
                    break
    if not faults(out):
        return "repaired", "the old art is back (%s)" % lost
    if not os.path.lexists(out) and not have_dump:
        return "nothing", "no game files"
    raise ArtError("incomplete (%s) and no game files to make it from" % lost)


def build(data, out):
    screens = Path(data) / "extracted" / "screens" / "screens.viv"
    fonts = Path(data) / "extracted" / "fonts" / "fonts.viv"
    if not screens.is_file() or not fonts.is_file():
        raise SystemExit("No game files under %s (expected extracted/screens/screens.viv and fonts/fonts.viv)" % data)
    if is_link(out):
        raise ArtError("%s is a link" % out.name)
    tmp = free_sibling(out, "tmp")
    for sub in ("fighters", "venues", "ui", "fonts", "modes"):
        (tmp / sub).mkdir(parents=True, exist_ok=True)
    blob = screens.read_bytes()
    entries = dict(hd_assets.archive_entries(blob))
    manifest = {"version": VERSION, "fighters": [], "venues": [], "ui": [], "modes": [], "fonts": [], "missing": []}

    for name, blob_entry in entries.items():
        m = re.fullmatch(r"screens/portrait/p_CT/p_CT_(\d+)\.big", name)
        if m:      # p_CT_<n> is Battle-mode ID n-1 (the battle roster's FE_ID + 1)
            fid = int(m.group(1)) - 1
            imgs = images(blob_entry)
            if imgs:   # the 128x256 texture holds the portrait and padding to its right and below
                trim_padding(next(iter(imgs.values()))).save(tmp / "fighters" / ("%d.png" % fid))
                manifest["fighters"].append(fid)
            continue
        m = re.fullmatch(r"screens/portrait/(vp|mVen)(\d+)\.big", name)
        if m:      # vp<n>: the venue's photo with its sign (256x256); mVen<n>: its name banner;
            fe = int(m.group(2)) - 1     # both are venue.xml feID n-1
            imgs = images(blob_entry)
            if imgs:
                image = next(iter(imgs.values()))
                if m.group(1) == "vp":
                    image.save(tmp / "venues" / ("%d.png" % fe))
                    manifest["venues"].append(fe)
                else:
                    image.save(tmp / "venues" / ("%d_name.png" % fe))

    write_faction_renders(entries, tmp, manifest)
    cache = {}
    for out_name, entry, image_name in UI_IMAGES:
        if entry not in entries:
            manifest["missing"].append(entry)
            continue
        if entry not in cache:
            cache[entry] = images(entries[entry])
        image = cache[entry].get(image_name)
        if image is None:
            manifest["missing"].append("%s#%s" % (entry, image_name))
            continue
        image.save(tmp / "ui" / (out_name + ".png"))
        manifest["ui"].append(out_name)
    manifest["ui"] += write_ps_glyphs(tmp / "ui")
    try:        # the fight overlays' lettering; without it they keep the port's own look
        done, missing = write_hud_art(data, tmp / "ui")
        manifest["ui"] += done
        manifest["missing"] += missing
    except (ValueError, IndexError, OSError) as error:
        manifest["missing"].append("hud art (%s)" % error)

    for name in entries:
        m = re.fullmatch(MATCH_LOGO.replace(".", r"\.").replace("%s", r"(\w+?)"), name)
        if m:
            imgs = images(entries[name])
            if imgs:
                # the logo is split into pieces; keep the largest
                best = max(imgs.values(), key=lambda im: im.size[0] * im.size[1])
                best.save(tmp / "modes" / (m.group(1) + ".png"))
                manifest["modes"].append(m.group(1))

    font_entries = dict(hd_assets.archive_entries(fonts.read_bytes()))
    for font in FONTS:
        if font not in font_entries:
            manifest["missing"].append("fonts/" + font)
            continue
        (tmp / "fonts" / font).write_bytes(maybe_refpack(font_entries[font]))
        manifest["fonts"].append(font)

    try:        # the port's own rank emblems (no game bytes); without a browser the game draws its own
        import importlib.util
        spec = importlib.util.spec_from_file_location("rank_emblems", Path(__file__).resolve().parent / "rank-emblems.py")
        rank_emblems = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(rank_emblems)
        font = tmp / "fonts" / "TradeCon.ttf"
        rank_emblems.render(tmp / "ranks", font if font.is_file() else None)
        manifest["ranks"] = 8
    except Exception as error:    # noqa: BLE001 - reported, never fatal
        manifest["missing"].append("ranks (%s)" % error)

    try:        # the port's own Towers buildings (build 97, no game bytes); without them the game draws its own
        import importlib.util
        spec = importlib.util.spec_from_file_location("tower_art", Path(__file__).resolve().parent / "tower-art.py")
        tower_art = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(tower_art)
        font = tmp / "fonts" / "TradeCon.ttf"
        tower_art.render(tmp / "towers", font if font.is_file() else None)
        manifest["towers"] = 3
    except Exception as error:    # noqa: BLE001 - reported, never fatal
        manifest["missing"].append("towers (%s)" % error)

    manifest["fighters"].sort()
    manifest["venues"].sort()
    (tmp / "manifest.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")
    for previous in [out] + sibling(out, "old"):    # also the pictures of an art folder moved aside and not back
        if previous.exists() and not is_link(previous):
            keep_new_fighters(previous, tmp)
            keep_skin_pictures(previous, tmp)
            break
    swap(tmp, out)
    return manifest


REPAIRED = 3        # --repair's exit code when it changed something (setup/mods.py tells the player)


def status_text(data):
    try:
        return (Path(data) / STATUS).read_text(encoding="utf-8")
    except OSError:
        return ""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--data", default=default_data())
    ap.add_argument("--check", action="store_true", help="only say whether the art is there and current")
    ap.add_argument("--repair", action="store_true",
                    help="only when the art is missing or incomplete: put an update's stranded art in place, else make "
                         "it again (the launcher's start; exit 0 nothing to do, %d repaired, 1 failed)" % REPAIRED)
    args = ap.parse_args()
    out = Path(args.data) / "online-art"
    if args.check:
        ok = not faults(out)
        print("online art: %s" % ("current" if ok else "missing or old"))
        return 0 if ok else 1
    try:
        if args.repair:
            what, detail = repair(args.data, out)
            print("online art: %s%s" % (what, " (%s)" % detail if detail else ""))
            if what == "repaired":
                write_status(args.data, "ok", "repair", detail)
            elif what == "current" and "result=failed" in status_text(args.data):
                write_status(args.data, "ok", "repair", "complete")
            return REPAIRED if what == "repaired" else 0
        m = build(args.data, out)
    except (Exception, SystemExit) as error:    # noqa: BLE001 - kept for the game's log, then as before
        step = "repair" if args.repair else "setup"
        write_status(args.data, "failed", step, error if isinstance(error, SystemExit) else reason(error))
        print("online art: %s failed: %s" % (step, reason(error)))
        if isinstance(error, SystemExit) or not args.repair:
            raise
        return 1
    write_status(args.data, "ok", "setup", "%d fighters, %d fonts" % (len(m["fighters"]), len(m["fonts"])))
    print("online art: %d fighters, %d venues, %d ui images, %d mode logos, %d fonts -> %s"
          % (len(m["fighters"]), len(m["venues"]), len(m["ui"]), len(m["modes"]), len(m["fonts"]), out))
    if m["missing"]:
        print("missing: " + ", ".join(m["missing"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

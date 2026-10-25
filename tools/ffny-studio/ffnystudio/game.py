"""The user's own game folder: which fighters and clothing parts it has, and loading one with its textures.

* Fighters: ``fighter.xml`` ``<model>`` names a basename in ``assets/v2ip.viv`` (``V2IP_003A.o`` + ``.xsh``).
  Models in the archive that no record names (alternate outfits such as ``V2IP_014B``) are listed after them.
* Clothing parts: the main meshes ``V2CP_<cat>_<n>_C.o`` (and ``_C1.o`` variants) of ``assets/v2cp_0..3.viv``.
  Their texture is a run-time placeholder (``cp_a``); the part's own ``<base>_A01_01.xsh`` (first of
  ``<base>_*.xsh``) stands in for it, and parts without one stay untextured.

Reads only; nothing is written anywhere.
"""

import os
import re
from dataclasses import dataclass, field
from pathlib import Path

from . import eagl, texture, viv

FIGHTER_ARCHIVE = "assets/v2ip.viv"
PART_ARCHIVES = ("assets/v2cp_0.viv", "assets/v2cp_1.viv", "assets/v2cp_2.viv", "assets/v2cp_3.viv")
_FIELD = re.compile(r"<(\w+)>\s*([^<]*?)\s*</\1>")
_PART = re.compile(r"^(V2CP_.+)_C\d*\.o$", re.IGNORECASE)


@dataclass
class Fighter:
    model: str                  # V2IP_003A
    display: str                # "House (STF)", or the model name for unlisted models
    name: str = ""              # HOUSE
    id: int = -1                # fighter.xml ID, -1 for unlisted models
    listed: bool = True

    @property
    def label(self):
        return "%s - %s" % (self.display, self.model) if self.display != self.model else self.model


@dataclass
class Part:
    name: str                   # V2CP_110_0001_C
    archive: str                # assets/v2cp_1.viv


@dataclass
class Asset:
    """One loaded model: the EAGL object, its decoded textures, and the image to use for placeholders."""
    name: str
    obj: eagl.EaglObject
    images: dict = field(default_factory=dict)     # SHPX image name -> texture.Image
    texture_file: str = ""
    fallback_texture: str = ""
    texture_errors: list = field(default_factory=list)


def parse_fighter_xml(text):
    """[{field: value}] per ``<fighter>`` record. A tolerant scan, like the port's own reader
    (src/hooks/online_data.c), so a stray character in the file does not lose the whole list."""
    records = []
    for block in re.findall(r"<fighter>(.*?)</fighter>", text, re.DOTALL):
        records.append({key: value for key, value in _FIELD.findall(block)})
    return records


def fighter_list(records, models):
    """The importable fighters: records whose model is in ``models`` (sorted by display name), then the
    models no record names (sorted)."""
    available = {m.upper(): m for m in models}
    out, seen = [], set()
    for rec in records:
        model = rec.get("model", "")
        key = model.upper()
        if not key.startswith("V2IP_") or key not in available or key in seen:
            continue
        seen.add(key)
        try:
            ident = int(rec.get("ID", "-1"))
        except ValueError:
            ident = -1
        out.append(Fighter(available[key], rec.get("dispname") or rec.get("name") or model,
                           rec.get("name", ""), ident))
    out.sort(key=lambda f: (f.display.lower(), f.model))
    extra = sorted(m for k, m in available.items() if k not in seen)
    out += [Fighter(m, m, listed=False) for m in extra]
    return out


def fighter_models(names):
    """Body models in a v2ip directory: ``V2IP_xxxx.o`` without the ``_F`` face-pose banks."""
    out = []
    for name in names:
        base, ext = os.path.splitext(name)
        if ext.lower() == ".o" and base.upper().startswith("V2IP_") and not base.upper().endswith("_F"):
            out.append(base)
    return out


def part_names(names):
    return sorted(os.path.splitext(n)[0] for n in names if _PART.match(n))


def part_texture(base, names):
    """The .xsh that stands in for a part's placeholder texture, or ''."""
    match = re.match(r"^(.+)_C\d*$", base, re.IGNORECASE)
    prefix = (match.group(1) if match else base).lower()
    own = base.lower() + ".xsh"
    candidates = sorted(n for n in names if n.lower().endswith(".xsh")
                        and (n.lower() == own or n.lower().startswith(prefix + "_")))
    return candidates[0] if candidates else ""


def resolve_game_dir(path):
    """The folder holding ``assets/v2ip.viv``: ``path`` itself, its ``extracted`` or ``game`` subfolder, or
    its parent when ``path`` is the ``assets`` folder. Raises ValueError with a readable message."""
    if not path:
        raise ValueError("set the game folder in the add-on preferences")
    base = Path(os.path.expanduser(str(path)))
    candidates = [base, base / "extracted", base / "game"]
    if base.name.lower() == "assets":
        candidates.insert(0, base.parent)
    for candidate in candidates:
        if (candidate / FIGHTER_ARCHIVE).is_file():
            return candidate
    raise ValueError("no %s under %s: pick the folder of your extracted game (the one with fighter.xml "
                     "and assets/)" % (FIGHTER_ARCHIVE, base))


class Game:
    def __init__(self, path):
        self.root = resolve_game_dir(path)
        self._archives = {}
        self._parts = None

    def archive(self, rel):
        if rel not in self._archives:
            self._archives[rel] = viv.Archive(str(self.root / rel))
        return self._archives[rel]

    def fighters(self):
        xml = self.root / "fighter.xml"
        records = parse_fighter_xml(xml.read_text("latin-1")) if xml.is_file() else []
        return fighter_list(records, fighter_models(self.archive(FIGHTER_ARCHIVE).names()))

    def parts(self):
        if self._parts is None:
            self._parts = []
            for rel in PART_ARCHIVES:
                if (self.root / rel).is_file():
                    self._parts += [Part(n, rel) for n in part_names(self.archive(rel).names())]
        return self._parts

    def find_part(self, name):
        key = name.strip().lower()
        if key.endswith(".o"):
            key = key[:-2]
        for part in self.parts():
            if part.name.lower() == key:
                return part
        raise KeyError("no clothing part %r in %s" % (name, ", ".join(PART_ARCHIVES)))

    def _load(self, rel, base, texture_name, fallback):
        archive = self.archive(rel)
        obj = eagl.EaglObject(archive.read(base + ".o"), base)
        asset = Asset(base, obj, texture_file=texture_name)
        if texture_name:
            for name, image, error in texture.shpx_images(archive.read(texture_name)):
                if image is not None:
                    asset.images[name] = image
                else:
                    asset.texture_errors.append("%s: %s" % (name, error))
            if fallback and asset.images:
                asset.fallback_texture = sorted(asset.images)[0]
        return asset

    def load_fighter(self, model):
        archive = self.archive(FIGHTER_ARCHIVE)
        xsh = model + ".xsh"
        try:
            archive.find(xsh)
        except KeyError:
            xsh = ""
        return self._load(FIGHTER_ARCHIVE, model, xsh, fallback=False)

    def load_part(self, name):
        part = self.find_part(name)
        return self._load(part.archive, part.name, part_texture(part.name, self.archive(part.archive).names()),
                          fallback=True)

"""FFNY Studio command line.

    python -m ffnystudio list <viv>                      entries of an archive (nested: a.viv::b.big)
    python -m ffnystudio info <viv> <entry>              what an entry holds (EAGL objects in detail)
    python -m ffnystudio export-fighter <V2IP_xxxA> <out-dir> [--no-skin] [--helpers]

Archive paths may be absolute or relative to the user's own dump (``assets/v2ip.viv`` or just ``v2ip.viv``);
the dump is found through $FFNY_GAME, the checkout's ``game`` junction or $DEFJAM_DATA/extracted.
Run from tools/ffny-studio, or with that folder on PYTHONPATH. Output goes only where you point it.
"""

import argparse
import collections
import os
import sys

from . import _repo, eagl, elf, gltf, viv


def resolve_archive(path):
    first = path.split(viv.SEP)[0]
    if os.path.exists(first):
        return path
    game = _repo.game_dir()
    for candidate in (game / first, game / "assets" / first):
        if candidate.exists():
            return str(candidate) + path[len(first):]
    raise SystemExit("no such archive: " + first)


def cmd_list(args):
    archive, inner = viv.open_path(resolve_archive(args.archive))
    if inner:
        archive = archive.open(inner)
    print("%s: %s, %d entries, header %d bytes, size field %s" % (
        archive.label, archive.magic.decode(), len(archive.entries), archive.header_size,
        {True: "little-endian (Xbox)", None: "does not match", False: "big-endian"}[archive.size_le]))
    for entry in archive.entries:
        head = archive.raw(entry)[:16]
        note = ""
        if viv.is_refpack(head):
            note = "RefPack -> %d" % viv.refpack_size(head)
        print("  %-44s %10d  %s" % (entry.name, entry.size, note))


def describe_eagl(obj):
    e = obj.elf
    print("ELF: machine %d, flags 0x%08x, %d sections" % (e.machine, e.flags, len(e.sections)))
    for s in e.sections[1:]:
        print("  %-10s type %d offset 0x%06x size %d" % (s.name, s.type, s.offset, s.size))
    kinds = collections.Counter(s.name.split(":::")[0] for s in obj.symbols if ":::" in s.name
                                and s.shndx == obj.data_index)
    print("typed symbols:", ", ".join("%s %d" % kv for kv in sorted(kinds.items())))
    print("imports: %d, relocations: %d" % (len(obj.imports()), len(obj.relocs)))
    print("byte-exact re-emit:", "yes" if obj.to_bytes() == obj.original else "NO")
    sk = obj.skeleton()
    if sk:
        pos = eagl.bind_positions(sk)
        pairs = [(b, sk.bones[b.mirror]) for b in sk.bones if b.mirror != b.index and 0 <= b.mirror < len(sk.bones)]
        asym = max((abs(pos[a.index][0] + pos[m.index][0]) for a, m in pairs), default=0.0)
        print("skeleton %s: %d bones, %d mirrored pairs (max |x + x'| %.3f), roots %s" % (
            sk.name, len(sk.bones), len(pairs) // 2, asym, [b.name for b in sk.bones if b.parent < 0]))
    vbs = obj.vertex_buffers()
    if vbs:
        print("vertex buffers: %d, consistent %d" % (len(vbs), sum(obj.check_vertex_buffer(v) for v in vbs)))
    for model in obj.models():
        print("model %s: %d materials, textures %s" % (model.name, len(model.materials), model.textures))
        if model.bbox:
            print("  bbox %s .. %s" % (["%.2f" % v for v in model.bbox[0]], ["%.2f" % v for v in model.bbox[1]]))
        for material, group in zip(model.materials, model.groups):
            print("  [%s] %d draws" % (material, len(group)))
    meshes = obj.meshes()
    if meshes:
        verts = sum(len(m.positions) for m in meshes)
        tris = sum(len(m.triangles()) for m in meshes)
        print("meshes: %d, %d vertices, %d triangles" % (len(meshes), verts, tris))


def cmd_info(args):
    archive, inner = viv.open_path(resolve_archive(args.archive))
    if inner:
        archive = archive.open(inner)
    entry = archive.find(args.entry)
    blob = archive.read(entry)
    print("%s: %d bytes stored, %d bytes, %s" % (entry.name, entry.size, len(blob), viv.entry_kind(blob)))
    if elf.is_elf(blob):
        obj = eagl.EaglObject(blob, entry.name)
        describe_eagl(obj)
    elif blob[:4] in viv.MAGICS:
        nested = viv.Archive(blob, entry.name)
        for e in nested.entries[:60]:
            print("  %-44s %10d" % (e.name, e.size))
    elif blob[:4] == b"SHPX":
        hd = _repo.hd_assets()
        for record, _ in hd.shpx_images(blob, decode=False):
            print("  image %(index)d %(name)s" % record, record.get("width"), record.get("height"),
                  "type 0x%02x" % record.get("type", 0), record.get("error", ""))


def load_textures(archive, base):
    try:
        blob = archive.read(base + ".xsh")
    except KeyError:
        return {}
    hd = _repo.hd_assets()
    images = {}
    for record, image in hd.shpx_images(blob):
        if image is not None:
            images[record["name"]] = image
    return images


def cmd_export_fighter(args):
    base = os.path.splitext(args.fighter)[0]
    archive = viv.Archive(resolve_archive(args.archive))
    blob = archive.read(base + ".o")
    obj = eagl.EaglObject(blob, base)
    textures = {} if args.no_texture else load_textures(archive, base)
    stats = gltf.write(obj, args.out_dir, base, textures, include_helpers=args.helpers, skin=not args.no_skin)
    path = os.path.join(args.out_dir, base + ".gltf")
    problems = gltf.validate(path)
    lo, hi = stats["bbox"]
    print("%s: %d primitives, %d vertices (%d skinned), %d triangles" % (
        path, stats["primitives"], stats["vertices"], stats["skinned_vertices"], stats["triangles"]))
    print("bbox min (%.2f, %.2f, %.2f) max (%.2f, %.2f, %.2f), height %.2f" % (*lo, *hi, hi[1] - lo[1]))
    print("files:", ", ".join(stats["files"]))
    print("validation:", "ok" if not problems else "; ".join(problems))
    return 1 if problems else 0


def main(argv=None):
    parser = argparse.ArgumentParser(prog="ffnystudio", description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list", help="list an archive")
    p.add_argument("archive")
    p.set_defaults(func=cmd_list)
    p = sub.add_parser("info", help="describe one entry")
    p.add_argument("archive")
    p.add_argument("entry")
    p.set_defaults(func=cmd_info)
    p = sub.add_parser("export-fighter", help="export a fighter to glTF")
    p.add_argument("fighter", help="basename in v2ip.viv, e.g. V2IP_003A")
    p.add_argument("out_dir")
    p.add_argument("--archive", default="assets/v2ip.viv")
    p.add_argument("--no-skin", action="store_true", help="leave out joints and weights")
    p.add_argument("--no-texture", action="store_true")
    p.add_argument("--helpers", action="store_true", help="include the facing-arrow helper mesh")
    p.set_defaults(func=cmd_export_fighter)
    args = parser.parse_args(argv)
    return args.func(args) or 0


if __name__ == "__main__":
    sys.exit(main())

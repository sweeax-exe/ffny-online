"""Export an EAGL fighter (meshes, skeleton, skin, texture) to glTF 2.0 (``.gltf`` + ``.bin`` + PNG).

Conventions: EAGL fighters are +Y up, facing +Z, the character's left at +X, which is glTF's frame, so no axis
change is applied. Matrices in the object are row-major for row vectors (D3D); their 16 floats in file order are
exactly glTF's column-major layout. Strips are converted to counter-clockwise triangles (see
``eagl.strip_triangles``). Bones become nodes posed at the bind pose (the inverse of each bone's inverse bind
matrix); the object's own rest TRS, which differs from the bind pose for a few facial and hair bones, is kept
in each node's ``extras``.
"""

import json
import math
import os
import struct

from . import eagl

COMPONENT_FLOAT, COMPONENT_UBYTE, COMPONENT_USHORT, COMPONENT_UINT = 5126, 5121, 5123, 5125
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963
HELPER_MATERIALS = {"Direction"}      # a ground arrow showing the facing, not part of the body


# -- small matrix helpers (row-major 4x4 lists) --------------------------------------------------------------

def mat_from_flat(values):
    return [list(values[0:4]), list(values[4:8]), list(values[8:12]), list(values[12:16])]


def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def mat_inverse(m):
    a = [row[:] + [1.0 if i == j else 0.0 for j in range(4)] for i, row in enumerate(m)]
    for col in range(4):
        pivot = max(range(col, 4), key=lambda r: abs(a[r][col]))
        if abs(a[pivot][col]) < 1e-12:
            raise ValueError("singular matrix")
        a[col], a[pivot] = a[pivot], a[col]
        p = a[col][col]
        a[col] = [v / p for v in a[col]]
        for r in range(4):
            if r != col:
                f = a[r][col]
                a[r] = [v - f * w for v, w in zip(a[r], a[col])]
    return [row[4:] for row in a]


def decompose(m):
    """Row-vector matrix -> (translation, quaternion xyzw, scale) in glTF (column-vector) terms."""
    t = m[3][:3]
    rows = [m[i][:3] for i in range(3)]
    s = [math.sqrt(sum(v * v for v in r)) or 1.0 for r in rows]
    r = [[rows[i][j] / s[i] for j in range(3)] for i in range(3)]
    if (r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
            + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0])) < 0:
        s[0] = -s[0]
        r[0] = [-v for v in r[0]]
    # column-vector rotation R = r^T
    R = [[r[j][i] for j in range(3)] for i in range(3)]
    trace = R[0][0] + R[1][1] + R[2][2]
    if trace > 0:
        k = math.sqrt(trace + 1.0) * 2
        q = ((R[2][1] - R[1][2]) / k, (R[0][2] - R[2][0]) / k, (R[1][0] - R[0][1]) / k, 0.25 * k)
    elif R[0][0] > R[1][1] and R[0][0] > R[2][2]:
        k = math.sqrt(1.0 + R[0][0] - R[1][1] - R[2][2]) * 2
        q = (0.25 * k, (R[0][1] + R[1][0]) / k, (R[0][2] + R[2][0]) / k, (R[2][1] - R[1][2]) / k)
    elif R[1][1] > R[2][2]:
        k = math.sqrt(1.0 + R[1][1] - R[0][0] - R[2][2]) * 2
        q = ((R[0][1] + R[1][0]) / k, 0.25 * k, (R[1][2] + R[2][1]) / k, (R[0][2] - R[2][0]) / k)
    else:
        k = math.sqrt(1.0 + R[2][2] - R[0][0] - R[1][1]) * 2
        q = ((R[0][2] + R[2][0]) / k, (R[1][2] + R[2][1]) / k, 0.25 * k, (R[1][0] - R[0][1]) / k)
    n = math.sqrt(sum(c * c for c in q))
    q = tuple(c / n for c in q)
    if q[3] < 0:
        q = tuple(-c for c in q)
    return list(t), list(q), s


# -- the builder ---------------------------------------------------------------------------------------------

class Builder:
    def __init__(self):
        self.doc = {"asset": {"version": "2.0", "generator": "FFNY Studio (ffnystudio)"},
                    "scene": 0, "scenes": [{"nodes": []}], "nodes": [], "meshes": [], "materials": [],
                    "accessors": [], "bufferViews": [], "buffers": []}
        self.bin = bytearray()

    def view(self, blob, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        view = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(blob)}
        if target:
            view["target"] = target
        self.bin += blob
        self.doc["bufferViews"].append(view)
        return len(self.doc["bufferViews"]) - 1

    def accessor(self, values, kind, component, normalized=False, target=ARRAY_BUFFER, minmax=False):
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[kind]
        code = {COMPONENT_FLOAT: "f", COMPONENT_UBYTE: "B", COMPONENT_USHORT: "H", COMPONENT_UINT: "I"}[component]
        flat = [v for value in values for v in (value if width > 1 else (value,))]
        blob = struct.pack("<%d%s" % (len(flat), code), *flat)
        if component == COMPONENT_UBYTE and width % 4:
            raise ValueError("pad ubyte attributes to VEC4")
        acc = {"bufferView": self.view(blob, target), "componentType": component, "count": len(values),
               "type": kind}
        if normalized:
            acc["normalized"] = True
        if minmax:
            cols = list(zip(*values)) if width > 1 else [values]
            acc["min"] = [min(c) for c in cols]
            acc["max"] = [max(c) for c in cols]
        self.doc["accessors"].append(acc)
        return len(self.doc["accessors"]) - 1

    def node(self, **fields):
        self.doc["nodes"].append({k: v for k, v in fields.items() if v is not None})
        return len(self.doc["nodes"]) - 1


def _unit(n):
    length = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
    return (n[0] / length, n[1] / length, n[2] / length) if length > 1e-8 else (0.0, 1.0, 0.0)


def build(obj, name, texture_files=None, include_helpers=False, skin=True):
    """Return (gltf dict, bin bytes, stats) for the first model of ``obj`` (an ``eagl.EaglObject``).

    ``texture_files`` maps SHPX image names (``A003``) to image URIs written next to the .gltf."""
    texture_files = texture_files or {}
    b = Builder()
    skeleton = obj.skeleton()
    meshes = [m for m in obj.meshes() if include_helpers or m.material not in HELPER_MATERIALS]

    # materials and textures
    images, mat_index = {}, {}
    for m in meshes:
        key = (m.material, m.texture, m.blended)
        if key in mat_index:
            continue
        material = {"name": m.material or "material", "doubleSided": False,
                    "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.8},
                    "extras": {"render_method": m.render_method, "texture": m.texture}}
        if m.blended:
            material["alphaMode"] = "BLEND"
        if m.texture in texture_files:
            if m.texture not in images:
                b.doc.setdefault("images", []).append({"uri": texture_files[m.texture], "name": m.texture})
                b.doc.setdefault("samplers", [{"magFilter": 9729, "minFilter": 9987, "wrapS": 33071,
                                               "wrapT": 33071}])
                b.doc.setdefault("textures", []).append({"source": len(b.doc["images"]) - 1, "sampler": 0})
                images[m.texture] = len(b.doc["textures"]) - 1
            material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": images[m.texture]}
        b.doc["materials"].append(material)
        mat_index[key] = len(b.doc["materials"]) - 1

    # skeleton nodes at the bind pose
    joints = []
    if skeleton:
        world = [mat_inverse(mat_from_flat(bone.inverse_bind)) for bone in skeleton.bones]
        first = len(b.doc["nodes"])
        for bone, w in zip(skeleton.bones, world):
            local = w if bone.parent < 0 else mat_mul(w, mat_inverse(world[bone.parent]))
            t, q, s = decompose(local)
            fields = {"name": bone.name, "translation": t, "rotation": q,
                      "extras": {"eagl_index": bone.index, "mirror": skeleton.bones[bone.mirror].name
                                 if 0 <= bone.mirror < len(skeleton.bones) else "",
                                 "rest_translation": list(bone.translation), "rest_rotation": list(bone.rotation),
                                 "rest_scale": list(bone.scale)}}
            if any(abs(v - 1.0) > 1e-5 for v in s):
                fields["scale"] = s
            b.node(**fields)
        for bone in skeleton.bones:
            if bone.parent >= 0:
                b.doc["nodes"][first + bone.parent].setdefault("children", []).append(first + bone.index)
        joints = [first + bone.index for bone in skeleton.bones]
        roots = [first + bone.index for bone in skeleton.bones if bone.parent < 0]
    else:
        roots = []

    # one glTF mesh, one primitive per EAGL render model
    stats = {"vertices": 0, "triangles": 0, "primitives": 0, "skinned_vertices": 0}
    prims, lo, hi = [], [math.inf] * 3, [-math.inf] * 3
    for m in meshes:
        tris = m.triangles()
        if not tris:
            continue
        attrs = {"POSITION": b.accessor(m.positions, "VEC3", COMPONENT_FLOAT, minmax=True)}
        for p in m.positions:
            for k in range(3):
                lo[k], hi[k] = min(lo[k], p[k]), max(hi[k], p[k])
        if m.normals:
            attrs["NORMAL"] = b.accessor([_unit(n) for n in m.normals], "VEC3", COMPONENT_FLOAT)
        if m.uvs:
            attrs["TEXCOORD_0"] = b.accessor(m.uvs, "VEC2", COMPONENT_FLOAT)
        if m.colors and any(c != (1.0, 1.0, 1.0, 1.0) for c in m.colors):
            attrs["COLOR_0"] = b.accessor([tuple(int(round(v * 255)) for v in c) for c in m.colors], "VEC4",
                                          COMPONENT_UBYTE, normalized=True)
        jw = m.joints_weights() if (skin and skeleton) else None
        if jw:
            js, ws = jw
            attrs["JOINTS_0"] = b.accessor(js, "VEC4", COMPONENT_UBYTE)
            fixed = []
            for w in ws:
                total = sum(w)
                fixed.append(tuple(v / total for v in w) if total > 0 else (1.0, 0.0, 0.0, 0.0))
            attrs["WEIGHTS_0"] = b.accessor(fixed, "VEC4", COMPONENT_FLOAT)
            stats["skinned_vertices"] += sum(1 for w in ws if sum(w) > 0)
        flat = [i for t in tris for i in t]
        idx = b.accessor(flat, "SCALAR", COMPONENT_USHORT, target=ELEMENT_ARRAY_BUFFER)
        prims.append({"attributes": attrs, "indices": idx, "mode": 4,
                      "material": mat_index[(m.material, m.texture, m.blended)],
                      "extras": {"render_model": m.name}})
        stats["vertices"] += len(m.positions)
        stats["triangles"] += len(tris)
        stats["primitives"] += 1
    b.doc["meshes"].append({"name": name, "primitives": prims})
    mesh_fields = {"name": name, "mesh": 0}
    if joints and any("JOINTS_0" in p["attributes"] for p in prims):
        ibm = b.accessor([skeleton.bones[i].inverse_bind for i in range(len(skeleton.bones))], "MAT4",
                         COMPONENT_FLOAT, target=None)
        b.doc["skins"] = [{"name": skeleton.name or "root", "joints": joints, "inverseBindMatrices": ibm,
                           "skeleton": roots[0] if roots else joints[0]}]
        mesh_fields["skin"] = 0
    mesh_node = b.node(**mesh_fields)
    b.doc["scenes"][0]["nodes"] = [mesh_node] + roots
    if not b.doc["materials"]:
        del b.doc["materials"]
    stats["bbox"] = (lo, hi)
    return b.doc, bytes(b.bin), stats


def write(obj, out_dir, name, texture_images=None, include_helpers=False, skin=True):
    """Write ``<name>.gltf``, ``<name>.bin`` and the PNGs; ``texture_images`` maps SHPX names to PIL images."""
    os.makedirs(out_dir, exist_ok=True)
    files = {}
    for shape, image in (texture_images or {}).items():
        filename = "%s_%s.png" % (name, shape)
        image.save(os.path.join(out_dir, filename))
        files[shape] = filename
    doc, blob, stats = build(obj, name, files, include_helpers=include_helpers, skin=skin)
    doc["buffers"] = [{"uri": name + ".bin", "byteLength": len(blob)}]
    with open(os.path.join(out_dir, name + ".bin"), "wb") as fh:
        fh.write(blob)
    with open(os.path.join(out_dir, name + ".gltf"), "w", encoding="utf-8") as fh:
        json.dump(doc, fh, indent=1)
    stats["files"] = [name + ".gltf", name + ".bin"] + list(files.values())
    return stats


def validate(path):
    """Structural check of a written .gltf: references in range, buffer sizes, accessor bounds. Returns problems."""
    problems = []
    with open(path, encoding="utf-8") as fh:
        doc = json.load(fh)
    base = os.path.dirname(path)
    if doc.get("asset", {}).get("version") != "2.0":
        problems.append("asset.version is not 2.0")
    buffers = []
    for i, buf in enumerate(doc.get("buffers", [])):
        file = os.path.join(base, buf["uri"])
        size = os.path.getsize(file) if os.path.exists(file) else -1
        if size < buf["byteLength"]:
            problems.append("buffer %d: %s is %d bytes, needs %d" % (i, buf["uri"], size, buf["byteLength"]))
        buffers.append(buf["byteLength"])
    sizes = {COMPONENT_FLOAT: 4, COMPONENT_UBYTE: 1, COMPONENT_USHORT: 2, COMPONENT_UINT: 4, 5120: 1, 5122: 2}
    widths = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}
    views = doc.get("bufferViews", [])
    for i, view in enumerate(views):
        if view["buffer"] >= len(buffers) or view.get("byteOffset", 0) + view["byteLength"] > buffers[view["buffer"]]:
            problems.append("bufferView %d out of range" % i)
    for i, acc in enumerate(doc.get("accessors", [])):
        view = views[acc["bufferView"]] if acc.get("bufferView", -1) < len(views) else None
        if view is None:
            problems.append("accessor %d: bad bufferView" % i)
            continue
        need = acc.get("byteOffset", 0) + acc["count"] * widths[acc["type"]] * sizes[acc["componentType"]]
        if need > view["byteLength"]:
            problems.append("accessor %d: needs %d bytes, view has %d" % (i, need, view["byteLength"]))
        if (acc.get("byteOffset", 0) + view.get("byteOffset", 0)) % sizes[acc["componentType"]]:
            problems.append("accessor %d misaligned" % i)
    nodes = doc.get("nodes", [])
    for i, node in enumerate(nodes):
        for child in node.get("children", []):
            if child >= len(nodes):
                problems.append("node %d: child %d out of range" % (i, child))
        if "mesh" in node and node["mesh"] >= len(doc.get("meshes", [])):
            problems.append("node %d: bad mesh" % i)
    parents = {}
    for i, node in enumerate(nodes):
        for child in node.get("children", []):
            if child in parents:
                problems.append("node %d has two parents" % child)
            parents[child] = i
    for mesh in doc.get("meshes", []):
        for p in mesh["primitives"]:
            count = None
            for name, acc in p["attributes"].items():
                if acc >= len(doc["accessors"]):
                    problems.append("%s: attribute %s out of range" % (mesh["name"], name))
                    continue
                c = doc["accessors"][acc]["count"]
                count = c if count is None else count
                if c != count:
                    problems.append("%s: attribute %s count %d != %d" % (mesh["name"], name, c, count))
            pos = doc["accessors"][p["attributes"]["POSITION"]]
            if "min" not in pos or "max" not in pos:
                problems.append("%s: POSITION without min/max" % mesh["name"])
            if "material" in p and p["material"] >= len(doc.get("materials", [])):
                problems.append("%s: bad material" % mesh["name"])
            if "indices" in p:
                acc = doc["accessors"][p["indices"]]
                view = views[acc["bufferView"]]
                file = os.path.join(base, doc["buffers"][view["buffer"]]["uri"])
                with open(file, "rb") as fh:
                    fh.seek(view.get("byteOffset", 0) + acc.get("byteOffset", 0))
                    raw = fh.read(acc["count"] * 2)
                top = max(struct.unpack("<%dH" % acc["count"], raw)) if acc["count"] else 0
                if top >= count:
                    problems.append("%s: index %d >= vertex count %d" % (mesh["name"], top, count))
                if acc["count"] % 3:
                    problems.append("%s: index count not a multiple of 3" % mesh["name"])
    for skin in doc.get("skins", []):
        for j in skin["joints"]:
            if j >= len(nodes):
                problems.append("skin joint %d out of range" % j)
        acc = doc["accessors"][skin["inverseBindMatrices"]]
        if acc["count"] != len(skin["joints"]) or acc["type"] != "MAT4":
            problems.append("inverseBindMatrices count/type mismatch")
    for i, image in enumerate(doc.get("images", [])):
        if not os.path.exists(os.path.join(base, image["uri"])):
            problems.append("image %d: %s missing" % (i, image["uri"]))
    return problems

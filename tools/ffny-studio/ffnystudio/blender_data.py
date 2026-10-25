"""What the Blender importer builds, as plain data: no ``bpy`` here, so all of it is unit-tested.

The add-on (tools/ffny-studio/blender/ffny_studio) only turns these lists into Blender objects.

Frames: EAGL fighters are +Y up, facing +Z, their left at +X (glTF's frame). Blender is +Z up and a character
faces -Y (towards the Front view), so a game point (x, y, z) becomes (x, -z, y), the same rotation Blender's
glTF importer applies. It is a proper rotation, so the counter-clockwise triangles of
``eagl.Mesh.triangles()`` stay front-facing. Geometry stays in game units; the importer scales the armature
object instead, so an exporter can read the data back unchanged.

Bones: the head is the bind position (from the inverse bind matrix), the tail points at the children (their
mean head) or continues the parent's direction for a leaf. ``align_z`` is the game bone axis that follows the
one closest to the bone direction (x -> y -> z -> x), which the add-on passes to ``EditBone.align_roll`` so the
bone's local axes stay close to the game's. Every bone keeps its inverse bind matrix, index, mirror and rest
TRS as custom properties, so nothing depends on that heuristic.
"""

import math
from dataclasses import dataclass, field

from . import gltf

HELPER_MATERIALS = gltf.HELPER_MATERIALS
MIN_BONE = 0.05            # game units; Blender drops zero-length bones
LEAF_FRACTION = 0.5        # a leaf bone is this fraction of its parent's length
MAX_NAME = 63              # Blender's ID name limit


def to_blender(v):
    return (v[0], -v[2], v[1])


def _sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def _add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def _scale(a, s):
    return tuple(x * s for x in a)


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _norm(a):
    return math.sqrt(_dot(a, a))


def _unit(a, fallback=(0.0, 0.0, 1.0)):
    n = _norm(a)
    return tuple(x / n for x in a) if n > 1e-9 else fallback


# -- armature ------------------------------------------------------------------------------------------------

def bind_frames(skeleton):
    """Per bone (head, (x axis, y axis, z axis)) of the bind pose in Blender coordinates."""
    out = []
    for bone in skeleton.bones:
        world = gltf.mat_inverse(gltf.mat_from_flat(bone.inverse_bind))
        head = to_blender(world[3][:3])
        axes = tuple(_unit(to_blender(world[i][:3])) for i in range(3))
        out.append((head, axes))
    return out


def build_armature_data(skeleton, min_length=MIN_BONE):
    """[{name, parent, head, tail, align_z, props}] in skeleton order (a parent can follow its child)."""
    bones = skeleton.bones
    frames = bind_frames(skeleton)
    heads = [f[0] for f in frames]
    children = {i: [] for i in range(len(bones))}
    for bone in bones:
        if 0 <= bone.parent < len(bones):
            children[bone.parent].append(bone.index)

    tails = [None] * len(bones)

    def solve(i, depth=0):
        if tails[i] is not None:
            return tails[i]
        head = heads[i]
        far = [heads[c] for c in children[i] if _norm(_sub(heads[c], head)) > min_length]
        if far:
            target = _scale(tuple(map(sum, zip(*far))), 1.0 / len(far))
            if _norm(_sub(target, head)) > min_length:
                tails[i] = target
                return target
            direction, length = _unit(_sub(far[0], head)), _norm(_sub(far[0], head))
        else:
            parent = bones[i].parent
            if 0 <= parent < len(bones) and depth < len(bones):
                ptail = solve(parent, depth + 1)
                pvec = _sub(ptail, heads[parent])
                offset = _sub(head, heads[parent])
                direction = _unit(offset if _norm(offset) > min_length else pvec)
                length = LEAF_FRACTION * _norm(pvec)
            else:
                direction, length = (0.0, 0.0, 1.0), 1.0
        tails[i] = _add(head, _scale(direction, max(length, min_length)))
        return tails[i]

    out = []
    for bone in bones:
        tail = solve(bone.index)
        head, axes = frames[bone.index]
        direction = _unit(_sub(tail, head))
        primary = max(range(3), key=lambda k: abs(_dot(axes[k], direction)))
        parent = bones[bone.parent].name if 0 <= bone.parent < len(bones) else None
        mirror = bones[bone.mirror].name if 0 <= bone.mirror < len(bones) else ""
        out.append({
            "name": bone.name, "parent": parent, "head": head, "tail": tail, "align_z": axes[(primary + 1) % 3],
            "props": {"ffny_index": bone.index, "ffny_mirror": mirror,
                      "ffny_inverse_bind": list(bone.inverse_bind),
                      "ffny_rest_translation": list(bone.translation), "ffny_rest_rotation": list(bone.rotation),
                      "ffny_rest_scale": list(bone.scale)},
        })
    return out


# -- meshes --------------------------------------------------------------------------------------------------

def material_key(mesh):
    return (mesh.material, mesh.texture, mesh.blended)


def material_label(key):
    """``emc4_cloth~DJIrradTextureEnvmap_Skin4`` + ``A003`` -> ``emc4_cloth A003``."""
    material, tex, blended = key
    label = (material or "material").split("~")[0]
    if tex:
        label += " " + tex
    if blended:
        label += " blend"
    return label[:MAX_NAME]


def faces_of(mesh):
    """Triangles without degenerate or repeated faces (strips join pieces with repeated indices)."""
    out, seen = [], set()
    count = len(mesh.positions)
    for tri in mesh.triangles():
        if len(set(tri)) < 3 or max(tri) >= count:
            continue
        key = tuple(sorted(tri))
        if key in seen:
            continue
        seen.add(key)
        out.append(tuple(tri))
    return out


def vertex_groups(mesh, skeleton):
    """{bone name: [(vertex, weight)]}: up to three bones per vertex, weights normalised, zero weights dropped."""
    if skeleton is None:
        return {}
    jw = mesh.joints_weights()
    if not jw:
        return {}
    groups = {}
    names = [b.name for b in skeleton.bones]
    for v, (joints, weights) in enumerate(zip(*jw)):
        per = {}
        for j, w in zip(joints, weights):
            if w > 0 and 0 <= j < len(names):
                per[j] = per.get(j, 0.0) + w
        total = sum(per.values())
        for j, w in per.items():
            groups.setdefault(names[j], []).append((v, w / total))
    return groups


def build_mesh_data(mesh, skeleton=None):
    """One draw as {positions, faces, normals, uvs, colors, groups, material, ...}; per-vertex attributes."""
    data = {
        "render_model": mesh.name, "material": material_key(mesh), "texture": mesh.texture,
        "render_method": mesh.render_method, "blended": mesh.blended,
        "positions": [to_blender(p) for p in mesh.positions],
        "faces": faces_of(mesh),
        "normals": [_unit(to_blender(n), (0.0, 0.0, 1.0)) for n in mesh.normals]
        if mesh.normals and len(mesh.normals) == len(mesh.positions) else None,
        "uvs": [(uv[0], 1.0 - uv[1]) for uv in mesh.uvs]
        if mesh.uvs and len(mesh.uvs) == len(mesh.positions) else None,
        "colors": None,
        "groups": vertex_groups(mesh, skeleton),
    }
    if mesh.colors and len(mesh.colors) == len(mesh.positions) \
            and any(c != (1.0, 1.0, 1.0, 1.0) for c in mesh.colors):
        data["colors"] = [tuple(c) for c in mesh.colors]
    return data


# -- the whole import ----------------------------------------------------------------------------------------

@dataclass
class ImportPlan:
    name: str
    bones: list                                    # build_armature_data
    meshes: list                                   # build_mesh_data, plus "object_name"
    materials: list                                # [{key, name, texture, image, blended}]
    images: dict = field(default_factory=dict)     # name -> texture.Image actually used
    stats: dict = field(default_factory=dict)


def build_import(obj, name, images=None, include_helpers=False, fallback_texture=""):
    """Everything to import one EAGL model. ``images`` maps SHPX names to ``texture.Image``; a mesh texture that
    is not among them uses ``fallback_texture`` (clothing placeholders) when given."""
    images = images or {}
    skeleton = obj.skeleton()
    bones = build_armature_data(skeleton) if skeleton else []
    meshes, materials, used = [], {}, {}
    for i, mesh in enumerate(obj.meshes()):
        if mesh.material in HELPER_MATERIALS and not include_helpers:
            continue
        data = build_mesh_data(mesh, skeleton)
        if not data["faces"]:
            continue
        key = data["material"]
        if key not in materials:
            image = mesh.texture if mesh.texture in images else (fallback_texture if mesh.texture else "")
            if image in images:
                used[image] = images[image]
            else:
                image = ""
            materials[key] = {"key": key, "name": "%s %s" % (name, material_label(key)), "texture": mesh.texture,
                              "image": image, "blended": mesh.blended}
        data["object_name"] = ("%s.%02d %s" % (name, len(meshes), material_label(key)))[:MAX_NAME]
        meshes.append(data)
    stats = {"bones": len(bones), "meshes": len(meshes),
             "vertices": sum(len(m["positions"]) for m in meshes),
             "triangles": sum(len(m["faces"]) for m in meshes),
             "skinned": sum(1 for m in meshes if m["groups"]), "textures": len(used)}
    return ImportPlan(name, bones, meshes, list(materials.values()), used, stats)


def image_name(model, image):
    return ("%s_%s" % (model, image))[:MAX_NAME]

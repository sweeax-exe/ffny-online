"""Turn an ``ffnystudio.blender_data.ImportPlan`` into Blender data. Kept thin: all decisions are in the plan."""

import bpy

from ._lib import blender_data, texture


def create_images(plan, model, pack=True):
    """{SHPX image name: bpy image} for the images the plan's materials use."""
    out = {}
    for name, image in plan.images.items():
        bimg = bpy.data.images.new(blender_data.image_name(model, name), image.width, image.height, alpha=True)
        bimg.pixels.foreach_set(texture.blender_pixels(image))
        bimg.update()
        bimg["ffny_shpx"] = name
        if pack:
            try:
                bimg.pack()
            except RuntimeError:
                pass
        out[name] = bimg
    return out


def create_material(spec, images):
    mat = bpy.data.materials.new(spec["name"])
    mat.use_nodes = True
    mat["ffny_material"] = spec["key"][0]
    mat["ffny_texture"] = spec["texture"]
    nodes = mat.node_tree.nodes
    bsdf = next((n for n in nodes if n.type == "BSDF_PRINCIPLED"), None)
    if bsdf is None:
        return mat
    bsdf.inputs["Metallic"].default_value = 0.0
    bsdf.inputs["Roughness"].default_value = 0.8
    image = images.get(spec["image"])
    if image is not None:
        node = nodes.new("ShaderNodeTexImage")
        node.image = image
        node.location = (bsdf.location[0] - 400, bsdf.location[1])
        mat.node_tree.links.new(node.outputs["Color"], bsdf.inputs["Base Color"])
        if spec["blended"]:
            mat.node_tree.links.new(node.outputs["Alpha"], bsdf.inputs["Alpha"])
    if spec["blended"]:
        if hasattr(mat, "surface_render_method"):      # EEVEE Next (4.2+)
            mat.surface_render_method = "BLENDED"
        else:
            mat.blend_method = "BLEND"
    return mat


def ensure_object_mode(context):
    if context.mode != "OBJECT" and context.view_layer.objects.active is not None:
        bpy.ops.object.mode_set(mode="OBJECT")


def create_armature(context, plan, name, collection, scale):
    arm = bpy.data.armatures.new(name)
    obj = bpy.data.objects.new(name, arm)
    collection.objects.link(obj)
    for other in context.view_layer.objects:
        other.select_set(False)
    context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    edit = {}
    for bone in plan.bones:
        eb = arm.edit_bones.new(bone["name"])
        eb.head = bone["head"]
        eb.tail = bone["tail"]
        edit[bone["name"]] = eb
    for bone in plan.bones:
        eb = edit[bone["name"]]
        if bone["parent"]:
            eb.parent = edit[bone["parent"]]
        eb.align_roll(bone["align_z"])
    bpy.ops.object.mode_set(mode="OBJECT")
    for bone in plan.bones:
        target = arm.bones.get(bone["name"])
        if target is not None:
            for key, value in bone["props"].items():
                target[key] = value
    obj.scale = (scale, scale, scale)
    arm.display_type = "STICK"
    obj.show_in_front = True
    return obj


def create_mesh_object(data, material, collection, armature=None, source=""):
    mesh = bpy.data.meshes.new(data["object_name"])
    mesh.from_pydata(data["positions"], [], data["faces"])
    mesh.validate(clean_customdata=False)
    loops = [0] * len(mesh.loops)
    mesh.loops.foreach_get("vertex_index", loops)
    if data["uvs"]:
        layer = mesh.uv_layers.new(name="UVMap")
        uvs = data["uvs"]
        layer.data.foreach_set("uv", [c for v in loops for c in uvs[v]])
    if data["colors"]:
        attr = mesh.color_attributes.new("Color", "FLOAT_COLOR", "POINT")
        attr.data.foreach_set("color", [c for colour in data["colors"] for c in colour])
    mesh.polygons.foreach_set("use_smooth", [True] * len(mesh.polygons))
    if data["normals"]:
        mesh.normals_split_custom_set_from_vertices(data["normals"])
    if material is not None:
        mesh.materials.append(material)
    mesh.update()

    obj = bpy.data.objects.new(data["object_name"], mesh)
    collection.objects.link(obj)
    obj["ffny_source"] = source
    obj["ffny_render_model"] = data["render_model"]
    obj["ffny_material"] = data["material"][0]
    obj["ffny_texture"] = data["texture"]
    obj["ffny_render_method"] = data["render_method"]
    if armature is not None:
        obj.parent = armature
        modifier = obj.modifiers.new(name="Armature", type="ARMATURE")
        modifier.object = armature
        for bone, entries in data["groups"].items():
            group = obj.vertex_groups.new(name=bone)
            for vertex, weight in entries:
                group.add([vertex], weight, "REPLACE")
    return obj


def create_meshes(plan, collection, armature, source, pack=True):
    images = create_images(plan, source, pack=pack)
    materials = {spec["key"]: create_material(spec, images) for spec in plan.materials}
    return [create_mesh_object(data, materials.get(data["material"]), collection, armature, source)
            for data in plan.meshes]


def import_fighter(context, plan, fighter_label, scale=1.0, pack=True):
    """A new collection with the armature and one mesh object per draw. Returns the armature object."""
    ensure_object_mode(context)
    collection = bpy.data.collections.new(plan.name)
    context.scene.collection.children.link(collection)
    armature = create_armature(context, plan, plan.name, collection, scale) if plan.bones else None
    objects = create_meshes(plan, collection, armature, plan.name, pack=pack)
    holder = armature or (objects[0] if objects else None)
    if holder is not None:
        holder["ffny_model"] = plan.name
        holder["ffny_display"] = fighter_label
        holder["ffny_scale"] = scale
        holder["ffny_parts"] = ""
        for key, value in plan.stats.items():
            holder["ffny_" + key] = value
        holder["ffny_images"] = ", ".join(sorted(plan.images))
    return holder


def import_part(context, plan, armature):
    """The part's meshes onto an existing FFNY armature (same 94-bone skeleton). Returns (objects, missing)."""
    ensure_object_mode(context)
    collection = armature.users_collection[0] if armature.users_collection else context.scene.collection
    bones = set(armature.data.bones.keys())
    missing = sorted({g for data in plan.meshes for g in data["groups"]} - bones)
    objects = create_meshes(plan, collection, armature, plan.name)
    parts = [p for p in str(armature.get("ffny_parts", "")).split(", ") if p]
    armature["ffny_parts"] = ", ".join(parts + [plan.name])
    return objects, missing

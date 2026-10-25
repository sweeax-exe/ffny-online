"""FFNY Studio: read (and later write) the asset formats of Def Jam: Fight for NY.

Pure Python, standard library only. Modules:

* ``viv``    BIGF/BIG4 archives with RefPack-compressed and nested entries.
* ``elf``    the 32-bit little-endian ELF relocatable container, with a byte-exact writer.
* ``eagl``   the EAGL object model on top of it: symbols, pointers, skeletons, render models, vertex buffers.
* ``gltf``   export one fighter (mesh, skeleton, texture) to glTF 2.0.
* ``texture`` SHPX textures decoded to RGBA without Pillow (for Blender's Python).
* ``game``   the user's game folder: fighters (fighter.xml), clothing parts, loading a model with its textures.
* ``blender_data`` the Blender importer's armature, meshes and materials as plain data (no ``bpy``).

Everything reads the user's own dump; nothing here carries game data.
"""

__version__ = "0.1.0"

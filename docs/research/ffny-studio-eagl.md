# FFNY Studio, stage 1: the EAGL object decoded (2026-10-25)

Follows [ffny-studio-formats.md](ffny-studio-formats.md) (stage 0). Worked out read-only from the user's own
dump with `tools/ffny-studio` (package `ffnystudio`); no game data was copied into the repository. Offsets
below are into `.data` unless noted. "Verified" means checked over every object of the archives named.

## Answers to the stage 0 open questions

| Question | Answer |
|---|---|
| Strip boundaries per render model | One triangle strip per draw. The `0x02` parameter holds one u32 length equal to the index count; pieces are joined with repeated indices (degenerate triangles). Verified: 3068 fighter draws, 7277 venue, 2126 prop, 1734 clothing draws, never more than one length, never a length mismatch. |
| Winding | Clockwise front faces (D3D). Strip triangle *i* is (s[i], s[i+1], s[i+2]) for even *i*, (s[i+1], s[i], s[i+2]) for odd; 5500 of 5516 House triangles face away from their vertex normals in counter-clockwise terms, so the exporter reverses all of them. |
| Bone parents | Field +0x0C of each 0x70-byte skeleton record (signed, -1 for `root`). |
| Skin mapping | The 2-byte stream is an NV2A constant register (c[-96..95]) selecting a matrix-palette slot; slot *k* holds the blend of weight entry *k* (three weights, three bone bytes). See "Skinning". Verified: all 349,722 skinned vertices of the 70 `V2IP_xxxA` fighters map to a valid entry. |
| Texture per render model | The draw's texture parameter (`0x0F`) points at an import `__EAGL::TAR:::...SHAPENAME=A003,1;...`; `A003` is the SHPX image name in the fighter's `.xsh`. Other texture slots (`spectexture1/2/4`) are run-time environment maps. |

## Container

* ELF32 little-endian relocatable, `e_machine` 8, `e_flags` 0x20924000, no program header, no `.text`.
  Sections: `.data` (align 16, at 0x40), `.shstrtab`, `.strtab`, `.symtab`, `.rel.data`.
* Two tool layouts: newer objects put the section header table last, aligned 16; older ones (`misc.viv/djv2rm.o`)
  put it between `.shstrtab` and `.rel.data`, aligned 4. Every section sits at its `sh_addralign`, padding is zero.
* Byte-exact re-emit (parse, lay out in the original order with those alignments, write): verified for every `.o`
  in v2ip (156), v2cp_0..2 (346 + 559 + 523), v2bg (499), v2cm (55), v2wp (155), props (55), misc (11).
* `.rel.data` is all `R_MIPS_32`. Against the section symbol (index 1) the stored word is the target offset in
  `.data`; against an undefined symbol the word is 0 and the loader supplies the address (render methods
  `ParentRM_*`, shader microcode `*__EAGLMicroCodeVS/PS`, texture slots, `__const MATRIX4:::...` matrices,
  `__EAGL::GeoPrimState:::...` state blocks whose names spell out the state setters).
* Typed objects are global symbols `__<Type>:::<name>` in `.data`: `__Skeleton`, `__Bone`, `__Model`, `__BBOX`,
  `__RM`, `__VB`, `__EAGL_TOOLLIB_VERSION` (0x10000 at offset 0).

## Skeleton (`__Skeleton:::root`)

Header 16 bytes: {u32 hash, 0, bone count (94), 0}, then one 0x70-byte record per bone:

| Offset | Field |
|---|---|
| +0x00 | scale float3 (always 1) |
| +0x0C | parent index i32 (-1 for root) |
| +0x10 | rest rotation, quaternion x, y, z, w, relative to the parent |
| +0x20 | rest translation float3, in the parent's frame |
| +0x2C | mirror bone index i32: the left/right counterpart, itself for centre bones |
| +0x30 | inverse bind matrix, 4x4 row-major for row vectors (D3D); its 16 floats in file order are glTF's column-major layout |

`__Bone:::root.<name>` records are {index, 0, 0, 0} and name the bones. House: 35 mirrored pairs whose bind
positions mirror in x to 0.007 units, 24 centre bones. The rest TRS chain reproduces the inverse bind matrices to
1e-3 for 90 of 94 bones; `PonyTail2`, `placellip`, `placerlip` and `Pendant2` differ (the rest pose is not the
bind pose for those), so the exporter poses nodes from the inverse bind matrices and keeps the TRS as extras.
Frame: +Y up, the fighter faces +Z, its left side at +X, feet at y = 0 — glTF's frame, no conversion.

## Vertex buffer (`__VB:::__GPvb_<15+stream>_<draw>_<model>`)

Descriptor {ptr trailer, byte length, stride, 1, ptr data}, data at descriptor + 20, `bb bb bb bb`, trailer
{1, ptr data, 0, 0}. Verified consistent for all 15,661 fighter buffers. Stream numbers 15..19 are position,
UV, colour, normal, skin.

## Render model (`__RM:::__GP_RM_<model>_<n>`)

| Offset | Field |
|---|---|
| +0x00 | ptr parameter descriptor |
| +0x04 | pass count (shader variants) |
| +0x08 | ptr vertex declaration |
| +0x0C, +0x14 | ptrs to the vertex / pixel shader imports per pass |
| +0x1C, +0x2C | the render method import `ParentRM_*` |
| +0x28 | -1 |
| +0x30 | ptr label `<model>_<n>_RM = <method>` |
| +0x34 | `dd dd dd dd` |

The **descriptor** is a list of entries terminated by a 0 word; each entry's first word is `type << 16 | length
in words`. Every entry except types 0x01, 0x04, 0x19 and 0x1A consumes one parameter of the draw, in order:

| Type | Meaning | Extra words |
|---|---|---|
| 0x03 | ComputationIndex | — |
| 0x10 | GeoPrimState (primitive type, blending, depth test) | — |
| 0x0F | texture slot (TAR) | two words, not decoded |
| 0x1F | skin weight palette | — |
| 0x1D | matrix palette (`EAGLAnimationBuffer`) | 0, then {start register, register count} pairs |
| 0x02 | strip lengths (u32 each) | — |
| 0x0E | indices (u16) | — |
| 0x14 | view-port matrix (`gpModelViewProjectionMatrix` ...) | 0, constant register |
| 0x05, 0x06, 0x23 | constants and blocks (lighting, irradiance, env colours) | 0, register, size |
| 0x22 | (venues only) not decoded | — |
| 0x09 | vertex stream | 0, stream, stride |
| 0x1A | draw indexed (no parameter) | 0, stream count |
| 0x01, 0x04, 0x19 | point-sprite state and draw (no parameter) | — |

The **vertex declaration** is pairs {0x2000_0000 + stream, 0x40_FF_00_00 + stream} ended by 0xFFFFFFFF, where FF
is the NV2A attribute format (low nibble type, high nibble count): 0x32 float3, 0x22 float2, 0x40 D3DCOLOR (B, G,
R, A), 0x16 packed normal (x 11, y 11, z 10 bits signed, /1023, /1023, /511), 0x15 one signed short, 0x42 float4
(venues).

## Draw (parameter block)

{ptr render model, then (count, ptr) for each consuming descriptor entry}. The count is the element count (1 for
a texture or matrix, the vertex count for a stream, the index count, the weight-entry count, 0x28 for the
palette). The first draw of a material group carries the full state (ComputationIndex, GeoPrimState, matrices,
lighting); later draws of the group carry only what changes (texture, weights, palette, strip, streams).

## Model (`__Model:::<name>_alpha`)

| Offset | Field |
|---|---|
| +0x00 | ptr parameter-name table (`ComputationIndex::CompIdx`, `Coordinate4::EnvmapColour1`, ...) |
| +0x0C | 4x4 identity matrix, then bounds (not decoded field by field) |
| +0x9C | material count |
| +0xA0 | ptr material name list (`emc4_cloth~DJIrradTextureEnvmap_Skin4`, `nospec`, `Direction`, ...) |
| +0xB4 | ptr model name |
| +0xC8, +0xCC | LOD count (1), ptr LOD: {u32 0, then per material: draw count, draw ptrs} |

`__BBOX:::<name>` is min xyz, max xyz. The group counts add up to the `__RM` count in all 78 fighters, 305 v2cp_0
parts and 317 venue models. The `Direction` material is a 10-vertex ground arrow (facing marker), not body;
the exporter leaves it out unless asked.

## Skinning

* Weight palette (`0x1F`): 16 bytes per entry, {w0, w1, w2, packed}, weights summing to 1, followed by one zero
  entry. `packed` is a float holding an integer whose bytes 3, 2, 1 are the bone indices for w0, w1, w2
  (e.g. 0x0C0D5800 = hip 12, waist 13, R_leg1 88). Up to 40 entries per draw; that limit is why a fighter is cut
  into 30-50 draws.
* Matrix palette (`0x1D`) words {0, -96, 56, -36, 104} in every fighter: register ranges c[-96..-41] and
  c[-36..67], four registers per slot, 14 + 26 = 40 slots (c[-40..-37] stays free for another constant).
* Skin stream value *v* = the slot's first register; slot *k* holds the blended matrix of weight entry *k*.
  So entry = (v + 96) / 4 for v < -40 and 14 + (v + 36) / 4 for v >= -36.
* Checked spatially on House: the dominant bone of 82% of vertices is the nearest bone segment or one adjacent
  to it (the single-range readings score 48% and 76%, and leave 598 and 94 vertices without an entry); a
  rotated `L_arm1` deforms the arm and shoulder cleanly in a skinned render of the export.

## Verification of the House export (`V2IP_003A`)

40 primitives (41 draws less the helper), 4754 vertices, all skinned, 5505 triangles, 3 images (`A003` 512x512,
`br01`, `br02` 256x256). Bounding box x -11.41..11.41, y 0..21.49, z -2.64..3.08, matching `__BBOX`
(the box's z -4..5 includes the helper arrow). A front render of the glTF shows the textured fighter in the bind
T-pose. All 70 `V2IP_xxxA` fighters export with heights between 20.9 and 22.6 units.

## Still unknown

* The remaining descriptor types' payloads (0x03, 0x05, 0x06, 0x22, 0x23): which constants, which registers.
* Model header fields between +0x0C and +0x9C beyond the identity matrix; the LOD list's leading word.
* `_F.o` face-pose banks and the clothing morph objects (`_A _B _D _E _F`): ELF objects without a model; their
  layout is the next format to read for outfits.
* Point-sprite draws (bling, types 0x01/0x19) are skipped by the mesh export.
* The irradiance / env-map shading (`DJIrradTextureEnvmap_Skin*`) is approximated by a plain textured material.

## Tool

```
cd tools/ffny-studio
python -m ffnystudio list v2ip.viv
python -m ffnystudio info v2ip.viv V2IP_003A.o
python -m ffnystudio export-fighter V2IP_003A <out-dir>
```

Output goes only to the folder given; exports are game data and stay local.

The Blender add-on (import of fighters and clothing parts, 2026-10-25) is described in
[tools/ffny-studio/README.md](../../tools/ffny-studio/README.md); it builds on the same views through
`ffnystudio.blender_data` and decodes SHPX without Pillow (`ffnystudio.texture`).

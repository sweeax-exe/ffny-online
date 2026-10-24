# FFNY Studio, stage 0: the game's asset formats (2026-10-24)

Read-only survey of the user's own dump (`game/`), no game data copied anywhere; hex below are short header
snippets only. Done by a research agent; claims marked "not verified" still need a check.

## Summary

1. All 3D data (fighters, clothing, venues, props, crowd, weapons, animation) is **32-bit little-endian ELF
   relocatable objects** (`.o`, e_machine 8 = MIPS, PS2-style e_flags 0x20924000) with a single `.data` blob;
   pointers inside it are fixed up by `.rel.data` (R_MIPS_32). Symbols name the objects (`__Bone:::root.L_arm1`,
   `__RM:::`, `__VB:::`, `__Model:::`, `__Skeleton:::root`).
2. The engine is **EAGL** (EA Graphics Library) plus the **AKI** gameplay engine (Def Jam Vendetta lineage);
   internal name "djv2" (XBE PDB path `D:\djv2\Code\Game\xbox\Game.pdb`). Not RenderWare; "MAD" is only the video codec.
3. Textures are `.xsh` = `SHPX`, already decoded by `scripts/hd_assets.py` (8-bit paletted, swizzled with flag 0x2000).
4. Vertex data: separate streams (float3 position, float2 UV, colour, NV2A 11:11:10 packed normal, 2-byte skin
   stream), triangle strips. Shaders are NV2A microcode in `misc.viv/djv2rm.o`.
5. Every fighter, clothing and morph `.o` shares the same **94-bone skeleton**: outfits are skeleton-compatible.
6. `fighter.xml <model>` names a basename in `assets/v2ip.viv` (`V2IP_003A.o` + `.xsh` + `_F.o`); `<coreID>` selects
   AI (`ai.viv: char00NN.xml`) and probably the move set (`motion/wres_all.viv`, `wresNNN`, not verified).
   55 created-player slots (`NULL`) are assembled from `v2cp_*.viv` parts listed in `createplayer_parts.xml`.
7. Outfits are **separate meshes per item** (`V2CP_<cat>_<n>_C.o`) with texture swaps/recolours
   (`..._A01_01..05.xsh`) and 5 body-type morph targets per item (`_A _B _D _E _F`).
8. Venues: `venue.xml` (48 venues) -> `assets/v2bg.viv` (`V2BG_xx`). Crowd, rope, ring-out, weapon and light
   positions, hazards and camera values are text XML; geometry is `.o`. `.cli` (`CLLI`) and `.jdi`
   (`TIME`/`TRAP`/`HSEM`/`BOLG`) are binary and unidentified. No explicit spawn or collision table found.
9. No community tool or write-up for FFNY models or EAGL ELF objects was found; only the BIG/VIV container has
   tools (unvivtool).
10. Difficulty: fighter export to glTF about 1-2 weeks; texture/outfit recolour easy; outfit mesh replacement
    medium-hard (needs an ELF writer); new character slot hard; venue XML placement easy, geometry hard.

## 1. Containers

118 files, 2.9 GB, only `.viv`. Header `BIGF` (or `BIG4` for screens.viv, misc.viv): bytes 4-7 total size
**little-endian** (the Xbox variant), 8-11 entry count BE, 12-15 header size BE, then {offset BE, size BE,
NUL-terminated name}. Most entries are **RefPack** (`10 fb` + 3-byte BE size); nested archives are BIGF inside
RefPack or plain BIGF.

| Archive (assets/ unless noted) | Entries | Contents |
|---|---|---|
| v2ip.viv | 234 | fighters: 78 body `.o`, 78 `_F.o` face-pose banks, 78 `.xsh` |
| v2cp_0..3.viv | about 8000 | clothing/parts: 2631 `.o` (about 1900 morphs), 5319 `.xsh`, `createplayer_parts.xml`, `cp_def0..3.xml` |
| v2cm.viv | 128 | crowd members |
| v2bg.viv | 860 | venues: 499 `.o`, 77 `.xsh`, 75 `.xml`, 110 `.irr`, 43 `.jdi`, 31 `.cli`, 25 `.pfx` |
| v2bg_cin.viv, v2ip_cin.viv | 27 / 56 | cinematic hi-res venues/fighters (nested BIGF) |
| v2wp.viv, v2ga.viv, props.viv, shops.viv | | weapons, props, shop sets |
| misc.viv (BIG4) | 26 | `djv2rm.o` (render methods, NV2A microcode), shadows, HUD |
| motion/wres_all.viv | 423 | move sets: `.mnl` name lists, `.ord` + `.orl` = one ELF split in two |
| motion/main_all.dat | | `WAZA` |
| screens/screens.viv | 8795 | Apt/Flash UI |

## 2. Fighters

`fighter.xml`: 142 records (77 `V2IP_xxxA`, 55 `NULL` created-player slots, 10 `EMPTY`). Checho points at
`V2IP_017A`, which is not in v2ip.viv. Alternate outfits are separate basenames (014A-D and others).

`V2IP_003A.o` (House): sections `.data` (0x38c10), `.shstrtab`, `.strtab`, `.symtab` (381 symbols), `.rel.data`
(1629 relocations); no `.text`.
* `.data` starts with `__EAGL_TOOLLIB_VERSION` (0x10000); 0x10..0x2a0 = 42 skin weight entries `{w0,w1,w2,packed}`.
* `__Skeleton:::root`: 94 bones, 0x70-byte records (matrix, inverse, scale, links; parent indices not decoded).
  Bones include body, fingers, hair, jaw, lips, brows, eyes: facial animation is bone-driven.
* 41 render models `__RM:::__GP_RM_V2IP_003A_alpha_N`, 203 vertex-buffer descriptors; each RM references a
  `ParentRM_*` render method (`DJIrradTextureEnvmap_Skin1/2/4`, `IrradTexture_Skin`, `LitGouraud`).
* Vertex buffer: descriptor `{ptr trailer, byte length, stride, 1, ptr data}`, data, sentinel `bb bb bb bb`, trailer.
  Streams: position float3 (12), UV float2 (8), colour (4), packed normal 11:11:10 (4), skin int16 (2).
* Bounding box: about 21.5 units tall, ground at y=0, +Y up.
* Not decoded: strip boundaries per RM, the exact skin mapping (weight table + int16 stream + a u16 table at
  0x2c0), bone parents, the texture assignment (TAR) per RM.
* Texture `V2IP_003A.xsh`: SHPX with 3 images (`A003` 512x512, `br01`, `br02` 256x256), 8-bit paletted.

Clothing: `createplayer_parts.xml` lists every part type (upper body 94 models / 384 textures, lower body 88 / 555,
feet 28, caps 39, hair 43, ...). An item `V2CP_111_0001_C.o` has the same layout and skeleton; its texture is a
placeholder `cp_a` chosen at run time from the part list; 5 morph files fit it to the body types.

## 3. Venues

`venue.xml`: 48 venues (`ARENA_BG01A` -> `V2BG_01A`). A venue (`V2BG_02A`, Foundation) is the main `.o` (51 RMs),
`.xsh` textures, an env map, breakable objects with geometry states (`base`, `break1..3`) and morphs, lighting
`.irr`, post effects `.pfx` (text), `.cli`, `.jdi` and two text XML files:
* `V2BG_02A.xml` (`<stageenvironment>`): hazards, crowd voltage, camera, particles/lights;
* `V2BG_02A_O.xml` (`<stageobjects>`): object states, morphs, positions: crowd (`translate ... rotate ...`), alpha,
  weapons, ropes, ring-outs, guests, crowd instances, venue animation, subway match width.

Not found: fighter spawn table, ring collision, navigation; the ring size is probably in code.

## 4. Engine

EAGL object files (ELF) with EAGL's Xbox back end: XBE strings such as "forgot to load eaglrm.o before a model
ELF", `EAGL::XBOXTM_SWIZZLE`, `CArenaObject::LoadXML`, `%s_F.o`, `%s.irr`, `%s.jdi`, `%s.pfx`; AKI ("waza").
The ELF objects carry PS2 flags (authored with the PS2 toolchain, reused for every platform).

## 5. Community knowledge found

Containers only: [unvivtool](https://github.com/bfut/unvivtool) (BIGF/BIG4, not verified against the Xbox size
field or RefPack), [OpenSAGE BIG notes](https://opensage.readthedocs.io/file-formats/big/),
[VIV on Just Solve](https://fileformats.archiveteam.org/wiki/VIV). EAGL background: OpenUG2 (a different EAGL
game's data). Xbox GPU: xboxdevwiki NV2A pages. Texture packs for the GameCube version exist (Dolphin forum).
**No** model extractor, Blender/Noesis plugin, venue tool or format write-up for FFNY/Vendetta was found.

## 6. Assessment and stage 1

| Goal | Still to reverse-engineer | Difficulty |
|---|---|---|
| Export fighters to glTF | strip ranges, skin weights/indices, bone parents, texture per RM | medium, 1-2 weeks (+1 for weights) |
| Replace an outfit mesh | an ELF writer (data, VB descriptors, RM tables, relocations, symbols), the morph format | texture swap easy; mesh 2-4 weeks |
| New character slot | XBE tables sized by fighter count, select-screen art, move sets | hard (reusing an `EMPTY` record and an existing move set: medium) |
| Edit a venue | XML placement known; geometry needs the ELF writer; `.cli`/`.jdi` unknown | XML easy, geometry hard, new venue very hard |

Stage 1: a read-only Python library ("eagl-elf": ELF + symbol-typed structures + SHPX via `hd_assets.py`),
validated by a byte-exact round trip (parse -> re-emit), then export House (`V2IP_003A`) and one upper-body part
to glTF, and the Blender add-on's importer on top.

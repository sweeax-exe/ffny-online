# FFNY Studio, stage 2: exporting changes back into the game (2026-10-26)

Read-only design study. Nothing was edited, built or run (no game, no Blender); structural facts about the dump come from
the studio's own read-only library run over the player's own files, and no game bytes are copied here. Follows
[ffny-studio-formats.md](ffny-studio-formats.md) and [ffny-studio-eagl.md](ffny-studio-eagl.md). Tags: **[V]** read in code or
measured in the dump, **[I]** inferred, **[U]** not known, with how to settle it.

Goal: community mods as data-only packs that never contain EA data (only the modder's changes) and are applied at run time
over the player's own dump, signed by our server, installed by the launcher, and compared between players in online rooms.
Order of content: re-textured outfits, then re-shaped outfits, then new characters, later maps.

## 0. Short answer

1. **Textures need no new engine code.** The runtime texture pack already replaces any texture by the SHA-256 of its guest
   bytes (`texture_pack.c`), including 8-bit paletted ones, at 1x to 8x the original size, with the original palette ignored.
   A re-textured outfit is "paint the image in Blender, write a PNG named by the original's runtime ID, list it in a
   `manifest.ini`". One real unknown gates it: whether the create-a-player clothing textures reach the GPU unchanged, so that
   the archive ID equals the runtime ID. A `[textures] dump` run settles it in minutes (section 2.5).
2. **Meshes need a file-level override, built locally.** The port already serves `<data>/override/<relative path>` instead of
   a game file (`kernel_path.c`, used by `scripts/apt-patch.py` for `screens/screens.viv`). A pack installer composes patched
   copies of only the touched archives (`v2ip.viv` 27 MB, `v2cp_1..3.viv` 26 to 102 MB), appending the modder's entries and
   re-pointing their directory records, exactly as apt-patch does. No guest code changes. An entry-level hook in the game's
   own file layer would be cleaner for the player (no archive copies) but needs reverse engineering and a re-lift; keep it as
   a later optimisation.
3. **The mesh writer is real work, not a patch.** Vertex or index counts change a dozen linked fields and five morph
   objects per part. The one genuinely hard step is the skin: at most 40 distinct weight blends per draw, selected through a
   constant-register number in the 2-byte skin stream. Plan the mesh export in two steps: same-topology edits first (move
   vertices only), then new topology.
4. **Pack = signed zip of manifest + payloads + hashes.** The payloads carry no EA bytes: textures are PNGs, meshes are a
   neutral geometry format that the installer grafts onto the player's own original object (skeleton, render methods, shader
   names stay the player's). The server signs the manifest (ECDSA P-256, like the updater's manifest). Rooms compare an
   8-byte content fingerprint next to the existing build fingerprint.
5. **First milestone (3 to 5 days):** "paint an outfit or a fighter texture in Blender, File > Export > FFNY texture pack,
   add one line to `settings.ini`, see it in game."

## 1. How the game loads a modified asset (question 1)

### 1.1 What exists today

| Mechanism | Where | Granularity | Notes |
|---|---|---|---|
| Texture packs | `tools/xboxrecomp/src/nv2a/texture_pack.c`; `try_texture_pack` in `kernel/nv2a_pb_exec.c`; settings `[textures] enabled / packs / cache_mb / dump` in `src/hooks/pc_settings.c` | one texture, by content ID | [V] Pack folders under `<data>/mods/<name>/manifest.ini`; later packs win; PNG or DDS; 512 MiB default cache; restart to change |
| File override | `xbox_path_set_override_dir` in `kernel/kernel_path.c`, set to `<data>/override` in `src/main.c` | one whole file, by path relative to the game folder | [V] Applies only to paths under the game directory, only when a file of that relative path exists; one `[PATH] override:` log line per use; Windows only (the POSIX branch of `kernel_path.c` has no override) |
| Archive patch tool | `scripts/apt-patch.py` | one entry of an archive | [V] Appends the new RefPack-wrapped entry to a copy of the player's own `screens.viv`, rewrites that entry's directory record (offset, size) and the little-endian total size in the header; hash-gated on the source; verifies its own output. Entry alignment 4. Live in the shipped builds |
| Launcher job runner | `src/hooks/hd_build.c` | spawns a script, reads `PROGRESS a / b` lines | [V] Precedent for an "apply packs" step run from the launcher |

The game itself reads an archive entry through its own file layer ([V] `rollback-async-loads.md` section 2.3: path strings
of the form `assets/v2cp_1.viv|name.o`, a worker thread that calls `NtReadFile`, `sub_001E46E0` = read and decompress a whole
file into a heap block, a RefPack decompressor at `sub_001E9000`). The kernel side is thin: `xbox_NtCreateFile` opens the
host file (after the path translation that applies the override) and `xbox_NtReadFile` is a plain `ReadFile` at the offset
the game asks for (`kernel/kernel_file.c`). Handles are raw Win32 handles.

### 1.2 Options for meshes and other files

| Option | How | Verdict |
|---|---|---|
| A. Whole-file override composed at install | Pack installer builds patched archive copies under `<data>/override/assets/` from the player's archive plus the pack's entries | **Recommended for stage 2.** Proven by apt-patch, no engine change, instantly revertable (delete or rename the folder; `override.off` already exists in the data folder as such a switch). Cost: disk (below), and a compose step when the set of packs changes |
| B. Virtual archive in the file IO layer | In `xbox_NtCreateFile`/`NtReadFile`/`NtQueryInformationFile`/`NtSetInformationFile`/`NtReadFileScatter`: for a given archive path serve the original bytes, a patched directory block, and appended replacement bytes past the old end | Same result as A without copies. About 200 lines in the toolkit fork plus every file-info call that reports size or position; any miss shows up as a hang or short read. Do later, only if copies become a problem |
| C. Entry hook in the guest file layer | Replace the "load a whole entry" path (`sub_001E46E0` and the open-by-name functions under it): look for `<mods>/files/<archive>/<entry>` first, allocate the block with the game's allocator and return it as if read | Cleanest for players: per-entry, no copies, pack removal is trivial. Costs: a day or two of reverse engineering (arguments, the async worker, the second block the decompressor needs, what the callers assume about the size), a re-lift and a hand-written replacement in `src/recomp_manual.c` (the function has direct callers, so `_enter` or `_gen` wrapping cannot replace its result, see CLAUDE.md section 4) and rollback review (file buffers live in the rolled-back heap, `rollback-async-loads.md` section 3). Keep as stage 3 |
| D. Patch the mesh after it is loaded | Edit the guest ELF image before `sub_0010B430`/`sub_0010AC20`, or the contiguous vertex copies afterwards | Rejected. After instantiation the vertex and index data have been copied into host-visible contiguous memory by the type handlers; only same-count position edits would be possible, and the instantiation is not rewound by rollback |
| E. Texture by ID | The existing texture pack | **Use for every texture.** No archive touched at all |

Option A detail. Composing patched archives needs:

* `ffnystudio/viv.py` already has the pieces: `Archive` (directory parse, lazy reads), `refpack_store` (valid literal-only
  RefPack), `build_archive`. Add `replace_entries(src, dst, {name: bytes})` that follows `apt-patch.py` (append at the end,
  align 4, rewrite the record, set the LE total size, verify). Adding new entries (new parts) grows the directory, so the
  archive must be rewritten with every offset shifted (`build_archive` does this); plan for it, but for replacements the
  directory size does not change.
* Measured facts about the real archives [V]: `v2cp_0..3` and `v2ip` are `BIGF`, entries sorted by offset, 4-byte aligned
  (not 2 KB), small gaps between them, the file ends exactly at the last entry, the little-endian total equals the file
  size, **every** `.o` and `.xsh` entry is RefPack. A literal-only RefPack costs about 3% more bytes than the original and
  is what apt-patch already ships; a real RefPack encoder is a later size optimisation.
* Disk cost per touched archive is its full size (27, 9, 102, 80, 26 MB for `v2ip`, `v2cp_0..3`), so a heavy user with
  mods in all five adds about 245 MB. Composed output is a cache keyed by the pack-set fingerprint.
* Two packs touching the same entry: later pack wins and the installer reports the conflict; two packs in the same archive
  are merged into one override file. A pack that targets `screens.viv` is refused while apt-patch owns that file.
* The apply step must run before the game starts (the launcher). Both packs and overrides are read at boot, so changing the
  set needs a restart (the texture settings are already `RECOMP_SETTING_RESTART`).

### 1.3 Risks, with evidence

| Risk | State |
|---|---|
| Does the game verify a checksum or signature of archives or entries? | [U], no evidence of one. The `BIGF` header holds only a total size (LE), entry count and header size; entries are opaque RefPack. The apt-patch override (changed entry offset, size and total) loads, which shows the directory is trusted, not summed. Settle for models with a no-op test: compose an override that re-stores one real `.o` literal-only and load it in a fight |
| Does another file hold absolute offsets into the archives (an index such as `djv2.sod`)? | [U]. Check by searching the dump's small non-archive files for the big-endian directory offsets of one archive |
| RefPack variants | [V] the decoder in `scripts/hd_assets.py` and the writer in `viv.py` agree on the `10 fb` form with a 3-byte size (a 3-byte size limits one entry to 16 MiB, far above any outfit). The game also has a "refpack file with a 0x10-byte header" request type (op 9); it is not what archive entries use |
| Directory growth | A new entry needs a bigger header. `Archive.header_size` is stored and the first entry sits after it (13,696 vs a 13,586-byte directory in `v2cp_0`, so the first offset is not exactly the header size: keep the original slack rule when rewriting) |
| ELF loader tolerance | The game's EAGL `DynamicLoader` parses the image in place (hash buckets over the symbol table, relocation types 2/4/5/6/9, type handlers per `__Type:::` symbol). It accepts what the original tool writes; anything outside that is untested. Byte-exact re-emit of every original `.o` is verified (stage 1), so the writer has a regression oracle |
| Heap and contiguous memory | The game's heap block is about 42 MB (`0x80003000..0x82A0D000`, fixed 14 MB held back) and model instantiation copies every vertex and index buffer into the 64 MB contiguous window, a bump allocator with reclaim on (`RECOMP_HEAP_RECLAIM`, since build 17). A House-sized fighter is 4,754 vertices; a clothing part about 1,000 (111_0001: 1,035 vertices, 99 KB object, 13 draws). Packs should be capped per part (suggest 2.5x the original vertex count and an absolute 6,000, to be tuned by measuring a four-fighter match; **[U]** until measured). Models load during match set-up, not in a fight, which keeps them away from the rollback problems of runtime loads |
| Host-side texture memory | Replacement textures live in the host's D3D11 cache (`cache_mb`, default 512), not in the Xbox heap. A 4x 512x512 texture is 4 MiB plus mips |
| Platforms | Both the texture pack loader (WIC, BCrypt) and the override directory are Windows-only today; the macOS and Linux builds render the original. Extending the override to the POSIX branch of `kernel_path.c` is small; PNG decoding there is the larger job |
| Changes inside the toolkit | Any change in `tools/xboxrecomp/` (file IO virtualisation, the override on POSIX) is a fork commit and a gitlink update; upstream PRs need the owner's approval |

## 2. Writing the formats (question 2)

### 2.1 What a clothing part is [V]

A part `V2CP_<category>_<n>` is six objects in one `v2cp_<k>.viv` (the archive follows the category: 110 to 115 in `_1`,
120 to 123 in `_2`, the 13x/14x/3xx/4xx categories in `_3`, heads/eyes/faces/etc. in `_0`; how the game picks the archive is
**[U]**). `_C.o` is the geometry. `_A _B _D _E _F` are the five body-type morph objects. The textures are
`<part>_A01_01..05.xsh` and, for some parts, other groups (`_C01_01`, `_G01_01`), each a one-image SHPX (`111_0001`: image
`0000`, 256x256, 8-bit paletted, 66,896 bytes). The model references a run-time texture slot named `cp_a`
(`SHAPENAME=cp_a`) in 94 of the first 200 parts; the SHPX image is named `0000`, so the game binds the chosen texture to
`cp_a` by a name it assigns at run time. `createplayer_parts.xml` (516 KB, in `v2cp_0.viv`) lists, per category, models with
their texture names and counts, plus colour lists; `cp_def0..3.xml` hold defaults.

`_C.o` of `111_0001`: 13 render models (draws), 63 vertex buffers, 1,035 vertices of which 23 belong to the `Direction`
helper arrow, 94-bone skeleton, 3 texture slot imports, 30 imports in all, 650 relocations. Each morph object is the
skeleton copy plus a `__Morph` record and about 12 bytes per vertex (24,448 bytes data, 4 relocations; the payload is
12,324 bytes for 1,012 non-arrow vertices, so beyond 12 bytes per vertex there are about 180 bytes of framing). Header word 8 of the
record equals the number of non-arrow draws in 29 of 29 parts sampled (`111_0001` to `111_0028`). The payload looks like float3 deltas per vertex (first values 0, 0.045,
0.085) **[I]**; its exact framing and the meaning of the flag word (`0xc00001b0`, `0xc0000030`, `0xc00008ac` seen) are
**[U]**. Decode by applying the deltas to the `_C` positions and checking the shape against the body types.

### 2.2 Fields that must change when geometry changes

Because pointers are stored as offsets plus a relocation table, a changed vertex or index count moves everything after it.
A writer should therefore **regenerate `.data` from a logical model** (the layout order is known from the originals) rather
than splice bytes, and use the existing `elf.build_object` for the container.

| Item | Stays | Must be rewritten when counts change |
|---|---|---|
| Skeleton (`__Skeleton:::root`, 94 `__Bone` records, hash word in its header) | yes, copy as is. Keep the hash: it probably identifies the skeleton for animation retargeting **[I]** | nothing |
| `__EAGL_TOOLLIB_VERSION`, imports (render methods `ParentRM_*`, microcode `*__EAGLMicroCodeVS/PS`, `spectexture*`, `GeoPrimState` blocks, `const MATRIX4`) | yes, copy | nothing; new draws reuse existing imports |
| `__VB:::__GPvb_<stream>_<draw>_<model>` descriptors | structure | per stream: byte length (count x stride), the 20-byte descriptor, data, the `bb bb bb bb` sentinel, the trailer {1, ptr data, 0, 0}; symbol names for added draws; symbol value and size |
| Vertex streams per draw | formats (float3 position, float2 UV, D3DCOLOR, packed 11:11:10 normal, int16 skin) | new data; normals through `eagl.encode_normal_cmp` (exists), UV v flipped back, colour BGRA; streams of different draws can have different vertex counts, and not every draw has every stream (in `111_0001`: 1,035 positions, 1,012 UVs, 1,018 skin values) |
| Draw parameter block `{ptr RM, (count, ptr) per consuming entry}` | one per draw | vertex counts per stream, index count, weight entry count, strip-length parameter (one u32 equal to the index count), pointers |
| Indices (type 0x0E, u16) and strip lengths (0x02) | one triangle strip per draw, repeated indices as degenerate joins | build strips from triangles (a greedy stitcher is enough), winding reversed relative to glTF (clockwise front faces), at most 65,535 vertices per draw, pad to 4 |
| Skin weight palette (type 0x1F): 16 bytes per entry `{w0, w1, w2, packed float of bone bytes 3,2,1}`, up to 40 entries per draw, followed by a zero entry | format | **the hard part.** Each vertex's blend must be one of at most 40 distinct entries of its draw; the skin stream stores the constant register of the entry's matrix slot, `slot = (v+96)/4` for `v < -40`, `14 + (v+36)/4` for `v >= -36`. An exporter must quantise Blender's per-vertex weights (at most 3 bones, weights summing to 1) to a palette, and split a draw when it needs more than 40 entries. This is why originals are 30 to 50 draws |
| Matrix palette descriptor (0x1D) `{0, -96, 56, -36, 104}` | yes | nothing |
| Texture parameter (0x0F, TAR import) per draw | yes | only if the material changes; a new texture name is a new import symbol (see textures) |
| `__RM` (render model) and the descriptor lists | clone the first draw of the same material as a template (the first draw of a material group carries the full state; later draws only what changes) | the RM pointer inside each new draw; `__RM:::` symbol names |
| `__Model`: material count (+0x9C), material-name list (+0xA0), LOD list (+0xCC: for each material the draw count and pointers) | names | draw counts per material, the pointer list; new materials only by copying an existing one's strings |
| `__BBOX:::<name>` (min xyz, max xyz) | symbol | recompute from vertices |
| `__Model` bounds between +0x0C and +0x9C | **[U]** (not decoded field by field) | must match the new volume if the engine culls with it; settle by re-exporting an unchanged part and diffing the object against the original, then changing one vertex far out and diffing again |
| `.symtab`, `.strtab`, `.rel.data` | structure | regenerate. One `R_MIPS_32` per internal pointer (stored word = target offset in `.data`) and per import use (stored word 0) |
| Morph objects `_A _B _D _E _F` | skeleton block | the payload, in the same vertex order as `_C`, for any change of vertex count or order. For same-count edits they can stay (they are deltas), at the price of slightly wrong fit on other body types. For new topology, **transfer** the original delta field to the new vertices (nearest or barycentric in the original `_C`) and verify all five body types in game |
| `createplayer_parts.xml`, `cp_def*.xml` | untouched when a pack replaces an existing part | for a new part: a new `<model>` element with its `<texture>` names in the right category. Append only: saved created players probably store indices into these lists **[I]**, so inserting in the middle would shift a saved outfit |

Alignment: `.data` is 16-aligned in the container; inside it vertex data sits at descriptor + 20 (4-aligned) and the game
copies it into contiguous memory itself, so 4-byte alignment suffices **[I]**. The `_F.o` face-pose banks of fighters are
not touched by clothing.

Practical staging of the mesh export:

* **2b, same topology:** keep every count, draw and index; replace only position (and normal) streams in place. This needs
  no relocation work: `EaglObject.data` is mutable and `to_bytes()` already writes a consistent container. Morphs stay.
  A modder can sculpt a hat, loosen a shirt or flatten a logo. Estimate 2 to 3 days including the override composer and a
  round trip test (export an unedited part, compare bytes to the original: must be identical).
* **2c, new topology:** the full writer above. Estimate 2 to 3 weeks: strip builder, weight palette quantiser and draw
  splitter, `.data` regenerator, morph transfer, then in-game fitting on every body type.
* **2d, new parts:** XML append ops, new entries (directory growth), a name allocator for `V2CP_<category>_<n>` (a number
  range reserved for mods so that nothing collides), and a check that the game finds a part's archive by name or category.

### 2.3 Can textures be swapped without touching any model file? Yes, within limits

The runtime ID is `SHA-256("XRTEX01\0" + LE32 {format, width, height, palette entries, row texels} + guest pixel bytes +
BGRA palette)` over exactly what the game uploaded (`texture_pack_hash`, `scripts/hd_assets.py texture_id`). For SHPX images
the archive ID the studio can compute offline is that same value for 8-bit paletted (format 0x0b, swizzled bytes plus
palette), 32-bit (0x06 or 0x12) and DXT (0x0c, 0x0e, 0x0f) images [V by the HD pilot: Blaze and 22 Foundation textures
matched archive IDs; 13 more matched only through decoded-pixel aliases in `config/hd-runtime-aliases.json`].

Conditions for a replacement to apply [V from `texture_pack.c`]:

* the PNG is a whole multiple (1x to 8x) of the source width and height; it is then padded to the physical row width and
  mip-mapped on the host, so UV layout, atlas layout and wrap behaviour are exactly the original's;
* the replacement is full colour with alpha: the 256-colour limit of the source does not apply, and the source palette is
  not used;
* `png_mips` in the manifest chooses how mips are made: `channels` (colour and alpha reduced independently) for textures
  whose alpha carries data (the cloth and skin materials, `DJIrradTextureEnvmap_Skin*`, probably mask or spec in alpha:
  Blaze's alpha spans 1 to 255 and its meaning is **[U]**), `opacity` for ordinary transparency. Default to `channels`;
* the same bytes anywhere in the game share one ID, so a replacement also changes every other place that uses identical
  pixels and palette (a colour variant that happens to equal another). The tool must list all archive entries with the
  same ID before export and warn;
* dynamic or generated textures (render targets, run-time composites) are excluded by the loader's surface check and
  cannot be replaced this way.

What a texture swap cannot do: change UVs, change the geometry's texture count, add a new texture to an existing material,
add a whole new texture entry for a new part (that needs the archive path).

### 2.4 Pitfalls the Blender side must handle

* **Premultiplied alpha.** Set the imported images to `alpha_mode = CHANNEL_PACKED` and write the PNG from the raw float
  pixels (quantised to 8 bit, rows flipped back) rather than through Blender's colour-managed save, so alpha-as-data is never
  altered. The importer today decodes with `texture.blender_pixels`; the exporter needs the exact inverse.
* **Detecting a changed image:** store the decoded pixel hash at import (`ffny_pixel_hash`) and export only images that
  differ; never write unchanged ones (they would be EA data, section 3.4).
* **Five colour variants** per part (`_A01_01` to `_05`): five images, five IDs; the exporter offers "apply to all variants".
* **Fighters:** `V2IP_003A.xsh` has three images (`A003` 512x512, `br01`, `br02`), each its own ID. A fighter texture swap
  affects every use of that fighter.
* **Order against the HD pack.** `packs=` is applied in order and later wins. Put a mod pack after the HD pack, and advise
  authors to paint at 4x to match HD output (any 1x to 8x works).

### 2.5 The decision gate: do create-a-player parts keep their archive ID at run time?

`cp_a` is a run-time slot; all parts use that same name, and the SHPX image inside is `0000`. If the game copies or
composes part textures into a shared atlas, the bound texture would not match any archive image and texture swaps would not
apply to parts (they would to fighters, which bind `A003` directly). Settle it first, in about ten minutes of playtime:
enable `[textures] dump=1`, equip one part in the creator, then compare the IDs in `textures.jsonl` (`id`, `decoded_id`) with
the IDs the studio computes for that part's `_A01_0n.xsh`. Outcomes: equal, the texture-pack route works for outfits;
different, fall back to **replacing the `.xsh` entry through the file override** (section 1.2 option A), which works either
way and also lets the modder ship a 32-bit SHPX.

## 3. Pack layout, signing and online comparison (question 3)

### 3.1 Principles

* A pack contains **only what the modder made**: PNGs, neutral geometry, XML edit operations, metadata. Never an ELF, a
  `.viv`, an `.xsh`, a skeleton, a render-method name list or any decoded EA bytes.
* The installer **builds** game files on the player's machine from the player's own dump plus the pack, and refuses if the
  dump does not match what the pack was made against.
* Everything in a pack is hashed in its manifest; the manifest is signed.

### 3.2 Layout

One `.ffnypack` file (a zip, stored or deflated; also accepted unpacked as a folder during development):

| Path | Content |
|---|---|
| `pack.json` | canonical JSON (sorted keys, UTF-8, no BOM): `schema` (1), `id` (random UUID chosen once per mod), `version` (integer, monotonic), `name`, `author`, `description`, `tool` (studio version), `base` (see below), `kinds` (list of `texture`, `mesh`, `xml`), `targets`, `files` |
| `pack.json` `base` | fingerprint of the dump the pack was made against: the SHA-256 of each source archive it touches (the same values `config/dump-manifest.json` already lists), so the installer can say "different game version" |
| `pack.json` `targets` | one record per change: `op` (`texture`, `mesh`, `part-add`, `xml-patch`), `archive` and `entry` (and `image` for textures), `base_sha256` (SHA-256 of the **decompressed original entry**, not shipped), for textures the 64-hex runtime `id` and the original `width`/`height`/`format`, the `payload` path and its `payload_sha256`, `png_mips` |
| `pack.json` `files` | `{path: sha256}` for every file in the pack, preview included |
| `textures/<id>.png` | one PNG per replaced texture; the file name is the runtime ID so the installer can emit a standard `manifest.ini` without a lookup |
| `mesh/<part>.fmesh` | the modder's geometry for one part (kinds below) |
| `xml/*.json` | operations for the parts list (append a model with texture names) |
| `preview.png` | optional, shown by the launcher (the modder's own render; it must not be a capture of the original) |
| `pack.sig` | 64-byte ECDSA P-256 signature (hex) over the exact bytes of `pack.json`, plus the signer key id; added by the server, not the author |

Mesh payloads (`.fmesh`: a small JSON header plus one binary block, or glTF with an `FFNY` extension; the studio already
writes glTF through `gltf.py`):

* **delta**: for each draw (named by the original's render-model index and material) the list of changed vertex indices with
  new position, normal and UV; counts and order stay the original's, so it targets same-topology edits and contains no
  original value (an unchanged vertex is simply absent);
* **full**: the modder's own triangles with positions, normals, UVs, colours, per-vertex bone names and weights, a material
  slot referring to one of the original part's materials by name, and an optional LOD-free flag. The installer strips,
  palettes and packs it (section 2.2) and builds the five morph objects by transfer.

### 3.3 Installing and diffing against the dump

`ffnystudio pack check <pack> --game <dump>` (and the same code inside the launcher) does, per target:

1. the archive and entry exist; the decompressed original hashes to `base_sha256` (else "built for a different dump");
2. for textures: the runtime ID computed from the player's entry equals the target `id`; the PNG size is 1x to 8x;
3. `payload_sha256` matches, and the manifest signature verifies against the pack key;
4. the "no EA data" tests of the next section;
5. a **diff report**: which entries each pack replaces, conflicts between enabled packs, and the disk and memory effect.

`pack apply` then writes (a) a generated texture pack under `<data>/mods/ffny-<fingerprint>/` (`manifest.ini` with schema 1,
`png_mips`, one line per ID, the PNGs hard-linked or copied), appended last to the effective `[textures] packs` list, and
(b) composed archives under `<data>/override/assets/` plus a receipt (`<data>/override/ffny-packs.json`: the pack-set
fingerprint, source archive hashes, output hashes) so a second run is a no-op and the game or launcher can tell whether the
override matches the enabled set. It is idempotent and atomic (write to `.tmp`, verify, rename), like apt-patch.

### 3.4 "No EA data" checks

Cannot be proven by software, only made hard to get wrong; the server's signing step should also be a human review.

| Check | Rule |
|---|---|
| Payload equals original | Reject any texture PNG whose RGBA equals the original decoded image (at 1x) or equals the project's own Lanczos 4x of it (the HD pack output is derived EA artwork). Reject a mesh delta that stores a value equal to the original at that index |
| Unchanged payload | Reject a texture of which, after reduction to the original size, more than a threshold of pixels (suggest 95%) is within 2/255 of the original; reject a full mesh whose position vertices are bit-equal to the original in more than a few percent, in order or anywhere |
| Foreign container | Reject ELF, `.viv`, `.xsh`, RefPack signatures and `SHPX`/`BIGF` magic anywhere in a payload, and any file not listed in `files` |
| Size sanity | Per part: at most the vertex, index and weight limits from section 1.3, texture at most 8x and 4096 per side (the loader's own bound), bounding box within 1.25x of the original (a hat is fine, a screen-filling mesh is griefing) |
| Scope | Texture, mesh and parts-list targets only. Reject targets that touch the simulation or the AI: `fighter.xml`, `*.xml` outside the parts list, `motion/`, `ai/`, `aki_cfg.xml`, `waza` data, the executable. This is also what lets cosmetic packs stay out of the lockstep checksum |

### 3.5 Signing and trust

Reuse the updater's scheme: ECDSA P-256 over SHA-256, verified with BCrypt (`src/hooks/updater.c`, `signature_ok`, public key
compiled in, private half kept with the publisher; `netplay/relay/cmd/updatesign` is the signing tool). Use a **separate
key pair** for packs so a pack-signing mistake cannot sign an update. The server publishes a signed `packs/index.json`
(`id`, `version`, `sha256` of the pack file, revoked flag), so a pack can be withdrawn. The launcher downloads by `id`, checks
the signature and hashes, and only then installs. The game process refuses to enable an installed pack whose receipt does not
verify.

### 3.6 Comparing packs in online rooms

* **Pack id** = SHA-256 of the canonical `pack.json` bytes (it already contains every payload hash).
* **Content fingerprint** = the first 8 bytes of SHA-256 over the enabled pack ids in effective order (order matters,
  later wins). The empty set has a fixed value (zeros), so a player with no mods meets the unmodded as today.
* Carry it like the build fingerprint (`netplay_build_fingerprint`, HELLO's 8-byte build field, room `STATUS` asks). The
  least intrusive form is a **separate** `content` field in the room creation and join messages and in quick-match pooling:
  rooms and quick match pools meet only an equal value. Folding it into the build fingerprint would also work but would stop
  the lobby listing rooms with a different set.
* Because the set is read at game start (textures and overrides need a restart), the unit of choice is the **equipped set**
  of a player, chosen in the launcher; the in-game hub then lists only rooms with the same fingerprint, and the launcher can
  fetch the missing packs for a room code before starting the game.
* Packs touch no simulation data (section 3.4 scope), so the lockstep checksum (fighters, generators, the rest) is
  unaffected and a mismatch only changes pictures; the equality rule is the owner's fairness and consistency choice and not a
  technical requirement for sync.
* A replaced outfit changes for everyone wearing that item, on every machine with the pack, so with replacement-only packs
  there is no "my custom outfit that only I wear": that needs additive parts (stage 2d) and, for online, the other player's
  copy of the pack. How created-player part choices travel between players in an online match was not studied here **[U]**.

## 4. First milestone: re-textured outfits and fighter skins (question 4)

"Paint a texture of an outfit or a fighter in Blender, export a texture pack, see it in the game." No game code changes, no
archive writing. Estimate 3 to 5 days.

### 4.1 Steps

| # | Work | Files and functions |
|---|---|---|
| 1 | **Settle the gate** (section 2.5): turn on `[textures] dump=1`, equip a part, compare IDs. Do this first, because a fail changes the milestone to an archive-entry replacement | read `<data>/hd-work/runtime/textures.jsonl` (`id`, `decoded_id`); `scripts/hd_assets.py inventory` gives archive IDs |
| 2 | Offline runtime ID in the Blender-safe library: `source_id(blob, offset)` returning (id, format, width, height, palette entries) without Pillow, a mirror of the `decode=False` branch of `hd_assets.shpx_images` | `tools/ffny-studio/ffnystudio/texture.py` (extend `decode_record`; keep the swizzled bytes and palette); parity test against `hd_assets` on synthetic SHPX in `tests/unit/test_ffny_studio_texture.py` |
| 3 | PNG writer with the standard library (`zlib`, `struct`) for the command line and tests; RGBA 8-bit | `texture.py: png_bytes(rgba, w, h)` |
| 4 | Pack writer: collects `{id: png}` and metadata, validates name (letters, digits, `_ - .`, as `valid_name` in `texture_pack.c`), 64-hex IDs, size multiples 1 to 8, mip policy, writes `manifest.ini` + `textures/` + `ffnypack.json` provenance (author, target archive/entry/image, original ID and size) | new `tools/ffny-studio/ffnystudio/texpack.py` with a Python mirror of the C manifest parser rules so a test can reject what the loader would warn about; new unit tests `tests/unit/test_ffny_studio_texpack.py` |
| 5 | Import tags: every Blender image gets `ffny_source_id`, `ffny_archive`, `ffny_entry`, `ffny_width/height`, `ffny_pixel_hash`, and `alpha_mode = CHANNEL_PACKED` | `ffnystudio/game.py` (`Asset` records where each image came from), `ffnystudio/blender_data.py` (`ImportPlan.images`), `blender/ffny_studio/build.py: create_images` |
| 6 | Export operator `File > Export > FFNY texture pack` and a sidebar button: lists changed images (hash differs), shows size check and shared-ID warnings, name and author fields, writes into the mods folder from the add-on preferences (default `$DEFJAM_DATA/mods`, else a folder picker), reports the two lines to put in `settings.ini`: `[textures] enabled=1` and `packs=<existing>;<new name>` | `blender/ffny_studio/operators.py` (new operator), `ui.py`, `preferences.py` (mods folder); stub-`bpy` structure test as the existing add-on test does |
| 7 | CLI twin for people who paint elsewhere: `python -m ffnystudio export-textures <fighter or part> <dir>` (originals as PNG with the target ID in the file name) and `python -m ffnystudio pack-textures <png dir> <name>` | `ffnystudio/__main__.py` |
| 8 | Verification in game: start with the pack, expect `[TEXPACK] loaded <id>` lines and `hits` in the closing `[TEXPACK]` line, take a capture; then remove the pack and confirm the original returns | fighter first (House `A003`, bound directly, proven path), then a clothing part (`V2CP_111_0001_A01_01`) |
| 9 | Documentation: a "Stage 2a" section in `tools/ffny-studio/README.md` including the legal note: share your PNGs and your pack, not the exported originals | README |

Test content for the first run: House `A003` recoloured (fighter, no create-a-player involved), then one shirt colour
variant, then a 2x or 4x repaint to check the scale path and the `png_mips=channels` effect on alpha.

### 4.2 What this milestone does not give

Same UV layout, same geometry, same part list; nothing is added; only the local machine sees the result; packs are not signed
or listed in the launcher yet. It is nonetheless a complete modder workflow (paint, export, play) and it exercises the pack
folder, the settings line, the ID computation and the Blender export code that stage 2b to 2d reuse.

## 5. Order of work after the first milestone

| Stage | Content | Depends on | Rough size |
|---|---|---|---|
| 2a | Texture packs from Blender (section 4) | the gate in 2.5 | 3 to 5 days |
| 2a+ | Pack format, `pack check/build/apply`, receipt, texture ops through a generated texture pack; launcher "Mods" page that enables packs; signing tool and index on the server (separate key) | 2a | 1 to 2 weeks |
| 2b | Same-topology mesh edits: `viv.replace_entries` composer, a no-op round trip of one real part through the override in a fight, position/normal export from Blender, delta payload | the no-checksum test in 1.3 | 1 week |
| 2c | New-topology writer: stripper, weight palette quantiser and draw splitter, `.data` regenerator, morph decode and transfer | 2b, morph decode | 2 to 3 weeks |
| 2d | New parts: XML append operations, new entries with directory growth, reserved number range, saves compatibility check | 2c | 1 to 2 weeks |
| 3 | Online: content fingerprint in the room protocol and relay, launcher fetch by id | 2a+ | about 1 week in the client and the Go relay |
| 4 | New characters and maps. Characters: a V2IP-style object and `.xsh`, a `fighter.xml` record (a loose file, so overridable), reusing an `EMPTY` record and an existing move set as the first target; XBE tables sized by the fighter count are the hard part. Maps: `venue.xml` and the `V2BG_xx.xml` stage files are text and can be patched as loose or archived files; geometry needs the 2c writer; `.cli` and `.jdi` are undecoded | 2c | open-ended, see stage 0 assessment |
| later | Option B or C (no archive copies); a real RefPack encoder; macOS and Linux override and PNG loading | | |

## 6. Open questions and how to settle them

| Question | Cheapest test |
|---|---|
| Do outfit textures reach the GPU with their archive ID? | section 2.5, ten minutes |
| Does the game check anything in archives or objects beyond the directory? | override with one real `.o` re-stored literal-only; then one with a vertex moved |
| Does any file hold absolute offsets into archives? | search the small non-archive files for a BE directory offset |
| Which archive does the game open for a part, and how does it find it? | log `[PATH]` lines while equipping parts from different categories; the lifted loader at `sub_00026940` builds the name |
| What is in `__Model` between +0x0C and +0x9C, and does the engine cull with it? | round-trip an unchanged part, then change one far vertex and diff |
| Morph record framing and flag word | decode with two parts of equal vertex count; apply deltas to `_C` and look |
| Meaning of alpha in skin and cloth textures | change alpha of a test pack texture to 0 and 128 in a fight |
| Per-part and per-match memory limits | four-fighter match with parts scaled 2x, 4x vertex counts; watch `[CONTIG]` and heap lines |
| Online sharing of created-player part choices | read the lockstep setup (`online-match-setup.md`) and the creator save format |

Files touched by this design (none changed by this study): `tools/ffny-studio/ffnystudio/{texture,game,viv,blender_data,__main__}.py`,
new `texpack.py` and later `pack.py`, `blender/ffny_studio/{build,operators,ui,preferences}.py`, `src/hooks/pc_settings.c`
(a `[mods]` section for the launcher), `src/hooks/netplay.c` and `netplay/relay` (content fingerprint), `src/main.c` (override
directory, unchanged), `tools/xboxrecomp/src/kernel/kernel_path.c` and `kernel_file.c` (only for options B or C and the POSIX
override).

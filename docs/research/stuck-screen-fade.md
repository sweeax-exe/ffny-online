# The black fight scene with the rollback beta: it is not a fade, it is the vignette read through a short palette

Read-only static study plus an analysis of the bug-report pictures and batch lists (nothing edited, built or run). Sources: the
lifted C in `src/recomp/gen/recomp_0006.c`, `recomp_0001.c`, `recomp_0007.c`, `src/hooks/rollback.c`, `src/hooks/netplay.c`,
`tools/xboxrecomp/src/kernel/nv2a_pb_exec.c`, the reports `logs/bug-reports/20261025-162943.976-cb2777`, `-163055.222-2f7042`,
`-163218.622-4cb55d`, `-163317.941-06a108` (all black) and `-164640.981-ed7519` (a later match, normal), normal reports from builds
18 to 25, and the structure (not the bytes) of the game's own `assets/tint.xsh`.

Tags: **[V]** read in code, a log or a picture; **[M]** measured from the pictures; **[I]** inferred; **[U]** open, with the probe that settles it.

## 0. Short answer

1. **The full-screen `FF000000` quad on texture 805ABD80 is not a fade and is not opaque.** Its render state is source factor ONE,
   destination factor CONSTANT_COLOR (0x8001), equation ADD, with blend colour `FFCDCFFF` in all four black reports. The result is
   the scene times (0.80, 0.81, 1.0) plus black: a colour grade, not a blackout. The same quad with the same blend state is in every
   fight frame of every build checked (blend colours `FFCFD9FF`, `FFF4C5B2`, `FFE2E1F0` in normal reports), including the later
   normal match `ed7519` in build 26. The texture it names (805ABD80 in the black reports, 8052D880 in `ed7519`, 80AECC00 in
   build 24) is whatever was left bound on stage 0; the combiner for this quad uses no texture (its batch says stage 00000000), so
   u = v = 0 samples nothing. **[V]**
2. **It is the post-effect manager** (`CPostEffectMan`, string at 0x2B3D80), singleton at 0x3A0188. The two quads at the end of the
   scene are its two quads: the tint quad (modulate and fill colour of the current preset) and the vignette quad. Section 2.
3. **What blacks out the picture is the second quad**: the vignette, a 128x128 palettised texture drawn with DST_COLOR times ZERO,
   so the picture is multiplied by the texture. In the black pictures the interior of that multiplier is exactly 0 (maximum
   value 1 to 5 in 40 000 interior pixels) and only a thin rounded frame at the edge survives. **[M]**
4. **That frame has the shape the vignette gets when every palette index of 32 or more reads as 0.** The vignette (first image of
   `assets/tint.xsh`) uses a 128-entry palette; its interior is index 125 (white), the border runs through lower indices. Cutting the
   palette to 32 entries predicts the visible margin as 5/10 px (left/right at mid height), 5/10 px (row 120) and 4/7 px (top/bottom
   at mid width); the pictures show 6/11, 7/11 and 4/9. 48 entries predicts 10/10, 64 entries 15/15, 96 entries 30/30. So the host
   expanded the vignette through **32 palette entries** (or through a palette whose entries from 32 up are zero). **[M]**, mechanism **[I]**
5. **No fade controller is stuck.** All the post-effect manager's state is in restored `.data` and its advance runs inside the re-run
   updates (only the ten sound handlers are muted there), so a load cannot leave it opaque. The lockstep match `ed7519` in the same
   build, with the vignette at the same address 8034DB80, draws normally, so the address and the texture data are fine in general; the
   fault is tied to rollback-active matches. Section 5.
6. The next step is a measurement, not a patch: dump what the host expanded and which palette length and address it was given
   (section 6). Mitigations that are safe to ship before that are in section 7.

## 1. The two quads in the batch list

Same two entries at the end of every fight frame, between the last scene batch (a glow sprite batch, additive) and the HUD.

| Batch | Texture and format | Vertex colour | Blend (src/dst, equation) | Blend colour | Meaning |
|---|---|---|---|---|---|
| tint quad | stale stage-0 texture, fmt 06, no stage used | FF000000 | ONE / CONSTANT_COLOR, ADD | `FFCDCFFF` (black reports) | scene = scene x modulate + fill (fill is the vertex colour) |
| vignette quad | 8034DB80, 128x128, fmt 0B (8-bit palettised), u and v 0..1 | FFFFFFFF | DST_COLOR / ZERO, ADD | 00000000 | scene = scene x palette[index] |

The render state of both batches (alpha test, depth, colour mask, cull) is byte-identical between the black report `cb2777` and a
normal build-24 report except the blend colour and the stale texture address. **[V]** (diff of the two batch blocks.)

Other facts from the black frames: 140 of 205 scene batches overlap the screen with about 57 000 triangles, so the scene is drawn; the
brightened picture shows scene colour at the very edge (a wall poster at the left, floor and a fighter at the bottom). **[M]**

## 2. The post-effect manager

| Item | Address or function | Notes |
|---|---|---|
| Singleton object | `0x3A0188` .. `0x3A079C` (0x3A079C bit 0 = constructed) | in `.data`; inside the restored range 0x345968..0x3A9324 and not in any skip or hole |
| Getter | `sub_000D8D90` | returns 0x3A0188, constructs on first use |
| Constructor | `sub_000D92C0`, called once at start-up from `sub_00011E40` | loads `assets/tint.xsh` through `sub_000D8880`, builds the glow, scratch, tint and vignette quads |
| Bus handler | `sub_000D8DD0` | ids F000000C, F0000013, F000002A, F000002B, F1000000, F1000001 set the preset index |
| Update (per game update) | `sub_000D90F0` | advances the transition counter and the factor |
| Apply (per render) | `sub_000D8C40`, reached only indirectly | blends the two presets into the live values and calls the draw |
| Draw | `sub_000D9AF0` (tint and vignette), `sub_000D8AD0` (scratches), `sub_000D99C0` (builds the tint and vignette quads), `sub_000D8930` (builds glow and scratch quads) | |
| Config parser | `sub_000D8320` | keys `ScreenTintEnable`, `ScreenTintVignetteEnable`, `ScreenTintVignetteID`, `TintModulateColorR/G/B`, `TintFillColorR/G/B`, `MinIntensity`, `GlowCompensate`, `PlatformGlow`-style keys |

Fields of the object (offsets from 0x3A0188): +0x04 array of the texture objects built from `tint.xsh` (the vignette is the first, the
scratches the second, count at +0x14), +0x74 the tint object with its two quads, +0x78 the glow object, +0x80 current preset index,
+0x84 transition countdown, +0x88 duration, +0x90 previous preset, +0x98 flag byte, +0x9C blend factor (float), +0xA0 a table of 16
presets of 0x44 bytes (modulate colour at +0x2C..0x34 of each, default 0xFF; fill at +0x38..0x40, default 0), +0x4E0..0x520 the live
blended values (+0x4E4 vignette on, +0x4E8 tint on, +0x4EC glow on, +0x50C..0x514 modulate RGB, +0x518..0x520 fill RGB).
Neighbouring globals: 0x3A0180 (a counter set to 3 at start-up), 0x2E3430 and 0x2E3434 (the flicker counter, in restored `.data`).

What advances it: the update `sub_000D90F0` runs once per game update from the screen update (`sub_00079B60`, after the FX screen);
bus events change the preset index and restart the countdown. The factor is countdown over duration while a transition runs, 0 or a
random flicker otherwise. Nothing in it reads host time or the render count. **[V]**

Why a stuck fade does not apply: a load restores the object with the rest of `.data`; the re-run calls the screen update, the event
manager and its snapshot copy (`rollback_to` in `netplay.c`), and `N.resim` only turns off the ten sound handlers listed in
`SOUND_HANDLERS`. The handler `sub_000D8DD0` is not among them, so a preset change posted inside a re-run is applied. The live values at
+0x4E0 are recomputed at every render. **[V]** The blend colour in the black frames is a plausible preset, not a black one.

## 3. What the vignette needs

* The texture is the first image of `assets/tint.xsh` (an EA shape archive: two images, 128x128 each). Image 0: 8-bit palettised,
  swizzled, 16 384 bytes of indices, a **128-entry** palette block of 0x210 bytes, entries a scrambled grey ramp (index 125 is
  255, index 0 is 0, 126 and 127 are 0). Image 1 (the film scratches): a 252-entry palette. **[V]** (read offline, structure only.)
* The index map: 89 % of the texels have an index of 64 or more, 94 % have 32 or more, and the whole interior (more than 10 texels in
  from the edge) is index 125. Only a thin border carries low indices. **[V]**
* Host expansion (`bind_texture_ready`, `nv2a_pb_exec.c`): each index below the palette length takes the palette entry, an index at or
  above it becomes 0. The length comes from bits 2 and 3 of the stage's palette register (`tex_stage_method`: 0 is 256 entries,
  1 is 128, 2 is 64, 3 is 32). The palette address is the register with its low six bits cleared. The cache key mixes the texture and
  palette addresses and is re-checked against a hash of the indices and the palette at most every 8 ms, so a changed palette is
  noticed. **[V]** So a stale host cache is not the cause; the host was given a short length or a short or zeroed palette.

## 4. The evidence for a 32-entry palette

Margins of the visible ring in the pictures, measured on the rows and column away from the HUD, against the margins the vignette
image gives for a palette cut to N entries (the image, upscaled 5 times, with every index of N or more set to 0):

| | left / right, mid height | left / right, row 120 | top / bottom, mid width |
|---|---|---|---|
| observed (`cb2777`) | 6 / 11 | 7 / 11 | 4 / 9 |
| 32 entries | 5 / 10 | 5 / 10 | 4 / 7 |
| 48 entries | 10 / 10 | 10 / 10 | 8 / 7 |
| 64 entries | 15 / 15 | 15 / 15 | 11 / 11 |
| 96 entries | 30 / 30 | 30 / 30 | 23 / 22 |
| 128 entries (right) | whole picture visible | | |

The left/right asymmetry of the observation is reproduced by the image itself. All four black reports show the same ring.

Two readings give this picture. (a) The palette register named 32 entries (size bits 3). (b) The register named 128 and an address
whose entries from index 32 on read 0 (a 32-entry palette followed by zeroed memory, or a freshly cleared block). The ring colours
in (b) would come from another palette; the geometry would be the same. [U] The probe in section 6 separates them.

Candidate sources of a 32-entry or zeroed palette, none confirmed:

1. **The palette register was not rewritten for the vignette draw** and still holds the previous draw's palette. The guest only
   writes the register when its renderer calls the palette setter; the host keeps the last value per stage. EAGL keeps a per-stage
   texture cache in `.data` at 0x3A5050..0x3A505F and a dirty byte at 0x3A61F8 (`rollback-async-loads.md` 3.1, 3.2 item 4), restored by
   loads while the D3D device is not. A cache that says "this palette is bound" while the device holds another gives exactly a
   correct texture offset with a foreign palette. In steady state the cache and the device agree at every frame end, so a single load
   would at most spoil one frame; a **persistent** error needs a cache word that a load keeps putting back to a value that the
   device no longer has, in every frame. That would need one of the cache words to differ between the moment a save is taken and the
   frame end, which holds for any state that changes late in a frame (the HUD's last textured draws are the candidates).
2. **The palette header's size bits changed.** In the Xbox D3D library the palette size is in the top two bits of the header's first
   word (0 is 256, 1 is 128, 2 is 64, 3 is 32), next to the reference count in the low half. A header of 0xFFFFFFFF or a word with
   bit 31 set would read as 32 entries. The header sits in a small-block pool page that the fight changes, so loads rewrite it from
   a save. A torn or wrong header would be a rollback-state bug. [U]
3. **Palette data zeroed** by a freed-and-reused block. Nothing in `rollback.c` writes the heap except loads and the arena's own
   memset (`rollback.c` around line 1248, outside the heap).

## 5. Correlation with rollback

| Report | Build | Match | Rollback state | Picture |
|---|---|---|---|---|
| `cb2777`, `2f7042`, `4cb55d`, `06a108` | 26 | room match, the beta on | prepared, hundreds to over a thousand loads | all black (clock 00:10, 01:30, 03:53) |
| `ed7519` | 26 | the match after one that ran 2751 loads and then "rollback off for the rest of this match"; King of the Hill, lockstep | not prepared | normal; vignette at the same address 8034DB80, blend colour `FFE2E1F0` |
| builds 19 and 20 reports | 19, 20 | rollback on for everyone, vignette at 80354580 | active | not black |
| builds 21 to 25 | | rollback off, vignette at 805D2800 | none | not black |

The vignette's address depends on whether the audio arena exists (80354580 and 8034DB80 with it, 805D2800 without), so the address
itself says nothing about the fault. The first black match was the first match of its process, so leftovers from an earlier match
do not explain it. Whether the first frames of a black match were already black is not known from the reports. [U]

Differences of a rollback-active match against an ordinary one, from `rollback.c` and `netplay.c`: the heap pages are write-guarded
and the fault handler unguards each page on its first write; loads restore `.data` and the changed heap pages (a page first changed
after a save comes back from the reference copy taken at the start); the render is not re-run; heap frees from other threads and
the XAPI heap are held; the audio arena exists. Build 26 differs from builds 19 and 20 mainly by the arena rules, the kept
`.data` holes and held frees; none of them touches the post-effect object, the tint asset or the palette.

## 6. Probes that settle it (nothing here changes behaviour)

1. `RECOMP_PAL_DUMP=0x8034DB80,<file>.bmp` in a rollback match writes what the host made of the vignette texture. A black centre with
   an intact ring shows the expansion is cut; the dump is rewritten each time it is expanded, so it also says whether the first
   expansion was already wrong. (Change of the existing log line, no new code: it already prints the palette address; add the length.)
2. Log, in `bind_texture_ready`, the palette address and length of every P8 bind whose texture is 128x128 and whose offset is the
   vignette's (read from the object: pointer at 0x3A018C, the texture data inside it). Compare with a normal match: 128 entries.
3. Read the register's source: log the guest value written for stage 0's palette (method 0x1B20) on the vignette draw, and whether it
   was written at all in that draw (`RECOMP_PEEK` cannot see it; a host log can).
4. At `rollback_prepare` and after every hundredth load, hash the vignette's texture data and palette and the 0x40-byte header
   record named at the texture object's +0x40, and print when it changes. A change means a rollback write; no change moves the
   fault to the render-state side (section 4, first candidate).
5. Run the same match with `RECOMP_ROLLBACK=on` and the loads off (prepare and write tracking only). If the picture is black, the
   fault is not in loads; the page guard or prepare causes it.
6. The post-effect state itself, for completeness: `RECOMP_PEEK` of 0x3A0188+0x80, +0x84, +0x88, +0x9C, +0x4E4, +0x4E8, +0x50C,
   +0x518 in a black match. They are expected to be an ordinary preset with the vignette flag set.

## 7. Fixes, in the terms of `rollback.c`

* **Nothing to keep live in `.data`.** The post-effect object (0x3A0188..0x3A079C) is consistent with the game updates; adding a hole
  there would stop it from rewinding with the preset changes that a re-run replays and is not what is wrong. The KO flash and the
  round transitions are driven by bus events and are replayed in a re-run, so they work today and a clear-after-load would break them.
* **Safe, narrow mitigations to try once the probes name the cause:**
  1. If the texture data or the palette turns out to be overwritten: add the vignette's texture data and palette pages to
     `s_skip` in `rollback_prepare` (`skip_heap` over the data of the first texture object of `tint.xsh`, pointer chain 0x3A018C then
     the section data; 16 KB of indices and 1 KB of palette). Never skip the 0x4C-byte object or the 0x40-byte header: they live in
     small-block pool pages that must rewind with the pool's lists.
  2. If the device-side palette register is stale: after every load set the EAGL dirty byte 0x3A61F8 to 1 (the renderer re-applies its
     cached state at the next draw, as it does after a vertex-buffer creation) and clear the per-stage cache words 0x3A5050..0x3A505F,
     so the first draw after a load rebinds textures and palettes. Cost: one frame of redundant state calls per load. Risk: the dirty
     path also resets render states the game set elsewhere; check the first frame after a load by comparing a picture.
  3. If the guest header's size bits are wrong: after each load, rewrite them from the shape archive (128 entries, size bits 1) or
     skip the header's page-state. Only worth it if probe 4 shows a changing header.
* **Host-side guard (independent of the cause):** in `bind_texture_ready`, when a P8 texture's largest index in the data exceeds the
  stated palette length by a wide margin (here 125 against 32), expand with the full 256-entry read instead of zero. Real hardware does
  not zero those indices; it reads the palette memory. This turns the blackout into a possibly wrong colour but never an all-black
  screen, and it is invisible when the length is right. Risk: a texture that relies on zero beyond the length (none known).

## 8. What this answers from the brief

1. The quad is the post-effect tint, drawn by `sub_000D9AF0`, state in the object at 0x3A0188, advanced by `sub_000D90F0` per update
   and applied per render by `sub_000D8C40`; the vignette quad after it is what the picture goes black through.
2. The fade does not stay opaque: it is not opaque in the first place. The vignette is read through a 32-entry palette (or a
   zero-padded palette) in rollback matches; how that happens is the open item, with five probes above.
3. No byte hole or per-word heap handling is called for by the evidence; the narrow options (skip the texture data pages, resync
   EAGL's dirty byte after a load, a host guard against short palettes) and their risks are in section 7.

## 9. Measured: the palette's reference count goes below zero (2026-10-25, fixed in `rollback.c`)

Two local copies of this worktree's build, a local relay, `RECOMP_ROLLBACK=on`, `RECOMP_NETPLAY_LAG_MS=70,10` or `70,20`
(about 140 ms round trip), random fight inputs, captures every 8 s. Temporary probes (not committed): the host logged every
bind of the vignette texture 8034DB80 with its palette address and length, and `rollback_load` compared EAGL's binding copies
and the D3D reference counts with the device after each load.

**The cause.** `D3DDevice_SetPalette` (`sub_00216140`) and `D3DDevice_SetTexture` (`sub_00215F60`) take a reference on what
they bind and drop one on what they replace: the object's `Common` word, +0x80000 / -0x80000 (bits 19-22 the internal count).
The device's bindings (`[0x234768]` +0xF88 textures, +0xF98 palettes) are not rewound; the objects' `Common` words, in heap
pages, are. A load back to a save taken while the device held other textures and palettes leaves the ones it holds now
without the reference it took, and the next set drops it again: the count goes to -1, `Common` reads `0xFFF8xxxx`. The palette
register is `Data | Common >> 28` (the shift was hidden in the reading of section 4), so a wrapped count turns the register's
low bits to `F`: length 32, context 3. A vignette palette bound at a count of -2 or less is expanded through 32 entries; its
interior (index 125) becomes 0 and DST_COLOR x ZERO blacks out the scene (section 4, reading (a)). The count drifts by one
at each load whose save had other bindings than the device has now, so it random-walks over a match: black stretches come and
go, which fits the reports (`2e3f83`: one capture with wrapped registers, a later one without).

Evidence:

| | Result |
|---|---|
| F9 reports with a palette register ending in `F` (stage 1/3 lines of the batch dump) | 4 of the 13 reports whose log shows rollback loads (builds 19, 20, 26: `58b63c`, `cf1f5a`, `bc0563`, `2e3f83`), 0 of the 34 without (builds 18-25); `0024F2CF`, `0024E9CF`, the same addresses as the normal `...C0`. The dump prints stages 1-3 only, so the vignette's own stage-0 register is not in it |
| Baseline, fix off, fights without pauses (8 min, 751 loads) | the frame-end bindings never differed: 0 of 500 loads with EAGL's copy unlike the device, no orphaned reference, vignette 256 entries in 27 000 binds, pictures normal |
| Baseline, fix off, knock-outs and START presses (8 min, 3 matches, 532 loads) | 152 of 500 loads restored copies of texture 0 and palettes 0, 1, 3 unlike the device (save-time `80051180`/`80051100` against none, and back); 628 device bindings without their reference; palette object `8009A780` reached `Common FFFB0001` (count -1) with data `0034D640`, the stage-3 palette seen in the players' reports. The vignette's own palette was not among them in this run, so no black picture here |
| Fix on, same scenario (9 min, 1562 loads) and fast knock-outs (9 min, 3 matches, 1318 loads) | 0 copies unlike the device, 0 orphaned references, 0 wrapped counts, vignette 256 entries in 30 000 binds, no crash, no desync (510 checksum rounds equal), pictures normal (darkest centre 15 of 255, a dark venue) |

The vignette's palette register says 256 entries in every normal bind (its `Common` top bits are 0; the data block holds 128).

**What section 7 assumed and is not so.** The byte `0x3A61F8` is not a "re-apply state" flag: `sub_0010EF00` runs `wbinvd` when
it is set and clears it (a cache flush after vertex-buffer copies), so setting it after a load does nothing here. And
`0x3A5050[4]` is EAGL's per-stage palette copy; the texture copy is `0x3A5030[4]`.

**The fix** (`rollback_unbind_textures`, called at the start of every update of a rollback match, before the load, the save and
the reference copy): the device lets go of every texture and palette (SetTexture/SetPalette with NULL, only for stages that
hold one) and EAGL's copies `0x3A5030[4]`, `0x3A5050[4]` are cleared, so the next draw binds again. No save and no load then
holds a device reference, so the counts agree with any state a load puts back. EAGL's other device-state copies
(0x3A5030..0x3A50A8: bindings, LOD bias, bump-env floats; 0x2E48B8..0x2E4998: texture-stage states) are kept live through
loads (`k_data_hole`), because the device they mirror is not rewound either.

**Left open.** Vertex buffers and index buffers are referenced the same way (`sub_0021BB10`, `sub_002161D0`, `sub_002149B0`
drop internal counts) and are not unbound yet; a drifted vertex-buffer count could free a buffer in use. EAGL's render-state
copies (0x2E5148..0x2E519C, 0x2E4B6C..) are still rewound. The host-side guard of section 7 (expand a P8 texture through
256 entries when its indices exceed the stated length by far) remains a useful second line in the toolkit.

# White quads: how a textured draw can end up flat white (D3D11 path)

Read-only static investigation, no build and no run. TK = `tools/xboxrecomp/src`.
Symptom: large flat white quads (full-screen white gradient over a fight, P2 health/momentum bar a white box while P1's
is fine, an options menu white with only "OFF" visible), on all maps, several PCs, never on the owner's PC.

**Addendum (coordinator's new evidence): the affected player's log has no `[TEXPACK]` line, so no pack was active.
The HD pack is therefore not the cause of this report. Part A (the pack question) is kept short because it is
now only an exclusion; Part B is the real investigation.**

Status of the conclusions: I found no single line that is certainly the cause. What follows is the complete list of
ways the code can produce a white/flat quad, which of them can vary between PCs, and the cheap probes that separate
them. Claims marked (inferred) are not proven by a read alone.

---------------------------------------------------------------------------------------------------------------------

## 0. How a draw is textured (needed for everything below)

* Per batch, `TK/kernel/nv2a_pb_exec.c:3343-3352`: `bind_extra_stages(gpu)` (stages 1-3), then
  `bind_texture_ready(&texs[0])` + `set_texture_address` + `set_texture_filter` if `texs[0].valid`, otherwise
  `pgraph_d3d11_set_texture_ready(0,NULL,0,0,0)` which does `SetTexture(0,NULL)`.
* `bind_texture_ready` (`:2772`): pack attempt (`:2789`), else `pgraph_d3d11_set_texture_ready` (`:2823`) which looks up
  the 256-entry `s_ready_tex` cache or creates a D3D11 texture (`TK/nv2a/nv2a_pgraph_d3d11.c:943-1093`).
* Wrapper `dev_SetTexture` (`TK/d3d/d3d8_device.c:729-757`): `SetTexture(NULL)` sets the SRV slot to NULL **and sets
  `COLOROP = DISABLE`** (`:754`); a non-NULL texture only re-enables COLOROP if it was DISABLE (`:748`).
* Two pixel pipelines, chosen per draw in `dev_DrawPrimitiveUP` etc. (`d3d8_device.c:967,987,1031,1095,1203`):
  1. **NV2A combiner shader** (normal case: `pgraph_d3d11_set_combiner_state(&s_gpu.rc)` runs for every batch,
     `nv2a_pb_exec.c:3345`). Texture presence is NOT consulted: stage i is sampled iff the title's shader-stage word says
     so (`d3d8_combiners.c:560-583`, `tex_mode[i] != NONE`). **A NULL SRV samples as (0,0,0,0)** - black/transparent,
     not white.
  2. **Fixed-function fallback** when `d3d8_combiners_prepare_draw()` returns FALSE (`d3d8_combiners.c:1131-1161`: no
     combiner token, no constant buffer, or `d3d8_combiners_get_shader` returned NULL: HLSL overflow, `D3DCompile` or
     `CreatePixelShader` failure, `:819-870`). There `COLOROP=DISABLE` (a NULL texture) means "output = interpolated
     vertex colour" (`d3d8_shaders.c:1179-1195`), i.e. the **white vertex colour** the comment at
     `nv2a_pgraph_d3d11.c:795-798` describes.
* So in the combiner pipeline a *missing* texture draws black/invisible; "white" there needs either a white texel
  (texture content wrong or not yet uploaded), a vertex colour of white, or a combiner program that reads no texture.
  This is the most useful discrimination for the screenshots: a white gradient (vertex alpha ramp over white) and a
  white bar both look like `vertex colour white x (texture = white or absent)`.

---------------------------------------------------------------------------------------------------------------------

## Part A. Texture pack (now an exclusion, answers to the original questions 1-3)

**Pack-specific paths never leave a stage untextured or half-bound.**

* `texture_pack_bind` (`TK/nv2a/texture_pack.c:436-503`) returns 0 on every failure before touching the device
  (miss `:490`, failed entry/backoff `:491`, upload `-1` -> retry in 1 s `:495`, rejected `:496`) and the executor then
  falls through to the normal path (`nv2a_pb_exec.c:2789`). The only device call is the final `SetTexture`
  (`:500`); if it fails it returns 0 and the original texture is bound instead.
* `upload_png` / `upload_dds` (`:326-417`) can fail on: creation (`CreateTexture` -> `d3d8_CreateTextureImpl`,
  `d3d8_resources.c:1433-1439`, any `E_OUTOFMEMORY`/`E_INVALIDARG`), size (PNG up to 16384 per side, `:335`, `:569`; DDS
  `:388`), budget (`make_room` `:300-320`, `memory>g_budget` `:347`), allocation. All return `-1`/`0` -> original texture.
  Not queried: the device's real maximum texture size (feature level 10.x is 8192, 9.x 2048/4096). A 2x-8x replacement
  of a 2048 source on an FL10 GPU fails `CreateTexture2D` and is retried **every second forever** (`-1`, `:495`) with the
  file re-read and re-decoded each time. No white, but a hitch.
* DDS formats other than DXT1/3/5 (BC7/DX10 header) are rejected (`:389-392`) -> `failed=1`, original used.
* Stage state after a successful pack bind (`nv2a_pgraph_d3d11.c:802-817`) sets COLOROP/ALPHAOP only; address modes and
  filter are set by the callers right after (`nv2a_pb_exec.c:2946-2947, 3348-3349`). It does not set CLAMP like the
  non-pack path (`:1090-1091`) but `set_texture_address` always follows. No mismatch that yields white.
* `make_room` protects textures currently on stages 0-3 (`:302-313`) and the executor binds stages 1-3 before stage 0
  (`nv2a_pb_exec.c:3343-3347`), so the texture bound earlier in the same batch is never evicted by a later upload.
  `release_entry` (`:286-299`) nulls any stage that holds the entry. A pack texture cannot be freed while bound.
* `g_pack_bound[stage]` (`nv2a_pgraph_d3d11.c:800`): written by `try_texture_pack` (`:807`), cleared at the start of
  every `set_texture_ready` (`:953`) and by `unbind_stage` (`:1173`). It is stale only between a pack-eviction
  `SetTexture(NULL)` on another stage and that stage's next bind; the next bind rewrites it. It only drives
  `MIPFILTER` (`:1146-1149`). A replacement with fewer mip levels than the filter asks for is harmless: the sampler has
  `MaxLOD = FLOAT32_MAX` (`d3d8_states.c:308`), which D3D11 clamps to the SRV's `MipLevels` (`d3d8_resources.c:1449`).
* GPU dependence of the pack: texture size limit (above), VRAM (`budget` counts GPU copy + the wrapper's CPU shadow
  `sys_mem`, `d3d8_resources.c:1417`, `texture_pack.c:346,414`), nothing format-related for PNG (always BGRA8).
* The `s_ready_tex` comment (`nv2a_pgraph_d3d11.c:795-798`) is about an old fixed bug (cache limit 64, no eviction).
  Eviction now exists (`:1012-1045`); see B3 for what is left of it.

Conclusion for the pack: it can only fall back, never blank a stage. The only pack-caused "wrong picture" is the
content of a replacement PNG itself (a data question for `scripts/build-hd-pack.py`), not a binding fault.

---------------------------------------------------------------------------------------------------------------------

## Part B. Non-pack causes, ranked by how well they fit "several PCs, not the owner's, 2x scale"

### B0. Scale is not a discriminator

`render_scale` only sizes the back buffer (`d3d8_device.c:274-294,354`), scales scissor rectangles by an integer
(`d3d8_SetScissorRect` `:1488-1505`) and nothing else; vertex shaders use the logical 640x480 size (`d3d8_shaders.c:941,973`)
and the viewport covers the whole buffer. No texture, shader or format choice depends on it. 1280x960 is also *less*
GPU-memory-hungry than 2560x1920. The owner's own harness logs say "rendering at 1280x960" too
(`logs/run-20261020-133253.log.err` line 60), so the owner's PC runs 2x. Only point-sprite size is not scaled
(`d3d8_extvs.c:70-75`, cosmetic). So neither "2x" nor memory pressure from a bigger target explains it.
=> The difference between PCs is the **GPU/driver/feature level**, or **timing/data**, or **settings/profile**.

### B1. Optional DXGI formats: CreateTexture2D fails on some GPUs, texture is NULL (GPU-dependent, plausible)

`d3d8_to_dxgi_format` (`d3d8_resources.c:73-91`) maps Xbox 16-bit formats to `B5G6R5_UNORM`, `B5G5R5A1_UNORM`,
`B4G4R4A4_UNORM` (NV2A 0x02/0x03/0x04/0x05 swizzled, 0x10/0x11/0x1C/0x1D linear; also `R5G5B5A1`/`R4G4B4A4`, `:86-91`).
In D3D11 these three are **optional** (DXGI 1.2 / D3D 11.1 runtime plus driver support); on some GPU/driver/OS
combinations `CreateTexture2D` returns `E_INVALIDARG`. The code never calls `CheckFormatSupport`
(grep: no hit in `TK/d3d`, `TK/nv2a`). Failure path: `d3d8_CreateTextureImpl` prints
`D3D8: CreateTexture2D failed: 0x%08lX (fmt=%d WxH)` (`d3d8_resources.c:1435`), `set_texture_ready` does
`SetTexture(stage,NULL)` and returns 0 (`nv2a_pgraph_d3d11.c:1046-1050`), nothing retried or logged by the executor.
Result: no SRV for that texture on every bind. In the combiner pipeline that samples (0,0,0,0) (black/invisible, B0
above) - so this alone gives black/clear rather than white, unless that quad is drawn through the fixed-function
fallback or its combiner reads no texture (a flat bar that is "colour x white-texture" is the typical case
where the hole would show white if the white texel comes from vertex alpha).
Which game textures use these formats is unknown from the repo (the `[TEXFMT]` log needs `RECOMP_TEX_FORMATS=1`;
`logs/` only shows DXT5/DXT3/linear A8R8G8B8/YUY2 in the capped "uploaded" lines). Needs the player's log:
grep `CreateTexture2D failed`.
Other format-driven oddities: unknown NV2A codes get bpp 32 (`d3d8_formats.c` `default: return 32`), so
`ready_tex_bytes` never returns 0 and the "no known size" branch (`nv2a_pgraph_d3d11.c:1003-1011`) is effectively
dead; `d3d8_to_dxgi_format` falls to R8G8B8A8 with `D3D8: Unknown format 0x%X` (`:237-239`) and the copy then
reads the wrong number of bytes. `FL<10` additionally limits A8/R8/BC support (not a realistic target).

### B2. Shader pipeline silently falling back, or not drawing (GPU/driver-dependent, medium)

* All pixel shaders are compiled as **ps_5_0** (`d3d8_shaders.c:516-517`, `d3d8_combiners.c:847-848`) and the vertex
  shaders as **vs_5_0** (`d3d8_shaders.c:834-835`, `d3d8_vsh.c:1211`) while the device is created with the default
  feature-level list (`D3D11CreateDeviceAndSwapChain(..., NULL, 0, ...)`, `d3d8_device.c:320-340`), which falls back to
  10.x/9.x. On a 10.x device `CreatePixelShader` of SM5 bytecode fails. `feature_level` is retrieved
  (`:266`) and never logged or checked. On such a device combiner shaders fail (`d3d8_combiners.c:1159`, falls to
  FFP), the FFP shader also fails (`d3d8_shaders.c:852,1113`); a total failure would draw nothing at all, not
  isolated white boxes, so this is only relevant as "a few shaders fail, others work" (e.g. a driver's instruction/temp
  limit on the larger generated combiner shaders). The log lines to look for: `NV2A combiners: ... failed`,
  `falling back to FFP`, `D3D8: PS compile failed`.
* When a shader falls back to the FFP, a textured quad is evaluated with `COLOROP`/args from the last
  `SetTextureStageState` calls (`nv2a_pgraph_d3d11.c:1084-1091`) i.e. `texture x diffuse`; if its texture is also
  missing it is flat diffuse = white. This is the **only** route in this code that matches the literal comment at
  `:795-798` ("drawn untextured in its white vertex colour").
* Latent bug in the combiner cache: when the 128-slot table is full the LRU entry's shader is released but the
  slot is left `in_use` with the old hash/state (`d3d8_combiners.c:925-947`); if the replacement compile then fails
  (`return NULL`) the slot keeps a dangling `shader`, and a later hit on the old state binds a freed shader.
  Needs >128 distinct combiner states in the session. (inferred: harmless in short runs.)
* Driver-specific HLSL results (NaN/precision/`discard` handling in the generated combiner code
  `d3d8_combiners.c:560-805`, compiled with `OPTIMIZATION_LEVEL3`) cannot be excluded by reading; they would produce a
  wrong colour for one combiner configuration on one vendor. (inferred)

### B3. The 256-entry texture cache `s_ready_tex` (not GPU dependent; fixed since the 64 limit, small leftovers)

* Eviction is LRU by `GetTickCount64` (15.6 ms resolution) with ties going to the lowest index
  (`nv2a_pgraph_d3d11.c:1012-1018`). A fight can use more than 256 distinct textures (fighters, crowd, stage, HUD);
  anything evicted is simply recreated on its next bind (an upload per bind is a cost, not white).
* After an eviction the slot is cleared of `tex` only (`:1038`); `addr/format/width/height` stay. If the following
  `CreateTexture` fails (`:1046-1050`) the slot is left matching its old key with `tex == NULL`. The next lookup finds it
  (`:966-975`), takes `tex=NULL` and runs the create path again, evicting yet another live texture first
  (`slot = s_ready_tex_count` = 256 -> LRU scan again, `:1012-1018`). Under a persistent creation failure (device
  removed, out of memory, B1) this drains the cache one texture per failed bind - a cascade in which more and more
  textures go missing. Fix: invalidate the slot's key at eviction and remember failures.
* Palettised textures are cached by `key = offset ^ palette * 2654435761` as format 0x12 (`nv2a_pb_exec.c:2828, 2867`);
  the key can alias a real guest address of the same size/format (negligible).
* Change detection: `ready_tex_sig` samples every 61st word, at most every 8 ms (`:855-869, 982-996`); a texture the
  CPU patches in place without touching a sampled word keeps its old picture. A texture uploaded **before** the game has
  finished writing it is cached with what was in memory then and is only refreshed if a sampled word changes. If that
  memory held 0xFF fill at bind time, DXT/A8R8G8B8 decode to opaque white (inferred: nothing in the repo shows such a
  fill). This is a plausible *timing/data* mechanism that differs between PCs because the executor reads guest memory
  at kick time (`nv2a_pb_scan.c`, driven from the DMA_PUT trap, `xbox_memory_layout.c:1617-1669`), whereas hardware
  reads it when the GPU gets to the draw.
* Reads of guest memory: `ready_tex_fill`/`ready_tex_sig` read `bytes` from `mem + offset` with no readability check;
  the pack path has one (`texture_pack.c:207-215`). A wild offset reads a trapped page (crash rather than white).

### B4. Render targets used as textures are not real (deterministic, but the content depends on guest memory)

* `surf_holds_texture` (`nv2a_pb_exec.c:2709-2718`) is consulted only by the pack. `bind_texture_ready` otherwise uploads
  whatever the guest left in those pages: the D3D11 path draws every surface into its one back buffer and never writes
  guest memory (comment `:2704-2708`; `docs/research/remaster-graphics-survey.md:108-116`). A full-screen quad that
  samples a surface the title rendered into (the fight composite samples 0x82A90000) therefore shows stale guest
  memory: black in the owner's runs per the comment, but **the content is whatever the allocator last put there**, so a
  region that is 0xFFFFFFFF (white) or recycled from a bright texture on another install would draw as a
  white/ghost full-screen layer. `RECOMP_SKIP_RT_SAMPLE=1` removes those batches (`:3469-3484`), default off.
  A "white full-screen gradient over the fight, on all maps" matches this shape better than any creation failure
  (inferred). It would also explain why only some PCs show it if the stale contents depend on load order and timing.
* Same family: any texture the GPU would have written (resolves/copies) reads stale; only YUV movie frames and
  palette expansions are re-uploaded (`:2821-2867`).

### B5. Vertex colour absent -> white (deterministic per state, depends on what the title programmed)

* `vertex_color` returns `0xFFFFFFFF` whenever the colour attribute cannot be fetched (`nv2a_pb_exec.c:1625-1630`):
  array size/stride zero, offset 0, address outside [0x1000, 0x04000000) / [0x80000000, 0x84000000), inline payload too
  short, or an attribute type `fetch_attr` does not handle (`:660-737`; types 3, 7+ return 0). `color_attr()`
  (`:1554-1565`) prefers slot 3 only when it has a non-zero offset, else the first D3DCOLOR size-4 attribute, else slot 3.
  For INLINE_ARRAY batches an attribute at inline offset 0 is legal (`:670-678`) but `color_attr`/`texcoord_attr`
  test `offset != 0`, so a diffuse at offset 0 plus another D3DCOLOR attribute (specular) at a non-zero offset selects the
  wrong one (inferred). A white box where only the **vertex colour** is lost (P2's bar = P2 team colour missing) looks
  exactly like the report and does not depend on textures at all. Not GPU dependent; but a different history of
  `SET_VERTEX_DATA_ARRAY_*` calls (e.g. which player slot drew first) changes it.
* Texture coordinates have the same fallback to `(0,0)` (`:3319-3321`): a quad with no UVs samples one texel, flat.
  If that texel is white (typical of a gradient/vignette corner or a plain-colour atlas) the whole quad is white.
  Counters exist (`batches_no_uv`, `batches_no_tex`, printed by `nv2a_pb_exec_report`, `:4651`) but only fed by the soft
  raster path (`:2523-2533`).

### B6. Things that do NOT produce white (so they can be ruled out)

* Stale `g_cur_textures` (bare pointers, `d3d8_device.c:734`): every release site clears the stages first
  (`nv2a_pgraph_d3d11.c:1025-1037`, `texture_pack.c:286-299`); only `shutdown` frees without clearing.
* Sampler/addressing: samplers are rebuilt every draw from the stage state (`d3d8_states.c:281-314, 378-383`); BORDER maps
  to a zero border colour (no white border); no scale term.
* Present/AA/sharpen pass (`d3d8_present.c:219-280`) works on the whole picture; it cannot produce local boxes.
* Device removed: `0x887A0005` appears in the owner's own earlier logs (`logs/run-20261020-133253.log.err`: 128k
  `CreateTexture2D failed` lines, "Gamma presentation failed"). Nothing handles it (grep `DEVICE_REMOVED` finds no code);
  every later `CreateTexture` fails, with the cascade of B3. After a TDR the whole picture stops, so this is unlikely
  to be a player's "white boxes" report, but if a player sees white boxes *after a driver reset* it is exactly this.

---------------------------------------------------------------------------------------------------------------------

## Which conditions depend on the PC

| Mechanism | PC-dependent? | What differs |
|---|---|---|
| B1 optional 16-bit DXGI formats fail | yes | GPU/driver/OS (DXGI 1.2 format support) |
| B2 shader create/compile failure or driver HLSL difference | yes | feature level (<11 fails ps_5_0), driver |
| B2 FFP fallback after combiner failure | indirect | same |
| B3 cache cascade after a failed create | only after another failure | out of memory, device removed, B1 |
| B3 early upload / sparse change detection | yes (timing) | CPU speed, load timing, frame pacing |
| B4 render-target texture reads stale guest memory | yes (content) | allocator/load history |
| B5 vertex colour/UV lost | no (state-dependent) | order of title's array setup |
| Pack paths | only with a pack | not this report |
| Render scale | no | cosmetic |

## Probes that separate the hypotheses (all runtime, from existing knobs)

1. Player's stderr log, grep: `CreateTexture2D failed`, `Unknown format`, `PS compile failed`, `NV2A combiners`,
   `falling back to FFP`, `vertex attribute outside guest memory`, `texture cache full`,
   `takes the CPU path`, `DEVICE_REMOVED`/`0x887A`. A blank result rules out B1/B2/B3 failures.
2. `RECOMP_PB_BATCH_DUMP=<flip>` (`nv2a_pb_exec.c:3379-3385`) on a white frame prints per batch: texture address,
   size, format, `valid` and the first vertex colour. `valid 0` = untextured; `valid 1` with `col FFFFFFFF` = vertex
   colour missing (B5) or a white texel; the texture address inside 0x82xxxxxx = B4.
3. `RECOMP_SKIP_RT_SAMPLE=1` (B4), `RECOMP_NO_COMBINERS=1` (forces the FFP path - tells whether the combiner or the
   texture is wrong), `RECOMP_TEX_FORMATS=1` (first texture per format, shows which formats the white quads use).
4. GPU name, driver, feature level: not logged anywhere today (`feature_level` is a local, `d3d8_device.c:266`).

## Fixes (generic, belong in the toolkit; nothing was edited)

1. **Never leave a stage untextured on failure** (`nv2a_pgraph_d3d11.c:1046-1055`): on `CreateTexture`/fill failure
   try once more after evicting (see 3), and if still failing bind a cached 1x1 *transparent* placeholder (alpha 0) rather
   than `SetTexture(NULL)`. In the FFP pipeline a NULL texture means flat white vertex colour (`d3d8_device.c:754`); a
   placeholder makes a missing texture invisible instead of white. Log the failure once per (format,size) with the HRESULT
   (today only the wrapper logs, and 128k times in a device-removed run: rate-limit it).
2. **Convert unsupported formats instead of failing**: at device creation call `CheckFormatSupport` for B5G6R5,
   B5G5R5A1 and B4G4R4A4 (and A8, R8, R8G8, R16, BC1-3); for any missing one map to `B8G8R8A8_UNORM` in
   `d3d8_to_dxgi_format` and convert in `d3d8_upload_mip_level` / `d3d8_convert_linear_pixels` (`d3d8_resources.c:1318-1361`,
   `d3d8_format_has_conversion` `:314`). Also change `d3d8_upload_bpp` accordingly.
3. **Fix the cache bookkeeping** (`nv2a_pgraph_d3d11.c:1012-1065`): on eviction zero `addr/format/width/height` (or set a
   `free` flag) so a failed create cannot leave a key-matching empty slot; remember failed keys for a few frames so a failing
   texture does not evict a live one on every bind; on `E_OUTOFMEMORY` evict half the cache (and the pack's
   `make_room(g_budget/2)`) and retry once. Use `used_ms` with a per-bind counter instead of `GetTickCount64`
   so same-frame ties are broken by recency.
4. **Query and log the device**: log adapter description, feature level, `D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION` for that level,
   dedicated VRAM and the format-support results once at start-up (`d3d8_device.c:320-340`). Require feature level 11_0
   (pass `{11_0}` and fail with a message) or compile with `ps_4_0`/`vs_4_0` when below 11, since all shaders are SM5 today.
   Expose `d3d8_GetMaxTextureSize()` and make `texture_pack.c` reject/downscale replacements above it
   (`:335, :388, :569`) and default `RECOMP_TEXTURE_CACHE_MB` to a fraction of dedicated VRAM.
5. **Combiner cache** (`d3d8_combiners.c:925-947`): null `entry->shader`/clear `in_use` before compiling the
   replacement, so a failed compile leaves an empty slot rather than a freed shader.
6. **Render-target textures (B4)**: either skip batches that sample a drawn colour surface by default
   (`RECOMP_SKIP_RT_SAMPLE` behaviour, `nv2a_pb_exec.c:3469-3484`) or bind a transparent placeholder, so stale memory can never
   show as a white layer; the proper fix is the host texture per surface described in
   `docs/research/remaster-graphics-survey.md:117-121`.
7. **Colour/UV fallbacks (B5)**: treat inline attributes with offset 0 as present in `color_attr`/`texcoord_attr`
   (`nv2a_pb_exec.c:1554-1596`), and log (once, counted) every batch that falls back to white vertex colour or (0,0) UVs.
8. **Handle device removal**: check `Present` for `DXGI_ERROR_DEVICE_REMOVED`/`RESET`, print
   `GetDeviceRemovedReason`, and stop the 100k-line `CreateTexture2D failed` spam.

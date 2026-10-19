# Remaster graphics survey: where each feature would go

2026-10-19. Read-only survey of the code and docs; nothing was built or run, so every "does" below is a
static reading. Paths are relative to the repo root; `TK` = `tools/xboxrecomp/src` (the toolkit submodule,
so changes there are fork commits per CLAUDE.md section 4 item 7 and `docs/07-m6-plan.md`).

## How the frame is drawn (the facts everything below hangs on)

- The game's Direct3D 8 is statically linked, so the game never calls the toolkit's D3D8 layer. It writes an
  NV2A push buffer; `TK/kernel/nv2a_pb_exec.c` decodes it and hands draws to `TK/nv2a/nv2a_pgraph_d3d11.c`,
  which drives the toolkit's D3D8-on-D3D11 layer (`TK/d3d/d3d8_*.c`). Host entry and window:
  `src/hooks/d3d11_translator.c` (`TRANSLATOR_WIDTH/HEIGHT` 640x480 at :48-49; device creation :420-445;
  frame end/Present :463-486).
- **One back buffer.** The executor draws every colour surface into the single default target; it never
  creates a host texture per guest render target and never writes guest memory
  (`TK/kernel/nv2a_pb_exec.c:2681-2685`). D42 (`PROGRESS.md:90`): the console renders a fight's 3D in a
  double-width surface (format 0x1128, AA mode 1) and filters it down; the port does not model that surface and
  supersamples the one buffer instead (`render_scale`, default 2, 1-4).
- **Resolution.** Swap-chain/target size = logical 640x480 x `render_scale` (`TK/d3d/d3d8_device.c:281-295,
  354-355`). With scaled presentation (always, in this port) the game draws into its own texture
  (`d3d8_present_create_game_target`, `TK/d3d/d3d8_present.c:142-175`) and `d3d8_present_blit`
  (`d3d8_present.c:270-310`) fits it into the window using `d3d8_present_set_aspect(keep, 4, 3)`
  (`src/hooks/pc_settings.c:321`).
- **Present order** (`TK/d3d/d3d8_device.c:125-160`): `d3d8_gamma_begin` (ramp applied to the game target in place,
  snapshot/restore) -> `d3d8_present_blit` (scale to window + ImGui overlay hook) -> `IDXGISwapChain_Present` ->
  `d3d8_gamma_end`.
- **Vertex programs run on the GPU** as generated HLSL (`TK/kernel/nv2a_vsh_cpu.c`, `TK/d3d/d3d8_extvs.c`). The
  title's program ends with D3D's epilogue that outputs screen *pixels* in 640x480 units (c[58]=viewport scale,
  c[59]=offset; mirrored from methods at `nv2a_pb_exec.c:4303-4313`); the HLSL then rebuilds clip space:
  `ndc = (R12.xy * Screen.xy) - 1`, `o.pos = (ndc*w, z, w)` (`nv2a_vsh_cpu.c:391-395`), with
  `Screen = 2/logical size` uploaded per draw (`d3d8_extvs.c:262-270`). The viewport is always the whole buffer
  (`d3d8_device.c:1785-1796`). Design doc: `docs/research/m4e-gpu-vertex-programs.md`.
- Windows D3D11 and macOS/Linux Vulkan are separate backends: `TK/d3d/d3d8_vk.c` has its own present/gamma
  (`gamma_pass` :215, `present_rect` :1671) and sampler (:1290). Anything in the present/sampler layer must be
  done twice (HLSL and GLSL/SPIR-V) to reach all three OSes.

## Summary table

| # | Feature | Where | Difficulty | Notes |
|---|---|---|---|---|
| 1a | FXAA / CAS sharpen in present | `d3d8_present.c:180-190` (PS), `:270-310` (pass) | **Low** | One shader string; luma-only edge AA is fine on the already-supersampled image |
| 1b | SMAA | same, 3 passes + LUT textures | Medium | Needs intermediate targets; LUT data to embed |
| 1c | MSAA on the game target | `d3d8_present.c:154`, `d3d8_device.c:393`, `d3d8_gamma.c:107` | Medium | Resolve before gamma/blit; `render_scale` 2 already gives 4x SSAA |
| 2 | Anisotropic filtering | `nv2a_pgraph_d3d11.c:1119-1133`, `d3d8_states.c:259-314` | **Low** for the sampler; Medium for real benefit | Non-pack textures are uploaded with 1 mip level |
| 3 | Higher-res shadows / RTT | no separate targets exist | n/a until measured; High to add real RTT | Everything already scales with `render_scale` |
| 4 | True 16:9 | game camera `sub_0001A4E0` (lead) and/or HLSL epilogue | **High** (Medium if the camera lead holds) | Nothing was documented before this survey; one concrete lead below |
| 5 | Post-process chain (bloom, grade, sharpen) | present path | Low-Medium; AO High | Depth is not shader-readable today |
| 6 | > 60 fps | sim is a 60 Hz step ring | Very High, not recommended | Docs defer it |
| 7 | Mesh replacement | `nv2a_pb_exec.c:2932-3200` | High static / Very High skinned | Vertices live in guest memory and are re-streamed per draw |

## 1. Anti-aliasing

State today: swap chain, depth, game target are all `SampleDesc.Count = 1` (`d3d8_device.c:306,393`;
`d3d8_present.c:154`). The toolkit's `dev_CreateRenderTarget/DepthStencil` already contain MSAA validation and
fallback (`d3d8_device.c:1280-1316, 1353-1385`) and `d3d8_resources.c:1011-1150` resolves MSAA surfaces for
locks, but the game never calls them (see "How the frame is drawn"), so only the default target matters.

**Post-process AA (recommended first).** Insert FXAA into the `ps` of `present_create_pipeline`
(`d3d8_present.c:182-190`; today it just `Sample`s the game texture with a point/linear sampler at :206-209) or add
a pass before `d3d8_present_blit`. Reading the game target at `render_scale` resolution and writing the window is
already what the blit does, so FXAA at window resolution is a shader swap. Add a `[display] aa` enum in
`src/hooks/pc_settings.c:35-72` (table) with a setter next to `d3d8_present_set_vsync`
(`pc_settings.c:319-324`) and a doc row in `docs/settings-reference.md`. Vulkan: a second shader in `d3d8_vk.c`'s
present. SMAA = same place but three passes and area/search LUTs, so Medium.

**MSAA.** Create the game texture and `default_depth` multisampled (`d3d8_present.c:154`, `d3d8_device.c:387-400`),
add a `ResolveSubresource` into a single-sample texture before `d3d8_gamma_begin`, because the gamma pass rejects
multisampled targets (`d3d8_gamma.c:107` returns `E_NOTIMPL`) and `capture_backbuffer`/golden captures read the
single-sample target (`d3d11_translator.c:497-683`, goldens must not change at default). Watch-outs: the game's
cut-out/alpha-tested sprites will not be smoothed (alpha-to-coverage could be enabled at the blend state in
`d3d8_states.c`), and the point-sprite geometry shader (`d3d8_extvs.c:38-60`) is unaffected. Because 2x
render scale is already 4 samples per output pixel on a 1280x960 window, MSAA only pays off at 1x scale or when
the window is much larger than the render target. Rating Medium.

Perf note: the gamma pass copies the full target twice per frame (`d3d8_gamma.c:139` snapshot, :166 copy-back in `d3d8_gamma_end`) and is on by default (`gamma` setting); a post chain should fold gamma into its own pass rather than
stack another copy.

## 2. Anisotropic filtering

Two layers set sampler state:

1. NV2A filter register -> D3D8 stage state: `pgraph_d3d11_set_texture_filter` (`TK/nv2a/nv2a_pgraph_d3d11.c:1119-1133`)
   maps min bits 16-23 and mag bits 24-27 to POINT(1)/LINEAR(2) only, and sets MIPFILTER only for bound
   texture-pack textures (:1129-1132). Address modes: `pgraph_d3d11_set_texture_address` (:1099-1110).
2. D3D8 stage state -> D3D11 sampler: `d3d8_to_d3d11_filter` and `d3d8_states_apply_sampler`
   (`TK/d3d/d3d8_states.c:259-314`). It already returns `D3D11_FILTER_ANISOTROPIC` when either filter is
   `D3DTEXF_ANISOTROPIC` (3) and copies `D3DTSS_MAXANISOTROPY` (21; `d3d8_xbox.h:425`) into `MaxAnisotropy`
   (:306), defaulting to 1. **Nothing ever sets filter 3 or MAXANISOTROPY**, so it is dead code.

Change: in `set_texture_filter`, when the stage is linear/linear, set MIN/MAG = 3 and
`SetTextureStageState(stage, 21, N)` from a new `[display] anisotropy` (0/2/4/8/16) setting; keep POINT stages
(sprites that need unfiltered texels, comment at :1112-1118) untouched. Caveats: (a) every draw re-creates the
samplers (`d3d8_states_apply` :378-383 -> release+`CreateSamplerState` per stage), which D3D11 de-duplicates but
costs CPU; cache by filter/address key when touching this. (b) Textures are created with **one mip level**
(`CreateTexture(dev, w, h, 1, ...)`, `nv2a_pgraph_d3d11.c:1046`; "Mip levels are not uploaded", :1118), so without mips
anisotropy only helps by multi-tap along the major axis of a level-0 footprint. Real benefit needs mip chains:
HD packs already supply them (`texture_pack.c` `upload_png/upload_dds`, :326-418), and for original textures a
box-filter chain could be generated in `ready_tex_fill` (:899-928) or via `GenerateMips` (needs RT+SRV bind flags).
The 8 ms change-signature re-upload (:855-869, :982-996) would then have to rebuild mips. Vulkan sampler: `d3d8_vk.c:1290`.
Rating: Low for the setting, Medium to do it properly with mips.

## 3. Shadows, render-to-texture, scaling of offscreen targets

- There are no host-side offscreen targets to scale. All guest colour surfaces are drawn into the one buffer, which
  is `logical x render_scale` (`d3d8_device.c:293-294,354-355`); scissor rects are scaled to match
  (`d3d8_SetScissorRect`, `d3d8_device.c:1488-1505`). So whatever the game does for shadows already renders at
  `render_scale` if it is done with ordinary geometry/stencil.
- The runtime's own `CreateRenderTarget` (`d3d8_device.c:1270`) uses the literal size requested and is not scaled, but
  the game never calls it.
- Textures that sit inside a drawn colour surface (render-to-texture feeding a later pass, e.g. the fight's
  composite sampling 0x82A90000) are *not* real: they read stale guest memory ("black", `nv2a_pb_exec.c:2681-2686`);
  `RECOMP_SKIP_RT_SAMPLE` (:3413-3425, off by default) skips such batches. How the fight composite looks today is not
  documented; **open question, check before promising "RTT quality".**
- **No shadow map was found in any doc or in the executor.** Stencil is implemented (`nv2a_pgraph_d3d11.c:1366-1380`,
  `ready_state`), which suggests stencil-volume or planar/blob shadows, but that is inference. To find out: run with
  `RECOMP_PB_SURFACE_TRACE=1` (`nv2a_pb_exec.c:2649-2680`, logs every colour surface, clip size and format) and
  `RECOMP_PB_BATCH_DUMP=shot`, then read the batch dump for a fight frame.
- If a real shadow/reflection RTT exists, supporting it means: a host texture per distinct colour-surface address,
  binding it as RTV in `pgraph_d3d11`, and returning it from `bind_texture_ready` when `surf_holds_texture()` is true
  (`nv2a_pb_exec.c:2686`), plus per-surface depth (the executor already keeps host depth per zeta surface, :1735+).
  High effort, and it would also fix any "composite is stale" issue. Not worth starting before the trace says it matters.

## 4. True 16:9 widescreen

**What the docs say:** almost nothing. `docs/07-m6-plan.md:27` lists it as unscheduled ToDo (D84: "wider 3D view in
fights with the HUD placed correctly; menus and FMV stay 4:3 unless cheap"); `src/patches/README.md` reserves the
folder for "a widescreen view"; `docs/hd-textures.md:6`, `known-issues.md:71`, `README.md:64,206` repeat "planned".
No projection/FOV/aspect addresses, no research note, no experiment is recorded anywhere in `docs/` or `PROGRESS.md`
(grep of "widescreen|16:9|aspect|fov|projection|camera" over docs/, PROGRESS.md and the hooks finds only the settings
`aspect = 4:3|stretch`, which is purely the present-pass letterbox, `d3d8_present.c:33-38`). Note the docs disagree on
the engine: `docs/research/m2-render-path.md:14,104` says Criterion RenderWare; `docs/01-plan-review.md:37` says in-house
"AKI" engine, no RenderWare. Treat the second as right until shown otherwise (the camera code below is not RenderWare).

**A new concrete lead found in this survey (static read of the lifted C; not run, medium confidence):**

- `sub_0001A4E0` (`src/recomp/gen/recomp_0000.c:27891-27974`, thiscall, `ret 20`) takes a mode and four out-pointers
  and produces `(x-ish, aspect, y, height)`:
  - mode 0: aspect = `0x3FAAAAAB` (4/3), viewport unchanged;
  - mode 1: aspect = `0x3FE38E39` (16/9) **letterboxed**: height = 0.75 H, y = y0 + H/8 (constants 0.25 and 0.5 at
    0x286D30 / 0x286D2C);
  - mode 2: aspect = `[ecx+0x1C] / [ecx+0x20]` (width / height of the camera's own rectangle), full viewport.
  The only two 4/3 and 16/9 float immediates in the whole XBE's .text are in this function (XBE offsets 0x1A569, 0x1A530).
- It is used by the camera/view object constructor `sub_0001A610` (:28118; mode stored at `this+0x10`, rectangle at
  +0x14..+0x20, near/far 30.0 / 65535.0) whose outputs feed `sub_001042D0` (looks like SetViewport X,Y,W,H,MinZ,MaxZ)
  and `sub_00103E10` (looks like SetProjection with aspect/near/far), and by the per-frame refresh `sub_0001A710` (:28236,
  dirty bits in `this+0x3C`). The one construction site seen (`recomp_0000.c:26275`, function ending near 0x1971F, creating
  a 0x44-byte object) passes mode 0 and takes width/height from a device query (`[esi+0x1C]`, `[esi+0x20]`).
- The toolkit already tells the game the console is set to widescreen: `xbox_ExQueryNonVolatileSetting(XC_VIDEO)` returns
  `XC_VIDEO_FLAGS_WIDESCREEN | HDTV` (`TK/kernel/kernel_xbox.c:146-152`) and the AV capabilities include
  `AV_FLAGS_WIDESCREEN` (`kernel_hal.c:903-908`). Whether the title reads it and ever selects mode 1/2 is untested.

**Two ways to build it** (they combine):

| Approach | Where | Pros | Cons |
|---|---|---|---|
| A. Game-side: make the 3D camera use aspect = display aspect (override `sub_0001A4E0` in `src/recomp_manual.c` with a version returning aspect W/H, full viewport, or force mode 2 with a wider rectangle), and widen the logical target width (853x480 or by setting) | `src/recomp_manual.c` (+ `src/patches/`); `TRANSLATOR_WIDTH` and `pp.BackBufferWidth` (`d3d11_translator.c:48,421`) | The engine recomputes the projection and its own frustum culling, so no pop-in at the sides; Hor+ with the vertical FOV kept | Needs the camera's other consumers found (HUD/viewport rect, mini-views, replays, menus); pillarbox 4:3 menus/FMV on a 16:9 target |
| B. Host-side: scale NDC x in the HLSL epilogue (`nv2a_vsh_cpu.c:391-395`, add a `Screen`-like constant in `d3d8_extvs.c:262-270`) for 3D programs, set `Screen.x = 2/853`, and let `d3d8_present_set_aspect(1,16,9)` fit | toolkit, one place | Zero game-code knowledge | The game still culls to a 4:3 frustum, so objects pop in at the edges; 2D/HUD programs must be told from 3D ones (program hash or w != 1 test) so UI keeps 640-unit "stage coordinates" (front-end note in `nv2a_vsh_cpu.c:5-10`) and is anchored left/right/centre |

Whichever is used, the HUD (APT/Flash-like, 640x480 stage units, `docs/research/m4a-fe-transitions.md`) is drawn by
vertex programs in pixels, so approach B's 2D branch is where anchoring happens; approach A leaves it to the game.
Next steps to firm up: (1) `RECOMP_WATCH_EXEC=0x0001A4E0,0x0001A610,0x0001A710` with `RECOMP_WATCH_EXEC_FIELD=0x10` to
log the mode actually used in a menu and in a fight; (2) `scripts/callers.py` on those; (3) find what else reads
`camera+0x1C/+0x20` and the viewport at device-context +0x954/+0x958 (`m2-render-path.md:159`); (4) try mode 2 plus a
wider rectangle in a throwaway override and look at one fight capture. Rating High overall, Medium if step 4 works
without side effects.

## 5. Post-processing hooks

There are two choke points, both run once per frame on the render thread:

1. `d3d8_gamma_begin/end` (`TK/d3d/d3d8_gamma.c:88-168`): operates on the game target at *render-scale resolution*,
   1:1, `Load`-based, before the window blit; only active when the ramp is non-identity (`g_enabled`).
2. `d3d8_present_blit` (`TK/d3d/d3d8_present.c:270-310`): scales to the window, runs the ImGui overlay callback after
   (`d3d8_present_set_overlay`, :263; overlay must stay last). Best place for output-side effects (CAS sharpen, FXAA,
   LUT colour grade, film grain, scanlines) because it already has a full-screen-triangle pipeline and linear/point
   samplers.

To add several effects, turn the blit into a chain: allocate ping-pong textures at render-scale or window size in
`present_follow_window` (`d3d8_present.c:218-260`), run the passes in the same deferred context, and merge gamma in
as the last step so the extra snapshot copies disappear. Bloom = downsample chain + blur (Medium). All HLSL, so the
Vulkan backend needs the same passes again.

**Depth.** The depth buffer exists (`state->default_depth`, D24S8, `d3d8_device.c:387-407`) but is created with
`D3D11_BIND_DEPTH_STENCIL` only, so no shader can read it; AO/depth fog/DoF would need
`R24G8_TYPELESS` + an `R24_UNORM_X8_TYPELESS` SRV. The deeper problem is *when*: the front end and HUD clear and reuse depth
as a mask (`nv2a_pgraph_d3d11.c:1345-1360`), and the single buffer holds 3D and HUD together, so by Present the depth
is not the scene's. A snapshot would have to be taken at the end of the 3D pass, which the executor cannot name yet.
Also the vertex shaders output a real clip-space `w` (`nv2a_vsh_cpu.c:393-394`), so linearisation is possible once depth is
captured. Rating: sharpen/grade/FXAA Low; bloom Medium; AO High.

## 6. Frame rate above 60

What the docs say: deferred out of M6 ("music replacement, frame rates above 60", `docs/07-m6-plan.md:29`;
`known-issues.md:72`). What exists: display-paced vsync on 60/120/180/240 Hz panels (`d3d8_present_display_paced`,
`d3d11_translator.c:478-482`; the executor's flip timer is relaxed to 63 Hz, `nv2a_pb_exec.c:552`), but the game still
renders 60 frames/s.

`docs/research/simulation-tick.md`: the fight advances in fixed steps, 0-4 per outer loop iteration, one per record of a
60 Hz timer ring (10 deep, `sub_000D75E0`, producer `sub_000D7700`/`sub_000D7DA0(60)`); the per-step dt is a constant
1/60 and real elapsed time is discarded, so raising the timer rate would speed the game up. Interpolation would have
to be host-side: keep the previous frame's draw list, match draws to the new frame's, and lerp the per-draw vertex-program
constants (bone and camera matrices) at the present. Draw matching across frames is unreliable (batches change count
and order) and the executor throttles on the host GPU thread, which already struggles in debug builds
(fight 21-43 fps debug, ~60 release: `docs/research/hd-textures-notes.md`, `PROGRESS.md:90`). Rating Very High; recommend
not doing it before everything else.

## 7. Mesh / model replacement

**How textures are identified** (the model to copy): the executor calls `try_texture_pack`
(`nv2a_pb_exec.c:2722-2747`, from `bind_texture_ready` :2749-2766, i.e. at bind time, not upload) which builds a
`RecompTextureSource` and calls `texture_pack_bind` (`TK/nv2a/texture_pack.c:436-503`). The id is a SHA-256 over a
`XRTEX01` header (format, width, height, palette entries, row texels), the base-level texels without row padding and the
palette (`texture_pack_hash`, :217-241), cached per source slot (address, format, size, palette; `source_slot` :424-434),
re-checked at most every 8 ms with a cheap 4-lane xxHash-style `texture_pack_quick` (:249-278). Address is deliberately
not part of the id. Pack format and hashing are documented in `docs/hd-textures.md` and `docs/research/hd-textures-notes.md`.

**Where vertex data comes from:** there is no vertex-buffer upload by the game. Attribute arrays are *guest memory
addresses* set per draw by NV097 methods (`SET_VERTEX_DATA_ARRAY_OFFSET` 0x1720+4i, `..._FORMAT` 0x1760+4i; handled at
`nv2a_pb_exec.c:4326-4340`, stored in `s_gpu.attr[]` with type/size/stride), indices arrive via ARRAY_ELEMENT16/32 and
DRAW_ARRAYS (:4102-4140) or INLINE_ARRAY. `draw_primitive` (:2932) reads them with `fetch_attr` (:637), and the GPU path
(:3095-3200) dedups the batch's vertices into one float4-per-input array plus a 16-bit triangle list, which
`d3d8_Nv2aProgramDraw` (`TK/d3d/d3d8_extvs.c:235-290`) sends through `d3d8_DrawIndexedExternalVS`
(`TK/d3d/d3d8_device.c:1137-1200`), a 16 MiB/1 MiB dynamic ring. So the host re-streams vertices every draw; the only
stable identity is "(array address, stride, format, index list/count)" and the vertex bytes themselves.

**Feasibility:** the same recipe works: hash (format, stride, vertex count, index list, attribute bytes) at
`draw_primitive` before `gv/gtri` are built, look up a replacement mesh, and substitute `gv/gtri` (and the
texture) before `pgraph_d3d11_draw_program` (:3461). Static props and venue geometry (programs without bone
palettes) are Medium. Fighters are skinned by the vertex program from constant-register bone matrices; a replacement has
to emit the exact attribute layout and bone-index convention the program reads (`nv2a_vsh_inputs_used`), so new models
must be authored/retargeted to the game's skeleton: High to Very High, plus a tool to dump meshes (as `RECOMP_TEXTURE_DUMP_DIR`
does for textures) and a pack format. The HD-lane owner already noted fighter-model refinement as a future item
(`hd-textures-notes.md`, D91). Per-draw hashing of vertex bytes must be cached (a fight is thousands of draws).

## Recommended order

1. **Sampler cache + anisotropy setting** (Low): small, safe, immediately visible on floors with HD packs. Do it with a
   Low-cost mip generator for original textures if the visual gain at 2x is wanted.
2. **FXAA / CAS in `d3d8_present_blit`** with a `[display] aa` and `sharpen` setting, default off (Low). Port the shader to
   `d3d8_vk.c`. This also builds the multi-pass scaffolding for 3.
3. **Present-chain refactor** (merge gamma, LUT grade, bloom) (Low-Medium), still default-off so goldens do not move
   (goldens run at 4:3, gamma ramp off, explicit render scale: `docs/07-m6-plan.md` Testing).
4. **Widescreen spike** (time-boxed): runtime trace of `sub_0001A4E0/0001A610/0001A710`, then a throwaway override to see a
   wider fight. Decide A vs B from the result; this is the highest-impact item and the largest unknown.
5. **MSAA** only if post-AA at render_scale 1-2 looks insufficient (Medium).
6. **Shadow/RTT investigation** (surface trace and batch dump; no code) to learn whether anything render-to-texture exists.
7. **Depth snapshot -> AO/DoF** (High), after 6 tells where the 3D pass ends.
8. **Mesh replacement** (High) as a separate research track after a vertex dump tool; **>60 fps** last or never.

Cross-cutting: every new setting needs a row in `src/hooks/pc_settings.c:35-72` (restart flag when the pipeline must be
rebuilt), `docs/settings-reference.md`, and the overlay/launcher page (`src/hooks/pc_ui*.c*`); toolkit edits go on the
`defjam/m6` fork branch as one-topic commits (CLAUDE.md section 4); Windows D3D11 and Vulkan implementations must both be
kept alive (D42 and the macOS/Linux releases).

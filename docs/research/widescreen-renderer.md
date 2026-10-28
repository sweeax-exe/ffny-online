# Widescreen: what the renderer does today and how to widen it

> **Errata (2026-10-11):** `gWidescreenMode` is at `0x29A9D8` (value `0x29A9D4`, set in `sub_0006B760`) and does nothing when set to "1" (tested); the game also discards the Xbox video flags, so the `XC_VIDEO` question in section 0 item 3 is moot. See `docs/widescreen/01-findings.md` sections 1 and 5.

Read-only code-reading pass over the toolkit (`tools/xboxrecomp/src`), the game-side hooks (`src/hooks`, `src/main.c`)
and the docs. Nothing was built or run. Two logs from the main checkout (gitignored, cited by name) were read for
evidence of what the title draws.

Tags: **[V]** verified by reading the cited code or log. **[I]** inference or recollection, with what would settle it.
Paths are repository-relative. `toolkit/` below means `tools/xboxrecomp/src/`.

## 0. Summary of findings that change the design

1. **There is exactly one render target.** The production path (`RECOMP_PB_EXEC` + `RECOMP_PB_D3D11`, both defaulted on by
   `src/hooks/pc_settings.c:301-313`) never switches render targets. `SET_SURFACE_COLOR_OFFSET`/`PITCH`/`FORMAT` only update
   executor bookkeeping (`toolkit/kernel/nv2a_pb_exec.c:3986-4000`); every draw and every clear lands in the device's single
   default colour+depth target. No offscreen target, texture copy or blit exists on this path. [V] The Vulkan device is the same
   (`SetRenderTarget` is a one-line stub, `toolkit/d3d/d3d8_vk.c:2513`). [V] "Main framebuffer vs other surface" is therefore
   not a distinction the translator can make today; D42 (`PROGRESS.md:88`) records this as a deliberate choice. [V]
2. **Positions reach the host in guest pixels, and one function turns them into clip space in every backend.** The same
   `Screen.xy = 2/logical size` constant drives the generated vertex-program HLSL (`toolkit/kernel/nv2a_vsh_cpu.c:393`), the
   Vulkan pre-transformed shader (`toolkit/d3d/d3d8_vk.c:523-540`) and the D3D11 fixed shader's `PRETRANSFORMED` branch
   (`toolkit/d3d/d3d8_shaders.c:155-156`). That constant is already re-uploaded **per draw** in both backends
   (`toolkit/d3d/d3d8_extvs.c:262-270`, `toolkit/d3d/d3d8_vk.c:1328-1343`). A per-draw x remap is therefore a two-float
   change, not a pipeline change. [V]
3. **Game-side hint worth checking first.** The XBE contains the Flash global names `gPlatform` ("XBOX") and
   `gWidescreenMode` (initial value "off"), registered as value/name pairs in the string area around VA `0x29AC58`
   (file offset `0x289A18`). The title's UI scripts therefore have a built-in widescreen mode. [V for the strings; I for what
   sets it.] The runtime reports `XC_VIDEO = WIDESCREEN|HDTV` (`toolkit/kernel/kernel_xbox.c:147-151`) but with the toolkit's
   own constants `0x01|0x02` (`toolkit/kernel/kernel.h:1098-1099`); my recollection is that the XDK's flag is `0x00010000`, so a
   game testing the real bit would see "not widescreen". [I - check against the XDK header and the lifted caller of
   `ExQueryNonVolatileSetting`.] If the game has an anamorphic 16:9 path of its own, architecture A below needs little or no 2D/3D
   classification. This is the cheapest experiment in this document.
4. **Letterboxing is the surface clip, clears ignore rectangles.** The cutscene letterbox is `SET_SURFACE_CLIP_*` turned
   into a scissor (`toolkit/d3d/d3d8_device.c:1485-1505`); `CLEAR_SURFACE` clears the whole target and ignores the clear rectangle
   and the scissor (`toolkit/nv2a/nv2a_pgraph_d3d11.c:581-620`, `toolkit/d3d/d3d8_device.c:635-667`). Window clip
   (`0x2B4`, `0x2C0`, `0x2E0`) is unhandled. [V]

## 1. Present path

### 1.1 How the picture reaches the window (verified)

There is no guest front-buffer copy. The soft rasteriser that wrote guest memory is off whenever `RECOMP_PB_D3D11` is set
(`toolkit/kernel/nv2a_pb_exec.c:855-860`); the host target *is* the frame. [V]

Frame end, Windows:

1. `NV097_FLIP_STALL` paces the flip, then calls `pgraph_d3d11_frame_end()` (`nv2a_pb_exec.c:4169-4242`, call at 4233-4242), which
   flushes the translator and calls the host callback (`toolkit/nv2a/nv2a_pgraph_d3d11.c:1193-1198`).
2. The callback is `d3d11_translator_frame_end` (`src/hooks/d3d11_translator.c`, registered at `src/main.c:709`). It runs
   `capture_backbuffer` (the `RECOMP_TRANS_SHOT` path), then `dev->Present` and sets the flip timer.
3. `dev_Present` (`toolkit/d3d/d3d8_device.c:547`) calls `d3d8_present()` (126-160): `d3d8_gamma_begin` (a ramp pass on the game
   target, `toolkit/d3d/d3d8_gamma.c:112-164`), then `d3d8_present_blit()`, then `IDXGISwapChain_Present`, then `d3d8_gamma_end`
   (restores the un-ramped target so captures and the next frame are clean).
4. `d3d8_present_blit` (`toolkit/d3d/d3d8_present.c:270-315`): makes the swap chain follow the window
   (`present_follow_window`, 218-258, `ResizeBuffers` on any client-size change), clears the window black (296), sets the viewport to
   `d3d8_present_fit(num, den, window, keep)` (286-293; the maths is in `toolkit/d3d/d3d8_present.h:33-54` and is unit-tested by
   `tools/xboxrecomp/tests/d3d8_present_fit`), and draws a full-screen triangle sampling the whole game texture
   (shader at 182-190; linear or nearest sampler, 294). Then it calls the host overlay (311-313).
5. The "game texture" is an `R8G8B8A8` texture of `logical size x render scale`, created by `d3d8_present_create_game_target`
   (`d3d8_present.c:142-166`) from `d3d11_create_render_targets` (`toolkit/d3d/d3d8_device.c:360-413`); the depth buffer is
   `D24S8` of the same size (388-405).

### 1.2 Render scale

`RECOMP_RENDER_SCALE` (default 2) or `d3d8_present_set_render_scale` (settings `display.render_scale`, restart-only,
`src/hooks/pc_settings.c:54-57`, applied at 522) sets `state->render_scale` (`d3d8_device.c:282-288`); `state->width/height =
logical x scale` (354-355); the logical size is whatever the host passed to `CreateDevice` (289-290), which is
`TRANSLATOR_WIDTH/HEIGHT` 640x480 (`src/hooks/d3d11_translator.c:48-49,404-408`). All consumers of "screen size" use the *logical*
getters (`d3d8_GetBackbufferWidth/Height`, `d3d8_device.c:192-193`), so the scale is invisible to the NDC maths; the scale is
applied explicitly only to the scissor (`d3d8_device.c:1496-1500`) and the capture (see 7). [V] Vulkan: `vk.scale`, `vk.tw/th`
(`d3d8_vk.c:2985-2989`), same shape. [V]

### 1.3 `aspect`

`display.aspect` is an ENUM `{"4:3","stretch"}` (`src/hooks/pc_settings.c:33,52-53`). `pc_settings_apply_display` maps it to
`d3d8_present_set_aspect(aspect == 0, 4, 3)` (321). Keep = letterbox/pillarbox via `d3d8_present_fit`; stretch = whole window.
Both backends implement the same entry point (`d3d8_present.c:31-38`, `d3d8_vk.c:86-90`); Vulkan fits with `present_rect`
(`d3d8_vk.c:1671-1690`) and `vkCmdBlitImage` of the whole target (2238-2244). [V]

### 1.4 Overlay

ImGui is drawn after the picture, in **window** pixels, on the window's own target: D3D11 `g_overlay(...)` at
`d3d8_present.c:311-313` (callback `pc_ui_overlay`, `src/hooks/pc_ui.cpp:983`, which uses `io.DisplaySize = window size`,
1149); Vulkan `overlay_draw` (`d3d8_vk.c:2040-2075`) inside its own render pass on the swap-chain image. Captures read the game
target, so the overlay is never in them (header comment `d3d8_present.h:95-102`). The overlay is independent of picture aspect
and needs no change for widescreen. [V]

### 1.5 Adding `16:9`, `16:10`, `21:9`, `auto (match window)`

Mechanically small and identical for both backends, because everything goes through `d3d8_present_set_aspect(keep, num, den)`:

- `src/hooks/pc_settings.c:33` extend `k_aspect` (keep `4:3` = 0 and `stretch` = 1 so existing `settings.ini` files and tests keep their
  meaning; append `16:9`, `16:10`, `21:9`, `auto`). `pc_settings_apply_display` (321) becomes a switch returning `(keep, num, den)`.
  The enum label list in `src/hooks/pc_ui.cpp:120,508` needs no change for new values (it shows the strings), but the strings are
  user-facing text. `docs/settings-reference.md:40`, `docs/build-and-play.md:164`, and the settings-reference unit test
  (`tests/unit`, the check that every setting is documented) need the new values.
- What `num:den` *means* depends on the architecture (section 8): in A it is the **presentation** aspect only (the target stays
  640x480); in B it is also the **target** aspect, so changing it changes the size of the game texture, depth buffer, gamma
  snapshot and capture, which is why B's `auto` needs a target-resize path and A's does not.

## 2. Render targets, viewport, clip

### 2.1 Surfaces (verified unless marked)

- `SET_SURFACE_CLIP_HORIZONTAL/VERTICAL` -> `s_gpu.clip_x/w/y/h` (`nv2a_pb_exec.c:3975-3985`); `PITCH` (3989), `FORMAT` (3986),
  `COLOR_OFFSET` (3992) -> `color_offset` + `surf_note_target`. `ZETA_OFFSET` (4275). None selects a host target.
- Not parsed: the anti-aliasing field of the surface format (nothing reads bits 12-15 of `format`), so a 2x-wide AA surface is
  drawn at guest coordinates into the 1x logical target. D42 explains the substitution (render-scale supersampling instead of
  modelling the AA surface).
- Evidence of what the title uses (log `logs/run-20260928-232159.log.err` and `logs/run-20261001-204440.log.err` in the main
  checkout, `[SURF]` lines): three colour surfaces, `0x82A90000` (pitch 5120, selected thousands of times, i.e. the 3D/main
  surface), and `0x82E14000`, `0x82CE8000` (pitch 2560, clip 640x480, format `0x1128`, the display pair). Per-batch dumps
  (`logs/run-20260930-212757.log.err`, frames f12120-f14822) show the main surface with `surface clip 640x480+0+0` throughout.
  Decoding `0x1128` as colour A8R8G8B8, zeta Z24S8, pitch layout, AA mode 1 is from my memory of the NV2A register layout [I]; the
  executor's own comment says the same ("surface format 0x1128, AA mode 1", `d3d8_device.c:279-281`).
- The title composites its fight through a full-screen quad that samples the main surface's address (comment at
  `nv2a_pb_exec.c:2681-2685`); `RECOMP_SKIP_RT_SAMPLE` exists to drop it (3413-3428), default off. How the shipped path gets away
  with this is not settled by reading [I] (the texture lookup would read stale guest memory; candidates are that the quad is not
  reached in the default configuration or that it is overdrawn). Whatever it is, it is not something widescreen changes.

### 2.2 Viewport

- `SET_VIEWPORT_OFFSET (0x0A20..)` and `SET_VIEWPORT_SCALE (0x0AF0..)` are written into vertex constants **59 and 58**
  (`nv2a_pb_exec.c:4303-4315`) because the D3D epilogue (`MAD oPos.xyz, R12, R1.x, c[59]`) reads them from the constant file
  (`toolkit/kernel/nv2a_vsh_cpu.c:13-17`). The program's output position is therefore **guest pixels** (x,y) plus zeta-unit depth and
  `w = 1/w_clip`. [V]
- The translator's own copies `g_pg.vp_offset/vp_scale` are stored and never used (`nv2a_pgraph_d3d11.c:211-213,703-721`). [V]
- The host viewport is always the whole target: initial (`d3d8_device.c:1783-1794`), `dev_SetViewport` (1467-1482) is not called by the
  executor, Vulkan `pass_begin` (`d3d8_vk.c:417-440`, negative-height viewport over `tw x th`). [V]
- Pixel -> NDC: vertex-program path `ndc = (R12.x*Screen.x - 1, 1 - R12.y*Screen.y)` (`nv2a_vsh_cpu.c:393`) with `Screen = (2/lw,
  2/lh, 1/zmax, point size)` and `TexScale` in the second float4 (`d3d8_extvs.c:262-269`; `frame[6]`, `frame[7]` = `TexScale.zw` are
  written as 1.0 and unused, a free pair); pre-transformed path `d3d8_shaders.c:155-156` / `d3d8_vk.c:523-540`. [V]

### 2.3 Scissor and clears

- Surface clip -> `pgraph_d3d11_set_surface_clip` (`nv2a_pgraph_d3d11.c:1143-1148`) -> `d3d8_SetScissorRect`
  (`d3d8_device.c:1488-1505`: guest rect times render scale; zero size lifts it; rasteriser has the scissor test always on,
  `d3d8_states.c:234`). Vulkan: `d3d8_vk.c:1455-1488`. The executor sets it only just before a draw that reaches the translator
  (`nv2a_pb_exec.c:3282-3288`). [V] The ledger entry is `docs/research/toolkit-rebase-ledger.md:94`.
- Clears: `CLEAR_SURFACE` -> `Clear(dev, 0, NULL, ...)` (`nv2a_pgraph_d3d11.c:595`) -> `ClearRenderTargetView` /
  `ClearDepthStencilView` on the whole target (`d3d8_device.c:641-664`), Vulkan `vkCmdClearAttachments` over `tw x th`
  (`d3d8_vk.c:2294-2326`). The clear rectangle registers are stored (`nv2a_pgraph_d3d11.c:573-579`) and never applied. The soft
  clear honours the clip but is off here (`nv2a_pb_exec.c:862-874`). [V]
- Window clip is not handled in either layer (no case in `nv2a_pb_exec.c:3974-4345`; absent from the ignore list in
  `nv2a_pgraph_d3d11.c:761-784`). [V]

### 2.4 Code that assumes the host target's aspect equals the guest's

| Site | Assumption |
|---|---|
| `toolkit/d3d/d3d8_device.c:289-290,354-355,388-389` | target and depth = logical x scale, logical = what the host asked for |
| `toolkit/d3d/d3d8_device.c:1496-1500`, `d3d8_vk.c:1455-1472` | scissor = guest x scale, same scale in x and y |
| `toolkit/kernel/nv2a_vsh_cpu.c:393` | `Screen.x = 2/lw` |
| `toolkit/d3d/d3d8_extvs.c:262-263`, `d3d8_vk.c:1334-1335` | same, uploaded per draw |
| `toolkit/d3d/d3d8_shaders.c:155-156,941-942,973-974`, `d3d8_vk.c:523-540` | pre-transformed path |
| `toolkit/d3d/d3d8_extvs.c:56` (point-sprite GS) | sprite half-size = `sz * Screen.xy`; pixel-sized in both axes only if the NDC maps square pixels |
| `toolkit/d3d/d3d8_present.c:286-293`, `d3d8_vk.c:1671-1690` | the whole target is shown at `num:den` |
| `src/hooks/d3d11_translator.c:48-49,497-530,610-640` | capture is exactly 640x480 x scale, box-filtered, rows read with the locked pitch |
| `toolkit/d3d/d3d8_vk.c:1225-1257,2922` (readback ring, `sf_LockRect`) | readback is `tw x th` |
| `toolkit/d3d/d3d8_gamma.c:112-160`, `d3d8_vk.c:1890` | follow the target's size (no assumption) |

## 3. Feasibility of a "wide host target"

### 3.1 Concept

Keep the guest's 640x480 coordinates. Define a host logical size `Lw x 480` (`Lw = 2*round(480*A/2)`; 854 for 16:9, 768 for 16:10,
1120 for 21:9) and a guest -> host offset `ox = (Lw - 640)/2`. Guest x in `[-ox, 640+ox]` is visible.

Because every consumer already works in *logical* units and scale is applied afterwards, the change is "logical width differs from
guest width" rather than a new coordinate system. Two numbers must be separated: the **guest** size (640x480: capture, HD
textures, executor, scissor input, tests) and the **host logical** size (`Lw x 480`: target, depth, NDC constants, present fit).

### 3.2 Changes by area

- **Target and depth**: create them at `Lw*s x 480*s`. D3D11: `d3d8_device.c:289-290,354-355` and the two creators it feeds
  (`d3d8_present.c:142`, `d3d8_device.c:388`). Vulkan: `vk.lw`/`vk.tw` (`d3d8_vk.c:2985-2989`), plus the readback ring (2922), the
  gamma target and framebuffers (`gamma_make`, 1705-1870). Resize needs the target recreated at a frame boundary (after
  `d3d8_present`), the RTV rebound (`d3d8_device.c:408`), the gamma snapshot (already size-aware, `d3d8_gamma.c:112-137`) and
  `g_device_state.viewport` reset (1783-1794 pattern). Dimensions: 21:9 at 4x is 4480x1920, well inside D3D11 and the Vulkan
  limits MoltenVK reports; memory about 34 MB colour + 34 MB depth. [V on the sites; I on the limits.]
- **Viewport transform**: keep the full-target viewport. Replace `Screen.x` by a per-draw `(kx, bx)`:
  `ndc.x_host = ndc.x_guest * kx + bx`. For the generated HLSL the edit is one line (`nv2a_vsh_cpu.c:393`), reading a new
  `Wide` field in `VshFrame`; for the fixed paths, `d3d8_vk.c:523-540` and `d3d8_shaders.c:155-156` (the D3D11 constant buffer
  has `ScreenSize` and padding, `d3d8_shaders.c:44-46`). `frame[6]/[7]` (`TexScale.zw`) are the zero-cost carriers; the point
  sprite geometry shader shares the `b2` buffer (`d3d8_extvs.c:39,56`) and needs its pixel size mapped with `2/Lw` rather than
  `2/640` so glows stay round. Constants c58/c59 do **not** need rewriting: the shader divides them out into NDC first.
- **Scissor / surface clip**: apply the same class mapping to the rectangle: a 2D-class draw gets `(x + ox)*s` .. `(x+w+ox)*s`;
  a 3D-class draw gets `x*s*Lw/640`. Edits in `d3d8_SetScissorRect` (`d3d8_device.c:1488`) and the scissor block of `bind_draw`
  (`d3d8_vk.c:1455-1472`), which must also be re-evaluated per draw (today it is cached by rectangle value,
  `d3d8_device.c:1493-1494`, `d3d8_vk.c:1484-1488`). 2D draws **must** be scissored to the 640 box: the menu dump shows UI quads
  reaching x = 754 (`logs/run-20260930-212757.log.err`, f14822: "x 518.0..754.0"), off-screen slide-ins that a wide target would reveal.
- **Clears**: unchanged in effect (whole target; for B that is the right behaviour for the black bars). If clear rectangles are ever
  honoured, a 640-wide rectangle needs the class-2 mapping. Cutscene letterbox is vertical so full-width bars appear for free.
- **Texture copies, render-to-texture, readbacks**: none exist on this path (section 0 item 1), so nothing to remap. Capture is
  the only readback (section 7). The title's own sampling of surfaces (`surf_holds_texture`, `nv2a_pb_exec.c:2686-2695`) is a
  diagnostic, not a copy.
- **Keeping off-screen targets at their own sizes**: there is nothing to keep, today. The translator would first have to grow real
  render-target switching (the project D42 declined). If that is ever built, "main" can be identified without guessing: the
  surface whose address is flipped. The executor already tracks `drawn_offset` and the flip registers
  (`nv2a_pb_exec.c:289-296,4145-4231`), and the display pair is the one logged with pitch 2560 / clip 640x480; in the logs the
  AA scene surface is pitch 5120. A cheap guard for now: compute `is_main = (clip_w == 640 && clip_h in {...} && pitch in {2560,
  5120})` when `SET_SURFACE_*` change, and force class "identity" for anything else so shadow/portrait passes never receive the wide
  mapping. [I; they are drawn into the same target either way.]

### 3.3 Verdict

Feasible, and not much code: roughly two shader lines, one cbuffer field, one scissor remap, two target creators and a capture
crop, in each backend. The cost and the risk are almost entirely in **classification** (section 4) and **auto aspect**
(target resize), not in the plumbing.

## 4. Per-draw 2D / 3D classification

### 4.1 What the translator has per draw

Two draw routes reach the host (both verified, `nv2a_pb_exec.c:3030-3466`):

| Route | When | Position | Notes |
|---|---|---|---|
| `pgraph_d3d11_draw_program` (GPU vertex program) | program mode, point/line/tri primitives, program compiles | object-space attribute 0 through the program; output is guest pixels with `w = 1/w_clip` | HLSL from `nv2a_vsh_emit_hlsl` (`nv2a_vsh_cpu.c:268-405`); constants c[0..191] in `b1` (`d3d8_extvs.c:258-260`) |
| `pgraph_d3d11_draw_ready` | no program (fixed), or CPU fallback | already guest pixels; `ready[n].w = 1` for the fixed case (`nv2a_pb_exec.c:3276`) or `o.pos[3]` for the CPU program case (3232) | `OutputVertex.rhw` is always 1 (`nv2a_pgraph_d3d11.c:1509,1526`); `w` only drives near-plane clipping (1219-1330) |

Signals available at the call, with how reliable each is:

1. **No vertex program** (`xf_mode != 2`, `nv2a_pb_exec.c:341-344`): positions are pixels by construction -> 2D. Strong. [V]
2. **Vertex program hash** (`nv2a_vsh_program_hash`, `nv2a_vsh_cpu.c:409-421`): stable across the run, cached already
   (`pgraph_d3d11_draw_program` computes it, `nv2a_pgraph_d3d11.c:1619`). The game has a finite program set (D3D's generated
   fixed-function programs plus APT/UI and effect programs). A table `hash -> {3D, 2D, ...}` built from the batch dump is the most
   robust classifier. [I on the size of the set; the dump `RECOMP_PB_BATCH_DUMP` already prints per-batch extents, formats and state.]
3. **Viewport constants c58/c59** (`nv2a_vsh_constants()` returns the 192-float file the executor already holds): a draw whose viewport
   is the full surface (`c58.xy = (320,-240)`, `c59.xy ~ (320,240)` plus the half-pixel bias) is a candidate for 3D; a smaller viewport
   is an inset 3D scene (create-a-player and portrait cameras, "SetCPViewportCam" strings are in the XBE) that must **not** be
   widened. [I; values not confirmed in logs.]
4. **Perspective test on the output**: run the first three vertices through the CPU interpreter (`nv2a_vsh_run_ready`, already used
   for the fallback path) and look at `pos.w`: constant 1 across a triangle = orthographic/2D; varying = perspective. A handful of
   vertices per batch, no per-vertex cost. [I]
5. **Vertex format**: UI batches in the dumps are `v0 float3 (stride 12), v1 float2, v2 D3DCOLOR` (`[BATCH] attrs:` lines), scene
   meshes carry more streams. Weak alone.
6. **Depth state**: a fight's 3D is depth-tested with z in zeta units near 1.64e7 (`[BATCH] z ...` lines); but the front end uses
   depth for APT masks (D38, `PROGRESS.md:84`), so "depth on" does not mean 3D. Weak.
7. **Textures**: movie frames are packed YUV (formats `0x24/0x25`, section 6). Strong for that class.
8. **Full-screen quad** (section 5).
9. **Game tag** [I]: because the Hor+ patch lives in the game, it can mark the 3D view itself (for example by leaving a recognisable
   value in the projection constants it writes), which turns the classifier into an exact test for the one case that matters.

Prior art (from memory; not verified here): Dolphin's widescreen hack scales the perspective projection and leaves orthographic
matrices alone, identifying them by projection type; RT64 does not guess, the recompiled game marks aspect-sensitive groups and
2D rectangle alignment through extended commands. This title's analogue of "projection type" is signals 1-4; its analogue of RT64's
tags is signal 9. The robust design is hash table first, heuristics as the unknown-program fallback, logging every unclassified
hash once.

### 4.2 Hook site and cost

- Decision point: the two translator entries (`pgraph_d3d11_draw_program`, `nv2a_pgraph_d3d11.c:1605`; `pgraph_d3d11_draw_ready`, 1447),
  or just above them in the executor (`nv2a_pb_exec.c:3282-3466`) where the program, depth, blend and surface state are all in scope.
  The result is a small struct `{kx, bx, scissor mapping}` passed down with the existing arguments.
- D3D11: `d3d8_Nv2aProgramDraw` writes `frame[8]` into `b2` every draw (`d3d8_extvs.c:262-270`) - add `kx, bx` at `frame[6], [7]` and make
  the HLSL read `TexScale.zw`. Ready draws: set `cb->screen_w`/a new field at `d3d8_shaders.c:941-942`. No extra state changes and no
  extra draw calls.
- Vulkan: `frame_constants` (`d3d8_vk.c:1328-1343`) is already called per draw for both routes (1630 and 2427); same two floats
  in `f[6]`, `f[7]`. `s_vs_fixed` (523-540) takes them from `Screen`/`TexScale`.
- Cost: two floats and a few compares per batch. The perspective test adds at most three vertex-program runs per batch (a fight
  submits hundreds of batches a frame, while the CPU path ran tens of thousands of vertices before M4e), so negligible.
- Alternative hook: per-draw viewport (D3D11 `RSSetViewports`, Vulkan `vkCmdSetViewport`) instead of a shader constant: a 2D draw
  gets a viewport that is the 4:3 sub-rectangle, a 3D draw the whole target. This needs no shader change at all (NDC then maps into
  the viewport), works for the fixed and the program paths alike and is cheap, but costs one state change per class flip, makes
  the scissor and `SV_Position`-reading pixel code see viewport-relative coordinates (none of our shaders read them except the gamma
  pass), and clips geometry to the viewport automatically, which is a *plus* for 2D (content outside the 640 box is clipped) and
  removes the need to scissor-remap 2D draws. It is the cheapest first prototype. For 2D "anchored" placement the same trick works
  (viewport at the left or right edge).

## 5. Clears and full-screen quads

- Clears: section 2.3. They are always whole-target, so in B the bars/outside region are cleared correctly with no work.
- Full-screen quads in the logs: `prim 6 n 4 ... x 0.0..640.0 y 0.0..480.0` (a 128x128 palettised texture, white diffuse, in fights:
  `logs/run-20260930-212757.log.err` f12408-f13992) and untextured `prim 5 n 6 ... x 0.0..640.0 y 0.0..480.0 col FFFF00FF` (the
  colour-masked depth-reset passes of APT, f14822). [V]
- Detection: on the ready path the vertices are already in `ready[]` pixels (`nv2a_pb_exec.c:3273-3280` and CPU program branch), so
  "all x within [-1, 641], all y within [-1, 481], covers both extents" is five compares. On the GPU path the vertices are object space;
  take the three-vertex interpreter run from 4.1(4). The cost is the same.
- Policy by purpose, not shape: (a) a fade/dim/vignette over the picture -> stretch to the whole wide target (class "fill"); (b) the
  depth-mask resets (colour writes off) -> stay in the 4:3 box with the masked content, since the content they mask is there; (c) a movie
  quad -> box with bars (section 6); (d) everything else full-screen -> ask the table. The conflict is only between (a) and (b): split by
  `color_mask` (the executor already skips colour-masked draws that write no depth, `nv2a_pgraph_d3d11.c:1418-1425`) and by
  `blend_enable`/texture presence. [I]

## 6. Movies

- A movie frame is the title's own decoder writing a **640x480 linear packed-YUV texture in place** and drawing a quad with it. Evidence:
  the executor samples `0x24` (YUY2) and `0x25` (UYVY) "how a title hands over a decoded video frame" (`nv2a_pb_exec.c:1209-1232`);
  the translator re-reads cached textures every 8 ms and re-uploads on a changed signature specifically for a movie decoded into the
  same buffer (`nv2a_pgraph_d3d11.c:842-996`); rows of 1280 bytes for 640 texels (`nv2a_pb_exec.c:388-393`) = 2 bytes/texel, and the
  D3D8 layer converts YUV to BGRA at upload (`toolkit/d3d/d3d8_resources.c:451-481`); linear texel coordinates are normalised by the
  row width (`nv2a_pb_exec.c:3263-3272`). The movie quad covering the whole screen is stated in the comment at 3263-3267. [V]
- How the HD work excludes them: `try_texture_pack` accepts only DXT and colour formats `0x06,0x07,0x0B,0x12,0x1E`
  (`nv2a_pb_exec.c:2722-2747`, whitelist at 2730), plus `surf_holds_texture`; YUV (`0x24/0x25`) is outside it. `docs/hd-textures.md:92`
  states "YUV/movie and depth formats and tracked render surfaces stay on their original path". So a movie is recognised **by texture
  format** at `bind_texture_ready` time. That the title's movies use `0x24/0x25` rather than a 16-bit RGB linear format is consistent
  with all of the above but not directly confirmed. [I - settle by `RECOMP_TEX_FORMATS=1`, which prints the first texture of each format
  (`nv2a_pb_exec.c:2771-2796`).]
- To pillarbox: when the bound stage-0 format is `0x24/0x25` (or a 640x480 linear quad whose texture changes in place), choose class
  "2D centred"; the black bars come from the full-target clear that precedes it. Nothing about the upload path changes. The host-player
  route (`RECOMP_FMV_HOST`, `.wmv` only, own window) does not apply to this title's `.mad` files
  (`docs/research/m2-render-path.md` section 5).

## 7. Interaction with existing features

- **Goldens and captures.** `capture_backbuffer` (`src/hooks/d3d11_translator.c:493-680`) reads the game target through `GetBackBuffer`
  (`d3d8_device.c:589-617`), assumes `width = 640*s`, box-filters `s x s` blocks and writes a 640x480 BMP; `frame-signature.py` hashes
  the BMP, and `tests/golden/*.json` store width 640, height 480 and a SHA-256 (`tests/golden/README.md`; the M4a golden is
  `bbox [0,29,639,479]`). With a wide target the capture must crop the 4:3 region `[ox*s, (ox+640)*s)` so the goldens stay bit-exact,
  and a separate opt-in (like the existing `RECOMP_TRANS_SHOT_NATIVE`, 505-512) should capture the whole wide frame. The Vulkan
  readback has the same shape (`d3d8_vk.c:1225-1257`, `shot_record` 2079). Default widescreen **off** under `RECOMP_SETTINGS=none` (what
  the harness uses, `src/hooks/pc_settings.c` header) keeps `scripts/regress.py` unchanged by construction; `docs/07-m6-plan.md`
  already states "the goldens run at 4:3 with the gamma ramp off and an explicit render scale". For the `visual` check
  (`<data>/test-baselines`) run at 4:3. [V on the sites; I that the default-off approach is enough.] Add a golden for the wide frame
  only as a frame signature (box, counts, hash) per the no-pixels rule.
- **HD textures.** Texture packs bind by source identity and keep guest dimensions and UVs (`docs/hd-textures.md:72-75`); they are
  unaffected by where a quad lands. Pure-translation 2D (B) is pixel-identical to today; A's 25% horizontal squeeze of 2D slightly
  softens HD UI at low render scales. Render scale applies to both.
- **Overlay.** None (1.4).
- **Steam Deck / Linux.** Same Vulkan device (`toolkit/d3d/CMakeLists.txt:21-57`); the Deck's 1280x800 is 16:10. Everything above exists in
  `d3d8_vk.c` in the same shape; the work is done twice (D3D11 and Vulkan), not designed twice. The shaders are shared HLSL
  (`d3d8_vk.c:25-27` header comment) so the shader edit is made once; the OpenGL fallback (`d3d8_gl.c`) does not draw this
  title's push buffer (`d3d8_nv2a_pending.c`) and is out of scope. [V]
- **macOS / MoltenVK.** Same code with high-DPI drawable (`src/host_posix.c:122-123`); window sizes come from `drawable_size`
  (`d3d8_vk.h`). Metal has no triangle fans, already handled (`d3d8_vk.c:2406-2417`). Large wide targets are the only new risk. [I]
- **Settings.** New enum values, docs and the settings unit test (1.5). The 4x scale at 21:9 doubles per-frame GPU cost over 4:3 at the
  same scale; `render_scale` could be capped by pixel budget. [I]

## 8. Architectures

All three share the per-draw `(kx, bx)` mapping and the classifier; they differ in target size.

### A. Anamorphic: game renders Hor+ into 640x480, host stretches to 16:9, 2D is pre-squeezed per draw

- What it is: the target stays 640x480*s. The game patch widens the projection for the chosen aspect (so NDC covers the wider
  view). Present with `num:den = 16:9` (or the window's aspect for `auto`). 2D-class draws get `kx = (4/3)/A` (0.75 at 16:9), centred,
  so after the stretch they have 4:3 proportions; 3D and fill classes get `kx = 1`. Scissors for 2D are remapped into the squeezed box.
- Touches: `pc_settings.c:33,321` (present aspect), shader line `nv2a_vsh_cpu.c:393` and the two fixed shaders, `frame_constants` /
  `d3d8_extvs.c:262-270`, `d3d8_SetScissorRect` and the Vulkan scissor, the classifier in `nv2a_pgraph_d3d11.c:1447,1605`, the game patch.
  No target, depth, gamma, capture, readback change. `auto` is free: the present rect is the window, and `kx` is recomputed from it.
- Pros: smallest change; goldens untouched at 4:3 (kx = 1, A = 4/3); no resize path; trivial Deck/MoltenVK behaviour. If the
  title's own `gWidescreenMode` path (section 0 item 3) already squeezes UI, even the classifier may be unnecessary.
- Cons: horizontal resolution of the 3D is 640*s for any window width (fine at s >= 2, soft at 21:9 and s = 1); 2D renders in 75% of the
  horizontal samples, visible only at low scale; scissors/viewport maths are in squeezed units, which is easy to confuse.
- Effort: 2-4 days plus the game patch. Risk: low-medium (classification).

### B. Wide host target with per-draw x remap

- What it is: section 3 in full; target `Lw*s x 480*s`; 3D `kx = 1` (game-patched projection) or `kx = (4/3)/A` for a host-side Hor+
  prototype that needs no game patch (x_clip scaled before the divide; the game's own culling will pop objects at the new edges); 2D
  centred `kx = 640/Lw`, `bx = 0`, or anchored `bx = +-(1-kx)`; fill `kx = 1`; movies and box-masked content in the 4:3 box.
- Touches: everything in A, plus `d3d8_device.c:289-290,354-405,1783-1794`, `d3d8_present.c:142-166,286-293`, `d3d8_vk.c:1328,1671,2922,2985-2989`
  (and gamma at 1705-1870), `src/hooks/d3d11_translator.c:48-49,497-680` (capture crop), the settings and docs of 1.5, a target-resize path for
  `auto` (snap to a few aspect buckets, recreate at a frame boundary).
- Pros: full horizontal resolution for 3D; physically correct pixel shapes (point sprites, line widths); exact 4:3 2D with no resampling; can
  anchor HUD to screen edges later.
- Cons: more surface area (two backends x target/depth/gamma/readback); resize on window change; memory and fill cost at 21:9 x 4x; capture
  and regression need the crop. 
- Effort: 1-2 weeks including both backends and tests. Risk: medium (classification as in A, plus resize bugs).

### C. Game-level wider guest framebuffer (for example 848x480 surfaces)

- What it is: change the surfaces and viewport the title itself creates (`BackBufferWidth` in its D3D setup, c58/c59, surface clip, the
  AA surface, Flash stage size) so the NV2A command stream is wide and the host needs almost no change.
- Touches: lifted D3D/engine code via `src/recomp_manual.c` overrides, the guest memory sizes for three surfaces, every constant
  640 in game code (UI layout tables, APT stage, camera setup, culling), plus the toolkit's logical size.
- Pros: the host sees a genuinely wide frame; no classification.
- Cons: the 2D UI and APT movies are authored for 640x480 and cannot be moved by one patch; memory layout of surfaces and tiling is fixed by
  the title's allocator; per-screen regressions everywhere; no way to test against hardware behaviour. 
- Effort: weeks to months. Risk: high. Not recommended.

### Recommendation

1. First, spend an hour on the `gWidescreenMode` / `XC_VIDEO` lead (section 0 item 3): if the game has its own 16:9 path, A is nearly free.
2. Build the per-draw `(kx, bx)` hook and the classifier with the **viewport trick** (4.2) as a prototype, in D3D11, with a debug overlay
   or env var that colours each class; fix the hash table from `RECOMP_PB_BATCH_DUMP` runs of the front end, a fight, a cutscene and a
   crib/create-a-player screen.
3. Ship A (stretch present, squeezed 2D). Treat B as an upgrade that reuses everything: the only additions are the target creators, the
   capture crop and `auto`.

## 9. Open questions that reading cannot settle

- Does the title's widescreen path exist and what does it do to 3D and Flash? (section 0 item 3.)
- Exact movie texture format (`RECOMP_TEX_FORMATS=1`).
- What the fight's full-screen composite quad samples in the shipped configuration (2.1).
- Viewport constants c58/c59 for inset 3D views; whether any 3D inset renders into a separate surface.
- Whether the patched projection also changes culling (edge pop-in) and which programs carry the projection.

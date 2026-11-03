# Widescreen implementation and coordinate reference

This describes the shipped game/toolkit boundary. The original emitter-side2D
proposal was rejected after entry probes counted zero calls at its proposed seams.
Use the renderer classifier and verified texture tags; older proposal diagrams
are available in Git history and are not implementation instructions.

## Architecture

- `src/patches/widescreen.c` owns aspect selection, full-screen3D Hor+, cinema
  framing and game-specific signals/tagging. `src/recomp_manual.c` connects its
  replacements/entry hooks to the guest.
- Toolkit `src/kernel/nv2a_pb_exec.c:ws_place_batch` classifies transformed
  constant-depth/w=1batches as2D. Other3D batches retain the widened game view.
  Full-screen untextured/small-texture fills stretch; large pictures center;
  other2D centers, with fight-HUD left/right anchoring using the match heartbeat.
- The renderer applies per-draw clip-space placement and the matching scissor,
  keeping off-stage slide-ins hidden. A global2D stretch would distort fonts,
  logos, prompts and sub-view previews.
- Texture identity tags and compatible `[wide]` registrations handle particular
  menu layers. Same-shape replacements alone do not change source geometry,
  clipping or animation; side slabs and foreground UI remain separate.

## 2. Coordinate maths (normative)

Symbols: `A` target aspect (width/height of the output picture), `A0 = 4/3`, `k = A0 / A` (< 1 when wide).
Guest pixel x runs 0..640 across the full canvas. Elements are authored in a 640x480 layout.

| Placement | Guest x after (`x` before) | Width after (`w` before) |
|---|---|---|
| Centre (default for all 2D) | `x' = 320 + (x − 320)·k` | `w·k` |
| Anchor left | `x' = x·k` | `w·k` |
| Anchor right | `x' = 640 − (640 − x)·k` | `w·k` |
| Stretch (full-screen fills) | `x' = x` | `w` |
| Anchor inside a centred box of aspect `B` (HUD "centered" mode, `B = 16/9`) | `x' = 320 + (anchor_B(x) − 320)·(B/A)` where `anchor_B` is the left/right formula with `k_B = A0/B` | `w·A0/A` |

3D (full-screen views only, rule R2):

```
if A > A0:  fov' = 2·atan( tan(fov/2) · A/A0 )     (fov is the game's horizontal angle in degrees)
            aspect' = A                             (keeps m11 = cot(fov/2)·A0: vertical view unchanged)
            fov' = min(fov', fov_cap)               (fov_cap 150° by default; see §5)
else:       unchanged (4:3; the present step letterboxes)
```

Sub-views (viewport rectangle smaller than the screen, any mode): squeeze the rectangle with the centre formula,
leave fov and aspect as authored. The guest renders the view narrower in guest pixels; the wide canvas expands it back.

Cutscenes (mode 1; authored aspect `C = 16/9`):

| Output | `cutscenes = authored` (default) | `cutscenes = fill` |
|---|---|---|
| `A ≥ C` (16:9, 21:9, 32:9) | Full height; aspect `A`, Hor+ from16:9. At16:9 this matches the authored framing. A centered16:9cinema box on ultrawide is not implemented. | Same full-height Hor+ path. |
| `A0 < A < C` (16:10) | full guest width, height `480·A/C` centred (thin top and bottom bars); aspect `C`, fov as authored | full screen; aspect `A`, fov as authored (slightly taller view: Vert+) |
| `A ≤ A0` (widescreen off) | original: 640x360 at y = 60 | same |

Point sprites and lines (host): their size is given in guest pixels; on the wide canvas the x size must be multiplied
by `640/Lw` so glows stay round.

Worked numbers: 16:9 → `k = 0.75`, `Lw = 853.3` (target 854·s), fov 30° → 39.32° (measured). 16:10 → `k = 0.8333`,
`Lw = 768`, 30° → 35.65°. 21:9 (2.3333) → `k = 0.5714`, `Lw = 1120`, 30° → 50.24°. 32:9 → `k = 0.375`,
`Lw = 1706.7`, 30° → 71.09°. The widest venue FOV (46°) becomes 59.0° at 16:9, 73.2° at 21:9 and 97.1° at 32:9.

## Runtime rules and settings

Center unknown2D by default; preserve source proportions. Widen full-screen3D,
not menu sub-rectangles. Fill classification is separate from full-screen picture
classification. With widescreen off, retain stock4:3goldens and replay; test wider
modes separately. Generic canvas/classifier/present support belongs in the toolkit,
while game addresses and screen-specific signals belong in the parent.

Aspect is chosen at boot. Auto uses the display aspect, snaps near16:9/16:10,
and caps wider automatic discovery at16:9; experimental wider shapes require
explicit selection. Window size and internal render scale are independent.
Switching to/from a wide canvas takes effect on the next Play/start; do not infer
a live canvas rebuild from ordinary window resizing.

Actual `[display]` keys are `aspect`, `widescreen_hud`,
`widescreen_cutscenes`, `widescreen_side_fill` and `widescreen_fov_cap`.
Use the [settings reference](../settings-reference.md#display) for values/defaults;
there is no implemented `side_fill=art`, per-screen AI-art directory, or
`--widescreen` harness option. `RECOMP_WS_ASPECT` is a numeric diagnostic override;
normal player selection is through the launcher/INI.

For centered pictures, frame classification requests side fill only when the
picture is2D without3D. D3D11 implements blurred ambient fill; the pinned Vulkan
`d3d8_RequestSideFill()` remains a no-op, leaving black sides. `black` is the
other setting. This backend difference does not prevent compatible menu-pack
slabs from rendering. Runtime side fill needs no AI model/offline movie tool.

## Platform and measurement scope

16:10Deck gives a768x480logical canvas; render scale2 yields1536x960. Wider
21:9/32:9 remain experimental; HUD Auto uses a centered16:9safe box there, while
cinema currently follows the full-height Hor+ path described above. Narrower than
4:3 leaves the wide path inactive. Validate actual monitor/fullscreen behavior
separately from a resized window or headless gamescope.

Pixel cost rises with output aspect at fixed render height:16:9is about33% more
than4:3,21:9about75%,32:9about167%. A32:9scale4target is approximately6827x1920;
color/depth allocations and pack/driver memory are distinct budgets. Original
anamorphic experiments measured guest work before the larger host target existed,
so their120presents/2s are not a current GPU-performance guarantee.

## Current cinema behavior and artwork boundaries

The Story intro measured mode 1's authored 16:9 picture as 640×360 at y 60 in the original 4:3 frame. At 16:9, rendering the same picture full-height preserves the makers' framing without inventing side content; no AI is required. At 16:10, `authored` retains thin bars (40 px each on an 800-row display), while `fill` exposes a little more vertically. Cast credits/subtitles remain centered 2D.

**Ultrawide implementation limit:** the desired centered 16:9 cinema box in the earlier table is not implemented. Current `ws_view_params` (`src/patches/widescreen.c`, mode 1) takes the full-height Hor+ branch for every aspect≥16:9, including `authored`. Thus 21:9/32:9 can expose unauthored sides; they must not be described as shipped boxed cinema. Widescreen-off keeps the original letterbox. The planned box would also require x/width adjustment at `sub_0001A710`/`sub_001042D0`, since `sub_0001A4E0` outputs only FOV/aspect/y/height. This is a source check, not new native ultrawide acceptance.

Artwork aspect alone never defines placement: source-shaped replacements retain APT triangles/UVs and the renderer's placement/scissor policy. At exact toolkit pin `4a6c88e841dd74d3c279f15b8f8e50110b0df70d` (2026-10-17 audit), large pictures were centered, small-texture/untextured fills stretched, and the Vulkan ambient-fill request was a no-op. Those are historical pin observations, not a current all-platform feature claim. Later `[wide]` slabs supply explicit background extensions; the old `side_fill=art`/per-screen PNG architecture remains a proposal. Default 640×480 captures squeeze the wide canvas; use native/wide captures to judge proportions.

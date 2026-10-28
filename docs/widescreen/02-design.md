# 02 — Design

## 0. Revision (2026-10-11, during the beta.1 build)

Measured while implementing, and now the design: **2D placement lives in the renderer, not in game code.**

- The native 2D quad setters the front-end report named (`sub_00028F90`, `sub_0002E390`, `sub_0002E470`) are
  **never called** in menus or fights (an entry-hook probe counted zero calls over a whole fight route).
- A batch dump of a fight frame shows a clean signal instead: 3D batches come out of their vertex program with
  varying depth, 2D batches (HUD, menus) with `w = 1` and one depth, in 640x480 pixels; P1's HUD pieces end left of
  x = 300, P2's start right of 340, and the timer and centred text straddle the middle.
- So the push-buffer executor classifies every batch (toolkit `ws_place_batch`, `nv2a_pb_exec.c`) and the renderer
  applies a per-draw clip-space placement `x * kx + bx`:
  - 3D: unchanged (the game's Hor+ view already fills the canvas);
  - full-screen 2D, untextured or with a texture of 128 texels or less (fades, tints, vignette): stretched;
  - full-screen 2D picture (movie, poster, backdrop): centred, and the present step fills the sides (blur);
  - other 2D: centred; during a fight (heartbeat from the match step `sub_00087700`, which runs only in matches)
    pieces wholly left of 300 / right of 340 are anchored to that side (to the screen edge, or the edge of a
    centred 16:9 box on ultrawide);
  - every placed batch is clipped to where its own clip rectangle lands (slide-ins stay hidden).
- Game code still owns the 3D view (Hor+, cutscene policy), the settings, and the two signals (k and the heartbeat).

This replaces decision W3's "place 2D in the game code that emits it". The game-side work packages WS-4/5/6/7 shrink
to verification of the classifier per screen; WS-11 became the main mechanism. Known limits of the heuristics are
tracked in [04](04-work-packages.md#status-2026-10-11).

## 1. Architecture in one picture (original plan, superseded in part by section 0)

```
            game code (lifted, src/)                         renderer (toolkit fork)
 ┌────────────────────────────────────────────┐     ┌──────────────────────────────────────┐
 │ 3D: sub_0001A4E0 replacement               │     │ wide canvas: logical Lw x 480        │
 │     Hor+ for full-screen views only        │     │ (Lw = 480·A), guest x 0..640 spans   │
 │     cutscene (mode 1) policy               │     │ the whole canvas ("anamorphic        │
 │ 2D: each emitter squeezes/anchors its own  │ ──▶ │ coordinates, square pixels")         │
 │     x in guest pixels (k = (4/3)/A)        │     │ present fit, capture crop,           │
 │     full-screen fills keep x (stretch)     │     │ point-sprite x scale, side fill      │
 │ sub-views: rect squeezed, aspect unchanged │     │ (optional) per-draw class tint/debug │
 └────────────────────────────────────────────┘     └──────────────────────────────────────┘
```

The guest keeps rendering into its 640x480 coordinate system. The host target is wider (for example 854x480 at
16:9, times the render scale) and maps guest x linearly over the full width. So:

- a 3D view whose projection was widened (Hor+) comes out with correct proportions and full horizontal resolution;
- a 2D element that the game squeezed by `k = (4/3)/A` in guest pixels comes out with its 4:3 proportions, centred or
  anchored;
- a full-screen fill that the game left alone stretches over the whole canvas, as it should.

This is the renderer report's architecture A/B merged: the canvas is wide (B's resolution) but guest coordinates
stay 0..640 (A's simplicity: no per-draw offset, viewport or scissor remapping in the host). All 2D knowledge stays in
game code, where it is exact (decision W3).

Why not classify draws in the renderer: the classifier would have to tell APT masks from fades, movies from
backdrops, and sub-views from full views by heuristics; prior art (RT64, Dolphin) shows those heuristics fail on
split rectangles and skyboxes, and every miss is a visible bug. The game-side emitters are few and known
([03](03-element-catalog.md)). The renderer-side per-draw `(kx, bx)` hook from the renderer report §4 is kept as
**WS-11, a debug overlay and fallback** for any 2D source that cannot be patched in game code.

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
| `A ≥ C` (16:9, 21:9, 32:9) | viewport = centred box of guest width `640·C/A`, full height; aspect `C`, fov as authored. Bars at the sides on ultrawide, none at 16:9. | full screen; aspect `A`, `fov' = 2·atan(tan(fov/2)·A/C)` (Hor+ from 16:9) |
| `A0 < A < C` (16:10) | full guest width, height `480·A/C` centred (thin top and bottom bars); aspect `C`, fov as authored | full screen; aspect `A`, fov as authored (slightly taller view: Vert+) |
| `A ≤ A0` (widescreen off) | original: 640x360 at y = 60 | same |

Point sprites and lines (host): their size is given in guest pixels; on the wide canvas the x size must be multiplied
by `640/Lw` so glows stay round.

Worked numbers: 16:9 → `k = 0.75`, `Lw = 853.3` (target 854·s), fov 30° → 39.32° (measured). 16:10 → `k = 0.8333`,
`Lw = 768`, 30° → 35.65°. 21:9 (2.3333) → `k = 0.5714`, `Lw = 1120`, 30° → 50.24°. 32:9 → `k = 0.375`,
`Lw = 1706.7`, 30° → 71.09°. The widest venue FOV (46°) becomes 59.0° at 16:9, 73.2° at 21:9 and 97.1° at 32:9.

## 3. Rules every work package follows

- **R1 Centre by default.** Any 2D element without an explicit class is centred. A wrongly centred element looks
  pillarboxed, never broken; a wrongly stretched one looks broken.
- **R2 Hor+ for full-screen views only.** In the `sub_0001A4E0` replacement, a view is full-screen when its
  viewport rectangle (`vp+0x14` x, `vp+0x1C` width) is `x = 0` and `width = device width` (`[CRS+0x1C]`).
  Everything else is a sub-view.
- **R3 Full-screen fills stretch.** Fades, dimmers (`PauseMenuGrey`, `PopupMenuGrey`), tint, vignette, scratches,
  flashes, clears. A 2D quad is a fill when it covers 0..640 x 0..480 **and** is untextured or uses a known fill
  texture (vignette/scratches). Full-screen **pictures** (posters, backdrops, movies) are not fills: they are centred
  with a side fill (§4).
- **R4 Off is bit-identical.** With a 4:3 or stretch `aspect` (and always under `RECOMP_SETTINGS=none`, which the harness
  uses) every hook returns the original values; goldens, `replay`, `visual` stay unchanged.
- **R5 Clip 2D to its box.** UI slides in from x up to 754 (seen in batch dumps). Centred or anchored 2D must be
  scissored to its own box so off-screen slide-ins do not appear in the side areas. The game patch sets the clip;
  the existing surface-clip → scissor path carries it.
- **R6 One aspect per boot (v1).** The canvas size and the game patch read `A` at start-up. `auto` uses the primary
  screen's aspect at start-up, windowed or not (a window's size is arbitrary and full screen is often entered later;
  owner's play-test 2026-10-12), snapped to 16:10 or 16:9 within 3%; wider screens get 16:9 unless an ultrawide
  shape is picked explicitly; otherwise the exact value.
  Live switching (resize → new canvas at a frame boundary, `sub_0001A710` re-run) is WS-13, later.
- **R7 Game-specific in `src/`, generic in the toolkit.** The canvas, present, capture and point-sprite pieces are
  generic toolkit features on a fork branch; every address in this pack is game code in `src/patches/widescreen.c`
  (new) with thin `_enter` hooks or replacements in `src/recomp_manual.c`.

## 4. Side fill for centred 4:3 pictures

Content that is a full-screen 4:3 picture (FMV, loading posters, 2D-only menu backdrops) leaves side areas. Options,
chosen by the `side_fill` setting:

- `blur` (default, confirmed by the owner after the FMV trial): at present time, the host fills each side area with a heavily blurred, darkened, horizontally
  stretched copy of the nearest third of the picture (the "ambient" look of video players). The game marks frames that
  need it through a host call (`recomp_ws_side_fill(on)`) from the movie and backdrop hooks, so 3D frames are never
  touched. Cost: one small downsample/blur pass per marked frame.
- `black`: nothing (the canvas clear).
- `art`: a texture pack provides per-screen side art (`mods/<pack>/widescreen/<screen>.png`); falls back to `blur`.
  This is the hook for the optional AI pack ([06](06-cutscenes-movies-and-ai.md)).

## 5. Settings

New entries in `[display]` (settings core `src/hooks/pc_settings.c`; launcher and overlay pick them up from the table;
`docs/settings-reference.md` and its unit test must list them):

| Key | Values | Default (beta → release) | Notes |
|---|---|---|---|
| `aspect` (merged, owner 2026-10-12) | `4:3`, `stretch`, `widescreen (auto)`, `16:9`, `16:10`, `21:9 (experimental)`, `32:9 (experimental)` | `4:3` → `widescreen (auto)` | Restart required in v1 (R6). Env `RECOMP_WIDESCREEN`. `off` keeps today's `aspect` behaviour. First release supports 16:9 and 16:10; `21:9` and `32:9` are shown as "(experimental)", and `auto` on a wider window falls back to 16:9 unless the player picks an experimental value. |
| `hud` | `auto`, `edge`, `centered` | `auto` | `auto` = edge up to 16:9, centred 16:9 box above. |
| `cutscenes` | `authored`, `fill` | `authored` | §2 table. |
| `side_fill` | `blur`, `black`, `art` | `blur` | §4. |
| `fov_cap` | 90..170 degrees | 150 | Horizontal FOV ceiling for Hor+ (only bites at 32:9 with wide venue FOVs). |

One setting, `aspect` ("Picture shape"), holds both the old choices and the widescreen ones (owner, 2026-10-12: merge the two); the widescreen sub-options show under it in the launcher only when a widescreen shape is chosen. With a widescreen shape the picture (the canvas has the chosen
aspect; the present step fits it into the window with bars if the window's aspect differs, e.g. a 16:9 canvas in a
16:10 window). The launcher shows the widescreen options together under "Display".

Launch toggle vs mod (decision W1): the launcher already appears on every launch, so the setting **is** the launch
toggle; `--widescreen=<value>` on the command line overrides it for one run (useful for Steam launch options).
Nothing about widescreen needs the mod folder except the optional side-art pack.

## 6. Steam Deck (16:10) and ultrawide

- **Deck:** `auto` on a 1280x800 screen gives 16:10 (`Lw = 768`). Render scale 2 → 1536x960 target, close to the
  4:3 cost at scale 2 (1280x960). HUD edge-anchored. Cutscenes: thin bars (authored) or Vert+ (fill). The Vulkan
  backend gets the same canvas work as D3D11 (WS-1b); Deck play-test is part of the beta.
- **21:9:** experimental in the first release (owner, 2026-10-11), first-class once the ultrawide venue sweep passes;
  HUD `auto` → centred 16:9 box; cutscenes in a 16:9 box.
- **32:9:** supported but labelled experimental in the launcher until a venue sweep at 32:9 passes; HUD centred;
  `fov_cap` may bite (venue FOV 46° → 2·atan(tan 23°·2.667) = 97°, below the cap).
- **Narrower than 4:3** (5:4, portrait windows): widescreen inactive, 4:3 with bars.

## 7. Performance and memory

Pixels scale with `A`: 16:9 is +33%, 21:9 +75%, 32:9 +167% over 4:3 at the same render scale. The wide canvas at
32:9 and scale 4 is 6827x1920 (about 52 MB colour + 52 MB depth): inside D3D11 and Vulkan limits, but the launcher
should warn (or cap `render_scale` by a pixel budget) at 32:9 x 4. The fight held 120 presents per 2 s at 16:9 and
21:9 in the anamorphic experiment (guest work only; the wider host target is new GPU fill to measure in WS-1).

# 04 — Work packages

Each package is sized for one agent session or a few, ends in a commit (or a short series) on `feat/widescreen`
(game side) or on a toolkit fork branch `defjam/widescreen` (generic renderer side, started from the parent's
current submodule pin), and has acceptance checks an agent can run unattended. Read [02-design.md](02-design.md)
first: its maths and rules R1–R7 are normative.

Ground rules for every package (from `CLAUDE.md`): one worktree per lane; game-specific code in `src/`, generic code
on the fork; `RECOMP_SETTINGS=none` runs (the harness) keep widescreen **off**; `python scripts/regress.py --quick`
green before every commit that touches `src/` or the toolkit, the full run before a merge; captures stay under
`logs/` and are never committed; `PROGRESS.md` updated in the same turn; addresses confirmed in a live run before
building on them (sub-agent reads were wrong on details before; the research reports here have known errata,
[01 §5](01-findings.md#5-errata-to-the-research-reports)).

The experiment in `src/recomp_manual.c` ("EXPERIMENT" section: `sub_0001A4E0` replacement, `sub_0006B760_enter`) is the
starting point for WS-2/WS-3 and is removed or absorbed by WS-2. `RECOMP_WS_UI` and the `sub_0006B760_enter` hook are
deleted there (finding: the flag does nothing).

## Status (2026-10-11)

| WS | State | Notes |
|---|---|---|
| WS-0 settings | done | `[display] aspect` widescreen shapes (merged with Picture shape, 2026-10-12), `widescreen_hud`, `widescreen_cutscenes`, `widescreen_side_fill`, `widescreen_fov_cap`; `RECOMP_WS_ASPECT` for experiments |
| WS-1a canvas (D3D11) | done | toolkit `d3d8_present_set_canvas_aspect`; scissor and point sprites per axis; captures (`RECOMP_TRANS_SHOT_WIDE=1`) |
| WS-1b canvas (Vulkan) | done on Deck and Mac | ported (toolkit `d3d8_vk.c`); Deck scripted runs in [07](07-testing-and-playtest.md) section 7; Mac original/HD, 16:9/16:10 and replay results in [09](09-macos-validation.md) |
| WS-2 Hor+ | done | `src/patches/widescreen.c` `ws_view_params`; full-screen views only |
| WS-3 cutscenes | mostly | 16:9 full screen, 16:10 thin bars; the 16:9 box beyond 16:9 is still Hor+ (experimental aspects only) |
| WS-4/5/6/7 2D | done via the renderer classifier (design section 0) | to verify per screen in the play-test: pause, popups, Story hub, shops, create-a-player, results |
| WS-8 side fill | done (blur, D3D11) | present-time blur of the picture's mips beside the 4:3 box when a full-screen picture was centred |
| WS-9 venue sweep | open | 16:9/16:10 first (owner's scope) |
| WS-10 tests | partly | canvas maths unit test; quick regression stays bit-identical; a widescreen regress check is still to add |
| WS-12 AI side art | to-do | owner: refine seam blending first ([08](08-fmv-ai-pipeline.md)) |

## Order and parallelism

```
WS-0 settings ──┬──▶ WS-1a canvas (D3D11) ──▶ WS-2 Hor+ ──▶ WS-3 cutscenes ──┐
                │                               │                            │
                │                               ├──▶ WS-6 sub-views ─────────┤
                │                               ├──▶ WS-4 native 2D + HUD ───┼──▶ WS-9 sweep ──▶ beta (07)
                │                               ├──▶ WS-5 APT front end ─────┤
                │                               └──▶ WS-7 movies ─▶ WS-8 side fill
                └──▶ WS-1b canvas (Vulkan: Deck, Linux, macOS)  (after WS-1a's API is fixed)
WS-10 tests: grows with every package.   WS-11 debug classifier: any time after WS-1a.
Later: WS-12 AI side-art builder (optional), WS-13 live switching and 32:9 polish.
```

Two lanes can run at once after WS-1a: a game-side lane (WS-2 → WS-3 → WS-6 → WS-4 → WS-5 → WS-7) and a
toolkit lane (WS-1b, WS-8 host part, WS-11).

---

## WS-0 — Settings and plumbing

- **Goal:** the settings of [02 §5](02-design.md#5-settings) exist, reach the game and the renderer, show in the
  launcher and overlay, and are off for the harness.
- **Code:** `src/hooks/pc_settings.c` (`g_display_settings`, `pc_settings_apply_display`), `src/hooks/pc_ui_settings.c`
  (launcher/overlay pages), `src/main.c` (`--widescreen=` argument), `docs/settings-reference.md`,
  `tests/unit/test_input_native.py`-style settings test (the reference test must list every key).
- **API to add** (game side, `src/patches/widescreen.h`): `float ws_aspect(void)` (0 = off; resolved once at start-up per R6,
  `auto` → window aspect snapped to {4:3, 16:10, 16:9, 21:9, 32:9} within 3%), `int ws_hud_mode(void)`,
  `int ws_cutscene_mode(void)`, `int ws_side_fill(void)`, `float ws_fov_cap(void)`.
- **Steps:** add the enum/int entries with descriptions and `RECOMP_SETTING_RESTART`; env overrides `RECOMP_ASPECT`,
  `RECOMP_WS_HUD`, `RECOMP_WS_CUTSCENES`, `RECOMP_WS_SIDE_FILL`; the launcher groups them under Display and greys the
  `aspect` setting when widescreen is on; mark 32:9 "(experimental)".
- **Accept:** settings round-trip unit test; `docs/settings-reference.md` test passes; launcher shows and saves the
  entries (scripted UI drive as in v0.4.0); a `RECOMP_SETTINGS=none` run logs no widescreen activity; regress quick green.

## WS-1a — Wide canvas, D3D11 (toolkit fork)

- **Goal:** the host colour/depth target is `round(480·A·s) x 480·s` while guest x 0..640 still spans it; present,
  capture and point sprites are correct. With `A = 4/3` everything is bit-identical to today.
- **Code** (from the [renderer report](../research/widescreen-renderer.md) §3, line numbers at the pinned commit):
  target and depth creation `toolkit/d3d/d3d8_device.c:289-290,354-355,388`, `d3d8_present.c:142`; present fit
  `d3d8_present.c:270-293` (`d3d8_present_fit` with the canvas aspect); scissor `d3d8_SetScissorRect`
  (`d3d8_device.c:1485-1505`): guest → host x factor becomes `Lw·s/640` instead of `s` (the cache by rectangle value at
  1493-1494 must include the factor); point-sprite geometry shader pixel size (`d3d8_extvs.c:39,56`) x-scaled by
  `640/Lw`; gamma snapshot (`d3d8_gamma.c:112-137`, already size-aware: verify); capture `src/hooks/d3d11_translator.c:493-680`
  (`capture_backbuffer` assumes `width = 640·s`): when the canvas is wide, the normal 640x480 capture box-filters the whole
  canvas down to 640x480 (an anamorphic image in guest coordinates, so existing tools keep working); goldens are only
  compared with widescreen off; and a new `RECOMP_TRANS_SHOT_WIDE=1` writes the full canvas at `Lw x 480` (box filter by `s`)
  for looking at and for the widescreen frame signatures.
- **API:** `d3d8_set_canvas_aspect(num, den)` (or a float) called by the game before device creation; `0` = 4:3.
- **Note:** the NDC constant `Screen.xy = 2/(640,480)` (`nv2a_vsh_cpu.c:393`, `d3d8_shaders.c:155-156`) does **not** change:
  guest pixels still map 0..640 → −1..1. Only the target size does.
- **Accept:** with the canvas at 4:3: `regress.py --quick` green, goldens identical. With `RECOMP_WS_ASPECT=1.7778` and the
  WS-2 (or experiment) Hor+: a wide capture of a fight shows round fighters and round light glows; window resize and
  Alt+Enter keep the canvas letterboxed correctly in a 16:10 and a 21:9 window; no D3D11 debug-layer messages
  (`win-x64-debug` run).

## WS-1b — Wide canvas, Vulkan (toolkit fork)

- **Goal:** WS-1a on the Vulkan backend (Steam Deck, Linux, macOS/MoltenVK).
- **Code:** `toolkit/d3d/d3d8_vk.c`: `vk.lw/vk.tw` (2985-2989), readback ring (2922), gamma target and framebuffers
  (`gamma_make`, 1705-1870), scissor block in `bind_draw` (1455-1472, cache at 1484-1488), `frame_constants` (1328-1343),
  `s_vs_fixed` (523-540), shot path (`shot_record` 2079, readback 1225-1257).
- **Accept:** Linux build on the Steam Deck (as in the Linux release testing): quick regress, a 16:10 fight and a
  16:9 cutscene captured with `RECOMP_TRANS_SHOT_WIDE=1`; macOS build compiles (CI) and, when the owner has his Mac,
  one fight.

## WS-2 — 3D Hor+ (game side)

- **Goal:** production version of the experiment for mode 0 and mode 2 views.
- **Code:** new `src/patches/widescreen.c`; the `sub_0001A4E0` replacement in `src/recomp_manual.c` calls into it.
  Original semantics when off (R4), including "mode ≥ 3 writes nothing" and x87-style double arithmetic for mode 1/2.
- **Steps:**
  1. Full-screen test (R2): `vp+0x14 == 0.0f && vp+0x1C == [CRS+0x1C]` (device width as float; the CRS is
     `sub_00012CF0(0x2FB148, 4)`'s object; read it once, or compare against 640.0 since the guest device stays 640x480).
  2. Mode 0 full-screen: Hor+ per [02 §2](02-design.md#2-coordinate-maths-normative), capped by `ws_fov_cap()`.
  3. Mode 0 / mode 2 sub-views: unchanged here (their rectangle is squeezed by WS-6).
  4. Leave the render-to-texture view (`sub_000177E0`, own aspect) and the restore sites (`sub_0005AEB0`, `sub_0007B9C0`)
     alone; confirm with a log line that they do not pass through the patch (they call `sub_00103E10` directly).
  5. Check the post pass `sub_00115CB0` (reads fov/aspect through `sub_00106450`/`sub_00103840`; clip-space ±0.5): capture
     a fight with and without widescreen and compare the effect's footprint; fix if it assumes 4:3.
  6. Grep for other users of `EAGL::ViewPort::gpProjectionMatrix` (`0x3A51E0`), `gpViewMatrix` (`0x3A5120`),
     `gpViewProjectionMatrix` (`0x3A5260`) and callers of `sub_00103840` that do their own screen-space visibility
     (crowd, particles, name tags, lens flares) and assume 640 or 4:3.
- **Accept:** `[WS]` log shows mode 0 widening only for full-screen views; fight, ffa, crib, gym, unlock routes at
  16:9 and 21:9 with no crash and presents ≥ the 4:3 baseline minus 5%; character-select fighters not widened;
  `RECOMP_SETTINGS=none` quick regress bit-identical.

## WS-3 — Cutscene policy (mode 1)

- **Goal:** [02 §2 cutscene table](02-design.md#2-coordinate-maths-normative) for `authored` and `fill`.
- **Code:** mode 1 branch of the `sub_0001A4E0` replacement for fov/aspect/y/height. **The x/width of the viewport is not
  an output of `sub_0001A4E0`**: the 16:9 box on ultrawide needs the rectangle applied in `sub_0001A710` (it passes
  x, y, w, h to `sub_001042D0`): wrap `sub_0001A710` (or add `sub_001042D0_enter` that adjusts x/w when the calling view
  is in mode 1 and `A > 16/9`).
- **Also:** the cast credits and any subtitle text drawn during cutscenes are 2D (CEN, via WS-4/WS-5). The area outside the
  box must be black: confirm the cinema path clears the whole target (it did at 16:9: no stale pixels).
- **Accept:** intro route at 16:9 (full screen, no bars), 16:10 (thin bars in `authored`, none in `fill`), 21:9 (16:9 box);
  the transition from a cutscene to the creator and back to a menu shows no flash of the wrong framing; the owner's
  check of three cutscenes against the console video framing.

## WS-4 — Native 2D quad core and the fight HUD

- **Goal:** every native 2D quad gets a placement class; the HUD is anchored per player.
- **Code:** the pixel → clip conversion with hard-coded `1/640` (`0x28B924`) and `1/480` (`0x28B920`) in `sub_00028F90`,
  `sub_0002E390`, `sub_0002E470` ([front-end report](../research/widescreen-frontend-2d.md) §4). Replace or wrap each so the
  x it emits follows the current placement: a small placement state (`ws_place_push(class, box)` / `ws_place_pop()`) set by
  entry hooks on the callers below, default CEN (R1).
- **Classes by caller:**
  - FILL: quads covering 0..640 x 0..480 that are untextured or use the vignette/scratch/tint textures (`sub_000D8930`,
    `sub_000D99C0`), `PauseMenuGrey`, `PopupMenuGrey`, fades/flashes (find each with a batch dump in a fight and on pause).
  - HUD (`sub_000B5140`, `sub_000B57A0`, locators from `misc.viv` → `hud.xsh`, `pos_pN_*`, `text_pN_*`): first decode the
    locator table (an offline reader in `scripts/`, metadata only) and record which element belongs to which player and
    side; then L for P1/P3 elements, R for P2/P4, CEN for `pos_over_*` (timer, round, KO). `hud = centered` or `auto` above
    16:9 uses the 16:9 box formula.
  - CEN: pause (`sub_000CD490`), popups (`sub_000D4980`), tutorial/help (`sub_000B6850`, `sub_000D66E0`), loading logo and
    snapshot (`sub_000D6A30`, `sub_0005AF80`), the legal/LOADING screen; spinner → R (bottom-right) as an upgrade.
- **R5:** set a scissor to the element's box for CEN/L/R quads that can slide (pause menu entry animation, popups).
- **Accept:** fight, ffa (4 HUD corners), versus, pause and a tutorial prompt captured wide at 16:9, 16:10, 21:9; bars keep
  their 4:3 proportions (measure a bar's width/height ratio in the wide capture vs the 4:3 capture, within 2%); tints and
  dimmers cover the full width; nothing 2D appears in the side areas except anchored HUD.

## WS-5 — APT front end

- **Goal:** all APT screens centred (CEN) with clipping, backdrops marked for side fill, header/footer anchoring optional.
- **First:** find the APT stage → clip transform. The APT root matrix is identity; the scale is in the APT vertex program's
  `c0/c1`. Capture one front-end frame with `RECOMP_PB_BATCH_DUMP` and read the constants of the full-bleed draw, then
  `RECOMP_WATCH_WRITE` on the constant upload to find the game function that writes them (APT renderer around `sub_0017A4A0` /
  `sub_00165A00`). Patch there: `x_clip' = x_clip·k` (centre is 0 in clip space).
- **Then:** clip to the box (R5) for the whole APT layer; identify backdrop bitmap layers (`_art/backgrounds`,
  `match/0_bg/paper_*`) and the loading posters for PIC/side fill (WS-8 request call); transitions (`trans/t1` 640x2000 slab)
  stay in the box.
- **Watch:** APT also draws colour-masked depth-reset quads (D38) — they must be squeezed with the content they mask
  (they are APT draws, so the stage transform covers them).
- **Accept:** boot route menus, `unlock`, `crib`, `gym`, `story-tour`, `two-matches` results at 16:9 and 21:9: no stretched
  panel, no content outside the box, 3D fighters line up with their panels; the menu at 4:3 bit-identical with widescreen off.

## WS-6 — Sub-views (3D inside panels)

- **Goal:** viewport rectangles smaller than the screen are centred-squeezed with the UI.
- **Code:** `sub_0001A300` (SetViewport x, y, w, h, called from `sub_00062170`, `sub_00062230`, `sub_00078C90`,
  `sub_000796A0`) and the CP viewport cameras (`SetCPViewportCam` `sub_0004BFD0`, `SetSingleCPViewportCam` `sub_0004C020`).
  Apply `x' = 320 + (x − 320)·k`, `w' = w·k` on entry; aspect stays as authored (mode 0 keeps 4:3, mode 2 uses w/h of the
  **unsqueezed** rectangle: patch mode 2 to divide by the original width, or store both).
- **Accept:** character select (4 fighters), create-a-player head, gym move preview and Unlock Fighters at 16:9 and 21:9:
  previews are correctly shaped and sit inside their frames.

## WS-7 — Movies

- **Goal:** FMVs centred with side fill.
- **Code:** trace the movie draw (`sub_000D7060` … `sub_00077020` per the front-end report; confirm the format with
  `RECOMP_TEX_FORMATS=1`: YUV `0x24/0x25` expected). Squeeze the movie quad (CEN) in the game code that emits it and call the
  side-fill request. Fallback if the quad is emitted somewhere awkward: the host recognises YUV-textured full-screen quads
  (WS-11 classifier) and centres them.
- **Accept:** boot route EA/THX logos, the Story intro movie and the attract loop at 16:9 and 21:9: centred, correct shape,
  audio sync unchanged (`movie-audio-clock.md` behaviour), side fill per setting.

## WS-8 — Side fill

- **Goal:** `side_fill = blur | black | art` ([02 §4](02-design.md#4-side-fill-for-centred-43-pictures)).
- **Host (fork):** `recomp_ws_side_fill(int on, uint32_t screen_id)` called by the game per frame; at present, before the
  gamma pass, if on: downsample the canvas's 4:3 centre to a small texture, blur (two passes), darken to ~45%, draw it
  stretched into each side area. Skip on frames without a request.
- **Art:** `mods/<pack>/widescreen/<screen_id>.png` (left and right halves of a `Lw x 480` image with the centre transparent),
  loaded through the texture-pack infrastructure; `screen_id` is a stable hash of the backdrop texture or the movie name.
- **Accept:** menus and movies at 16:9 look as designed in all three modes; no side fill ever on a fight frame (assert in a
  test run: the request counter is 0 during the `fight` route's match).

## WS-9 — Venue, camera and cutscene sweep

- **Goal:** every venue and shot type looked at in wide, issues triaged.
- **Method:** for each of the 28 playable venues (`V2BG_xxA.xml` in `assets/v2bg.viv`, list in the [cameras report](../research/widescreen-game-cameras.md) §1.2):
  a 1v1 and a 4-way FFA with fighters driven to the left and right extremes (scripted pad input), wide captures at 16:9, 21:9,
  32:9 at minimum and maximum zoom; plus five special-camera finishers and the intro/outro of three venues. Highest-risk first
  (by inference): 07A Inferno, 17A Parkade, 15A Crow's Office, 05A Dragon House.
- **Mitigations, in order of preference:** accept; per-venue `fov_cap` or maximum aspect for the 3D (pillarbox the 3D to,
  say, 16:9 in that venue); clamp the camera target X (the game never clamps it); never edit geometry.
- **Output:** `docs/research/widescreen-venue-sweep.md` with a table venue × aspect → verdict, and the mitigation table in code.
- **Tooling:** a harness stage that selects a venue by index (the Terrordome routes already do this for venue 6); the
  debug free camera (`[0x3BE0F4] & 0x40`, `sub_000AAE00`) for looking at set edges.

## WS-10 — Tests

- Unit tests for the maths in [02 §2](02-design.md#2-coordinate-maths-normative) (a C test in the toolkit's test folder for the
  canvas/scissor maths; a Python mirror for the placement formulas used by the game side, or a C unit test built with the
  game's test target).
- Harness: `--widescreen <value>` on `harness.py run` (sets `RECOMP_WS_ASPECT` and `RECOMP_TRANS_SHOT_WIDE=1`); new routes are
  not needed — existing routes run at other aspects.
- `regress.py` check `widescreen` (full run only): fight at 16:9 and intro at 16:9 — no crash, presents within 5% of the 4:3
  run, `[WS]` lines present, wide capture frame signatures (box, counts, hash; no pixels committed) against reviewed baselines
  in `<data>/test-baselines/widescreen/`.
- `replay` with widescreen on, compared with off (see the assumption in [03 §6](03-element-catalog.md#6-not-affected)).
- Everything else in `regress.py` keeps running with widescreen off.

## WS-11 — Renderer debug classifier and fallback (toolkit fork)

- `RECOMP_WS_DEBUG=1`: tint each draw by the placement the game gave it (or "untagged"), outline the 4:3 box; log every
  vertex-program hash that draws 2D-looking geometry (w == 1 on three vertices) without a game tag, once.
- Fallback (off by default): a per-draw `(kx, bx)` NDC remap from the [renderer report](../research/widescreen-renderer.md) §4.2
  (`frame[6..7]`, `nv2a_vsh_cpu.c:393`, `d3d8_shaders.c:155-156`, `d3d8_vk.c:523-540`) driven by a table of vertex-program hashes,
  for any 2D source that cannot be patched in game code.

## WS-12 — Optional AI side-art pack builder (to-do: seam blending first)

See [06](06-cutscenes-movies-and-ai.md#4-generative-ai). Local, user-run, never distributed, writes an `art` pack for WS-8.
The owner asked (2026-10-11) for one FMV to be tried both ways (blurred side fill and AI extension) before deciding;
the trial and its result are in [06 §5](06-cutscenes-movies-and-ai.md#5-fmv-trial-blur-vs-ai). Verdict: promising, but the seam
is visible; resume with the refinement plan in [08 §6](08-fmv-ai-pipeline.md#6-to-do-refinement-plan-in-order) using
`scripts/fmv-widescreen.py`. Not needed for the beta.

## WS-13 — Live switching and 32:9 polish (later)

Resize → canvas recreated at a frame boundary, `sub_0001A710` re-run (set the viewport's dirty bit 1 to recompute the
projection), HUD recomputed; Unleashed's lesson: repeated switching caused crashes, so soak it (100 switches).

## Effort guide (agent sessions, rough)

| WS | Size | Risk |
|---|---|---|
| 0 | 1 | L |
| 1a | 2 | M |
| 1b | 2 | M |
| 2 | 1 | L |
| 3 | 1–2 | M |
| 4 | 3–4 | M–H |
| 5 | 2–3 | M |
| 6 | 1 | M |
| 7 | 1–2 | M |
| 8 | 2 | L–M |
| 9 | 2–3 (mostly runs and looking) | M |
| 10 | spread, ~2 | L |
| 11 | 1–2 | L |

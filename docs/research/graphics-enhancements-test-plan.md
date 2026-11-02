# Graphics enhancements: test plan for Windows and Linux / Steam Deck

For branch `feat/graphics-enhancements` (toolkit `defjam/graphics-enhancements`). What was built and why:
[graphics-enhancements.md](graphics-enhancements.md). macOS (Vulkan through MoltenVK) was tested on the M1 Pro;
this plan originally covered the two remaining platforms: **Windows (Direct3D 11)** and
**Linux / Steam Deck (native Vulkan)**. The [Windows validation report](graphics-enhancements-windows-2026-11-01.md)
now records native MSVC builds, runtime tests and a D3D11 texture compatibility fix.

Every new setting defaults to the old picture. Run part A first: if anything in it changes with the
defaults, stop and report, because nothing new should be active yet.

Allow about 90 minutes per platform: 30 for the build and part A, 60 for parts B-D.

## 0. Get the build

Both branches are published on GitHub. The game is PR #151 and the toolkit is fork PR #4.

```
git fetch origin
git switch feat/graphics-enhancements
git submodule update --init --recursive
```

The branch adds a new entry hook (`sub_000D8C40_enter`) and a wrapper (`sub_000D8AD0`), so the game must be
**re-lifted**, not only rebuilt:

- Windows: `.\scripts\recomp.ps1` then `.\scripts\build.ps1 -Preset win-x64-release`, matching the
  regression runner's Release default. The build script stops only the executable owned by that checkout
  and preset; do not terminate another checkout's running game.
- Linux / Deck: `djpy scripts/pipeline.py recomp` then `djpy scripts/pipeline.py build`.

Pass: the build finishes with no new errors or warnings in `d3d8_present.c`, `d3d8_device.c`, `d3d8_states.c`,
`d3d8_readback.c`, `d3d8_vk.c`, `nv2a_pgraph_d3d11.c`, `pc_settings.c`, `picture.c`.

## A. Nothing changes with the defaults (automated, ~30 min)

```
python scripts/regress.py --quick
python scripts/regress.py --only fight,ffa,visual,launcher,overlay,unlockopt,picture
```

Pass: every row passes, including the three golden frames and `visual`. On the Deck run them under headless
gamescope as `docs/build-macos-linux.md` describes.

On Windows, the current `launcher`, `overlay`, `unlockopt` and `picture` rows explicitly skip because
they exercise the SDL/Vulkan host. Check the Windows app and native D3D11 overlay separately; a skip
is not a pass. Approved visual comparisons also require the matching saved-profile progression.

Every run's log (`logs/run-*.log.err`) now has one line saying which options are on; with the defaults it reads
`[DISPLAY] filter smooth, texture levels off, anisotropy 1x, MSAA 1x asked`. Linux also logs
`[D3D8-VK] anisotropic filtering: up to 16x` (the GPU's limit, whatever the setting).

Also check the first `settings.ini` the build writes (rename yours first, or point `RECOMP_SETTINGS` at a new
path): `[display]` has `msaa = off`, `texture_mips = false`, `anisotropy = off`, `color_tint = 100`,
`scratches = true`, each with its comment, and `filter` lists `smooth (high quality)`.

## B. Each feature

Use one seeded fight so pictures can be compared pixel for pixel (crowds aside):

```powershell
python scripts/scenario_suite.py run fight --step-input default --rng-seed 20261006 --no-unlock `
  --fixture <your profiles fixture> --step-shots 300,900,1500 --settings <file>.json
```

PowerShell uses a backtick for line continuation; cmd uses `^`, and Linux shells use `\`.
Replace the angle-bracket placeholders with local paths before running. Captures land in
`logs/scenarios/fight-<time>/captures/fight-step*.png`. Each settings file is a small JSON object, e.g.
`{"render_scale": 1, "texture_mips": true}`.

### B1. Game texture levels (`texture_mips`)

| Run | Settings |
|---|---|
| base | `{"render_scale": 1}` |
| mips | `{"render_scale": 1, "texture_mips": true}` |

Look at the floor and the back rows of the crowd at step 900 and 1500.

- Pass: the far floor and crowd are smoother and shimmer less in motion (watch a live fight too: overlay,
  Display, "Game texture levels" on and off). Nearby textures look the same.
- Fail: any texture with wrong colours, blocks of noise or another texture's content at a distance (a mip
  chain read from the wrong place); textures turning blank; a crash in `pgraph_d3d11_set_texture_ready_mips`
  or `ready_tex_fill_level`.
- Also: with HD textures on (`[textures] enabled = true`), texture levels on and off: packs must look as
  before.

### B2. Anisotropic filtering (`anisotropy`)

| Run | Settings |
|---|---|
| af | `{"render_scale": 1, "texture_mips": true, "anisotropy": 16}` |

- Pass: compared with "mips", the floor towards the back of the arena is sharper; nothing else changes. Live:
  overlay, "Anisotropic filtering" off / 2x / 16x changes the floor at once.
- Fail: sprites or cut-outs (crowd cards, the HUD, fonts) gaining fringes or coloured rims (the setting must
  leave nearest-filtered textures alone).

### B3. MSAA (`msaa`, restart)

| Run | Settings |
|---|---|
| base2 | `{"render_scale": 2}` |
| msaa | `{"render_scale": 2, "msaa": 4}` |

- The log names the sample count once: Windows `D3D8: MSAA 4x asked, 4x used`; Linux `MSAA: 4x asked, 4x used`.
- Pass: the edges of fighters, ropes, speakers and the HUD frame are smoother at step 300/900/1500; the picture
  is otherwise the same (same brightness: the gamma ramp still applies).
- Then by hand, `msaa = 4x` in `settings.ini`:
  - menus, the intro movie, a Story cutscene and a fight all draw;
  - brightness 150 in the overlay changes the picture (the gamma pass reads the resolved target);
  - widescreen (auto) with a movie or a menu: the blurred side fill beside the 4:3 picture still appears
    (Windows: it generates mips from the resolved texture);
  - F9 bug bundle / a harness capture is not black and shows the fight (readbacks resolve first);
  - F1 overlay draws normally;
  - 2x and 8x start too (8x may be lowered; the log says to what).
- Performance: one FFA at The Pit at render scale 2, MSAA off, then 4x. Note the fps lines (`[D3D] ... fps` or
  `[VKPERF]`). On the Deck also try 2x at scale 2.
- Fail: black or frozen picture, a device-lost / validation error, `CreateTexture2D` or
  `vkCreateRenderPass`/`vkCreateGraphicsPipelines` failures in the log, captures that are black.

### B4. Smooth (high quality) scaling (`filter`)

Only visible on screen: the captures are taken before scaling. Set render scale **4** in a 1080p window (or full
screen on the Deck's 800-line panel), stand in a fight, and switch Display, "Scaling filter" between
`smooth` and `smooth (high quality)` in the overlay.

- Pass: high quality is calmer: fewer crawling edges on the HUD frame, ropes and crowd, no blur beyond what
  smooth has. With render scale 1 or 2 in that window the two look the same.
- Also: resize the window, switch full screen (F11), and use 4:3 and widescreen: no stretched, offset or black
  picture.
- For a screenshot use the OS's own (Win+Shift+S, the Deck's screenshot button); the harness captures are taken
  before scaling and show no difference.
- macOS check that ran: a 2560x1920 target (scale 4, 4:3) in a 1280x800 drawable took one exact halving, then a
  bilinear step to 1067x800. On Windows the same case uses the shader path (3x3 taps at that ratio).

### B5. Colour tint (`color_tint`)

| Run | Settings |
|---|---|
| tint0 | `{"color_tint": 0}` |

- Pass: the fight's warm, sepia grade is gone (denim reads blue, whites read white); the vignette is unchanged.
  Live: overlay slider 100 -> 50 -> 0 moves the picture smoothly. A Story cutscene and a venue transition look
  sensible at 0 (they use other tints; note any that turn odd).

### B6. Film scratches (`scratches`)

The harness's default One on One draws them (macOS); not every venue does. Run
`python scripts/harness.py run fight --env RECOMP_SCRATCHES=0 --env RECOMP_POSTFX_TRACE=1` and send the
`[POSTFX]` and `[PICTURE]` lines. Pass: `[PICTURE] film scratches removed` appears and no scratches show in a live
fight with the option off; with it on they show as before.

## C. Launcher and overlay

- Launcher, Display: "Anti-aliasing (MSAA)", "Game texture levels (mipmaps)", "Anisotropic filtering",
  "Colour tint (%)", "Film scratches" are present; MSAA is marked as applying on Play/restart.
- Overlay (F1), Display: the same; changing texture levels, anisotropy, filter, tint and scratches applies at
  once; MSAA shows as pending until restart.
- Values survive a restart (`settings.ini`).

## D. What to send back

For each platform: the GPU and driver, the `regress.py` tables from part A, the B1-B5 capture folders (or the
three step PNGs of each run), the fps lines from the B3 performance runs, any `[POSTFX]` lines from B6, and a
yes/no for every Pass line above. Any crash: `python scripts/triage.py` on that run's log, and the bug bundle if
the recorder was on.

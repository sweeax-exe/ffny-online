# Graphics enhancements: Windows validation, 2026-11-01

Validation of game PR #151 (`feat/graphics-enhancements`, `95afe690`) and its published toolkit
pin `d4f5890c` (fork PR #4). The Windows compatibility fix advances that pin to
`ebf6f12828efe67409180381aed41d99e23d44c5`. PR #51 is an earlier release PR; the graphics PR is #151.

## Environment and isolation

- Windows build 26200; Core i7-13700K; NVIDIA GeForce RTX 4070 Ti SUPER,
  driver `32.0.16.1088`. The connected display reports 144 Hz.
- VS 2019 Build Tools, MSVC 14.29.30133, CMake 3.20, Ninja 1.10.2; `win-x64-release`.
- Separate checkout `C:/Users/Vlad/code/defjam-graphics-win`, build, logs, settings and save root.
  The extracted dump is referenced as input. Game runs are serial and use guarded/disposable saves.
  Captures, PCM, lifted sources, executables and detailed logs remain local and untracked.
- Fresh analysis and re-lift, including the new picture hooks; the game and the standalone
  `win-x64-app` app both link with MSVC. The presentation shader also compiles with Windows'
  `D3DCompile` (`vs_4_0` and `ps_4_0`).

## Default settings and app

- `regress.py --quick`: **5/5**, `logs/regress-20261101-211147.txt`.
  Loading, normal legal/EA/THX/intro/Press Start startup and main-menu goldens match.
  Standard fight: 120 seconds, median 120 presents/2 s, minimum 113.
- Full unit suite with the MSVC environment loaded: **687 passed, 30 skipped**
  (`logs/graphics-units-msvc-final.log`); includes the slow branding check.
- Fresh `settings.ini` has `msaa = off`, `texture_mips = false`, `anisotropy = off`,
  `color_tint = 100`, `scratches = true`, `filter = smooth`, and the associated comments.
- Toolkit fixtures `nv2a_ffp_backend`, `nv2a_host_clip`, `nv2a_raster_batch`,
  `nv2a_small_capture`, `nv2a_ws_left_tag`, `d3d8_present_fit`: **6/6** with MSVC.
- Standalone Windows app: Home shows Anti-aliasing and Game texture levels. Advanced Display
  shows the MSAA restart label, mipmaps, anisotropy, colour tint and film scratches. A scripted
  mipmap change saves; restart retains it and the other requested values (4x MSAA, 16x AF,
  HQ filter, tint 75, scratches off). Captures and saved INI: `logs/graphics-app-ui/`.
  These are app checks, not substitutes for the native game's launcher/overlay checks.

## Seeded comparisons

Local runner: `logs/graphics-matrix.py`; reports, captures and PCM in `logs/graphics-matrix/<name>/`.
Each fight uses seed 20261006, step input, original unlock state, captures at steps 300/900/1500
and 75 seconds of combat. The suite still requires at least 60 seconds of presentation evidence;
the separate standard regression above covers the full 120-second route. The crowd is not seeded,
so whole-frame differences are not a pixel-perfect renderer comparison.

| Case | Settings | Status |
|---|---|---|
| base1 | scale 1 | Passed selected runtime, save, audio, pacing and memory gates; median 60 fps |
| mips1 | scale 1, guest mipmaps | Passed selected gates; median 60 fps; post texture has less moire, no blank/corrupt textures in inspected captures |
| af16 | scale 1, mipmaps, 16x AF | Passed selected gates; median 60 fps; no new HUD/cut-out fringes in inspected captures |
| base2 | scale 2 | Passed selected gates; median 60 fps |
| msaa4 | scale 2, 4x MSAA | Passed selected gates; 4x used; median 60 fps; intact captures and comparable brightness |
| tint0 | scale 2, tint 0 | Passed selected gates; median 60 fps; neutral, brighter picture with vignette retained |
| msaa2 | scale 2, 2x MSAA | Passed selected gates; median 60 fps |
| msaa8 | scale 2, 8x MSAA | Passed selected gates; 8x used; median 60 fps |
| all4 | scale 4, 4x MSAA, mipmaps, 16x AF, tint 50, scratches off, HQ downscale | Passed selected gates; median 60 fps; intact inspected captures |

**9/9 configurations pass all selected gates.** Brief two-second minima occur in the baseline
too (scale 2: 102 presents; MSAA 2x/4x: 104; MSAA 8x: 103; combined: 104). This is a median
60 fps result, not a claim that every individual frame met its deadline.

## Native presentation pixels

A local, game-free fixture compiles the production `d3d8_present.c` and `d3d8_gamma.c` against
the real Windows SDK and GPU, with a hidden test window, swap chain and staging readbacks.
Source/executable/output: `logs/present-test.c`, `logs/present-test.exe`, `logs/present-pixels.log`.
It checks the post-presentation pixels that ordinary game captures cannot inspect:

- A 3:1 striped downscale: ordinary smooth samples 255; HQ returns the expected area mean **85**.
- 4x MSAA resolve, nonidentity gamma and widescreen side fill: centre **128**, side **65**;
  gamma-end restores the original target to **64**.
- Resize to 80x48: the picture remains correctly centred and nonblack.
- The D3D11 SDK debug layer is available and records **zero validation errors**.

## Additional runtime coverage

- Four-player default fight: 152 seconds of combat, median 120 presents/2 s, minimum 98.
  Matching scale-2 run with 4x MSAA: median 120, minimum 105; no detected runtime faults.
- Normal legal/EA/THX/intro startup with scale 4, 4x MSAA, HQ downscale, 16:9 and brightness 150
  reaches the main menu with intact inspected captures. FMVs run at their own lower rate.
- Story intro with those presentation settings, tint 0 and scratches off reaches the character
  creator. Inspected cutscene captures show the intended neutral picture, without blank frames.
- Native D3D11 overlay opens, reports ready and closes with 4x MSAA; no detected runtime faults.
- HD pack fights with 4x MSAA and guest mipmaps both off and on pass the selected runtime,
  save, audio, pacing and memory gates. Logs confirm replacement textures and their mip chains
  are actually uploaded (20,946-entry combined index; 512 MiB cache).
- All harness/scenario runs restore their disposable or guarded save roots byte-for-byte.

## Windows issue found and fixed

The gym produces roughly 161,000 repeated `CreateTexture2D failed: 0x80070057 (fmt=15 256x2)`
messages on the original pin. D3D11 rejects a BC3/DXT5 base level with height 2. This is an
existing resource compatibility bug, predating the graphics changes, exposed by the wider tests.

The toolkit fix stores unaligned 2D DXT1/3/5 base sizes in BGRA at their exact guest dimensions
and decodes uploads with the existing DXT decoder. Guest compressed lock bytes, mip levels,
UV dimensions and alpha remain intact. Block-aligned base textures keep native BC storage.
CPU-backed surface locks retain their parent storage and support valid compressed subrectangles.

New fixture `tests/d3d8_thin_dxt` uses WARP and synthetic blocks, with actual GPU readback:
18 format/size combinations, all mip levels, exact colour/alpha, texture/surface locks, changed
subrectangle writes, bounds validation and parent-handle lifetime. The original implementation
fails the first allocation; the fixed implementation passes all cases and CTest (1/1), including
the exact Release configuration used by CI (`logs/thin-dxt-release.log`).

The game was regenerated and rebuilt with the fixed runtime. The affected Story/crib/gym route
passes its six selected checks and has **zero failed texture allocations**; all three captures
are retained in `logs/graphics-fixed-gym/`. The gym movie preview was visually inspected.
The default regression passes again: **5/5**, `logs/regress-20261101-221447.txt`; standard
120-second fight median 120 presents/2 s, minimum 112. The new fixture is registered in the
parent repository's Windows Release CI matrix.
The combined scale-4/4x-MSAA/mipmaps/16x-AF/tint-50/scratches-off/HQ fight also passes again
on the fixed runtime: 76 seconds of presentation, median 120 presents/2 s, minimum 107;
all selected runtime, save, audio, pacing and memory checks pass (`logs/graphics-fixed-all4/`).

## Validation limits

The initial approved visual check reports **2/4 comparisons passed**. Its gym comparisons also
have a profile mismatch: the October 6 reference has 20/82 moves and 465 points; the current
local profile has 15/82 and 85. The move preview is visible.

The original `vy2-hour-v1` fixture was subsequently found in an older local checkout and copied
for an exact-profile retest (`logs/graphics-fixed-approved-visual/`). All six runtime, route,
preview-request and save checks pass, but only **1/4** old visual comparisons passes:

- The gym now renders grey row backgrounds from the formerly rejected thin DXT texture;
  the old goldens omit them. A before/after pair using byte-identical current profiles also
  demonstrates this change (`regress-visual-20261101-214055` versus `graphics-fixed-gym`).
  The historical-fixture gym differences are 5.51%/6.05%, confined to these row backgrounds
  in the inspected difference images. The loose unmasked preview comparison passes.
- The crib difference is 1.05% (MAE 1.14), just over its limits. Its inspected difference image
  shows the music ticker extending past the reference mask.

These references need review against the corrected rendering; visual acceptance is **not**
declared. Baselines, masks and thresholds were not changed, and captures remain local.

The existing `launcher,overlay,unlockopt,picture` regression rows explicitly skip Windows:
they target the SDL/Vulkan game host. Those skips are not passes; the Windows app checks above
and separate native game checks cover the applicable paths. Linux/Steam Deck testing remains separate.

## Instructions corrected

The test plan used cmd's `^` as PowerShell continuation and built the default Debug preset while
the regression commands expect Release. It now uses PowerShell syntax, explicitly builds
`win-x64-release`, and specifies termination of only the checkout's owned executable.

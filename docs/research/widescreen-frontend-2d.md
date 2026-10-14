# Widescreen: every 2D element, and what the front end's dormant widescreen mode really does

> **Errata (2026-10-11):** in sections 6 and 7 the aspect modes of `sub_0001A4E0` are swapped: mode 1 is the 16:9 letterbox and mode 2 is width/height (checked on the original lifted body). See `docs/widescreen/01-findings.md` section 5.

Read-only static pass. Nothing was built, run or edited. Sources: the user's own dump
(`<data>/extracted/screens/screens.viv`, `assets/misc.viv`, `movies/*`, `default.xbe`), the repo's
`scripts/apt-dump.py` (run over all 180 APT movies outside the pure-art directories), and grep of the
lifted C (`src/recomp/gen/recomp_00NN.c`). No game bytes are reproduced; excerpts are short.

Labels: **VERIFIED** = read directly in the bytecode, XBE or lifted C. **INFERRED** = a conclusion drawn from
verified facts. **UNRESOLVED** = not determined; the cheapest way to settle it is given.

## 0. Bottom line

1. **The front end's widescreen mode is a dead flag.** `gWidescreenMode` ("1" turns it on) only sets a
   boolean `WIDESCREEN` inside the two EA-Sports-framework root movies `main` and `mainBE`, and copies it to
   `_global.WIDESCREEN`. **No movie in the whole archive reads it back** (VERIFIED by a full-archive scan:
   the token occurs in exactly two constant pools). It changes no stage scale, no `_xscale`, no `_x`.
   `feloader`/`loader` merely `trace()` the value. Passing "1" instead of "off" would change nothing visible.
2. The design the leftovers hint at is a **wide stage, not an anamorphic squeeze**: `mainBE` is an
   **854x480** stage that is shifted by `(854 - SCREEN_WIDTH)/2` (107 px for 640). There is no 75 % scale
   anywhere. But those pieces belong to a shared EA Sports "cafe" library (`cafeLib/*.swf`, NHL screen names)
   that is **not in the archive**, and `main`/`mainBE` are not in `feflow.xml`. The Def Jam screens (all 640x480) were never
   authored for a wide stage.
3. **All Def Jam APT screens are 640x480.** So are the loose movies. The treatment for the front end
   is **pillarbox the 4:3 stage**, centred, with extension only for a few full-bleed layers (section 5).
4. **The in-fight HUD, pause menu, popups, first-time help, fades/tint/vignette/scratches quads and the
   loading spinner are native sprite code, not APT.** They share one 2D-quad core that converts
   *virtual 640x480 pixels* to clip space with hard-coded `1/640` and `1/480`. That core is the single
   best place to introduce a widescreen anchor/pillarbox (section 4).
5. The engine already contains a **16:9 letterbox rect function** (`sub_00018CA0`, gives the 60-px bars) and
   a **3D view aspect mode switch** (`sub_0001A4E0`: 0 = 4:3, 1 = width/height, 2 = 16:9 letterbox).
   The only constructor call passes mode 0 (4:3).

## 1. What the ActionScript "widescreen mode" does

### 1.1 C side (VERIFIED)

`sub_0006B760` (`src/recomp/gen/recomp_0003.c` ~40170-40310) calls `sub_00068850(name, value)` for the
Flash globals. Its last call pushes `0x29A9D4` ("off") and `0x29A9D8` ("gWidescreenMode"). That call is
the only reference to the string. `gPlatform` is "XBOX". Nothing reads an Xbox video setting: the XBE has
no `XGetVideoFlags`-style import use for it (string absent), so the value is a constant.

### 1.2 Scan result (VERIFIED)

A case-insensitive scan of all 8,794 entries of `screens.viv` (nested BIG/RefPack walked) for "idescreen"
finds four constant pools: `main`, `mainBE`, `feloader`, `loader`. `apt-dump.py --actions` over all 180
APT movies gives:

| token | movies that use it |
|---|---|
| `WIDESCREEN` | `main`, `mainBE` only |
| `gWidescreenMode` | `main`, `mainBE` (read), `feloader`, `loader` (trace only) |
| `SCREEN_WIDTH` | `main`, `mainBE`, `extras/extras` |
| `gScreenShift` | `main`, `mainBE` |
| `alignMovie` | `main` (defined, never called in the archive) |
| `scaleMode`, `Stage` | none |
| `_xscale` | ~30 gameplay screens, all as local UI animation (bars, sliders, `_yscale` in 4 files); none tied to WIDESCREEN |

### 1.3 `main.apt` (stage 640x480, 10 frames; bytecode offsets in `main.apt`)

* 0x4064-0x4070: `PLATFORM = "PS2"` default (local), `WIDESCREEN = false` (local), `PLATFORM =
  extern.gPlatform`, then `trace("extern.gWidescreenMode=" + gWidescreenMode)`.
* 0x408E-0x40A8: `if (extern.gWidescreenMode === "1") WIDESCREEN = true` (a `StrictEqual` against the
  *string* "1"; the C side must pass "1", not 1).
* 0x40C6-0x4100: if `System.capabilities.os` is non-empty ("running in pc flash"), `PLATFORM = "FLASH"`,
  `WIDESCREEN = true`, debug on. 0x4111-0x4120: `PLATFORM == "PS2_APTVIEWER"` also sets `WIDESCREEN = true`.
* 0x42F8-0x4301: `_global.WIDESCREEN = _root.WIDESCREEN` (exported for any movie to read).
* `unloadCafe` (0x3F83-) sets `_root`/`_global` `WIDESCREEN` to null. `gEH.WIDESCREEN`, as seen in the
  constant pool, is just the adjacent constants `gEH` and `WIDESCREEN` (indices 766, 767); there is no
  member access of that name.
* No instruction ever reads `WIDESCREEN` for a decision. (VERIFIED: the only occurrences of constant 767
  are the stores above.)

### 1.4 `mainBE.apt` (stage **854x480**, 15 frames)

* 0xFFB-0x1013: `if (extern.gWidescreenMode === "1") WIDESCREEN = true`.
* 0x1718: `if (PLATFORM == "PSP") { WIDESCREEN = true; SCREEN_WIDTH = 480; SCREEN_HEIGHT = 272 }`.
* 0x16B6-0x1709: `if (_global.SCREEN_WIDTH == null) { SCREEN_WIDTH = 854; SCREEN_HEIGHT = 480 }`;
  `gScreenShift = (854 - SCREEN_WIDTH) / 2` when `SCREEN_WIDTH != 854`, else 0.
* gScreenShift is *subtracted* from `_x` of `gScreenTitle`, `gMainMenu`, `gTicker`, `gHelpFooter` just after
  they are created with `cafeLib/stdTitle.swf`, `stdMenu.swf`, `stdTicker.swf`, `stdHelp.swf` (0x2386-
  0x23F3). In `main` the same pattern appears (0x2B5F-0x2E73) inside `loadEasn` with `+`/`-`.
* `main` also defines `alignMovie(m) { m._x = (SCREEN_WIDTH - 854) / 2 }` (0x3CFB).

### 1.5 Reading

* **Which properties change and by how much:** none, in this archive. The only effects are the global
  boolean and, in the cafe-library code paths, an `_x` shift of `(854 - SCREEN_WIDTH)/2`.
* **Anamorphic or wide stage?** Wide stage (INFERRED from the 854x480 stage and the centring maths): the
  EA Sports framework authors for 854x480 square-pixel 16:9 and, on a narrower stage, centre/crop by half the
  difference (so 4:3 loses ~107 px per side). No `_xscale = 75` or `_width` squeeze exists.
* **Value that turns it on:** the string "1".
* **Which screens honour it:** none. `cafeLib/*.swf` (the likeliest consumers, and the movies `menu/scrMenuMain`
  (854x480, 6 frames), `analogtest` (852x480)) are leftovers from the shared codebase: `scrMenuMain` lists
  NHL screens (`menu/seasonMenu`, `scrMenuMyNHL`, ...). The `cafeLib` directory does not exist in
  `screens.viv`, and `feflow.xml` (54 screens) names none of `main`, `mainBE`, `menu/*`, `cafeLib/*`.
* **Is `main` even used?** Native code names "main" once (`0x29A670`, `sub_0006BC90` switch case 2,
  `recomp_0003.c` ~41000) and passes it with `_level0` to the movie-load helper `sub_000687B0`, then in
  the common path loads the loader movie (`beloader` in game; else `demoload`/`loader`/`feloader`, strings
  `0x29A678-0x29A698`) into the same `_level0`. The string `mainBE` occurs nowhere in the XBE. So whether
  `main`'s script ever runs, and whether `mainBE` is ever loaded, is **UNRESOLVED**; the leftovers are
  harmless either way. Cheapest check: a log of the `sub_000687B0` file arguments in a normal boot.
* **Would setting "1" give a correct 16:9 menu layout?** No. It would flip a flag nobody reads. A real
  widescreen front end has to be done by the host (section 2).
* `extras/extras` computes the poster-wall scroll from `SCREEN_WIDTH / 4` (0xD284-0xD2C6) and no loader sets
  `SCREEN_WIDTH` in the Xbox build. Existing 4:3 behaviour (variable undefined) is unchanged by any plan
  here unless someone defines it; leave it alone or define it as 640.

## 2. The APT stage and where to pillarbox

**Stage sizes (VERIFIED, `apt-dump.py` header line):** 640x480 for the 78 gameplay-facing movies including
`feloader`, `loader`, `beloader`, `demoLoad`, `main`, `pageHead`, `xHead` (overlay header/footer), and every
`story/*`, `battle/*` screen except the asset libraries; 550x400 is the default size of ~82 image-library
and `match/1_venue`, `match/2_type` art movies (never displayed as a stage); `trans/t1` and `test/trans1`
are 640x2000 (the white transition slab); `battle/unlockven` is 600x480 (VERIFIED, odd one out);
`mainBE` and `menu/scrMenuMain` 854x480; `test/analogtest` 852x480. Frame time 16 ms.

**How the stage reaches the screen (UNRESOLVED, with the existing evidence):**
* The movie's width/height are in the header; the APT renderer has no 640/480 immediates (grep of
  `0x44200000`/`0x43F00000` hits only `recomp_0000/0001/0002/0003/0005/0006`, none in the APT module in
  `recomp_0010/0011`). The 2D matrix chain (`sub_00179890`, `sub_0017D4D0`; `m4a-apt-matrix.md`) starts at an
  identity root at `0x3B1E74`, so the stage-to-screen mapping is *not* applied by the matrix stack.
* Each sprite's 2x3 is uploaded as vertex constants `c0`/`c1` through `sub_00119910`; the title screen was
  seen with `c0 = (0.000244, 0, 0, 3)` (handoff log), i.e. the vertex program, not game code, applies the
  final stage scale. The host already executes the game's NV2A vertex programs
  (`m4e-gpu-vertex-programs.md`), so the stage-to-clip map is a constant in that program or its cbuffer.
* **Cheapest way to settle it:** capture one APT draw of a full-bleed 640-wide background
  (`RECOMP_PB_BATCH_DUMP`) and read `c0`/`c1` plus the vertex program; then the clip-space x scale to
  change is visible directly.

**Where a host could pillarbox the UI (RECOMMENDED, INFERRED feasibility):**
1. *Whole-frame (today):* `d3d8_present.c` already scales the entire 640x480xN game texture into the window
   and keeps 4:3 (`aspect=4:3`). That is a pillarbox of everything; no work needed for the front end alone.
2. *Mixed 2D/3D (needed for a wide 3D fight):* render the game texture at the wider aspect and, for any
   draw that is APT or native 2D, multiply the clip-space x by `r = (4/3)/aspect` (0.75 for 16:9, 0.571 for
   21:9, 0.833 for 16:10). Two seams: (a) the APT vertex program constants (host-side translator, per-draw
   cbuffer), (b) the native 2D core, section 4. Anchoring (left/right) = additionally add `±(1 - r)` to x.
3. Do not widen the APT stage itself: `trans/t1` (640x2000) and the `overlay`/`pageHead` clips are 640 wide
   and would leave side strips uncovered (see `m4a-fe-transitions.md`, `clip2` slab).

## 3. What is native and what is APT

### 3.1 In-fight HUD (VERIFIED native, no APT)

* No APT constant pool has health/momentum/KO/round text (scan for "health", "momentum", "blazin" hits only
  character-select stats and the hi-score table).
* The HUD loads texture atlases `assets/misc.viv|hud` (`hudF` French), `hudtutor` (`hudtutrF`) -
  `sub_000B5140`, `sub_000B76A0` (`recomp_0005.c` 35186/40727), and `Gamehud::Texture` (`sub_000B4C10`).
* Layout is **data inside `hud.xsh`** (SHPX, 453 KB decompressed): named locator entries per player
  `pos_pN_{healthBar, momentumBar, blazin, blazinFire01..04, danger, mainBarContainer, tapBarContainer,
  tapBarMeter, tapHealthMeter, rightStick}`, text slots `text_pN_{name, score, combo_name, totalscore,
  tapname, taphealth, danger}` (N = 1..4), plus `pos_over_{ko, dko, fight, ring, out, over, submit, time,
  totalled, double}` and `text_time`. The builder is `sub_000B57A0` ("PLAYERHUD", `pos_p%d_%s`,
  "Nametag"). The locator values were not decoded (the entries are 8x8 stub textures with packed data).
* HUD arithmetic is in 640-space: `sub_000B6850` (`recomp_0005.c` 38720-38830) computes `(640.0 - w) * 0.5`
  (constants `0x2AA498` = 640.0, `0x286D2C` = 0.5) to centre tutorial/help text; `sub_000B6B30` likewise.
  The fight tutorial text ("FirstTimeHelp::String", "GameHudTutorial:Tar") is the same system.
* `[HUD]: BLAZING Movie End!!!` (XBE string, no live reference) suggests a Blazin' overlay movie; not traced.

### 3.2 Pause, popups, first-time help (VERIFIED native)

* Pause: `assets/misc.viv|pause.xsh` (`sub_000CDDC0`), object names `PauseTar`, `PauseMenuRed`,
  `Pause:QN_{HELPBTN, PAUSEBG, QUITBTN, ARR, BOX, BAR, BARFILL, BARBG}` (`sub_000CD490`), and
  `PauseMenuGrey`, a dimmer quad created by `sub_000CCE60` with literal size **640.0 x 480.0** and alpha 0x7F.
  Centre constants 320.0 (`0x2AF808`) and 240.0 (`0x2A9C30`) are used throughout (`recomp_0006.c` 7639-11285).
* Popups (`popups.xsh`/`popupsF.xsh`, `sub_000D4980`): `PopupTar`, `Popup:SimpleQuad`, `PopupMenuGrey`,
  `texPaper/texTitle/texNote/texFooter`, Xbox A/B/X glyphs. First-time help: `sub_000D66E0`
  (`FirstTimeHelpTar`, `FTHMenuGrey`). The grey layers are full-screen 640x480 quads.
* The memory-card popups are different: they are APT (`BEopenPopup`/`closePopup` defined in `feloader`,
  `loader`, `beloader`; native code calls `_level0._root.BEopenPopup` etc.).

### 3.3 APT screens (VERIFIED: movies exist, all 640x480 unless noted)

* Front end: `intMain/{prestart, mainMenu, message, bootLoad, legal1, demoMenu, demoStrt}`, `options/*`
  (`screenPosition`, `screenBright` call native `Option.SetScreenPosition/GetScreenPosition/SetBrightness`,
  registered near `recomp_0002.c` 24453), `extras/*`, `jukebox/beatbox`, `hiScores`, `memoryCard/*`.
* Story mode: `story/crib` (crib), shops `clothing`, `jeweller`, `tattoo`, `barber`, `wardrobe`, `gym`,
  phone/messages `msgCentr`, create-a-player `charDec` and `Sketch`, `storyChs`, `storyID`, `tournament`,
  `trophies`, `choosPart`, `chooseOp`, `chooseStyle`, `choosGF2/4`, `skipSumm`.
* Battle flow: `battle/{cmtype, chooseF2/3/4, chsTeam4, chsVenue, rules (292x284), numPlyrs (235x229), tips,
  rematch, matchSm1..4 (match summary), rewards1, rewardun, unlockchar, unlockven (600x480), buyrewards,
  demoCF, matchLoa/oldMLoad (match loading, 120 frames)}`. The map screens `battle/map`, `venueMap` are
  550x400 image libraries drawn inside other screens.
* Loaders/overlays: `feloader`, `loader`, `beloader`, `demoLoad` (all 640x480) host the transitions; `xHead`
  and `pageHead` (46 frames each) are the persistent overlay header/footer; `trans/t1` is the 640x2000 slab.
* INFERRED: the post-fight summary/reward screens are the APT screens loaded while `g_inGame` is 1 via
  `beloader`; pause and the HUD stay native.

### 3.4 Loading screens

* Native: `LoadSnapScreen` and `Load:LogoQuad` (`sub_000D6A30`, `recomp_0006.c` 30142; also
  `sub_0002A410`/`sub_0002A590`), the spinner `gLoadingIconQuad0/1` drawn by `sub_0005AF80` (x = 412.0,
  y = 460.0 or 480.0 chosen from a config field, 444.0), and `movies/load0.xsh` (17 KB SHPX). UNRESOLVED:
  whether the snapshot is the full back buffer (probably) or a 640x480 copy.
* APT: `matchLoa`, `oldMLoad`, `bootLoad`, `demoLoad` (all 640x480).

## 4. The native 2D quad core (best single point of control)

* `CSimpleQuad`/`CQuad` (XBE strings `CQuad::Init.m_pPosition/m_pScale`). Init is `sub_00028950` (42 direct
  call sites, args like `0,0,0,640.0,480.0,0`), position setter `sub_000282D0` (27 sites).
* Pixel-to-clip conversion (VERIFIED by reading; role INFERRED): `sub_00028F90`, `sub_0002E390`, `sub_0002E470`
  fetch the renderer singleton (`sub_00012CF0(4)` on `0x2FB148`), whose display config at `+0x88` is
  `{640.0, 480.0, 32, 32, 32, 1}` (set in `sub_00011E40`, `recomp_0000.c` 5297-5315). They compute
  `x_ndc = (x*W/640 - W/2)/(W/2) = 2x/640 - 1` and `y_ndc = 1 - 2y/480`, and sizes `2w/640`, `2h/480`, using
  hard-coded floats `0x28B924` (1/640) and `0x28B920` (1/480). `W,H` cancel, so changing `+0x88` alone does
  nothing; the virtual resolution is baked into those two floats.
* **Recommended hook:** after these three functions store their results, multiply the x components by `r`
  (position: about screen centre; size: by `r`). Full-screen layers (quads built with width 640.0 at x = 0:
  `PauseMenuGrey`, `PopupMenuGrey`, `FTHMenuGrey`, `TINT Quad`, `Vignette Quad`, `scratches Quad`, `NISQuad`)
  should be exempt from the `r` scaling so they cover the whole wide frame. HUD elements get an anchor
  offset `±(1 - r)` by player slot (left players left, right players right) via the `pos_pN_*` builder.
* Risk: the three functions have few direct call sites but are probably reached via the 42/27-site wrappers
  only; quads that bypass them (immediate-mode text, `initDraw gpImmedGeoPrimState` at `sub_000B7950`) need
  checking. Text is drawn by the EA font system (`fonts/befonts.viv`, XFN) and is not covered here.

## 5. Full-screen overlays and the letterbox

| Overlay | Evidence | Must stretch full width? |
|---|---|---|
| Pause / popup / first-time-help dimmers | native quads, literal 640x480, alpha 0x7F | **Yes** |
| Screen tint, vignette, scratches | `sub_000D8930` ("scratches Quad", 3 sites at 640x480), `sub_000D99C0` ("TINT Quad", "Vignette Quad"); tunables `ScreenTintEnable`, `VignetteAlpha`, `TintFillColorR..`, `ScratchesEnable` in the XBE | **Yes** (a vignette must follow the wide frame; scratches may be cropped) |
| Blazin' screen filter | probably the tint/vignette pair (INFERRED; the Blazin' state sets none by name that I found) | Yes |
| Fade to black/white between APT screens | APT transitions: `trans/t1` slab (640x2000, `clip2`), `doorZoomIn`/`panUp`/`panDown` in the loaders; `BackgroundColor` black | Needs a wider slab or confinement to the 4:3 pillar |
| Camera fades in cutscenes/outros | XML anim names `fadein`/`fadeout` (EXTRO subsystem) - 3D side | follows 3D |
| Cutscene letterbox | `sub_00018CA0(this, &x, &y, &w, &h)`: with its flag set returns `(0, (H - 0.5625*W)/2, W, 0.5625*W)`, i.e. `(0, 60, 640, 360)`; used as the quad rect by two callers (`sub_00073FD0`, the NIS player; `sub_0008B730`). Constants 0.5625 at `0x286D34`, 0.5 at `0x286D2C`. Matches the in-engine 640x360 measured by the main agent. | Bars are horizontal: they must span the *wide* width; and in 16:9+ the 16:9 rect fills the screen (no bars at 16:9; pillar sides at 21:9) |
| Loading snapshot / logo | `LoadSnapScreen`, `Load:LogoQuad` | Snapshot: full frame; logo: anchor centre |
| Loading spinner | `gLoadingIconQuad` at about (412..460, 444..480) | Anchor bottom-right |
| 3D view aspect | `sub_0001A4E0`: mode 0 writes 4:3 (`0x3FAAAAAB`), mode 1 writes `W/H` of the view, mode 2 writes 16:9 (`0x3FE38E39`) with `y = top + H/8`, `h = 0.75*H`. The only constructor call (`sub_0001A610` from `sub_000195C0`, `recomp_0000.c` 26275) passes mode 0. | 3D, out of scope; mode 1 is the natural widescreen entry. Who changes the mode after construction: UNRESOLVED |

## 6. Movies

Facts supplied by the main agent (measured, not redone): loose `.mad` are 640x480 (intro 82 s, attract
49 s, soulplan 30 s, thx 5 s, eagames 3 s, SoulTrai/extra 2.5 s); `movies.viv` holds 87 clips at 240x176
(in-menu previews); in-engine story cutscenes are letterboxed to exactly 640x360.

My additions: VERIFIED header check of `thx.mad` and `eagames.mad`: magic "MADk", little-endian width
0x0280 = 640 and height 0x01E0 = 480 at +0x14/+0x16. **Frame decoding was not possible** (no ffmpeg, OpenCV
or PyAV available and no installs allowed), so whether any 640x480 movie has black bars baked in is
UNRESOLVED; a visual check of a few decoded frames would settle it. Playback path: boot sequence
`sub_000D7060` selects `movies/eagames`, `movies/thx`, `movies/intro` (`0x2B3852-0x2B3870`); `sub_0001E460`
opens the file (`assets/movies/%s`, `%s.mad` near `0x297228`, `sub_0005ADA0`); `sub_00077020`/`sub_000773A0`
start it; the decoder objects are the `0x0013A...0x0013C...` family (`movie-audio-clock.md`). The draw call
that puts the decoded frame on screen was not traced (UNRESOLVED). Because the host presents the whole
640x480xN texture, the movies are pillarboxed with everything else today; with a wide game texture they would
need the same treatment as the 2D core (stay 4:3 centred) or a dedicated stretch/blur-fill path.

## 7. Element table

| Element | How drawn | Source | Recommended treatment | Risk |
|---|---|---|---|---|
| Front-end APT screens (menus, story hub, shops, CAP, results) | APT movie 640x480 via vertex-program `c0/c1` | `screens.viv`, APT renderer `sub_0017A4A0`/`sub_00165A00` | **Pillarbox** centred 4:3 (scale clip-x by `r`) | Low once the stage-to-clip constant is found |
| Full-bleed menu backgrounds (`_art/backgrounds`, `match/0_bg/paper_*`) | APT bitmap layers 640x480 | same | Pillarbox now; optional extension of the background layer later | Low (extension = art/AI work) |
| `xHead`/`pageHead` overlay (header/footer, prompts) | APT, 46-frame clip at (0,0) | `screens.viv` | Pillarbox with the screen, or anchor left/right as an upgrade | Low |
| Transitions (`trans/t1` 640x2000, panUp/Down, doorZoom) | APT slab in `clip2` | `feloader`/`loader` | Keep inside the pillar (no widening) | Medium: slab covers only 640 px |
| Loader movies (`feloader` etc.) | APT, black `BackgroundColor` | `screens.viv` | Pillarbox | Low |
| `main`/`mainBE`, `WIDESCREEN` flag | EA Sports leftovers | n/a | **Ignore**; do not set "1" | None |
| Memory-card popups | APT (`BEopenPopup`) | loaders | Pillarbox | Low |
| In-fight HUD (bars, names, combo, Blazin', tap meters, score) | native CSimpleQuad sprites from `hud.xsh` locators `pos_pN_*` | `sub_000B5140`, `sub_000B57A0`, `sub_00028950` family | **Anchor** by player slot to the wide frame (left P1/P3, right P2/P4); keep each bar intact | Medium-high: locator values undecoded; 4-player layout |
| Fight KO/FIGHT/round text | `pos_over_*` + overlay quads | `hud.xsh` | **Centre**, native scale | Low |
| Tutorial/first-time-help text | native, centred with `(640 - w)*0.5` | `sub_000B6850`, `sub_000D66E0` | Centre; dimmer quad stretches | Low |
| Pause menu | native `Pause:QN_*` quads from `pause.xsh` | `sub_000CD490` | Centre 4:3; `PauseMenuGrey` **stretch** | Low |
| Popups | native, `popups.xsh` | `sub_000D4980` | Centre; `PopupMenuGrey` stretch | Low |
| Tint/vignette/scratches | native 640x480 quads | `sub_000D8930`, `sub_000D99C0` | **Stretch** (do not scale by `r`) | Low-medium |
| Cutscene letterbox | `sub_00018CA0` rect (0,60,640,360) | NIS player | Full-width bars; at 16:9 no bars | Medium: bar scissor must grow with the wide surface |
| Loading snapshot / logo / spinner | native quads | `sub_000D6A30`, `sub_0005AF80` | Snapshot full frame; logo centre; spinner anchor bottom-right | Low |
| `.mad` full-screen movies | decoder then quad (path not traced) | `sub_000D7060`..`sub_00077020` | **Pillarbox** 4:3 (optionally blur-fill later) | Low |
| 3D camera | `sub_0001A4E0` mode 0 | `sub_000195C0` | Mode 1 + wider viewport (3D plan) | High: culling, LOD, shadow cascades |

## 8. Recommended order of attack (INFERRED)

1. Keep pillarbox for everything 2D at first: the host texture/viewport only needs the wide aspect for 3D
   (nothing in section 1 needs an ActionScript change).
2. Capture one APT background draw and find the clip-x constant, then apply `r` per draw in the host's
   vertex-program path.
3. Patch the three native conversion functions (section 4) with `r`, with the full-screen exemption by quad
   size, then add the HUD anchor pass.
4. Cutscene letterbox and view aspect last.

## 9. Open items and how to close them

* Stage-to-clip constant of the APT vertex program: `RECOMP_PB_BATCH_DUMP` on a full-bleed APT draw.
* Whether `main` is loaded at all, and whether `mainBE` is ever loaded: log `sub_000687B0` arguments.
* Setter of the letterbox flag / view mode (`sub_00018CA0` `this+0xA4`, view `+0x10`): a watchpoint.
* `hud.xsh` locator values: decode SHPX `pos_*` entries (stub 8x8 textures with packed data) or capture
  positions at `sub_000B57A0`.
* Black bars baked into the 640x480 `.mad`: decode two or three frames with a decoder on the host side.
* How `Option.SetScreenPosition` is applied (user overscan offset): handlers registered at
  `recomp_0002.c` 24453-24459 (`0x48F40`/`0x48F10` as registered, semantics not verified).

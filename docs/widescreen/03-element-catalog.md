# 03 — Element catalog

Every element the player sees, how it is drawn, the widescreen treatment (classes from
[02 §2](02-design.md#2-coordinate-maths-normative)), the placement policy and original risk assessment. "Verified" sources
are in the research reports and [01](01-findings.md). Risk: L/M/H.

Classes: **HOR+** (3D, widened), **SUB** (3D sub-view: rectangle centred, view unchanged), **CUT** (cutscene policy),
**CEN** (2D centred), **L/R** (2D anchored left/right), **FILL** (stretch to full width), **PIC** (full-screen 4:3 picture:
centred + side fill).

## 1. Boot and attract

| Element | How drawn | Class | Placement / original scope | Risk |
|---|---|---|---|---|
| EA and THX logo movies (`eagames.mad`, `thx.mad`, 640x480) | FMV: title decodes into a YUV texture, draws a full-screen quad | PIC | Logo centred at 4:3, blurred ambient sides (or black) | L |
| Legal and LOADING screen (crest + wordmark) | native 2D quads | CEN | Centred, black sides (it is on black) | L |
| Opening movie (`intro.mad`, 82 s) and attract loop (`attract.mad`, 49 s) | FMV | PIC | Centred 4:3 with blurred sides; optional AI side art | L |
| Retail title screen (`prestart`, three APT layers) | APT 640x480 | CEN (+ PIC side fill for the backdrop layer) | Centred title; blurred or art sides | L |

## 2. Front-end menus (APT)

All Def Jam APT stages are 640x480 (`unlockven` 600x480, `trans/t1` a 640x2000 slab). The original emitter-hook plan was superseded by renderer classification; the
APT vertex program's stage mapping still matters when auditing source placement.

| Element | How drawn | Class | Placement / original scope | Risk |
|---|---|---|---|---|
| Main menu, Battle setup (fighter count, venue), options, memory-card and user-ID screens, name-entry keyboard | APT; backgrounds are APT bitmaps (`_art/backgrounds`, brick/paper) | CEN, backdrop PIC | Panels centred and correctly shaped; brick/paper backdrop centred with blurred sides | L–M |
| Header/footer strip (`xHead`/`pageHead`: screen title top-left, A/B legends top-right) | APT clip at (0,0) | CEN in v1; L/R anchoring as an upgrade | Title and legends sit at the 4:3 box corners in v1 | L |
| Character select (Select User ID with 3D fighters in panels, up to 4) | APT panels + 3D fighters in sub-views (mode 0 with a rectangle) | CEN + SUB | Panels centred; fighters inside them, correct shape (the experiment showed they squash if widened, hence rule R2) | M |
| Unlock Fighters / Venues, trophies, Message Center | APT; Unlock Fighters puts a full-screen-view 3D fighter beside the panel | CEN + HOR+ | Fighter and panel line up as at 4:3 (Hor+ keeps the 4:3 picture in the centre); 3D room or backdrop extends at the sides | M |
| Transitions (`trans/t1`, pan up/down, door zoom) | APT slab in a clip | CEN with clip to box (R5) | Transition stays inside the 4:3 box; sides show what is behind | M |
| Match results / summary, FFA four-fighter results | APT | CEN | Centred | L |
| Loading posters (e.g. "FREE FOR ALL — FOUNDATION" with portraits) | full-screen 2D picture (loading snapshot / APT) | PIC | Poster centred, blurred brick sides (the experiment shows a stretched poster is clearly wrong) | L |

## 3. Story mode

| Element | How drawn | Class | Placement / original scope | Risk |
|---|---|---|---|---|
| In-engine cutscenes (CINEMA, `assets/cinema/*.viv`) | 3D, aspect mode 1 (16:9, 640x360 letterbox) | CUT | 16:9: full screen, authored framing (verified). 21:9/32:9: full-height Hor+; centered16:9cinema box is not implemented. 16:10: thin bars or Vert+ | L (verified at 16:9) |
| Cast credits over black during the intro ("BUSTA RHYMES / FAT JOE / DANNY TREJO") | 2D text over black | CEN | Same relative placement inside the 4:3 box | L |
| Choose Style, create-a-player (Fighter Decision folder, sketch-artist head) | APT + 3D head sub-view | CEN + SUB | Folder centred, head in its frame | M |
| Crib hub (3D room; "CRIB" top-left, legend top-right, icon bar bottom-left) | 3D full-screen view + APT overlay | HOR+ + CEN (anchors as upgrade) | Room fills the screen; overlay at the 4:3 box corners (v1), screen corners later | L |
| Map / shop district | APT (2D map) | CEN + PIC | Centred map, blurred sides | L |
| Shops (barber, tattoo, clothes, jewellery) with `SHOP::Cin Cam` and tattoo camera | 3D scene + APT panels | HOR+ + CEN | Shop room wider; panels centred; 3D fighter stays aligned with the panels | M |
| Gym "Learn Moves" (3D room, projector screen, move preview) | 3D room (HOR+), preview = mode 2 sub-view (fov 20) showing a 240x176 clip texture | HOR+ + SUB + CEN | Room wider; projector and preview stay aligned in the centre | M |
| Phone / messages, trophies, photos (green rims are console-accurate) | APT | CEN | Centred | L |
| Tutorial fight prompts and first-time help | native, centred with `(640 − w)·0.5` (`sub_000B6850`, `sub_000D66E0`) + dimmer | CEN + FILL dimmer | Centred text, full-width dim | L |

## 4. Fights (1v1, 2v2/tag, 3-way, 4-way FFA, special match types)

| Element | How drawn | Class | Placement / original scope | Risk |
|---|---|---|---|---|
| Arena, fighters, crowd, props | 3D full-screen view (mode 0) | HOR+ | More arena and crowd at the sides, same fighter size (verified 16:9 1v1, 21:9 FFA) | L |
| Fight camera (2–4 fighters) | `sub_000AB550`, aspect-independent | none | Behaves exactly as at 4:3; see [05](05-cameras-and-venues.md) | L |
| Venue edges (walls, crowd cut-outs, set backs) | venue geometry | HOR+ | Mostly fine (Foundation verified at 21:9); some venues may show unfinished edges at 21:9/32:9 → sweep | M |
| Special cameras (finishers, environment attacks, Blazin' moves), fight intros/outros | 3D with scripted/orbit cameras, full-screen view | HOR+ | Wider close-ups; a few shots may show set edges or extras early → per-shot check | M |
| Health bars (two layers), momentum meter, Blazin' flame, names, scores, point pop-ups ("+50000"), move names ("MUDHOLE STOMP"), combo counters, DANGER!, KO marks, button prompts | native quads, positions from `hud.xsh` locators `pos_pN_*`, `text_pN_*` (`sub_000B57A0`) | L/R by player slot (P1/P3 left, P2/P4 right) in `edge` mode; centred-16:9 box in `centered` | 16:9: bars out at the screen corners, same size; 21:9+: bars at the corners of a centred 16:9 box | M–H (locators undecoded; 4-player layout) |
| Timer, round/KO/FIGHT text (`pos_over_*`) | native quads | CEN | Centred, unchanged | L |
| Pause menu (`pause.xsh`, `Pause:QN_*`) and its `PauseMenuGrey` dimmer | native quads (`sub_000CD490`) | CEN + FILL | Centred menu, full-width dim | L |
| Popups (`popups.xsh`, `PopupMenuGrey`) | native (`sub_000D4980`) | CEN + FILL | Same | L |
| Venue screen tint, vignette, scratches (`V2BG_xxA.pfx`; `sub_000D8930`, `sub_000D99C0`) | native 640x480 quads | FILL | Covers the whole screen, as at 4:3 | L–M |
| Blazin' colour filter, hit flashes, fades to black/white | constant-colour blend / full-screen quads | FILL | Full-screen | M (find each emitter) |
| Screen shake | camera offset (`sub_000AACA0`) | none | Unchanged | L |
| Light glows, smoke (point sprites) | point sprites | host x-size scale | Round glows | L |
| Shadows | 3D (stencil) | HOR+ | Unchanged | L |
| Render-to-texture views (`sub_000177E0`, own fov/aspect) | separate view | none | Unchanged (must not be widened) | L |
| Post pass `sub_00115CB0` (reads the view's fov/aspect, clip-space ±0.5 constants) | unknown effect | verify | Should follow the widened view automatically; check visually | M |

## 5. Port's own UI

| Element | Treatment |
|---|---|
| Launcher, in-game overlay (Dear ImGui, window pixels after present) | Shares the current display settings; rendered in host/window coordinates. |
| Window bars (canvas aspect ≠ window aspect) | Present fit, as today. |
| Captures for tests | Stock goldens/replay explicitly use4:3. Default wide capture squeezes the canvas to640x480; native/wide captures are separate opt-in acceptance evidence. |

## 6. Not affected

- Movie previews in menus (`movies.viv`: `blazin_NN`, `styleNN`, 240x176) play inside a panel: they move with the panel.
- Audio/input/save behavior must be validated separately. Wider culling can change
  crowd work and random draws. Stock replay runs4:3; any wide replay comparison
  is separate measured evidence, not an implemented WS-10acceptance gate. See
  the current harness guide and crowd-determinism research.


## Retail title and Mode Select layer map (file audit, 2026-10-17)

The retail title is `screens/intMain/prestart.big` (`intMain/preStart` in flow), rather than demo-only `demoStrt`. The 640×480 retail movie has 165 main-timeline frames,42 characters (Movie 1/Image 15/Shape 10/Sprite 14/Font 1/Text 1) and 15 SHPX textures. Its three city layers are split pairs:

| Layer | SHPX names / APT image IDs | Native canvases | Placement |
|---|---|---|---|
| First | `1` + `2` |128×512 +512×512|Sprite 4,y 29|
| Second | `5` + `6` |128×512 +512×512|Sprite 8,y 102|
| Third | `10` + `11` |128×512 +512×512|Sprite 13,y 16|

Names are not directory indices: name 1=index 0,2=index 5,5=index 13,6=index 14. Widths total 640, but 512 texture rows do not imply 480 visible rows: retain shape bounds/UVs, padding, alpha and vertical offsets. Shield/effect, logo, localized delayed start prompt and copyright are separate live layers. Restore joined scenery coherently and split at original tile boundaries; never bake interactive/localized text into scenery.

`demoStrt.big` is a separate 640×480,380-frame movie with 33 characters/8 images, demoSticker/esrbClip/startSticker and an inactivity timer. City images 1/4/8 are 1024×512; other images 11=512×128,15=256×64,19=256×128,26/28=256×64. Those larger canvases include padding/streaking and do not prove ready-made wide retail art.

`mainMenu.big` is 640×480 with 4 main frames and 227 characters (Movie 1/Sprite 40/Shape 65/Image 120/Morph 1),120 textures rather than 120 backgrounds. Frame 0 places Sprite 222 `mainMenu` at(316.2,239.9); it has 73 frames and independently animated `spotMover`, nested `mainMenu`, `door`, `doorText` and artwork. Initial offsets: nested menu x−162.5, door(140.6,28.5), doorText(143,81). ActionScript moves spotlight/rim, selects posters and changes door text before transitions. Image pairs 4/5=64×512+256×512,13/14=64×512+256×512,52/53=64×512+512×512 need shape/UV grouping, not dimension-based guesses. Preserve masks, live lighting, text/localization and pan/zoom separately; a flattened menu plate loses those states.

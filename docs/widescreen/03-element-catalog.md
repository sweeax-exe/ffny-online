# 03 — Element catalog

Every element the player sees, how it is drawn, the widescreen treatment (classes from
[02 §2](02-design.md#2-coordinate-maths-normative)), what it will look like, and the work package. "Verified" sources
are in the research reports and [01](01-findings.md). Risk: L/M/H.

Classes: **HOR+** (3D, widened), **SUB** (3D sub-view: rectangle centred, view unchanged), **CUT** (cutscene policy),
**CEN** (2D centred), **L/R** (2D anchored left/right), **FILL** (stretch to full width), **PIC** (full-screen 4:3 picture:
centred + side fill).

## 1. Boot and attract

| Element | How drawn | Class | How it will look | WS | Risk |
|---|---|---|---|---|---|
| EA and THX logo movies (`eagames.mad`, `thx.mad`, 640x480) | FMV: title decodes into a YUV texture, draws a full-screen quad | PIC | Logo centred at 4:3, blurred ambient sides (or black) | WS-7 | L |
| Legal and LOADING screen (crest + wordmark) | native 2D quads | CEN | Centred, black sides (it is on black) | WS-4 | L |
| Opening movie (`intro.mad`, 82 s) and attract loop (`attract.mad`, 49 s) | FMV | PIC | Centred 4:3 with blurred sides; optional AI side art | WS-7, WS-12 | L |
| Title screen (`demoStrt`, three APT layers) | APT 640x480 | CEN (+ PIC side fill for the backdrop layer) | Centred title; blurred or art sides | WS-5, WS-8 | L |

## 2. Front-end menus (APT)

All Def Jam APT stages are 640x480 (`unlockven` 600x480, `trans/t1` a 640x2000 slab). The stage-to-clip scale is
in the APT vertex program's `c0/c1`; its writer must be found first (WS-5).

| Element | How drawn | Class | How it will look | WS | Risk |
|---|---|---|---|---|---|
| Main menu, Battle setup (fighter count, venue), options, memory-card and user-ID screens, name-entry keyboard | APT; backgrounds are APT bitmaps (`_art/backgrounds`, brick/paper) | CEN, backdrop PIC | Panels centred and correctly shaped; brick/paper backdrop centred with blurred sides | WS-5, WS-8 | L–M |
| Header/footer strip (`xHead`/`pageHead`: screen title top-left, A/B legends top-right) | APT clip at (0,0) | CEN in v1; L/R anchoring as an upgrade | Title and legends sit at the 4:3 box corners in v1 | WS-5 | L |
| Character select (Select User ID with 3D fighters in panels, up to 4) | APT panels + 3D fighters in sub-views (mode 0 with a rectangle) | CEN + SUB | Panels centred; fighters inside them, correct shape (the experiment showed they squash if widened, hence rule R2) | WS-5, WS-6 | M |
| Unlock Fighters / Venues, trophies, Message Center | APT; Unlock Fighters puts a full-screen-view 3D fighter beside the panel | CEN + HOR+ | Fighter and panel line up as at 4:3 (Hor+ keeps the 4:3 picture in the centre); 3D room or backdrop extends at the sides | WS-5 | M |
| Transitions (`trans/t1`, pan up/down, door zoom) | APT slab in a clip | CEN with clip to box (R5) | Transition stays inside the 4:3 box; sides show what is behind | WS-5 | M |
| Match results / summary, FFA four-fighter results | APT | CEN | Centred | WS-5 | L |
| Loading posters (e.g. "FREE FOR ALL — FOUNDATION" with portraits) | full-screen 2D picture (loading snapshot / APT) | PIC | Poster centred, blurred brick sides (the experiment shows a stretched poster is clearly wrong) | WS-4/5, WS-8 | L |

## 3. Story mode

| Element | How drawn | Class | How it will look | WS | Risk |
|---|---|---|---|---|---|
| In-engine cutscenes (CINEMA, `assets/cinema/*.viv`) | 3D, aspect mode 1 (16:9, 640x360 letterbox) | CUT | 16:9: full screen, authored framing (verified). 21:9: 16:9 box. 16:10: thin bars or Vert+ | WS-3 | L (verified at 16:9) |
| Cast credits over black during the intro ("BUSTA RHYMES / FAT JOE / DANNY TREJO") | 2D text over black | CEN | Same relative placement inside the 4:3 box | WS-4/5 | L |
| Choose Style, create-a-player (Fighter Decision folder, sketch-artist head) | APT + 3D head sub-view | CEN + SUB | Folder centred, head in its frame | WS-5, WS-6 | M |
| Crib hub (3D room; "CRIB" top-left, legend top-right, icon bar bottom-left) | 3D full-screen view + APT overlay | HOR+ + CEN (anchors as upgrade) | Room fills the screen; overlay at the 4:3 box corners (v1), screen corners later | WS-2, WS-5 | L |
| Map / shop district | APT (2D map) | CEN + PIC | Centred map, blurred sides | WS-5, WS-8 | L |
| Shops (barber, tattoo, clothes, jewellery) with `SHOP::Cin Cam` and tattoo camera | 3D scene + APT panels | HOR+ + CEN | Shop room wider; panels centred; 3D fighter stays aligned with the panels | WS-2, WS-5 | M |
| Gym "Learn Moves" (3D room, projector screen, move preview) | 3D room (HOR+), preview = mode 2 sub-view (fov 20) showing a 240x176 clip texture | HOR+ + SUB + CEN | Room wider; projector and preview stay aligned in the centre | WS-2, WS-6 | M |
| Phone / messages, trophies, photos (green rims are console-accurate) | APT | CEN | Centred | WS-5 | L |
| Tutorial fight prompts and first-time help | native, centred with `(640 − w)·0.5` (`sub_000B6850`, `sub_000D66E0`) + dimmer | CEN + FILL dimmer | Centred text, full-width dim | WS-4 | L |

## 4. Fights (1v1, 2v2/tag, 3-way, 4-way FFA, special match types)

| Element | How drawn | Class | How it will look | WS | Risk |
|---|---|---|---|---|---|
| Arena, fighters, crowd, props | 3D full-screen view (mode 0) | HOR+ | More arena and crowd at the sides, same fighter size (verified 16:9 1v1, 21:9 FFA) | WS-2 | L |
| Fight camera (2–4 fighters) | `sub_000AB550`, aspect-independent | none | Behaves exactly as at 4:3; see [05](05-cameras-and-venues.md) | — | L |
| Venue edges (walls, crowd cut-outs, set backs) | venue geometry | HOR+ | Mostly fine (Foundation verified at 21:9); some venues may show unfinished edges at 21:9/32:9 → sweep | WS-9 | M |
| Special cameras (finishers, environment attacks, Blazin' moves), fight intros/outros | 3D with scripted/orbit cameras, full-screen view | HOR+ | Wider close-ups; a few shots may show set edges or extras early → per-shot check | WS-9 | M |
| Health bars (two layers), momentum meter, Blazin' flame, names, scores, point pop-ups ("+50000"), move names ("MUDHOLE STOMP"), combo counters, DANGER!, KO marks, button prompts | native quads, positions from `hud.xsh` locators `pos_pN_*`, `text_pN_*` (`sub_000B57A0`) | L/R by player slot (P1/P3 left, P2/P4 right) in `edge` mode; centred-16:9 box in `centered` | 16:9: bars out at the screen corners, same size; 21:9+: bars at the corners of a centred 16:9 box | WS-4 | M–H (locators undecoded; 4-player layout) |
| Timer, round/KO/FIGHT text (`pos_over_*`) | native quads | CEN | Centred, unchanged | WS-4 | L |
| Pause menu (`pause.xsh`, `Pause:QN_*`) and its `PauseMenuGrey` dimmer | native quads (`sub_000CD490`) | CEN + FILL | Centred menu, full-width dim | WS-4 | L |
| Popups (`popups.xsh`, `PopupMenuGrey`) | native (`sub_000D4980`) | CEN + FILL | Same | WS-4 | L |
| Venue screen tint, vignette, scratches (`V2BG_xxA.pfx`; `sub_000D8930`, `sub_000D99C0`) | native 640x480 quads | FILL | Covers the whole screen, as at 4:3 | WS-4 | L–M |
| Blazin' colour filter, hit flashes, fades to black/white | constant-colour blend / full-screen quads | FILL | Full-screen | WS-4 | M (find each emitter) |
| Screen shake | camera offset (`sub_000AACA0`) | none | Unchanged | — | L |
| Light glows, smoke (point sprites) | point sprites | host x-size scale | Round glows | WS-1 | L |
| Shadows | 3D (stencil) | HOR+ | Unchanged | — | L |
| Render-to-texture views (`sub_000177E0`, own fov/aspect) | separate view | none | Unchanged (must not be widened) | WS-2 | L |
| Post pass `sub_00115CB0` (reads the view's fov/aspect, clip-space ±0.5 constants) | unknown effect | verify | Should follow the widened view automatically; check visually | WS-2 | M |

## 5. Port's own UI

| Element | Treatment |
|---|---|
| Launcher, in-game overlay (Dear ImGui, window pixels after present) | Unaffected; gains the new settings (WS-0). |
| Window bars (canvas aspect ≠ window aspect) | Present fit, as today. |
| Captures for tests | Crop the centre 4:3 for goldens when widescreen is on; a separate opt-in writes the whole canvas (WS-1, WS-10). |

## 6. Not affected

- Movie previews in menus (`movies.viv`: `blazin_NN`, `styleNN`, 240x176) play inside a panel: they move with the panel.
- Audio, input, saves. The simulation should not change either, but this is an assumption: wider culling draws more
  crowd and props, and the crowd update draws random numbers (`docs/research/crowd-nondeterminism.md`). WS-10 runs the
  `replay` check with widescreen on to find out; a difference is acceptable only if it is confined to the crowd and the
  fight result stream is unchanged.

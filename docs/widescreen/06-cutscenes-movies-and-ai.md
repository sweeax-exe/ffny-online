# 06 — Cutscenes, movies, backdrops and generative AI

## 1. Cutscenes: try it out — done

The owner's question was whether cutscenes look wrong in widescreen and should stay 4:3. Answer from the 16:9 run
of the Story intro **[M]**: they look right, because they were made for 16:9. The game draws them through view
mode 1 (aspect 16/9, viewport 640x360 at y = 60): the horizontal view is the same as a 4:3 shot and the top and bottom
are cut off. Rendering the same 16/9 view at full height in a 16:9 canvas shows exactly what the console showed between
the bars, bigger. Nothing new appears at the sides, so the classic "widened cutscene shows unauthored content" problem
(Zelda64Recomp #255, Unleashed's cutscene lock) does not arise at 16:9.

Policy (`cutscenes` setting, WS-3; maths in [02 §2](02-design.md#2-coordinate-maths-normative)):

| Screen | `authored` (default) | `fill` |
|---|---|---|
| 16:9 | Full screen, makers' framing | Same |
| 16:10 (Steam Deck) | 16:9 picture, thin bars top and bottom (40 px each on the Deck's 800-line screen) | Full screen, a little more picture at the top and bottom (Vert+). The engine renders that area anyway; it was only hidden by the viewport |
| 21:9 / 32:9 | 16:9 picture in a centred box, black sides | Full screen, Hor+ from 16:9 (unauthored sides: experimental) |
| widescreen off | Today's 640x360 letterbox | Same |

Cast credits and any text drawn over cutscenes are 2D and stay centred (CEN).

The owner's check: compare three cutscenes at 16:9 against the console video (the M4f reference), as for the earlier
"looks like the console" work.

## 2. Pre-rendered movies (FMV)

Inventory **[M]**: seven full-screen `.mad` files at 640x480, about 3 minutes in total; the opening movie (`intro.mad`,
82 s) and the attract loop (`attract.mad`, 49 s) are most of it; the rest are logos and short stingers. The 87 clips in
`movies.viv` (240x176) play inside menu panels and need nothing.

Default treatment: centred 4:3 with a blurred side fill (`side_fill = blur`), or black. Not stretched, not cropped.
Whether any FMV has bars baked into its frames is unknown (no decoder on this machine); if one does, a "zoom to fill" for
that file would be the cheap win. Check once a frame can be decoded (WS-7: decode one frame per file through the game's own
decoder by capturing a movie frame in the boot route).

## 3. Full-screen 2D backdrops and posters

Brick/paper menu backgrounds, the title screen backdrop, loading posters ("FREE FOR ALL — FOUNDATION"), the map. These are
centred with the side fill. A plain stretch looks clearly wrong on the posters (portraits get fat in the 21:9 experiment);
on texture-like backdrops (brick, paper) a stretch of the backdrop layer alone might pass, and WS-8 can try it per screen.

## 4. Generative AI

The owner asked whether generative AI can extend textures, assets, menus, cutscenes or cinematics. Summary of the
[generative AI report](../research/widescreen-genai.md) applied to what this game actually needs:

| Content | Needs AI? | Why |
|---|---|---|
| 3D textures and assets | **No** | Hor+ renders more of the world; textures do not need extending. |
| In-engine cutscenes | **No** | Authored in 16:9 (§1). |
| Menus (APT) | **No** for the panels; **optional** for backdrop plates | Panels are centred; only the full-screen backdrop bitmaps leave empty sides. |
| Loading posters, title backdrop | **Optional** | Same as backdrops; posters with faces/text near the edges are poor AI candidates. |
| FMV (about 3 minutes) | **Optional, experimental** | Video outpainting works locally but is slow (the report estimates 2–4 GPU-hours per minute of 480p footage, unmeasured), flickers at seams and fails on close-ups and text. 3 minutes keeps it bounded. |

Recommendation: ship the non-AI default (blur or black), and make AI a later, optional, **user-run local pack builder**
(WS-12), following the HD texture pack's model:

- The repo ships only the script and a manifest; it downloads an Apache-2.0 model on the user's request (stills:
  Qwen-Image-Edit or FLUX.2 [klein] 4B; video: Wan 2.1/2.2 VACE), reads the user's own extracted files, writes an `art`
  pack into the data folder (`mods/<pack>/widescreen/`), and never uploads anything. No game bytes, no generated outputs in
  the repo or releases (`CLAUDE.md` §3). Non-commercial or region-restricted models (FLUX.1 Fill/Kontext [dev], ProPainter,
  HunyuanVideo) are never defaults.
- Stills first (a few dozen backdrop plates): upscale first (the HD pipeline already does this), outpaint only the side
  strips, paste the original pixels back over the centre, then a contact sheet for human review of every seam.
- FMV second and only if the stills look good: shot-split, outpaint in overlapping chunks, composite the original centre back
  each frame (so the 4:3 picture stays bit-exact and only the sides are generated), review per shot, fall back to blur per shot.
- The engine side is the `art` mode of WS-8; it works the same whether the side art is AI-made, hand-made or absent.

Before committing to WS-12, a cheap spike (an afternoon on the owner's GPU): outpaint five backdrops and three 5-second FMV
shots and look at them. The report's §7 lists the exact checks.

## 5. FMV trial: blur vs AI

Owner's request (2026-10-11): try both options on one FMV, then decide on WS-12. Status: in progress; the clip, the
method, the hardware, the time taken and the verdict are recorded here when done. The outputs are the game's artwork and
stay local (`logs/ws/fmv-trial/`), never committed.


# Widescreen prior art: 4:3 to 16:9 / 16:10 / ultrawide in ports, recomps and emulator patches

Purpose: survey how other decompilation ports, static recompilations and emulator/patch communities widened 4:3 games, and extract lessons for the Def Jam: Fight for NY PC port (640x480 source, Xbox GPU command stream translated to D3D11/Vulkan; targets 16:9, 16:10 Steam Deck 1280x800, 21:9, 32:9).

Method and confidence: this is web research only (project READMEs, config docs, source files, issue/PR trackers, forum threads). Source files were read through a summarising fetch tool, so statements about code are paraphrases of what was returned, not line-by-line audits. Anything I could not confirm is marked **[UNVERIFIED]**. Statements that are my own inference rather than something a source said are marked **[INFERENCE]**. Dates in some trackers are 2026 (current as of this research).

---

## 0. Headline findings

1. The most successful recomps (Unleashed Recompiled, Zelda64Recomp) make widescreen a per-element decision, not a global stretch. Unleashed tags every UI element with flags (stretch, anchor left/right/top/bottom, extend, scale, skip); Zelda64Recomp pins each HUD element to left/right/none with RT64 extended-GBI alignment commands.
2. Both ship a "UI alignment" choice for ultrawide: Edge (HUD at screen edges) vs. Centre (HUD inside a 16:9 safe area). Edge is the default in Unleashed.
3. Cutscenes are the single most-reported pain point everywhere. Widening a 4:3-authored cutscene reveals un-authored space (off-screen actors, missing backdrops, jittering helper actors). Unleashed locks cutscenes to original aspect by default; OpenGOAL forces 16:9 for cutscenes (commit-title evidence); Zelda64Recomp and Ship of Harkinian users repeatedly asked for pillarbox/letterbox and maintainers agreed it makes sense.
4. Full-screen 2D overlays (fades, tints, flashes, freeze effects, song/curse effects) are the second most common bug class. They must be stretched or re-meshed to cover the wider area, while HUD must not be stretched.
5. Culling is the main 3D bug class at the edges: games cull against a 4:3 frustum in game code. Fixes are in game code (Zelda64Recomp removes screen-edge tests and keeps depth tests; Ship of Harkinian scales horizontal uncull planes by aspect with a toggle; Trouble Makers widens actor culling but keeps authored camera boundaries). A renderer-only widening cannot fix this. This matters directly for a recomp where culling runs in recompiled guest code.
6. Hor+ (fixed vertical FOV, horizontal grows) is the near-universal default for 3D. Unleashed uses "proper Vert+" (increase vertical FOV) only for aspect ratios narrower than 16:9 and leaves FOV unchanged at or above 16:9.
7. Very wide aspect ratios need a cap or option: edge stretching from rectilinear projection, HUD far from the player's eyes, and revealed un-authored geometry. Nobody found an authoritative industry rule; recommendations here are engineering judgement.
8. 16:10 is a first-class case only in Unleashed (explicit Steam Deck constant and a blend between 16:9 and 16:10 layouts). Most other projects do not document it.
9. Fighting-game specific evidence is thin. No documented Def Jam widescreen patch was found for PS2/GC/Xbox. The only close analogues (Mortal Kombat 11 ultrawide mod, Trouble Makers camera bounds, Smash Melee codes) point the same way: keep the playfield and camera bounds unchanged, extend only the background, and do not let the camera follow into un-authored space.
10. Release hygiene lesson: Mega Man X6 recomp shipped a widescreen HUD regression because a dev-only config section differed from the shipped config; the fix added a packaging check.

---

## (a) Project table

| Project | Technique | HUD / menus / cutscenes | Lessons | Sources |
|---|---|---|---|---|
| **Unleashed Recompiled** (Sonic Unleashed, Xbox 360, XenonRecomp) | Game viewport reported to guest as fixed 1280x720; real output handled by computed offsets/scales applied to 2D draw coordinates. Hooks the camera aspect/FOV (mid-asm hooks). Aspect option: Auto / 16:9 / 4:3 / "Original 4:3". Wide (>=16:9): FOV unchanged (Hor+ by construction). Narrow (<16:9, includes 16:10): vertical FOV increased with `2*atan(tan(fov/2) / aspect * 16/9)`. Below 4:3: cropped vertically to a centred 4:3 area. | Each UI element ("CSD cast") identified by hashed path, mapped to flags: stretch (backgrounds, bars, fades), anchor L/R/T/B, extend-to-edge, repeat, unstretch, skip, gameplay-scale. `UIAlignmentMode`: Edge (default) or Centre (16:9 safe area). Gameplay HUD shrinks toward its anchored corner between 16:9 and 16:10 (Steam Deck constant 16:10 exists). Cutscene option `CutsceneAspectRatio` Original (default) / Unlocked: letterbox if narrower than 16:9, pillarbox if wider. FMVs are scaled to fill (no bars) so a 2.39:1 movie fills ultrawide; other FMVs are 16:9 with side bars. | Per-element tags beat a global rule. Dozens of ultrawide bug reports were each a single missing tag (fades not stretching, arrows "sticking" to the side, scanlines not reaching the edge). Elements with no left-side texture can't be extended without drop-shadow errors; maintainers declined and left to mods. Post-process (DoF/blur) scaling had to be rewritten to scale by width on narrow ratios. Switching aspect ratio repeatedly caused corruption/crashes (fixed). | README https://github.com/hedge-dev/UnleashedRecomp ; config reference https://mintlify.wiki/hedge-dev/UnleashedRecomp/configuration/video (third-party mirror) ; https://raw.githubusercontent.com/hedge-dev/UnleashedRecomp/main/UnleashedRecomp/patches/aspect_ratio_patches.cpp ; https://raw.githubusercontent.com/hedge-dev/UnleashedRecomp/main/UnleashedRecomp/patches/aspect_ratio_patches.h ; https://raw.githubusercontent.com/hedge-dev/UnleashedRecomp/main/UnleashedRecomp/patches/camera_patches.cpp ; issues #171, #145, #510 (https://github.com/hedge-dev/UnleashedRecomp/issues/171 , /issues/145 , /issues/510), PR #228 https://github.com/hedge-dev/UnleashedRecomp/pull/228 |
| **Zelda64Recomp** (OoT/MM via N64Recomp + RT64) | Any aspect ratio; RT64 auto-detects full-screen 2D rects to stretch vs. keep 4:3 ("rect aspect" heuristic AUTO/STRETCH/ADJUST). Game code patched (mod C) so HUD calls RT64 extended-GBI alignment commands. 3D is simply rendered with a wider projection. | HUD elements pinned left / right / none via `gEXSetRectAlign`, `gEXSetViewportAlign`, `gEXSetScissorAlign` with a global margin (default 8) pulling elements toward the edges; clock and reticle use origin NONE; equip animation interpolates origin center->right; scissor widened to full screen for HUD then reset. Option to place HUD in a 16:9 region on ultrawide. Letterbox bars rebuilt as ortho triangles (instead of scissor) so they can be interpolated and span full width. Pillarbox/screen-fill/story-fade are full-screen fill rects. Cutscenes: widened by default (reveals off-screen actors); maintainer agreed an option is sensible and suggested bars shrink as aspect widens while keeping original horizontal FOV up to 16:9. Status of an official implementation not confirmed; a community "Letterbox Adjustments" mod exists. | Anchoring must be a first-class renderer feature (RT64 added origin enums LEFT/CENTER/RIGHT/NONE for rects, viewports and scissors). Heuristics fail on split rectangles (square texrect grids/skyboxes, RT64 #28) -- fix was explicit overrides from game code rather than smarter heuristics. Full-screen effect meshes need to be rebuilt to cover 32:9 (Skull Kid curse clipped at 32:9, PR #259). Cutscene reveals (Great Bay turtle scene, Tatl/Tael jitter at edges) reported as #255, #63, #360. Bloom assertion on widescreen (RT64 #159). | https://github.com/Zelda64Recomp/Zelda64Recomp ; https://raw.githubusercontent.com/rt64/rt64/main/include/rt64_extended_gbi.h ; https://github.com/rt64/rt64 ; patches https://raw.githubusercontent.com/Zelda64Recomp/Zelda64Recomp/dev/patches/ui_patches.c and .../culling.c ; issues https://github.com/Zelda64Recomp/Zelda64Recomp/issues/255 , /issues/63 , /issues/360 , PR /pull/259 ; https://github.com/rt64/rt64/issues/28 |
| **Ship of Harkinian** (OoT decomp port) | Widescreen by wider projection; actor culling in game code is scaled by aspect. | Options for HUD placement/positions exist; users asked for "preserve interface aspect ratio" and "4:3 horizontal FOV during cutscenes with bars scaled" (both turned into discussions, no implementation visible). Pausing slightly widens the 3D scene in widescreen (open bug #6603). | Extended culling PR #4285: old code hardcoded side/top/bottom planes at 2x (arbitrary, caused pop-in on 32:9, reverted); new option scales only horizontal planes by aspect, leaves vertical unchanged, separate toggle from draw distance; some actors excluded to preserve speedrun glitches. Culling needs per-actor-class tuning (doors, trees, plants in the fishing pond still miss it, #7243). | https://github.com/HarbourMasters/Shipwright/pull/4285 ; https://github.com/HarbourMasters/Shipwright/issues/3495 ; /issues/840 ; /issues/6603 ; search listing https://github.com/HarbourMasters/Shipwright/issues?q=widescreen |
| **OpenGOAL** (Jak & Daxter 1-3 decomp) | Aspect modes: Automatic, 4:3, 16:9, Custom, "Fit to Screen" (Jak 2/3 have a custom-ratio menu). Per-element HUD/sprite fixes plus LOD/fog/particle hacks for wide view. | Early "HUD widescreen" PRs fixed sprites one by one; commit titles mention "always 16x9 for cutscenes" and automatic 16:9 letterboxing for movies (from PR #1203 commit list, body gives no detail). Binocular/first-person borders are hidden on non-standard ratios (PR #3596). Pause/progress menu positions derived by hand-aligning common ratios and fitting a polynomial per element for in-between ratios (PR #3596, "best-effort"). Screen warp, freeze-screen and moon/sun sprites needed aspect-aware scaling. Opening-cutscene black backdrop box stops at the 4:3 boundary (issue #1521 / #4174). | Prefer hiding an element that cannot be placed correctly over showing it misplaced. Interpolating per-element offsets from a few hand-verified aspect ratios works but is "never perfect". Full-screen effects (freeze-screen, warp, fades) were fixed repeatedly over years (issues #3653, #4179; PRs #2574, #2603, #3822). Menu text boundaries still being fixed in 2026 (PR #4380). | https://github.com/open-goal/jak-project/pull/3596 ; /pull/1203 ; /issues/4174 ; /issues/1521 ; listings https://github.com/open-goal/jak-project/issues?q=widescreen and https://github.com/open-goal/jak-project/pulls?q=aspect+ratio ; releases https://github.com/open-goal/jak-project/releases |
| **Trouble Makers recomp** (N64 platformer, small recomp) | Widescreen mode vs 4:3 vs cinematic mode, each with its own camera settings. | Widened actor culling only during real widescreen gameplay; 4:3 and cinematic modes keep original camera settings. A widened logical camera boundary made the camera follow the player past the arena end; fix was to keep the authored right boundary in both modes. Rotating-room stages now fill the widescreen field instead of pillarboxing. A regression harness checks the runtime aspect mode. | Do not widen camera bounds -- only visibility. Have an automated check of the aspect mode in the test harness. | https://newreleases.io/project/github/ThiagoLira/trouble-makers-pc-recomp/release/v0.6.0 (release-notes mirror) |
| **Golden Balloon, Mega Man X6 recomp** (small recomps) | Optional "Expanded/Widescreen HUD" mode. | Golden Balloon keeps the widescreen HUD off by default; fixes were layout details (lap times piling on the right, centred labels, race-start slide-in starting off-screen). MMX6 hotfix: HUD meters anchor to widescreen corners via a HUD packet range in config. | Slide-in/out animations that start "off screen" need their start position moved with the edge anchor. Dev config and shipped config diverged and released builds had a broken widescreen HUD; packaging now fails if the `[widescreen]` section differs. | https://newreleases.io/project/github/akratch/goldenballoon/release/v1.5.2 ; https://newreleases.io/project/github/mstan/MegaManX6Recomp/release/v1.0.3 |
| **Perfect Dark PC port** (N64 decomp port) | Widescreen resolutions and configurable FOV listed as features; later a "centered HUD" option reported in press. | Split-screen multiplayer works. Ultrawide behaviour and HUD scaling details **[UNVERIFIED]** (README fetch for the repo returned 404; only press summaries found). | Offering a "centered HUD" toggle is a common minimum. | https://github.com/fgsfdsfgs/perfect_dark (search result listing; fetch failed) ; https://www.dsogaming.com/?p=175480 (press) |
| **re3 / reVC** (GTA III / VC reimplementation) | README lists widescreen support with properly scaled HUD, menu and FOV. | Details of the implementation **[UNVERIFIED]** (repo returns HTTP 451; raw source fetches failed). | Treat as existence proof only. | https://www.github.com/halpz/re3 (search snippet; direct fetch blocked) |
| **Splinter Cell: Double Agent widescreen fix for Cxbx-Reloaded** (ThirteenAG, original-Xbox title) | ASI plugin injected into the Xbox emulator. Computes `hudScale = aspect / (4/3)` and `hudOffset` that centres the 640x480 reference HUD on the wider screen; replaces the game's quad-drawing function so X is divided by `hudScale` and offset; rotates quads around the centroid when needed. | HUD is kept 4:3 and centred (not anchored to edges). FMV handling not present in that source. | Closest direct analogue to our setup: Xbox 640x480-reference 2D layer; the fix is a single transform at the 2D quad-submission point. Edge anchoring needs more than this. | https://github.com/ThirteenAG/WidescreenFixesPack (listing) ; https://raw.githubusercontent.com/ThirteenAG/WidescreenFixesPack/master/source/SplinterCellDoubleAgent.CXBXR.WidescreenFix/dllmain.cpp |
| **ThirteenAG WidescreenFixesPack** (many PC/PS2/PSP/Xbox plugins) | Per-game byte-pattern hooks on FOV read, aspect float, and 2D draw routines. The shared design (from the Need for Speed: Carbon release note and the Punisher source): configurable HUD "constrain" to 16:9, 21:9 or custom ratio, HUD scale, auto-fit; FMV either crop or stretch; a reference preset resolution (640x480 / 800x600 / 1024x768 / 1280x960) is chosen to lay out the HUD, then each HUD element, identified by call order mapped to an enum, is scaled, right-anchored, edge-pinned or left alone. | Black side bars are drawn by hooking full-screen quads (ThePunisher plugin). FMV pattern present but disabled in that plugin. | The reusable pattern is: identify element -> choose policy (scale/anchor/leave) from an enum; constrain HUD to a max aspect; handle full-screen quads separately. | https://github.com/ThirteenAG/WidescreenFixesPack/releases/tag/nfsc ; https://raw.githubusercontent.com/ThirteenAG/WidescreenFixesPack/master/source/ThePunisher.WidescreenFix/dllmain.cpp |
| **Flawless Widescreen** (per-game memory plugins) | Memory patches on FOV, aspect and HUD fields. Community guides use a "Display Detection override": real resolution, then a narrower "HUD Width" and offsets so the HUD sits in a centred 16:9-like area; for Eyefinity/surround a "fake centre" with 3 displays. | Plugins separately fix FOV for menu, cinematics, gameplay, aiming (RE3 plugin listing). | HUD-in-centre-16:9 on 21:9/32:9 is the standard community answer. | https://www.wsgf.org/node/29927 ; https://community.pcgamingwiki.com/files/file/1858-resident-evil-3-flawless-widescreen-fix-for-329surround/ (search listings; summarised only) |
| **Dolphin** (widescreen hack vs Gecko/AR codes) | Built-in hack patches projection matrices at GPU/vertex level; treats everything as 3D, so 2D overlays stretch; a heuristic tries to exempt UI but has never been completely revamped. Game-specific Gecko/AR codes edit the game's own aspect/FOV/culling values and are version-specific. | Objects pop in/out at the left and right edges because games cull against a 4:3 frustum ("render fixes"/culling codes needed). A dev notes true native-aspect filling would need game-specific culling removal and a custom viewport (games often set a 4:3 viewport). | Generic hack = fast, wrong at edges. Game-aware patch = correct but per version. Our recomp is in the second category by nature. | https://forums.dolphin-emu.org/Thread-widescreen-development-topic ; https://bugs.dolphin-emu.org/issues/13945 ; https://forums.dolphin-emu.org/Thread-widescreen-stretched-hud-can-it-be-fixed (search listing) |
| **PCSX2 widescreen patches** | `.pnach` patch group named "Widescreen 16:9" that sets `gsaspectratio` and writes game memory (`patch=1,EE,addr,word,value`); FOV/aspect float found by searching for the known value (e.g. 1.3333/1.7778 or a degree/radian constant) with the debugger and changing the value; patches usually also handle HUD and FMV separately (example gist). | Community patches ship with PCSX2 (~1200 games). Newer builds let users set aspect per game manually. | Must test cutscenes separately; often separate camera values. | https://pcsx2.net/docs/advanced/writing-patches ; https://github.com/PCSX2/pcsx2_patches ; PCGW tutorial (search listing) https://community.pcgamingwiki.com/topic/5647-tutorial-for-creating-widescreen-fixes/ |
| **RPCS3 patches** | Patch system with per-game aspect-ratio patches. Specifics **[UNVERIFIED]** (wiki page content did not load). | -- | -- | https://github.com/RPCS3/rpcs3/wiki/Patches |
| **xemu / original Xbox** | No built-in widescreen hack found; an "over-the-fly" XBE patcher was mentioned as work in progress in a community wiki. **[UNVERIFIED]** whether a widescreen patch set exists. Def Jam: Fight for NY row on the consolemods list reads No for 16:9, Yes for 480p (search snippet; page itself returned 403). | -- | Our base game has no native 16:9 mode on Xbox, so no anamorphic path to reuse. | https://consolemods.org/wiki/Xbox:Games_with_Alternate_Display_Modes (snippet only) |
| **Mortal Kombat 11 ultrawide mod** (fighting game) | Fan mod removes black bars on ultrawide by showing the stage beyond the old boundaries; playable field unchanged. Anti-cheat compatibility **[UNVERIFIED]**. | Official PC patches list "fixed several issues with ultrawide" without detail; early users reported HUD broken on ultrawide and fullscreen reverting to 16:9. | Extend the background, never the playfield. | https://www.gamerevolution.com/?p=532393 ; https://overclock3d.net/news/software/pc-modder-has-added-full-60fps-support-to-mortal-kombat-11/ ; https://mortalkombatgamessupport.wbgames.com/hc/en-us/articles/360038366794-Official-MK11-PC-Patch-Notes-11-14-19 |
| **Super Smash Bros. Melee on Dolphin** (fighting game) | Dolphin generic hack has issues; community prefers game-specific Gecko/AR 16:9 and 21:9 codes (per game version) that also fix HUD and screen flashing. Brawl has a native 16:9 option with a wider camera view. | Whether blast zones/camera bounds change with the code: **[UNVERIFIED]** (not stated anywhere found). | Fighting-game widescreen is done with game-aware codes; generic hacks are called inadequate. | https://smashboards.com/threads/melee-gecko-codes-guide-and-discussion.327311/page-14 ; https://supersmashbros.fandom.com/wiki/Screen |
| **Halo CE (PC, Chimera) / Insurrection** | Chimera exposes vertical-FOV commands "to prevent distortion when switching aspect ratio" and a separate cinematic FOV; Insurrection forces the UI to 16:9. | Halo MCC letterboxes widescreen cutscenes inside the frame (cited as precedent in Zelda64Recomp issue #63). | Separate cinematic FOV from gameplay FOV. | https://www.mintlify.com/SnowyMouse/chimera/commands/visual ; https://github.com/Sledmine/insurrection ; https://github.com/Zelda64Recomp/Zelda64Recomp/issues/63 |
| **re:Blue (Blue Dragon recomp), GoldenEye Xbox 360 recomp** (ReXGlue) | Press-listed aspect options: 16:9, 4:3, 16:10, 21:9, 32:9, Auto, Stretch (Blue Dragon); GoldenEye lists "widescreen options". Implementation **[UNVERIFIED]**. | -- | Confirms the user-facing option set recomp users now expect (including 16:10). | https://www.generationamiga.com/2026/08/27/blue-dragon-gets-an-unofficial-pc-port-nearly-20-years-after-its-xbox-360-debut/ ; https://tbreak.com/goldeneye-007-xbox-360-pc-recompilation/ |

Not found / not researched in depth: sm64ex and sm64-port widescreen HUD internals (only generic notes that "disable drawing distance" exists and that N64 widescreen needs culling fixes), 2Ship2Harkinian internals (it shares the Ship/Harbour Masters approach; no separate source read), Halo Xbox widescreen patches (none found), xemu widescreen patches (none found), PCSX2 or Dolphin Def Jam widescreen patches (none found).

---

## (b) Technique taxonomy

### B1. How the 3D view is widened

| Technique | What it is | Pros | Cons |
|---|---|---|---|
| **Hor+** | Keep vertical FOV, grow horizontal with aspect. | Preserves what 4:3 players saw vertically; framing of the main subject unchanged; default in Unleashed (wide side), Zelda64Recomp, most PC fixes. | Reveals more at the sides: culling pop-in, un-authored scenery, camera collisions. Edge stretching at large FOV (a 32:9 example was quoted at ~143 deg horizontal in one calculator, https://west-games.com/ultrawide-fov-compensation-calculator/). |
| **Vert-** (keep horizontal, crop vertical) | Same horizontal FOV, vertical shrinks at wider aspect. | No new content to the sides, so no culling/seam issues; safest for authored scenes. | Loses top/bottom; characters appear larger; framing breaks (heads cut off). Often seen as a bad default. |
| **Vert+** | Widen vertical FOV for narrow screens. | Right choice for narrow-than-reference ratios; Unleashed uses it below 16:9. | Only meaningful when the screen is narrower than the reference. |
| **Anamorphic / stretch** | Render at 4:3, stretch to 16:9 (or game's native 16:9 anamorphic mode with squeezed image). | Zero engine changes; no new content revealed. | Distorted proportions; HUD stretches too. Not available natively here (Xbox FFNY has no 16:9 mode per consolemods snippet). |
| **Pillarbox / letterbox** | Preserve the original aspect, add bars. | Perfect fidelity. | Not "widescreen"; wastes display. Good fallback and good for cutscenes/menus/FMV. |
| **Crop (zoom)** | Fill 16:9 by cropping vertical area of a 4:3 image. | No new horizontal content. | Loses top/bottom; HUD edges cut. |

### B2. Where the widening is implemented

| Technique | Pros | Cons |
|---|---|---|
| **Renderer/emulator-level projection or viewport hack** (Dolphin widescreen hack, RT64 heuristics) | Works without per-game knowledge. | Treats 2D as 3D (stretch) or relies on heuristics that fail on split rects/skyboxes (RT64 #28); cannot fix game-side culling. |
| **Game-side patch of FOV/aspect/projection values** (PCSX2 pnach, Gecko/AR, Flawless, ThirteenAG, Unleashed mid-asm hooks, Zelda64Recomp mod C) | Correct; can fix culling, camera, HUD together. | Per-game/per-version work; fragile addresses. |
| **Render-target / viewport widening** (output wider framebuffer, keep game's 4:3 virtual space centred) | Simple for pillarbox and for effects that read the framebuffer. | Intermediate RTs (bloom, DoF, shadows, reflection, blur) assume the original size; Unleashed had to fix post-process scaling (#145); RT64 asserted on bloom in widescreen (#159). |
| **Virtual-resolution trick** (tell the game it still has one fixed size, apply offset/scale at the 2D submit point) | Unleashed reports a fixed 1280x720 to the guest and transforms later; Splinter Cell CXBXR transforms at the quad function. Keeps game logic undisturbed. | Needs reliable classification of each 2D draw. |

### B3. 2D / HUD strategies

| Strategy | Used by | Pros | Cons |
|---|---|---|---|
| **Stretch to screen** | default naive; correct for fades/backdrops only | trivial | Distorts HUD and text. |
| **Centre 4:3 (pillar the HUD)** | Splinter Cell CXBXR fix; Flawless "HUD width" | Keeps authored layout intact | Wastes space; on 32:9 HUD sits tiny in the middle. |
| **Centre 16:9 safe area** | Unleashed "Centre"; Zelda64Recomp HUD-at-16:9; Flawless | Good for ultrawide readability | Needs max-aspect clamp logic. |
| **Edge anchoring (left/right/top/bottom)** | Unleashed "Edge" (default), Zelda64Recomp (default margin 8), MMX6 | Feels native on 16:9 | Far from the action at 21:9/32:9; slide-in animations and bars need re-lay-out. |
| **Per-element policy table** | Unleashed (hashed path -> flags), ThirteenAG (enum per element) | Best quality | Large tagging effort; every new screen is a bug source. |
| **Max-aspect clamp for HUD** | ThirteenAG "constrain" to 16:9/21:9/custom | One knob solves ultrawide HUD | Must still anchor within the clamped area. |
| **Hide what cannot be placed** | OpenGOAL binocular/first-person borders | Avoids visibly wrong output | Loses information; only for decorative elements. |
| **Hand-tuned ratios + interpolation** | OpenGOAL (polynomial per element) | Works for odd ratios | Not exact; maintenance. |

### B4. Full-screen 2D overlays versus HUD

Everyone ends up with a separate class for "must cover the whole screen": fades (shop fade, NPC fade-out, title fade), tint (Song of Storms), freeze/flash effects (OpenGOAL freeze-screen, Zelda curse), letterbox bars (rebuilt full width in Zelda64Recomp and Unleashed), scanlines and banners. Two implementation styles exist: (1) classify at draw time by coverage (RT64 AUTO heuristic: a rect that spans the 4:3 area becomes stretched), (2) explicit tag (Unleashed stretch/extend flags, Zelda64Recomp patched meshes). Heuristics need explicit overrides (RT64 #28).

### B5. Cutscene / FMV / menu policy

| Policy | Examples |
|---|---|
| Lock to original aspect (pillarbox) by default, option to unlock | Unleashed (`CutsceneAspectRatio`: Original default) |
| Force 16:9 frame (letterbox/pillarbox around it) | OpenGOAL (commit titles); Halo MCC cited as precedent |
| Unlocked/widened | Zelda64Recomp today; produces revealed off-screen assets |
| Shrinking bars with aspect while keeping original horizontal FOV to 16:9, then widen at minimal bar height | Maintainer proposal in Zelda64Recomp #360 |
| FMV scaled to fill vs. fit | Unleashed fills with no bars (can look "stretched" on 21:9: reported as bug #510, closed as intended); ThirteenAG offers crop or stretch |
| Menus: pillarbox/centre or scale | Many; Unleashed Original-4:3 mode replicates thicker text shadows etc. |

### B6. Culling, LOD and collision on widening

- Games cull on the CPU against the 4:3 frustum; widening reveals pop-in (Dolphin forums; Ship of Harkinian PR #4285; Zelda64Recomp culling patch).
- Fix styles: remove screen-edge tests and keep depth (Zelda64Recomp), scale only horizontal planes by aspect (Ship), widen only in widescreen gameplay mode (Trouble Makers), per-class tuning (doors, trees, plants).
- Side effects: glitch-dependent behaviour (Ship excludes some actors), performance (Ship's old "disable draw distance" caused lag), pop-in on very wide ratios when a fixed multiplier is used (2x at 32:9).
- Point lights / glows depend on screen-position tests; Zelda64Recomp moved to per-vertex Z test in the GPU.
- Camera collision/occlusion: no source covered this directly. Trouble Makers' bug (camera followed the player past the authored arena boundary after the logical boundary was widened) is the closest evidence. **[INFERENCE]** camera-vs-wall tests that use a point or a narrow frustum will clip into walls at wider horizontal FOV because the near-plane corners move outwards.

### B7. FOV caps and distortion

- Rectilinear projection stretches edges as horizontal FOV grows (one source quotes ~143 deg at 32:9 Hor+). Mitigations proposed by the community: cap horizontal FOV, offer Panini-style projection, let players choose FOV axis (https://west-games.com/ultrawide-fov-compensation-calculator/ , https://github.com/sixthsurge/photon/issues/542 as a feature-request source).
- No studio guideline found. **[UNVERIFIED]** whether any recomp implements a projection other than rectilinear.

### B8. What 16:10 needs that 16:9 does not

- From a 4:3 source, 16:10 (1.6) is a modest widening, so Hor+ and culling are lower risk than at 16:9 (inference).
- The HUD layout sits between a 4:3 and a 16:9 composition: Unleashed treats it explicitly (a Steam Deck aspect constant; between 16:9 and 16:10 the gameplay HUD scale is interpolated so elements shrink toward their corner; most UI blends with a narrow factor).
- Narrow-than-16:9 elements need rework that 16:9-only fixes don't test: text clipping, logos cut off at narrow ratios (Unleashed #1499, PR #1489 for credits text), shop names displaced (#1373).
- Post-process effects must scale by width on narrow ratios (Unleashed #145), and at 1280x800 lower-cost shader tiers are sufficient (same issue).
- If the compositor/gamescope letterboxes a 16:9 render on a 16:10 panel, the player loses screen area; render native 16:10 instead. **[INFERENCE]**

### B9. Competitive and multiplayer fairness

- General: wider view gives a situational advantage in shooters/racers; the real impact depends on FOV rules, HUD placement and standards (https://us.ktcplay.com/blogs/technology-hub/ultrawide-competitive-games-unfair-advantage , general article, not about fighting games).
- Fighting games: keep playfield identical; extend only background (MK11 mod, https://www.gamerevolution.com/?p=532393). One forum commenter warned that extended views can make corners confusing because players run into an invisible wall (summarised from https://steamcommunity.com/app/1971870/discussions/0/3821912152398346571 , **[UNVERIFIED]** forum opinion).
- Our game is single-player/local; fairness is not a blocker, but the same principle (do not reveal an advantage or hide the boundary) helps authenticity.

### B10. Hor+ failure modes in fighters and split screen

- Split screen: Perfect Dark port supports split-screen but sources do not describe how aspect interacts **[UNVERIFIED]**. **[INFERENCE]** each pane has its own aspect; a global Hor+ per output aspect would mis-frame panes.
- Fighting camera: see section (e) below.

---

## (c) Lessons for our port: concrete recommendations

All items below are proposals derived from the evidence above; "evidence" cites the supporting finding.

### C1. Default modes and toggles

1. **Aspect ratio setting:** `Auto (match window)` default, plus `4:3 (original)`, `16:9`, `16:10`, `21:9`, `32:9`, `Custom`. Mirrors Unleashed and re:Blue option sets. Include an "Original 4:3" mode that reproduces original rendering exactly (Unleashed has a separate "Original 4:3" for hardcoded behaviours).
2. **3D widening:** Hor+ (fixed vertical FOV). Do not use Vert- or stretch as default. Narrow-than-reference ratios use Vert+ only if we ever support below 4:3 (Unleashed's approach).
3. **UI alignment:** two modes like Unleashed/Zelda64Recomp: `Edge` and `Centre 16:9 safe area`, plus a third `Centre 4:3` for purists (Splinter Cell CXBXR approach). Default: Edge up to 16:9; auto-switch to 16:9 safe area for anything wider than 16:9 unless the player picks Edge (matches the community's ultrawide HUD preference; Unleashed's default Edge draws complaints about bars "extending forever" at ultrawide, issue #171).
4. **Cutscene policy toggle:** `Original (4:3 pillarbox)`, `Adjusted (16:9 frame, bars around)`, `Unlocked`. Default Original or Adjusted, never Unlocked (see C4).
5. **Max rendered aspect** (clamp) for 3D with the remainder pillarboxed: default none up to 21:9 only after QA; for 32:9 default to opt-in. This is the "max-aspect clamp" idea from ThirteenAG/Flawless.
6. **Per-effect toggle for extended culling** (as Ship of Harkinian separates widescreen culling from draw distance).

### C2. HUD anchoring design (for a 640x480 reference)

- Treat all HUD in the original 640x480 coordinate space. Define the transform at the 2D submission point (as in the Splinter Cell CXBXR fix and Unleashed's virtual-resolution approach): `scale = height / 480`, `offsetX = (screenW - 640*scale) / 2` for Centre mode.
- Add per-element **anchor classes**: `LEFT`, `RIGHT`, `CENTER`, `STRETCH` (full-screen), `EXTEND` (bars/backdrops that need to reach the edge), `SKIP` (hide), `KEEP` (do nothing). This is Unleashed's flag set and RT64's origin LEFT/CENTER/RIGHT/NONE. Fight HUD candidates: health/stamina bars left and right, momentum/gauge, timer centre, announcer text centre, crowd meters, pause menu centre.
- Identify elements stably: Unleashed uses a hash of the UI project path; ThirteenAG uses call order mapped to an enum; Zelda64Recomp patches the draw function. Our equivalent should key on (display list / draw sequence ID, texture hash, or guest caller address) in the translator **[INFERENCE: depends on what the translator exposes]**.
- Offer a global **margin** value (Zelda64Recomp has `margin_reduction`, default 8) so players can pull the HUD in from panel edges; also covers overscan on handhelds/TVs.
- Slide-in/slide-out animations that start from "off-screen" must start from the true screen edge, not the 4:3 edge (Golden Balloon issue #51; Unleashed arrows "sticking to the side").
- Elements that are two-piece with one side mirrored need care when extended (Unleashed #171: no left texture, flipping creates wrong drop shadows).
- Prefer hiding a decorative element over showing it misplaced (OpenGOAL).

### C3. Full-screen 2D overlays

- Build a registry (or a coverage-based heuristic) that marks any 2D quad that covers the whole 640x480 as `STRETCH`. Include fades, white flashes, damage vignettes, blur/ghost filters, tints, freeze effects, scene transitions, and letterbox bars (rebuild as full-width, as Zelda64Recomp and Unleashed do).
- Allow explicit overrides for the heuristic's false positives and false negatives: split rects, tiled backgrounds, skyboxes (RT64 #28), gradient strips with clamp addressing (Zelda64Recomp note on edge glitch).
- Reaction effects computed in 3D space (screen-warp, freeze) need aspect-aware scaling, not just quad stretching (OpenGOAL PRs #2574, #2603).
- Render-to-texture post effects (bloom, blur, DoF) should allocate at the output aspect, not 640x480 stretched; test at 16:10 and 32:9 (Unleashed #145, RT64 #159).

### C4. Cutscene policy

- Cutscenes and FMVs are authored for 4:3. Evidence of un-authored space being revealed: Zelda64Recomp #255 (turtle/Link appearing at edges), #360 (Tatl/Tael jitter), Unleashed warns of oddities when unlocked, OpenGOAL #4174 (backdrop box ends at 4:3 edge), Ship of Harkinian #3495 (request to scale bars).
- **Default**: render scripted cutscenes inside the original 4:3 frame (pillarbox) or an "Adjusted" 16:9 crop that has been visually vetted shot-by-shot only if feasible. FMVs: pillarbox at original aspect (or a fill-with-bars for already-wide FMVs as Unleashed does).
- **Option "Unlocked"** labelled as experimental, default off.
- Keep a per-cutscene whitelist: a cutscene that has been verified to have no missing geometry at 16:9/21:9 can run unlocked; others pillarbox. **[INFERENCE]** for a game with limited cutscene count this is cheaper than the polish of fixing all of them.
- Transitions between pillarboxed cutscene and wide gameplay need a fade or cut that is not visible as a flash (full-screen fade quads must stretch).
- Fight intros/entrances and finishers (in-engine cameras) are the Def Jam equivalents; test each with the 4:3-pillarbox policy.

### C5. 16:10 and Steam Deck policy

- Treat 16:10 (1280x800) as a first-class target, not a "16:9 with bars". Unleashed defines a dedicated Steam Deck aspect constant and interpolates HUD scale between 16:9 and 16:10.
- Render at native 1280x800; do not rely on the OS to letterbox a 16:9 output.
- HUD: in Centre-16:9 mode the safe area is wider than the screen at 16:10, so use `min(screenAspect, safeAspect)` rather than an absolute 16:9 box. In Edge mode ensure bars/text that were laid out for 4:3 + side space fit without shrinking too much (Unleashed shrinks gameplay HUD toward its corner).
- Post-process and shadow RT sizes: derive from output width and height; lower-cost tiers are adequate at 1280x800.
- Handheld text legibility: HUD font scale option is cheap and valuable (inference).

### C6. Ultrawide policy

- Support up to 21:9 by default with Hor+; treat 32:9 as an opt-in until QA passes (Zelda64Recomp needed a specific PR for 32:9 effects; Ship of Harkinian found fixed 2x culling planes failed at 32:9).
- HUD defaults to the 16:9 safe area when aspect > 16:9 (see C1).
- Provide a horizontal FOV cap (or max rendered aspect) and mention the distortion trade-off. Panini or non-rectilinear projection is optional/stretch goal.
- Backdrops: stage backgrounds, skyboxes, and arena surrounds must extend (see C7).

### C7. Fighting/arena specific

- **Keep the playfield and camera bounds authored for 4:3.** Widen visibility only (Trouble Makers camera bounds fix; MK11 mod).
- **Do not alter the camera distance/zoom logic's inputs.** If the original camera frames fighters using their distance and a fixed vertical FOV, Hor+ keeps the vertical framing identical; if it uses aspect-ratio-based screen-space extents, recalibrate. **[INFERENCE]** this must be verified against the actual guest code, not assumed.
- Fighters should stay centred horizontally: any HUD or camera patch that uses screen-edge-based coordinates (e.g. "keep fighters within N pixels of edge") needs the 640 reference width, not the new width. **[INFERENCE]**
- Arena walls, ropes, ring posts, crowd, backgrounds will show more at the sides: visually check each arena and add a policy (extend geometry, fog/vignette to mask, or max-aspect clamp). Evidence is analogous only (MK11 background extension; OpenGOAL/Zelda un-authored geometry).
- Disable guest frustum culling on screen-edge tests for actors and crowd (Zelda64Recomp culling approach: remove side tests, keep depth/distance), but maintain distance culling for performance.

### C8. Test checklist (proposed)

Resolution/aspect matrix: 640x480 (4:3), 1280x720 (16:9), 1920x1080, 1280x800 (16:10), 2560x1080 (21:9), 3440x1440 (21:9), 5120x1440 (32:9), plus a narrow case (1280x1024 5:4). Switch aspect at runtime repeatedly (Unleashed #217/#218 crashes), resize window, fullscreen toggle, pause/unpause (Ship #6603: pause widened the 3D scene), alt-tab.

For each:
1. Main menu, mode select, character select, create-a-player, options, loading screens: no stretching, correct centring, backgrounds reach the edges.
2. Pause menu and overlays: pause screen does not change the 3D framing.
3. In-fight HUD (both players, partner, crowd/momentum meters, timers, pop-up text): anchored per policy, no overlap at 16:10, readable at 32:9 in Centre mode.
4. Fades, white flashes, hit flashes, blood/blur filters, transitions: cover the entire screen at all aspects.
5. Entrances, intros, finishers, replays, victory cutscenes: no un-authored space, no jittering helper objects, no black boxes ending at 4:3 edges (OpenGOAL #4174).
6. FMVs/logos/credits: correct policy, no stretching on 21:9 (Unleashed #510), no logo cut off at narrow ratios (#1499).
7. Every arena: walls/rope/crowd/background visible at the wide edge; no black strips, no pop-in of crowd/props/fighters near edges; fighters near screen edge not culled.
8. Camera zoom: two fighters far apart, throws, ring-out/wall slams, grapples into corners: fighters stay in frame, same framing as 4:3 for the vertical direction.
9. Post effects: bloom, blur, DoF, shadows and reflections correct and not offset at each aspect (RT64 bloom assertion, Unleashed post-process scale).
10. Performance: 5120x1440 vs 1920x1080 frame time (about 2x pixels); verify no memory spike from wide render targets.
11. Packaging: release build config contains the same widescreen section as dev (MMX6 lesson); automated test that reads the runtime aspect mode (Trouble Makers).
12. Regression: "Original 4:3" mode produces pixel-identical output to the pre-widescreen build.

---

## (d) Pitfalls other projects hit

1. **Cutscenes reveal un-authored content** (Zelda64Recomp #255, #360, #63; Unleashed docs; OpenGOAL #4174; Ship #3495). Maintainers disagreed on whether bars are creative intent vs. a defect; give both options.
2. **Full-screen effects not covering the screen**: shop fade, NPC fade, Song of Storms tint, freeze-screen, curse effect at 32:9, transitions (Unleashed #316, #134; Ship #419/#421/#7343; OpenGOAL #3653/#4179; Zelda64Recomp #259).
3. **Elements that "stick" or appear at the edge**: boss-title arrows, upgrade arrows, result arrows, charging bars in Centre mode (Unleashed #159, #160, #364, #327, #342).
4. **Element extension requires art that doesn't exist**: no left-side texture, mirrored art causes wrong shadows (Unleashed #171, resolved as "mod it").
5. **Heuristic stretch detection misclassifies split rects/skyboxes** (RT64 #28).
6. **Fixed culling multipliers**: arbitrary 2x planes failed at 32:9 (Ship #4285); individual classes (plants in fishing pond, gossip stone, doors) keep reappearing as bugs (Ship #7243, #4097, PR #3888).
7. **Glitch-preserving behaviours break when culling changes** (Ship excludes King Zora, Gerudo guard etc.).
8. **Pausing or menu transitions changing scene framing** (Ship #6603: pause widens 3D scene) and `g_needsResize` projection recompute for pause stretching (Unleashed camera hook).
9. **Widened logical camera bounds** made the camera follow past the arena (Trouble Makers).
10. **Post-process and render-target scale**: DoF/blur only correct at one aspect (Unleashed #145), bloom assertion on widescreen (RT64 #159), repeated aspect switching corrupted visuals/crashed (Unleashed #217/#218).
11. **Narrow aspect regressions** when only testing ultrawide: logo cut off, credits cropped, button prompts clipped at 4:3 (Unleashed #1499, #1354, PR #439, #1489).
12. **Per-element hand-tuning drifts**: OpenGOAL menu and HUD alignment is still being fixed after years (PRs #4075, #4380).
13. **Generic emulator hacks stretch HUD and cull edge objects** (Dolphin forum; Melee community moved to game-specific codes).
14. **Config drift between dev and release** (MMX6 v1.0.3).
15. **Animation start positions** off-screen on one side (Golden Balloon #51).
16. **FMV scaling expectations**: fill-the-screen scaling of a wide FMV looks like stretching to users (Unleashed #510); decide and document fit vs fill.
17. **Anti-cheat/online compatibility of widescreen mods** is an issue for fighters like MK11 mods **[UNVERIFIED]**; not relevant for us.
18. **Native-ratio memory overhead and performance** at 32:9 (about 2x pixels versus 16:9); one article quantified 34% more GPU work for 3440x1440 vs 2560x1440 (https://us.ktcplay.com/blogs/technology-hub/ultrawide-competitive-games-unfair-advantage).

---

## (e) Fighting games and arena brawlers: what the evidence says

Honest summary: there is very little direct documentation of fight-camera behaviour when widened.

- **Def Jam (PS2/GC/Xbox):** No widescreen patch found for Vendetta or Fight for NY in PCSX2/Dolphin/xemu searches. A 2016 Dolphin thread on Vendetta (GC) only says the widescreen hack works but RealXFB video prevents upscaling (https://forums.dolphin-emu.org/Thread-gc-def-jam-vendetta). Nothing found on zoom behaviour, off-centre fighters or visible walls.
- **Smash Melee/Brawl:** Melee's community moved to per-version Gecko/AR 16:9 and 21:9 codes because the generic hack breaks; one poster says a good code also fixes HUD and screen flash problems (https://smashboards.com/threads/melee-gecko-codes-guide-and-discussion.327311/page-14). Brawl offers a native widescreen mode with a wider camera view (https://supersmashbros.fandom.com/wiki/Screen). Whether blast zones or camera clamps are affected: **[UNVERIFIED]**.
- **Mortal Kombat 11:** the ultrawide mod shows the background beyond the old bars, keeps the playable field unchanged (https://www.gamerevolution.com/?p=532393). Early official PC support had HUD problems on ultrawide (Steam thread https://steamcommunity.com/app/976310/discussions/0/1681441347890232317).
- **SoulCalibur II (GC):** Dolphin wiki recommends the game's native 16:9 option rather than the generic hack (https://wiki.dolphin-emu.org/forum/Soulcalibur II/embed).
- **SoulCalibur VI / DoA6 / Tekken 7 / SF6:** only forum chatter on missing ultrawide support, with a fan mod that adds space behind characters **[UNVERIFIED]** (https://steamcommunity.com/app/838380/discussions/0/1843566500519485459).
- **Closest recomp analogue (side-scrolling arena):** Trouble Makers' fix (widen culling, keep authored camera bounds) is the cleanest documented example of "see more but don't move the camera/player constraints."

**[INFERENCE]** for Def Jam specifically:
- Hor+ keeps vertical framing, so a distance-based camera zoom (zoom out as fighters separate) should behave the same if it works off world distance and vertical FOV; it needs a code read to confirm.
- If the camera clamps fighters to a horizontal screen margin using a 640-wide assumption, fighters will appear off-centre or cramped. Review any 640/480 literals in camera and AI-positioning code.
- Arena walls/ropes/props that were designed to be mostly hidden by the 4:3 frustum may show seams or voids at the edges; the fix is content-dependent (extend, mask with fog, or clamp the max aspect).
- Fighter and crowd culling uses screen-edge tests in game code (typical of the era); remove or scale them in the recompiled code rather than at the translator level, as Zelda64Recomp did.

---

## Source index (primary links)

- Unleashed Recompiled: https://github.com/hedge-dev/UnleashedRecomp ; patches directory https://github.com/hedge-dev/UnleashedRecomp/tree/main/UnleashedRecomp/patches
- Zelda64Recomp: https://github.com/Zelda64Recomp/Zelda64Recomp ; RT64: https://github.com/rt64/rt64 ; extended GBI header https://raw.githubusercontent.com/rt64/rt64/main/include/rt64_extended_gbi.h
- Ship of Harkinian: https://github.com/HarbourMasters/Shipwright
- OpenGOAL: https://github.com/open-goal/jak-project
- ThirteenAG WidescreenFixesPack: https://github.com/ThirteenAG/WidescreenFixesPack
- PCSX2 patch docs: https://pcsx2.net/docs/advanced/writing-patches ; patch repo https://github.com/PCSX2/pcsx2_patches
- Dolphin forum widescreen thread: https://forums.dolphin-emu.org/Thread-widescreen-development-topic ; Dolphin tracker https://bugs.dolphin-emu.org/issues/13945
- Consolemods Xbox display modes list: https://consolemods.org/wiki/Xbox:Games_with_Alternate_Display_Modes
- Trouble Makers release notes: https://newreleases.io/project/github/ThiagoLira/trouble-makers-pc-recomp/release/v0.6.0
- Other items cited inline above.

Known gaps: sm64ex / sm64-port HUD handling, 2Ship2Harkinian internals, re3 source, Flawless Widescreen internals, RPCS3 patch format, xemu widescreen patches, direct Def Jam widescreen patches, and official 16:10-specific guidance from any recomp other than Unleashed were not found or could not be fetched.

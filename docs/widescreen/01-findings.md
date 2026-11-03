# 01 — Findings

Evidence levels: **[M]** measured in a run of the game, **[V]** verified in the lifted code or the game files,
**[R]** from a research report and not re-checked, **[A]** assumption. Guest addresses are image VAs.
Captures stay local (`logs/ws/` in the worktree; never committed, they are the game's artwork).

## 1. Original image and community evidence

- The supported USA Xbox image uses the4:3path described below. The dated community
  search found database reports and PS2patches, not a measured all-platform retail
  survey; see the confidence-labeled community annex.
- The Xbox build reads the dashboard video flags (`XGetVideoFlags`, `sub_001F53FD`) only from `sub_000DB040`,
  which stores an aspect field in a stack object that `sub_00011E40` never reads; the device's
  `D3DPRESENTFLAG_WIDESCREEN` setter `sub_0010D310` has no callers **[V]**. The toolkit's "widescreen + HDTV" answer
  therefore changes nothing, which matches what we saw.
- The Flash front end's `gWidescreenMode` global (`sub_0006B760` pushes the value string `"off"` at `0x29A9D4` for the
  name at `0x29A9D8`) is read only by `main`/`mainBE`, two movies of a shared EA Sports UI framework that copy it to
  `_global.WIDESCREEN` and nothing else uses **[V]** ([front-end report](../research/widescreen-frontend-2d.md) §1).
  Setting it to `"1"` (`RECOMP_WS_UI=1`) changed nothing visible in the user-ID screens, the menus or the Story intro **[M]**.
- Community patch reports are preserved in the [dated prior-art annex](../research/widescreen-prior-art.md#def-jam-community-patch-annex-2026-10-10).
  Negative search results do not prove no patch exists, and file metadata alone
  does not establish native option availability or correct HUD/movie behavior.

## 2. All 3D goes through one aspect function

- `sub_0001A4E0` (`GetViewParams(this = viewport, mode, &fov, &aspect, &y, &height)`, thiscall, `ret 0x14`) supplies
  the field of view and aspect to the scene viewport. Its only callers are the viewport constructor `sub_0001A610`
  and refresh `sub_0001A710` **[V]**. Modes, from the original body:
  - **mode 0** (everything the game uses for full-screen 3D): aspect = literal `0x3FAAAAAB` (4/3);
  - **mode 1**: aspect = literal `0x3FE38E39` (16/9), and the viewport is cut to 75% of its height starting at
    `y + h/8`: a 640x360 picture at y = 60 inside 640x480. This is the cutscene letterbox;
  - **mode 2**: aspect = viewport width / height (sub-rectangle views: the gym's move preview, menu previews);
  - **mode ≥ 3**: writes nothing.
- `sub_00103E10` (`View::SetPerspective(fov, aspect, near, far)`, `recomp_0007.c` near line 86918) builds the matrix.
  It passes aspect **1.0** to the D3DX-style builder `sub_001F384B`, then multiplies `m11` by the real aspect itself, so
  `m00 = cot(fov/2)`, `m11 = cot(fov/2)·aspect`: **the game's FOV is a horizontal angle in degrees** **[V]** (checked by
  hand on the lifted lines: `MEMF(edi) = [esi+0x140]·m00`, `m11 = [esi+0x144]·aspect·m11`).
- Frustum culling planes are recomputed inside `sub_00103E10` from the same fov and aspect and consumed by
  `sub_00103B10` (sphere visibility) **[V]**. Widening the projection widens culling.
- The aspect mode is set through `sub_00018C80(mode)`; mode 1 is chosen by `sub_000712D0` (cinema presentation),
  mode 2 by `sub_00062170` (sub-rectangle views) **[V]** ([projection report](../research/widescreen-game-projection.md)).

## 3. The experiments

A prototype replacement of `sub_0001A4E0` and an entry hook on `sub_0006B760` are on this branch in
`src/recomp_manual.c` (section "EXPERIMENT"), off unless `RECOMP_WS_ASPECT=<w/h>` / `RECOMP_WS_UI=1` are set. The guest
still renders 640x480, so captures are **anamorphic**: they were stretched to the target aspect to judge the result
(scratch script; the real renderer work is WS-1). Release build, harness routes with the save guard, all restored
byte-identical.

| Run | Setting | Result |
|---|---|---|
| `fight` (1v1, Foundation) | 16:9 | **[M]** Mode 0, fov 30.00 → 39.32 (Hor+). Fighters correctly proportioned, more crowd and arena at the sides, no edge pop-in, no edge artefacts (edge columns compared with a 4:3 run). Presents median 120 per 2 s (unchanged), 0 crashes. HUD stretched (expected: no 2D work yet). |
| `intro` (Story opening) | 16:9 + UI flag | **[M]** Cutscenes use **mode 1** (fov 20, 25, 30 logged). With the letterbox dropped and the 16:9 aspect kept, the scenes fill a 16:9 frame with the authored framing, no bars. Menus unchanged by `gWidescreenMode = "1"`. Credit text over black is 2D (stretched in the preview). |
| `ffa` (4 fighters) | 21:9 | **[M]** Four fighters framed as at 4:3 (the camera is aspect-independent, see [05](05-cameras-and-venues.md)); crowd and walls fill the extra width; no missing geometry at this venue. Loading poster and HUD stretched (2D). Character-select 3D previews (mode 0 sub-views) were widened too, which is wrong once the 2D is centred (rule R2 in [02](02-design.md)). |
| `gym`, `crib`, `unlock` | 16:9 | **[M]** Hub rooms (mode 0, full screen) widen cleanly. The gym move preview is a mode 2 sub-view (fov 20). In Unlock Fighters the full-screen 3D fighter drifts toward the centre while the stretched UI moves outward; with the UI centred they line up again (rule R1). |

Baseline 4:3 captures for comparison: the same routes without the variables (and older ones in other worktrees).

Measured facts about content:
- In-engine cutscenes: exactly **640x360 visible, 60 px bars** top and bottom (intro at 46 s and 58 s) **[M]**.
- Story hub and gym: menu panels cover the frame from y ≈ 35 down; 3D room behind **[M]**.
- FMV files (`<data>/extracted/movies/*.mad`, `MADk`): all **640x480**, ~30 fps. Durations from the chunk count:
  intro 81.9 s, attract 49.2 s, soulplan 30.1 s, thx 5.0 s, eagames 3.2 s, SoulTrai 2.5 s, extra 2.5 s:
  **≈ 3 minutes in total** **[M]**. The 87 clips in `movies.viv` (82 `blazin_NN`, 5 `styleNN`, ≈ 8.3 minutes) are
  **240x176** previews shown inside menu panels: they need no widescreen work **[M]**.

## 4. What this means for the design

1. The 3D side is a small, game-side patch: done properly it is `sub_0001A4E0` plus a "full-screen view only" rule.
2. Cutscenes need a policy (authored 16:9 framing), not new content.
3. The renderer needs a wider canvas whose guest coordinates still run 0..640 (anamorphic coordinates, square
   host pixels). Nothing else in the renderer has to know about 3D vs 2D if the game places its 2D.
4. The 2D emitters are a short list: the native quad core (`sub_00028F90`, `sub_0002E390`, `sub_0002E470`, which
   divide by hard-coded 640 and 480), the APT renderer's stage transform (vertex constants `c0/c1`, writer not yet
   found), the movie quad, and the sub-view rectangle setter `sub_0001A300`. See [03](03-element-catalog.md).
5. The fight camera needs no change; venues need a visual sweep for exposed set edges.

## 5. Errata to the research reports

| Report | Claim | Correction |
|---|---|---|
| [cameras](../research/widescreen-game-cameras.md) §TL;DR 2, §1.7 | "The engine applies the FOV as a vertical FOV" | It is **horizontal**: `sub_00103E10` passes aspect 1.0 to `sub_001F384B` and scales `m11` by the aspect itself. The camera's distance formula fits the fighters' spread into the **horizontal** 4:3 field, so its half-width tables in §1.6 and §3.2 should be re-derived; the conclusion (aspect-independent, keep the zoom logic) stands. |
| cameras §4.3 | "No engine-drawn letterbox; mode 1 never selected" | Mode 1 is selected by `sub_000712D0`; the Story intro logs mode 1 and the 640x360 letterbox matches it exactly. Its "pillarbox story cinema to 4:3" recommendation is replaced by the authored-16:9 policy. |
| [front end](../research/widescreen-frontend-2d.md) §6, §7 | "mode 1 is width/height, mode 2 is 16:9 letterbox" | Swapped: mode 1 is the 16:9 letterbox, mode 2 is width/height (checked on the original lifted body's branch order). |
| [front end](../research/widescreen-frontend-2d.md) §4 | "The native 2D quad core is the best single hook" | Its three setters are never called in menus or fights (probe, 2026-10-11). The HUD and menus reach the GPU as ordinary vertex-program batches; placement moved to the renderer ([02 §0](02-design.md#architecture)). |
| [renderer](../research/widescreen-renderer.md) §0.3 | `gWidescreenMode` "around VA 0x29AC58"; the game's widescreen path "could make A nearly free" | The name is at `0x29A9D8`, its value at `0x29A9D4`, used in `sub_0006B760`. The path does nothing (finding 1). |
| renderer §0.3 | toolkit `XC_VIDEO` bits may differ from the XDK's | Irrelevant: the game discards the answer (finding 1). |


## Community evidence and its limits (research, 2026-10-10)

PS2 regional database entries report no native 16:9 option ([USA](https://psxdatacenter.com/psx2/games2/SLUS-21004.html), [PAL](https://psxdatacenter.com/psx2/games2/SLES-52507.html), [Japan](https://psxdatacenter.com/psx2/games2/SLPM-65907.html)); these are database fields, not a first-hand test. No native 16:9 evidence was found for GameCube/Xbox, but absence is weaker than the game's verified Xbox code above. The [ConsoleMods Xbox row](https://consolemods.org/wiki/Xbox:Games_with_Alternate_Display_Modes) was only readable as a search summary: its claimed 16:9=no/480 p=yes/720 p=no/1080 i=no column interpretation remains unverified.

The [PCSX2 patch files](https://github.com/PCSX2/pcsx2_patches) were read directly: FFNY patches contain projection/menu and renderfix edits, with no HUD-repositioning or FMV-resize lines. Reading their horizontal factor as Hor+ and their renderfix as cull/clip repair is inference from instructions/comments, not measured behavior. No game-specific Xbox/xemu/Dolphin patch was found in that bounded search; this is not proof none exists. The detailed dated links and confidence labels are retained in [prior art](../research/widescreen-prior-art.md#def-jam-community-patch-annex-2026-10-10).

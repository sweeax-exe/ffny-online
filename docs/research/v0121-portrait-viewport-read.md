# v0.12.1 Story portrait and Power Plant investigation

2026-10-25. Visual issue: https://github.com/vyanhursky/DefJamFFNY-recomp/issues/108.

## Confirmed portrait cause and correction

The owner confirmed the VY3 loading-card failures at The Limit and Syn Energy
Power Plant. Native, scripted runs against disposable copies of VY3 reproduced
the cropped face at 16:9. A 4:3 control rendered it correctly; disabling texture
packs and switching to CPU vertex execution did not fix widescreen.

The game viewport hook is applying the intended centered transformation. On The
Limit card the resulting logical viewport is x=117, y=35, width=202, height=369.
The pushbuffer trace then shows an AA-expanded surface clip at x=234, y=35,
width=404, height=369. That doubled left boundary matches the visible cut.

`surface_apply_clip` in the toolkit executor expands logical clip registers for
physical AA software surfaces. Its D3D translator forwarding path incorrectly
reused those expanded coordinates even though host vertex positions and the
host supersampling canvas use logical pixels. Widescreen moves the portrait
farther right, exposing the mismatch; a smaller four-player panel can disappear.
This explains why bypassing viewport placement made the face visible while
incorrectly enlarging/moving it. That experiment is not the fix.

The isolated toolkit branch `defjam/fix-aa-host-scissor` uses raw logical clip
registers at the host forwarding boundary, for centered/edge 2D placement, and
for wide side slabs. Physical software/backend clips and AA scales are retained.
No game camera or projection code is changed. Temporary game diagnostics were
removed.

Native verification restores the complete VY3 portrait at both The Limit and
Power Plant, with the authored panel placement and proportions. Tests and final
integration status are recorded below as checks complete. The issue stays open
until the fork change is published and integrated into the release.

## Local evidence (never publish captures or saves)

- Owner evidence retained separately under `logs/v0121-triage/owner-evidence/`:
  sessions `20261025-120402` and `20261025-125336`. The latter has confirmed clip
  timestamps 24.3 s (The Limit) and 29.9 s (Power Plant); corresponding session
  times are 26.2904 s and 53.4020 s. Clips and session clocks are different.
- Before: `logs/shots/vy3-limit-route3/vy3-limit-route3-5s.bmp`.
- Fixed: `logs/shots/vy3-limit-aa-fixed/vy3-limit-aa-fixed-5s.bmp` and
  `logs/shots/vy3-plant-aa-fixed/vy3-plant-aa-fixed-1s.bmp`.
- Detailed trace: `logs/run-20261025-131727.log.err`, lines 17005 and 17009 show
  the doubled clip. This is actual measured state, not an inferred atlas layout.
- Reproduction controls/manifests: ignored `logs/v0121-triage/repro.py` and each
  named run's JSON. Owner saves were verified unchanged; every run's disposable
  save was restored identically.

## Power Plant FFA: measured, not yet fixed

At 16:9, render scale 3, VSync on, a 512 MiB texture cache and the installed menu
pack, the VY3 Story route runs close to 60 FPS but does not consistently meet a
16.67 ms frame interval. Camera and combat vary across runs, so differences below
are descriptive rather than a controlled before/after performance claim.

For the 10-60 seconds after match start (capture disabled during this interval):

| Run | FPS | p95 ms | p99 ms | worst ms | median draws/frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| Story baseline, windowed | 59.52 | 19.39 | 21.86 | 28.57 | 619 |
| Story baseline, fullscreen | 59.41 | 19.91 | 23.18 | 38.86 | 572 |
| Story, portrait correction, windowed | 59.85 | 17.19 | 21.08 | 46.34 | 586 |
| Battle Mode, matched roster | 60.00 | 16.71 | 17.01 | 20.07 | 486 |

The matched control verifies telemetry indices `[108,32,73,61]` in both modes
(VY3, Havoc, Trick and Solo), and both runs load the Power Plant `V2BG_03A` arena.
Battle uses `RECOMP_START_FIGHTERS=99,28,3,9` and `RECOMP_START_VENUE=7`.
Ordinary selections map through fighter coreID minus one; 99 is the created-player
selector. Do not confuse XML ID, coreID, FE venue ID and telemetry index.

The matched Battle control has 486 median draw calls and 332,210 vertices/frame;
Story with the correction has 586 and 394,953: approximately 21% more draws and
19% more vertices. Its p99 interval is 21.08 ms versus 17.01 ms. This supports
additional rendering load in the observed Story match, while camera, outfit and
fight state still need isolation before attributing it to Story mode itself.
A preliminary Battle control used different opponents and is superseded.

The copied Story fixture is a genuine Power Plant FFA with VY3, but it is later
progress than the owner's earlier recorded match and has a different opponent
lineup. Local result: `logs/v0121-triage/vy3-battle-matched.json`, session
`20261025-140500`; compare `vy3-plant-aa-fixed.json`, session `20261025-135313`.
The clean comparison window ends before the thread sampler starts. That sampler
places active work across pushbuffer execution, attribute fetch, shader/texture
state and guest submission; it is not a time profile and cannot prove a bottleneck.

No sustained texture replacement churn was measured during the fights. Reported
GPU busy time is generally well below a frame budget; that does not identify the
CPU or display timing bottleneck by itself. The owner's original 144 Hz fullscreen
recording also has uneven displayed frame durations despite approximately 60
rendered frames per second. Therefore average FPS alone does not disprove the
reported choppiness. Do not claim that the portrait fix resolves performance.

Next: identify the additional batches in the Story frame, controlling camera,
costumes and fight state; collect a time profile on the active renderer thread.
Keep measurements with recording pictures disabled and change one setting at a
time. Do not change the owner's display refresh or profile progress.

## Validation and integration

- New `nv2a_host_clip` fixture fails on the original executor and passes with
  logical host clips. It covers AA off/2x1/2x2, centered 2D and 3D, 4:3 and 16:9.
- Existing `nv2a_ffp_backend` and `nv2a_raster_batch` fixtures pass, preserving
  software surface AA behavior.
- Native loading cards: both pass; no crashes, watchdogs or unresolved calls.
- `regress.py --quick`: 5/5 pass, including 639 unit tests (27 skipped, one slow
  deselected), three golden images, and 120 seconds of fight at median 60 FPS.
  Evidence: `logs/regress-20261025-140418.txt`.
- Matched Power Plant FFA: 96 seconds, median 120 presents per two seconds,
  minimum 100; no crashes/watchdogs/unresolved calls; saves restored. This
  exercises the four-player display path at the reported arena and profile.
- Story visual: 4/4 comparisons pass using the original approved VY2 fixture;
  crib/gym routes and preview request pass. `logs/regress-20261025-141406.txt`,
  `logs/scenarios/regress-visual-20261025-141226/report.json`.
  The first attempt used current saves and failed only the strict move-list
  comparison (15 versus 20 unlocked moves, 85 versus 465 points). The diff is
  confined to those labels. The approved baseline was not modified.
- Comparison frame/event/session files are retained independently of automatic
  recorder pruning in `logs/v0121-triage/measurements/`.
- The game submodule remains clean at its published pin. The tested executable
  is configured with `-ToolkitDir C:/Users/Vlad/code/defjam-v0121-aa-scissor`.
  The owner subsequently authorized publication; the fork commit is published
  and this PR pins it.


## Decisions and hand-off

- D-new: correct the host clip coordinate space in the runtime rather than
  bypassing the game's widescreen viewport. Native evidence shows the viewport
  is correct and its AA-expanded host scissor is wrong.
- D-new: compare the same VY3 profile, arena and telemetry roster before assigning
  the performance difference to Story mode. Keep owner saves and all captures local.

The renderer correction and synthetic fixture are committed locally as toolkit
`bf9c97502b6ace99a6b2111a736deb2dc44b6125`, branch
`defjam/fix-aa-host-scissor`, worktree `C:/Users/Vlad/code/defjam-v0121-aa-scissor`.
The owner authorized publication on 2026-10-25. The fork branch is now pushed and
the game gitlink pins `bf9c975`; no game assets or recordings are published.

To rebuild the tested candidate from the game worktree:

```powershell
./scripts/build.ps1 -Preset win-x64-release -ToolkitDir C:/Users/Vlad/code/defjam-v0121-aa-scissor
```

Follow the v0.12.1 release workflow for integration. Do not close #108 until
integration is complete. Release checks on other platforms remain due.
The Power Plant next step is a paired batch trace/time profile to identify the
additional Story rendering work; no performance fix is claimed by this commit.

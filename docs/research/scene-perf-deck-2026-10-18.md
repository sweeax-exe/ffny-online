# Story scene performance on the Steam Deck, 2026-10-18

The PR #45 tour (`scripts/scene-perf.py run all`, stress profile: 16:9, render scale 3) on a Steam Deck
(AMD Custom APU 0932, SteamOS, desktop session, Vulkan, 90 Hz panel), three times:

| run | build | toolkit | shader cache |
|---|---|---|---|
| **base** | `test/scene-perf-tour` (77714da, PR #45) | 5445b73 | none |
| **cold** | `perf/shader-cache` (040eae5) | 2edf96f | empty `Data/cache/vulkan` at start |
| **warm** | same build, straight after the cold run | 2edf96f | filled by the cold run |

All 38 scenes played and ended in every run; no log faults, no crash. Built in the Deck podman image
(`be9dd70c0e79`), run one launch per scene, one run at a time, with the owner's `Data` and a clean
shader cache for the cold run. Numbers-only copies: `scene-perf-deck-{base,shader-cold,shader-warm}-2026-10-18.json`.

## Totals

| | base | cold | warm | Mac (M1 Pro) |
|---|---|---|---|---|
| frames measured | 74,757 | 75,087 | 75,387 | 77,811 |
| late frames | 3.67% | 3.43% | 3.44% | 0.33% |
| scenes with >1% late | 11 | 11 | 11 | 1 |
| hitches (>= bundle threshold) | 22 | 10 | 8 | 23 |
| worst frame | 172 ms | 172 ms | 60 ms | 213 ms |
| scene load, median / mean | 494 / 713 ms | 305 / 544 ms | 309 / 540 ms | 420 / 599 ms |
| scene loads over 2 s | 1 (sc07a) | 0 | 0 | 0 |
| audio `dry` (all scenes) | 2 | 6 | 8 | 6 |
| peak memory | 300 MB | 300 MB | 280 MB | 555 MB |

## Does the shader cache help?

**Yes, for first-use hitches and loads; no for steady frame pacing.**

- **Hitches:** 22 → 10 (cold) → 8 (warm). sc00, the scene the branch targets, went from 13 hitches and a
  172 ms worst frame to 2 hitches (cold, the 172 ms first-use frame remains) and 0 hitches with a 40 ms worst
  frame (warm). Its p99 fell from 45.5 to 31 ms and its 1%-low from 17.5 to 28-31 fps. sc13 (164 → 45 ms worst),
  sc07b, sc18 and sc08 lose their single hitch too.
- **Loads:** the median scene load drops from 494 to about 307 ms, and the 2.08 s sc07a load to 1.95 s. The gain
  is mostly in the short loads (265-370 ms → 60-250 ms); the long ones (sc00, sc03, sc06, sc07a at 1.4-2.0 s)
  barely move, so they are not shader compiles.
- **The cold run already gets most of it.** The per-HLSL-text module fix (toolkit 2edf96f) is independent of the
  disk cache, and the cold run's totals are close to the warm run's. The disk cache removes the one remaining
  first-launch hitch (sc00's 172 ms at +0.3 s) and little else.
- **Late frames barely change** (3.67% → 3.44%). The same 11 scenes are late by the same amounts in all three runs
  (sc00 21.6/20.3/20.3%, sc06 13.6/-/12.8%, sc11 10.9/10.4%, sc16 9.7/9.9%, sc03 7.3/7.0%, sc07b 7.1/5.7%,
  sc19 6.0/5.4%, sc01 5.0/5.5%): this is content or CPU cost, not compilation.
- **`dry` rose from 2 to 6-8 drop-outs across the tour** (warm run: sc07a, sc09, sc13, sc19, sc20, gf1b2; base: sc00, sc20).
  Each is one event in a scene, in a different scene each run, so this is more likely audio scheduling noise
  (issue #11, PR #30) than the cache; it is worth a repeat run before anyone reads it as a regression.
- **Cache size:** 268 KB on disk after the tour.

## Deck against Mac

- **Frame time:** median 16.5-19.2 ms on the Deck too (sc07a 19.2 ms is the exception), mean 59.6 fps.
- **Late frames are the difference:** 3.4-3.7% against 0.33%, concentrated in 11 scenes where the Mac has one.
  On the Deck, p99 sits at 29-31 ms in those scenes (probably alternating 16/33 ms frames; not checked frame by frame), so they may run at an effective
  ~45 fps even though the median is 60. sc00 is the worst (21%).
- **Hitches:** Deck base 22 against Mac 23; the game-side stalls the Mac report show up here too, all as `game`.
- **Loads:** 494 ms median against 420 ms; no load over 2.1 s (the Mac's eleven slow loads are 0.84-1.79 s).

## Caveats

- The panel is 90 Hz, so `[D3D8-VK] display refreshes at 90 Hz, not a multiple of 60: vsync not used`. The game
  paces itself to 60 and the table counts a frame late against a 60 Hz vblank, so "late" is stricter than on the
  Mac's vsync'd 60 Hz. The 29-31 ms p99 in those scenes says it is not only the metric.
- Desktop session, not gamescope (gamescope exits after its first game launch, which a per-scene tour can't use).
- One run each; the cold and warm runs agree closely with each other, but the base-to-cold difference in `dry`
  and the single-digit hitch counts are within what a repeat might move.
- Not run: `--profile default`, HD packs on (the harness runs with packs off: `textures.enabled` is `false` in every session.json; the earlier version of this line said the owner's packs were enabled, which was wrong).

## Where to look next

1. The 11 late-frame scenes (sc00, sc06, sc11, sc16, sc03, sc07b, sc19, sc01, sc07a, sc04, sc09): CPU-bound
   on the Deck; F5 in the Mac report (fence-hold spin) is the first suspect.
2. sc00 / sc03 / sc06 / sc07a loads of 1.4-2.0 s: waiting on the push-buffer executor, not shaders.
3. Repeat the tour for `dry`.

## Analysis from the Mac side, and a retest (2026-10-18)

Read against the Mac report and the two follow-up branches (`perf/shader-cache`, `test/hitch-capture`).

**The shader cache does what it was for.** Hitches 22 to 8, sc00 from 13 hitches and 172 ms to none and 40 ms,
short loads from about 300 to about 100 ms. Most of it comes from compiling each HLSL text once (cold run),
not from the disk; the disk cache removes the one first-launch compile left in sc00.

**The Mac's random 100-300 ms stalls do not happen here.** The worst frame of the warm run is 60 ms. The stall
session (`test/hitch-capture`, D134) traced them to macOS: a yielding thread drops to the lowest priority there,
and the push-buffer executor ran at default priority, so the executor was starved. Linux's `sched_yield()` does
not drop priority, so no Deck change is expected from that fix.

**What is left is late frames in 11 scenes, and they are not shown to be CPU-bound yet.** The median frame is
normal (16.8-17.3 ms) but p99 is 29-31 ms: heavy shots alternate 16 and 33 ms frames. Warm run against the Mac,
time before Present (*work*: game, executor and the game's own wait for its 60 Hz tick) and in Present:

| scene | late % Deck / Mac | work p50 Deck / Mac | work p99 Deck / Mac | present p50 Deck / Mac | present p99 Deck / Mac |
|---|---|---|---|---|---|
| sc00 | 20.3 / 2.9 | 13.5 / 12.4 | 22.2 / 27.1 | 4.7 / 4.6 | 16.0 / 11.9 |
| sc06 | 12.8 / 0.2 | 12.0 / 13.1 | 20.9 / 17.9 | 4.7 / 3.3 | 6.5 / 5.4 |
| sc11 | 10.4 / 0.5 | 11.6 / 12.3 | 20.1 / 17.9 | 5.4 / 3.7 | 7.2 / 5.8 |
| sc16 | 9.9 / 0.1 | 12.3 / 13.3 | 21.2 / 17.8 | 4.6 / 3.3 | 5.7 / 4.6 |
| sc03 | 7.0 / 0.0 | 11.5 / 12.8 | 20.8 / 16.0 | 5.2 / 3.6 | 6.4 / 5.4 |
| sc01 | 5.5 / 0.2 | 9.0 / 10.9 | 13.2 / 14.6 | 7.8 / 5.4 | 20.1 / 13.4 |
| sc02 (fine) | 0.1 / 0.1 | 12.1 / 13.3 | 14.4 / 16.4 | 4.3 / 3.1 | 6.2 / 4.9 |

Two differences, and both can cost a frame:

1. **Render scale 3 may be heavy for this GPU.** It renders 2560x1440 for a 1280x800 screen, 3.6 times its
   pixels. Present is longer than the Mac's (4.3-5.4 ms median against 3.1-4.6), although here it holds no vsync
   wait and on the Mac it does, so the GPU's share is a candidate, not shown. The clearer sign is *work* p99:
   20-23 ms in the late scenes against 13-16 ms in the ones that are fine (sc01 is the exception: its
   late frames are on the Present side, p99 20 ms).
2. **No vsync, so no slack.** The Mac presents with vsync and its queue absorbs a frame of 18 ms. The 90 Hz
   panel is not a multiple of 60, so here the game paces itself: a frame past 16.7 ms waits for the next tick
   and becomes 33 ms. On a 90 Hz panel even a perfect 60 fps shows each frame for one or two refreshes
   (11 or 22 ms), a judder a player can see whatever the numbers say.

*work* includes the wait for the next tick, so these numbers cannot by themselves tell CPU cost from waiting;
"CPU-bound" above is a hypothesis. The retest separates the three.

Also to check: the report says the owner's HD packs were enabled. The harness runs with `RECOMP_SETTINGS=none`,
which leaves packs off, as on the Mac, and the Deck's peak memory (300 MB against the Mac's 555 MB) points the same
way. `"textures.enabled"` in any `logs/sessions/<s>/session.json` of the run settles it.

### The retest

Same build as this branch (the shader cache is in it; let the cache warm with one pass first if it was cleared),
the 11 late scenes, one arm at a time. Setting the panel to 60 Hz: System Settings, Display & Monitor, Refresh
rate 60 Hz (desktop mode); the summary's *Display* line then no longer says *vsync not used*.

```
SC="sc00 sc06 sc11 sc16 sc03 sc07b sc19 sc01 sc07a sc04 sc09"
# panel at 90 Hz
python3 scripts/scene-perf.py run $SC --every 0 --out logs/scene-perf/deck-A-stress-90hz
python3 scripts/scene-perf.py run $SC --every 0 --env RECOMP_RENDER_SCALE=2 --out logs/scene-perf/deck-B-scale2-90hz
# panel at 60 Hz
python3 scripts/scene-perf.py run $SC --every 0 --out logs/scene-perf/deck-C-stress-60hz
python3 scripts/scene-perf.py run $SC --every 0 --profile deck --out logs/scene-perf/deck-D-deck-60hz
# each against A
python3 scripts/scene-perf.py compare logs/scene-perf/deck-A-stress-90hz logs/scene-perf/deck-B-scale2-90hz
```

About ten minutes an arm. A is the control (same as the warm run, repeated, so run-to-run spread is known);
B changes only the render scale, C only the refresh (vsync on), D is what a Deck player would sensibly use
(16:10, scale 2, vsync). Commit `baseline` copies of the four summaries beside this report.

How to read it:

| result | meaning | next |
|---|---|---|
| B well below A | GPU cost of render scale 3 | recommend scale 2 on the Deck (launcher default or the Deck guide) |
| C well below A | pacing without vsync | Game Mode at 60 Hz in the Deck guide; consider better self-pacing on non-60 Hz panels |
| D near zero | the sensible Deck setting is fine | document D as the Deck configuration |
| none moves | CPU cost | `perf record` on the Deck during sc00; the fence-hold spin (Mac report F5) first |

`dry` (2 against 6-8 between runs) is read from all four arms together; if it stays at one or two single events
per run in different scenes, it is noise.

Per-scene comparison, base against warm:

| scene | load ms A / B | median A / B | p99 A / B | worst A / B | 1% low A / B | late % A / B | hitches A / B | dry A / B |
|---|---|---|---|---|---|---|---|---|
| sc00 | 1630 / 1629 | 17.29 / 17.27 | 45.5 / 31.0 | 172 / 40 | 17.5 / 30.9 | 21.63 / 20.29 | 13 / 0 | 1 / 0 |
| sc01 | 304 / 118 | 17.41 / 17.72 | 28.9 / 29.3 | 52 / 50 | 31.2 / 31.3 | 5.02 / 5.46 | 0 / 1 | 0 / 0 |
| sc02 | 305 / 110 | 16.86 / 16.86 | 21.3 / 20.7 | 43 / 42 | 44.0 / 44.9 | 0.06 / 0.06 | 0 / 0 | 0 / 0 |
| sc03 | 1637 / 1476 | 16.99 / 16.96 | 28.4 / 28.9 | 47 / 45 | 32.7 / 32.8 | 7.33 / 7.02 | 0 / 0 | 0 / 0 |
| sc04 | 372 / 146 | 16.93 / 16.96 | 25.6 / 25.6 | 27 / 27 | 38.5 / 38.4 | 1.47 / 1.48 | 0 / 0 | 0 / 0 |
| sc05 | 1294 / 1142 | 16.91 / 16.91 | 24.6 / 24.6 | 46 / 27 | 39.4 / 40.4 | 0.18 / 0.05 | 0 / 0 | 0 / 0 |
| sc06 | 1524 / 1365 | 17.20 / 16.98 | 29.6 / 29.6 | 50 / 38 | 31.0 / 33.2 | 13.56 / 12.83 | 0 / 0 | 0 / 0 |
| sc07a | 2082 / 1953 | 19.18 / 19.02 | 26.4 / 26.3 | 49 / 54 | 31.5 / 31.2 | 3.77 / 3.15 | 0 / 1 | 0 / 1 |
| sc07b | 281 / 114 | 17.02 / 17.02 | 31.6 / 28.4 | 64 / 35 | 28.9 / 31.6 | 7.12 / 5.73 | 2 / 0 | 0 / 0 |
| sc08 | 338 / 125 | 16.83 / 16.82 | 23.6 / 23.6 | 61 / 45 | 38.3 / 39.8 | 0.27 / 0.24 | 1 / 0 | 0 / 0 |
| sc09 | 1417 / 1202 | 16.86 / 16.84 | 26.9 / 26.7 | 49 / 57 | 33.0 / 32.6 | 1.70 / 1.57 | 0 / 1 | 0 / 1 |
| sc10 | 1045 / 822 | 17.25 / 17.19 | 23.9 / 23.7 | 49 / 52 | 36.4 / 34.2 | 0.18 / 0.24 | 0 / 2 | 0 / 0 |
| sc11 | 674 / 476 | 17.11 / 17.15 | 29.7 / 29.7 | 60 / 59 | 30.3 / 31.2 | 10.86 / 10.43 | 1 / 1 | 0 / 0 |
| sc12 | 265 / 75 | 16.78 / 16.73 | 21.2 / 21.3 | 21 / 21 | 46.9 / 47.0 | 0.00 / 0.00 | 0 / 0 | 0 / 0 |
| sc13 | 1380 / 1348 | 16.83 / 16.84 | 20.6 / 20.8 | 164 / 45 | 37.8 / 46.0 | 0.09 / 0.03 | 1 / 0 | 0 / 1 |
| sc14 | 267 / 102 | 16.82 / 16.86 | 20.8 / 20.6 | 48 / 23 | 41.7 / 48.2 | 0.14 / 0.00 | 0 / 0 | 0 / 0 |
| sc15 | 665 / 483 | 16.51 / 16.92 | 21.1 / 21.9 | 22 / 24 | 46.6 / 43.9 | 0.00 / 0.00 | 0 / 0 | 0 / 0 |
| sc16 | 1464 / 1282 | 17.26 / 17.20 | 29.7 / 29.6 | 45 / 31 | 30.5 / 33.3 | 9.70 / 9.87 | 0 / 0 | 0 / 0 |
| sc17 | 1179 / 1003 | 16.88 / 16.88 | 21.6 / 21.6 | 55 / 57 | 44.2 / 44.4 | 0.05 / 0.05 | 1 / 1 | 0 / 0 |
| sc18 | 294 / 94 | 16.85 / 16.83 | 20.7 / 20.7 | 70 / 49 | 43.6 / 45.8 | 0.11 / 0.03 | 1 / 0 | 0 / 0 |
| sc19 | 361 / 122 | 17.23 / 17.23 | 29.8 / 29.6 | 52 / 60 | 31.3 / 29.8 | 5.99 / 5.39 | 1 / 1 | 0 / 3 |
| sc20 | 236 / 80 | 16.84 / 16.88 | 20.8 / 21.2 | 47 / 44 | 42.9 / 43.4 | 0.10 / 0.07 | 0 / 0 | 1 / 1 |
| sc21 | 302 / 92 | 16.86 / 16.85 | 21.3 / 21.3 | 41 / 24 | 43.0 / 46.3 | 0.08 / 0.00 | 0 / 0 | 0 / 0 |
| gf1a | 997 / 856 | 16.85 / 16.87 | 21.4 / 20.7 | 43 / 21 | 37.0 / 48.1 | 0.27 / 0.00 | 0 / 0 | 0 / 0 |
| gf1b1 | 584 / 440 | 16.87 / 16.88 | 21.1 / 20.9 | 45 / 22 | 30.6 / 46.8 | 0.43 / 0.00 | 0 / 0 | 0 / 0 |
| gf1b2 | 646 / 472 | 16.85 / 16.86 | 21.8 / 22.5 | 41 / 40 | 32.7 / 33.0 | 0.87 / 0.64 | 0 / 0 | 0 / 1 |
| gf1b3 | 521 / 367 | 16.90 / 16.85 | 21.7 / 20.7 | 40 / 39 | 36.4 / 38.5 | 0.22 / 0.21 | 0 / 0 | 0 / 0 |
| gf1b4 | 597 / 429 | 16.85 / 16.86 | 21.0 / 20.6 | 43 / 22 | 37.2 / 46.5 | 0.22 / 0.00 | 0 / 0 | 0 / 0 |
| gf1c | 925 / 693 | 16.91 / 16.95 | 22.8 / 22.8 | 64 / 47 | 33.3 / 38.6 | 0.21 / 0.11 | 1 / 0 | 0 / 0 |
| gf1d | 520 / 342 | 16.78 / 16.74 | 20.7 / 20.6 | 32 / 21 | 40.4 / 47.0 | 0.33 / 0.00 | 0 / 0 | 0 / 0 |
| gf2a | 468 / 254 | 16.84 / 16.84 | 20.7 / 21.3 | 22 / 21 | 47.8 / 46.9 | 0.00 / 0.00 | 0 / 0 | 0 / 0 |
| gf2b | 383 / 199 | 16.84 / 16.87 | 20.7 / 20.7 | 23 / 22 | 46.6 / 47.4 | 0.00 / 0.00 | 0 / 0 | 0 / 0 |
| gf2c | 431 / 255 | 16.86 / 16.85 | 20.6 / 20.6 | 22 / 21 | 47.3 / 48.5 | 0.00 / 0.00 | 0 / 0 | 0 / 0 |
| gf2d | 358 / 195 | 16.81 / 16.82 | 20.6 / 20.7 | 21 / 44 | 48.5 / 37.4 | 0.00 / 0.25 | 0 / 0 | 0 / 0 |
| gf2e | 459 / 276 | 16.86 / 16.87 | 20.6 / 20.6 | 49 / 21 | 39.4 / 48.4 | 0.15 / 0.00 | 0 / 0 | 0 / 0 |
| gf3a | 448 / 262 | 16.91 / 16.80 | 23.7 / 24.5 | 27 / 28 | 39.0 / 38.0 | 0.59 / 0.86 | 0 / 0 | 0 / 0 |
| gf3b | 222 / 63 | 16.86 / 16.85 | 21.3 / 20.7 | 47 / 44 | 30.4 / 39.1 | 0.41 / 0.20 | 0 / 0 | 0 / 0 |
| gf3c | 214 / 64 | 16.81 / 16.87 | 21.4 / 20.7 | 21 / 21 | 46.8 / 48.2 | 0.00 / 0.00 | 0 / 0 | 0 / 0 |


## Retest results (run on the Deck, 2026-10-18)

The four arms above, the 11 late scenes each, same build (`perf/shader-cache`, cache warm), packs off
(`textures.enabled=false` in every session.json, which settles the question above). Numbers-only copies:
`scene-perf-deck-retest-{A-stress-90hz,B-scale2-90hz,C-stress-60hz,D-deck-60hz}-2026-10-18.json`.

**The Deck's panel offers only 90 Hz** (`kscreen-doctor -o`: one mode, 800x1280@90), so arms C and D could not use
the System Settings refresh rate. They ran each launch through `gamescope -W 1280 -H 800 -r 60 --` (a wrapper in
place of the game binary in the local build folder, undone afterwards); the summary's *Display* line reads
`[HOST] display refresh 60 Hz`. This is a **nested gamescope window on the desktop session, not Game Mode**, so it
adds a compositing step that the 90 Hz arms do not have. C and D are therefore a lower bound for what 60 Hz can do.

| arm | settings | late frames | scenes >1% late | mean fps | hitches | `dry` |
|---|---|---|---|---|---|---|
| A | stress (16:9, scale 3), 90 Hz, no vsync | 8.20% | 11 | 54.0 | 2 | 5 |
| B | scale 2, 90 Hz, no vsync | **3.80%** | 9 | 56.1 | 3 | 4 |
| C | stress, 60 Hz in gamescope, vsync | 10.87% | 11 | 53.2 | 2 | 0 |
| D | `deck` profile (16:10, scale 2), 60 Hz in gamescope, vsync | 6.38% | 11 | 56.0 | 5 | 5 |

Late % per scene (p99 ms in brackets for A and B):

| scene | A | B | C | D |
|---|---|---|---|---|
| sc00 | 21.7 (30.8) | 10.9 (29.8) | 24.4 | 12.9 |
| sc06 | 13.0 (30.7) | 3.7 (28.8) | 14.4 | 6.5 |
| sc11 | 11.0 (29.8) | 5.4 (28.8) | 16.0 | 8.4 |
| sc16 | 9.8 (30.6) | 3.0 (29.6) | 11.7 | 4.2 |
| sc03 | 7.2 (29.2) | 3.9 (26.8) | 10.2 | 5.3 |
| sc07b | 6.8 (30.8) | 5.3 (29.8) | 11.0 | 9.0 |
| sc01 | 5.5 (29.2) | 0.2 (22.9) | 6.5 | 2.1 |
| sc19 | 5.3 (29.7) | 2.6 (28.9) | 7.7 | 5.9 |
| sc07a | 2.8 (25.7) | 2.3 (25.6) | 7.3 | 2.2 |
| sc04 | 1.5 (25.6) | 0.1 (24.8) | 3.0 | 2.6 |
| sc09 | 1.9 (26.7) | 1.1 (25.4) | 4.9 | 5.0 |

**Reading, against the table above:**

- **B well below A: yes.** Render scale 2 more than halves late frames (8.2% to 3.8%) and takes sc01 and sc04
  to about zero, sc06 from 13% to 3.7%, sc16 from 9.8% to 3.0%. So much of the late-frame cost is GPU work at
  scale 3 (2560x1440 on a 1280x800 panel). Recommendation: **scale 2 on the Deck** (the launcher default is 2;
  the `stress` profile is a Mac setting).
- **C below A: no.** C is worse than A (10.9% against 8.2%), not better, so a 60 Hz panel with vsync does not by
  itself cure the late frames. Caveat: C is nested gamescope, and the extra compositing may account for the
  difference. Game Mode at 60 Hz proper was not tested; it needs the owner at the Deck.
- **D near zero: no.** D is 6.4% and worse than B (3.8%), with the same settings apart from the 60 Hz vsync path
  and 16:10. Again the vsync path is no help here (nested-gamescope caveat applies).
- **Does anything leave a floor?** Yes. Even at scale 2, nine scenes stay above 1% late and p99 remains
  25-30 ms: sc00 (11-13%), sc11 (5-8%), sc07b (5-9%), sc06 (4-7%) are the worst. Their GPU cost is not the whole
  story; the CPU hypothesis (`perf record` during sc00, fence-hold spin first) is still open.
- **`dry` is noise.** 5, 4, 0, 5 across the arms (and 2 to 8 in the tour), in different scenes each time, with no
  link to any setting; C had none. Nothing to act on.
- **Hitches and worst frames:** 2-5 hitches per arm over 11 scenes, worst frame 60 ms (sc11), no repeat of the
  172 ms first-use compile. The cache holds.

**Not done:** Game Mode at 60 Hz; `perf record`; a 90 Hz arm with vsync forced. The first is the one that
answers whether the Deck's own 60 Hz (LCD) models, or Game Mode's frame limiter, behave better than this
desktop-session run.

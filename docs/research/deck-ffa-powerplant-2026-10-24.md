# Def Jam recomp on the Steam Deck: FFA at the Power Plant, 2026-10-24

Benchmark: v0.11.0 (installed build, toolkit `bd8c633`), every fighter and venue unlocked
(`RECOMP_UNLOCK_ALL=1`), Battle > Free For All with four fighters at the Power Plant (retail front-end
venue 7, `Game.ChooseVenue(7)`), scripted pad, 100 s of fight, HD packs on (`cache_mb` 2048), 16:10 unless
stated, vsync on. Run through the project's harness under `gamescope --backend headless -W 1280 -H 800 -r 60`
(the 90 Hz rows say so), recorder on for frame times with its pictures off (`RECOMP_RECORD_FPS=0`).
The owner's save was guarded and restored byte-identical after every run.

Columns: mean fps over the fight, p99 frame time, 1% low (mean of the slowest 1% of frames, as fps).

## Baseline (v0.11.0, unmodified)

| setting | fps | p99 ms | 1% low | notes |
|---|---|---|---|---|
| scale 1 | 58.2 | 19.6 | 38.6 | |
| scale 2 | 58.7 | 20.0 | 39.1 | the owner's setting |
| scale 3 | 53.1 | 21.8 | 32.6 | |
| scale 4 | 49.1 | 23.8 | 34.1 | |
| scale 2, 4:3 | 59.1 | 19.3 | 41.6 | |
| scale 2, packs off | 56.8 | 20.1 | 37.3 | packs are not the cost |
| scale 2, fullscreen / windowed | 58.7 / 56.9 | 20.1 / 20.2 | 34.8 / 33.4 | no difference beyond noise |
| scale 2, 90 Hz | 57.5 | 20.3 | 35.4 | |

Run-to-run spread is about 1 fps and 1 ms of p99 (scale 2 was measured four times: 55.8 to 58.7 fps).

## What limits it

Frame time barely moves with scale, aspect, fullscreen or packs, so it is not the GPU at scale 2. `perf record`
of the fight (30 s, 997 Hz) shows two busy threads: the game thread (about 70% of a core) and the push-buffer
executor thread (about 80%), and 65% of all cycles are the executor (`nv2a_pb_exec_method`, `fetch_attr`,
`pb_segment`, `pgraph_d3d11_method`, `draw_primitive`, `bind_draw`). The lifted game code is a few percent.

Per frame on the executor thread: about 13 ms of CPU, then `frame_submit_wait`: submit and **block on the
fence** for the GPU's 3 ms (scale 2), 5 ms (scale 3) or 7 ms (scale 4), then present. 13 + 3.7 is 16.7-17 ms against
a 16.67 ms budget, so there is no slack: any frame with a few more draws lands on the next vblank (33 ms). That is
the uneven 40-60. Scale 3 and 4 add GPU wait to a thread that cannot afford it.

## Changes (toolkit branch `perf/deck-frame-overlap`, commit 8d54440, patch next to this file)

1. **Frame slots in `d3d8_vk.c`.** Two sets of command pool and buffers, descriptor pool, vertex/index/uniform/staging
   rings, fence, acquire semaphore and retired-image list. A frame is submitted without waiting; its fence is waited
   when its slot is wanted again. Each submit begins with a memory barrier so the GPU still runs the frames one after
   the other (texture uploads cannot overtake the previous frame); only the CPU's recording overlaps. The present waits
   on a per-swap-chain-image semaphore. Read-backs, window captures, the overlay, gamma ramp changes, ring overflow and
   swap-chain changes still wait for everything in flight. `RECOMP_VK_SYNC=1` restores the old behaviour.
2. **`combiner_shader`** hashed and compared the whole combiner state on every draw (4% of the executor thread);
   the last two programs are now remembered.
3. **Push-buffer method inventory** (`note()`, only printed for `RECOMP_PB_SCAN`) counted every parameter word of every
   frame; it runs only when asked for.

## After (same runs, patched build)

| setting | fps | p99 ms | 1% low | before |
|---|---|---|---|---|
| scale 1 | 59.7 | 17.2 | 42.8 | 58.2 / 19.6 / 38.6 |
| scale 2 | 59.5 | 17.5 | 43.9 | 58.7 / 20.0 / 39.1 |
| scale 3 | 59.9 | 17.2 | 48.2 | 53.1 / 21.8 / 32.6 |
| scale 4 | 59.8 | 17.3 | 46.1 | 49.1 / 23.8 / 34.1 |

Two more scale-2 runs of the patched build: 59.6 / 17.6 / 46.5 and 59.8 / 17.6 / 53.2. The GPU wait in the
`[VKPERF]` line goes from 3.0 ms to 0.00 and Present from 3.7 ms to 0.7 ms. The executor still spends about
13 ms of CPU a frame, so the slack is now 3.5 ms; scale 4 is as smooth as scale 1. The 1% low is set by the few
50 ms frames at scene starts, which this does not touch.

Pinning the game to one thread per physical core (`taskset -c 0,2,4,6`) did not help (p99 21.0 / 20.5 ms).

## Verification of the patched build

- Golden frame signatures `m2`, `m3`, `m4a` match bit for bit; `fight`, `ffa` and `intro` pass.
- `combat` and `versus` fail one assertion, `audio.dry_queue` (the device ran dry), **on the unmodified build too**
  (dry events counted: baseline 1 and 2, patched 3 and 4; gaps logged: baseline 2 and 4, patched 3 and 4): a Deck
  audio issue, different every run, not new. A longer comparison would settle whether the patch moves it.
- Not run: Windows (uses the Direct3D 11 backend, untouched), macOS (the same Vulkan path through MoltenVK: needs a
  regression run), the Vulkan validation layers, Game Mode on the real panel.

## Not measured, and why it matters

- Headless gamescope has no real display, so the compositor and panel are not in these numbers.
- The Deck's panel is 90 Hz. On a refresh that is not a multiple of 60 the game switches vsync off and paces itself to
  60, so each frame is shown for one or two refreshes (11 or 22 ms): uneven motion even at a steady 60 fps. In Game Mode
  set the refresh rate to 60 Hz (quick access menu > Performance) to get proper vsync.

## Next candidates (CPU, executor thread; planned in docs/04-improvement-backlog.md)

`nv2a_pb_exec_method` and `pgraph_d3d11_method` each switch on every method (about 14,000 a frame, half of them
vertex-program constants); `fetch_attr` runs per vertex and attribute with a bounds check each time; `draw_primitive`
fetches a first vertex for statistics only. A faster dispatch and a batch fetch path might return another 10-20% of
the executor's CPU.

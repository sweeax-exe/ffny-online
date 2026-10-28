# Story scene performance on macOS, 2026-10-18

Every one of the 38 Story scenes in the USA dump, played through the v0.10.0 scene player
(the developer module's REAL TIME CINEMA mode, [Playing a chosen Story scene](../scene-selector.md))
with the play-test recorder on, on a stress setting: 16:9 widescreen, render scale 3, vsync on.
The aim was to find faults a player would meet and the places worth optimising, and to leave a
baseline the Steam Deck run can be compared with. The numbers are in
[`scene-perf-macos-2026-10-18.json`](scene-perf-macos-2026-10-18.json) (numbers only, no logs or
pictures); how to repeat the run is at the end.

## Summary

- **Every scene plays and ends.** 38 of 38, each returning to the main menu, with no crash,
  watchdog, unresolved call, untranslated instruction or debug-layer line in any log. 22.1 minutes
  of scenes, 77,811 frames measured.
- **Steady 60 fps.** The median frame is 16.5-17.1 ms in every scene; 0.33% of frames are late
  (longer than 25 ms), 387 missed vblanks in total. The GPU is not the limit at render scale 3: a
  frame waits 1.5-3.5 ms on its fence.
- **The faults a player sees, in order:**
  1. **Random 100-300 ms stalls**, about one per two minutes of scene, on the game's side. They
     do not repeat at the same moment when a scene is played again, so they are the runtime or
     the host, not the scene's content. 83% of them fall in a frame in which the game read
     streamed data, against 17% of ordinary frames; two experiments ruled out the log and the
     disk. **Cause found:** the host starved the push-buffer executor (default priority) and every
     yielding thread (Darwin's yield drops the caller to priority 0); fixed on `test/hitch-capture`
     (D134). See [F1](#f1-random-game-side-stalls-of-100-300-ms).
  2. **Scene loads hold the picture for 0.8-1.8 s** in eleven scenes (one frame lasts that long);
     the game is mostly waiting for the push-buffer executor. [F2](#f2-scene-loads-freeze-the-picture-for-up-to-18-s).
  3. **sc00, the opening scene, is the heaviest:** 2.9% late frames against 0.1-0.5% elsewhere.
     [F3](#f3-sc00-is-the-heaviest-scene).
  4. **First-use shader compiles stutter** in every launch, because nothing is cached between
     runs. [F4](#f4-shaders-are-compiled-at-first-use-every-launch).
- **Fixed on this branch:** the overlay's long tooltips (the *Play scene* help among them, and
  every long setting description) ran off the right edge of the window and were cut off; they now
  wrap. Contact sheets of a widescreen run were squashed to 4:3; they keep the capture's shape.
- **Checked and not a fault:** the files the game fails to open during a scene (its own
  loose-file lookups, made in Story too), the 2.5 s of silence after each scene (the return to the
  main menu), and sc07b's 366 MB of texture uploads (an animated reflection).
- **For the Deck:** the game thread spends 38% of its time in a yield loop waiting for the
  executor ([F5](#f5-the-fence-hold-spins)). Harmless on an 8-core M1 Pro; on the Deck's four
  cores it competes with the executor and the audio threads, and is the first thing to look at
  if the Deck run shows more late frames than this one.

## What was run

| | |
|---|---|
| Machine | MacBook Pro, Apple M1 Pro (6 performance + 2 efficiency cores; corrected 2026-10-18, first written as 8 + 2), 16 GB (swap nearly full during the runs), macOS 26.5.2, built-in display at 120 Hz |
| Build | v0.10.1 (`main` at `c4078ef`), toolkit `5445b73`, `posix-release`; the measured build was the released source |
| Settings | Harness defaults (`RECOMP_SETTINGS=none`) plus `RECOMP_ASPECT=16:9`, `RECOMP_RENDER_SCALE=3`, `RECOMP_PRESENT_VSYNC=1`: a 2560x1440 render target in a 1280x960-point window (2560x1920 swap chain), Vulkan through MoltenVK, FIFO present |
| Recorder | On, 10 pictures a second (asynchronous read-back, 0.6-0.7 ms a picture, taken off before judging a frame) |
| Start | Intro skipped (`RECOMP_SKIP_INTRO=1`), every fighter unlocked, saves guarded by the harness |
| Window | Each scene in its own launch; measured from the first frame after the scene's load to its `cinema ended` event; frames next to the harness's 16 contact-sheet captures left out |

`scripts/scene-perf.py` (new, see the end) ran the tour, then repeated the eleven scenes that had a
hitch, then two A/B experiments on six long scenes (two passes each way, about 47,000 frames a
side), and `sample` profiled sc00. The whole pass took
about three hours on the Mac.

The window being 4:3 does not change the work: widescreen sizes the render target, not the window.
Displays at 120 Hz pace to 60 with vsync, as the [known issues](../known-issues.md) describe.

## Results by scene

*load* is the frame that carried the scene's load (the picture holds for that long); *late* frames
are longer than 25 ms; a *hitch* is a frame of at least 50 ms and 2.5 times the running median;
*dry* is the audio device running out of data. All times in ms.

| scene | secs | load | median | p99 | worst | 1% low fps | late | hitches | dry |
|---|---|---|---|---|---|---|---|---|---|
| sc00 | 87.2 | 1432 | 17.10 | 33.7 | 138.8 | 21.8 | 139 (2.9%) | 4 | 0 |
| sc01 | 58.5 | 235 | 16.95 | 20.6 | 175.0 | 35.4 | 8 (0.2%) | 1 | 0 |
| sc02 | 29.9 | 235 | 16.61 | 20.2 | 33.9 | 45.6 | 1 (0.1%) | 0 | 0 |
| sc03 | 56.2 | 1388 | 16.77 | 19.7 | 26.6 | 48.3 | 1 (0.0%) | 0 | 0 |
| sc04 | 68.2 | 290 | 16.74 | 20.9 | 213.0 | 34.7 | 7 (0.2%) | 2 | 1 |
| sc05 | 69.9 | 1096 | 16.64 | 21.4 | 34.5 | 43.7 | 2 (0.1%) | 0 | 0 |
| sc06 | 45.0 | 1321 | 16.83 | 21.3 | 33.8 | 42.2 | 5 (0.2%) | 0 | 0 |
| sc07a | 20.3 | 1790 | 16.82 | 20.7 | 36.5 | 43.5 | 1 (0.1%) | 0 | 0 |
| sc07b | 66.1 | 221 | 16.82 | 21.6 | 47.5 | 40.1 | 8 (0.2%) | 0 | 0 |
| sc08 | 50.4 | 257 | 16.60 | 21.8 | 174.8 | 33.3 | 6 (0.2%) | 1 | 0 |
| sc09 | 46.2 | 1201 | 16.70 | 21.5 | 34.6 | 43.2 | 3 (0.1%) | 0 | 0 |
| sc10 | 29.0 | 839 | 16.69 | 21.4 | 32.0 | 41.6 | 3 (0.2%) | 0 | 0 |
| sc11 | 41.7 | 536 | 16.74 | 21.4 | 96.1 | 30.1 | 12 (0.5%) | 3 | 0 |
| sc12 | 5.6 | 205 | 16.63 | 21.5 | 22.4 | 45.4 | 0 | 0 | 0 |
| sc13 | 56.3 | 1197 | 16.56 | 22.1 | 130.0 | 37.5 | 4 (0.1%) | 1 | 0 |
| sc14 | 49.1 | 199 | 16.62 | 21.3 | 77.1 | 32.7 | 9 (0.3%) | 3 | 3 |
| sc15 | 16.1 | 554 | 16.77 | 21.7 | 23.9 | 43.9 | 0 | 0 | 0 |
| sc16 | 21.5 | 1221 | 16.81 | 20.8 | 31.2 | 44.5 | 1 (0.1%) | 0 | 0 |
| sc17 | 95.0 | 994 | 16.62 | 21.2 | 74.9 | 42.0 | 6 (0.1%) | 1 | 0 |
| sc18 | 62.5 | 233 | 16.64 | 21.9 | 200.7 | 34.2 | 6 (0.2%) | 2 | 0 |
| sc19 | 94.4 | 304 | 16.77 | 21.8 | 33.8 | 41.6 | 10 (0.2%) | 0 | 0 |
| sc20 | 69.9 | 188 | 16.54 | 22.1 | 32.4 | 41.6 | 9 (0.2%) | 0 | 0 |
| sc21 | 39.5 | 234 | 16.60 | 22.4 | 27.4 | 42.9 | 2 (0.1%) | 0 | 0 |
| gf1a | 12.6 | 862 | 16.49 | 22.9 | 72.0 | 26.8 | 5 (0.7%) | 1 | 0 |
| gf1b1 | 7.9 | 505 | 16.75 | 20.8 | 32.5 | 41.1 | 1 (0.2%) | 0 | 0 |
| gf1b2 | 7.9 | 511 | 16.63 | 22.1 | 30.7 | 40.5 | 1 (0.2%) | 0 | 0 |
| gf1b3 | 7.9 | 425 | 16.62 | 22.3 | 23.8 | 43.2 | 0 | 0 | 0 |
| gf1b4 | 7.9 | 485 | 16.72 | 21.5 | 22.6 | 45.4 | 0 | 0 | 0 |
| gf1c | 16.6 | 750 | 16.54 | 21.7 | 29.5 | 43.7 | 1 (0.1%) | 0 | 0 |
| gf1d | 5.2 | 414 | 16.65 | 20.0 | 20.6 | 49.1 | 0 | 0 | 0 |
| gf2a | 21.7 | 366 | 16.65 | 21.7 | 57.5 | 38.2 | 3 (0.2%) | 1 | 1 |
| gf2b | 9.9 | 566 | 16.66 | 21.5 | 22.4 | 45.4 | 0 | 0 | 1 |
| gf2c | 9.9 | 344 | 16.66 | 21.8 | 24.2 | 44.1 | 0 | 0 | 0 |
| gf2d | 6.9 | 284 | 16.54 | 20.8 | 22.5 | 45.4 | 0 | 0 | 0 |
| gf2e | 11.2 | 385 | 16.57 | 21.1 | 22.4 | 46.2 | 0 | 0 | 0 |
| gf3a | 6.0 | 371 | 16.50 | 20.7 | 21.1 | 47.7 | 0 | 0 | 0 |
| gf3b | 8.4 | 165 | 16.83 | 21.2 | 32.6 | 39.9 | 1 (0.2%) | 0 | 0 |
| gf3c | 7.3 | 163 | 16.61 | 23.1 | 84.4 | 13.6 | 4 (1.0%) | 3 | 0 |

Memory peaked at 555 MB (sc17; sc00 and sc19 553 MB). Each scene ran in its own launch, so growth
across scenes was not measured. The texture cache peaked at 39 MB and its 256-entry limit was
reached in sc00 (77 evictions), sc07b (10) and sc19 (34). No audio clipping in any scene; the click detector found 1
to 3 candidate clicks in sc00, sc07b, sc17, sc18, sc19 and gf2b and 13 in sc20, which may be the
scenes' own sound effects (gunshots, impacts) and are worth a listen on the Deck.

## Findings

### F1. Random game-side stalls of 100-300 ms

**What a player sees:** the picture freezes for a tenth to a third of a second, sometimes with a
short gap in the sound, at no particular moment. 23 hitches in the tour, 15 of them 75 ms or more;
the worst 213 ms (sc04), 201 ms (sc18), 175 ms (sc01, sc08).

**They do not belong to the scenes.** The eleven scenes with a hitch were played again. Only three
hitches came back at the same moment: sc00 at +0.28 s (139 ms both times, right after the load; F4),
sc13 at +6.78 s (130 and 134 ms) and a 57-63 ms one in sc18 at +13.15 s. The others moved or
vanished (sc04 213 ms at +32.0 s, then 280 ms at +15.8 s; gf1a 72 ms, then 314 ms; gf3c three of
80 ms, then none). They are on the game's side of Present (the frame's time is spent before it).

**They coincide with file reads.** Across all 29 frames of 60 ms or more in the tour and the repeat,
a `[READ]` line (the game reading its streamed data, 8 KB at a time) falls in 83% of them, against
17% of a random sample of 5,643 ordinary frames. Nothing else in the log comes close.

**Not the logging.** Every read logs a line and flushes the log, after giving up the guest CPU and
taking it back; the executor flushes four lines a frame too. An experimental build that left both
out (six long scenes, two passes each way, 47,000 frames a side) changed nothing: late frames 336
against 316, hitches 13 against 14, loads within 1%.

**One stall was caught in the act:** sc17's 75 ms hitch is the frame in which a thread kept the
guest CPU 48 ms in host code after it had been asked to give it up (`[KERNEL] guest CPU kept 48 ms
in host code`), at `__psynch_mutexwait`: blocked on a host lock while holding the guest CPU, so
every guest thread waited.

**Not slow disk reads either.** The host `read()` in `NtReadFile` runs while the reading thread
holds the guest CPU, so a slow read would stop every guest thread. A second experimental build
timed every read and could give the guest CPU up around it (`RECOMP_EXP_READ_LEAVE`). In twelve
launches of the same six scenes only 3 reads took 4 ms or more, the longest 6.2 ms; giving the CPU
up changed hitches from 16 to 11 and late frames from 360 to 364, within the run-to-run spread
(one scene's worst frame moves between 40 and 250 ms from one run to the next). The reads are fast;
the stalls come with them, not from them.

Both experimental builds were local only and are reverted.

**Cause found (2026-10-18, branch `test/hitch-capture`, toolkit `defjam/hitch-capture`, D134): the
host starves the two threads every frame waits for.** A hitch-triggered capture
(`RECOMP_HITCH_PROBE=1`, below) sampled every thread while frames were late. In the stalls it caught
the game thread was in Direct3D's fence hold (`sub_0021EB90_enter`), yielding until the push-buffer
executor caught up, and the executor (`nv2a_ack_thread`) had work queued (a backlog of 50-220 KB, the
fence 16 behind) but was barely running: over one 54 ms window the game ran 3 ms and the executor
8 ms, though neither was blocked, and in another 77 ms window 5 and 6 ms. A recorder thread that does
nothing but sleep 4 ms at a time woke 30-140 ms late in the same frames. Two things put those threads
behind everything else on the machine:

- **The executor ran at the default priority (31),** the band of every ordinary thread of every
  application. Guest threads and the APU's frame thread already run at user-interactive QoS (46,
  D121); the thread that is the GPU had been left out.
- **Every yield depressed the caller to priority 0.** Darwin's `sched_yield()` is `swtch_pri(0)`:
  until the thread next runs it is below every runnable thread on the host. The fence hold, the
  executor's loop and the APU frame thread all yield in loops (`SwitchToThread`, `Sleep(0)`); the
  probe shows the game thread at priority 0 in the fence hold.

This Mac has 8 cores (6 performance, 2 efficiency; the table above said 10), a browser and the
system's daemons using several of them, and 7 of its 8 GB of swap in use, so it is often short of a
free core for 50-100 ms; that is when a frame stalls, which is why the stalls move from run to run.
Why they fall in frames with a streamed read is not proven: reads come when new content arrives,
which is when the executor has the most to do, so a starved executor shows there first.

**Fix.** The executor runs at user-interactive QoS (`xbox_memory_layout.c`, `RECOMP_EXECUTOR_QOS=0`
restores the default), and on Darwin a yield is `thread_switch(MACH_PORT_NULL, SWITCH_OPTION_NONE, 0)`,
which gives the CPU to a ready thread without lowering the caller (`win32_compat.c`,
`RECOMP_YIELD_DEPRESS=1` restores `sched_yield()`). They go together. Measured on four scenes (sc01,
sc04, sc08, sc11) with six busy processes at default priority on the host, one or two passes each:

| | late frames | hitches | audio ran dry |
|---|---|---|---|
| before (both switches back) | 6.1-7.4% | 9 in two passes | 46 |
| yield change only | 10.8% | 11 | 0 |
| executor QoS only | 2.4% | 0 | 23 |
| both (the fix) | 0.00-0.02% | 0 in two passes | 0 |

The yield change alone is worse: a game thread that no longer depresses itself in the fence hold
takes the cores from an executor still at 31. The QoS change alone leaves the audio frame thread,
which yields, starved. Unloaded, on the six scenes of the earlier experiments (two passes each way,
alternating, shader cache warm): before, 0.12% and 0.09% late with 9 and 7 hitches; with the fix,
0.02% and 0.03% with none (the last pass covers five scenes: something outside the run stopped it in
sc17). An unloaded Mac rarely runs short of cores, so the difference there is smaller, as expected.

**The capture.** `RECOMP_HITCH_PROBE=1` (or a number of milliseconds) with the recorder on: a
recorder thread looks every 4 ms for a frame that has not come for 60 ms and, while it stays late,
calls the runtime's `guest_turn_probe()` every 50 ms (six times at most). That samples every thread
from outside with Mach, one suspended at a time (registers, the frame-pointer chain read with
`vm_read`, priority, CPU time), names the frames with `dladdr` afterwards, and adds which guest thread
holds the guest CPU, its `g_xbox_kernel_caller`, the ICALL ring and the executor's state (position
against PUT, a software method it is stopped at, the fence against Direct3D's latest). A sample costs
2-7 ms on the recorder's thread; nothing runs unless asked. The holder goes into the frame's `hitch`
event, every sample into `stalls.jsonl`; `scripts/hitch-probes.py <session> --summary` reads them.
macOS only (Linux and Windows pin guest threads to a CPU and have no token to ask about). Runs:
`logs/scene-perf/probe-*`, `ab-yield-*` (the yield change alone, unloaded), `ab-load-*`, `ab-fix-*`.
Other things the probes showed: in the first launches after the change, most stalls were F4's
compiles on the executor (fixed by the shader cache, which this branch is built on); one stall had
the executor in an uninterruptible log `write()` with the game waiting for the same lock; scene loads
(F2) are the game and the executor both busy, not waiting.

### F2. Scene loads freeze the picture for up to 1.8 s

In eleven scenes the load holds one frame for 0.84-1.79 s (sc07a 1.79 s, sc00 1.43 s, sc03 1.39 s,
sc06, sc16, sc09, sc13, sc05, sc17, gf1a, sc10); the rest load in 0.16-0.75 s. The picture simply stops
for that long; on the console the same load ran behind the previous picture too, so this is not a
regression, but it is the longest freeze a player meets in a scene, and the same code loads fights.

sc00's load reads 10.6 MB in 1,311 reads of 8 KB, about 1.1 ms a read, far slower than the disk.
`sample` inside the load (1 s, 677 samples per thread) shows where it goes:

- the game thread spends about a quarter of the load in Direct3D's fence hold
  (`sub_0021EB90_enter` in `src/recomp_manual.c`), yielding until the push-buffer executor is
  fewer than 16 fences behind;
- the executor is busy only part of the time: 18% sleeping in vsync pacing (`flip_pace`), 9%
  compiling shaders (`shader_make`), and in one of two profiles 37% inside a single signal
  delivery under the diagnostic read of `PCRTC_START` (`xbox_memory_layout.c:1698`, a trapped
  register read made on every executor loop because `RECOMP_PB_EXEC` turns the NV2A trace on). In
  the other profile, and in steady play, that read costs 1.7-2.3% of the executor, so it is an
  occasional long stall rather than a steady cost.

**Where to optimise:** the load is throttled by the executor, and the executor by pacing and
compiles, not by the disk. Candidates, cheapest first: skip the trapped `PCRTC_START` read unless
`RECOMP_NV2A_TRACE` asks for it; do not pace the executor to vsync while the game is inside a load
(no picture is changing); the shader cache of F4. Not tried here.

### F3. sc00 is the heaviest scene

sc00 (the opening: the police, the car crash) has 2.9% late frames, a p99 of 33.7 ms and 1% lows of
22 fps, against at most 1.0%, 20-23 ms and (outside two short scenes with one hitch) 30-49 fps for
every other scene; it draws the most (median
347 draws, up to 907, 509,000 vertices a frame) and uploads the most textures (43 MB, 77
evictions). The [known issues](../known-issues.md) mention one audio skip as the car crash hits; in
the 4:3, render-scale-2 trial it was there (a 174 ms frame and the device running dry twice at
+82.5 s), in the stress tour and its repeat it was not. Steady sc00 frames are game-side (`work` p99 27 ms);
the GPU fence wait stays near 2-3.5 ms. sc00 is the scene to use when measuring any CPU-side
optimisation, and the first to watch on the Deck.

### F4. Shaders are compiled at first use, every launch

The Vulkan renderer (`d3d8_vk.c`) translates each new vertex and pixel program from HLSL to SPIR-V
with shaderc when it is first drawn, and creates its pipelines with no `VkPipelineCache`; nothing is
kept between runs. Every launch therefore compiles every shader again as it is first needed, and on
macOS MoltenVK then compiles each to Metal as well. sc00's 139 ms hitch right after the load repeats
in every run and carries a `compiled vertex program` line; `shader_make` was 9% of the executor's
time during the load. A shader and pipeline cache on disk (keyed by the program's hash, as
`vk.progs` already is) would remove these, and matter more on the Deck, where pipeline creation is
slower.

**Follow-up (2026-10-18, branch `perf/shader-cache`, toolkit `defjam/shader-cache`, D133).** Two
changes. (1) One module per HLSL text: the program hash and the combiner state cover more than the
generated text, so sc00 compiled its 29 distinct shaders 188 times (2,323 ms) and made 348 pipelines
for 33 (880 ms) in every launch; now 29 compiles (314 ms) and 33 pipelines (34 ms), and sc00's late
frames fell from 2.8% to 0.2% (the baseline's 2.85% was this). (2) The SPIR-V and the
`VkPipelineCache` are kept in `<data>/cache/vulkan`, keyed by the text, the stage and a translator
version; damaged, stale or other-device files are ignored and replaced. With the cache full a launch
spends 6-8 ms on shaders (sc00, gf1a, sc13) and 8-19 ms on pipelines, and sc00's repeatable
137-140 ms hitch at +0.3 s is gone in two warm runs; gf1a loads in 750 ms instead of 860. Runs
`logs/scene-perf/f4-cold` (old behaviour), `f4-dedup-cold`, `f4-first` (empty cache), `f4-warm`,
`f4-warm2`, `f4-damaged`. The remaining sc00/sc13 hitches in warm runs are F1's random stalls.

### F5. The fence hold spins

In steady play the game thread spends 38% of its samples (1,314 of 3,427 in five seconds of sc00)
inside the fence hold, calling `SwitchToThread` in a loop until the executor catches up. The
executor itself is waiting for vsync for most of that time. On the Mac the spare cores absorb it,
and frame pacing is fine; on the Deck's four cores a spinning thread competes with the executor and
the audio mixer, which is the classic way to turn a held frame into a late one. A wait on an event
the executor signals when it completes a fence would cost nothing. Worth trying if the Deck run has
more late frames or audio running dry than this one. (On the Mac the spin also made the stalls of F1
worse, because each yield dropped the game thread to priority 0; D134 changed the yield, not the spin.
Linux's `sched_yield()` does not depress and the QoS call is Darwin's, so the Deck runs as before.)

### Checked, not faults

- **Files not found during scenes.** Each scene asks for loose copies of its parts first
  (`D:\sc00_03.viv`, `D:\assets\irrad\00_03.irr`, props such as `PROP_COFFEECUP.viv`; 243 names in
  all) and then reads them from the scene's own archive. The same requests are in a Story run that
  reaches sc00, so this is the game, not the scene player.
- **2.5 s of silence after each scene.** The scene ends, the developer module unloads 0.14 s later,
  and the main menu's first call comes about 2 s after that; frames keep coming throughout. A
  transition, not a drop-out.
- **sc07b uploads 366 MB of textures in 66 s.** One or two 256 KB textures are uploaded again in
  22% of frames (the limousine's reflective paint), with no hitch. A candidate only if uploads ever
  show in a profile.
- **The recorder keeps 20 sessions whole** (`[debug] keep_sessions`), so a tour of 38 launches lost
  the first ones' frame rows. Documented behaviour; `scene-perf.py` copies each session's rows into
  its own folder as it goes. `scenarios.py scenes all --record` has the same limit.

### The scene player itself

Besides the 38 launches by `RECOMP_SCENE`, the overlay path was driven with `RECOMP_HOST_SCRIPT`
(F1, the Debug tab, *Play scene*, close): the scene plays, the button is disabled with *Playing
sc00* while it runs, *Back to the main menu* is offered, and after the scene a second *Play scene*
plays it again, each with a clean module load and unload. Two faults in the panel, the first fixed:

- **Fixed:** long tooltips did not wrap. *Play scene*'s ran off the right edge of the window and
  lost its last sentence; so did every long setting description in the launcher and the overlay
  (the picture shape's, for one). `src/hooks/pc_ui.cpp` now wraps them at 35 em.
- **Open:** in a 1280x960 window the Story scenes section is at the bottom of the Debug tab and its
  status line (*Playing sc00*, *The developer module is not running*) is cut off until the tab is
  scrolled. Moving the section above the recorder's settings, or a scene line in the overlay's
  header, would fix it.

## Repeating the run (the Steam Deck)

From the repository on this branch, with the game built and `DEFJAM_DATA` set, nothing else
running, and the display awake (on the Deck, in desktop mode, plugged in):

```
python3 scripts/scene-perf.py run all
python3 scripts/scene-perf.py compare docs/research/scene-perf-macos-2026-10-18.json logs/scene-perf/<the folder it printed>
```

`run all` plays the 38 scenes with the same stress profile (16:9, render scale 3, vsync) and the
recorder on, about 40 minutes, and writes `summary.md` and `summary.json` beside each scene's
contact sheet; `--profile default` uses the built-in 4:3, render-scale-2 settings instead. An
interrupted run resumes when given the same `--out`. `compare` prints the two side by side.
The audio columns need NumPy and the contact sheets Pillow; without them those are left empty.

What to look for on the Deck, against these numbers: late frames above 1% in any scene other than
sc00 (CPU limit; F5 first), loads above 2 s (F2), hitches that repeat at the same moment (content
or first-use compiles, F4), and `dry` above zero in more than a few scenes (the audio work in #30).

Everything under `logs/` holds the game's pictures and sound: keep it on the machine.

## Files

- `scripts/scene-perf.py` (`run`, `summarize`, `compare`, `baseline`) and
  `tests/unit/test_scene_perf.py`.
- `scene-perf-macos-2026-10-18.json`: this run's numbers, the baseline for `compare`.
- Local only, not published: `logs/scene-perf/mac-stress-20261018/` (the tour),
  `mac-stress-repeat-20261018/` (the repeat), `ab-*` and `ab2-*` (the experiments), the run logs they
  name, and the `sample` profiles.

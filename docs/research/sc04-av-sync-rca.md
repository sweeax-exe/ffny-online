# SC04 audio/video synchronization: root cause

Investigation date: October 31, 2026. Tracks [issue #161](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/161), milestone **v0.14.0**.

## Identified cause

The retail cinema renderer has an **SC04-specific minimum-time fallback**. When
the current shot name begins `sc04` and the pending cinema delta is below
1/120 second, it substitutes **1/30 second**, including when the delta is zero.
The recomp can draw a frame on which the simulation has taken zero steps.
That rendered frame therefore invents 33.333 ms of animation time. Audio does
not receive the corresponding jump. Subsequent simulation steps still run,
so the extra animation time accumulates: the picture gets ahead and the sound
appears late.

This is a confirmed video-side drift mechanism. Raw-XBE disassembly, live
cinema/audio counters and an intervention in an isolated RC process agree.
The intervention removed the positive drift with both normal frame-clock
steering and the steering-disabled diagnostic. The subsequent
[source fix and validation](sc04-fix-validation.md) preserve nonzero deltas and
bound the affected scene set. The RC experiments below remain the original
diagnostic record. Their small offsets are not a measured speaker-to-mouth
synchronization result.

## Exact baseline and isolation

- Game source: `5d22e8b9711a0099d58daec83956b5e9340147cc`, the Windows
  v0.13.0 RC used for the issue's report; toolkit
  `d569ca10811cef67ec4e9fa2173088c2f9fd0580`.
- `pipeline-state.py verify-build --preset win-x64-release` passes in that RC
  checkout; its source and toolkit remain clean. An independent copy of its
  unchanged executable ran in the investigation worktree. SHA-256:
  `8649e815237232b23296579e8521e0b715e6f83715f8e399d52ae92e3d96be11`.
- The user's own dump has XBE SHA-256
  `31cc0d11f7c656b1a6f337fdf7d9bbaac9df11c324a2fb881f7eb639d4be00c2`.
- Windows, a 144 Hz display, windowed 1920x1080, 16:9, render scale 2,
  vsync on. At 144 Hz the game's timer paces the approximately 60 Hz frames.
- HD trials use the same ordered four packs as the report: the local Lanczos
  opacity/channels packs, HD Widescreen Menus 1.4.0 and HD Fighters 1.0.0;
  cache 2048 MiB. A separate pressure control uses 32 MiB.
- Each run plays SC04 through `RECOMP_SCENE=sc04` in the developer cinema
  mode. It has its own data/save root, executable copy and logs. Game runs
  are serial; the owner's saves, settings and RC executable are untouched.
  This is direct scene playback, not an instrumented traversal of Story Mode.

SC04 is a real-time 3D scene, loaded from `assets/cinema/sc04.viv`. It is not a
MAD intro movie. The older [movie audio clock](movie-audio-clock.md) report
does not describe its animation clock.

## Source and binary evidence

These are guest addresses, not offsets in the host executable:

| Address or field | Meaning |
|---|---|
| `MEM32(0x002FB19C)` | Cinema player `P`, registry subsystem 0x15 |
| `P+0xC4`, `P+0xCC` | Shot-record array and current shot index; stride 0xCC |
| `P+0x764` | Pending simulation delta, consumed during cinema rendering |
| `P+0x768` | Local shot time; reset at shot/transition boundaries |
| `P+0x758` | Playback selector; normally 1 in these SC04 samples |
| `P+0x978` | Cinema update counter, one per eligible simulation update |
| `0x000766CE` | Start of the minimum-delta comparison in `sub_000766B0` |
| `0x0029D24C` | Float 1/120 used by that comparison |
| `0x0029D008` | `sc04` comparison string |
| `0x00076709` | SC04 fallback writes float 1/30 to `P+0x764` |

`sub_00076CD0 -> sub_00076490` accumulates the supplied simulation delta.
The caller supplies fixed 1/60-second steps, consistent with the
[simulation tick investigation](simulation-tick.md). The render path is
`sub_00077B20 -> sub_000766B0`.

At `0x000766CE`, the renderer compares the pending delta with 1/120. Below
the threshold it calls `sub_00201020(current_shot_record, "sc04", 4)`, a
bounded string comparison. A match causes the 1/30 substitution at
`0x00076709`. It is not a log/assert call. The record starts with its inline
shot name, so all of SC04's matching shots qualify. The later zero-delta
guard would otherwise avoid advancing animation.

With selector 1, `sub_000761A0` advances local shot time. Selectors 0 and 2
use `sub_00076580` and `sub_00075F50` for transitions. `sub_000766B0` clears
the pending delta at `0x00076813` after consumption. The examined animation
path does not synchronize its clock to the audio play cursor.

An independent read-only Capstone pass checked the original instructions,
comparison constants and string helper against the user's XBE. The lifted
code agrees with the original branch: this is not evidence of a mistranslated
floating-point comparison. Frame-clock steering reduces zero-step frames;
it does not make them impossible, especially around loading and phase changes.

## Controlled measurements

Eight complete scene trials are summarized in the accompanying
[numbers-only dataset](sc04-av-sync-2026-10-31.json). The main comparisons:

| Trial | Frame-clock steering | Intervention | Animation minus simulation, first/last shot midpoint | Animation minus audio copied, last shot midpoint |
|---|---|---|---|---|
| `sc04-hd-memory-2` | normal | none | +0.100 / +0.431 s | +0.651 s |
| `sc04-unlocked-baseline` | disabled diagnostic | none | +0.433 / +3.031 s | +3.131 s |
| `sc04-unlocked-zero-guard` | disabled diagnostic | suppress fallback | 0.000 / -0.252 s | -0.374 s |
| `sc04-default-zero-guard` | normal | suppress fallback | 0.000 / -0.219 s | -0.470 s |

Positive values mean animation is ahead. The comparison uses the same RC
executable and HD configuration; host scheduling is not deterministic, so
these are observations, not a promised reproduction rate. The disabled-lock
baseline develops more than 2.5 seconds of additional animation lead between
its first and last shot midpoints. The intervened run has **more** reported
zero-step frames in the whole-launch frame-clock windows yet no positive
accumulation, separating the fallback from merely improving frame pacing.

The intervention changed only the live process's float at `0x0029D24C` from
1/120 to zero before scene playback. This lets the existing zero-delta guard
operate. It did not change a file, executable, audio rate, buffer size or
frame-clock setting. It is a causal experiment and a fix candidate, not a
production patch with regression coverage.

### Measurement definition and limits

- Read-only process samples every approximately 100 ms record shot selection,
  local animation time, the cinema update count and active audio channel
  progress. A separate explicit diagnostic writes the threshold in the two
  intervention trials only.
- Reconstructed animation time sums prior shot lengths
  `(frame_count - 1) / 29.969999313354492` and adds current local shot time.
  The frame count is the active track's getter value at `P+0x58`. The final
  shot's track is torn down before its authored blackout; those blackout
  samples are excluded from animation metrics.
- Simulation time is `P+0x978 / 60`. Audio is the head source node's samples
  **copied into the guest hardware ring**, divided by its channel's 16-bit
  sample rate. SC04 used channel 3, stream handle 3, at 36,000 Hz in these
  samples. The offline audio header independently agrees on that sample rate.
  These counters advance at approximately the expected rate during the
  positive-drift trials.
- Copied audio leads actual speaker output by guest/device buffering. Shot
  boundary handling also affects the reconstructed offset. These values do
  not establish absolute lip sync, and the negative intervention offsets
  must not be advertised as perfect synchronization. They establish removal
  of the accumulating positive video lead.
- The printed frame-clock windows cover the whole launch, including startup;
  their zero-step totals are not a count of SC04 fallback activations.
- `RECOMP_AUDIO_HOST=1` reports zero starvation events, refused submissions
  and underruns in these eight trials. `audio-clicks.py` finds zero clicks
  and zero exact-zero dropouts in all eight device-input WAVs. Those WAVs
  describe submitted sound, not a recording of physical device output.

An early HD trial (`sc04-hd-1`) also showed an approximately 0.955-second
device-played versus host-clock deficit during loading. It did not repeat in
the later clock-sampled HD trials. That is a separately observed timing
anomaly, not the basis for this RCA or a proven HD-pack defect. Low-cache
texture churn alone did not reproduce it. Audio dropout counters alone would
also miss the video-side fallback identified here.

## Reproduction and source follow-up

For a normal local run, use `RECOMP_SCENE=sc04`, the reported HD packs,
`RECOMP_FRAME_CLOCK_TRACE=1`, `RECOMP_AUDIO_HOST=1`, and recording with
`RECOMP_RECORD_FPS=0`. Preserve the entire scene. For an amplifier, repeat
with `RECOMP_FRAME_CLOCK_LOCK=0`; this is a diagnostic setting. Retain the
normal-steering comparison too. The local experiment runner and reduction
script are `logs/run_baseline.py` and `logs/summarize_sync.py` in the
`defjam-sc04-sync` investigation worktree; each run has a receipt, memory
samples, device-input WAV and stderr log under `logs/<trial>/`. Recorder
session IDs are in the dataset. All recordings and raw samples remain local.

The [implemented game-side guard](sc04-fix-validation.md) prevents **zero pending
simulation time from becoming positive animation time for SC04**, while
preserving actual positive deltas and shot/blackout transitions. It does not
change audio speed or buffer sizes. Unlike the live threshold intervention,
it retains the fallback for positive deltas below 1/120.

The follow-up records the native fixture, new source build, normal/stressed
SC04 runs, other-scene controls and quick regression separately. Full regression
is deferred at the owner's request. Before release closure, validate
macOS/Linux and have the owner confirm the later dialogue in a Story-mode
playtest; those results are not inferred from the clock counters.

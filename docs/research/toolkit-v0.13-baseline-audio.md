# v0.13 migration: baseline audio queue failures

Fixture correction discovered later: these two runs used profiles one directory
too high and therefore booted with an empty profile list. The measured queue
events below are real, but these runs do not supply the corrected saved-profile
comparison baseline. Corrected original versus (`regress-versus-20261007-172835`)
passes 26/26 including audio. Keep both sets of observations; do not change gates.

Read-only investigation, 2026-10-07. Baseline parent `e39cefba`, toolkit
`2ac8e705`; candidate toolkit reviewed at `bec01fa2` plus its working tree.
No builds, tests, game runs or live process inspection were performed here.
This report preserves the existing acceptance gates.

## Finding

The 64 combat and 13 versus dry counts are genuine observations of an empty
XAudio2 source queue during active combat, not cumulative counters accidentally
attributed to the scenario window. They do not prove how long the hardware was
silent, or that each count represents a separate audible gap. Their cause is
not established by these logs.

The backend checks `BuffersQueued == 0` before submission and increments
`g_xa2_starved` only after an earlier successful submission. This excludes the
initial empty queue. `mcpx_apu_monitor_frame` prints and clears both queue
counters once every 187 generated 256-sample periods (0.997333 seconds of
generated audio). Thus each logged value is an interval count, not a lifetime
total. These are generated-time reports, not wall-clock one-second reports.

`scripts/test_evidence.py:audio_checks` sums reports after the first matching
FUNCCALL anchor and before the result-screen anchor. The first report can
include up to one generated second from before the anchor, since the counter
does not reset at the anchor. **That uncertainty does not explain these
failures:** the first post-anchor report is zero in both runs; the first dry
counts are in post-anchor report 17 and 41 respectively.

## Phase distribution

| Baseline scenario | Reports in assessed window | Positive dry reports | Dry count | Concentration |
|---|---:|---:|---:|---|
| `regress-combat-20261007-153102` | 213 | 27 | 64 | 63 in reports 17-54; one in report 136 |
| `regress-versus-20261007-153828` | 122 | 10 | 13 | All in reports 41-67 |

These report numbers start at the first APU level line after `Game.StartGame`.
The `match_start` telemetry record follows ten reports in both runs. All
positive counts occur after `match_start`. Combat's result and `match_over`
follow report 210, so its dry counts precede the result and slow-motion finish.
No versus result is recorded in the assessed window.

The nearest preceding/following timestamped TEST-EVENT records put combat's
first dry reports between 3.594 and 13.469 seconds after `match_start`, the end
of its dense burst between 83.922 and 87.219 seconds, and its isolated late
count between 165.735 and 173.188 seconds. Versus's first count falls between
29.875 and 33.438 seconds and its last between 52.313 and 57.875 seconds.
These are event brackets, not exact APU timestamps; log delivery can interleave
threads and APU reports have no wall timestamp.

Combat's raw log includes a pacing report near the early burst:
`60 presents`, maximum interval `5487.63 ms`. The final performance assertion
uses full-rate windows, so its reported worst interval of `310.41 ms` is not
the largest stall in the raw log. In contrast, the pacing reports adjacent to
versus's first dry counts show 120/121 presents with roughly 16.67 ms median
and zero late presents. A whole-game stall therefore cannot explain every
observed empty audio queue.

Combat generates 212.437333 seconds of PCM between host-observed anchor
boundaries spanning 254.790430 wall seconds. This corroborates substantial
audio-production lag somewhere in that window, although the boundaries are
polled and are not exact event alignment. The versus run generates
120.277333 post-anchor PCM seconds; its end is the captured file boundary,
not a timestamped result anchor. Both PCM checks pass clipping and silence;
PCM is written **before** device submission and cannot establish uninterrupted
device playback.

Evidence: each scenario's `report.json` and `game.log.err` under
`logs/scenarios/`. Combat anchor is log line 37296, first dry line 47263,
last dry line 120571, result line 165472 and result-screen line 167566.
Versus anchor is line 38530, first dry line 63512 and last dry line 80405.

## Runtime mechanisms and migration comparison

`src/apu/apu_xaudio2.c` uses three 1,024-sample buffers at 48 kHz (21.333 ms
each), normal source-voice playback and no source-voice callback. Empty queue
observations have no duration, timestamps or HRESULT detail. A failed
`SubmitSourceBuffer` returns without a separate failure counter, so the
existing zero-drop evidence does not exclude an API failure or repeated
observations within one gap.

`src/apu/apu_core.c` throttles every eight 32-sample frames toward a 5,333 us
deadline. A delay beyond one period resets the deadline to the current time;
it does not generate all audio missed during a long pause. The Windows shim
waits with integer-millisecond `SleepConditionVariableCS`. The APU thread
holds the device lock for processing, releases it, calls `SwitchToThread`,
then reacquires it after every frame. Potential delay sources include DSP/VP
work, guest contention on that lock, condition-variable wake latency,
yield/reschedule latency, PCM file writes and device submission.

`apu_shim.h` creates the APU worker with ordinary `CreateThread`; the reviewed
path does not apply an audio-specific priority or MMCSS policy. That makes
scheduler delay a plausible hypothesis, not a conclusion from these logs.

There is **no APU source delta** from upstream `1409a7d` to `b3700e1`, nor from
the current fork `2ac8e705` to candidate `bec01fa2`, nor uncommitted candidate
APU edits at review time. Direct comparisons of `apu_core.c`, `apu_xaudio2.c`,
`apu_shim.h`, `apu_vp.c`, `apu_dsp.c` and `apu_state.h` also match after newline
normalization. These failures already occur on the frozen baseline. Other
candidate kernel/lifter changes could indirectly affect game scheduling, but
they are not the source of these two baseline failures.

## Focused next diagnostic

Use a short repeat of the same baseline fixture and route, with the existing
audio gate and buffer sizes unchanged. Capture a bounded Windows performance
trace containing CPU sampling, context switches/ready-thread events, DPC/ISR
activity and file I/O. Identify the APU worker by thread creation and its
`mcpx_apu_frame_thread` stack. An identical candidate trace can then provide a
controlled comparison.

A small opt-in runtime trace would make that measurement decisive: record
QPC timestamp and thread ID, queue depth, samples played, submission HRESULT,
time since previous submission and generated sample count. For a dry event,
retain recent phase durations for throttle's requested/actual wait, VP/DSP
processing, PCM `fwrite`, post-yield device-lock reacquisition and submission.
Prefer a bounded in-memory ring dumped after the event; per-frame disk logging
would itself affect pacing. This is a proposed diagnostic, not a change made
by this investigation.

Interpretation: long ready-but-not-running time indicates scheduling pressure;
long running processing time indicates production cost; a blocked lock wait
identifies runtime contention; delayed wakeups implicate timer/wait behavior;
file-I/O delay implicates capture; submission failure implicates the backend.
Existing `RECOMP_APU_TRACE` provides coarse frame/voice/notification context,
but lacks these phase timings and cannot alone distinguish the hypotheses.

Keep zero dry submissions as the existing acceptance gate. Retain both failed
baseline attempts and compare matched runs; do not count a retry alone as a
root-cause explanation, relax the threshold or enlarge buffers to conceal the
observations.

# Controller tracing and regression follow-up — 2026-10-30

This records the earlier observational phase. The subsequently authorized [input correction and final results](input-transition-fix-2026-10-30.md) supersede its implementation status and next-step list. The virtual trigger-neutral fixture is now fixed. `sub_001AD380` proved CPU-only; its old human-pad observation was a later checkpoint, not the human cache writer. The final probes distinguish all three equivalent human producer paths. Preserve the earlier numerical evidence with those boundary qualifications.

This follows the [unmodified v0.12.3 investigation](input-latency-2026-10-30.md). The base game is release tag `v0.12.3`, commit `65038d047249c3456fe037cd91d07f8bce81653d`; base toolkit is `be83eb981663f52bb55bfe04b26e1ec2540d775e`. Local observational tracing is now added in isolated game/toolkit worktrees. No changes have been published or released, and normal input settings/behavior have not been changed. Zero-hold runs are diagnostic comparisons.

## What the new trace establishes

The 120 ms minimum hold merges separate rapid taps **after latching, during report construction**. The first press is not held back for 120 ms; its down state is extended, hiding the release and next press. This can feel like a missed button even with smooth rendering. It is distinct from a slow first response and does not establish the cause of every subjective delay.

All runs use the SDL virtual gamepad through normal host mapping, sampler, USB/OHCI, guest driver and fighter input code. `RECOMP_TEST_INPUT` is unset. Ten pairs of 20 ms X pulses are sent 70 ms apart, then ten pairs 170 ms apart, with one second between pair starts and a long gap between phases. Each phase therefore has 20 deliberate presses. Runs are serial, seed 314159, in the same fight with the same disposable data/save isolation. Retained windows finish more than ten seconds before the last metadata checkpoint.

| Stage: X rising edges | Default 120 ms, 70 ms pairs | Default 120 ms, 170 ms pairs | Diagnostic 0 ms, 70 ms pairs | Diagnostic 0 ms, 170 ms pairs |
|---|---:|---:|---:|---:|
| Script / SDL received / host mapped | 20 / 20 / 20 | 20 / 20 / 20 | 20 / 20 / 20 | 20 / 20 / 20 |
| Host read / raw USB sample / latched report | 20 / 20 / 20 | 20 / 20 / 20 | 20 / 20 / 20 | 20 / 20 / 20 |
| Report after minimum hold | **10** | 20 | 20 | 20 |
| Successful USB completion | 10 | 20 | 20 | 20 |
| Fighter pad-table read / fighter held state | 10 / 10 | 20 / 20 | 20 / 20 | 20 / 20 |

The exact loss boundary is observed on both sides of `min_hold`, rather than inferred from attack animation. Disabling hold preserves these 20 ms pulses in these runs, but does not prove arbitrarily short physical pulses will survive the guest's own read cadence. A production correction should preserve both press and release transitions through guest consumption, with bounded age and explicit focus/device policy, rather than simply change 120 to 0.

## Software timing

These are observed boundary delays, not physical-button-to-photon latency. Each phase contains only 20 input presses; p95/max values describe that small sample. Cadences use the many poll/tick records in the same windows. Latency pairs exclude ambiguous or >250 ms matches; the default fast phase has ten explicitly unmatched/merged source presses.

| Measured path | Default, rapid pairs (10 retained) | Default, wider pairs (20) | Zero hold, rapid pairs (20) | Zero hold, wider pairs (20) |
|---|---:|---:|---:|---:|
| Host mapped → fighter pad read, median / p95 ms | 28.57 / 38.36 | 25.79 / 28.18 | 25.59 / 28.80 | 26.66 / 29.29 |
| Host mapped → successful USB completion, median ms | 6.53 | 4.59 | 2.66 | 6.02 |
| USB completion → fighter pad read, median / p95 ms | 21.72 / 30.77 | 20.83 / 22.80 | 21.91 / 24.98 | 20.23 / 21.99 |

Do not sum stage medians to invent a total; the table includes directly matched host-to-fighter measurements. Host means mapped publication, so physical acquisition before that point is outside those numbers. In the default rapid phase, script-setter-to-host median is 1.96 ms; receipt of an SDL button event to mapped publication is about 0.004 ms within the same worker iteration. The latter excludes time before that receipt.

Actual host poll medians are 2.32–2.50 ms across these phases, despite the nominal 2 ms sleep. USB sampler tick medians are 4.52–4.62 ms, despite nominal 4 ms. Default report construction is about 8.02 ms median; fighter pad reads are about 16.67 ms. The slowest host-poll interval in these four short windows is 4.96 ms. These are software cadences, not a physical controller report-rate measurement.

Roughly 20–22 ms after USB completion remains the largest measured segment for typical retained presses. We have **not** established whether that segment comes from XDK servicing, the game's controller cache, or update ordering, and it is not proof of an intentional combat buffer. The fighter routine copies the observed pad state into held fields within microseconds. The next Present ends roughly 14 ms later; that boundary does not prove when an attack or pixel responds.

## Trace integrity and overhead check

The first try-lock queue prototype exposed its own record loss and was replaced with a bounded 8,192-cell MPSC sequence queue. Four-producer and concurrent-shutdown native fixtures verify copied payloads, accounting and drain. The runtime probes perform no file writes; a separate thread batches CSV output. All stages share QPC relative nanoseconds with original frequency/origin retained. A final review added safe budget clamping, 64-bit ongoing attempt/budget counters, checked header writes and initial metadata before producers start. An immediate-termination fixture confirms a reused path cannot retain old successful metadata. These safeguards were added after the measured comparison runs; their native tests and final rebuilt regression are recorded below. [Usage and schema](../input-latency-tracing.md).

The final default checkpoint has 263,603 attempted/accepted/written events. The zero-hold checkpoint has 259,382 attempted/accepted, 259,379 written at the concurrent checkpoint. Both report zero full/budget/payload/I/O losses. The harness kills the process at its deadline, so `closed` is 0 and the final tail is excluded: 1,242 default and 1,505 zero-hold records. These are valid earlier checkpointed test windows, not complete-session captures. The maximum observed enqueue duration is 46.7 µs default and 338.8 µs zero hold; maxima include producer scheduling and are not average tracing cost.

| Fight variant | Full-rate windows | Median Present interval | Late presents | Pacing assertions |
|---|---:|---:|---:|---:|
| Default, tracing on | 29 | 16.67 ms | 5 / 3,483 (0.144%) | 3/3 pass |
| Zero hold, tracing on | 28 | 16.67 ms | 5 / 3,360 (0.149%) | 3/3 pass |
| Default, tracing off control | 28 | 16.67 ms | 6 / 3,359 (0.179%) | 3/3 pass |

Each run reached the fight, reported no crash/watchdog/unresolved call, and restored its guarded save root identically. This checks for a gross tracing regression; one run per condition cannot establish zero overhead or a statistical performance difference. Startup/loading remain in the pacing windows selected by the existing assertions.

## Limits and next targets

- Physical USB/Bluetooth report timing, wireless interference, switch actuation and display scanout are unmeasured. This independent reproduction does not need another owner play-through, but hardware validation will still be needed for a physical latency claim.
- The existing SDL virtual test hook uses joystick trigger axis 0 as neutral; its mapping produces constant half-trigger values (about `0x78`) and constant guest modifiers `0x3000`. X varies as `0x40`. This predates tracing and does not invalidate the observed X edge boundary, but these runs cannot establish unmodified combat-animation response. The analyzer excludes constant modifiers only for isolated-bit calibration. Fix the virtual fixture separately before using it for latency-to-action tests.
- Instrument the guest XDK call and the later game cache handoff to split the remaining USB-to-fighter segment. Bounded source/assembly inspection identifies `XInputGetState` as `sub_0027E075`, called at `0x001EBC74` inside `sub_001EBBE0`. It copies a packet DWORD and a device-context-sized report; do not assume the Windows XInput structure size. Separate routine `sub_001AD380` remaps six DWORDs from controller records at `0x003BFA10`, stride `0xB8`, into the logical pad table. Existing probes observe the later fighter consumption, not those two intervening boundaries.
- Windows currently calls `SDL_PollEvent` on its input worker. Official SDL3 docs specify main-thread event polling; update-only gamepad acquisition is allowed on any thread. Correct ownership as part of a focused input redesign, without attributing the confirmed minimum-hold merging to this separate issue. See the [15-project survey](recomp-controller-input-practices-2026-10-30.md) for pinned source evidence and a proposed fighting-game input design.

## Validation and handoff

The instrumented comparison build passed all seven selected regression gates (`--only unit,m2,m3,m4a,fight,combat,replay`): 654 unit tests passed, 28 skipped and one deselected; three golden checks passed; a 120-second fight had median 120 presents per two seconds (minimum 110); combat passed 27/27 assertions; replay matched all 80 golden records over 1,800 steps. Total selected run: 14 minutes. After the final diagnostic safeguards, all 15 focused buffer/analyzer tests pass. The final rebuilt quick regression also passes **5/5**: 658 unit tests passed, 28 skipped and one deselected; all three golden checks match; the 120-second fight again has median 120 presents per two seconds, minimum 110. This run took 7 minutes (`logs/regress-20261030-154413.txt`). Build identity verification passes. Source hygiene found zero publication-policy failures; source-only dry-run staging includes no game/generated/built/log files, and both worktrees pass `git diff --check`.

D-new: keep tracing opt-in, bounded and observational, with explicit quality counters; do not combine diagnosis with a production change to input holding/polling. The confirmed rapid-repeat defect and remaining USB-to-fighter segment justify a separate input-consumption correction with the acceptance matrix from the survey. Hardware report rate and button-to-photon claims require independent hardware measurement.

Logs and generated/built/game files remain private under ignored paths. Source-only changes remain uncommitted in `C:\Users\Vlad\code\defjam-input-latency` (branch `investigate/input-latency`) and `C:\Users\Vlad\code\xboxrecomp-input-latency` (branch `diag/input-latency`). The game lane uses that isolated toolkit; its upstream gitlink has not been changed to an unpublished commit. Publish the reviewed toolkit source first only when authorized, then update the parent gitlink; do not commit a parent state which requires an unpublished toolkit commit.

Retained evidence: `logs/input-audit/trace-default.csv`, `trace-zero.csv`, their metadata and `.analysis.json` files; `trace-fight-default.json`, `trace-fight-zero.json`, `trace-fight-control.json`, `trace-fights-summary.json`, `trace-fights.log`, `regress-tracing.log`, `analysis-and-buffer-tests.log`, `build-trace-mpsc.log`, `build-trace-hardened.log`, `trace-hardened-tests.log`, and `regress-trace-final-quick.log`. Reproduce analysis with `python scripts/input-latency.py logs/input-audit/trace-default.csv --button x --slot 0 --guest-bit 0x40` (and the zero trace). Earlier `queue-prototype-*` traces intentionally retain the lossy prototype and must not be substituted for final evidence.

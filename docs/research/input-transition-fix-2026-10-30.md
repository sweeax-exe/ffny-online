# Controller transition preservation — 2026-10-30

This is the implementation follow-up to the [measured tracing investigation](input-latency-tracing-2026-10-30.md) and [15-project source survey](recomp-controller-input-practices-2026-10-30.md). It is local work based on released v0.12.3, not a published release. Windows is the measured platform.

## Problem and correction

The released input path detected both fast presses at the host, then its 120 ms minimum hold merged them into one continuous press. Simply setting that hold to zero makes short taps vulnerable between slower downstream updates. The correction retains separate button transitions until a real consumer receives them.

D-new: add an application-opted-in, per-controller FIFO in the toolkit, with explicit USB-success and guest-consumption acknowledgements. The game enables it by default. Generic toolkit behavior remains opt-in, preserving compatibility with titles that have no guest acknowledgement adapter. The game defaults to a 1 ms input-worker deadline; explicit 1/2 ms options permit controlled comparisons. The worker uses a private high-resolution waitable timer on Windows and skips missed deadlines instead of spinning to catch up. SDL worker acquisition uses UpdateGamepads and PeepEvents, whose documented threading policy permits worker use; PollEvent is main-thread-only.

Repeated transitions of the same button stay separate. Independent button changes may coalesce into a chord. USB transfer attempts and failed TDs cannot retire an event. A guest packet must advance and match a successfully delivered snapshot. During fights, a second adapter boundary waits for the mapped human fighter to read the raw gameplay-button state; this covers frames where controller servicing occurs but simulation does not step. Native fighter filtering and action eligibility remain in place. Start/Back and frontend input use successful XDK-copy acknowledgement, so pause/menu input does not require a fighter step.

D-new: bound every queue to 64 transitions and 100 ms of age. Explicit focus/UI/unplug/remapping/USB-reset cancellation clears old commands; overflow/expiry resynchronizes to current state and is separately counted. A forced-poll-preemption fixture verifies that an older sample cannot re-publish input after UI cancellation. The ignore-focus diagnostic option remains respected. Control GET_REPORT reads a current snapshot rather than borrowing a retained transition without a delivery token.

Analog button presence uses Def Jam's `>1` threshold. Pressure changes within the same classification remain current, as do stick axes. Guest Xbox state is a packet DWORD followed by an 18-byte body, not Windows XINPUT_STATE. Actor controller indexes are resolved through the actual pad mapping; they are not assumed to equal actor ordinals. The game transforms A/B and Black/White positions relative to report order. Single-control runtime calibration confirms these gameplay masks:

| Control | Raw game pad-table mask |
|---|---:|
| A / B / X / Y | `0x20` / `0x10` / `0x40` / `0x80` |
| Black / White | `0x200` / `0x100` |
| LT / RT | `0x1000` / `0x2000` |
| D-pad up / down / left / right | `0x1` / `0x2` / `0x4` / `0x8` |

The calibration run delivered **3/3 presses for each of these 12 controls** through host mapping, USB, guest XDK and human fighter read. There were zero retention overflow/expiry events and zero reported trace loss. The input worker consumed 1.9171% of one CPU core over 145.075 seconds with tracing enabled. All three frame-pacing assertions passed; the guarded save root restored identically. Evidence: `logs/input-audit/edges-calibration*` and `calibration-buttons.json`. The sweep deliberately excludes Start/Back; this is not a physical controller or animation-response measurement.

## Timing interpretation

A 1 ms worker target improves acquisition resolution. It cannot make hardware produce reports faster or make a roughly 60 Hz simulation execute an attack every millisecond. Captured taps can survive until a fighter read, subject to the explicit freshness/capacity bounds. Taps entirely between acquisition samples can still be missed. Multiple new actions during recovery need not become multiple immediate attacks; that is native gameplay behavior.

The earlier approximately 26 ms measurement ended at the fighter's input read. It was never evidence of a 26 ms delay until a visible attack. The new XDK probe splits guest API servicing from the remaining game update/read interval. A preliminary probe around `sub_001AD380` was only a later checkpoint for human input, because that function updates CPU actors. A statically identified human producer `sub_0002D9E0` also produced no events in the tested Battle path; it must not be presented as that path's measured writer. The [boundary audit](input-consumption-boundaries.md) records alternate producers and the distinction between static candidates and runtime evidence.

The final-build boundary run confirms **`sub_00079280`** as the active Battle producer: 14,448 after-row observations (four rows per invocation). Each phase preserved 20/20 presses through the actual cache writer, both fighter boundaries and the consumption acknowledgement, for **40/40 final-build presses**. There was zero reported trace loss, expiry or overflow; all roster/liveness, fault, save and pacing checks passed. The harness killed the process after the test; 2,710 uncheckpointed tail rows were excluded, and both complete scripted phases precede that tail.

| Observed segment | 70 ms pairs, median | 170 ms pairs, median |
|---|---:|---:|
| Virtual-script setter → mapped host | 0.9861 ms | 0.9812 ms |
| Mapped host → full USB delivery | 6.2872 ms | 4.8749 ms |
| USB delivery → successful guest XDK read | 11.6333 ms | 13.6776 ms |
| XDK read → human pad-cache update complete | 7.5870 ms | 7.9188 ms |
| Cache update complete → fighter raw read | 0.1281 ms | 0.1338 ms |
| Fighter raw read → post-read held fields | 0.0009 ms | 0.0009 ms |
| Mapped host → fighter raw read, measured directly | 26.0908 ms | 26.7583 ms |

These are separate distributions, so their medians need not add exactly. The XDK query and game-cache writer each run about every 16.7 ms in this capture; USB construction is about every 8.06 ms, host acquisition about every 1.008 ms. The observed pipeline explains the previously unsplit interval and identifies guest/controller servicing plus game-update phase as the remaining software delay. It does not establish a mandatory fixed delay, identify physical controller latency, or prove an attack's visible onset. Evidence: `logs/input-audit/edges-final-boundaries*`.

## Evidence and handoff

### Focused and new-path validation

The final native input/tracing run passed **32/32** tests. Three additional registration-verdict tests subsequently passed, including explicit rejection of a missing fighter press, trace loss and retention expiry. Both SDL fixtures pass (legacy host behavior and consumed-edge integration with the forced UI-cancellation race). The real OHCI walker fixture passes failed-DMA, zero-byte, partial-report, full-retry and periodic-NAK delivery checks. That fixture exposed and verified the correction for committing a full report when zero/partial bytes had moved.

The final build's normal two-player fight through the new host/USB/guest path passed **6/6 combat assertions plus 3/3 pacing assertions**. Both human slots moved and landed hits that caused damage. The roster stayed live, saves restored identically, and the trace reported zero loss, expiry or overflow. The trace checkpoint accepted 976,686 events; its harness-killed final tail of 2,179 rows was excluded. Evidence: `final-input-tests.log`, `analyzer-runner-tests.log`, `input_host*-test.log`, `usb-transfer-test.log` and `edges-final-combat*` under `logs/input-audit/`.

The reusable pulse runner now automatically requires the expected number of presses in both phases at every delivery and consumption stage, including the actual pad-cache writer and fighter acknowledgement. It also requires trace quality and zero retention discards. A healthy frame-rate/run/save verdict alone cannot pass input registration. Applying that stricter check to the final boundary capture passes (`registration-verdicts.json`).

### Standard regression

The final unchanged runtime build also passed all seven selected game gates: M2 frame match (10 s), M3 legal/EA/THX/intro/title match (110 s), M4a main-menu match (58 s), the 120-second fight (201 s total; median 120 presents per two seconds, minimum 114), combat **27/27** assertions (278 s), versus **26/26** assertions (213 s), and replay **80/80 records over 1,800 steps matching the golden hash** (203 s). Runtime evidence is in `logs/regress-20261030-173044.txt` and the linked scenario reports.

The first batch's unit row had three fixture-setup errors alongside 672 passing tests: the standalone input-settings fixture did not link the newly required `input_edges.c` and `host_time.c`. Its build list was corrected without changing runtime behavior. The isolated full unit rerun is recorded separately, so the original batch's honest `7 of 8` result is retained rather than overwritten.

The corrected full unit rerun passes **678 tests**, with 28 skips and one slow-test deselection, in 84.37 seconds (`logs/regress-20261030-173213.txt`). The pytest failure cache is empty. Together with the seven passing runtime gates, all eight selected verification areas are resolved. The final build identity verifies; source-only staging in a temporary index passes publication-policy checks, both worktrees pass whitespace checks, and the real indexes remain unstaged. No changes have been published.

### Sustained comparison

Each run used two SDL virtual pads and two human fighters, with 80 X taps in 70 ms pairs and another 80 in 170 ms pairs. Each tap lasted 20 ms. Both tests passed roster/liveness checks, all three pacing assertions, fault checks and byte-identical save restoration.

| Worker target | Observed poll median / p99, rapid phase | Raw host → fighter rises, rapid / wider | Worker CPU over whole run |
|---|---|---|---:|
| 2 ms | 2.01 / 2.27 ms | 80→80 / 80→80 | 0.9410% of one core, 209.223 s |
| 1 ms | 1.0076 / 1.5342 ms | 80→80 / 80→80 | 2.0227% of one core, 210.115 s |

Both runs preserved **160/160** through host, USB, XDK, fighter acknowledgement, raw fighter read and post-read held state: **320/320 total**. Neither reported retention expiry/overflow or trace loss. Both used a one-million attempted-event budget; their harness-killed traces have `closed:0`. Analysis excludes 1,123 final-tail records at 2 ms and the corresponding uncheckpointed tail at 1 ms; all scripted phases ended before the retained checkpoints. These are complete test windows, not complete-process traces.

Host-mapped → fighter-read medians were 17.83/19.66 ms in the two 2 ms phases and 16.0858/29.3762 ms in the two 1 ms phases. The 1 ms p95 values were 32.4426/32.0268 ms, with no unmatched or ambiguous rising edges. The faster poll did not uniformly reduce full-path latency; relative USB/driver/game scheduling dominates these phase-specific measurements. The acquisition-resolution improvement is directly measured, and its CPU cost is modest on this Windows machine. No hardware or button-to-photon claim follows.

The first long AI-opponent trials were unsuitable for whole-run input counts because the AI could end the match during the second phase. A separate pre-fighter-ACK trial also exposed a press reaching the cache without a corresponding fighter read. Those traces motivated the controlled two-human fixture and consumption boundary; they are not substituted for the passing comparison above. The final native adapter fixture independently verifies retention across API reads without a fighter step.

Evidence: `logs/input-audit/edges-controlled-poll{1,2}.csv`, their metadata and analysis JSON, and the same-name run reports/logs. These comparison runs preceded the final tracing-only addition of the alternate cache writers and the full-transfer USB delivery guard; final-build verification is recorded below.

Focused native tests cover ordered rapid taps, independent chords, delivery/packet guards, packet wrap, analog threshold boundaries, magnitude freshness, queue overflow/expiry/reset, repeated ring use, and the game adapter's wait for the mapped human fighter. SDL fixtures cover two controller slots, neutral trigger setup, snapshot reads, UI/focus cancellation and the forced stale-poll race. The virtual test fixture now initializes trigger axes to SDL's actual neutral value; earlier baseline constant half-trigger modifiers are not present.

Source lanes: `C:\Users\Vlad\code\defjam-input-latency` (`investigate/input-latency`) and `C:\Users\Vlad\code\xboxrecomp-input-latency` (`diag/input-latency`). Builds, CSVs, logs, generated code and game data stay ignored/local. The parent gitlink is unchanged because the toolkit implementation has not been published. Publish source only when authorized, publish the toolkit commit before updating the parent gitlink, and validate other platforms before release.

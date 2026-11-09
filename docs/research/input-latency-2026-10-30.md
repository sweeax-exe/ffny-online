# Controller input investigation — 2026-10-30

Question: can the latest release's logs identify consistent or intermittent controller delay, and can unattended fights reproduce missing presses?

**Result:** current logging cannot measure full controller latency. Independent tests did reproduce loss of rapid repeated press edges in the released input path, including during a 60 fps fight: 20 taps in 70 ms pairs became 10 USB presses with the default 120 ms hold. Wider-spaced taps survived; disabling the hold for diagnostics preserved all 20 fast taps. Standard fight and combat regressions passed. This establishes an input-handling defect and a tracing gap, not a measured constant delay or a diagnosis of the owner's particular controller transport. A new play-through is not required to pursue those findings.

## Release and isolation

GitHub latest was verified as v0.12.3 (published 2026-10-28), tag resolving to source `65038d047249c3456fe037cd91d07f8bce81653d`, toolkit `be83eb981663f52bb55bfe04b26e1ec2540d775e`.
The investigation branch `investigate/input-latency` starts at that tag. Generated guest code was freshly lifted and a separate Windows Release executable built and certified by pipeline-state. Runtime sources are unchanged. The build uses the existing toolkit at the exact release pin and existing pinned dependency sources; it has its own build directory, logs and copied UserData. Original saves and settings are not test targets.

Local evidence: `logs/input-audit/recomp.log`, `build.log`, `hold-results.json`, `fight-hold-120.json`, `fight-hold-0.json`, and the run logs named by the fight reports. These contain private diagnostic evidence and must remain local. Nothing was published.

## What logging currently proves

| Evidence | What it tells us | Limit |
|---|---|---|
| Host input configuration / connection / focus messages | Backend, controller assignment, some state changes | No per-press acquisition timestamp or measured polling jitter |
| `RECOMP_INPUT_DIAG` | Roughly once per second, a sampled keyboard/input-chain status | Too coarse for latency; not a continuous button-edge trace |
| `RECOMP_PAD_REPORT_TRACE=N` | First N changed USB reports, GetTickCount64 timestamp, polls since prior report, digital/A/B/X/Y state | Off by default, capped, no player in printed report, no host acquisition timestamp, no game-read acknowledgement; digital/stick coverage incomplete |
| OHCI periodic summary | Poll/report totals | Average cadence cannot show individual press latency |
| `RECOMP_TEST_OBSERVATIONS=1` / recorder game state | Simulation steps, fighter held/pressed state, attacks and damage, host_ms | Fighter samples every 30 steps normally, 6 with `RECOMP_TEST_PAD_DUMP`; most individual press edges are not emitted |
| Recorder `frames.csv` and game events | Frame interval, presentation/work/capture cost, stalls and combat context | No physical-button-to-host/USB/game/display correlation |
| Combat regression attack assertion | A human attack resolved within 60 simulation steps of an action press, followed by damage | Approximately one second at normal speed; not a low-latency gate |
| Deterministic step-input replay | Game-side simulation consistency | Directly overwrites guest pad table; bypasses host/SDL/USB, unsuitable for controller latency |

Thus existing logs can correlate sluggish periods with frame hitches, and opt-in report tracing can show USB states. They cannot establish a p50/p95/p99 distribution of end-to-end controller latency, prove that every physical press was delivered, or distinguish physical transport delay from host polling, guest scheduling, and game animation rules.

## Confirmed short-tap behavior

The host input worker requests a poll every 2 ms (`tools/xboxrecomp/src/input/input_host.c`, POLL_MS and thread loop). A separate Windows USB sampler requests sampling every 4 ms and OR/max-latches buttons until a report (`tools/xboxrecomp/src/usb/usb_gamepad.c`, sampler_thread/merge_latch). These are requested intervals, not measured timing guarantees.

The report path calls `min_hold`, which defaults to 120 ms (`RECOMP_PAD_MIN_HOLD_MS`). This extends a press level; it does **not** wait 120 ms before first reporting a press. The purpose was to keep quick taps alive until the guest reads them. However, a release and re-press during the hold can become one continuous down state, losing the second rising edge. Multiple presses between USB deliveries can also collapse in the latch.

An isolated native C probe compiled the exact released `min_hold` function, replacing only its clock with a synthetic clock. Reports were evaluated every 5 ms, using 20 ms raw taps. No simulation/game/backend inference is involved in these probe results:

| Hold | Tap onsets | Raw press edges | Reported press edges |
|---|---|---:|---:|
| 120 ms (default) | 0, 70 ms | 2 | 1 |
| 120 ms | 0, 170 ms | 2 | 2 |
| 0 ms (diagnostic) | 0, 70 ms | 2 | 2 |
| 0 ms | 0, 170 ms | 2 | 2 |

Evidence: `logs/input-audit/hold_probe.py` extracts the function, compiles it, and writes `hold-results.json` with the source hash. This proves a reproducible input transformation, not the cause of any particular physical-controller experience. Disabling the hold globally is not yet an accepted fix: it reopens the original risk of short presses being released before the guest reads them.

## Fight measurements

All runs are serial, on the same certified executable. The custom runs use the real host/SDL/USB path, not RECOMP_TEST_INPUT. Each initial custom run performs normal movement/attacks first, followed by ten pairs of 20 ms X taps at 70 ms spacing and ten pairs at 170 ms spacing. Default hold and diagnostic zero hold are separate launches. Physical wireless/USB transport and display scanout are outside this test.

The project's standard gates, `python scripts/regress.py --only fight,combat`, both passed (`logs/regress-20261030-144540.txt`):

- Fight: 120 seconds of fight, median 120 presents per two seconds, minimum 111; 201 seconds including navigation.
- Combat through match result: 27/27 assertions, including all seven combat assertions (player setup, movement, attack, damage, decisive result, results screen); 184 seconds. Report: `logs/scenarios/regress-combat-20261030-144235/report.json`.

These validate normal operation and existing broad timing gates. They do not establish low controller latency or preserve every short-tap edge.

| Custom SDL run | Sampled taps, 70 ms pairs | USB X rising edges | Sampled taps, 170 ms pairs | USB X rising edges |
|---|---:|---:|---:|---:|
| Default 120 ms, first run (stalled) | 20 | 8 | 20 | 19 |
| Diagnostic 0 ms | 20 | 20 | 20 | 20 |
| Default 120 ms, repeat | 20 | 10 | 20 | 20 |

The first custom default run is **not a clean comparative performance result**: combat attack/damage checks failed and its whole session had 438 >=50 ms hitches, p99 239.72 ms, worst 1,281.8 ms. GPU Present took roughly 0.3 ms during the worst stall; the time was before presentation, and executor diagnostics showed waiting for title work. Other background build/analysis activity was observed earlier in the session; causation was not established. This run cannot isolate host input handling from slow game servicing. It remains evidence that the raw script saw all 40 short taps while the delivered state did not preserve all those rising edges.

The diagnostic zero-hold run passed all eight selected custom combat/pacing assertions, including movement, a player hit and its damage. Its 56 full-rate fight windows had median 16.67 ms, 11 late presents among 6,734 (0.163%). The whole session had p95 16.73 ms, p99 16.84 ms, nine >=50 ms hitches and worst 159.6 ms; startup/loading are included in those whole-session numbers. These observations do not make zero hold a safe fix for physical pads, because short pulses can still fall between the guest's own reads.

The default repeat (`logs/run-20261030-144541.log.err`, `logs/input-audit/repeat-default.json`) confirmed the rapid-pair merging independently of the first run's large stalls. It had no crash, watchdog or unresolved call; the save root restored identically. All three selected pacing checks passed: 29 full-rate fight windows, median 16.67 ms, seven late presents among 3,484 (0.201%). The 20 ms raw-script pulses appeared as roughly 125 ms X-down intervals at USB delivery. Both pulses of each 70 ms pair were sampled, but there was no reported up edge between them; the 170 ms pairs had two reported down edges. This is precisely the behavior predicted by the native min_hold probe. It is not necessary to infer it from missed attacks or animation timing.

`logs/input-audit/analyze_taps.py` counts X transitions in the changed-report trace after each isolated phase begins. All script taps in each phase also appear in the log. It measures report-level edge preservation, not animation outcomes or physical latency. No trace cap was reached. All custom harness save guards reported identical restoration; the standard combat scenario additionally used its disposable fixture root.

## Existing play-test evidence

The latest retained marked bundle in `defjam-rc-0.12.2`, `logs/sessions/20261027-145243/bundles/002-key-145614`, was analyzed independently with `bug-bundle.py latest`. Its 1,981 frames had median 16.67 ms, p95 16.71 ms, p99 16.77 ms, worst 22.4 ms and no >=50 ms hitches. It identifies itself as 0.12.2 and predates the latest release; this is contextual evidence only. Its missing input timestamps prevent attributing any missed press to a specific stage, even while frames were smooth.

Its source log (`run-20261027-145237.log.err`) identifies a DualSense through SDL. The removal/re-add messages at lines 12-13 surround launcher shutdown/game input initialization; the last removal at line 224269 is followed by window-close exit. These are lifecycle messages and do not establish an intermittent in-fight controller disconnect.

## Next diagnostic requirement

Before asking for another play-through solely for input latency, add opt-in, bounded, buffered tracing on one monotonic clock: player/device/backend, raw and mapped button edges with sequence IDs, host poll intervals, latch/hold transitions, USB completion, guest pad read and fighter pressed/held observation, simulation step and presentation frame. Keep complete counters after the event budget ends, expose trace drops, and avoid synchronous per-poll disk logging. Analyze acquisition-to-report and report-to-guest percentiles, outlier gaps, missing/merged rising edges, and correlation with frame stalls separately. Physical actuation and display response still require external measurement for true button-to-photon latency.

No normal controller settings were changed and no fix has been shipped from this investigation.

## Historical baseline handoff

The source and toolkit remain the exact released versions (pipeline-state verify-build and source diffs checked after the runs). The investigation report is the only untracked non-ignored change on this branch. Test scripts, generated code, executable, copied data and logs remain private. No changes were pushed or released. The worktree is retained for a potential diagnostic/fix follow-up; do not interpret its test-only zero-hold run as a recommendation to change the owner's settings.

The paragraph above records the state at the end of the baseline investigation. The owner's subsequent request authorized new tracing; see the [tracing follow-up](input-latency-2026-10-30.md#observational-tracing-follow-up-2026-10-30) for the current local changes, precise loss boundary, software timing and validation, and the [15-project survey](recomp-controller-input-practices-2026-10-30.md) for input design research.


## Observational tracing follow-up (2026-10-30)

The follow-up instrumented the same v0.12.3 base without changing normal holding/polling behavior. Serial seed-314159 tests used SDL virtual input through host mapping, sampling, USB/OHCI and guest code, with `RECOMP_TEST_INPUT` unset: ten pairs of 20 ms X taps 70 ms apart, then ten pairs 170 ms apart, one second between pair starts. Every phase had 20 deliberate presses.

| X rising edges | Default 120 ms, rapid / wider | Diagnostic 0 ms, rapid / wider |
|---|---:|---:|
| Script, SDL, mapped host, host read, raw USB sample and latch (each) | 20 / 20 | 20 / 20 |
| Report after minimum hold and successful USB completion (each) | 10 / 20 | 20 / 20 |
| Later pad-table/held observations | 10 / 20 | 20 / 20 |

This places merging **after latching, in `min_hold` report construction**. The first press is reported immediately and its down level is extended; 120 ms is not a first-response delay. Zero hold preserved these pulses but does not guarantee arbitrarily short pulses survive guest cadence. The original virtual fixture also used trigger-axis zero as neutral, producing constant half-triggers near `0x78` / guest modifiers `0x3000`. This did not invalidate the isolated X edge boundary, but it prevents unmodified combat-animation claims. The transition implementation later fixed trigger neutral.

The historical trace labeled its endpoint “fighter pad read.” Subsequent boundary work corrected `sub_001AD380` to CPU/AI-only: its human-pad observation was a later checkpoint, not the human cache writer. Keep these timing values as the earlier observed checkpoint distributions; the final transition report supplies the actual Battle writer and fighter-read measurements.

| Observed path, median / p95 ms | Default rapid (10 retained) | Default wider (20) | Zero rapid (20) | Zero wider (20) |
|---|---:|---:|---:|---:|
| Mapped host → later input checkpoint | 28.57 / 38.36 | 25.79 / 28.18 | 25.59 / 28.80 | 26.66 / 29.29 |
| USB completion → that checkpoint | 21.72 / 30.77 | 20.83 / 22.80 | 21.91 / 24.98 | 20.23 / 21.99 |

Mapped-host → USB medians were 6.53, 4.59, 2.66 and 6.02 ms respectively. Default rapid script-setter → mapped-host median was 1.96 ms; SDL event receipt → publication was about 0.004 ms within one worker iteration, excluding earlier acquisition. Measured host poll medians were 2.32–2.50 ms, USB sampler 4.52–4.62 ms, report construction about 8.02 ms and later pad reads about 16.67 ms; the slowest host-poll interval in those windows was 4.96 ms. These are software cadences, not physical device report rates. Stage medians are separate distributions and must not be summed. Twenty presses per phase cannot establish population tail latency; ambiguous or >250 ms matches were excluded and the default rapid phase explicitly had ten merged/unmatched source presses. The next Present roughly 14 ms later does not identify visible attack onset.

Tracing-on default/zero/control-off fights all passed 3/3 pacing checks: respectively 29/28/28 full-rate windows, 16.67 ms medians, and late presents 5/3,483 (0.144%), 5/3,360 (0.149%) and 6/3,359 (0.179%). Each reached combat without crash/watchdog/unresolved call and restored saves byte-identically. One run per condition detects a gross regression; it cannot prove zero tracing overhead or a statistical speed difference.

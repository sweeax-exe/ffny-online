# v0.13 migration: incomplete baseline repeat streams

Fixture correction discovered later: these captures/streams used profiles one
directory too high; the game saw an empty profile list. The WEAPON popup is a
valid observation of those runs, but a USB dismissal recipe has not been adopted.
Corrected frozen profiles are being rerun first, preserving the golden/step input.

Corrected original replay subsequently passes all 80 records through 1,800 steps
with the unchanged golden hash (`regress-replay-20261007-174946`). No tutorial
dismissal recipe or input changes were needed. The two-run repeat check follows.

Read-only investigation, 2026-10-07. Reviewed original baseline reports and
logs, the stream file, and the collection/telemetry sources. No code changes,
builds, tests, game runs or live process inspection were performed.

## Finding

The supplied reports show **A has 75 records and B has 77**, rather than the
reverse. All 75 records in A match the first 75 normalized records in B.
B adds a sample at step 1681 and a damage event at step 1703. There is no
observed simulation-state mismatch within the shared range. Neither run
establishes the complete step-1800 golden horizon.

Subsequent direct inspection of A's retained 60-second and 110-second PNGs
resolves the cause: **both show the WEAPON tutorial, `(A) CONTINUE`, with the
game clock held at 00:33**. The game is waiting for tutorial confirmation.
Its simulation pauses while rendering continues. The earlier hypothesis of
an unexplained update stall or missing instrumentation is retired. This is a
fixture/UI-driving gap in the baseline repeat recipe, not evidence of an
upstream-induced simulation difference.

## Exact evidence

Sources under `logs/scenarios/`:

- `regress-repeat-a-20261007-164156/report.json` and `game.log.err`.
- `regress-repeat-b-20261007-164156/report.json` and `game.log.err`.
- `regress-repeat-20261007-164156.stream`, which equals A's normalized stream.

| Observation | A | B |
|---|---:|---:|
| Recorded stream records | 75 | 77 |
| Last state sample | 1651 | 1681 |
| Last event/state step observed | 1651 | 1703 (damage) |
| Samples parsed | 56 | 57 |
| Events parsed, including excluded `input_loaded` | 20 | 21 |
| Malformed state/event JSON | 0 | 0 |
| Host-observed StartGame seconds | 180.626663 | 180.559117 |
| Process run seconds | 301.170600 | 301.015435 |
| Observation window after StartGame, approximately | 120.543937 s | 120.456318 s |
| Match-start to last timestamped telemetry | 27.500 s | 28.359 s |
| Last state-sample log line | 60528 | 60916 |
| Later log lines, including final sample line | 39653 | 41179 |

A's stream SHA-256 is
`851799dfeea2384ea462c9fde1d383820b65e4214dcfec2463ba57cf734c01b8`;
B's is `5a97f0b4b70f323997f4ef86b1e4541022ced3dd7f8ea2d9341e5e34836658a2`.
Different hashes here follow directly from unequal lengths, and are not
evidence of a differing overlapping record.

Both use the same certified Release executable hash
`38eb602cb0ee94f9e23556c802dc3abbfc367917fa3871a120a586feb1bf0111`,
the same input-file hash
`31c90e92e7dd4d686a8d1abfd2325824c3cd5e94efbcad2792ce255503a4df90`,
and seed `20261006`. Both reach `Game.StartGame`; fixture/save assertions
pass, and all assertions other than B's stream comparison pass. Both
presentation checks report a median of 120 presents per two seconds.

## Tail completeness and continued rendering

Samples occur at every scheduled step `1 + 30*k`: A covers 1 through 1651;
B covers 1 through 1681. There are no missing scheduled samples within those
ranges. The sampling source uses `STATE_PERIOD=30`, not a wall-clock timer.
There is no later match_start, result, match_over or menu FUNCCALL in either
tail that would account for selecting another match or ending gameplay.

After A's sample at step 1651, no further TEST line appears. Its last independent
health observation is update 1633, 297 ms before that sample. B has hit-resolution,
health-change and damage records at update/step 1703, 359 ms after sample 1681,
then no further TEST line. The sample clocks still indicate active phase 1 and
time_scale 1; they do not indicate a decided match.

All TEST tags combined number only 852 (A) and 856 (B). The budget in
`test_telemetry.c` is 20,000; the recorded samples/events are well below its
capacity. Their JSON is complete and parseable. The old observation module's
individual 10,000-hit/change limits are also well above these logged counts.
There is no evidence of malformed records or exhausted logging budgets.

A's latest sample appears near renderer frame 10516, while its final renderer
summary reaches frame 15239. B's latest sample similarly precedes roughly
4,600 further presented frames. Both still capture the back buffer at the
StartGame-relative 60-second and 110-second checkpoints. The A capture log
lines are 68776 and 93941; B's are 69467 and 95711. Thus the harness retains a
substantial rendering tail after step telemetry ceased; it did not simply
stop reading at sample 1651/1681.

Visual evidence: `regress-repeat-a-20261007-164156/captures/fight-60s.png`
and `fight-110s.png`, both inspected directly after the initial log review.
Their WEAPON popup asks for A to continue and explains that X or Y uses a
weapon held by someone in the crowd. The unchanged 00:33 clock agrees with
the early telemetry tail. No popup FUNCCALL is available in the post-StartGame
log, so another existing menu anchor cannot presently drive its confirmation.

Both runtime-fault assertions say `no detected runtime faults`. Searches find
no `[CRASH]`, `[WATCHDOG]`, unresolved `[ICALL] Failed`, or input-error event.
That proves absence of these detected faults, not continued simulation
progress. The harness kills the process when its normal route condition is
met; no unexpected-exit or fault conclusion should be inferred from that kill.

## Collection and comparison semantics

`scripts/regress.py:check_repeat` requests the `fight` route twice with
`--no-audio --observe-combat --step-input default --rng-seed 20261006`.
`scenario_suite.py` uses the fight route's 420-second upper bound and ends
120 wall seconds after the first StartGame anchor. `harness.LogTail` polls
every two seconds and the final full log is read after process termination.
It does not stop on a fight-step count.

`test_evidence.state_stream` selects the first match with `match_start`, keeps
its event/state records through step 1800, removes host timestamps and
excludes `input_loaded`/`input_error`. It skips malformed JSON but none was
found in these logs. `stream_checks` deliberately treats matching prefixes
with unequal lengths as incomplete coverage; B's report correctly states
that the streams agree but cover different step ranges.

The golden remains **80 records, max_step 1800, seed 20261006**, hash
`66a0593bf098a5431cab169eeaddde85fdfdc2da12f20cdadb63987ee05227ec`.
The absent scheduled samples are A's 1681/1711/1741/1771 and B's
1711/1741/1771. A additionally lacks B's damage at 1703. Those tail counts
are consistent with an incomplete reference-length stream, but the golden
contains only a hash: the missing records' values cannot be verified from it.
Do not update the golden or claim a new simulation difference on this evidence.

## Minimal tutorial-input correction

`scenario_suite.bootstrap_stages` appends `game.startgame(=back@9999` to
close the venue stage's fallback A presses when combat starts. In these
step-input runs there is no subsequent real USB A press to acknowledge a
tutorial. `defjam_test_input`, hooked at the fighter reader `sub_001BAA00`,
writes only the selected fighter's `PAD_TABLE` button word immediately before
that reader consumes it. When the paused tutorial UI runs outside that
fighter-reading path, step-controlled fighter input cannot advance the UI.

A host USB A pulse is therefore the appropriate next controlled action. Start
with one short P1 A press (150-300 ms, followed by release) while the WEAPON
popup is visibly confirmed, with the same disposable fixture, step-input file
and seed. Require simulation to resume and retain the unchanged **80-record,
1800-step golden hash** gate. Do not pre-clear tutorial state in the fixture,
disable tutorials, substitute a different profile or alter the fighter script.

For an unattended repeat, a finite sparse A confirmation schedule around this
popup's observed window is a smaller change than restoring the entire
host-timed combat tail. The first RNG setup precedes match_start by about
16 seconds, and the pause appears about 28 fight seconds later; this suggests
a window around 44 seconds after StartGame, but the StartGame observation has
a two-second polling uncertainty. An isolated bounded schedule around that
window is a diagnostic recipe, not proof of generalized popup handling. Stop
extra confirmation pulses after the tutorial is dismissed/full horizon is
collected rather than continuously sending A for the rest of gameplay.

The native step-input fixture demonstrates that a host-populated fighter
button word is overwritten before the scripted fighter reader, while other
slots/words remain untouched. It does not prove that every other UI/camera
consumer ignores host A. Thus any host-confirmation recipe must reproduce the
full existing golden; if it changes the stream, use a narrower guard based on
a verified tutorial-active state or positively observed popup. A stalled step
counter plus continued presents alone is not a sufficient popup guard.
No stable tutorial-popup address or callable popup anchor was established in
this bounded review; Story tutorial-task flags describe another mechanism and
must not be assumed to guard the WEAPON hint.

The historical acceptance record in `docs/worklog/2026-10.md` reports the
80-record stream reproduced on Release/Debug and two fixtures, with seed/input
pinning as documented in `fight-determinism.md` and `combat-telemetry.md`.
Those records do not establish the tutorial flags of the new frozen v0.4
four-ID fixture. Keep that original fixture and resolve its UI confirmation,
instead of manufacturing a fixture that avoids the failure.

A future explicit coverage guard should select the same first match as
`state_stream`, accept only complete valid TEST-STATE sample JSON with integer
match/step, and require a sample beyond step 1800 (normally 1801). It must not
let another match, pre-match RNG records or malformed lines establish coverage.
That guard can classify incomplete runs; it does not replace tutorial driving
or the original stream count/hash acceptance gate.

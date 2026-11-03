# Optional late controller report experiment — 2026-10-30

> Historical evidence: The optional refresh shipped in v0.13.0 and remains off by default; the old v0.12.4 release label was superseded.
> Current support/qualifications live in the versioned release notes and GitHub Issues.

The original controlled comparison used the v0.12.3 baseline and toolkit revisions
in the transition report. Late refresh remains optional and disabled by default.

Implementation: refresh the report at the controller manager's existing per-device update,
instead of re-running the title's device poller or writing its fighter pad table.
`RECOMP_INPUT_LATE_REFRESH=1` enables it; `shadow` checks the conversion only;
`0` and an unset option preserve the normal queued-report path.

`DD930` snapshots held buttons into previous buttons, invokes `DC970` for each
connected Xbox controller, and then runs `DCE00`, `DD770` and the normal filters.
The optional wrapper first lets `DC970` supply its normal fallback report. During
a live fight it reads that port's existing XDK handle using `0027E075`, on the same
guest thread with private guest-stack scratch. It reconstructs only the raw held
fields read by `D7BF0`, then calls the original `D7BF0` conversion against that
private record. The existing manager update, mappings, thresholds, repeats,
fighter restrictions and simulation frequency remain authoritative.

The raw conversion uses the game's constants and x87 precision model. Shadow
observations compare all 26 bytes read by `D7BF0` against the original poller's
record on its own thread. The original converter is reused for the full 0x94-byte
report. No raw one-poll edge latch, global poller record, queued clock record or
USB delivery token is overwritten. Equal API packets can supply the same held
report without advancing controller history a second time.

The poller, initialization and teardown share a native lifecycle lock with the
optional read. The revised experiment **tries** both the existing dispatch
exclusion and that lifecycle lock, keeping the normal report on contention.
The scope does not itself change IRQL or drain DPCs; the XDK API's own raise
recursively enters the already acquired exclusion. API failure, foreign report
length, invalid/disconnected port,
frontend and disabled host-consumption mode also keep the original report. Guest
integer/x87 state is restored around the injected integer/x87 callees.

D-new: bind delayed fighter acknowledgements to the input queue's generation.
Reset, expiration and overflow invalidate the previous generation; the host
checks generation atomically with consumption. The adapter captures generation
before the XDK copy and checks it afterward, rejects older pending packets within
one generation, and drops pending acknowledgements invalidated before the fighter
read. This closes the reviewed reset → same-button-redown stale-ACK case. Queue
generations also carry forward across host-input Stop/Start instead of restarting
at one; the current startup-only restart was not a reachable gameplay bug, but
the public lifecycle API now preserves this invariant.

## Validation record

- `logs/input-audit/late-unit.txt`: 27 focused tests pass, including the real late
  adapter's register restoration, private scratch, API-error fallback, foreign
  layout, frontend/port guards and contended lifecycle lock. The actual guest ACK
  fixture covers generation invalidation and older completion rejection.
- `late-shadow`: 16/16 scripted presses reached all required stages; save guard,
  roster and pacing checks passed. More than 16,000 in-progress raw comparisons
  across two ports matched exactly; final totals and additional axis coverage
  are recorded below after measurement.

The initial enabled pilot preserved 40/40 taps with 17,805 matching raw shadow
comparisons and 5,522 applied reports. Its host → fighter-read medians were
18.74/17.95 ms in rapid/wider phases. A subsequent baseline had 15.99/16.68 ms
medians but 31.23/30.69 ms p95: initial frame alignment materially affects the
typical delay, so the earlier 26 ms number cannot stand in for every baseline.

The first long enabled trial (`late-ab-b1`) is a **failed candidate run**: rapid
40/40 reached the fighter, wider 37/40, with 12 transitions expiring. Trace loss,
conversion mismatches, API errors and lifecycle-contention fallbacks were zero.
Boundary-aware analysis found a 477.24 ms game-step gap at 158.455–158.933 s;
token 167 was delivered at 158.642 s and expired at 158.743 s during that gap.
Host polling, USB sampling and worker API reads continued. A few fast catch-up
steps inside a narrow window initially hid the gap; including neighboring
events establishes it. The capture does not prove which wait caused the gap.
It must not be counted as a successful latency comparison or hidden by extending
the retention timeout. The batch stopped on this failure.

The revision makes dispatch acquisition opportunistic and traces the **entire**
refresh, including acquisition/release, instead of only the inner API copy.
The pulse verdict now rejects game-step gaps over 100 ms in its unpaused test
windows, in addition to requiring every press and zero retention discards. The
general pacing check judges full-rate windows; that alone was insufficient to
reject the slow windows at the end of the failed run.

`late-native-irql.txt`: both native gate-off/gate-on kernel fixtures pass,
including immediate failure under contention, recursive acquisition, unchanged
IRQL and nested API raise/lower. `late-host-edges-restart.txt`: native SDL queue
reset/generation and restart checks pass. `late-opportunistic-unit.txt`: 19
focused tests passed before adding further analysis/fixture coverage.

## Revised phase-swept comparison

The identical executable ran `late-phase-a` with refresh off and `late-phase-b`
with refresh on. Each used 20 pairs in each phase (40 taps per phase), 20 ms taps,
1 ms host acquisition, and a 7 ms offset added per pair to move arrivals through
different positions within a 16.67 ms frame. The two phases used 70 ms / 170 ms
between the paired taps. Both preserved 80/80 through all required stages, with
no ambiguous or censored matches, retention discards, or reported trace loss.
Both save guards, human rosters, liveness and pacing checks passed.

| Host-mapped press → fighter's raw input read | Refresh off | Refresh on |
|---|---:|---:|
| Rapid phase median | 21.1482 ms | 13.8287 ms |
| Rapid phase p95 | 30.1854 ms | 21.6183 ms |
| Wider phase median | 22.1398 ms | 13.1013 ms |
| Wider phase p95 | 29.9876 ms | 21.2333 ms |
| Worst observed press, both phases | 32.7619 ms | 24.8416 ms |

The complete enabled refresh was measured 7,852 times: median 0.0016 ms,
p99 0.0036 ms, maximum 0.4827 ms; one dispatch-contention fallback, no lifecycle
fallbacks or API errors, and no unmatched begin/end boundaries. Its 20,161 raw
shadow comparisons matched. The applied report reached the pad cache in
0.0209 / 0.0228 ms medians. This is distinct from the earliest worker XDK read,
which can still precede that cache update by roughly half a frame.

Maximum game-step gaps in the two enabled test phases were 20.9813 / 23.6287 ms;
the off phases were 19.3030 / 21.7837 ms. These runs do not reproduce the initial
candidate's long pause. They establish a local software-path improvement, not
that a rare stall is impossible or that its earlier cause was conclusively found.

The harness terminates the process, so traces have `closed:0`; the analyzer
excludes the uncheckpointed tail. All input-test windows finished before the
retained checkpoints. These are complete measured test windows, not complete
process captures. With 40 presses per phase, p95 and observed maxima describe
these samples; they cannot establish a rare-event population p99.

The reverse-order repeat (`late-phase-b2`, then `late-phase-a2`) preserved another
160/160 presses. Across A/B/B/A, all **320/320** presses reached the fighter's raw
read and post-update held state; both enabled runs together preserved 160/160.
All four runs used the same executable, with only the refresh flag changing.

| Reverse-order repeat | Refresh on (`b2`) | Refresh off (`a2`) |
|---|---:|---:|
| Rapid phase median | 13.8531 ms | 22.0149 ms |
| Rapid phase p95 | 23.3603 ms | 31.4153 ms |
| Wider phase median | 13.9286 ms | 23.2272 ms |
| Wider phase p95 | 22.4413 ms | 31.5171 ms |
| Worst observed press, both phases | 24.5819 ms | 33.8740 ms |

The second enabled run applied 7,860 reports, with no dispatch/lifecycle fallbacks
or API errors. All 20,162 shadow comparisons matched. Complete refresh median was
0.0016 ms, p99 0.0034 ms, maximum 0.5616 ms; phase game-step gaps peaked at
22.5694 / 18.9818 ms. The repeated improvement is approximately 8–9 ms in the
typical host-sample-to-fighter-read delay. It does not imply 1,000 game updates
per second: the game still advances at its original simulation rate.

`late-buttons` verified three presses of each of A/B/X/Y, Black/White, both
triggers and all four D-pad directions: **36/36**, independently counted at ten
stages through SDL scripting, host mapping, USB, XDK, refreshed report, pad cache,
queue consumption, fighter acknowledgement/read and post-update held state.
Its 18,476 raw shadow comparisons matched, with no retention discards or trace
loss. `late-combat` exercised two SDL virtual controllers with refresh enabled;
both human fighters walked and landed damaging hits, all combat assertions and
the complete-save-root guard passed.

The first `late-axes` run is **invalid as stick coverage**: the seconds-script
parser logged the eight stick commands but did not recognize or apply them.
Independent report-range analysis caught four neutral axes. This is a harness
limitation, not evidence of lost physical stick input. Corrected routing and a
fresh runtime stick check are required before counting that coverage.

The standard regression finished **8/8** (`logs/regress-20261030-202727.txt`):
684 unit tests pass (28 skips, one slow-test deselection); M2, M3 and M4a frame
references match; the fight ran 120 seconds at median 120 presents per two
seconds; combat passed 27/27 assertions; versus passed 26/26; replay's 80
records over 1,800 steps match the existing golden hash. The standard scenarios
use their ordinary scripted backend; the separately recorded SDL combat test
above exercises the active late path.

The seconds-script correction adds all eight stick directions in the host's
control order and signed report offsets, and logs a command as sampled only
after recognizing it. All three native USB fixtures pass (`late-usb-axis-tests.txt`):
existing transfers/buttons/delivery guards, signed stick reports, and the actual
SDL → host → USB stick path across four slots. The final executable was rebuilt
after this harness-support-only change; the input retention and late-refresh
implementation measured in A/B/B/A is unchanged.

`late-axes-fixed` passed the in-game sweep. Applied XDK reports covered all four
axes: LX/RX ranged −32767..32767; LY/RY ranged −32767..32766 (SDL's Y inversion
accounts for the one-unit endpoint). All raw-conversion shadow comparisons
matched, with zero retention discards or reported trace loss. The independent
analysis requires both signs above 20,000 on every axis, so neutral sticks cannot
pass it. The 36-button calibration still passes its independent stage counts.
Combined evidence is `logs/input-audit/late-final-evidence.json`; the earlier
invalid-axis evidence remains in `late-final-evidence-before-axis-fix.json`.

The corrected stick run applied 4,022 reports and matched 16,301 raw shadow
comparisons. Its complete refresh maximum was 0.2301 ms, with no fallbacks,
API errors or unmatched refresh boundaries.

`late-active-replay` explicitly enables the SDL host and late refresh while
feeding the same seeded, step-timed fighter commands as the pinned replay.
**80/80 records over 1,800 steps match the existing golden hash**
`66a0593bf098a5431cab169eeaddde85fdfdc2da12f20cdadb63987ee05227ec`.
The trace proves that the new path executed: 2,732 applied reports and 8,430
matching shadows, with no API errors or reported trace loss. The save guard and
fault checks pass. Step input establishes simulation preservation; independent
SDL pulse tests establish button registration. Neither substitutes for the other.

## Historical build and physical scope

Final Windows executable SHA256 `1c9edf8777e177721c4209c9a3549ab9d3b9e773f9b707db1fae32d8165ddf5a`;
A/B/B/A executable `9a02472363065d834f2e9a0b7d879d0f30796cbd103a7e26b0dffe9b097c7e12`.
Only seconds-script stick support changed between them. No physical switch-to-display
or visible attack-start delay was measured. The current tracing guide explains
the retained physical-controller helper; old local-playtest scheduling is retired.

## PR submission — 2026-10-31

Toolkit source is published at `b7db8c1a110711b42e6a7e69121e008ee0c4f8be`
([fork PR #7](https://github.com/vyanhursky/xboxrecomp/pull/7)), based on the
v0.12.3 toolkit pin. The parent records that exact published gitlink. The release
toolkit branch must combine it with other queued toolkit topics before candidate
validation. No upstream PR is part of this submission.

The game work later shipped in v0.13.0; the old v0.12.4/draft label is historical. The additional required `two-matches` input-scope
regression is recorded in the PR's validation table. The MSVC CI now explicitly runs the
new retention/adapter/trace fixtures and the standalone edge/SDL fixtures; the
USB matrix entry enables SDL so its virtual-stick branch also executes.

Source reviews: [call chain](late-input-refresh-2026-10-30.md#static-review-rationale-and-implemented-guards),
[design review](late-input-refresh-2026-10-30.md#static-review-rationale-and-implemented-guards). Baseline behavior and prior test results:
[transition correction](input-transition-fix-2026-10-30.md).


## Static review rationale and implemented guards

These are read-only source/disassembly findings, not additional runtime tests. The earlier design review rejected a direct native-host snapshot: `CurrentState` has neither a queued-edge token nor successful USB-delivery proof, and `PeekState` alone does not mark delivery. GET_REPORT deliberately stays snapshot-only. It also rejected re-running `001EBBE0`: that poller opens/closes handles and rewrites current/previous digital history, so reading the same report twice can erase a one-poll rising edge. Re-running `DD930` would likewise advance previous/current/repeat and axis filters twice. The implemented per-device callback replacement instead leaves the authoritative manager transition/conversion path running once and never fabricates packet IDs or PAD_TABLE values.

Static mapping verified the converter's nonidentity order: analog LT, White, RT, Black, A, B, X, Y; digital Start, Back, stick clicks, D-pad up/down/left/right. Eight analog entries carry flag 1 and game-scaled floats; eight digital entries carry 0/2 values with flag zero; four axes occupy `+0x80..+0x8C`. Existing XDK normalization and the later device deadzone/inversion/filter must not be applied twice. This field-shape review was not byte-for-byte proof for every device configuration; the measured shadow/button/axis results above provide the tested scope.

The injected `D7BF0` converter and `0027E075` getter each take two guest arguments and return with `ret 8`. Scratch is separate from their downward-growing frames: 22-byte state at `[ESP-0x80, ESP-0x6a)`, raw record `[ESP-0x120, ESP-0x90)`, injected stack below `ESP-0x180`. The reviewed callees use scalar x87/integer state and no modeled XMM operations; the outer wrapper restores integer registers, flags, direction and x87 state. Dispatch safe points preserve the full modeled register set, including XMM/MMX. The runtime-resolved target behind `0x28525C` was not followed, so static review alone does not certify arbitrary stack bounds or every indirect target.

The revised lock review found no concrete lock-order/lifetime or register-restoration defect: refresh tries dispatch exclusion without changing IRQL, then tries lifecycle without waiting. Lifecycle contention releases dispatch immediately and keeps the original report; successful scopes release lifecycle before dispatch. The nested XDK raise/lower recursively uses the exclusion. This superseded the initial lock-order concern; supplied fixtures were not rerun by the reviewer.

The design review identified a concrete stale-ACK risk: resetting the toolkit queue cleared its packet baseline while a game-side pending record survived, allowing a same-button post-reset delivery to match old pending state. Implemented generation checks now bind pending records to their host queue generation, validate around the XDK copy and atomically at consumption, reject older packets, and carry generations across host Stop/Start. The hypothesized asynchronous old-completion race was not established for the synchronous nested getter; serial/generation guards also protect future yielding changes. These are implemented protections, not outstanding review gates. Static uncertainty about whether a candidate runs in Battle must remain separate from the measured Battle evidence above.

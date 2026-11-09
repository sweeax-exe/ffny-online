# Test policy and acceptance

The [harness guide](09-testing-harness.md) describes runnable checks and what each
proves. [Releasing](releasing.md) owns the release-train process; the
[release playbook](agent/release.md) is its action checklist. Remaining coverage
work belongs in [GitHub Issues](https://github.com/vyanhursky/DefJamFFNY-recomp/issues).

## Evidence layers

| Layer | Evidence | Limits |
|---|---|---|
| Source hygiene | `python scripts/check-source-tree.py`; pinned media/branding; no game bytes, generated guest code or builds | Current-tree policy is separate from an audit of Git history. |
| Unit and native fixtures | `python -m pytest -q tests/unit`; registered toolkit CMake/CTest projects | Report platform/compiler/skips; an empty suite is not a pass. |
| Build | Hosted runtime/game-source compile checks and game-free Setup; local complete game build | CI has no dump and cannot play the game. |
| Smoke | `tests/smoke/boot-smoke.ps1` stages and watchdog diagnostics | Presentation without a crash does not prove gameplay or a full campaign. |
| Golden/reference | Stock still-frame signatures and local approved visual manifests | Port regressions and console-fidelity comparisons are different claims. |
| Gameplay | Read-only state/event assertions, ordered routes, fixture identity and save restoration | A requested input/match is distinct from observed consumption/identity/result. |
| Audio/performance | Generated/output audio, frame timing and OS-specific memory samples | Counters are not full fidelity or button-to-photon evidence; Windows private bytes and POSIX RSS are different metrics. |
| Manual acceptance | Physical controllers, audible output, visuals and representative play | Record exact build, platform, settings and tested scope. |

## Checks for a change

Run unit tests and source hygiene for documentation/tools changes. Runtime,
settings, input and UI changes also run `regress.py --quick` and the affected
`--only` checks on the target platform. A toolkit migration gets one full local
run before queueing. The full regression runs once per platform on the combined
release candidate, not again for every queued PR. See [Contributing](../CONTRIBUTING.md#testing).

For Setup, retain the [update acceptance matrix](setup-update-test-plan.md),
game-free packaging/integrity checks and real install/Clean Update acceptance.
Synthetic packages prove recovery policy without publishing fictitious releases.
CI cannot substitute for owner-dump installation or physical-machine playtesting.

## Acceptance and failure records

- Run games serially and use disposable fixtures. Guard the complete save root,
  including its original absence; unequal restoration fails and retains recovery data.
- Name source/toolkit/build, platform, settings, fixture, assertions and logs.
  Missing required probes or checkpoints is blocked, never a pass.
- Preserve raw failures, skips, retries and omitted gates. A later targeted pass
  does not make the first full aggregate all-green. Record an owner's explicit
  release exception alongside the outstanding issue; never silently relax a golden
  or threshold to obtain a pass.
- Keep original-image compatibility separate from pack-on artwork/runtime/platform
  acceptance. Use identical controlled settings for comparisons and label startup,
  loading and active-play measurements separately.
- A regression a scripted route can demonstrate should gain a focused check.
  Health/movement/damage counters alone do not prove a particular move, block,
  sound-to-event alignment or complete Story coverage.
- Captures/audio/save bytes remain local. Run-log names in retained reports and
  PROGRESS protect evidence through `scripts/prune-logs.py`; migrate citations
  before removing superseded documents.

## Historical bring-up milestones and references

The original M1–M6 gates describe September–October2026 bring-up, not a current
roadmap. Their [full definitions at the pre-cleanup revision](https://github.com/vyanhursky/DefJamFFNY-recomp/blob/2e6648cf29073db24147cf8188ce99efdf315d64/docs/02-test-plan.md#milestone-gates)
and [original milestone order](https://github.com/vyanhursky/DefJamFFNY-recomp/blob/2e6648cf29073db24147cf8188ce99efdf315d64/docs/01-plan-review.md)
remain available for interpreting landed decision IDs. Actual acceptance and
exceptions stay in PROGRESS and the versioned release evidence; completing a
milestone never proves untested modes or platforms.

The original September19 xemu baseline reached the loading screen and froze at a
PTimer limitation. It supplied an early rendering reference, not a current survey
of emulator compatibility. Later title/menu/fight port goldens are regression
baselines; do not relabel them console-certified or claim every emulator is still
blocked without checking fresh evidence. The game files needed by any reference
remain the player's own and are never stored here.

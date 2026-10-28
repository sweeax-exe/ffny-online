# Gameplay testing and upstream upgrade roadmap

Requested by Vlad on 2026-10-05. Deferred work, not implemented acceptance gates.
This plan does not change current milestone definitions or toolkit pins.

## Goal

After a gameplay-affecting change, run repeatable scenarios that assert controls,
combat outcomes, rendered output and audio behavior, and produce reviewable
evidence. Extend the existing harness and regression runner.

## Current baseline

Public Def Jam source reviewed at `2ae6032`, toolkit `aa1a1b9`.
`regress.py` has nine checks: project units, three static-screen signatures,
fight, intro, crib, gym and boot soak. Quick mode has five checks. The fight
checks entry, sustained presentation and median presentation rate, not damage
or KO. Unlock is a harness route outside the default suite.

`frame-signature.py` checks dimensions, nonblack bounds and pixel count; it
does not compare recorded hashes or distinct-colour counts. Gamma is disabled
for these goldens. Audio diagnostics exist but are not regression verdicts.
Hosted CI has no game dump and does not run the full game.

## Ordered implementation To-Do

Status as of v0.2.4 (2026-10-06): `[x]` done, `[~]` partly done, `[ ]` open. How to run what exists: [09-testing-harness.md](09-testing-harness.md).

### 1. Repeatable setup and useful combat assertions

- [x] Define disposable, versioned local profile fixtures with hashes and required
  progression; select profiles explicitly instead of relying on list ordering.
  *Done: hashed fixtures run on a throw-away copy; the regression makes one from the saved profiles. Profiles are still chosen by list position.*
- [x] Record settings and RNG seed where controllable; identify sources of timing
  variance. Keep log-anchored menu navigation; add simulation-frame timing for combat.
  *Done for the fight: step-timed input and pinned seeds (the match generator and the CPU fighters' own) reproduce thirty seconds of game state exactly; one longer pair parted after 52 seconds, cause unknown. Menus are still on the host's clock.*
- [x] Add read-only state/event observations for fighter position, health, round
  state and result. Verify hooks are present in actual generated integration.
  *Done: `src/hooks/test_telemetry.c`, confirmed against live fights (`research/combat-telemetry.md`).*
- [~] Add one scenario: fixed fighters/venue, move, block, attack, verify damage,
  complete KO, reach results and return to menu. Bound each expected transition.
  *Part: `fight-result` asserts movement, attack, damage, the result and the summary screen. Not asserted: a block, the fighters' identity, the return to the menu.*
- [x] Prove a deliberately broken input/damage path fails the relevant assertion.
  *Done: a live run whose input file never presses anything failed movement, attack and damage; unit tests break each assertion with altered event streams.*

Exit: a fight that renders but ignores controls or never causes damage cannot pass.

### 2. Visual regression

- [~] Select fight and cutscene checkpoints anchored to game state or simulation
  frames, with warm-up and tolerances documented.
  *Part: `--step-shots` captures at a fight step; cutscene captures are still at host-time offsets.*
- [x] Compare local approved baselines using pixel/perceptual metrics and masks
  for legitimately variable regions. Produce baseline/current/difference images.
  *Done for still screens: crib, Learn Moves and the move preview have reviewed local baselines with measured tolerances and masks (`regress.py --only visual`). Fights: fighters, HUD and arena repeat pixel for pixel but the two crowds do not (`docs/research/crowd-nondeterminism.md`), so whole-frame fight baselines are not usable. Since 2026-10-14: crowd test controls (`RECOMP_TEST_CROWD`) and masks learned from repeated runs (`scripts/crowd-lab.py`), both unmeasured on the game (`docs/research/scenario-launching.md`).*
- [ ] Cover normal gamma, fighters, arena, lighting, transparency, effects and FMV.
- [ ] Keep console-fidelity references distinct from last-known-good port images.
- [ ] Prove representative missing textures/effects fail without making healthy
  animation or driver variation fail routinely.

Captures and reference pixels stay local under current source publication policy.
Any public media exception requires the existing owner review process.

### 3. Audio regression

- [~] Capture and distinguish generated PCM from accepted/output audio. Align
  observations to scenario events; do not infer wall-clock alignment from PCM length.
  *Part: generated PCM is captured and bounded by observed events; it is not the device's output and alignment is to the log polling interval. With the play-test recorder (`--record`) every sample is placed on the frames' clock through `pcm_bytes`, and `bug-bundle.py` finds gaps, clicks and clipping in time.*
- [x] Gate unexpected silence, clipping, dropped buffers and dry queues with
  separately defined startup/loading and active-game budgets.
  *Done for fights, with the drop allowance calibrated on healthy runs. No separate loading budget.*
- [ ] Add event-aligned music, announcer and hit-SFX checks; test channel balance,
  playback speed and audio/video timing where measurable.
  *Open.*
- [ ] Keep a short listening checklist for fidelity not covered by metrics.
- [ ] Register appropriate synthetic mixer/decoder fixtures in CI after auditing
  their prerequisites and assertion behavior in optimized builds.

### 4. Coverage and performance

- [~] Add grapple/combo, two-player input, representative venues/fight modes,
  Story progression, save/load, cutscene natural/skip and unlock navigation scenarios.
  *Part: One on One, Free For All (to its result, with eliminations asserted), two pads, two matches in one launch, the Terrordome, crib, gym, intro and unlock. Open: grapples as such, save and load, cutscene skipping. Since 2026-10-14: a fixture library and generated venue and fighter routes (`scripts/scenarios.py`), and front-end command injection for cutscenes (command not yet found).*
- [~] Measure frame-time percentiles/hitches, memory growth over repeated matches,
  long sessions and simulation speed across supported FPS settings.
  *Part: frame pacing and memory growth are asserted for one fight. Open: repeated matches, long sessions, other frame rates.*
- [x] Keep fast PR checks separate from extended nightly/release checks.
  *Done: `regress.py --quick`, the full run, and `--only` for the rest.*

### 5. Reports and orchestration

- [x] Write JSON/JUnit results and a local HTML report with explicit pass/fail/skip,
  prerequisite failures, assertions, logs, captures and comparison metrics.
  *Done.*
- [~] Include game/toolkit source SHAs, upstream base/tag, generated-input/output
  hashes, executable hash, compiler, machine/GPU/driver, settings and fixture hash.
  *Part: source, toolkit pin, build and executable hashes, settings, fixture, OS and GPU driver. Open: compiler version, upstream base.*
- [x] Preserve complete save restoration and fail closed on missing checkpoints.
  *Done.*
- [ ] Label historical failures and skips explicitly; compare candidate and baseline
  under the same environment and fixtures.

## Testing an upstream xboxrecomp release across three games

### Dependency assessment (public pins, fetched 2026-10-05)

Upstream main: `1409a7d`; latest fetched release tag: `v0.12.0`.
Treat `v0.13.0` as a future candidate until a tag is verified.

| Game | Integration | Upgrade implication |
| --- | --- | --- |
| Def Jam | `vyanhursky/xboxrecomp` gitlink `aa1a1b9`; upstream base `1409a7d`, 15 fork commits | Rebase/merge required topics onto release candidate; retire upstream-equivalent changes only with behavioral evidence. |
| TimeSplitters 2 | `phobos665/xboxrecomp` gitlink `f6eac56`; common ancestor `3706cef` dated September 12 | Substantial integration work; raw divergence is 305 upstream-only / 143 fork-only commits. Patch equivalence is not established by these counts. |
| Mercenaries | Customized runtime/recompiler in repository, port defaults `XBOXRECOMP_DIR` to repository root | No common Git ancestor found in fetched published histories. Selective source migration or explicit dependency extraction, not an ordinary gitlink bump. |

Def Jam's fork is 61 commits beyond v0.12.0: 46 upstream commits after the tag
plus 15 fork commits. It is not missing current upstream main at this snapshot.
These counts describe ancestry, not semantic compatibility or upgrade effort.
Other agents' active local changes are outside this public-pin assessment.

### Future upgrade To-Do

- [ ] Define per-game adapters for dependency preparation, fresh analysis/lift,
  generated patching, build, source/native tests, smoke, scenarios and reports.
- [ ] Test two explicitly labeled candidates where feasible: unmodified release,
  and release plus audited required downstream changes. Passing the latter does
  not prove the former works.
- [ ] Run the known-good baseline first on identical data/settings/hardware.
- [ ] Use isolated checkouts and disposable saves. Regenerate from the candidate
  lifter; rebuilding old generated C tests only part of the upgrade.
- [ ] Run compiler/runtime fixtures, then game-specific scenario gates and capture
  comparisons. Preserve baseline failures rather than treating them as green.
- [ ] Report each game as build-only, smoke-passed, scenario-passed, manually
  accepted, failed or blocked; never reduce unavailable checks to a global green.
- [ ] Keep game runs serial initially; concurrent games distort timing and may
  contend for input/audio/save resources. Never run Def Jam harnesses in parallel.
- [ ] Track every carried fix by upstream status, affected subsystem, reproducer,
  tests and dependencies. Audit high-coupling rendering/timing/audio interfaces.

Exit: one invocation produces a three-game compatibility matrix for exact
candidate builds. Overall release acceptance requires all designated scenarios
and the remaining manual fidelity checks, not merely successful compilation.

## Sources

- Def Jam: `scripts/regress.py`, `scripts/harness.py`, `scripts/frame-signature.py`,
  `.github/workflows/ci.yml`, `docs/research/toolkit-rebase-acceptance.md`.
- TimeSplitters: https://github.com/phobos665/split2-recomp/blob/cdd1d80783fa63ebf6c30649c098e0d729ed812b/CLAUDE.md
- Mercenaries: https://github.com/KraftMacAndChee/Mercenaries-Recompiled/blob/6e217ac9ea9b27e28bacead5ea0615effd95b7e6/MAINTENANCE.md
- Mercenaries runtime selection: `ports/mercenaries/CMakeLists.txt` at that revision.
- Upstream releases: https://github.com/sp00nznet/xboxrecomp/releases

No candidate upgrade, game build or gameplay acceptance was performed for this plan.

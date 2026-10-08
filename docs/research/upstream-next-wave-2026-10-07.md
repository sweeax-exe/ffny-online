# Recommended next upstream xboxrecomp PR wave

Reviewed 2026-10-07 while the isolated Def Jam migration runs its serial game
tests. This is a refreshed recommendation and preparation inventory, not an
isolated-branch validation result. No upstream PR or message was created.

## Current upstream and previous notes

GitHub API confirms latest published release
[v0.13.1](https://github.com/sp00nznet/xboxrecomp/releases/tag/v0.13.1), current
main `193e2995ffaec871bb3be43a43bd66aa5fccfeb1`. Our first five contributions
[#167–171](https://github.com/sp00nznet/xboxrecomp/releases/tag/v0.13.0) merged
into v0.13.0. The old October 4/5 OPEN tables are historical, not pending work.

v0.13.1 also includes #173 trailing jump-table arms, #174 gap alias bounds and
#175 manual-wrapper routing. Future PR extraction should start at current main
and refresh before submission. The authorized game migration still targets
exact v0.13.0 `b3700e1`; its running source is unchanged by this review.

Only #162 and #176 are open in the fresh API snapshot. Read their current
bodies, changed files and maintainer discussions. #162's backend interface is
still under review; the maintainer requests PUT-boundary and vertex-index
bounds fixes plus an enabled-path Burnout 3 run. #176's callback discovery has
revised its weak-evidence checks following false-entry reports. Do not duplicate
these scopes or describe either as accepted. No other-game runs were performed
by this project during this review.

Ignored read-only audit: `logs/upstream-013-work/upstream-pr-audit/`, including
all PR state pages, five relevant PR bodies/files/reviews/comments and the full
v0.13.0-to-current-main comparison. That comparison changes discovery/wrapper
code, tests and docs; it does not change the CPU remainder/double-compare/parity
defects or runtime event paths identified below.

## Prepare the narrow correctness and test wave first

| Order | Proposed PR | Why it matters | Evidence already available | Work before submission |
|---|---|---|---|---|
| 1 | Clear x87 C2 after completed FPREM/FPREM1 | FXAM can leave C2 set; completed host remainder does not clear it, so CRT remainder polling never finishes. | Bounded compiled test with a failing mutation; 12 added real-x86 input vectors; Release/Debug guest CRT checks. Direct producer defect in pristine upstream. | Extract only completion-status emission and synthetic cases on current main; capture baseline failure/fix success. Explicitly exclude quotient-bit, partial-reduction and exception emulation. |
| 2 | Read the double lane for COMISD/UCOMISD register operands | Reading `.f[0]` from a double register can make different doubles compare equal; 1.0/2.0 discriminate it. | Independent expected answers and wrong-lane mutation test in the integrated fork. Current upstream still uses the wrong register read. | Use a minimal consumer already supported upstream; preserve the existing memory-operand path. Limit scope to comparisons, then run isolated tools/conformance gates. |
| 3 | Execute compiled fixtures under MSVC and report genuine skips | GCC/Clang-only probes skip tests on CL-only machines; two print-and-return branches were counted as passes. | Source audit accounts for missing execution; candidate adapter restores compiler-backed checks without weakening their oracles. | Port only applicable upstream tests/helper. Preserve GNU flags, libm, compile-failure controls and recorder artifacts. Keep unsupported MSVC UBSan explicit and real-x86 assembly separate. Do not include fork-only TLS scaffolds. |
| 4 | Support parity SETcc/CMOVcc readers consistently | SETP/SETNP and CMOVP/CMOVNP are decoded but omitted by emission/consumer whitelists. | Combined compiled parity cases, independent low-byte parity oracles and ordered/NaN cases. | Demonstrate known-producer failures on clean current main; cover both positive/negative parity and decoded aliases. Avoid importing the whole dynamic flag-state design. |
| 5 | Make title-header event multiple waits match single waits | Existing opt-in header synchronization is prepared in single wait but not multiple wait; successful synchronization waits must consume the corresponding guest header. | Integrated native fixtures cover single/multiple, WaitAny/WaitAll, timeout and reinitialization. Pristine source exposes the missing preparation path. | Extract without the fork lazy resolver/preemption. Add pristine opt-in fixtures and unchanged-default controls; identify initializer Size/validator mismatch separately. Sequential fixtures do not prove setter/waiter or pulse races. |

Start preparation with **FPREM, double comparisons and fixture portability**.
Parity readers and the event follow-up form the next small wave. Keep public
diffs independent; do not submit a single migration-compatibility PR containing
all these changes. Each branch needs its own evidence, even though the combined
candidate source, native tests and Release game matrix are passing.

## Best older feature candidates to retain

These come from the October 4 catalogue and subsequent M6 notes; old IDs are
planning IDs, not GitHub PR numbers. None is an already isolated ready branch.

| Original ID | Candidate | Recommendation and preparation gate |
|---|---|---|
| 24 | Portable input mapping rules | Strong next feature candidate: key names, bindings, deadzones and remapping have standalone native tests. Extract parser/rules without SDL, topology or title input-hold policy; prove unchanged defaults and invalid-input handling. |
| 20, then 23 | INI-backed settings, then free-text settings | Small independent infrastructure with native tests. Extract the module without wiring presentation/launcher behavior; keep text support as an additive follow-up if useful for review. |
| 11 | Canonical vertex-program state and mutation versions | Best small graphics foundation. Existing adapter checks support upload/constant versioning; extract portable state tests from the WARP/D3D adapter and cover every setter/invalidation path. No HLSL/backend dependency is needed for this slice. |
| 10 | Luminance/alpha texture sampling | Useful bounded rendering correctness candidate. Needs known-pixel A8/luminance/luminance-alpha tests through both shader consumers plus ordinary-format controls. Shader compilation alone is insufficient. |
| 7 | Writable UDATA/TDATA routing | Valuable generic storage feature. Add synthetic filesystem trees, alias/count-limited paths, existing-save preservation and no-disc-write controls; separate first-run copying if necessary. |
| 6 | Recorded physical DMA translations | Useful existing mapping tests, but a wider producer/USB/APU contract. Add alias/cache/concurrency and zero-address consumer proof before extraction. |

The original 19-candidate programme leaves 14 IDs (6–19) after the five merged
PRs. M6 candidates 20–26 and newly discovered migration fixes are separate
planning additions, not a promise of a fixed number of submissions.

## Defer the broader dependency and policy work

- Dynamic flag snapshots, zero-count/partial writers, architectural float/SAHF
  preservation and multiple-reader joins need a coherent staged foundation.
  Follow up on accepted #159 rather than transplanting dependent fork hunks.
- LAST-table slot recovery is a fix to fork-only recovery helpers, not an
  independent pristine-upstream bug. Keep it with the original bounded recovery
  proposal and prove it does not override legitimate guest table mutation.
- Live heap/contiguous statistics are a guest-accounting feature. Upstream has
  no fork `xbox_HeapLiveBytes` helper to repair; introduce synchronized statistics
  with their consumer and preserve physical high-water checks. Do not advertise
  our helper lock as an existing-upstream race fix.
- The larger D3D11/HLSL/GPU/cache/point-sprite/buffer programme follows an agreed
  backend interface and synthetic draw/failure tests. Coordinate with
  [#162](https://github.com/sp00nznet/xboxrecomp/pull/162); its acceptance is not
  established. Scaled presentation/vsync inherit those dependencies.
- SDL host input, rumble and hot-plug follow the portable input mapping layer;
  reconcile existing upstream keyboard/pad ownership and preserve defaults.
- Fixed CPU affinity, title fence offsets/backlog thresholds, DSP scratch
  auto-ack and Def Jam voice selections remain title policy or redesign work.
- The new AC97 publication repair is in Def Jam `src/hooks/ac97_bm.c`, outside
  xboxrecomp. Its deterministic proof is useful local work, not an upstream
  toolkit PR candidate as currently implemented.

## Concrete preparation packet

For each selected scope, prepare a current-main branch containing one behavior,
source-only synthetic reproduction, baseline-fail/fix-pass results, relevant
tools/conformance/runtime results, compiler/optimization/skips, and a concise
draft description. State source/AI provenance and retain licences. Integrated
Def Jam success is supporting evidence; it is not cross-title or isolated-PR
proof. Refresh overlap before opening any PR; source-only preparation can proceed
while long game tests run, with executable test gates serialized afterwards.

Detailed reviews:
[new lifter candidates](upstream-v013-new-lifter-candidates.md),
[new runtime candidates](upstream-v013-new-runtime-candidates.md),
[historical candidates](upstream-v013-historical-candidates.md), and
[original maintenance queue](upstream-pr-maintenance.md).

# xboxrecomp v0.13.0 migration — execution record

Status: source integration, toolkit/native/x86/parent gates and fresh Release/
Debug builds with CRT self-tests pass. Corrected original scenarios pass on
rerun; retain the intermittent startup crash. Candidate M2 golden passes; full
game matrix is active. Branch publication and parent pin are pending.

The owner authorized the migration and machine regression/debug tests on
2026-10-07, with his live test after automated success (D76).

## Refs and isolation

- Original private parent: `e39cefba902823d4fc40ae29acb8d4e8e7f2cbd0`.
- Accepted toolkit pin: `2ac8e705c453854079f1da7cd9af3250d37b0259`.
- Upstream v0.13.0: `b3700e1d60bcd9c3dfaad734f45149b6f0bc5cb4`.
- Candidate parent: `C:/Users/Vlad/code/defjam-upstream013`,
  `migration/xboxrecomp-v0.13`.
- Candidate toolkit: nested `tools/xboxrecomp`, `defjam/upstream-v0.13`;
  22 replayed retained topics, provisional tip `bec01fa`, followed by local
  compatibility edits described below.

Four equivalent production topics already upstream are omitted. The MSVC
multiple-source fixture helper and remaining kernel behavior are retained.
The 108 historical patches are not replayed. Published old branches are intact.

## Semantic integration

| Area | Resulting implementation | Prepared evidence |
|---|---|---|
| Flag joins | Plan by consumer instruction address; shared condition selection for predecessors and block lifting. Declare edge variables explicitly rather than matching generated comments. | Mixed integer/float predecessors, multiple SETcc/CMOVcc readers, existing backedges and clobber sweeps. |
| Float/parity | Publish PF alongside ZF/SF/OF; scalar SSE and x87 EFLAGS comparisons publish snapshots. SAHF reads actual AH; LAHF consumes published flags. COMISD/UCOMISD read double register lanes. | Independent low-byte parity answers across widths and REP, zero/nonzero shifts, NaNs, double compares, SAHF/LAHF, status-only x87 compare after FCOMIP. Negative controls remove float publication or restore the wrong double lane. |
| Switch slots | Include displacement slot zero in the backward census; retain bounded exact-table proofs and live-target priority. | Compiled local and foreign last-slot tables, damaged loaded targets, rejected census, diagnostic tail-frame checks. |
| Dispatcher/events | Keep fork resolution/preemption/affinity with upstream dispatch wrapper. One shadow owner per guest address. Share header preparation between single/multiple waits; consume only successfully waited synchronization headers. | Default set/reset/single/multiple waits; opt-in first access, WaitAny, WaitAll, timeout, pulse, reinitialization and stdcall cleanup. |
| Allocation statistics | Lock the heap census. Separate live contiguous usage from the physical high-water range; route MmQueryStatistics to live usage only under reclamation. | Before/free/after bridge statistics, high-water preservation and reuse, upstream allocator/VMA modes. |
| OHCI | Keep fork TD diagnostics and upstream short-packet error handling in order. | Native USB transfer fixture plus game pad enumeration/input routes. |

These fixtures now pass the compiled source gates described below. Source syntax and
`git diff --check` have been checked. The shared MSVC fixture helper still hashes
to `c33ecc8c573b4690df4b454ace87c99c57f8910323bb0860a8ee6871a6697a1b`.

`RECOMP_HEAP_RECLAIM` and `RECOMP_EXT_VMA` must be **absent** for default acceptance:
upstream checks their presence, so a value of `0` enables them.
`RECOMP_TITLE_KEVENTS=0` and `RECOMP_GUEST_LOCK=0` disable the other new modes.
Memory fixture modes clear ambient switches before selecting their mode.

Sequential event fixtures do not establish concurrent waiter/setter or pulse
delivery correctness. Upstream header reconciliation still has races; the
opt-in remains disabled during game acceptance. AF remains outside the flag
model. The scalar double-lane fix is local to EFLAGS compares, not a broader
SSE double arithmetic audit.

## Baseline and safety

`logs/upstream-013-work/baseline/manifest.json` records hashes for original saves,
generated C, both binaries/maps/caches and toolkit analysis outputs. Release
freshness passed. The preserved old Debug artifact is stale and cannot certify
the current source.

The complete 22-file save root is copied and verified under
`logs/upstream-013-work/runtime-data/save`. A second frozen fixture has manifest
SHA-256 `76eb2b23f4c9f499686d9c285873412393436ea993d79ec938b210e399f6a887`.
Original saves are not used by runs. Every harness run retains its full-root
save guard; baseline and candidate use the same fixture serially.

Baseline runner: `logs/upstream-013-work/run-baseline.ps1`; full Release with
10-boot soak, then the five opt-in game checks. At 15:39, seven checks passed
(173 unit tests, three goldens, One on One, Free For All and Terrordome FFA).
Combat completed 26/27 assertions: all seven gameplay assertions passed,
`audio.dry_queue` failed with 64 post-anchor dry events. Exact evidence:
`logs/scenarios/regress-combat-20261007-153102/report.json` and `game.log.err`.
At 15:46, versus completed 25/26 assertions: all six gameplay assertions passed,
and the same audio gate failed with 13 dry events. Evidence:
`logs/scenarios/regress-versus-20261007-153828/report.json`.
Replay failed with zero records because the main-menu bootstrap never reached
StartGame (`logs/scenarios/regress-replay-20261007-154351`); it does not establish
a simulation mismatch. Intro, crib and gym passed. At 16:12, the full baseline
finished **10/14**. The soak stopped at its third boot: two successful boots and
one presentation hang (`logs/run-20261007-160639.log.err`,
`logs/hang-20261007-160822`). The native stack is in the D3D fence-wait family;
later state samples from the same PID show continued fence traffic, so the
exact cause is unproven. Baseline opt-ins: Terrordome One on One passes, FFA
result passes 28/28 and two-matches passes 35/35. Repeat reaches both fights but
fails its length check (75 recorded versus 77 replayed records; overlapping
records agree), so it does not establish a changed simulation. Evidence:
`logs/scenarios/regress-repeat-b-20261007-164156/report.json`. Visual fails due
to missing route anchors/captures (no measured pixel mismatch), recorded in
`logs/scenarios/regress-visual-20261007-165202/report.json`. Baseline opt-ins
finish **3/5**. All flag-review coverage gaps have source fixes. Toolkit Python
collection required the missing pefile dependency; installed version 2024.8.26
under isolated scratch python-deps, then restarted all compiler-backed tests.

These are pre-migration failures; neither the audio threshold nor a golden is
changed. APU source is unchanged between the accepted fork and candidate, and
the upstream release has no APU delta. The dry counters are actual queue-empty
observations, concentrated in early combat; they are not just a post-anchor
report-window artifact. Retained repeat captures show a WEAPON tutorial awaiting
A in both runs: the step-timed fighter input cannot dismiss paused UI after the
USB menu bootstrap closes. A bounded three-pulse USB diagnostic is queued on
the original build, retaining the same fixture/seed/step input and requiring
all 80 records plus the unchanged golden hash. This is not a timeout fix.

Candidate preflight passes all 15 native fixture projects. Real x86 differential
tests pass 5843 snippets and 211 compiler-function vectors with zero mismatches.
Conformance harnesses needed the retained indirect-tail TLS variable declared
in their synthetic runtime scaffold. Compiler portability fixes close 26
probe skips and two silent uncompiled passes while retaining their oracles and
negative controls. Final toolkit results: 654 passed, 107 subtests passed, one
explicit unsupported MSVC signed-overflow-sanitizer skip. Parent 173 unit tests
pass. Both fresh builds linked, but Release's guest CRT gate exposed a real
status-production bug: FXAM sets C2 for normal finite values; FPREM computed a
complete remainder without clearing C2, so _CIfmod's FNSTSW/SAHF/JP loop never
returned. FPREM and FPREM1 now clear C2 after full host-libm reduction. Bounded
compiled tests reject the old emission; two real-x86 cases cover remainder,
stack depth and completion status. Real differential tests now pass 5855
snippet and 211 compiler vectors, zero mismatches. Fresh toolkit/re-lift/build
chain 50384 completes: 656 toolkit tests/107 subtests pass, one unsupported
sanitizer skip; both builds freshly linked and both guest CRT gates pass all
four divide helpers and eight remainder values/depth checks, no runtime faults,
save restoration verified. Logs `run-20261007-172729` (Release) and `172754`
(Debug). Corrected baseline scenarios now precede the candidate game matrix.

## Remaining gates

Baseline correction (17:26): the first fixture placed profiles at
`save/45410049`, but the runtime maps UDATA to `save/UserData/45410049`.
The seven scenario checks (combat, versus, FFA result, two matches, replay,
repeat, visual) therefore used an empty profile list. Their previous results
remain diagnostic evidence, not the corrected acceptance baseline. Other
harness routes/soak used the correct complete disposable save root.
`fixture-corrected` retains all 15 frozen profile files byte-identically under
UserData, manifest SHA dc67b420d610b33369428b55c6eaf136548b474ed25e0987ee581ebe9a9af023.
The original-build seven-check rerun is queued behind fresh candidate builds;
candidate comparisons use this same corrected fixture. The finite USB tutorial
probe was cancelled while idle; no input recipe or golden has been changed.

Corrected original combat first hit an early read AV at AC97 0xFEC0010B in
`nv2a_ack_thread`, before guest boot (`regress-combat-20261007-172831`). Source
review identifies protection before ownership publication; NV2A already avoids
the same startup window. Candidate-only parent correction publishes initialized
shadow/base atomically before PAGE_NOACCESS, rolling back failed protection.
A deterministic native fixture forces ownership and byte-read emulation inside
the protection call, with failure/idempotence checks and old-source negative
control. It will run after the baseline chain, then both builds refresh.
Corrected original versus passes 26/26 including audio (`172835`), FFA result
passes 28/28 including eight gameplay assertions and audio (`173343`). Original
two-matches passes 35/35 including 15 gameplay assertions (`173955`). Original
replay passes all 80 records/1800 steps against the unchanged golden hash
(`regress-replay-20261007-174946`); no tutorial/input workaround was needed.
Original
combat gets one retained rerun after the remaining corrected baseline checks,
then the deterministic AC97 fixture/negative control and both builds run.

Corrected repeat also passes: both runs produce 80 identical records matching
the unchanged golden hash (`regress-repeat-b-20261007-175445`). The image
manifest explicitly associates its images with `vy2-hour-v1`, an earlier VY2
career snapshot. Its six fixture files and three image SHA hashes validate;
an unchanged frozen copy is `fixture-visual-approved`, manifest SHA
607fefd546119be9750ed0b071bbd4ee8f21d69a11593569640216c6c55a090a.
Both original and candidate image gates use this exact reference dataset;
gameplay uses fixture-corrected. Current-career visual results remain retained
as a separate diagnostic. No image approval, golden or threshold changes (D77).

Corrected current-profile visual passes 4/4 comparisons (`180444`), bringing
the corrected seven-check baseline to 6/7; only the early AC97 combat crash
fails. Reference-fixture visual independently passes 4/4 (`180808`). Original
combat rerun starts `run-20261007-181126`; deferred source/build chain follows.

Complete baseline, then all toolkit Python tests with the reviewed MSVC adapter;
15 native fixture projects, real x86 snippets and compiler corpus. Fresh
analysis/lift and both game builds with freshness certificates; full Release,
five opt-ins, targeted Debug and guarded keyboard/SDL/overlay smokes. Classify
failures against the original evidence. Verify original-save hashes again.

Only source is staged/committed. Publish the reviewed toolkit commit before a
parent gitlink commit. No upstream PR without separate owner approval. Leave the
isolated Release candidate ready for the owner's live test before final acceptance.

## 18:17 validation checkpoint

Corrected original combat retry passes 27/27, including all seven gameplay
assertions and audio (`regress-combat-20261007-181124`, 294 seconds). The first
early AC97 crash remains an intermittent baseline defect. All seven corrected
scenario checks have passing runs; this does not erase the first failure.
The candidate AC97 forced-interleaving/read/rollback fixture passes, and the
original source compiles but fails six checks under the same interleaving.
Parent unit tests pass 173 again. Both candidate builds have been refreshed
and certified after AC97. Candidate M2 smoke matches the unchanged frame-40
golden (`regress-20261007-181708`). Full Release, opt-ins, Debug and real host
input tests continue serially in session19137; no complete matrix verdict yet.

## 20:16 game and recommendation checkpoint

Candidate Release full14/14 passes (regress20261007-193201,74minutes),
including10/10 boot soak with no hangs/failures, three unchanged image goldens
and replay80records/1800steps. All5 opt-ins pass: Terrordome fight, FFA-result
28/28, two-matches35/35, repeat80identicalrecords/unchangedgolden (repeat-b200129),
and approved-reference visual4/4 (201131). Debug9+5boots is active, followed by
four real-host-path checks. Final preservation, explicit quick/Debugversus,
publication/reproducibility/CI and owner live acceptance remain.

Read-only upstream recommendation review found current v0.13.1/main193e2995
with #173–175 merged and #162/#176 still under review. It does not change the
authorized exactv0.13.0 migration target or running source. Details and isolated
PR proof gaps: upstream-next-wave-2026-10-07.md. No upstream PR created.

## Final scheduled local validation results

Status verified2026-10-07 23:59. All recorded local gates pass: candidate-game-exit.json
smoke/full/optins/visual/debug/host all0 and complete; final-local-exit.json
Debugversus/quick/preservation all0 and complete. Debug9/9 includes5/5 boots
with0hangs/failures(regress20261007-205713). Four real host paths pass:keyboard,
SDLtwo-pads,late attachment,overlayready/open/closed(host-results.json). Extra
Debugversus26/26(regress-versus20261007-211946);Releasequick5/5(regress20261007-213523).
Preservation confirms original22save/24generated/10build files and current15/
reference6/originalreference6 fixtures unchanged; auxiliary build/dependency
hashes are recorded. These results do not imply publication,clean-clone/CI,
owner live acceptance or merge/release. Integration edits remain uncommitted;
originalmain separately advanced toba43570 by a setup.exe-backlog docs commit.

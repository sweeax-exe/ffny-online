# xboxrecomp v0.13.0: source review and migration plan

Reviewed 2026-10-07. **Planning and source analysis only:** no rebase, build,
pytest/conformance run or game launch was performed. Current game behavior has
not been certified on the candidate. The checked-out toolkit and parent gitlink
remain unchanged.

## Recommendation and exact inputs

Migrate in an isolated game checkout and toolkit worktree, starting at the exact
release tag and replaying reviewed residual fork topics. This is a manageable
update, but not a mechanical rebase. Keep the current default runtime behavior
for the first acceptance run; evaluate new opt-in behavior separately.

| Input | Exact revision |
|---|---|
| Private DefJam checkout | `e39cefba902823d4fc40ae29acb8d4e8e7f2cbd0` |
| Current published toolkit pin / `defjam/m6` | `2ac8e705c453854079f1da7cd9af3250d37b0259` |
| Common upstream base | `1409a7d7801d3e931fb1074be6104209ddd9a33e` |
| Upstream `v0.13.0` | `b3700e1d60bcd9c3dfaad734f45149b6f0bc5cb4` |

The fork has **26 local topic commits**: the accepted 15-topic rebase plus 11
settings/presentation/input/overlay commits. It changes 99 paths from the base.
The release changes **57 paths**, with **21 paths overlapping the fork**.
`git merge-tree --write-tree --name-only 2ac8e70 v0.13.0` reports **12 conflicting
paths**. That probe writes Git objects only; it does not modify an index, branch
or checkout. Its conflicts are an aggregate merge result, not a prediction of
the number of per-commit rebase conflicts.

Local reproducible source evidence: `logs/upstream-013-analysis/collect.py` and
`source-evidence.json`; merge tree `834265a675f45a04b22de93030a2b092b345aa7b`.
Initial parent status reported a deleted generated `.gitkeep`, while the sandbox
also reported denied access to that directory; this analysis did not alter it.

## What this release adds to our existing base

[Official release](https://github.com/sp00nznet/xboxrecomp/releases/tag/v0.13.0).
Its broad Burnout/graphics changelog is cumulative: compare **1409a7d to the tag**
for this migration. The incremental delta does not change the D3D backend, APU,
NV2A executor or host-input implementation; much of that release work was
already in our base. Direct conflicts concentrate on lifter, kernel and OHCI.

New incremental work includes mixed/float flag joins (#159), backward jump-table
measurement and resync repair (#134/#135), absolute save-directory creation
(#133), opt-in memory reclamation/high virtual reservations (#158), opt-in
guest CPU locking/title-built events (#160), path/exit/call-profile diagnostics
(#161), kernel/USB correctness (#157), and Linux/clang-cl portability (#172/#165).
The final tag includes the follow-up fix moving title-built event pulse handling
into `KePulseEvent` (`882378e`); use the tag, not an earlier merge snapshot.

All five submitted DefJam PRs are included: #167 PUSHAD/POPAD, #168 entry hooks,
#169 caller cleanup, #170 MMIO REP copies, #171 directory-search lifetime. The
original fork commits are broader/adapted: `git cherry` marks all 26 `+`, so Git
will not automatically remove equivalent work. Classify by behavior and tests,
not patch-id or commit title. Preserve submitted-fixture coverage and fork-only
tests while removing duplicate implementations.

| Original topic | Upstream replacement | Replay decision after source comparison |
|---|---|---|
| `193db87` entry hooks | `7f1b263` / #168 | Production changes match; omit replay, retain upstream expanded tests. |
| `af45945` PUSHAD/POPAD | `1ba591e` / #167 | Omit equivalent production changes; extract residual MSVC fixture support. Later `7f7eef9` also needs the helper's multiple-translation-unit support. |
| `e53263f` caller cleanup | `d88cbea` / #169 | Macros and decoded caller-cleanup classification match; omit replay. |
| `032c9f2` MMIO REP copies | `5e1d2ed` / #170 | Helper and byte/word/dword dispatch match; omit replay, keep expanded tests/conformance. |
| Directory portion of `799c19b` | `a4e0f36` / #171 | Deduplicate that behavior only; the rest of the large kernel topic remains necessary. |

## Conflict and risk ledger

| Area | Evidence / risk | Resolution and acceptance requirement |
|---|---|---|
| Backward switch slots — high | Upstream includes the displacement slot in a backward table (`inside + backward(base-4)`); fork `_switch_index_pairs` still recognizes the old census. These code hunks merge automatically. | Align target census and signed slot indices; retain foreign-arm ownership rescue and damaged-slot fallback. Execute backward-table slot-zero/negative-index tests with corrupted guest slots. A clean merge is insufficient. |
| Mixed flag joins — high | #159's per-edge conditions coexist with fork dynamic ZF/SF/OF/CF/PF publication. The new code searches generated `_flags /* ...` text and `fallback flag var`; fork predicates/declarations use another representation. Machinery can merge but fail to activate. | Reconcile snapshot validity and float/mixed joins deliberately. Retain width-correct result flags, REP comparisons, LOOP consumers and partial writers. Run compiled branch/setcc/cmov, float unordered, swapped-operand and back-edge fixtures with negative controls. |
| Guest events — high | `kernel_bridge.c` conflicts in `KeSetEvent`/single wait. Fork `ke_object_resolve` already lazily shadows events **and semaphores** by default; upstream opt-in `ke_guest_event` synchronizes guest SignalState and marks entries specially. | Keep default fork resolver and multi-wait behavior. Avoid two owners of one guest event; reconcile table lifetime, set/reset/pulse, auto-reset and direct header writes. Retain the final pulse fix. Run object-path/event fixtures and intro/Story/soak. |
| Guest execution / GPU — high | Upstream restructures `kernel_thunk_dispatch` around an optional guest lock. Fork holds the title for GPU software interrupts/backlog and pins workers/timer guest callbacks to one core. Conflict blocks contain substantial independent implementations. | Preserve affinity and `kernel_gpu_preempt` in the correct dispatcher body. Keep `RECOMP_GUEST_LOCK` off initially. Inspect lock ordering, callbacks/APCs, waits and thread exit. The upstream lock cannot preempt a guest-only spin loop and callbacks do not take it; it is not a replacement for our model. |
| Heap, contiguous arena and DMA — high | #158 adds heap locking, split/reclaim, contiguous free and high VMA. The automatic result leaves fork `xbox_HeapLiveBytes` scanning a table without the new lock. Fork statistics use contiguous high-water bytes; reclaimed live bytes differ. | Give statistics a synchronized accounting path; distinguish address-range high-water from live allocation usage. Keep reserved first physical page and canonical inverse map/generation. Before enabling reclaim, verify reuse/free map-cache lifetime and device DMA behavior. Run memory, physical-map and allocator fixtures. |
| Directory search — medium | #171 overlaps fork `NtClose`/directory cleanup. Upstream edits the public query function; fork renamed it to private `query_directory` and wraps it for diagnostics. | Keep one exported query function, one close cleanup, tag/native-handle resolution and wrapper behavior. Preserve writable UDATA/TDATA routing. Run directory lifetime/handle-reuse tests and profile/Story routes. |
| OHCI short packet — medium | New DATA UNDERRUN return overlaps a fork trace block in `ohci.c`. | Keep both bounded tracing and short-IN-without-bufferRounding handling after CBP accounting. Keep Controller S topology, indexed pads, QPC polling, anchors, rumble and hot-plug. Test short packet, buffer rounding, two/four pads and host input. |
| Platform/diagnostics — medium | Several paths auto-merge: TLS/compiler rules, pseudo-handle sign extension, path hooks and runtime header macros. Profiling requires a project-defined callback when enabled. | Audit TLS in kernel/bridge/generated code, preserve new mapping to DOS error 487 and 64-bit counters. Leave call profiling off unless wired deliberately. Check save-hook ordering and normal path routing. |
| M6 features — high acceptance risk | No direct release edits to settings, SDL host input or D3D presentation, but they depend on retained fork callbacks/USB behavior. Ordinary game tests disable host settings/input and skip launcher. | Replay all 11 newer commits; retain settings text/reset APIs, flip/scaled presentation, SDL3, bindings, focus, rumble/hot-plug and overlay hooks. Add dedicated integration and owner checks below. |

Specific clean-hunk evidence in the synthetic merge tree: `lifter.py:2751`
includes slot zero, while `:2776` still uses the old `_switch_index_pairs`
enumeration. Also check the foreign-arm reader's backward census at `:2800`.
`translator.py:2461` searches the old predicate substring and `:2497` the removed
declaration marker. Reconcile through explicit predicate emission, not merely
restoring a text marker. Preserve upstream guards against unseen entry/switch
edges and live-register hazards. Same-family SSE/x87 snapshot merging from
`fc54a43` is additive and should survive.

The twelve observed conflict paths are:

```
src/kernel/kernel_bridge.c
src/kernel/kernel_file.c
src/kernel/xbox_memory_layout.c
src/usb/ohci.c
tools/recomp/lifter.py
tools/recomp/translator.py
tools/recomp/test_entry_hooks.py
tools/recomp/test_flag_join.py
tools/recomp/test_flag_join_backedge.py
tools/recomp/test_icall_caller_cleans.py
tools/recomp/test_lifter_pushal.py
tools/recomp/test_rep_movs_mmio.py
```

## Migration stages

1. **Freeze a current baseline.** Preserve both source revisions, generated C,
   analysis/lift/build certificates, Release/Debug exe+PDB hashes, compiler and
   pinned SDL/ImGui versions, settings and test environment. Hash the original
   complete save tree; make a disposable runtime-data copy, with extracted game
   data accessible and local visual baselines copied. Freeze one explicit hashed
   profile fixture for baseline and candidate. Verify the fixture has the profile
   ordering the Story scripts expect (currently third ID). Run current baseline
   acceptance commands serially before modifying any runtime. Retain raw logs
   and failed attempts. Do not use an October 3 build as the v0.4 baseline.
2. **Create an isolated candidate from the exact tag.** Use a new local branch
   and separate game checkout/build roots. Inventory the 26 topics in a new
   migration ledger; do not replay the 108 archived patch files. For each merged
   PR, compare the original fork topic with the upstream version and carry only
   remaining behavior/tests. Replay remaining lifter topics, kernel/runtime,
   audio/USB/rendering adapters, then the 11 M6 commits in dependency order.
   Rebuild topic commits without rewriting published `defjam/m6` history.
3. **Resolve semantics and run source gates.** Handle the ledger's switch/flag,
   event/dispatch and allocator issues explicitly, including clean hunks. Start
   with `RECOMP_HEAP_RECLAIM` and `RECOMP_EXT_VMA` unset (upstream enables
   these by presence, so `=0` would turn them on), `RECOMP_TITLE_KEVENTS=0`,
   `RECOMP_GUEST_LOCK=0` in controlled runs; inherited diagnostics must not enable
   them accidentally. This preserves the fork's default resolver/affinity.
   Execute the complete toolkit Python suite with compiled fixtures under MSVC,
   independent x86 conformance, and native fixtures below. Disclose all skips.
4. **Fresh analysis, lift and both game builds.** Old generated C cannot validate
   new switch/flag semantics. Use `analyze.ps1`, `recomp.ps1` and `build.ps1` for
   each candidate; compare seeds, function extents, switch ownership, manual
   hooks, translated counts and unresolved stubs. Investigate changes rather
   than insisting historical counts remain fixed. Require freshness certificates.
5. **Game acceptance and owner review.** Run quick, full and all opt-in gates,
   then host-input/overlay integration, on the same disposable data/fixture as
   the baseline. Use the same environments and hardware; no concurrent harness
   or unit jobs. Keep game-state and visual goldens unchanged. Review failures
   with baseline/candidate comparisons before attributing known crowd variance
   or profile-order mistakes to the toolkit. Listen and play-test physical pads.
6. **Integrate only the accepted candidate.** Update the migration ledger and
   evidence report. Publish a reviewed source-only fork commit before pointing
   the parent gitlink at it; set `.gitmodules` branch metadata consistently (it
   currently still names `defjam/rebase-2026-10`, though the pin is on `defjam/m6`).
   Fresh-clone/build the exact pin and pass CI. Keep private history private and
   handle public source sync through its established workflow. Preserve old pin,
   builds, fixture and evidence for rollback. No new upstream PR is needed here.

## Executable test plan

From the **candidate game checkout**, with `DEFJAM_DATA` set to the prepared
disposable runtime-data root (not the original saves):

```powershell
. .\scripts\env.ps1
.\scripts\analyze.ps1 -DataDir $env:DEFJAM_DATA -ToolkitDir $candidateToolkit
.\scripts\recomp.ps1 -ToolkitDir $candidateToolkit
.\scripts\build.ps1 -Preset win-x64-release -ToolkitDir $candidateToolkit
.\scripts\build.ps1 -Preset win-x64-debug -ToolkitDir $candidateToolkit
python scripts/pipeline-state.py verify-build --preset win-x64-release
python scripts/pipeline-state.py verify-build --preset win-x64-debug
python scripts/regress.py --quick --preset win-x64-release
python scripts/regress.py --preset win-x64-release --fixture $fixture
python scripts/regress.py --only fight-terrordome,ffa-result,two-matches,repeat,visual --preset win-x64-release --fixture $fixture
python scripts/regress.py --only fight,versus,combat,replay --preset win-x64-debug --fixture $fixture
```

`$candidateToolkit` and `$fixture` are prepared absolute paths, not existing
artifacts supplied by this analysis. Quick is 5 checks; full is 14 and includes
three soak boots by default. For the migration use `--soak 10` on the full run.
The opt-in command adds all 5 omitted checks. Budget roughly an hour for a full
Release run, plus opt-ins, fresh lifting/builds, baseline runs and owner review;
the older 20-minute estimate is stale. Do not automatically update goldens.

Run native fixtures from their individual `tests/<name>` CMake projects with
Ninja/MSVC Release (Debug for relevant lifetime/ABI fixtures), build, then
`ctest --test-dir <build-dir> --output-on-failure --no-tests=error`. Include current
CI's `kernel_bridge`, `kernel_directory`, `usb_transfer`, `nv2a_vsh`, `settings`,
`d3d8_present_fit`, `input_map`, `input_host`; additionally `kernel_events`,
`kernel_regressions`, `memory_regressions`, `kernel_object_paths`,
`kernel_dispatch`, `kernel_irql_abi`, `kernel_physical_address`. Some focused
fixtures disable GPU preemption because their synthetic RAM has no aperture;
that is not game-path GPU-preemption evidence.

`scripts/test-toolkit-msvc.py` selects the current submodule and pins its shared
fixture helper SHA. Retain the fork helper's MSVC/multiple-source capabilities
and reviewed fingerprint; it differs from pristine upstream. For a separate
toolkit path, use a candidate-checkout submodule or adapt the runner's root
deliberately. Run all `tools` tests as well as its narrower recomp/disasm gate.
The parent native input test can skip without `cl`; activate MSVC before gates.

Additional Release harness routes must turn on the real host input plumbing:

```powershell
# Keyboard player 2 beside scripted player 1:
python scripts/harness.py run versus --preset win-x64-release --env RECOMP_INPUT_HOST=1 --env RECOMP_INPUT_IGNORE_FOCUS=1 --env RECOMP_PAD_HOST=1 --env RECOMP_INPUT_NO_PADS=1 --env RECOMP_PAD_SCRIPT_KEYS=1 --env RECOMP_KEYBOARD_PLAYER=2
# Two pads through SDL's virtual-gamepad path; inspect rumble/slot evidence:
python scripts/harness.py run versus --preset win-x64-release --env RECOMP_INPUT_HOST=1 --env RECOMP_INPUT_IGNORE_FOCUS=1 --env RECOMP_PAD_HOST=1 --env RECOMP_INPUT_NO_PADS=1 --env RECOMP_INPUT_VIRTUAL_PADS=2 --env RECOMP_PAD_SCRIPT_VIRTUAL=1 --env RECOMP_RUMBLE_LOG=1
# Open/close the overlay with the real host-pad chord during a fight:
python scripts/harness.py run fight --preset win-x64-release --env RECOMP_INPUT_HOST=1 --env RECOMP_INPUT_IGNORE_FOCUS=1 --env RECOMP_PAD_HOST=1 --env RECOMP_INPUT_NO_PADS=1 --env RECOMP_INPUT_VIRTUAL_PADS=1 --env RECOMP_PAD_SCRIPT_VIRTUAL=1 --env RECOMP_INPUT_VIRTUAL_CHORD_MS=40000
```

Add keyboard-player-1 `fight-result` and virtual late-pad arrival (one initial
pad, `RECOMP_INPUT_VIRTUAL_LATE_MS=20000`). Inspect anchors, input/rumble/menu
logs; these harness observations do not supply the complete scenario combat
verdict. Separately test launcher Play/Quit/skip, settings persistence/reset,
F1/chord close/resume, focus, resize/fullscreen and menus with Xbox/DualSense.
Ordinary harness screenshots exclude the post-scale overlay, so review it live.

## Acceptance, rollback and limits

Require: source/conformance/native gates; fresh certified Release/Debug game
builds; Release quick/full/all opt-ins and targeted Debug all passing; retained
game-state hash and reviewed local visual signatures; identical guarded save
restoration and unchanged original save hashes; preserved v0.3/v0.4 behavior;
owner listening/physical-device play-test; fresh-clone reproducibility and CI.
Run new opt-in memory/locking features only in separate experiments if desired;
their activation is not required to consume the release.

On failure, keep the old accepted pin and baseline active, diagnose candidate
code in isolation, and rerun affected gates after fixes. Full-frame crowd poses
are nondeterministic; a replay mismatch must be investigated, not normalized.
Missing local visual baselines are a blocked gate, not a successful skip.

Automated audio checks cover PCM/queue health, not perceptual fidelity. Native
input tests cannot establish physical rumble feel. Current CI does not build
the C++ launcher/overlay. Proton/Steam Deck, Switch Pro/DirectInput, long-session
memory and broader gameplay outcomes remain unverified. The build certificate
does not cover auxiliary game `cmake/*.cmake` or external dependency checkout
hashes; record those explicitly. Historical successful runs remain baseline
evidence and are not results for this release.

Source references: `scripts/regress.py` (ORDER/DEFAULT/QUICK and check functions),
`scripts/pipeline-state.py`, `scripts/harness.py`, `scripts/scenario_suite.py`,
`.github/workflows/ci.yml`, `docs/09-testing-harness.md`, `docs/10-input.md`,
`docs/launcher-and-overlay.md`, `docs/research/toolkit-rebase-ledger.md`, and
`docs/research/first-five-pr-submissions.md`.

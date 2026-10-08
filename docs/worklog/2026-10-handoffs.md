## Archived 2026-10-08 10:20 — live acceptance and release integration

## 7. Hand-off

### Upstream v0.13.0 migration: all scheduled local tests complete

Owner authorized migration/all machine tests, then his live acceptance (D76).
D78: finish current rebase merge/release before the five focused upstream PRs.
Candidate C:/Users/Vlad/code/defjam-upstream013, branch migration/xboxrecomp-v0.13,
parent basee39cefba. Nested toolkit defjam/upstream-v0.13:22 retained topics on
exactb3700e1d60bcd9c3dfaad734f45149b6f0bc5cb4, replaytipbec01fa2 plus30modified/
2new source/test files uncommitted. Four upstream-equivalent topics omitted.
Do not replay108 old patches. All26 topics mapped in toolkit-v0.13-topic-ledger.md.
Original checkout nowba43570 (setup.exe backlog docs commit added separately);
preserve its newer history and original uncommitted research/worklogs during
integration. Candidate source remains isolated; accepted build/toolkit2ac8e705
and original save artifacts are preserved.

Scratch C:/Users/Vlad/code/defjam-recomp/logs/upstream-013-work. Original baseline
manifest/snapshots retained. First fixture missed UserData; its seven scenario
results are diagnostics, not comparisons. Corrected current profiles15files,
SHA dc67b420d610b33369428b55c6eaf136548b474ed25e0987ee581ebe9a9af023;
approved olderVY2 visualfixture6files/3imagehashes,
SHA607fefd546119be9750ed0b071bbd4ee8f21d69a11593569640216c6c55a090a(D77).
No golden/threshold/input-recipe change. Original corrected scenarios have
passing runs; firstcombatAC97 startupAV retained172831,retry27/27 passes181124.
Originalvalidsoak2success/1hang remains an unproven fence-wait baseline defect.

Source gates:656 toolkit tests+107subtests,one explicit MSVCsanitizer skip;
15 native toolkit projects;5855 real-x86 snippets+211compiler vectors zero
mismatches;parent173. Fresh analyze/lift/both builds certified;both CRT4divide+
8fmod values/depth pass. FPREM staleC2 and parentAC97 publication race repaired.
AC97 forced-interleaving/read/rollback fixture passes,oldsource compiles/fails
6checks. Both builds refreshed afterwards. Trusted shared helper SHA
c33ecc8c573b4690df4b454ace87c99c57f8910323bb0860a8ee6871a6697a1b.

All local game gates PASS, serial chains19137/76407 completed:
- Release14/14(regress20261007-193201),10/10boots0hang/fail.
- Five opt-ins:Terrordomefight,FFA-result28/28,two-matches35/35,
  repeat80identicalrecords/1800steps unchangedgolden,approvedvisual4/4(201131).
- Debug9/9(regress20261007-205713),5/5boots0hang/fail.
- Host4/4:keyboard,SDLtwo-pads,SDLlate-pad,overlayready/open/closed.
  host-results.json and scenario reports upstream013-host-*-20261007-205713.
- ExtraDebugversus26/26(211946),Releasequick5/5(regress20261007-213523).
- preservation-result.json/log:original22save,24generated,10build files
  unchanged;corrected15/reference6/originalreference6 unchanged. Auxiliary
  CMake/dependency source hashes recorded. No live test/CI/publication claimed.

candidate-game-exit.json all6gates0/complete;final-local-exit.json
debug_versus/quick/preservation0/complete. No repeated game tests required
without source changes/failures. DefaultHEAP_RECLAIM/EXT_VMA UNSET (presence
enables even0);TITLE_KEVENTS/GUEST_LOCK=0. Runs used full-root save guards and
disposable runtime-data;original saves must never be used by live/debug runs.

Remaining:review and source-only topic commits,gitadd-A--dry-run beforeeach;
publish reviewed forkSHA before parentgitlink commit;sync research/PROGRESS/
worklogs into candidate and preserve newer originalmain docs;fresh-clone exact
publishedpin/build and CI;owner physical/audio/UI live acceptance;then merge/
release. All executable gates remain serial. Never distribute generatedguestC,
XBEs,binaries,saves,logs/captures. PrivateDJFFNY-recomp and public clean-history
DJFFNY-public-preview are separate;no privatehistory publicpush. Current
candidateparent edits:CI matrix,AC97source/newfixture andgitlink only.
Keep acceptedbuild forrollback;candidateRelease available for safe live setup.

Upstream recommendations in upstream-next-wave-2026-10-07.md. As of review:
main193e2995/v0.13.1,#167–171 mergedv0.13.0,#173–175 mergedv0.13.1,
#162backend/#176callbacks open. FuturePRbase currentmain;this migration stays
exactv0.13.0. After currentmerge/release,prepare FPREMcompletion,doublelane,
MSVCfixtures,parityreaders,eventmultiwait withisolatedupstreamfail/fixproof.
No upstreamPR submitted;submission approval requirement remains.

Read-only heartbeat def-jam-migration-test-updates ACTIVE20min reports meaningful
existing-result changes only. It cannot execute tests/changefiles/Git/publish.
Broader side-effect automation was rejected;do not broaden it. Automatedtest
completion was notified21:44;normal23:59statusrequest verified allresults and
updated this formerly stale active-test handoff. Migration/release not complete.


## 2026-10-07 23:59 — superseded active-test handoff

## 7. Hand-off

### Active upstream v0.13.0 migration (D76)

Owner authorized implementation and all machine tests, with live acceptance
after automated success. Accepted original parent e39cefba / toolkit2ac8e705
remain intact. Isolated candidate C:/Users/Vlad/code/defjam-upstream013,
branch migration/xboxrecomp-v0.13; nested toolkit defjam/upstream-v0.13,
22 replay topics on exact b3700e1d60bcd9c3dfaad734f45149b6f0bc5cb4,
tip bec01fa2 plus uncommitted semantic/test integrations. Four equivalent
production topics omitted; do not replay108 historical patches. Full mapping:
docs/research/toolkit-v0.13-topic-ledger.md; execution record/plan alongside it.

Scratch C:/Users/Vlad/code/defjam-recomp/logs/upstream-013-work. Baseline
manifest hashes22 original save files, generated24 and builds10; snapshots
retained. All runs use disposable runtime-data with full-root save guards.
Initial profile fixture missed UserData; seven old scenario results invalid
for comparison, retained as diagnostics. Corrected fixture UserData15 files
SHA dc67b420d610b33369428b55c6eaf136548b474ed25e0987ee581ebe9a9af023.
Reviewed visual baseline names older VY2 fixture, frozen6files/3image hashes,
SHA607fefd546119be9750ed0b071bbd4ee8f21d69a11593569640216c6c55a090a.
D77 records per-gate datasets. No golden/threshold/input-recipe changes.

Original corrected seven scenarios have passing runs; first combat crashed
before guestboot at AC97FEC0010B nv2a_ack_thread+0xC48 hostoffset. Retain that
intermittent failure (172831), retry27/27 passes181124. Original soak2success/
1hang was valid; fence-wait stack root remains unproven. Old Debug stale.

Candidate source gates:656 toolkit tests+107subtests, one unsupportedMSVC
sanitizer skip;15 native toolkit projects;5855 real-x86 snippets+211compiler
vectors zero mismatches;parent173. Shared trusted helper hash
c33ecc8c573b4690df4b454ace87c99c57f8910323bb0860a8ee6871a6697a1b.
Fresh analyze/lift/Release+Debug certified;both CRT4divide+8fmod values/depth
pass,no faults,save restored. FPREM staleC2 fixed. ParentAC97 publishes
initialized shadow/base atomically before protection; forced interleaving/read/
rollback/idempotence fixture passes, old-source negativecontrol compiles/fails
6checks. Both builds refreshed afterAC97, chain3509 completed0.

ACTIVE serial game chain19137: smokeM2 pass;Release14/14 in74min
(regress20261007-193201),including10/10 boots0hang/fail;Terrordome436s,
FFA-result28/28,two-matches35/35,repeat80identicalrecords/1800steps unchanged
golden (repeat-b200129);approved visual4/4(201131). All5 opt-ins pass.
Debug9checks+5boots active,m2/m3/m4a imagegoldens pass;four keyboard/SDL-two/late-pad/overlay
checks follow. JSON candidate-game-exit has smoke/full/optins/visual0;debug/
host incomplete. KeepHEAP_RECLAIM/EXT_VMA UNSET (presence enables even0),
TITLE_KEVENTS/GUEST_LOCK=0. All tests/builds/game gates serial. No current
baseline/game source changes from the recommendation review.

Next: inspect Debug/host results; diagnose any failures, rerun affected gates.
Targeted Debugversus in original plan still needs a run (current9 omit it),
and explicit Releasequick before source commits per AGENTS. Then idle
verify-preservation.py: original saves/generated/build hashes;corrected15/
reference6 unchanged;auxiliaryCMake/dependency-source hashes. Publish reviewed
toolkit topic commits to fork before parentgitlink commit,source-only staging
and gitadd-A--dry-run each. Fresh-clone exact published pin/build+CI required.
Preserve original uncommitted docs/worklogs;sync candidate before parentcommit.
No private history to publicDJFFNY-public-preview. Keep original accepted
main available;leave isolated candidate for owner physical/audio/UI live test.

User asked upstream PR recommendations while tests run. Fresh API latest
v0.13.1/main193e2995;#167–171 mergedv0.13.0,#173–175 mergedv0.13.1.
Only#162backend/#176callbacks open, current maintainer reviews read. Future
PR extraction startscurrentmain;game migration staysexactv0.13.0.
docs/research/upstream-next-wave-2026-10-07.md prioritizes FPREM,doublelane,
MSVCfixtures,parityreaders,eventmultiwait;olderinputmap/settings/VSHstate/
texturechannels/storage/DMA retained. Three read-only detailed reviews done.
No PR branches/submissions or new test runs for recommendations; clean-upstream
proof still needed. No upstream PR without separate owner approval.

D78: user defers the five focused upstream candidates until rebase merge/release.
Read-only progress heartbeat def-jam-migration-test-updates ACTIVE every20min;
reports meaningful changes only, no executable tests/file/Git/publication work.
Broader autonomous side-effect automation was rejected; do not broaden it.

## 2026-10-07 20:16 — superseded migration validation handoff

## 7. Hand-off

### Active upstream v0.13.0 migration (D76)

Owner authorized implementation and all machine tests; live acceptance follows
automated success. Original parent e39cefba / toolkit 2ac8e705 remain accepted.
Isolated parent C:/Users/Vlad/code/defjam-upstream013, branch
migration/xboxrecomp-v0.13. Nested toolkit defjam/upstream-v0.13 replays 22
retained topics on exact v0.13.0 tag b3700e1d60bcd9c3dfaad734f45149b6f0bc5cb4,
tip bec01fa plus uncommitted semantic/fixture integrations. Four equivalent
upstream topics omitted; do not replay historical patches. Detailed evidence:
docs/research/toolkit-v0.13.0-execution.md and migration-plan.md.

Scratch root logs/upstream-013-work under ORIGINAL defjam-recomp. Original
generated C, builds, analysis and 22-file saves preserved in baseline/, hashes
in baseline/manifest.json. Only disposable runtime-data and guarded scenario
roots used. Original Debug was stale; never claim it as source-certified.

WARNING: my initial fixture/save/45410049 was one level too shallow. Seven
scenario checks used empty profiles; retain those logs as diagnostics, not
comparison baseline. Other original harness routes/soak used correct full root.
New fixture-corrected/save/UserData retains 15 profile files byte-identically;
manifest SHA dc67b420d610b33369428b55c6eaf136548b474ed25e0987ee581ebe9a9af023.
Old full was10/14, opt-ins3/5; audio/replay/repeat/visual require corrected rerun.
Original ten-boot soak stopped after2success/1hang; actual fence-wait stack,
later fence traffic, root unproven (baseline-hang.md). No golden changes.

Source gates pass: 656 toolkit tests +107 subtests, one explicit unsupported
MSVC sanitizer skip; all15 native CMake fixture projects; 5855 real-x86 snippet
and211 compiler-function vectors zero mismatches; parent173 tests. Fixtures
cover flag union, switches, event shadow ownership, four memory modes and
host input. Compiler adapters restore26 skipped probes/two silent passes.
Shared trusted helper hash c33ecc8c573b4690df4b454ace87c99c57f8910323bb0860a8ee6871a6697a1b.
Fresh analyze/lift and both builds certified; both CRT gates pass4divide+8fmod
values/depth, saves restored/no faults (candidate run172729/172754). A real
FPREM stale-C2 loop was fixed at producer; bounded negative controls/native
cases pass. Keep new HEAP_RECLAIM/EXT_VMA UNSET (presence enables even0),
TITLE_KEVENTS/GUEST_LOCK=0; default modes used for acceptance.

ACTIVE serial chain: build50384 completed; corrected baseline60163 runs seven
checks (combat,versus,ffa-result,two-matches,replay,repeat,visual). Firstcombat
crashed at startup nv2a_ack_thread+0xC48 readingXboxFEC0010B, before guestboot;
log regress-combat-20261007-172831. Corrected versus26/26, FFA-result28/28 and
two-matches35/35 pass including audio (172835/173343/173955). Corrected replay
passes80records/1800steps unchanged golden hash (regress-replay-20261007-174946).
Repeat passes80identicalrecords and unchanged golden (repeat-b-20261007-175445).
Current-profile visual passes4/4 (regress-visual-20261007-180444); corrected
baseline6/7, sole failure startupAC97 in firstcombat. Reference VY2 visual also
passes4/4 (180808). Original combat retry passes27/27 including audio (regress-combat-20261007-181124); retain the first intermittent crash as evidence.
Approved image manifest explicitly names the older vy2-hour-v1 career fixture;
six files and three image hashes verified, frozen as fixture-visual-approved
SHA607fefd546119be9750ed0b071bbd4ee8f21d69a11593569640216c6c55a090a.
Read-only AC97 review confirms protect-before-ownership publication window.
Candidate parent publishes shadow/base via Interlocked before protection;
deterministic forced-interleaving/read/rollback fixture passes; old-source
negative control compiles and fails6checks as expected. Parent173 pass again;
both builds refreshed/certified after AC97. Rebuild3509 completed0. Candidate19137
M2smoke passes frame40 (regress181708), now runs full14+10boots,
fiveopt-ins/Debug9+5boots/keyboard+SDL-two/late-pad+overlay. Scripts save JSON exit results;
no candidate full-matrix result yet. All games/builds/tests serial; no concurrent
gate. Tutorial pulse probe37477 cancelled idle; no harness recipe adopted.

Next: inspect corrected scenario results, debug candidate-only failures,
rerun affected checks without changing goldens/audio thresholds. Run
verify-preservation.py when idle (now correctly compares profile-only fixture
against frozen UserData subset) to check original saves/generated/build hashes
and record auxiliary CMake/dependency-source hashes. Source-only topic commits
after required game gates; git add-A --dry-run before each. Publish reviewed
toolkit SHA to fork before parent gitlink commit. No upstream PR without
separate permission. Preserve original uncommitted docs/worklogs; sync them
into candidate parent before committing. Restore generated .gitkeep if needed;
never stage generated C/game bytes/binaries/saves/logs/captures.

Keep v0.4 accepted build available for rollback and leave isolated candidate
ready for owner's physical/audio/UI live test. Private originDJFFNY-recomp;
publicDJFFNY-public-preview is separate history. NextM6 after migration remains
v0.5 true16:9, thenM5Proton/M9macOS.

## 2026-10-07 17:31 — superseded migration handoff

## 7. Hand-off

### Active upstream v0.13.0 migration (D76)

IMPORTANT: original fixture was malformed (save/45410049 instead of
save/UserData/45410049). Seven scenario checks therefore used empty profiles;
their results below are diagnostic, not the valid comparison baseline. All
other harness routes/soak used the correct complete runtime-data root. Preserve
old fixture/evidence. New fixture-corrected has identical frozen 15 profile
files under UserData, SHA dc67b420d610b33369428b55c6eaf136548b474ed25e0987ee581ebe9a9af023.
Queued 60163 reruns seven affected original checks after build 50384 succeeds;
candidate matrix must use fixture-corrected and wait for corrected baseline.
Cancelled old tutorial probe 37477 while idle; no new A recipe adopted.

Owner authorized execution and machine regression/debug tests, followed by his live test.
Original parent `e39cefb`, toolkit pin `2ac8e705`, upstream v0.13.0 `b3700e1`.
Isolated game `C:/Users/Vlad/code/defjam-upstream013`, branch
`migration/xboxrecomp-v0.13`; nested toolkit branch `defjam/upstream-v0.13`.
Twenty-two retained topic commits replayed (tip `bec01fa`), plus uncommitted
semantic flag/switch/event/accounting fixes and synthetic fixtures. Original
checkout/source and generated artifacts preserved. No candidate tests/build yet.

Scratch `logs/upstream-013-work`: `baseline/manifest.json` hashes saves, generated
sources, both binaries and analysis outputs. Original Release freshness passes;
old Debug preserved but stale, never use as a current baseline certificate.
`runtime-data/save` is a complete verified disposable copy (22 files), fixture
`fixture/fixture.json` hash `76eb2b23f4c9f499686d9c285873412393436ea993d79ec938b210e399f6a887`.
Baseline serial runner `run-baseline.ps1` finished; full
Release with 10-boot soak then all five opt-ins. First seven checks pass (unit
173, three goldens, fight, FFA and four-minute Terrordome FFA). Combat passes
all seven gameplay assertions but fails `audio.dry_queue` (64 post-anchor events;
`logs/scenarios/regress-combat-20261007-153102/report.json`). Versus passes all six
gameplay assertions but the same audio gate fails (13 events;
`logs/scenarios/regress-versus-20261007-153828/report.json`). Intro, crib and gym
also pass. Replay fails with an empty stream: the menu bootstrap never reaches
`Game.StartGame`, so this is not yet evidence of a changed simulation
(`logs/scenarios/regress-replay-20261007-154351/report.json`). The ten-boot soak
stopped after boot 3 hung (2 successful boots, not a completed ten-boot pass).
Full baseline is 10/14: the four failures are audio in combat and versus, replay
bootstrap and soak. `logs/hang-20261007-160822` includes an actual native fence-wait
stack, but later samples from the same PID show continued fence traffic; root
cause is unproven. Opt-in Terrordome One on One passes (240 seconds, median 60 fps),
and FFA-to-result passes 28/28 including audio. Two-matches passes 35/35 (15
combat assertions). Repeat reaches the fight twice but fails: 75 recorded versus
77 replayed records, overlapping records identical; the shorter stream is not a
completed golden pass (`regress-repeat-b-20261007-164156/report.json`). Visual
fails because the route never reaches crib/gym and all four checkpoint captures
are missing, not a measured pixel mismatch (`regress-visual-20261007-165202`).
Opt-ins finish 3/5. Read-only findings are in `toolkit-v0.13-baseline-hang.md`,
`toolkit-v0.13-baseline-audio.md`, and `toolkit-v0.13-baseline-repeat.md` under
`docs/research/`. Logs:
`baseline-full.log`, `baseline-optins.log`, eventual `baseline-exit-codes.json`.
Never run another harness/regress/unit gate concurrently with this baseline.
Toolkit collection initially failed for missing pefile (fusion tests); installed
pefile 2024.8.26 into isolated scratch python-deps. Full MSVC toolkit gate passes
652 tests and 107 subtests after closing 26 compiler-probe skips and two silent
uncompiled passes. Three skips remain: x86 discovery (VS2019 locator configured
for final run) and the unsupported MSVC signed-overflow sanitizer. Parity macro
and comment-location fixture updates preserve expected answers/negative controls.
All 15 native fixture projects pass, including default/title-event and all four
memory modes. A scratch PowerShell scalar-splat configure failure at fixture 11
was repaired by retaining an array; resume from index 10, preserving prior logs.
Preflight 80799 and 10279 correctly stopped on failed prerequisites.
Real x86 conformance then needed g_itail_site TLS scaffolding in all three
harness generators; repaired without changing production behavior. It now
passes 5843 snippet vectors and 211 compiler-function vectors (zero mismatches).
Final toolkit gate: 654 tests, 107 subtests, one explicit unsupported MSVC
sanitizer skip. Parent 173 tests pass. Both builds linked, but 71202 stopped at
Release CRT: FPREM never cleared FXAM's C2, so _CIfmod loops on JP. Clear C2 at
the completed remainder producer; bounded compiled negative controls and real
x86 cases added. Resume 53267 passes 5855 snippet vectors and 211 compiler
vectors, zero mismatches, and 173 parent tests. Corrected a nearest-even unit
oracle to the unambiguous 8/3 case; 50384 passes 656 toolkit tests/107 subtests
with one unsupported sanitizer skip and is re-lifting/building both presets.
Native/conformance/parent unit/build chain stops at the first failed gate. Its
scratch scripts write `candidate-toolkit-exit.json`,
`candidate-preflight-exit.json`, and `candidate-build-exit.json`.
Candidate game gates 15604 were stopped while verified idle. Relaunch after the
original tutorial diagnostic requeued behind both fresh builds/selftests (the
old 98713 queue correctly stopped at the failed CRT prerequisite).
The game matrix is initial M2 smoke, full 14/10 boots,
five opt-ins, nine targeted Debug checks/5-boots, then keyboard/SDL/overlay
scenarios. Fresh-build chain also asserts the existing guest CRT self-tests in
both presets. `run-host-smokes.py` explicitly overrides scenario PAD_HOST=0 so
the converted script actually reads through host input. No candidate result yet.

Flag fixture review gaps (SETP persistence, SAHF preserving OF, second x87
status comparison) are addressed in source, awaiting compiled execution.
Repeat A stops at step 1651 and B at damage step 1703. Retained A60/A110 and B60
captures show WEAPON tutorial A CONTINUE: this is a visible tutorial pause,
not a simulation hang. The bootstrap closes USB input at match start and the
step-timed fighter input cannot dismiss paused UI. Preserve fixtures/golden;
test finite USB A presses around the observed 44-second window, then demand the
full unchanged 80-record hash before adopting any harness adjustment.
Next: after baseline completion run complete
MSVC toolkit tests, native fixtures in default and new opt-in modes, real-x86
conformance. Fresh analyze/lift/build both presets in isolated game; all 14
Release checks plus five opt-ins and targeted Debug/host-input/overlay tests.
Classify failures against baseline; never refresh goldens to normalize them.
Verify original save hashes again before live test. Source-only topic commits,
publish reviewed fork SHA before any parent pin commit; upstream PRs still need
separate permission. Parent PROGRESS/worklogs include earlier uncommitted analysis
docs; preserve them. Original .gitkeep deletion may be a sandbox false report.

v0.4.0 remains accepted/released. Next M6 feature after migration remains v0.5.0
true 16:9, then M5 Proton and M9 macOS. Keep private/public history separate and
game/generated code, binaries, saves and captures local.

## 2026-10-07 15:23 — superseded analysis-only handoff

## 7. Hand-off

### xboxrecomp v0.13.0 migration: analysis complete, execution pending

Owner asked for release/patch risk analysis and a migration plan. Report:
`docs/research/toolkit-v0.13.0-migration-plan.md`. Parent reviewed `e39cefb`,
toolkit pin `2ac8e705`, target tag `b3700e1`, common base `1409a7d`. Source evidence:
`logs/upstream-013-analysis/source-evidence.json` (57 upstream paths, 21 overlapping,
12 aggregate merge conflicts). Checked-out toolkit and gitlink unchanged; no
candidate build/tests/game run. Five submitted upstream PRs #167–171 are merged.

Next implementation: freeze current v0.4 Release/Debug baseline and a hashed
fixture/disposable complete save root; create isolated game/toolkit checkouts;
deduplicate four equivalent production topics but retain MSVC/multiple-source
test helper support and kernel residuals. Fix clean-hunk switch census/fallback
and flag-emission incompatibilities, then reconcile event/dispatch/allocator/USB
conflicts. Keep new heap/VMA/event/guest-lock opt-ins off initially. Fresh analyze,
lift and both builds; source/native/conformance gates; Release 14-check full plus
all five opt-ins and stronger soak; targeted Debug; host-input/overlay/owner tests.
Never run harness/regress or unit-only gates concurrently. Do not change goldens
to normalize candidate failures. Publish the accepted fork commit before parent
pin integration. New upstream PRs still require separate owner approval.

### Continuing M6 / public publication context

v0.4.0 is released (D74/D75): public `6ffb407`, private publication merge
`5cfc469`, toolkit `2ac8e70`. Input and launcher/overlay guides remain current.
The v0.4 save tree has four IDs and Story routes pick the third; preserve that
ordering in migration fixtures. Prior v0.4 full attempt was 12/14, followed by
passing corrected crib/gym retries; this is not a new all-green migration result.
The older PR-only handoff's aa1a1b9/v0.1.0 pins are historical, not current.

After the toolkit migration, M6's next planned feature remains v0.5.0 true 16:9;
see `docs/research/m6-handover.md` section 7. Keep game/generated code, binaries,
saves and captures local. Preserve private/public history separation. The exact
public-sync instructions and previous M6/PR handoff are archived in
`docs/worklog/2026-10-handoffs.md`; do not rewrite published release tags.

# Superseded handoff (M6 v0.4.0 prepared), replaced 2026-10-07

## Superseded 2026-10-07 14:58 — v0.13.0 migration analysis

### M6 v0.4.0 launcher and overlay: released 2026-10-07 (D74, D75)

v0.4.0 is public: tag `v0.4.0` at `6ffb407`, private main `5cfc469`, toolkit fork `defjam/m6` at `2ac8e70`. Guide `docs/launcher-and-overlay.md`,
notes `docs/releases/v0.4.0.md`. The sync recipe is `logs/m6-work/sync-public.py` (local): copy only changed files the public tree has plus the new ones
it names, `git checkout` anything that differs only by line endings or redaction (release.yml, CMakePresets.json, CONTRIBUTING.md, docs/releasing.md,
docs/releases/v0.1.0.md, docs/research/*, tests/golden), hand-write the public PROGRESS.md, wait for CI, tag, inspect the draft, publish.

Open ends: render scale and gamma are restart settings (live render scale not attempted); the overlay does not pause the game; a pad SDL does not know
as a gamepad cannot navigate; the C++ UI is not built in hosted CI; the Story harness routes pick the THIRD user ID (the save area has four profiles) and
break again if profiles change (`scripts/harness.py`, comment above `crib`). Not validated: Switch Pro and DirectInput pads, small windows, Proton and Steam Deck.

Next: v0.5.0 true 16:9 (`docs/research/m6-handover.md` section 7): first find out, with read-only sub-agent investigations (CLAUDE.md 5a), whether the game has
its own widescreen path (the kernel already answers the video-flags query with widescreen and HDTV set; the title still sets 640x480), where it builds its
projection matrix and viewport, and how the HUD is placed; then design. The presentation side already takes any aspect (`d3d8_present_set_aspect`); the
`aspect` setting needs a `16:9` choice. Goldens stay 4:3. Then v0.6.0 texture packs. Ask Vlad before opening any upstream PR.

### PR-only maintenance in this chat (D62)

Owner assigns this chat xboxrecomp PR checkups when asked and possible future
named submissions after approval. Another agent handles remaining game milestones.
No recurring automation/background monitor was requested or created.

Start with docs/research/upstream-pr-maintenance.md: all five submitted PR links,
14 unopened candidate scopes/dependencies, ownership and follow-up procedure.
docs/research/first-five-pr-submissions.md has exact heads/branches/test results.
docs/research/upstream-pr-review-and-plan.md retains detailed risks, source mapping,
graphics staging and20 held topics. Refresh upstream overlap before new extraction.

2026-10-05 07:01 local state check: #167–171 still OPEN. This was not a new review
of all comments/checks. All five source worktrees, accepted toolkit and both game
checkouts were clean before this docs-only handoff. No game test process remains.
Preserve worktrees/evidence for review fixes; no source/build/pin cleanup remains.

PR source suites and real-x86 comparisons passed; same33 baseline skips disclosed.
Combined game attempt6/9 followed by passing serialfight and second-profile
crib/gym/preview retries, with save restoration. See submission record; do not
call the original attempt9/9 or claim isolated branches are playable game builds.
Never run harness/regress commands in parallel, including unit-only gates.

Game/toolkit pin aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc unchanged. Public
DefJamFFNY-recomp sourcev0.1.0 immutable89688c0; private DJFFNY-recomp history
stays private. No game/generated code, binaries, saves or logs published. Only
two fingerprinted D58 README visuals permitted. Original saves remain untouched.
Another agent owns M6, M5Proton, M9macOS and gameplay backlog; no milestone
definitions or acceptance gates changed by this handoff.

## 7. Hand-off

### M6 v0.4.0 launcher and overlay: prepared, waiting for Vlad's play-test (D74)

Branch `m6-overlay` (private, off main `38a8fb2`); toolkit fork `defjam/m6` at `2ac8e70` (published); the parent gitlink points at it.
Quick regression 5 of 5, full 12 of 14 with the two Story failures explained and fixed (work log 2026-10-07 14:10). Not tagged, version
already 0.4.0 in CMake. **To release, once Vlad says so:** write `docs/releases/v0.4.0.md` (what is new, build note: the first configure now
also downloads Dear ImGui and the project builds C++, the checks above, his play-test words, limits), merge the PR, sync the public checkout
(copy only changed files the public tree has plus the new ones named in the previous sync script `logs/m6-work/sync-public.py`, add `cmake/imgui.cmake`,
`thirdparty/imgui-LICENSE.txt` and the new `src/hooks` files; `git checkout` anything that differs only by line endings or redaction; hand-write the
public PROGRESS.md entries), wait for CI, tag, inspect the draft (if the workflow's draft step fails with HTTP 500 again, create the draft by hand
with the same arguments), publish. The game executable is not built in CI, so the C++ UI is only checked locally.

What Vlad is to try: the launcher on every launch (mouse, keyboard, pad), Skip checkbox and Shift/--launcher to bring it back, F1 and the pad chord
for the overlay, changing a key binding and a pad button and seeing them work, window size and full screen live, the controllers page (live pads,
rumble test), text size at his resolutions, that the game does not react while the overlay is open.

Open ends: render scale and gamma are restart settings (live render scale was not attempted); the overlay does not pause the game; a pad SDL does
not know as a gamepad cannot navigate; the Story harness routes depend on the save area's profile order (third entry now). Next: v0.5.0 true 16:9
(`docs/research/m6-handover.md` section 7), then v0.6.0 texture packs.

---

# Superseded handoff (M6 v0.3.0 released), replaced 2026-10-07

## 7. Hand-off

### M6 v0.3.0 input: released 2026-10-07 (D63-D66, D72, D73)

v0.3.0 is public: tag `v0.3.0` at `8a6074f`, private main `4e295ff`, toolkit fork `defjam/m6` at `b1f002d`
(settings text type, input mapping rules, host input layer, rumble decode and strengthening, hub hot-plug).
Guide `docs/10-input.md`, reference `docs/settings-reference.md` (a unit test keeps it complete), notes
`docs/releases/v0.3.0.md`. Diagnostics for unattended runs: `CLAUDE.md` 4b and `docs/10-input.md`.

Open ends: pointer-driven menus were dropped (D72). One shared `[gamepad]` map for all pads. No rebind UI or live
apply of the pad map until the overlay. Not validated: Switch Pro and DirectInput pads, three or four physical pads at
once, input latency, cursor hiding and confinement in full screen (never reported on), Proton and Steam Deck (a Deck's
pad appears as an XInput pad under Proton; Steam Input may hide pads from SDL). Upstream candidates 23-26 are logged in
`docs/research/upstream-pr-maintenance.md`; no upstream PR without his approval.
Release-process note: on 2026-10-07 the Source release workflow's draft step failed with HTTP 500 twice; creating the
draft by hand with the same arguments worked (see the work log).

Next: v0.4.0 overlay and launcher (`docs/research/m6-handover.md` section 6): one UI code base on the D3D11 device
(Dear ImGui is the suggestion, a new dependency: same rules as SDL3), a neutral pad to the game while it is open, live apply
where possible (`render_scale` and `gamma` need a restart), and the launcher shown before the guest boots. Ask Vlad when the
launcher appears (first run, a switch, a held key). Then v0.5.0 true 16:9 and v0.6.0 texture packs. The testing roadmap
(`docs/08-testing-roadmap.md`) continues separately.

---

# Superseded handoff (M6 v0.3.0 prepared), replaced 2026-10-07

## 7. Hand-off

### M6 v0.3.0 input: prepared, waiting for Vlad's play-test (D63-D66, D72)

Branch `m6-input` (private, off main `18b127d`); toolkit fork branch `defjam/m6`
(worktree `logs/m6-work/toolkit`) is seven commits above `c7059bf`, the last being `b1f002d` (rumble made felt, hub hot-plug); the
parent gitlink points at it. Full Release regression 14 of 14 (work log 2026-10-06 19:15), then 8 of 8 after Vlad's
first feedback (22:20): his open question is whether rumble is now felt (`docs/10-input.md` troubleshooting). Not tagged,
version not bumped (CMake still says 0.2.4). **To release, once Vlad says so:** bump
`project(... VERSION 0.3.0)`, write `docs/releases/v0.3.0.md` (supported input, the checks above, his
play-test words, limits), merge the PR, sync the public checkout the usual way (skip `PROGRESS.md`,
`docs/worklog/*`, `docs/research/upstream-pr-*.md`, `docs/research/m6-handover.md`), wait for CI, tag,
inspect the draft, publish. CI gained `input_map` and `input_host` fixtures and
`tests/unit/test_input_native.py`; the first hosted run also downloads SDL3, so watch that job.

What Vlad is to try: a DualSense and an Xbox pad each play a match (and rumble is felt), the keyboard
plays a match, a pad against the keyboard, two pads against each other, rebinding a key or a pad control
in `settings.ini` and restarting, the pointer hiding after two seconds and staying inside the window in
full screen (not scriptable here: `GetCursorInfo` shows no cursor in the tool session), focus loss
releasing everything. The default keys are a guess at ergonomics (`docs/10-input.md`); the in-game
action behind each Xbox control was not mapped, so ask him whether any default feels wrong.

How to test without hardware: `docs/10-input.md` "Diagnostics" and `CLAUDE.md` 4b. Scripted pad to keys
(`RECOMP_PAD_SCRIPT_KEYS=1`) and to SDL virtual pads (`RECOMP_INPUT_VIRTUAL_PADS=n`,
`RECOMP_PAD_SCRIPT_VIRTUAL=1`) both play a match through the real input path. `RECOMP_RUMBLE_LOG=1`
logs what the game sends.

Open ends: pointer-driven menus were dropped (D72). One shared `[gamepad]` map for all pads. No rebind UI
or live apply of the pad map until the overlay (v0.4.0, `docs/research/m6-handover.md` section 6). A pad plugged
in after start is added to the hub as a controller (checked with a virtual pad). Not validated: Switch Pro and DirectInput pads (SDL names
them; nothing here ran on one), three or four physical pads, input latency, Proton and Steam Deck (a Deck's
pad appears as an XInput pad under Proton; Steam Input may hide pads from SDL).
Upstream candidates 23-26 are logged in `docs/research/upstream-pr-maintenance.md`; no upstream PR without his approval.

Then: v0.4.0 overlay and launcher, v0.5.0 true 16:9, v0.6.0 texture packs
(`docs/research/m6-handover.md`). The testing roadmap (`docs/08-testing-roadmap.md`, v0.2.4 is the latest
tooling release) continues separately: next a repeatable loading window, sounds tied to events, a human
win, save and load, cutscene skipping (`docs/research/testing-harness-handover.md`).

---

# Superseded handoff (testing harness v0.2.4 and M6 after v0.2.0), replaced 2026-10-06 19:15

## 7. Hand-off

### Gameplay test harness: v0.2.4 released (D67-D71)

v0.2.2, v0.2.3 and v0.2.4 are public (work log 2026-10-06 13:55). No branch is open.
Local-only: the still-screen baselines in `<data>/test-baselines/story-tour-v1`.

Next in the roadmap, in order: a repeatable loading window (the two crowds,
`docs/research/crowd-nondeterminism.md`; equal `[TEST-DRAWS]` counts at step 1 are
the test), sounds tied to events, a human win, save and load, cutscene skipping.
`docs/research/testing-harness-handover.md` has the list; `docs/09-testing-harness.md`
is the guide. If `replay` fails with the CPU fighter near the crowd, that is the
known cause, not a regression.
The audio chip model's trap handling is a to-do, not scheduled (backlog 1b, D70).
M6 (input, overlay, 16:9, texture packs) is paused; hand-over in
`docs/research/m6-handover.md`.

### M6 after v0.2.0 (D63-D65)

v0.2.0 is accepted and published: public tag `v0.2.0` at `933afcf`, private main `c01e3f6`.
v0.2.1 (Terrordome crash fix, D66) follows it; the full regression has not been run to completion
on the v0.2.1 lift, so run it before the next change to generated code.
The agent taking M6 forward starts from `docs/research/m6-handover.md`: state,
workflow, the Terrordome fix and the stubs it left open, then input (v0.3.0), overlay and
launcher (v0.4.0), true 16:9 (v0.5.0) and texture packs (v0.6.0). Vlad play-tests
each release before the next slice starts.
Toolkit fork branch `defjam/m6` at `c7059bf` (worktree `logs/m6-work/toolkit`).
To build against new toolkit commits: commit in the worktree, then
`git -C tools/xboxrecomp checkout --detach <sha>`; `build.ps1 -ToolkitDir` fails
because the pipeline state lives in the submodule's `game_files`.
The testing roadmap is `docs/08-testing-roadmap.md`; another agent takes it after
v0.2.0. Baseline build for A/B runs: `logs/m6-work/build-baseline.ps1` and
`logs/m6-work/run-baseline.py` (local, ignored).

---

# Superseded testing handoff, increment 1, 2026-10-05 20:01

## 7. Hand-off

This isolated testing branch owns harness extensions only. Start with
`docs/research/testing-harness-handover.md`: exact implemented/pending scope,
verification, commands, data policy, next steps and merge surfaces.

Worktree `C:\Users\Vlad\code\defjam-test-harness`; branch
`tests/gameplay-harness`; base private main85cfa71; toolkit c7059bf unchanged.
Main `defjam-recomp` belongs to the PC/Terrordome-fix agent. No merge/push.

Checkpoint: 113/113 project/native unit tests pass under MSVC2019;
`logs/unit-final.txt` is the final output. No full build/game run/live calibration
in this worktree. Shared crib/gym and experimental live session driver, hashed
fixtures, visual differences, audio metrics and structured reports are implemented.
Combat-state producer, simulation-frame inputs, exact fighters/RNG, chained
fight/results/menu/Story transitions and speech/SFX alignment remain unfinished.
Missing required combat telemetry reports BLOCKED and returns nonzero.

Continue with isolated fresh analyze/lift/build and local disposable fixture;
coordinate game runs because another agent's older scripts may kill by name.
Never bypass pipeline certificates or share mutable generated/build folders.
Preserve all main changes when coordinating the eventual merge. Current milestone
definitions and public releases are unchanged.


# Superseded worktree handoff, 2026-10-05

Future testing To-Do requested 2026-10-05 is in `docs/08-testing-roadmap.md`,
linked from the improvement backlog. Source-only dependency audit: public Def Jam
pin is 15 commits atop upstream main1409a7d; TimeSplitters pin diverges305/143;
Mercenaries embeds its runtime and has no common fetched upstream ancestor.
No candidate upgrade/build/game run performed; another agent's active PC-feature
changes and toolkit checkout were left intact.

### Gameplay test harness: v0.2.3 released, v0.2.4 in progress (D67-D70)

v0.2.2 and v0.2.3 are public. v0.2.4 continues the roadmap on a new branch from
main: visual baselines (still screens first, then step-anchored fight captures),
a four-fighter match played to its result, two pads, several matches in one
launch. `docs/research/testing-harness-handover.md` has the ordered list and the
working notes; `docs/09-testing-harness.md` is the guide.
The audio chip model's trap handling is a to-do, not scheduled (backlog 1b, D70).
M6 (input, overlay, 16:9, texture packs) is paused; hand-over in
`docs/research/m6-handover.md`.

### M6 after v0.2.0 (D63-D65)

v0.2.0 is accepted and published: public tag `v0.2.0` at `933afcf`, private main `c01e3f6`.
The agent taking M6 forward starts from `docs/research/m6-handover.md`: state,
workflow, the Terrordome crash (v0.2.1 first), then input (v0.3.0), overlay and
launcher (v0.4.0), true 16:9 (v0.5.0) and texture packs (v0.6.0). Vlad play-tests
each release before the next slice starts.
Toolkit fork branch `defjam/m6` at `c7059bf` (worktree `logs/m6-work/toolkit`).
To build against new toolkit commits: commit in the worktree, then
`git -C tools/xboxrecomp checkout --detach <sha>`; `build.ps1 -ToolkitDir` fails
because the pipeline state lives in the submodule's `game_files`.
The testing roadmap is `docs/08-testing-roadmap.md`; another agent takes it after
v0.2.0. Baseline build for A/B runs: `logs/m6-work/build-baseline.ps1` and
`logs/m6-work/run-baseline.py` (local, ignored).

### PR-only maintenance in this chat (D62)

Owner assigns this chat xboxrecomp PR checkups when asked and possible future
named submissions after approval. Another agent handles remaining game milestones.
No recurring automation/background monitor was requested or created.

Start with docs/research/upstream-pr-maintenance.md: all five submitted PR links,
14 unopened candidate scopes/dependencies, ownership and follow-up procedure.
docs/research/first-five-pr-submissions.md has exact heads/branches/test results.
docs/research/upstream-pr-review-and-plan.md retains detailed risks, source mapping,
graphics staging and20 held topics. Refresh upstream overlap before new extraction.

2026-10-05 07:01 local state check: #167–171 still OPEN. This was not a new review
of all comments/checks. All five source worktrees, accepted toolkit and both game
checkouts were clean before this docs-only handoff. No game test process remains.
Preserve worktrees/evidence for review fixes; no source/build/pin cleanup remains.

PR source suites and real-x86 comparisons passed; same33 baseline skips disclosed.
Combined game attempt6/9 followed by passing serialfight and second-profile
crib/gym/preview retries, with save restoration. See submission record; do not
call the original attempt9/9 or claim isolated branches are playable game builds.
Never run harness/regress commands in parallel, including unit-only gates.

Game/toolkit pin aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc unchanged. Public
DefJamFFNY-recomp sourcev0.1.0 immutable89688c0; private DJFFNY-recomp history
stays private. No game/generated code, binaries, saves or logs published. Only
two fingerprinted D58 README visuals permitted. Original saves remain untouched.
Another agent owns M6, M5Proton, M9macOS and gameplay backlog; no milestone
definitions or acceptance gates changed by this handoff.


<!-- Archived 2026-10-05 07:04; superseded by PR-only ownership handoff D62 -->

## 7. Hand-off

### First five upstream PRs submitted (D61)

Owner authorized candidates1–5; all five are OPEN/ready and attached to the chat:
#167 PUSHAD/POPAD; #168 entry hooks/boundaries; #169 caller cleanup;
#170 MMIO-aware REP MOVS; #171 Windows directory-search lifetime.
Exact URLs, heads, branches and evidence: docs/research/first-five-pr-submissions.md.
Five independent commits directly on upstream1409a7d, source/synthetic tests only.
No game/generated code, binaries, saves or captures published. Attribution vyanhursky.
Descriptions disclose AI assistance and scope/platform/provenance limitations.

All five full tools suites pass:564/567/570/563/561, plus57subtests, same33
environment skips as baseline561. New MSVC2019 /O2 compiled fixtures really run.
Real-x86 conformance:5801/5741/5741/5783/5741 snippets +211 function vectors
each, zero mismatches. VS2019 vcvars discovery adapter only; no comparison changes.
Four lifter baseline-fail/fix-pass proofs; directory baseline builds/fails then
fixed Release CTest1/1 passes with144 abandoned search cycles. NO_MORE_FILES
already upstream: ordinal301 untouched. Existing directory concurrency not fixed.
Both final read-only source reviews found no blocker; reports in docs/research.

Fresh combined-fork full game attempt:6/9, three goldens, intro and3/3 soak pass.
Fight was interrupted by our parallel unit gate's cleanup; Story routes stopped
at choices with first disposable profile. Never run harness/regress gates in
parallel: even --only unit kills game processes on exit. Serial reruns follow:
Serial fight PASS: 122 s of fight, median 120 presents per 2 s, minimum 111. Second-profile crib/gym PASS: crib, gym and move preview reached; save identical=True.
Logs:pr-first-five-game-regress.log and pr-first-five-targeted-game.log/json under
logs/rebase-work. Independent upstream branches are not standalone game builds.
Historical accepted game9/9 and owner1hStory remain historical, not a fresh9/9 claim.

Next: respond to upstream reviewer feedback within approved first-five scope.
Other14 candidates and20 held/coordination topics require owner discussion/approval.
Do not open more PRs or contact unrelated authors. Graphics remain staged and
validated in dependency order; follow upstream-pr-review-and-plan.md.

CLI worktrees:C:/Users/Vlad/code/xboxrecomp-prs/{pushad,entry-hooks,caller-cleanup,
mmio-rep,directory}; test venv there. Never default-push their upstream tracking
branches: push only explicit origin branch. No source work in accepted game pin.

Game pin aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc unchanged. Public game
DefJamFFNY-recomp v0.1.0 immutable89688c0; private DJFFNY-recomp history stays
private. Rebase test data uses logs/rebase-work/baseline-path.txt/runtime-data;
complete save guards restore every route. Original C:/Users/Vlad/code/defjam save
untouched. Only two fingerprinted D58 README visuals may be committed.
Roadmap thereafter:M6, M5Proton, M9macOS; gameplay backlog unchanged.

<!-- Archived 2026-10-04 22:39; superseded by submitted first-five handoff -->

## 7. Hand-off

### First five upstream PRs authorized and in preparation (D61)
Owner explicitly approves opening candidates1–5; complete tests/review/descriptions
and publish source branches/PRs without re-asking. Other14 candidates remain
unapproved. No PRs have been opened yet. Do not post unrelated messages/issues.

Five isolated CLI toolkit worktrees, all based on freshly fetched upstream
1409a7d7801d3e931fb1074be6104209ddd9a33e:
C:/Users/Vlad/code/xboxrecomp-prs/{pushad,entry-hooks,caller-cleanup,mmio-rep,directory}
Branches:defjam/{fix-pushad-popad,entry-hooks,caller-cleanup,mmio-rep-copies,directory-lifecycle}.
No commits/pushes yet. Use git -C EXACT_WORKTREE -c safe.directory=EXACT_WORKTREE
for each operation; no default parent cwd. Current source stable, focused tests
being strengthened/reviewed. Rebase branch/game gitlink stay ataa1a1b9.

Fresh report:first-five-upstream-refresh.md.11 openPRs unchanged. PR5 is Windows
search lifetime only: ordinal301 NO_MORE_FILES18 already in upstream, so omit
the combined fork's broad host mapper (overlaps#157). It tests direct closes and
tagged guest handles created through NtOpenFile; untagged handles intentionally
are not closed by bridge_NtClose. Test thunk lookups must occur immediately before
each call because resolver selects a shared thread-local bridge slot.

Task-local venv:xboxrecomp-prs/.venv, system packages pluspefile (baseline full
suite otherwise cannot collect fusion). MSVC2019 14.29/SDK19041 installed;
new compiled fixtures use cl /O2. Full suites pass with33 existing environment
skips/57subtests:pushad564,entry566,caller568,MMIO563,directory561 before latest
entry/caller test additions. Re-run changed suites. No GNU compiler present.
Use -rs to record skip reasons honestly; don't claim all compiled tests ran.

Real-x86 conformance uses ignored discovery-only wrapper
logs/rebase-work/run-conformance-2019.py, pointing at installedvcvars32 rather
than upstream's VS2022-only auto-discovery. Upstream assembler/native/lift/corpus
tests and comparisons unchanged. All branches5741 vectors +211function vectors,
0 mismatches; new PUSHAD case raises to5801+211. Logs:pr-TOPIC-conformance.log.
Four synthetic baseline failures/fix passes:validate-first-five.py and logs.
Directory Release runtime/fixture builds;validate-directory-baseline.py proves
baseline compiles/fails then fixed passes (144cyclefixture). No production status
mapping/logging changes. Both close paths release search handles/table ownership.

Full combined-fork Release game regression is running in exec session88535
against disposable runtime-data derived from logs/rebase-work/baseline-path.txt;
output pr-first-five-game-regress.log. This is combined-fork evidence, not a game
build from each isolated upstream branch. No original save/game pin changes.
Final read-only reviews delegated: first-four-pr-final-review.md and
directory-pr-final-review.md. Resolve blockers, don't infer gameplay from them.

Next: inspect final diffs/skip reasons, finish needed tests and game regression,
write self-contained PR bodies with exact tested titles/compiler/provenance,
source-only staging/hygiene and commits on each branch, push authorized branches
to vyanhursky/xboxrecomp and create five focused PRs into sp00nznet/xboxrecomp.
Attach every created PR with attach_artifact. Don't duplicate prerequisite hunks.
Keep PROGRESS/worklog/report current with URLs/exactSHAs/checks. User requests
vyanhursky attribution only. AI assistance/provenance disclosure is required.

Public game remains DefJamFFNY-recomp sourcev0.1.0 immutable89688c0, pin aa1a1b9;
private DJFFNY-recomp history stays private. Never push its history to public.
Only two fingerprinted D58 visuals permitted; no game/generated code/binaries.
Roadmap thereafter:M6, M5Proton, M9macOS; gameplay backlog unchanged.

# Handoffs, October 2026

## Superseded by first-five preparation authorization, 2026-10-04

## 7. Hand-off

### Upstream contribution discussion, 2026-10-04
Graphics follow-up (D60): owner asks whether staged work can span releases.
Recommendation is ordered small waves, release-safe builds/defaults, optional
incomplete paths and tests at every intermediate merge. No all-or-nothing train;
#162 contract first, then foundations/backend correctness/GPU execution and
later particles/performance. Two-three weeks is not a guaranteed schedule.
Public108 historical .patch files are archived; active fork15 commits ataa1a1b9;
19 candidates plus20 held topics are another decomposition, not39 new patches.
See the master report's follow-up. No code or submission authorization added.

Owner requests explanation and overlap review before any xboxrecomp PR.
No upstream PR/issue/comment/message was created; no toolkit source, topic
branch, accepted game build or pin changed. Do not open PRs or contact authors
until explicitly authorized. Proposal:19 new-PR candidates, first five contained
fixes, then small waves of2–3 after named approval. This is not19 ready PRs.

Read `docs/research/upstream-pr-review-and-plan.md` first; deeper catalogues:
`upstream-lifter-pr-plan.md`, `upstream-runtime-pr-plan.md`,
`upstream-render-pr-plan.md`. Whole-fork39 boundaries:19 selected,20 held for
coordination/optional/redesign. First five: PUSHAD/POPAD, entry hooks/boundaries,
failed-indirect-call stack cleanup, MMIO-aware REP, directory-search lifecycle.

Audited all11 open PRs and22 merged since v0.12.0 (2026-09-27). Upstream main
1409a7d7801d3e931fb1074be6104209ddd9a33e is exactly our fork base; all22 merges
are ancestors. Private ignored API evidence:logs/rebase-work/upstream-pr-review/
plus upstream-review-selection.json/tree/open/closed captures. Refresh before
each wave. Flags#159 and executor/backend#162 compete with our implementation;
events#160 and audio/thread/USB umbrella#128 need coordination. Omit duplicate
logging overflow#157; compose writable routing with#133 and memory with#158.
Four-pad input, P8 sampling, shader interpreter/combiners already exist upstream.

After discussion approves preparation: isolate each scope on current upstream,
carry self-contained synthetic tests/helpers, prove baseline fail/fix pass,
run full upstream tools pytest and real-x86 conformance plus runtime/game checks.
Existing combined-fork results are not those results. Review exact drafts with
owner before named submissions. No lifted guest bodies or game assets in public
reproducers. Follow CONTRIBUTING/provenance/licences and disclose AI assistance.
Resolve optimized Clang NEG32 before broad flags work; switch recovery can
override intentionally changed valid targets; GPU fallback can drop first-use
batches and point sprites need one/two-point/error cases. HLSL provenance and
expanded numeric/pixel tests are submission gates, not assumed done.

Accepted toolkit pin stays aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc; fork branch
vyanhursky/xboxrecomp defjam/rebase-2026-10. Game regressions9/9 each preset,
Debug20/20 boots, extra routes5/5 and owner one-hour Story acceptance stand.
Do not relift/rebuild/run the game for docs-only planning.

Public game:vyanhursky/DefJamFFNY-recomp, checkout DJFFNY-public-preview.
Source v0.1.0 tag89688c040f42629dfc369648cfb60004a841967b is immutable;
public closure main2ae6032 CI37224118030:7/7, release37223822550:9/9.
Private checkout defjam-recomp retains private DJFFNY-recomp history; never
push it to public. Private closure414d1ac CI37224111755 succeeded.
Two D58 README visual exceptions only; no binaries/game/generated source.
Current checks:96 project,533 toolkit/107subtests (one skip), native saves19,
five CTest cases; supported MSVC selection retains documented Clang limits.

### Standing gameplay backlog
Two-pad gameplay, long-session memory, rare `sub_001A3310` crash/silent boot,
loading bar, black profile thumbnails, Blazin' film grain and half-pixel alignment.
M6 PC features, then M5 Proton and M9 macOS after contribution-scope discussion.

## Superseded by graphics staging clarification, 2026-10-04

## 7. Hand-off

### Upstream contribution discussion, 2026-10-04
Owner requests explanation and overlap review before any xboxrecomp PR.
No upstream PR/issue/comment/message was created; no toolkit source, topic
branch, accepted game build or pin changed. Do not open PRs or contact authors
until explicitly authorized. Proposal:19 new-PR candidates, first five contained
fixes, then small waves of2–3 after named approval. This is not19 ready PRs.

Read `docs/research/upstream-pr-review-and-plan.md` first; deeper catalogues:
`upstream-lifter-pr-plan.md`, `upstream-runtime-pr-plan.md`,
`upstream-render-pr-plan.md`. Whole-fork39 boundaries:19 selected,20 held for
coordination/optional/redesign. First five: PUSHAD/POPAD, entry hooks/boundaries,
failed-indirect-call stack cleanup, MMIO-aware REP, directory-search lifecycle.

Audited all11 open PRs and22 merged since v0.12.0 (2026-09-27). Upstream main
1409a7d7801d3e931fb1074be6104209ddd9a33e is exactly our fork base; all22 merges
are ancestors. Private ignored API evidence:logs/rebase-work/upstream-pr-review/
plus upstream-review-selection.json/tree/open/closed captures. Refresh before
each wave. Flags#159 and executor/backend#162 compete with our implementation;
events#160 and audio/thread/USB umbrella#128 need coordination. Omit duplicate
logging overflow#157; compose writable routing with#133 and memory with#158.
Four-pad input, P8 sampling, shader interpreter/combiners already exist upstream.

After discussion approves preparation: isolate each scope on current upstream,
carry self-contained synthetic tests/helpers, prove baseline fail/fix pass,
run full upstream tools pytest and real-x86 conformance plus runtime/game checks.
Existing combined-fork results are not those results. Review exact drafts with
owner before named submissions. No lifted guest bodies or game assets in public
reproducers. Follow CONTRIBUTING/provenance/licences and disclose AI assistance.
Resolve optimized Clang NEG32 before broad flags work; switch recovery can
override intentionally changed valid targets; GPU fallback can drop first-use
batches and point sprites need one/two-point/error cases. HLSL provenance and
expanded numeric/pixel tests are submission gates, not assumed done.

Accepted toolkit pin stays aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc; fork branch
vyanhursky/xboxrecomp defjam/rebase-2026-10. Game regressions9/9 each preset,
Debug20/20 boots, extra routes5/5 and owner one-hour Story acceptance stand.
Do not relift/rebuild/run the game for docs-only planning.

Public game:vyanhursky/DefJamFFNY-recomp, checkout DJFFNY-public-preview.
Source v0.1.0 tag89688c040f42629dfc369648cfb60004a841967b is immutable;
public closure main2ae6032 CI37224118030:7/7, release37223822550:9/9.
Private checkout defjam-recomp retains private DJFFNY-recomp history; never
push it to public. Private closure414d1ac CI37224111755 succeeded.
Two D58 README visual exceptions only; no binaries/game/generated source.
Current checks:96 project,533 toolkit/107subtests (one skip), native saves19,
five CTest cases; supported MSVC selection retains documented Clang limits.

### Standing gameplay backlog
Two-pad gameplay, long-session memory, rare `sub_001A3310` crash/silent boot,
loading bar, black profile thumbnails, Blazin' film grain and half-pixel alignment.
M6 PC features, then M5 Proton and M9 macOS after contribution-scope discussion.

## Superseded by upstream contribution review, 2026-10-04

## 7. Hand-off

### Public repository and v0.1.0 complete, 2026-10-04
Public: https://github.com/vyanhursky/DefJamFFNY-recomp
Release: https://github.com/vyanhursky/DefJamFFNY-recomp/releases/tag/v0.1.0
Vlad's D58 publication is complete. Source v0.1.0 points to clean public commit
`89688c040f42629dfc369648cfb60004a841967b`, with toolkit gitlink
`aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc`. No private parent history is
included. Public main CI `37223781912` passes 7/7; tagged source-release
workflow `37223822550` passes 9/9. Draft inspected and published with zero
uploaded executable/assets; GitHub supplies the source archives. Two exact
fingerprinted owner-approved README visuals are included in the source tree.

Current evidence: 96 project tests pass locally under MSVC and in public CI,
533 toolkit tests /107 subtests pass (one skip), native saves19/19 and five
registered native CTest cases pass. CI runtime/fixtures are actual Release;
game preset remains optimized RelWithDebInfo. Clang is unsupported, with the
two initial failures documented; shared helper override verifies its reviewed
fingerprint without changing fixture/assertion/optimization behavior.

Repository separation:
- `C:/Users/Vlad/code/defjam-recomp` keeps private origin
  `vyanhursky/DJFFNY-recomp`, with full development history and local game build.
  Its private v0.1.0 draft remains unpublished and must stay that way.
- `C:/Users/Vlad/code/DJFFNY-public-preview` is now the published source checkout,
  with public origin `vyanhursky/DefJamFFNY-recomp`. Do not amend its published
  root/tag or push private history there. Update public maintained source with
  deliberate source-only commits; standalone public docs closure follows the
  release without moving its immutable v0.1.0 tag.
- The original repo was not renamed or made public. Private history still
  contains copied guest excerpts; 132 mapped Markdown fences in 21 current
  docs are redacted. Publication audit/manifest stay available. A fresh clone
  uses `git clone --recursive --branch v0.1.0` with the public URL.

README attribution is vyanhursky only, per the owner's follow-up. The first
successful source workflow created an unpublished draft at initial root67c2672;
that draft/tag was replaced BEFORE first release publication with the corrected
README commit89688c0, then all source-release gates reran successfully. Published
v0.1.0 is immutable. README now says playable and FUN, records over an hour of owner Story playtime
without additional graphical glitches, lists macOS/native Linux on the future
roadmap, removes the fork paragraph from What Comes Next and embeds the supplied
screenshot plus silent six-second GIF. D58 only permits these exact two visuals;
other captures, game data, generated C, saves and game executables stay excluded.

Accepted local gameplay executable/runtime/pin unchanged. No game launch or
rebuild during publication. Preserve generated C, certificates, saves and
rollback refs. Next: agree M6 PC feature slice, then M5 Proton and M9 macOS.
Every upstream PR or named batch still requires separate owner approval.

### Standing gameplay backlog
Two-pad gameplay, long-session memory, rare `sub_001A3310` crash/silent boot,
loading bar, black profile thumbnails, Blazin' film grain and half-pixel alignment.

## Superseded by public publication completion, 2026-10-04

## 7. Hand-off

### Public publication authorized, 2026-10-04
Vlad approved a fresh PUBLIC `vyanhursky/DefJamFFNY-recomp` (D58).
Keep `vyanhursky/DJFFNY-recomp` PRIVATE, with its original name/history.
Do not push private history to public. Local working game checkout remains
`C:/Users/Vlad/code/defjam-recomp`; clean public checkout is
`C:/Users/Vlad/code/DJFFNY-public-preview`. Accepted game binary/toolkit pin
`aa1a1b9` unchanged; no game runs/rebuilds for docs/release work.

README feedback applied: playable and FUN, owner reports over an hour of Story
Mode without additional graphical glitches, macOS/native Linux future roadmap,
removed fork-consolidation paragraph from What Comes Next. Root README/setup/
release links now target the approved public name. Screenshots and GIF are
explicit owner-approved D58 exceptions; the two exact SHA-256 fingerprints in
`scripts/check-source-tree.py` are the only permitted media. Matching
AGENTS/CLAUDE and contributor/release rules updated. Source archives include
these two README visuals; game executable/data/lifted code remain excluded.

Private CI/release proof: PR #2 merged at `43a2b07`; CI 7/7, Source release
`37221619994` 9/9, v0.1.0 private draft unpublished/zero uploaded assets.
Tests: project92, toolkit533 and107 subtests (one skip), saves19, all five
native CTest cases pass. MSVC-only; initial Clang failures documented.

Current next steps: test approved-media boundary and matching instructions,
commit/push private main, refresh one-root-commit public preview, verify no
private parent history and exact toolkit pin, create requested public repo,
push main, wait all seven CI jobs, push approved v0.1.0 tag, wait all nine
source-release jobs, inspect draft, publish source Release. Record URLs and
final results here in the same turn. No extra permission needed for this
approved publication. Each upstream PR still requires separate approval.

History audit/redaction evidence: `docs/research/publication-history-audit.md`;
132 Markdown guest-code fences redacted in 21 current documents, historical
copies retained privately. Ignored snapshot manifest/archive in
`logs/rebase-work/`; it records exact hashes and public-source inventory.

### Standing gameplay backlog
Two-pad gameplay, long-session memory, rare `sub_001A3310` crash/silent boot,
loading bar, black profile thumbnails, Blazin' film grain and half-pixel alignment.
Next feature work: agree M6 PC slice, then M5 Proton and M9 macOS.

## Superseded by owner publication approval, 2026-10-04

## 7. Hand-off

### Public preview ready for approval, 2026-10-04
Rebase accepted (D56); accepted optimized game build and toolkit `aa1a1b9`
remain unchanged. No game run or rebuild during this documentation/CI work.
Vlad selected v0.1.0 and supplied Mercenaries/Burnout 3 README examples.
README, portable setup, docs index, known issues, contribution/release guides
and release notes are complete. Milestone definitions remain unchanged.

- Private PR #2 merged at `43a2b079f0ae8c51f71f34671b4c3d2c0bf04176`.
  PR CI `37221395036` and merged-main CI `37221615496` both pass 7/7.
  Hosted project tests 92/92; toolkit CPU/disassembly tests 533 passed,
  1 skipped and 107 subtests passed; native save cases 19/19 and all four
  Release fixture projects pass (five registered CTest targets).
- Private annotated v0.1.0 points to that merge. Source release workflow
  `37221619994` passes 9/9, including exact-tag/version/main ancestry policy,
  reusable CI and draft creation. Release remains PRIVATE and DRAFT, with
  zero uploaded assets and no publishedAt. Do not publish this old-history tag.
- History audit: 251 original reachable commits / 697 UTF-8 blobs at the
  audited base. No detected secrets or binary assets; copied guest bodies
  and disassembly existed inside Markdown. 132 mapped fences in 21 current
  docs have been redacted, retaining surrounding findings. Old blobs remain.
- Owner approval is still required for the proposed transition: rename the
  original PRIVATE repository to `DJFFNY-recomp-history`, create fresh PUBLIC
  `DJFFNY-recomp` from the reviewed clean snapshot, then publish a source-only
  v0.1.0 after its own CI succeeds. Never make the old history public.
  No rename/new public repository/public Release has occurred.
- Unpublished preview: `C:/Users/Vlad/code/DJFFNY-public-preview`, one root
  commit, no remote, exact public toolkit pin, no original parent history.
  Review `README.md` there. Ignored archive/manifest:
  `logs/rebase-work/public-source-preview-v0.1.0.zip` and
  `logs/rebase-work/public-preview-manifest.json`; manifest carries exact
  preview/source SHA, archive hash and file inventory. Refresh from final
  private documentation commit before asking approval. Its origin/main is
  only a local policy-test ref; public remote CI has not yet run.
- Supported compiler is MSVC. Initial hosted CPU helper chose Clang and
  exposed two documented failures. Reviewed helper fingerprint now selects
  cl without changing assertions, sources, optimization or skip gates;
  changed-helper synthetic test fails closed. Clang remains unsupported.
- Preserve local game data, saves, generated C, certificates and rollback
  refs. Public releases ship maintained source/tools/docs, never the game EXE.
  After approval, use a recursive clone for toolkit content, re-run public CI
  and source-release workflow, inspect draft and publish it. Update original
  and public remotes deliberately; keep private history available locally.
- Next feature work: agree M6 PC slice, then M5 Proton and M9 macOS.
  Every upstream PR or named batch still needs separate approval.

### Standing gameplay backlog
Two-pad gameplay, long-session memory, rare `sub_001A3310` crash/silent boot,
loading bar, black profile thumbnails, Blazin' film grain and half-pixel alignment.
Owner acceptance is bounded; keep these separate from release/doc cleanup.

## Superseded by private release validation, 2026-10-04

## 7. Hand-off

### Public source/docs and v0.1.0 preparation, 2026-10-04
Rebase accepted and merged (D56): private PR #1 at `c23b44a`, main CI 3/3 green.
Vlad reports perfect play, much better audio, smooth frames and no observed graphical
glitches. Closure docs published at `5bffcba`; toolkit remains clean `aa1a1b9`.

Current work is D57, before any upstream PR: public README/docs and working CI/Release.
Branch `public-readiness`. Vlad selected v0.1.0. New public guides lead with supported
play, portable build setup, known issues, credits and source-only release instructions.
The accepted game Release preset stays optimized RelWithDebInfo; no runtime source,
toolkit pin or build fingerprint changed. Hosted runtime/fixtures use actual Release.

- Audit: `docs/research/publication-history-audit.md`. 251 original reachable commits,
  697 UTF-8 blobs, no detected secrets/binary assets; copied guest bodies/disassembly
  existed inside Markdown. 132 mapped excerpts in 21 current docs are replaced with
  notices retaining surrounding findings. Original historical blobs remain reachable.
- Never change the original repository to public. Proposed final transition requires
  owner approval: retain it privately as `DJFFNY-recomp-history`, create a clean public
  `DJFFNY-recomp` from the sanitized source snapshot, publish v0.1.0 after CI.
  No history rewrite, rename or public repository creation has been done.
- Workflows: CI is reusable; source policy scans tracked paths/content; MSVC checks
  native saves, CPU/disassembly semantics and Release runtime. Four separate Release
  fixture projects register kernel bridge/directory, USB and VSH/WARP tests; empty
  suites fail. Tag workflow checks version/notes/exact tag/main ancestry, reuses CI
  and creates a source-only draft Release. Private CI and draft-release validation next.
- Local source checks: source policy has zero failures; 92 project
  tests pass under MSVC (25 focused source-release cases). Accepted Release build
  certificate verifies; actionlint 1.7.12 passes both workflows. Official
  actionlint archive checksum verified. Native save tests need MSVC in hosted CI.
- Initial expanded hosted CI `37220447054`: 6/7 jobs pass; all native Release
  fixtures and runtime build pass. Shared CPU helper chose Clang, exposing NEG32
  optimized flag behavior and a switch-fixture declaration failure. Select MSVC
  explicitly with a reviewed helper fingerprint; assertions, sources and /O2
  remain unchanged. Focused cases plus negative control pass 3/3. Clang remains
  unsupported and documented; hosted revalidation follows.
- Preserve ignored snapshot, generated C/build certificates, original saves and
  rollback refs. Do not rebuild or launch games for documentation/release changes.
  Clean export/synthetic Git validation is source-only and uses ignored scratch.
- Next after this publication work: agree M6 PC feature slice, then M5 Proton and
  M9 macOS. Every upstream PR or named batch still requires separate approval.

### Standing gameplay backlog
Two-pad gameplay, long-session memory, rare `sub_001A3310` crash/silent boot,
loading bar, black profile thumbnails, Blazin' film grain and half-pixel alignment.
Owner acceptance is bounded; keep these separate from the release/doc cleanup.

## Superseded by public release preparation, 2026-10-04

### Rebase accepted and merged, 2026-10-04
Vlad accepted the Release play-test: it plays perfectly to him, audio is much
better than before, frames are smooth and he observed no graphical glitches.
D56 closes the integration acceptance gate. Private PR
#1 (private development record) is merged at
`c23b44a51c11333017b94f47c7276aba8bb11b33`; local checkout is now main.
The launcher fix `c70f86d` passed 3/3 CI before merging. Merged-main
CI (private development record) is
also 3/3 green: hygiene, scripts/unit tests and Windows native save/runtime build.

- Toolkit fork: `vyanhursky/xboxrecomp`, branch `defjam/rebase-2026-10`, clean
  detached pin `aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc`. All 108 historical
  patches map to 15 published topics; replay is retired and the archive retained.
- Both final game regressions 9/9; Release fight 120/112 presents per 2 seconds,
  Debug 120/66; 20/20 Debug boots, zero hangs/failures. Extra routes 5/5, 13
  captures reviewed, nine PNGs byte-identical. Project tests 67; CPU suite 464
  passed / 26 existing skips / 57 compiled subtests; native runtime fixtures green.
  Full evidence and limits: `docs/research/toolkit-rebase-acceptance.md` and ledger.
- Correct upstream SHRD retained; read-only exact-metadata legacy save compatibility
  preserves old profile folders. Original save rehash stayed identical: 18 files /
  5,243,503,676 bytes. Manual play used the disposable snapshot `runtime-data` root.
- Fresh-shell launcher now supplies absent vblank/push-buffer/D3D11/USB defaults.
  Windows PowerShell 5.1 real-window/render/controller verification:
  `logs/run-20261004-094353.log.err`. Use `scripts/run.ps1`, with the review-save
  command in the acceptance report. Do not run builds or automated game tests
  while Vlad is using the machine. No new local runs are needed for this closure.
- Rollback: toolkit `ours-on-pin` = `dc32f30b303c79e1f2aaa592e2138fb933be93cf`;
  pre-rebase parent `1a2e2b4`; snapshot pointer `logs/rebase-work/baseline-path.txt`.
  Preserve snapshot, generated code, build certificates and ignored test evidence.
- Next: agree the first M6 PC feature slice, then M5 Proton and M9 macOS.
  Each upstream PR or named batch needs Vlad's separate approval. No upstream PR
  has been opened. CPU vertex fallback performance remains unmeasured.

### Standing items
M4 closed (D45). Backlog: two-pad gameplay, memory over three fights, rare
`sub_001A3310` crash, loading-bar glitch, rare silent boot, black profile thumbnails,
Blazin' film grain and half-pixel alignment. Owner acceptance does not constitute
exhaustive testing of these backlog items. See docs/03 and docs/05.

## Superseded by accepted rebase, 2026-10-04

### Rebase ready for owner play-test, 2026-10-04
The fresh-shell owner command exposed a launcher gap: `run.ps1` omitted the four
runtime flags always supplied by the harness. Two windowless processes were
stopped; the launcher now supplies absent graphics/vblank/USB settings, preserves
explicit overrides and restores added settings after exit. Windows PowerShell 5.1
launch verified a real window, D3D11 and controller initialization, and rendering
in `logs/run-20261004-094353.log.err`, using the disposable `runtime-data` root.
The game was left running for Vlad. Do not start builds or automated game runs
while he play-tests. Manual acceptance and main merge remain pending; toolkit
pin and certified executable are unchanged. See `docs/research/launcher-defaults-review.md`.

Vlad approved fork/rebase/retests, unattended game runs and the exact public source publication.
Claude is not editing for two days. Parent branch `toolkit-rebase`; leave main unmerged until the
Release play-test. The parent pin/retirement commit is `7068110`, published on the private topic.
Draft PR #1 (private development record) is open and attached to this chat.
Fresh-checkout CI (private development record) is 3/3 green:
67 Ubuntu tests, 19 MSVC compatibility cases, runtime build and hygiene. Final documentation follows
that source checkpoint; leave the draft unmerged until Vlad's Release play-test.

- Published toolkit fork `vyanhursky/xboxrecomp`, branch `defjam/rebase-2026-10`, exact SHA
  `aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc`. Actual submodule is clean and detached at that pin.
  All 108 patches map to 15 logical commits; archive retained, replay scripts and CI replay removed.
  Generic changes use fork topics/publish-before-gitlink; game-specific changes stay in src/.
- Fresh actual-checkout analysis 17,876 functions; lift 17,886 translated / 4 manual / 111 same stubs,
  20 C files / 87 MB. Seeds unchanged. 409 local tables / zero missing arms; 15 foreign arms separately
  verified. Coverage 87.95%, +9 bytes. Both hooks and PB scanner preserved; census changes explained.
  Both MSVC presets certified, and their certificates still verify after final game runs.
- Final Release `logs/regress-20261003-191107.txt`: 9/9, fight median/minimum 120/112, 3/3 boots.
  Final Debug `logs/regress-20261003-202026.txt`: 9/9, fight 120/66, 20/20 boots, zero hangs/failures.
  Debug early dips exist in pre-rebase logs too; later minimum 107 vs previous 119/116/117.
  Full cadence-window evidence is retained; do not claim exact Debug timing parity.
- Project tests 67/67; CPU suite 464 passed / 26 existing skips / 57 compiled subtests. Native
  kernel/audio fixtures 9/9, USB 1/1, VSH WARP 2/2 including 60 numeric cases and state checks.
  D52 retains correct SHRD and uses read-only canonical-first/exact-metadata legacy save compatibility;
  19 native cases cover wrapper ABI, collisions, bounds, shared root policy and Unicode paths.
- Extra Release intro/fight/crib/gym/unlock 5/5, 13 captures, all save restores equal and fault lists empty.
  Nine PNGs byte-identical, including all four Unlock Fighters selections. Four movie/gameplay frames
  differ in phase/pose/camera; sampled materials/layout are consistent. Both 110s fight HUDs read 1:53.
  Audio: 121 post-game-start level reports, 120 nonzero, zero dropped/dry events. Whole run retains ten
  dry events in the first six reports before game start. PCM has no full-scale samples. Perceptual
  fidelity and startup audio remain listening checks; no equivalent baseline PCM/counters exist.
  Full report: `docs/research/toolkit-rebase-acceptance.md`; artifacts stay in ignored logs/.
- Final original-save rehash identical: 18 files / 5,243,503,676 bytes (`original-save-verified.json`).
  All tests used disposable snapshot runtime-data. Serial runner 73907 completed with exit 0; no game
  was left by that automated runner. The corrected manual launch is now running for Vlad; the acceptance report contains the Release command using the review copy.
- Rollback: `ours-on-pin` = `dc32f30b303c79e1f2aaa592e2138fb933be93cf`, exact original live 108-patch
  toolkit state. Snapshot pointer `logs/rebase-work/baseline-path.txt` preserves source/builds/analysis,
  generated code, original saves and captures. Migration worktree/branch retains the published topics.
- Next: Vlad's Release play-test of
  existing profiles, Story/creator, a fight, menus/Unlock Fighters and audio before main merge.
  Upstream PRs require separate approval. M6 PC features follow the rebase, then M5 Proton and M9 macOS.

### Standing items
M4 closed (D45). Backlog: two-pad gameplay, memory over three fights, rare `sub_001A3310` crash,
loading-bar glitch, rare silent boot, black profile thumbnails, Blazin' film grain, half-pixel alignment.
Keep these outside the rebase. See docs/03 and docs/05 for diagnostics and standing rules.

## Superseded by owner launch fix, 2026-10-04

### Rebase ready for owner play-test, 2026-10-03
Vlad approved fork/rebase/retests, unattended game runs and the exact public source publication.
Claude is not editing for two days. Parent branch `toolkit-rebase`; leave main unmerged until the
Release play-test. The parent pin/retirement commit is `7068110`, published on the private topic.
Draft PR #1 (private development record) is open and attached to this chat.
Fresh-checkout CI (private development record) is 3/3 green:
67 Ubuntu tests, 19 MSVC compatibility cases, runtime build and hygiene. Final documentation follows
that source checkpoint; leave the draft unmerged until Vlad's Release play-test.

- Published toolkit fork `vyanhursky/xboxrecomp`, branch `defjam/rebase-2026-10`, exact SHA
  `aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc`. Actual submodule is clean and detached at that pin.
  All 108 patches map to 15 logical commits; archive retained, replay scripts and CI replay removed.
  Generic changes use fork topics/publish-before-gitlink; game-specific changes stay in src/.
- Fresh actual-checkout analysis 17,876 functions; lift 17,886 translated / 4 manual / 111 same stubs,
  20 C files / 87 MB. Seeds unchanged. 409 local tables / zero missing arms; 15 foreign arms separately
  verified. Coverage 87.95%, +9 bytes. Both hooks and PB scanner preserved; census changes explained.
  Both MSVC presets certified, and their certificates still verify after final game runs.
- Final Release `logs/regress-20261003-191107.txt`: 9/9, fight median/minimum 120/112, 3/3 boots.
  Final Debug `logs/regress-20261003-202026.txt`: 9/9, fight 120/66, 20/20 boots, zero hangs/failures.
  Debug early dips exist in pre-rebase logs too; later minimum 107 vs previous 119/116/117.
  Full cadence-window evidence is retained; do not claim exact Debug timing parity.
- Project tests 67/67; CPU suite 464 passed / 26 existing skips / 57 compiled subtests. Native
  kernel/audio fixtures 9/9, USB 1/1, VSH WARP 2/2 including 60 numeric cases and state checks.
  D52 retains correct SHRD and uses read-only canonical-first/exact-metadata legacy save compatibility;
  19 native cases cover wrapper ABI, collisions, bounds, shared root policy and Unicode paths.
- Extra Release intro/fight/crib/gym/unlock 5/5, 13 captures, all save restores equal and fault lists empty.
  Nine PNGs byte-identical, including all four Unlock Fighters selections. Four movie/gameplay frames
  differ in phase/pose/camera; sampled materials/layout are consistent. Both 110s fight HUDs read 1:53.
  Audio: 121 post-game-start level reports, 120 nonzero, zero dropped/dry events. Whole run retains ten
  dry events in the first six reports before game start. PCM has no full-scale samples. Perceptual
  fidelity and startup audio remain listening checks; no equivalent baseline PCM/counters exist.
  Full report: `docs/research/toolkit-rebase-acceptance.md`; artifacts stay in ignored logs/.
- Final original-save rehash identical: 18 files / 5,243,503,676 bytes (`original-save-verified.json`).
  All tests used disposable snapshot runtime-data. Serial runner 73907 completed with exit 0; no game
  remains running. The acceptance report contains the Release launch command using that review copy.
- Rollback: `ours-on-pin` = `dc32f30b303c79e1f2aaa592e2138fb933be93cf`, exact original live 108-patch
  toolkit state. Snapshot pointer `logs/rebase-work/baseline-path.txt` preserves source/builds/analysis,
  generated code, original saves and captures. Migration worktree/branch retains the published topics.
- Next: Vlad's Release play-test of
  existing profiles, Story/creator, a fight, menus/Unlock Fighters and audio before main merge.
  Upstream PRs require separate approval. M6 PC features follow the rebase, then M5 Proton and M9 macOS.

### Standing items
M4 closed (D45). Backlog: two-pad gameplay, memory over three fights, rare `sub_001A3310` crash,
loading-bar glitch, rare silent boot, black profile thumbnails, Blazin' film grain, half-pixel alignment.
Keep these outside the rebase. See docs/03 and docs/05 for diagnostics and standing rules.

## Superseded 2026-10-03 20:47

## 7. Hand-off

### Active rebase, 2026-10-03
Vlad approved the fork/rebase/retests and unattended game runs (D48); Claude is not editing for two days.
Parent branch `toolkit-rebase`, committed checkpoint `87af9e2`; leave main unmerged until Vlad's release play-test.

- Rollback ref `ours-on-pin` = `dc32f30`, exact 108-patch baseline; parent gitlink still `6f55eaa`.
  Actual `tools/xboxrecomp` working checkout is detached at published `aa1a1b9` (D53/D54), clean.
  Candidate `logs/rebase-work/migration`, branch `defjam/rebase-2026-10`, published tip `aa1a1b9`.
  All 15 logical topics committed with an explicit source allowlist, no game data; working tree clean.
  Public fork `https://github.com/vyanhursky/xboxrecomp` branch is published. Vlad explicitly approved
  the source payload; remote SHA verified exactly `aa1a1b91dea9fd266acb3a3fe51dfcec3b2e6bbc` (D54).
- Baseline snapshot pointer `logs/rebase-work/baseline-path.txt`: executables/PDB/maps, generated C,
  analysis, save tree and five capture routes. Fresh pre-rebase Release 9/9 (`regress-20261003-123017.txt`).
  Run only with `DEFJAM_DATA` set to snapshot `runtime-data`; all runs guard its complete save root.
  Original 18 save files / 5,243,503,676 bytes rehashed identical after the clean candidate run.
- Clean candidate Release: `logs/regress-20261003-180239.txt`, 9/9 in 28 min, goldens match,
  fight median 120 / minimum 112 presents per two seconds, clean Story creator/crib/gym routes,
  3/3 soak boots. Initial 4/9 failure evidence retained in `regress-20261003-165713.txt`.
  Candidate Debug full regression `regress-20261003-183307.txt`: 9/9 in 28 min, 67 unit tests,
  goldens/Story routes pass and 3/3 boots. Fight median 120, minimum 53; dips at intervals 1/2 after
  Game.StartGame, after the first 16 s minimum 116. Preserve the full minimum and repeat it in the
  final actual-checkout regression. No timing-run diagnostics. Final actual-checkout analysis/lift/audits and both builds complete. Release `regress-20261003-191107.txt` and Debug `regress-20261003-202026.txt` are 9/9; Debug 20/20 boots clean. Extra Release routes run on session `73907`.
- D52: baseline zero-count SHRD produced legacy VY2/VY3/Options name hashes. Correct CPU semantics
  retained; game replacement `sub_001F8360` uses canonical-first, exact-metadata-verified legacy fallback,
  with no folder migration. 19 native compatibility tests, including ABI, collisions, bounds and Unicode.
- Project unit suite 67/67. Toolkit CPU suite 464 passed, 26 existing compiler-dependent skips,
  57 compiled subtests; final segment-rejection switch fixture 5/5. Kernel/audio CTest 9/9,
  USB 1/1, VSH WARP 2/2 (60 numeric cases plus state/cache checks).
- Fresh analysis 17876 functions; lift 17886 translated / 111 identical unresolved stubs / 4 manual,
  20 C files / 87 MB. Same seed file. Local jump audit 409 tables / zero missing arms; all 15 recovered
  foreign arms verified separately. Coverage 87.95%, +9 bytes; manual replacement explains new gap.
  Hooks precede guest instructions; 16 added definitions and two changed extents explained in
  `docs/research/toolkit-rebase-fresh-lift-audit.md`. PB scanner byte-identical to original baseline.
- Local `.gitmodules`, AGENTS/CLAUDE, docs/05 and patch-archive README are prepared; replay scripts
  and CI replay step removed locally. Windows CI gains the native save fixture; CTest failures propagate.
  Public approval, publication and remote-SHA verification are complete; commit the gitlink after final gates.
  New Windows-header integration also uses proper LARGE_INTEGER performance-counter types in the old
  fence hook; timing arithmetic unchanged. Analysis reads the validated original extracted dump; only
  game runs use the disposable save root (which intentionally contains no extracted dump).
  Actual-checkout 67 project tests and 464 CPU tests / 57 compiled subtests pass; both builds certified.
  Final Release 9/9 (`regress-20261003-191107.txt`), Debug 9/9 (`regress-20261003-202026.txt`) with
  20/20 boots clean. Serial runner `73907` now runs five extra PNG/audio routes. Do not start another game or build. Compare baseline
  captures, cadence and audio counters; reverify original saves. No source edits during game/build gates.
- Then publish private parent topic for review/CI. Final gates: Vlad's release play-test before main
  merge; each upstream PR requires separate approval. M6 follows rebase, then M5 Proton and M9 macOS.

### Standing items
M4 closed (D45). Backlog: two-pad gameplay, memory over three fights, rare `sub_001A3310` crash,
loading-bar glitch, rare silent boot, black profile thumbnails, Blazin' film grain, half-pixel alignment.
Keep these outside the rebase. See docs/03 and docs/05 for diagnostics and standing rules.

## Superseded 2026-10-03 18:07

## 7. Hand-off

### Active rebase, 2026-10-03
Vlad approved initial fork/rebase/retests and unattended game runs (D48); Claude will not edit for two days.
Parent branch `toolkit-rebase`, current committed checkpoint `1fd0b50`. Public fork
`https://github.com/vyanhursky/xboxrecomp` exists; no topic branch published yet.

- Original toolkit checkout remains `ours-on-pin` at `dc32f30` (all 108 patches reconstructed exactly).
  Candidate: `logs/rebase-work/migration`, branch `defjam/rebase-2026-10` atop upstream `1409a7d`.
  Eight CPU commits through `7f7eef9`; runtime integration still uncommitted. Both candidate game presets built successfully after reporter integration. Initial release regression completed 4/9; do not publish or switch the parent pin until retests are green.
- Baseline snapshot pointer `logs/rebase-work/baseline-path.txt`: full original save, generated sources,
  both executable/PDB/map sets and captures preserved. Fresh release baseline 9/9 (`regress-20261003-123017.txt`),
  120 presents/2 s median, minimum 112; five extra routes pass. Run only with `DEFJAM_DATA` set to its
  `runtime-data` disposable copy. Original save hashes verified unchanged.
- Candidate full recompiler suite 464 passed, 26 compiler-dependent skips, 57 compiled subtests
  (`logs/rebase-work/foreign-switch-full-tests.txt`); final switch fixture 5/5 with MSVC. All runtime libraries build (`runtime-build.txt`).
  VSH CTest 2/2 (`vsh-test-results.txt`), including 60 numeric WARP shader cases and cache/state checks.
  USB CTest 1/1 (`usb-test-results.txt`): hub + four pads, partial transfers, rumble, failed DMA retry,
  indexed resets, shared done chain and WDH acknowledgement. Kernel/audio CTest 9/9 (`runtime-fixtures-results.txt`), including 1025 physical mappings. Project pipeline/save-guard/save-compatibility unit tests 67/67 (`pipeline-save-compat-tests.txt`): complete payload restore, failed-backup retention, absolute/empty roots, both ICALL failure forms, start/end provenance and protected lifter arguments.
- Parent source changes: defaults for anchored-seconds pad scripts and six-channel audio; frame-end callback
  registration; runtime-owned indirect-tail site compatible with the baseline toolkit.
- Fresh analysis: 17876 functions; fresh lift after D52: 17886 translated, 111 stubs, 4 manual functions, 20 C files / 87 MB. Coverage audit: 16 added definitions (15 recovered foreign switch arms, one existing seeded thunk), one new game replacement (001F8360); two extent deltas. Entry hooks precede guest work; all 111 unresolved stub identities match the baseline.
- Current measurements: debug `run-20261003-161055.log.err` reached the main menu with identical save restoration.
  Initial release regression (`logs/rebase-work/candidate-release-regress.txt`) matches M2/M3 goldens, but M4a
  `run-20261003-162319.log.err` never captured the menu; fight `run-20261003-162654.log.err` stops at Battle ID;
  intro `run-20261003-163410.log.err` times out before Story. Keep these failures; do not dismiss as boot variance.
  Intro had live thread-stack sampling and is not a clean timing/performance run; rerun without diagnostics.
  Live diagnostics in `logs/rebase-work/regress-{intro,live}-*`; slow FIFO/fence progress and tick clock stalls.
  GPU scanner is byte-identical to the preserved baseline. Inspect ISR/DPC integration and fresh-lift deltas.
  Profile cause proven: baseline `sub_002032A0` SHRD count zero ORed both hash words, creating old folder spellings
  `005600590036/37` and `...4ADF`. Correct spellings are `005600590032/33` and `...4ADD`.
  Exact optimized fixture reproduces all three (`save-hash-fixture-results.txt`). D52 keeps corrected CPU
  behavior and uses a read-only, metadata-verified game replacement at `sub_001F8360`; relift and both builds certified; clean full Release regression passes goldens, fight and intro so far. Fight median/minimum 120/112 matches the baseline; remaining checks running.
  GPU source audit finds no dropped acknowledgement; ISR/DPC changes remain a separate contract risk.
- Next: finish save-compatibility relift and both builds, repeat affected routes, then full regression and debug soak. All 108 ledger
  rows have source outcomes; full game acceptance remains pending. Commit logical runtime topics only when green,
  publish clean fork branch; switch parent submodule to its published
  commit and retire old patch replay. Re-analyze and re-lift with existing seeds unchanged, audit coverage,
  build debug and release, full regressions + extra routes + 20 debug boots, compare captures/audio/cadence.
- Final gates: Vlad's release play-test before merging the private parent branch; confirm each upstream PR
  separately before opening. M6 remains after the rebase, then M5 Proton, then M9 macOS (D45).

### Standing items
M4 closed on Vlad's play-test (D45). Backlog: two-pad game play, private bytes over three fights,
rare `sub_001A3310` crash, loading-bar glitch, rare silent boot; black profile thumbnails; Blazin' film grain;
half-pixel alignment needs evidence. Do not add these features during the rebase. Full reference and
diagnostics remain in `docs/05-reference.md` and `docs/03-workflows.md`.

# Hand-off archive, 2026-10

## Superseded at 2026-10-03 15:46

## 7. Hand-off: exact next steps

### State as of 2026-10-02 17:30 (read this first; earlier hand-offs are in `docs/worklog/2026-09-handoffs.md`)

**Where it stands.** M0-M4 are closed (M4 and M4f on Vlad's play-test, D45). The game is playable end to
end in the release build: menus, a One on One fight, Story mode from a new ID through the cutscenes,
creator and tutorial, the crib, shops, Stapleton Athletics, and saved Story progress with two profiles.
Vlad's words on 2026-10-02: the intro "near perfect", Story fights "look good", "definitely playable and
feels good to play". Toolkit patches run to 0108. `python scripts/regress.py` is 9 of 9 on the release
build (`logs/regress-20261002-162043.txt`). Nothing is uncommitted; everything is pushed to `origin main`.

**Start here: the toolkit rebase (D46).** Vlad approved moving our toolkit changes onto upstream
xboxrecomp's latest code, retesting, and then offering our generic fixes upstream as pull requests. The
work plan is `docs/06-toolkit-rebase-plan.md` (phases 0-4, the ledger to keep, what "done" means); the
background is `docs/research/upstream-xboxrecomp-2026-10.md` (255 upstream commits since our pin, at least
15 of our patch topics fixed upstream too, upstream fixes we lack). The fork is decided (Vlad,
2026-10-02): on his account `vyanhursky`, **public** from the start (`gh repo fork sp00nznet/xboxrecomp
--clone=false`). Opening each pull request upstream still needs his OK first.
Do the rebase on a branch of this repo too (`toolkit-rebase`) and merge to `main` only when Phase 3 is
green and Vlad has play-tested.

**After the rebase, in Vlad's order** (D45): M6 PC features and enhancements (the list is to be agreed with
him feature by feature; already in: 2x render scale `RECOMP_RENDER_SCALE`, the gamma ramp), then M5 Steam
Deck via Proton, then M9 macOS (needs a second graphics backend: the renderer is Direct3D 11 only; keep M6
from deepening that dependency). Before M6 touches them, split `src/kernel/nv2a_pb_exec.c` (3,100 lines)
and `src/kernel/kernel_bridge.c` (10,000) -- after the rebase, not before, or the split conflicts with it.

**Confirmed by Vlad on 2026-10-02**: the darker picture (the gamma ramp) matches the console as far as he
can tell, and X in Learn Moves plays the move's movie. Every item of his 2026-10-01 and 2026-10-02 lists
is closed.

**How to run and check it.** CLAUDE.md §4 and §4b, `docs/05-reference.md` "Running it, and checking it":
- `python scripts/regress.py [--quick] [--only fight,intro]`: the regression run (25 minutes in full).
- `python scripts/harness.py run <boot|fight|crib|gym|unlock|intro> [--png] [--stage ...] [--shots ...]
  [--env NAME=VALUE]`: one scripted route with captures in `logs/shots/<route>/`. Story routes run inside a
  save guard (`$DEFJAM_DATA/save-guard/` keeps the last three copies of the save area).
- `python scripts/harness.py soak --boots N`: boots until one stops presenting, then dumps stacks and device
  state to `logs/hang-<stamp>/`.
- `python scripts/toolkit-patch.py cut|verify`: until the rebase retires the patch files.
- `python scripts/worklog-add.py <entry file>`: a §6 entry, older ones archived to `docs/worklog/`.
- `python scripts/prune-logs.py [--delete]`: clears run logs older than three days that the notes do not cite.

What the routes press, for extending them (the stage syntax is in `scripts/harness.py`'s docstring):
- fight: Battle -> One on One with defaults, then a fighter that walks right and throws every attack.
  With more than one user ID in the save area Battle asks for one first (the route handles it).
- crib: Story with the first profile in the list; the crib opens on a "Messages Waiting" prompt (down, A
  for NO); its bar is Map, Messages, Wardrobe, Options, Trophies, Exit.
- gym: crib -> Map -> A (Shop District) -> down -> A (Stapleton Athletics) -> Learn Moves -> X previews.
  The map: Shop District -> right Club 357 -> right The Limit; up from Shop District is the Foundation.
- unlock: Main Menu -> Unlock Rewards -> Unlock Fighters, moving the selector (nothing is bought).
- intro: Story from a new ID ("AAA"); the escort shot is ~16 s after `game.startstorymode(` in release, the
  car crash 46-62 s, the creator is `getscreeninfo(story/chardec`. Past it: A through the creator,
  `game.continueload(`, `game.showfightingmovie(=a@6;left@9;a@11` (the style prompt defaults to NO),
  `@game.entertutorialmode(`.

**Other open items.**
- A one-off full freeze of a debug boot about 30 s in (`run-20261002-110917`): the log stops mid-frame,
  the timer thread included, no crash record. Not seen in ~70 boots since. An overnight
  `harness.py soak --boots 100 --preset win-x64-debug --keep-going` would settle it.
- The D3D11 debug layer's exception 0x87D (two debug runs on 2026-10-01); patch 0091 reports it as
  `[D3D11-DEBUG]`; not seen since, and `regress.py` fails on it.
- M4d items never formally met (D45, backlog): two pads, private bytes over three fights, the rare
  `sub_001A3310` crash at a fight's start, the loading-bar glitch, a rare boot with no sound.
- Profile thumbnails on Select User ID are black squares. Film grain in Blazin' (no separate pass found).
- Half-pixel alignment of screen-space quads: tried and taken back (§6 2026-10-02 12:30); needs evidence
  from hardware or from xemu's source.
- The fence hold (0095) engages ~500 times a second in a release fight and never timed out; if a scene
  stutters in release, `RECOMP_FENCE_HOLD=0` says whether it is the hold.

**Known loose ends in the GPU model.** Modelled: depth, stencil, blend (constant colour and equation
included), alpha test, combiners, four texture stages, palettes, address modes, min and mag filters, the
surface clip, indexed and array draws, point sprites, the display's gamma ramp. Not modelled: polygon
offset (enabled with zero factor and bias), fog (set, but no final combiner seen reads it), float depth,
texture-shader modes beyond plain 2D/3D/cube, render targets sampled as textures, mip levels (only level 0
is uploaded), the G8B8/R8B8 channel mapping, lines and points on the CPU vertex path, batches over 32,768
indices (truncated, with a log line). Upstream's executor has some of these; see the rebase plan.

**Diagnostics** (all in `docs/03-workflows.md` and `docs/05-reference.md`): `RECOMP_PB_BATCH_DUMP=shot`,
`RECOMP_PB_PROBE=x,y;...`, `RECOMP_PB_UNHANDLED_ALL=1`, `RECOMP_PAL_DUMP=<addr>[~<mask>],<file>`,
`RECOMP_GAMMA=0`, `scripts/peek-guest.py`, `scripts/hang-peek.py`, `scripts/native-stacks.py`,
`scripts/sample-threads.py`, `scripts/guest-stack.py`, `scripts/apt-dump.py` (a screen's ActionScript;
needs `DEFJAM_DATA` set), `scripts/window-shot.ps1` (what the window shows, after presentation).

### Reference
How the rendering works, the object addresses, the diagnostic tools, the standing items and how to run
and check a build are in `docs/05-reference.md`.

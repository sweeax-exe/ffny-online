# Startup skip integration onto PR #25

2026-10-17. Read-only comparison of main `9c1730917bb1ea080db1cbae2e012fa4ce184b66`
and PR #25 head `0bb115fab155de0420db1af696f8857a14703d0f`, inspected in
`/Users/vlad/Code/DefJamFFNY-recomp-publish`. The finalized startup feature in
the coordinating root checkout was also read. No build, game run, checkout
edit, commit, push or external message was performed by this research task.
The new report is written only in the coordinating checkout.

## Integration result

The startup hook and setting can be added to this PR without a known guest
function or settings-name conflict. Preserve PR #25's existing recorder,
front-end injection and crowd hooks while adding the feature. The main risks
are the older version and divergent toolkit pin, plus changing harness timing
without explicitly preserving a complete normal boot test.

## Source compatibility

| Area | PR #25 difference from main | Integration consequence |
|---|---|---|
| `src/recomp_manual.c` | Adds recorder/front-end injection includes, crowd callbacks on existing fight hooks, `sub_00073FD0_enter`, recorder events before FuncCall dispatch and injection after dispatch | Add only `sub_0006BC90_enter` and its delegate declaration. There is no hook at that address in PR #25. Do not replace the file with the root's older copy. |
| `src/hooks/pc_ui_settings.c` | Identical to main at the compared revisions | The new `[debug] skip_intro` row applies directly. Its bool/env/restart contract remains valid. Six UI-table entries fit the existing eight-entry local capacity and the caller's sixteen-entry allocation. |
| `src/hooks/pc_ui.cpp` | Adds a separate Debug tab for the recorder, mark controls and generalized pad-chord parsing | Retain these additions. Adding the startup label and General-page checkbox does not collide with the recorder Debug tab or chord state. Keep checkbox automation anchored to the actual rendered layout. |
| `src/hooks/pc_settings.c` | Registers eleven `pc_debug_settings` entries after UI settings, grows the combined array, starts recording after the launcher and before audio | No startup-specific edit is necessary. The skip setting is registered before loading INI/environment overrides; the game hook reads the effective setting after the launcher. Recorder `settings_json()` will include the new skip row automatically. |
| `src/hooks/guest_call.h` | New full-register synthetic guest-call helper for scene injection/crowd controls | Compatible with the startup helper's existing synthetic `sub_00055640` call. Replacing the proven helper merely to reuse this abstraction is optional, not required for integration. |
| CMake | Version is `0.9.2`, whereas compared main is `0.9.3`; source globs are unchanged | `src/patches/skip_intro.c` is picked up by the patch-source glob. Preserve the intended current version instead of unintentionally restoring 0.9.2. |

The startup entry is before the first screen-selection body. PR #25's
recorder sees the resulting first `Control.GetScreenInfo(preStart)` call;
its `fe` event is emitted before normal dispatch. `fe_inject_after_call`
still runs after dispatch, so its ordering is unchanged. Injection recipes
anchored to legal/bootLoad intentionally will not fire when skipping is on;
Story/crib/menu anchors remain available. The scene-loader hook is a
different address and is unaffected by the startup decision.

`RECOMP_UNLOCK_ALL`, `RECOMP_DEV_DEBUG` and recorder options have distinct
names from `RECOMP_SKIP_INTRO`. The shared `[debug]` section is not a conflict:
registration is by section/key. PR #25's input-native fixture only registers
input plus UI settings, so its expected count changes from 60 to 61, not by
the recorder's eleven settings. Recorder-native and scenario-control tests
remain relevant to preserving PR #25 behavior.

## Toolkit and build identity

Main pins toolkit `4a6c88e841dd74d3c279f15b8f8e50110b0df70d`; exact PR #25
pins `9051be9cf04a5ef90f543c5af0aad05e2039fe1f`. These histories diverge from
`872fbc3d18b994f72266b1ff6b532abc98a7a60b`, and neither pin is simply the
other plus the desired feature. Equivalent earlier audio fixes have different
commit IDs on the two branches.

PR #25's toolkit supplies asynchronous back-buffer read-back and texture-cache
statistics used by its recorder. Main's toolkit does not supply those new
APIs. Selecting main's pin unchanged would discard required recorder support.
Conversely, PR #25's pin lacks two later main topics:

- `a5b2ccf`: scene-load CPU handoff fixes, including releasing the guest CPU
  while writing read diagnostics, yielding to waiting urgent threads, and
  setting Darwin guest-thread QoS.
- `4a6c88e`: Darwin APU frame-thread QoS.

The direct source comparison confirms these differences; they are not inferred
from commit names. A full regression on exact PR #25 plus startup skip therefore
tests that runtime combination, not main's later macOS audio behavior. If the
coordinator deliberately combines toolkit topics, record and test that new
combination separately. No new toolkit change is required by the skip hook
itself; the toolkit differences contain no lifter changes.

### Search for an existing published combined pin

A read-only live `git ls-remote` check of the public fork's heads and tags
found no already-published combined tip. The live heads matched the local
remote-tracking objects used for the source check. The fork advertised no
`refs/pull/*/head` entries; its published tags were older v0.6.0–v0.12.0.
Source inspection across unique published heads produced this relevant set:

| Published branch / tip | Async read-back API | Urgent-aware yield | Darwin guest QoS | Darwin APU QoS | Read logging outside guest CPU |
|---|---|---|---|---|---|
| `defjam/async-readback` / `8ae5cb7e5aa9` | Yes | No | No | No | No |
| `defjam/audio-crackle` / `9051be9cf04a` | Yes | No | No | No | No |
| `defjam/audio-fix` / `4a6c88e841dd` | No | Yes | Yes | Yes | Yes |

Both final audio pins include equivalent KeTickCount shutdown protection;
that is not a missing behavior in PR #25. The important differences are the
four scene-load/QoS behaviors in the table and PR #25's read-back/cache-stat
support. No published head had both sets by content, not merely by ancestry.

Consequently preserving both sets requires a deliberate combined toolkit
change, tested and published before its gitlink is committed. That publication
is a separate authorized action; this research made no toolkit or ref changes.
Using the exact PR #25 pin is possible for a clearly labelled test build, but
must not be described as retaining main's later scene-load/QoS fixes. This
search cannot establish whether an unreferenced commit exists somewhere on
the server; it establishes that no usable combined published tip was found
among the fork's advertised refs.

At inspection time the publish checkout's `tools/xboxrecomp` directory was
empty/uninitialized. Its parent gitlink was read from Git, not guessed from a
working toolkit. Initialize the exact pin or use an explicitly recorded
external toolkit path before analysis/lifting/building. The coordinating root's
toolkit object database was used for the pin-to-pin source comparison.

Re-lift after adding the entry hook: PR #25 already requires its own
`sub_00073FD0_enter` hook, and copying old generated C would lose either hook.
`pipeline-state.py` hashes the manual file, lifter/template inputs, generated
lift identity, game sources and runtime sources; let its normal certification
run. A binary or lift certificate from the coordinating root is not evidence
for the PR #25 integration.

## Save guard and recorder interaction

`SaveGuard` itself is unchanged by PR #25. `harness.run_game` still resolves
the effective `DEFJAM_DATA` first and protects that root's complete `save/`
tree. `scenario_suite.execute` still copies the named save fixture to a
temporary runtime root and supplies it to the same guarded harness.
Recorder output is under the checkout's ignored `logs/sessions/`, not the save
root. `--record` adds a recorder-session assertion and analysis after the run;
it does not replace the save guard or weaken restoration checks.

Use disposable explicit INIs for checkbox/file-setting checks. The save guard
does not restore an arbitrary settings file beside the save directory.
Keep failed save backups and record byte-identical restoration as usual.

PR #25's `pc_debug_start` defaults recording off. With recording on it may
enable game observations and an audio PCM file before guest execution; this
does not change the startup selection, but it does change performance and
measurement scope. Preserve an unrecorded regression baseline and label any
recorder-on compatibility run separately. The recorder's session settings
will report the skip flag, and its FE events provide uncapped screen evidence.

## Faster regression without losing normal startup coverage

The existing PR #25 regression has no skip integration. It also contains
independent captures for loading, title and menu. Its generic scripted
startup clock is the historical `feflow.xml` anchor, which is invalid after
bypassing the first legal/boot/movie chain. Enable startup skip together with
the confirmed `getscreeninfo(intmain/prestart` anchor for shortened routes;
retain the title's native animation and Start-input delay.

For each quick/full set, require one positive normal startup sequence with
`RECOMP_SKIP_INTRO=0` explicitly overriding inherited environment/INI values:
legal first, bootLoad, EA/THX/intro movie opens, preStart, and successful Start
to mainMenu. Reuse its timed captures for goldens if the implementation does
so intentionally; a loading-frame capture alone is not a complete boot test.
All other applicable routes can use the skip flag. Keep a normal boot in a
standalone `--only` subset when that subset claims normal startup coverage.

Specific timing/evidence risks:

- M2 loading captures are fixed frame numbers and cannot simply inherit the
  skip default: its loading window may shorten enough to miss all samples.
  Keep M2 on the normal path or reuse an explicitly checked normal-run image.
- M3 and M4a set their own `RECOMP_SCRIPT_ANCHOR`; update those explicit
  values as well as `run_game`'s scripted default. Shortening only the route
  default leaves golden captures waiting for an anchor that will not arrive.
- `scenario_suite` uses a new Python process for several checks. A parent-local
  variable does not configure those runs. Pass/inherit the explicit skip
  value into the child and keep the environment in its run identity.
- Honor explicit `RECOMP_SKIP_INTRO=0` and option-matrix overrides after
  applying acceleration defaults. The integration must not overwrite them.
- Retain `harness.LAST_LOG`/`triage_hint` reporting from PR #25 when merging
  the new regression checks; these changes overlap the main regression loop.
- Do not change the pinned fight-stream golden just to accommodate startup
  acceleration. Replay/step-input assertions must establish that the fight
  simulation still matches, since startup timing can shift ambient RNG work.
- Keep soak semantics explicit: skipping all soak boots is a test of the
  accelerated boot. The single normal boot per set covers the full normal
  chain once, not a multi-boot normal-start reliability claim.

Source review found no blocker to adding the finalized feature. The runtime
and regression claims must come from the coordinator's newly certified PR #25
build and serialized, guarded play-tests.

## Local integration checkpoint

The coordinator merged exact PR25 head `0bb115f` into startup commit `321cd8e`
as `db88b0a`, preserving main's version0.9.3, current status and PR25's recorder,
scenario, triage and adapter changes. Missing decision rows D115–D119 were
retained; superseded PR25 work-log entries moved unchanged to the local archive.

Isolated toolkit branch `defjam/skip-intro-pr25` starts from9051be9 and applies
the already-published a5b2ccf/4a6c88e topics unchanged. Merge `5445b73` records
the released4a6c88e history too; its tree is identical to the two-topic result
`8507a75`. Compared with released main, only PR25's read-back/cache-stat source
and fixture additions remain. Both original toolkit tips are ancestors.

On macOS arm64, the guest-CPU fixture passes1/1 and real Vulkan/MoltenVK read-back
passes2/2, including scaled presentation. Integrated units pass461, skip34
platform-specific cases, and deselect the one slow branding test, matching PR25's
regression policy. Logs: `logs/startup-skip/fx-guest-cpu.txt`, `fx-readback.txt`,
`pr25-unit.txt`. Re-lift/build and native quick/full validation follow this
checkpoint. Nothing was pushed or merged remotely; the parent keeps its published
base pin until the combined toolkit publication is authorized.

The integrated build is now certified after regenerating17,896 functions into
20 C files (90MiB). Native quick regression passes5/5 in410s
(`logs/regress-20261017-151938.txt`), versus576s for the earlier all-normal
startup run. These are observed suite durations on different source integrations,
not an isolated benchmark of the hook. The skipped menu check takes57s versus154s;
the fight check takes201s versus297s and retains120s of combat (median120 presents
per2s, minimum112). The exact existing title/menu/loading goldens pass unchanged.

Quick log audit: `run-20261017-151321`, `151520`, and `151617` each contain one
redirect and first screen preStart. `151330` contains no redirect and positively
passes legal→bootLoad→EA→THX→intro→preStart plus the title golden. Thus three
of the four quick game launches skip, and exactly one complete normal startup
remains. All save guards restore identical. The final full suite results below supersede this initial checkpoint.

## Final native validation on PR25 integration

Source `0ea5c5d` adds the first-menu unlock fix caught by the initial full run:
when startup movies are bypassed, unlock must apply immediately at the first
real main menu, rather than waiting for another front-end event. The normal
Game.StartMainMenu path is preserved. All nine native startup fixture cases and
the native unlock option plus five-case skip matrix pass
(`logs/regress-20261017-153428.txt`).

The final quick set passes **5/5**, 412 seconds
(`logs/regress-20261017-154147.txt`): 461 unit passes, 34 platform skips, one slow
branding case deselected, all three existing screen goldens unchanged, and 122
seconds of combat at median121 presents/2seconds (minimum113). Audit
`logs/startup-skip/quick-final-startup-audit.json` confirms four game launches:
**three skipped startups and exactly one positively ordered normal startup**.

The full set completed **16/17 in35minutes**
(`logs/regress-20261017-161802.txt`, console
`logs/startup-skip/pr25-final-full.txt`). The sole failed assertion is
`audio.dry_queue`, one post-StartGame underrun in `combat` (26/27 assertions;
all seven gameplay assertions pass). This same intermittent gate is recorded
in D122; this run does not isolate its cause. No gate was relaxed or waived.
Two-pad `versus` passes26/26, all six combat assertions. Replay exactly matches
80 golden records across1800steps. Story opening scenes reach the creator;
crib, gym movie requests, all launcher/overlay/options, goldens, and3/3soak
boots pass. Every completed save guard restores byte-identical.

Full startup audit `logs/startup-skip/full-startup-audit.json`: **20launches,
19 with one startup redirect and first FE screen preStart, exactly one legal/
EA/THX/intro startup, no unclassified launch**. The feature remains default off
for players; only the harness default is fast. Initial loading and native title
animation/input delay remain. Story and attract movies still play.

The full table’s old `ffa-terrordome` row exercised Hunt’s Point14, discovered
by checking its actual venue request. Its row must not be counted as Terrordome
coverage. The route correction, primary loading-caption evidence and focused
replacement stress are recorded in `startup-skip-venue-audit.md`. No game C or
binary changed for this route correction.

Scope: local macOS26.5.2 arm64, Clang21/MoltenVK. Windows and Deck have not been
tested by this session. PR25 remains open at exact head0bb115f as of16:41 on
2026-10-17; no remote merge or push occurred. The tested combined toolkit5445b73
remains unpublished, so the parent gitlink still names published9051be9 until
Vlad authorizes toolkit publication.

### Final affected checks and audio retry

The corrected Terrordome route passes240seconds of actual four-player
Terrordome combat at median120 presents/2seconds (minimum110), no detected
runtime fault or save mismatch (`logs/regress-20261017-163533.txt`). The
scenario composer was synchronized with its early command and loading capture;
actual request-context extraction reports venue6 and all four fighter slots
(`logs/startup-skip/terrordome-final-context.json`).

Final complete units pass **464**,34skip,1slow deselected. The original combat
check then passes **27/27**, including all7combat assertions, audio.dry_queue=0
and byte-identical save restoration
(`logs/regress-20261017-164056.txt`,
`logs/scenarios/regress-combat-20261017-163605/report.json`). The focused result
is2/2. The first full set remains recorded as16/17; a successful retry does not
turn that raw run green or establish an audio root cause. No audio code or
threshold changed. The game binary is unchanged since0ea5c5d. All game runs
are finished, and source hygiene is checked before the final source commit.

## Publication and v0.10.1 preparation (D128)

On2026-10-17 Vlad authorized pushing the integration and opening the PRs, naming
v0.10.1 for the startup option and more time-efficient regression sets. Toolkit
`5445b730d08348c20fc029cd728bb24a4a85c8e0` is now published and its remote SHA
verified. [Fork PR1](https://github.com/vyanhursky/xboxrecomp/pull/1) targets
`defjam/audio-fix`, preserving the tested tree. No upstream PR is opened.
The parent may now pin that published commit. Version0.10.1 and its release
notes are prepared separately from PR25’s pending merge/release.

The version-only0.10.1 build completes and is certified
(`logs/startup-skip/release-0.10.1-build.txt`);58 focused release-policy, agent
instruction and native startup tests pass. Its first short boot reaches preStart
but fails the unchanged runtime gate on one non-code ICALL80003344/caller27EC6E
(`run-20261017-201609.log.err`). Bounded triage matches the earlier normal boot
`run-20261017-143136.log.err` and documented baseline D95; no new cause is
claimed. The unchanged retry (`run-20261017-201656.log.err`) reaches preStart in
6.02s, one redirect, no detected fault, save restoration identical. The first
failure is retained rather than filtered. No game/runtime code changed in the
release preparation.

[Game PR39](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/39) is now pushed
and opened as a draft stacked on PR25’s `feature/clever-feynman-4ovoyk` branch.
It contains the published toolkit pin, version0.10.1, release notes and the
feature/test changes. Both PRs are attached to this task. PR25 was still open
at0bb115f during publication; after its merge/release, retarget and reconcile
PR39 onto main before merging/tagging0.10.1. No release or remote merge was
performed in this step. CI results remain available on the PR.

### PR39 CI dependency correction (branch-local D129/CI)

The first source-only scripts/unit job fails nine startup-fixture setups because
that job deliberately checks out no toolkit submodule (missing settings header
and source), while the local/native jobs include it. Log:
`logs/startup-skip/pr39-ci-python-first.txt`, run39082276988/job117541517642.
The fixture now skips only when its required toolkit source is absent, matching
the existing native-input dependency handling. Explicit startup fixture steps
are added to the recursive-checkout Windows/MSVC, macOS and Linux runtime jobs
so the source-only skip does not remove native CI coverage. Locally, all9native
cases pass with the toolkit; an isolated source-only test copy skips all9.
No game/runtime source or regression gate changed.

## Reconciliation with the released v0.10.0 (D131)

The readiness check found old PR39 head9ff054d green28/28 but conflicting with
PR25’s updated02d9d14. That update restores the developer module/menu and the
scene launcher, adds game-side MSVC compilation, and prepares0.10.0. Its native
hooks were absent from the earlier startup integration. Conflicts are resolved
by retaining version0.10.1, both README features, the expanded Debug tab and
General startup checkbox, both release notes, and all native CI coverage.
PR25’s D129/D130 remain verbatim; the independently assigned PR39 CI decision
is qualified D129/CI rather than silently replacing either history.

New guest code is regenerated:17,896functions,20Cfiles,90MiB. The33-step native
build is certified (`pr25-updated-{recomp,build}.txt` under logs/startup-skip).
Units pass470/34skip/1slow deselected. Quick passes5/5
(`logs/regress-20261017-213306.txt`), including the ordered normal movie startup,
three unchanged goldens and122seconds of combat (median120/min105presents/2s).

Two targeted fast-start/developer-scene checks pass. SC12 enters cinema, ends,
removes its developer task on Unload and returns to the main menu, with no
detected faults and identical saves (`run-20261017-212547.log.err`). Its8second
capture caught the return-loading transition. SC05 provides actual visual proof:
the24second capture renders the scene, one startup redirect, no faults, saves
restored (`run-20261017-213317.log.err`,33.10seconds elapsed). Image stays local:
`logs/startup-skip/updated-scene-sc05.png`. These are request, execution and
visual observations separately; a request log alone is not render proof.

PR25 is now merged at5331f58 and v0.10.0 published. The main merge tree is
identical to02d9d14; PR39 is retargeted to main. The toolkit pin is still the
same published5445b73. Updated full regression completes on this exact source
and guarded disposable saves: **15/17 in36minutes**
(`logs/regress-20261017-221050.txt`, console
`logs/startup-skip/pr25-updated-full.txt`). All three screen goldens, UI/options,
fights, actual240second Terrordome (median120/min102), replay80records/1800steps,
Story/creator/crib/gym and3/3soak pass. Every save-restoration gate passes.

The only failures are `audio.dry_queue`: combat26/27, dry3/drops0
(`logs/scenarios/regress-combat-20261017-215259/report.json`), and versus25/26,
dry1/drops0 (`logs/scenarios/regress-versus-20261017-215710/report.json`). All7
combat and6two-player gameplay assertions pass. Bounded triage confirms live
presentation through the final captured frames; the verbose tail is not proof
of a hang. These are the two known strict gates documented in released0.10.0
(and D122), not a newly isolated audio cause. No audio source, limit or gate is
changed or waived. This raw full run remains15/17; the earlier27/27 retry belongs
to the prior integration and is not presented as a new retry of this source.

The updated startup audit passes: quick4launches=3fast/1normal, full20=19fast/
1normal, zero unknowns and exactly one redirect in each fast launch. Evidence:
`logs/startup-skip/pr25-updated-{quick,full}-startup-audit.json`.

Vlad explicitly authorizes merging and releasing0.10.1 (D132). Toolkit forkPR1
is merged with merge commit `a37e19edf07473fd8e963209a28a2cdd3e142598`; verified
5445b73 remains its ancestor and the game gitlink stays unchanged/clean. Main's
subsequent PR41 only records publication of0.10.0; preserve that metadata while
finalizing this integration. Fresh PR39 CI and tagged release asset checks completed as recorded below.


## v0.10.1 published (D132)

Final head99c5d56 passes28/28 CI checks (39089261537), including Windows/MSVC,
macOS and Linux native startup fixtures and game-free setup builds. PR39 merged
with merge commit `0985f40c4b8230fed1dd4165a03b7f0fd16102d2`; its tree exactly
matches the tested head. Source tagv0.10.1 names that reviewed main commit.
Local release policy verifies version, notes, exact tag/main ancestry and
source hygiene396files/0fail. Source release workflow39089619805 passes30/30.

Exactly9 workflow assets were downloaded and independently verified: three
game-free setup packages, SHA-256 files and provenance JSON. Every file matches
its GitHub SHA-256 digest; each installer matches its checksum and provenance.
All production manifests name source0985f40 and toolkit5445b73. Payload file
counts are4186Windows,4907macOS,3234Linux. Local verification evidence:
`logs/startup-skip/release-v0.10.1/verification.json`. No game executable, lifted
source, logs, captures or dump data is attached.

Published2026-10-17 22:25:58 ET and verified through the latest-release API:
[v0.10.1](https://github.com/vyanhursky/DefJamFFNY-recomp/releases/tag/v0.10.1).
The release notes retain the strict15/17 full outcome and its known audio limits.
Merged game branch is pruned/deleted locally after switching to main; the toolkit
integration branch remains checked out in its isolated worktree and is retained.

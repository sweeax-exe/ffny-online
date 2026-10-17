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

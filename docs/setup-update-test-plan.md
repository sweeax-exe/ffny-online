# Setup release selection and update acceptance plan

Status: implemented in draft PR #18; macOS initial execution recorded in
[the results ledger](setup-update-test-results.md). Full release acceptance is pending. Baseline:
public v0.7.2 (`ce4d4ab`). This plan covers the setup wizard/installer, not a
background updater inside the game launcher. See [setup guide](setup-installer.md).

## Product contract (D100, 2026-10-12)

- Windows x64, Apple Silicon macOS and **Steam Deck/SteamOS only** for Linux acceptance.
  Ubuntu hosted jobs validate portable code and packaging; they do not establish
  desktop Linux support or Deck acceptance.
- Install, Repair and Clean Update are distinct. Clean Update builds in a fresh
  candidate, with fresh analysis/lift/objects. It preserves the installed dump,
  saves/profiles/caches, settings, mods, HD packs and user artwork. It never starts
  by uninstalling the working game. Same-version clean rebuild is also isolated.
- GitHub published releases are visible; unavailable platform assets are explained.
  Latest stable is the default online selection, bundled release remains usable
  offline. Prereleases stay hidden by default as an implementation default; there
  is no requirement to add a prerelease channel in this iteration.
- Historical pre-updater versions are **not downgrade targets**. Legacy -> new
  update is supported; new -> legacy is refused in GUI and CLI before mutation.
  Fresh historical install may use its own engine. Existing historical repair
  uses the original setup directly; the new updater refuses that remote handoff.
- Let **U0** be the first published release implementing this contract; its actual
  version is assigned when released, not guessed here. Downgrade starts with future
  capable versions: U1 -> U0, U2 -> U1/U0, only when the target supports the required
  protocol and settings/save/pack formats. A version number alone is insufficient.
  Incompatible pairs are refused with an actionable reason; no silent data reset.
- Failure recovery to the pre-operation installation is distinct from user-requested
  downgrade. A failed legacy -> U0 attempt must restore the legacy installation.
  This recovery does not expose legacy releases as selectable downgrade targets.
- No dump/game/generated code leaves the computer. Continue existing unsigned setup
  policy and approved exe/dmg/tar.gz formats. No publication of new asset types.

## Fixtures, environments and evidence

Use synthetic game-free payloads for CI. Include native tiny executables/scripts
that report marker/version, executable path, data root and working directory and
can hold a game lock. These test real process/launcher behavior, not only receipts.
Use a synthetic disc directory/image; do not disguise real game bytes as fixtures.

Fixture labels: **L** = legacy receipt/payload (v0.7.2 plus earliest supported setup
schema per platform); **A/B/C** = future U0/U1/U2 protocol-capable packages with distinct
source/toolkit/dependency/build fingerprints; **X** = incompatible future protocol
or data schema. These labels are not public release tags. Include prior settings
with comments, unknown keys, repeated sections, custom bindings and paths; multiple
profiles, populated caches, user/owned HD packs, sentinel files, foreign shortcuts
and custom Steam artwork. Keep a mock release server with deterministic manifests,
pagination, downloads and network faults. Production URL overrides must not silently
enable untrusted endpoints; any fixture hook must be test-only and unavailable in
normal released setup.

Real acceptance uses the owner's verified dump and **throw-away copied data roots**.
Never test downgrade, disk-full, hard shutdown or migration against the real save.
No overlapping game/build/audio runs. VM snapshot or disposable account/volume for
crash/reboot tests; physical Deck/Mac/Windows smoke is a separate requirement.

For each case record: case ID, exact setup SHA-256/source/toolkit commits, selected
release/asset ID and digest, source/target schema, OS/build/architecture, filesystem,
install/data/cache volumes, compiler/SDK or container digest, options, injection
point, exit code, log/report path, pre/post hashes, actual launcher marker and result
PASS/FAIL/BLOCKED/NOT RUN. N/A needs a platform reason and is never counted as PASS.
Keep raw results in `logs/setup-updates/<session>/` (local, ignored); publish only
game-free summaries. An unavailable platform host leaves its gate BLOCKED.

Capture a full data inventory before and after setup: relative names, file SHA-256,
symlink targets, relevant executable permissions and directory identity. Compare
before launching the game, because normal game execution changes saves/caches.
Require byte-identical original dump, saves, profiles, existing packs/mods and
unknown user files. Explicitly requested HD generation may add owned packs/caches
and update its settings selection; enumerate that allowlist per run. No other
settings changes are permitted. Future format migrations require a reversible,
documented allowlist and backup; downgrade must preserve current progress or reject
the incompatible data, rather than silently restoring an older save snapshot.

## Coverage strategy and required operation matrix

Every identified case below is required at its stated layer. Combine inexpensive
synthetic cases exhaustively. Use pairwise coverage for real option combinations,
with the named high-risk combinations below mandatory regardless of pairwise output.
Store the generated combination manifest so coverage is reproducible. Do not claim
that a few happy-path installs cover every combination.

| ID | Starting state -> operation | Required outcome | Execution |
|---|---|---|---|
| OP01 | Empty -> bundled install offline | Builds bundled identity; launch path works without Git/system Python | CI synthetic + all hosts |
| OP02 | Empty -> latest stable online | Verifies exact platform asset; selected setup owns build and result | CI synthetic + all hosts |
| OP03 | Empty -> selected non-latest compatible install | Explicit choice retained; not silently changed to latest | CI synthetic + all hosts |
| OP04 | L -> U0 clean update | Fresh candidate; copied installed dump works without original ISO; data preserved | CI synthetic + all hosts |
| OP05 | A -> B clean update | No prior generated/object reuse; active A remains intact until commit | CI synthetic + all hosts |
| OP06 | A -> C skipped-version update | Compatibility checked across skipped versions | CI synthetic + all hosts |
| OP07 | A -> A repair, intact then corrupt owned source/executable | Valid work reused; corrupt owned work restored; user data untouched | CI synthetic + all hosts |
| OP08 | A -> A clean rebuild | Distinct attempt path even with identical version/commits | CI synthetic + all hosts |
| OP09 | Failed first install -> rerun | Safe owned-stage resume; no fake active installation | CI synthetic + all hosts |
| OP10 | Failed/cancelled update -> rerun | A still launchable, rerun yields one consistent B | CI synthetic + all hosts |
| OP11 | B -> A and C -> B/A future downgrade | Compatible target rebuilt and activated; current player data preserved | CI synthetic + all hosts |
| OP12 | A/B -> L downgrade | GUI disabled with reason; CLI rejected before download/build/mutation | CI synthetic + all hosts |
| OP13 | C -> X or incompatible future A data/protocol | Clear refusal, current version/data unchanged | CI synthetic + all hosts |
| OP14 | Successful update/downgrade -> repair -> uninstall -> reinstall | Ownership cleanup correct; data retained and reusable | CI synthetic + all hosts |

At U0 release, native A/B/C packages must be local game-free test builds exercising
the real protocol, not production tags or a reason to publish dummy releases. When
U1 ships, replace OP05/OP11 evidence with the actual U0/U1 packaged pair on all hosts;
each later release tests adjacent and oldest supported capable downgrade targets.

Option dimensions: wizard/silent; ISO/XISO/extracted input (fresh install); original
present/absent on update; defaults/custom roots; same/separate volumes; HD unchecked,
newly checked, pre-existing enabled/disabled/user pack; all shortcuts/menu only/none;
online/cached/offline; default/non-latest release; Deck Steam addition on/off and
existing/missing shortcut; compiler already provisioned/missing/repaired. Enumerate
unsupported combinations as refusal tests, not successful paths.

Mandatory real combinations per host: XISO+defaults+HD off+normal shortcuts; extracted
folder+Unicode/custom paths+HD on; update with original missing+existing custom
settings/HD+shortcuts disabled; future downgrade with profiles advanced since update;
separate install/data volumes; wizard and silent for at least install/update/downgrade.
On Windows also supported full ISO input; on Mac/Deck exercise full ISO at least once
if the verifier advertises that input. Every individual option occurs at least once
in its real platform lane. Publish the option manifest with the evidence summary.

## Common cases: discovery, integrity, paths and process control

| ID | Cases / stimulus | Required result | Layer |
|---|---|---|---|
| NET01 | Empty list; >100 releases; missing/looping pagination; reordered results; malformed JSON; v0.9 vs v0.10 | All reachable releases once; bounded fetch; numeric ordering; explain empty/incomplete list | CI |
| NET02 | Draft/prerelease/source-only release; latest lacks host asset | No draft install; default stable; unavailable reason; latest compatible distinguished from latest GitHub release | CI + wizard smoke |
| NET03 | Duplicate asset names, wrong architecture/extension/tag, upload pending/deleted/replaced after selection | Pin asset identity; refuse ambiguous/wrong/stale assets; re-fetch only with explicit selection reconciliation | CI |
| NET04 | Offline/DNS/TLS/timeout/proxy error; ETag/304 cached refresh; 403/429 rate limits; 404/5xx; Retry-After | Bounded retry; responsive cancel/logs; bundled/cached repair remains available; no token required | CI + clean-host TLS |
| NET05 | Disconnect mid-download; short/oversize response; checksum mismatch; digest absent; invalid provenance | Never extract/execute unverified data; missing trustworthy digest refuses online route; safe fresh retry | CI |
| NET06 | Cancel then resume; corrupt cache; two download sessions; cache renamed or low disk | Verified cached bytes only; unique staging; no stale partial interpreted as complete | CI |
| NET07 | Redirect downgrade to HTTP/untrusted host; credentials/token in URL; external release-note link | Reject unsafe download redirects; no secrets/log injection; notes rendered as inert text | CI |
| ARC01 | Zip/tar traversal, absolute/drive/UNC paths, symlink/hardlink escape, device/FIFO entries | Refuse before writing outside private staging or running any code | CI |
| ARC02 | Duplicate/case-folded entries, reserved names, corrupt compression, expansion bomb, wrong inventory/schema/development payload | Bounded extraction; fail before child execution; ownership/data untouched | CI |
| PATH01 | Root/nonempty foreign folder; nested/identical roots; symlink/junction aliases; manipulated ownership receipt | Reject without modifying foreign files; canonical roots verified under lock | CI + native filesystem |
| PATH02 | Spaces/Unicode/quotes/ampersands/percent/backticks/dollar/newlines; boundary path length | Literal arguments, no shell execution; supported paths work; unsupported paths rejected early | CI + all hosts |
| PATH03 | Read-only/noexec volume; disappearing/remounted removable volume; permission revoked mid-run | Clear failure; no fallback to unintended root; current version still works when volume restored | CI fault + hosts |
| PATH04 | Install/data/cache on distinct devices; just-below/at budget; disk full mid-stage | Operation-specific budget includes candidate/download/backup/HD/container; safe cleanup and recovery | CI fault + hosts |
| DATA01 | Installed extraction valid, missing, extra/tampered file, changed dump identity, original absent | Validate target manifest; reuse valid installed copy; request original/new valid location if corrupt; no merge | CI + all hosts |
| DATA02 | Custom settings/comments/unknown keys/bindings; profiles/caches; user/owned HD packs | Preservation inventory passes; no unrelated rewrite; pack compatibility refusal never deletes user packs | CI + all hosts |
| DATA03 | Foreign files in owned install; foreign shortcut/art; relocation request; two installs sharing data | Owned cleanup only; relocation refused as update unless implemented; data lock prevents concurrent mutation | CI + native |
| BUILD01 | Analysis/lift/build/HD/runtime probe fails; missing compiler/dependency; changed pins | No promotion; prior launcher really launches prior version; rerun validates stages | CI + hosts |
| BUILD02 | Deliberately seeded old generated C/objects; same-version rebuild; compiler/SDK/container change | Clean candidate uses fresh outputs; repair reuse only with complete valid fingerprints | CI + hosts |
| HAND01 | Selected child setup; parent exit/crash/cancel; child launch refused/fails | No fetch recursion or lock deadlock; explicit result ownership; no false parent success | CI native + hosts |
| HAND02 | Dump/data/install/HD/shortcuts/Deck toolchain/Steam options transferred | Literal choices arrive unchanged; child uses target release engine/interpreter/pins | CI native + wizard |
| LOCK01 | Two setups same install or shared data; game via launcher or direct executable; game starts during update | One writer; native play lock blocks racing launch; busy exit; no mutation of active version | CI multi-process + hosts |
| LOCK02 | Force-kill holder; stale lock files; second user without permissions | OS lock released; stale file harmless; permissions respected; no steal of live lock | CI native + hosts |
| UI01 | Refresh/change selection; download/build in progress; close/back/cancel; double click start | No duplicate operation; controls reflect actual state; window remains responsive; stale callback cannot apply old selection | CI smoke + manual |
| UI02 | Installed/selected/bundled/latest labels, unavailable/downgrade reason, progress/log path | No ambiguity about target/action; logs accessible on early failure; no automatic game launch | CI smoke + manual |
| CLI01 | Explicit version/operation, conflicting flags, bundled-only handoff, help, all existing options | Deterministic validation, same policy as GUI; no interactive fallback in silent mode | CI + packaged smoke |
| CLI02 | Success/invalid input/prerequisites/stage failure/busy/cancel/reboot | Preserve documented 0/2/3/4/5/6/3010 meanings; classify any new network codes explicitly | CI + packaged smoke |

## Durable activation and crash matrix (release blocking)

Provide named checkpoints as internal test hooks, off by default. Parent controller
waits for a checkpoint, then terminates the child process tree from outside; caught
exceptions alone do not test crash recovery. Flush journal/files and stage renames
on their destination filesystem. Test failure of the recovery itself, not only
happy-path rollback. No irreversible settings migration in the first iteration.

| ID | Interrupt / fail point | Required state on next setup/launcher invocation |
|---|---|---|
| TX01 | Before/after journal prepare and backup publication | Old installation/data intact; incomplete candidate not active |
| TX02 | Mid source staging/extraction/analysis/lift/compile/HD | Partial owned staging identifiable; old executable remains launchable |
| TX03 | Before/after new launcher staging/signing | Old launcher remains present; partial launcher not used |
| TX04 | Between old launcher/app backup and new launcher promotion | Recovery restores a usable old launcher, including macOS bundle |
| TX05 | Before/after authoritative active-pointer replacement | Recovery settles entirely old or verified new version; launcher/receipt agree |
| TX06 | Between JSON/Windows INI/HD settings writes | No mixed authoritative version/settings; original bytes restored on rollback |
| TX07 | Menu/Desktop/Applications/Steam shortcut or icon/art failure | Core installation usable; foreign entries unchanged; optional failures logged and repairable |
| TX08 | Before/after commit marker and during old-version cleanup | Committed target retained; never delete active/required rollback paths |
| TX09 | Journal corrupt/truncated/unsupported; backup missing/altered; escaping journal path | Reject unsafe recovery; preserve evidence and owned builds; no arbitrary deletion/execution |
| TX10 | Repeat recovery, kill during recovery, disk full/permissions during rollback | Idempotent or safely blocked; backup not destroyed before recovery succeeds |
| TX11 | B fails launchability check; downgrade compatibility fails after staging | A remains active; no profile/settings reset; staged candidate not reported installed |
| TX12 | Restart/reboot after each rename/write boundary | Durable journal reconciles correctly; locks reacquired; no misleading success |

Run TX01–TX11 for **caught failure + explicit cancel + hard process termination**, at
before/after sides of every persistent mutation, on Windows/macOS/Linux native CI
where supported. Include OP04 legacy update, OP05 future update, OP08 same-version
clean rebuild and OP11 future downgrade in the synthetic transaction matrix. Run
each termination checkpoint at least three times to expose ordering/race failures.
TX12 uses disposable VM reset/reboot on each OS; physical hosts verify recovery
from killed processes. Do not equate process termination with proven power-loss
durability. Loss of optional Steam integration cannot undo a committed core build;
retry reconciles it without creating duplicates. Always run the real stable launcher
with a synthetic version marker after recovery, not merely compare JSON.

## Windows acceptance lane

| ID | Native scenario | Required result |
|---|---|---|
| WIN01 | Windows 11 standard user, no compiler; provisioning unchecked/checked; UAC denied/accepted; offline/reboot pending | No unwanted elevation; clear prerequisite stop; opted-in Microsoft provisioning; resumable after reboot |
| WIN02 | VS 2019 supported build tools and VS 2022; SDK absent/changed; toolchain environment with secrets | Correct probe/build; invalid reuse refused; no environment secrets in logs |
| WIN03 | Browser-downloaded unsigned exe; SmartScreen allowed/denied; downloaded target setup in cache | Expected system prompt respected; denial retains current game; no self-overwrite |
| WIN04 | Active game/launcher, locked exe/DLL, antivirus delay; NTFS junction; install/data separate drives | Busy/failure recoverable; no forced close; launcher/INI/JSON consistency |
| WIN05 | Start menu/Desktop/all off; foreign matching-name shortcut; launch via each location from wrong CWD | Stable target/data/CWD; preserve foreign shortcut; choices survive handoff/update |
| WIN06 | GUI keyboard/tab/high DPI; silent setup; long/Unicode/metacharacter paths | Usable controls, accurate progress/log path; arguments literal; same operation policy |

Run clean Windows VM for compiler/UAC/SmartScreen and transaction reset tests;
physical Windows with the owner's dump for installed launcher/profile/fight checks.

## macOS acceptance lane

| ID | Native scenario | Required result |
|---|---|---|
| MAC01 | Apple Silicon on declared minimum supported OS and current OS; Intel/wrong payload | Supported hosts work; unsupported architecture/OS refused before build; unavailable minimum host blocks claim |
| MAC02 | Missing/outdated CLT, invalid selected SDK, compiler repair; no Homebrew/system Python | Actionable prerequisite stop; bundled dependencies used; safe rerun after CLT repair |
| MAC03 | Browser-downloaded quarantined dmg/setup; Open Anyway accepted/denied; target dmg read-only mount | Gatekeeper preserved; no silent quarantine removal; denied open leaves current version intact |
| MAC04 | Setup launched from dmg vs writable app copy; mount already used/fails; child handoff or cancel | Unique mount/staging; cleanup only own mount after child exit; never detach unrelated mounted image |
| MAC05 | App-bundle swap interrupted; codesign fails; ~/Applications/Desktop denied; foreign same-name app/alias | Working old/new launcher recoverable; ad-hoc signed candidate; foreign app preserved; optional shortcut failure nonfatal |
| MAC06 | APFS case-insensitive/case-sensitive; install/data external volumes; app moved/stale shortcut | Clear stale-root handling; no unintended relocation; stable target/data/CWD and path validation |
| MAC07 | AppKit keyboard/accessibility/high DPI; close while busy; launcher from Dock/Finder | UI responsive; result accurate; installed profile and bindings usable |

Downloaded first-open evidence must come from a clean account/Mac; a locally built
unquarantined bundle is not Gatekeeper acceptance. Current and minimum OS checks use
separate hosts/VMs when the product continues to advertise that minimum.

## Steam Deck acceptance lane (Linux product scope)

| ID | Native scenario | Required result |
|---|---|---|
| DECK01 | Stock declared SteamOS stable (record exact build), no compiler; Desktop wizard and silent; LCD/OLED when available | Container path works without disabling read-only system or requiring root; every advertised hardware variant has evidence |
| DECK02 | Podman absent/unavailable; container uncached/cached/stale pin; registry offline/digest failure/low container storage | Clear prerequisite/fetch failure; pinned container only; cached offline build works; current install untouched |
| DECK03 | Host compiler option valid/missing; switch host/container; library/driver missing or incompatible | Fingerprint invalidation; host runtime checked separately; no activation of unusable binary |
| DECK04 | Internal storage; data on ext4 or exFAT microSD; noexec/install on unsupported filesystem; card pulled/remounted | Supported copy fallback works; unsupported install refused clearly; no wrong-root writes; data preservation after remount |
| DECK05 | Add to Steam on/off; existing shortcut; Steam stopped/slow write; multiple users; customized artwork | One stable shortcut; same appid/controller identity after update/downgrade; foreign/user art preserved; stopped Steam nonfatal |
| DECK06 | Desktop install -> Gaming Mode launch; update -> return to Gaming Mode; future downgrade -> relaunch | Stable executable/CWD/data; controls/audio/overlay/settings/profile work; no duplicate library entry |
| DECK07 | Suspend/resume during download, container build and activation; network loss; low battery interruption simulated on disposable data | Resume or safe recovery; truthful state/progress; prior install remains available |
| DECK08 | Handheld/docked, touchscreen/trackpads/on-screen keyboard; HD on/off, custom bindings | Setup usable without external keyboard; unchanged options/settings; target game plays a match |

Hosted Ubuntu tests cover container recipe boundaries, launcher locks/quoting,
desktop entries, safe extraction and Steam binary-VDF fixtures. A real Deck is
mandatory for DECK01/04–08; no desktop-Linux acceptance claim follows from Ubuntu.

## Test implementation and release gates

Existing coverage to preserve: `tests/unit/test_setup_engine.py` (ownership, input,
locks, extraction, resume, cancellation, shortcuts); `test_hd_setup.py` (pack/settings
publication and preservation); `test_linux_setup.py` (container, launcher, Steam
and packaging); `test_macos_payload.py`; `test_source_release.py`. New coverage in `test_setup_updates.py` tests release policy/transport, fresh
candidates, verified installed-dump reuse and subprocess journal recovery.
`scripts/test-setup-updates-macos.py` builds native marker executables with the
packaged runtime and exercises signed launcher activation; it runs in macOS CI.

Implement shared release/transport/policy tests, native subprocess transaction
fixtures, and packaged handoff/UI smoke in **each** existing setup CI job. Test
discovery deterministically with mocked HTTPS responses; use a read-only live public
GitHub smoke once per candidate to verify production API/TLS/redirect behavior.
Do not publish fixtures/dummy releases or rely on mutable public latest in assertions.
Run native synthetic recovery tests without requiring game/toolkit compilation;
setup package builds still inventory the exact published source/toolkit pins.

Maintain a results ledger grouped by ID, platform and variant with planned/implemented/
executed status. Links to logs and inventory hashes are mandatory for PASS. Machine
reports should emit JSON/JUnit, CI fail on unexpected skips or zero collected tests,
and preserve failed evidence. Pairwise option coverage is checked from its manifest.

Release gates:

1. All common policy/integrity/preservation/native transaction cases pass on the
   three CI OS lanes, including hard termination and repeated recovery. No data
   corruption, mixed active version, unsafe extraction or legacy downgrade bypass.
2. Packaged wizard/silent/handoff checks pass for exe/dmg/tar.gz, and source/payload
   hygiene/provenance checks pass. No asset-format exception or game data in CI.
3. Required native host matrix and option manifest complete on Windows, Apple
   Silicon and Deck; missing minimum OS/hardware evidence narrows support or blocks
   the advertised claim. Gatekeeper/SmartScreen/Steam are checked on real downloads.
4. On each installed target, use disposable saves for launcher/profile/settings,
   a complete fight, quick regression and HD off/on where selected. Run the existing
   full regression if runtime/lifter/renderer changes; do not weaken goldens or
   classify inherited failures as PASS. Record baseline failures separately.
5. Prove L -> U0 recovery and future-capable B -> A downgrade end to end, including
   current progress preserved, stable shortcuts and reinstall after uninstall.
   First U0 validates downgrade with synthetic capable packaged peers; U1 requires
   actual U0/U1 package evidence. Any later data-format change repeats compatibility
   tests against every declared supported downgrade target.
6. User-visible docs match actual supported operations and versions; exact release
   notes enumerate unresolved platform limits. Owner reviews concrete native results
   before release. Draft PR status alone does not imply updater completion.

Current execution: shared engine, three wizard integrations, packaging and CI
tests are implemented. macOS packaged native operations and initial owner-dump
build/update/game checks have run. See the [results ledger](setup-update-test-results.md)
for exact evidence, fixture limits and pending Windows/Deck/option/reboot gates.
None of the six release gates above is declared complete by these initial results.

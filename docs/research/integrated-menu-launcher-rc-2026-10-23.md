# Integrated hosted-menu launcher RC — 2026-10-23

## Scope

Historical integration of PR17/62/73/82 into PR91, later shipped as Mod Support Beta.
The October23 base was dcd0df0/toolkit8589f88; candidate source0279950 used version0.10.8
and combined toolkitbd8c633. These are test identities, not the current release pin.
The independent RC2 archive was421.6MB and installed423.4MB; retaining download and
installed copies took about845MB before rollback copies. Artwork/game executables
did not enter the game/catalogue source repositories. Current user instructions
are in the mod-manager and menu handover guides.

## Changes beyond the prior PRs

- Windows helper locks no longer write a sentinel byte before locking. Windows
  byte-range locks support ranges beyond EOF; a running game locked the empty
  file, causing initialization writes and buffered-close flushes to fail. Busy
  rejection now has no pending write and retains normal error handling. Native
  interop test asserts the file remains empty during/after acquisition.
- An explicit native "Show testing versions (release candidates)" checkbox
  passes --prerelease to all helper jobs for that launcher session. Default stays
  stable-only. Metadata shows testing status; disabling the filter never removes
  installed testing versions. Helper tests cover selection/view/install behavior.
- macOS image creation sizes the temporary filesystem from source allocation plus
  128MiB metadata/headroom and reports free disk space. This gives hdiutil deterministic
  filesystem headroom after the prior no-space failure; CI now passes packaging
  on all three platforms.
- Catalogue source includes RC2 with exact immutable R2 URL/size/hash, capabilities,
  expansion bounds and optional Faithful order recommendations. Runtime minimum
  0.10.8 plus wide-v1 capability prevents offering it on existing released builds.
- Explicit protected first-publication bootstrap accepts reviewed initial entries,
  applies the same download/archive/malware checks as later publications, and
  refuses an already-published signed history. Unexpected404 without bootstrap
  still fails closed. This avoids two owner signing approvals merely to publish
  an empty catalogue and then its first mod. Owner approval is never bypassed.

## Validation

- Native combined build succeeds with DEFJAM_MOD_WIDE=1.
- Fast units589pass/32skip/1slow deselected with normal setup-cache/process access.
  The first sandboxed run had two permission-only failures, rerun successfully.
- Combined toolkit texture fixtures3/3 and settings1/1 pass.
- Real RC2 validates through catalogue archive parser:157 texture mappings and
  textures-v1/wide-v1 capabilities. Full public bytes/hash/range verification is
  retained in the R2 upload report.
- Completed serial regression, manager tours, platform CI and Windows artifact
  verification are detailed below. Actual hosted installation and Play also pass
  against the owner-approved live catalogue.

## Operator receipts

- Catalogue source: d1f8e796c32b8027f05d2dd70ca2aa8325a8abec, catalogue PR1 merged.
- Protected publish run: https://github.com/vyanhursky/DefJamFFNY-mods/actions/runs/39996850444.
  Download verification, archive validation and malware scan pass. Vlad approved
  the protected catalogue-release deployment; signing and Pages publication
  completed successfully at 2026-10-23 21:17UTC. No approval boundary was bypassed.
- Local native build: logs/integrated-rc-build-v0108.txt.
- Full serial regression: logs/integrated-rc-regression.txt, using the preserved
  disposable mod-manager-native/data fixture in the manager worktree.
- Unrestricted units: logs/integrated-rc-units-unrestricted.txt.
- Toolkit focused fixtures: logs/integrated-rc-texture-fixture.txt and
  logs/integrated-rc-settings-fixture.txt.
- Reusable native tour: `scripts/test-mod-launcher.py` (synthetic update/order,
  large text, recovery and testing-channel install). The RC2-only hosted tour is
  historical evidence; its pinned source is linked in the hosted-acceptance section below.

### Full-run diagnostics retained

The two-controller versus run completes25/26 assertions, including its gameplay
checks, but runtime.faults flags the historical stream callback0x001E3C14.
Digest and bounded original slices were inspected: logs/run-20261023-110256,
ICALL at24290, caller0x001E3A68. D108 and the earlier launcher implementation
report record the same callback before this work. This is not suppressed or
counted as a full pass; it is a retained baseline limitation.

The copied-fixture intro run stops at the existing-profile name dialog
(USR_NAME_EXISTS_XBOX, last occurrence22354 in logs/run-20261023-110952).
Bounded original slices confirm continuous rendering afterward. It does not
reach Story creation, so that row remains failed; the same fixture limitation
is recorded in the prior manager validation report. Crib passes separately.

Gym also reproduces the previously documented route mismatch: the copied profile
reaches story/clothing.swf (17046 in logs/run-20261023-111624), not Athletics.
The digest and bounded original screen calls/tail show normal continuous
rendering; the intended gym/movie-preview gate remains failed, not covered.

## Completed full native regression

15/18 passed in44minutes, logs/regress-20261023-112317.txt. Three goldens,
launcher/overlay/settings, one-on-one, both four-player checks, combat27/27,
deterministic replay80records/1800steps, crib and boot soak3/3 pass. Versus
runtime callback, intro duplicate-name and gym route failures remain as described
above. Every run used complete-save guards; no ordinary save/settings changed.

Combined toolkit published as bd8c633 on defjam/integrated-menu-mods-rc, retaining
8589f88 current startup fixes and dd63e17 wide/long-settings source. Active toolkit
files match the tested tree byte for byte; build certification remains valid.

Four focused native manager tours pass: synthetic update/shared order,820x560
at250% text, interrupted recovery preserving skip=true, and explicit testing
channel installation of2.0.0-rc1. All exit 0 and restore saves byte-identical.
Receipts/captures: logs/integrated-rc-native-manager/launcher-receipts.json;
testing and large-text captures inspected. This does not substitute for actual
signed hosted RC2 installation, which is checked separately below.

## Platform CI and Windows candidate

[CI40003181247](https://github.com/vyanhursky/DefJamFFNY-recomp/actions/runs/40003181247)
passes29/29, including Windows native lock/bridge fixtures, Windows/Linux/macOS
setup builds, runtime fixtures and source hygiene. Both prior failed CI gates
are resolved. Game PR91 is conflict-free with main; no stable game release/tag
has been made. The real hosted-install gate is checked separately below.

[Windows game-free setup artifact](https://github.com/vyanhursky/DefJamFFNY-recomp/actions/runs/40003181247/artifacts/12521984699),
windows-setup-assets, contains DefJamSetup-0.10.8-windows-x64.exe, its checksum and
provenance. Downloaded locally and verified:58819072bytes, SHA256
8fbc03aee2a762c801c32f8dac299ac2bd92ebfb992992517a64c7bafc3a8212.
Payload SHA2566e4ea7527fdc925934a446d138d33f2d80384babae1d92378d75e2d58c1c6a82.
CI merge source dd17361144e563bf988a86bd9aefb956d65396c7 has parents main dcd0df0
and candidate0279950; toolkit pin bd8c6336c268cf15783b480fce6869eb11d74ec0.
Local source comparison matches208files exactly plus367Windows CRLF checkout
variants, with no other differences. Full receipt is private under
logs/integrated-rc-windows-verified.json; no installer bytes are tracked in Git.

## Additional native acceptance

Fresh empty-save intro passes in157seconds: both Story cutscenes reach character
creation, median120presents/2seconds and four captures. This separates the copied
fixture collision from working Story startup without erasing the full-run failure.
Receipt logs/regress-20261023-112708.txt.

Approved unchanged menu-only pack passes Press Start and settled Main Menu at
4:3,16:10,16:9,21:9 and32:9, with captures at640x480,768x480,854x480,1120x480
and1707x480 respectively.4:3 records zero wide side batches; all four wide cases
record positive side replay. All save guards restore identical. The first two
ultrawide attempts used unsupported short enum labels and fell back to16:10;
these are explicitly excluded, with separate rejected-attempt receipts. Corrected
runs use the exact experimental enum labels and assert captured dimensions.

32:9 actual pre-fight loading also passes with both optional Faithful components
preceding RC2:1707x480 loading capture, complete match start, no runtime faults
and byte-identical save restoration. Main-menu montage and loading capture
inspected locally. Receipt logs/integrated-rc-wide/receipts.json, corrected
loading log logs/run-20261023-113657.log.err; no new artwork was generated.

## Owner-approved live publication

Vlad approved catalogue-release deployment 39996850444. Both verify and publish
jobs succeeded; the live index and detached signature are available at the
client's default GitHub Pages URL. Client verification with the unchanged pinned
public key accepts revision 1, issued 2026-10-23 17:46:17UTC and expiring 2026-11-22
17:46:17UTC. The RC2 record binds the exact 421563157-byte immutable R2 archive
and SHA256 43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634.
This was the initial metadata expiry; it is not a current catalogue-availability
check. Installed packs/offline play were independent of that publication window.

At that checkpoint PR17/62/73/82 were superseded by PR91, before its eventual
release. The following tests retain their original candidate scope.

## Real hosted launcher acceptance — passed

From a new disposable data root, the [historical RC2-specific helper](https://github.com/vyanhursky/DefJamFFNY-recomp/blob/2e6648cf29073db24147cf8188ce99efdf315d64/scripts/test-hosted-menu-launcher.py) exercised
native Mods controls against the production URL and unchanged client trust pin.
No local archive, synthetic catalogue or substitute key was supplied. Refresh
accepted signed revision 1; Show testing versions exposed RC2; Download and install
fetched 421,563,157 bytes from R2 and installed the stable menu-ui-hd-wide folder.
All 326 installed files subsequently matched their recorded SHA256 values.

Enablement appended the menu pack after both local Faithful components. Reopening
with an unavailable network proxy preserved the enabled order. The native UI
reports 157 textures overridden by menu-ui-hd-wide. All four launcher tours exit 0
and restore saves byte-identical. Receipts/captures remain private at
logs/hosted-menu-launcher-20261023-141908/receipt.json and installed-hash-receipt.json.

A separate actual native Play click from the same managed settings starts the
game at 32:9 with no texture-root or pack-list environment overrides. Press Start
renders at 1707x480; metadata records 45 wide anchors/17 stretch entries and 73 wide
side batches. Capture inspected; no runtime faults and save restoration is
byte-identical. Play receipt is in the same folder; game log is
logs/run-20261023-142158.log.err. The triage digest's unclassified lines were
checked in bounded originals: unsupported filesystem-control probe, absent
optional metadata/LCONFIG, ordinary worker waits and unused CPU-raster counters;
continuous presentation and the wide capture confirm no stop at those lines.

The later Windows acceptance is retained in the consolidated appendix below.
The original Mac15/18 full-suite result remains a failed aggregate.


## Historical RC2 artifact and native acceptance supplements

This appendix describes initial RC2 acceptance, not current installation instructions. Current shipped game/menu versions are v0.13.0/1.4.0; see [current ownership and runtime coverage](../menu-ui-modpack-handover.md).

RC2 (`menu-ui-hd-wide-v1-rc2`, stable catalogue identity `menu-ui-hd-wide`, version `1.0.0-rc2`) contains 157 source-shaped 4× replacements (149 reviewed menu IDs plus eight actual-loader IDs), 164 side slabs, 45 wide anchors and 17 stretched overlays. Its 326 files expand to423,392,524bytes; ZIP421,563,157bytes, SHA-256 `43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634`. Faithful, private masters, captures, dump/code and executable bytes are excluded. The original 149 images/136 slabs are byte-identical between RC1 and RC2; RC2 adds eight loader IDs/28 slabs.

Public R2 delivery was verified on2026-10-23T16:46:50Z: full HTTP200 bytes/length/hash, plus a64KiB range at a nonzero1MiB offset returning206 with exact Content-Range and ZIP bytes. Immutable prefix: `menu-ui-hd-wide/1.0.0-rc2/43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634/`. This proves transfer/resume, not native visuals. The final immutable companion guide is `WINDOWS-PLAYTEST-v3.txt`,3572bytes, SHA-256 `44e878b6ee1597fd6cbaabc563295307b828365435cf8f5254733cae5fe9ac7a`; earlier guides were retained rather than overwritten. v3 corrects v2: combined dd63e17 already has1023usable pack-list bytes. Default urllib received403 for v2; the application User-Agent and actual `DefJamMods/1` ranged read passed, without host security changes.

The then-public v0.10.7 toolkit `8589f8834e3c370e2def3c6757b0d4f6e3e89d70` lacked `[wide]` parsing and `texture_pack_wide_slabs`. Successful hosting alone therefore did not make that release compatible. Historical compatible menu CI source `bf967a316b8347a690908be320c0e8e8492a4f94` used toolkit `dd63e17f0bd0a23f5a5b7218ee1c3bbb2ced994d`; CI39447027154's game-free setup SHA-256 was `12e8e07f09d0300abfae5e4f8edd8f8bb68cd4b61aacc5e07036ce137a1c36f0`. The integrated candidate above superseded that manual test path.

Before integration, native RC2 one-on-one loading passed at all five aspects; four-player FFA loading and menu-only boot/transition/main menu passed at32:9. Options, Beatbox and High Scores were captured at32:9 after correcting a route that wrongly assumed Main Menu preserves selection on return. A disposable new-ID fixture subsequently reached `story/charDec` with nested/outer full-save restoration. Earlier existing-name and unreachable-Crib probes remain failed/unreached, not captures. Late Story girlfriend/partner, Crib Options, Tips/legacy loader and three-player/team consumers were not covered by that matrix. Missing-content/kernel-omission diagnostics remained despite no crash/watchdog/unresolved-call faults in completed runs.

Normal Battle uses `battle/oldMLoad.swf` with dynamic `screens/match/0_bg/paper_1v1.big`; `battle/matchLoa` is a different preview. `paper_ffa3`, `paper_ffa4` and `paper_team` complete eight actual-loader tile IDs. Each pair is512×512left plus128×512right,480visible rows with padding preserved. Original centers and independent portraits/banner/text/vignette/loading layers stay intact; all eight registered anchors correlated1.0 against retained baseline canvases. Fractional authored UV spans exposed exporter float offsets; integer manifest shifts now round to the nearest source texel (≤0.5texel), leaving integer-width layers unchanged.

An app shutdown interrupted Options before save restoration; later physical copies of4.9GB save/cache images exhausted disk, causing a menu-only run's restore/capture writes to fail. Neither is a pass or game-hang diagnosis. Retained pre-run trees were restored and independently byte-compared; failed guard payloads remained available. `scripts/harness.py` gained APFS clonefile copies with independent full-copy fallback, tested for edit independence, fallback and failed-launch restoration. Recovery receipts: `logs/menu-ui-v1-rc/interruption-recovery.json`, `diskfull-recovery.json`. Source tests:534passes/32skips/1slow deselection after two sandbox-only Setup failures passed with normal cache/process access;64focused harness/menu/export checks passed.


## Earlier manager validation and publication receipts

The2026-10-21 manager foundation (`DefJamFFNY-mods` source `adf8099`) passed seven tests including18invalid-metadata cases and Actions39612826714; its empty unsigned preview did not establish trusted publication. Protected signing implementation `3597c0e` passed13tests/22subtests and Actions39623189224. Exact-byte signature verification precedes parsing; rollback/expiry rules fail closed for installs while cached installed state and offline Play remain usable. Publishing isolates archive/malware checks from signing secrets and requires exact reviewed source, append-only history and every mirror's size/hash. Unexpected missing history fails closed outside explicit initial bootstrap.

The earlier helper validation covered tampered/untrusted/expired/replayed metadata, SemVer/revocation/capabilities/dependencies; oversized receipts before switch; ignored/invalid Range; traversal/links/device paths/case collisions/unsupported files; missing images/expansion bounds and stretch-only anchors; cancellation, local-name collisions/edits, modified rollback files, redirected roots including Windows junctions and interruption before/after switch. Native fixtures covered paths with spaces, Python/C++ locks and incomplete-journal launch refusal. Review corrected unowned rollback cleanup, DDS checks, directory durability, repeated failed recovery, saved-skip handling, missing-history reset and signer dependency verification. Unknown local edits remain preserved. All nine verifier wheels matched platform pins; retained macOS CPython3.13.16 imported its pinned crypto wheels, verified an ephemeral Ed25519 signature and validated HTTPS. This is distinct from platform native acceptance.

Final earlier units were569passes/32skips/1slow deselection (567plus two sandbox cache/process-denial failures before the successful rerun);37focused bridge/helper checks passed. Three actual native synthetic tours, repeated after the final helper change, passed update/order retention,820×560 at250%text and interrupted recovery retaining skip=true, with byte-identical save restoration. Receipts: `logs/mod-manager-native/launcher-receipts.json`, `logs/mod-manager-tour-20261021-112754/launcher-receipts.json`. The real RC2 importer validation (`logs/mod-manager-native/real-menu-validation.json`) was format validation, not hosted installation or visual approval.

The earlier full regression passed15/18 in44minutes (`logs/regress-20261021-112705.txt`): three goldens, overlay/unlock/picture, fight/FFA/Terrordome, combat27/27, versus26/26, replay80records/1800steps, Crib and soak3/3. Failed rows were launcher USB callback `0x80003344` (caller `0x0027EC6E`), intro duplicate profile (`USR_NAME_EXISTS_XBOX`, line22306) and Gym routing to `story/clothing.swf` rather than Athletics. Presents continued at median120/2s in the fixture failures. Fresh-save launcher/intro repeated2/2 (`logs/regress-20261021-113118.txt`); Gym remained uncalibrated. These failures differ from the later integrated versus stream callback and remain historical failures.

The2026-10-24 author-only catalogue correction `c8940e2` changed `Vlad` to `vyanhursky` without archive/hash changes. Protected publication40100204475 succeeded; the real client accepted signed live revision2 with unchanged trust pins. Metadata validation passed13applicable tests/22subtests; WinError1314 prevented the Windows symlink fixture, which remained for CI. The owner approved the revised native Mods/priority layout and removal of its mod-specific recommendation. UI changes had49focused passes and606fast passes/26skips/1slow deselection; later merge-focused checks74passed. Automated visual interaction was blocked by computer-use kernel initialization, so those tests did not establish large-text or cross-platform visual acceptance. The later owner screenshot review is separate evidence. Historical issue95 and deferred1.1artwork claims must be interpreted through their later fixes/current1.4.0 coverage, not as open release gates.


## Windows packaged-helper and native acceptance (2026-10-23)

Windows11Pro10.0.26200 / RTX4070TiSUPER driver32.0.16.1088 used MSVC14.29, CMake3.20 and Ninja1.10.2 with native D3D11, toolkit `bd8c6336c268cf15783b480fce6869eb11d74ec0`. The original CI installer above installed offline but exposed two packaged-manager faults: Python `._pth` isolation omitted the script directory, breaking sibling `mod_catalogue` import; progress replacement failed near8MiB with WinError5. The helper now prepends its trusted installed script directory; an unrelated-CWD `python -I` regression and actual bundled-helper `--help` check cover it. `durable_rename` retries only Windows5/32/33 for at most10attempts/0.9s total backoff, preserving permanent errors and atomic replacement.500native polling replacements and a deterministic locked-reader test pass; the actual421.6MB download resumed and installed. cp1252 progress output also gained replacement handling; only missing Windows symlink privilege1314 skips the separated symlink fixture.

Corrected game-free `DefJamSetup-0.10.8-windows-x64.exe` SHA-256 `267f67c1c190da312d38437f91bc05aee6b3c3ce7eb34ea0b3b5dda60fad5e39` used reviewed source `7ed7f663c4ba5f9b1c4eadb03876f033ab3d4adc` (development=false); `863ea98` added the deterministic lock regression only. No C/C++ runtime, renderer, lifter or toolkit change accompanied these fixes. Clean packaged rebuild/embedded-helper/game-free checks and `--ui-smoke` passed;337protected files remained byte-identical (`../logs/fixed-setup-rebuild.log`, `rc-data-after-rebuild.json`).

Actual native flow verified signed revision1 → testing RC2 → R2 install without manual extraction → enable after both Faithful81ddfce230ca components → saved order → offline reopen → offline Play/F1. Runtime loaded19,305entries,45wide anchors/17stretch entries and reported157menu overrides. Unavailable proxy127.0.0.1:9 caused Refresh10061 but retained cache/install/order. Gameplay correctly disabled catalogue mutation controls. All325hash-listed files matched; `files.json` itself is the326th (`../logs/installed-pack-final-integrity.json`). The missing checked legacy `faithful-hd` placeholder emitted an unavailable-manifest warning; both real components remained enabled. This was not a missing managed install or failed replacement.

Windows validation:600units/26skips/1slow deselection;6/6helper/locked-reader checks; texture-pack Release1/1 and settings Debug1/1. Full serial regression initially12/13applicable (four SDL/Vulkan-only skips), then loading-golden passed after supplying deliberately excluded development `tests/golden` to the disposable installed source:13/13applicable across original run plus corrected retry, not an all-green first aggregate. C/C++/toolkit sources matched across both. Fight/FFA/Terrordome ran120/150/242s (median120presents/2s; minima114/102/103), combat27/27, versus26/26, replay80records/1800steps, intro/Crib/LearnMoves+movie and soak3/3passed. Receipts: `../logs/full-windows-regression.log`, `m2-corrected-rerun.log`, `../units-complete.log`, `../locked-reader-tests.log`.

Nine serial native menu rows restored copied saves without crash/watchdog/unresolved-call faults:4:3,16:10,16:9 and experimental21:9/32:9 title/pan/settled menu, plus32:9Options/Beatbox/one-on-one loading/fight entrance (`../logs/wide-matrix/receipts.json`). Corrected Mode routing covered all eight states with changing live spotlight/icon/shutter; the earlier route covered only six because it hit an unselectable cell. Receipts: `../logs/transition-controls.json`, `win-mode-all-eight-20261023-193126`, run `20261023-193254`. The inherited transition hole still reproduced then; its negative controls/draw evidence is now consolidated in [the later geometry repair](menu-ui-v1.1-2026-10-26.md). Ordinary23-file settings/save inventory remained byte-identical (`../logs/original-user-data-verification.json`). These Windows observations neither erase Mac15/18 failures nor imply untested platform/late-Story/three-player coverage.


## Retained run-log citations from consolidated reports

These citations preserve log-retention evidence from the removed dated reports; they do not change their original verdicts.

- `menu-fire-escape-transition-mapping-2026-10-20.md`: `logs/run-20261020-100730.log.err`.
- `menu-transition-native-black-rectangle-2026-10-20.md`: `logs/run-20261020-214604.log.err`, `logs/run-20261020-215923.log.err`, `run-20261020-220927.log.err`.
- `mod-manager-validation-2026-10-21.md`: `run-20261021-104553.log.err`, `run-20261021-111344.log.err`, `run-20261021-112016.log.err`.

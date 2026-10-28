# Integrated hosted-menu launcher RC — 2026-10-23

## Scope

Vlad authorizes combining game PR17 (design), PR62 (menu source/registrations and
wide runtime), PR73 (long settings lists, already included in PR62 toolkit) and
PR82 (manager/UI) into one RC candidate on current main. Deliver a game-free
Windows setup for testing; no manual texture download/extraction. The menu pack
remains independent from optional local Faithful HD. The manager retains the verified421.6MB ZIP cache as well as
the423.4MB installed files (about845MB before future rollback copies).
No game executable or art
bytes enter the game/catalogue source repositories.

Current main dcd0df0 uses toolkit8589f88, retaining the Windows startup layout
checks. The isolated toolkit merge includes published dd63e17 wide/settings work;
no lifter change/re-lift. Candidate CMake version0.10.8 distinguishes the setup
from current released v0.10.7; this is an untagged RC, not a stable release.

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

## Windows testing procedure

Use the Windows game-free setup artifact from this candidate's successful CI,
verify its checksum/provenance, and choose Bundled release (offline) during setup.
Use separate installation/data directories for this RC. Setup compiles the user's
own supported dump; selecting latest public source would instead choose a build
without this candidate. Faithful HD generation is optional and separate.

Open Mods, Refresh catalogue, enable Show testing versions, find HD Widescreen
Menus1.0.0-rc2, then Download and install. Enable HD textures and the installed
menu pack. In Load Order place it after all enabled Faithful components; later
packs win. No archive download/extraction or hand editing pack paths is needed.
Restart and verify enablement/order persist. Check4:3 and available wide aspects,
Press Start -> fire-escape pan -> Main Menu, Options/Jukebox/Battle setup and
pre-fight loading. Compare with/without Faithful if available.

The inherited first-left-tile black rectangle during the transition and remaining
Mode/branding/late-Story/three-player acceptance gates are documented in the prior
menu release report. They remain visible limitations, not approved final results.
No ordinary save/settings or restored artwork is modified by candidate tests.

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
- Reusable native tours: scripts/test-mod-launcher.py (synthetic update/order,
  large text, recovery and testing-channel install) and
  scripts/test-hosted-menu-launcher.py (actual trusted catalogue/R2 installation,
  enablement/order and reopen with an unavailable network proxy).

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
Republish with owner approval before expiry to keep new downloads available;
installed packs and offline play remain usable.

Game PRs17/62/73/82 are closed as superseded by the single draft PR91. No game
PR was merged, stable release tagged, or texture asset committed.

## Real hosted launcher acceptance — passed

From a new disposable data root, scripts/test-hosted-menu-launcher.py exercised
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

The integrated candidate is ready for the owner's Windows acceptance using the
verified artifact linked above. No manual Cloudflare action or mod archive
extraction is needed. PR91 remains a draft until platform/playtest acceptance;
this does not turn the retained full-suite 15/18 result or inherited visual gates
into passes. The source implementation and tested installer remain 0279950;
subsequent commits only record documentation/evidence.

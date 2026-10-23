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
  filesystem headroom after the prior no-space failure; successful full packaging
  CI remains required to confirm resolution.
- Catalogue source includes RC2 with exact immutable R2 URL/size/hash, capabilities,
  expansion bounds and optional Faithful order recommendations. Runtime minimum
  0.10.8 plus wide-v1 capability prevents offering it on existing released builds.
- Explicit protected first-publication bootstrap accepts reviewed initial entries,
  applies the same download/archive/malware checks as later publications, and
  refuses an already-published signed history. Unexpected404 without bootstrap
  still fails closed. This avoids two owner signing approvals merely to publish
  an empty catalogue and then its first mod. Owner approval is never bypassed.

## Validation in progress

- Native combined build succeeds with DEFJAM_MOD_WIDE=1.
- Fast units589pass/32skip/1slow deselected with normal setup-cache/process access.
  The first sandboxed run had two permission-only failures, rerun successfully.
- Combined toolkit texture fixtures3/3 and settings1/1 pass.
- Real RC2 validates through catalogue archive parser:157 texture mappings and
  textures-v1/wide-v1 capabilities. Full public bytes/hash/range verification is
  retained in the R2 upload report.
- Full serial native regression, synthetic native manager/testing-channel tours,
  protected catalogue publication, actual hosted native launcher installation
  and CI/Windows artifact receipts will be appended when complete. Do not treat
  these pending gates as passed.

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
  Download verification, archive validation and malware scan pass; signing waits
  for the owner-selected catalogue-release approval. The agent does not approve
  its own trusted publication.
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
channel installation of2.0.0-rc1. All exit0 and restore saves byte-identical.
Receipts/captures: logs/integrated-rc-native-manager/launcher-receipts.json;
testing and large-text captures inspected. This does not substitute for actual
signed hosted RC2 installation, which still waits for catalogue approval.

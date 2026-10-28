# Windows integrated menu/mods RC verification — 2026-10-23

## Result

The original PR91 Windows setup installs offline, but its packaged mod manager
has two Windows failures. Both are fixed locally and verified in a rebuilt
game-free installer: isolated Python sibling imports and transient progress-file
replacement failures. The signed catalogue, actual RC2 download/install, saved
enablement/order, offline reopening, Play and native overlay work on this machine.

The five-aspect menu matrix and all eight Mode selections render successfully.
The inherited fire-escape black rectangle still reproduces. Consecutive-frame
draw evidence narrows it to an uncovered region of the transition scene, rather
than a required dependency on the modpack or wide-side renderer. It is **not
fixed or accepted**. PR91 remains draft; this report is not stable-release signoff.

## Machine and candidate

- Windows 11 Pro 10.0.26200; RTX 4070 Ti SUPER, driver32.0.16.1088.
- VS2019 Build Tools/MSVC14.29, CMake3.20, Ninja1.10.2. Native D3D11 runtime.
- Isolated work root: `<work root>`; `source/` is a
  public-lineage worktree, branch `codex/pr91-windows-validation`, based on
  PR91 head`66002541fa523e5cd216ce02ce517c6dc42d7d2a`. Toolkit remains
  `bd8c6336c268cf15783b480fce6869eb11d74ec0`, clean and unchanged.
- Original CI40003181247/artifact12521984699 setup:58,819,072bytes,
  SHA-256`8fbc03aee2a762c801c32f8dac299ac2bd92ebfb992992517a64c7bafc3a8212`.
  Its provenance is merge`dd17361144e563bf988a86bd9aefb956d65396c7`.
- Corrected setup: `source/build/setup-assets/DefJamSetup-0.10.8-windows-x64.exe`,
  SHA-256`267f67c1c190da312d38437f91bc05aee6b3c3ce7eb34ea0b3b5dda60fad5e39`.
  Reviewed source`7ed7f663c4ba5f9b1c4eadb03876f033ab3d4adc`, development=false.
  Later commit`863ea98` adds only the deterministic Windows file-lock regression.
- RC installation/data are `app/` and `data/`, separate from the ordinary user
  installation. `app/installed.json` resolves the exact activated build and
  immutable source folder. The final build is source7ed7f66, toolkitbd8c633.

The verification phase performed no source push, PR update, merge, tag, release,
catalogue deployment or artwork publication. All captures, game data, generated code and logs stay
outside tracked source. No game executable is a deliverable.

## Fixed Windows failures

| Failure reproduced | Correction and evidence |
|---|---|
| The bundled Python's `._pth` isolation omits the script directory. `setup/mods.py` cannot import sibling `mod_catalogue`; the launcher reports a generic helper failure. | Resolve and prepend this installation's script directory before importing the contract. An isolated `python -I` status regression runs from an unrelated directory. Windows packaging validation now invokes the actual bundled helper's `--help`, alongside its crypto smoke. |
| The real download fails at about8MiB with WinError5 while replacing a progress snapshot. Rapid atomic replacement also reproduces it outside native polling. Windows readers/scanners can briefly deny replacement. | `durable_rename` retries only Windows errors5/32/33, up to10attempts with0.9s total backoff. Permanent errors still propagate; atomic replacement and previous-install protection remain. Native polling stress500replacements passes, as does a deterministic test holding the destination open briefly. The actual421.6MB download resumes and installs. |
| `scripts/progress.py` crashes when the Windows console uses cp1252 and status contains emoji. | Configure stdout/stderr replacement handling; a cp1252 subprocess regression passes. |
| A release validation test assumes permission to create Windows symlinks. | Keep ordinary traversal validation unconditional; separate the symlink test and skip only missing Windows symlink privilege (WinError1314). No OS security setting was changed. |

Implementation commits are`2bbe9df`, `7ed7f66`, and test-only`863ea98`.
There is no C/C++ runtime, lifter, renderer or toolkit modification in these fixes.

## Setup and managed-pack acceptance

1. Original setup: Bundled release/offline, own supported dump, isolated app/data
   folders, no shortcuts; installation exits0 (`../logs/setup.log`).
2. Actual native Mods flow: refresh signed catalogue revision1, show testing
   versions, download/install HD Widescreen Menus1.0.0-rc2 from the live immutable
   R2 URL. No manual texture extraction. Archive421,563,157bytes;
   SHA-256`43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634`.
3. Enable HD and RC2, after both actual Faithful81ddfce230ca components in shared
   Load Order. Native UI reports157menu overrides; runtime loads19,305entries,
   45wide anchors and17stretch registrations. Existing Faithful directories are
   junctions in RC data, not regenerated or modified.
4. Corrected standalone setup passes embedded-helper/game-free checks and native
   `--ui-smoke` (exit0). Clean packaged rebuild of the RC installation exits0
   (`../logs/fixed-setup-rebuild.log`). It uses its bundled reviewed helper;
   no post-install helper patch is needed. All337protected RC files, including
   settings, saves, manager state/cache and pack files, remain byte-identical.
5. Reopen that rebuilt native launcher with HTTP/HTTPS proxies pointing to the
   unavailable127.0.0.1:9 endpoint. RC2 stays enabled and last in Load Order;
   verified cached revision1 and Installed1.0.0-rc2 are visible. Explicit Refresh
   fails with WinError10061 while retaining the cached catalogue/installation.
6. Actual offline Play launches the corrected D3D11 game. F1 opens its native
   overlay; Mods shows both actual Faithful components, RC2 and157overrides.
   Catalogue download/update/removal controls are unavailable during gameplay,
   with the return-to-launcher explanation. Test game closed normally afterward.

The default `faithful-hd` name remains as a checked **Not installed** placeholder
above the two real Faithful components. Automatic approval review rejected
clearing this missing entry, interpreting it as disabling the required pack;
it was retained, with both actual components enabled. The runtime emits an
unavailable-manifest warning for the placeholder and then loads the real packs.
This warning is not a missing managed pack or a failed texture load.

Final integrity:326installed files; all325entries listed by `files.json` match
their size/hash (the326thfile is `files.json` itself). The earlier complete
post-install inventory and337-file rebuild comparison include that manifest.
Receipts: `../logs/installed-pack-hashes.json`,
`../logs/installed-pack-final-integrity.json`,
`../logs/rc-data-after-rebuild.json`.

## Automated and native validation

| Check | Windows result |
|---|---|
| Fast unit suite | **600passed,26skipped,1slow deselected**,83.41s; `../units-complete.log` |
| Native helper/locked-reader checks |6/6; `../locked-reader-tests.log` |
| Toolkit focused native fixtures | texture_pack Release1/1, settings Debug1/1; `../logs/native-*-fixture.log` |
| Full serial gameplay regression | Initial aggregate **12/13passed,4skipped**,37min; loading-golden test retried successfully on the rebuilt installation, giving **13/13applicable checks satisfied across the run and corrected retry** |
| Fight / FFA / Terrordome |120/150/242s gameplay; median120presents per2s, minima114/102/103 |
| Scenario assertions | combat27/27, versus26/26; replay80records/1800steps match golden |
| Story and boot routes | intro to creator, crib, Learn Moves + preview movie,3/3boot soak pass |
| Native UI | actual Home/Mods/order/Play/offline flow and F1overlay inspected separately; four SDL/Vulkan-specific automation rows explicitly skip Windows |

The first loading-golden failure was a test setup error: consumer packaging
intentionally excludes `tests/golden`, but the installed-source harness needed
that development fixture. The test suite was copied into the disposable installed
source, and m2 reran successfully (`../logs/m2-corrected-rerun.log`). The initial
failure and nonzero aggregate exit remain in `../logs/full-windows-regression.log`;
do not call that first invocation an all-green full run. The original-runtime
gameplay and rebuilt-runtime C/C++/toolkit sources are identical.

The Mac15/18 result and its three historical failures remain recorded in the
[Mac integration report](integrated-menu-launcher-rc-2026-10-23.md). Their faults
were not reproduced by the corresponding Windows routes; they are not erased
or reclassified as global fixes. The four skipped SDL/Vulkan rows are not passes.

## Native menu inspection

Serial installed-build matrix receipts: `../logs/wide-matrix/receipts.json`.
All nine rows have no crash/watchdog/unresolved-call fault signatures and restore
their copied saves. Native captures confirm aspect selection and45wide/17stretch
metadata. Inspected title,0.65/1.3s pan, and settled menu at4:3,16:10,16:9,
21:9experimental and32:9experimental. Side scenery fills the requested canvas;
interactive text/paper panels keep their central composition. Inspected32:9
Options, Beatbox and One-on-One pre-fight loading; the later gameplay capture
shows the fighter entrance. Actual sustained fights are covered by the regression.

The all-eight Mode route covers Story, Battle, Unlock Rewards, Extras, Beatbox,
Options, High Scores and Credits. Live spotlight, selected icon and shutter
emblem change correctly; side scenery remains the authored dark composite.
Receipts/captures: `../logs/transition-controls.json`,
`win-mode-all-eight-20261023-193126`, run`20261023-193254`.
An earlier tour hit the unselectable bottom-left cell and covered only six
unique states; its captures are retained, and the corrected route supplies the
remaining coverage. These are rendering observations, not new artwork approval.

## Remaining transition defect

The0.65s first-left-tile black rectangle reproduces in4:3 and32:9. Serial4:3
controls reproduce it with packs disabled, normal managed packs, synchronous
texture loading, depth testing disabled, and forced alpha-blend diagnostic
state. No diagnostic toggle was saved as a user preference or retained as a fix.
Controls have restored saves and no runtime faults:
`../logs/transition-controls.json`, `../logs/transition-state-controls.json`.

The ordinary `PB_BATCH_DUMP=shot` trace follows the capture by a frame, which
is too far apart to identify pixels in this fast pan. Fixed consecutive-flip
dumps and dense captures resolve that ambiguity (`../logs/transition-fixed-flips.json`,
installed run`20261023-193721.log.err`):

- Same-framef1358 and the following0.601s capture: black upper-left region ends
  near y216. The first left scenery tile, guest texture`0x82813880`, starts at
  x≈0,y216.1 and ends y728.2 (line7909). Its right sibling`0x82994080` starts
  atx512 at the same y (7924); the narrow right strip above it ends at216.1
  (7939). The candidate left tile does not cover the black region above it.
- The full-screen passes at7635 and8220 have color mask0 (7642/8227).
  The initial surface copy precedes the0xF3zero clear at7577. No subsequent
  color-writing draw covers the upper-left gap; clear0x1at8257 is depth-only.
- Later frames move the leading tile above the viewport and the rectangle
  disappears. This is evidence of missing scene coverage during the early pan,
  rather than black source pixels in that leading tile or later color overdraw.

The [existing transition mapping](menu-fire-escape-transition-mapping-2026-10-20.md)
also records bitmap9's unusual x512..1024placement and UV1..2 while the first
left tile starts at shape-local y512. The trace does **not** establish whether
the root cause is authored geometry, guest data/transform behavior or an earlier
porting defect, nor where bitmap9 should be placed on the console. A hardcoded
tile move, depth bypass or new painted cover would be an unverified workaround.
No such renderer/artwork change is retained. The next useful comparison is the
retail/reference transition's first512×512left region and bitmap9transform,
followed by a focused geometry fix and full runtime regression if warranted.

Remaining owner acceptance includes this transition, final branding/side-border
judgment and previously deferred late-Story/three-player artwork consumers.
The Windows manager failures are fixed; unconditional release readiness is not
established.

## Data protection and handover

All native game runs were serial. Regression and matrix saves were disposable
copies; ordinary saves were never selected for gameplay. The original23-file
settings/save inventory is byte-identical at completion
(`../logs/original-user-data-before.json`, `original-user-data-after.json`,
`original-user-data-verification.json`). Final test game is closed.

Use `app/DefJamLauncher.exe` for this isolated corrected RC. The ordinary user
installation and existing public-preview/private checkouts remain untouched.
The clean setup above is ready for another local RC install; publishing the
source fixes or replacing the CI artifact requires a subsequent owner request.

### Publication follow-up

Vlad subsequently authorized pushing the verified fixes and report. They are
delivered through the existing PR91 branch `codex/integrated-menu-mods-rc`;
PR91 remains draft and the inherited transition gate remains open. This does
not merge, tag or release the candidate. The already installed playtest build
and launcher path above are unchanged; no reinstall is needed for local playtesting.

# Separate HD widescreen menu release candidate — 2026-10-20

## Result and scope

Vlad's D182/UI authorizes local native tests; D183/UI explicitly requires a separate menu pack
that overrides conflicting Faithful HD textures. The pack is **Def Jam HD Widescreen Menus**,
`menu-ui-hd-wide-v1-rc2`, a sparse schema-1 PNG pack folder and ZIP. Faithful HD remains optional,
independently installed, and earlier in the enabled list. No Faithful library, executable, dump,
build output, developer source or captures are in the archive. Selected original HD scenic
canvases are retained where needed to keep the center and side scenery consistent without
requiring Faithful. Other deferred small-panel/shop candidates are excluded.

- 157 source-shaped 4x PNGs: 149 reviewed menu IDs and eight separately registered actual-loader IDs.
- 164 wide side slabs; 62 `[wide]` entries: 45 anchors and 17 stretched overlays.
- 326 installed files, including the manifest, provenance and file-integrity receipts.
- Download: **421,563,157 bytes** (421.6 MB / 402.03 MiB).
- Installed: **423,392,524 bytes** (423.4 MB / 403.78 MiB).
- ZIP SHA-256: `43bbb22cd14197091143623551f350a0b7ff622f0efccb1da058f491e7157634`.

These counts refer to the menu pack only. Neither the roughly 6.21 GB optional Faithful library
nor private masters, rejected drafts, captured frames or archives contribute to these numbers.
All artwork stays local; only source, tables, hashes and documentation are published.

## Location and local playtest

On this Mac the isolated data root is
`/Users/vlad/Code/defjam-data/playtest-menu-ui-v1-rc1/` (the root retains the first iteration's name).
The immutable pack folder and same-stem ZIP are under its `mods/` directory.
`play.sh all` enables the two optional Faithful components, then the menu pack;
`play.sh menu-only` enables only the menu pack; `play.sh faithful` is the comparison baseline.
The ordinary game data/settings/save root was not changed. Initial process inspection found no
running game; no pre-existing player game was stopped. Harness runs are serial and restore the
complete isolated save tree byte-for-byte.

The runtime requires an **unpacked folder**, not a ZIP path. Extract the ZIP into `<data>/mods/`,
disable superseded `menu-ui-*-test-v1` packs and put the new pack after any Faithful components.
Removing/disabling this one menu entry returns to the prior enabled textures. The current Mac
binary uses published toolkit `dd63e17`; older runtimes lacking side-slab support are incompatible.
No runtime, renderer or toolkit source was changed for this candidate. The harness save-copy helper now uses macOS/APFS copy-on-write cloning when available, with an independent full-copy fallback; full-tree byte verification and failed-backup retention remain unchanged.

## Rebuild and input integrity

`config/menu-ui-release.json` defines the pack scope and component receipt pins;
`scripts/build-menu-ui-release.py` builds from private frozen components and approved masters:

```sh
python scripts/build-menu-ui-release.py \
  --art-root <private-pilot-v1> \
  --base-mods <old-local-playtest>/mods \
  --approved-components <approved-pack-exports> \
  --out <isolated-data>/mods/menu-ui-hd-wide-v1-rc2 \
  --receipt <local-build-receipt.json>
```

Use Python with Pillow and NumPy. Output folders and ZIPs are immutable; use a fresh version for
another candidate. The builder checks frozen component receipts/output hashes,23retained-center input pins,4x canvas sizes,
unique source IDs, safe relative paths, original artwork pins and technical derivative parent pins.
Explicit retained-core component order avoids directory-order precedence. `files.json` hashes every
payload file except itself. The ZIP hash is pinned in the local build receipt; ZIP timestamps mean
rebuilding is not promised to reproduce the same archive bytes. The exact local RC2's outputs were
also independently checked against every component receipt and all 325 payload hashes/sizes.

All 670 approved/conditional art pins remain intact. No new imggen calls were made; the historical
artwork total remains 162. New runtime registration is engineering, recorded separately from owner
art approval in `config/menu-ui-runtime-registrations.json`.

## Actual pre-fight loading screens

Normal Battle uses `battle/oldMLoad.swf` with dynamic
`screens/match/0_bg/paper_1v1.big`, not the separate `battle/matchLoa` preview background.
Three-player, four-player and team use `paper_ffa3`, `paper_ffa4` and `paper_team`: eight tile IDs.
Each has a 512x512 left tile and 128x512 right tile; 480 rows are visible, padding stays unchanged.
Their APT placement/UV metadata is retained locally in `logs/menu-ui-v1-rc/loading-bg-mapping.json`.

The approved wide loading wall has the same brick structure but a brighter baked response than
the actual loader's wall. Four registered derivatives reuse only its approved exterior, fitting
RGB response on the outer 12 native columns. Slopes are approximately [.7221,.7272,.7301], offsets
[2.4829,2.9446,.5615]; every actual original center is retained exactly. Portraits, mode banner,
fighter lettering, venue label, vignette and loading animation remain independent live layers.
All eight anchors match at normalized correlation1.0 against the retained baseline canvases.
The original legacy preview registration remains available if the game uses that resource elsewhere.

Fractional authored UV widths also exposed a real exporter defect: generated slab shifts were
floating-point values, while the runtime manifest accepts integer source-texel shifts. Shifts are
now rounded to the nearest texel (at most half a source texel); integer-width layers are unchanged.
The synthetic regression covers both cases.

## Native evidence and remaining acceptance

Actual native screenshots and per-run receipts remain local in `logs/menu-ui-v1-rc/` and
`logs/shots/menu-rc*/`; the browser review at `http://127.0.0.1:51738/native-playtest.html` shows them.
The 149 RC1 native images and 136 RC1 side slabs are byte-identical in RC2; the five-aspect pan
captures therefore cover the same transition/main-menu assets. RC2 adds eight loader IDs/28 slabs.

The 32:9 native loading card now has full-width brick scenery with portraits and lettering intact.
One-on-one loading has now completed at all five aspect ratios, with save guards and runtime fault checks passing; four-player FFA loading at32:9 and the menu-only32:9 boot/transition/main-menu run also pass, with restored save guards.
The five-aspect transition captures show wide scenery, but a pure-black first-left-tile rectangle
appears in several captures. A separate **Faithful-only 16:9 control reproduces it**; a 4:3 capture
also has it, where side replay is bypassed. It predates the new art/slabs and is a release issue,
not an accepted native result. See the [focused audit](menu-transition-native-black-rectangle-2026-10-20.md).

No crashes, watchdogs or unresolved runtime-call faults were found in completed candidate runs;
all completed save guards passed. Existing missing-content/kernel omission diagnostics remain in
the logs and must not be described as a perfectly clean triage. Native title/emblem registration,
Mode's specifically conditional lighting and the transition still need owner visual signoff.
Late Story girlfriend/partner screens require appropriate saves; no such fixture is currently in
the library. Crib Options and Tips/legacy loader resources also lack successful
new native captures in this pass; they must not be counted as covered by the loading-card matrix. Approved artwork and packaged geometry are not substitutes for those native captures.
This remains a **release candidate**, not a published or fully accepted modpack.

## Hosting

The [PR17 hosting review](menu-modpack-hosting-pr17-2026-10-20.md) separates a small signed catalogue
on GitHub Pages from externally hosted versioned artwork archives and a later download/install
helper. The menu ZIP fits one download; no splitting or Faithful bundling is needed. The approved
masters cannot be reconstructed exactly from public source recipes alone. No account, bucket,
public artwork upload, tag or release was created in this task.

## Interrupted-run and disk-space recovery

The app shutdown interrupted the Options tour before guard restoration. Its original isolated-save payload was retained and restored byte-identically; the interrupted state was preserved separately. Later, repeated physical copies of4.9GB save/cache images exhausted this disk. The menu-only run ended without a game crash, but its save restore/capture write failed: it is **not a passed test**. The complete isolated tree was recovered again from the retained pre-interruption payload, independently compared byte-for-byte, and the incomplete restore removed. Both failed guard payloads are retained. Receipts: `interruption-recovery.json` and `diskfull-recovery.json` under the session log folder. The ordinary user save root was never changed.

To prevent recurrence, `scripts/harness.py` uses macOS `clonefile` for independent copy-on-write save copies where supported, falling back to `shutil.copy2` elsewhere. Synthetic tests cover independent edits, fallback, complete-tree restoration, launch failure and failed-backup retention. Cleanup reclaimed about1.06GB by removing163BMPs only after exact pixel comparison with retained lossless PNGs, plus337MB of temporary partial slab exports. No approved masters or finished release artifacts were deleted. Capture receipts retain original BMP names; corresponding PNGs are the retained evidence. Remaining tests are retried after recovery.

## Source test results

The fast unit suite completed with532passes,32skips and1slow deselection inside the sandbox. Two unrelated setup tests were blocked by its cache-lock/process-list permissions; both passed when rerun with the required host access. Total covered:534passing unit tests,32skipped,1slow deselected. All64focused harness/menu/export tests passed, including copy independence, normal-copy fallback and failed-launch save restoration. Native review JavaScript syntax also checks.

The later Story probes did not establish coverage: the intro route reached an existing-name prompt and was stopped, and a subsequent Crib probe never reached its title/screen anchor within its limit. Both guards restored. These are failed/unreached route probes, not accepted Story captures; they remain separate from the successful loading and menu-only runs. A suitable calibrated fixture/input path is still needed.

The corrected main-menu tour reaches Options, Jukebox/Beatbox and High Scores at32:9, captures all three and restores the save with no runtime fault checks. The main menu resets its selection to Story on return; the earlier attempt incorrectly assumed the selection was retained and entered Battle. Receipt: `menu-rc2-tour3-32x9-all-receipt.json`. This is a navigation calibration correction, not a texture change.

The calibrated Story new-ID route subsequently passes using a disposable fixture: inside an outer full save guard, temporarily remove the existing AAA test profile and run the original harness keyboard schedule unchanged. This avoids inventing a new keyboard path. The character-decision (`story/charDec`) background is captured at32:9 with live text/portraits; both nested and outer saves restore byte-identically and fault checks are empty. Receipt: `menu-rc2-intro-clean-32x9-all-receipt.json`. The earlier failed probes remain retained and do not count as coverage.

## Private preservation checkpoint

The complete frozen archive is `<data>/scratch/menu-ui-native-release-rc2-20261020/`:11716files,5552616413logicalbytes, manifestSHA256 `f93b80f023bee2b5d1e43348aa4a67327244017ad0e96f9e08d38e01054da3e6`. All670approved/conditional pins verify inside it. It holds the entire working art/history root, selected source metadata, component exports, native receipts/logs, configuration and the separate ZIP. This backup size is not the modpack download size.

The final native-review layout/proof overlay is `<data>/scratch/menu-ui-native-release-rc2-review-final-20261020/`:7files/282521bytes, manifestSHA256 `5656a5e13bbf18703f0b0733e6de142bd1f9e585176e2e00d12dec91d2386419`. Apply its session files only to a working copy of the base; both snapshots remain immutable. Public source for registration/QA/review is frozen in the modding-tools repository's `research/menu-ui-native-release-20261020/`.

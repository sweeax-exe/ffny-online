# v0.6.0 candidate validation

Owner authorization: 2026-10-08, prepare v0.6.0. The accepted artwork is faithful
Lanczos 4x; widescreen, font-outline integration, HD movie playback and another
customization/fighter artwork pass remain follow-ups.

## Source and data boundaries

- Parent candidate starts from public main/v0.5.1 `1eb4c55048afb84f078828ac72bafbb825eb8be4`.
- Toolkit candidate starts from the published Windows/native-port pin
  `8f08c4f99b7b6e8401033fe43a6be09e5679f5c4` on the separate HD branch.
- Own dump, saves, generated C, binaries, model weights, PNGs, captures and logs
  remain local/ignored. The release installer generates the player's artwork
  locally; it contains no texture pack or compiled game.
- All game runs use the HD lane's disposable data/save root and existing complete
  save guards. Original user saves and unrelated checkouts remain untouched.

## Completed focused validation

- Fresh local analysis and lift on the latest public base: 17,896 identified
  functions, 17,882 ABI entries, 20 generated C files. Release build succeeds.
- Project unit suite: **246 passed**, including HD setup final activation success,
  cancellation, shortcut failure and receipt failure; prior settings and receipts
  remain byte-identical on failure. `logs/hd-v060-unit.txt`.
- Release native texture-pack fixture: **1/1 passed**, real WIC/CNG and synthetic
  images with mock D3D8 resources. Covers content/palette/stride IDs, unreadable
  source memory, PNG alpha/channel mips, odd extents, row padding, DDS validation,
  allocation retry, precedence and four-stage-safe eviction/resource lifetime.
  `logs/hd-v060-fixture.txt`.
- Actual embedded Python negative control reproduces the missing sibling-script
  import; adding `../source/scripts` fixes `build-hd-pack.py --help`.
  Final payload checks require all HD entry-point sources and execute this probe.
  `logs/hd-v060-embedded-path.txt`.
- Earlier whole-corpus proof generated **18,971 images / 18,984 manifest keys**,
  13 independently correlated runtime aliases, one unsupported static encoding.
  All output PNG hashes match the owner-accepted gallery; generation 238.017 s.
  This proof precedes cache-publication fixes, which preserve pixel processing.

Read-only installer and runtime reviews were made (unpublished development notes). Their findings are reviewed evidence;
source inspection alone does not replace real installation/game gates.

## Game regressions (2026-10-09)

Full `scripts/regress.py`, 14 checks each, on the candidate with the HD runtime built in:
**Release 14 of 14 passed in 55 min**
(`logs/regress-20261009-001522.txt`, 246 unit tests) and **Debug 14 of 14 passed
in 57 min** (`logs/regress-20261009-012256.txt`, 250 unit tests). Both cover the
three still-frame goldens, fight, free-for-all and Terrordome, combat (27 of 27
assertions), versus (26 of 26), the simulation replay hash (80 records over 1,800
steps), Story intro, crib, Learn Moves and a three-boot soak. Source-tree policy:
384 tracked files, 0 failures.

## Release procedure and remaining gates

The toolkit change is one commit (`4bff257`) on the fork branch `defjam/hd-textures`,
published before the parent pin. Source tag, version, notes, main ancestry and
hygiene are re-checked by `scripts/check-release.py`; hosted CI and the Setup asset
inventory run on the tag before the release draft is created.

## Packaged installer run (2026-10-09)

The production `DefJamSetup-0.6.0-windows-x64.exe` (built by `scripts/build-setup.ps1`
from commit `30b0a88`, asset check passed) was run silently with `--hd-textures` into
fresh install and data folders, from the owner's own dump: exit 0 in 9 min 33 s.
Stages: analysis (17,882 ABI entries), lift (17,891 of 17,896 functions, 0 failed,
20 C files), Release build, HD generation (18,971 images, 238.3 s), verified assembly
into two packs (14,054 and 4,917 images, 5.9 GB), activation, receipt.
`settings.ini` ended with `[textures] enabled=1` and both packs named.
The installed `defjam_recomp.exe`, started with `--no-launcher` against that data
folder, loaded the manifest (18,984 entries) and replaced textures at run time
(multi-mip loads of 10 to 12 levels) with no `[TEXPACK]` errors, for 120 s until
stopped. Logs: `logs/hd-v060-installer-run-setup.log`, `logs/hd-v060-installer-run-game.err`.

Hosted CI on pull request 5: 23 of 23 checks passed (Windows runtime and fixtures,
Setup build, macOS and Linux runtime).

Not run: the update, repair and cancel Setup scenarios against a real dump (these
have unit coverage), the unchecked-HD packaged run, and a cold/warm full-pack
performance sweep.


## Prototype controls and artwork acceptance (2026-10-08)

The earlier prototype is historical evidence, separate from the rebuilt v0.6.0 acceptance above. Its bounded census found 24,267 image entries, 24,266 supported aliases / 18,971 final unique images, one unsupported SHPX `0x65`, 24 fonts (10 TTF / 14 XFN), and 94 movies. Seven large top-level audio VIVs exceeded the 128 MiB traversal budget; the census does not prove they contain no images. Read-only XML correlation distinguished V2IP named-fighter atlases, V2CP customization, icrowd/cincrowd/V2CM crowds, and V2BG venues/Story/shop interiors; the shared `cp_a` atlas was not proved to be a face. Screen-container provenance overrode misleading embedded names.

An original Release control passed 5/5 quick checks (173 units, three goldens, fight median/minimum 120/107 presents per two seconds; `logs/regress-20261008-144436.txt`). Lossless capture preserved the M2 image and observed 13 assets. A marker altered only the expected loading-image area at frames 40/70; frames 150/220 were unchanged because those assets had left the screen. The coarse M2 golden also passed the marker run: explicit replacement-load logs and pixel differences, rather than that golden alone, establish replacement. The mock-resource fixture covered real WIC/SHA, palette/content/stride changes, malformed input, PNG/DDS mips, precedence and protected resource lifetime (`logs/hd-native-fixture.txt`).

The 40-image review covered six fighters, six customization assets, eight venues, four props, two logos, four UI, four HUD, four effects and two crowds. Original plus three methods at 2x/4x produced 240 candidates; hashes, dimensions, alpha and viewer controls were checked. Neural methods smoothed intentional wear or changed markings; the subsequent SwinIR audition and full-corpus choice are recorded in their retained reports. D90 selected Lanczos for the wider static corpus; D91 accepted the complete static gallery for the first HD release, while leaving customization and possibly fighter refinement for later. This was artwork acceptance, not runtime, coverage or performance certification. Private prompts, reviews and approvals remain in the authoring workspace.

The in-game pilot installed only 26 runtime IDs: Blaze, 22 opaque Foundation entries and three loading assets, correlated from 320 observed IDs. Five nonopaque venue candidates were excluded; Blaze's alpha range 1..255 did not establish its shader meaning. Lanczos 4x processed RGB/alpha separately, retained atlas/aspect, used eight pixels of clamped edge padding, UI RGB bleed and no world RGB bleed. Wrap axes, masks and seams still required material review. Native captures positively showed fighter/world replacements at 1280x960 and 2560x1920; same-route/seed poses and cameras differed, so these were appearance comparisons, not synchronized pixel tests. Native capture kept gamma and did not change the default 640x480 goldens.

Preserve the failed gates: the packs-off prototype full Release run was **11/14** (`logs/regress-20261008-155040.txt`): versus failed `audio.dry_queue` (602 dry, no dropped buffers), replay produced 40 rather than 80 records, and intro logged one indirect call to non-code. Installer extraction and another game instance overlapped that run; causation was not established. After pilot changes the quick run was **4/5** (`logs/regress-20261008-202746.txt`): 185 units and three goldens passed, but the packs-off fight median 43 presents per two seconds failed the unchanged floor of 110. Both pack-on and pack-off diagnostics could run near 21 fps. Moving permission checks out of cached binds removed an extra observed collapse, but render scale and host conditions also changed; this was not a controlled speedup. Captures/profiling and a 5% GPU spot check do not diagnose its cause. No threshold or golden was changed. The later rebuilt Release/Debug 14/14 results above supersede these candidates for acceptance without erasing their failures.

The Setup proof before the packaged run passed 211 units / 31 skips, embedded-Python imports and a 1/1 Release texture fixture. Its first Release fixture failed because NDEBUG removed setup calls inside assertions; keeping fixture assertions enabled corrected the test without changing renderer behavior. Full generation took 238.017 s, matching all accepted PNG hashes; later atomic cache/INI fixes were covered synthetically and did not alter image pixels. A UI probe using an older game-free payload established UI behavior only. Real unchecked/update/repair/cancel scenarios remained unmeasured at v0.6.0 acceptance, as listed above; synthetic coverage is not a packaged-install result.

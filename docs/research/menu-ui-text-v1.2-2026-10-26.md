# Menu text 1.2 candidate — 2026-10-26

The owner requested all menu text textures for the next 1.2.0 modpack using the
previously selected **Stronger edge clarity** treatment. Local branch
`codex/menu-text-1-2-0` starts from the completed 1.1 publication records, with
no new runtime/toolkit changes. This is an authoring/export candidate, not a
published catalogue update. Any future PR remains assigned to v0.12.2 as requested.

## Inventory and treatment

The private visual census covers all 1,834 core menu/support source IDs and
295 external fighter-name/loading-venue/loading-match resources: 2,129 distinct
static source IDs. Complete contact sheets were inspected; selection is not
inferred from texture size alone. Separate samples confirm that the other
portrait/loading-fighter families contain portraits rather than name banners.
Paper/scenery, photographs, effects, symbol-only graphics and decorative text
inside background art remain outside this text pass.

The final overlay has **1,140 texture overrides**:

| Group | Overrides |
|---|---:|
| Menu labels, descriptions, tiled companions and textual controls |574|
| Fighter-name banners, including 2/3/4-fighter layouts |214|
| Loading venue/match-type wordmarks and companion tiles |81|
| Observed native headings/labels and one shared bitmap glyph page |271|

Static text comprises 869 entries; runtime-captured text adds 271. The census
includes English/French resources and unused/demo variants where they are
text assets; it does not assert native visibility of every historical resource.
All 214 fighter-name layout variants are present, independently of which fighters
the native selection tour happened to display.

Apply the exact existing recipe: 4x Lanczos RGBA; detect alpha edges where a
9-pixel maximum-minus-minimum is at least 192; narrow the alpha ramp with slope 1.8
about 127.5. RGB, the >=128 alpha silhouette, and alpha outside the detected
strong edges remain exact relative to each recorded 4x baseline. Textures without
an eligible edge retain the 4x baseline. No font redraw, spelling change or new
image generation is used. Local OCR labels are navigation aids, not artwork.

The six earlier Stronger selections reproduce byte-for-byte. Ten other text-bearing
entries overlap the published 1.1 restoration: use the existing 4x artwork as
the baseline, preserving its RGB and silhouette rather than replacing it with a
fresh enlargement of the original. An intermediate classification accidentally
included six paper/scenery/shadow fragments; they were removed before RC1.
The glyph-page export preserves its layout and glyph spacing; it is an observed
source-ID replacement, not a general runtime font engine change.

`config/menu-ui-text-v1.2-candidate.json` pins all 1,140 input/output hashes,
dimensions, baseline kind and changed-alpha counts. Original1.0/1.1 assets,
ledgers and public catalogue records remain unchanged.

## Coverage boundary

A source-ID texture pack cannot enumerate arbitrary player names, changing
counters/stat values, every font-cache state or every fade/palette permutation.
Private player-name rasters and matching contour/fade variants are excluded;
unclassified runtime rasters and mutable numeric-only values are held. Runtime
P8 also includes coloured scenery/posters, so those are not automatically treated
as text. The shared glyph page is explicitly identified by visual inspection.
Do not claim universal live-font coverage from this bitmap export. Remaining
font-cache integration, if requested, requires separate investigation.

## Standalone candidate and installer

`menu-ui-hd-wide-v1.2.0-rc1.zip` has 643,033,075 bytes compressed,
646,222,148 bytes unpacked, 1,528 files, 1,304 native texture mappings and the
same 80 wide/stretch mappings as1.1. Internal root:
`menu-ui-hd-wide-v1-2-0-rc1`.

SHA-256: `d8249d6e184f53eae5d62bac508097e6d56125b76cdb2ccf9ceae61a5264d2d4`.

The pack integrates the published 1.1 artwork with the text overlay. All 386 base
files outside the replaced text and metadata remain byte-identical, including
all wide artwork/slabs; the six old text pins also remain exact. Faithful is
optional and unbundled. A private first copy attempt lacked a new-text directory;
it is retained as an incomplete intermediate and was not packaged or tested as RC1.

An independent reread of all 1,140 installed PNGs checks input/output hashes,
dimensions, RGB, the >=128 silhouette and nonedge alpha against the recorded
baselines (`independent-integrity-result.json`). The exact ZIP passes CRC and the released 0.12.1 helper's hash/count/path/root and
pack validation, plus the catalogue's strict PNG/manifest/wide-anchor validator.
The helper and catalogue contract files are byte-identical to tag v0.12.1. A
post-validation reporting lookup used the wrong return-key name initially;
corrected reporting verifies 1,304 textures/80 wide entries. This was not an archive
validation failure. Local evidence: `logs/menu-ui-v1.2/candidate-result.json` and
`installer-validation-result.json`. Native final checks load the ZIP-extracted
folder, with no preview overlay beside it.

No game reinstall is required for these existing `textures-v1` / `wide-v1`
capabilities. The separate fire-escape geometry repair remains game PR #131,
milestone v0.12.2; a texture-only update cannot add that code to game v0.12.1.

## Native checks

All runs are serial on macOS with disposable data roots and complete-save-root
restoration verified byte-identical. No measured crashes/watchdogs/unresolved
calls, debug-layer errors or truncated batches. Presents are per 2 seconds.

| Log | Scope | Result |
|---|---|---|
| `run-20261026-161411.log.err` | Initial front tour | See exact run identity in private receipt; not final-RC acceptance. |
| `run-20261026-162148.log.err` | Initial text overlay: Battle/fighter/directional map at 1440p |45 intervals, median120/min95.|
| `run-20261026-162452.log.err` | Corrected known-Story fixture: crib and gym |46 intervals, median120/min98.|
| `run-20261026-162650.log.err` | Four-fighter selection and Choose Teams |47 intervals, median121/min99.|
| `run-20261026-163747.log.err` | Corrected Extras/Credits tour, baseline pack |28 intervals, median120/min99.|
| `run-20261026-164138.log.err` | V3 overlay: clothing, gym, barber, tattoo, jeweller |64 intervals, median120/min99; five captures.|
| `run-20261026-165157.log.err` | Final ZIP-extracted pack: Main Menu/Options/Beatbox/High Scores at 2560x1440 |37 intervals, median120/min99; new Music/Announcer/SFX/Difficulty texture uploads confirmed.|
| `run-20261026-165406.log.err` | Final ZIP-extracted pack: fighter selection and moving venue map at 32:9/1440p |43 intervals, median120/min70; no measured runtime faults; saves identical.|

The first Story schedule selected a new-Story profile and reached charDec rather
than the gym (`161553`). The first Extras return selected Options rather than
Credits (`162944`). The first shop cycle skipped later input/capture anchors
because their order differed from actual screen visits (`163306`); corrected
chronological anchors and authored shop coordinates reach all five shops. These
are incomplete probe routes, not passed scope. Preserve their evidence.

Triage retains the known optional-file/kernel omissions. Bounded inspection of
the first unclassified return confirms `NtFsControlFile(0x90020)`. Its occasional
line-count hang heuristic is disproved by advancing final `[TRANS]` frame records
in the bounded tail. This is not a claim of zero diagnostic messages. Native
checks here are macOS evidence; there is no new game-code/full-regression claim.

## Review and hand-off

Read-only gallery: `http://127.0.0.1:51740/review.html`. It provides all 1,140
comparisons, category/search/paging, cropped Original/Baseline/Stronger views,
Actual 1440p scale, backdrop choices, and native captures. The existing 51739
1.1 review and 8768 authoring workspace are unchanged. The 1.2 gallery is a separate
reader with no review-state migration or automatic per-image approval.

Private recipes, source census, OCR labels, export receipts, revision 1–4 packs,
installer extraction and native receipts live in `logs/menu-ui-v1.2/`. Local
candidate ZIP is under its `release/` subdirectory. The previous invalid/held
intermediates and 1.1 publication remain untouched. Feature branch does not edit
PROGRESS.md. A private candidate checkpoint is recorded in the local hand-off; preserve it before archiving this worktree.

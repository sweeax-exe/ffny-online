# Menu wall/map joins — 2026-10-26 follow-up

Feature branch `codex/hd-widescreen-menu-v1.1`, still based on release PR #123
(`53e5d8b`), toolkit `7a0f2f6`; no runtime changes in this follow-up.
This supersedes the previous report's map-v3 join failure and Team-v1 selection
for the current review. Prior artwork and review pages remain intact.

The owner asked to keep repairing the map joins and repair Choose Teams using
the Battle wall as reference. Six Stronger text selections remain unchanged.
The owner subsequently approved the revised backgrounds: “These revised
backgrounds look good to me. I approve them”. Current Battle-v4 / Team-v2 /
map-v6 masters and six Stronger pilots are pinned in the separate additive
`config/menu-ui-v1.1-approved-artwork.json` ledger. Native runs remain evidence,
not publication or full platform acceptance.

## Choose Teams v2

- Master `choose-teams-wide-v2.png`, 5121x1440, SHA-256
  `37ba3e0631b1610a71fef5493e0fac01bf5209f84a7e35553d34f8a8739e48ac`.
- Uses the actual left/right exterior regions of Battle wall v4 (SHA-256
  `125bc7366e529b818eb398d8e5f89297a1bb0c0e0a7cf2ed0a2535056f119214`).
  The right donor region is resized from 1599 to 1602 master pixels to account
  for the source's 640px width versus Battle ID's 641px composition.
- Match per-channel wing mean/variance to the original Team border material,
  then join through 32 source pixels of reflected wall material on each exterior.
  The complete original 640x480 center is restored pixel exact at 3x master scale.
  Papers, slots and live controls retain their original placement.
- A built-in imagegen candidate using Battle v4 as reference is retained but
  rejected: it added letterboxing and changed the framing. The reviewed v2 uses
  the actual Battle artwork donor and the established technical join method.

## Map v6

- Master `choose-venue-map-wide-v6.png`, 9453x3345, SHA-256
  `0bc0df7bec3446a0498d4dd292e396f83ff81df926f01683a4ee0cad79541aca`.
- **D-new:** repair narrow bridges rather than regenerate the entire map.
  Three built-in imagegen inpaints use fixed surrounding map fragments: left
  exterior connection, right exterior connection, and obsolete inset fill.
  Existing reference-guided v3 geography remains the basis of the outer field.
- Small monotone row/column registration corrects generated context drift.
  Context color statistics and an eight-source-pixel residual ramp (bounded to
  24 per RGB channel) integrate each new strip. This adjusts generated pixels;
  it does not blur the original map. V4 had an empty registration context in
  the inset and was held; V5 fixes the context bounds; V6 also limits residual
  corrections to prevent bright flecks. Finite/nonempty context assertions pass.
- **D-new:** permit 24 source pixels on either map border in wide side copies
  to connect to actual road/coast endpoints and remove the upper-left blue
  graphic fragment. Exact original-core preservation otherwise applies, except
  the prior obsolete inset rectangle. Explicit map-local exceptions:
  `[0,0,24,1115]`, `[1331,0,1355,1115]`, `[44,418,351,670]`.
- The nine wide anchors / 24 exported slabs use the revised master consistently.
  Native central texture draws stay original; the overlay has no `[textures]`
  entries. These are scenery changes, with live venue pins/cards unchanged.

## Validation and review

All native runs are serial, use the isolated runtime-data root, and restore its
complete save tree byte-identically. No crashes, watchdogs, unresolved calls,
debug-layer errors or truncated batches. Present figures are presents per 2 s:

| Log | Scope | Intervals | Median / minimum |
|---|---|---:|---:|
| `logs/run-20261026-133910.log.err` | Team v2, 16:9 at 2560x1440 |45|120 /98|
| `logs/run-20261026-135423.log.err` | Map v6, 32:9, four directional selections and six map checkpoints |45|120 /99|
| `logs/run-20261026-135811.log.err` | Map v6, 16:9 at 2560x1440, same directional schedule |43|120 /100|

The familiar inherited optional-file/kernel messages remain in triage. Bounded
inspection of the first unclassified return confirms the existing
`NtFsControlFile(0x90020)` stub. This is not a claim of zero diagnostic lines.

Integrity receipt verifies the Team center, map core outside the three declared
rectangles, both master hashes, all 24 slab hashes/dimensions and absence of
native texture replacements in the map overlay. Source publication checker and
diff checks pass. The runtime was unchanged; previous quick-regression results
remain applicable and were not repeated for artwork-only revisions.

Private workspace: `logs/menu-ui-v1.1/revision-v3/`, including exact prompts,
raw images, targets, integration/export recipes, receipts and native captures.
Masters live in `logs/menu-ui-v1.1/artwork/`. Review:
`http://127.0.0.1:51739/review.html#map` and `#teams`. Browser QA verifies 34
loaded images, no broken images/page-width overflow, current v6 capture links,
and working comparison controls. `review-v2.html` preserves the prior review;
do not run an older gallery builder over the current page.

The nine new approval pins verify with zero failures; the old 670 pins remain
unchanged and are verified separately against their original private art root.
A new local 1.1 RC1 integrates the prior RC2 with the approved background/text
exports and the transition-top anchor. Faithful remains optional and unbundled;
the branch's runtime first-tile repair is required for the transition change.
Existing release assets/catalogue and prior approval pins are untouched. No push
or publication was requested or performed. Feature branch does not edit PROGRESS.md.

Private immutable authoring checkpoint (made before the final approval):
`/Users/vlad/Code/defjam-data/scratch/menu-ui-v1.1-revision3-20261026`,
1,328 files / 1,019,926,210 bytes, manifest SHA-256
`f4eabf47b98540d60fcd7b8fb158cadcbb8c955fe7c9c40c16ecb7322b423d97`.
All 1,328 entries independently verified without mismatches. Approval is recorded
separately in the current ledger; the checkpoint's earlier pending-review status
is preserved historically.

Local candidate `menu-ui-hd-wide-v1.1.0-rc1` has 163 native textures,
222 side PNGs, 63 wide anchors and 17 unique stretch entries. ZIP 503,829,023 bytes,
SHA-256 `a37087217f662d0bafaece5749d5d2e3073de141ae3ab82c1f5dcafd8d082fac`.
All 386 manifest/referenced file hashes and archive CRC pass. Input component
manifest pins, approval-ledger pin, receipt pin and output digest are recorded in
`config/menu-ui-v1.1-local-candidate.json`; the private receipt retains every
input/output file hash and source precedence. The pack is menu-only, with six
selected text overrides and no bundled Faithful library. Do not replace the
released archive or catalogue with this local candidate.

The **single integrated pack**, with only optional earlier-loaded Faithful beside
it, passes Team at 2560x1440 (`logs/run-20261026-141650.log.err`, 47 present
intervals, median 120/min 85) and Battle ID/fighter selection/directional map at
32:9 (`logs/run-20261026-141932.log.err`, 44 intervals, median 120/min 99).
Both restore complete disposable saves identically, with no runtime faults;
the same inherited diagnostic omissions remain. These checks do not require the
individual preview overlays. Current native/artwork checks are macOS only;
Windows/Deck and wider release acceptance remain separate gates.

Approved-art/candidate checkpoint:
`/Users/vlad/Code/defjam-data/scratch/menu-ui-v1.1-approved-20261026`,
28 files / 563,142,001 bytes, manifest SHA-256
`12a0280735d7a21f315a7daa2c9b19eca54e62563cb5776394e4da690bbc065b`.
Its nine art pins verify directly against this root. It contains the approved
masters/text, candidate ZIP/receipt, source ledgers, generation integration
receipt and integrated-run evidence. Full raw/authoring history remains in the
separate 1,328-file checkpoint. Its source report predates this paragraph.

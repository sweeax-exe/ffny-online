# HD widescreen menu continuation — 2026-10-26

Branch `codex/hd-widescreen-menu-v1.1` starts at release PR #123,
`53e5d8baf38029e66b24639ae50f49052b1de968`. Toolkit stays at the release pin
`7a0f2f60ac2f8671e6f34ce4ff61a77759ec8a7f`; no toolkit edits or publication.
The owner prioritizes headings and fighter banners for sharpness; the existing
approved backgrounds already feel faithful. The missing backgrounds remain
modpack 1.1 work, separate from the v0.12.1 release train.

## Fire-escape rectangle: source geometry repair

The supported dump's `screens/trans/t1.big::t1.o` already contains the misplaced
quad. This is independent of PNG decoding, asynchronous replacement uploads,
depth state, the widescreen shader and side-fill choice. The previous Windows
draw audit established an uncovered region above the first left scenery tile.
This session independently read the original object and reproduced the gap on
the current release-base Mac build, with no texture packs.

Shape 11/part 1, bitmap 9 (`51ebe2dcf268…`), has an identity texture matrix,
but its four vertices decode to x512..1024/y512.042..1024.084. The other nine
parts leave the first x0..512/y0..512.042 cell empty. The object's overall
bounding box ends at x641, so that quad also exceeds its parent bounds.
The source image is the upper-left stairwell artwork; its lower/right
neighbors belong to the same continuous composition.

`src/patches/menu_geometry.c` subtracts 4096 and 1656 from that quad's encoded
x/y coordinates. It joins the next row's encoded y exactly, without a float
round-trip. The identity UV matrix then samples the intended first tile.
The entry hook is `sub_00109F80_enter`: DynamicLoader's ELF parser, whose
input descriptor at ECX holds the raw header at +0x10. The repair runs before
section fixup, relocation and vertex-buffer upload. It changes guest memory;
the dump and replacement images are unchanged.

The hook first checks the known ELF/section layout, then the whole object's
13,089-byte FNV-1a fingerprint (`208bd986`, repaired `abe1d4d6`). Original
object SHA-256 is
`ec5dd8d50a085e36a2758a728b7a588f37d4f0d5ffe8d5fbe02c34d9059db8ff`.
Other objects and repeated loads are harmless. `RECOMP_MENU_TRANSITION_REPAIR=0`
restores the original geometry for comparison; `RECOMP_MENU_GEOMETRY_TRACE=1`
prints at most 32 parser entries. Normal play logs one repair when it loads.

`transition-9` in `config/menu-ui-wide-layers.json` adds the missing left side
anchor, using the existing pinned v4 master at (1600,0). Its native center stays
Faithful. The local exporter matches that center at correlation 1.000 and emits
two slabs. This registration requires the repaired runtime; keep the released
RC2 archive immutable and package it in a new mod version.

This is a source-informed correction, not a claim of a newly captured retail
console comparison. Native no-pack captures demonstrate that it closes the
reported hole, and trace positions independently confirm the corrected quad.

## Three missing backgrounds

There are two different screens labelled Select User ID. `options/userID`
(Story) already has the approved `8665c0…` wall and functioning wide slabs;
this was verified with live controls and native replacement/side-draw records.
The uncovered Battle screen is `options/battleID`, with a different six-tile
641x857 scene: four tall frames and a lower paper panel. Its vertical motion
must remain intact; a flattened 640x480 replacement is insufficient.

`battle/chsTeam4` has a separate 512+128 tile wall, including the four-window
paper backing. Two new exterior-wall imagegen drafts were made for these two
resources. Their source centers were restored at the 3x master scale; the local
preview overlay changes only side scenery, leaving the actual original/native
center textures and live controls intact. Upper Battle ID's 128-wide right tile
uses edge clamping for the fractional overhang, rather than a black padded
129th column. No old approved image or approval pin was changed.

The active Choose Venue map is **`screens/battle/map.big`**, not the older
`battle/venueMap.big`. Exact runtime source identities establish this distinction.
The old seven-tile probe did not render side art and is retained as an unsuccessful
probe. A second local overlay registers the actual nine map tiles with shift 0:
the renderer replays their existing offstage geometry outside the central clip.
Native 16:9/1440p captures show the original map across the full width with
the pins, photographs, captions and selection controls still centered.
No new map geography was generated. Wider aspect/pan coverage remains a
separate acceptance gate because the authored map is finite.

A 32:9 native reveal subsequently exposes the finite right edge and the empty
black header/frame of a baked map inset in the far-left original tile. The tile
PNG itself contains that box; it is not a leaked foreground draw. A **third,
unapproved** map-art draft extends the finite map and reconstructs the inset
area. Its source core is restored except map-local rectangle [44,418,351,670].
That exception is used only in shift-0 side copies for images 9 and 5; all
central/native texture mappings remain untouched. Additional left shifts -1024/
-512 and right shifts330/660/990 extend the edge quads. The new local overlay
has nine anchors/24slabs and needs its own native moving-map acceptance.

`config/menu-ui-v1.1-worklist.json` records source IDs, dimensions, master hashes
and pilot status. It is deliberately **not** an approval catalogue or release
manifest. The wall drafts remain unapproved pending the owner's review.

## Heading and fighter-banner clarity

Native source dumps identify separate rasterized fill/outline text textures,
including 512x64 heading atlases and 512x128 fighter banners. The dump also
contains the original TrueType faces in `fonts/fonts.viv`. Re-rendering three
headings from FERRO matched the face but changed native contours/spacing
(mask IoU 0.70–0.78), so those redraws are held and absent from the test pack.

The preferred local pilot retains the actual source lettering instead. Six
atlases have Gentle and Strong variants: Main Menu, Choose Match Type, Select
User ID, Choose Venue, Method Man as Blaze, and Fat Joe as Crack. Starting with
4x Lanczos RGBA, it narrows the alpha ramp only near strong opaque/clear edges
(9-pixel local max-minus-min at least 192). Slopes are 1.35 and 1.8 about 127.5.
RGB is byte-identical to the 4x baseline; the >=128 alpha silhouette is exact;
alpha outside the detected strong edges is exact. This changes edge clarity,
not typeface, spelling or source geometry. Soft effects without a strong edge
keep their baseline alpha. Neither strength is selected for release.

The local read-only review is `http://127.0.0.1:51739/review.html`: native wipes,
five-aspect wall previews, and synchronized text comparisons with Fit/Actual
1440p views. The older 51738 menu review and 8768 workshop were not changed.
Private recipes, prompts, raw outputs, source metadata and receipts live under
`logs/menu-ui-v1.1/`, with a durable snapshot in the data folder's scratch area.
No artwork, font, source dump, capture or executable is a public deliverable.

Private snapshot: `/Users/vlad/Code/defjam-data/scratch/menu-ui-v1.1-review-20261026/`,
1,042files /313,415,965bytes. Manifest SHA-256:
`5967d28f09904d803ca6a7828f4a964779587a9b449bd2bab855358fe3c08e51`.
It includes the new preview overlays, not the separately installed released
RC2/Faithful components. Saves, caches, binaries and generated guest C are excluded.

## Validation and retained evidence

- Release-base analysis/lift inputs were verified before reuse; the changed
  entry hook received a complete fresh lift (18,446 functions; 21 generated C
  files) and native build. Generated code remains ignored.
- `regress.py --quick`: **5/5** in seven minutes,
  `logs/regress-20261026-110132.txt`. Units **635 passed, 33 skipped, one slow
  deselected**; three golden checks match; fight provides 122 seconds of combat,
  median 120/minimum 107 presents per two seconds. Two earlier sandbox-only
  unit permission failures passed separately with host access.
- Native repair guard tests pass, including the actual supported object read
  locally, unknown-object rejection, exactly scoped writes, idempotence and
  the diagnostic disable control. The positive game-object check skips without
  `DEFJAM_DATA`; no game fixture is checked in. MSVC uses binary stdin/stdout.
- Original/no-pack baseline: `logs/run-20261026-102927.log.err`; repaired native
  comparison: `logs/run-20261026-103847.log.err`. The initial sandbox attempt
  failed memory mapping before rendering and is not a passed run.
- Serial transition captures: 4:3/no packs `20261026-104100`, 16:9/menu-only plus
  top anchor `104128`, 16:10 `104827`, 21:9 `104856`, 32:9 `104925`. Actual capture
  widths are 640/854/768/1120/1707 at height480; experimental enum names were
  used explicitly. The 16:9/no-pack repaired run is independent of the pack.
- Story User ID wide verification: `logs/run-20261026-103916.log.err`.
  Battle/venue baseline tour: `104157`; initial 1440p old-map probe: `104954`;
  Battle draft and actual-map reveal: `105306`. The first optional-User-ID shot
  schedule waited for an unvisited screen and produced no captures; that
  earlier tour is not visual coverage. Corrected tours use visited anchors.
- Choose Teams native1440p: `logs/run-20261026-110221.log.err`, actual
  `chsTeam4` reached, both wall side anchors replay, live team UI captured.
  Gentle text pilot native1440p: `110358`, all six source-key uploads observed;
  headings and fighter-name banner captures retained. Original-map32:9 reveal:
  `110522`, renders normally but retains the finite-edge/inset limitations;
  it is **not** accepted full-width coverage.
- Completed native checks restore the disposable save tree byte-identically
  and have no crash/watchdog/unresolved-call signatures. Digests retain inherited
  optional-file/kernel diagnostics; bounded originals were inspected. This is
  not a claim of zero diagnostic messages or a full three-platform regression.

## Decisions and hand-off

- **D-new:** repair the fingerprinted guest resource before relocation, in the
  game repo. The defect exists in the source coordinates; a generic renderer
  offset or painted black cover would affect the wrong layer of the stack.
- **D-new:** distinguish Battle ID from the covered Story profile screen, and
  active map from the unused venueMap resource, using exact runtime identities.
- **D-new:** keep source-contour clarity as the review pilot; hold TTF redraws
  whose lettering no longer matches closely enough. New art/clarity variants
  require the owner's selection before approval/export/publication.

Next: record the owner's wall/text feedback, finish moving-map and Team native
acceptance at the intended aspect set, freeze approved hashes in the proper
ledger, and build a **new** 1.1 candidate. Do not replace the released archive,
modify the signed catalogue or push/release without the owner's instruction.
Windows/Deck acceptance remains necessary before release. Keep PR #123 and its
active worktrees intact; this feature branch does not edit PROGRESS.md.

The new map draft's first serial probe refused to launch because another
checkout's game was active; it was left untouched. Retry the local
`logs/menu-ui-v1.1/probe.py venuepan '32:9 (experimental)'` schedule with the
RC2, transition-top, background-draft and `menu-ui-venue-wide-draft-v1` overlays
after that game has finished. This probe contains four directional venue
selections and six map checkpoints. The 32:9 **original-map** result above
must not be confused with acceptance of the new map-art draft.

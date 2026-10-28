# Widescreen menu background candidates — 2026-10-20

**Current checkpoint — D181/UI:** Vlad approved all seven scenes on2026-10-20.
Seventy final-file pins are frozen (670total);149source identities reviewed,
131unconditional plus18previously conditional. New scenic artwork is approved;
pack integration and native acceptance remain pending. The earlier review
record below describes how the batch was prepared and its unchanged limits.

Vlad chose the first menu-mod release scope: finish widescreen scenery, include
pre-fight loading, retain the main menus and previously approved artwork, and
leave the remaining panel/button/detail candidates alone. This supersedes the
wait-for-inventory-selection step in D178/UI. It does not approve new art,
install a pack, authorize closing the running game, or publish game assets.

## Delivered for artwork review

Seven scene candidates use thirteen new source identities, plus the already
approved Options door and a separate existing loading-lighting identity:

| Scene | New scenery | Preserved content |
|---|---|---|
| Story `choosGF2` | Both brick-wall sides | Center shield, exact two source canvases and separate APT depths |
| Story `choosGF4` | Both brick-wall sides, separately referenced | Variant's different tile order/compression; center shield |
| Story `choosPart` | Both brick-wall sides | Both paper notes, live portraits/text |
| Story `charDec` | Both wood-desktop sides | Folders, writing, pen and source shadows |
| Crib Options | Left wall only | Original translucent wall tail; approved shared Options door/right extension |
| Battle `matchLoa` | Both masonry sides of the opaque match plate | Baked card/header; live portraits, venue name, logos and masks; separate vignette |
| Battle `tips` | Both dark plaster-wall sides | Center sign; live tip text and animated sticker |

Review: `http://127.0.0.1:51738/wide-backgrounds.html`, also linked from
`review.html`, `inventory.html`, and relevant inventory cards. Previous/Next and
selector-focused arrows step scenes; images retain the shared linked wheel
zoom, drag pan and Fit controls. Original/Faithful references, five aspects,
left/right join close-ups, unchanged-center inspection and browser-local notes
are available. Existing inventory shortlist storage is untouched. The loader
lighting checkbox is a **review-only proposal** to stretch its original
full-stage vignette across the wide frame; it is not runtime evidence.

Private working root is `logs/ui-restoration-20261017/pilot-v1/`;
new art/history is under `wide-backgrounds-v1-20261020/`. `groups.json` records
source IDs, exact UV/local geometry and integer reconstruction. Exact prompts,
input hashes and built-in tool arguments are in `generation-jobs.json` and
`bridge-jobs.json`; output hints/raw copies are in generation receipts. No API
fallback was used. Fifteen built-in calls were made: thirteen side extensions
and two targeted tips-wall join repairs; historical total162. All generations,
including superseded tips revisions, remain local.

Masters are5120×1440 with the original640×480 scene at3×, centered at
x1600..3520. The entire Faithful HD scene core is reconstructed directly from
its source canvases, rather than repainted or passed through the generated
context. AI pixels are admitted only outside it. Source padding, alpha, labels
and live display-list pieces remain separate. Reference images use the source
palette; no warmer/yellower grade is applied. Bounded source-derived Lab
correction compares the model's fixed context against the actual reference;
it never alters the core. The tips wall required both128-native-pixel bridge
repairs, followed by source-matched fine L-detail contrast (median gain≈.494
on each side). The outer wall and core stay exact during these repairs.

Crib Options requires actual parent placement: door shape5 atx469.4 beneath
root `bkgWall`, wall shape9 atidentity beneath `menu`, with the translucent tail
drawn over the door. The review composition preserves that order. Its right
scenery is reused from approved `options-softness-v4/options-wide-v4.png`,
not regenerated. The standalone new wall-layer master retains its alpha.

## Verification and limits

Offline verification checks35/35 aspect crops retain their exact Faithful scene
cores, all105 source/draft scene images have the intended dimensions, and all42
join-reference/draft paths exist. The Crib composite core and original wall
alpha are separate from the standalone layer. All600 earlier approved or
conditional art pins pass unchanged. Existing22 game metadata/export/instruction
tests pass. Browser scene/aspect paths, no overflow at662px, Original/Faithful,
join controls, stepping and linked navigation are checked separately in the
private browser receipt. New integrity pins are preservation records, **not
owner approvals**; the public approved-artwork catalogue remains unchanged.

Source mapping is documented in `menu-widescreen-background-gaps-2026-10-20.md`,
`menu-prefight-widescreen-mapping-2026-10-20.md` and
`wide-background-root-placement-audit-2026-10-20.md`. Those audits establish
source identities and authored placements, not active game routes or runtime
clips. `matchLoa` and `tips` have mapped scenic plates. `oldMLoad` dynamically
loads venue scenery; its static full-stage image is a lighting clip, not a
replacement venue plate. Do not claim that every active pre-fight route is
finished until runtime observations identify the actual loader(s).

No game was started/stopped, no build was run, and no active pack, save, launcher
setting,8768 writer, fighter workshop or prior approval changed in this turn.
New art is UNAPPROVED and UNINSTALLED. This is the final scenic artwork batch
for review, not a claim that the mod's first release has passed native testing.

## First-release integration checkpoint

Retain approved title scenery, main menu/Options/Jukebox, Battle setup and
2–4fighter selection, Rewards/High Scores, approved title/emblems, Extras,
Phone, Popup/Profile and Gym/control artwork. The approved Profile wall master
already has32:9 scenery but still needs wide registration. Keep Mode's specific
right-edge lighting and the fire-escape moving pan conditional; transition
bitmap9 remains Faithful-only until its actual transformed geometry is checked.
Exclude the held16-study/19-source detail batch, deferred shop-paper drafts and
all other unselected inventory candidates. Faithful HD remains their baseline.

After the seven new scenes are approved: pin final masters; add source-shaped
retained-Faithful tiles as needed; register side slabs with measured transforms
using `scripts/export-menu-ui-wide.py`; keep the original vignette as a separate
full-width overlay. Use fresh immutable components and an isolated release
candidate, leaving currently tested packs intact. The generic renderer already
supports slabs/stretch; no speculative runtime feature was added here.

Final gate: when Vlad permits serial game checks, identify the active loading
resources, verify their live portraits/text/alpha/masks, and capture4:3,16:10,
16:9,21:9 and32:9 on the intended main-menu routes. Check root transforms, UVs,
scissors, fades and the complete fire-escape pan. Only then call the first mod
release runtime-ready. See `menu-wide-first-release-integration-audit-2026-10-20.md`
for the existing exporter contract and pack order.

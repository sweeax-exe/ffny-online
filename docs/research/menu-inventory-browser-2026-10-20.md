# Menu image inventory and widescreen focus — 2026-10-20

Vlad approves the Gym/control15-study/22-source batch (D175/UI), then changes
restoration scope (D177/UI): routine buttons and small paper panels offer too
little improvement over Faithful HD. Stop those individual imagegen jobs. Focus
on backgrounds where widened scenery improves the aspect-ratio experience,
and on exceptions Vlad identifies in the new inventory browser. Do not start
further imagegen calls before his selections. The earlier16-study/19-source
batch remains pending; this direction does not approve it.

D175/UI freezes127newfiles,600total pins/136reviewed source IDs:118unconditional,
8Mode Select conditional and10transition conditional. The separate22-tile Gym/
controls source4×export is pixel-identical and uninstalled. The existing game,
active packs, saves and8768authoring writer remain untouched.

Before the scope change, source mapping prepared30studies/57sources:10complete
paper panels and20Faithful utility graphics. Three requests (Barber, Clothing,
Jeweller) were already in flight; all three raw outputs are retained, with exact
prompts/args/references/hashes, under private `story-shop-v1-20261020/`.
No raw output was adopted, assembled, approved, exported or installed. Seven
other planned generations were not started. Historical total is147calls.
Read the [paper mapping](menu-story-shop-mapping-2026-10-20.md) only as an offline
source audit; the proposed paper batch is deferred.

## Inventory browser

Open `http://127.0.0.1:51738/inventory.html`. The original review page links it.
All53registered flow names yield1,323unique source IDs, deduplicated across
aliases. Default view includes all1,323; the optional core-menu/support scope
has1,834. The latter includes important Story backgrounds absent from the
registered-flow list. All sources have original and Faithful4×PNGs plus checker
thumbnails, held privately. Full source-aligned custom comparisons are offered
where derivable from pinned/exported art; approved group artwork without a
native source-aligned preview is labeled explicitly. No guessed registrations.

| Scope | Total | Approved/conditional reviewed | Pending draft sources | Other untouched | Outside approval |
|---|---:|---:|---:|---:|---:|
| Registered menu flows |1323|115 (97+18)|13|1195|1208|
| Core menus/support |1834|132 (114+18)|19|1683|1702|

Counts of textures, prepared mosaics, raw proposals and aspect crops are
separate. These totals do not count imagegen jobs. All16prior pending studies
remain pending. The wide broad UI category has11,261unique IDs and136reviewed;
its11,125residuals include catalog/portrait/support material and are not added
to the overlapping registered/core scopes. Earlier flat coverage fields were
incorrectly carried across scopes after supporting resources were approved;
the current catalogue uses actual set intersections:115registered/132core/
136broad. [The census](menu-ui-remaining-census-2026-10-20.md) records the method.

The browser has search by ID/name/path, screen and size filters, status badges,
Original/Faithful/custom thumbnails, full-size paired comparison, synchronized
mouse zoom/pan, Previous/Next and arrow navigation, and a browser-local imagegen
shortlist with notes/download. Faithful-retained treatments are labeled
separately from custom restoration. The shortlist does not alter artwork
approval, packs, or8768workspace review records. Actual owner selections remain
in localStorage at the existing51738origin; do not clear or replace them.
CSV exports: `menu-inventory-index.csv` (1323rows) and
`menu-inventory-core-index.csv` (1834rows), private serving root.

D176/UI also adds Previous/Next/position counters to eleven existing artwork
selectors. Left/right keys while a selector or its navigation is focused step
textures; image-focused arrow keys retain pan behavior. New resources are
`review_sequence.mjs`, `menu_inventory.{html,css,mjs}` and the packaged shared
navigation source. The image-navigation module now avoids reattaching to an
already initialized viewport when imported by the inventory page.

## Widescreen work is not complete

[The bounded background audit](menu-widescreen-background-gaps-2026-10-20.md)
confirms ten unreviewed source tiles in five candidate groups:

- Story girlfriend selection variants2and4: two wall compositions, two tiles each.
- Story partner selection: two wall/paper tiles.
- Story character details: two wood-desk/folder tiles.
- Crib Options: an opaque wall/paper tile plus translucent tail. Its separate
  Options wall image4 is an exact already-approved source-ID duplicate.

Four local compositions cover the640×480APTstage; Crib Options' new layer is
571×480 before parent transforms. Active root-frame use, placement, clipping
and actual widescreen exposure remain unverified. Tournament's sampled
352×256mixed-alpha shapes do not establish a full-stage background. This is a
bounded audit, not a claim to have classified all remaining backgrounds.
The gallery's **Unrestored widescreen candidates** filter selects these10tiles
and switches to core scope. Review before generating. Earlier fire-escape v4
still needs a full native pan playtest; Mode Select's specific lighting
condition remains open. Existing title/front-end/Battle/Rewards/HighScores and
Profile artwork approvals do not settle these other Story resources.

## Verification and handoff

Builder and finalizer validate all1834original/Faithful images and native-sized
custom crops. Registered pagination is28pages,1323distinct IDs with no repeats;
core scope contains1834unique IDs. Browser checks cover scopes, counts, sizes,
known background filter, empty search, thumbnail modes, source/custom comparisons,
linked framing, arrow navigation and shortlist note persistence across reload.
Test selection/note removed after verification; owner selections preserved.
No overflow/errors at662px. Screenshots and browser receipt remain private.
Four new Node filter/sequence tests and four existing navigation tests pass;
22game metadata/export/agent tests and600integrity pins pass. No runtime build
or game test was run while the owner's game remains active.

Source-only recipe/census/mapping bundle:
`DefJam-Modding-Tools/research/menu-ui-inventory-20261020/`. Private additive
archive `<data>/scratch/menu-ui-inventory-widescreen-focus-20261020/` contains
the current gallery, paused paper inputs/raws, Gym approval/export and review
source; earlier immutable archives remain bases. The overlay contains7712files,
778,610,365bytes; manifestSHA
`dd626aafd031b303af5ef739ae51ba99bc3cb6481796d84fe92e10b70736dbe4`.
All600pins and2730previousPNGs remain exact. GitHub sign-in recovered during finalization. Source-only game and authoring
branches and their existing draftPR descriptions are updated; artwork stays local. Next action:
Vlad reviews/shortlists the inventory; then restore/outpaint chosen widescreen
backgrounds, preserving palette, native4:3center and both full-height joins.

# Popup, profile and autosave restoration batch — 2026-10-20

Vlad says the prior16-item/19source batch looks good but explicitly asks to
“Leave them for review and move on” (D170/UI). It remains pending, unchanged,
not approved, pinned, exported or installed. Exact words/status are retained in
private `menu-details-pending-20261020.json`. Catalogue343pins/92reviewed IDs
remains unchanged:74unconditional,8Modeconditional,10transitionconditional.
Phone approval and prepared two-tile export remain; native checks still pending.

## New batch

Fifteen new studies cover22unique source IDs, disjoint from all92reviewed IDs
and19pending IDs. Ten paper surfaces receive tiny source-protected restoration;
one original-core wall study adds wider scenery, and four stamp/effect studies
retain Faithful4x. All15new studies are UNAPPROVED.

| Study | Sources | Treatment |
|---|---:|---|
|Profile/User ID brick backdrop87|1|Exact Faithful original core; new centered wide wall|
|Profile list paper91+90|2|Restrained cream-paper detail; red band/lines exact|
|Profile soft backing127+125|2|Faithful; paired black alpha preserved|
|Profile radial120|1|Faithful; native effect sampling retained|
|English/French no-ID stamps6/2|2|Faithful; original glyphs and alpha exact|
|Shared popup paper127+126(beloader)|2|Restrained paper; shared exact IDs in3movies|
|Feloader header/control30+31|2|Restrained cream-paper detail; black band/cap exact|
|Shared card diagram220,GameCube236,layered note244|3|Restrained paper; all authored graphics exact|
|PS2 English42+41 and French47+46 autosave|4|Restrained paper; matching language pairs/graphics exact|
|GameCube A/B autosave60+59|2|Restrained paper; baked icons/labels exact|
|Xbox autosave67|1|Restrained paper; console graphic/warning exact|
|**Total**|**22**|**15studies:10papers+1wide scene+4Faithful**|

[Shared popup mapping](menu-shared-popup-mapping-2026-10-20.md) establishes
adjacent paper quads: b4c0left256 +b791right151,texel407×256. APTheight310is
not the rendered256height and is not filled. The header uses256UVtexels over
343geometrypixels plus77rightcap; author in333×96texel space, review420×96
source-shape footprint with that native left stretch. This is authored sampling,
not a new invented stretch. The originals, exact matrices and fractional UVs
remain retained; fulltile4xexports preserve storage outside used crops.

[Profile/autosave mapping](menu-profile-autosave-mapping-2026-10-20.md) proves
profile87samples640×480from1024×512storage. Its paper/header/emblem are baked
into the wall and remain exact. Profile list is187×328from91+90; soft backing
is256×372from127+125, not the full292wide APTbounds. No new ink/mask painting.

The autosave root has explicit PS2/GC/Xbox states. PS2 English Shape43 and
French Shape48 are LANGUAGE variants inside bkgSprite50; French is not a GC
state. GCbkg64 uses the A/BShape61, with rootScale1.021484375 and live language
text; Xboxbkg70usesShape68. Integer previews keep used512×400,512×375,467×256
regions and the English lower-strip1pixel offset. Exact image41UV x.0293..511.0109
and geometry x1.0293..512.0109stay in metadata, not silently rounded at export.
APT313height for Xbox does not justify inventing57rows beyond its256tallquad.
Dynamic profile/edit/scroll/prompt/button/language layers remain live.

## Generations and protected assembly

Fifteen built-in ImageGen calls,142historical total: ten paper proposals,
one unused native-wall proposal,two wider sides,two original-edge bridges.
Full exact prompts/reference paths/input hashes/raws are retained in
`generations.json`; `generations-v1.json` keeps the first13call record.
No CLI/API fallback. Native wall safe mask admitted zero generated pixels,
so its entire source core remains Faithful4x; it is explicitly labeled that
way in review. Do not claim the native core was repainted/restored by the model.

Tile-aware4xLanczos is the authority. For papers adopt16%of sigma4fine L detail,
clamp±1.25L and remove safe mean; original chroma/broad lighting retained.
Use sourcealpha erosion9HDpx, edge exclusion13HDpx, L40..96, cream/neutral
material mask. Explicit regions protect warning icons/header strips, right-hand
card/console/label groups, the profile red selector, source borders/creases and
all tilejoin/activecrop guards16HDpx. Reconstruct actual review mosaics from
final native-shaped full storage candidates after guards, preserving all unused
rows/columns. Final alpha, protected graphics/geometry and padding exact across
22tiles. MaximumRGBdelta4; recorded mean safeΔLab per restoredpaper lies within
L−0.0066..+0.0015,a−0.0069..+0.0008,b−0.0047..+0.0019. Quantization is recorded,
not claimed mathematically zero. Original cream is retained without added yellow.

## Widened wall and join refinements

Two790×480side targets hold256columns of original context and534gray columns;
fill only new wall, no repeated paper/shield/newobjects, keep brick courses,
original palette/focus/light across both full-height joins. Normalize only
near-identical aspect outputs, return to native sampling, take gap pixels,
match source boundary low-frequency Lab row profiles (16nativecolumns,sigma24HD,
clampL10/a8/b8,256nativepxdecay). Original4:3core is retained exactly. A5124×1440
master crops2HDpx eachside to5120×1440, source core atx1600..3520.

V1 still had brick-course breaks, so both384×480bridge targets hold128columns
outerwall +128graygap +128original(or opposite order). Only128new nativecolumns
outside each original boundary enter v2; fixed context/core discarded. Endpoint
palette profiles/clampL8/a6/b6 and24HDpxouterfade preserve the new outerwall.
V3adds no generation: reduce bridge fine Lcontrast toward original adjacent
32nativecolumns, sigma18HDhighpass/24HDrow profile, gain0.4..1,48HDouterfade.
Median gains0.625left/0.885right. Core, native alpha/graphics and exterior beyond
those128nativecolumns remain exact. All attempts/parameters retained; v1/v2
stay selectable history. Both current full-height joins were visually inspected;
this is a review draft, not owner/runtime acceptance or a claim that numerical
center equality proves material continuity.

## Review and verification

Review51738 starts with this15-item batch, overview, original/Faithful, alpha,
checker/dark/light, side/wipe/candidate, five ratios and wall version/both-join
controls. Other panels have isolated source-footprint scale studies, not native
screen positions or runtime captures. Wall crops retain the original center
through4:3,16:10,16:9,21:9,32:9. The prior16-item batch follows unchanged/pending;
phone approved; ladder conditional pending native moving-pan playtest.

All343approved/conditional pins and2,044prior PNGs unchanged.22tilealpha/graphics/
UVguards/padding and75aspect centers pass. Browser60reference/channel cases,
75aspectpairs,3BGs/3modes,75%bounds at16:9,three wall revisions and bothjoin
images checked;11initial loading cases decoded on revisit, including explicit
21:9view. No overflow at662px, zero warnings/errors. Proof `review-proof.png`.
Existing22focused tests pass. No build/runtime/game launch/stop/default/activepack
change. Live8768writer/17candidates/Trejo unchanged. No new source IDs approved.

Next: explicit feedback on both pending batches; pin only accepted scopes.
Continue bounded classification of remaining Story/widgets/maps/thumbnails
rather than treating broad UI counts as painting counts. Later validate approved
Extras/phone, native transition transforms/bitmap9, profile/live text and popup
platform states under complete-save guards and allfiveaspects. Leave the other
checkout's game running as requested;4:3transition fix is not yet proven in game.

Working `logs/ui-restoration-20261017/pilot-v1/popup-profile-v1-20261020/`.
Private immutable additive archive `<data>/scratch/menu-ui-popup-profile-v1-pending-20261020/`;
apply to a COPY of earlier complete/Extras/transition/phone/details snapshots.
Source-only recipes/prompts/review/mapping copies in DefJam-Modding-Tools
`research/menu-ui-popup-profile-20261020/`. All artwork/raws/captures/arrays
remain local. Earlier immutable archives unchanged.

Publication note: source-only commits are saved locally in both existing topic
branches. Both pushes failed because saved GitHub authentication was unavailable;
`gh auth status` reported an invalid token. Renew sign-in before retrying; PR62
description has not yet been updated for this batch. The local review and
verified art archive are ready independently.

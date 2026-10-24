# Menu/UI next scope and art decisions — 2026-10-17

Vlad approves the current water-tower/roof artwork (D131). Mode Select receives
conditional approval (D132): retain the current version until the running game
can show whether uneven darkness at the right original 4:3 boundary is visible
with the real spotlight/masks and transitions. No repaint is authorized by this
concern alone. Skyline v3 remains approved; the city/middle layer still awaits
an explicit decision. This is art review, not completed runtime acceptance.

Exact current versions: ignored `pilot-v1/approvals-v1/approval.json` pins the
water-tower restrained/master/five previews (7files) and mode dark/lit masters,
ten previews and four source-shaped inputs (16files). Raw/earlier generations,
city art and four separate workshop trial revision approvals are excluded.
Review51738 now displays the per-study status. Writer8768 is unchanged.

## Recommended next batch

The original census remains 53distinct registered screen names / 1,323unique
static images referenced by those flows; this is not a background count.
For this recommendation, inspected 28 original texture references from10named
resources, saved only as private contact sheets and hash-pinned metadata in
`logs/ui-restoration-20261017/next-scope/`. No new image generation.

| Priority | Screens / art | Evidence and intended treatment |
|---|---|---|
| 1 | Options and Jukebox | Original options/options has12static textures; jukebox/beatbox has23. Inspected options names71/8: brick wall and weathered pinned paper; name4: dark door/wall strip. Jukebox20/19 form another brick/paper board. Strong, small scenery targets for restrained restoration and centered wide extensions, with paper/layout and original lighting preserved. Similar appearance alone does not prove shared IDs or reusable geometry. |
| 2 | Battle match-type and fighter selection | cmtype has158static IDs, chooseF2 has183, with many UI/portrait fragments. Inspected cmtype2/1: blue-green wall/pipe/poster background; chooseF2 338/337: wall/paper portrait frames. Restore the backgrounds and worn frames first; map active bounds/animation before widening. Portrait restoration is separate. |
| 3 | Finish title and mode UI artwork | Title wordmark and localized Press Start fragments remain outside the scenery studies. Main-menu support assets include six256square emblems. Preserve exact lettering, silhouettes and intentional distress; use faithful scaling or source-aligned reconstruction rather than asking imagegen to rewrite text. The shield/effect requires its actual shape/mask mapping, not a guessed full-rectangle image. |
| Later | Venue-selection frames, rematch/results, Story phone | chsVenue143/145/150 are photo-frame/mask pieces, not scenic backgrounds. rematch1 is a weathered paper panel; msgCentr110/111 is a split phone shell. Useful UI work after the two-scene runtime test. Do not confuse thumbnail/catalog counts with independent scenery. |

Options/Jukebox first, then Battle setup/selection, is a recommendation rather
than authorization to generate a new batch. The smallest immediate next step
is the title/mode integration pilot, so we validate layered art in game before
replicating a widescreen integration pattern across further screens.

`battle/oldMLoad` has a flow entry, but its name and offline art alone do not
prove the active loading path. Do not prioritize remastering it until a runtime
capture confirms it is used. Fonts, localization, HUD, catalog thumbnails and
3D shop rooms are outside this scenery batch.

## Conditional Mode Select acceptance

Use source-mapped replacements and explicit wide layer/UV/scissor support;
flattened review compositions cannot replace animated game layers. Review the
right original4:3 boundary in dark/lit states, moving spotlight, closed/open
shutter, selection changes and entrance/exit transitions. Compare matched
animation/light states at4:3,16:10,16:9,21:9,32:9. First test Mac Vulkan, then
separately Windows D3D11/Deck Vulkan as appropriate. Existing runtime support
still needs integration; no game capture or acceptance is claimed this turn.

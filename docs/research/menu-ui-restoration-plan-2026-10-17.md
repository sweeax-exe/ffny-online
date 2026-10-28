# Menu/UI restoration and widescreen artwork — inventory and proposed pilot

2026-10-17. Scope requested by Vlad: inventory the menu textures, evaluate the
existing comparison tools and imagegen, and return a plan before artwork trials.
This pass used source/archive reads and a live workshop check. No game build,
game launch, generated artwork, candidate import or pack installation occurred.

## Feasibility and recommendation

Imagegen can produce source-guided restoration candidates and new side scenery.
It cannot guarantee exact lettering, original pixel placement, alpha or tile
registration. Keep the original/accepted Lanczos baseline as the authority;
technically restore protected pixels and alpha after generation. Review new
detail as an interpretation rather than recovered ground truth.

The current workshop already works for individual UI textures. Same-shape
restorations can use the existing runtime replacement path. **True full-width
menu backgrounds need additional authoring and runtime support.** Both import
and loader require uniform integer 1–8x scaling of the original dimensions;
changing PNG shape alone cannot change APT geometry, UVs or clipping.

Recommend a two-screen pilot: the retail Press Start and mode-select menu,
restoring their authored layers first and then extending only the background.
Keep controls, logos, prompts, localization, selection effects and animation
live. A menu-control re-layout is a separate optional design choice.

## Inventory method and evidence

Read the retained accepted faithful corpus (18,971 supported unique textures,
24,266 aliases, one explicitly unresolved image) and freshly traversed the
owner's `screens/screens.viv` with the existing bounded BIG/RefPack/SHPX parser.
Fresh metadata inventory: **13,144 texture records, 11,251 unique source IDs, zero errors,
zero IDs missing from the retained corpus, zero retained screen IDs absent from
the fresh traversal**. This validated format metadata and complete source
identities; it did not freshly decode every image. All 28 original/faithful PNG
hashes for the 14 pilot tiles matched the retained corpus. No archive payload
was written to source control.

The broader corpus category **UI and menus contains 11,261 unique textures**:
11,251 from screens.viv plus ten popup/HUD-shared textures in assets/misc.viv.
This is a static-image scope, not a count of background paintings or every UI
primitive. Fonts, vector shapes, masks/timelines, generated text and video need
separate treatment. The overall census retains 24 font and 94 movie entries;
the unresolved 0x65 image is an environment map, outside this menu pilot.

`feflow.xml` registers **54 screen records / 53 distinct names**, referencing
**1,323 unique static textures**. This flow list is not every loadable screen:
runtime-loaded shops/support art and old/demo/test resources also exist.
`story/chalsum` and `story/summary` have flow entries but no matching APT member;
retain them as unresolved/stale references rather than inventing textures.

There are 8,794 APT members and 8,693 SHPX-containing archive paths in screens.viv.
Most are small art-library wrappers, portraits, thumbnails and combinations;
these numbers do not represent thousands of interactive menus.

The core groups (front end, Battle, Story, Options, memory card, extras, scores,
jukebox, headers, transitions and shared `_art`) contain **1,834 unique textures**.
Of those, **452 are at least 256x128**. That size filter is a candidate shortlist,
not a visually verified background count: it includes text, logos and posters.
The first pilot is classified below; the remaining backgrounds need per-screen
composition review before an honest background-only job count is possible.

Full local metadata deliverables, under ignored `logs/ui-restoration-20261017/`:

- `all-menu-texture-aliases.csv`: all 13,170 relevant alias rows, IDs, names,
  indices, dimensions, alpha extrema and original/faithful relative paths.
- `fresh-screens-metadata.json`: fresh SHPX metadata and hashed APT member list.
- `registered-screens.csv`: all 53 flow names, texture counts and APT presence.
- `summary.json`, `large-core-candidates.json`: counts and candidate records.
- `inventory.py`: session-local reproduction script with exact input paths.
- Five contact sheets: retail title, demo title, main menu and support art.

Corpus ID: `a512d2429f11f9ab03d2eaef65df53afd0a2a2536cfe67af638156ba69bb00ec`.
Corpus JSON SHA-256: `b4b1433617bcd0687ab5242d5aa0ca63b395f06ee9174b8d9cbb35282d461089`.

### Texture families

Counts are unique within each family; shared IDs overlap, so do not add rows.
Containers below count SHPX-bearing paths, not visible menus.

| Family | Unique textures | Containers |
|---|---:|---:|
| `_art` | 259 | 18 |
| `_shared` | 53 | 6 |
| `battle` | 653 | 27 |
| `chyron` | 1 | 1 |
| `extras` | 37 | 4 |
| `hiScores` | 42 | 1 |
| `intMain` | 248 | 9 |
| `jukebox` | 23 | 1 |
| `match` | 1,147 | 568 |
| `memoryCard` | 63 | 4 |
| `options` | 146 | 9 |
| `portrait` | 2,132 | 1,698 |
| `shopThumbs` | 4,620 | 4,766 |
| `sketchAssets` | 1,368 | 1,384 |
| `story` | 519 | 19 |
| `test` | 35 | 4 |
| `thumbs` | 305 | 146 |
| `trans` | 10 | 1 |
| `venuethumbs` | 76 | 27 |

## Pilot assets and actual workload

**Retail Press Start is `screens/intMain/prestart.big`, not `demoStrt`.** Its
640x480 APT contains 15 textures: six scenery tiles, a separate shield/effect, the title
logo, and seven localized prompt fragments. The scenery is three logical layers:

| Layer | XSH names | SHPX indices | Source dimensions |
|---|---|---|---|
| Skyline | `1`, `2` | 0, 5 | 128x512 + 512x512 |
| Near city | `5`, `6` | 13, 14 | 128x512 + 512x512 |
| Water tower / roof | `10`, `11` | 1, 2 | 128x512 + 512x512 |

Assemble each pair according to its authored shape/UV bounds before asking
imagegen to restore it, then split the accepted result back to the original
tile layout. Texture height is 512 while the stage is 480, and the layers have
different placements and alpha; simple full-rectangle flattening is insufficient.
`demoStrt` has eight different assets including DEMO VERSION and rating art;
its larger 1024-wide textures include padding/streaks, not usable wide scenery.

**Mode select is `screens/intMain/mainMenu.big`.** Its 120 textures are:

- Eight environmental tiles, in four sets: dark poster wall (`4/5`), lit wall
  (`13/14`), rolling shutter (`17/18`), and wall/door jamb (`52/53`).
- Two moving spotlight tiles (`223/224`).
- 110 UI fragments: 21 door emblems, 27 titles, 30 subtitles/tags and 32 descriptions.

This is an animated scene with nested sprites, moving light, door movement,
poster selection and language variants. Lettering/icons are also baked into
some wall textures and need protection. The eight environmental IDs have no
aliases elsewhere in this corpus, so this restoration will not automatically
restore other menus. See the [precise layer mapping](menu-asset-mapping-2026-10-17.md).

The initial scenery scope is therefore **14 original tiles across seven logical
layer/state sets, serving two screens**. The logo, shield/effect, spots and text remain
separate. This is a manageable art pilot rather than an 11,261-image generation
batch. Start by auditioning one assembled title layer, then one main-menu wall
set, before spending generation/review effort on the rest.

## Existing tools: reusable and missing

Authoring now belongs in `/Users/vlad/Code/DefJam-Modding-Tools`; the recomp's
HD setup helpers are frozen compatibility snapshots. The live workshop remains
at http://127.0.0.1:8768. Its private corpus/workspace paths are in that project's
ignored `local.json`; retain its single writer and current fighter reviews.

Live check: switched an agent-created inspection tab to Full texture library,
searched `prestart.xsh`, found all 15 images, and displayed the 512x512 water-tower
tile beside faithful HD. Existing synchronized 2D comparison, RGB/alpha,
checker/dark/light, pan/zoom, regional notes, immutable revisions, protected
assembly and sparse export all apply to UI. No fighter mesh is required.
Restored the original fighter filters, Trejo v4 selection, and closed the
inspection tab. No review notes, approvals or candidate files were edited.

Useful later extensions: neutral menu-specific briefs; two selectable Rich
Original Detail revisions beside the baseline (current lanes are style-specific);
split-tile/layer composition with boundary overlays and timeline keyframes;
4:3/16:10/16:9 crop/safe-area previews; separate wide-art metadata and export.
Do not weaken the current atlas import checks globally to admit arbitrary art.
See the [tooling audit](ui-texture-tooling-audit-2026-10-17.md).

## Proposed production sequence

1. **Prepare references and protected regions.** Retain source IDs, all aliases,
   lossless originals and faithful enlargements. Map displayed UV bounds and
   masks. Capture retail title/menu keyframes later in guarded disposable saves.
2. **Generate small restoration auditions.** Use built-in imagegen with source
   and faithful references, Rich Original Detail direction, original palette,
   grit, city silhouette, lighting and layout. Do not regenerate text/logos.
   Store exact prompts, input/output hashes and raw generations privately.
   Use deterministic assembly to preserve approved alpha and protected regions;
   masks/annotations in a prompt alone cannot guarantee exact pixels.
3. **Review texture and screen together.** Compare original, faithful and each
   candidate at game size and full resolution; check seams, registration, alpha
   fringes, intentional wear, contrast and lit/dark consistency. Import new
   immutable revisions through the existing server. Keep prior fighter work.
4. **Make a protected-center wide master per background layer.** Extend scenery
   horizontally rather than stretching it. Store the center rectangle, source
   composition, layer role and crop policy. Derive 16:10 and 16:9 from a coherent
   master at common height. Example art-space sizes: center1920x1440,
   16:10=2304x1440, 16:9=2560x1440. These are composition examples, not current
   pack dimensions; source tile exports still obey uniform scale. Wider masters
   or explicit fallbacks are needed for 21:9/32:9. Preserve alpha in the old
   center, but newly extended layer coverage needs its own reviewed alpha.
5. **Implement an opt-in runtime integration.** Current one-ID/one-image loader,
   geometry, UVs and scissor do not support wide masters. Add explicit per-screen/
   layer binding and fallback; keep Def Jam-specific mappings in this repo and
   generic support in the toolkit fork. Do not globally widen UI draws. Match
   each extended layer's motion, opacity, mask and transitions. A static side
   plate can be a prototype, but it will not prove animated-layer fidelity.
6. **Test approved drafts in game.** First ordinary same-shape replacements at
   4:3, then wide-background implementation at 4:3/16:10/16:9; finally agreed
   ultrawide targets. Export the small override after faithful HD in pack order,
   restart, and verify authoritative runtime texture identities. The current
   alias file contains only 13 previously verified keys, not all menu textures.
7. **Expand by screen family.** Battle setup/options, loading/results, then
   Story/shop/creator screens. Reuse actual shared IDs; keep 3D shop rooms,
   catalog thumbnails, fonts and in-fight HUD as separately measured work.

## Widescreen/runtime validation requirements

The port supports original4:3, stretch, auto,16:9,16:10 and experimental21:9/32:9.
Menus currently remain centered with their original proportions. The exact
toolkit pin `4a6c88e` was read from the clean local `xboxrecomp-audio-fix` checkout;
this workspace's submodule is not initialized. Existing blur side-fill is
D3D11-only; Vulkan/MoltenVK/Deck currently have black sides. Design-document
`side_fill=art` is not implemented. See the [runtime audit](ui-widescreen-audit-2026-10-17.md).

Use actual harness `--env RECOMP_WS_ASPECT=1.7777778` or `=1.6`, with
`RECOMP_TRANS_SHOT_NATIVE=1` for full native detail or `RECOMP_TRANS_SHOT_WIDE=1`
for aspect-preserving overview captures. The default 640x480 capture squeezes
the wide canvas and cannot judge proportions. Planned `--widescreen` and a
`regress --only widescreen` check do not exist in current scripts.

Test settled/entrance/exit and parallax/spotlight/door keyframes, language prompts,
tile seams/mips, scissor/fades, live selection/input, source-center alignment,
wide fallback and first-load/cache cost. Compare art at the same visual time and
color state: menu animation makes unrelated frames misleading. Capture texture
IDs separately from performance measurements. Use backbuffer captures rather
than desktop screenshots. Validate Windows D3D11 and Mac/Deck Vulkan separately;
renderer/loader changes require focused fixtures and full project regression.
No new game/runtime acceptance is claimed by this analysis.

## Questions for Vlad

1. Within Rich Original Detail, should the first samples be restrained cleanup
   or visibly richer surface detail? Recommend a restrained source-faithful
   candidate with original lettering/logo protected.
2. Keep menu controls centered and extend only background scenery, or also
   re-layout controls/header/footer across the width? Recommend backgrounds first.
3. Make the first wide masters for 16:9/16:10, with an explicit ultrawide fallback,
   or include 21:9/32:9 scenery in the first art pass? Recommend first validating
   4:3/16:10/16:9, while recording the intended eventual widest target now.

These are art/product choices for the next phase, not prerequisites for this
completed inventory. No generation batch should be inferred from total counts.

## Owner choices accepted, 2026-10-17 (D124)

Vlad approved restrained cleanup, original color accuracy with no additional
warm/yellow cast, centered original controls/composition with extended scenery,
and21:9/32:9 scenery in the first pass. These replace the open choices above.
The first art pass must plan for all five ratios:4:3,16:10,16:9,21:9,32:9.
Use a protected-center widest master or overlapping side panels, retaining
provenance/crop coordinates. At height1440 the32:9 master is5120wide, with the
1920wide4:3 center at x1600;21:9 is3360wide. If a single generation cannot cover
this ratio/resolution, generate overlapping side panels and assemble against
the exact center. Do not silently downgrade ultrawide to a fallback.

Prompt color preservation explicitly and measure corresponding source colors;
prompting alone is not an accuracy guarantee. Retain raw generations, distinguish
any technical assembly, preserve original tint instead of neutralizing it, and
reject/revise warmer or yellower drafts before acceptance.

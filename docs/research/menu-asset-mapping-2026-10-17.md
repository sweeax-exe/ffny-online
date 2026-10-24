# Menu asset mapping — 2026-10-17

Offline structural and visual audit of the user’s own dump. No code changes, build, game run, or asset export was made. This report contains metadata only.

## Evidence and method

- Source: `<data>/extracted/screens/screens.viv`, members `screens/intMain/mainMenu.big` and `screens/intMain/prestart.big`.
- Structure: existing `apt-dump.py` from `/Users/vlad/Code/DefJamFFNY-recomp/scripts/`; SHPX/archive readers in this checkout’s `scripts/hd_assets.py`.
- Visual inspection: existing originals in the local setup Lanczos corpus (recipe directory `b9aa783f1d66d41be59fb97ba25fb86e1e9f73d8ee041263b0ff5df85ceb26f0`). No new image was written.
- APT Image records contain the same numeric identifier used by SHPX four-character names. These names are **not** SHPX directory indices.
- APT Shapes name a model in the embedded `.o` (ELF32 little-endian MIPS resource object). `__RM:::__GP_RM_<shape>_<part>` symbols’ relocation at `+0x48` refers to a `__EAGL::TAR...;SHAPENAME=<image>,1;...` symbol. Following these links maps bitmap fragments to Shapes; following APT PlaceObjects maps Shapes to named clips.
- The fragment categorization below is exhaustive for mainMenu: recursive dependencies of Sprite222 reach all 120 image names, matching all 120 SHPX entries and all 120 corpus aliases.

## Main menu: what 120 textures actually means

The retail main menu is authored at 640×480, 16 ms/frame. Its outer movie has 4 frames; Sprite222 (`mainMenu`) carries the 73-frame scene animation. There are 120 Image, 65 Shape, 40 Sprite, 1 Movie and 1 Morph characters.

**120 = 8 environmental bitmaps + 2 spotlight bitmaps + 110 menu/text/icon fragments.** The 110 comprise 21 door emblems, 27 title fragments, 30 subtitle/tag fragments, 32 description fragments. This is not 120 independent backgrounds.

| Environmental component | SHPX name (directory index) | Source size | APT route |
|---|---|---|---|
| Poster wall, dark | 5 (93),4 (85) | 256×512 +64×512 | Sprite222 → Sprite16 → Shape6 |
| Poster wall, spotlight-lit | 14 (23),13 (16) | 256×512 +64×512 | Sprite222 → Sprite16 → Shape15 |
| Rolling shutter | 18 (47),17 (40) | 256×256 +256×128 | Sprite222 `door`51 → Sprite20 → Shape19 |
| Door surround/right wall | 53 (95),52 (94) | 512×512 +64×512 | Sprite222 → Sprite55 → Shape54 |

The poster-wall dark/light versions are near-identical compositions with different brightness. The Story/Battle/Options/Jukebox/Rewards/Extras/High Scores/Credits artwork and words are already baked into the environmental bitmaps. They need protected masks or deterministic reconstruction during restoration. The two tile pairs must remain mutually registered so the moving spotlight does not reveal differently placed lettering or bricks.

The separate rolling shutter is animated for 93 frames in Sprite51. It also carries 21 bitmap fragments through its `doorLogos` Sprite50. They represent 8 actual emblem states; the 10 labelled frames include `mmOnline` and `mmCreatePlayer` frames without their own new image. Their existence is not evidence of enabled retail menu choices.

Sprite16 has two frames. Its first uses dark wall Shape6 at depths 2 and 6; the second switches the depth 6 occurrence to lit wall Shape15. `spotlight` Sprite9 is a mask with clip depth 7, while `spotlightRim` Sprite12 renders above. Sprite8 and 11 each have 175 frames. Spotlight bitmap names 223/224 are only 8×8; their Shapes/animation supply the spatial mask/rim. A single newly painted flattening would lose these relationships.

Raw source name 53 visibly contains a bright-green doorway region. Likewise title resources contain green filler areas. This audit did not establish whether every such region is unused by mesh UVs, keyed by the runtime, or both. Preserve the existing mapping and verify the real renderer before treating a source bitmap as a complete visible rectangle. Do not paint the green placeholder into a flattened final scene.

### Authored initial placements

Coordinates below are local APT positions, **not native texture pixel offsets**. Outer movie frame 0 places Sprite222 at `(316.2,239.9)`, with x scale≈0.999863. Inside Sprite222:

| Depth | Character/clip | Initial translation |
|---|---|---|
|1|Shape2, untextured base|0,0|
|2|Sprite3 `spotMover`|-358,-227|
|4|Sprite16 `mainMenu` (poster wall/spotlight)|-162.5,0|
|30|Sprite51 `door`|140.6,28.5|
|35|Sprite55 (right wall/door surround)|233.5,0|
|38|Sprite219 `doorText`|143,81|

Within Sprite16, Shape6 starts at `(-3.5,14)` and its second copy at `(-3.5,14.5)`; the lit replacement at depth 6 retains that placement. Spotlight mask and rim begin near `(-331.75,47.9)`, scale 0.9. These values move or change during the scene, so any wide-frame background geometry must handle the authored timeline rather than capture only its first frame.

### Nonenvironmental clips

| Clip | Bitmap count | Purpose |
|---|---:|---|
|Sprite50 `doorLogos`|21|Large selected-mode emblems on the moving shutter|
|Sprite106 `title` → `titles`|27|Selected-mode title, frame-selected variants|
|Sprite161 `subtitle` → `tag`/`tags`|30|Selected-mode subtitle/tag, frame-selected variants|
|Sprite218 `descript` → `text`|32|Selected-mode description, frame-selected variants|
|Shape10 via Sprite11/12|2|Spotlight rim names 223/224|

The menu bytecode explicitly selects the title/tag/description frames using the current language and selection. Their exact wording and typography should be preserved. These are good candidates for deterministic scaling or manual/vector reconstruction, rather than unconstrained image generation.

## Press Start (`prestart`)

The actual retail title movie has 15 bitmap entries, 165 outer timeline frames, 42 characters, 640×480 at 16 ms/frame. It is not `demoStrt`.

| Names (directory indices) | Source size | Character path | Visual role |
|---|---|---|---|
|1 (0),2 (5)|128×512 +512×512|Sprite4 → Shape3|Far skyline/clouds, opaque|
|5 (13),6 (14)|128×512 +512×512|Sprite8 → Shape7|Middle city/rooftop layer, alpha|
|10 (1),11 (2)|128×512 +512×512|Sprite13 → Shape12|Foreground water tower/roof layer, alpha|
|16 (3)|256×256|Sprite18 → Shape17|Shield/graffiti layer; green inner region in raw source|
|19 (4)|512×128|Sprite22 → Sprite21 → Shape20|Def Jam title wordmark with authored animation|
|27 (6),28 (7),30 (8),31 (9),34 (10),35 (11),37 (12)|8×64 through 256×64|Sprite41 →40 →33/39 → Shapes29/32/36/38|Start-sticker/platform/language texture fragments|

This is a strong first restoration pilot: **six scene tiles forming three compositing layers**, with the branding/start sticker held separate. Restore and outpaint each assembled layer with its alpha/silhouette intact. Keep its native seam placement when splitting the work back into replacements. The sky has an opaque black margin; the middle and foreground have authored transparency, so sending them as an undifferentiated flattened screenshot is insufficient.

The background layers are placed on the root at different y translations: Sprite4 `(0,29)`, Sprite8 `(0,102)`, Sprite13 `(0,16)`. They belong to an animated title sequence. Sprite15 has 382 frames (untextured shape animation); Sprite22 wordmark animation has 403 frames; Sprite41 start sticker 30 frames. The stage composition, texture canvases and content bounds differ.

## Shared resources and the limits of reuse

- None of the 8 mainMenu environmental IDs has any alias in another archive in this corpus. The 15 prestart bitmaps likewise have no other aliases. Texture restoration for these screens will therefore not automatically improve the Options/Story/Battle menus. Similar-looking material elsewhere may be a differently lit/cropped/rendered source.
- The only 2 duplicated mainMenu bitmaps are spotlight names 223/224: same IDs as `demoMenu` names216/217 (directory indices38/39). Texture replacements are content-addressed, so these two would affect both retail and demo resources.
- A concrete shared environmental set exists in the rewards/unlock screens: `buyrewards` names1/2 (indices0/5), `unlockchar` names10/11 (indices0/3), and `unlockven` names10/11 (indices0/1). They are the same 256×512 +512×512 brick-wall pair (IDs beginning `6cd17deca345` / `fc7e7ea76a80`). One restoration can cover this trio, subject to geometry/aspect checks.
- Shared paper/modal art also exists: IDs beginning `b791ca1da12e` and `b4c0a0db45b6` (256×256 each) occur in `beloader`, `feloader`, and `intMain/message`. These are paper overlay fragments, visually inspected; they are not a full universal background.
- The `feloader` APT remains resident and owns named popup elements `titleBacker`, `noteBacker`, `messageBacker`, `buttonBacker` and platform button art. These are another reusable UI family, separate from scene environments.
- Alias evidence detects identical source IDs, not all semantic reuse. This audit did not claim every visually similar image is shared, or map every feloader import/overlay instance into every screen.

## Practical implication for a restoration/widescreen plan

1. Pilot prestart’s three scene layers, then mainMenu’s four environmental components; keep original logos/labels/spotlight masks isolated.
2. Build a mapping-aware authoring view: original tile, assembled layer and running-game composition, plus alpha and original UV bounds. The existing corpus gallery can compare/export individual UI entries but it does not infer this hierarchy itself.
3. Restore each component in a stable coordinate system. For mainMenu, derive dark/light wall pairs from the same restored base and preserve baked poster graphics.
4. Outpaint peripheral environment only. A wide PNG alone does not expand the APT Shape or change its UVs; the renderer/geometry path must explicitly expose the extra content while retaining the original 640×480 composition. The other renderer audit owns the implementation decision.
5. Validate an idle frame, each selection/spotlight position, the rolling-door transition, the title intro and the Press Start sticker across aspect ratios. A single still is not enough to validate this scene.

## MainMenu complete texture-to-layer lookup

Source dimensions below are original dimensions from the corpus; the corpus recipe scales outputs 4×, but `width`/`height` are the original sizes. Hashes are source-ID prefixes, not PNG file hashes. Full original/replacement files remain in the local corpus.

| SHPX index | SHPX/Image name | Original size | Layer | Source-ID prefix |
|---:|---:|---|---|---|
|85|4|64×512|environment|`a3ef5488f84a`|
|93|5|256×512|environment|`5b657a938f08`|
|16|13|64×512|environment|`2fca375e9466`|
|23|14|256×512|environment|`44ac477e5bbe`|
|40|17|256×128|environment|`de1deb64a25f`|
|47|18|256×256|environment|`5e6f2fec0b51`|
|65|21|128×32|door emblem|`2e48cf151ce7`|
|70|22|64×128|door emblem|`757992057df3`|
|73|23|128×128|door emblem|`239f61c152ea`|
|74|25|128×64|door emblem|`f41bba879fc1`|
|75|26|64×128|door emblem|`bfb188f53f66`|
|76|27|128×128|door emblem|`b36b549df15b`|
|77|29|128×16|door emblem|`ff8471670aa8`|
|78|30|16×128|door emblem|`79b05c160853`|
|79|31|128×128|door emblem|`c70864f7a3de`|
|80|33|128×32|door emblem|`2170eb15025e`|
|81|34|128×128|door emblem|`a85e9d316451`|
|82|36|64×128|door emblem|`cd4ffadac63f`|
|83|37|128×128|door emblem|`58dbd292b761`|
|84|39|128×64|door emblem|`baebf3aee433`|
|86|40|128×128|door emblem|`18db0581724a`|
|87|42|128×32|door emblem|`f3bd86237ccb`|
|88|43|32×128|door emblem|`f3c9018cb3ee`|
|89|44|128×128|door emblem|`c4c26c3dbdae`|
|90|46|128×64|door emblem|`c8115cdb70a7`|
|91|47|32×128|door emblem|`d55c90a43955`|
|92|48|128×128|door emblem|`b6ad4739ea52`|
|94|52|64×512|environment|`832e9d0b1495`|
|95|53|512×512|environment|`d543121c2fd1`|
|96|56|128×64|title|`a0dc87761e77`|
|97|58|16×64|title|`f77513878825`|
|98|59|128×64|title|`4053bba9f26d`|
|99|62|8×64|title|`dbbe2149d30c`|
|100|63|128×64|title|`31a2c1533364`|
|101|65|32×64|title|`4b3d3464ae13`|
|102|66|128×64|title|`6f4e46b1f669`|
|103|69|64×128|title|`3bb40c1e7fc8`|
|104|70|128×128|title|`08ed4d760461`|
|105|72|64×64|title|`48d5f495e2b2`|
|106|73|128×64|title|`518d32f473f5`|
|107|76|32×64|title|`60ebf0fd420b`|
|108|77|128×64|title|`82bf3b9371f0`|
|109|80|64×64|title|`410c4eb2ba73`|
|110|81|128×64|title|`c76bf6e1b50c`|
|111|83|64×64|title|`94abf6a77f97`|
|112|84|128×64|title|`6c0b40a345d9`|
|113|87|16×64|title|`bdafcac57be8`|
|114|88|128×64|title|`26295580e2e3`|
|115|90|128×64|title|`15415dc869c2`|
|116|93|128×128|title|`a2b5e4b9baca`|
|117|95|32×128|title|`294f0c09995c`|
|118|96|128×128|title|`95d153980ecd`|
|119|99|64×128|title|`16d9e25270cb`|
|0|100|128×128|title|`452f96c338a8`|
|1|102|64×64|title|`d2a88244c0fd`|
|2|103|128×64|title|`f77261012938`|
|3|107|64×64|subtitle/tag|`8fa5e3d2018a`|
|4|108|128×64|subtitle/tag|`baf3912bd63b`|
|5|110|128×64|subtitle/tag|`4a1949e56797`|
|6|113|64×64|subtitle/tag|`0d4f5b93fd4c`|
|7|114|128×64|subtitle/tag|`d0d3a413ed4a`|
|8|116|128×64|subtitle/tag|`d8a6f15f9ba4`|
|9|117|128×64|subtitle/tag|`07caab4499e4`|
|10|120|64×64|subtitle/tag|`7103a48907d5`|
|11|121|128×64|subtitle/tag|`9051365e8d27`|
|12|123|32×32|subtitle/tag|`92276244aed8`|
|13|124|128×32|subtitle/tag|`a1468f50e30a`|
|14|127|64×64|subtitle/tag|`82ca5d17a2aa`|
|15|128|128×64|subtitle/tag|`0787719e7d1c`|
|17|130|64×64|subtitle/tag|`15890bf89456`|
|18|131|128×64|subtitle/tag|`6b68944fa934`|
|19|134|128×32|subtitle/tag|`65b70c57181e`|
|20|135|128×32|subtitle/tag|`4375a27b3879`|
|21|137|32×64|subtitle/tag|`07a5168d8b41`|
|22|138|128×64|subtitle/tag|`7c7b3d0541dc`|
|24|141|64×64|subtitle/tag|`d4dfce5d1dd0`|
|25|142|128×64|subtitle/tag|`be748a8fdf78`|
|26|144|32×64|subtitle/tag|`363297cb437d`|
|27|145|128×64|subtitle/tag|`49452fbeee79`|
|28|148|128×64|subtitle/tag|`729987900bd7`|
|29|150|128×64|subtitle/tag|`2f0447a9c695`|
|30|151|128×64|subtitle/tag|`5dbf8323037d`|
|31|154|128×64|subtitle/tag|`3f5451f33fdd`|
|32|155|128×64|subtitle/tag|`5e31f037d1b2`|
|33|157|128×64|subtitle/tag|`ba2d5999b5ed`|
|34|158|128×64|subtitle/tag|`849e89d4f49b`|
|35|162|128×128|description|`5eda0035f8c4`|
|36|163|128×128|description|`6e88034de166`|
|37|165|128×128|description|`96afb3863d14`|
|38|166|128×128|description|`3e8e25af69a6`|
|39|169|64×128|description|`c78c3b0aa542`|
|41|170|128×128|description|`631e17310934`|
|42|172|128×128|description|`3be03bd11952`|
|43|173|128×128|description|`5671c19dd0d5`|
|44|176|64×128|description|`32a4ad07553e`|
|45|177|128×128|description|`36ebf8e8e779`|
|46|179|128×128|description|`d4f5af6809ab`|
|48|180|128×128|description|`0a5ba5de5a1b`|
|49|183|64×128|description|`5236a9e5b237`|
|50|184|128×128|description|`8ea60588417d`|
|51|186|64×128|description|`f440772e70c6`|
|52|187|128×128|description|`89beb3f67f32`|
|53|190|64×128|description|`21400c6595fb`|
|54|191|128×128|description|`ff6027fba573`|
|55|193|64×128|description|`49b514eff666`|
|56|194|128×128|description|`72139ae44b1c`|
|57|197|128×128|description|`a5f0d023045d`|
|58|198|128×128|description|`3854fc5303fc`|
|59|200|64×128|description|`65623b9c75f7`|
|60|201|128×128|description|`0a8bae2ddb07`|
|61|204|128×128|description|`0f01f709337c`|
|62|205|128×128|description|`3776659c8b5d`|
|63|207|64×128|description|`34c33f072045`|
|64|208|128×128|description|`8d45e60139e3`|
|66|211|128×128|description|`c949d4e238f5`|
|67|212|128×128|description|`be3e8ec30592`|
|68|214|128×128|description|`487518d60f26`|
|69|215|128×128|description|`9e67686cae14`|
|71|223|32×32|spotlight|`ee6a59402047`|
|72|224|32×32|spotlight|`bbd769ec797f`|

## Open technical questions from this bounded audit

- Confirm the intended handling of bright-green source filler in the actual material path before constructing a geometry-accurate offline composite.
- APT tree and material lookup are mapped; the `.o` vertex layouts/texture UVs were not fully decoded here. A simple contact-sheet join must not be presented as an exact game frame.
- Determine which authored layer/Shape should gain side extensions, while hit areas, spotlight positions, animation pivots, logos and text remain registered. This requires the renderer audit and later in-game captures.

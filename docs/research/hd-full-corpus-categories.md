# Full texture corpus: provenance and review categories

This report records a read-only census of the owner's local dump and proposes
categories for the Lanczos corpus/gallery. Counts below describe the existing
`hd-work/inventory.json` snapshot, not a claim that every texture has been seen
in the running game. No game artwork or archive payload is included here.

## Census and provenance

The snapshot contains 24,267 image records: 24,266 supported image aliases and
one unsupported image. Supported records have 18,971 distinct `source_id`
values. An alias is one archive path plus SHPX image index; several aliases can
contain identical texture data. There are also 24 font entries, 94 movie entries
and seven archive traversal errors. Only 40 distinct originals were exported by
the initial limited inventory run; the inventory count is not an export count.

`scripts/hd_assets.py:90` preserves nested BIG archive membership as a `::`
chain. Its image reader at line 136 preserves the SHPX image index, name,
dimensions, type, source identity and decode errors. This provenance must survive
deduplication: one output image should retain every source alias, and the gallery
should allow filtering any of those aliases. The `source_id` describes source
texture data; a decoded identity or an archive name alone does not prove a
runtime replacement key. Runtime capture/correlation remains a separate check.

Normalize separators and case for classification, split on `::`, and use the
first component as the actual top-level archive. Treat member paths such as
`d:/DJV2/assets/textures/...` as opaque provenance, not filesystem destinations.
Use identities for output filenames. The current `category()` at line 226 has
improved since this inventory was generated, so stored categories need
reclassification before producing the full gallery.

## Proposed category rules

Apply these rules in table order. Counts are **image aliases**, including the
unsupported row; they sum to 24,267. These are proposed rules, not a claim that
the script currently implements this exact table.

| Gallery category | Ordered provenance rule | Aliases |
|---|---|---:|
| UI | Top-level `screens/` first, regardless of embedded fighter or venue names | 13,144 |
| Crowds | Top-level `assets/icrowd.viv`, `cincrowd.viv`, or `v2cm.viv` | 238 |
| Logos | `movies/load0.xsh`, or final member `nis_logo.xsh` | 5 |
| Effects | Top-level `assets/blingfx.viv` or `decalfx.viv` | 8 |
| Animated textures | Top-level `assets/texanim.viv` or `assets/cinema/` | 239 |
| Character customization | Top-level `assets/v2cp_*.viv` | 5,526 |
| Fighters and character models | Top-level `assets/v2ip*.viv`, or final member beginning `v2ip_` | 286 |
| Environment maps / review | Final member beginning `v_env` or `c_env`, before the venue rule | 652 |
| Venues and interiors | Top-level `assets/v2bg*.viv`, or final member beginning `v2bg_` | 3,338 |
| Props, weapons and trophies | Top-level `assets/props.viv`, `v2wp.viv`, or `trophies.viv` | 392 |
| HUD and pause | Top-level `assets/misc.viv`, final member beginning `hud` or `pause` | 295 |
| UI | Top-level `assets/misc.viv`, final member beginning `popups` | 18 |
| Other / needs review | Everything unmatched | 126 |

UI therefore totals 13,162 aliases. Preserve secondary tags for `_cin`
cinematic character/environment variants, source bundle, SHPX image name,
dimensions, alpha presence and decoder format. Deduplication can put one image
in several filters; avoid assigning a single misleading category to a shared
identity. Logos embedded within a larger screen bundle need a reviewed tag,
rather than classifying the whole bundle as logos from a substring.

The environment-map category is a filename-based review group. Its name does
not establish whether every image is colour, a mask, a lookup table or a normal
map. Likewise, a venue texture can be an atlas or non-tiling detail. Do not infer
wrap padding or RGB/alpha treatment solely from these gallery categories.

## Semantic checks behind the rules

* `assets/v2cp_0.viv::createplayer_parts.xml` defines creator templates, body,
  face, hair, accessories and tattoos using `V2CP_*` references. These are
  player customization assets. They are not the general crowd corpus.
* The extracted `fighter.xml` maps Blaze to `V2IP_113A` (lines 790–797) and
  other named fighters to `V2IP_*`. A model atlas still needs visual/UV review
  before labeling individual areas as face, clothing or skin.
* `icrowd01.xml` contains explicit crowd model and idle/excited definitions;
  `cincrowd.xml` contains crowd member and body/skin/facial-hair fields with
  `V2CM` references. This supports the crowd group.
* `venue.xml` maps `V2BG_01A` to Hunt's Point Scrapyard (lines 7–8),
  `V2BG_07A` to Red Hook Tire Co. Inferno (119–121), and `V2BG_100` to the
  clothing store (487–488). The venue group includes Story interiors.
* `assets/shops.viv` mixes `V2BG_*` interiors, `V_ENV*` images and `V2IP_*`
  shopkeeper models. Classifying this whole archive as one category loses
  useful meaning. Unresolved `ht01` through `ht39` and `loader.xsh` belong in
  review until inspected; their precise role is not established here.
* `assets/v2wp.viv::weapon.xml` has weapon data and material fields. The prop
  group includes weapons and trophies rather than claiming that all these
  surfaces are venue textures.

The remaining review group also contains `assets/tint.xsh` and
`assets/v2ga.viv::tape.xsh`. Keep their original names visible. `ta_*` images
within `texanim.viv` and `cinema/sc*.viv` are animated texture assets, not
automatically decoded full-motion video.

## Unresolved image and inventory limits

The sole unsupported SHPX image is
`assets/misc.viv::C_ENV00A.xsh`, index 0, image name `ec1`, type `0x65`,
32 × 32, swizzled. Local inspection and the guest format table indicate a
16-bit payload, but have not established its channel encoding or semantic use.
The archive name and byte distribution are insufficient to select a colour,
bump or normal decoder. Do not upscale or silently substitute a guessed decode.

The primary [EA-Graphics-Manager support table](https://github.com/bartlomiejduda/EA-Graphics-Manager)
lists type 101 (`0x65`) as preview/export supported. This is a lead for a separate
decoder investigation, not validation against this FFNY asset. Keep an explicit
gallery row with provenance, dimensions, type, unsupported status and no invented
preview. Its absence from the generated PNG corpus must remain visible.

The seven traversal errors are the large top-level audio archives
`music0.viv`, `music1.viv`, `music2.viv`, `sfx1.viv`, `sfx2.viv`,
`speech1.viv`, and `speech2.viv`. They exceed the script's 128 MiB traversal budget
(`scripts/hd_assets.py:18,251`), rather than failing an image decoder. Preserve
the exact inventory error rows in the gallery/status page. An archive-directory
or bounded signature audit is needed before asserting that these contain no
additional images; do not describe this census as exhaustive disc coverage.

## Fonts, UI timelines and video boundaries

The 24 font entries consist of 10 TTF and 14 XFN assets. TTF outlines offer a
separate route to sharp glyph rasterization. XFN is a separate bitmap-font
format; it is not covered by the SHPX reader. Keep fonts in a distinct inventory
section and do not report their glyphs as successfully bulk-upscaled textures.
Runtime-generated font atlases require their own support/correlation.

The 94 MAD entries are separate full-motion-video records: 87 packed movie
entries and seven loose entries. They need a video decode/upscale/re-encode and
playback/audio-synchronization workflow. A file called `bg.asf` encountered in
a cinematic archive has `SCHl` audio magic, so extension alone must not classify
it as Microsoft ASF video. Screen `.apt`/`.const`/`.o` assets describe UI/Flash
timelines and can contain vector/text content; a texture census does not cover
all their visual primitives. Dynamic movie frames and render targets should
remain outside the static SHPX replacement corpus.

## Gallery review behavior

Show the original and Lanczos result with nearest-neighbor zoom, checkerboard
alpha preview and dimensions/type/provenance. Filter by logical category and
secondary tags, and count unique images separately from aliases. Provide local
concern flags for alpha edges, seams, text/logo legibility, atlas boundaries,
palette changes, faces/clothes, unexpected colour and suspected data textures.
Allow an exclusion flag without deleting provenance. A concern remains a review
record until assessed; category alone is not approval for a texture treatment.

List unsupported images, skipped archives, fonts and movies alongside supported
images, with explicit statuses. These boundaries let the owner review the full
census while the supported Lanczos batch completes.

# Pre-fight loading screens: bounded offline mapping — 2026-10-20

This source-only audit maps the three named resources `battle/matchLoa.big`,
`battle/oldMLoad.big` and `battle/tips.big` from the user's own
`screens/screens.viv`. It records each textured shape part, native canvas,
source ID, APT bounds, packed vertices, UV matrix and root/sprite placements.
It writes metadata only: no texture pixels or archive bytes. No game was
started, stopped or inspected; no build, source-code edit, art generation,
pack change or installation was performed.

Replay the exact geometry/alias audit with:

```sh
python3 logs/ui-restoration-20261020/audit-prefight-loading-uv.py
```

The output is
`logs/ui-restoration-20261020/prefight-loading-uv-metadata.json`. The audit
reuses the ELF decoder from `logs/ui-restoration-20261017/uv-audit/audit_pilot_uv.py`
and joins the existing `all-menu-texture-aliases.csv`. Full APT display-list
and ActionScript evidence was independently produced with `scripts/apt-dump.py`
for all three movies using `--frames --actions`. Vertex decoding is inferred
as described in `menu-battle-background-shape-audit-2026-10-17.md`; UV endpoints
are the decoded corner coordinates, not a GPU capture. Resource hashes are in
the JSON.

## Mapping

| Movie / shape part | Source / canvas / alpha | APT and decoded geometry | UV used | Interpretation |
|---|---|---|---|---|
| `matchLoa` shape 2, part 0, image 1 | `7a5d6ce31c47aec8fdd9bbe11c9561a7d8cb833ded7058ab05cb229fad76d185`; 1024×512; alpha 255 | `[0,0,640,480]`; one full-stage quad | `[0,0]` to `[640,480]`; U/V clamp | Opaque loader background plate. The native image has unused right/bottom regions; the mesh samples only its upper-left 640×480 rectangle. Do not stretch the sampled region to fill a wider frame. |
| `matchLoa` shape 10, part 0, image 9 | `11b71080a3890bb179763083899791bd5ad7537e73a4f9d6f79a3c18979fae91`; 1024×512; alpha 0–205 | `[0,0,640,480]`; separate full-stage quad at root depth 17 | `[0,0]` to `[640,480]`; U/V clamp | Translucent full-stage companion. Keep it as its own layer with original alpha and order over the plate; do not merge it into the opaque plate. |
| `matchLoa` shape 6, part 0, image 5 | `1a968d8c758014cd610129b4f7f835626cdaee74ceea23bd60e928851da78155`; 8×8; alpha 255 | APT bounds `[327.95,-35,333.95,-29]` | approximately `[0,0]` to `[6,6.003]` | Tiny marker outside the visible full-stage composition; not a background tile. Leave it alone. |
| `oldMLoad` shape 58, part 0, image 57 | `05af223223a5b76f2fe1b25a5e5f1f4914c6c52b1885ff2a86880a6970031151`; 512×256; alpha 0–255 | `[45,237,593,441]` | approximately `[0,0]` to `[512,204]` | Large translucent foreground composite. It is separate from the scenic plate; preserve it and its companion. |
| `oldMLoad` shape 58, part 1, image 56 | `24264085f6ffc3343ae92a6b52c9722f01cefa35072f7b4c53372f4c5e99fbc7`; 64×256; alpha 0–255 | Same `[45,237,593,441]` shape; right-side companion | approximately `[0,0]` to `[36,204]` | Completes the shape-58 composite. The two parts are adjacent material regions, not interchangeable duplicates. |
| `oldMLoad` shape 61, part 0, image 60 | `3a3391187097863fc346959ba1cc2a1982384f247290056c791b68ad87170ca1`; 256×256; alpha 0–255 | `[0,0,640,480]`; full-stage quad nested in sprite 62 named `lightingClip` | `[0,0]` to `[640,480]`; U/V clamp | Static lighting/effect layer, not the base venue scenery. Keep its clip/sprite relationship and alpha intact. Clamp means the oversized UV extent does not prove a repeating pattern. |
| `tips` shape 3, part 0, image 2 | `a021919997cc9a50b377c257f6afed472242e7281e3f9ba8ca691525bba8ea77`; 512×512; alpha 255 | `[0,0,640,480]` | `[0,0]` to `[512.031,480]` | Opaque left/main wall tile. |
| `tips` shape 3, part 1, image 1 | `cc76ce61906858b8da3d522063f4a8d2e3cce1de51d7375a4db519f110676afb`; 128×512; alpha 255 | Same full-stage shape | `[0.031,0]` to `[128,480]` | Opaque right tile. This joins part 0 at the authored x≈512 seam. Preserve both native tiles and their approximately shared quantized seam; retain the rows below y=480 as source padding. |
| `tips` shapes 10, 14, 16, 18 | `6fb6cfa15da35cf51ec6db54a87abb8ec088f08924eced597cfac5dfc08c4017` (128×64), `e4e694772d273bf98fa98d54fd7b693e9d795005788d903651690dada402c6ff` (32×32), `cad8d51e92373bb1094c9e344439fcb376ee5d827edb10cdd2b298b9d297f581` (32×32), `0e907085ba4fc42ef9601d799f032dbe821931361abfa3cb6b14d9f8d2dbb523` (32×32), all alpha 0–255 | Small animated or sprite-local pieces, not full-stage shapes | Full decoded vertices and matrices are in JSON | These are foreground sticker/glyph pieces; preserve their source IDs and animation. They do not join the wall. |

All full `encoded_xyz`, UVs and material matrices are included in JSON. The
alias sheet reports exact source canvas and alpha extrema; the geometry audit
does not independently decode or export pixel data.

## Live layers and scope

`matchLoa` is a 120-frame 640×480 movie. Its frame-0 root keeps portrait clips
and the named `p1Portrait`/`p2Portrait`, `venueName`, `p1Logo`/`p2Logo`, a
separate mask clip, and the shape-10 full-stage companion. The loader ActionScript
reads venue and character indices and loads venue/logo content. Preserve those
live portrait, logo, venue-label, mask and alpha-overlay layers above the opaque
plate. The tiny image-5 shape is not evidence of a side-fill source.

`oldMLoad` is also 120 frames at 640×480. Its root retains `bkg`,
`matchHeader`, `fighters`, venue name/text, and `lightingClip`; ActionScript
contains `loadVenue`, `loadLogos`, and `loadPortraits` calls and ends with
`Game.StartGame()`. The match venue scenery therefore comes from a dynamic
venue load; the static shape-61 full-stage quad is the clipped lighting/effect
layer. Keep the shape-58 foreground composite, dynamic fighter portraits/logos,
text and lighting clip on their existing layers. This audit maps their authored
bounds and order; it does not resolve loader-parent transforms or prove which
loader is used during a particular match.

`tips` is a 2-frame 640×480 movie. Its two-part opaque wall is shape 3. The root
places separate `tipsTitle`, `venueText`, `charText`, and animated `infoSticker`
objects above it. Preserve the title, dynamic venue/character copy and sticker
animation as live foreground content. The sticker sprite includes a text clip,
button, platform variants and frame-by-frame position changes.

The existing alias inventory contains no explicit `screens/story/...load...`
or Story loading alias to add to this bounded batch. `bootLoad` and `demoLoad`
are front-end/attract loader resources, not an explicitly identified Story
pre-fight loading screen, so they were not substituted by name alone. A source
mapping cannot establish which pre-fight loader is actually chosen in-game;
runtime capture remains necessary before calling any candidate active-path
coverage complete.

For wide restoration, widen the scenic plate/background around the centered
640×480 composition while preserving the existing live layers and exact tile
joins. `matchLoa` has an explicit static opaque base and translucent overlay.
`tips` has a clear two-tile opaque wall. `oldMLoad` has no mapped static venue
wall in these APT assets; investigate the dynamic venue-load path before making
a replacement background. Do not flatten portraits, text, tips, sticker,
lighting, masks or alpha effects into any replacement.

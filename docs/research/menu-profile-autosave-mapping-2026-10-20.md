# User ID and autosave UI source mapping (2026-10-20)

This is a source-only geometry/alpha audit of `screens/options/userID.big` and `screens/memoryCard/autosaveMessage.big`. It uses the same RefPack/ELF/APT readers, packed-vertex decode, model scales and material UV matrices as `logs/ui-restoration-20261020/audit-phone.py` and `audit-venue-frame-20261020.py`. The source alias inventory is `logs/ui-restoration-20261017/all-menu-texture-aliases.csv`; original PNG hashes/dimensions are read from the corpus named in the structured metadata. Output: [profile-autosave-metadata-20261020.json](../../logs/ui-restoration-20261020/profile-autosave-metadata-20261020.json). Coordinates are resource-local and UV points retain the slight fractional overhangs from packed vertices; they are not rounded during decoding.

## User ID page

Image 87 (`8665c0…`) is the only opaque artwork in this scope. Its 1024×512 source is sampled at exactly UV [0,640]×[0,480] by shape 88 (part 0), whose APT bounds are [0,0,640,480]; root frame 0 places shape 88 at (0,0). It is therefore a real 640×480 page backdrop. The source itself is a composite: brick wall, centered blank paper panel, and pale wall emblem. The remaining right/bottom source area is smear/padding and is not part of the used UVs. Keep the paper and emblem intact if extending the wall for a wider page; the image is not a clean wall-only layer.

The other seven unapproved ≥128×128 userID sources are nested panel/card textures, not evidence for a full-page background. Shape 92 uses image 91 as a 187×256 panel and image 90 as a second 187×72 strip on the same APT bounds [17,7,204,335]. Shape 128 uses image 127 across its square face and image 125 over only UV Y≈0..116 on the same bounds [−146,−186,146,186]. Shape 3 uses image 2 cropped to Y≈0..97 on bounds [−14.9,−20.8,273.1,76.2]; shape 7 uses image 6 cropped to Y≈0..109 on [−10,−13,246,96]. Shape 121 uses image 120 across 256×256 UVs stretched to the complete 640×480 bounds. These shapes have alpha range 0–255; preserve their masks and pairings. Do not infer a page background from any of these isolated shapes.

Two source IDs in this movie are already approved and are omitted from the metadata and recommendations. The sixteen-item pending batch has no source-ID overlap with either target movie.

## Autosave message states

The root movie labels frame 0 `ps2`, frame 1 `gc`, and frame 2 `xbox`. These root frames place different `bkg` sprites; the labels below come from those root tags, not from guessing at the character numbers. PS2's language alternatives are nested inside its `bkg` sprite, so the French variant is not a separate platform state.

| Root state and nested source grouping | Used geometry and source-pixel mapping | Mapping |
|---|---|---|
| PS2 root frame 0 → `bkg` character 50, nested frame 0 (English); same `bkg`, nested frame 2 (French). Images 42+41 and 47+46 respectively. | English uses shape 43 with APT bounds [0,0,527,400]. Part 0/image 42 decoded XY corners are (0,0), (0,256.005859), (512.010864,0), (512.010864,256.005859); UV corners are identical. Part 1/image 41 geometry starts at X=1.029297, Y=256.005859 and ends X=512.010864, Y=400; its UV corners are (0.029297,0.005859), (0.029297,144), (511.010864,0.005859), (511.010864,144). French shape 48 has the same APT bounds. Its part 2/image 47 uses XY/UV corners as part 0; part 3/image 46 starts at XY (0,256.005859) and UV (0,0.005859), ending at XY (512.010864,400) and UV (512.010864,144). | The nested `bkg` frame 0 places shape 43 and live `message` character 45. Nested frame 2 removes that message, swaps in shape 48, and places message character 49. The source pixels show English versus French slot labels. These are PS2 language variants. Keep each background/strip and message variant paired. |
| GC root frame 1 → `bkg` character 64, nested frame 0; message-only replacement at nested frame 2. Images 60+59. | Shape 61 APT bounds [0,0,512,375]. Part 0/image 60 corners: XY (0,0), (0,256.027222), (512,0), (512,256.027222); UV corners are identical. Part 1/image 59 corners: XY (0,256.027222), (0,375), (512,256.027222), (512,375); UV (0,0.027222), (0,119), (512,0.027222), (512,119). Root places `bkg` at scale 1.021484375. | Source pixels show warning card art with `Slot A`/`Slot B` lettering baked in. Nested frame 2 only changes live message character 62 to 63; the shape 61 artwork remains. |
| Xbox root frame 2 → `bkg` character 70, nested frame 0. Image 67. | Shape 68 bounds [0,0,467,313]. XY/UV corners are (0,0), (0,256.031860), (467,0), (467,256.031860). | Self-contained Xbox-console warning/paper artwork; the source bounds stop at X=467 and the live message is character 69. |

All seven source images have 0–255 alpha. Exact decoded points, transforms, and UVs are in the JSON. Integer coverage shorthand is misleading here: packed geometry has fractional XY endpoints and image 41's material has a −1 X translation, so its UV begins at 0.029297 while the quad begins at X=1.029297. The French strip has no X translation. The broad green-looking areas in previews are transparent RGB/keyed padding; use alpha and decoded UV crops, not preview background color, to judge coverage. Do not substitute the A/B panel for the PS2 paired-card screen or treat a cropped strip as a standalone backdrop.

## Live content to preserve

The userID root places dynamic `titleText`, `userDisplay`, `enterID`, `menu`, and `shadow` objects, plus a clipped `scrollMover`; its descendants include the editable `userIDName`, profile/userID field, completion text, stat names/values, scroller and stat bars. Keep these live text/control layers above the source artwork and preserve their clip/scroll transforms. The autosave root has `bkg`, platform-specific `titleText`, `message`, `infoSticker`, and button artwork. The `message`, `infoStickerTextClip`, `infoStickerButton`, and `buttonArt` descendants carry runtime prompt/button content; preserve localization and platform selection. Lettering already painted into the warning/card source images is part of those source tiles and should remain exact too.

The APT character graph proves resource and placement structure; this audit does not establish which userID menu state is visible at runtime, or approve a new art treatment. No code, art, build, or game run was changed or produced.

## Reader provenance

The decoder ran inline. `logs/ui-restoration-20261020/map-profile-autosave-assets-20261020.py`
is a later transcription of that successful read, not the original executed
file, and has not been rerun from its saved path. Approval filtering, visual
classification and nested-timeline enrichment were separate inline steps.
The saved metadata and this report retain the observed results; the helper is
provided as an unexecuted reconstruction, with that limit explicit.

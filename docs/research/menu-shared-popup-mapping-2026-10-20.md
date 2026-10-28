# Shared popup material mapping (2026-10-20)

## Scope and method

Read-only inspection of `screens/feloader.big`, `screens/beloader.big`, and `screens/intMain/message.big`. Used `all-menu-texture-aliases.csv`, the pinned HD corpus cited in `audit-phone.py`, and the ELF/vertex/APT decoders from `audit_pilot_uv.py` and the 2026-10-20 audits. The generated coordinate-only record is [shared-popup-metadata-20261020.json](../../logs/ui-restoration-20261020/shared-popup-metadata-20261020.json); the reproducible local reader is [map-shared-popup-assets-20261020.py](../../logs/ui-restoration-20261020/map-shared-popup-assets-20261020.py). No game run, code/runtime change, or image generation was performed.

All seven distinct material sources at least 128×128 are listed below. Dimensions and alpha counts refer to each source `originals/<ID>.png` in the corpus. UV bounds are source-pixel coordinates computed from compressed vertex positions and the 4×4 material matrix; shape bounds are APT coordinates. Full vertices, matrices, hashes, exact per-movie source aliases, and named APT placements are in the JSON.

| Source ID | Source canvas | Alpha counts (0 / partial / 255) | Observed role |
|---|---:|---:|---|
| `b4c0a0db45b63a646f10e58659f548b0fab579633811b431c645f0344d813d38` | 256×256 | 671 / 7,970 / 56,895 | Actual paper, left panel of the shared wide paper shape. |
| `b791ca1da12e6fda255d91d9cf02d774145707b9a07592d18d55f8622cf9a14f` | 256×256 | 3,900 / 30,179 / 31,457 | Actual paper, continuation/right torn edge of that shape. |
| `807a290c7dec5edf4728faec4925c1112c4a069e788f79a1d15c936f51127769` | 256×128 | 10,240 / 4,040 / 18,488 | Actual paper strip/header texture, left portion of feloader shape 32. |
| `4aebe3a6d599cbe5f176e545d4d8f28de10d632703c51be89ff127196408b037` | 128×128 | 10,217 / 1,783 / 4,384 | Small illustrated/outlined paper control artwork; feloader shape 32 right cap. No legible text was identified. |
| `efdbaec32b07abd34f907688e17b907bed6731f5d3e27446a36fa04fd740a489` | 128×128 | 319 / 5,019 / 11,046 | Actual layered paper/sheet art, used by a 128×112 quad. |
| `b1cdc4d1ce950b939e320ab74b50426c47551b2471cc19d7b5a9418c3028373e` | 128×128 | 77 / 4,627 / 11,680 | Protected graphic: illustrated phone/message control. |
| `acf9559da275598298f9a0a1d52f2580b01eeb41823d68a027dd62b88c800980` | 128×128 | 76 / 4,627 / 11,681 | Protected graphic: distinct illustrated/lettered sticker or note control. |

The icons are artwork with transparency, not blank masks. None of the seven is a text-only texture. Dynamic text is represented separately in APT clip placements (`titleText`, `noteTextClip`, `messageTextClip`, etc.); do not merge it into an art reconstruction. The first two sources are the repeated paper IDs already noted in the task; they have actual paper pixels and partial/transparent alpha, so they are neither masks nor text.

## Shared paper geometry and repeat/tiling evidence

The shared paper is a single shape made from **two adjacent quads**, not one texture stretched across the full 407×310 APT bounds and not a repeated-UV texture:

| Movie / shape | Part 0: source | Part 0 UV pixels | Part 0 object quad | Part 1: source | Part 1 UV pixels | Part 1 object quad | APT bounds |
|---|---|---|---|---|---|---|---|
| feloader / 77 | b4c0… (256²) | x 0.0293–256.0205, y 0.0193–256.0231 | x 5.0293–261.0205, y −0.9807–255.0231 | b791… (256²) | x 0.0205–151.0503, y 0.0193–256.0231 | x 261.0205–412.0503, y −0.9807–255.0231 | [5, −1, 412, 309] |
| beloader / 6 and 128 | same | same | same | same | same | same | same |
| message / 33 | same | same | same | same | same | same | same |

The join is at object x≈261.02: part 0 occupies the left 256 pixels and part 1 the right ≈151 pixels. The source UVs stay within each 256×256 canvas to within ~0.03 px of quantization/float rounding. They do not wrap across an edge or repeat. The shape's APT bounds extend to y=309 although the decoded paper quads end near y=255; that difference is in the APT shape bounds and does not justify filling or scaling the lower area from these textures. Both parts are independently referenced on the same shape, preserving the seam and source alpha.

Feloader shape 32 is also two adjacent quads on a header/control strip: part 1 uses `807a…` over object x=0–343.0444, UV x=0–256.0333/y≈0–96; part 0 uses `4aeb…` over x=343.0444–420, UV x≈0.0444–77/y≈0–96. The shape's APT bounds are [0, −23, 420, 73]. This is a real two-source joint; the UV x endpoint 256.0333 is a 0.0333 px overrun, not evidence for a wrapped full-canvas repeat. The remaining 128px control graphics occupy their own 118×128 quads, with APT bounds [0,0,118,142].

The other large-source uses are: `efdba…` on feloader shape 66 (UV 0–128.0035 × 0–112; APT [0,0,159,112]) and beloader shape 245, identical geometry; `b1cdc…` on feloader shape 42 and beloader shape 221 (UV 0–118 × 0–128.0115; APT [0,0,118,142]); `acf955…` on feloader shape 58 and beloader shape 237, same bounds. No one of those sources occurs in message.big. `all-menu-texture-aliases.csv` gives the canonical source identities and per-movie XSH alias/index/name; the metadata preserves each occurrence.

## Named hierarchy, without screen-composition inference

The four supplied clip names are actual `PlaceObject.name` values, not names inferred from texture appearance. In feloader they are placed by character 168 frame 0; in beloader by character 98 frame 0 and again by 218 frame 0; in message they are placed by root movie character 0 frame 0. Their character IDs are:

| Movie parent | `paper` | `titleBacker` | `noteBacker` | `messageBacker` | `buttonBacker` |
|---|---:|---:|---:|---:|---:|
| feloader 168 | 78 | 81 | 84 | 87 | 81 |
| beloader 98 | 7 | 10 | 13 | 16 | 10 |
| beloader 218 | 129 | 132 | 135 | 138 | 132 |
| message root 0 | 34 | 37 | 40 | 43 | 37 |

Each named target is a one-frame Sprite. `titleBacker` and `buttonBacker` deliberately reference the same character in each loader/movie. The material-backed paper art is separately named `paper`; its sprite leads to the large split paper shape (feloader shape 77; beloader shape 6/128; message shape 33). The two-frame outer popup timelines are feloader 168, beloader 98/218, and message root 0. This establishes source clip and shape relationships; it does **not** claim which clips are active together in a runtime state or how their final composition looks. Exact PlaceObject matrices and full decoded timeline records are in the JSON.

## Source identity and scope notes

The CSV's exact aliases for the repeated paper pair are b791… (`feloader.xsh` index 39 name `75`; `beloader.xsh` index 3 name `126`; `message.xsh` index 7 name `31`) and b4c0… (`feloader.xsh` index 40 name `76`; `beloader.xsh` index 4 name `127`; `message.xsh` index 8 name `32`). Other candidates are `feloader.xsh` indices 21/22/24/31/35 for 807a/4aeb/b1cdc/acf955/efdba respectively, and `beloader.xsh` indices 39/47/51 for b1cdc/acf955/efdba. See the metadata for the corresponding four-character texture names, hashes, matrices, alpha counts, shape indices, and UV vertex order. No runtime-use conclusion, accepted-patch claim, or artwork approval is implied by this mapping.

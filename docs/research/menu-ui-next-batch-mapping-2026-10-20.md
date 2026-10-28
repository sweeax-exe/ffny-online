# Next Menu/UI batch source mapping — 2026-10-20

This is a source/geometry audit for the candidate Story gym and shared popup
assets. It does not claim runtime visibility, texture quality, or approval.
Candidate filtering and dimensions come from
`logs/ui-restoration-20261017/all-menu-texture-aliases.csv`. The full coordinate
record is
`logs/ui-restoration-20261017/uv-audit/next-menu-batch-metadata-20261020.json`.

## Story gym geometry

`screens/story/gym.big` has one four-part paper-like shape 50. Its APT bounds
are `[7, 25, 344, 479]`. The four source textures form a 2×2 mosaic with a
vertical join at x≈263.039 and horizontal join at y≈281.015. Thus names 49/48
are the upper row, and 47/46 are the lower row; each row is not a separate
shape or independent placement.

| XSH name | Source ID | Alias index; source canvas | Part | Geometry vertices `(x,y)` in emitted order | Corresponding UV vertices `(u,v)` in source pixels |
|---|---|---|---:|---|---|
| 49 | `9276f96f3218687f6587b3bfff8a1f848a435cd39cb896cdf247cce6069523be` | 61; 256×256 | 0 | `(7.012695,25.025879)`, `(7.012695,281.014893)`, `(263.039063,25.025879)`, `(263.039063,281.014893)` | `(0.012695,0.025879)`, `(0.012695,256.014893)`, `(256.039063,0.025879)`, `(256.039063,256.014893)` |
| 48 | `c263c20b240c4ec872e1c13e6b1fd0e7bf3489b7b5220989589f253d0cf80166` | 60; 128×256 | 1 | `(263.039063,25.025879)`, `(263.039063,281.014893)`, `(344,25.025879)`, `(344,281.014893)` | `(0.039063,0.025879)`, `(0.039063,256.014893)`, `(81,0.025879)`, `(81,256.014893)` |
| 47 | `05006f7ec4c10ac0910b104e5c6776c625444726194983e57ab08782116fda0c` | 59; 256×256 | 2 | `(7.012695,281.014893)`, `(7.012695,479.058472)`, `(263.039063,281.014893)`, `(263.039063,479.058472)` | `(0.012695,0.014893)`, `(0.012695,198.058472)`, `(256.039063,0.014893)`, `(256.039063,198.058472)` |
| 46 | `f72dcdc83ade2fd316565909e95b1ff523449286350169861ade8898a1251716` | 58; 128×256 | 3 | `(263.039063,281.014893)`, `(263.039063,479.058472)`, `(344,281.014893)`, `(344,479.058472)` | `(0.039063,0.014893)`, `(0.039063,198.058472)`, `(81,0.014893)`, `(81,198.058472)` |

These are near-identity source-to-geometry placements. The right-column
textures have a 128-pixel-wide source canvas, but only x≈0–81 is sampled. The
lower row samples y≈0–198 from each 256-pixel source. Endpoints slightly above
integer edges are retained from packed-vertex decoding; they are not evidence
of a deliberate wrap.

Shape 60 has APT bounds `[-11, 1, 307, 343]` and four parts, including the
requested image names 59 and 57. Image 59 is part 0, at geometry x≈−10.980–
245.015, y≈1.005–257.041, with UV x≈0.020–256.015, y≈0.005–256.041. Image 57
is part 2, directly below it at geometry x≈−10.980–245.015, y≈257.041–343,
with UV x≈0.020–256.015, y≈0.041–86. The other two parts are image 58 (part 1)
beside 59 and image 56 (part 3) beside 57; together these narrow right-side
tiles extend the overall shape to x=307. The full shape width of 318 pixels
therefore includes those neighboring tiles. Image 59 itself spans about 256
geometry pixels and samples about 256 UV pixels; it is not stretched from 256
to 318. A texel-space art study of 59+57 should be kept distinct from the
complete four-part APT composition.

Shape 64 has APT bounds `[-1, 0, 316, 87]` and two parts. Image 63 is part 0,
at geometry x≈−0.964–255.014, y=0–87, with UV x≈0.036–256.014, y=0–87. Image
62 is part 1, immediately to its right at geometry x≈255.014–316, y=0–87,
with UV x≈0.014–61, y=0–87. So image 63 alone is not the full APT strip.

## Shared popup texture names

`popups.xsh` and `popupsF.xsh` are entries in `assets/misc.viv`, not in a
movie `.big` containing an APT. Each pair of aliases points at the same source
ID:

| Name | Source ID | `popups.xsh` index | `popupsF.xsh` index | Source canvas |
|---|---|---:|---:|---:|
| `texP` | `0dd32b612b4047d3d475b4954973f686d6b4ad8f1fa8f4311281334d9b9cf7a6` | 0 | 0 | 256×256 |
| `texN` | `3f74caf51ce9218f2e2929deb9a28f6b553281d29e3e0fef1701c17727e3b5f7` | 2 | 2 | 256×64 |
| `texT` | `684eb85fd7ac720a3d423b0cc1f4dd84e5c58efd08feb98f2ff144189b437900` | 1 | 1 | 256×32 |
| `texF` | `1ff57a715094afdf7804d5b6d764c8df6f277c9176c346a59218a4f03af942cf` | 3 | 3 | 256×32 |

A bounded scan of 8,781 nested `.big` object files in `screens/screens.viv`
found no `SHAPENAME=texP`, `texN`, `texT`, or `texF` reference. This gives no
APT crop or geometry for the four standalone names. It does not prove there is
no consumer elsewhere in the game.

## Reproducibility and limits

The saved script
`logs/ui-restoration-20261017/uv-audit/audit-next-menu-batch-20261020.py` is a
transcription of the successful inline audit, not the original inline source.
It uses the existing `audit_pilot_uv.py` archive reader, `scripts/apt-dump.py`
movie reader, source alias CSV and RefPack/object symbol readers to regenerate
the metadata JSON. Packed-vertex to geometry conversion uses the same inferred
formula as the existing audit helper; the emitted record preserves packed XYZ,
model scale, material matrix, decoded geometry/UV vertices, source IDs and
archive hashes for review. Re-running the saved script later completed without
a game launch, build, or code change. No APT-to-screen-state or runtime claim is
made.

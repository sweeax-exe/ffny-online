# Story/shop paper panel source mapping — 2026-10-20

This bounded offline audit maps each requested paper-panel image to its complete
APT shape, including every companion part. It records source aliases, source
canvas dimensions, emitted geometry and sampled UV coordinates. It does not
identify runtime visibility or approval. No image bytes were exported or
inspected.

The machine-readable record is
`logs/ui-restoration-20261020/story-shop-paper-shapes.json`; rerun
`python3 logs/ui-restoration-20261020/map-story-shop-paper-shapes.py` to
reproduce it. The script reads only the ten named `.big` entries from the
user's own dump, using `audit_pilot_uv.py`'s archive and object readers and
`scripts/apt-dump.py`'s movie reader. Geometry decoding follows the existing
helper formula. JSON preserves full vertex coordinates, packed XYZ, material
matrices, archive hashes and source aliases. Tile order below is by ascending
material part number, matching the vertices in the JSON.

Bounds are `(left, top, right, bottom)`, rounded here to three decimals. UV
bounds are in source pixels. The reported source canvas is the alias canvas;
UV endpoints may be fractional and are retained exactly in JSON.

| Group / exact XSH source | APT shape; full bounds | Part | Image | Alias index | Source ID | Canvas | Geometry bounds | Used UV bounds |
|---|---|---:|---:|---:|---|---:|---|---|
| story/barber · `screens/screens.viv::screens/story/barber.big::barber.xsh` | 114; `[9, 18, 342, 480]` | 0 | 113 | 8 | `3c955f8fa2a709577cab8b49c5c0fd06f1f0ed2f11a3dcf83f09a458d38aa94c` | 256×256 | `[9.018, 18.047, 265.017, 274.043]` | `[0.018, 0.047, 256.017, 256.043]` |
| story/barber · `screens/screens.viv::screens/story/barber.big::barber.xsh` | 114; `[9, 18, 342, 480]` | 1 | 112 | 7 | `d847c1a86a3b8c746c97f5adae19b1e584d55350c0831e959786c3def598f01d` | 128×256 | `[265.017, 18.047, 342, 274.043]` | `[0.017, 0.047, 77, 256.043]` |
| story/barber · `screens/screens.viv::screens/story/barber.big::barber.xsh` | 114; `[9, 18, 342, 480]` | 2 | 111 | 6 | `b420752f90cf9e127a0628ae795f6b707f4254b2963c7b0db1a6ea4be9459a49` | 256×256 | `[9.018, 274.043, 265.017, 480]` | `[0.018, 0.043, 256.017, 206]` |
| story/barber · `screens/screens.viv::screens/story/barber.big::barber.xsh` | 114; `[9, 18, 342, 480]` | 3 | 110 | 5 | `3a8166731bdcab427a0cdbbc203d9be3f74a7b27a6a22840db554d801d9e8e16` | 128×256 | `[265.017, 274.043, 342, 480]` | `[0.017, 0.043, 77, 206]` |
| story/clothing · `screens/screens.viv::screens/story/clothing.big::clothing.xsh` | 145; `[0, 0, 336, 455]` | 0 | 144 | 17 | `4689ac4b3e098ba284bb974c30516cf1f940ece1bf80d56ff5061e2cfe8679aa` | 256×256 | `[0, 0, 256.02, 256.049]` | `[0, 0, 256.02, 256.049]` |
| story/clothing · `screens/screens.viv::screens/story/clothing.big::clothing.xsh` | 145; `[0, 0, 336, 455]` | 1 | 143 | 16 | `829ef0ca41403019ca8c08f79103020c70fc01792ae56f1e5cc9fb103ccf029a` | 128×256 | `[256.02, 256.049, 336, 455]` | `[0.02, 0.049, 80, 199]` |
| story/clothing · `screens/screens.viv::screens/story/clothing.big::clothing.xsh` | 145; `[0, 0, 336, 455]` | 2 | 142 | 15 | `c9fb303ded108a732c62910f59a2acfceb22ec68b264873b1b0fec5987884cac` | 128×256 | `[256.02, 0, 336, 256.049]` | `[0.02, 0, 80, 256.049]` |
| story/clothing · `screens/screens.viv::screens/story/clothing.big::clothing.xsh` | 145; `[0, 0, 336, 455]` | 3 | 141 | 14 | `05b6ff1621e07370710740081845de131129de32bc7fb47eb271b76c8e3cc63f` | 256×256 | `[0, 256.049, 256.02, 455]` | `[0, 0.049, 256.02, 199]` |
| story/jeweller · `screens/screens.viv::screens/story/jeweller.big::jeweller.xsh` | 136; `[0, 0, 332, 455]` | 0 | 135 | 13 | `42ecdd4416b15edf1799d158415af2e2ffae88fb4bc7cfa3224a55d9750c673c` | 256×256 | `[0, 0, 256.011, 256.049]` | `[0, 0, 256.011, 256.049]` |
| story/jeweller · `screens/screens.viv::screens/story/jeweller.big::jeweller.xsh` | 136; `[0, 0, 332, 455]` | 1 | 134 | 12 | `e65e6d847cda3d6cc3ad4629c89d5b9112556a54630042db50b4786c170c9c49` | 128×256 | `[256.011, 256.049, 332, 455]` | `[0.011, 0.049, 76, 199]` |
| story/jeweller · `screens/screens.viv::screens/story/jeweller.big::jeweller.xsh` | 136; `[0, 0, 332, 455]` | 2 | 133 | 11 | `09a069a8872871d844c875d57a6ba49a8c93cd81fc28f7c4b98d7d7b36a9d4a6` | 128×256 | `[256.011, 0, 332, 256.049]` | `[0.011, 0, 76, 256.049]` |
| story/jeweller · `screens/screens.viv::screens/story/jeweller.big::jeweller.xsh` | 136; `[0, 0, 332, 455]` | 3 | 132 | 10 | `9e282c773858f4480aace53e3277499ffa52970107b0db3f2fe2899f20066591` | 256×256 | `[0, 256.049, 256.011, 455]` | `[0, 0.049, 256.011, 199]` |
| story/wardrobe · `screens/screens.viv::screens/story/wardrobe.big::wardrobe.xsh` | 110; `[0, 0, 335, 449]` | 0 | 109 | 6 | `4b4ca81894b6779fa55841a6215a3e82600a6c5739d992753c9c53aeb829cffb` | 256×256 | `[0, 0, 256.035, 256.016]` | `[0, 0, 256.035, 256.016]` |
| story/wardrobe · `screens/screens.viv::screens/story/wardrobe.big::wardrobe.xsh` | 110; `[0, 0, 335, 449]` | 1 | 108 | 5 | `5beeacf3d854d9e2d4acf7270fcedc0ebd252b9ae6c60e6f0e7f0963ae5b1bd4` | 128×256 | `[256.035, 256.016, 335, 449]` | `[0.035, 0.016, 79, 193]` |
| story/wardrobe · `screens/screens.viv::screens/story/wardrobe.big::wardrobe.xsh` | 110; `[0, 0, 335, 449]` | 2 | 107 | 4 | `8c3f65b6b161acf6ae8e4bd81f6517dbb24e35a665b763529e5d85417f3a5f55` | 128×256 | `[256.035, 0, 335, 256.016]` | `[0.035, 0, 79, 256.016]` |
| story/wardrobe · `screens/screens.viv::screens/story/wardrobe.big::wardrobe.xsh` | 110; `[0, 0, 335, 449]` | 3 | 106 | 3 | `113c38f06caf4d595a1644a053a21f5ba43ba3ce29ce3efb328986df0a4e8241` | 256×256 | `[0, 256.016, 256.035, 449]` | `[0, 0.016, 256.035, 193]` |
| story/chooseStyle · `screens/screens.viv::screens/story/chooseStyle.big::chooseStyle.xsh` | 5; `[7, 24, 340, 481]` | 0 | 4 | 9 | `51de154c49a1a7f99cef4fd7e2f5f943027bbb2c60ea8a7beb75a8c0b44d29c0` | 256×256 | `[7.014, 24.015, 263.01, 280.016]` | `[0.014, 0.015, 256.01, 256.016]` |
| story/chooseStyle · `screens/screens.viv::screens/story/chooseStyle.big::chooseStyle.xsh` | 5; `[7, 24, 340, 481]` | 1 | 3 | 7 | `8f08f4076346d6b54aa1376ac6c9abfd78cbe465df4b5f2faf4393534a4919d7` | 128×256 | `[263.01, 24.015, 340, 280.016]` | `[0.01, 0.015, 77, 256.016]` |
| story/chooseStyle · `screens/screens.viv::screens/story/chooseStyle.big::chooseStyle.xsh` | 5; `[7, 24, 340, 481]` | 2 | 2 | 3 | `9198d94f3acaf3ff56598a963dff2f51f30ac647be846f9d9ab83720a904ea24` | 256×256 | `[7.014, 280.016, 263.01, 481]` | `[0.014, 0.016, 256.01, 201]` |
| story/chooseStyle · `screens/screens.viv::screens/story/chooseStyle.big::chooseStyle.xsh` | 5; `[7, 24, 340, 481]` | 3 | 1 | 0 | `ed7984a2e525cb262ffb4648107f530eb542d8981886f96efea541ff7fea4630` | 128×256 | `[263.01, 280.016, 340, 481]` | `[0.01, 0.016, 77, 201]` |
| memoryCard/saveWarn · `screens/screens.viv::screens/memoryCard/saveWarn.big::saveWarn.xsh` | 6; `[0, 0, 468, 314]` | 0 | 5 | 18 | `2cdf2154a82c1498c80cdf1d1564474816a36e43653ed991d1f34c2fdc533ead` | 256×256 | `[0, 0, 256.052, 256.007]` | `[0, 0, 256.052, 256.007]` |
| memoryCard/saveWarn · `screens/screens.viv::screens/memoryCard/saveWarn.big::saveWarn.xsh` | 6; `[0, 0, 468, 314]` | 1 | 4 | 13 | `f3597a2ec08bf63bec9c16b25f679878ba39e2ff71bbcc6be4723cc9ac3dd39a` | 256×256 | `[256.052, 0, 468, 256.007]` | `[0.052, 0, 212, 256.007]` |
| memoryCard/saveWarn · `screens/screens.viv::screens/memoryCard/saveWarn.big::saveWarn.xsh` | 6; `[0, 0, 468, 314]` | 2 | 3 | 9 | `451a0ce1b0a115d2939e8189d20cc24996525d646a34ee92066a0c726b6d2fe3` | 256×64 | `[0, 256.007, 256.052, 314]` | `[0, 0.007, 256.052, 58]` |
| memoryCard/saveWarn · `screens/screens.viv::screens/memoryCard/saveWarn.big::saveWarn.xsh` | 6; `[0, 0, 468, 314]` | 3 | 2 | 4 | `6246747fcaf5a1f02ccc73cf8f4b8ec173827ee6a66aa9f5fe915a780ad711b9` | 256×64 | `[256.052, 256.007, 468, 314]` | `[0.052, 0.007, 212, 58]` |
| memoryCard/memWarn · `screens/screens.viv::screens/memoryCard/memWarn.big::memWarn.xsh` | 5; `[-8.5, -4.5, 651.45, 487.45]` | 2 | 4 | 15 | `d2094ee2a6ddac17a58438a1a1c5f6862b277b3ab2566353db7a522aa1a4de75` | 256×256 | `[104.015, 79.058, 360.041, 335.016]` | `[0.015, 0.058, 256.041, 256.016]` |
| memoryCard/memWarn · `screens/screens.viv::screens/memoryCard/memWarn.big::memWarn.xsh` | 5; `[-8.5, -4.5, 651.45, 487.45]` | 3 | 3 | 10 | `3dbf1dd82d3b0a7097320f122f7765f033d54d8ce19633b946149bc08315354b` | 256×256 | `[360.041, 79.058, 528.023, 335.016]` | `[0.041, 0.058, 168.023, 256.016]` |
| memoryCard/memWarn · `screens/screens.viv::screens/memoryCard/memWarn.big::memWarn.xsh` | 5; `[-8.5, -4.5, 651.45, 487.45]` | 4 | 2 | 5 | `d0933021359bc336c22a2a0f655cd5c9f58f408d3ab4610b39a0a7c3c735a733` | 256×64 | `[104.015, 335.016, 360.041, 398.025]` | `[0.015, 0.016, 256.041, 63.025]` |
| memoryCard/memWarn · `screens/screens.viv::screens/memoryCard/memWarn.big::memWarn.xsh` | 5; `[-8.5, -4.5, 651.45, 487.45]` | 5 | 1 | 0 | `07b76ebe7e94a292af17b651e4fad1df66501ce9bbd3c1a62423a5578a33a792` | 256×64 | `[360.041, 335.016, 528.023, 398.025]` | `[0.041, 0.016, 168.023, 63.025]` |
| options/conSetup · `screens/screens.viv::screens/options/conSetup.big::conSetup.xsh` | 2; `[66.5, 43, 578.5, 437]` | 0 | 1 | 0 | `3c6c26662cc78b60fee88be2e38040bc1d3665ec7ce30c64b4de9ab3d80312b7` | 512×512 | `[66.522, 43.049, 578.5, 437]` | `[0.022, 0.049, 512, 394]` |
| story/tattoo · `screens/screens.viv::screens/story/tattoo.big::tattoo.xsh` | 174; `[6, 20, 341, 480]` | 0 | 173 | 29 | `ecbe66241e12ee8d44fde39365de1e3b2110a565ef10e94e91cd99c8e57d331e` | 256×256 | `[6.036, 20.039, 262.036, 276.035]` | `[0.036, 0.039, 256.036, 256.035]` |
| story/tattoo · `screens/screens.viv::screens/story/tattoo.big::tattoo.xsh` | 174; `[6, 20, 341, 480]` | 1 | 172 | 28 | `5fb43775e0716d265ead3c1cb9cbe4c895bbec468dcbf2dd1195fcbd2381b0e8` | 128×256 | `[262.036, 20.039, 341, 276.035]` | `[0.036, 0.039, 79, 256.035]` |
| story/tattoo · `screens/screens.viv::screens/story/tattoo.big::tattoo.xsh` | 174; `[6, 20, 341, 480]` | 2 | 171 | 27 | `a4229c4967cc5787b2fb3684132f54d2c7225085af969a7b6837afdac0b046bb` | 256×256 | `[6.036, 276.035, 262.036, 480]` | `[0.036, 0.035, 256.036, 204]` |
| story/tattoo · `screens/screens.viv::screens/story/tattoo.big::tattoo.xsh` | 174; `[6, 20, 341, 480]` | 3 | 170 | 26 | `df5dbee169f6f626569e6307a43ec4f68f121f5cf92d1038e0d07ceace2c6f14` | 128×256 | `[262.036, 276.035, 341, 480]` | `[0.036, 0.035, 79, 204]` |
| story/trophies · `screens/screens.viv::screens/story/trophies.big::trophies.xsh` | 5; `[299.95, 25, 628.95, 483]` | 0 | 4 | 15 | `16ec969961dd106f17f463f5a612d92a50a98371c019b127fc3741ad1f3a11f5` | 256×256 | `[299.964, 25.058, 556.013, 281.003]` | `[0.014, 0.058, 256.063, 256.003]` |
| story/trophies · `screens/screens.viv::screens/story/trophies.big::trophies.xsh` | 5; `[299.95, 25, 628.95, 483]` | 1 | 3 | 10 | `8880df656be458f030c36d3487793efee9c1e5e7bf20c34a89aed2dbd5462a05` | 128×256 | `[556.013, 25.058, 629.027, 281.003]` | `[0.063, 0.058, 73.077, 256.003]` |
| story/trophies · `screens/screens.viv::screens/story/trophies.big::trophies.xsh` | 5; `[299.95, 25, 628.95, 483]` | 2 | 2 | 5 | `eb0025c40b3b9101b79677413db631916d4bba9ced7604af32d003dbdd5829c5` | 256×256 | `[299.964, 281.003, 556.013, 483]` | `[0.014, 0.003, 256.063, 202]` |
| story/trophies · `screens/screens.viv::screens/story/trophies.big::trophies.xsh` | 5; `[299.95, 25, 628.95, 483]` | 3 | 1 | 0 | `500941cc66482c8f3aeb962ebf7b902c3877c219be68533858ba3e16a9374751` | 128×256 | `[556.013, 281.003, 629.027, 483]` | `[0.063, 0.003, 73.077, 202]` |
The barber, clothing, jeweller and wardrobe shapes are four-part mosaics. In
part order, barber lays out 113/112 above 111/110. Clothing, jeweller and
wardrobe lay out 144/142 above 143/141, 135/133 above 134/132 and 109/107
above 108/106, respectively. The right-column companions are 128×256 canvases
and sample 77–80 pixels of their width. `chooseStyle` lays out 4/3 above 2/1;
its right column samples 77 pixels from each 128-pixel-wide source. `saveWarn`
and `memWarn` each have four textured geometry parts: a two-column upper panel
and a 64-pixel-high message strip below. In `saveWarn`, image 5 is the left
panel (part 0) and image 4 the right panel (part 1); the lower strip spans
468×58 geometry pixels. In `memWarn`, images 3/4 are parts 3/2. Their paper
geometry spans about 424×319 pixels within the larger APT bounds, and the
right column samples about 168 pixels from the 256-pixel-wide source canvases.
The `memWarn` object also has RM symbols 0 and 1, but both point to the
viewport-matrix constant and neither has a corresponding vertex buffer; the
bounded audit records them in `non_geometry_symbols`, outside the textured
geometry tile table. `conSetup` image 1 is the sole textured geometry part in matched shape 2
(512×394 used within a 512×512 source canvas). Tattoo and trophies each map
to a four-part shape; their complete tile order and sampled bounds are listed
above.

No image identity was inferred from a texture label: each source ID and canvas
comes from the alias CSV row for the exact `.big`/`.xsh` resource and image
name. Object and APT SHA-256 values in the JSON identify the parsed inputs.

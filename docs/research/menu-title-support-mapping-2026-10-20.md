# Title and support UI source mapping — 2026-10-20

Read-only source-corpus check for the remaining title shield/effect and menu support tiles. Corpus: the local `setup-lanczos` corpus at `/Users/vlad/Library/Application Support/DefJamPR19-20/data/hd-work/setup-lanczos/b9aa783f1d66d41be59fb97ba25fb86e1e9f73d8ee041263b0ff5df85ceb26f0/corpus.json`. Asset identity below is by corpus ID and source alias; no artwork is copied into this report.

## Findings

The seven requested PressStart records resolve to the `screens/intMain/prestart.big::prestart.xsh` table, with the following corpus indices, sizes, and source IDs:

| XSH name | XSH index | Size | Corpus ID | Active alpha bounds (pixel coordinates) |
|---:|---:|---:|---|---|
| 27 | 6 | 8×64 | `0f1af3c3ef0983a2fe575032cb24658b126863ab18dde2bf62bddce420a82cd7` | (0,8)–(5,40) |
| 28 | 7 | 128×64 | `d8c462cab04613a7f2e6f87512c2e260604d60ea5707bf3315d28c2404e6258d` | (3,0)–(128,40) |
| 30 | 8 | 64×64 | `6fb2b16bd9c24e3b76f7cc865e193484c01c295cfd8263e9d7550b9c5d9a77e6` | (0,0)–(38,40) |
| 31 | 9 | 128×64 | `97cf4616382ce4d7d848cf562f251ec852c16c8a0b4a7785c4de55fcff35cb9b` | (0,0)–(128,40) |
| 34 | 10 | 64×64 | `6d1d40f427f2924edf7462e07c4d2ed98888d466df99cc0f4753787ac82b9bfa` | (0,0)–(64,40) |
| 35 | 11 | 128×64 | `39d44c6bbbb508f3b3faf08d812ab336bd89adf005d5f2f34b8be4e74d385bed` | (0,0)–(128,40) |
| 37 | 12 | 256×64 | `7bf3ddab13a8a55e4d9deffb1f627a983c8a5bde1d06ad6315308017467faacf` | (0,0)–(244,40) |

All seven have alpha range 0–255; the visible region ends at y=40 and most canvases retain substantial transparent vertical padding. The prestart APT/ELF and timeline decode identify four exact sticker shapes:

| APT shape | Source names, left-to-right | Shape bounds | Material UV bounds (pixel coordinates) | Static reading |
|---:|---|---|---|---|
| 29 | 28 + 27 | 134×40 | 28: x=0..128.013; 27: x=0.013..6; both y=0..40 | “Press START”; 128-pixel first tile plus a 6-pixel right edge. Name 27's 8×64 canvas is sampled over only x≈0..6. |
| 32 | 31 + 30 | x=−12..155, y=0..40 | 31: x=0.004..128.004; 30: x=0.004..39; both y=0..40 | French “Appuie sur START”; first tile has a −12 local left overhang, then a 39-pixel second segment. |
| 36 | 35 + 34 | 183×40 | 35: x=0..128.002; 34: x=0.002..55; both y=0..40 | “Press START button”; 128-pixel lead and 55-pixel continuation. |
| 38 | 37 | x=−29..215, y=0..40 | x=0.025..244, y=0..40 | French “Appuie sur la touche START” in one 244×40 sampled region; source canvas is 256×64. |

Each pair is evidenced by the same APT shape's two `__RM` materials and contiguous geometry, not by assembling images from their file widths. All UVs sample y=0..40; material x offsets and decoded quad edges are in the static metadata. The authored APT bounds differ from the quad's left edge for shapes 32 and 38, so retain the negative overhang. Sprite 33 shows shape 29 on frame 0 then replaces it with shape 32 on frame 1. Sprite 39 shows shape 36 on frame 0, removes it, then shows shape 38 on frame 1. Sprite 40 labels frames `xbox`, `gc`, `ps2`: frame 1 places sprite 33 at (−65,−14); frame 2 (`gc`) has no sticker placement; frame 3 places sprite 39 at (−93.5,−13). Sprite 41 scales sprite 40 (1.5× initially, then about 1.2×) and main frame 134 places that named `startSticker` at (321,262). This supports four authored platform/localization variants with the exact groupings above; runtime selection remains dependent on the movie's frame/action behavior.

The requested name 16 is `prestart.big::prestart.xsh`, index 3, 256×256, ID `4807e175bb7531ea7d2cc928ef8ccce87f9e7f76bf584e678ecda270a8865c46`. It is the green radial effect texture, alpha range 0–255 with full-canvas nonzero-alpha bounds. Shape 17 samples it over a 640×480 quad with identity UV material matrix and UV corners (0,0), (0,480), (640,0), (640,480); the timeline places it via sprite 18 at main depth 21, identity transform, on frame 0. This is the effect image, not the shield artwork.

For `screens/intMain/support/mainMenuLogosIcons.big::mainMenuLogosIcons.xsh`, requested IDs are names 11/14/17/20/23/26 at indices 0–5, all 256×256. IDs respectively: `e9ad5bba3390127d63d4a4431020a81d2d8d0685d9d69ce133d2eacf0fddd3f2`, `14c02269ec3523a3e7067aee18140055e27dabfb166702c84b5741c2be8bbb33`, `a6e7c14ecee42e628d8be37f8b050a2f84b2958fc7cad86c66f5b0b3a37f7d28`, `af5cb7be674fe608f4b9812ff5252960294fd9869b6a13357aaa3e957438da5c`, `0024896ced50f0fb089245a8d26bd6e226dabd7827db4cc6afd1b9a3616a3d85`, `9f93608748ab0b38fdb0560eb51ebc470da5c79c1c8b10769ce55e4be17245e0`.

RGBA decoded-original byte comparisons against every `mainMenu.big::mainMenu.xsh` corpus record found no exact pixel duplicate for any of the six support IDs. Visuals identify them as a shield/crest (11), fist (14), lightning fist (17), menu/screen icon (20), record/deck (23), and warning triangle (26). All are 256×256. Their alpha bounds / ranges are: 11 `(0,0)–(178,169)` / 0–212; 14 `(2,2)–(150,137)` / 0–255; 17 `(2,2)–(189,201)` / 0–255; 20 `(2,2)–(148,162)` / 0–255; 23 `(2,2)–(182,137)` / 0–255; 26 `(2,2)–(164,156)` / 0–255. Treat them as independent originals/variants; do not substitute an approved mainMenu emblem merely because it looks related. The support `.big` contains the XSH image library and no paired APT/ELF object, so it does not itself define shape quads or placements. The `mainMenu.big` object does reference its own same-number XSH names for 14, 17, 23 and 26 (shapes 15,19,24,28 respectively), but these are separate corpus records with different dimensions and no byte-identical pixels; names 11 and 20 are not referenced as SHAPENAME materials in that object. Do not carry those mainMenu object placements over to the support texture set as proven geometry. This comparison is strictly exact RGBA equality at equal dimensions; transformed, recolored, or cropped relations were not tested.

## Scope and next read-only step

The existing `logs/ui-restoration-20261017/logo-audit/metadata.json` contains the prestart shape, vertex, UV, asset-alpha, and timeline decode, and the shared utility is `logs/ui-restoration-20261017/uv-audit/audit_pilot_uv.py`. Metadata is static source evidence; it does not prove the runtime-selected frame or a rendered result. The support image library lacks a paired object/timeline, so geometry and placement for those six images remain undefined by that resource itself.

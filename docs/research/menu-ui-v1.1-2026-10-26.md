# Fire-escape transition geometry repair — 2026-10-26

## Fire-escape rectangle: source geometry repair

The supported dump's `screens/trans/t1.big::t1.o` already contains the misplaced
quad. This is independent of PNG decoding, asynchronous replacement uploads,
depth state, the widescreen shader and side-fill choice. The previous Windows
draw audit established an uncovered region above the first left scenery tile.
This session independently read the original object and reproduced the gap on
the current release-base Mac build, with no texture packs.

Shape 11/part 1, bitmap 9 (`51ebe2dcf268…`), has an identity texture matrix,
but its four vertices decode to x512..1024/y512.042..1024.084. The other nine
parts leave the first x0..512/y0..512.042 cell empty. The object's overall
bounding box ends at x641, so that quad also exceeds its parent bounds.
The source image is the upper-left stairwell artwork; its lower/right
neighbors belong to the same continuous composition.

`src/patches/menu_geometry.c` subtracts 4096 and 1656 from that quad's encoded
x/y coordinates. It joins the next row's encoded y exactly, without a float
round-trip. The identity UV matrix then samples the intended first tile.
The entry hook is `sub_00109F80_enter`: DynamicLoader's ELF parser, whose
input descriptor at ECX holds the raw header at +0x10. The repair runs before
section fixup, relocation and vertex-buffer upload. It changes guest memory;
the dump and replacement images are unchanged.

The hook first checks the known ELF/section layout, then the whole object's
13,089-byte FNV-1a fingerprint (`208bd986`, repaired `abe1d4d6`). Original
object SHA-256 is
`ec5dd8d50a085e36a2758a728b7a588f37d4f0d5ffe8d5fbe02c34d9059db8ff`.
Other objects and repeated loads are harmless. `RECOMP_MENU_TRANSITION_REPAIR=0`
restores the original geometry for comparison; `RECOMP_MENU_GEOMETRY_TRACE=1`
prints at most 32 parser entries. Normal play logs one repair when it loads.

This is a source-informed correction, not a claim of a newly captured retail
console comparison. Native no-pack captures demonstrate that it closes the
reported hole, and trace positions independently confirm the corrected quad.


The original art-review records and export recipes are retained in the private
authoring archive. Source-only restoration procedures live in Modding Tools.


## Earlier mapping and diagnostic controls (2026-10-20–23)

The affected retail movie is `screens/screens.viv::screens/trans/t1.big::t1.xsh`, not `screens/test/trans1.big`. Static APT decode found a 640×2000 stage, Shape 11 at (0,0), and a 641×2533 shape bounding box. All ten SHPX bitmaps form this scene; the five initially reported runtime tiles were only a subset. Static shape-local bounds were inferred from compressed vertices, not measured screen coordinates:

| Bitmap / source prefix | Canvas | Part | Shape-local rectangle |
|---|---|---:|---|
| 10 / `90c3ba5f36d9` | 512×512 | 0 | x0–512, y512–1024 |
| 6 / `0d9299ca3f85` | 512×512 | 4 | x0–512, y1024–1536 |
| 4 / `8327612c6a9f` | 512×512 | 6 | x0–512, y1536–2048 |
| 2 / `8736c571e654` | 512×512 | 8 | x0–512, y2048–2533 (bbox clip) |
| 8 / `dd4feccde8c4` | 256×512 | 2 | x512–641, y0–512 |

The other siblings are bitmap1/`2f7823f2ca85`, 3/`a4a02a3f4424`, 5/`0a57126a9840`, 7/`b870dce8d1fd` (256×512 each), and 9/`51ebe2dcf268` (512×512). Bitmap9/part1 decoded to x512–1024,y512–1024 despite identity texture translation and UV1–2. A texel-space continuous reconstruction did not prove runtime geometry. `clip2` hosts `trans/t1`; `panDown` moves it from y480 toward `-g_iSize`, with inherited placement defined by the host ([transition reference](m4a-fe-transitions.md)).

Early Mac controls reproduced the rectangle with Faithful-only 16:9 and RC1 4:3. At 4:3 placement and side replay return before wide processing, excluding side replay as a necessary cause. A no-pack 16:9 capture also reproduced it. One synchronous Faithful shot showed a complete staircase, but wall-clock capture phase shifts prevented claiming an async-loading fix. The 32:9 run `logs/run-20261020-100730.log.err` captured title/autosave/settled menu in 13 frames and missed the pan; its restored save and absence of crash/watchdog/ICALL faults do not establish transition coverage.

Windows serial 4:3 controls reproduced the hole with no packs, managed packs, synchronous loading, disabled depth testing and forced alpha blending. Fixed consecutive-flip dumps resolved the ordinary `PB_BATCH_DUMP=shot` one-frame lag. In `20261023-193721.log.err`, f1358's first left tile `0x82813880` starts at x≈0,y216.1 (line7909); its right sibling `0x82994080` starts at x512 at the same y (7924), and the narrow strip ends there (7939). The initial surface copy precedes zero clear0xF3 at7577. Full-screen passes at7635/8220 have color mask0 (7642/8227); clear0x1 at8257 is depth-only. No color-writing draw fills x0–512,y0–216. This established missing coverage, excluding black pixels in that leading tile and later color overdraw as explanations for that frame. Receipts: `../logs/transition-controls.json`, `transition-state-controls.json`, `transition-fixed-flips.json` in the Windows validation workspace.

Batch bounds do not prove final pixel ownership, sampler/mip selection or per-pixel UV. Clip lines apply to subsequent batches until changed; probe hits show triangle coverage rather than final color. The source geometry repair above supersedes the earlier open-cause conclusion; no depth bypass, forced blend state, tile cover artwork or hardcoded renderer workaround was retained. It does not claim a captured retail-console comparison.

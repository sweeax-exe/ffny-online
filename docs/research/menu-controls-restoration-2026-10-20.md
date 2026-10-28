# Gym and controls restoration; linked review navigation — 2026-10-20

Vlad approved the previous fifteen popup/profile/autosave studies (22 source
IDs), D172/UI. The freeze adds 130 pins, bringing the catalogue to 473 files
and 114 reviewed source IDs: 96 unconditional, eight Mode Select conditional,
ten transition conditional. All previous 343 pins are unchanged. An isolated
22-tile source-shaped export is pixel-identical and uninstalled. Profile wall
v3 is approved artwork; no speculative wide runtime registration was added.
The earlier sixteen-item/19-source batch remains pending under D170/UI.

## New batch: fifteen studies, twenty-two source textures

Private folder: `logs/ui-restoration-20261017/pilot-v1/controls-v1-20261020/`.
The page at port51738 starts with this batch. Only the Gym folded paper and
shared popup paper use new generated detail. Thirteen studies retain Faithful
HD: the Gym display and rail, eight separate menu button emblems, and three
shared dark popup strips. The small emblems preserve original typography and
silhouettes; no invented strokes or generative sharpening are accepted.

[The source audit](menu-ui-next-batch-mapping-2026-10-20.md) establishes the
Gym shapes. Shape50 is a337×454 texel mosaic:49/48 above47/46, right column81
pixels and lower row198. Shape60 is a complete318×342 four-part display
(59/58 above57/56). Shape64 is a317×87 two-part rail (63/62). Three narrow
companion tiles were added after checking every part; the initial partial
survey is retained as history. Image59 is not stretched to318 pixels; the
extra62 pixels come from image58. These are integer texel previews, with the
fractional UV and geometry endpoints separately retained in the private audit.
The four standalone popup textures occur under both popups/popupsF aliases;
they are four unique IDs. No APT consumer/placement has been established.

Two built-in imagegen calls bring the session total to144. Exact prompts,
reference PNGs, original raw outputs and their hashes, masks, native IDs,
assembly code and results remain private with the batch. The final assembly
uses tile-aware4×Lanczos, original alpha, and only Gaussian-sigma4 fine L
variation at0.16 strength, clamp1.25L and weighted mean removal. Native a/b
color and broad lighting remain authoritative. Source-edge/alpha masks,
16HD-pixel active-crop guards and explicit Gym header/printed-band exclusions
protect typography, folds, seams, silhouette and unused storage. Final RGB
change is at most four levels. Raw silhouette/alpha/grade changes are excluded.

All nineteen original large sources plus three companion sources produce
22 full-storage4× candidates. Rebuilding each mosaic from these exact tile
outputs matches its review draft. All22 alpha planes and protected graphics,
guards and padding are exact. All473 approved pins and2,439 earlier PNGs are
unchanged. Seventy-five isolated aspect pairs retain original graphic scale;
these are not captures or proof of runtime placement. New artwork is
UNAPPROVED, uninstalled, and absent from the approved catalogue/export table.

## Review navigation

D173/UI implements the requested mouse interaction in reusable authoring
resources `review_navigation.mjs` and `review_navigation.css`. It follows the
fighter workshop's shared pan and the model viewer's scroll/linked camera
convention. Scroll zooms about the cursor; left or right drag pans. A comparison
shares normalized scale and offsets across reference, candidate and wipe,
including unequal responsive pane sizes. Fit, double-click or Home/0 resets;
+/− and arrow keys also work. Source changes reset framing. Background/view
changes retain it. Bounds and wipe layers move with their images. The tall
transition's existing animated crop remains inside the navigation transform.
Comparisons stay paired at the owner's662px desktop width; small phones stack.

The module covers all105 current images in46 groups, including prior artwork,
alpha previews, full-height joins and raw history. Future dynamically inserted
images are included. No artwork bytes, approvals or persistent workshop notes
are changed by navigation. The8768 authoring writer is untouched.

Browser verification:60 original/Faithful/channel cases and75 aspect pairs
loaded; actual mouse wheel and drag produced identical transforms in all
three reference/draft/wipe viewports. Eleven legacy comparison groups linked,
Fit/source reset and wipe preservation passed; the transition animated crop
and wipe still render. No horizontal overflow or console errors at662×970.
Private `browser-verification.json` and actual page screenshots record it.
Four focused Node math tests cover cursor anchoring, unequal-size pan, zoom
limits without drift and inverse zoom. Twenty-two game metadata/export/agent
tests and all473 integrity pins pass. Authoring regression:67 tests and12
subtests pass across the main run plus the two HTTP tests retried outside
the restricted loopback sandbox. No game/runtime/build/save activity.

## Source and private handoff

Authoring source bundle: `research/menu-ui-controls-20261020/`; reusable viewer
resources are packaged under `src/defjam_tools/resources/`. Private additive
archive: `<data>/scratch/menu-ui-controls-v1-popup-approved-20261020/`.
353files/55,243,978bytes; manifestSHA
`1a97a2ff071cd6c2b0631ac315c989b0df433625026a543d2272b35f7e552d58`.
Earlier pending popup/profile and other immutable archives remain required
bases. Source publication is still blocked by GitHub credentials: CLI token
invalid; macOS Git keychain access failed last turn. Source commits stay local
until sign-in is restored. Do not claim a push or PR62 description update.

Next: owner feedback on the new15 and previous16 studies, then a bounded
visual/source audit of remaining Story/widgets; integrate only approved
components when serial game checks are available. Keep the launcher-redesign
game running. Ladder full native-pan approval and Mode Select's specific
right-border condition still need playtest evidence.

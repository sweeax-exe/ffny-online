# Extras menu restoration — 2026-10-20

**Later same-day update (D164/UI):** Vlad approved all six studies.
`extras-approval-20261020.json` pins51files; the catalogue now has277pins and
80reviewed source IDs(72approved+8conditional). A separate nine-tile4x component
`menu-ui-extras-test-v1` is mapped and exported locally; active packs and the
running game were left unchanged. Native acceptance remains pending. Earlier
draft findings below are retained as history.

Vlad resumed Menu/UI restoration after accepting the working HD/widescreen
menus. Original palette, restrained cleanup, centered scenery, 4:3 through
32:9, exact transparency and continuous joins remain the art direction.
This batch adds six **unapproved review studies**, spanning nine native tiles.
It does not change the tested packs, game runtime, defaults or approvals.

## Current integration checked

PR [62](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/62) at `65882a1`
already carries PR [73](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/73)'s
pack-list fix. Combined toolkit pin `dd63e17` merges widescreen `20e7ba7` and
settings `72bec86`. A 1023-character setting/loader limit replaces the earlier
95/255 limits; rejected overlong settings are preserved. PR73 remains open for
main. Do not duplicate the settings change or replace the combined toolkit pin.

The implemented pipeline exports 39 restored tiles, 22 registered branding
tiles and eight conditional Mode Select tiles, over Faithful HD. Separate
wide slabs cover 14 layers, two inner replacements and 16 stretch overlays.
The skyline's native tiles still use Faithful HD. See the
[playtest implementation](menu-ui-playtest-pack-2026-10-19.md).

## Extras sources and geometry

Retail `screens/screens.viv::screens/extras/extras.big::extras.xsh`:

| Study | Shape | Bitmap names | Active canvas | Stored source canvases |
|---|---:|---|---:|---|
| List paper | 36 | 35, 34, 33, 32 | 150×354 | 128×256, 32×256, 128×128, 32×128 |
| Instructions, arrow note | 38 | 37 | 204×197 | 256×256 |
| Unlock track, tall note | 43 | 42 | 208×256 | 256×256 |
| Invalid cheat, red underline | 49 | 48 | 190×203 | 256×256 |
| Unlock points, note | 53 | 52 | 195×200 | 256×256 |
| Ribbon | 59 | 58 | 156×38 | 256×64 |

Full source identities, per-part used UVs and matrices are retained privately in
`extras-v1-20261020/references.json` and `extras-metadata.json`. The list paper
uses four tiles and retains its 22-column right strip and 98-row bottom strip.
Unused stored regions are not stretched to fill the panel. The ribbon's Shape
bbox and material UV width differ; its source tile stays an exact Faithful
upscale, with original UV mapping reserved for game use.

The six placement studies use observed static APT placements and retain panel
scale within a centered 640×480 viewport. Transparent wider margins remain
transparent. Fractional placements round only in the 3× previews; exact original
matrices are retained. Live text, state visibility and animation are omitted;
these are not whole-screen or runtime proofs.

## Generation and source protection

Five built-in ImageGen edits, with transparent output, produced material
proposals for the list and four notes. The ribbon uses Faithful HD only.
Historical pilot call count is now 106 (101 before this batch).
Exact prompts/inputs/raws/hashes remain in `jobs.json` and `generations.json`.

Final candidates use tile-aware 4× Lanczos source geometry, color and alpha.
Only masked fine brightness detail from each proposal is adopted: Gaussian4
residual ×0.14, clamped ±1.25 L, weighted mean removed, feather4 inside the mask.
Source Lab a/b and broad lighting remain the authority. Protect partial alpha,
source contours/folds, whole arrow/underline bands, list frame/header/rules,
UV boundaries, unused padding and four-native-pixel tile guards exactly.
The square arrow raw drifts 3.43% in canvas aspect and changes the arrow; those
features are rejected. Its normalization supplies fine material residual only,
not accepted geometry. Never apply this exception to structural seam repairs.

Final changes are deliberately small: at most 3 RGB levels on the list and
4 on the notes; mean safe-region Lab changes are within 0.002 per channel.
The ribbon is byte-identical to Faithful HD. This is a restrained technical
restoration proposal, not recovered authentic detail or the raw model output.

## Verification and review

Local review: `http://127.0.0.1:51738/review.html`, Extras first. Original/Faithful
references, color/alpha, three backgrounds, side/wipe/draft views and five
placement ratios are available. Browser checks: 24 reference/channel cases,
30 stage pairs, wipe50%, 16:9 viewport guide75%, no horizontal overflow and
zero console warnings/errors. The initially loading ribbon16:9 images were
rechecked complete at2560×1440. Proof: `extras-v1-20261020/review-proof.png`.

Nine tile alpha/padding/edge guards and six protected composites pass exactly;
all 30 stage centers match their 4:3 counterparts. All 1,314 previous PNGs and
226 approved/conditional file pins remain unchanged. No new source enters the
approved-art catalogue or installed playtest components before review.

The earlier 23-source shortlist remains historical: this batch addresses its
four note IDs and adds five neighboring list/ribbon IDs, not nine from that
shortlist. Nineteen shortlist IDs still need disposition; the newly reported
ten-tile fire-escape scene is additional work. Broader untouched UI includes
many live graphics, masks and portraits, not a count of restoration jobs.

## Game baseline and next action

One serial guarded Mac run used the existing build/packs at32:9:
`logs/run-20261020-100730.log.err`, 13 native3414×960 captures, 15 present reports
(min56, median118, max121), no crash/watchdog/ICALL failure, save root restored
byte-identically. It showed working settled title/main menus. Capture timing
hit the autosave warning and **missed the actual pan**; this is not a transition
acceptance result. Triage had unclassified baseline diagnostics (unsupported
FS-control return, optional contentmeta/LCONFIG lookups, worker waits and
software-raster statistics); bounded original slices were inspected. No runtime
fix is inferred from them.

A second main-menu-anchored probe refused to launch while a game from the
launcher-redesign checkout was active. That process was left untouched; Vlad
was asked whether it may be closed for serial scripted tests. Existing save
guards must remain enabled. Continue with
[the fire-escape mapping](menu-fire-escape-transition-mapping-2026-10-20.md):
measure the pan, prepare one continuous ten-tile master, restore/outpaint both
sides, review joins, then extend the explicit wide-layer registrations. No
transition artwork or runtime fix has been delivered yet.

## Local preservation

Working batch: `logs/ui-restoration-20261017/pilot-v1/extras-v1-20261020/`.
Immutable private overlay: `<data>/scratch/menu-ui-extras-v1-20261020/` with
hash manifest; apply to a copy of the older paused snapshot to recover history.
The older 2.53 GB snapshot stays immutable. Source-only prompts/recipes/review
code are preserved in the standalone authoring repository under
`research/menu-ui-extras-20261020/`. Art, arrays, capture evidence and review
records stay local. No 8768 writer, workshop candidates or Trejo art changed.

Final checks: 22 existing menu-art/export/agent-instruction tests passed; the
approved-art verifier reports226pins/0failures. The private overlay contains
185files /199,746,134bytes; manifest SHA-256
`8d9c4177ffe3550559e0bdda76b032de9e19cb2013ddebeda6c12e0dffdde44e`.

# Menu/UI HD + widescreen playtest pack (Mac), 2026-10-19

Delivery from [the handover](../menu-ui-modpack-handover.md): every approved menu artwork in game on
a Mac build of v0.10.4 (`110d50e` merged into this branch), with true widescreen backgrounds from the
approved wide masters. No new artwork was generated (D157/UI). Decisions D158/UI–D160/UI.

## Components (local, `<playtest-data>/mods/`)

| Component | What | Source |
|---|---|---|
| `menu-ui-restored-test-v1` | 39 source-shaped 4x tiles | approved tiles/mosaics, unchanged pixels (D158/UI) |
| `menu-ui-mode-conditional-test-v1` | 8 Mode Select tiles | conditional D132/UI art, own component |
| `menu-ui-branding-registered-test-v1` | title logo + 21 emblem tiles | approved D137/UI canvases mapped back to the native atlases (D159/UI) |
| `menu-ui-wide-test-v1` | `[wide]` side slabs for 14 background layers, 2 inner slabs, 16 stretch overlays | approved wide masters (D160/UI) |

Pack order: Faithful HD opacity; Faithful HD channels; restored; branding; Mode; wide. Only the title
skyline's 4:3 tiles stay Faithful HD (its approved art is the wide master, which the sides use).
Tables (hashes only, no art): `config/menu-ui-source-shaped-exports.json`,
`config/menu-ui-wide-layers.json`. Scripts: `scripts/export-menu-ui-pack.py`,
`scripts/register-menu-branding.py`, `scripts/export-menu-ui-wide.py`. Each component has a
`receipt.json` (input pins, placements, output SHA-256).

## Logo and emblems (D159/UI)

transparency-v2 kept, for each approved canvas, the guide placement of the native artwork and the
dense registration field it was fitted with. `register-menu-branding.py` pushes every approved canvas
pixel through that chain onto the 4x native grid (forward splatting; an inverse iteration did not
converge where the field folds), keeps the native Faithful 4x alpha exactly (holes, partial alpha,
silhouettes are the originals), blends to Faithful where the approved art has no material, and keeps
stored padding outside each tile's sampled UV rectangle. Approved coverage 0.76–0.97 of native opaque
texels; review sheets (Faithful vs registered, dark/light) under `<playtest-data>/review/branding/`.
This is a derivative of approved art and needs Vlad's visual sign-off; no new art was needed.

## Widescreen backgrounds (D160/UI, toolkit `defjam/menu-wide`)

Toolkit commit `20e7ba7` on `vyanhursky/xboxrecomp` branch `defjam/menu-wide` (fork only; no upstream PR):

- **`[wide]` slabs.** `<anchor id> = <dt>:<png>;...`. With a widescreen picture, a centred 2D batch
  whose stage-0 source has slabs is drawn again per slab: same vertices, colours, combiners and blend,
  shifted `dt` source texels, clipped to the side areas. Extensions therefore follow the layer's motion,
  scale and fades (title parallax, Options scroll, Mode pan). The shift uses the batch's measured
  texture coordinates (oT0 on the vertex-program path).
- **`inner:<png>`.** Also replaces the anchor itself in widescreen: the Rewards wall, whose approved
  wide view removes the baked 4:3 edge fade (handover Rewards exception).
- **`<id> = stretch`.** A full-width overlay drawn across the whole picture instead of centred. The
  menus draw a 256x256 vignette over 640x480 and a 256x4 header/footer band; centred, they darkened
  the 4:3 edges and left a black header strip that stopped at the seams (owner screenshots, title,
  Options, Jukebox). 16 listed: the header band, 6 vignettes seen in routes, 9 more black vignettes
  from a corpus scan (19 vignette-shaped 256x256 textures; the 4 grey ones are left out). A stretch
  applies only when the overlay spans the full 640 width.
- Full-width untextured fills (x 0..640 within 12 px) stretch like full-screen ones.
- RECOMP_WS_WIDE=0 turns all of it off; RECOMP_WS_WIDE_TRACE=1 logs geometry, centred full-screen
  pictures and large centred batches. Pack list from RECOMP_TEXTURE_PACKS may be 1023 characters.
- Shared renderer layer (`nv2a_pgraph_d3d11.c` + texture packs), so D3D11 gets the same path;
  Windows not built or run here. Frames with slabs skip the D3D11 blurred side fill.

Layer mapping (`config/menu-ui-wide-layers.json`): each anchor's settled geometry measured in game at
16:9 (every layer is exactly 1 title pixel per texel); its position in the master found by matching the
approved tile against the master (score 0.995–1.000; columns hidden by foreground in the master are
excluded with `match_u`). Title masters are texture-row aligned (anchor at master 1600,0); screen
masters at 1600 + 3·x0. Mode Select sides use the dark composite only (an unmasked lit slab painted
fully lit brick beside the spotlighted 4:3 wall). Results papers get no slabs (handover).

## Runtime evidence (Mac Vulkan, harness, guarded saves restored identical)

- Source-shaped pack: 29 of 47 IDs proven uploaded on `two-matches`/`unlock`; logs
  `logs/run-20261019-153015.log.err`, `-154012`.
- Widescreen: title, main menu (Mode), Battle setup, 2-fighter select, Unlock Fighters, Options,
  Jukebox, High Scores captured at 16:9 and 32:9 (21:9, 16:10 and 4:3 in the matrix run); 0 crashes or
  watchdogs in 40+ probe launches. 4:3 is unchanged (the placement path returns before any of this at
  k=1). Owner screenshots during the run found the vignette and header seams; both fixed and recaptured.
- Toolkit fixture `tests/texture_pack`: 3/3 (slab order, inner, stretch, malformed items, shape check,
  binding). Parent unit tests pass; regression results in PROGRESS §6.

## Playtest

```bash
/Users/vlad/Code/defjam-data/playtest-menu-ui/play.sh            # everything
/Users/vlad/Code/defjam-data/playtest-menu-ui/play.sh nowide     # same HD art, no side art
/Users/vlad/Code/defjam-data/playtest-menu-ui/play.sh nomode     # without Mode Select textures
/Users/vlad/Code/defjam-data/playtest-menu-ui/play.sh faithful   # Faithful HD only
/Users/vlad/Code/defjam-data/playtest-menu-ui/play.sh original   # no packs
```

`settings.ini` there: `aspect = widescreen (auto)` (picks the window or display shape; 21:9/32:9 by
choosing them in the launcher), 1920x1080 window. The script passes the pack list itself.

## Gaps and issues

1. **settings.ini pack list limit (fixed 2026-10-20, D142, PR #73, merged into this branch).** Text settings hold 95 characters
   (`RECOMP_SETTING_TEXT_MAX` 96). The two Faithful HD component names alone are 66; any longer list is
   rejected and the game falls back to `faithful-hd`, which a setup install does not have, so **no packs
   load**. The first playtest of this branch hit it. Strings now hold 1023 characters (toolkit `72bec86`,
   in this branch's pin `dd63e17`); `play.sh` still passes the list in the environment.
2. Title→main-menu pan uses other city tiles (`90c3ba5f36d9`, `0d9299ca3f85`, `8327612c6a9f`,
   `8736c571e654`, `dd4feccde8c4`…) with no approved art: black sides for that second of transition.
3. Not captured in widescreen: 3/4-fighter select, Unlock Venues, results/rematch/reward screens,
   Extras/Credits. Their backgrounds/vignettes use the same mechanism and tables.
4. Mode Select remains conditional: the side art is a flattened composite (no spotlight or door
   motion beside 4:3); the right 4:3 border is now visible to judge.
5. A 2-pixel beige sliver at the left 4:3 edge on the title (the save notice parked off screen) is
   pre-existing placement behaviour, more visible against the new sides.
6. Windows D3D11 and Steam Deck not run.

# Options / Jukebox background geometry — 2026-10-17

Bounded offline audit of `screens/options/options.big` and
`screens/jukebox/beatbox.big` from the owner's own `screens/screens.viv`.
No game, build, generation, asset modification or workshop operation.
Metadata/replay remain ignored:
`logs/ui-restoration-20261017/next-scope/options-jukebox-geometry.json` and
`audit-options-jukebox.py`. Seven retained original file hashes and decoded
RGBA images match fresh SHPX decoding. All seven are unswizzled DXT5
(SHPX0x62/NV2A0x0f). Every named image alias/source ID is retained in the JSON.

## Options: two console variants plus a separate right wall

**Options71 and8 are alternate console states, not simultaneous art layers.**
They are different source IDs and different pixels: 129,561 of262,144RGBA
texels differ, RGB difference bounds [48,80,512,432]. Similar brick/paper
appearance does not make them interchangeable or shared source art.

The explicit APT chain is root `menu`(sprite89) → its `menu`(sprite88).
Sprite88 has three labelled frames and stops on each: zero-based frame0
`ps2` uses shape9, frame1 `gc` retains that background, frame2 `xbox` replaces
it with shape72 at the **same depth2**, inheriting identity transform.
Sprite89's initial ActionScript reads `_root.g_console` and calls the child
menu's `gotoAndStop`. Preserve that platform selection. In this Xbox port,
shape72 is the intended Xbox-labelled variant; actual loader state still
needs a game capture rather than an offline assumption.

| Role | Shape / source order | Stored sheets | Nominal sampled active rectangle and join |
|---|---|---|---|
| Xbox options panel/wall | shape72, names71 then70 | 512×512 +64×512 | shape[0,0,574,480];71 samples[0,0,512,480],70 samples[0,0,62,480] at localx512 |
| PS2/GC options panel/wall | shape9, names8 then7 | 512×512 +64×512 | shape[0,0,568,480];8 samples[0,0,512,480],7 samples[0,0,56,480] at localx512 |
| Separate right wall `bkgWall` | shape5, name4 | 256×512 | shape/sample[0,0,171,487]; rootx469.4, initialy0 |

Packed vertices put the actual joins atx512.05957(shape72) and512.04590
(shape9), and the tail UV starts at0.05957/0.04590 respectively. These are
small mesh quantization differences, not a reason to crop or stretch runtime
textures. APT bounds independently match ELF `__BBOX` records.

The main sheets71/8 and wall4 are fully opaque. Tail70 is alpha0..255,
with nonzero-alpha stored bbox[0,0,56,512]; tail7 also has alpha0..255 and
stored bbox[0,0,64,512]. **Stored alpha bounds are not active UV bounds**:
shape72 samples62tail columns even though visible content ends by56;
shape9 samples56columns even though its sheet contains nonzero alpha beyond56.
Keep the original alpha and clip to geometry for previews. Do not append all64
stored tail columns or all512stored rows. The right wall below the partially
transparent tail remains a separately composited image.

Root draws `bkgWall` at depth1 and `menu` at depth3. Initial right-wall stage
extent is[469.4,0,640.4,487]; Xbox panel extent is[0,0,574,480]. The overlap is
intentional and includes the alpha boundary on the panel's right edge.

### Transforms are live

There is no universal settled transform independent of selection state.
Root ActionScript defines:

| Property | High/top endpoint | Low/bottom endpoint |
|---|---:|---:|
| `menu._y` | 0 | −149.9 |
| `bkgWall._y` | 0 | +181 |

`setInitialPos` chooses endpoints according to menu scrolling state/position;
key handlers and `easyScroll` interpolate the menu and background independently.
Their rootx positions remain0 and469.4 in these records. A preview should label
its chosen top state explicitly, not claim these layers are permanently fixed.
Do not bake the separately moving wall into the options panel.

Keep the child menu's live `highLightBar`, text objects, difficulty/vibration/
autosave controls, three audio sliders and highlight positions. Platform
frames replace/reposition text independently of the raster background. Root
also places sprite92 at depth5, scaled(2.5,1.875); it draws shape91/image90,
a256×256full-screen overlay. Its visual lighting role has not been inferred
from a name alone; preserve it separately until the game confirms compositing.

Name4's exact source ID is shared with `screens/options/cribOptions.big`:
`1c2de449c28536a5491ef5828f81d171544917f2238a18c8a1474ea8685edbae`.
The four panel/tail images have only their one options alias in this corpus;
no cross-screen identity reuse is established.

## Jukebox: one full-stage backdrop, independent controls/overlays

Root places **shape21 at depth2, identity**, stage/sample[0,0,640,480]. It is
one backdrop split horizontally into name20(left512×512) and name19(right
128×512), nominal source joinsx512, active height480. Actual mesh joinx512.03125
maps to tail UVx0.03125. Both sheets are fully opaque; bottom32stored rows are
not active backdrop pixels. Neither source ID is shared with Options; both
have only the beatbox alias in this corpus.

| Name | Source ID |
|---|---|
| 20 | `cc11d92079b1a81132faae6e64053f82e397ba78f4e097be11d409fb8f9c5d64` |
| 19 | `f8e71b8bc56f4d3a502fd09ccc526d33a6173173570b1595bf880185ffeacc6b` |

Preserve these later layers independently:

- `buttonTitle0..4`, `button0..4`, `scroller`, `chyron`, `nowPlaying`,
  `listContents` and scripted `mover`; they are actual controls/text/list state.
- Rootdepth22 shape43 is a mask with `clip_depth25`, nominal stage bounds
  [45,190.05,587,433.05]. The song list at depth23 remains inside that mask.
- Rootdepth49 shape89 draws a separate bottom strip, nominal bounds
  [0,431,640,480], from names88|87. It must not disappear when widening the
  backdrop; its relationship to overlays should be validated in game.
- `lightingClip` at depth50 is sprite92→shape91/image90(256×256), root scale
  approximately(2.4999695,1.8750305). RootActionScript explicitly hides it when
  `_root.g_console == 'gc'`. Retain that conditional layer rather than baking
  its effect into the restored backdrop. Xbox compositing remains live.

Background shape21 has no later timeline transform updates in the two-frame
root. This establishes its authored identity placement; external runtime
changes, actual mask/lighting behavior and transition coverage still require
a capture. Do not turn this source layout study into a claimed game render.

## Authoring implications and scope limit

For the next authorized scenery batch, compose Options **Xbox71|70** at top
state over separate wall4 for review, retaining the PS2/GC8|7 variant as a
separate source reference. Jukebox20|19 is a straightforward640×480source
mosaic. Extend scenery while retaining centered original controls and existing
alpha overlaps; keep live layers/masks out of generated paintings. Preserve
original texts/poster graphics in raster art and retain source colors.

No geometry in this report is a ready-made runtime widescreen patch. Wide UV,
scissor, layer placement and transient loading still need deliberate integration
and testing. No dependency beyond these two movies, their `.o`/`.apt` records,
and the already-retained source corpus was explored.

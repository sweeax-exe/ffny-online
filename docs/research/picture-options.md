# Picture options: brightness and the venue vignette

2026-10-18. Issues [#44](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/44) (brightness; its
subtitle request is separate) and [#47](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/47) (turn
off the vignette). Source: `src/patches/picture.c`, hooks in `src/recomp_manual.c`, settings
`[display] brightness`, `vignette`, `vignette_strength` ([reference](../settings-reference.md)).

## Brightness

The title sets one gamma ramp at start-up through `sub_002203C6` (an S curve that deepens shadows). The port
already applied it at presentation (`d3d8_gamma_set`, Direct3D 11 and Vulkan) and kept a copy,
`g_recomp_gamma`, for captures. Brightness is a power curve composed on top of that ramp:
`out = ramp(in) ^ (100 / brightness)`, so 0 and 255 stay where they are and the mid-tones move. 100 skips
the curve and hands the title's ramp over unchanged, bit for bit. With `[display] gamma` off the curve is
applied to the identity. Brightness and the vignette are not marked *restart*: a change in the launcher or
the overlay is pushed through `pc_settings_apply_display` at once.

## Where the vignette comes from

The vignette is one of the venue post effects of `CPostEffectMan` (`CPostEffectMan::Init` at `0x000D92C0`),
read from `assets/postfx/<name>.pfx` by `sub_000D8320`. Each file holds `NumVariance` records of 0x44 bytes
at `this+0xA0`, keyed by the names in the image:

| Offset | Key | Offset | Key |
|---|---|---|---|
| +0x00 | `GlowEnable` | +0x24 | `ScreenTintVignetteID` |
| +0x04 | `ScreenTintVignetteEnable` | +0x28 | `VignetteAlpha` |
| +0x08 | `ScreenTintEnable` | +0x2C..+0x34 | `TintModulateColorR/G/B` |
| +0x0C | `ScratchesEnable` | +0x38..+0x40 | `TintFillColorR/G/B` |
| +0x10 | `FlickerEnable` | +0x14..+0x20 | glow intensities and blend |

`sub_000D8050` copies (or interpolates) the current variance to `this+0x4E0`. Each frame `sub_000D8C40`
draws it: with `ScreenTintEnable` it calls the platform tint's draw,
`sub_000D9AF0(this, modR, modG, modB, fillR, fillG, fillB, vignette)` (thiscall, `ret 0x1C`), the last
argument being `ScreenTintVignetteEnable != 0`. That draw puts the "Vignette Quad" (`this+4`, built by
`sub_000D99C0` as a 640x480 quad) over the screen with blend preset 4 and colour 0xFF in all four channels
(`sub_00028460`). Preset 4 (`sub_00113580`) is a multiply: source factor `DESTCOLOR` (0x306), destination
`ZERO`, operation `ADD`. `VignetteAlpha` is interpolated into `this+0x508`, but nothing on the draw path
reads it, so the vignette's darkness is in its texture.

## What the options do

- **Off** (`vignette = false` or strength 0): `sub_000D9AF0_enter` clears the last argument, so the quad is
  not drawn. Tint, glow and scratches are untouched.
- **Strength** `s` below 100: the same hook remembers the vignette quad; `sub_00028460_enter` sees its colour
  call, changes the destination factor to `INVSRCALPHA` (0x303) and the colour to `s` in all four
  channels. The result is `dst * (s * v + 1 - s)` for a vignette texel `v` with opaque alpha: the authored
  picture blended toward the undarkened one. The game sets preset 4 again every frame, so 100 restores it.
  The hook only acts when the state still holds the multiply it was written for.

## Measurements (macOS, Vulkan)

Five repeatable One on One fights (`scenario_suite.py run fight --step-input default --rng-seed 20261006
--no-unlock --step-shots 300,900,1500 --settings <json>`; the new `--settings` keys carry the three options).
The crowd is not seeded, so comparisons use the pixels that agree within 3 between two authored runs (70-75%
of each frame). Default venue; its variance draws the vignette (`[PICTURE] venue vignette drawn as authored`).

| Capture, step 1500 | centre mean | top and bottom eighth mean |
|---|---|---|
| authored | 39.6 | 28.8 |
| vignette off | 39.1 | 42.7 |
| strength 50 | 39.5 | 35.7 |
| brightness 150 | 65.0 | 52.1 |

- Off: the vignette factor `authored / off` is 0.67-0.72 at the top and bottom and 0.97-1.00 in the middle.
- Strength 50 against the prediction `off * (0.5 + 0.5v)` over all three steps: median absolute error 0,
  mean 1.0-1.4 (of 255). Where the vignette darkens by more than 20% the measured `half / off` is 0.65-0.70
  against a predicted 0.71-0.73, a little darker than linear, so the texture's alpha is close to opaque.
- Brightness 150 lifts the mid-tones as the curve says (step 1500: 20 to 46, 60 to 95, 118 to 151, 191 to
  210) and leaves the extremes in place.
- The window itself (`RECOMP_WINDOW_SHOT`, after the presentation gamma pass) with brightness moved from 100
  to 197 in the overlay during a run: the title screen around the overlay goes from a mean of 0.4-1.3 to
  4.0-8.4, so the composed ramp reaches the display, not only the captures.
- The launcher: `regress.py --only picture` clicks the slider and the checkbox, finds them in
  `settings.ini` and the game starting with them.

Not measured: Windows (Direct3D 11) and Steam Deck. Both use the same hooks and the same `d3d8_gamma_set`
entry point. Other venues and the Story scenes were not toured for their own variances.

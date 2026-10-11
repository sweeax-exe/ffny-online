# Icons and Steam artwork

The shield icon, the title wordmark and the title-screen backdrop used for the application
icons and the Steam Deck / Linux library artwork. **This is the game's own artwork, rendered
from its textures. It remains the property of its rights holders; the project's MIT licence
does not cover it.** The owner approved committing exactly these files (D98, 2026-10-10);
`scripts/check-source-tree.py` pins them by the SHA-256 of `SHA256SUMS`, so any change needs
a new approval.

## What is here

| Path | Use |
|---|---|
| `icon/defjam.ico` | Windows: the game, launcher and setup executables (`cmake/app.rc.in`) and the Start menu and desktop shortcuts. Sizes 16 to 256. |
| `icon/macos/defjam.icns` | macOS: `Def Jam Setup.app`, `Def Jam Recompiled.app`, and the Dock. Inset on a transparent canvas with a soft shadow, as macOS expects. |
| `icon/png/defjam-N.png` | Linux: the application-menu and desktop entries (256), the setup wizard (128) and any other size a desktop asks for. |
| `icon/window/*.rgba` | Raw 128x128 RGBA, compiled into the macOS and Linux game as the SDL window icon (`cmake/app_icon.cmake`). |
| `steam/portrait.png` 600x900 (the cover), `wide.png` 920x430, `hero.png` 1920x620, `logo.png` 1280 wide (transparent), `icon.png` 256 | The Steam library artwork. The Linux setup copies the first four into the Steam shortcut's `grid` folder (`<appid>p.png`, `<appid>.png`, `<appid>_hero.png`, `<appid>_logo.png`). |
| `github/social-preview-1280x640.jpg` | The repository's social preview image (upload it under Settings, Social preview). |
| `fan/` | The same logo, wide and portrait capsules and social preview with **FAN RECOMP** under the title, so the port reads as a fan project. `fan/steam/` is what the Linux setup places for Steam (its hero and icon are the plain set's); the plain files above are kept as the editable originals. |
| `masters/` | The source images everything else is rendered from, including `fan-tag.png`, the FAN RECOMP tag drawn once in Impact (distressed with the plate's speckle and scratches, outlined in its ink) so rendering does not need the font. |
| `SHA256SUMS` | The fingerprint of every file above. |

## Where the masters come from

Decoded textures of the supported USA Xbox dump (ids as in the corpus `scripts/hd_corpus.py`
writes):

| Master | Texture |
|---|---|
| `shield.png` | `screens/battle/support/battlemodeLogosIcons.big::battlemodeLogosIcons.xsh`, `76ed1cf6...` (256x256), the stray strip at its right edge removed, upscaled 8x with the edge re-sharpened |
| `wordmark.png` | `misc.viv::nis_logo.xsh`, `7454dcc2...` (512x128), 4x on premultiplied channels; the letters, transparent in the texture, are filled with ink so it reads on any backdrop |
| `scene.png` | the title screen's three layers, `demoStrt.xsh` `f3bcc263...` (sky), `e2c11109...` (rooftops), `35be3a12...` (water tower), composited at their native 640x480 |
| `skyline.png` | the skyline strip of the sky layer, used to extend the hero past the scene |
| `cover.jpg` | the official cover art without its logo, 660x930, byte for byte as downloaded on 2026-10-10 (SHA-256 `b6f5263e73557a80b479bf0d0fe2b28bab4fff59ab2837fb473f779555720c47`) from a community upload on SteamGridDB (`grid/fead4e29f9507390899f8e198518a262.jpg`, "No Logo", by Peipara) with the owner's approval. The portrait capsule is this cover cropped to 2:3 with the wordmark master drawn over it. It is not from the dump. |

The textures are 640x480-era artwork: the renders are as sharp as the sources allow, and the
backdrop is graded (lifted, vignetted, grain) rather than invented.

## Regenerate

```
python scripts/make-branding.py render                         # everything from masters/ (Pillow, NumPy)
python scripts/make-branding.py masters --corpus <corpus dir>  # masters from your own dump's corpus, then render
python scripts/make-branding.py fan-tag --font <font.ttf>      # redraw the FAN RECOMP tag master, then render
```

Rendering is not byte-for-byte reproducible across Pillow versions, so a regeneration produces a
new `SHA256SUMS` that the owner has to approve.

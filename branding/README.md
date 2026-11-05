# Icons and Steam artwork

The shield icon, the title wordmark and the title-screen backdrop used for the application
icons and the Steam Deck / Linux library artwork. **This is the game's own artwork, rendered
from its textures. It remains the property of its rights holders; the project's MIT licence
does not cover it.** The owner approved committing exactly these files (D98, 2026-10-10);
`scripts/check-source-tree.py` pins them by the SHA-256 of `SHA256SUMS`, so any change needs
a new approval. On 2026-11-05 the owner approved the HD city/title refresh below,
retaining the original Impact subtitle.

## What is here

| Path | Use |
|---|---|
| `icon/defjam.ico` | Windows: the game, launcher and setup executables (`cmake/app.rc.in`) and the Start menu and desktop shortcuts. Sizes 16 to 256. |
| `icon/macos/defjam.icns` | macOS: `Def Jam Setup.app`, `Def Jam Recompiled.app`, and the Dock. Inset on a transparent canvas with a soft shadow, as macOS expects. |
| `icon/png/defjam-N.png` | Linux: the application-menu and desktop entries (256), the setup wizard (128) and any other size a desktop asks for. |
| `icon/window/*.rgba` | Raw 128x128 RGBA, compiled into the macOS and Linux game as the SDL window icon (`cmake/app_icon.cmake`). |
| `steam/portrait.png` 600x900, `wide.png` 920x430, `hero.png` 1920x620, `logo.png` 1280 wide (transparent), `icon.png` 256 | The Steam library artwork. The city backdrop is cropped to each aspect without stretching. The hero is background-only because Steam draws the separate logo over it. The Linux setup copies the first four into the Steam shortcut's `grid` folder (`<appid>p.png`, `<appid>.png`, `<appid>_hero.png`, `<appid>_logo.png`). |
| `github/social-preview-1280x640.jpg` | The repository's social preview image (upload it under Settings, Social preview). |
| `fan/` | The same logo, wide and portrait capsules and social preview with **FAN RECOMP** under the title, so the port reads as a fan project. `fan/steam/` is what the Linux setup places for Steam (its hero and icon are the plain set's); the plain files above are kept as the editable originals. |
| `masters/` | The source images everything else is rendered from, including `fan-tag.png`, the FAN RECOMP tag drawn once in Impact (distressed with the plate's speckle and scratches, outlined in its ink) so rendering does not need the font. The tag is fitted under the right-hand title plate. |
| `SHA256SUMS` | The fingerprint of every file above. |

## Where the masters come from

The shield comes from the supported USA Xbox dump. The city and title use the
published **HD Widescreen Menus 1.4.0** pack's prestart artwork (archive SHA-256
`f3d79f86a024b5786919ef2d6f38b1f599cb49a151d031b4a85023a5f480848d`).
Texture IDs below use the corpus format `scripts/hd_corpus.py` writes.

| Master | Texture |
|---|---|
| `shield.png` | `screens/battle/support/battlemodeLogosIcons.big::battlemodeLogosIcons.xsh`, `76ed1cf6...` (256x256), the stray strip at its right edge removed, upscaled 8x with the edge re-sharpened |
| `wordmark.png` | `screens/intMain/prestart.big::prestart.xsh`, title texture `c1943283...`, the pack's 2048x512 restoration, cropped to its visible bounds; silver pixels retained and transparent letter openings filled with ink |
| `scene.png` | 5120x1440 panorama assembled from the pack's sky `627117d2...` + `91955063...`, city `fdc3666e...` + `99aa2410...`, and water tower `427424b7...` + `682f298f...`, plus their published side slabs. Authored idle positions: sky source y−134, city y102, tower y16; active city/tower heights 310/446 logical pixels. Side slabs contribute only outside the original 640px stage. |
| `skyline.png` | 5120x948 visible sky layer before city/tower compositing; retained as an editable layer. The HD hero uses the full panorama directly. |
| `cover.jpg` | the legacy official cover art without its logo, 660x930, byte for byte as downloaded on 2026-10-10 (SHA-256 `b6f5263e73557a80b479bf0d0fe2b28bab4fff59ab2837fb473f779555720c47`) from a community upload on SteamGridDB (`grid/fead4e29f9507390899f8e198518a262.jpg`, "No Logo", by Peipara) with the owner's approval. Retained for the original-source workflow; HD portraits use the city panorama. It is not from the dump. |

The HD exports retain the pack's colours and texture detail. Crops preserve aspect ratio;
the hero has a subtle lower-left shade for Steam's separate logo. No mirrored skyline,
extra sharpening, artificial grain or new generated artwork is added by the branding renderer.

## Regenerate

```
python scripts/make-branding.py render                         # everything from masters/ (Pillow, NumPy)
python scripts/make-branding.py masters --corpus <corpus dir>  # masters from your own dump's corpus, then render
python scripts/make-branding.py hd-masters --pack <menu-ui-hd-wide-v1.4.0.zip> # HD city/title, then render
python scripts/make-branding.py fan-tag --font <font.ttf>      # redraw the FAN RECOMP tag master, then render
```

Rendering is not byte-for-byte reproducible across Pillow versions, so a regeneration produces a
new `SHA256SUMS` that the owner has to approve.

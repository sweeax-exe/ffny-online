# Media index

[Back to the project](../../README.md) · [Media upkeep](capture-plan.md)

What each approved image and clip in this folder shows, where it came from and who approved it. The exact
fingerprints are in `scripts/check-source-tree.py`.

## Project site and README comparisons

The `compare-*` pairs and `app-*` screens are for the [project site](../../site/README.md) and the guides, approved by the
owner on November 5, 2026. Captured on an Apple M1 Pro from the v1.0.0 candidate build `6396ac3`, whose
interface matches v1.0.0 beta 2 apart from test hooks.

- **`compare-*-before.png`:** 640×480, 4:3, render scale 1, original textures.
- **`compare-*-after.jpg`:** 1708×960, 16:9, render scale 2, Upscaled Original Textures, HD Widescreen Menus
  1.4.0, HD Fighters 1.0.0, MSAA 4×, anisotropic filtering 16×, default colour tint and film scratches.
- **`compare-fight-*`:** Nyne vs Trejo at The Limit, one seeded fight at simulation step 900. The crowd differs
  because the wider view frames it differently.
- **`compare-cutscene-*`:** Story scene `sc05`, 9 seconds after its file opens. Unlike the other pairs, both sides
  are 4:3, so the frame and letterbox match. The after picture is 1280×960, render scale 2, with the same textures
  and filtering as the other after pictures.
- **`compare-menu-*`:** the main menu, idle. **`compare-map-*`:** the Battle venue map.
- **`site-trailer.mp4`:** the owner's trailer, supplied on November 5, 2026: a silent 1280×720 H.264 web copy, 38.6 s at
  3.5 Mbps. **`site-trailer-poster.jpg`:** a frame from it.
- **`readme-compare-*.jpg`:** the README's comparisons, at the owner's request on November 5, 2026. `fight` and `menu` are composed from
  the `compare-*` pairs. `foundation` is Blaze vs Fat Joe at The Foundation, one seeded fight (seed 20261006) at simulation step 1000, in the
  same before and after settings.
- **`app-*`:** the app in a test home folder with the Modern experience.
- **`install-*`:** the [install guide](../install-guide.md)'s setup screens, from the same candidate, with neutral example paths on a temporary
  volume. The free space on `install-step-3.png` (120.0 GiB) was simulated for the capture; the app shows the real figure.
- The README's trailer is an upload to GitHub, not a file here: a 9.1 MB, 960×540 copy of the owner's trailer.

## Launcher and developer menu

`launcher.png` and `launcher-advanced.png` (used by the [launcher guide](../launcher-and-overlay.md)): source `ffb428b` with
only the CMake version changed to 0.10.6 before capture; toolkit `32aab4d`. Executable SHA-256:
`ef478860ad10ddacb3586fab744e6471b688c56b3a1afdf4c33a7541645a0f75`.
Captured through SDL/Metal at 2400×1600 pixels and downsampled to 1440×960;
no visual mockup or game artwork was added. The owner requested this replacement
on October 20 (D144). Disposable data and the complete-save-root guard were used;
restoration passed. Local receipt: `logs/launcher-review/beta-20261020/receipt.json`.

`developer-menu.png` is used by [the developer menu guide](../developer-menu.md).

## Removed exports

On November 5, 2026 the owner removed the older gallery exports (gameplay stills, GIF and clips, the texture and
widescreen comparisons including the Blaze and Doc still, and the historical overlay and setup screenshots). They and their provenance remain in
repository history.

Game artwork belongs to its respective rights holders. The project's MIT
license does not cover it. Approval applies only to these exact exports and
does not permit publishing raw game assets, saves or other recordings.

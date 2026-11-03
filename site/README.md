# Project site

The GitHub Pages site for the port: one page, plain HTML, CSS and JavaScript with no build
step of its own. It is served at `https://vyanhursky.github.io/DefJamFFNY-recomp/`.

| File | Holds |
|---|---|
| `index.html` | All the copy and structure: hero, features, screens, install, mods, news, FAQ, roadmap and docs, community, footer. |
| `styles.css` | The look. Colours and fonts are variables at the top. |
| `app.js` | Live details: the latest release line, the download button for the visitor's OS, the news list, the before/after sliders and the screenshot lightbox. The page still works without it. |

## Images

This folder holds text only; `scripts/check-source-tree.py` refuses image and video files
outside the approved set. `scripts/build-site.py` copies the pictures in when the site is
built, from the files already approved for publication (the README media, D58/D144, and the
branding set, D98), and checks each one against its pinned SHA-256 first. Its `MEDIA` table
maps each published name under `media/` to its source; `tests/unit/test_build_site.py`
checks that the page only refers to names in that table.

A new picture needs the owner's approval and a fingerprint, like any README media: add it to
`docs/media/` and the allowlist in `scripts/check-source-tree.py`, then add a line to `MEDIA`.

## Preview

```sh
python scripts/build-site.py
python -m http.server --directory build/site 8000
```

Then open `http://localhost:8000`. The release line and news load live from the GitHub API.
When the API is unavailable, the page reads `releases.json`, which the deploy writes with
`--releases`.

## Publishing

`.github/workflows/pages.yml` builds and deploys the site when a stable (not pre-release)
release is published, and on demand from the Actions tab (**Pages → Run workflow**). Before the
first deploy, enable Pages once: **Settings → Pages → Build and deployment → Source: GitHub
Actions**.

The release line, download buttons and news follow the latest release by themselves. Feature
copy, the FAQ and the roadmap are written by hand: update them when a release changes what the
port does.

## Proposed captures

Pending the owner's approval. None of these exist yet; each would be captured from a tagged
release, reviewed, then pinned as described under **Images**.

| Priority | Shot | Where it goes | Why |
|---|---|---|---|
| 1 | Mods page with the catalogue and a pack installed (UI only) | Mods section, replacing the Advanced screenshot | Shows the feature the section describes. |
| 1 | A second venue in daylight or a bright club, 16:9, HD | Screens gallery | Every current gameplay shot is the dark Basement. |
| 1 | Steam Deck in Gaming Mode, 16:10 | Screens gallery, Linux install card | Platform claim with no picture today. |
| 2 | Four-fighter free-for-all | Features card 06 or gallery | Local multiplayer; caption only what the capture proves. |
| 2 | Created fighter in the Sketch Artist or crib | Screens gallery | Story mode beyond versus fights, and the v0.12.1 fixes. |
| 2 | Current setup wizard on macOS or the Deck, neutral paths | Install section | The only setup capture is the historical Windows one. |
| 3 | A short loop of a grapple or Blazin' move, 16:9 | Hero reel | Replaces the face-off clip with a signature moment. |
| 3 | Windows capture of the same fight frame | Screens fine print | All captures are Mac today. |

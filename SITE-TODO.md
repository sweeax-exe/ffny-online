# Project site: to-do for v1.0.0

**Temporary. Delete this file in the last commit before this PR is marked ready.**

The shared work list for the GitHub Pages site. The site goes live with stable v1.0.0. Several
agents may work from this file at once, so follow the rules below.

## How to work on an item

1. Read `AGENTS.md` and [site/README.md](site/README.md). The branch is `feat/project-site`.
2. Use your own worktree, on a branch of your own:
   `git worktree add -b site/<item> ../defjam-site-<item> origin/feat/project-site`. When the item is
   done, rebase it on `origin/feat/project-site` and push it to `feat/project-site`.
3. Claim the item before you start: change its status to `claimed: <agent or session>`, commit and
   push only that change. If the push is refused, pull, re-read the table and pick again.
4. One item per commit. `git pull --rebase` before every push. When done, set the status to `done`
   with the commit hash.
5. Stop at anything marked **owner**. Ask Vlad, record the answer here, and do not guess.

**Rules that apply to every item:**

- `site/` holds text only. Any new image or video needs Vlad's approval first, then a file in
  `docs/media/`, its SHA-256 in `scripts/check-source-tree.py` and a line in `MEDIA` in
  `scripts/build-site.py`. Never commit unapproved captures, game data or dumps.
- `git add -A --dry-run` before each commit, then run `python scripts/check-source-tree.py` and
  `python -m pytest -q tests/unit/test_build_site.py`.
- Do not edit `PROGRESS.md` on this branch. Decisions go in the PR description as `D-new` lines.
- Do not edit `README.md` and `site/index.html` for the same claim in one commit; they review separately.
- Only one agent edits a given section of `site/index.html` at a time. Item S7 owns the whole file
  while it is claimed.

**Preview:** `python scripts/build-site.py`, then
`python -m http.server --directory build/site 8765`. The page reads the latest stable release, which is
v0.13.0 until 1.0 ships, so its buttons offer the old setup. To see the app downloads, change
`!r.prerelease` in `build/site/app.js` (never `site/app.js`) to allow the newest beta's tag.

## Status

| ID | Item | Needs | Status |
|---|---|---|---|
| S1 | Replace the hero gameplay clip | | done, see question below |
| S2 | New features tagline | | done |
| S3 | Feature cards: order and wording | | done, see question below |
| S4 | New captures: Menu/Launcher, Mod launcher, other old-UI pictures | | done |
| S5 | Install guide for the app | | done, real-system checks below |
| S6 | Owner confirmations | | answered; docs follow in S8 |
| S7 | Vlad's review of the remaining page sections | | done |
| S8 | README pass for 1.0 | | README done; other docs open |
| S10 | Four new before/after sliders | | done |
| S11 | New Roadmap entries | | done |
| S9 | Go-live | **owner**: Pages setting | open, last |

## Items

### S1. Replace the hero gameplay clip

Done (2026-11-05): Vlad supplied the trailer. `docs/media/site-trailer.mp4` is its silent 720p web copy (16.8 MB, 38.6 s), with
`docs/media/site-trailer-poster.jpg` as its poster, and it replaces the old clip on the page.

- README: the trailer plays from a GitHub attachment Vlad uploaded (9.1 MB copy).
  under-10 MB copy uploaded, then its URL replacing the poster link in the Gameplay section.

### S2. New features tagline

Done: Vlad chose "Everything you remember. Nothing holding it back."

### S3. Feature cards

Done in Vlad's order: native on three platforms, 60 fps and 4×, widescreen, language, mod support,
controllers and keyboard, one app, cheat menu, Add to Steam. The cheat card no longer says what is off
by default or asks first.

**Question for Vlad:** these cards are no longer in the list. Should any come back, or be mentioned elsewhere?

- **Four players, one couch.** Local multiplayer now appears only in an FAQ answer.
- **EA's developer menu.** Now only a docs link.
- **Upscaled textures.** Still in the sliders and the Mods section.
- **Picture options** (Modern or Original, MSAA, anisotropic filtering, tint, film scratches). Now nowhere
  except "Modern or Original" in the install steps.

**Wording to check:** French is the disc's own translation, turned on by a setting, not a mod (2,825 of
3,068 strings; the voices and menu icon art stay English). The card says so.

### S4. New captures

Done: `docs/media/app-*.png`, approved by Vlad on 2026-11-05 and pinned. The remaining optional shots are in
[site/README.md](site/README.md#proposed-captures).

### S5. Install guide for the app

Done: `docs/install-guide.md` and its pinned `docs/media/install-*.png` screenshots.

The Modern table in the guide assumes PR #193 (MSAA 2×, anisotropic filtering 16×), so that PR must land with this one.

Still to check on real systems, since they came from the old setup guide or general knowledge:

- Windows: the SmartScreen wording and the install screen.
- macOS: the "Open Anyway" steps for the app's `.zip`.
- Linux and Steam Deck: marking the AppImage executable in Dolphin, and the install screen.

No Windows or Linux screenshots exist yet, and neither does one of the finished screen.

### S6. Owner confirmations

Vlad's answers (2026-11-05), now the policy for the page and the docs:

- Mod support is not a beta.
- The page may say Steam Deck hits a steady 60 fps.
- The generated textures are called **Upscaled Original Textures**.
- Players should not attach F9 bug bundles to reports.
- Custom fighters, music, stages and game modes are coming; working branches exist.

**Follow-ups:**

- `docs/releases/v1.0.0.md` must not tell players to attach F9 bundles. The published beta 1 and beta 2 notes do, so Vlad
  decides whether to edit those on GitHub.
- Item S8 applies the naming, the mods wording and the F9 rule to the README and the guides.

### S7. Review of the remaining page sections

Vlad has reviewed the features so far. Still to come: the hero, Screens, Install, Mods, News, FAQ,
Roadmap and docs, community and footer. Add his notes here as they arrive.

Done from Vlad's notes (2026-11-05): launcher and settings pictures removed from Screens, slider captions removed,
FAQ moved below Roadmap and docs, and the cutscene recaptured at 4:3 on both sides so the slider lines up (pair approved by Vlad). The developer menu picture is removed too.

### S8. README pass for 1.0

The README is done and approved by Vlad. Still open, in the other docs:

- Naming: use **Upscaled Original Textures**. "Faithful HD" or "Recomp HD" also appear in `docs/hd-textures.md`, `docs/hd-texture-workflow.md`,
  `docs/language-packs.md`, `docs/launcher-and-overlay.md`, `docs/mod-manager.md`, `docs/menu-ui-modpack-handover.md`,
  `docs/texture-modding-getting-started.md` and `docs/widescreen/09-macos-validation.md`.
- Mods: `docs/mod-manager.md` calls broader mods future work. Vlad says they're coming.

### S10. Four new before/after sliders

**Done (2026-11-05):** all four pairs are approved, pinned as `docs/media/compare-*` and on the page, with
`docs/media/README.md` recording how they were made. The two old sliders are removed, by Vlad's choice.

Add four sliders to the Screens section. In each one:

- **Before:** original resolution, 4:3.
- **After:** widescreen, 2× resolution, all HD modpacks and graphics settings.

Scenarios:

- [x] One v One fight in The Limit, Nyne vs Trejo
- [x] A cutscene: Vlad chose Candidate A, sc05
- [x] The main menu
- [x] Map Select

Vlad's answers: the old sliders go; Map Select is the Battle venue map; the colour tint and film scratches stay at their defaults.

### S11. New Roadmap entries

Done on the page: Extensible Custom Fighter Mod support, Custom and Restored Songs, Additional HD Upscaling mods (Story
Fighter customization, Stages and Level textures) and the Def Jam: The Takeover expansion (its mechanics and story
mode in this game). The README's roadmap table gets the same entries in S8.

### S9. Go-live

- [ ] Vlad, once: Settings → Pages → Build and deployment → Source: GitHub Actions.
- [ ] This PR lands before v1.0.0 is tagged. `pages.yml` must be on the tagged commit or the deploy will not run.
- [ ] Remove the line in `docs/releases/v1.0.0.md` saying the site was taken out of the release.
- [ ] Delete this file and mark the PR ready.
- [ ] After publishing v1.0.0, check the Pages run and that the live site shows v1.0.0 with app downloads.

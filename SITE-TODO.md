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
| S1 | Replace the hero gameplay clip | **owner**: clips | waiting on clips |
| S2 | New features heading | **owner**: pick | waiting on pick |
| S3 | Feature cards: order and wording | | done, see question below |
| S4 | New captures: Menu/Launcher, Mod launcher, other old-UI pictures | **owner**: approval | open |
| S5 | Install guide for the app | | open |
| S6 | Owner confirmations | **owner**: answers | open |
| S7 | Vlad's review of the remaining page sections | **owner**: notes | waiting on notes |
| S8 | README pass for 1.0 | **owner**: review | open |
| S10 | Four new before/after sliders | **owner**: answers, approval | open |
| S11 | Three new Roadmap entries | | open |
| S9 | Go-live | **owner**: Pages setting | open, last |

## Items

### S1. Replace the hero gameplay clip

Vlad will supply clips and ask for them to be combined. The clip is the one playing under the hero:
`docs/media/hd-16x9-gameplay.mp4`, shown as `media/hd-16x9-gameplay.mp4`, with
`docs/media/gameplay-hero.png` as its poster.

- Combine the clips as Vlad directs. Keep the working files outside the repo or under the ignored `logs/`.
- Keep it silent, looping and small enough for a web page (the current one is 5.6 MB).
- Vlad approves the final file, then pin it as in the rules above.
- Update the `<video>` label and caption in `site/index.html` to say what it now shows.
- **Question for Vlad:** should the README's GIF (`docs/media/gameplay-preview-compact.gif`) change too?

### S2. New features heading

"The same fight. A sharper corner." is to be replaced. Suggestions, for Vlad to pick from or redirect:

1. Same streets. Sharper fight. (recommended)
2. Old-school fight. New-school hardware.
3. The fight you remember. Built for now.
4. Same block. Better everything.
5. Your corner. Your rules.

Then change `#features-title` in `site/index.html`.

### S3. Feature cards

Done in Vlad's order: native on three platforms, 60 fps and 4×, widescreen, language, mod support,
controllers and keyboard, one app, cheat menu, Add to Steam. The cheat card no longer says what is off
by default or asks first.

**Question for Vlad:** these cards are no longer in the list. Should any come back, or be mentioned elsewhere?

- **Four players, one couch.** Local multiplayer now appears only in an FAQ answer.
- **EA's developer menu.** Now only a gallery picture and a docs link.
- **Upscaled textures.** Still in the sliders and the Mods section.
- **Picture options** (Modern or Original, MSAA, anisotropic filtering, tint, film scratches). Now nowhere
  except "Modern or Original" in the install steps.

**Wording to check:** French is the disc's own translation, turned on by a setting, not a mod (2,825 of
3,068 strings; the voices and menu icon art stay English). The card says so.

### S4. New captures

Vlad has asked for these two first:

- [ ] **Menu/Launcher:** the app's current Home screen. It replaces `media/launcher.png` in the Screens gallery.
- [ ] **Mod launcher:** the app's current Mods page, showing the catalogue. It replaces the Advanced picture
  in the Mods section.

Pictures with the `needs-capture` class show a red "Old UI" label on the page: Home, Advanced (twice)
and the F1 overlay. The rest of the shot list is in [site/README.md](site/README.md#proposed-captures).
Vlad approves each capture. Pin it, swap it in and remove the class. The labels must all be gone before
go-live, either replaced or by Vlad's choice to ship the old picture.

### S5. Install guide for the app

`docs/setup-installer.md` still describes the old setup wizards, and the page's platform guide links and
Install guide card point to it.

- Rewrite it, or add an app guide, covering first run, the Modern and Original choice, updates, Repair,
  Uninstall and Add to Steam, per platform. `src/app/README.md`, `docs/launcher-ux-candidate.md` and
  `docs/releases/v1.0.0-beta.1.md` are the sources.
- Check these page claims against the app itself while there. They came from the old setup guide or
  general knowledge:
  - macOS: the "Open Anyway" step and the Xcode Command Line Tools requirement;
  - Linux: "Mark the file as executable";
  - the 12 GiB and 15 GiB space figures.
- Point the page's links at the result.

### S6. Owner confirmations

Record each answer here, then apply it to the page and the README.

- [ ] Remove "beta" from Mods and Widescreen? The page has removed both, following the README. v0.11.0's
  notes called mod support a beta.
- [ ] Is "Steam Deck hits a steady 60 fps at 2×" safe to say publicly? The README adds "in all game modes"
  and "with all HD texture mods". Vlad's note did not mention "at 2×"; the page keeps it from the README.
- [ ] One name for the generated textures. The app says "Upscaled Original Textures" and the page follows
  it. The docs also say "Faithful HD" and "Recomp HD".
- [ ] F9 bug bundles: the beta notes say to attach them, the README says to keep them local and share only
  text diagnostics. The page only says F9 saves one.
- [ ] Custom fighters, music, stages and game modes are now "coming up on the roadmap" on the page. The
  README and `docs/mod-manager.md` still call broader mods an unscheduled proposal. Item S8 follows this answer.

### S7. Review of the remaining page sections

Vlad has reviewed the features so far. Still to come: the hero, Screens, Install, Mods, News, FAQ,
Roadmap and docs, community and footer. Add his notes here as they arrive.

### S8. README pass for 1.0

Not started; Vlad wants to review its wording. Known issues:

- Top: the "v1.0.0 release candidate" paragraph and "Setup checks your dump".
- Launcher section: "Improved Game Launcher (v0.10.6)", the old screenshot and "Get the latest setup".
- Get started: the steps and table describe the wizard and `DefJamSetup` files. Use the app and the AppImage.
- Features: match the page's list and order. "Settings before and during play" describes the old launcher.
- HD textures: "a local upscaling model" is misleading. Setup uses Lanczos 4x on the CPU, with no AI model.
- Roadmap: "21:9 and 32:9 remain experimental" contradicts Features, the unified app is called
  "proposals/in progress", and "texture packs work today" leaves out language mods. Add the S11 entries.

### S10. Four new before/after sliders

Add four sliders to the Screens section. In each one:

- **Before:** original resolution, 4:3.
- **After:** widescreen, 2× resolution, all HD modpacks and graphics settings.

Scenarios:

- [ ] One v One fight in The Limit, Nyne vs Trejo
- [ ] A cutscene
- [ ] The main menu
- [ ] Map Select

**Questions for Vlad, before capturing:**

- Do the two existing sliders (original vs upscaled textures, 4:3 vs 16:9) stay, making six, or do these replace them?
- "All graphics settings": MSAA, anisotropic filtering, game texture levels and high-quality downscaling
  sharpen the picture, but the colour tint and film scratches change its look. Should those two be left off?
  (Suggested: yes.)
- Which cutscene?
- Is "Map Select" the Story map or the Battle stage select?
- Which platform should these be captured on? The page's fine print says captures are from the Mac, so
  update it if that changes.

**How:**

- Each pair must show the same moment. `docs/09-testing-harness.md` has seeded scenarios and `--step-shots`
  for fight steps, and `docs/scene-selector.md` covers playing a chosen cutscene. Match menus by the same
  screen, and say so in the caption if something on screen moves between shots.
- Game runs are serial: never start two at once, including across agents. Use disposable saves, and keep raw
  captures under the ignored `logs/`. Record the build used.
- Vlad approves each exported image, then pin it as in the rules above and add it to `MEDIA`. Keep each
  file about 1 MB or less, since six sliders load on one page.
- The slider code assumes one image holding two labelled panels side by side
  (`.compare__img` and `--bar` in `site/styles.css`). New pairs will probably be two separate files, so the
  slider needs to support that. The 4:3 side sits pillarboxed inside the 16:9 frame, as the existing
  widescreen slider does.
- Give each slider tags, alt text, an `aria-label` and a caption. Check the layout at phone width.

### S11. Three new Roadmap entries

Add to the Roadmap in `site/index.html` (`.roadmap`), with Vlad's wording:

- [ ] **Extensible Custom Fighter Mod support**
- [ ] **Custom and Restored Songs**
- [ ] **Additional HD Upscaling mods**, covering Story Fighter customization, and Stages and Level textures

The "Broader mods" entry already mentions custom fighters and music, so trim it to avoid repeating them, and
keep the Mods section's fine print and the "Mod support" feature card consistent. That makes nine entries,
which fills the grid. The README's roadmap table gets the same entries in S8.

### S9. Go-live

- [ ] Vlad, once: Settings → Pages → Build and deployment → Source: GitHub Actions.
- [ ] This PR lands before v1.0.0 is tagged. `pages.yml` must be on the tagged commit or the deploy will not run.
- [ ] Remove the line in `docs/releases/v1.0.0.md` saying the site was taken out of the release.
- [ ] Delete this file and mark the PR ready.
- [ ] After publishing v1.0.0, check the Pages run and that the live site shows v1.0.0 with app downloads.

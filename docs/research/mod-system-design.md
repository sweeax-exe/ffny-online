# Mod system: design proposal

2026-10-11 proposal (D104). **2026-10-23 implementation update:** the texture-only
signed catalogue, helper and shared launcher order are implemented in the
[integrated RC](integrated-menu-launcher-rc-2026-10-23.md), with acceptance and
protected publication gates tracked there. Generic overrides, recipe delivery and
code mods remain proposals. The sections below preserve the original design
context. It takes the backlog's
[item 4](../04-improvement-backlog.md) ("design the mod and asset-override system deliberately") and adds a
curated, remotely updated mod list that players manage from the launcher. Come back to the open questions in
section 12 before starting.

## 1. Goals and the owner's answers

The idea: a project-endorsed mod list that every installation fetches and keeps current. The player ticks
mods in a list, the chosen mods download and install, and a newer version shows as "Update available". Mods a
player drops into `mods/` by hand keep working beside it.

Vlad's answers of 2026-10-11:

| # | Question | Answer |
|---|---|---|
| 1 | Derived assets (upscales of the game's own textures, movies, audio) | The project **only links** to third-party hosts for them. The project itself hosts original content and local-generation recipes only. |
| 2 | Curation | Modders submit pull requests to the index. Each is reviewed and must pass the rules and the security scans. |
| 3 | Code mods | Data-only first; scripts or DLLs may follow later. |
| 4 | Where the network code lives | In a **helper application** for mod management, not the game. |
| 5 | Budget | Free or low-cost hosting. |
| 6 | Updates | Show "Update available"; the player chooses when to update. |
| 7 | Faithful HD pack | Stays generated locally. Local generation is an opportunity for more mod kinds, not only downloads. |

## 2. What exists today

- `<data>/mods/<pack>/manifest.ini` with `textures/<source-id>.png`, enabled by `[textures] packs=a;b` in
  `settings.ini` (later packs win), with the **Textures** page in the launcher and overlay
  ([hd-textures.md](../hd-textures.md), [settings reference](../settings-reference.md)). Packs are keyed by the
  SHA-256 of the original texture, so one pack works on every platform. The loader already rejects absolute
  paths, `..` and paths that leave the pack.
- The faithful HD pack is produced by Setup on the player's computer from their own dump; no artwork is
  downloaded. Setup carries a pinned Python with Pillow and NumPy on all three platforms
  ([setup-installer.md](../setup-installer.md)).
- No other override layer. Every guest file open goes through the toolkit's `xbox_translate_path`
  (`tools/xboxrecomp/src/kernel/kernel_path.c`), so replacing a whole loose file is a small change there. Most
  data sits inside EA BIGF `.viv` archives ([disc layout](disc-layout.md)), so the useful granularity is the
  archive **entry**.
- The game executable has no network code. Only Setup downloads (`setup/engine.py`).

## 3. Precedents

| Project | Mod menu | Remote list and download |
|---|---|---|
| Zelda 64: Recompiled (N64Recomp) | In-game list that toggles and configures mods; texture packs share it; `.nrm` format and a shared mod tool | Mods hosted on a Thunderstore community; players download by hand ("Manual Download"). No in-app download. |
| Northstar (Titanfall 2) | Yes | `VerifiedMods` repository: a JSON list of endorsed mods with a Thunderstore ID and a SHA-256 per verified version; the client downloads them automatically. **Closest to this proposal.** |
| Luanti (ContentDB) | In-game content browser | Install, dependency resolution and update badges from the project's own server, with a public API. |
| Celeste (Everest, Olympus) | Manager plus an in-game updater | Files on GameBanana; a central update database and checker server; dependency download. |
| Unleashed Recompiled | External Hedge Mod Manager | GameBanana one-click install and update checks; HMM supports several games. |
| Ship of Harkinian | Load-order menu over `.o2r` archives in `mods/` | None. |
| Cemu | Graphic packs window with checkboxes | Community packs come from a GitHub repository's releases (not re-verified for this note). |

The combination proposed here (a curated, reviewed, hash-pinned index that points at third-party hosts, plus
a subscribe-and-update list in the game's own launcher) is uncommon. Northstar has the endorsed list, Luanti
the in-app browser, Zelda 64 Recompiled the mod menu; no recomp project found does all three.

## 4. Architecture

```text
 index repo (GitHub)                    third-party hosts
 DefJamFFNY-mods                        GameBanana, Internet Archive, a modder's
   index.json  (signed)  ----links--->  GitHub releases, R2 bucket ...
   recipes/, original content
        |                                      |
        | HTTPS (only the helper talks to the network)
        v                                      v
 +-------------------------------------------------------------+
 | defjam-mods  (helper, headless, also a CLI)                  |
 |  fetch + verify index, download with resume, SHA-256 check,  |
 |  unpack, run recipes, write mods/<id>/, write state.json     |
 +-------------------------------------------------------------+
        ^  spawned with --json, progress on stdout     | writes
        |                                              v
 launcher "Mods" page (ImGui, existing)        <data>/mods/<id>/manifest.ini
   list, checkboxes, order, "Update available"  <data>/mods/state.json
        |                                       settings.ini [mods] enabled=
        v
 game executable (offline): loads enabled mods in order at start
```

### 4.1 Game side: one loader, several override kinds

Generalise today's texture pack into a mod with `schema=2`:

```ini
[mod]
schema=2
id=faithful-hd
name=Faithful HD
version=1.2.0
min_recomp=0.9.0
kinds=textures

[textures]
<source-id>=textures/<source-id>.png

[files]            ; whole guest files, guest path -> mod path
movies/intro.mad=files/movies/intro.mad

[entries]          ; one entry inside a .viv archive
assets/fe.viv:frontend/logo.xsh=entries/logo.xsh

[audio]            ; host-side stream substitution (future)
[movies]           ; host-side video substitution (future)
```

- `settings.ini` gets `[mods] enabled=a;b;c` (later wins) and keeps reading `[textures] packs` as a fallback.
- Precedence is one rule for every kind: the last enabled mod that has the key wins; the original is the floor.
- Overrides resolve once at start into a table; nothing is scanned per file open.
- The game never deletes or edits a mod. A malformed entry falls back to the original with a bounded log line,
  as texture packs do now.

### 4.2 The helper (`defjam-mods`)

A separate program, installed by Setup next to the game. It is the only component that uses the network.

- **Proposed shape:** a headless core with a CLI (`defjam-mods list|install|update|remove|recipe --json`),
  started by the launcher's Mods page, which reads its JSON progress lines. The player sees one familiar
  screen; the game executable stays offline. The same CLI works in a terminal and on a Steam Deck.
- **Language:** Python is the low-cost option, because Setup already ships a pinned Python with Pillow and
  NumPy on Windows, macOS and Linux, and the HD generation already runs in it; recipes would run there too.
  (Confirm that Setup leaves that Python in the installation; see section 12.)
- **State:** `<data>/mods/state.json` lists installed mods, versions, source (index or local) and the cached
  index's age. The launcher reads it to draw the list and the "Update available" badges without starting the
  helper.
- **Installing:** download to `*.part` with HTTP range resume, verify size and SHA-256, unpack into a temporary
  folder, validate the layout, then rename into `mods/<id>/`. An update installs beside the old version and
  switches only after verification; the previous version is kept until the next successful start (rollback).
- **Offline:** the cached index is used; Play is never blocked by the helper.

### 4.3 The index repository

A **separate** repository (proposed `vyanhursky/DefJamFFNY-mods`), so that a takedown aimed at a linked mod
never touches the port's repository. GitHub Pages serves `index.json` and its signature.

```json
{
  "schema": 1,
  "generated": "2026-10-11T00:00:00Z",
  "mods": [{
    "id": "brooklyn-night-stage",
    "name": "Brooklyn at night",
    "authors": ["someone"],
    "kind": ["textures"],
    "class": "linked",
    "licence": "CC-BY-4.0",
    "homepage": "https://gamebanana.com/mods/...",
    "versions": [{
      "version": "1.1.0",
      "min_recomp": "0.9.0",
      "files": [{
        "urls": ["https://.../brooklyn-1.1.0.zip", "https://archive.org/download/.../brooklyn-1.1.0.zip"],
        "size": 412345678,
        "sha256": "..."
      }],
      "depends": [], "conflicts": [],
      "reviewed": "2026-10-20", "revoked": false
    }]
  }]
}
```

- `class` is `linked` (files on a third-party host; required for anything derived from the game's assets),
  `original` (the author's own work; may be hosted by the index repository's releases) or `recipe` (section 5).
- Each version is pinned by size and SHA-256, so what was reviewed is what installs, wherever it is hosted.
- `index.json` is generated by CI from one file per mod (`mods/<id>.json`), so pull requests touch one file.
- The index is signed (minisign or ed25519) by a key that never lives in CI's public logs; the public key is
  built into the helper. A compromised Pages site or repository cannot push files the helper will accept.
- `revoked: true` withdraws a version: the helper shows a warning and offers to disable or replace it.

## 5. Mod kinds

1. **Data mods** (first): textures, loose files, archive entries; later audio and movie substitutions.
2. **Recipe mods**: a declarative description of how to make a mod on the player's computer from their own
   dump, the way the faithful HD pack is made today. A recipe names inputs (texture categories, archive
   entries, movie files), a tool from an **allow-list pinned by the helper** (the existing Lanczos path in
   `scripts/hd_assets.py`; later, for example, a Real-ESRGAN ncnn-Vulkan build for textures and movies, or an
   audio resampler), model weights by URL, SHA-256 and licence, and parameters. Output is an ordinary data mod
   in `mods/<id>/`. A recipe is data, never a script, so reviewing one means reviewing parameters and a licence.
   This is how upscales stay within answer 1: the index distributes the method, the player's computer makes
   the pixels.
3. **Code mods** (later): see section 6.3.

## 6. Review rules and security

### 6.1 Submission rules (for `CONTRIBUTING` in the index repository)

- One mod per pull request, one file under `mods/`, versions append-only.
- Linked files only for anything derived from the game; original work states its licence.
- No game executables, no full archives copied from the disc, no ISO or dump content.
- Credits and permissions for any third-party work used; the reviewer may ask for proof.
- Size and preview image required; a short description of what changes.

### 6.2 Automated checks (CI on every pull request and nightly)

- Schema validation of `mods/<id>.json`.
- Download every new file, check size and SHA-256, and fail if any mirror differs.
- Archive layout: allow-listed extensions only (`.ini`, `.png`, `.dds`, `.wav`, `.ogg`, `.flac`, `.mad`, `.xsh`,
  ...); no executables, scripts, links, absolute paths or `..`; no file over a size limit; a valid
  `manifest.ini` that loads with the game's own parser.
- Malware scan (ClamAV in CI; optionally a VirusTotal lookup by hash, free tier).
- Nightly link check: a dead mirror opens an issue; a changed hash revokes the version automatically.

### 6.3 Code mods later

Only once data mods work. Proposed minimum: public source; built by the index repository's CI from a tagged
commit, never uploaded as a binary; a versioned mod API in the game with an ABI check at load; a clear
"runs code" warning and a separate opt-in in the launcher. There is no sandbox for native code, so review is
the only protection; this is the main reason to keep data and recipe mods first.

## 7. Update flow

1. The helper refreshes the index at most once a day (and on demand from the Mods page).
2. If an installed mod has a newer non-revoked version, `state.json` marks it and the launcher shows
   "Update available" on the mod and a count on the Mods tab.
3. The player clicks Update (or Update all). The download, verification and switch happen as in 4.2.
4. A player can pin a version; pinned mods never show updates.

## 8. Hosting

| Option | Cost | Fit |
|---|---|---|
| GitHub Pages | Free | The index and its signature. |
| GitHub Releases in the index repository | Free | Original content and recipe tools. Each file under 2 GiB, 1,000 files per release, no bandwidth cap. |
| Cloudflare R2 | 10 GB free, then about $0.015 per GB-month; no egress fees | Low-cost mirror for original content if Releases become awkward. |
| Internet Archive | Free | Mirror for linked content; every item also gets a torrent with a web seed. Slow; anyone can file a takedown. |
| GameBanana | Free | Where modders upload linked content themselves; the index points at their files. |
| Thunderstore | Free | A community needs a request on their Discord showing existing modder interest. Revisit when there are mods. |
| Google Drive, MEGA, MediaFire | Free | Mirrors at most: large files hit a virus-scan page or quotas and links change, so unattended downloads fail. |
| BitTorrent in the helper | Free | Needs libtorrent; not for v1. The Internet Archive torrents cover the case. |

With answer 1, the project hosts only the index, recipes and original content, which fits within free GitHub
hosting for the foreseeable future.

## 9. Feasibility by mod type

| Type | Status | What it needs |
|---|---|---|
| Texture packs | Works now | Only the index, the helper and the Mods page. Recipes for other upscalers. |
| Audio packs, new tracks | Research | Music, effects and speech are inside `audio/*.viv`. Either entry overrides plus encoding to the game's EA audio format, or host-side substitution of a stream by name. Replacing an existing track is far easier than adding tracks (the track tables are fixed). |
| Movie (FMV) packs | Research | `.mad` movies are decoded by game code at their original size. HD needs a host-side player that replaces that decode (overlaps the widescreen branch's FMV to-do). |
| Fighter and stage replacements | Research | `fighter.xml` and `venue.xml` are data-driven; replacing a model, skin or stage art with entry overrides is plausible. |
| New fighters and stages | Long shot | Likely fixed tables, memory budgets and the save format. Needs a spike before any promise. |

## 10. A generic pattern for other recomps

Yes, there is room for one, because the hard parts are not game-specific. The split that would travel:

1. **Index spec**: the JSON schema of section 4.3, published with a JSON Schema file and a reference signer.
   Any recomp can host its own index; a game is identified by an id in the index header.
2. **Index repository template**: the CI of section 6.2 as a reusable GitHub Actions workflow (schema, hash,
   layout allow-list, ClamAV, link check, index build and signing). A new project forks the template and
   edits one config file (allowed extensions, size limits, game id).
3. **Helper core**: game-agnostic download, verify, install, update and rollback, driven by a small game
   profile (data folder, mods folder, how to enable mods: a `settings.ini` key or a list file). Recipe tools
   plug in per game.
4. **Game-side contract**: a documented `manifest.ini` / precedence rule; each recomp implements its own
   override hooks (file, archive entry, texture hash), because those are runtime-specific.

Precedents for shared tooling exist (r2modman and Thunderstore across Unity games, Hedge Mod Manager across
Sonic titles, the N64Recomp mod tool across N64 recomps), but none ships a curated, signed, hash-pinned index
for native recomps. Recommendation: build it for this port first with these seams kept clean (no Def Jam names
in the helper core or the index CI), then extract it once a second project wants it, possibly
[xboxrecomp](https://github.com/sp00nznet/xboxrecomp) titles first since they share the runtime.

## 11. Phased plan

1. **Local mods, one loader.** Manifest `schema=2`, `[mods] enabled`, whole-file and archive-entry overrides
   (toolkit `kernel_path.c` and a BIGF entry table), a Mods page listing local mods with order and checkboxes.
2. **Index and helper for texture packs.** Index repository, CI, signing; helper with download, verify,
   install, update and rollback; "Update available" in the launcher.
3. **Recipes.** Move the faithful HD generation behind the recipe format; add one more upscaler.
4. **Audio and movies.** Host-side substitution research and implementation.
5. **Fighter and stage spike**, then code mods if still wanted.
6. **Extract the generic pattern** if another project is interested.

## 12. Open questions

1. Helper language and form: a Python CLI on Setup's bundled Python, driven by the launcher (proposed), or a
   native program? Does Setup keep its Python in the installation after building?
2. Name and owner of the index repository (`DefJamFFNY-mods` under the same account, or an organisation so
   other maintainers can review).
3. Who holds the index signing key, and whether CI signs (key in a protected environment) or Vlad signs
   releases of the index by hand.
4. Reviewers besides Vlad, and the turnaround to promise modders.
5. Whether the launcher's Textures page becomes the Mods page or the two stay separate.
6. Whether the index may list linked mods hosted on sites with download interstitials (Drive, MEGA) as
   "manual download" entries the helper opens in a browser instead of fetching.

## Sources

- Zelda 64: Recompiled modding: [GamingOnLinux](https://www.gamingonlinux.com/2025/05/zelda-64-recompiled-majoras-mask-adds-modding-support-texture-pack-support-optimizations-and-more/page=1/), [Thunderstore community](https://thunderstore.io/c/zelda-64-recompiled/)
- Northstar verified mods: [VerifiedMods (fork with the README)](https://github.com/Alystrasz/VerifiedMods), [Northstar blog](https://northstar.tf/blog/mad-update/)
- [Luanti ContentDB](https://docs.luanti.org/about/contentdb/)
- Celeste: [Olympus](https://flathub.org/apps/io.github.everestapi.Olympus), [Everest wiki: uploading mods](https://github-wiki-see.page/m/EverestAPI/Resources/wiki/Uploading-Mods)
- [Unleashed Recompiled: installing mods](https://mintlify.wiki/hedge-dev/UnleashedRecomp/mods/installing-mods)
- [Ship of Harkinian mod menu change](https://code.moparisthebest.com/mirror/Shipwright/commit/dc5e9686c0a5c1b4f1cdf285ad8bf581be6233e1)
- [GitHub: about releases (limits)](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)
- [Thunderstore: adding a new game](https://wiki.thunderstore.io/ecosystem/adding-a-new-game)
- [Internet Archive torrents](https://help.archive.org/help/archive-bittorrents/)
- Cloudflare R2 pricing (third-party summary, check the official page): [flarecalc](https://flarecalc.com/calculators/r2)

# Texture authoring and runtime integration

Use [DefJam Modding Tools](https://github.com/vyanhursky/DefJam-Modding-Tools)
for texture extraction/review, optional upscale auditions, native atlas assembly,
comparison viewers and export. Start with its [workflow](https://github.com/vyanhursky/DefJam-Modding-Tools/blob/main/docs/WORKFLOW.md),
[menu lettering guide](https://github.com/vyanhursky/DefJam-Modding-Tools/blob/main/docs/menu-text-restoration.md) and
[menu exporters](https://github.com/vyanhursky/DefJam-Modding-Tools/blob/main/docs/menu-exporters.md). Keep the existing private corpus,
workspace, exact prompts/revisions, approvals and backups intact. Reopening a
review must not regenerate accepted artwork or reset its history.

This game repository maintains runtime texture replacement, manifest compatibility,
launcher/mod management, native tests and the frozen HD code used by Setup and
branding. `scripts/hd_assets.py`, `hd_corpus.py`, `hd_corpus.html` and
`config/hd-runtime-aliases.json` are the deliberately pinned compatibility snapshot;
`scripts/build-hd-pack.py` remains the game-side Faithful HD generator. Changes to
that snapshot require an explicit pin update and setup compatibility tests.

General authoring development belongs in the tools repo. Former standalone menu
export scripts are packaged there as `defjam-tools menu-*` commands; old neural
auditions are `defjam-tools audition` and `defjam-tools swinir`. Pack-specific JSON
recipes, approval ledgers and checkpoint paths are private inputs, not game config.

See [the first-pack guide](texture-modding-getting-started.md) for runtime capture,
manifest layout and loading a local pack; [HD textures](hd-textures.md) covers game
settings and Setup. Published versions, hashes, download locations, credits and
concise release notes belong in the [mod catalogue](https://github.com/vyanhursky/DefJamFFNY-mods).
Validate candidate packs with isolated saves and serial native game runs. Artwork
approval, runtime acceptance and signed publication are separate stages.

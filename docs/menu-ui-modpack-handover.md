# HD menu pack integration and ownership

HD Widescreen Menus is independently released through the signed mod catalogue.
The latest combined pack in this handoff is 1.4.0: 1,343 approved reconstructed
mappings, 601,895,156 download bytes and 605,772,797 unpacked bytes. It requires
recomp 0.12.1 with `textures-v1` and `wide-v1`. Refresh the launcher catalogue,
update the mod and restart the game; the texture pack does not require reinstalling
the game. Faithful HD remains optional and separate.

- [Release metadata and notes](https://github.com/vyanhursky/DefJamFFNY-mods/blob/main/docs/menu-ui-hd-wide.md) belong to DefJamFFNY-mods.
- [Restoration/review procedure](https://github.com/vyanhursky/DefJam-Modding-Tools/blob/main/docs/menu-text-restoration.md),
  [exporters](https://github.com/vyanhursky/DefJam-Modding-Tools/blob/main/docs/menu-exporters.md), [native registration](https://github.com/vyanhursky/DefJam-Modding-Tools/blob/main/docs/menu-native-registration.md)
  and [lossless PNG staging](https://github.com/vyanhursky/DefJam-Modding-Tools/blob/main/docs/lossless-png.md) belong to Modding Tools.
- The original authoring workspace and independent private backups contain raw
  images, exact prompts, drafts, approvals, detailed ledgers, viewer data and
  checkpoint manifests. Preserve them in place. A Git clone is not their backup.

## Measured runtime compatibility

The exact 1.4 ZIP passed the unchanged released 0.12.1 installer and a real signed
network update from 1.3, installed/backup hash checks and rollback to 1.3. Its
isolated save marker remained unchanged. Six serial native runs covered front
menus at 16:9, 4:3 and 32:9, Battle/FFA loading and fight start, and Story Crib/Map/Gym:
35 captures, 420 distinct replacement IDs loaded, including 290 newly approved IDs.
Saves restored byte-identically with no measured runtime faults or permanent
replacement rejections. This is representative coverage, not every source,
language or loading-banner variant. Detailed evidence remains private.

The [fire-escape geometry fix](research/menu-ui-v1.1-2026-10-26.md) is separate game
code. Retain loader, renderer, launcher, installer and native diagnostic changes
here. Runtime aliases and the HD setup compatibility snapshot remain pinned.

## Repository boundary

Production batch records and private backup pointers are excluded from game
source. General tooling uses synthetic fixtures and explicit private input paths.
Do not import authoring history into a game release branch or commit art, captures,
saves, generated code or pack ZIPs. Older production records were archived before
cleanup; their source IDs, schemas, hashes and original paths remain intact.

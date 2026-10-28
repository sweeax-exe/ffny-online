# Mod management

The Mods page has installed local/project packs, a curated project catalogue and
one shared Load Order window. Later enabled packs win. Downloaded packs are separate
from locally generated Faithful HD; install HD Widescreen Menus after its optional
Faithful baseline. Mod installation never changes the enabled list automatically.

The launcher starts `setup/mods.py` with the installed version's retained Python.
It passes argv directly, polls a bounded atomic INI projection and stays responsive.
The game executable itself makes no network requests. Developer checkouts may set
`DEFJAM_MOD_PYTHON` to a Python with Pillow and cryptography; broken installed Python
must be repaired rather than silently using an unrelated system interpreter.

```sh
python setup/mods.py status --data-dir /path/to/data --runtime 0.10.6
python setup/mods.py refresh --data-dir /path/to/data --runtime 0.10.6
python setup/mods.py install --id example-pack --data-dir /path/to/data --runtime 0.10.6
python setup/mods.py rollback --id example-pack --data-dir /path/to/data
python setup/mods.py remove --id example-pack --data-dir /path/to/data
```

Testing builds offer an explicit "Show testing versions (release candidates)" checkbox.
It affects catalogue choices and installs for this launcher session; stable-only
is the default. Turning it off does not remove an installed testing version.
The CLI equivalent is `--prerelease` for status, refresh and install.

These commands are examples; catalogue publication is tracked separately.
`--capability wide-v1` is appropriate only for a runtime with the wide-layer loader.
The launcher derives that capability from the toolkit compiled into the game.
Current main's toolkit lacks it; the separate menu/PR73 integration must land before
wide menu downloads can be offered as compatible. Unsupported game data/code/recipe
mods are outside this first texture-only installer. General schema-2 overrides are
future work. Full storefront/Quiver adapters are also future work; see the
[storefront assessment](research/mod-storefront-integration-2026-10-21.md).

State lives at `<data>/mod-manager/`, outside runtime pack discovery:

- `catalogue.json`: signed exact bytes, signature, highest accepted revision/hash.
  The public key is pinned in `config/mods/catalogue-keys.json`, not fetched from
  the same site as the index. Empty/missing trust fails closed for fresh installs.
- `state.json`: managed receipts, stable folders, exact installed-file hashes and
  one previous-version receipt. Local matching folder names never establish ownership.
- `downloads/`: size/hash-addressed `.part` and verified ZIP cache. Resume accepts
  correct byte ranges; a server ignoring Range restarts the file, never appends it.
- `staging/` and `rollback/`: private same-filesystem complete folders. Journaled
  renames and state replacement recover process interruption. POSIX directory fsync
  and Windows write-through moves order commit durability; power-loss semantics
  still depend on the filesystem/hardware.
- `writer.lock`: OS-held single helper writer; `runtime.lock`: exclusive mutation,
  shared lifetime game use, including launch without UI. Stale filenames are harmless.
- `journal.json`: interrupted commit marker. Never delete this to bypass a recovery
  error without inspecting the preserved target/rollback files and receipts.
- `view.ini` / `progress.ini`: bounded atomic UI projections, never precedence authority.

The helper rejects unsafe archive paths, links, special/encrypted entries, case
collisions, unknown extensions, excessive expansion, unsupported manifest grammar,
missing/invalid referenced PNG/DDS files, incompatible capabilities and modified
managed/rollback folders. Runtime validation still owns comparison of replacement
scale/layout against a source texture seen in the game. Never treat a validator
pass as proof that a visual mapping is correct.

Catalogue source is [DefJamFFNY-mods](https://github.com/vyanhursky/DefJamFFNY-mods).
Publishing checks append-only history and every mirror, validates packages and
runs ClamAV in a job without signing credentials. Only a separate owner-approved
job signs metadata matching the reviewed source. Artwork stays on external hosts;
no artwork, private keys or executable game code enter either public source tree.

## Catalogue operations

The protected publisher issues metadata with a30-day expiry. New installs and
updates require fresh verified metadata; installed packs and ordinary offline
Play remain available. Republish through the reviewed main workflow and approve
the catalogue-release environment before expiry, even when package versions have
not changed. Revisions increase and existing package records stay immutable.

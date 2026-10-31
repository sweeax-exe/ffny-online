# v0.13.0 candidate regression

Run the same release PR head and toolkit `d569ca1` on Windows, macOS and Linux.
The owner runs macOS/Linux; the release agent runs Windows. Keep games serial
on each machine. Use a disposable data root with a copy of the local save fixture;
the harness still verifies byte-identical save restoration. Link `game` to the
verified extracted dump, and link `<test-data>/extracted` to the same dump.

After fetching the release branch, initialize submodules and perform a fresh
analyze/lift/build (the new manual wrappers cannot use a previous generated tree).
On macOS/Linux, with `DEFJAM_DATA` pointing to the disposable root:

```sh
git submodule update --init --recursive
python3 scripts/pipeline.py analyze --no-archive
python3 scripts/pipeline.py recomp
python3 scripts/pipeline.py build --preset posix-release
python3 setup/mods.py refresh --data-dir "$DEFJAM_DATA" --runtime 0.13.0 --capability wide-v1
python3 setup/mods.py install --data-dir "$DEFJAM_DATA" --runtime 0.13.0 --capability wide-v1 --id menu-ui-hd-wide --version 1.4.0
python3 setup/mods.py install --data-dir "$DEFJAM_DATA" --runtime 0.13.0 --capability wide-v1 --id hd-fighters --version 1.0.0
python3 scripts/regress.py --preset posix-release --release-profile
```

On Windows use `python`, `scripts/env.ps1`, `pipeline.py analyze/recomp` and
`scripts/build.ps1 -Preset win-x64-release`, then the same mod commands and:

```powershell
python scripts/regress.py --preset win-x64-release --release-profile
```

`--texture-root PATH` can point to an existing managed mods folder. The profile
refuses any version other than Menus 1.4.0/Fighters 1.0.0 and verifies every installed
file against its receipt. It records archive/manifest identities in
`logs/release-profile.json` and in each regression run. The HD lane uses a 2048 MiB
texture cache explicitly; report that budget with results (#141 remains open).

Every gameplay, scenario, soak and controller run receives 16:9 and both packs,
even when scenario isolation clears inherited diagnostics. Original-image
`m2/m3/m4a` checks run separately with stock 4:3. Their reference hashes stay intact.
The native launcher/overlay checks keep their existing Windows SKIP qualifications.

New full-suite rows (also available with `--only`):

- `french`: French language-table selection, main-menu reachability and capture.
  Review `logs/shots/boot` for MENU PRINCIPAL, CHOISIR and RETOUR with readable text.
- `subtitles-en`: English sc05, forced all-cutscene subtitles and six timed
  captures under `logs/shots/subtitles-en`. Review multiple visible English lines.
  A subtitle-enable log alone does not certify rendered glyphs or timing.
- `input-trace`: two serial SDL/USB/XDK/cache/fighter pulse trials, late refresh
  off/on. Each requires all 40 presses through nine stages, no trace loss, no
  retention discard, no game-step gap above 100 ms. Enabled refresh must apply
  reports and have matching shadow conversions with no read errors. Evidence
  stays under `logs/input-audit/regress-*` (CSV, checkpoints, JSON and output).

Send back head SHA, OS/GPU, complete regression table/report, profile identities,
French/subtitle capture-review verdict and any failure's triage output. Keep raw
logs, saves, screenshots, audio and built game executables local.

The full regression does not replace setup install/Clean Update from v0.12.3 or
the owner's combined-candidate playtest. Leave those release gates pending until
they are actually performed. Confirm failures with `--only`, without updating goldens.

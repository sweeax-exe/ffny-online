# v0.13.0 regression reproduction

> Dated evidence: this records the named build/release, not current pending work.
> Current support and qualifications are in the release notes and GitHub Issues.

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
python3 scripts/build-hd-pack.py --root "$DEFJAM_DATA/extracted" --work "$DEFJAM_DATA/hd-work/setup-lanczos" --mods "$DEFJAM_DATA/mods" --receipt "$DEFJAM_DATA/lanczos-pack-build.json" --aliases config/hd-runtime-aliases.json
python3 scripts/regress.py --preset posix-release --release-profile --lanczos-receipt "$DEFJAM_DATA/lanczos-pack-build.json"
```

On Windows use `python`, `scripts/env.ps1`, `pipeline.py analyze/recomp` and
`scripts/build.ps1 -Preset win-x64-release`, then the same mod commands and:

```powershell
python scripts/build-hd-pack.py --root "$env:DEFJAM_DATA/extracted" --work "$env:DEFJAM_DATA/hd-work/setup-lanczos" --mods "$env:DEFJAM_DATA/mods" --receipt "$env:DEFJAM_DATA/lanczos-pack-build.json" --aliases config/hd-runtime-aliases.json
python scripts/regress.py --preset win-x64-release --release-profile --lanczos-receipt "$env:DEFJAM_DATA/lanczos-pack-build.json"
```

`--texture-root PATH` can point to an existing managed mods folder. The profile
refuses any version other than Menus 1.4.0/Fighters 1.0.0 and verifies every installed
file against its receipt. It records archive/manifest identities in
`logs/release-profile.json` and in each regression run. The HD lane uses a 2048 MiB
texture cache explicitly; report that budget with results (#141 remains open).

The owner added full Lanczos 4x textures to the candidate on October 31, after the
initial two-mod regression and memory investigation. `--lanczos-receipt` verifies
both generated mip-policy folders against their retained corpus image hashes,
recipe identity and runtime aliases. It records corpus/manifest digests and loads
Lanczos first, then Menus, then Fighters, so the specialized mods retain priority.
Keep the generation cache and receipt together on each test host. The first
Windows two-mod results do not certify this expanded configuration. The owner
approved the playtest; the all-pack Windows retest finished16/17 with memory
growth failing again on a targeted repeat. The longer all-pack menu observations
pass, while the Lanczos-only control fails. Keep every result and the64MiB gate;
see the [post-playtest memory report](research/rc-v0.13.0-hd-memory.md#post-playtest-full-lanczos-configuration).
Mac/Linux runs are now complete with recorded qualifications; see the release
notes and PR160 comments. The owner authorized release on November1. Local setup
install/Clean Update was not performed; memory follow-up is issue166.

Every gameplay, scenario, soak and controller run receives 16:9 and all selected packs,
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

For the Windows combat memory-growth failure, the exact rerun and all four pack
controls passed the existing 64 MiB gate. The original full run's failure is
retained. See the [HD memory investigation](research/rc-v0.13.0-hd-memory.md) for
phase measurements and limits. A reusable comparison on any configured host is:

```sh
python3 scripts/memory-pack-audit.py --fixture logs/fixtures/YOUR_FIXTURE --texture-root "$DEFJAM_DATA/mods" --output logs/memory-pack-comparison
```

It keeps 16:9 and a 2048 MiB texture budget, verifies both exact mod versions,
compares no packs/Menus/Fighters/both serially, and preserves each scenario's
failure status. `--groups both --result-tail 60 --hold-menu` records a longer
post-match menu observation. Compare Windows private bytes and POSIX RSS only
within the same host; the harness uses different memory metrics on those hosts.

After the owner finishes the Lanczos playtest, compare the previous two-mod lane,
Lanczos alone, and the combined configuration (same fixture, host and native build):

```sh
python3 scripts/memory-pack-audit.py --fixture logs/fixtures/YOUR_FIXTURE --texture-root "$DEFJAM_DATA/mods" --lanczos-receipt "$DEFJAM_DATA/lanczos-pack-build.json" --groups both,lanczos,all --result-tail 60 --hold-menu --output logs/lanczos-memory-comparison
```

Do not overlap the owner's playtest and automated runs. The existing 64 MiB memory
growth limit and original-image goldens remain unchanged.

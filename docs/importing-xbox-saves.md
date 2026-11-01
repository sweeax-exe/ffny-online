# Importing Xbox saves

Profiles from the original Xbox game (a console, an emulator, or save zips shared by the community)
do not load by simply copying them into the save area: the game lists the profiles and then shows
**Damaged game**. Each profile file carries signatures made with the key of the console that wrote
it, and the port cannot reproduce that key. `scripts/save-import.py` copies the profiles and signs
them again with the key the port uses, which is the only change it makes to the data.

Tested so far: one Story profile from a community-shared Xbox save, imported with this tool, loaded
and reached the Story map with its unlocks (macOS, October 2026). Options files, other profiles
and Windows have not been tried. Saves are the game's own data, so they are not published in this
repository.

## Steps

1. **Get the save folder.** You need the profile folders from the console's `E:\UDATA\45410049\`,
   or a `.zip` that contains them (a Pro Action Replay zip is fine). Each profile is a folder with a
   `SaveMeta.xbx`, a file named like the profile, and usually a `saveimage.xbx`. Any tool that can
   read the console's E: drive works (for example FTP from a modded dashboard, or a FATX browser on
   the hard disk or emulator disk image); we have not tested the copying step itself.
2. **Close the game.**
3. **Run the tool** with any Python 3 (it uses only the standard library). Your *data folder* is the
   one you chose in setup; the save area is its `save` folder.

   ```
   python scripts/save-import.py my-saves.zip --save-dir "<data folder>/save"
   ```

   It prints what it found and what it did. If `DEFJAM_DATA` is set, or the macOS default install is
   present, `--save-dir` can be left out; `--install <folder>` finds the data folder from an install.
4. **Start the game** and pick the profile on the Select User ID screen.

Useful options: `--dry-run` (say what would happen, write nothing), `--check` (only report whether
the signatures are valid), `--force` (replace a profile that already exists; the old one is moved to
`<save area>/import-backup-<time>/` first).

## What the tool does and does not do

- Copies `SaveMeta.xbx`, the profile file and `saveimage.xbx` of each profile into
  `UserData/45410049/<folder>`. The folder is named from the profile name the way the game does it,
  even when the source folder had another name.
- Skips files other tools added (for example `datelinfo.xbx`) and adds `TitleMeta.xbx` and
  `TitleImage.xbx` only when the save area has none.
- Never overwrites an existing profile unless you pass `--force`. Files that do not look like a
  profile (wrong size or header) are skipped with a message, not modified.
- Saves made by the port will not load on a real Xbox; this is one-way.

## How it works

The profile file starts with a 76-byte header: `20CM`, the total size, the header size (`0x40`), the
payload size, then three 20-byte HMAC-SHA1 fields at `0x10` (over the 64-byte identity block at
`0x4C`), `0x24` (over the payload from `0x8C`) and `0x38` (over the first 56 bytes, so over the other
two). The game checks the `0x38` field first and stops at the first mismatch. The key is the kernel's
`XboxSignatureKey`, which the port exports as 16 zero bytes. The tool recomputes the three fields
with that key; `tests/unit/test_save_import.py` fails if the kernel stops exporting zeros, because
imported saves would then stop verifying. Details and the trace that found this are in
[issue #154](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/154).

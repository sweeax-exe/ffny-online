# One app for setup, launcher and updates (design, 2026-10-26)

Status: **proposal**, nothing implemented. Owner's answers of 2026-10-26 are D214; his answers to
the §11 questions, the same day, are D215. Reference
point: Dusklight v2.0.3 (TwilitRealm's Twilight Princess port), whose one app selects the disc,
becomes the launcher and offers GitHub updates.

## 1. What the owner decided

| # | Decision |
|---|---|
| 1 | Game code is still never distributed. The app builds the game on the player's machine. |
| 2 | No precomputed analysis metadata in releases now. It stays a possible later step (§5.4). |
| 3 | No Apple Developer ID or Windows code signing for now. |
| 4 | Explore moving the launcher out of the game and into the app entirely. |
| 5 | Add to Steam on Windows and macOS too, not only Linux. |
| 6 | A ~10-minute first install is fine. Shorten **updates** only where it is safe. |
| 7 | Explore an AppImage for Linux with every dependency inside it: `.exe`, `.app`, `.AppImage`. |
| 8 | The HD texture upscaler stays supported. |
| 9 | The new release asset names and payload contents (§3.3, §6.1) are approved (D215). |
| 10 | The Linux compiler spike tries Zig first (§6.3). |
| 11 | The Windows `.exe` installs itself: to `%LOCALAPPDATA%\Programs\DefJamRecompiled\` by default, or to a folder the player chooses (§3.4). |
| 12 | After a game session the app closes. |
| 13 | Update checks are on by default. |

## 2. Why Dusklight takes seconds and we cannot

Dusklight is built on the Twilight Princess **decompilation**: hand-written source code that its
team compiles and ships as a 49 MB executable. The disc only supplies the game's files, which it
reads straight from ISO, RVZ, WBFS or CISO through the `nod` library. The "recompile" a player
sees is a hash of the disc (strings in the binary: "Disc verification", XXH3). Its macOS app is
signed with a Developer ID and notarized.

We are a **static recompilation**. The game's code is EA's Xbox code turned into C, so it can only
be built on the player's machine. Measured stage times:

| Machine | Analyze | Lift | Build | Setup end to end |
|---|---|---|---|---|
| Windows (v0.12.1 gates, D213) | 162 s | 71 s | 112 s | install 6 min 31 s; Clean Update 6 min 28 s |
| M1 Pro (D204) | 114 s | 65 s | 41 s | — |
| Steam Deck (v0.16 ledger) | 250 s | 138 s | 110 s | 7 min 56 s (container) |

An update today costs as much as a fresh install: `Engine.prepare_source` returns early for
`update`, `downgrade` and `rebuild`, so analysis, lift and objects are always fresh.

## 3. The app

One game-free program per platform, **Def Jam Recompiled**:

- **Contents.** The setup engine and its payload (reviewed source, toolkit, Python, pinned tools
  and libraries), the launcher screens, the updater and the Steam integration.
- **What stays on the player's machine.** The locally built game, under the install folder's
  `versions/`, as today.

```
first start ─ no verified build ─▶ choose dump ▶ options (HD textures, shortcuts, Steam)
                                    ▶ verify ▶ analyze ▶ lift ▶ build ▶ (HD upscale) ▶ Home
later starts ─ build matches app ──▶ Home: Play · Settings · Mods · Textures · Maintenance
             ─ app is newer ───────▶ Home with "Update ready: build v0.13.0 (~3 min)" and
                                     Play (previous build) still available
Play ─▶ the app holds play.lock, sets DEFJAM_DATA and the working directory,
        starts the game with --no-launcher, waits for it, then closes (D215)
```

### 3.1 What it replaces

| Today | In the app |
|---|---|
| Three setup front ends: Win32 C++ (`setup/main.cpp`), Swift (`setup/macos/Setup`), Tk (`setup/linux/wizard.py`) | One SDL3 + Dear ImGui front end, driving the **same** `engine.py` CLI through the existing `--status-file`, `--cancel-file` and `--releases-file` protocol |
| Three play wrappers: `DefJamLauncher.exe`, `setup/macos/Launcher/main.swift`, `setup/linux/launcher.sh` | The app's Play: receipt, play lock, logs, environment |
| The launcher window inside the game executable | The app's Home and Settings screens; the game keeps only the F1 overlay |
| Update / Repair: choose a setup file and open it | Native, in the app (§4) |

The engine remains the single implementation of verification, the pipeline, activation and
uninstall, so the safety work behind it (locks, receipts, activation journal, attempt folders)
carries over unchanged.

### 3.2 Moving the launcher out of the game: feasibility

Each file's references to game-side code, on `main` (`9c5c2d2`):

| File | Lines | Depends on |
|---|---|---|
| `pc_launcher_home.cpp`, `pc_mods.cpp`, `pc_ui_actions.cpp`, `pc_ui_settings.c` | 99, 231, 326, 50 | nothing game-side |
| `pc_launcher_sdl.cpp` | 321 | `host_posix_call_on_main` (the macOS main thread; the app is on it already) |
| `pc_ui.cpp` | 2,164 | toolkit settings (`recomp_settings_*`), toolkit input map and pad state (`xbox_InputMap_*`, `xbox_HostInputRawPad`, `xbox_HostInputRumbleTest`); **overlay only:** `scene_launcher_*`, `dev_module_*`, `d3d8_vk_set_overlay` |
| `pc_settings.c` | 606 | the setting tables are plain data; the apply functions call the present path and the widescreen patch |

None of it touches lifted code. The plan:

1. **Settings catalogue.** Split `pc_settings.c` into a data-only catalogue
   (`pc_settings_catalog.c`: the `RecompSetting` tables and enum names) and the game-side apply
   code. Both the app and the game register the same catalogue, so the Settings screens cannot
   drift between them.
2. **Overlay-only pieces.** Compile `pc_ui.cpp` twice, once for the game and once for the app with
   `DEFJAM_APP`. In the app's build:
   - the Debug page's Story-scene and developer-menu controls are hidden, because they act on a
     running game and stay in the overlay;
   - the overlay entry points are stubbed.
3. **Input.** The Controllers page's live pad view and rumble test need the toolkit's input host
   without the rest of the runtime. Either add a small `xboxrecomp_input` library target in the
   fork, or let the linker drop the unused objects from the static `xboxrecomp` library. A spike
   decides which.
4. **Rendering.** SDL3's renderer on all three platforms: D3D11 on Windows, Metal on macOS, Vulkan
   or OpenGL on Linux. That replaces `pc_launcher.cpp`, the Win32 D3D11 launcher, in the app. The
   Windows payload already carries SDL3 source.
5. **Version skew.** After the app updates itself, it may hold a newer settings catalogue than the
   build it launches. This is safe: `recomp_settings.c` keeps unknown lines (`keep_unknown`), so
   an older game preserves newer keys and the reverse.
6. **In the game.** The pre-game launcher stays behind `--launcher` for development and harness
   runs, and is skipped by default once the app ships.

The same reasoning already covers `DefJamLauncher.exe`: a game-free launcher binary shipped in the
Windows payload. The app extends that precedent.

### 3.3 Packaging per platform

| Platform | Release asset (proposed) | First run | Stable path (Steam, shortcuts) |
|---|---|---|---|
| Windows x64 | `DefJamRecompiled-<v>-windows-x64.exe` | Installs itself to `%LOCALAPPDATA%\Programs\DefJamRecompiled\` or a folder the player chooses, adds a Start-menu entry, then continues there (§3.4) | That copy |
| macOS arm64 | `DefJamRecompiled-<v>-macos-arm64.zip` holding `Def Jam Recompiled.app` | "Open Anyway" once. App translocation runs an unsigned app from a random read-only path, so the app asks to be moved to `/Applications` (or `~/Applications`) and refuses to build from a translocated path | The moved bundle |
| Linux x86-64 | `DefJamRecompiled-<v>-x86_64.AppImage` | `chmod +x` (or the file manager), then run. Offers to copy itself to `~/Applications/` with a desktop entry | That file, renamed without the version so updates keep the name |

Release-asset names are pinned by AGENTS.md §3 and `scripts/check-setup-assets.py`. The owner
approved these names and the new payload contents on 2026-10-26 (D215); AGENTS.md §3 and the
checker change in the same PR that first publishes them. For one or two releases the old `DefJamSetup-*` assets would ship alongside, so a release-picker
setup (PR #18) keeps working.

### 3.4 Windows: what Dusklight does, and what we do

Read from Dusklight v2.0.3's source (`src/dusk/data.cpp`, `src/dusk/ui/prelaunch.cpp`) and the
`encounter/borealis` library it uses (`src/data.cpp`, `src/update/github.cpp`):

- **No installer.** The Windows asset is a portable zip (`Dusklight-v2.0.3-win32-x86_64.zip`,
  56 MB; an arm64 zip too). The exe runs from wherever the player unpacked it.
- **Data is separate from the exe.** By default it lives in SDL's preference folder,
  `%APPDATA%\TwilitRealm\Dusklight\`. Settings can move it to `data\` next to the exe (portable)
  or to any folder. The choice is a small `data_location.json`, looked for next to the exe first,
  then in the preference folder. Every candidate folder gets a write probe, and moving data
  copies known files and asks for a restart.
- **Updates only point at the release.** A check at start (on by default,
  `backend.checkForUpdates = true`) reads the latest GitHub release, skipping drafts. When it is
  newer, a banner's Download button opens the release page in the browser. The app never
  replaces itself.

Why we cannot copy it as is:

- **Our app is not the game.** Dusklight's exe is the whole product. Ours builds a 1–2 GB game
  under `versions/`, which Steam shortcuts, Start-menu entries and the receipt point into. A
  portable exe moved after install leaves all of those stale, which is why the owner chose an
  installing exe (D215).
- **Self-update needs a fixed home.** Swapping the exe in place (§4) is only safe in a folder the
  app owns.

What we take from it:

- **The location record and write probe.** The first run proposes
  `%LOCALAPPDATA%\Programs\DefJamRecompiled\` with **Choose folder…**. The app probes the folder
  (create, write, rename, delete a file), copies itself there, adds a Start-menu entry and
  relaunches from the copy. The chosen path is recorded in `%LOCALAPPDATA%\DefJamRecompiled\location.json`,
  so a second download of the exe finds the existing install instead of starting a new one.
- **Path checks on a chosen folder.** Refuse a folder under `Program Files` (not writable without
  elevation), a synced folder (OneDrive), a network or FAT32 drive, or a path long enough that the
  MSVC build would exceed `MAX_PATH` (the build tree adds about 120 characters).
- **Default on, quiet failures.** Checks are on by default (D215), and a failed check only logs.
  Our Download fetches and verifies the asset in the app (§4), rather than opening a browser.
- **Moving later.** A **Move install** action in Maintenance would re-run the probe, move the
  folder while nothing holds a lock, then rewrite the shortcuts and Steam entry. It is optional,
  not first-release work.

Viability: yes, with the install step. Dusklight's data-location pattern fits our install
folder, and its update check matches what `setup/releases.py` already does.

## 4. Updates inside the app

`setup/releases.py` already does the hard parts: GitHub discovery, the stable-only picker,
SHA-256 and provenance checks, the verified download cache, and refusing historical repairs. The
app adds the visible flow:

1. **Check.** On start, at most once a day, on by default (D215, as Dusklight does), with a
   setting to turn it off and a note that no personal information is sent. Rate-limit and HTTP
   errors are silent.
2. **Download.** "v0.13.0 is available" → **Download**. The new app is fetched next to the current
   one and verified exactly as releases are verified today.
3. **Swap and relaunch.**
   - **Windows:** rename the running exe to `.old`, move the new one into place, relaunch, and
     delete `.old` on the next start.
   - **macOS:** replace the bundle while it is closed, using a small helper step from the
     downloaded app.
   - **Linux:** rename the new file over `$APPIMAGE`, which is atomic on the same filesystem.
4. **Rebuild.** The new app finds that its payload is newer than the active build and offers
   **Build update**. The previous build stays playable until activation, as today, and stays
   available for rollback.

Neither OS should prompt again after the first run, but this must be confirmed in testing:

- A file our own code downloads carries no Mark-of-the-Web on Windows and no
  `com.apple.quarantine` on macOS, so the self-update should not trigger SmartScreen or
  Gatekeeper.
- The setup already strips quarantine from its unpacked payload (`ditto --noqtn`, #59).

## 5. Faster updates, safely

### 5.1 How often the inputs actually change

Each analysis or lift input compared between consecutive releases, v0.8.0 → v0.12.1 (15
transitions):

| Stage input | Unchanged in |
|---|---|
| Analysis: toolkit `tools/**/*.py`, `config/seed_functions.json`, `scripts/pipeline.py` | **13 / 15** (changed: v0.11.0→v0.12.0, the v0.16 toolkit migration; v0.12.0→v0.12.1, 3 lifter files) |
| Lift: the above, plus `src/recomp_manual.c` and `templates/runtime` | **8 / 15** (`recomp_manual.c` changed in 7) |

Analysis is the most expensive stage and the one that almost never changes.

### 5.2 Gaps in the current stage keys

`scripts/pipeline-state.py` already certifies reuse by hashing each stage's inputs **and** outputs,
and Repair uses it. Before Update may rely on it, these gaps must close. Today they are harmless
only because Update never reuses:

1. **Analysis command line.** It is not in the key. `--extra-sections D3D,D3DX,...` and the order
   of the steps live in `scripts/pipeline.py`. Add the analysis argv (or the hash of
   `pipeline.py`) to `analysis_inputs`.
2. **Interpreter and wheels.** Python, `pyxbe` and `capstone` versions are not in the key, and a
   capstone change could alter the disassembly. Add the payload's dependency identities
   (interpreter version, wheel SHA-256 from `dependencies*.json`).
3. **Non-Python tool data.** It is not hashed; only `tools/**/*.py` is. Example:
   `tools/recomp/data/win32_api_names.txt`. Hash every file under the tool packages that analysis
   or lift import, not only `.py`.
4. **Lifter options.** `--split` and the extra lifter arguments are written to `lift-state.json`
   but are not part of the compared `inputs`. Move them in.
5. **A schema salt.** Add a `PIPELINE_KEY_VERSION` constant to each stage key, so a key-format
   change invalidates everything once.

### 5.3 Rollout in tiers

| Tier | Change | Saves per update (Windows / Deck) | Risk control |
|---|---|---|---|
| 0 | Close the five gaps above. Change nothing else. | — | Unit tests: perturbing each input invalidates its stage |
| 1 | Update and Downgrade may **copy** the previous version's verified analysis into the new attempt folder (Repair's path), and `verify_analysis` decides | ~2.7 / ~4 min, in 13 of 15 past updates | Copy, never link or move: the old version stays intact. If a reused stage fails, retry once fully fresh. Clean Rebuild stays in Maintenance |
| 2 | The same for lift | ~1.2 / ~2.3 min more, in 8 of 15 | As Tier 1 |
| 2b | Key lift on what the lifter **reads** from `recomp_manual.c` (`tools/recomp/manual_scan.scan` and `entry_hooks`, run by the pinned toolkit itself), not on the file's bytes: editing a manual function's body no longer forces a re-lift | lift reused in most of the 7 remaining | Gate: a body-only edit must lift byte-identically to a fresh lift |
| 3 | A compiler object cache (ccache: content-hashed, compiler and flags included) | ~1 / ~1.5 min | Later, if at all. MSVC needs `/Z7` instead of `/Zi`, and ccache is GPL-3, so shipping it needs the owner's approval |

**Proof that reuse equals fresh.** On each release candidate, the release train gains one check:

1. Update from the previous release with reuse.
2. Clean Rebuild the same version.
3. Compare the certified output hashes (`analysis_outputs`, `generated_outputs`). They must be
   identical.

The v0.16 ledger already observed analysis output and all 23 lifted C files byte-identical across
toolkit commits that did not change their logic (pin `2b73d81` vs `32aa980`). That is evidence the
stages are deterministic, but a gate must check it on every release.

**Expected Windows update, Tiers 1 and 2 combined:** about 2.5–3 minutes when both stages are
reused, 4 minutes when only analysis is, instead of 6.5.

### 5.4 Not now (D214)

Precomputed analysis metadata (function boundaries, ABI facts; no code bytes) would remove the
analysis stage from **first** installs too. It is recorded here as a later possibility only.

## 6. Linux AppImage with every dependency inside

Today the Linux build needs either a host toolchain (clang, cmake, ninja and the SDL3, shaderc and
Vulkan development packages) or podman with a ~1 GB Arch build container (SteamOS). The game is
then linked against the host's SDL3 3.2+ and shaderc (`check_host_runtime`). Two requirements for a
self-contained AppImage:

### 6.1 Build dependencies in the AppImage

| Need | Source | Notes |
|---|---|---|
| Python + wheels | python-build-standalone + the seven pinned wheels (already) | Tk is no longer needed once the ImGui front end replaces `wizard.py` |
| CMake, Ninja | Kitware and ninja-build static Linux releases | As the macOS payload already does |
| SDL3 | Prebuilt **static** `libSDL3.a` + headers from the pinned source, built in CI with its X11, Wayland, PipeWire and PulseAudio backends loaded at runtime | zlib licence |
| shaderc | Prebuilt static `libshaderc_combined.a` from the pinned commit | As macOS builds it; Apache-2.0 |
| Vulkan | Headers only | The loader `libvulkan.so.1` must stay the host's (it finds the GPU drivers); SDL already loads it by name (#77) |
| Dear ImGui | Source (already) | |
| **C/C++ compiler** | **Option A:** `zig cc` / `zig c++`, one pinned Zig release (MIT, upstream clang/LLVM + lld, glibc stubs for a chosen glibc version, libc++). **Option B:** an LLVM clang + lld build plus a pinned glibc/libstdc++ sysroot | A is far smaller and needs no host headers. B is closer to today's clang. Either removes podman and the container download |

With static SDL3 and shaderc, built against a fixed old glibc target (for example 2.31), the game
the player builds depends only on glibc, the host's Vulkan loader, and the display and audio
libraries SDL loads at runtime. It then runs outside the AppImage: Steam can launch it directly,
and no `LD_LIBRARY_PATH` into the mount is needed.

### 6.2 AppImage points to verify

- **Read-only mount.** Builds, receipts, logs and HD work go under the chosen install and data
  folders, never in the mount. Nothing may record a `/tmp/.mount_*` path, which changes every run.
- **FUSE.** Use the current static type-2 runtime. Test on SteamOS (desktop and Game Mode), Ubuntu
  24.04, Fedora and Arch/CachyOS (issue #83). `--appimage-extract-and-run` is the fallback.
- **Game Mode.** A native file dialog may not appear in Steam Game Mode, so the app needs its own
  gamepad-driven file browser for choosing the dump on Linux. That is another reason the ImGui
  front end beats Tk.
- **One toolchain per platform.** One pinned compiler on every distro is also a reliability gain:
  the CachyOS failure (#83/#90) came from host-toolchain variation. Keep `--toolchain host` as a
  developer option only.

### 6.3 Spike before committing to it

Build the game on Linux x86-64 with `zig cc` (Option A), static SDL3 and static shaderc, against
glibc 2.31. Then:

1. Run `regress.py --quick` on the Deck.
2. Diff `posix-release` frame times against the container build.
3. Check that the replay hash matches. The lifted code relies on `-fno-strict-aliasing -fwrapv
   -ffp-contract=off`, which Zig's clang accepts.

If the spike fails or regresses, fall back to Option B. The owner chose to try Zig first (D215).

### 6.4 Windows and macOS

- **macOS.** Already bundles everything except Apple's Command Line Tools, which cannot be
  redistributed. The app runs `xcode-select --install` itself and waits.
- **Windows.** Still needs Microsoft Build Tools. A later, separate spike could do for Windows what
  6.3 does for Linux (llvm-mingw or `zig cc` targeting `x86_64-windows-gnu`). It is out of scope
  here.

## 7. Add to Steam on all three

Linux today: `steamos-add-to-steam`, or `steam://addnonsteamgame` with `/tmp/addnonsteamgamefile`,
then read back `shortcuts.vdf` and place the artwork (`engine.py:926`, `place_steam_art`).

Windows and macOS have no such request, so the app writes the shortcut itself, as Steam ROM
Manager, BoilR and Heroic do:

1. **Find Steam.**
   - **Windows:** `HKCU\Software\Valve\Steam\SteamPath`, falling back to
     `%ProgramFiles(x86)%\Steam`.
   - **macOS:** `~/Library/Application Support/Steam`.
2. **Pick the account.** Use `config/loginusers.vdf`'s most recent user, or ask when there are
   several.
3. **Close Steam.** Steam rewrites `shortcuts.vdf` from memory on exit, so **Steam must be
   closed**. The app says so and offers **Close Steam and continue** (asking Steam to shut down),
   then **Start Steam**.
4. **Write the entry.** Add a binary-VDF writer next to the existing `parse_binary_vdf` and append
   an entry: `appid`, `AppName`, `Exe` (quoted), `StartDir`, `LaunchOptions`, `icon`.
   - The `appid` is chosen and written by us, so the artwork ids are known up front. No read-back
     wait is needed.
   - Keep a backup of `shortcuts.vdf`, and round-trip tests on real files from each platform.
5. **Artwork.** The same four images to `config/grid/<id>p.png`, `<id>.png`, `<id>_hero.png`,
   `<id>_logo.png`. The existing ownership rules carry over: the app never replaces user art and
   removes only what it placed.
6. **Target.** The app itself, at its stable path (§3.3), with `--play` so Steam starts the game
   directly. Steam Input and the overlay then attach to the game process the app starts. This
   needs checking on macOS, where Steam's support for non-Steam `.app` bundles is weaker.

The button lives in Maintenance (**Add to Steam** / **Remove from Steam**), and the first-run
options offer it too. Linux keeps its working path, and could later move to the same writer.

## 8. HD textures in the app

`build_hd_textures` needs only the verified extraction and the bundled Python with Pillow and
NumPy (`scripts/build-hd-pack.py`). It does not depend on the game build, so in the app:

- **First run.** The same checkbox, run after the build, using the progress protocol that already
  exists (`PROGRESS n / m`).
- **Textures page.** A new **Generate HD textures** button, so a player who skipped it can add it
  later without any rebuild. The existing rules carry over: a cancelled or failed run never
  enables packs, and a rerun resumes the verified work.
- **Updates.** Updates never redo it. The generated packs and caches live in the data folder,
  which is preserved.
- **AppImage.** Nothing extra is needed: it already ships the Pillow and NumPy wheels.

## 9. Moving existing players over

- On first start, the app looks for an existing installation, in the default locations and from
  the old setup's remembered paths. If it finds one, it adopts it through the receipt
  (`installed.json`): same version, no rebuild; older, it offers **Build update**.
- Old shortcuts keep pointing at the old wrappers, which keep working. On adoption, the app
  replaces `DefJamLauncher.exe`, the macOS launcher app and `launcher.sh` with forwarders that
  start the app.
- The data folder format does not change.

## 10. Suggested order

1. **Tier 0 and Tier 1 update reuse** (§5). Valuable on its own, and works with the current setup
   wizards.
2. **Settings catalogue split, and the app with Home and Settings only.** Play from the app on all
   three platforms; the in-game launcher stays as a fallback.
3. **Setup inside the app** (first run, Repair, Clean Rebuild), driving `engine.py`. The three old
   wizard front ends retire.
4. **Self-update** (§4) and the new release assets (owner's approval first).
5. **Add to Steam on Windows and macOS** (§7).
6. **The Linux toolchain spike** (§6.3), then the AppImage.
7. **Tier 2 and 2b update reuse.**

## 11. Open questions

The five questions asked on 2026-10-26 are answered (D215, rows 9–13 of §1). None is open now.
New ones go to `PROGRESS.md` §5.

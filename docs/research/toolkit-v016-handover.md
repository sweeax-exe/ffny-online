# Toolkit v0.16 migration: Windows and Linux/Steam Deck test handover (temporary)

**Temporary.** Remove this file before PR #85 merges; its results go to `PROGRESS.md` and
`docs/research/toolkit-v016-ledger.md`. Written 2026-10-22 for the agents that test PR #85 on
Windows and on Linux/Steam Deck; updated 2026-10-23 after the Windows pass (pin `2b73d81`).

Read `AGENTS.md` first, then `python scripts/progress.py brief`. This file covers only this
test pass.

## 1. What is being tested

PR #85 (https://github.com/vyanhursky/DefJamFFNY-recomp/pull/85, draft) moves the port from
xboxrecomp v0.13 to upstream v0.16.0 (decision D147).

| Item | Value |
|---|---|
| Port branch | `feat/toolkit-v0.16` (includes main after the v0.10.7 publication) |
| Toolkit fork branch | `vyanhursky/xboxrecomp` `defjam/upstream-v0.16` |
| Toolkit pin | `2b73d81` (v0.16.0 `f0f8a46` plus 16 fork commits; the 16th is the disasm decode cache, see item 7 below) |
| Mapping of old fork commits to new | `docs/research/toolkit-v016-ledger.md` |
| CI on the branch | installers come from the newest green run: `gh run list --branch feat/toolkit-v0.16 --status success --limit 1` (call its id `<run>`). Use a run of a commit after `04b7ca4`, the pin of `2b73d81`; older runs build `32aa980` |

`git -C tools/xboxrecomp log --oneline -1` must print `2b73d81` (section 3).

**Done on the Mac (M1 Pro), on `32aa980`:**
- full regression 18/18 (`regress.py`);
- replay hash unchanged;
- toolkit Python tests pass (938);
- every native fixture that builds on macOS passes.

**Done on Windows (2026-10-22/23; ledger "Windows results"):** toolkit tests with MSVC, 19/19
native fixtures, full regression 14/14 (4 macOS/Linux-only checks skip), saves, ICALL scan, HRTF,
Setup install and boot. With `2b73d81` the analysis output and the lifted C are byte-identical to
`32aa980`'s, so `2b73d81` changes analysis time only.

**Not done yet:** the Steam Deck and the owner's play-test.

**What changed and could break on your platform:**
1. **Function seeds.** v0.16's analysis drops about 340 functions the old one found. The port
   now seeds them from `config/seed_functions.json`: 343 seeds, 207 of them `"alias": true`.
   An alias seed is an entry point inside another function; it does not split that function.
   - A missed seed shows up as `[ICALL] Failed to resolve 0x...` in the run log.
   - `scripts/check-manual-addresses.py` runs after every lift and fails if a hand-written
     function in `src/recomp_manual.c` no longer starts a lifted function.
2. **Saves.** Upstream v0.16 names the save folders `UDATA`/`TDATA`. The fork keeps the old
   names `UserData`/`TitleData`, so existing saves still load. Check this on a real install
   that already has saves.
3. **Windows memory layout (issue #72).** v0.16 rewrote the memory code. The fork's issue #72
   fix (`windows_guest_layout`) was carried over by hand. MSVC broke on it once; that is fixed
   in `32aa980`.
4. **Audio.**
   - Upstream v0.16 turns HRTF (3D audio) on by default; the port turns it off in
     `src/main.c` (`RECOMP_APU_HRTF=1` turns it back on).
   - Upstream's new APU timing (#192: `RECOMP_DISPATCH_GATE`, `RECOMP_VBLANK=paced`,
     `RECOMP_FLIP_PACING`) is opt-in, and the port does not use it. Do not set those switches.
   - On the Mac, the migrated build had one burst of audio clicks the old build did not.
   - On Windows it clicked all through the opening scene in 3 of 4 runs on 2026-10-22, then not
     in 14 runs on 2026-10-23. It comes back when Windows' power throttling is forced on the
     process. Issue #93 tracks it, with a fix kept on the fork branch
     `fix/windows-power-throttling` (not pinned). Whether Linux shows it at all is unknown
     (section 5.4).
5. **Input.** `xbox_InputMap_*` was renamed, and wheel up/down got the virtual keys `0x88`/`0x89`.
   Check pads, rumble, keyboard and the launcher's control mapping.
6. **SDL3 only.** v0.16 added SDL2 host files; the port's SDL3 build leaves them out. The
   Linux build is the most likely place for a link error here.
7. **Analysis time.** Upstream's callback probes (v0.14.0, #182) made the disasm step take 990 s
   on Windows with v0.16 (the Setup install 30.5 min). `2b73d81` is the fork copy of upstream
   PR #255 (issue #254): 167 s on Windows, output byte-identical. It is plain Python, so the
   same gain is expected on Linux; record the time there (section 5.3).

## 2. Rules for this pass

- **Game runs are serial.** Never start two at once, and check no other session is running
  the game.
- Never commit game data, lifted C, logs or built executables. Run `git add -A --dry-run`
  before every commit.
- **Commit results to `feat/toolkit-v0.16` only.** Do not push without Vlad's go-ahead.
  Vlad merges.
- **No upstream activity.** Do not open upstream PRs or post upstream comments.
- **Fixes:**
  - a toolkit fix is a commit on the fork branch `defjam/upstream-v0.16`, published, then
    pinned in the parent (`AGENTS.md` §4.7);
  - a missing function is a seed (`config/seed_functions.json`; set `"alias": true` when the
    address is inside a function that already exists);
  - a lifter workaround goes in `src/recomp_manual.c`.
- Keep `docs/worklog/` untracked.

## 3. Get the branch

```bash
git fetch origin
git switch feat/toolkit-v0.16
git submodule update --init --recursive
git -C tools/xboxrecomp log --oneline -1    # must print 2b73d81
```

Use a separate worktree or clone if your main checkout has other work. Set `DEFJAM_DATA` to
the existing data folder (the one with `extracted/` and `save/`). A new worktree also needs the
`game` link to `$DEFJAM_DATA/extracted` before `regress.py`; without it every game check fails at
once with "./game/default.xbe missing".

**Back up `$DEFJAM_DATA/save` before the first run.** Copy it somewhere outside the data
folder; restore it if the saves check fails.

## 4. Windows

On a Windows x64 PC with VS 2022, run in a Developer PowerShell (x64).

### 4.1 Toolkit tests with MSVC

```powershell
py -3 scripts/test-toolkit-msvc.py
```

Needs pytest. All tests must pass, with no new skips.

### 4.2 Native fixtures, especially issue #72

These are the 18 fixtures CI runs (Release, Ninja). Run at least `windows_guest_layout`,
`memory_regressions`, `kernel_regressions` and `input_host`:

```powershell
foreach ($t in 'windows_guest_layout','memory_regressions','kernel_regressions','input_host') {
  $extra = if ($t -eq 'kernel_regressions') { '-DXBOX_SLOW_TESTS=ON' } else { '' }
  cmake -S tools/xboxrecomp/tests/$t -B build/fx-$t -G Ninja -DCMAKE_BUILD_TYPE=Release $extra
  cmake --build build/fx-$t
  ctest --test-dir build/fx-$t --output-on-failure --no-tests=error
}
```

### 4.3 Build the game from source

```powershell
.\scripts\analyze.ps1
.\scripts\recomp.ps1
.\scripts\build.ps1 -Preset win-x64-release
```

- With `2b73d81`, `analyze.ps1` takes about 3 minutes (16.6 with `32aa980`), `recomp.ps1` and
  `build.ps1` 1-2 minutes each on a 16-core PC. VS 2019 Build Tools work.
- `recomp.ps1` does not run `check-manual-addresses`; run
  `py -3 scripts/check-manual-addresses.py --toolkit tools/xboxrecomp` after it (42 checked,
  0 missing). `pipeline.py recomp` runs it itself.
- Close `defjam_recomp.exe` before building (a running copy causes LNK1168).

### 4.4 Regression

```powershell
py -3 scripts/regress.py --preset win-x64-release
```

- The full regression has 18 checks and takes about 20 minutes. Run it in the background and
  read its table.
- Do not edit toolkit or `src/` files while it runs: the harness then refuses the remaining
  checks as stale.
- For a failed row, use the `triage.py` line it prints (`docs/agent/triage.md`).

**Windows baseline (v0.10.7, D148).** On the test PC (RTX 4070 Ti SUPER, VS 2019 MSVC 14.29), the
full regression gave 12/14, with 4 checks skipped by design (the SDL launcher and Vulkan overlay
checks). `combat` and `versus` sometimes fail only on `audio.dry_queue`: 2 of 8 runs, issue #88,
present before this migration. For this pass:
- a `dry_queue`-only failure that passes on a rerun counts as unchanged;
- any other failing assertion is new.

### 4.5 Setup installer

Download `windows-setup-assets` from CI run `<run>`
(`gh run download <run> -n windows-setup-assets`). Then:
1. Install it into a new folder.
2. Point it at the dump and let it build.
3. Start the game from the launcher.

This is the issue #72 path: a crash before the first frame, in `VCRUNTIME140.dll`, is the
failure to look for. If it fails, keep the setup log and the game's `.err` log.

## 5. Linux / Steam Deck

SteamOS cannot compile. Build in the pinned Arch container from `docs/build-macos-linux.md`,
"Steam Deck" section: the `podman build` with the pinned image digest, and the `djc`/`djpy`
aliases. Then run the game on SteamOS itself. On a regular x86-64 Linux, the native packages
from the same document work as well.

### 5.1 Toolkit and port tests

```bash
djpy -m pytest -q tools/xboxrecomp/tools/recomp tools/xboxrecomp/tools/disasm
djpy -m pytest -q -m "not slow" tests/unit
```

Run the toolkit tests from inside `tools/xboxrecomp` if the imports need it, as CI does.

### 5.2 Native fixtures

```bash
for t in mmio_trap_posix guest_cpu_posix memory_layout_posix windows_guest_layout kernel_memory_posix kernel_path_posix kernel_bridge nv2a_vsh settings texture_pack d3d8_vk_cache; do
  djc cmake -S tools/xboxrecomp/tests/$t -B build/fx-$t -G Ninja -DCMAKE_BUILD_TYPE=Release &&
  djc cmake --build build/fx-$t && djc ctest --test-dir build/fx-$t --output-on-failure --no-tests=error || echo "FAIL $t"
done
```

### 5.3 Build and check the libraries

```bash
djpy scripts/pipeline.py analyze
djpy scripts/pipeline.py recomp
djpy scripts/pipeline.py build              # preset posix-release
ldd -r build/posix-release/defjam_recomp     # on SteamOS, outside the container: nothing missing
```

- **Time the analyze step** (`time djpy scripts/pipeline.py analyze`) and report it. On Windows the
  disasm step went from 990 s with `32aa980` to 167 s with `2b73d81`. Fifteen minutes or more
  here means the fix did not take effect on Linux: check the pin first.
- `pipeline.py recomp` must end with the manual-address check passing (42 checked, 0 missing).

### 5.4 Regression

```bash
python3 scripts/regress.py --preset posix-release
```

Run it on SteamOS, not in the container; add numpy and pytest the way your setup does.

**Deck baseline.** Before the migration, the Deck passed 12 of 14 checks; `combat` and
`versus` failed only on audio-queue underruns (issue #11). For this pass:
- that pattern with the same or fewer underruns counts as unchanged;
- any other failure is new.

**Audio clicks on Linux** (issue #93 is Windows so far; this says whether Linux has it too). Two
runs of the opening scene with the migrated build, after the regression:

```bash
python3 scripts/harness.py run intro --preset posix-release --secs 330 --to-end --env RECOMP_APU_VOICE_PCM=logs/aud-deck1-
python3 scripts/audio-holes.py logs/aud-deck1-
python3 scripts/harness.py run intro --preset posix-release --secs 330 --to-end --env RECOMP_APU_VOICE_PCM=logs/aud-deck2-
python3 scripts/audio-holes.py logs/aud-deck2-
```

Report the F8 and FA lines of each. For comparison, Windows on an idle PC gives F8 5-11 holes
per 330 s (0.02-0.03/s) and FA 0-8; the bad 2026-10-22 runs gave F8 187-421, holes spread over the
whole scene. If one run is high, say which minutes the holes fall in and what else was running.

### 5.5 Setup installer

Run `linux-setup-assets` from CI run `<run>`
(`gh run download <run> -n linux-setup-assets`). Install, build and launch it on the
Deck in Game Mode. Then check that the Deck's controls work as a pad, including rumble.
Record how long the install takes: on Windows the v0.16 Setup took 30.5 min with `32aa980`,
against about 10 for v0.10.7, and `2b73d81` should bring it back near that.

## 6. Checks on both platforms

| Check | How | Pass |
|---|---|---|
| Saves | Start with the existing `save/` and open Story, Load. | The existing profile loads. No new `UDATA`/`TDATA` folders appear under `save/`. |
| Startup | Three cold launches (`regress.py --only soak --soak 3`). | Every launch reaches the menu. |
| Pads and rumble | Play-test, or the `versus` check for two pads. | Both pads work. Rumble works. The keyboard works. Rebinding in the launcher works. |
| Audio clicks | See below. | Same as the old build; no new runs of clicks. |
| HRTF | No log line reports it. `src/main.c` sets `RECOMP_APU_HRTF=0` unless the variable is already set, so check that neither the shell nor the launcher sets it. | Unset in the environment. Sound is positioned as on v0.10.x. |
| Missing functions | `python3 scripts/triage.py` on any run log. | No `[ICALL] Failed to resolve`. |

**Audio clicks.** Record the opening scene with the migrated build, at least twice (counts vary
by about 20% between identical runs):

```bash
python3 scripts/harness.py run intro --preset <preset> --secs 330 --to-end --env RECOMP_APU_VOICE_PCM=logs/aud-mig1-
python3 scripts/audio-holes.py logs/aud-mig1-
```

`audio-holes.py` needs numpy. If an old (v0.10.x) build of the same machine is at hand, record
it the same way and compare.

The Mac's numbers are four values for voice F8, migrated vs old: 147/10/5/5 vs 32/7/6/0. The
steady state was the same; the migrated build had one burst of clicks.

## 7. How to report

1. **Work log.** Add one line per platform to `PROGRESS.md` §6 with
   `python scripts/worklog-add.py <entry file>`. Include:
   - the check counts;
   - the log names;
   - the audio numbers;
   - the analyze and Setup install times;
   - any new seeds or fixes.
2. **Status row.** Update the D147 row in §0, e.g. `✅ Mac, ✅ Windows, ⏳ Deck`.
3. **Ledger.** In `docs/research/toolkit-v016-ledger.md`, add a short "Windows" or
   "Linux/Deck" results section.
4. **Commit.** Commit to `feat/toolkit-v0.16` with `docs:` / `fix(...)` messages. Ask Vlad
   before pushing, and say in chat what failed and what you changed.

If something fails and you cannot fix it within the rules in §2, log it, do not commit a
workaround, and stop.

## 8. Known risks

- **Windows memory layout (issue #72).** Upstream's memory rewrite plus the hand-merged fix.
  This is the risk most likely to show up only on Windows.
- **MSVC-only compile errors.** The Mac and Clang builds never see them. One already happened
  (`contig_native`, fixed in `32aa980`).
- **Seeds.** A function reached only on a path the Mac regression did not take could still
  be missing. It shows up as `[ICALL] Failed to resolve`.
- **Windows `audio.dry_queue` (issue #88).** Intermittent before the migration. Compare its
  rate, not one run.
- **Audio underruns on the Deck (issue #11).** This existed before the migration; compare
  against the 12/14 baseline.
- **Linux link errors.** The SDL2 files are kept out of the build by `XBOXRECOMP_SDL3`. A link
  error naming SDL2 symbols means one slipped through.

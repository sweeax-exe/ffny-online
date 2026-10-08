# Improvement backlog

## Gameplay regression and cross-title upgrade testing — planned 2026-10-05

Owner-requested future To-Do: [testing roadmap](08-testing-roadmap.md). Extend
the existing harness with repeatable fixtures, combat outcome assertions, visual
and audio comparisons, performance coverage and structured reports. Also define
per-game adapters for testing new upstream xboxrecomp releases against Def Jam,
TimeSplitters 2 and Mercenaries, accounting for their different toolkit integrations.
This is deferred planning; existing milestone gates and dependency pins are unchanged.

## A setup.exe that builds the game — planned 2026-10-07

Owner-requested future To-Do, to be taken by another agent. Goal: a clickable path for players who are not
comfortable with Python, a compiler or a command line. A single `DefJamSetup.exe` that takes the player's own
dump and leaves them with a built game and a shortcut. Not scheduled against M6; it can be done before or after
v0.5.0 (owner's call, see "Open questions").

**Why this and not a prebuilt game.** A built `defjam_recomp.exe` contains the ~17,900 functions lifted from EA's
`default.xbe`, so it is a derivative of EA's binary. Our rules (D5, D10, `CLAUDE.md` section 3, `docs/releasing.md`)
never publish executables, lifted code or game data. The installer contains none of that: it produces the EA-derived
code on the player's machine from the player's dump, which is what the repository's own build already does. Binary
releases of the game itself stay off the table (no legal advice has been taken; treat it as policy). Hosted CI also
cannot build the game (no dump), so it could not produce one anyway.

**What it does** (Windows only; Linux and Steam Deck run the Windows build under Proton, macOS is M9):

1. Ask for the dump: an ISO, an XISO or an extracted folder. Verify `default.xbe` against
   `config/dump-manifest.json` (SHA-256 `31cc0d11...`) and explain wrong-version dumps in plain language. Extraction
   uses `extract-xiso` (check its licence and how it is obtained before bundling or downloading it).
2. Check prerequisites and install what is missing:
   - the compiler: Visual Studio Build Tools (x64 MSVC toolset, Windows SDK, CMake, Ninja) through Microsoft's own
     bootstrapper with the right workload IDs; it cannot be bundled (licence), needs a UAC prompt and the player's
     acceptance. VS 2019 built the tested game, VS 2022 is untested here. `scripts/build.ps1` shows how the existing
     scripts find it (vswhere);
   - Python for the lift: an embedded Python (PSF licence) with `pyxbe` and `capstone`, so the player never sees
     Python;
   - source: the release's source at its tag plus the exact toolkit commit from the submodule pin (the GitHub source
     ZIP omits submodules), SDL3 and Dear ImGui (the build already fetches both at pinned hashes).
3. Run what `docs/build-and-play.md` describes: extract, `analyze.ps1`, `recomp.ps1` (the lift, about 20 minutes),
   `build.ps1 -Preset win-x64-release` (about 15 minutes, `/bigobj` objects). Show progress and keep a log; make it
   resumable and make a failed step restartable.
4. Put the dump (or a link to it) where the game expects it (`game/` junction, data folder with `settings.ini` and
   `save/`), create a desktop and Start-menu shortcut, offer to launch. The launcher (v0.4.0) is the first thing
   the player then sees.
5. An update path: running a newer setup against an existing install updates the source and rebuilds; skip the
   lift when its inputs (`config/seed_functions.json`, lifter, pinned toolkit) did not change
   (`scripts/pipeline-state.py` already compares input hashes).

**Suggested shape.** A small C++ Win32 program reusing the Dear ImGui and Direct3D 11 code of the launcher
(`src/hooks/pc_launcher.cpp`, `pc_ui.cpp`), so there is no new dependency, built from this repository by the
release workflow (it needs no game data). A PowerShell script wrapped in an exe is quicker but feels less polished
and is flagged more by antivirus.

**Costs and risks to plan for.**
- About 5-10 GB of disk and roughly an hour on a typical PC, mostly the compiler install; check free space first,
  handle paths with spaces and Windows long paths, and cope with antivirus slowing or blocking compiles.
- An unsigned `setup.exe` triggers SmartScreen and some antivirus warnings. Options: document it, or buy a code
  signing certificate (cost, identity verification). Owner has not decided.
- Release policy has to change deliberately and narrowly. Today `docs/releasing.md` says no executable assets, and the
  release workflow and `scripts/check-release.py` / `scripts/check-source-tree.py` refuse binaries. Attaching
  `DefJamSetup.exe` means allowing exactly that one asset (name, built by the workflow from tagged source, hash in
  the release notes) and keeping every other rule: never publish the game executable, lifted code, captures or
  game data. The "Source release" workflow is also where its build and checksum belong. Update `docs/releasing.md`,
  the CI hygiene check and the CONTRIBUTING notes in the same change.
- The installer must never upload or copy the player's dump anywhere, and must not embed any game bytes.
- Version drift: the installer pins one source tag and one toolkit commit, and reports them in its log and in the
  game's About page.
- Test it on a clean Windows 11 VM with no Python, no Visual Studio and no git, and on a machine that already has
  VS 2022; use a dump that is not the developer's usual one. Record the timings in the release notes.

**Open questions for Vlad** (ask before starting): do it before or after v0.5.0 (true 16:9)? Is an unsigned
installer with a documented SmartScreen warning acceptable, or should code signing be planned and paid for? Should
the installer also offer to install `extract-xiso` itself, or require an already extracted dump?

**Done when:** on a clean Windows 11 machine a player with only the ISO and the installer reaches a working game
window without opening a terminal, the install log names the pinned source and toolkit versions, an update rebuild
works, the release workflow builds and checksums the installer, and `docs/releasing.md` and the policy checks
describe and enforce the narrow exception. Nothing from the dump or the lift is in the installer or the repository.

Work that is deliberately deferred until M2 and the Direct3D interception experiment are settled.
Nothing here blocks the current milestone. Revisit this file when M2 closes, and again whenever the
rendering approach is decided, because several items depend on that outcome.

Ordered by value, not by effort.

## 1. Explicit jump-table and function-extent configuration

**2026-10-05: this caused a shipped crash.** A function cut short left a jump to its own
shared exit as an empty stub, and Terrordome matches crashed on v0.1.0 and v0.2.0 (fixed in
v0.2.1 by seeding seven self-contained exits). `scripts/stub-targets.py` reports about
seventy further stubbed targets that decode as real code and cannot be seeded: a seed ends
the containing function at the seed, so a fragment that branches back into its parent only
multiplies the stubs. They need function extents in the lifter. Do this before more
gameplay coverage is added.

**Why.** The mature recompilation toolchains treat jump tables as declared data, not as something to
infer: the Xbox 360 toolchain runs a separate analysis pass that emits a configuration file listing every
switch table before the recompiler runs. Our toolkit infers them, and gets it wrong in seventeen places.
That is the bug that silently truncated every affected memory copy this session, and its root is one level
down: the toolkit accepts a seeded function *start* but has no way to declare where a function *ends*, so a
body cut short makes its own jump-table arms look external.

**Measured and largely closed, 2026-09-20.** `scripts/coverage-audit.py` now accounts for every byte of the code-bearing sections against the extents the lifter recorded, classifying each gap as padding, string data, a jump table or something that looks like a function. It found two real unlifted functions, which are seeded. What remains is that the classification is a heuristic on the first bytes; a function that begins with an unusual prologue would still read as unclassified.

**Measured, 2026-09-20.** `scripts/jump-table-audit.py` reads all 402 indexed-jump tables out of the image and checks every entry against the labels the lifter placed: zero lost arms, including for the one function historically believed truncated. So the arms-past-the-end failure is not present in this title's current output. What remains unmeasured is a table in code the lifter never reached, which is what function extents would settle.

**What to do.** Add a project-level configuration listing known jump tables with their extents, and known
function boundaries, fed into the lift. Model it on how the 360 toolchain separates analysis from
recompilation. This removes a whole class of silent corruption rather than fixing instances of it.

**Interim.** The scan that finds the remaining sixteen is described in `docs/03-workflows.md`. Each one can
be neutralised today by hand-writing that function into `src/recomp_manual.c`.

## 1b. The audio chip model's trap handling (to do, D70)

**Why.** The rare sound-library crash guarded in v0.2.3 comes from the toolkit's audio chip model: its
frame thread raises an idle-voice trap and keeps walking the voice list, the interrupt routine runs about
a millisecond later, and a later method can overwrite the trapped voice number. On a console the trap is
serviced at once and names one voice (`docs/research/dsound-voice-list-crash.md`).

**What to do.** In `src/apu/` of the toolkit: latch the first trapped method and voice until the trap is
acknowledged, stop the frame's voice walk at a trap, and consider queueing guest register writes while
trapped. Then remove or keep the guard in `src/recomp_manual.c` (`sub_0025FB7C`) as a safety net.

**When.** Not scheduled. It changes the audio path the owner accepted by ear after the rebase, so it needs
his listening test afterwards, and it is a candidate for upstream.

## 2. Validate the lifter automatically

**Why.** We are finding instruction-level translation bugs by playing the game, which is the most expensive
possible way to find them. The 360 project validates its recompiler against an open instruction test suite.
Our toolkit already ships a conformance directory and a cross-checking setup against a 32-bit Linux
container, and we are not using any of it.

**What to do.** Wire the toolkit's conformance suite into continuous integration and make it a gate, not an
advisory step. Anything that translates x86 semantics wrongly should fail a build, not a boss fight.

## 3. Continuous integration builds unpatched code — DONE 2026-09-19

**Why.** The workflow checks out the submodule and builds the runtime, but never applies
`patches/xboxrecomp/`. It is therefore validating upstream code rather than what we ship. It passes, which
is worse than failing, because it reads as coverage we do not have.

**Done.** The patch-apply step is in the workflow as of 2026-09-19. The first real run also exposed a second
fault in the same job: it named the toolkit's `xboxrecomp` umbrella as its build target, which is an
INTERFACE library with no build rule, so ninja rejected it. The default target builds every runtime library,
which is what the job is for. All three jobs are green.

## 3b. Xbox library symbols need a purpose-built database, not Ghidra

**Why.** A Ghidra headless pass on this title recovers 153 meaningful names out of 9,370 functions, and every
one of them is C runtime: `memmove`, `qsort`, `sprintf`, the exception-handling helpers. It identifies
**zero** Xbox library functions. Ghidra's FunctionID databases simply do not cover the Xbox SDK, so this
route cannot find the Direct3D entry points that the rendering plan depends on.

It is still worth having. It independently confirmed two of our findings: that `0x002016B0` is `memmove`
with the cdecl convention we hand-wrote for it, and that the next function starts at `0x002019ED`, which
proves our detector ended that body early and is exactly the jump-table truncation bug.

**What to do.** For Xbox library identification use Cxbx-Reloaded's XbSymbolDatabase, which is open,
signature-based, and versioned by SDK build, and this title's build is recorded in `config/dump-manifest.json`.
The alternative is signature-matching against a donor library, which needs a donor we do not have. Settle
this before starting the Direct3D interception work, because that work is mostly a naming problem.

## 4. Design the mod and asset-override system deliberately

**Why.** Improved textures and soundtrack replacement were in the original goals. The N64 projects have a
real mod format with a template and a build tool. We have a sketch that amounts to intercepting file reads.

**What to do.** Once rendering works, design the override layer properly: a directory layout, a manifest, a
defined precedence order, and a decision about whether overrides sit at the archive level or the entry
level. The game's data is in EA archive containers, so entry-level replacement is the useful granularity.
Do not grow this organically out of debugging hooks.

## 5. Move to Clang before any native Linux work

**Why.** The mature projects require Clang, including on Windows, where they use `clang-cl`. We are on the
Microsoft compiler because it was already installed. That is fine for a Windows-only debug build, and stops
being fine when the same source has to produce a consistent result on two platforms.

**What to do.** Add a `clang-cl` preset, get it building and passing the smoke test, and make it the
default before starting the Linux target. Steam Deck is planned via Proton first, so this is not urgent,
but it should land before M7.

## 6. Upstream what belongs upstream

**Why.** Every patch we carry makes the next submodule bump more expensive, and our fixes would get
exercised against other titles, which is validation we cannot do alone.

**What to do.** Follow `docs/06-toolkit-rebase-plan.md` Phase 4 and the replacement ledger. Select small,
single-purpose fixes from the consolidated fork; do not mechanically split the original 0001 patch,
whose behavior now includes upstream-covered and superseded parts. Keep diagnostics gated and validate
each proposed fix against current upstream. The direct-call wrapper limitation is documented in
AGENTS.md; old findings need fresh source evidence before they become issue reports.

**When.** After the rebase's automated gates and Vlad's Release play-test. Opening each upstream PR
requires his separate approval (or an explicitly named batch). The 108-patch replay mechanism is retired.

## 6b. `apply-toolkit-patches.ps1` is only idempotent for patches that do not overlap

**Retired by the fork rebase.** Patch replay and its scripts are removed; the parent now checks out
the fork commit directly. The diagnosis and proposed remedy below describe the old patch mechanism.

**Why.** It decides "already applied" by reverse-checking each patch on its own. When a later patch edits
the same lines (0005 and 0008, 0008 and 0009 all touch `lifter.py`), the earlier patch no longer
reverse-applies on a fully patched tree, so a second run tries to apply it again and throws. A first run
on a clean submodule is fine, which is the case CI and a fresh clone hit.

**What to do.** Decide the state for the stack as a whole: if the submodule has no local changes, apply
every patch; otherwise check that the tree equals "all applied" (the check `regen` used on 2026-09-23:
replay the patches into a scratch checkout of the pinned commit and compare the touched files ignoring
line endings), and only then report "already applied".

## 6c. Deliver the APU interrupt when a title needs it

**Why.** The APU model sets `ISTS` bits and `set_irq` when a voice finishes or a stream segment is
played, but nothing reads `set_irq` and `update_irq` calls a stub, so DirectSound's service routine
never runs. Def Jam connects it (vector 5, `sub_002603BE` → `sub_00260322`: bit 6 queues the deferred
routine that completes voices and stream packets), but it only plays looping buffers and raised no
notification in two minutes, so it does not need it. A title that uses DirectSound streams or waits for a
buffer to stop will.

**What to do** (written and built on 2026-09-23, then withdrawn because nothing exercised it; D27). The
kernel keeps a level-triggered line per vector (`xbox_SetInterruptLine(vector, asserted)`), and the timer
thread calls the connected routine through `kernel_raise_interrupt` while the line is up, then drains DPCs.
`update_irq` drives the line instead of `pci_irq_assert`, and `se_frame` and the voice-processor write
path call `update_irq` when `set_irq` is set. The routine acknowledges by writing `ISTS`, which is
write-1-to-clear in the model already, and that write drops the line.

## 7. Structural comparison notes

Kept for context when revisiting the above.

| Area | Mature projects | Us |
|---|---|---|
| Legal hygiene | user-supplied dump, hash validated | same, plus a CI job that greps tracked files |
| Build | CMake with presets, Clang required | CMake with presets, MSVC |
| Switch tables | declared in a config produced by an analysis pass | inferred, wrongly in 17 places |
| Lifter validation | recompiler run against an instruction test suite | none in CI |
| Renderer | a serious modern renderer | a translation layer plus a diagnostic executor |
| Mods | defined format with template and build tool | not designed yet |
| Steam Deck | native Linux build plus Flatpak | Windows build under Proton, native deferred |

The renderer row is the one that decides the project. Everything else on this page is ordinary work.

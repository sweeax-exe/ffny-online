# Playing a chosen Story scene

The Story's cutscenes are in-engine scenes, not movies: the game plays them by name
(`sc00` to `sc21`, then a `gf` set) from `assets/cinema/<name>.viv`, with the engine's own
fighters, sets and cameras. The game also still contains EA's developer tools, among them
a **REAL TIME CINEMA** mode that plays any scene by number, but the module that runs them
was cut off from the retail build. The port rebuilds that module from outside the game
and uses it to play the scene you name: no save, no profile, no menus.

It is a test feature: nothing changes unless you ask for a scene, and the game's data is
not modified. How it works is in [the research notes](research/dev-module-revival.md).

## Which scenes there are

```bash
python3 scripts/scenarios.py scenes
```

lists the scenes in your dump: 38 in the USA Xbox version (`sc00`-`sc06`, `sc07a`, `sc07b`,
`sc08`-`sc21`, `gf1a`-`gf3c`). Scenes can also be given by number (0 is `sc00`, 37 is
`gf3c`). Which scene is which is not named yet: the tour below shows them.

## In the game: the overlay

Open the overlay (**F1**, or the pad's overlay buttons), go to **Diagnostics**, and under
**Story scenes** pick a scene and press **Play scene**. Close the overlay to watch.

- The scene starts within a second if the game is past its title screen; earlier, the
  request waits for the title screen.
- Whatever was running is left: after the scene the game returns to the **main menu**, not
  to the fight or Story point you were in.
- **Developer menu** opens EA's own debug menu instead: any match, any scene, the venue viewer.
  Its lines, controls and modes are in [The developer menu](developer-menu.md).
  **Back to the main menu** leaves it.

## From the command line: one command

```bash
python3 scripts/scenarios.py scene sc12
```

starts the game, plays `sc12` as soon as the title screen is up (about a minute after start,
after the opening movies), returns to the main menu and quits, then writes a labelled contact
sheet of the scene to `logs/scene-tour/<time>/sc12.png`. Several at once, or every scene:

```bash
python3 scripts/scenarios.py scenes sc05 sc07a gf1a
python3 scripts/scenarios.py scenes all        # each in its own launch, about 2-3 minutes each
```

`--secs` (default 60) is how long to capture after the scene begins, `--every` (default 4)
the spacing, and `--record` keeps a play-test session of each launch. `index.md` in the
folder lists every scene, whether it played and ended, its sheet and its log.

## Measuring them

```bash
python3 scripts/scene-perf.py run all
```

plays every scene the same way with the play-test recorder on and writes one table of load times,
frame pacing, hitches, audio drop-outs and the log's faults (`logs/scene-perf/<time>/summary.md`).
`compare` puts two runs, or a run and a committed baseline, side by side. The method and the Mac's
results are in [the 2026-10-18 report](research/scene-perf-macos-2026-10-18.md).

## By hand, without the scripts

Set `RECOMP_SCENE` when you start the game; the scene plays once the title screen is up and
the game then returns to the main menu. A list (`RECOMP_SCENE=sc12,sc05,gf1a`) plays each in
turn, back to the main menu between them.

macOS and Linux (from the repository folder):

```bash
RECOMP_SCENE=sc12 ./build/posix-release/defjam_recomp
```

Windows (PowerShell, from the repository folder):

```powershell
$env:RECOMP_SCENE = "sc12"; .\build\win-x64-release\defjam_recomp.exe
```

`RECOMP_DEV_MODULE=1` alone opens the [developer menu](developer-menu.md) at the title screen instead.

## What the log says

| Line | Meaning |
|---|---|
| `[DEVMOD] queued: module ..., current module ... (type 0 ...)` | the developer module was handed to the game |
| `[DEVMOD] Load` | it started; its mode system now runs |
| `[SCENE] loading sc12` | the game opened the scene's files (logged for every scene, Story's too) |
| `[SCENE] playing sc12 (scene 13) ...` / `[SCENE] sc12 ended ...` | the scene ran |
| `[DEVMOD] leaving: front-end screen 15, ...` / `[DEVMOD] Unload` | back to the main menu |
| `[SCENE] RECOMP_SCENE=... is not a scene of this game; known: ...` | a name the game does not have |
| `[DEVMOD] the queued module ... was replaced ...` | the game made a request of its own at the same moment; ask again |

With `--record` (or `RECOMP_RECORD=1`) the session's `events.jsonl` also has `devmod` and
`cinema` events.

The pictures are the game's own: keep `logs/` on your computer, do not commit or share it.

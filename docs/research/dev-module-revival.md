# Reviving the developer module (CRunModuleAki): progress

Owner request (2026-10-16): play any Story scene at any moment, with any profile, without
the menus. The game still holds EA's developer mode system, whose REAL TIME CINEMA mode
plays a scene by number, but the module that runs it is dead code in the retail build.
This document tracks the attempt to bring it back. Branch `feature/scene-launcher`
(worktree `DefJamFFNY-recomp-scenes`).

## Status

| Step | What | State |
|---|---|---|
| 0 | Find the cinema mode and why it never runs | done (below) |
| 1 | Map the run-module interface and the developer module's methods; how the next module is chosen | done 2026-10-17 (viable) |
| 2 | Build the module ourselves (guest object + virtual table) and inject it; its init runs | **done 2026-10-17**: Load, Update, Render run; EA's debug menu is drawn |
| 3 | Play a scene in it, return cleanly; overlay picker and `RECOMP_SCENE` | **done 2026-10-17** (macOS): scene by name, back to the main menu, overlay picker, `scenarios.py scene`, guide |

## Established (step 0, 2026-10-16)

- **The mode system.** `CModeSubsystem` (0x2AC bytes, allocated by `sub_00077C40` with the tag
  "CRunModuleAki::Init.m_pMode", registered as subsystem 9 in the registry at `0x2FB148`,
  init `sub_0002C790`). Its update `sub_0002DFF0` (thiscall, one stack argument) switches on
  `this+0x2A0`: 0 the debug menu (`sub_0002DBE0`), 1 the game, 2 idle, 3 REAL TIME CINEMA,
  4-7 the viewers (stage, animation, create player, ...). Settings: 15 words at
  `MEM32(this+0x2A8)`; the scene number is at `+0x18`. Init sets state 0 (debug menu).
- **The cinema mode.** `sub_0002D4F0` starts it: pauses subsystem `0x22` (`sub_000C5900`),
  sets subsystem `0x15` (cinema) flag 2, state 3, names the scene through the table at
  `0x002DC018` (38 pointers: 0 `sc00` ... 22 `sc21`, 23 `gf1a` ... 37 `gf3c`) and has the cinema
  player at `0x3459A0` load it (`sub_00076C60` -> `sub_00073FD0`, which formats
  `assets/nis_%s.bin` and `assets/cinema/%s.viv`). State 3 calls `sub_00076CD0` on the player
  each frame; when it reports done, `sub_0002D570` unloads (`sub_00076D00`), resumes `0x22`
  (`sub_000C5910`) and sets state 0.
- **Strings beside it.** Mode names (`MODE - GAME`, `MODE - REAL TIME CINEMA`, `MODE - STAGE VIEWER`,
  `MODE - ANIMATION VIEWER`, `MODE - CREATE PLAYER`, `MODE - CROWD TEST`, `MODE - BLAZIN VIEWER`),
  debug menu lines (`DEBUG MENU -- %s`, `MATCH_MODE`, `FIGHTER_0`..`3`, `STAGE`, `CINEMA`,
  `JUMP STARTING THE DAMN GAME!!!`), `AKIDEBUG`.
- **Why it never runs.** The module that owns the mode system, `CRunModuleAki` (methods
  `0x779A0`-`0x77EF0`: update `0x77A40`, a render-like `0x77B20`, init `0x77C40`, shutdown
  `0x77DD0`, mode-state setter `0x77EB0`, destructors), is referenced by nothing: no virtual
  table, no call, no jump, no stored address anywhere in the image. The run-module factory
  `sub_000773A0` (switch on 17 types, tags `RunModule`/`RunModuleLoad`/`RunModuleSMLoad`) builds
  every module the game uses; the developer module is not among them. A hook on its update
  (`sub_0002DFF0_enter`) never fired in a Battle fight (`logs/run-20261016-202155.log.err`).
- **Story's cinema module** is a different class, virtual table `0x29D664` (slot 1
  `sub_0007B150` loads the scene, whose name comes from the Story state through subsystem
  `0x18`'s entity data), so it cannot be told a scene by number.
- **The module interface.** Every live module has an 8-slot virtual table (slot 0 a destructor);
  the manager functions `0x77040`, `0x771E0`, `0x77230`, `0x772D0` call slots `+0x04` to `+0x1C`.

## Step 1 findings

### A. The run-module interface (2026-10-16; sub-agent report, spot-checked)

Spot-checked: the base table, slot 3's `ret 4` (Story cinema `0x7B2D0`), and the swap in
`sub_00077040` (`+0x18` <- `+0x1C`, `+0x24` cleared, then 2).

- **Manager:** `CRunModuleMan`, 0x4C bytes, registry subsystem `0x17` (stored at `0x2FB1A4`).
  Fields: **+0x18** current module, **+0x1C** pending module, **+0x24** state (0 running,
  1 requested, 2 loading, 3 loaded/waiting to swap), +0x21 restart request (`sub_00077020`),
  +0x20 "exit the main loop" (no writer found).
- **Base class:** virtual table `0x29D5A4` = destructor `0x76D30` + seven `_purecall` (`0x2007EF`).
  Only slot 0 (destructor, `delete` flag) and slot 3 (`float dt`) take a stack argument.

| slot | offset | name (ours) | when |
|---|---|---|---|
| 0 | +0x00 | destructor(delete) | after Stop/Unload of the old module |
| 1 | +0x04 | Load() | on the **loader thread** (`sub_000771E0`), on the pending module; then state 3 |
| 2 | +0x08 | Start() | main thread, right after the swap (and on an in-place restart) |
| 3 | +0x0C | Update(float dt) | every tick (`sub_00077040`), under the critical section `0x2FB000` |
| 4 | +0x10 | Render() -> bool | once a frame (`sub_00077740`); the bool feeds the fps counter only |
| 5 | +0x14 | CanLeave() -> bool | polled in state 3 before the swap; 0 holds the swap |
| 6 | +0x18 | Stop() | inverse of Start: in the swap, the restart and teardown |
| 7 | +0x1C | Unload() | inverse of Load: in the swap and teardown |

- **The swap (state 3):** old->CanLeave(); old->Stop(); old->Unload(); new->Start();
  delete old. The main loop is `sub_00011500` (init `sub_00011E40`, loop `sub_00011CD0` calling
  `sub_00077040(dt)` and `sub_00077740` per frame, shutdown `sub_00011060`).
- **The 17 types** (factory `sub_000773A0`, heap memory comes back zeroed):

| id | module | size | table |
|---|---|---|---|
| 0 | front end | 0x28 | `0x29D784` |
| 1 | 3D front end (guess) | 0x4C | `0x29D5C4` |
| 2 | game | 0x4C | `0x29D5E4` |
| 3 | tutorial (guess) | 0x50 | `0x29D604` |
| 4 | Story cinema | 0x08 | `0x29D664` |
| 5 | movie player | 0x20 | `0x29D644` |
| 6 | gym (guess) | 0x24 | `0x29D624` |
| 7 | character select (guess) | 0x14 | `0x29D764` |
| 8-16 | loaders, 0x18 each | | 9->0, 15->1, 10->2, 11->3, 13->4, 14->5, 12->6, 16->7 ("RunModuleSMLoad"); 8 never requested |

  A loader's Start (`sub_0007AB80`) requests its target `this+4`, so a switch is
  old -> loader -> target, and the old module unloads before the target loads. The first loader
  ("RunModuleLoadStartup") is made directly in `sub_00077230`.
- **`LoadNextRunModule`** (front-end command, `sub_00046200`) does not call the factory: it calls
  slot 2 of an object at `game(0x18)+0x38C`, a separate flow-object class.
- Open: what `CanLeave`'s helper `sub_001811F0` tests (a 256-entry table; audio streams?);
  `sub_00019320` on subsystem 4 at every swap (resource pools?).

### B. The developer module (2026-10-17; sub-agent report, spot-checked)

Spot-checked: `0x77B20` returns 1; the game module's table `0x29D5E4` has `0x793A0` in slot 1 and
`0x79580` in slot 7 (the counterparts of the developer module's `0x77C40` and `0x77DD0`);
`0x76EE0` is `mov al,1; ret`, used as CanLeave by three live loader tables; `sub_00044150`
fetches subsystem 9 and reads its mode state.

- **What in 0x77740-0x77EF0 is not the developer module:** `0x77740` the host's Render loop;
  `0x77780` the constructor of table `0x29D784`; `0x777A0`-`0x77980` the deleting destructors of the
  16 live tables; `0x779B0`-`0x779E0`, `0x77E70`, `0x77E90` belong to FXTexAnim and the pause menu;
  `0x77EB0`/`0x77EE0`/`0x77EF0` are `CModeSubsystem`'s constructor and destructors.
- **The developer module's own methods:** `0x779F0` (creates the pause menu, 0x104 bytes, loads
  `assets/misc.viv|pause.xsh`, fetches subsystem 0x28 Popup into `this+0x40`; runs in the
  constructor), `0x77C40` Init, `0x77A40` Update(dt), `0x77B20` Render, `0x77DD0` Shutdown.
  `0x779A0` is an unreferenced `ret 4` no-op of unknown role.
- **The table to write** (base `0x29D5A4` filled in):

| slot | entry | why |
|---|---|---|
| 0 | `0x76D30` (base destructor) | frees with `sub_00029740` |
| 1 Load | `0x77C40` | same shape as the game module's slot 1 `0x793A0`; runs on the loader thread |
| 2 Start | a `ret` stub (ours) | no surviving function; the live modules' Start is trivial for this kind |
| 3 Update | `0x77A40` | `ret 4` |
| 4 Render | `0x77B20` | returns 1 |
| 5 CanLeave | `0x76EE0` | must return true or the module can never be left |
| 6 Stop | `0x76EF0` (`ret`) | |
| 7 Unload | `0x77DD0` | undoes Init, as the game module's slot 7 `0x79580` does |

- **The object:** at least 0x44 bytes from `sub_00029680` (so the destructor's `sub_00029740` frees
  it). Fields (subsystem ids in brackets; some names are guesses): +04 Render (4), +08 Task (1),
  +0C Sprite (5), +10 Scene (6), **+14 the mode system** (0x2AC bytes, registered as 9), +18 Font
  (7), +1C BackEnd (0x18), +20 Cinema (0x15), +24 Particle (0xA), +28 LightMan (0xB), +2C FrontEnd
  (0xD), +30 FXSparkleMan (0x21), +34 FXTexAnim (registered as 0x22), +38 FXScreen (0x26),
  +3C pause menu, +40 Popup.
- **Constructor to write** (as the game module's `0x76DC0` does): set the table pointer, zero
  `+04`..`+40`, call `0x779F0`.
- **What Init needs:** the registry filled by `sub_00011E40` (always so once a module exists);
  it claims a render view "AKIDEBUG" (`sub_00016240`), opens `RunModuleAKI.log`, adds the static task
  `0x2DBF28`, reads the venue (0x1F) and fighter (0x20) tables, allocates a 0x400-byte console, and
  registers subsystems 9 and 0x22. Unload clears 9 and 0x22 and frees what Init made (not the
  AKIDEBUG view slot).
- **Subsystem 9 is read by retail code** (`sub_00044150`, `441C0`, `44210`, `44AC0`, `44B10`,
  `44B90`, `44C20` switch on its mode state and default to GAME when it is absent), so it must
  exist only while the developer module runs, which Init/Unload already ensure.
- **The debug menu** (state 0, 15 items) is complete: pad 0 through the input subsystem 0x19
  (`sub_000DD2B0`: masks 0x1000/0x2000 move, 0x10 activates, others change values), drawing
  through `sub_001DC908`. Nothing in it looks stripped; what is missing is the constructor, the
  table, the factory case and one error print.

### C. How the next module is chosen (2026-10-16)

- The run-module manager is **subsystem `0x17`** in the registry at `0x2FB148`.
- `sub_000773A0` is its **request** method (thiscall on the manager, one stack argument: the
  module type, 0-16; `ret 4`). Each case allocates the module with `sub_00029680(size, tag, flags)`,
  sets its virtual table or calls its constructor, stores it at **manager+0x1C** (the next module)
  and sets **manager+0x24 = 1** (a switch is pending). A failed allocation stores 0 and still sets
  the flag. The manager makes the switch on its next pass (agent A: the loop).
- Requests come from Story flow control (`sub_000C1240` types 9, 0xD, 0xC, 0x10, 0xA, 0xF;
  `sub_000C1AF0` 0xF; `sub_000C1DF0` 9; `sub_000C20B0` 0xA, 9; `sub_000C4460` a variable type),
  the back end (`sub_000D7060`: 0xE, 9, a variable), `sub_0007BB50` (0xB), `sub_0006AF30` (0xA),
  and loader modules: `sub_0007AB80` / `sub_0007ABA0` are virtual methods of loader classes that,
  when done, request the type stored at their own `this+4` ("load, then run type N").
- **Injection point for step 2:** on the game thread, build the developer module, store it at
  manager+0x1C and set manager+0x24 = 1; or intercept one request inside `sub_000773A0`. No new
  mechanism is needed: the game switches modules this way itself.

## Verdict after step 1 (2026-10-17)

**Step 2 looks viable, with modest work.** Everything the module needs exists except four
things we can supply from the host side without touching the lifter:
1. an 8-dword virtual table in guest memory (allocated once with `sub_00029680`): the table above;
2. a constructor: `sub_00029680(0x44, tag, 0)`, set the table pointer, zero the fields, call `0x779F0`;
3. the injection: on the game thread, store the object at manager+0x1C and set manager+0x24 = 1
   (manager = subsystem 0x17). The manager then runs Load on its loader thread, waits for
   CanLeave of the current module, and swaps; no factory case is needed;
4. a way back: request the front end's loader (factory id 9 -> module 0) when done.

**Step 3 then follows from code that is all present:** with the module running, the mode system
starts in state 0 (the debug menu, driven by pad 0). The cinema can be started the way
`scene_launcher.c` already does (set settings+0x18, call `sub_0002D4F0`); when it ends the mode
returns to state 0, and we request the front end again.

Risks that remain: Load running on the loader thread while the front end's resources are released
(order: old Stop/Unload, then new Load); the AKIDEBUG render view and the console text needing a
font that the retail boot did not load; Render `0x77B20` drawing nothing visible without the
render setup a developer boot did; audio for the cinema (the mode pauses subsystem 0x22, and the
cinema subsystem may expect the Story module's audio setup).

## Step 2 and 3 results (2026-10-17, macOS)

`src/hooks/dev_module.c` (build and queue, traces) and `src/hooks/scene_launcher.c` (the scene request),
hooked in `src/recomp_manual.c` on the manager tick `sub_00077040`, the module's methods and the mode
update `sub_0002DFF0` (one re-lift).

- **Step 2:** `harness.py run boot --env RECOMP_DEV_MODULE=1` (`logs/run-20261017-071354.log.err`):
  queued once the main menu was up (module `0x80026340`, table `0x8000A0A0`, pause menu and popup made
  by `0x779F0`), the manager loaded it (`Load` = `CRunModuleAki::Init`), swapped from the front end
  (`0x80011E50`) and has run `Update` and `Render` every frame since (mode system `0x800C5F80`, state 0),
  60 frames a second, no fault, saves restored identical. The picture is **EA's debug menu**:
  `DEBUG MENU -- assets`, `MATCH_MODE - SINGLE`, `FIGHTER_0..3 - Crow1 (MAR) 1 (PLAYER / CPU Easy)`,
  `STAGE - 02A The Foundation (FightClub)`, `CINEMA - sc00`, and the seven `MODE -` lines with the
  cursor on `GAME`.
- **Step 3 core:** with `RECOMP_SCENE=sc12` as well (`logs/run-20261017-071746.log.err`,
  `-072...`): the menu shows `CINEMA - sc12`, `[SCENE] loading sc12`, `playing sc12 (scene 13)`, the
  game draws 221 draws a frame against the menu's 25, the captures show the scene in-engine and
  letterboxed, and after about 4.5 s `[SCENE] sc12 ended`, back to the menu. The same scene runs
  310 frames (about 5 s) in Story (`logs/run-20261016-194515.log.err`), so it played in full.

Still to do for step 3: leave the module (request the front end's loader, factory id 9) after the
scene or on demand; an overlay picker (Debug tab) and a one-command `scenarios.py scene`; replace the
Story-based switch and its docs (`docs/scene-selector.md`); try the debug menu by pad (it reads pad
0) and the other modes; regressions with everything off.

## Step 3 finished (2026-10-17)

- **Leaving:** the front end opens the screen whose number is at FrontEnd(subsystem 0xD)+0x9C, an index into
  the name table at `0x2DFE08` (0 legal, 1 title `intMain/preStart.swf`, 15 `intMain/mainMenu.swf`, ... 33 "none";
  `sub_0006BC90` reads it and resets it to 33). Requesting the front end's loader (type 9) with 33 there gave an
  unnamed screen stuck on LOADING; with 15 the main menu comes back with its music. A game or Story module is
  not re-entered (its own flow sets up the match first), so leaving always goes to the front end.
- **When to queue:** at `bootLoad` the start-up sequence (logos, movies; `sub_000D7060`) requested its next module
  after ours and took the single pending slot, after our Load had registered subsystem 9. Requests now wait for
  the title screen (`intMain/preStart`), and a displaced module is reported.
- **Front ends to it:** `RECOMP_SCENE` (alone: arm at the title, play, leave), the overlay's Debug tab
  (Play scene, Developer menu, Back to the main menu), `scenarios.py scene` / `scenes` (one launch a scene,
  captures on the scene file's clock, contact sheet). Guide: `docs/scene-selector.md`.
- **Not yet:** the debug menu's buttons, its other modes and `MODE - GAME`; a request from inside a fight;
  Windows and the Deck.

## Play-test findings (2026-10-17, owner on macOS)

- **Frozen at the second scene of a run** (sc16 after sc05; sc12 twice). Cause: the mode system's init
  (`sub_0002C790`) adds the static task `0x2DBF28` to the task list (subsystem 1, head at `+4`, next at
  node `+8`) with `sub_0001B170`, which links at the head with no check for a duplicate; `sub_0002D4F0`
  takes it out (`sub_0001B1C0`) and `sub_0002D570` puts it back, but neither `0x77DD0` nor `0x2C970`
  removes it, so the second module's init linked it again, to itself, and the next walk never ended.
  Fix: `dev_module.c` removes it at Unload (and before Load). Reproduced with `RECOMP_SCENE=sc12,sc12`
  (frames stopped at the second Load, `logs/run-20261017-094348.log.err`); after the fix sc12, sc12, sc16
  and sc05 in one run (`logs/run-20261017-0947*`).
- **"A does not select" in the debug menu:** A does start the highlighted mode (bit `0x10` of pad 0's
  pressed mask, `sub_000DD2B0(1)`): scripted A moved the mode state 0 -> 1. The cursor starts on
  `MODE - GAME`, which starts a match with the default `Crow1 (MAR)` fighters and stalls asking for
  `speech/streams/intro/028.asf` and the venue announcer, files the retail disc does not have. Choosing
  real fighters and a stage first is untried.
- The render view the module's init claims ("AKIDEBUG", `sub_00016240`) is a fixed slot set up again
  each time, not an allocation; four entries in one run were fine.

## Regressions (2026-10-17, macOS, everything off)

Quick 5/5 (`logs/regress-20261017-122223.txt`). Full 15/17 (`logs/regress-20261017-132453.txt`): every check
passes except `audio.dry_queue` in `combat` and `versus` (1 dry event each, no dropped buffers), the known macOS
audio gate (issue #29) that main fails the same way. The developer module, the scene launcher and their hooks
change nothing when not asked for.

## The debug menu, mapped (2026-10-17, for v0.10.0)

Guide for players: [`docs/developer-menu.md`](../developer-menu.md). Read from `sub_0002DBE0` (input),
`sub_0002CA20` (drawing) and the mode update `sub_0002DFF0`, then checked with scripted input on macOS.

- **The game's button mask** (input subsystem 0x19, `sub_000DCBA0(0)` = pad 1; `sub_000DD2B0(0)` held at
  `+0x84`, `(1)` newly pressed, `(2)` auto-repeat at `+0x8C`), logged per button: LT `0x01`, White `0x02`,
  RT `0x04`, Black `0x08`, A `0x10`, B `0x20`, X `0x40`, Y `0x80`, Start `0x100`, Back `0x200`, d-pad up,
  down, left, right `0x1000`-`0x8000`. The stick clicks are not mapped.
- **Items** (cursor at mode `+0x2A4`, 15 items, labels at `0x2DBFB0`, kinds at `0x2DEDC`, value counts at
  `0x2DC0B0`): 0 `MATCH_MODE` (6: SINGLE, TAG, 3WAY, 4WAY, 1_VS_2, 1_VS_3), 1-4 `FIGHTER_0`-`3` (fighter table
  0x20; A cycles the controller at `0x33E75C + 4n` through PLAYER, CPU Easy, Normal, Hard), 5 `STAGE` (venue
  table 0x1F), 6 `CINEMA` (38), 7 empty (skipped), 8-14 the modes. Up/down move (wrapping), left/right ±1,
  LT/RT ±10; on item 14 left/right change the Blazin number at `0x33DBA0` (0-81). A on 8-14 calls
  `sub_0002D1C0` (GAME; also item 14), `0x2D4F0`, `0x2D5D0`, `0x2D6C0`, `0x2D7C0`, `0x2D8C0`. The cursor starts
  on `MODE - GAME`. A fighter line prints the record's name (`+0xC`) and a number (`+0x14`) from the
  character table, for example `Ludacris (KIC) 4`, `DavidBanner (STF) 16`, `Ice-T (SUB) 15`.
- **GAME** with real fighters runs a whole match and returns to the menu (`logs/run-20261017-203456.log.err`:
  Ludacris vs David Banner, The Foundation, intro, fight, KO, menu at about 95 s). Only
  `speech/streams/intro/028.asf` is missing; with the `Crow1` stand-ins the match stalled (above).
- **STAGE VIEWER** (`sub_0008D5C0` on `0x3797F0`): five lines (OBJECT, MORPH, ENVATK, TRANSP, ANIM), up/down
  pick, left/right step, B steps the object's variant. Its exit is `held & 0x100` (Start) with `pressed &
  0x200` (Back), but Start's press first opens the module's pause menu, and in a scripted run the chord
  did not leave the viewer; the pause menu's QUIT goes to the main menu (owner). Start alone opens the pause
  menu from any mode.
- Not run: TAG, 3WAY, 4WAY and the handicap modes; the animation viewer, create player, crowd test and
  Blazin viewer (owner tried them; left undocumented at the owner's request).

## Decisions and risks

- Injecting means constructing the object on the host side with the game's allocator
  (`sub_00029680(size, tag, 0)`), writing a virtual table of our own into guest memory, and
  handing the object to the manager in place of a module it was about to make.
- Risks: pieces stripped with the table (debug-menu input, font, a loading module the developer
  boot used); an init that assumes developer-boot state; subsystem ids it registers clashing
  with the live game's.

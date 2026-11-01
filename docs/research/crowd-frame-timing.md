# Does render-frame timing reach the crowd records and the fighters? (static read)

Read-only study of the lifted C in `src/recomp/gen`, the XBE image and `src/hooks/netplay.c`. Nothing was built or run.
Addresses are guest addresses; `file:line` are lines of the lifted C in this tree (they are about 16 lines lower than
the ones quoted in `crowd-lockstep-reset.md`). Marks: **[V]** read in code, **[I]** inferred, **[U]** not settled.
Call-graph claims come from a direct-call walk (`RECOMP_ABI_CALL`, tail jumps) plus a raw scan of `default.xbe` for
function-address words; vtable and function-pointer calls are listed where found and are the limit of the method.

## Findings

1. **The hypothesis does not hold for the position.** `sub_0007D560`, `sub_00084260` and `sub_00084190` never write
   `member+0x20..0x2B`; they read it as an input and hand it to the model (`sub_00024BE0` = set position). The record
   position that `sub_0007F030` publishes is the static XML placement, identical on every PC whatever the frame timing [V].
2. **Pose sampling is not reached from drawing.** `sub_00084260` and `sub_00084190` have exactly one caller,
   `sub_0007D560`; `sub_0007D560` has three callers, `sub_00081170` (crowd update), `sub_00080880` (restart) and
   `sub_00080ED0` (load). None of the three is reachable from the render entry `sub_000796A0`, from the event manager,
   from the render subsystem tick or from the screen manager's own code [V]. The crowd update runs once per game step,
   inside `sub_0008BE60`, never once per presented frame [V].
3. **What does depend on time in the records is the status, not the position**: `rec+0x18` (from the member's animation
   state `member+4`, which the clip clock `member+0x50` and the generators drive), `rec+0x20/0x24/0x1C/0x30`. Those
   advance once per step, with a constant `dt`. So a record can differ between PCs only through the number of crowd
   updates before step 1 (loading states run the update at host pace, `crowd_reset` covers that) and the generators.
4. **Per-render-frame writers into fight data: none found by direct calls.** Best candidates (section 3): the fade
   object `sub_000D8C40` draws the shared generator `0x39C0E0` once per rendered frame; the shared game heap;
   un-walked indirect calls. `netplay.c` already puts `0x39C0E0` and the time scale in hash part 5, so if "random
   generators stayed equal" included that part, the first candidate is already ruled out [I].
5. A cheap run-time probe settles the rest (section 4): hash the fight state at render entry and at the next step's start.

Side corrections: (a) `sub_00084260` does **not** read the noise object at `0x39C0E0` as `crowd-lockstep-reset.md` and
`spectator-shove.md` say; its `sub_000A9750` call is an atan2 angle between two points (`recomp_0005.c:4481-4496`) [V].
(b) The comment in `netplay.c` ("the host's measured frame time") is not true of this image: the crowd's `dt` argument is
the constant `0x28C798` (1/60) already (`recomp_0001.c:19898`; `fight-determinism.md` Q4). The override of `g_edi` is harmless.

## 1. Callers of the pose sampler (question 1)

Direct callers, found by grep of `0x0007D560u` / `0x00084260u` / `0x00084190u` and by `scripts/callers.py`:

| Callee | Callers (file:line) |
|---|---|
| `sub_00084190` (pose by clip time, `recomp_0004.c:483`) | `sub_0007D560` (`recomp_0003.c:87408`) only |
| `sub_00084260` (static-plane model placement, `recomp_0004.c:609`) | `sub_0007D560` (`recomp_0003.c:87337`) only |
| `sub_0007D560` (`recomp_0003.c:87290`) | `sub_00080880` (:115948, restart), `sub_00080ED0` (:117169, load), `sub_00081170` (:117306 viewer branch, :117317 normal branch) |

No word `0x7D560`, `0x84260`, `0x84190` exists anywhere in `default.xbe` (checked as little-endian words), so there is no
function-pointer or vtable entry for them [V]. Members have no vtable (`member+0` is the type).

Who reaches the three callers:

* `sub_00081170`: only through the crowd vtable `0x29EBE8` slot +8 (xbe words: `+0 7CF20, +4 81240, +8 81170, +0xC 7E6A0`).
  The only call of slot +8 in the lifted C is `recomp_0004.c:19053-19054` (`call [eax+8]`) in `sub_0008BE60`, taken in every arena
  state except 7 (state 7 calls `sub_0007E740` instead, `:19061`) [V]. `sub_0008BE60` is called by `sub_00079B60`
  (`recomp_0003.c:77840`), `sub_0007A8F0` (:80108) and `sub_0002DFF0` - the screen update methods, once per step. The screen vtable
  `0x29D5E4` has `+0xC = sub_00079B60` (update) and `+0x10 = sub_000796A0` (render) [V from the xbe]. In the net the crowd
  update is in the update arm only.
* `sub_00080880`: `sub_000891A0` (:12263) <- `sub_0008BB00` (match setup). Once per match [V].
* `sub_00080ED0`: `sub_00081240` (vtable slot +4) <- `sub_0008B730` (:18187-18198, `call [eax+4]`) <- `sub_0008BB00`. Once per match [V].
* Whole external interface of the crowd code range 0x7C000-0x81400 (every function outside it that calls into it): the scene-B
  functions (`sub_0002D8C0/D9A0/DB00/DFF0`, scene type 7 only), the getters `sub_00044030/44100/44210`, `sub_00086F40` (round-end stop),
  `sub_00087040` (an arena *render* hook that calls `sub_0007F8F0`, which is a bare `ret`, `recomp_0003.c:93428`), `sub_000891A0`,
  `sub_0008BE60` (`sub_0007E740`), constructors, and the fighter request writers `sub_0007F900/F920` [V, script over the whole tree].
  Nothing from the render subsystem tick, the event manager, `sub_000BD860/BDF40`, or the screen manager.
* Direct-call closures (hand-built graph over all `recomp_00NN.c`): `sub_000796A0` reaches 430 functions, `sub_000BD860` 4,
  `sub_000BDF40` 1, `sub_0001A240` 10, `sub_00077040` 52 (it enters the screen by vtable), `sub_00087040` 4, `sub_00087010` 18; **none**
  contains `sub_0007D560`, `sub_00081170`, `sub_00080880` or `sub_00080ED0`. `sub_00079B60` does (via `sub_0008BE60`).
* The arena's own draw-time pieces are empty in this image: `sub_00087040` calls `sub_001A3300` (a `ret`, `recomp_0012.c:7724`),
  `sub_0007F8F0` (a `ret`) and tail-jumps `sub_000A1E60` (a `ret`, `recomp_0004.c:83792`) [V]. `sub_00087010` is a screen filter
  (`sub_00028460` colour passes, flag `CLogic+0x2FFB0`), not the crowd.
* Scene B (type 7, container `0x3473F0`, manager at +0x400): `sub_0007C2F0`, `sub_0007C100/C280/C940` contain no indirect call, so they do
  not run the vtable update; it is not the fight scene anyway (`sub_0002DFF0` picks A for type 1) [V for the absence, I for the rest].

Timing of the one real caller: `dt = sub_00044C20() * arg`. `sub_00044C20` returns `*0x3BDE80` in arena states 5 and 8 and
`CLogic+8` or `CLogic+4` (`sub_00088D70`, `recomp_0004.c:11409`) otherwise: game state. `arg` is the main loop's `dt`, the constant
`0x28C798` [V]. The rotation index `manager+0x1C` moves once per update (`recomp_0003.c:117330`ff).

## 2. What `sub_0007F030` copies, and who else writes it (question 2)

* Copy: `sub_0007F030` loop (`recomp_0003.c:92128-92140`): `rec+0..0xB = member+0x20..0x2B`, `rec+0x10 = member+0x38`, `rec+0x1C = 0`;
  then the status switch (:92142ff). Nothing else of the member's position is copied [V].
* Writers of `member+0x20..0x37`, from a store scan of the crowd range plus a check that no code outside it holds the member list
  (`manager+0xA90` is addressed only inside 0x7C000-0x81400 for the crowd; the other hits in `recomp_0012.c` are fighter-record
  offsets) [V]:
  * `sub_0007DD10` (member XML): position floats parsed into `member+0x20` (`recomp_0003.c:88735-88739`) and rotation into `+0x2C`
    (:88787-88792); `+0x30/+0x38/+0x3C` set at :88835-88845. At load only.
  * `sub_00080700` (viewer/test member, flag `manager+0x20 & 1`): zeroes `+0x20..0x34` (:115686-115691). Not a fight path.
  * `sub_0007D560` writes only `member+0x30` (current heading, +-5 degrees per update, :87356-87390); `sub_0007D620` writes `+0x3C`;
    `sub_0007F030` writes `+0x3C` too (:92215). Heading drift is visual and per update.
  * Tiny setters `sub_0007D0F0` (`+0x3C`) and `sub_0007D120` (`+0x2C`) have no direct callers (scene-B vtable slots, I).
* Outside the crowd code nothing writes the table records except the fighters' request writers (`+0x1C/+0x20/+0x24`, from the step),
  `sub_001B93B0` (`+0x30 = -1`) and `sub_00080C10` (init) [V, spectator-shove.md section 1].

Consequence: for Club 357 as for every arena the published spectator position and heading cannot depend on the host's frame
timing; the lerp target `fighter+0x898` copied from `rec+0` (spectator-shove.md 3.B) is the same on all PCs. If the fighters'
positions part in a crowd interaction, the cause is *which step* a spectator reaches a status, not *where* it is.
That in turn depends only on crowd state at step 1 (reset by `crowd_reset`, `netplay.c:2550`ff), the rotation index, the crowd
generator `0x375F68`, the private copy of `0x33DB5C` (`netplay_crowd_update`, :2541), the fighters' own data
(`sub_00080D40`), and the weapon pool. A lerp's frame count is derived from distance/time (`sub_001CA070`, `recomp_0012.c:105302`ff): an
off-by-one there looks like "positions a few hundredths apart, states equal" - worth a trace [U].

## 3. Other once-per-render writers (question 3)

Method: closure of the render roots {`sub_000796A0`, `sub_000D02A0`, `sub_000D0C20`, `sub_0001A880`, `sub_001E9550`, `sub_00077170`}
= 633 functions; closure of the step = `sub_00087700` + the 102 handler addresses of `sub_001B54D0` + `sub_001A4A30` = 1091 functions.
Absolute-address stores of the render closure in the fight ranges (0x340000-0x342000, 0x349000-0x34A000, 0x375000-0x377000,
0x39C000-0x39D000, 0x3B8000-0x3C2000): none that belong to fight data (only `0x3412A8` in `sub_00045100`, `0x341590` in `sub_00056280`: UI) [V].
Only 16 functions of the render closure lie in the fighter code range and they are unrelated (`sub_001A3300` is a `ret`, the rest are
runtime stubs near 0x1DC86A..0x1DE887). Addresses touched by both closures: D3D library state (0x3A5030, 0x3A52A0, 0x3A61F8, 0x3A6E14/18),
CRT state (0x3CCA98, 0x3CCC60) - not fight inputs. Functions in both closures (149): the registry lookup `sub_00012CF0`, the allocator
`sub_00029680`, `sub_000AA2B0/AA340` (generator draws), the D3D wrappers 0x107230-0x10E0C0, the CRT and C++ runtime.

Best candidates, strongest first:

1. **Shared generator `0x39C0E0` drawn at render cadence.** `sub_000D8C40` (the fade/overlay object at `0x3A0188`) is called from the render
   `sub_000796A0` (`recomp_0003.c:77001`; same call in the other screens at :72104, :78240, :81821) and draws
   `sub_000AA340(0x39C0E0, 0xFF)` (`recomp_0006.c:35886-35887`) when the object's `+0x4EC` and byte `+0x98` are set (a flicker mode; which
   screens set them is not read, [U]). The step also draws it: camera director `sub_000AC590` -> `sub_000AC1F0` -> `sub_000AACA0` /
   `sub_000ABB70` (`recomp_0005.c:8100, 10273`; `ABB70` also contains the shot choice that sets the match time scale through
   `sub_00044C10`, `:10899-10900`, `:11649`), plus the per-step fade `sub_000D90F0` (`recomp_0006.c:36421-36422`). If a fight ever passes
   through the flicker mode, the camera's choices and hence the time scale `0x349764` (and so `0x3B8F2C`, the scale that moves the fighters)
   depend on the number of rendered frames [I]. `fighter_hash` part 5 (`netplay.c:2375`) hashes exactly `0x39C0E0` and the time scale,
   so an observed equal part 5 disfavours it [I].
2. **Shared game heap and the D3D layer.** The render path allocates and frees from the same allocator (`sub_00029680`) as the step
   (weapon slots, particles, sound commands, animation mirrors per `rollback-step-structure.md`). No fight logic keyed on a pointer was
   found, but pointer-ordered behaviour (a free list, hash by address) is not excluded [U].
3. **Not walked.** Indirect calls from the render path (scene draw, model draw via the renderer, `sub_001ECE20`'s host-timer callbacks at
   `0x3C9508`, the producer and audio threads) are outside the direct-call method. `rollback-step-structure.md` 0.7 found no guest thread that
   writes fighter or CLogic state [V for reachability]; the callbacks of `sub_001ECE20` were not read [U].
4. **Not a frame-cadence path but the same symptom**: float library differences between CPUs. The lifted x87 code calls the host's `atan2`,
   `sin`, `cos` (for example `fpatan` -> `atan2`, `recomp_0005.c:4481`); results can differ in the last bit between machines with and without
   FMA paths in the C runtime, and a truncated frame count (`sub_001CA070`) or a threshold can turn one bit into a visible step [I]. Worth
   keeping in mind when the four PCs are different CPUs.

## 4. Probes that settle it (not run)

1. Render writes: hash `ACTORS .. +4*0x12E8`, `0x3BDE80` (0x27C bytes), `0x340D58` (0x550) in `sub_00077740_enter` (render start, the last step
   is over) and again in `sub_0002C530_enter` (next step start); equal hashes on a thousand frames prove the render path and the loop tail
   write no fight state. `netplay.c` already has the hash code (`fighter_hash`).
2. Count calls of `sub_000D8C40`'s draw: `RECOMP_WATCH_EXEC` cannot see a call site; use `sub_000AA340_enter` with `MEM32(g_ecx)`-style check of
   `g_ecx == 0x39C0E0` and the return address `0xD8D08`; compare against steps and frames.
3. For the real divergence: log per step, for the two fighters in a catch, `fighter+0x860..0x868` (lerp step), `+0x898..` (target) and
   `rec+0..0xB` of the bound record. If `rec+0` is equal on all PCs (this report predicts it), the difference is in the lerp's frame count or in
   when the state was entered, not in the spectator.

## Evidence index

| Item | File:line |
|---|---|
| `sub_0007D560` body, reads `+0x20/+0x2C` into `sub_00084260/84190` | `recomp_0003.c:87290-87420` |
| `sub_00084190`, `sub_00084260` (position sent to model by `sub_00024BE0/24BA0`, no store to the member) | `recomp_0004.c:483-760` |
| `sub_00081170` update; `sub_0007D560` calls | `recomp_0003.c:117217-117380` |
| Crowd update call from the arena frame | `recomp_0004.c:19045-19061` |
| `sub_0007F030` copy | `recomp_0003.c:92089-92140` |
| Member XML parse | `recomp_0003.c:88574`, :88735-88792 |
| Arena draw hook `sub_00087040` (stubs) | `recomp_0004.c:6952-6990` |
| Render `sub_000796A0`; fade draw | `recomp_0003.c:76866`; `recomp_0006.c:35769-35976` |
| Main loop `dt` | `recomp_0000.c:5075`; `recomp_0001.c:19817-19898` |
| Hooks that already neutralise the crowd | `netplay.c:2541-2548` (crowd update with private generator), :2550ff (`crowd_reset`), :2756-2767 (dt) |

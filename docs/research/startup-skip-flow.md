# Skipping the startup legal screen and movies

2026-10-17. Research against the owner's image, current generated C and APT
metadata, followed by native macOS arm64 play tests by the coordinating agent.
This report contains behavioral findings and addresses, no extracted game
bytes, lifted bodies or captures. Captures remain under ignored `logs/`.

## Result

The normal title screen is `intMain/preStart.swf`. The implemented option is
a one-time change to the front-end manager's pending
screen ID, from 0 (legal) to 1 (preStart), immediately before its ordinary
screen-selection routine `sub_0006BC90` runs. Its original body should still
run. It preserves the controller initialization normally performed by legal's
first ActionScript frame. Native measurements below distinguish the static
reasoning from the tested behavior.

## Verified static flow

The native screen-name table starts at guest `0x002DFE08`. Relevant entries:

| ID | Screen | Name pointer |
|---|---|---|
| 0 | `intMain/legal1.swf` | `0x00299FD4` |
| 1 | `intMain/preStart.swf` | `0x00299FE8` |
| 15 | `intMain/mainMenu.swf` | `0x0029A0F8` |
| 19 | `intMain/legal1.swf` | `0x0029A148` |
| 20 | `intMain/demoStrt.swf` | `0x0029A15C` |

`sub_0006BC90` (generated in `recomp_0003.c`) reads the pending ID from
`this+0x9C`, writes it to current ID `this+0x98`, replaces pending with `0x21`,
selects the table entry, and continues the ordinary screen/loader setup. It
has direct callers; `recomp_lookup_manual()` alone cannot intercept it.
An added `sub_0006BC90_enter()` is the supported way to adjust the pending ID
without replacing the implementation. It needs regeneration after addition.

`sub_0006BF90` creates and initializes the front-end player, calls
`sub_0006B760` to publish loader globals, then calls `sub_0006BC90` with guest
return address `0x0006C03E`. The other direct call sites return to
`0x000782EF` and `0x0007BD21`. The implementation consumes only the first
selection and requires pending ID 0. A different first screen is logged and
left alone; later requests are always left to the game.

APT metadata supplies the following presentation chain:

1. Legal frame 0 calls `Controller.ControllerBootup()`; most of the remaining
   73-frame timeline draws the legal presentation. Final frame 72 requests
   `intMain/bootLoad.swf` via the parent's `transitionStart`.
2. BootLoad performs `MemCard.QueryBootupCheck` and conditionally
   `MemCard.BootupLoad` only when `g_inGame == 1` and the platform is **not**
   Xbox. On Xbox it calls its callback directly. The callback invokes
   `Game.StartMainMenu(0)`.
3. `Game.StartMainMenu` is registered to native `sub_00046610`. It creates
   the actual intro-controller object: vtable `0x0028996C` has initialization
   `sub_000D7050` and update `sub_000D7060`. Its numeric parameter becomes
   the state at object+4, and object+8 starts at `0x11` (state bookkeeping,
   **not** an event type). It clears controller state, installs the object
   at game-data manager `+0x38C` through `sub_0001F1A0`, and invokes its update.
   Initialization `sub_000D7050` returns 0 without other work. Avoiding this
   call intentionally avoids creating the startup movie controller.
4. Native intro state handler `sub_000D7060` requests `movies/eagames`,
   `movies/thx`, `movies/intro` in states 0, 1, 4 respectively. State 2 sets
   front-end pending screen 1 and requests scheduler state 9. State 5 is the
   separate credits path (front-end pending `0x1E`), so blanket movie skipping
   at this shared handler would need careful state gating.
5. PreStart has its own Start handler and a 3600-tick attract timer. Start
   performs the Xbox boot-up memory-card query/load, then transitions to
   `intMain/mainMenu.swf`. Its attract path calls `Game.StartMainMenu(4)`.

The checked `feflow.xml` confirms preStart/mainMenu are normal front-end
screens. `demoStrt` is a separate screen and is not in feloader's normal
screen array. Existing tests/log anchors should target `prestart`.

## Initialization that cannot be silently discarded

Legal's `Controller.ControllerBootup()` is registered by `sub_00055E00` to
`sub_00055640`. That function uses manager slot `0x19` in the manager table
at `0x002FB148` and calls:

- `sub_000DCAD0`: sets controller manager `+0xA14` to 4.
- `sub_000DCB40`: clears bindings and sends a zero state to applicable
  controller objects; it walks eight device slots.
- `sub_000DD630`: installs mappings for four controller devices.

It then returns a result string at `0x00341330` used by legal's disconnected
controller warning. PreStart has no equivalent ControllerBootup call. A
candidate that bypasses legal should preserve the native initialization once
before exposing title input; do not infer it is optional from a successful
single-pad run. The initialization wrapper does not inspect its two stack
arguments. Its native ABI consumes the guest return address and 8 argument
bytes (`ret 8`); a synthetic call requires two arguments plus a return word.
Save and restore guest caller-visible registers around such a helper. The
normal game's call returns a string; no synthetic APT result is needed when
calling it only for initialization.

## Hook and ABI constraints

- `sub_0006BC90_enter()` must preserve the original entry's `g_ecx`, stack
  and registers. The original function's caller still owns the pending
  guest return address and argument. The entry hook does **not** pop either.
- The generated `sub_0006BC90` returns with `ret 4`; letting its ordinary
  epilogue run retains this contract and the complete screen setup.
- Declaring an `extern sub_0006BC90_gen()` wrapper would cause direct callers
  to call `_gen` directly and miss the wrapper; use the `_enter` convention.
- `sub_00068BA0` (existing FuncCall wrapper) is indirect-only and suitable
  for tracing. Returning early from a legal FuncCall would skip only the
  native command, not the legal timeline or movie request chain.
- Do not skip the global MAD decoder/open path. Story movies and attract
  playback share movie infrastructure. A startup screen decision is narrower.

## Validation requirements from the static review

Skipping bootLoad also omits its `Game.StartMainMenu(0)` controller creation.
Static reading shows controller-binding cleanup and startup-movie-controller
ownership side effects; it does not prove these are required when preStart
is already loaded through the normal player setup.
Measure native state progression, input, menu-to-match, return-to-title,
attract movie and fresh-Story cutscenes. If the initial redirect leaves the
outer lifecycle in the legal/boot state, use the trace to identify the
specific normal transition rather than inventing an initialization bypass.

The title's first visit has an ActionScript delay: preStart's first frame
sets `iPressStartDelay=240`, then its frame-133 movie timer resumes the
timeline before frame 134 defines the Start handler. Returning to the title
uses an `alreadyBeenToStart` variable to omit the wait. There are two
similarly named variables in the script (`alreadyBeenToStart` and
`g_bAlreadyBeenToStart`); changing the latter cannot be assumed to remove
the delay. This delay is separate from startup movies/legal and can remain
unless the requested behavior includes immediate input readiness.

For the option-off control, verify the complete legal/boot/movie sequence.
For option-on, verify the earliest screen anchor/capture is preStart, no
startup movie is opened, Start and memory-card processing still function,
and ordinary Story movies and attract playback still occur. No claims here
are based on a missed breakpoint or capped log count.

## Follow-up: earlier controller boot, hot-plug and save checks

The parent measured preStart at about 4 seconds with the candidate, followed
by Start at 18 seconds and main menu at 24 seconds; it also captured the first
fresh-Story movie. Those are coordinating-task observations, not runs by this
research task. The remaining question was whether moving ControllerBootup
ahead of the original legal frame loses a device-enumeration window.

**No one-time enumeration dependency was found in the moved helper.** Global
manager setup already performs the same mapping operation before the front
end is created:

- `0x0001272A` calls constructor `sub_000DD800`. It constructs four embedded
  Xbox-device objects at manager `+0x8D4`, stride `0x50`, sets their port IDs
  to 0, 1, 2, 3, initializes bindings, and sets manager `+0xA14` to 4.
- `0x00012737` calls `sub_000DCA20`, which initializes low-level input,
  registers the input poll callback and links manager slot `0x27` at `+0x8CC`.
  Slot `0x19` is registered before `0x0001274B` calls `sub_000DD630`.
- `sub_000DD630` only installs pointers to the four already-constructed
  embedded objects in the mapping array at manager `+0x64`. It does not query
  device presence, take a connection snapshot or wait for enumeration.
- `sub_000DCAD0` sets the same four-port limit already used by the constructor.
  `sub_000DCB40` sends zero output to currently mapped type-2 devices, clears
  their rumble marker and clears the eight bindings. `sub_000DD630` immediately
  reinstalls the four default mappings. Missing devices do not abort the loop.
- Runtime update `sub_000DD930` compares each current port's status from
  low-level manager `+0x17D0..+0x17DC` with the embedded object's previous
  `+0xC` status on every update, accumulating insert/remove transitions.
  Low-level callback `sub_001EBBE0` separately polls insert/remove masks and
  opens/closes devices continuously. The moved boot helper does not disable
  either update path. `sub_000DCA20` also invokes this poll once during global
  initialization, before the candidate hook.

Thus an earlier invocation preserves constructed objects, default mappings
and later device discovery. It does not require controllers to have finished
USB enumeration before `sub_0006BC90`. This is a static conclusion about the
game helper; it does not certify every physical host device or connection.

The host keyboard and SDL pads enter these same emulated Controller S ports.
`pc_input_start()` starts the host reader and sizes the hub; its slot callback
`xbox_UsbSetPadCount` allows a pad arriving later to create another emulated
controller. Moving the guest mapping reset does not bypass this host path.

**No Xbox memory-card boot operation is lost.** The bootLoad branch condition
above bypasses its query/load on Xbox even in the normal flow. PreStart's
Start handler performs the Xbox query/load. `QueryBootupCheck` is native
`sub_00052D10`: its once-only flag is `0x0034153C`, and it reads a boot option
from manager slot `0x13`. `BootupLoad` is `sub_00052470`, which starts the
memory-card operation through `sub_000D0CC0`. Both remain on the candidate's
normal Start path before the main menu. Do not synthesize either at the
early hook or set their once-only flag.

Focused follow-up play-tests, with skip enabled and a complete-save guard:

1. Run the existing `versus` route with the host keyboard as player 2
   (`RECOMP_INPUT_HOST=1`, `RECOMP_INPUT_IGNORE_FOCUS=1`, `RECOMP_PAD_HOST=1`,
   `RECOMP_INPUT_NO_PADS=1`, `RECOMP_PAD_SCRIPT_KEYS=1`,
   `RECOMP_KEYBOARD_PLAYER=2`). Verify both players join and control their
   selected fighters; this exercises keyboard input through the normal ports.
2. Run `versus` through SDL virtual pads, starting with one pad and a second
   arriving after the title loads: the common host/focus/script flags above,
   `RECOMP_INPUT_VIRTUAL_PADS=1`, `RECOMP_INPUT_VIRTUAL_LATE_MS=8000`,
   `RECOMP_PAD_SCRIPT_VIRTUAL=1`, `RECOMP_KEYBOARD_PLAYER=off`. Do not force
   two initial USB pads. Require `[INPUT] a virtual pad was plugged in`, slot-2
   assignment and `[OHCI0] pad 2 arriving`, then verify player 2 joins and
   responds in the match. This covers discovery after the moved reset.
3. On a prepared disposable save and on a fresh save, press Start and verify
   the normal query/load/popup path reaches the main menu. The fresh-Story
   movie already observed is useful evidence that movie decoding remains
   active; retain an attract-mode check for the independent title timer.

The code reads support proceeding with these tests; no additional controller
wait, save initialization or second boot-helper call is justified by this
follow-up.

## Audio initialization (issue #65)

The first release of the option (v0.10.1 to v0.10.4) left Press Start and the menu transition sound effects silent, although
music played and the front end made the same `Audio.Play(...)` calls as a normal boot.

`sub_0003DE20` is the audio engine's initialization, `AudioInit(mode)` (cdecl, one argument). If the engine is already
up (`0x00340374 == 1`) it first shuts it down through `sub_0003A570(0)`. Mode 0 then only sets `0x00340374 = 1`. Any other mode
also reads the audio configuration, builds the music list and sets `0x00340320 = 1`. A normal boot calls it twice, both from
the state-0 entry `sub_00078090` (traced with temporary entry logging, not kept):

| When | Call | Result |
|---|---|---|
| First state-0 entry, before `legal1` | `sub_0003DE20(0)` | `0x340374 = 1`; `0x340320` stays 0 |
| After the startup movies, state 0 is entered again | `sub_0003A570(0)`, then `sub_0003DE20(1)` | full init: `0x340320 = 1`, music list and configuration loaded |

Skipping `bootLoad` and the movie controller removes the second entry. A guest memory dump at the first `Audio.Play(0)` differed
from a normal boot in exactly these places (`0x340320`, the music name list near `0x33E798`).

`defjam_skip_intro_enter` now calls `sub_0003DE20(1)` once, in the same place it replays `ControllerBootup`: after the first state-0
entry and before the screen is selected, so the full init is in place before `preStart.swf` loads. The call is made on the same
terms as the game's own (return word, one cdecl argument, caller drops it) and the caller's registers are restored.

Checks: `regress.py --only skipintro` requires `0x00340320 == 1` (`RECOMP_PEEK`) in every enabled case; it fails on v0.10.4
and passes with the fix. The native fixture asserts the call and its stack layout. On the Mac, `RECOMP_APU_PCM` captures of the
`boot` route showed no output at the menu sound events before the fix and the same bursts as a normal boot after it.

## Implementation and native acceptance

`src/patches/skip_intro.c` reads the effective `[debug] skip_intro` setting on
the first screen selection. It is off by default; `RECOMP_SKIP_INTRO=1` or `0`
overrides the file. `sub_0006BC90_enter()` in `src/recomp_manual.c` connects the
patch to direct and indirect callers after re-lifting. The original selector
still runs. A different first screen is logged and left unchanged. The
one-shot latch is consumed even when disabled, so changing the setting in the
overlay never changes a running game. The startup patch changes no toolkit,
lifter or renderer code; the initial validation uses released pin `4a6c88e`.
The owner's later PR25 integration requires combining its recorder runtime
with main's released audio topics, documented in
[the integration report](startup-skip-pr25-integration.md).

Compatibility with the existing debug unlock needs one additional boundary in
`dev_unlock.c`: its old activation waited for `Game.StartMainMenu(0)`, which
bootLoad no longer calls when skipping. The actual main-menu screen request
now also arms it and applies the unlock before that menu handles the profile.
The ordinary boot still takes the original activation path.
This keeps both `unlock_all` and `RECOMP_DEV_DEBUG` usable with startup skip.

The launcher and overlay expose **General → Debug → Skip startup movies and
legal screen**. A choice made before Play applies to that launch; an overlay
choice applies next launch. A brief resource-loading crest and the title's
own opening animation/input delay remain.

Native measurements use source `9c17309` plus this patch, regenerated output
and a certified `posix-release` build. All runs use disposable save roots;
every guard restored identical bytes. The owner's saves/settings are not test
inputs for writing. The reported times are host log observations sampled at
two-second intervals, not a repeated performance benchmark.

| Check | Evidence |
|---|---|
| Normal startup | `run-20261017-143136.log.err`: legal1 → bootLoad → EA/THX/intro → preStart, observed at102s. One pre-existing boot non-code call to `0x80003344` was logged; it is retained, not filtered. |
| Skip enabled | `run-20261017-143558.log.err`: first screen preStart at4s, no legal/bootLoad or startup movie opens, one redirect. Existing title golden passes; rendered Press START inspected at12s. Start runs QueryBootupCheck/BootupLoad and reaches mainMenu at24s. No faults. |
| Settings precedence | All5 final cases pass: default off, INI on, INI on/env0, INI off/env1, and combined unlock. Enabled cases match the existing title golden and reach the menu with Start (`logs/startup-skip/option-matrix-final.txt`,100s). |
| Real launcher | `run-20261017-143919.log.err`: pointer clicks select General and toggle the checkbox; INI saves true, Play applies it immediately, first screen preStart. Checked UI and title frames. |
| Fresh Story | `run-20261017-143949.log.err`:152s, both opening scenes visibly render, creator reached;4 captures, no crash/watchdog/unresolved or non-code call. |
| Played match | `run-20261017-144222.log.err`:253s, all7 combat assertions pass: telemetry, setup, movement, player attack, damage, decisive result and results screen. Median120 presents/2s; captured fight inspected. |
| Idle attract | `run-20261017-144635.log.err`:165s, native timer calls StartMainMenu(4), intro.mad opens and visibly renders, then returns to preStart. One initial redirect only; no faults. |
| Fresh save/host keyboard | `run-20261017-144925.log.err`:34s, real keyboard-script path presses Start and reaches the menu; initially absent save root restored to absence. |
| Late SDL pad/two players | `run-20261017-145000.log.err`:211s, second virtual pad arrives8s into boot, after preStart loads. Host assigns player2, guest OHCI hot-plugs port4. All6 assertions pass, including both players walking and landing damaging hits; no faults, save restored. |
| Both debug options | `run-20261017-145651.log.err`:64s, startup skipped and unlock routine runs; Unlock Fighters visibly shows74/74 and all portraits. No faults, save restored. |

The native game-free fixture tests file/environment precedence (including
explicit `false`), unexpected first-screen refusal, one-shot scope, synthetic
controller-call stack layout, register preservation and leaving the current
screen field to the generated selector, and the combined debug options without
the omitted startup event. `regress.py --only skipintro` provides
the repeatable five-case startup check. Per the owner's follow-up (D124), harness
routes now default to skip enabled and use the actual preStart request as their
clock anchor. `m3` alone forces normal startup in both quick/full sets and requires
the ordered legal/movie chain plus the existing title golden. Explicit env0
retains normal startup for an individual route; an empty value lets INI/default
settings apply. This policy also reaches scenario runs that sanitize inherited
environment variables.

The integrated full run caught a first-menu timing gap in the initial compatibility
change: the file-only unlock boot enabled the option and reached mainMenu, but
the newly armed hook returned until a later screen change. That fails the existing
`unlockopt` gate, which stops at the menu. The fix applies unlocks at that first
menu immediately; the native fixture now asserts the call at the first event,
then a second call for the following screen. Evidence retained in
`logs/startup-skip/pr25-full-unlock-boundary.txt` and `run-20261017-152910.log.err`.
The setting/export path was already working; no gate or test duration was weakened.

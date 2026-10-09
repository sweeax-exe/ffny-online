# M4a — feloader's screen-transition system: easeInOut, the pan/swap/door functions, and what drives them per frame

Produced offline with `scripts/apt-dump.py screens/screens.viv screens/feloader.big --actions`
against the user's own dump (`DEFJAM_DATA=<data>`). All addresses below are
absolute byte offsets into `feloader.apt`'s own stream — the same numbers the tool prints — inside
the movie's first Action block (`Action @0xB154`, main-timeline frame 0). No game bytes are quoted;
everything below is opcode mnemonics, resolved constant-pool strings, and my own paraphrase.

This report does not build or run the game and does not edit anything outside
`scripts/apt-dump.py` (the tool fix below) and this file.

## 0. Why this was unreadable before, in one paragraph (CONFIRMED)

`Action @0xB154` used to stop decoding at file offset `0xC175` on `EA_CallMethod` (opcode `0x5E`),
which OpenSAGE's `InstructionCollection.Parse` never implements (confirmed by reading that switch
statement directly from `github.com/OpenSAGE/OpenSAGE`, commit `588ac47`: it has cases for
`EA_CallFunc`, `EA_CallFuncPop` and `EA_CallMethodPop` — all no-operand — but no case at all for
`EA_CallMethod`). Manual byte tracing from `0xC175` (see the comment added above `CONFIRMED` in
`apt-dump.py`) shows `EA_CallMethod` is a bare, no-operand opcode, exactly like its three siblings:
treating it that way keeps every following instruction on a real opcode boundary all the way to
`0x00CE2E End`, including a `BranchIfTrue` target that lands exactly on `EA_PushThisVar`. With that
one-line fix, the block decodes fully (`0xB154`–`0xCE2E`, ~13.4 KB, was previously cut off after
~4.2 KB) and includes everything below — `transitionStart`, `transitionDone`, `setupMovie`, all
four `do<Type>` functions, and the `Math.easeInOut` definition — which were previously invisible
past `0xC175` or, in `easeInOut`'s case, simply never reached because the whole tail of the block
was undecoded.

Everything quoted below is marked **CONFIRMED** (read directly from the bytecode, opcode by
opcode) or **INFERRED** (a conclusion I draw from confirmed facts, not itself present as bytes).

---

## (a) `Math.easeInOut` — CONFIRMED

Defined at the very end of the block, right before the block's own `End`:

```
0x00CDEA  push Math                       ; assignment target object
0x00CDEC  push "easeInOut"                ; assignment target member name
0x00CDEE  DefineFunction  name=''  params=(t)  body_size=37  body_at=0xCE08
    0x00CE08  push "fRet"
    0x00CE0B  push "t";  GetVariable            ; t
    0x00CE0F  push 3.14159
    0x00CE14  Multiply                          ; t * 3.14159
    0x00CE15  push 1.5708
    0x00CE1A  Subtract                          ; (t*3.14159) - 1.5708
    0x00CE1B  push 1                            ; argc=1 for the CallMethod below
    0x00CE1C  push Math
    0x00CE1E  push "sin"
    0x00CE21  CallMethod                        ; Math.sin(t*3.14159 - 1.5708)
    0x00CE22  push 1
    0x00CE23  Add2                              ; + 1
    0x00CE24  push 2
    0x00CE26  Divide                            ; / 2
    0x00CE27  SetVariable                       ; fRet = (Math.sin(t*3.14159 - 1.5708) + 1) / 2
    0x00CE28  push "fRet"; GetVariable
    0x00CE2C  Return                            ; return fRet
0x00CE2D  SetMember                       ; Math.easeInOut = <the function above>
0x00CE2E  End
```

So, exactly:

```
Math.easeInOut = function(t) {
    var fRet = (Math.sin(t * 3.14159 - 1.5708) + 1) / 2;
    return fRet;
}
```

**One parameter, `t`.** `3.14159` and `1.5708` are literal `float` constants in the bytecode
(π and π/2 to single precision, not computed) — CONFIRMED, `EA_PushFloat` operands, not named
constants. Algebraically `sin(x - π/2) = -cos(x)`, so this is the standard cosine ease curve
`fRet = (1 - cos(π·t)) / 2`: monotonic on `t ∈ [0, 1]`, `easeInOut(0) = 0`, `easeInOut(0.5) = 0.5`,
`easeInOut(1) = 1`, zero slope at both ends. `DefineFunction`'s `body_size=37` lands the function
body's last byte exactly on `Return` (`0xCE08 + 37 = 0xCE2D`, and `0xCE2D` is the outer `SetMember`)
— an independent confirmation that the decode is correctly aligned here.

The only caller of `Math.easeInOut` anywhere in this block is `dopanDown`/`dopanUp` (below), always
with a single argument, `g_nTransitionIndex`.

---

## (b) The transition state machine — CONFIRMED, with the per-frame driver and final positions

### Named pieces and what each one is

| Name | What it is (CONFIRMED from usage) |
|---|---|
| `g_mcCurrentClip` | var holding the on-screen movie clip (set to top-level clip `clip1` at init, `0x00CDC6`) |
| `g_mcNextClip` | var holding the incoming movie clip (`clip3` at init, `0x00CDCC`) |
| `clip2` | the transition-effect clip (per the task's own context: hosts `trans/t1.big`, a 640×2000 white slab) |
| `overlay` | a separate UI clip (header/title fade) that panDown/panUp also slide |
| `g_currentTransitionType` | string: `'panDown'`, `'panUp'`, `'swap'`, `'doorZoomIn'`, or `'doorZoomOut'` |
| `g_nTransitionIndex` | the `t` fed to `easeInOut`; the per-frame progress counter |
| `g_nTransitionIncrement` | per-call step added to `g_nTransitionIndex` (`0.005` for pan/door, `1` for swap) |
| `changeY`, `oldY1`, `oldY2`, `oldY3` | precomputed by `setupMovie()`; the per-type endpoints the pan functions interpolate between |
| `g_iSize` | screen size read from the loading movie's own metadata (see `transitionStart`, below) |

None of these clips (`clip1`/`clip2`/`clip3`/`overlay`) are placed anywhere in `feloader.apt`'s own
frame data — CONFIRMED negative: `apt-dump.py --frames` on this movie shows no `PlaceObject` with
`name='clip2'` (etc.) at all. They must be named clips that live on whatever movie hosts `feloader`
as a child (their initial `PlaceObject` matrix — including any inherited rotation/skew — is defined
there, not here; out of scope for this dump).

### `transitionStart(sNextMovie)` — `0xBA23`, body 240 bytes (CONFIRMED)

```
g_nReadyCode = 0
trace("[APTLOADER] Start Transition to " + sNextMovie)
if (_root.g_inGame != 1) SyncGlobalData(0)
_root.onFirstClip = false
ActiveController(0, 1)
g_sNextMovie = sNextMovie
trace("Screen to load " + sNextMovie + " -> screen index = " + g_nScreenIndex)
str = (_root.g_inGame != 1)
      ? FuncCall("Control.GetScreenInfo(" + sNextMovie + ")", 1, _level0)   ; EA_CallNamedMethod on _root, name="FuncCall"
      : getScreenInfo(sNextMovie)                                          ; direct AS call, in-game path
trace("[LOADER] Screen info = " + str)
g_oParamList.resetString(str, 1)
g_oParamList.nextValuePair(0)
szFile2Load = g_oParamList.getValue(0)
g_oParamList.nextValuePair(0)
iTypeIndex  = g_oParamList.getValue(0)
g_currentTransitionType = g_TransitionTable[iTypeIndex]
g_oParamList.nextValuePair(0)
g_iSize = ToInteger(g_oParamList.getValue())            ; CallMethod (stack-based), then ToInteger
if (_root.g_bInShop == 1 && g_currentTransitionType != 'swap') {
    trace("[LOADER] - IN SHOP ONLY - KILL CLIP!!!!")
    clip2.loadMovie('trans/empty2.swf')                  ; GetURL2 clip2, "trans/empty2.swf"
    g_mcCurrentClip.loadMovie('trans/empty2.swf')
} else {
    setupMovie(0)                                        ; <-- see below
}
trace("File2Load = " + szFile2Load + "     transition= " + g_currentTransitionType + "    size=" + g_iSize)
g_sNextMovie = g_mcNextClip.loadMovie                     ; GetURL2 g_mcNextClip, g_sNextMovie  (loads the new screen)
clip2.loadMovie = szFile2Load                             ; GetURL2 clip2, szFile2Load           (loads t1 into the slab clip)
trace("[APTLOADER] ***...***")
Play()
```

(`getScreenInfo(sMovie)`, `0xB8CC`, walks `g_aScreens`/`g_aScreenBranches` to build the
`"tscreen=<clip>&type=<n>&data=<size>"` string; **CONFIRMED it returns `''` — empty string,
`0x00BA20`–`0xBA22` — if `sMovie` isn't found in `g_aScreens` at all**, not an error or a default
record.)

### `setupMovie()` — `0xB75C`, body 340 bytes, `switch(g_currentTransitionType)` (CONFIRMED)

Every branch converges at `0x00B8CC`, which is exactly `body_at(0xB778) + body_size(340)` — i.e.
these are ordinary implicit returns off the end of the function, not a jump into the next
function's `DefineFunction` opcode.

```
switch (g_currentTransitionType) {
  case 'panDown':                                   // 0xB7C0
    clip2._y = 480;  clip2._x = 0;
    g_mcNextClip._y = clip2._y + g_iSize;    // = 480 + g_iSize  (reads clip2._y AFTER the line above)
    g_mcNextClip._x = 0;
    changeY = (0 - 480) - g_iSize;           // = -480 - g_iSize
    oldY1 = 0;  oldY2 = 480;  oldY3 = g_mcNextClip._y;   // = 480 + g_iSize
    g_nTransitionIncrement = 0.005;
    break;
  case 'panUp':                                     // 0xB80C
    clip2._y = 0 - g_iSize;  clip2._x = 0;
    g_mcNextClip._y = clip2._y - 480;        // = -g_iSize - 480
    g_mcNextClip._x = 0;
    changeY = g_iSize + 480;
    oldY1 = 0;  oldY2 = clip2._y;  oldY3 = g_mcNextClip._y;
    g_nTransitionIncrement = 0.005;
    break;
  case 'swap':                                      // 0xB858
    clip2._y = 0; clip2._x = 0; g_mcNextClip._y = 0; g_mcNextClip._x = 0;
    g_nTransitionIncrement = 1;              // changeY/oldY1..3 NOT touched -- doswap() never reads them
    break;
  case 'doorZoomIn':                                // 0xB87C
    clip2._y = 0; clip2._x = 0; g_mcNextClip._y = 0; g_mcNextClip._x = 0;
    g_nTransitionIncrement = 0.005;          // changeY/oldY1..3 NOT touched -- dodoorZoomIn() never reads them
    break;
  case 'doorZoomOut':                               // 0xB8A4
    clip2._y = 0; clip2._x = 0; g_mcNextClip._y = 0; g_mcNextClip._x = 0;
    g_nTransitionIncrement = 0.005;
    break;
  default:
    // no case, no branch taken -- setupMovie() falls straight through to its own end and does
    // *nothing*: clip2/g_mcNextClip keep whatever position they already had. CONFIRMED: the
    // switch is a closed if/elseif/elseif/elseif/elseif chain with no trailing `else`.
}
```

### The four `do<Type>` functions (CONFIRMED)

```
dodoorZoomIn()   { gotoAndFrame(1); g_mcCurrentClip.play(); }     // 0xB5BF, body 14 bytes -- that's the entire function
doswap()         { }                                              // 0xB68C, body 0 bytes  -- a genuine no-op
dopanDown()      { yMovement = Math.easeInOut(g_nTransitionIndex) * changeY;
                    g_mcCurrentClip._y = oldY1 + yMovement;
                    clip2._y           = oldY2 + yMovement;
                    g_mcNextClip._y    = oldY3 + yMovement;
                    overlay._y         = oldY3 + yMovement;  }     // 0xB5E6, body 56 bytes
dopanUp()        { yMovement = Math.easeInOut(g_nTransitionIndex) * changeY;
                    g_mcCurrentClip._y = oldY1 + yMovement;
                    clip2._y           = oldY2 + yMovement;
                    g_mcNextClip._y    = oldY3 + yMovement;
                    overlay._y         = oldY1 + yMovement;  }     // 0xB638, body 56 bytes (NOTE: overlay uses oldY1 here, not oldY3 -- dopanDown uses oldY3 for it; read directly from the operands, not a transcription slip)
```

**No `dodoorZoomOut` function exists anywhere in this block** (CONFIRMED negative — the full,
now-complete function list is: `SyncGlobalData`, `dodoorZoomIn`, `dopanDown`, `dopanUp`, `doswap`,
`setupMovie`, `getScreenInfo`, `transitionStart`, `transitionDone`, the `Math.easeInOut` anonymous
function, `CSimpleParam.prototype.{isDone,getName,getValue,resetString,nextValuePair}`, and
`PlayShopGreeting`). `'doorZoomOut'` is only ever compared against as a string, in `setupMovie`'s
switch.

**Neither `_rotation` nor `_xscale`/`_yscale` appear anywhere in `feloader.apt`'s constant pool at
all** (CONFIRMED negative, grepped the full `--actions` dump). Nothing in this movie's own bytecode
ever writes a rotation or scale to `clip2`, `g_mcCurrentClip`, `g_mcNextClip`, or `overlay` — only
`_x` (always to the literal `0`) and `_y` (animated, above). A rotated/skewed appearance can only
come from that clip's own inherited `PlaceObject` matrix on whatever movie places it (not examined
here) or from a value fed into `_y`/`_x` that the renderer maps unexpectedly (see (c)).

### What drives the per-frame update (CONFIRMED — main timeline, not `onEnterFrame`/`getTimer`)

`GetTimer` does not appear anywhere in `feloader.apt` (CONFIRMED negative, grepped). The animation
is driven by the movie's own main timeline advancing one frame per render tick (`ms_per_frame` from
the movie header) through a small ping-pong between frame 3 and frame 4:

```
frame 1: Stop
frame 2: Stop; place char 171 (a clip, no visible name) at depth 3 with a per-frame
         clip-event handler at 0xFEF4 (see below)
frame 3 (Action @0xDB60):
    if (g_nTransitionIndex > 1) {
        transitionDone();
        GotoFrame(1);                       // idle
    } else {
        window["do" + g_currentTransitionType]();   // EA_CallFuncPop on a computed name --
                                                      // this is the literal mechanism that ties
                                                      // g_currentTransitionType's string values
                                                      // ('panDown','panUp','swap','doorZoomIn',
                                                      // 'doorZoomOut') to the do<Type> function names
        g_nTransitionIndex += g_nTransitionIncrement;
        // falls off the end of the frame-3 action -> timeline auto-advances to frame 4
    }
frame 4 (Action @0xDB98): identical shape, except its "not done" branch ends with
    GotoFrame(3); Play();                   // -> back to frame 3, repeat
```

So the interpolation driver is: **once per rendered frame, call `do<g_currentTransitionType>()`
(which reads `g_nTransitionIndex`, applies `easeInOut`, and writes `_y`), then advance
`g_nTransitionIndex` by `g_nTransitionIncrement`, until `g_nTransitionIndex > 1`.**

Separately, the clip placed at frame 2/depth 3 (character 171) carries a per-frame handler at
`0x00FEF4` (`apt-dump.py` labels its event flag `KeyPress`, but see `docs/research/m3-feloader-script.md`'s
correction: that flag value is EnterFrame in real SWF numbering, not KeyPress — the label is an
artifact of `apt-dump.py` reusing OpenSAGE's enum names, not evidence about semantics). Read
directly:

```
0x00FF00  if (_parent.g_nReadyCode < 2) return;      // gate: waits for a "ready" signal
0x00FF10  if (_parent.g_currentTransitionType == 'panUp')   _root.PlaySound(FESFX_TRANSITION_DOWN, 1, _root);
0x00FF29  if (_parent.g_currentTransitionType == 'panDown') _parent.g_mcCurrentClip.play();
0x00FF4B  _parent.g_nTransitionIndex = (_parent.g_currentTransitionType == 'swap') ? 1.1 : 0;
0x00FF6E  _parent.wait.gotoAndStop(1, 1);
0x00FF77  _parent.play();
```

This is the code that (re)seeds `g_nTransitionIndex` to `0` (or `1.1` for an instant `'swap'`) and
kicks the main timeline off its `Stop`, but only once `g_nReadyCode` reaches `2`. Nothing in this
block increments `g_nReadyCode` — it is only ever zeroed, in `transitionStart` and again in
`transitionDone` — so whatever bumps it to `2` is native/host-side (a "next movie finished loading"
signal), outside this movie's own bytecode. **INFERRED, not confirmed from this file alone.**

### Final positions when a transition completes (CONFIRMED math, given finite `g_iSize`)

`easeInOut(0) = 0` and, in the limit `g_nTransitionIndex → 1`, `easeInOut(1) = 1`, so:

- **panDown**: `g_mcCurrentClip._y → oldY1 + changeY = -480 - g_iSize` (off the top),
  `clip2._y → oldY2 + changeY = -g_iSize`, **`g_mcNextClip._y → oldY3 + changeY = 0`** (lands
  exactly on-screen), `overlay._y → oldY3 + changeY = 0` (same target as `g_mcNextClip`).
- **panUp**: `g_mcCurrentClip._y → g_iSize + 480` (off the bottom), `clip2._y → 480`,
  **`g_mcNextClip._y → 0`** (lands exactly on-screen), `overlay._y → oldY1 + changeY = g_iSize + 480`
  (tracks `g_mcCurrentClip`, not `g_mcNextClip`, in this direction — see the `dopanUp` note above).
- **swap**/**doorZoomIn**/**doorZoomOut**: `_y`/`_x` were already forced to `0` by `setupMovie()`;
  the `do<Type>` function for these either does nothing (`doswap`) or just calls `.play()` on the
  already-loaded clip (`dodoorZoomIn`) — any visual zoom/door effect is baked into that clip's own
  authored timeline, not computed here.

`transitionDone()` (`0xBB2C`, body 181 bytes) then, CONFIRMED: plays or fades `overlay`
(`overlay.play()`, or `overlay.gotoAndPlay(36)` if `g_bFadingHeader`); resets
`g_mcNextClip._x/_y = 0` again; swaps `g_mcCurrentClip`↔`g_mcNextClip`; calls
`g_mcCurrentClip.swapDepths(g_mcNextClip)`; **if `_root.g_bInShop != 1`, unloads `clip2` and the
(now old) `g_mcNextClip` back to `'trans/empty2.swf'`** (`clip2.loadMovie('trans/empty2.swf')`,
`g_mcNextClip.loadMovie('trans/empty2.swf')`) — this is the step that normally clears the white
slab off-screen; restores `g_focusTarget`; zeroes `g_nReadyCode`; re-enables the controller
(`ActiveController(1,1)`); plays the new `g_mcCurrentClip`.

---

## (c) Failure modes that would leave a clip covering the screen — mostly INFERRED, built on the confirmed math above

**The transition-done gate is a strict numeric comparison with no fallback.** Frame 3/4's own gate
is `if (g_nTransitionIndex > 1) { transitionDone(); ... } else { ...continue... }`. In IEEE-754,
`NaN > 1` is `false`, so **a `NaN` `g_nTransitionIndex` makes the "else" (keep animating) branch
win forever** — the loop never calls `transitionDone()`, so `clip2` (and the swap of
`g_mcCurrentClip`/`g_mcNextClip`) never happens, and `clip2.loadMovie('trans/empty2.swf')` (the
step that normally clears the white slab) is never reached. CONFIRMED as the literal comparison in
the bytecode; that `g_nTransitionIndex` actually becomes `NaN` at runtime is INFERRED, not observed.

**How `g_nTransitionIndex` could become `NaN` or never advance, chained from confirmed facts:**
- `g_nTransitionIndex` is written in exactly two places in this block: the `0xFEF4` per-frame gate
  (`= 0` or `= 1.1`) and frame 3/4's `+= g_nTransitionIncrement`. It is never explicitly initialized
  anywhere else in `feloader.apt`. If the very first transition's frame-3/4 code runs before the
  `0xFEF4` gate has ever fired (i.e. before `g_nReadyCode` first reaches `2`), `g_nTransitionIndex`
  is read as `undefined`; `undefined > 1` is `false` (same "keep going" branch), and
  `undefined + g_nTransitionIncrement` evaluates to `NaN` in ActionScript's numeric-coercion rules
  — from the second time through the loop onward, `g_nTransitionIndex` is `NaN` for good, and the
  loop above runs forever. **INFERRED chain — I did not observe an actual `undefined` read at
  runtime, only that nothing else in this file initializes the variable.**
- `g_iSize` comes from `ToInteger(g_oParamList.getValue())` inside `transitionStart`, fed by
  `getScreenInfo(sNextMovie)`'s parsed string. `getScreenInfo` **confirmed returns `''`** if
  `sNextMovie` isn't found in `g_aScreens` at all. Parsing an empty string through
  `g_oParamList`'s token splitter would plausibly leave `g_iSize` (and `iTypeIndex`, hence
  `g_currentTransitionType = g_TransitionTable[iTypeIndex]`, an array index) as `NaN`/`undefined`
  rather than a number. If `g_iSize` is `NaN`, `setupMovie()`'s `panDown`/`panUp` arithmetic
  (`changeY`, `oldY1..3`, all built from `g_iSize`) becomes `NaN` throughout, and — importantly —
  the algebraic cancellations that make `g_mcNextClip._y` land exactly on `0` (shown in (b)) do
  **not** save you: `NaN - NaN` is still `NaN`, not `0`, in floating point. Every clip's `_y` this
  frame becomes `NaN`. **INFERRED** — I did not confirm `sNextMovie` actually misses `g_aScreens`
  at runtime, only that the code path exists and is unguarded.
- `g_currentTransitionType` itself has no default case in `setupMovie()`'s switch (CONFIRMED,
  above): if it is ever a value other than the five known literals (including `undefined`,
  e.g. from an out-of-range `g_TransitionTable[iTypeIndex]` lookup), `setupMovie()` silently does
  nothing — `clip2`/`g_mcNextClip` keep whatever position a *previous* transition (or the host
  movie's initial `PlaceObject` matrix) left them at — and frame 3/4's
  `window["do" + g_currentTransitionType]()` would try to call a function literally named
  `"doundefined"`, which does not exist in this movie. What an undefined global function call does
  in this APT VM is outside what `feloader.apt` alone can answer.

**Given the reported symptom** ("a static, mostly white image with one rotated corner: large
rotated/oversized quads whose transforms put them far off-screen") **and that this bytecode never
writes `_rotation`/`_xscale` at all**, the rotated-corner part is most consistent with `clip2`'s
*inherited* `PlaceObject` matrix (on whichever movie places `clip2` as a child — not `feloader.apt`
itself) never having been reset, combined with a `_y`/`_x` that this code left at `NaN` or at a
stale/never-updated value (from the `setupMovie()` no-default-case path, or the
never-reaches-`transitionDone()` loop above) so `clip2`'s 640×2000 slab is still on-stage, still at
its original — off-screen-by-design, until animated — authored position, or at a `NaN` position the
port's transform pipeline doesn't special-case the way the original renderer may have.
**All of this paragraph is INFERRED** — plausible and mechanically consistent with the confirmed
bytecode, not observed at runtime; confirming it needs a runtime read of `g_nTransitionIndex`,
`g_iSize`, and `g_currentTransitionType` (or `clip2`'s matrix) during boot, which this offline
disassembly cannot provide.

**Built-in/native dependencies this system relies on (CONFIRMED, by opcode/string):**
- `Math.sin` (`CallMethod` on `Math`, inside `easeInOut`) and `Math.easeInOut` itself, defined in
  this same block and consumed only by `dopanDown`/`dopanUp`.
- `FuncCall` — a native `FuncCall` string bridge (`EA_CallNamedMethod name='FuncCall'` on `_root`),
  used once in `transitionStart` to fetch `Control.GetScreenInfo(<movie>)` when `_root.g_inGame != 1`
  (i.e. on the front-end path this bug report is about).
- `PlaySound` (`_root.PlaySound(FESFX_TRANSITION_DOWN, 1, _root)`), `ActiveController`,
  `SyncGlobalData` — all native/global function calls (`EA_CallNamedFuncPop`/`EA_CallNamedMethodPop`),
  not defined inside this block.
- `getTimer` is **not** used anywhere in this system (CONFIRMED negative) — timing is frame-count
  based, tied to the movie's `ms_per_frame`, not wall-clock.

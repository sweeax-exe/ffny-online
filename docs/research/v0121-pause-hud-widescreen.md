# v0.12.1: widescreen fight HUD while paused (#95)

Reproduced on macOS (M-series, headless, 16:9 via `RECOMP_WS_ASPECT=1.7778`,
full-canvas captures with `RECOMP_TRANS_SHOT_WIDE=1`, no texture packs). The
artwork packs are not involved: the same shift happens with none installed.
Half a second after Start opens the pause menu, the health/Blazin bars, names
and scores move from the screen edges into the centred 4:3 box. They return
to the edges after resume.

## Cause

The renderer anchors 2D batches in the HUD strips (top `y1 <= 130`, bottom
`y0 >= 350` with 3-4 fighters, wholly left of 300 or right of 340) only while
the game's HUD heartbeat is recent: `nv2a_ws_hud_heartbeat` within the last 30
flips (`ws_place_batch`, toolkit `nv2a_pb_exec.c`). The only caller was the
entry hook of the fight's simulation step `sub_00087700`.

A read-only call-count probe over a 1v1 with three pauses shows what runs
while the pause menu is open. The Battle fight tick `sub_00079B60` keeps
running at about 60 calls a second. It skips the match update `sub_0008BE60`,
the step and the HUD object update `sub_000B5710`, which all stop together.
So the heartbeat lapses, and every HUD batch falls back to the centred class
while the HUD stays on screen. The tick's pause condition is the pause
object at `this+0x30` (byte `+0x206`).

Match state `+0x2D820` does not change on pause; it stays 5. State 7 is a
separate non-simulating state (seen during a special-move camera), not the
pause. A first candidate keyed on state 7 had no effect on a real pause.

## Correction (game code, `src/recomp_manual.c`)

- The step hook marks the HUD live and sends the heartbeat, as before.
- The match update's entry hook clears the mark in any state other than 5
  and 8 (the two that run the step): intro, special-move camera, match end.
- The fight tick's entry hook re-sends the heartbeat while the mark is set.
  The mark is still set after a pause from a live fight, so the HUD keeps its
  edge anchoring. `sub_0007A8F0` is hooked the same way: it is the other
  fight-mode tick built like `sub_00079B60` (same match update and HUD
  calls). No route reaches it, so it is not exercised here.

No toolkit change; the pin stays at `9a30c25`.

The pause panel cannot be pulled apart by the longer heartbeat. A batch dump
of a paused four-fighter frame (`RECOMP_PB_BATCH_DUMP=shot`) puts every panel
piece between y 95 and 385. None ends above 130 or starts below 350, and the
background and title straddle the middle. The dimmer is a full-screen fill
and stays stretched. At 4:3 (`k = 1`) the classifier returns before any
placement, so 4:3 output cannot change.

## Evidence (local, ignored logs)

| Run | Log | Result |
|---|---|---|
| Baseline FFA, pause 55 s | `run-20261025-181212.log.err` | paused HUD centred (reproduced) |
| Probe 1v1, three pauses | `run-20261025-183904.log.err` | tick runs; match update, step and HUD update stop |
| Fix 1v1, three pauses | `run-20261025-184346.log.err` | six paused captures, HUD at edges; panel centred |
| Fix FFA, three pauses | `run-20261025-184617.log.err` | pauses at 00:01 and 00:08 keep all four corners at the edges; intro pause has no HUD either way |

Each run: no crashes, watchdogs, truncated batches or unresolved calls; median
120 presents per two seconds; disposable save restored identically.
Captures stay in `logs/shots/`.

Not covered: Windows D3D11 (the change is game code, shared by every backend),
16:10 and the ultrawide "centred" HUD (same heartbeat, different reach), and
the pause menu's help page.

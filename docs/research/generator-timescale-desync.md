# Desyncs that name "the shared generator and time scale": what they are, who feeds them, how to stop it

2026-10-25. Read-only study (nothing edited, built or run). Sources: `src/hooks/netplay.c`, `src/hooks/rollback.c`,
`src/hooks/test_telemetry.c`, `src/recomp_manual.c`, the lifted C in `src/recomp/gen` (direct-call walks with
`scripts/callers.py`, plus a small script that lists every call made with `ecx = 0x39C0E0`), the XBE data for two
vtables, the bug reports in `logs/bug-reports/`, and the two-copy lockstep logs
`run-20261025-173501.log.err` / `run-20261025-173504.log.err` (kept in the worktree `.claude/worktrees/agent-a9371189462dbced9/logs/`).
Earlier notes this builds on: `fight-determinism.md`, `rollback-step-structure.md`, `rollback-state.md`,
`crowd-lockstep-reset.md`, `simulation-tick.md`.

Tags: **[V]** read in code or log; **[I]** inferred (reason given); **[U]** not settled (how to settle it is given).
Addresses are guest addresses unless marked "host". Indirect calls (vtables) are not followed by the walks, so
"no caller" never proves absence.

## 0. Short answer

1. The "detail" line is a hash of two unrelated things folded into one word: the 0x88 bytes of the shared object-form
   generator at `0x39C0E0` (32-float table, state `0x39C160`, odd term `0x39C164`) and the float at `CLogic+4`
   (`0x349764`). The log cannot say which of the two differs. The "CPU fighters' generators equal" half is meaningless
   in a match without CPU fighters (two players): those four generators are seeded identically and never drawn. [V]
2. The shared generator is **not** only a sim generator. It is drawn by the camera director inside the fight step (fine),
   but also by the music/ambience director, the screen-transition machine, a render-path effect, the crowd's animation
   creation and the venue object `CArena`, all of which run outside `sub_00087700` at counts that depend on the audio
   state, the render rate, loading or the previous match. [V for the call sites, I for which of them varies]
3. **Terror Dome has a continuous drawer.** In the two-copy lab run the shared generator changed 23 to 24 times in the first
   1200 steps of both Terror Dome matches and 1 to 2 times in the Foundation and in each of The Pit's two matches. [V] So on that venue the
   generator sits in the open, and any host-timed disturbance of the drawer's phase offsets it for good.
4. Lab case (second match of the session, both PCs lockstep): the generator was already **three draws apart at step 1**
   (same sequence, one PC ahead), before any fight step; the fighters' positions differ by step 31 and the first checksum
   (step 60) fails. The other four matches (Foundation, The Pit twice, and the second Terror Dome match) stayed in step. [V]
5. Player case (build 26, mixed: rollback here, lockstep there; input delay 10 proves one side had rollback off): the generator
   and the *remote* fighter differ for the first time at step 1020, after 16 equal checksums and about 270 rollbacks. That is a
   different timing from the lab case (the generator was in step for 1000 steps), and it fits **re-runs that draw a
   different number of times than the first run** (section 6). [I]
6. The `sub_000EDF90` crash at step 4039 is **not a consequence of the desync** and is not in generator code. It is a
   walk down a fighter model's node tree (the fighter animation update) whose child array has been reused: the known "restored
   `.data` points into a heap that moved on" class (`rollback-state.md` section 5). [V stack, I class]
7. Cheapest robust fix: **re-seed the shared generator at the start of every fight step** from `(session, match, step)` through
   the game's own seed routine (`sub_000AA0D0`, `ecx = 0x39C0E0`), the way `crowd_reset` already does for the crowd. Every
   out-of-step draw is then wiped before it can reach the sim. Plus: split the hash part, log the time scale per step, and do not
   run rollback unless every PC in the room does (section 8).

## 1. The reports

Search of all `logs/bug-reports/*/log.txt` for "desync in match" (five reports) and the two lab logs. "Detail" exists only from build 23.

| Report | Build | Venue (feID) | Type / players | Step | Rollback | Detail line |
|---|---|---|---|---|---|---|
| `20261023-003102.794-eef32e` | 20 | Gun Hill Garage (17) | 2 (three-way), 3 | 300 | on this PC (147 rollbacks by step 325, then a crash "predicting") | none yet |
| `20261023-051501.950-531df5` | 21 | 7th Heaven Intense / Red Room (9) | 3, 2 | 3600 | off (no rollback lines) | none yet |
| `20261023-202700.112-e535d7` | 22 | 125th Street Station (11) | 3, 2 | 60 | off ("rollback 0" in the later give-up line) | none yet |
| `20261025-005751.228-d9236f` | 24 | 7th Heaven Intense / Red Room (9) | 0, 2 | 3780 | off (no rollback lines) | fighters 1 and 2 differ; CPU generators equal; **shared generator and time scale equal** |
| `20261025-175421.792-048f9e` | 26 | **Terror Dome (6)** | 0, 2 | 1020 | **mixed**: on here, delay 10 (no rollback discount) so the other PC was off | **fighter 1 only; CPU generators equal; shared generator and time scale differ** |
| lab `173501` / `173504` | current | **Terror Dome (6)**, 2nd match of the session after a Foundation match | 0, 2 | 60 | off on both (delay 2) | fighters 1 and 2 differ; shared generator and time scale differ |

What the table says: only two of the six name the shared generator, and both are Terror Dome. The 3780 case (Red Room) has the
generator and time scale **equal** and both fighters different, so it is another cause (fighter-side), not this one. The three old
reports predate the detail and cannot be classified. Checksums are taken every `HASH_EVERY` = 60 steps, so "step N" is the first
checkpoint after the parting, not the parting itself. Venue names: `online-match-setup.md` section on venues.

## 2. What is hashed and what "detail" means (question 1, first half)

`fighter_hash` (netplay.c, "the game's hooks") is computed at the entry of `sub_00087700` for the step about to run, and returns
the whole-state word plus six parts:

| Part | Content |
|---|---|
| 1 to 4 | per fighter slot: the six words at record offsets +0x30 (state), +0x44/+0x48/+0x4C (position), +0x54 (yaw), +0x350 (health) |
| 5 (`mine[4]`) | the four AI generators, 4 x 0x88 bytes from `0x3BFC70` |
| 6 (`mine[5]`) | the shared generator, 0x88 bytes from `0x39C0E0`, **folded with** the float at `0x349764` |

`desync_detail` prints "differ" for part 6 if either the generator block or the time scale differs. Consequences [V]:

* The time scale is not shown to differ by anything we have: every telemetry sample of the lab run prints `time_scale` as 1
  (`%.4g`), so in the lab case the generator is the thing that differs. In the player case it is unknown which.
* Only 0x88 bytes of the 0x39C0E0 object are hashed. The camera director's own fields (`0x39C168..0x39C1D8`) are not.
* Part 5 is hashed even with no CPU fighter, so "equal" there says nothing.

The generator's layout and semantics: `fight-determinism.md` Q1 (a draw advances the state word twice and replaces one table
float; `sub_000AA2B0` draw, `sub_000AA300` chance, `sub_000AA340` integer 0..n-1, `sub_000AA0D0` seed).

Seeding: `sub_0008BB00` (match setup, called from the arena frame `sub_0008BE60` state 15 and at the start of a match) calls
`sub_000AA0D0` with `ecx = 0x39C0E0` and `CLogic+0x2D874/78`. `netplay_on_seed` (hooked on `sub_000AA0D0_enter`) replaces both
words with `mix32(session, ecx, per-key counter)`; the counters are cleared at the sync point (`netplay_sync_point`), so on both PCs
the first seeding of `0x39C0E0` in a START gets the same words [V]. The lab confirms it: the sequences of generator states on the two
PCs are identical, only shifted (section 4).

## 3. Who draws the shared generator, who writes the time scale (question 1, second half)

All sites that call the draw routines with `ecx = 0x39C0E0` (68 call sites, the draw sites below; the other sites are getters and
setters of the director's fields at `0x39C168..0x39C1D8`, `sub_000A97xx..sub_000AA07x`, which do not draw). The "return address"
column is what `RECOMP_TEST_DRAWS=1` prints as `caller` (`sub_000AA340_enter` records `MEM32(g_esp)`).

| Function (return address of the draw) | What it is | Runs | Can depend on |
|---|---|---|---|
| `sub_000AACA0` (0xAADB2), `sub_000ABB70` (0xABCAA, 0xABCF4, 0xABE76, 0xAC02E), reached from `sub_000AC590` (director, last call of `sub_00087700`), `sub_000AC1F0`, `sub_000AC100`, `sub_0008AB00` | camera director: random camera moves, shake | in the step | fighter state only [V]; deterministic |
| `sub_00036DF0` (0x36EB6) | **music/ambience director tick.** A timer at `0x34036C` grows by the update's dt; at the threshold `0x34035C` it asks the audio engine whether voice slot 0 is active (`sub_00033A10` -> `sub_0001CB40` -> `sub_00120110`, a query of the live engine). If the slot is idle it draws a new random delay from `0x39C0E0`, draws a variant from the **global** generator `0x33DB5C`, picks a cue from the four fighters' state flags and starts it (`sub_00137390`). If active it only pushes the timer back by 5.0 | every screen update (`sub_0003D960` <- `sub_0003D560`, from `sub_00079B60` and the other screen updates); gates: bytes `0x340540`, `0x33EA14`, `0x340348` must be 0, `0x33EBC0` and `0x33EC34` non-zero [V code, U whether they hold in a fight] | **the audio engine's voice state** (host-timed, not rewound by rollback, queries are not muted in re-runs), the update count of the loading window |
| `sub_0003E170` (0x3E45B) | the same director's match-start init: resets the timer from `0x340718`, draws the first threshold, opens the AEMS stream (the `[MUSIC] ... from 0003E1CF` line). Only caller `sub_0008BB00` | once per match setup | deterministic (called right after the seed) |
| `sub_000D90F0` (0xD913B) and `sub_000D8B90` (0xD8BCF, 0xD8BEF, 0xD8C0E) | the screen-overlay / transition state machine at `0x3A0180` (countdown `+0x84`, state `+0x80`, global counter `0x3A0184`) | every screen update (`sub_00079B60`, `sub_0007A8F0`, ...) | transitions; its object lives across matches and is not obviously reset at match setup [U] |
| `sub_000D8C40` (0xD8D08) | screen effect that draws a random value when object flags `+0x4EC` and byte `+0x98` are set | **in the render chain**: callers `sub_000796A0` (the screen's draw), `sub_00077B20`, `sub_00079E40`, `sub_0007B3B0` | **presented frames**, not updates; re-runs do not render [V code, U whether it is active in a fight] |
| `sub_00084640` (0x84741) | animation creation: picks a random start frame | via `sub_00084AC0` <- `sub_00084B20` <- `sub_0007E170` (crowd member setup at load) and `sub_00080700` (<- `sub_0007C2F0`, `sub_00081240`) | load and spectator events; the spectators' animation banks load lazily in the first seconds of a fight (`rollback-state.md`, "dynamic load ... caller 0010B423" lines 109 to 116 updates in) |
| `sub_000937E0` (0x93847), `sub_000938E0` (0x9398D, 0x939A5, 0x939C6), `sub_00093760` (0x9378E) | `CArena` per-update effect spawns (they call `sub_00091860`, the `fx_` particle spawner), time-scaled by `sub_00044C20` | per update, from `sub_00094B80`, which is slot 2 of the `CArena` vtable at `0x29EB94` (slot 1 `sub_00095A80` loads `V2BG%s`, stage objects, weapons, ropes, guests); reached indirectly from the scene dispatcher | step count if it runs once per update; its order relative to `sub_00087700` inside one update is [U] |
| `sub_00093EF0` (0x941EB), `sub_00094260` (0x946EB, 0x946F8) | `CArena` load: default lighting, hidden/venue weapon placement picks | match setup, inside `sub_0008BB00` after the seed | deterministic given the venue XML |

The match generator `0x3C1404` and the four AI generators `0x3BFC70` are not drawn by any of these; the AI ones are drawn only by the
AI decision code in the step and the match generator by fight handlers (`fight-determinism.md` Q3).

The global generator `0x33DB5C` has 62 direct callers, all in `0x20000..0xCF000` (UI, audio, effects, the crowd); none in the fighter
code (`0x1A0000+`) [V], so it cannot move a fighter directly; netplay already gives the crowd a private copy of it.

**Time scale `CLogic+4` (`0x349764`).** One writer, `sub_00044C10`, called from `sub_0007C2F0` (0 when paused, else 1.0, from the scene
dispatcher `sub_0002DFF0` every update), `sub_0008D5C0` (1.0), and `sub_000AC1F0` (the camera director's slow motion; in the step,
so it follows the director's random choices). `sub_00087700` passes it to `sub_001A4900` which turns it into the fighters' `0x3B8F2C`.
Two things can make it differ between PCs without any draw: a pause (START or a "Controller Disconnected" prompt on one PC;
`fight_input` hides START and keeps every port plugged in, so this should not happen online) and a different director choice.
Neither is shown in any log. [V code, U event]

**Do fighters read any of this?** Not directly: no fighter-side function appears among the draw sites or reads `0x39C0E0..0x39C1D8` [V],
and `rollback-step-structure.md` 2.11 says the same. The coupling is indirect: the director picks camera moves and slow motion (time scale
and the fighters' step), and `CArena` spawns effects and weapons that fighters then use. Why the lab fighters part by step 31 is therefore
[U]; the evidence for the link is only that every match whose generator was in step at step 1 stayed in step (section 4).

**Leftovers of the previous match.** Re-armed at match setup: the music director (`sub_0003E170`), the generator seed, the time scale
(1.0 at match start). Not obviously re-armed: the transition object at `0x3A0180`, and above all the **audio engine's voice state**, which
at the start of match 2 still reflects the previous match's results-screen music tail. That is a plausible reason the lab failure was the
second match of the session [I]. (The player's was the first match of the session; there is no leftover there, which is why it is
explained differently in section 6.)

## 4. What the lab run shows

Both copies ran the same scripted five-match session (Foundation, Terror Dome, The Pit, Terror Dome, The Pit; lockstep, delay 2). The
`[TEST-STATE]` samples every 30 steps carry `rng_util` (the state word `0x39C160`) and `time_scale`.

| Match (harness count) | Venue | `rng_util` at step 1, PC A / PC B | Draw rate, first 1200 steps | Result |
|---|---|---|---|---|
| 1 | Foundation | 2046350824 / 2046350824 | 1 state change | in step |
| **2** | **Terror Dome** | **1104532946 / 3164005944** | **24 changes** | **desync at step 60** |
| 3 | The Pit | 3408947150 / 3408947150 | 2 changes | in step |
| 4 | Terror Dome | 2021274756 / 2021274756 | 23 changes | in step |
| 5 | The Pit | 905976154 / 905976154 | 1 change | in step |

* In match 2 both PCs walk the **same sequence of states** (1104532946, 424483152, 518071582, 1751485628, 2364266410, ...). PC B reaches
  the state PC A holds at step 1 at step 181 to 211; in general every state is reached about 180 to 210 steps later on B: **a constant
  offset of three draws**, set before step 1 and never closed. [V]
* The offset is not in how the generator is seeded (identical sequences) and not in the number of draws after step 1 (24 changes each in
  the same period). It is in the number of draws between the seed in `sub_0008BB00` and the first step: the loading window. [V]
* The crowd is not the carrier here: the Terror Dome crowd has **0 records** (`crowd hash ... (0 records)`) on both PCs and the crowd hash at
  steps 1, 2 and 60 is equal. [V] The Foundation has 26 records.
* The fighters differ already at step 31 (slot 0: x 1.799 / 1.802, facing 118.0 / 120.8, both in state 69) while the same venue later (match 4)
  gives identical values on both PCs. [V]
* The cadence of one draw per roughly 50 to 60 steps in Terror Dome (consecutive changes 30 to 90 steps apart) looks like a timer, not
  like an event. A pure function of the step count would give the same phase on both PCs, so the phase is set by something host-timed in
  the window. [I]

Settling which drawer it is takes one run (section 9): the draw counters per calling site at step 1, and the game's own screen-update
count at step 1 (`0x340538`), already in the `[TEST-DRAWS]` report.

## 5. Why Terror Dome appears in both (question 2)

* No Terror-Dome-specific game code was found: the fight compares the venue word `0x3B9084` with 4, 5, 7, 0x11 and 0x13 only
  (`team-and-special-match-types.md`); Terror Dome is match type 21 and none of those. [V]
* What the data does show is that Terror Dome is the venue where the shared generator is **busy** (23 to 24 draws in 1200 steps against 1 to
  2 elsewhere). [V] A generator that is drawn about once a second by something outside the step is exposed to every timing difference; one
  that is almost idle is not. The same disturbance (a different number of idle voice checks, screen updates or presented frames in the
  loading window) is harmless in the Foundation and The Pit.
* Leading candidate for the drawer: the music/ambience director (`sub_00036DF0`). It draws exactly when the voice slot is idle and
  re-arms with a random delay, which fits a once-a-second cadence on a venue whose cue ends quickly, and it explains why the phase
  depends on wall-clock audio events and the previous match's music tail. Alternatives: `CArena`'s effect spawner (`sub_000938E0`, Terror
  Dome's own props), the transition machine (`sub_000D90F0`), the render-path effect (`sub_000D8C40`). [I; the discriminating run is in
  section 9]
* The first (player) case also has "0 records" in the crowd hash, so there too the crowd is not the cause. [V]
* Sample size: two Terror Dome failures, two earlier Terror Dome matches that passed (lab match 4; the player's other matches are not in
  this report). Do not read "Terror Dome only" into it: other venues fail with fighter-only parts (Red Room, step 3780).

## 6. Rollback on one PC, lockstep on the other (question 3)

`rollback_to` re-runs a step the way the main loop does: `sub_000DD930` (input), `sub_00077040` (the screen manager: the **whole screen
update**), and, if it continues, `sub_000BD860`, `sub_000BDF40`, `sub_0001A240`. Only the sound engine's command call (`sub_00137010`),
ten audio message-bus handlers and the stream stop are muted (`N.resim`). The render (`sub_00077740`) is not run. [V]

So the order and number of generator calls change in these ways:

1. **Everything per-update runs again, unmuted**, from the restored generator state: the music director tick (`sub_00036DF0` through
   `sub_0003D960`), the transition machine, `CArena`'s update, the crowd update. These draws are reproduced only if their *decisions* are.
   The music director's decision is a query of the live audio engine, which a rollback neither rewinds nor, in a re-run, drives with the
   commands the first run issued. The first run may have seen "voice active" (no draw, timer back by 5.0) and the re-run "idle" (a draw
   and a new cue, whose start is then muted) or the reverse. The lockstep PC runs each update once and never asks again. [I; the query
   is [V], the effect is the mechanism]
2. **Draws outside the update are lost on the rollback PC and kept on the other.** The snapshot for step f holds the generator as it was at
   the start of that update; the draws a render-path site such as `sub_000D8C40` made since are overwritten by the load and not redone,
   because re-runs do not render. A PC that never rolls back keeps them. If that site is active in a fight, rollback vs lockstep parts
   steadily, and rollback vs rollback parts as well (different rollbacks lose different draws). [I; whether the site is active is U]
3. `N.seeds`, the crowd's private generator copy and the sim state are in the snapshot, so seed counters and crowd draws stay equal. [V]
4. Fighter inputs on the rollback PC are predicted and corrected; every draw made *inside* a step follows from the state, so it is replayed
   identically after a correction. [V by design, no counter-example]

This matches the player report: 16 checkpoints equal through about 270 rollbacks, then the generator and the remote fighter differ at the
same checkpoint. It does not match the lab pattern (parted at step 1). Two different mechanisms behind one detail line.

Also note: the line says "fighter 1" only. Fighter 1 is the *remote* player from the rollback PC's side. A fighter that differs in the same
60-step window as the generator suggests the generator's draw changed something the fighter then did (a weapon or effect appearing, a
camera move changing the time scale), or that a re-run failed to restore both. Which came first cannot be told from one checkpoint (section 8, P0).

## 7. The crash in `sub_000EDF90` (question 4)

Log: `[ROLLBACK] at the crash: running steps again, step 4039, final 4038, update 4037, 1235 rollbacks, the last back to 4039 from 4044,
... predicting`, then an access violation at host `RIP 0x1406ABEE5`, "in sub_000EDF90+0x215" (a **host** offset into the compiled function, not a
guest offset), fault address 0xCE6669B4 = guest 0xBE6669B4 read as a pointer. [V]

* `sub_000EDF90` is 100 bytes at guest 0xEDF90, slot 6 (+0x18) of the vtable at `0x2B5850`. It is a **container node**: `this+0x10` is an
  array of child pointers, `this+0xC` points to a descriptor whose 16-bit count sits at +8; if the array is null it calls `sub_000EE400`; then
  it walks the children from last to first and calls each child's slot 6 (the same method, so it recurses), OR-ing the results. [V]
* At the fault `this` = 0x8012C008 (heap), the child index is 0, and child [0] is 0xBE6669B4, which as a float is -0.225: **the array pointer
  now points into memory that holds floats**. The previous iteration's child had the vtable 0x2B5850 (that is the `edx` in the dump). [V]
* The guest stack scan matches the fighter animation update chain `sub_001A8480` -> `sub_001A76A0` -> `sub_000446C0` -> `sub_0009EF00` ->
  `sub_0009ECB0` -> ... with words inside `sub_00021F40` and `sub_000243B0` (0x22247, 0x222C3, 0x24063). `sub_00021F40` is the lazy
  `CAnimation::InitMirror` allocation that `rollback-step-structure.md` 2.7 lists as a heap allocation made inside a step. [V words, I that
  the node belongs to a mirrored animation]
* The signature (vtable 0x2B5850, then a float as pointer) is exactly the one recorded in `rollback-state.md` section 5 for a restore that
  left `.data` pointing into a heap that had moved on. [V]
* It is **not a consequence of the desync**: nothing branches on `N.desync` except the result reports (`netplay_on_result`, `report_left`) and
  the toast; the crash is 3000 steps and about 800 rollbacks after the first checkpoint failure; and generator code is not on the stack. The
  desync does not change what rollback does. [V]
* What is likely: a heap block allocated in a step (the mirror structure, or a model/animation-bank load of the kind logged as "a dynamic load in the
  fight" at update 109 to 116) that a later load put back without or with the wrong heap pages, so the node's array pointer is stale.
  Which of the "left alone" pages or "kept allocated" frees it was is [U]. To settle it: extend `rollback_crash_state` to print `this`
  (edi), its vtable, `this+0xC`, `this+0x10` and `rollback_page_state()` of each, and whether the allocation happened inside a re-run.

## 8. Fixes (question 5)

Ordered by value for risk. P0 and P1 are implemented (2026-10-25, section 11); P3 is being done separately.

| # | Change | Where | Effect | Risk |
|---|---|---|---|---|
| P0 | **Split the hash part and log the time scale.** Part 6 becomes the shared generator alone; add parts for the time scale and the director's fields (`CLogic+4`, `CLogic+8`, `0x3B8F2C`, `0x3B9054`, `0x39C168..0x39C1D8`) and the match generator `0x3C1400..0x3C1488` (not hashed at all today). In `desync_detail` print "no CPU fighters" instead of "equal" for the AI part. Optional `RECOMP_NETPLAY_HASH_EVERY` (default 60) for the lab, and a ring of the last 120 per-step (generator state word, time scale) pairs dumped at the desync. | `HASH_PARTS`, `fighter_hash`, `desync_detail`, `P_HASH` payload (netplay.c) | tells which of generator or time scale differs and which differs first; no behaviour change | The packet grows by 12 to 16 bytes; online is same-build-only, so no compatibility shim. Bump `LOCKSTEP_VERSION` anyway if it is the guard. |
| P1 | **Pin the shared generator to the step.** At every step start (right after `s = ++N.step` in `netplay_on_step`, before `fighter_hash`), call the game's seed routine `sub_000AA0D0` with `ecx = 0x39C0E0` and two words `mix32(session, match, step)` (second word odd), using `guest_call` and the register/x87 save that `crowd_reset` uses, with `N.seed_bypass = 1` so `netplay_on_seed` leaves the words alone. Same call from the re-run loop in `rollback_to` (the loop calls `sub_00077040`, which reaches `sub_00087700_enter` again, so the hook already runs; check it does not early-return on `N.resim` before the call). | `netplay_on_step` | Every draw made outside the step (music director, transition, render-path, crowd/animation creation, `CArena`, the loading window) is erased before the next step, so the sim sees a state that is a pure function of `(session, match, step)`. Removes both the lab offset (step 1 re-seed) and the mixed-rollback drift, if the shared generator is the cause. | (a) Consumers that run **before** `sub_00087700` in the same update and decide something sim-relevant from the generator (`CArena::Update` if it runs first [U]) still see leftovers: add the same re-seed at the update start (`netplay_on_update` for live updates, the head of the `rollback_to` loop for re-runs, key `N.step + 1` in both). (b) Visual effects that draw from it now start every step from a fresh seed: no visible repetition because the seed changes each step. (c) Cost: one seed routine (about 35 LCG steps) per step. (d) The hash then cannot see draw-count drift of this generator; that is the point, and part 6 still catches a real time-scale or director difference. (e) Does nothing for causes outside the generator (time scale, weapon pool, the 3780 Red Room case). |
| P2 | Fix the cause as well, once section 9 names it. If it is the music director: replace `sub_00036DF0` outright in `src/recomp_manual.c` (it has a direct caller, so the `_gen` wrapper pattern would be bypassed; a hand copy of the 447-byte body with the generator swapped for a private copy on entry and exit, as `netplay_crowd_update` does for `0x33DB5C`), or hold its timer in lockstep fights. If it is the render-path effect: gate the draw by `N.lockstep`. | `src/recomp_manual.c`, re-lift | removes the host-timed input at the source | Hand-written copy of a lifted function; needs a re-lift and the regression run. Only worth it if P1 leaves a gap. |
| P3 | **Do not mix rollback and lockstep in one room.** `N.peer_rb[]` already carries each PC's setting (P_RTT byte 7) and `agreed_delay` already uses "all roll back". At step 1, turn rollback on only when `rollback_wanted()` **and** every PC reports it; otherwise run lockstep on all. | `netplay_on_step` step 1 (`rb_on` line) | removes the asymmetric re-run class (section 6) for mixed rooms | Rollback users in mixed rooms lose prediction and get the larger delay; the toast should say why. Does not help two rollback PCs if a render-path draw is live (P1 does). |
| P4 | Time scale: only if P0 shows it differs. Then compare the three writers (pause, `sub_0008D5C0`, director) per step; candidates are a pause prompt or the director picking a slow-motion move after a different draw. | `sub_0007C2F0`, `sub_000AC1F0` | | |
| P5 | Crash diagnostics (section 7). If the stale array is confirmed, the rollback fix is in `rollback.c` (page left alone, or an allocation kept across a load), not here. | `rollback_crash_state` | | |

Order: P0 (so the next report is decisive) with P1 together, then P3 as the policy guard until P1 is confirmed in a mixed run.

## 9. How to settle the open points with one run each

1. **Which drawer, and is it the loading window?** Two-copy lockstep, Foundation then Terror Dome (the lab script), both with
   `RECOMP_TEST_DRAWS=1`. The report lines `[TEST-DRAWS]` list `(generator, caller, count)` at match setup and at steps 1, 300, 900; the
   generator is the decimal of 0x39C0E0 = 3784928. Compare the counts of the caller (return address) values in section 3: 0x36EB6 (music
   director), 0xD913B/0xD8BCF.. (transition), 0xD8D08 (render path), 0x84741 (animations), 0x9398D.. (`CArena`). The site whose count differs by
   3 at step 1 is the one. The last report line carries `screen_updates` (`0x340538`): if it is equal on both PCs the window had the same
   number of updates and the difference is audio/render-timed; if it differs by about 180 it is update-count-driven.
2. **Is the music director live in a fight?** `RECOMP_WATCH_EXEC=0x36DF0,0x33A10` on Terror Dome and on the Foundation: entry totals every five
   seconds. A hit is evidence, a miss is not (CLAUDE.md 4a rule 1); the `[TEST-DRAWS]` counter for 0x36EB6 is the better test.
3. **Does a draw in the window reach the fighters?** Run the lab pair with `RECOMP_NETPLAY`-style test input and `RECOMP_TEST_RNG_SEED`
   unset, once as is and once after P1; if the step-31 positions agree after P1 the generator was the carrier.
4. **Mixed case.** One copy with `RECOMP_ROLLBACK=on`, one without (this is the player's configuration), Terror Dome first match, the
   200-line `crowd hash`/hash log with P0. The split hash shows whether the generator, the time scale or a fighter moves first.

## 10. Not settled

* Whether the music director's gate flags (`0x340540`, `0x33EA14`, `0x340348`, `0x33EBC0`, `0x33EC34`) hold during a fight, and the meaning of
  voice slot 0. (`sub_0001CB40` indexes a table at `0x331D58`; default slot `0x331CF0`.)
* The position of `CArena::Update` (`0x94B80`, vtable `0x29EB94` slot 2) relative to `sub_00087700` inside one update; its caller is an
  indirect call from the scene dispatcher family (the `CScene` base constructor `sub_0007BE40` stores that vtable first).
* The mechanism by which three extra draws change two fighters' positions by step 31 in the lab (no fighter-side reader was found).
* Whether the time scale ever differed in any report.
* Which heap block the `sub_000EDF90` crash read, and why the load left it stale.

## 11. Measurements after P0 and P1 (2026-10-25)

What was built: P1 in `netplay_on_step` (`shared_gen_reseed`), P0 in `fighter_hash` / `desync_detail` (`HASH_PARTS` 8; merged into build 35 as three words appended to the P_HASH extras of `src/hooks/netplay_check.h` instead, part 5 unchanged, so the first 37 bytes stay what builds 23..34 read). The seed routine
was read in the lifted code before use: `sub_000AA0D0` (thiscall, `ret 8`) stores its two words at `this+0x80` (state) and `this+0x84`
(increment), fills the 32 floats at `this+0..0x7C` from them and writes nothing else; esi/edi are pushed and popped, the x87 stack is
balanced. The words are `mix32(match ^ mix32(step * 0x9E3779B9 ^ 0x39C0E0))` and `mix32(match ^ mix32(step * 0x85EBCA6B ^ 0x5EED39C1)) | 1`,
the same on every PC and for a step rollback runs again (the re-run reaches `sub_00087700_enter` with `N.step` rewound). Lockstep only:
offline play, the soak and the golden replay never reach it. `RECOMP_NETPLAY_GEN_PIN=off` (tests) turns it off.

Set-up: two copies of one build on one PC (CPUs 28 and 30), a local relay with a fixed venue sequence (a scratch build of
`netplay/relay` that answers every vote with the next venue of a list; not committed), `scripts/harness.py` routes `online5-p1/p2`
and `online-p1/p2`, `RECOMP_TEST_OBSERVATIONS=1`, `RECOMP_TEST_DRAWS=1`, accounts at 127.0.0.1:9. "rng_util" is the state word
`0x39C160` in the `[TEST-STATE]` sample, which `defjam_test_step` takes at the step's entry **before** the re-seed, so with P1 it still
shows what was drawn outside the step since the last one. A temporary build also tagged each draw of `0x39C0E0` made between the
update's start (`sub_0002C530`) and the step (`sub_00087700`) [not committed].

| Run | Pin | Venues (match order) | Result |
|---|---|---|---|
| repro1 | off | Foundation, Terror Dome | Terror Dome: generator **different at step 1** (4280241561 / 1156477769) and in all 134 of 134 samples to step 3991; fighter checksums equal to the KO (step 3420). Foundation: 0 of 468 samples differ. |
| fix1 | on | Foundation, Terror Dome, The Pit | 500 checksums equal, no desync (the session then stopped on a results screen, see below) |
| fix2 | on | Foundation, Terror Dome, The Pit, Terror Dome, 125th Street | **1140 checksums equal, no desync**; Terror Dome samples different before the re-seed: 17 of 557 and 3 of 488, each one alone (the next sample equal again) |
| rb1 | on, rollback on both, 60 ms (+0..10) lag | Foundation, Terror Dome | 680 checksums equal, no desync, no crash; 5839 and 7554 rollbacks (deepest 9 and 8 steps) |

What the draw counts say ([TEST-DRAWS], generator 3784928):

* **The loading-window drawer is `CArena`'s effect spawner** `sub_000937E0` (return address 0x93847), not the music director: in
  repro1's Terror Dome match it had drawn 9 times on one PC and 17 on the other by step 1, with 473 and 926 screen updates since the
  match set-up (`0x340538`). Every other site had equal counts at step 1 (music director 0x36EB6: 6 and 6). The offset is set by the
  number of updates the venue's load took on each PC, so it changes from run to run: in fix1 and fix2 the counts happened to agree
  (10 and 10) and step 1 was equal before the re-seed too.
* **Order inside an update** (section 10, first open point): in a fight `CArena`'s draws (0x93847; its other sites 0x9398D.. did not draw) all fall **after**
  the step; none came between the update's start and the step (the tagged count stayed at its loading-window value from step 1 to 900).
  The music director (0x36EB6) draws **before** the step (6 -> 7 between steps 300 and 900 on one PC only in repro1; 11 -> 12 in fix2).
  So the step-start re-seed is before every in-step reader and wipes the music director's host-timed draws; `CArena` then draws from
  the pinned state plus the step's own draws. No gap that P2 would have to close was seen.
* The isolated differences in fix2 are those music-director draws: one PC drew between two steps, the sample before the next step shows
  it, the re-seed removes it. Without the pin the same event leaves a permanent offset (repro1).

Not reproduced: the lab's fighter parting. In repro1 the generator was apart for the whole Terror Dome match and the fighters still
stayed equal to the KO, so the generator offset is the precondition, not a sufficient cause, which fits section 10 ("mechanism ... not
found"). With P1 the precondition cannot arise in a fight: the step sees a state that depends only on (match, step).

P0, the next report's detail line:
`desync detail: <fighters>; the CPU fighters' generators equal|differ|unused (no CPU fighters); the shared generator equal|differs;
the time scale equal|differs (<here>, <there>); the match generator equal|differs`. With P1 the shared generator part is checked after
the re-seed, so "differs" there means the re-seed itself failed; generator drift caused by draws inside the step shows in the fighters
or the match generator.

Seen on the way, not part of this fix: in a hub session the **loser's results screen can stop for good** (section 11b has the
cases). Every time it was the loser's copy, and its own A presses on the summary did nothing.

### 11b. After the rebase onto build 29 (2026-10-26)

Rebased onto `remaster` 077bbe4 (builds 27 to 29: rollback server switch, King of the Hill lockstep, select() receive, crowd
guard) without conflicts; re-lifted and rebuilt. Runs with `scripts/netplay-hub-test.ps1` (lockstep: the relay's rollback switch is
off), CPUs 28 and 30, relay port 27847, the venue order fixed with the new `-Venues` (relay `-test-venues`).

| Run | Pin | Settings | Venues played | Result |
|---|---|---|---|---|
| repro3 | off | `-FastKo 600` | Foundation, Terror Dome, The Pit, Terror Dome, Terror Dome | all checksums equal; 5th match (Terror Dome after Terror Dome): generator **different in 32 of 32 samples from step 1** (offset from the loading window); the 4th: 2 of 33 |
| fix3 | on | `-FastKo 600` | Foundation | equal (session stopped on the results screen) |
| fix3c | on | `-FastKo 600` | Foundation, Terror Dome | equal; Terror Dome **different at step 1 before the re-seed, equal in all 32 samples after** |
| fix4 | on | natural fights | Foundation, Terror Dome, The Pit, Terror Dome | **980 checksums equal**, no desync; 2nd Terror Dome **different at step 1** (state 813727448 / 1237985602; draws so far: music director 40 / 41, CArena 284 / 291) and in 6 isolated samples later, fighters equal to the KO (step 3840) |
| fix5 | on | `-Bot -FastKo 600 -FightSecs 9` | Foundation, Terror Dome, The Pit, Terror Dome | all checksums equal, all 109 generator samples equal |

So on build 29 the pin was seen to erase a loading-window offset at step 1 twice (fix3c, fix4) and the fights stayed in step; without
it the offset stayed for the whole match (repro3, as repro1). No desync in any run, with or without the pin: the fighters' parting of
the lab case was not reproduced in four Terror Dome matches without the pin (repro1 one, repro3 three).

Not reached: six matches in a row in one session after the rebase. Five sessions stopped on a results screen after 1, 2, 4, 4 and
(before the rebase) 3 matches; fix2 before the rebase got through 5. What was seen at each stop:

* It is always the loser's copy. Its summary stays up; its own scripted A presses (from 4 or 16 s, every 3 or 6 s, 10 to 12 of
  them) do nothing, and `Game.UpdateMatchRewardsPoints()` never comes. The winner's copy is through and back in the room.
* After the knockout each copy logs `the match is decided and player N went on ahead ... the rest runs on this PC alone`, so the
  results screens run outside the lockstep, and on the loser's copy the winner's port then reads as a pad at rest (`fight_input`,
  `REST_ONLY`).
* The first guess, that the summary takes only the winner's pad and the winner had left before the loser's copy showed it, does not
  hold for fix5: there the loser's copy showed its summary about 8 s before the winner's copy and was stuck anyway.
* The routes' blind fight presses run 1200 s and so go on through the results screens; on the winner's copy they took the summary
  and rewards in two seconds (fix3c, fix4). `-FightSecs` stops them, which did not prevent the fix5 stop.

Whether real players hit it, and why the loser's copy takes no input there, is open; it does not touch the generator fix. The hub
test gained `-Venues`, `-SummaryPresses`, `-FightSecs` and the route family `online8` for these runs.

`regress.py --quick` on the rebased build: 5 of 5 passed (661 unit tests). Also: `regress.py --only replay` fails on this base as on the fixed build with the same numbers (76 records, golden 80, hash
3495378e7de7...; measured 2026-10-25 on 747bffb with and without the fix): the golden stream does not match the current base, and the
pin does not change it (offline never reaches it).

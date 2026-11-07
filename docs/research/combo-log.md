# Combo log (`RECOMP_COMBO_LOG=1`)

A test and diagnostic switch that writes one compact line per hit, and per victim state change that
matters, to the normal run log (`[COMBO]` prefix). It is for play-testers and mods: record a legal or
illegal combo, read it back, and later design a detector from real data. It does **no** detection, has
no penalties, changes no game memory and sends nothing anywhere. It is off by default; when off each of
its three hooks costs one test of a flag.

Code: `src/hooks/combo_log.c` (and `.h`); hooks in `src/recomp_manual.c`:

| Hook | What |
|---|---|
| `sub_001C6CD0_enter` (new) | one hit, before its body: cdecl `(attacker, defender, hitFlags)` at `g_esp+4/+8/+12` |
| `sub_00087700_enter` (existing) | a fight simulation step begins: the hit's "after" state, state changes, combo ends |
| `sub_001A4D90_enter` (existing) | a match is set up: counters reset, step counter back to 0 |

Adding the new `_enter` hook needs a re-lift (`recomp.ps1`, or `pipeline-state.py lift`).

## Lines

`s=` is the combo log's own fight step (steps since the match was set up; steps that rollback runs again
are skipped, so online the log follows the steps as first run). Slots are fighter slots 0..3. States are
fighter `+0x30` in hex.

```
[COMBO] s=984 hit a=0 v=1 mv=00000047 ast=45 cs=1 fl=1 vst=01>0E wall=0 stun=0 dz=0 rh=0 m=5.28 c=2 h=1 bc=0 ws=0
[COMBO] s=2729 hit a=0 v=1 mv=00000047 ast=45 cs=1 fl=1 vst=02>03 blk=1
[COMBO] s=909 st v=0 10>15 ph=2 wall=0 stun=0 dz=0 rh=0 m=0.00 c=2 h=1 ws=0 lastmv=00002714
[COMBO] s=1027 end v=1 a=0 c=2 hits=2 bc=0 ws=0 stuns=0 dz=0 why=free
[COMBO] s=2106 st v=1 11>37 ph=1 wall+ wall=1 stun=0 dz=0 rh=0 m=48.84 c=9 h=1 ws=1 lastmv=00000058
[COMBO] s=2150 hit a=0 v=1 mv=00000047 ast=45 cs=1 fl=1 vst=37>37 wall=1 stun=0 dz=0 rh=1 m=61.60 c=9 h=2 bc=0 ws=1
[COMBO] s=2271 st v=1 37>15 ph=2 wall- wall=0 stun=0 dz=0 rh=3 m=98.18 c=9 h=4 ws=1 lastmv=0000004C
[COMBO] s=2700 ablock a=0 v=1 from=45 c=11 h=2 vst=11
[COMBO] s=2733 bcancel a=0 v=1 c=11 h=3 n=1 mv=00000036
[COMBO] s=1055 grab a=1 v=0 vst=0E wall=0 stun=0 dz=0 open=1 c=3 h=1 ws=0 offwall=-1 sincehit=13
```

| Kind | Fields |
|---|---|
| `hit` | `a` attacker, `v` victim, `mv` the attacker's move word (`+0xF50`), `ast` attacker state, `cs` attacker chain step (`+0x40C`), `fl` hit flags (third argument), `vst` victim state before>after (after = at the next step, or at the next hit on the same victim), `wall`/`stun`/`dz` flags after, `rh` pinned re-hit counter (`+0x384`), `m` victim meter (`+0x374`), `c` the victim's combo index, `h` hit number in it, `bc` block cancels so far, `ws` wall sticks so far. A blocked hit prints `blk=1` and no combo fields. |
| `st` | a victim's state (or stun flag) changed while a combo is open: `from>to`, `ph` phase (`+0x34`), tags `wall+`/`wall-`, `stun+`/`stun-`, `dizzy+`/`dizzy-`, the counters, and `lastmv` the move of the last hit. A change the `hit` line just showed is not repeated. |
| `end` | a combo closed: totals (`hits`, `bc`, `ws`, `stuns` stun-flag entries, `dz` dizzy entries) and `why`: `free` (state 1), `block` (2, 3, or the hit was blocked), `acts` (the victim strikes or grabs on its own), `ko` (0x18), `new_attacker`, `match`. |
| `ablock` | the attacker entered block (state 2) while its combo was open. If its next hit lands in the same combo, a `bcancel` line follows. |
| `bcancel` | a block cancel: `a`, `v`, `c`, `h`, `n` cancels in this combo so far, `mv`. |
| `grab` | a fighter entered the grab start state 0x1E: `v` the victim of its last hit (or the nearest fighter), the victim's state and flags, whether its combo is open, `offwall` steps since the victim left a wall state (-1 never), `sincehit` steps since the attacker's last hit on it. |

### Combo rule (ours; the game keeps no combo counter)

A combo opens on a hit that is not blocked and closes when the victim is back in a free stance (1),
blocks (2, 3), strikes or grabs on its own (0x45..0x4B, 0x6C..0x70, 0x1E), is knocked out (0x18), another
fighter hits it, or the match is set up again. Hits on a knocked-down victim (0x15 > 0x0F) stay in the
combo.

### Reading the illegal-tech rules from it (for a later detector)

| Rule | What to look for |
|---|---|
| block-cancel infinites | `bcancel` lines; `bc` on `hit`/`end` |
| wall redaze | an `st` line with `wall-` where `to` is a dizzy/stun state (`dizzy+` or `stun=1`) |
| grab on a victim coming off the wall | `grab` with a small `offwall`, `open=1` |
| the same chain stun twice in one combo | two `stun+`/`dizzy+` lines in one combo with the same `lastmv` |
| a second wall restick | `ws` reaching 2 in one combo |

## Offsets and states

Confirmed live in three bot fights: offline vs the CPU (`logs/dj/20261108-001900-offline/copy0.log.err`),
online 2 matches `--bot 60` (`logs/dj/20261108-002328-online/`, 74 hits) and online
`--bot p1=aggressive,p2=defensive` (`logs/dj/20261108-002712-online/`, 96 hits, one wall slam, one block
cancel). Online, both copies wrote byte-identical `[COMBO]` lines (lockstep).

- Fighter records `0x3B92E0 + slot*0x12E8`; state `+0x30`, phase `+0x34`.
- `sub_001C6CD0` is the per-hit entry: cdecl `(attacker, defender, hitFlags)`; it fires once per landed or
  blocked hit (13 telemetry hits in the 97 s fight, 23 `hit` lines with blocked ones and CPU hits). It
  returns at once when `(defender+0x91E >> 8) & 0xE`; those calls are not logged.
- `+0xF50` first word is a move id: 0x36..0x6C for the standard strings (state 0x45..0x4A), 0x2710/0x2714
  for the heavy hits from states 0x6C/0x6E.
- `+0x40C` is the attacker's chain step (1, 2 in consecutive hits of a string).
- Hit flags (third argument) match the attack state as the wall-stagger study says: 0x45 -> 1, 0x46 -> 2,
  0x47 -> 8, 0x49 -> 2, 0x4A -> 0x10, 0x6C -> 0x40, 0x6E -> 0x100.
- Block: the victim holds state 2; a hit on the block takes it to **state 3** and back to 2 (not 0x0F).
- Grab start state 0x1E (attacker side).
- Hit reactions seen: 0x0E flinch, 0x10 big stagger, 0x11 stagger, then 0x15 (knocked down) -> 0x1A
  (getting up) -> 1, or 0x11 -> 0x17 -> 1, 0x11 -> 0x5F -> 0x36 -> 1.
- A victim on the ground (0x15) hit by a 0x4A (flags 0x10) goes to **0x0F**: 0x0F is not (only) the
  blocked-hit reaction.

- Wall slam 0x37: a stagger (0x11) into the wall goes 0x11 > 0x37; hits on the pinned victim keep it in
  0x37 and count `+0x384` up by one per hit (1, 2, 3); it slumps 0x37 > 0x15. `+0x384` keeps its value
  after the victim leaves the wall (it was 3 for the rest of that fight), so read it only while pinned
  (the static study says it is reset when a pin begins; a second pin was not seen).
- A block cancel by the definition above happened once (the attacker blocked at chain step 2 while the
  victim staggered, then hit again in the same combo, chain step back to 1).

Not confirmed live (no occurrence in the test fights; the code uses the static study's values):

- Wall states 0x14/0x1B/0x08/0x60 (only 0x37 was seen), a second wall stick in one combo, a grab right
  after the wall (the one grab after a wall came 109 steps later).
- Stun: `+0x927` bit 2 never set; no state 0x16 seen. `+0x374` behaves like a meter that grows with
  hits taken and drops to 0 on a knockdown; it reached 144 once, so it is not a 0..100 percentage.
- Whether the one `bcancel` is the players' "block cancel" technique or just a bot that blocked and
  struck again: the definition (attacker in state 2 during an open combo, then its next hit lands in
  the same combo) is ours. `ablock` also fires when the attacker blocks from idle while the victim is
  still in a reaction, which a detector should weigh by `from=`.
- The telemetry hit count (`sub_001A5DD0`, damage) differs from the `hit` lines (`sub_001C6CD0`): the
  online fights had 82 and 105 telemetry hits against 74 and 96 `hit` lines (blocked hits included);
  likely because state handlers such as the wall slam (0x37) apply damage through `sub_001A5DD0` without a
  new `sub_001C6CD0` call (not measured).
- Rollback: re-run steps are skipped; with rollback on the logged hits are the first-run (predicted) ones.

To capture the rules above, record a human session against a wall with the switch on (two copies or
vs the CPU) and read the `[COMBO]` lines: `grep "\[COMBO\]" logs/run-*.log.err`.

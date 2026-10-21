# Wall stagger: why a hit opponent at a wall falls in the port (static read)

Read-only study of the lifted C (`src/recomp/gen/recomp_0012.c` unless a file is named), the disassembly under
`D:\DefJam\data\analysis\tools_disasm_output\asm\text.asm` and the XBE data tables. Nothing was built or run.
Addresses are guest addresses; `file:line` is in `src/recomp/gen`. No game code is reproduced, only described.
Confidence: **[V]** read in code or data, **[I]** inferred from shapes or names, **[U]** not settled.

## Short answer

1. The decision is not one branch. It is three chained gates, two of them random. All three draw from the **match
   generator** (`0x3C1404`, through `sub_001BC730`) [V]:
   * **Gate A, hit reaction level**: `sub_001C6CD0` tail. Deterministic table lookup (health band x momentum band x
     combo step). It picks state `0x0E` (flinch), `0x11`, `0x12` or `0x10` (stagger-back) [V].
   * **Gate B, wall contact while staggering**: `sub_001D9E40`, called every frame by the stagger states `0x11`
     (mode 1) and `0x12` (mode 2). One random roll per state entry decides wall-slam (state `0x37`) against staying
     put, with a probability that depends on the defender's health band [V].
   * **Gate C, re-hit while pinned**: `sub_001C6CD0` at `loc_001C7295` ff. (`recomp_0012.c:97505`). A hit on a fighter
     that is already in a pinned state (`0x1B`, `0x14`, `0x37`, `0x60`) bumps `fighter+0x384` and rolls
     `sub_001A5C30(fighter, 0, mode)`: p = 0.075 x hits. Success picks the "slide out / fall" phase, failure the
     "recoil, stay pinned" phase [V]. This is the closest match to the player's "mostly stays, falls now and then, or
     after a big hit".
2. **No lifter or runtime mistake was found** on any of these paths (section 5 lists what was checked). The port computes
   the same decision as the console from the same inputs. So the divergence is in an *input*, or in the *geometry/state
   that feeds the gates*, not in the compare or random-draw code. Section 6 ranks the suspects and gives the test that
   separates them. I could not read the arena collision data, so which gate a given wall takes is [U].
3. The most useful first measurement: log the CPU fighter's `+0x30` state and the draws at the three gates for ~20 wall
   hits (section 7). The state number the opponent ends up in tells which gate fires.

## 1. State numbers involved (state = fighter `+0x30`, table built in `sub_001B54D0`, `recomp_0012.c:55249`)

State number = table index; handler in brackets. Names are mine [I]; the numbers and handlers are [V].

| State | Handler | Reading |
|---|---|---|
| `0x45` | `sub_001C85D0` | the standard strike (punch/kick chain). Calls `sub_001C6CD0(self, target, 1, 0)`; sets combo step `+0x40C` = 1 at phase 0 |
| `0x46..0x4B`, `0x6C..0x70` | `sub_001C8840`, `8A00`, `8C00`, `8D80`, `8EC0`, `7F60`, `80C0`, `8230`, `8370` | other attack states; they call `sub_001C6CD0` with hit-class flags 2, 8, 4, 2, 0x10, 0x40, 0x80, 0x100, computed |
| `0x0E` | `sub_001C5350` | light flinch in place (anim `0x2858..0x2879`) |
| `0x0F` | `sub_001C5A90` | reaction to a blocked hit |
| `0x10` | `sub_001C5420` | big stagger-back. Contains a `sub_001BC730(0.25)` roll that only picks an animation |
| `0x11` / `0x12` | `sub_001C5660` / `sub_001C5870` | stagger-back, levels 2 and 3. Each frame: `sub_001B6320`, `sub_001B95F0(3)` (crowd), then **`sub_001D9E40(self, 1)` / `(self, 2)`** (`sub_001C5660` at `0x1C5719`, `sub_001C5870` at `0x1C5929`) |
| `0x13` | `sub_001D7BD0` | "fall to lower level" (string `FallToLowerProc`), entered only near a special fall plane |
| `0x14` | `sub_001B51B0` | rope/fence bounce (anim `0x2839..0x283D`); phase 6 = recoil and stay, phase 8 = slump and leave |
| `0x1B` | `sub_001DC120` | pinned in a ring corner (positions itself with `sub_001AB7C0`, anim `0x280B..0x280F`); phase 7 = recoil and stay, phase 9 = slump and leave |
| `0x37` | `sub_001DACE0` | wall slam; damage through `sub_001A5DD0`; contains the `Event_Enviro ... wall hit` path (`sub_001DA5C0`) |
| `0x08` | `sub_001BD530` | lean on the wall/ropes (anim `0x27E4`, `0x27E5`, `0x2805`) |
| `0x1A` / `0x19` | `sub_001CA380` / `sub_001CA270` | ground states reached from `sub_001B46B0` / `sub_001B47F0` (end-of-animation dispatchers) |

## 2. Gate A: reaction level (`sub_001C6CD0`, 0x1C6CD0, `recomp_0012.c:96639-98764`)

Signature (cdecl): `(attacker, defender, hitFlags, 0)`. It stores `hitFlags` in `defender+0x914`, copies the attacker's
attack info (`attacker+0xF50`, 0x1A dwords) to `defender+0xFB8`, calls `sub_001C5E90` (relative bearing bits into
`defender+0x918`), then dispatches on the defender's current state:

* First switch (`recomp_0012.c:96760`ff, table `0x1C7EB8`/`0x1C7E8C`): defender already in state `0x1B` (and
  `+0x92E` bit 0), `0x14`, `0x37` (and `+0x92E` bit 2), `0x60` ORs `0x1000`, `0x2000`, `0x4000`, `0x8000` into
  `+0x914`. That is how a re-hit on a pinned fighter is recognised (Gate C) [V].
* Damage is applied before the reaction: `sub_001A5DD0(attacker, defender, 0, attackInfo, mult, 1, 1, 3)`
  (`recomp_0012.c:97371` ff).
* Guard branch (state `0x0F`) at `loc_001C7130` (`:97336`): `(+0x914 & 0x210) && (+0x91E & 0xE0)` or `+0x91E & 0x60`.
* Re-hit branch at `loc_001C7295` (Gate C, below).
* Ordinary branch from `loc_001C755B` (`:98013`): computes `L10 = sub_001A6360` (momentum band 0..7: `+0x374/+0x378`
  against 8 thresholds at `0x2EC0A8`: 0.2, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0), `L18 = sub_001A6320` (health band 0..3:
  `+0x35C` against `0x2EC098`: 0.75, 0.5, 0.25, 0.0) and **`L14 = sub_001DA460(defender, 10.0)`** (a "fall plane"
  within 10 units, section 4). The level `esi` then comes from a stack table built per hit class, index
  `momentum + 8 * health`:

  | hit class (`+0x914`) | table (rows = health band 0..3, 8 columns = momentum band) |
  |---|---|
  | `&0xA` | 2 2 2 2 2 3 3 4 / 2 2 2 2 3 3 3 4 / 2 2 3 3 3 3 4 4 / 2 3 3 3 3 3 4 4 |
  | `&4` and `&0x100` | 3 3 3 3 3 4 4 4 / 3 3 3 4 4 4 4 4 / 3 3 4 4 4 4 4 4 / 4 x8 |
  | `&0x40` | 2 x8 / 2 2 2 2 2 2 2 4 / 2 2 2 2 2 2 4 4 / 2 2 2 4 4 4 4 4 |
  | `&0x80` | 2 2 2 2 2 3 3 4 / 2 2 2 2 3 3 3 4 / 2 2 3 3 3 3 4 4 / 2 3 3 3 3 3 4 4 |
  | `&1` (normal strike, state `0x45`) | indexed also by the attacker's combo step `+0x40C` (1..4), 32 entries per step: steps 1..3 are `1` (flinch) in every cell except momentum band 7 (`2`) and, for steps 2..3, health bands 2..3 at momentum band 6 (`2`); **step 4 is `2` (state `0x11`) in the low bands and `4` (state `0x10`) from momentum band 7 (health band 0), 6 (band 1), 5 (band 2), 3 (band 3) upward** |
  | none of the above (e.g. `0x10`) | reads **uninitialised stack** `[esp+0x9C]` at `loc_001C7D9C` (`:98585`) [V]; any value outside 2..4 gives the flinch |

  The table values are built from registers (`1`=edi, `2`=ebx, `3`=eax, `4`=esi) so they only show in the lifted C as
  `MEM32(esp + k) = reg` runs (`:97800` ff). I decoded them from the disassembly (script in my scratch, not in the repo).
* Level to state (`loc_001C7DA3`, `:98588`): `esi == 2` -> `0x11`; `3` -> `0x12`; `4` -> `0x13` if `L14 != 0` else `0x10`;
  anything else -> `0x0E` with `defender+0x919 |= 1`. Defender states `6, 8, 0x11, 0x12, 0x17, 0x36` skip the table
  (second switch `0x1C7F18`/`0x1C7F20`, `:98092`) and are forced to level 4. Before the table: any hit class in
  `0x1CE` with `L14 != 0` goes straight to state `0x13` (`:98100-98140`).

**Consequence [V]:** a strike chain (class 1) cannot reach the stagger states before combo step 4, except at the top
momentum band. Steps 1..3 only flinch (`0x0E`). A fall after the *first* hit of a chain therefore does **not** come from
Gate A for class 1 unless the table row is wrong (it reads `+0x40C`, `+0x35C`, `+0x374/+0x378`).

## 3. Gate B: wall contact in the stagger states (`sub_001D9E40`, `recomp_0012.c:150731`)

Signature (cdecl): `(fighter, mode)`; returns 1 if it changed state. Stores `mode` in `fighter+0x934` (16 bit).
Order of tests [V]:

1. return 0 if `fighter+0x924 & 0x2000` (already decided in this state; the flag is cleared by every state set,
   `sub_001B3B40` ANDs `0xDEFB`), or `sub_001B45F0(fighter, +0x2EC, +0x2F0)` fails, or `fighter+0xAB0` (contact plane) is 0.
2. `dot = sub_001BAFF0(heading, plane+0x34)` where `heading = sub_001BAD20(fighter+0xD4, 1.0)`; stored at `esp+0x40`.
3. Corner clause (`:150840-150870`): `idx = sub_001AC440(fighter+0xE8, 7.07)`, nearest of 4 ring corners; then
   `sub_001B4120`; `dot(heading, table 0x2EF5B0[idx]) > 0.5` -> state `0x1B`, return 1. Only live when
   `[0x3B9050] & 0xC` (section 4).
4. Wall branch, taken when the plane material (`*(plane+0x44)`, first dword = flags) has bit `0x8` (`:150875`ff):
   sets `0x924 |= 0x2000`, then
   * `mode & 0x1C != 0` -> forced "hit";
   * else **`p = table[health_band + 4 * ((~mode) & 1)]`** with table `0.2, 0.3, 0.5, 0.7, 0.35, 0.55, 0.75, 0.95`
     (constants stored at `:150896-150903`); `hit = sub_001BC730(p)` (`:150912`, return address `0x1D9FB4`).
   So mode 1 (state `0x11`): 0.2/0.3/0.5/0.7 by health band 0..3; mode 2 (state `0x12`): 0.35/0.55/0.75/0.95.
   Then, if `material & 0x42240000 || hit`: needs `|dot| > 0.342` (cos 70) and `fighter+0xABC & 1` (contact flag);
   `dot > 0` sets `0x924 & 2`; `(mode & 8) && material & 0x200000` -> state `0x13`, else **state `0x37`** (wall slam).
5. Rope/fence branch, material bit `0x80000` (`:151124` ff, `loc_001DA0F1`): needs `dot > 0.866` (cos 30) and `mode & 3`;
   rolls `sub_001BC730(momentum ratio)` (ratio doubled when `mode & 1 == 0`); success -> state `0x14` (return 1).
   Otherwise, if `|dot| > 0.707`, state `0x08` (lean on the wall) with `0x920 |= 1`, `0xAC0 = 0xAB0`.
   Else return 0 and the fighter simply stays in the stagger, held by the collision push-out.

[I] (but strongly suggested by the numbers) this is the "wall stagger": a staggered fighter that touches a wall is
either slammed (`0x37`, probability by health band) or bounced/leaned (`0x14` / `0x08`) or just pinned by collision.

## 4. Where the contact and the geometry come from

* `fighter+0xAB0..0xABE` is the output block of the per-update collision sweep: `sub_001A4A30` calls `sub_001AD110`
  (`:33001`) after the state handlers; for each fighter with `rec+0xA70 != 0` it calls
  `sub_001AC490(rec, rec+0xA74, rec+0xAB0)` (`:31007`). The block is written **through the third argument**, so a search
  for stores to `+0xAB0` finds almost nothing: plane A, plane B, plane C, and a word at `+0xC` = 1 when the first
  contact kind is 1 or 2 [V]. Because the handlers run before the sweep, a state reads the *previous* step's contacts.
* `rec+0xA70/0xA74...` (shape, scale, material mask) are written by `sub_001ABA50` / `ABAC0` / `ABB40` / `ABBD0` /
  `ABC40`, which each state handler calls with its own mask (`0x11`: 3; `0x10`: `0x2AB`; `0x0E`: 3).
* Planes are `*(0x3BF1B8)`, count `0x3BF1C0`, stride 0x48, built from a loaded collision blob by `sub_001ABCC0`
  (`CollisionPlaneInit`): plane `+0` flags (bit `0x2000000` = material bit 0 clear), `+0x34` normal, `+0x44` -> material
  entry (stride 0x30, flags dword first). **The material flags decide the gate.** I could not read the blob [U].
* The ring boundary and corners (`sub_001AB7C0`, `sub_001AC440`) exist only when `[0x3B9050] & 0xC`. `sub_001A4D90`
  sets bit 4 for `[0x3B9084]` = 9, 0xA, 0xD, 0x1A and bit 8 for 0xE (jump table `0x1A4ED8`, byte table `0x1A4EE4`). With
  both clear `sub_001AB7C0` returns zeros and `sub_001AC440` returns -1, so state `0x1B` can never be entered [V].
  (`0x2DBCC8` names index 9 `Window` and 10 `Cage`; the notes did not prove `0x3B9084` indexes it [U].)
* `sub_001DA460` ("WallFallLowerNearChk", `:151758`) only considers planes with plane flag `0x2000000` and material
  `0x200000` and not `0x2`; a distance below 10.0 and a segment test along the inverse normal. It is the fall-to-lower
  test of Gate A, not a general wall test [V].

## 5. Inputs, and what I checked for lifter/runtime errors

Inputs [V]: `defender+0x35C` (health ratio), `+0x374/+0x378` (momentum), `+0x40C` of the attacker (combo step 1..4,
set in `sub_001C85D0` phase 0 and `sub_001C61C0`), `+0x914`/`+0x918` (hit class and bearing), `+0x384` (re-hit count),
`+0x934` (mode), `+0x924`, `+0xAB0`/`+0xABC`, plane normal and material flags, the heading `+0xD4`, the attack-info
copy at `+0xFB8` (its `+0x48` flag word bit `0x800` forces the slide-out phase). Randomness: only the match generator.
Thresholds: constants above plus `0.075` (`0x2C105C`) and the cosines `0x2CCE60/64/68` = 0.7071 / 0.8660 / 0.3420.

Checked and found faithful in the lifted C [V unless noted]:

* The generator `sub_001BC6E0` (`:72008`), the chance test `sub_001BC730` (`:72073`; p <= 0 false, p >= 1 true, else
  `draw <= p`), and the seeding `sub_001BC690`. Signed-int load plus the `2^32` fix-up for negative states, the `2^-32`
  scale (`0x2A8200` = `0x2F800000`), the flag handling of `jge`, and the `fcomp` / `fnstsw` / `test ah,0x41` / `jnp`
  patterns all map to the right branches (`RECOMP_FCMP_CC` gives `<`=0x0100, `>`=0, `=`=0x4000, unordered=0x4500; PF
  helper is correct).
* `sub_001A5C30` modes 6..9 (`:13973`; jump table `0x1A5D9C`, mode 6 and 9 -> `+0x384 * 0.075`, mode 7 and 8 the same
  formula), including the stray bogus function the lifter emits after it for the jump table bytes (`sub_001A5D9A`; it is
  never reached).
* `sub_00043EB0` (fabs written as `fld; fcomp; fld; fnstsw; fchs`) and `sub_00200BCC` (`_ftol` via `fistp` plus a
  rounding correction; correct for round-to-nearest and for truncate).
* All switches here are lifted as comparisons against the live table contents (`_jt == ...`), so a table that sits just
  after the function end is not lost (unlike the memmove case in `recomp_manual.c`): `sub_001C6CD0` (two), `sub_001DC120`
  (13 entries), `sub_001A5C30` (11).
* x87 precision: `g_fp_control_word` starts at `0x037F` (`xbox_memory_layout.c:1904`), so `RECOMP_FP_PC` does not round
  to float (`recomp_types.h:294`). That matches a console running 53/64-bit precision and only matters at exact ties.
  Offline `netplay_on_seed` is a no-op (`netplay.c:76`; with a session it replaces the seeds).
* 16-bit reads: `mov ax, [esp+8]` for the mode and `[esi+0x934]` are lifted as `SET_LO16(eax, MEM16(...))`; correct.

Not checkable by reading: [U] the uninitialised stack word at `0x1C7D9C` (only for hit class `0x10`, the states
`0x4A/0x4B`; the console's leftover value is unknowable; any value outside 2..4 behaves like the flinch).

## 6. Ranked suspects (what could make the port fall almost always)

1. **[I] A wall takes Gate B with a "forced" material.** `material & 0x42240000` skips the roll and always slams (state
   `0x37`) when `|dot| > 0.342` and the contact flag is set. If the arena's walls carry these bits the opponent falls on
   the first stagger-back regardless of p. That would be data, identical on the console, so on its own it cannot explain
   the difference; but `dot`, the contact flag and the material mask (`0x11` uses mask 3) all depend on geometry and
   animation root motion that the port computes through lifted floats and the model/animation host code
   (`sub_00044660`, `sub_000445A0`). Anything that makes contact appear earlier or more often (a different push-out
   from `sub_001AC490`, a joint position `+0xE8` that differs) moves the fighter into Gate B sooner.
2. **[I] Gate C counter starts non-zero or grows too fast.** p = 0.075 * `+0x384`. The counter is bumped at *every*
   `sub_001C6CD0` call that lands on a pinned fighter; if the port applies one attack to the same defender on more than
   one step (the attacker state `0x45` only advances its phase after a hit when `attacker+0x41C & 4` is set, line
   `0x1C861F`), p climbs 7.5 % per extra application and the slide-out phase becomes near certain. This is a pure
   timing/phase question, so it would show as `sub_001C6CD0` being entered more than once per swing.
3. **[I] Gate A reads a wrong combo step or health/momentum band** (`+0x40C` = 0 would index the previous stack table
   and give levels 2..4 at the first hit; `+0x374/+0x378` or `+0x35C` wrong gives a high band). Cheap to log.
4. **[U] A different state is the "fall".** The state the opponent ends in was not observed. If it is `0x1A`/`0x19`, it
   comes from the end-of-animation dispatchers `sub_001B46B0` / `sub_001B47F0`, which switch on the animation category
   `fighter+0x318` (set by `sub_001A85A0` / `sub_001A8770` from the animation tables), not from these gates.
5. **[I] Match generator bias.** Nothing in the code suggests it, and the sequence is only as random as the table fill.
   Cheap to rule out with the log in section 7 (a run of `draw <= p` successes with p = 0.075 would be visible).

Not suspects (checked): FCOM/FUCOMI flag patterns, FISTP rounding, int/float conversions, signed compares, 16-bit
reads, modulo of a signed value (`sub_001BC770` is not on this path), seeding (offline the match generator is seeded once
per match by `sub_001A4D90` -> `sub_001BC690`, first match with `0x7714F95` / `0x382D28A3`).

## 7. Tests

**T1: state trace (no code).** Offline versus the CPU in a venue with walls, scripted input holding the CPU against a
wall (`RECOMP_TEST_INPUT`). Temporarily set `STATE_PERIOD` to 1 in `src/hooks/test_telemetry.c` (or use
`RECOMP_TEST_PAD_DUMP`, period 6) and read the CPU's `state` after each hit. Expected original: `0x45` hits 1..3 -> `0x0E`
(flinch), hit 4 -> `0x11` or `0x12`, then mostly stays (`0x08` / `0x14` / held), `0x37` with the section 3 probabilities.
A port that jumps from `0x0E` straight to a ground state, or to `0x37` on hit 1, names the gate.

**T2: three entry hooks (needs a re-lift, like the existing `_enter` hooks in `src/recomp_manual.c`).**

```
sub_001C6CD0_enter: log MEM32(esp+4) attacker, MEM32(esp+8) defender, MEM32(esp+0xC) hitFlags,
                    defender +0x30 state, +0x34 phase, +0x384, +0x92E, floats +0x35C +0x374 +0x378,
                    attacker +0x40C, and the call count per swing (should be 1 per attack-state entry)
sub_001D9E40_enter: log fighter +0x30, mode = word at esp+8, +0xAB0, +0xABC, +0x924,
                    material flags = MEM32(MEM32(+0xAB0)+0x44 -> *ptr), plane normal +0x34..+0x3C, heading +0xD4
sub_001BC730_enter: log return address MEM32(esp), p = MEMF(esp+4), and the draw that is about to be returned:
                    s1 = MEM32(0x3C1404)*0x5D588B65 + MEM32(0x3C1400); r = MEMF(0x3C1408 + 4*(s1 & 31));
                    result = (p > 0) && (p >= 1.0f || r <= p)
```

The draw returned by `sub_001BC6E0` is the *old* table slot, so the hook can print it without advancing the generator.
Return addresses to filter on: `0x1D9FB4` (Gate B wall roll), `0x1DA139` (Gate B rope roll), `0x1A5D82` (Gate C,
modes 6 and 9), `0x1A5D42` (mode 7), `0x1A5D62` (mode 8), `0x1C5548` (state `0x10` anim roll). A plain
`sub_001A5C30_enter` can log its mode (`MEM32(esp+0xC)`) and `fighter+0x384` (`MEM32(MEM32(esp+4)+0x384)`) directly.
For ~20 wall hits expect: roll 1 at p = 0.2..0.7 (health band) once per stagger-back; `sub_001A5C30` p = 0.075, 0.15, ...
Count how often the result is 1.

**T3: split the gates with overrides (cheap).** Replace `sub_001D9E40` with a function that returns 0 (as
`sub_0001A4E0` is replaced in `recomp_manual.c`): if the falls vanish Gate B is the cause. Do the same for
`sub_001A5C30` returning 0 for modes 6..9 to test Gate C. Whichever removes the falls is the gate; then look at its
inputs with T2.

**T4: look at the geometry.** At match start read `MEM32(0x3BF1C0)` planes at `*(0x3BF1B8)` stride 0x48 and print, per
plane, `+0` flags, `+0x34..+0x3C` normal and `*(MEM32(+0x44))` material flags. Predict: walls that show `0x8` take
Gate B, `0x80000` take the rope branch, any of `0x42240000` slam without a roll. `scripts/peek-guest.py` reads a live
process; the pointer chase is two reads.

## 8. What I could not settle

* Which materials the shipped walls and cages carry, so which branch an ordinary wall takes: needs the collision blob
  (loaded by `sub_001A2EA0` -> `sub_001ABCC0`; source file not identified).
* The meaning of `fighter+0x40C` after step 4 and of `rec+0xA70` ownership for every state.
* Whether the player's "fall" is state `0x37`, `0x1A`, or something not on this list.
* The contents of the attack-info flag word at `+0x48` (`0x800`, `0x80000`, `0x8000000` are tested) that comes from the
  move tables; if the moves data were mis-read the slide-out phase would be forced (`+0x1000 & 0x800`).

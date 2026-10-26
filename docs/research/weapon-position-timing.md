# Is the weapon position that feeds fighter+0x81C host-timed? (static read)

Read-only study of the lifted C in `src/recomp/gen` and the XBE image. Nothing was built or run.
Addresses are guest addresses; `file:line` are lines of the lifted C in this tree (`recomp_00NN.c` under `src/recomp/gen`).
Marks: **[V]** read in code, **[I]** inferred, **[U]** not settled.

## Findings

1. **`sub_001AAD40` is the only writer of `+0x81C` whose input lies outside the compared data.** It is fighter state `0x69`
   ("walk to a weapon on the floor and pick it up"; handler table slot `0x1A8`, `recomp_0012.c:55388` -> state `(0x1A8-4)/4`). Its
   target is `*(entry+0x74)` = `entry+0x64` = the translation row of a 4x4 matrix stored at `entry+0x34`, where
   `entry = 0x3BE778 + fighter+0x8D0 * 0xA4` [V]. The four other float writers read only fighter-record fields (details in
   section 1), which the measurement says were equal, so by elimination `sub_001AAD40` is the one [I]. The weapon table is
   **outside** the compared ranges (fighter records `0x3B92E0..`, globals `0x3B8F28..0x3B9074`): its content may have differed
   before `+0x81C`; the measurement cannot have seen it [I].
2. **The table at `0x3BE778` is the fight system's weapon-entry table: 16 entries of `0xA4` bytes, `0x3BE778..0x3BF1B8`** (loop bound
   `cmp esi, 0x3BF1B8`, `recomp_0012.c:24562`). `entry+0` = weapon handle (index into the weapon manager's 16 slots),
   `+4` = weapon type, `+0xC/+0x10` = owner slot, `+0x30` = flags, `+0x34..+0x73` = cached world matrix, `+0x74` = pointer to the
   matrix translation (`entry+0x64`, set by the init `sub_001A9BC0`, `recomp_0012.c:24434-24490`) [V].
3. **No host-timed writer of that position was found.** Every writer of the entry matrix and of the weapon slot matrix is reached
   only from the step (`sub_00087700` and the update arm of `sub_0008BE60`/`sub_00079B60`), with the constant `dt` or the match
   time scale. None is in the closure of the render entry `sub_000796A0` (section 3). The honest conclusion: the "host-timed
   weapon position" hypothesis is **not supported** by the code; what is supported is "the weapon table is a fight input that nobody
   hashes or resets" [I].
4. **Smallest action, in order of cost**: (a) add the weapon table (`0x3BE778`, `0xA40` bytes) and the weapon manager
   (`0x376144`, `0xD2C` bytes) to the per-step hash and log the first step they differ, before touching any gate; (b) if they
   differ, the origin is the spectators' weapon offer (section 4) and the existing gate `sub_0007EA70` -> 0 removes it; (c) a
   fight-side gate `sub_001AB200` -> `-1` removes state `0x69` for floor weapons without touching the crowd.

## 1. The writers of `+0x81C` (question 1)

| Writer | Lifted C | State / caller | What it computes | Inputs | Can run in a fight step |
|---|---|---|---|---|---|
| `sub_001AAD40` | `recomp_0012.c:27073`, store at 27155 | state `0x69` (indirect, table slot `0x1A8`) | phase 0: `+0x81C = sub_001BBC40(fighter+0x44, *(entry+0x74))`, plays anim `0x28C6`; phase 1: turns the yaw toward `+0x81C` (`sub_001B3E10`, rate 20.0), then equips (`sub_001AA1D0`) or plays `0x28C8` | **weapon entry table** | yes (picked by `sub_001CA990`, line 106873, which sets `fighter+0x8D0 = sub_001AB200(...)` then state `0x69`; also reached from the CPU through `sub_001AE0C0`) [V] |
| `sub_001B4E40` | `54099`, store at 54378 | only direct caller `sub_001DBFD0` (155752); it is an arc-movement planner | `+0x81C = angle(...)/fighter+0x9AC` steps; reads the other fighter's record through `MEM32(fighter)` and `fighter+0x854/85C/9AC` | fighter records only | yes, but input equal on all PCs [I] |
| `sub_001B6560` | `57073`, store at 57204 | state `0x71` (slot `0x1C8`) | `+0x81C = angle from the world origin to the fighter` (`atan2(x,z)` of `fighter+0x44/0x4C`, line 57146), then `sub_001BACD0` | fighter position only | yes, deterministic given equal positions |
| `sub_001D0870` | `124956`, store at 124991 | called from `132976` (`sub_001D3800` region) | `+0x81C = sub_001BBC40(fighter+0xE8, other+0xE8)`: yaw toward **another fighter's** joint-0 position | two fighter records | yes, deterministic given equal records |
| `sub_001D0A50` | `125274`, store at 125314 | called from `129845` (`sub_001D28C0` region) | `+0x81C = partner.+0x81C (+0 or +180)` | a fighter record | yes |
| `sub_001612B0` (integer) | `recomp_0010.c:89367` | caller `recomp_0010.c:8855` (`0x14638B`), outside `0x1A0000-0x1E0000` | integer store | n/a | not a fight-step path [I] |
| `sub_001CAC40` (integer) | `recomp_0012.c:107359` | callers `108123/108156/108412` | integer store (clearing/flag use) | n/a | integer, cannot produce -23.2 |

`sub_001BBC40` (`recomp_0012.c:70606`) = `atan2(to.x - from.x, to.z - from.z) * 0x292520` with both arguments **pointers to vec3**
(`MEMF(eax)`, `MEMF(eax+8)`; `x` and `z`, `y` ignored). It uses the host `atan2` (`fpatan`), but -23.2/-25.8/-24.0 against 164.2 is
degrees apart, not a libm last-bit difference [I].

Why the weapon writer: the other four write `+0x81C` only from data that sits in the fighter records or from `fighter+0x44`
(already compared). The 164.2 -> about -24 jump of fighter 0 on all four PCs together with different values means the target changed
by a different amount on each PC; a weapon that just became a candidate, or a candidate at a different place, fits [I]. Note
`+0x81C` is also *read* by the state-`0x69` follow-up (`sub_001B3E10` at `27195-27210`), so a bad value persists.

## 2. The table and the position (question 2)

* Init of an entry: `sub_001A9BC0` (`recomp_0012.c:24434`): zero `0xA4` bytes, fields `+0..+0x10`, `+0x28` = -1, `sub_0002EFD0(entry+0x34)`
  (identity matrix), `entry+0x74 = entry+0x64` [V]. Callers: `sub_001AA140`, `sub_001AB410`.
* Entry creation / registration (the matrix gets its translation here):
  * `sub_001A9F10` (`24832`), caller `sub_000BBF30` (`recomp_0005.c:53649`): takes a weapon slot's world matrix (`sub_000BA5D0(slot, buf, -1)`
    = a 16-dword copy of the slot's own matrix at `slot+0xC`, `recomp_0005.c:48573-48620`), rebuilds the orientation, copies it to
    `entry+0x34`, casts a ray with `sub_001AC360` for the floor height (`entry+0x78`), and publishes the matrix back with `sub_000BB9E0`.
    Callers of `sub_000BBF30`: `sub_0007ED10` (crowd, held-weapon upkeep; the toss case at `recomp_0003.c:91939`), `sub_0008E4D0`
    (`recomp_0004.c:25608`, level script) and `sub_000BC700` (`recomp_0005.c:55228`) [V].
  * `sub_001A9C50` (`24529`), caller `sub_001B5D80` (`56327`, `56624`; match-state code, positions from `fighter+0xE8` of a fighter record): a
    weapon spawned by the fight itself.
  * `sub_001AA1D0` (`25321`): **equip**. Allocates a free entry (first with `+0` empty), writes `+0` = handle, `+4` = type
    (`sub_000BBB40`), `+0xC/+0x10` = owner slot, `fighter+0x8D0 = index`, `fighter+0x8D4 = weapon class`. It does not copy a matrix; the
    entry matrix stays identity while the weapon is held [V].
  * `sub_001AA330` (`25520`, release / throw / drop): copies the slot's matrix into `entry+0x34` (`sub_000BBB10`), zeroes the velocity vectors
    `+0x7C/+0x88/+0x94`, sets flags `+0x30 |= 0x18 | 0x20/0x40/0x80` [V].
* Who **reads** `*(entry+0x74)`: the candidate scan `sub_001AAAE0` (`26593`; iterates all 16 entries from `0x3BE7A8` = entry+0x30, distance
  fighter->`*(entry+0x74)` with `sub_001BBC10`, sorts), the picker `sub_001AB200` (`28010`; takes the first with distance < `0x29238C` and
  floor-height delta within `0x2C82C0..0x2A1180`; returns -1 if none), and `sub_001AAD40`. `sub_001CA990` (`106873`) sets state `0x69` only when
  `sub_001AB200 != -1` and the fighter holds no weapon (`+0x8D4` bit 0 clear) [V].

## 3. Who writes the position, and from which path (question 3)

* **Entry matrix** `entry+0x34`/`+0x64`: `sub_001A9BC0`, `sub_001A9C50`, `sub_001A9F10`, `sub_001AA010` (drop, `25010`), `sub_001AA330`, and
  the per-step physics `sub_001AB410` (`28376`) [V].
* **`sub_001AB410`**: loop over the 16 entries (start `esi = 0x3BE7F4`, stride `0xA4`), called once per step by `sub_001A4A30` (the
  per-fighter pass inside `sub_00087700`). For entries with the "airborne" flag it integrates velocity with **`0x3B8F2C`**
  (`recomp_0012.c:28425, 28463`), casts the floor ray `sub_001AC360`, collides (`sub_001AA4F0`), writes the new matrix with
  `sub_000BB9E0` and wakes it with `sub_000BBE80` [V]. `0x3B8F2C` is stored by `sub_001A4900` (`11066`, `11096`) from its argument `CLogic+4`
  (the match time scale) times constants; it is the value the question names as equal on all PCs. No wall clock is read [V].
* **Weapon slot matrix** `slot+0xC` (16 slots of `0xD0` at manager `0x376144 = CLogic+0x2C9E4`, `recomp_0004.c:21610`): written by `sub_000BA4C0`
  (callers `sub_000BB9E0`, `BC960`, `BC9C0`, `BCA10`, `BCAC0`, `BCC50`) and by the slot update `sub_000BAF90` (`recomp_0005.c:50396`;
  `dt * sub_00044C20()`; scripted lerp between `slot+0x58..` and `+0xA4..` by the clock `slot+0x50`, then `sub_00044DB0`/`sub_000BA870`;
  when owned it reads a model joint through `sub_00025100`). `sub_000BAF90` is called by the manager update `sub_000BCC50` (vtable `0x29EBF8`
  slot `+8`; vtable words read from the XBE: `7CFF0, BD290, BCC50, BC660`) [V].
* **Who calls the manager update**: the task subsystem `sub_0001B100` (`recomp_0000.c:30110`) iterates its list and calls `vtable+8(obj, dt)`;
  the manager is registered by `sub_0001B170` at `recomp_0004.c:18214`; `sub_0001B100` is called once per step from the screen update
  `sub_00079B60` (`recomp_0003.c:77917`) with `edi = dt` = the constant 1/60, after `sub_0008BE60` [V for the call path, I for "the weapon manager is in that list"].
* **Closure check**: upward call graph (`scripts/callers.py 000BA4C0 --depth 6`) from `sub_000BA4C0`: every chain ends in `sub_00087700`, the
  crowd update (`sub_00081170` via `sub_0008BE60`), `sub_0008BE60` arms (`sub_00089BC0`) and indirect vtable entries `sub_0008ED10`,
  `sub_00090E50`, `sub_000BD290`. **None is `sub_000796A0` or in its closure.** Callers of `sub_000BB9E0`/`sub_000BBE80`/`sub_000BBB10`
  likewise [V for direct calls; indirect: `sub_0008ED10`, `sub_00090E50` (level-script spawners, `recomp_0004.c:26551`, `32906`), `sub_000BD290` are
  vtable entries and their dispatcher was not read, U].
* **The crowd path**: the spectator's weapon is created by `sub_000BCAA0` -> `sub_000BCA10` at the spectator's static position with Y lowered
  (`crowd-lockstep-reset.md` section 7). It becomes a *fight* entry only when `sub_0007ED10` (case `0x7EF60`, `recomp_0003.c:91939`) calls
  `sub_000BBF30` at a clip-clock threshold (`MEMF(ebx+0x50)` against a per-weapon table, `recomp_0003.c:91590-91640`): that clock advances once per
  crowd update with the constant `dt` (`crowd-frame-timing.md`). If the spectator instead hands the weapon over (state `0x64`, `sub_001B93B0`) the
  weapon goes through `sub_001AA1D0` and the entry matrix stays identity, so state `0x69` is not used [V].
* Anything host-timed in these paths? Only candidates, none found: (i) model joint evaluation `sub_00025100` reached from `sub_000BA5D0` /
  `sub_000BAAA0` when a weapon is attached to a joint (its time source is the model's animation state, advanced by the step, not read here, U);
  (ii) `sub_000BCC50`'s particle calls (`sub_00091860/918B0`) are cosmetic.

## 4. What to do (question 4)

Evidence level: the code gives no host-timed position, so do not gate on the hypothesis yet.

1. **Measure first (cheap, no behaviour change).** Add to `fighter_hash` (`src/hooks/netplay.c`, part list near `:2375`) two ranges: the weapon
   entry table `0x3BE778..0x3BF1B8` and the weapon manager `0x376144..0x376E70` (`CLogic+0x2C9E4`, 16 x `0xD0` from `+0xC`). Log
   which part first differs and at which step. Expected result, by the elimination in finding 1: the table or the manager differs a few steps
   before `+0x81C`. The entry fields `+0x30` (flags), `+0x64..+0x6C` (position), `+0xC` (owner) tell whether it is a toss
   (`sub_000BBF30`), a floor spawn (`sub_001A9C50`) or physics (`sub_001AB410`).
2. **If it is the spectators' weapon** (position/flag differs for an entry with owner `-1` created from `sub_000BBF30`): make the offer
   deterministic or remove it. Minimal removal: replace `sub_0007EA70` (`thiscall`, plain `ret`, returns al; `g_eax = 0; g_esp += 4;`) so the
   crowd never creates a weapon; documented in `spectator-shove.md` section 6. Finer: only `sub_0007ED10`'s toss (hook `_enter`, cannot skip the
   body, so replace the function: the weapon would then stay "held" forever, so prefer removing the offer).
3. **If it is a floor weapon the fight picks up** and the table already differed at step 1: the cause is the weapon pool state (leftovers of the
   previous match or of the loading window, `crowd-lockstep-reset.md` section 7 and probe 3). Reset the pool with `sub_000BC060(ecx = 0x376144)`
   (`recomp_0005.c:53862`, clears all 16 slots and byte `+0xD28`) plus `sub_001A9BC0(i)` for the 16 entries, at the same place `crowd_reset` runs.
4. **Fight-side gate if only the pickup matters**: `sub_001AB200` -> `eax = -1; g_esp += 4` (cdecl, stack arg, plain `ret`; callers `sub_001CA990`
   and the CPU `sub_001AE0C0`). Then state `0x69` is never entered and `+0x81C` is never written from a weapon, at the cost of the pickup move.
   `sub_001CA990` then falls through to the other move choices [V for the fall-through, I for gameplay effect].
5. **No render-path fix is indicated**: nothing from `sub_000796A0` writes these structures, so there is no "advance" call to move.

## 5. Not read / open

* The flag conditions inside `sub_001AB410` (which entries count as airborne) and the body of `sub_001AA4F0` (collision) and `sub_001AC360` (floor ray: reads
  the stage collision data; static, but not opened) [U].
* The dispatcher that calls `sub_0008ED10`, `sub_00090E50`, `sub_000BD290` (indirect) and the level-script data that decides which weapons they place [U].
* The precondition of state `0x69` for the *CPU* (`sub_001AE0C0` -> `sub_001AB200`) and which venue constant selects the match-state branch of
  `sub_001B5D80` (`MEM32(0x3B9084)`) [U].
* Whether fighter 0 was in state `0x69` at the measured step: a state log (`+0x30`) at the step before would confirm it [U].

## Evidence index

| Item | File:line |
|---|---|
| State `0x69` handler `sub_001AAD40`; store of `+0x81C` | `recomp_0012.c:27073`; `:27155` |
| Handler table writes (`0x1AAD40` at `esp+0x1A8`, `0x1B6560` at `+0x1C8`) | `recomp_0012.c:55388`, `:55394` |
| `sub_001BBC40` (yaw from a point to a vec3 pointer) | `recomp_0012.c:70606` |
| Entry init `sub_001A9BC0` (`+0x74 = +0x64`) | `recomp_0012.c:24434` |
| Entry spawn `sub_001A9F10` / `sub_001A9C50` | `recomp_0012.c:24832` / `:24529` |
| Equip `sub_001AA1D0`; release `sub_001AA330`; drop `sub_001AA010` | `:25321`; `:25520`; `:25010` |
| Candidate scan `sub_001AAAE0`; picker `sub_001AB200` | `:26593`; `:28010` |
| Pickup decision `sub_001CA990` (`fighter+0x8D0 = idx`, state `0x69`) | `:106873`, `:106922-106925` |
| Per-step weapon physics `sub_001AB410` (uses `0x3B8F2C`) | `:28376` |
| `0x3B8F2C` stored by `sub_001A4900` | `:11066`, `:11096` |
| Weapon registration `sub_000BBF30`; its callers | `recomp_0005.c:53649`; `recomp_0003.c:91939`, `recomp_0004.c:25608`, `recomp_0005.c:55228` |
| Slot matrix getter `sub_000BA5D0`; slot update `sub_000BAF90`; manager update `sub_000BCC50` | `recomp_0005.c:48573`; `:50396`; `:56222` |
| Weapon manager ctor (vtable `0x29EBF8`); placement in CLogic | `recomp_0003.c:86330-86345`; `recomp_0004.c:21610` |
| Task subsystem update and its call | `recomp_0000.c:30110`; `recomp_0003.c:77917` |

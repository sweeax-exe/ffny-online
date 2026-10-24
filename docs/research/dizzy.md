# Dizzy / stun: what decides it, and whether difficulty changes it (static read)

Read-only study of the lifted C (`src/recomp/gen`), the disassembly (`D:\DefJam\data\analysis\tools_disasm_output\asm\text.asm`),
the XBE data and `game\assets\gamecore.xml`. Nothing was built or run. Addresses are guest addresses, `file:line` is in
`src/recomp/gen`. No game code is reproduced, only described.
Marks: **[V]** read in code or data, **[I]** inferred, **[U]** not settled.

## 0. Short answer

1. The game has no "dizzy" string. The mechanism is a per-fighter **stun meter** (`fighter+0x374`, cap `+0x378`) that is
   filled by **the health damage the fighter takes** and drained slowly. The "stunned" flag is `fighter+0x927` bit 2. [V]
2. Fill: every damage event (`sub_001A5DD0`) adds *exactly the health damage applied* to the meter (the stun scale
   argument is 1.0 at every call site). Cap: **0.5 x max health**. Drain: about 4 % of the cap per second at full health, 1.35 % per
   second at zero health (`gamecore.xml` "stun recovery rate 1.35"). So the meter is, to a first approximation, "damage taken
   recently" and a fighter is fully stunned after taking roughly half of its max health. [V]
3. **Difficulty (`Option.SetDifficulty`) does not touch any of that.** Its only effect on a fight is a **momentum-gain scale**
   (the Blazin meter, `fighter+0x364`), taken from `gamecore.xml` "momentum scale per difficulty level": human 1.45 / 1.35 / 1.25,
   CPU 1.00 / 1.20 / 1.25 for Easy / Normal / Hard. Momentum feeds damage only through a bonus term, so it can move dizzy only
   slightly, and **Hard gives humans the *lowest* momentum gain** (-7.4 % vs Normal, -13.8 % vs Easy), i.e. the opposite sign of
   the complaint. [V for the chain and the values, I for the size of the damage effect]
4. **The brief's premise was wrong:** `CLogic+0x30040` (guest `0x3797A0`+... = `0x379DA0`) is **not** the difficulty. It is the
   *restart/rematch counter* (`docs/research/crash-001B2480.md` section 9.1 had this right). It only lowers the **CPU** brains'
   skill by 5 per restart (table `0x2AE3A4`: 0, -5, -10, -15, -20, -25). Humans never read it. [V]
5. Nothing online-specific was found that changes stun for human fighters: online slots are all human (pad = slot, CPU flag 0), so the
   player tables are used for both. Remaining suspects are in section 7 (a failed `gamecore.xml` load would make dizzy easier;
   a double application of a hit is unproven either way).

## 1. Fields and states [V unless marked]

| Field (fighter record `0x3B92E0 + slot*0x12E8`) | Meaning | Evidence |
|---|---|---|
| `+0x350` / `+0x354` / `+0x35C` | health / max health / health ratio | `sub_001A50B0` (`recomp_0012.c:12297`), `sub_001A4F30` |
| `+0x364` | **Momentum** (Blazin meter) 0..100, starts at 20.0, cap 100 (98/99 snap = "can blaze") | `sub_001A5190` (`:12437`), init `sub_001A4F30` (`:12162`ff, `MEM32(esi+0x364)=0x41A00000`) |
| `+0x36C` | score-like counter (damage dealt as percent of victim max health, capped 100000000) | `sub_001A5DD0` tail |
| **`+0x374`** | **stun meter** | adds in `sub_001A5DD0`, decay in `sub_001A56C0` |
| **`+0x378`** | **stun meter cap = 0.5 x `+0x354`** | `sub_001A4F30` stores `max health x 0.5` (`recomp_0012.c:12156`; constant `0x286D2C` = 0.5) |
| `+0x927` bit 2 | **stun meter was full ("stunned")**; also stops the drain | set at `sub_001A5DD0` (asm `0x1A60C6`), tested in `sub_001A56C0` (`0x1A56C4`) and `sub_001B50D0` (`0x1B5158`) |
| `+0x390` | remaining stun time in steps | written by `sub_001B50D0` from `sub_001A59E0` |

Correction to `wall-stagger.md`: the "momentum band" that selects the hit-reaction level (`sub_001A6360`, `+0x374/+0x378`
against 0.2, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0) is the **stun ratio**, not momentum. The real momentum is `+0x364`. [V: the
same two fields are what `sub_001A56C0` drains with the "stun recovery rate" and `sub_001A5DD0` fills]

States: the stun state is **state `0x16`**, handler `sub_001B50D0` (`recomp_0012.c:54459`; table slot from the builder
`sub_001B54D0`, `:55249ff`, store of `0x1B50D0` into the 22nd slot; slot formula index = (esp offset - 4) / 4 after the push, checked
against `0x11 -> 0x1C5660`). It is entered by the end-of-animation dispatcher `sub_001B46B0` (`:52839`) when the finished animation's
category (`fighter+0x318`) is **4** (byte table `0x1B47CC`, jump table `0x1B47B4`). Which hit animations are category 4 is [U].
Neighbours: `0x15` = `sub_001DC400` (ground stun), `0x14` rope bounce `sub_001B51B0`, `0x1B` corner pin `sub_001DC120`, `0x19`/`0x17`
ground states (categories 2 and 3), `0x1B` also for categories `0x15`/`0x16`.

## 2. What fills the meter

`sub_001A5DD0(attacker, defender, 0, attackInfo, mult, 1.0, stunScale, 3)` (`recomp_0012.c:14261`; args are floats, the callers push
`0x3F800000`). Callers: 19 direct sites (`sub_001C6CD0` x3 = the normal hit path, `sub_001A63B0`, `sub_001A8AB0`, `sub_001B6670` x2,
`sub_001B94C0`, `sub_001D31D0` x2, `sub_001D5870`, `sub_001D5960`, `sub_001D7BD0`, `sub_001D8DC0` x3, `sub_001DACE0` wall slam,
`sub_001DB1B0`, `sub_001DC120`). I read the argument pushes of all 19: the stun scale (arg 6) is **1.0 everywhere**; `mult` (arg 4) is
1.0 except at the main hit path where it is a computed per-hit scale. [V]

Inside (asm `0x1A5DD0..0x1A6320`, lifted C `:14261-14980`):

1. Raw damage `S` = positive-only sum of (a) base damage `D` (from attackInfo) x attacker power slot `[+0x820 + 4*type]` x 0.02
   (all slots start at 50.0 so x1.0), (b) 0.05 x (attacker stat for the type - defender stat for the type) x `D` if positive,
   (c) a **momentum term** `(attacker.+0x364 - defender.+0x364) x 0.1` if positive, only when |(b)| >= 0.001 (so not for equal
   stats). [V structure, I on what "type" is]
2. Multiplied by `1 + 0.1 x clamp((attackerPower - defenderToughness) x 0.5, min -9)`, where attackerPower is the "upper power"
   stat (types 0-2), "lower power" (types 3-4) or their mean (flag `0x80000`), defenderToughness is the toughness stat, each
   expressed as `(stat - 5) x XML factor + 5` ("skill effects" in `gamecore.xml`: upper power 0.95, lower power 0.95, toughness damage
   decrease 0.80). [V structure; the XML-to-address mapping of the factors at `0x2F04BC..0x2F04D4` is [I]]
3. `health -= S x mult` through `sub_001A50B0`. Attacker score += `S x mult / victim max health x 100`.
4. **`stun += S x mult x stunScale`** (asm `0x1A609C-0x1A60BE`, `:14621-14639`). If `stun >= cap` then `stun = cap` and `+0x927 |= 4`. [V]

So the stun gain equals the health lost on every damage event, including wall slams and the other environment hits. [V]

## 3. What drains it

`sub_001A56C0` (`recomp_0012.c:13196`), once per fighter per step from `sub_001A4A30` (asm `0x1A4AFF`; the loop at `0x1A4AE0` runs each
active record once). Skipped entirely while `+0x927` bit 2 is set. Per step:

`stun -= (healthRatio x 0.02 + 0.01) x 1.35 x cap x (1/60) x stepScale`   (`stepScale` = `0x3B8F2C` = 1.0; 1.35 = "stun recovery rate").

Per second (60 steps): 4.05 % of the cap at full health, 2.7 % at half health, 1.35 % at zero health. A full cap drains in about 25 s
(full health) to 74 s (zero health). At or below zero the meter and `+0x37C` are cleared. [V]
Consequence: the meter is almost a running total of damage taken. Fully stunned = about half of max health taken. [V arithmetic, I as a gameplay statement]

When the stun state starts with the flag set, `sub_001B50D0` resets `+0x374` to 0, clears the flag and uses stun-time mode 7;
otherwise mode 6. [V]

## 4. How long the stun lasts, and the threshold ("valid-stun %")

`sub_001A59E0(fighter, other, mode)` (`:13701`) returns the stun time in steps. `ratio = +0x374 / +0x378`. Values come from
`gamecore.xml` (parsed in the gamecore loader around `recomp_0012.c:58050-59000`; targets below) and are lerped between min and max
by `(ratio - threshold) / (1 - threshold)`; **at or below the threshold the stun time is 0**. [V]

| Mode | Used by (state) | XML key | target | XML value (min s, max s, valid %) | XBE default (if the XML did not load) |
|---|---|---|---|---|---|
| 0 | `0x15` ground stun (`sub_001DC400`) | ground stun duration | `0x2F0384/88/8C` | 0.75, 2.0, **5 %** | 0.1, 1.0, 10 % |
| 1, 2, 3, 4?, 5? | corner, wall slam, rope (`1DC120`, `1DACE0`, `1B51B0`, `1B8F00`, `1D99E0`) | sticky point stun duration | `0x2F039C/A0/A4` | 0.6, 2.0, **0 %** | 0.5, 2.5, 10 % |
| **6** | **`0x16` stun, flag clear** (`sub_001B50D0`) | standing stun duration | `0x2F0390/94/98` | 0.5, 1.2, **30 %** | 0.3, 0.75, 10 % |
| **7** | **`0x16` stun, flag set (meter was full)** | none | n/a | (1 + health ratio) x 60 steps = 1.0 to 2.0 s | same |

(Modes 4 and 5 use their own formulas, not read in full: mode 4 is `ratio x 120 + 60` steps, mode 5 uses health values. [I])
So a standing stun from a non-full meter needs a stun ratio above 30 %; a full meter always gives 1-2 s. [V]
Step = 1/60 s (`0x296DDC` = 60.0 converts seconds to steps). The CPU-only extra multiplier `0x2EC034` (1.0 in the XBE) applies when
the game mode word `0x3B9078` is 2 and the fighter is CPU controlled. [V]

Hit-reaction severity (state `0x0E` flinch, `0x11`, `0x12`, `0x10`, `0x13`) also depends on the stun ratio band (the table in
`wall-stagger.md` section 2, with the corrected name), so a higher meter makes every later hit stagger harder. [V]

## 5. Does difficulty reach any of this?

### 5.1 The chain from the option to the fight [V]

`Option.SetDifficulty(d)` (handler `sub_00048BB0`, `recomp_0002.c:23233`) stores `d` in the byte at **profile+0x5E** (`*(*(this+0xC0C)+4)+0x5E`;
`Option.GetDifficulty`, `sub_00048B80`, reads the same byte). `Game.StartGame` -> `sub_00020270` (`recomp_0000.c:47588`) reads that byte
(`:47891`) and writes **0 / 1 / 2** (Easy / Normal / Hard) into four dwords `GS+0x198, 0x19C, 0x1A0, 0x1A4`, which are `setup+0xD8+4*slot`
of the setup block at `GS+0xC0` (`online-match-setup.md` section 2). `sub_000871F0` (`recomp_0004.c:7547`) copies them to the globals
**`0x3B90A8[slot]`**. The only reader of `0x3B90A8` in the lifted code and in the whole disassembly is `sub_001A5190`
(`recomp_0012.c:12483`, asm `0x1A51DC`), and it reads **slot 0's** entry (an absolute address; all four hold the same value).
Online automatch calls `Option.SetDifficulty(2)` first, so every online fight has index 2.

### 5.2 What `sub_001A5190` does with it [V]

`sub_001A5190(fighter, amount)` adds momentum to `fighter+0x364` (cap 100; reaching the top sets `+0x93E` bit 0, the Blazin-ready flag).
`amount` is multiplied by:

* the charisma-based factor of the fighter (`1 + stat x 0.1 x ...`, skipped when `+0x93E & 3`),
* **the per-difficulty scale**: player table `0x2F0360` if `fighter+0x937` bit 0 (CPU flag) is clear, else AI table `0x2F036C`,
  indexed by `0x3B90A8`,
* when the game mode word `0x3B9078` is 2, a second per-difficulty table (`0x2EC01C` human, `0x2EC028` CPU) - all 1.0 in the XBE.

Values (`gamecore.xml`, parse targets verified by the element-name strings `player` at `0x2C99F4`, `AI` at `0x2C99FC`):

| Difficulty (index) | Human momentum scale | CPU momentum scale |
|---|---|---|
| Easy (0) | **1.45** | 1.00 |
| Normal (1) | **1.35** | 1.20 |
| Hard (2) | **1.25** | 1.25 |

The game-mode word `0x3B9078` second table is 1.0 for all entries. [V from the XBE; the XML does not override it]

### 5.3 How momentum can reach stun [V structure, I size]

Momentum enters the damage only through the term (c) in section 2: `(attackerMomentum - defenderMomentum) x 0.1`, positive only,
up to +10 raw damage points when 100 vs 0. So the fighter with more momentum deals more damage and therefore fills the stun meter
faster. Lower momentum gain (Hard) means *less* of that bonus. Going from Normal (1.35) to Hard (1.25) cuts momentum gain by 7.4 %;
from Easy (1.45) to Hard by 13.8 %. Whether Hard online is "dizzier" than Normal offline is therefore **no**, by this path; at most
the effect is a few percent of the momentum term. How big the term is next to `D` needs the damage values of the moves
(`fighter.xml`/the move database, not read here) [U].

### 5.4 What does NOT depend on difficulty [V]

* Stun gain per hit (section 2, steps 1-4), cap, drain, thresholds, durations: no read of `0x3B90A8` or of the option byte.
* The AI brain: the lifted AI reads no difficulty either. `fight-determinism.md` line 67 names `CLogic+0x30040` as difficulty; that is
  the restart counter (section 6).
* Other readers of profile+0x5E seen (`sub_00015B80` `recomp_0000.c:15540`, `sub_00065910` `recomp_0003.c:23783`) compare it with 1
  in front-end/profile code, not in the fight [I].

## 6. The `CLogic+0x30040` question (the brief's premise)

* It is `this+0x30040` of the match object (`this = 0x349760`, so `0x379DA0`; note the brief's `0x3797A0` is a different address).
  Zeroed by the constructor `sub_0008CB90` and the full init `sub_0008B730` (`recomp_0004.c:17813`), incremented by the restart path
  `sub_000891A0` (`:12142`). Nothing else writes it. [V]
* Readers: `sub_000871F0` passes it as arg 0 of `sub_001A4D90` (`0` means "parse `main_all.dat`"); `sub_000891A0`/`sub_0008B730` pass it to
  `sub_000C8820` (`recomp_0005.c:91129`) which, **for CPU-flagged slots only** (`0x3B9088[slot] != 0`), sets the brain skill to
  `base + table[min(counter, 5)]` clamped to 1..100, table at `0x2AE3A4` = 0, -5, -10, -15, -20, -25 (as signed ints). A fighter whose
  `0x3B9278[slot]` equals `0x157C1` is skipped (gets 0 added). [V]
* So it is a "CPU gets weaker with each rematch" mechanism, unrelated to the Option difficulty and to humans.

## 7. Online-specific checks

* Online slots: `src/hooks/netplay.c` builds the request with `m.pad[i] = i` for the humans and `-1` (CPU, random fighter) only for empty slots
  (`netplay.c` around the `m.pad[i]` assignments, lines ~992-1004). Humans get CPU flag 0, so the player momentum table and no
  CPU-only multipliers apply. [V]
* Fight step, stun add and drain are not frame-rate dependent: the add happens at the hit; the drain runs once per fighter per step with
  `stepScale = 0x3B8F2C` (1.0, 0 when paused, the slow-motion ramp at the end). Per-step cap is 4 steps per presented frame; steps are
  not duplicated. `sub_001A56C0` has exactly one call site (`sub_001A4A30`). [V]
* Double application of one hit (`sub_001A5DD0` called more than once per swing) is the same open question as Gate C in
  `wall-stagger.md` (suspect 2). Not proven either way; it would show as the stun meter rising by more than the health lost, or as
  health lost twice. [U]
* If `gamecore.xml` is not applied at runtime, the code falls back to the XBE defaults in section 4: stun recovery 1.0 instead of 1.35
  (slower drain), standing threshold 10 % instead of 30 % (a stun after much smaller damage), ground threshold 10 % instead of 5 %,
  shorter durations, and the momentum scale 1.0 instead of 1.45/1.35/1.25. The loader is the big keyword parser around
  `recomp_0012.c:58050`ff. I found no lifter error in its shape, but its float conversion goes through `sub_00200EAD` and the 3-float
  helper `sub_0002BC70`; not verified at runtime. [U]
* The remote player's stats (power, toughness, health slots `+0xB34..+0xB4C`) come from the same stat block on both PCs; a different
  block on either side would change max health (`176 + 16 x health stat x factor`), the stun cap (half of it) and the damage. [I]

## 8. Measurements that would settle it (no code needed beyond peeks)

Use `RECOMP_PEEK` / `scripts/peek-guest.py` (slot 0 record `0x3B92E0`, +0x12E8 per slot):

1. `0x2F0378` should read 1.35, `0x2F0390..0x2F0398` 0.5 / 1.2 / 30.0, `0x2F0360..` 1.45 / 1.35 / 1.25. Defaults (1.0, 0.3 / 0.75 / 10.0)
   mean the XML did not load.
2. `0x3B90A8` should read 2 online (the value of `Option.SetDifficulty`). Anything else means the option byte route in 5.1 is not what
   the fight sees.
3. Per fighter: `+0x354` (max health), `+0x378` (should be exactly half), `+0x374` per hit (rise should equal the health drop),
   `+0x364` (momentum), `+0x927` bit 2. Compare an offline hit and an online hit on the same fighters and move.
4. Count entries of `sub_001A5DD0` per swing (`RECOMP_WATCH_EXEC`) online versus offline.

## 9. Addresses and lines (quick index)

| What | Guest | File:line |
|---|---|---|
| damage + stun add | `sub_001A5DD0` | `recomp_0012.c:14261`, stun add `:14621-14639` |
| stun drain | `sub_001A56C0` | `recomp_0012.c:13196` |
| cap / init | `sub_001A4F30` | `recomp_0012.c:12095`, cap `:12156` |
| stun time | `sub_001A59E0` | `recomp_0012.c:13701` |
| stun state 0x16 | `sub_001B50D0` | `recomp_0012.c:54459` |
| end-of-anim dispatcher | `sub_001B46B0` | `recomp_0012.c:52839` |
| momentum add + difficulty scale | `sub_001A5190` | `recomp_0012.c:12437`, `0x3B90A8` read `:12483` |
| option set / get | `sub_00048BB0` / `sub_00048B80` | `recomp_0002.c:23233` / `:23200ff` |
| option -> setup block | `sub_00020270` | `recomp_0000.c:47588`, writes `:47900-47928` |
| setup -> globals | `sub_000871F0` | `recomp_0004.c:7264`, `0x3B90A8` store `:7547` |
| restart counter / AI penalty | `sub_000891A0`, `sub_000C8820` | `recomp_0004.c:12142`, `recomp_0005.c:91129` |
| gamecore XML keys | momentum scale `0x2C99A8`, stun recovery `0x2C9A00`, ground `0x2C9A4C`, standing `0x2C9A64`, sticky `0x2C9A7C` | parser `recomp_0012.c:58050ff` |
| XML file | `game/assets/gamecore.xml` lines 16-27 | |

# How the foreground spectators reach the fighters (static read)

Read-only study of the lifted C (`src/recomp/gen`), the disassembly under `D:\DefJam\data\analysis`
and the XBE data tables. Nothing was built or run. Addresses are guest addresses.
Confidence per claim: **[V]** read in code, **[I]** inferred from shapes or names, **[U]** not settled.
Lifted-C references are `file:line` in `src/recomp/gen`.

## Short answer

1. There is no collision or push against spectator bodies. The only channel from the crowd to a fighter is a
   table of 0x34-byte "audience public" records at `*(0x3494E4)`, count at `0x3494E0` [V]. The crowd writes
   each interactive spectator's position, heading and a status code into its record every update; fighter code
   scans the table, and when a spectator is in range and has a suitable status the fighter changes state
   (`0x5E, 0x5F, 0x60, 0x64, 0x74` and others), turns toward the spectator and is moved by a scripted lerp
   whose target is the spectator's published position [V].
2. The "shove" in the earlier note is therefore not a shove but this handshake: crowd-interaction states
   (catch, hold, throw back, take a weapon). Whether a fighter enters them, when, and where it ends up depends on
   the spectator's animation state and position on that frame [V for the dependency, I for the names].
3. There is a second, sharper desync path that was not in `crowd-nondeterminism.md`: the fighter-side entry
   point `sub_001B95F0` makes a **match-generator draw** (`sub_001BC730`, `0x3C1404`) only when a spectator is
   a candidate and an angle test passes (`recomp_0012.c:65172`) [V]. A different candidate set on one frame
   changes the fight's own draw count, and everything after it shifts. This explains a large, sudden fight
   divergence better than a small positional nudge.
4. Narrowest gate: make the record count read zero (`MEM32(0x3494E0) = 0`) before the fighter code reads it.
   One store, from an `_enter` hook. It makes the scan (`sub_001B8760`), the CPU target scan (`sub_001ADD50`) and
   the crowd's own publish loop (`sub_0007F030`) all do nothing. Details and residual risks in section 6.

## 1. The record table (the whole interface)

`rec(i) = *(u32*)0x3494E4 + i*0x34`, `count = *(u32*)0x3494E0`. Allocated and indexed in `sub_00080C10`
(`recomp_0003.c:116355`; the allocation is `sub_000296B0(count*0x34, "CCrowdControll::InitAudiencePublic...")`,
count = number of members for which `sub_0007D760` is true, i.e. member type 0..3) [V]. Each public member
gets its record number in `member+0x0C`, other members get -1 [V].

| Off | Meaning | Written by | Read by | Conf |
|---|---|---|---|---|
| +0x00..0x0B | spectator position (copy of `member+0x20..0x2B`, the animation root sampled by `sub_0007D560` via `sub_00084260`) | `sub_0007F030` every update (`recomp_0003.c:92161`) | fighter scan, AI scan, weapon system | V |
| +0x10 | spectator heading (copy of `member+0x38`) | `sub_0007F030` | `sub_001B95F0` (via `sub_001BAC30`) | V |
| +0x18 | status code derived from the member's animation state `member+4` (switch, table `0x7F3A0`/`0x7F358`; 1/2 = idle, available; 4; 5/6 = pair hold (6 when the partner `member+0x174` has status 7); 7; 8 = offering a weapon [I]; 9, 0xB, 0xC, 0xD = busy). Members of type 2/3 are clamped to 0xD whenever the code would be <= 4 (`recomp_0003.c:92542` ff.) | `sub_0007F030` | `sub_001B8760`, `sub_001B8BA0`, `sub_001B8F00`, `sub_001B95F0`, `sub_001CE2A0`, `sub_001CF800`, `sub_001ADD50` | V (codes), I (names) |
| +0x1C | request code from a fighter (1..0x21), cleared to 0 by `sub_0007F030` each update | fighters through `sub_0007F900(idx, code, slot)` / `sub_0007F920(idx, code, slot, f32)` | `sub_00080A00` (`recomp_0003.c:116045`..) | V |
| +0x20 | fighter slot the spectator is engaged with (also copied from `member+0xC8` by `sub_0007F030`) | fighter requests, `sub_0007F030` | `sub_0007E940`, `sub_00080A00` | V |
| +0x24 | float handed over with a request (heading or distance) | `sub_0007F920` | `sub_00080A00` -> `sub_0007D620` | V |
| +0x28 | 1 if `member+0xDD == 1` (set once at init) | `sub_00080C10` | `sub_001B8A80` | V |
| +0x2C | member index | `sub_00080C10` | `sub_0007F030`, weapon system | V |
| +0x30 | weapon handle handed out by this spectator (result of `sub_000BCAA0`), -1 if none | `sub_00080A00`, `sub_0007E940`; cleared by `sub_001B93B0` | `sub_001B93B0`, `sub_0007F030` | V |

Companion data, fighter -> crowd (the crowd's view of the fight; it never writes fighter memory):
`sub_001B8330` snapshots, per slot, body position (record+0xE8 with Y from +0x48) to `0x349508 + 0xC*slot`,
rotation to `0x349538 + 0xC*slot`, and a status (1..4 from bits of record+0x928, 6 = slot unused) to
`0x349568 + 4*slot` [V]. `sub_00080D40` and `sub_0007E940` / `sub_0007EA70` read them.
Fighter-side per-fighter fields used for the binding: `+0x890/+0x894` (record numbers of the two nearest
candidates, -1 none), `+0x898..+0x8AF` (two copied positions), `+0x8B0/+0x8B4` (heading as angle),
`+0x8B8/+0x8BC` (target yaw), `+0x8C0` (timer), `+0x8C4` (accumulated time, cleared on a failed attempt) [V].
A per-record "claimed" byte array lives at `0x3C13C8 + i` (set by fighter code on binding, tested by the scan) [V].
Global byte `0x3B8F20` gets bits `0x12` when the scan sees a status-8 spectator (`recomp_0012.c:62452`) [V];
its readers were not followed [U].

## 2. The spectator side: creation, update, identity

* Class: the crowd manager (called `CCrowdControl` in the strings). Its vtable is at `0x29EBE8`:
  `+4 = sub_00081240` (load/init), `+8 = sub_00081170` (update, one call per crowd update), `+0xC = sub_0007E6A0`
  (release, zeroes `0x3494E0/E4`) [V]. The object's base is `0x375150` [I]: the generator at `0x375F68` is
  `manager+0xE18` (`sub_00080ED0` stores that address into every member at `member+0x178`) [V].
* Creation: `sub_00081240` -> `sub_00080ED0` walks the arena XML (`crowdposition` / `crowd` nodes) and for each node
  allocates a 0x17C-byte member (`sub_00029680(0x17C, "CCrowdControl::InitXMLCharactor::m_pWork")`, constructor
  `sub_0007DFE0`), stores it at `manager+0xA90+4*n`, count at `manager+0x18`, then `sub_0007DD10` fills it from XML
  (`recomp_0003.c` near `sub_00080ED0`). At the end each member gets a random initial speed factor and initial
  state from the **global** generator `0x33DB5C` (`sub_0002A770`), see section 5 [V]. Then `sub_00080C10` builds the
  record table and the partner links (`sub_0007E830`, `member+0x174`).
* Per update (`sub_00081170`, `recomp_0003.c:117233`): skipped when `manager+0x20 & 1` and `byte 0x349500 == 1`;
  `sub_00080D40` (reads the four fighters' joint positions through `sub_000445A0` and distances); then for each
  member `m = (manager[0x1C] + i) mod manager[0x18]` (so the processing order **rotates** by one position
  every update: `manager[0x1C]` is decremented at the end, `recomp_0003.c:117372`): `sub_00080A00` (react: consume a
  fighter request or pick a reaction) then `sub_0007D560` (advance/sample the animation, writes `member+0x20/0x2C`),
  `sub_0007D8F0` (for public members when not disabled) ; finally `sub_0007F030` publishes all records [V].
* Who sets the animation: `sub_0007D2F0(member, state, arg)` is the only place that picks a clip: `member+4 = state`;
  for state < 5 it picks the variant from a per-(type,state) name list at `0x29F0A0` (8-byte rows: count, pointer),
  retrying until it differs from the last pick (`member+0x10/0x14`), so the number of draws is itself random; for
  state >= 5 the name comes from `0x2E0854`; the clip name is `V2CA_%s`. Mirror flag `member+0x120` comes from a 50 percent draw [V].
  `sub_0007D4B0` returns a finished clip to an idle state. Clip end is detected by `sub_0007D870` against the clip
  length (`sub_00084480`) [V].
* Telling a spectator from a fighter: they are different object families and never mixed in one list. Fighters
  are the 0x12E8-byte records at `0x3B92E0 + slot*0x12E8` (slot at +0x28, state at +0x30, phase at +0x34).
  Spectators are 0x17C-byte members under the crowd manager, `member+0` is the type (0..3 public/interactive,
  `sub_0007D760`; 0xC and 0xD are special in `sub_00080ED0`) [V]. They meet only through record numbers and slot
  numbers. In the weapon system an owner id >= 4 means "held by crowd record owner-4" (`sub_000BA550`) [I].

## 3. Spectator data into fighter state: the exact places

All [V] unless stated. Order is the order of execution in one fighter attempt.

A. **Scan** `sub_001B8760` (`recomp_0012.c:62328`; register call: eax = mode, ecx = fighter, `[esp+4]` = int[2] out).
   Modes 2..4 first require the fighter-only test `sub_001B45F0`. Then for each record `i < *0x3494E0`
   (`loc_001B87E0`..`loc_001B885F`, lines 62418-62500): skip when `byte 0x3C13C8+i` is set; accept only when
   `rec.status <= 3` or `== 8` (62429-62440); require `|fighter.y(+0x48) - rec.y| < const 0x289478`; take the
   distance `sub_001BBC10(fighter+0xE8, rec.pos)` and keep it when under 10.0 (13.0 when `0x3B9084 == 0xE`, the
   venue constant at `esp+0x14`); sort ascending, return the two nearest. Returns the count (0, 1, 2).
   **Spectator-dependent decision #1: which spectators are candidates.**

B. **Binding** `sub_001B95F0` (`recomp_0012.c:64780`; stack: fighter, mode). Returns 0 at once if the scan found
   nothing (`loc_001B9CB7`: clears `+0x8C4`). Otherwise `loc_001B9630`..`loc_001B9678` (64837-64880) copies, for
   the two candidates: `fighter+0x890+4i = candidate`, `fighter+0x898+0xC*i = rec.pos` (the spectator's position
   goes straight into the fighter record here), `fighter+0x8B0+4i = sub_001BAC30(rec.+0x10)`.
   Then it decides by mode (callers: mode 0 `sub_001CB130` (state 1); mode 1 `sub_001BE2F0`/`sub_001BE340`
   (states 6 and 0x57); mode 2 `sub_001DBC30` (state 0x36); mode 3/4 `sub_001C5660`, `sub_001C5870`,
   `sub_001D3800`, `sub_001D41D0`, `sub_001D90C0` (states 0x11, 0x12, 0x1F, 0x2E, 0x31)):
   * mode 3/4: angle between the fighter's yaw and the spectator heading below const `0x292394`, then a chance
     `p = (fighter+0x374 / fighter+0x378) * const` through `sub_001BC730` (**match-generator draw**, line 65172).
     On success: request code `0xD` to the record (`sub_0007F900`), `sub_001B3CA0(fighter, 0x60)`, claim the record.
     Otherwise, if `rec.status == 8`, a second angle test and `sub_001B3CA0(fighter, 0x74)` (take a weapon).
   * mode 1/0: `sub_001B8BA0` (weapon take: needs `rec(+0x890).status == 8`, `fighter+0x8D4` bit 0 clear, angle test,
     request `0x1C` -> state `0x64`), else request `0x21`/`0x22` with angles -> state `0x5F`, flags `fighter+0x920 |= 1/4/8`.
   * mode 2: state `0x5E`/`0x5F`/`0x74` by `rec.status == 8` and angle tests.
   **Spectator-dependent decisions #2: whether and which interaction state is entered; #3: whether a match-generator draw happens.**

C. **The state itself moves the fighter.** States entered above (handler table built in `sub_001B54D0`,
   `recomp_0012.c:55249`; state number = (offset-4)/4 in that table):
   `0x5E sub_001B8C80` (turn toward `+0x8B0`), `0x5F sub_001B8D50`, `0x60 sub_001B8F00`, `0x61 sub_001B9170`,
   `0x62 sub_001B92D0`, `0x63 sub_001B9350`, `0x64 sub_001B93B0`, `0x74 sub_001B94C0`, and `0x18 sub_001B39F0`
   (sends request `0x10` to the bound record). Each calls `sub_001C44B0(fighter, ...)` once. For states
   `0x5F, 0x60, 0x64, 0x74` its crowd cases (`recomp_0012.c:91081, 91138, 91180, 91206`) take the **target
   position from `fighter+0x898` (the spectator's published position)** and the yaw target from `+0x8B0/+0x8B8`,
   compute a frame count and call `sub_001CA070`, which stores `(target - position)/N` in `fighter+0x860`; the yaw
   step goes to `fighter+0x870`. Every following frame `sub_001C4440` calls `sub_001C9F30`, which adds `fighter+0x860`
   to the fighter's position `+0x44..+0x4C` and to all 20 joint copies, and adds `+0x870` to the yaw `+0x54`.
   **This is the place where spectator-derived data physically moves a fighter.** (Source: `sub_001C44B0` at
   `recomp_0012.c:90333`, `sub_001C4440` at 90237, `sub_001C9F30` at 105148, `sub_001CA070` at 105302.)

D. **Phase gating on spectator status.** State `0x60` (`sub_001B8F00`): every frame it adds the step scale to
   `fighter+0x8C0` and tests `rec(+0x890).status == 6` (`loc_001B8FFC`, `recomp_0012.c:63690-63723`); if so it plays
   anim `0x272C` and switches to state `0x63` (the held phase) [V]. Otherwise it sends requests `0xE`, `0xF`, `0x10`
   at later phases. Reading: the spectator pair has caught the fighter and holds him [I]. State `0x61` picks a throw-back
   animation with the match generator (`sub_001BC770`, retry until different from the last pick), then requests `0x2722-ish`
   codes; `0x63` requests `0x1B` and bumps a stat counter through `sub_001A6870`.

E. **Weapon hand-over.** State `0x64` (`sub_001B93B0`, `recomp_0012.c:64399`): reads `rec(+0x890).+0x30`
   (the weapon handle the spectator created), equips it with `sub_001AA1D0(fighter, 1, 0, handle)`, posts the
   weapon event through `sub_000B0170(slot, table[+0x8D0])`, and writes -1 back into `rec.+0x30` [V].
   The handle comes from the crowd: `sub_00080A00` calls `sub_000BCAA0(weapon, &pos)` at `recomp_0003.c:116256`
   after `sub_0007EA70` (decision) and `sub_0007EC00` (choice) say yes, and `sub_0007E940` does the same from state 0xE.

F. **CPU fighter.** `sub_001ADD50` (`recomp_0012.c:35150`; stack args, plain `ret`, returns al) scans all records,
   keeps the nearest with `rec.status == 8` (`sub_001CE2A0`) and writes its position as the CPU's target, with the
   record number or'ed with `0x100`; `sub_001CF800` (`recomp_0012.c:120516`, called from the AI entry
   `sub_001B0290` and `sub_001AE0C0`) revalidates that target and drops it when the status is no longer 8 [V].
   The CPU therefore walks toward a spectator who is offering a weapon, on a timeline set by the spectator's clip.
   This fits the earlier finding that the first field to differ was the CPU fighter's position [I].

What was **not** found: any write of fighter health, velocity or flags from the crowd range (`0x7B000-0x86000` writes
only to `0x3473xx` and `0x3494xx`), any collision test against a spectator, any hit delivered by a spectator.
Health is touched only through the ordinary fight code after a state change (`sub_001A6870` calls in the states above
are stat counters, [I]).

## 4. Fighter -> spectator requests (for completeness)

Codes written with `sub_0007F900/F920` (seen: `0xD` run into the crowd, `0xE/0xF/0x10` during the catch,
`0x1B`, `0x1C` take weapon, `0x1D`, `0x21`). `sub_00080A00` maps a code to a spectator animation state with
`sub_0007F740` (jump table `0x7F7F8`/`0x7F838`; e.g. 0xD -> state 14, 0x1C -> state 0x26, or 0x2A for type 3) and calls
`sub_0007D2F0`. Spectators also react by themselves: with a fighter within 10.0 they pick state 7 or 8 from the
global generator (`sub_0007F740`/`sub_00080A00`), turn toward him (`sub_0007D620`), etc.; these affect only the
spectators [V].

## 5. Why a different spectator timeline reaches the fight (chain)

1. The only random choices are the clip variant and mirror flag in `sub_0007D2F0`, and the retry loop makes the
   draw count itself variable. Draws come from the manager's generator (`0x375F68`), so a different draw count
   moves every later variant for every member.
2. A different variant is a different clip length and root path. `member+4` (state) returns to idle when the clip
   ends (`sub_0007D870`, `sub_0007D4B0`), so the frame on which a spectator is again `status 1/2` (available) or
   `8` shifts. The record position (`member+0x20`) is the clip's sampled root, so it also differs.
3. The processing order of members rotates with the update count (`manager[0x1C]`); one extra or missing crowd
   update changes the order in which members draw, even with identical seeds [V for the rotation, I for the effect].
4. Fighter-side the scan, binding, state choice, the spectator-derived lerp target, the CPU target and the
   match-generator draw at `recomp_0012.c:65172` all read the table (section 3). Any frame on which the two PCs
   disagree about a candidate changes a state, a position or the match generator's draw count.
5. Extra generator not covered by `fight-determinism.md`: **`0x33DB5C`** (`sub_0002A770` -> `sub_0002A390`, a
   counter-style generator seeded by `sub_0002A720` from `sub_00011950` / `sub_00011E40`) is drawn by the crowd in
   `sub_0007E170`, `sub_0007EA70` (the weapon-offer trigger: `rand() % 10 == 0`), `sub_0007F740`, `sub_00080A00`,
   `sub_00080ED0` and the `sub_0007FDA0` family, and by about 60 other callers (`0x20D70..0xCF6B0`). No function in
   the fighter range `0x1A0000-0x1E0000` calls it directly, so this is not a proven fight input, but nobody counts
   its draws yet (`sub_0002A770_enter` would show it) [I].

## 6. The gate

### Recommended: zero the record count (hook, no function replaced)

```c
/* in recomp_manual.c, only when a netplay session is active */
void sub_00081170_enter(void) { MEM32(0x3494E0) = 0; }   /* crowd update, every update */
void sub_001B8760_enter(void) { MEM32(0x3494E0) = 0; }   /* in case a fight step precedes the first crowd update */
void sub_001ADD50_enter(void) { MEM32(0x3494E0) = 0; }
```

What it does [V from the loops]: `sub_001B8760` reads the count after its mode-specific test and returns 0 when it is
<= 0, so `sub_001B95F0` returns 0 at `loc_001B9CB7` for every mode; `sub_001ADD50` returns false at once; `sub_0007F030`
returns before its loop, so the table is frozen at its initial content. No request code is ever written, so spectators
never receive a fighter command. Fighters keep every move and all of their own physics. The count is set only in
`sub_00080C10` (init) and cleared in `sub_0007E6A0` (release), so nothing re-raises it mid-match. Keep the original
count in a static before the first zeroing if the table must be cleared or restored.

What it breaks, visually and in play: fighters can no longer be caught, held or thrown back by the crowd, cannot
run into spectators with the crowd reacting, cannot take weapons from spectators, and the CPU no longer walks to a
weapon-offering spectator. Spectators still animate, cheer, turn toward close fighters and are drawn, just without
touching the fight. Offline play changes too unless the hook is gated on the netplay session.

Residual risks, to check before trusting it:
* State `0x60` is also entered by the hit handler `sub_001C6CD0` (`push 0x60` at `0x1C7415`, move flag in the high
  byte of the hit word) without the scan. It would then use `fighter+0x890/+0x898` as left by an earlier attempt
  (never bound with the gate: the field's initial value) and `rec(+0x890)` from a frozen table. That is deterministic
  on both PCs if the table content is, but it can move the fighter toward a bad target. Zero the table once (count*0x34
  bytes) right after `sub_00080C10`, and test a ring-out style hit near the crowd [U].
* The crowd can still spawn weapons by itself: `sub_00080A00` -> `sub_0007EA70` / `sub_0007EC00` -> `sub_000BCAA0` runs
  independent of the count and creates a weapon object (`sub_000BCA10`, the same spawner the fight uses at `sub_001A9C50`).
  A spectator-held weapon sits at a spectator-dependent position; whether a fighter can pick it up through the weapon
  system's own proximity code is not settled [U]. If it can, also make `sub_0007EA70` return 0 (see below).
* The global generator `0x33DB5C` stays desynced between the PCs (section 5, point 5). Harmless only if nothing
  in the fight reads it.

### Alternatives

* Replace `sub_001B8760` outright: `g_eax = 0; g_esp += 4;` (register args, caller-clean plain `ret`, so only the
  return address is popped; `sub_0001A4E0` in `recomp_manual.c` shows the replacement style). Same effect as the count
  for human fighters; leaves the CPU scan, so replace `sub_001ADD50` the same way (`g_eax = 0; g_esp += 4;`). Cannot
  be done with `_enter` hooks (they cannot skip the body).
* Replace `sub_0007EA70` (`thiscall`, plain `ret`, returns al): `g_eax = 0; g_esp += 4;` stops the spectators from
  offering weapons. It does not touch fighters directly and is safe for the fight; the cost is that the crowd never
  hands out a weapon.
* Not recommended: replacing `sub_0007D2F0` or `sub_00080A00` (they own the animation state and the clip clock,
  the crowd would freeze or crash); gating `sub_0007F030` alone (fighters would then read a stale but deterministic
  table, however the finder still runs and the first stale state decides).

## 7. Experiments that would settle the open points (not run)

1. Hook `sub_001B8760_enter` and log per fighter step: mode `g_eax`, slot `MEM32(g_ecx+0x28)`, `MEM32(0x3494E0)`, and
   a checksum of the table (`count*0x34` bytes at `*(0x3494E4)`). Compare two pinned runs: the first step where the
   table checksum differs, versus the first fighter field that differs (step 871 in the old measurements).
2. Hook `sub_001BC730_enter` and count calls whose return address `MEM32(g_esp) == 0x001B9849` (the draw in
   `sub_001B95F0`); a different count between runs is the second path of section 5.
3. Log fighter `+0x30` (state) when it enters `0x5E..0x64, 0x74`; this directly shows the interaction.
4. Run the gate of section 6 on the same pinned fixture; if the stream no longer parts at 871 the causal link is shown.
5. Add `sub_0002A770_enter` to the draw probe (generator `0x33DB5C`), and a log of `manager[0x1C]` (`0x375150+0x1C`
   [I]) at the first step: it shows whether the two runs had the same number of crowd updates.

## 8. Not read

`sub_001B45F0` (the mode 2..4 pre-test), `sub_001BA9B0`, `sub_001B4930`, `sub_001AA1D0` bodies, `sub_001C6CD0`'s
condition for state `0x60`, the animation data (clip lengths per variant), `sub_0007D8F0`, `sub_0007F3D0`, where
`sub_001B8330` and `sub_00081170` are called from in the frame (indirect), and the weapon system's use of owner ids
(`sub_000BAF90`, `sub_000BD860`). The state names above are guesses from behaviour; the numbers and offsets are exact.

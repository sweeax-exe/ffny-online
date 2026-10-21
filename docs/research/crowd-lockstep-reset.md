# Foreground crowd: how to make it identical on both PCs from the first fight step (static read)

Read-only study of the lifted C in `src/recomp/gen`, the XBE data tables and `src/hooks/netplay.c`.
Nothing was built or run. Addresses are guest addresses. Lifted-C references are `file:line`
(`recomp_0003.c` unless another file is named). Confidence per claim: **[V]** read in code,
**[I]** inferred from shapes, **[U]** not settled.

Builds on `spectator-shove.md` and `crowd-nondeterminism.md`. Three of their statements need correcting
(section 1); one new finding changes the plan (section 4).

## Short answer

1. **A game routine that restarts the crowd exists and is field-only: `sub_00080880`** (thiscall, ecx = manager,
   no stack arguments, plain `ret`). The game itself calls it when a match starts on an arena that is still loaded
   (`sub_0008BB00` -> `sub_000891A0` -> `sub_00080880`, recomp_0004.c:12280). It resets every member, the weapon-offer
   schedule and the crowd "intensity", re-picks every member's idle clip, and clears the records' weapon handles. It
   allocates and frees nothing [V]. The load (`sub_00081240`, vtable slot 1), the release (`sub_0007E6A0`, slot 3) and the
   record builder (`sub_00080C10`) allocate and are not safe mid-match [V].
2. **It is not enough on its own.** It does not reseed the crowd's generator, does not reset the rotation index
   `manager+0x1C`, and does not touch each member's speed factor `member+0x68`. Worse, **the crowd also draws the
   game's global generator at `0x33DB5C`, which is seeded from the wall clock at boot on each PC** (section 4). That
   alone makes two PCs disagree whatever the timing, and netplay never touches it.
3. Recipe (a): at step 1 reseed generator `0x375F68`, call `sub_00080880`, set `manager+0x1C = 0`, overwrite
   `member+0x68` with a shared value, re-publish with `sub_0007F030`; and run the crowd update on a **private copy of
   the `0x33DB5C` state** (wrapper around `sub_00081170`). Recipe (b) does the same by hand without the XML reader.
4. The manager is found with no search: inside `netplay_on_step` (entry of `sub_00087700`) `g_ecx` is the arena object
   `0x349760`, and the manager is `g_ecx + 0x2B9F0 = 0x375150`, its generator `0x375F68 = manager + 0xE18`.
5. The weapon a spectator creates uses only a pool index (first free of 16 slots) and the crowd's private generator;
   the 1-in-10 offer trigger uses the **global** `0x33DB5C` generator, so it is PC-specific today.

## 1. Corrections and layout

* **The manager is a sub-object of the arena, not a free-standing global.** Fight arena object ("A"): static at
  `0x349760`, constructed by `sub_0008CB90` from the C++ static-init stub `sub_002102D0` (recomp_0015.c:15388,
  `ecx = 0x349760`). `sub_0008CB90` stores the crowd vtable `0x29EBE8` at `A + 0x2B9F0` (recomp_0004.c:21612) and
  seeds at `A + 0x2C800/0x2C804`. So manager = `0x349760 + 0x2B9F0 = 0x375150` [V]. The other scene class ("B", scene
  type 7: `sub_0007D010` from `sub_002102B0`, container `0x3473F0`, manager at `+0x400 = 0x3477F0`, generator
  `0x348608`) uses the same crowd code [V]. `sub_00044210` (recomp_0002.c:9772) picks between them from
  `*(sub_00012CF0(0x2FB148, 9) + 0x2A0)`: 1 -> `0x375150`, 7 -> `0x3477F0` [V]; scene dispatcher `sub_0002DFF0`
  (recomp_0001.c:24250) uses the same field to choose A (case 1) or B (case 7) [V].
* **Vtable `0x29EBE8` has four slots** [V from the data]: `+0 sub_0007CF20` (dtor), `+4 sub_00081240` (load),
  `+8 sub_00081170` (update, one stack arg), `+0xC sub_0007E6A0` (release). The next words (`0x7CFF0`, ...) belong to
  another vtable.
* **The record position and heading are static XML placement, not animation output.** `member+0x20..0x2B` (position) and
  `member+0x2C..0x37` (rotation) are filled once from the arena XML by `sub_0007DD10` (88590; attributes parsed into
  `ebx+0x20` / `ebx+0x2C`, Y of the rotation is wrapped to +-180 and copied to `+0x38` and `+0x3C`) [V]. `sub_0007D560`
  (87306) and `sub_00084260` / `sub_00084190` read them as **inputs** to place the model (ground height from the noise
  object at `0x39C0E0`, visual only) [V]. `sub_0007F030` publishes `rec.pos = member+0x20..0x2B` and
  `rec.heading = member+0x38` (92170-92176) [V]. No store to `member+0x20..0x38` exists anywhere in
  `sub_0007CEA0..sub_00081330` except the XML read [V for that range; I elsewhere]. So the earlier note's "the record
  position is the clip's sampled root, so it also differs" is **wrong**: position and heading are identical on both PCs
  already. What differs is the **status code**, i.e. which animation state a spectator is in on a given step.
* The crowd update's step is `dt = [0x3BDE80] * arg` (`sub_00081170`: `sub_00044C20()` returns `*(float*)0x3BDE80` while the
  arena state `*(u32*)0x376F80` is 5 or 8, recomp_0002.c:11747; multiplied by the stack arg, 117180ff). `0x3BDE80` is the
  first word of the fight-step snapshot that `sub_001A2F60` fills from `0x3B8F2C`, the fighters' game-speed scale
  (1.0 by default, changed by slow motion) [V], so it is fight state and equal on both PCs [I]. The stack arg is the frame
  delta handed down `sub_00077A40 -> sub_0002DFF0 -> sub_0008BE60 -> vtable+8`; **whether that delta is constant or
  measured is [U]** (section 8, probe 1).

## 2. Who calls the crowd, and when (the arena frame)

`sub_0008BE60` (recomp_0004.c:18724) is the arena's per-frame function. It switches on the arena state
`*(u32*)0x376F80` (`A + 0x2D820`, jump table `0x8C0D0`, 16 entries) [V]:

| arena state | calls |
|---|---|
| 0 | `sub_00088630` |
| 1 | `sub_0008B120` |
| 2 | `sub_000888C0` |
| 3 | `sub_000898E0` |
| 4 | `sub_00089650`, then falls into 5 |
| 5 | `sub_00087700` (fight step; netplay hook), `sub_0008AB00` |
| 6 | `sub_00089B30` |
| 7 | `sub_00089BC0` |
| 8 | `sub_00087700` (fight step), `sub_00089DB0` |
| 9..12 | `89E00`, `89F20`, `8A300`, `8A5B0` |
| 15 | `sub_00086F40` or `sub_00089310` (cleanup, calls vtable+0xC), then `sub_0008BB00` |

Afterwards, **every frame in every state except 7**, the arena calls the crowd update through the vtable:
`mov eax,[mgr]; push edi; call [eax+8]` at 0x8C035 (recomp_0004.c:19062-19070), `ecx = A + 0x2B9F0`. In state 7 it calls
`sub_0007E740` instead (sets a dirty bit in every member's animation object, no update) [V]. This confirms that the
crowd animates during the loading states and that, from the first `sub_00087700`, there is exactly one crowd update per
arena frame, **after** `sub_00087700` returns. The hook therefore runs before the first fight-time update.

Match start (`sub_0008BB00`, recomp_0004.c:18312): seeds the arena's own generator `0x39C0E0` with `A+0x2D874/78`,
stores the match info at `A+0x2FF5C`, and then takes one of two paths depending on bit `0x400` of `A+0x2B9EC` [V]:

* bit clear: `sub_0008B730` (full load). It copies `A+0x2D874/78` into `manager+0xE10/0xE14` and calls vtable+4
  (`sub_00081240`), which allocates the members and the record table and seeds the generator (recomp_0004.c:18425-18440).
* bit set: `sub_000891A0` (light restart of a loaded arena, recomp_0004.c:12140). It calls `sub_00080880` on the
  manager and **does not reseed the crowd generator** at all, so the generator continues from the previous match.
  The bit is set at 88C4B (in `sub_00088B90`) and at recomp_0004.c:20306 [V]; **which path a room match takes is [U]**
  (probe 2).

## 3. Candidate routines (question 1)

All thiscall, `ecx` = object. "Safe" = only writes fields, no allocation.

| Routine | Address (file:line) | Args | Touches | Alloc | Verdict |
|---|---|---|---|---|---|
| **restart crowd** `sub_00080880` | 0x80880 (115771) | none, `ret` | XML re-read of the crowd section via `sub_0007F950` and `sub_0007E4E0`; per member `sub_0007D240`; `sub_0007F3D0(0)`; per member `rec.+0x30 = -1`; per non-0xC member idle state by the quota counters then `sub_0007D2F0(m, S, -1)`, `sub_0007D560(m, 0)` | none (XML reader works on the in-memory text at `[[mgr+0x24]+0xA4]`, size from `sub_00029870`) [V] | **safe**, subject to the XML text still being there [U] |
| member restart `sub_0007D240` | 0x7D240 (86793) | ecx = member, `ret` | `+4 = 0`, `+8 = 0`, `+0x50 = 0`, `+0x54 = 0`, `+0x14 = -1`, then `sub_00084110(member+0xE0)` (animation object reset; takes the object out of / back into the bank's list at bank+0x510 [I]) | none seen | safe |
| set clip `sub_0007D2F0` | 0x7D2F0 (86972) | ecx = member, `(state, slot)`, `ret 8` | the **only** writer of `member+4` besides 7D240/7D1A0; picks the clip, `+8`, `+0x10/+0x14`, `+0x50 = 0`, `+0x54`, `+0x58` name, `+0x4C`, `+0xC8 = slot`, mirror `+0x120` | none | safe; draws the member's generator `member+0x178` |
| sample pose `sub_0007D560` | 0x7D560 (87306) | ecx = member, `(float)`, `ret 4` | `+0x30` toward `+0x3C`, pose via `sub_00084190/84260` | none | safe |
| intensity and quotas `sub_0007F3D0` | 0x7F3D0 (92602) | ecx = mgr, `(float dt)`, `ret 4` | `+0xC20..0xC34`, counters `+0xC38/C40/C48`, quotas `+0xC3C/C44/C4C` | none | safe |
| publish `sub_0007F030` | 0x7F030 (92105) | ecx = mgr, `ret` | all records (pos, heading, status `+0x18`), clears `rec.+0x1C`; may call `sub_0007D2F0` on members in weapon/hold states | none | safe |
| idle-state dealer `sub_0007F6E0` | 0x7F6E0 (92981) | ecx = mgr, `ret` | counters `+0xC40/C48/C38`; returns state 4, 3, 2 or 0 | none | safe |
| load `sub_00081240` (vtable+4) | 0x81240 (117413) | ecx = mgr, `ret`, returns al=1 | zeroes `+0x18/+0x20/+0x1C`, seeds gen, `sub_00080ED0` (members), `sub_00080C10` (records), `sub_0007E170/F3D0/FDA0/F950/E4E0` | **members (0x17C each, `sub_00029680`), record table (`sub_000296B0`), anim bank** | **unsafe** (leaks the old members and table; fighters hold the table pointer) |
| release `sub_0007E6A0` (vtable+0xC) | 0x7E6A0 (90356) | ecx = mgr, `ret` | frees members, bank, record table; `0x3494E0/E4 = 0` | frees | **unsafe** |
| record builder `sub_00080C10` | 0x80C10 (116355) | ecx = mgr, tail-jumps `sub_0007F030` | count, new table, `member+0xC`, partner `member+0x174` | **allocates** (`sub_000296B0`, overwrites `0x3494E4`) | unsafe |
| anim stop `sub_0007E710` | 0x7E710 (90453) | ecx = mgr | `sub_0007D260 -> sub_00084140` per member | frees from bank list [I] | unsafe (round-end cleanup, `sub_00086F40`) |
| default member `sub_0007D1A0` | 0x7D1A0 (86727) | ecx = member | `+0 = 4`, `+4/+8/+0x50/+0x54/+0x68/+0x6C = 0`, `+0xC8 = -1`, `+0xD0 = -1`, `+0x10 = 4`, `+0x14 = -1`, `+0x174 = 0` | none | **do not** (resets the type, position fields of the XML are kept but `+0` and `+0x68` are wiped) |

Strings: `CCrowdControl::InitXMLCharactor::m_pWork` (0x29F4B0, used by 80ED0 at 116969), `CCrowdControl::InitTestCharactor::m_pWork`
(0x29F64C), `CCrowdControll::InitAudiencePublic::g_CrowdAudiencePublic.m_Audience)` (0x29F688, 80C10 at 116429);
`CCrowd::*` / `CCrowdData::*` / `CCrowdAnimation::Init` belong to the animation bank and the 2D background crowd.

Nothing resets the crowd at "FIGHT!", at a KO or between rounds [V]: the only external callers of manager
methods are the arena's load (`sub_0008B730`), restart (`sub_000891A0`), cleanup (`sub_00086F40`, `sub_00089310`) and
frame (`sub_0008BE60`); `callers.py` shows no other direct caller for any method in 0x7C000-0x81400
(`sub_0007F900/F920` are the fighters' request writers, `sub_0007F860/F890/F8C0` read-only getters).

## 4. The PC-specific input that is not timing: generator `0x33DB5C`

* `sub_0002A770` (recomp_0001.c:13401) = `ecx = 0x33DB5C; jmp sub_0002A390` (12758): a 6-word generator at
  `0x33DB5C..0x33DB73` (state words +0..+0x14) [V].
* Its only seeders are `sub_0002A720(seed)` (13359), called from `sub_00011950` (recomp_0000.c:3935, seed built from the
  16 bytes returned by `sub_001F4EB6`, a calendar-time query [I]) and from the big start-up function `sub_00011E40`
  (recomp_0000.c:5610, same time fields) [V]. So on each PC it starts from that PC's clock. Neither `netplay_on_seed`
  nor the test harness hooks it: `src/recomp_manual.c` hooks `sub_000AA0D0`, `sub_001BC690`, `sub_001AD220`, not
  `sub_0002A720` [V]. It is also drawn by about 60 other callers (effects, audio, UI) whose rate may depend on frames [I].
* Crowd draws (all through `sub_0002A770`):

| Site | Line | When | Effect |
|---|---|---|---|
| `sub_00080ED0` | 117126 | load, per member | `member+0x68 = (r % 60 - 30) * 0.01 + 1.0` (0.75 for type 0xD) -> **idle clip speed** |
| `sub_0007E170` | 89568 | load, per member | `r % 10 - 5` -> scale in `sub_0007D2C0` -> `member+0x11C` (= anim object +0x3C) and `member+0xF8` |
| `sub_0007FDA0` family (7FFDA...80101 are aliases) | 95079..95666 | load | costume / body choice [I] |
| `sub_0007EA70` | 91173, 91234, 91304 | **play**, when a spectator becomes idle | `r % 10 == 0` gates a weapon offer |
| `sub_00080A00` | 116290 | **play**, when a fighter is within 10.0 | `(r & 1) + 7` -> reaction state 7 or 8 |
| `sub_0007F740` | 93130 | **play**, request code 13 | `(r & 1) + 5` |

* `member+0x68` is used every update: `sub_0007D690` (87526) returns it for states 0..8 (class 0); states 9..0x1D etc. use
  the fighter-slot table `*(float*)(0x3BE0DC + 4*rec.slot)` (class 1) or 1.0 (class 2) [V, byte table at 0x7D6D8].
  `sub_0007D870` advances the clip clock: `member+0x50 += speed * 60.0 * dt` and reports "finished" when it exceeds the
  clip length from `sub_00084480` (87894-87998) [V]. So **different `+0x68` means different clip end frames, hence
  different state timelines, from the first step, on two PCs that run at the same host speed.**

Consequence: even if the crowd state were copied exactly at step 1, any later draw of `0x33DB5C` by the crowd
(weapon trigger, reaction coin flip) would diverge, and it already diverges at load (speeds). Two ways to deal with it:
set the 6 words to a shared value at step 1 (G1, minimal; breaks if any other consumer draws at a PC-dependent rate), or
give the crowd its own copy for the duration of each update (G2, recommended, section 6).

## 5. State inventory (question 2)

### 5.1 Generators

| Generator | Address | Size | Layout | Drawn by the crowd in play | Equalize? |
|---|---|---|---|---|---|
| crowd private | `manager+0xE18` = **0x375F68** (members hold it at `+0x178`) | 0x88 | 32 floats at +0..+0x7F, state word `+0x80`, odd increment `+0x84`; a draw is two LCG steps `x = x*0x5D588B65 + inc`, index `x & 0x1F`, returns and **replaces** that table entry (`sub_000AA2B0`, recomp_0005.c:6289) [V] | `sub_0007D2F0` (variant picks via `sub_000AA340`, mirror flag via `sub_000AA300`), `sub_0007EC00` (weapon pick via `sub_000AA2B0`) [V] | **yes, whole 0x88 bytes**; one call to `sub_000AA0D0(gen, A, B)` rewrites all of it (recomp_0005.c:6065) |
| global | **0x33DB5C** | 0x18 | 6 words; seeded by `sub_0002A720(seed)` (arithmetic sequence from one word) | see section 4 | yes (G1 or G2) |
| match | `0x3C1404` | | | not by the crowd (fighter side `sub_001B95F0` only) | already covered by `netplay_on_seed` |
| arena noise/world | `0x39C0E0` | 0x88 | same class, seeded by `sub_0008BB00` | `sub_00084260` reads it as a smooth noise for model height (visual) | no |
| C runtime `rand` (`sub_00200820`) | | | | not by the foreground crowd (callers: 13AA0, 146D0, 65FD0, C33E0, DB2D0, DB660 = background crowd etc.) [V by `callers.py`] | no |

`netplay_on_seed(g_esp, 2, g_ecx)` already replaces the arguments of every `sub_000AA0D0` call with session-derived
words (keyed by `which = g_ecx`, with a per-key counter) [V, netplay.c:1584]. It therefore also rewrites the arguments
of a reseed made by the hook; to keep the values under control, bypass it with a flag while the hook reseeds.

### 5.2 Manager (`mgr = 0x375150`)

| Offset | Meaning | Class |
|---|---|---|
| +0x00 | vtable `0x29EBE8` | static |
| +0x14 | non-zero = XML crowd (selects `sub_00080ED0` in the load) | static |
| +0x18 | member count | static after load |
| **+0x1C** | **rotation index**: member order of an update is `(idx + i) % count`; decremented (wraps to count-1) at the end of each update (117224-117232) | **carried; set to 0** |
| +0x20 | flags; bit 0 = "viewer" mode (static pose, update skipped / replaced) | static (0 in play) |
| +0x24 | pointer to the arena base object (`A + 0x10`); `[+0xA4]` = XML text, `[+0xA8]` = size | static |
| +0x30.. | animation bank object (members' `+0x170`) | static |
| +0xA90 + 4*i | member pointers | static after load |
| +0xC20 / +0xC24 / +0xC2C / +0xC34 | crowd intensity state (current, applied, previous, rate), reset by `sub_0007E4E0` (defaults 60.0, 60.0, -1.0, 0) and then integrated each update by `sub_0007F3D0` from fighter data at `0x340D80 + 0xC8*slot`, `0x340E32` ... | **carried** (restart resets it) |
| +0xC38 / C40 / C48 | counts of members in idle states 2 / 4 / 3 | derived each update (`sub_0007F3D0` 92896-92957) |
| +0xC3C / C44 / C4C | quotas for those states | derived each update from +0xC24 |
| +0xC50..C64 | tunables (XML) | static |
| +0xC68..CC8 | weapon and threshold tables (XML, `sub_0007F950`) | static |
| **+0xC98 / +0xCD0 / +0xCCC** | offers made / schedule index / next offer time (`-1` = none) | **carried**; reset by `sub_0007F950` (94156-94158: 0, 0, -1) |
| +0xCD4 | byte, "an offer was made this update", cleared at the start of each update (117276) | derived |
| +0xCD8 + 0x84*k | weapon weight tables (XML) | static |
| +0xDE0.. | four fighters' joint positions, rewritten every update by `sub_00080D40` | derived (used only for the visual lean) |
| +0xE10 / +0xE14 | copy of the arena seeds `A+0x2D874/78` | static (not what the generator is seeded with in a session) |
| **+0xE18** | the private generator (above) | **carried** |
| +0xEA0 | byte, "high detail" (`obj(0x18)+0x3A0 > 2`) | static |

### 5.3 Member (0x17C bytes at `*(mgr+0xA90+4*i)`)

| Offset | Meaning | Class |
|---|---|---|
| +0x00 | type: 0..3 public/interactive (`sub_0007D760`), 0xC and 0xD special (`sub_00080ED0`), set from the model name by `sub_0007D7B0`; `sub_0007D2C0` can turn 8 into 9 | static |
| **+0x04** | **animation state** (written only by `sub_0007D2F0`, `sub_0007D240`, `sub_0007D1A0`) | **carried** |
| **+0x08** | variant index of the current pick | carried (set by 7D2F0) |
| +0x0C | record index, -1 if not public | static (set by 80C10) |
| **+0x10 / +0x14** | last pick (type, variant), used to redraw a repeated variant; `sub_0007D240` sets `+0x14 = -1` | carried |
| +0x20..0x2B / +0x2C..0x37 | placement position / rotation from the XML | static |
| +0x30 | current heading, moves toward `+0x3C` by 5.0 degrees per update (`sub_0007D560`, 0x29238C = 5.0) | carried (visual; `+0x3C` is also handed to the partner at 92343) |
| +0x38 | base heading (published) | static |
| +0x3C | target heading (`sub_0007D620`) | carried |
| +0x40..0x48 | lean offset away from fighters (`sub_0007D8F0`), applied to the model only (`+0x12C` = animation object `+0x4C`, read in `sub_00084190`) | carried, visual |
| +0x4C | byte, from the clip's table entry (`sub_00085470` entry `+0xC`) | derived at 7D2F0 |
| **+0x50** | **clip time** (float, frames), advanced by `sub_0007D870`, zeroed by 7D2F0/7D240/7E780 | **carried** |
| +0x54 | clip id (`sub_00086860` name lookup in the bank) | derived at 7D2F0 |
| +0x58..0x67 | clip name `V2CA_%03d`-style (format `V2CA_%s`; all names are <= 8 characters, so `+0x68` is not overwritten) | derived |
| **+0x68** | **speed factor**, set once at load from the global generator | **PC-specific, must be overwritten** |
| +0x70 | model-name string from the XML | static |
| **+0xC8** | engaged fighter slot, -1 = none (second argument of 7D2F0) | carried |
| +0xD0 / +0xD4 | flag / zero | static |
| **+0xD8** | offer countdown (set -1 on offer, 0x708 when a held weapon is given, decremented by 7F030) | carried while offering |
| **+0xDC** | byte, "a fighter was near" latch (80A00 116167, 116289) | carried; set 0 |
| +0xDD | public flag from the XML | static |
| +0xE0..+0x17B | animation object A: `+0xE0` kind (0/1 = static plane, else clip player), `+0xEC` low-level model, `+0xF0` bank (`mgr+0x30`), `+0xF4` clip table, `+0x11C` scale, `+0x120` mirror byte (50 percent draw in 7D2F0), `+0x124` flags, `+0x128` = 1.0, `+0x12C` offset pointer | mixed, see below |
| +0x170 / +0x174 / +0x178 | bank pointer / partner member (computed at load from positions by `sub_0007E830`) / generator pointer | static |

**Where the clips live.** The gameplay clock is `member+0x50` against the clip length read from the low-level model
(`sub_00084480 -> sub_000248E0(A+0xC)`, a function of the clip id only) [V]. The pose is a pure function of
`(clip id, member+0x50)` via `sub_00084190`; blend weights in the animation object (`A+0x1C/0x24/0x38`, `sub_000848E0`
at recomp_0004.c:1869) and the bank list at `bank+0x510` are visual bookkeeping. They do **not** need equalizing for
gameplay; `sub_0007D240` / `sub_0007D2F0` rebuild them consistently [I].

### 5.4 Record table (`*(0x3494E4)`, `0x3494E0` records of 0x34 bytes) and nearby globals

Derived each update by `sub_0007F030` (pos, heading, status `+0x18`, `+0x20` from `member+0xC8`): no need to set, only
to **re-publish after the reset** so the first fighter scan sees fresh values. Carried: `+0x30` (weapon handle, -1 none;
the restart clears it), `+0x1C/+0x20/+0x24` (fighter requests; cleared each update). Globals: `0x3494E8` (copy of
`mgr+0xC24`), `0x349508.. / 0x349538.. / 0x349568..` (fighter positions, rotations, statuses, rewritten by
`sub_001B8330` every fighter step), `0x3BE0F4` bit 0 (suppress interactions) and `0x3BE0EC/F0` (match clock, used by
the offer schedule) are all fight state. `0x347390/0x3473DC/0x3473E0/0x3473E8` belong to the viewer mode only.

## 6. Recipes (question 3)

All of this runs at `N.step == 1` inside the sub_00087700 hook, where `g_ecx` is the arena object. Guest calls from the
hook follow the pattern already used in `recomp_manual.c` (~line 389): set `g_ecx`, `PUSH32(g_esp, arg)` right to left,
`PUSH32(g_esp, <any return address>)`, call `sub_XXXXXXXX()`; the callee pops the return address and its `ret N`
arguments. Save and restore `g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_ebp, g_esp` (and the x87 top) around the whole
block: the hook sits in the prologue of `sub_00087700` and its caller `sub_0008BE60` keeps `esi/edi` live [I].

Precondition checks (skip the whole block and log if any fails): `mgr = g_ecx + 0x2B9F0`; `MEM32(mgr) == 0x29EBE8`;
`n = MEM32(mgr + 0x18) > 0`; `MEM32(mgr + 0xA90) != 0`; `MEM32(mgr + 0x20) & 1 == 0`.

### (a) Call-based (preferred)

1. Reseed the private generator with shared words: `g_ecx = mgr + 0xE18; push B; push A; call sub_000AA0D0`
   (`ret 8`; A, B = hash of the session and match counters, B odd). Do it first, because the restart draws from it.
   Set a flag so `netplay_on_seed` leaves A, B alone (otherwise it replaces them with its own counter-keyed words, which
   are also shared, but only if both PCs seeded this generator the same number of times).
2. Guard: `xml = MEM32(mgr + 0x24)`; require `xml != 0`, `MEM32(xml + 0xA4) != 0`, `MEM32(xml + 0xA8) > 0` (the restart
   re-reads the crowd tunables from that text; if it were gone `sub_0007F950` would wipe the weapon tables to -1 and
   leave the crowd weapon-less, equally on both PCs). If the guard fails use (b).
3. `g_ecx = mgr; call sub_00080880` (no args). Effects, all [V]: per member state/time/variant reset (`7D240`), tunables
   and intensity reset (`7F950`, `7E4E0`), offer schedule reset, `rec.+0x30 = -1`, idle states re-dealt by quota and
   clips re-picked (`7D2F0(m, S, -1)`, `7D560(m, 0)`), type-0xC members skipped.
4. Fix what the restart leaves: `MEM32(mgr + 0x1C) = 0`; for each member `m`: `MEM32(m + 0x68) = speed_i` (shared value:
   1.0, or the game's formula with `r = hash(session, i) % 60`: `(r - 30) * 0.01 + 1.0`, 0.75 when type is 0xD),
   `MEM8(m + 0xDC) = 0`, `MEM32(m + 0xD8) = 0`. (`+0x68` is only read by later updates, so setting it after step 3 is fine.)
5. `g_ecx = mgr; call sub_0007F030` to re-publish the records (the first update comes only after `sub_00087700`).
6. The global generator: see G1/G2 below.

### (b) State-based (no XML reader)

Same as (a) but replace step 3 by: for `i in 0..n-1`, `m = MEM32(mgr + 0xA90 + 4*i)`: `ecx = m; sub_0007D240`;
`MEM32(m + 0xC8) = -1`; `MEM32(m + 0x10) = MEM32(m)`. Then manager fields: `+0xC98 = 0`, `+0xCD0 = 0`, `+0xCCC = -1`,
`+0xCD4 = 0`, `+0xC20 = +0xC24 = 0x42700000` (60.0), `+0xC2C = 0xBF800000`, `+0xC34 = 0`; `ecx = mgr; push 0.0f;
call sub_0007F3D0`; then for each member (in index order) `S = type == 0xC ? 0x2F : (ecx = mgr; call sub_0007F6E0)`
(skip 0xC if you want the exact game behaviour, which leaves them in state 0), `ecx = m; push -1; push S;
call sub_0007D2F0`, then `ecx = m; push 0; call sub_0007D560`. Risks: the intensity defaults are the code's, not the
arena XML's (a tuning difference, identical on both PCs); the order of the dealer calls matters (member index order,
as in 80880 at 115880-115965).

### Generator `0x33DB5C`

* **G1 (minimal)**: at step 1, with the real global saved, `push seedword; call sub_0002A720` (that writes the 6 words
  from one word). Valid only if no other consumer draws it at a PC-dependent rate; unknown [U].
* **G2 (recommended)**: keep a private 6-word copy. `sub_00081170` has no direct callers (`callers.py`), so a
  `recomp_manual.c` override of `sub_00081170` (the `extern void sub_00081170_gen(void)` pattern from CLAUDE.md) is
  reached by the arena's indirect call. In the wrapper: swap `MEM32(0x33DB5C..0x33DB73)` with the private copy,
  `sub_00081170_gen()`, swap back. Initialize the private copy at step 1 as in G1 on a saved/restored real state. All
  in-play crowd draws of the global generator (`7EA70`, `7F740`, `80A00`) are inside the update [V by caller lists], so
  the wrapper covers them. The wrapper should do nothing special when no lockstep session is active.
* Either way, **the loading window no longer matters**: the reset at step 1 overwrites everything the loading window
  changed.

### Which is safer

(a). It is the game's own match-start path for an already loaded arena, performs the re-deals in the right order,
resets the offer schedule and intensity with the arena's own XML values, and clears the handles. Its single external
dependency is the XML text (guarded). (b) re-implements the same sequence from the static read and is the fallback.
Neither replaces the generator and speed fixes.

### What could still differ after the reset

* The frame delta argument of the update (section 1, **[U]**), if it is measured time. Probe 1 settles it.
* Weapons already spawned by a spectator during the loading window: the restart forgets their handles
  (`rec.+0x30 = -1`) but the weapon objects stay in the 16-slot pool. If the pool differs between PCs at step 1 the
  later pool indices differ. Check the pool at step 1 (probe 3); the offer needs `sub_00093CA0` (weapon count limit) and
  the match clock thresholds, so it is probably empty [U].
* Other consumers of `0x33DB5C` if G1 is used instead of G2.
* Members' positions: static, they do not walk [V in range]. No physics acts on the crowd; fighters never collide with it
  (`spectator-shove.md`). The lean offset (`+0x40`) and the model height are visual.
* The path taken in `sub_0008BB00` (light restart vs full load) does not matter for the recipe: both end with a
  crowd whose generator, rotation and speeds the hook overwrites.

## 7. Weapons (question 5)

Chain: `sub_00080A00` (116211-116259) -> `sub_0007EA70` (decision) -> `sub_0007EC00` (choice) -> `sub_000BCAA0(type,
&pos)` (recomp_0005.c:55908) -> `sub_000BCA10(type, pos, -1)` (55800) -> weapon manager `sub_00044AC0`, `sub_000BB730`
(51654), result stored in `rec.+0x30`; same from `sub_0007E940` (state 0xE hand-over). [V]

* **Decision** `sub_0007EA70` (91049): needs `mgr+0xCD4 != 1`, `*(u8*)0x3BE0F4 & 1 == 0`, `sub_00093CA0(mgr+0x24, 1)`
  (weapon-count limit against `sub_000BBE10/30`, 41246), thresholds `mgr+0xC70..`, per-fighter floats `0x3BDF14 + 0x74*i`
  and statuses `0x349568`, the match clock `0x3BE0EC * 60 + 0x3BE0F0` against `mgr+0xCCC/0xCD0`, and finally
  **`rand() % 10 == 0` from the global generator `0x33DB5C`** (3 sites). All inputs but that coin are fight state.
* **Choice** `sub_0007EC00` (91355): weighted pick over `mgr + 0xCD8 + 0x84*k` using the **crowd's private generator**
  (`ecx = mgr + 0xE18`, `sub_000AA2B0`), retried until `sub_000BBF10(type)` (type loaded in the weapon manager) holds [V].
* **Spawn**: the handle is the pool index = first free of 16 entries of 0xD0 bytes at `wm+0xC` (entry `+4 == -1` is free);
  the type table is `*(0x39DB10)` (0x7C bytes per type; a type with no data falls back to the first loaded one). The
  position is the spectator's static `member+0x20..0x28` with Y lowered by `0x29F7B0` [V]. `sub_000BA3D0` (slot init) makes no
  calls [V]. No allocator order, no pointer, no generator: nothing PC-specific beyond the global generator in the decision
  and the occupancy of the pool.
* **Fighter side**: `sub_001B93B0` reads `rec.+0x30`, equips `sub_001AA1D0(fighter, 1, 0, handle)` and writes -1 back.

## 8. Probes that would settle the open points (not run)

1. Crowd step: at `sub_0008BE60_enter` log `MEM32(g_esp+4)` (the frame delta) and `MEM32(0x3BDE80)` for 300 steps on
   both PCs. If the delta is not a constant, the update's `dt` needs a fixed substitute (wrap `sub_00081170` and replace
   the stack argument).
2. Add `sub_0008B730_enter` and `sub_000891A0_enter` probes: which path does a first room match, a second room match, a
   solo fight take? Also `sub_00080880_enter` (call count).
3. At step 1 (before the reset), count used weapon-pool entries: `wm = sub_00044AC0()`, entries `wm+0xC+0xD0*i`, `+4 != -1`.
4. Crowd state hash for lockstep checks (like `fighter_hash`): per member `+4, +8, +0x50, +0x68, +0xC8, +0xD8`, manager
   `+0x1C, +0xC98, +0xCCC, +0xCD0, +0xC24`, the generator 0x88 bytes at `0x375F68`, the 24 bytes at `0x33DB5C` (only
   meaningful inside G2's private copy), and the statuses `rec.+0x18` for all records. Compare at steps 1, 2, 60, 300.
   The first field that differs names what is left.
5. After the reset, log `sub_0002A770` draw counts per caller (use `sub_0002A770_enter` with the return address) to see
   how much the crowd draws in play.

## 9. Function index

| Function | File:line | Role |
|---|---|---|
| `sub_00081170` | recomp_0003.c:117233 | update (vtable+8), `ret 4`, indirect only |
| `sub_00081240` | 117413 | load (vtable+4) |
| `sub_0007E6A0` | 90356 | release (vtable+0xC) |
| `sub_00080880` | 115771 | restart crowd |
| `sub_00080ED0` | 116785 | members from XML, initial speed and state (tail loop 117122-117201) |
| `sub_00080C10` | 116355 | record table |
| `sub_00080A00` | 116003 | react + advance one member (per update) |
| `sub_00080D40` | 116573 | fighters' positions into the crowd's globals |
| `sub_0007D2F0` / `7D240` / `7D1A0` / `7D4B0` | 86972 / 86793 / 86727 / 87216 | clip start / member restart / defaults / back to idle |
| `sub_0007D560` / `7D690` / `7D870` / `7D8F0` | 87306 / 87526 / 87894 / 88006 | pose / speed class / clock + finished / lean |
| `sub_0007DD10` / `7DFE0` | 88590 / 89189 | member XML / member ctor |
| `sub_0007EA70` / `7EC00` / `7E940` / `7ED10` | 91049 / 91355 / 90860 / 91586 | offer decision / weapon pick / hand-over / held-weapon upkeep |
| `sub_0007F030` / `7F3D0` / `7F6E0` / `7F740` | 92105 / 92602 / 92981 / 93051 | publish / intensity and quotas / idle dealer / request-code to state |
| `sub_0007E4E0` / `7F950` | 90065 / 93527 | intensity defaults + XML / weapon tables + offer schedule reset |
| `sub_0008BE60` | recomp_0004.c:18724 | arena frame (state machine, update at 0x8C035) |
| `sub_0008BB00` / `8B730` / `891A0` | 18312 / 17806 / 12140 | match setup / full load / light restart |
| `sub_0008CB90` | 21520 | arena ctor (manager vtable store at 21612) |
| `sub_00044210` | recomp_0002.c:9772 | scene type -> manager address |
| `sub_00044C20` | 11747 | time scale (`0x3BDE80` in states 5, 8) |
| `sub_000AA0D0` / `AA2B0` / `AA300` / `AA340` | recomp_0005.c:6065 / 6289 / 6355 / 6423 | generator seed / draw float / chance / draw int |
| `sub_0002A770` / `2A390` / `2A720` | recomp_0001.c:13401 / 12758 / 13359 | global generator draw / body / seed |
| `sub_00011950` | recomp_0000.c:3935 | boot seeding from the clock |
| `sub_000BCAA0` / `BCA10` / `BB730` | recomp_0005.c:55908 / 55800 / 51654 | weapon spawn / slot allocation |

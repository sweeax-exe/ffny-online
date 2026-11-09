# Cheat menu: research for issue #71

2026-10-23. A dated record of a static read: nothing was built or run, and no game code was changed.
The source is authoritative. §7 (2026-10-24) records the implementation and what the live runs showed;
where it differs from §1-§6, §7 is the checked one.

- Request: [vyanhursky/DefJamFFNY-recomp#71](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/71) "cheat menu".
- What #71 asks for: max development points and cash from the start of Story, infinite health, and a
  full Blazin' meter.
- Sources: the lifted C of the user's own USA dump (`default.xbe`), `assets/gamecore.xml` from the
  same dump, [Unlocking everything](unlock-everything.md), and published GameCube and PS2 cheat lists
  (links in §2).
- Every address is a guest virtual address. **(checked)** means I read it in the lifted C myself.
  Everything else comes from a reader's static pass and still needs a live run.

## Short answer

Each address below is a guest virtual address. **(checked)** marks a claim confirmed by reading the
lifted function directly. The rest comes from a bounded static read and still needs a live run before
anything is built on it.

- All four things in #71 can be done, and none of them needs a new game mechanic. Each is one value
  written into a field the game already uses:
  - Story cash is profile slot +0x20 **(checked)**.
  - Development points are slot +0x28 **(checked)**.
  - Health is fighter +0x350, held at its ceilings.
  - The Blazin' meter is fighter +0x364, plus the ready/active state byte at +0x93E **(checked)**.
- The game's own cheat codes give only songs and reward points (925 points in all). Developer leftovers
  add unlock-all and some debug flags. None of them touches cash, development points, health or the meter.
- Most of the extra ideas are feasible:

  | Feasibility | Ideas |
  |---|---|
  | Easy | double/half health, slow motion, swapping and randomising movesets, giant/tiny fighters |
  | Medium | fast motion, all shop items, all items in the closet |
  | Hard | a true big head mode |

- One policy question comes first. Story cheats change the profile in memory, and **the game saves that
  slot as a raw memory image**, so cash, points, stats, styles and items stay in the player's save for
  good. Fight cheats (health, meter, time, movesets) live only in memory and leave no trace.

## Owner's picks (2026-10-23, PR #97)

| Cheat | Where in this note | Hook |
|---|---|---|
| Story: max cash, max development points | §3.1, §3.2 | write the active profile slot |
| Infinite health, max Blazin' | §3.3, §3.4 | per fight step |
| Giant fighters, tiny fighters | §4 | model "default scale" at model +0x2E0 |
| No health regeneration | §2, §4 | zero the regen path through `sub_001A5730`, or the gamecore recovery values |
| Story: choose fighting style | §4 | hero style bytes S+0x140..0x143 |
| Story: play as a different fighter, with that fighter's moveset | below | match setup for the hero's slot |

**Rule:** every cheat that changes the Story profile (cash, development points, styles) tells
the player, before it applies, that the change is saved into their profile and cannot be undone.
Advise them to back up the save first.

### Replacing the Story hero

- **In fights:** the hero's slot takes a character index and a model from the match setup. These are
  two separate paths:
  - the character index (moveset, styles, stats) reaches the record from 0x3B90B8[slot];
  - the model comes from setup +0x128+slot.
- So both are rewritten at match setup (`sub_000871F0`), the same hook as the moveset swap in §4, with
  the hero's slot only. The other fighter then appears with their own look and moves.
- Story stats copy into the match from the profile only when a byte in the setup matches
  (`sub_000871F0`, the 0x3B9128 block). Whether the chosen fighter keeps the hero's trained stats or
  uses their own is a design choice, and both look possible.
- **Outside fights** (cutscenes, the crib, shops, the map), the created hero is drawn from the profile's
  creator data. Showing another fighter there is not explored and is likely much harder.
- **Decided (2026-10-23, PR #97):** the first version is fights only, and "everywhere" is a later enhancement. It can
  be a temporary toggle, since it needs no profile
  write. That differs from the style choice, which is stored in the profile.

## 1. What is built in

### 1.1 Retail cheat codes (Extras, Cheat Codes)

The handler is `SubmitCheatCode` (`sub_00049D00`). Only two kinds of code exist:

| Kind | Codes |
|---|---|
| Song, once each (16) | MILITAIN BIGBOI CHOPPER KIRKJONES CHOCOCITY RESPECT PLATINUMB GONBETRUBL ULTRAMAG PUMP LOYALTY GHOSTSHELL POWER SIEZE CARTAGENA AKIRA |
| Reward points, once per amount | THESOURCE 25, GETSTUFF 50, DUCKETS 100, CROOKLYN 250, NEWJACK 500 |

- The menu can apply these without typing: call the same handler with each code string, or the song-
  and points-unlock calls it makes.
- That is really one menu item, "Unlock all songs + 925 reward points".
- An Indonesian fan site lists more strings (STY572939 and others). No other source has them and the
  handler's classifier does not recognise them, so they are almost certainly fake.

### 1.2 Developer leftovers in the same handler (no code reaches them)

| Branch | Effect |
|---|---|
| `BIGBANG` | `sub_000158F0`: all 74 fighters and all venues, reward points 10,000. **Already exposed** as `[debug] unlock_all` in the launcher and overlay |
| `LQA` | `g_LocalCheat` flag for the Flash screens (game-data +0x1AD5). What the screens do with it is not explored |
| `MSGS` | `sub_00013590(2)`; unknown |
| "Debug" (code word lost) | byte 0x0039C1DA: an animation label under each health bar. Already `RECOMP_DEV_DEBUG=1` |
| "Holla" (code word lost) | byte game-data +0x1AD4; unknown |

Also built in:

- **EA's developer menu**, restored in v0.10.0: any match, fighters, venue, scene, stage viewer,
  Blazin' viewer.
- **`gamecore.xml`** (below), the game's tuning file. It is re-read at the start of every match.
- **A starting-health multiplier**, 0x3B92C8 flag + 0x3B92CC[slot] float **(checked)**:
  - `sub_000871F0` copies it from the match setup.
  - `sub_001A4F30` multiplies each fighter's starting health by it.
  - It looks like a handicap for fights where a fighter starts hurt.
- **A per-fighter random health/momentum switch** at byte 0x3B907E (reader's claim, not verified;
  another note says the same byte carries mode bits).

### 1.3 `gamecore.xml`, the tuning file

- It is a plain loose file in `extracted/assets/`. The parser `sub_001B6940` runs at the start of
  every match (`sub_001A4D90`).
- Values land in fixed globals:

  | Value | Address |
  |---|---|
  | health base | 0x2F04D0 |
  | tank minimum | 0x2F03A8 |
  | momentum expiry | 0x2F03AC |
  | style damage rates | 0x2F0530/44/58/6C |
  | KO slow-motion | 0x2F0580/84/88 |
  | momentum penalty | 0x2F02C0.. |
  | difficulty scale | 0x2F0360/6C |

- A cheat can overwrite them after the parse, which needs no file change and fits the mod system.
- Knobs in it: damage per style and move type, health/stun/stamina recovery, momentum decay and gain
  per difficulty, stun lengths, move repetition penalty, animation speed per style, submission
  length, button-escape rate, KO slow-motion speed, start positions.

## 2. Cheat tables from other versions

- No Xbox cheat device codes, trainers, xemu cheat files or Cheat Engine tables were found for any version.
- GameCube and PS2 have full lists:
  - Dolphin's `GNWE69.ini`
  - gamehacking.org (PS2 US SLUS-21004, PAL SLES-52507)
  - PCSX2 pnach files (`4538436F.pnach`)
  - Codejunkies AR MAX (66 codes) and CodeBreaker (both encrypted, names only)
- Their layouts match ours: same fighter-record shape (PS2 stride 0x12F8, ours 0x12E8), momentum at
  health +0x14, cash and development points 8 bytes apart in a profile slot (PS2 stride 0x5960, ours
  0x5950). That is independent confirmation of the fields in sections 3 and 4.
- Their addresses are GameCube or PS2 memory: use them only as hints about the layout.
- Links:
  - Dolphin `GNWE69.ini`: https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Data/Sys/GameSettings/GNWE69.ini
  - gamehacking.org, PS2 US: https://gamehacking.org/game/103490 ; PS2 PAL: https://gamehacking.org/game/101117
  - PCSX2 pnach files (US, PAL): https://github.com/Eszuri/CHEAT_GAME_PS2 (`Front_end/public/4538436F.pnach`, `873F8ED6.pnach`)
  - CodeBreaker list (names only): https://almarsguides.com/retro/walkthroughs/PS2/Games/DefJamFightForNY/Codebreaker/
  - CheatHappens (GameCube list; the Xbox page has no trainers): https://www.cheathappens.com/9337-Gamecube-Def_Jam_Fight_for_New_York_cheats

| Code on GC/PS2 | Ours | Notes |
|---|---|---|
| Infinite / max cash, dev points, reward points | yes | slot +0x20 / +0x28, profile +0x50 |
| Infinite health, no health (P1-P4) | yes | §3 |
| Infinite / no momentum ("stamina") (P1-P4) | yes | §3 |
| Blazin' any time (P1-P4) | yes | state byte +0x93E |
| Max / low score | likely | PS2: health +0x130, so ours is probably +0x480 (unverified) |
| Unlock all fighters / venues | already have | `unlock_all` |
| Access all gear / moves | medium | §4 |
| No health regeneration | yes | zero regen through `sub_001A5730` or the gamecore value |
| Per-limb health (head, arms, legs) | maybe | fields not mapped here |
| Can't escape submissions | yes | gamecore "button escape" values, or the PS2-style flag |
| Buying adds money | yes | trivial with a hook on the spend function `sub_00013640` **(checked)** |
| Forced facing, mute announcer / crowd | yes | not needed for a menu |

## 3. The four requests in #71

### 3.1 Max cash from the start of Story

- **Where:** active profile slot `S = P + MEM32(P+0x5AC7C)*0x5950 + 0x177C`, with
  `P = MEM32(game-data + 4)` and `game-data = MEM32(0x2FB148 + 0x18*4)`.
- Cash is the u32 at S+0x20 **(checked**: `GetPlayerCash` `sub_0004CA30` reads it and spend
  `sub_00013640` subtracts from it**)**. S+0x168 holds lifetime earnings.
- The game has no cap in code. The Flash shop's display limit is unknown, so start with 999,999 and check
  the Story HUD and shop screens.
- **Persists into the save.** The cheat could be a one-shot button ("Add $100,000") rather than a hold.
- Option: "purchases are free", as a hook on `sub_00013640`, instead of setting a number.

### 3.2 Max development points

- Development points are the u32 at S+0x28 **(checked**, `GetPlayerDevPts` `sub_0004CA70`**)**.
- The six stats (Upper, Lower, Speed, Toughness, Health, Charisma) are dwords S+0xFC..S+0x110.
- Gym cost per point rises from 5 to 5,000 (table 0x2DBB70).
- The maximum is probably 50 per stat (reader's inference).
- That gives two cheats: "Max development points" (one-shot) and "Max all stats" (writes 50 into each,
  needs a check of what the gym screen does with it).
- Both persist into the save.

### 3.3 Infinite health

- **Fields:**
  - health is f32 fighter +0x350; fighters are `R(slot) = 0x3B92E0 + slot*0x12E8`;
  - max health is +0x354;
  - the recoverable "tank" ceiling is +0x358: each hit lowers it, regen heals only up to it, and its
    floor is 5% of max.
- **Damage path:** `sub_001A50B0(record, delta, flags, extra)`, called from damage calc `sub_001A5DD0`
  and regen `sub_001A5730`.
- **Method:** every fight step, put +0x358 back to +0x354 and +0x350 back to +0x358 for the chosen
  side. The step hook already exists (`sub_00087700_enter`, used by widescreen and telemetry).
- Optionally, zero the delta at the `sub_001A50B0` entry so hit reactions still play but nothing is lost.
- **Gap:** two direct killers (`sub_001D1140`, `sub_001B6670`, likely environment KOs or the Blazin' KO)
  do not go through that path, so the step hook must run after them or they must be hooked too.
- **Menu:** P1 only / all humans / everyone, so a player can't make the CPU unbeatable by mistake.

### 3.4 Max Blazin' gauge

- **Fields:**
  - the momentum meter is f32 fighter +0x364: floor 20.0, ceiling 100.0, HUD shows value*0.01;
  - Blazin' state is byte +0x93E: bit 0 = ready (meter parked at 99.0, timer +0x368 = 1800), bit 1 = active;
  - `sub_001A55C0` activates it, writing meter 100.0, timer 1800.0, state 2 **(checked)**.
- **Infinite meter:** each step, set +0x364 = 99.0 and +0x93E bit 0 (ready) when the fighter is not
  already Blazin'. The player still triggers it by the normal appeal, so the animations, HUD fire and
  sound all play normally.
- **"Always Blazin'"** (keep it active): extend +0x368 while bit 1 is set. It needs a play-test,
  because the KO-move logic may expect the timer to run down.
- The tutorial setup `sub_001A28A0` writes exactly these states, so the game itself does this.
- All gain goes through `sub_001A5190(record, amount)`. So a softer "x2 momentum gain" can be an entry
  hook there, or the gamecore difficulty scale.

## 4. The extra ideas

| Idea | Verdict | How |
|---|---|---|
| **Double / half health** | Easy | Scale +0x354, +0x350, +0x358 (and +0x360, +0x378, which `sub_001A4F30` derives) at the first step of a match. Or use the built-in multiplier 0x3B92C8 / 0x3B92CC, which only scales starting health, not the max, so the bar would overflow at x2. The step-hook way is cleaner |
| **Slow motion** | Easy | Scale the value `sub_001A4900` receives (its entry hook, or `sub_00087700_enter` before CLogic+4 at 0x349764 is read). A plain write to 0x349764 is not enough: the effects cue `sub_000AC1F0` rewrites it. 98 functions read the per-step scale 0x3B8F2C (animation, timers, regen, round clock), so everything slows together. Pause already runs at scale 0, so low values are safe. Sound effects keep pitch |
| **Fast motion** | Medium | A scale above 1 is never used by the game and may break hit detection (larger steps skip contact frames). Better: run the fight step `sub_00087700` extra times per frame (the game already runs 0-4 steps a frame). Needs testing |
| **Big head** | Hard | The record scale +0x5C is never used for drawing. Bones are built as parent-chain products of 3x4 pose matrices (`sub_000E9380`). No bone names exist, so the head bone must be found at run time (e.g. the highest joint in the record's joint array at +0xDC), then its pose matrix scaled. Needs a live experiment first |
| **Giant / tiny fighters** | Easy-medium (good substitute for big head) | The model's "default scale" vec3 at model +0x2E0..0x2E8 feeds the world matrix (`sub_00025180`). Collision and hit distance won't follow, so it's a visual joke |
| **Swap movesets** | Easy | The character index (fighter +0x2C, from 0x3B90B8[slot], = fighter.xml `coreID`-1) selects the moveset, styles and stats from `motion/main_all.dat`. The model comes from a separate path, so overwriting 0x3B90B8[slot] before `sub_001B3110` gives fighter A fighter B's moves with A's look. Animation banks are global (loaded once per match), so no missing-bank risk |
| **Randomised movesets** | Easy | Same hook with a random index. All 141 entries have data, including 55 "CP" style-combination templates (indices 86-140, `GameCoreFightStyleToID` `sub_001A4700`), so "random style combo" is also possible. Use the pinned match seed or a host RNG, so the replay goldens stay unaffected when off |
| **All shop items purchasable** | Medium | Items carry a per-item lock flag (set test in `sub_000480E0`, 2-bit Locked/Unlocked/Owned state from `sub_00048810`), not a story-chapter check. Inserting every item id into the unlock set should show them; the id ranges are not enumerated yet. Persists into the save |
| **All items in the closet** | Medium | Five owned-item sets in the profile (P+0x14E4 / 0x153C / 0x1594 / 0x15EC / 0x1644; clothing, jewelry, gym, ...). Inserting through the set's vtable +0x1C, the way `PurchaseItem` does, marks items owned. Needs the id list per shop. Persists into the save |
| **Any fighting styles in Story** | Easy-medium | Hero styles are bytes S+0x140 (main), +0x141, +0x142 (hybrids, 0xFF none); +0x143 caches the style id. Set them, recompute +0x143, call `sub_00014C20` |

Other ideas found on the way:

- **Damage multipliers** (x0.5 / x2 / one-hit KO) through the gamecore style damage rates. They are
  re-applied each match, so easy to undo.
- **No regen**, **instant stun recovery**, **can't escape submissions**, **endless submissions**, and
  **no move repetition penalty**, all gamecore values.
- **Unlock all songs**, through the retail codes (§1.1).
- **Free shopping**, through the spend hook.
- **Max score**, at fighter +0x480 (to verify).
- **Show move labels**: the debug byte, already there.

## 5. Implementation notes

- **Where it goes:**
  - A Cheats page in the overlay (F1), next to Display, Controls, Mods, Interface, Diagnostics and
    Maintenance, so fight cheats can be toggled mid-match.
  - The launcher shows the same page for the Story one-shots and the hold-type defaults.
  - Settings group `[cheats]` in `settings.ini`, documented in `docs/settings-reference.md`, which its
    unit test keeps complete.
- **Hook types needed:**
  - per-step writes from `sub_00087700_enter`, which exists;
  - one entry hook each on `sub_001A50B0`, `sub_001A4900` and `sub_00013640`, plus a match-setup hook
    for movesets;
  - writes into the active profile slot for the Story one-shots.
  - All of these are game-side code in `src/patches/`, like `dev_unlock.c`. No toolkit change.
- **Saves.** Story cheats permanently change the profile once the game saves. Suggest:
  - a warning line on that group;
  - one-shot buttons instead of persistent toggles;
  - a note that the player should copy their save first.
- **Tests.** The harness pins fights for the `replay` golden, so the switches must be off in
  regression runs (as `unlock_all` is for `replay`). Add a unit test that each setting maps to the
  right poke, and a harness check per fight cheat that reads health and meter from `[TEST-STATE]`.
- **Achievements / online:** none in this port, so no fairness concern beyond Story saves.

## 6. Still unknown

- The Flash display caps for cash and points.
- Whether forcing the item lock alone shows every item.
- The item id ranges per shop.
- Whether "always Blazin'" breaks the KO cinematic.
- How fast motion behaves with extra steps.
- The head bone.
- What `LQA` / `g_LocalCheat`, `MSGS` and the "Holla" byte do.
- Whether byte 0x3B907E is really a random health/momentum switch.
- The score field.

Each of these needs a live run on a throw-away data root (`DEFJAM_DATA` plus a fixture copy), not
the owner's save.

## 7. Implementation and live checks (2026-10-24)

Built as `src/patches/cheats.c`, the `[cheats]` settings and the launcher's and overlay's **Cheats**
page (`src/hooks/pc_ui.cpp`), and checked on the Mac (M1 Pro, macOS 26) against the user's own dump,
with the harness's save guard (every run restored the save byte for byte). Unit test:
`tests/unit/test_cheats_native.py`.

| Cheat | How it works | Live check |
|---|---|---|
| Infinite health | `sub_001A50B0`'s entry hook zeroes a negative delta for the chosen fighters; each step puts +0x350 and +0x358 back at +0x354 (a fighter already at 0 is left down) | P1 stayed at 265.6/265.6 for a whole One on One while the CPU took 27 damage events and P1 none |
| Max Blazin' | after 300 fighting steps, the game's own gain `sub_001A5190(record, 1000.0)` for a fighter with +0x93E & 3 clear; the old direct writes only as a fallback | the gain sets ready itself; the CPU, given it, started Blazin' and ran it out over and over (+0x93E 1, 2, 6, 2, 1 ...) to the end of the fight |
| No health regeneration | the regeneration call (return address 0x001A5807) gets a delta of 0 | health went up 79 and 62 times in a stock fight, 0 times with the cheat |
| Giant / tiny | scale 1.4 / 0.6 on the model's scene node: model + 0x30, or for a model built from parts (+0xABB4 bit 0, the created hero) the node at MEM32(model + 0x9D0); scale at node + 0x2E0 | both fighters drawn at 1.4x over a normal-size crowd |
| Story cash | slot +0x20 = 999,999 | the clothing shop shows "You have $999,999" |
| Story development points | slot +0x28 = 99,999 | the gym shows "You have 99,999 dev points" |
| Story fighting styles | +0x140/+0x141/+0x142, +0x143 = table value + 1, then `sub_00014C20` on the slot | 2/0/none gave +0x143 = 110 (table 109), and the next Story fight's hero character index was 109 |
| Story hero | the match setup's hero slot rewritten at `Game.StartGame` (below) | the hero fought as Bo: character index 55 (coreID 56), Bo's model, the hero's name on the HUD; the fight ran to its result |

What the live runs corrected or added to the static read:

- **Pointers.** The game-data and profile objects are on the contiguous heap at 0x80000000 (game
  data 0x801A91E0, profile 0x801EA240 in these runs), not in the image's range.
- **When it is Story.** The Story map is `battle/chsVenue.swf`, so the screen name cannot say. Story is
  tracked from `Game.StartStoryMode` until the front end shows an `intMain/` screen again.
- **Style id.** +0x143 holds the style table's value plus one (the live profile: styles 1/none/none,
  +0x143 = 98, table 97), which is what the gym's purchase (`sub_0004EB60`) writes. In a Story fight
  the hero's character index is that table value (97, 109): the "CP" style templates.
- **Blazin' timing.** A CPU fighter made ready during the walk-in (phase 1 starts two steps into the
  match) started Blazin' at once and the match stopped there for good: the fight step was never called
  again, while the frame loop kept running. Five seconds of fighting first avoids it. Writing the meter
  fields directly left the player's ready state unused in the runs below, so the cheat now goes
  through the game's own gain, which an earned meter also takes.
- **Story hero.** In Story the match setup (game data + 0xC0) is built by `sub_000C1240` during
  `Game.LoadNextRunModule`, not by `Game.StartGame`, and with gd+0x344 = 0 (the slot that
  MEM32(gd)+0x14 names is the hero while byte MEM32(gd)+0x18 is set). So changing the fighter id
  (gd+0x1AAA) before `Game.StartGame` changes nothing. The setup marks the hero's slot with
  gd+0x1E8+slot = slot + 1; `sub_0008B730`/`sub_0008B440` then use the profile's parts model, and
  `sub_000871F0` takes the character index from gd+0x168+4*slot - 1. The cheat, at `Game.StartGame`
  (before the match is made), writes the chosen fighter's coreID (from `game/fighter.xml`) to
  gd+0x168 and gd+0x188, points gd+0x208 and gd+0x228 at that fighter's model and shadow names in the
  game's own loaded copy of `fighter.xml` (found as whole strings near the opponent's model name), and
  clears the mark. The hero's name (gd+0x218) and the profile stay as they are.
- **Display caps.** $999,999 and 99,999 points show in full on the shop and gym screens.
- **Time limits.** Infinite health keeps a fighter standing, but a Story fight with a time limit is
  still decided on points when the clock runs out: with both fighters at full health the CPU won the
  2:00 Club 357 fight on the decision.

Not exercised by the harness:

- A **player** starting Blazin' with the cheat. In the runs that logged it, the scripted pad's
  right-stick flicks and clicks never showed in the fighter's input words (+0x9E0, +0xA04), with or
  without the route's attack presses, so the player's activation is for a hands-on play-test. The ready state is the one
  the game's own gain sets for an earned meter.
- The fighter swap in a Story fight's cutscenes, the crib and the shops (out of scope for the first version), the
  Story result after a swapped fight beyond the result screen, and any fighter other than Bo.
- Windows and the Steam Deck.


# King of the Hill: design (2026-10-25)

A new FFNY Online room mode after Mortal Kombat's King of the Hill: a room of up to ten
players; two of them fight One on One, everyone else waits in a line and watches the fight.
The winner stays on, the loser goes to the back of the line and everyone moves up one place.

Status: the relay side is built (`netplay/relay/koth.go`, `protocol_koth.go`, tests in
`koth_test.go` and `protocol_koth_test.go`). The game side (`src/hooks/netplay.c` and the
online UI) is not started; section 9 lists what it needs. Nothing here is deployed.

## 1. What players see

- **KING OF THE HILL** in the ONLINE menu next to the room list and QUICK MATCH: create a room
  (name, public or code only, 2..10 players, rules per fight, venue by vote or at random) or
  quick-join one.
- **The hill screen** (the room screen of this mode): the line from top to bottom with names,
  fighters and wins in this room; a crown and "x wins in a row" on the king; the two called
  players highlighted with a ready countdown; away players greyed out at the bottom.
- **"You're up next"**: when the player is the next challenger (first in the line behind the
  current pair) a notice on the hill screen and over the fight they are watching; when called,
  a full-screen "YOU'RE UP - press A" with the ready countdown, so a player who looked away
  is caught before being skipped.
- **Watching**: everyone not fighting watches the fight full screen, running it on their own PC
  from the fighters' inputs, with a spectator HUD: both fighters' names, the king's crown and
  streak, the next three in the line, "you're up next" when it applies, and "watching" so the
  player knows their pad does nothing.
- **Between fights**: a short result card ("CARL beats DANA - CARL x3"), then the hill screen
  with the next pair called.

Picture-in-picture like MK (the waiting players see the fight in a small window while they
browse) is not possible: the game renders one scene with one camera, and the hill screen is a
front-end screen. A spectator either watches full screen or sits in the hill screen; the
client may let them switch between the two (section 9.4).

## 2. Room settings

The existing `Settings` block, unchanged in layout, with two reserved mode values:

| Field | King of the Hill meaning |
|---|---|
| `mode` | **0x40**: venue by vote of the two fighters; **0x41**: venue drawn at random for each fight. Every fight itself is One on One (`START.mode` 0). |
| `max` | 2..**10** (other modes stay 2..4) |
| `rounds` | the rules of every fight, as in other rooms (0 = the game's default) |
| `pool` | the venues to vote on or draw from (1..32, unsafe venues left out as everywhere) |
| `name`, `public` | as in other rooms |

The host (the lowest slot present) can change the settings while no vote or fight runs; that
calls the current pair again with a fresh ready time. A room cannot switch between a King of
the Hill mode and another mode (`ERROR 8`), because its ROOM layout and members differ; 0x40
and 0x41 can be swapped.

Why vote by the two fighters only: they play the venue; ten votes would make the vote slow
(everyone must answer or the 15 s run out) for a room whose point is pace. Random (0x41) is
the MK behaviour and the fastest; it is the default of the client's create screen.

## 3. The line and the rotation

The relay keeps every member in one ordered **line**. `line[0]` is the king when there is
one; the first two members of the line who are not away are the **pair**: the two who fight
next (or are fighting). A new member joins at the back, also during a fight.

| Event | New line |
|---|---|
| W beats L | `[W, rest..., L]`: the winner stays at the top, everyone else moves up one place, the loser goes to the back. W's streak +1 if W was king, else W becomes king with streak 1. |
| Draw | `[rest..., challenger, king]`: both to the back, the challenger first; no king, streak 0. |
| Reports disagree, or none in 30 min | as a draw (`how` = void) |
| A fighter leaves during the fight | the leaver takes the loss: the other is the winner, as above |
| A called player leaves before the fight starts (calling or voting) | removed; the next in the line is called; the one still called keeps its ready flag; no result |
| A waiting player leaves | removed; everyone behind moves up |
| The king leaves while waiting alone | no king |

**Draw: both to the back, not a rematch.** A rematch can loop: two friends (or two idle players
timing out with full health) can hold the hill forever with draws while eight people wait. Both
to the back keeps the line moving whatever happens in the fight, cannot be farmed, and is cheap
for a real king because FFNY draws are rare (a time-out with equal health, a double KO). It is
also the same rule as a void fight, so the relay has one rule for "no winner". With only two
players in the room it is a rematch anyway (they are the next pair again, in swapped order).

The challenger goes ahead of the old king on a draw because it has waited longer.

## 4. Ready check and AFK

When a pair is called the relay starts the **ready time** (`KothReadyTime`, 20 s). Each of the
two must send `READY 1` (with its fighter) within it; the king too, because the king may have
walked away during the fight that crowned them. As soon as both are ready the vote starts
(mode 0x40) or the fight starts at once (0x41, or a pool with one venue).

- A called player who is not ready when the time runs out is **skipped**: moved to the back of
  the line. A skipped king loses the crown and the streak. The relay then calls the new pair
  with a fresh ready time; a player who was ready stays ready.
- After **two skips in a row** (`KothAwayAfter`) a player is **away**: still in the room (they
  can chat and watch) but never called, so the room does not wait 20 s for them every round.
  Any `READY 1` brings them back at their place in the line. Fighting a fight clears the count.
- `READY 1` from a player who is not called only clears "away" and records the fighter; being
  ready counts only when called, so a stale ready from minutes ago cannot start a fight for a
  player who is no longer there.
- While voting or fighting a fighter cannot change its fighter; spectators can.
- With fewer than two players who are not away the hill **waits** (no countdown).

The client should make readying one press: on "YOU'RE UP" the A button sends `READY 1` with
the last fighter, and the fighter select is optional.

## 5. Starting a fight

When both are ready (and the venue is chosen) the relay draws a fresh 32-bit seed and sends:

- **`START`** to the two fighters, exactly as in other rooms: `mode` 0 (One on One), the room's
  `rounds`, the venue, and two players (their real room slots, in slot order, with fighters).
  To the fighters' games a King of the Hill fight is an ordinary two-player hub match.
- **`WATCH`** (new, the same body as START) to every other member present at that moment. The
  relay remembers who got it: those are the fight's **watchers**.

A lost START or WATCH is recovered as in other rooms: a `VOTE` during the fight re-sends the
sender's START (a fighter) or WATCH (a watcher). A member who joined during the fight gets
neither and watches the next one (section 7.3).

## 6. Results

The relay must know the winner to rotate the line; it does not run the game, so the fighters
tell it, with the new **`RESULT`** (the START seed and the winner's slot, or 0xFF for a draw),
repeated every 400 ms until a `HILL` shows the fight is over (`fights` advanced). Rules:

- Two RESULTs that agree decide the fight. Two that disagree void it (both to the back): the
  games went out of sync, and the relay cannot tell who is right.
- One RESULT and no second within `KothResultWait` (10 s): the one decides. The other game
  crashed or lost its connection; making the room wait longer punishes eight people for it.
  King of the Hill is unranked, so a lying client gains a crown in one room, nothing more.
- A RESULT from a spectator, for another fight, or naming a slot that is not a fighter changes
  nothing (`ERROR 8` for the last).
- A fight with no RESULT after `PlayTimeout` (30 min) is void.
- `OVER` in a King of the Hill room changes nothing (answered with ROOM and HILL).

**Accounts.** Only the two fighters report the match to the account service, as today
(`POST /api/matches`, key `<room>-<SEED>-1`, the two fighters as players, `"start"` /
`"result"` / `"left"` events). The stats count it like any hub match: both reports must agree,
a leaver gets the loss. The report names match positions (0 and 1), not room slots, so a
fighter in room slot 7 is still a valid report (the account service accepts positions 0..3
only, `accounts/stats.go` `valid`); no account-service change is needed. Spectators never
report: they have no position in the match (the client must not call `account_report_match`
while watching). The relay posts nothing to the account service for King of the Hill (no pairing: it is not ranked).
The king's streak is a room thing, shown in the room; it is not stored.

## 7. Spectating

### 7.1 How a spectator watches

A spectator's game runs **the same lockstep simulation** of the 1v1 as the two fighters, from
the same START body (seed, venue, fighters, rules), fed with both fighters' input entries. It
needs no video stream: the fighters' `P_STEPS` (32-byte entry per step per fighter, the last 8
steps repeated in every packet) are exactly what the fighters' own games consume, and the relay
already forwards them.

During a fight the relay forwards **one way**:

| DATA from | goes to |
|---|---|
| a fighter | the other fighter and the fight's watchers |
| a spectator | the other spectators only (never a fighter) |
| anyone, between fights | every other member (room voice, as in other rooms) |

So nothing a spectator sends can reach the lockstep, and a spectator's voice never disturbs
the fighters. Spectators send nothing for the simulation at all: no `P_STEPS`, `P_HASH`,
`P_RTT`, `P_REMATCH`.

### 7.2 Spectators are never waited for

- They are not in START's player list, so the fighters' lockstep never waits for their entries,
  their checksums or their ping (the delay agreement `P_RTT` runs between the two fighters).
- The relay never waits for them either: the ready check, the vote and the result involve the
  two fighters only. A spectator who leaves, freezes or times out changes nothing for the fight.
- A spectator whose PC loads the venue slower simply runs behind: the inputs are buffered and it
  plays the fight from step 1 a few seconds late. It must keep the whole fight's inputs (the
  fighters' ring of 512 steps, 8.5 s, is too short for a slow load): about 3.8 KB per second of
  fight, 2.3 MB for ten minutes.
- If it falls too far behind (more than about 20 s), or the next WATCH arrives while it still
  watches, it drops the old fight and goes to the next one.

### 7.3 Joining late

A member who joins during a fight cannot run it: the simulation needs every input from step 1,
and the fighters only repeat the last 8 steps. The relay therefore does not forward the
fighters' DATA to a late joiner (it would be useless traffic) and sends no WATCH; the late
joiner sits in the hill screen with "fight in progress: A vs B" (from HILL) and watches from
the next fight. Fights are one to three minutes, so the wait is short.

Later, if wanted: a relay-side input log. The relay would keep the current fight's entries
(it would have to parse `P_STEPS`, which today is opaque to it, and dedupe the 8-fold
redundancy: about 2 × 32 bytes per step, under 1 MB for a long fight) and replay them to a late
joiner, who fast-forwards the simulation without drawing until it catches up. Not built: it
ties the relay to the lockstep format, and the fast-forward needs a game-side "simulate
without presenting" that does not exist.

### 7.4 What can go wrong for a spectator

- **Lost input.** A spectator cannot ask for a resend (it sends nothing, and the fighters must
  not be burdened). A gap longer than the 8-step redundancy (133 ms of loss in a row) cannot be
  filled: the spectator stops watching ("lost the fight's feed") and waits for the next fight.
- **Desync.** A spectator gets the fighters' `P_HASH` and compares them with its own: a
  mismatch means its picture is wrong; it stops watching. It never affects the fight.
- **Rollback.** With `RECOMP_ROLLBACK=on` the fighters predict and correct, but the entries
  they send are the confirmed ones. A spectator never predicts: it runs only on entries it has
  for both fighters, a fixed few steps behind, so it never rolls back.

### 7.5 Bandwidth

Every fighter packet is copied to every watcher. A fighter sends about 60 to 120 DATA a second
(STEPS, voice); with eight watchers the relay sends about 9 copies of each: roughly 0.5 MB/s
(4 Mbit/s) out per full King of the Hill room. The Hetzner box carries this for a handful of
full rooms; if it becomes a cost, the fighters can send STEPS at 30 Hz with 4 new steps each,
or the relay can thin the copies it sends to spectators (every second packet: the 8-step
redundancy covers it).

## 8. Protocol additions

Same header (version 1). New types from 0x20; a relay without them drops them silently.
All integers little-endian.

### 8.1 Announcing support

| Type | Name | Direction | Body after the header |
|---|---|---|---|
| 0x20 | `CAPS` | client -> server | `token` 8, `caps` u32 (bit 0: King of the Hill) |
| 0x20 | `CAPS` | server -> client | `caps` u32 (what the relay supports; bit 0: King of the Hill) |

A client sends CAPS once after WELCOME (lobby mode). No answer within 2 s: the relay is older,
and the client hides KING OF THE HILL. **A client that did not announce bit 0 is never put
into a King of the Hill room**: CREATE, QUICK or JOIN of one gets `ERROR 8`, so does HELLO
mode 1 with its code, and LIST never shows such rooms to it. Old clients matter here: their
ROOM parser drops a ROOM with more than four players, and their START handler assumes slots
0..3. Every other room behaves exactly as before.

### 8.2 Room settings, LIST, QUICK

- `Settings.mode` 0x40 / 0x41 as in section 2; `max` up to 10 for them.
- `LIST` with mode 0x40 or 0x41 lists both kinds of King of the Hill rooms. `ROOMS` entries
  show `max` up to 10 and the ROOM phase (0..2).
- `QUICK` with mode 0x40 or 0x41 and `max` 2..10 joins the public King of the Hill room of the
  client's build with a free slot and the most players (then the oldest), **in any phase**, or
  creates "Quick: <name>" with that mode and max and the quick pool.
- `JOIN` of a King of the Hill room works in any phase (the new member joins the back of the line).

### 8.3 ROOM in a King of the Hill room

Layout unchanged; up to 10 players, slots 0..9. Its `phase` is 0 while waiting or calling, 1
while the fighters vote, 2 during the fight; `ready` is the called players' ready flag;
`candidates`/`votes`/`ms_left` are the fighters' vote. Every ROOM of such a room is followed by
a HILL; while calling or voting a HILL alone goes out once a second for the countdown.

### 8.4 HILL (0x21, server -> client)

| Offset | Size | Field |
|---|---|---|
| 0 | 6 | `room` |
| 6 | 1 | `phase`: 0 waiting (fewer than two can fight), 1 calling, 2 voting, 3 fighting |
| 7 | 1 | `king`: slot, 0xFF none |
| 8 | 2 | `streak`: the king's wins in a row |
| 10 | 2 | `ms_left`: calling: ready countdown; voting: vote countdown; else 0 |
| 12 | 4 | `seed`: fighting: the fight's START seed; else 0 |
| 16 | 2 | `fights`: fights finished in this room (wraps); a change means a new result |
| 18 | 1 | `last_winner`: slot (a draw: the old king/first fighter), 0xFF none |
| 19 | 1 | `last_loser`: slot (a draw: the challenger), 0xFF none |
| 20 | 1 | `last_how`: 0 none yet, 1 won, 2 draw, 3 the loser left, 4 void (reports disagreed or timed out) |
| 21 | 1 | `count` (0..10) |
| 22 | 4 x count | per player in line order: `slot` u8, `state` u8 (0 waiting, 1 called, 2 called and ready, 3 fighting (voting or fighting), 4 away), `wins` u16 (fights won in this room) |

The first entry is the king when `king` is not 0xFF. "You're up next" is the first entry after
the two in states 1..3 whose state is 0. At most 62 bytes of body.

### 8.5 WATCH (0x23, server -> client)

The body of START exactly: `seed` u32, `mode` u8 (0), `rounds` u8, `venue` u8, `players` u8 (2),
per player `slot` u8, `fighter` u8. Sent to every member who is not fighting when the fight
starts; re-sent on that member's VOTE during the fight.

### 8.6 RESULT (0x22, client -> server)

`token` 8, `seed` u32 (the fight's START seed), `winner` u8 (the winner's slot, 0xFF a draw).
From the two fighters only; repeated until HILL `fights` changes. Outside a room `ERROR 9`, in a
room of another mode `ERROR 8`.

### 8.7 Changed meaning of existing messages in a King of the Hill room

| Message | In a King of the Hill room |
|---|---|
| `READY` | allowed in every phase (section 4) |
| `VOTE` | the two fighters while voting; anyone during the fight re-requests START/WATCH; spectators while voting: `ERROR 8` |
| `OVER` | no effect (the fight ends with RESULT); answered with ROOM + HILL |
| `PEER` | never sent (even with `max` 2) |
| `DATA` | routed as in section 7.1 |

## 9. What the game client needs (src/, not started)

### 9.1 Protocol and session (`src/hooks/netplay.c`)

- Send CAPS after WELCOME; remember the relay's caps; hide the mode without bit 0.
- Parse HILL into the room view; accept ROOM with up to 10 players (today `count > MAXP` drops
  it), ROOMS entries with `max` up to 10.
- **Slots 0..9.** `pos_of_slot[MAXP]` and the T_DATA mapping `b[6] < MAXP` assume slots 0..3; a
  fighter in slot 7 would have its STEPS dropped. Same for voice (`voice.c` `SLOTS` 4): voice
  from slot 4..9 is ignored. These need a separate "room slots" bound (10) from the "fighters in
  a match" bound (`NETPLAY_MAX_PLAYERS`, 4).
- START handling stays as is for the fighters. In a King of the Hill room: no rematch screen
  (`P_REMATCH` off), one match per START; at the result send RESULT (winner position ->
  `start_slots`) every 400 ms until HILL `fights` changes, then back to the hill screen.
- on_room's "a player of the running match has left" logic must look only at the two fighters.
- Report to the account service as today for fighters; never for a spectator.

### 9.2 Spectator mode (the hard part)

A new session state "watching", entered on WATCH:

- Start the same automatic match as a fighter would (`automatch_request` with the START body,
  both fighters as remote pads, the free places as the mode's CPU rule gives them), with **no
  local position**: this PC's pad feeds no fighter, and every local-entry path (`latest_local`,
  `send_steps`, `P_HASH`, `P_RTT`, the stall toasts that name "the other player") is off.
  `N.pos` is used as "this PC's fighter" throughout netplay.c; watching needs a value meaning
  "none" and an audit of every use.
- The step loop consumes both fighters' entries only; it waits for them (it is behind anyway)
  but never gives up the match the way a fighter does after `STALL_LIMIT_MS`, and never
  reports a leaver. It keeps the whole fight's entries (7.2), not the 512-step ring.
- Compare received P_HASH with its own at the same step; on a mismatch, stop watching.
- The crowd restart at the fight's start (`netplay.c`, "crowd restarted for the match") and
  everything else that makes the two fighters' games agree must happen identically on the
  spectator: it is a third copy of the same deterministic match. The fighters' games already
  agree on everything from the START body and the entries, so a spectator that does the same
  should too; this is the main thing to prove in testing (three copies, hashes compared).
- Leaving the fight early (menu button) returns to the hill screen; the fighters never know.
- When the fight ends (KO screen, or HILL `fights` changes while still watching), skip the
  results/rematch flow and return to the hill screen.

### 9.3 Screens

- ONLINE menu entry and the create screen (mode vote/random, max 2..10, rules, pool).
- The hill screen (section 1), from ROOM + HILL: the line, crown and streak, called pair with
  the ready countdown, away players, "you're up next", the last result card, chat/voice.
- "YOU'RE UP" prompt with one-press ready.
- The spectator HUD over the fight: fighters' names, the king's crown and streak, the next three
  in the line, "you're up next", a "watching" tag; drawn by the overlay (like the netplay
  toasts), not by the game.

### 9.4 Optional

- A "back to the hill screen" toggle while watching, and "watch" from the hill screen during a
  fight that started while the player was there (it has the WATCH; it re-enters the match only
  if it kept the inputs).

## 10. What is hard, honestly

1. **Spectators running the simulation.** The whole design rests on a third PC reproducing the
   fight bit for bit from inputs it only receives. Two PCs already do (with checksums every 60
   steps), so it is plausible, but the spectator's game must start the match, load the venue and
   restart the crowd exactly like a fighter's while controlling nobody, and the client code
   assumes everywhere that this PC is one of the players. Expect desyncs found only with three
   real copies running.
2. **No catch-up.** Late joiners and spectators who lose more than 133 ms of input in a row miss
   the fight. Fixing it needs either a relay-side input log or a game-side fast-forward.
3. **One scene, one camera.** No picture-in-picture; spectators watch full screen or sit in the
   hill screen. The spectator HUD has to be drawn by the overlay, outside the game's own HUD.
4. **Bandwidth.** Ten-player rooms multiply the relay's outgoing traffic by up to nine.
5. **Trust.** One fighter's RESULT decides after 10 s, so a modified client can claim wins it did
   not earn in its room. Acceptable for an unranked mode; ranked King of the Hill would need the
   account-service agreement rule instead.

## 11. Relay implementation

- `protocol_koth.go`: CAPS, RESULT, HILL, WATCH; `Settings.max` up to 10 for modes 0x40/0x41
  (`maxPlayers`), the ROOM decoder accepts ten players in such a room only.
- `koth.go`: the line, the ready check, skips and away, the vote by the fighters or a random
  venue, START + WATCH, one-way forwarding, RESULT agreement and the single-report timeout,
  leaving, the play timeout; QUICK for the mode.
- `lobby.go`, `relay.go`: rooms hold up to 10 slots (other rooms keep their `max` 2..4), and
  every room path branches to the hill only when the room's `koth` is set, so other rooms run
  the same code as before. Config: `KothReadyTime` (20 s), `KothResultWait` (10 s),
  `KothAwayAfter` (2).
- Tests: rotation (win, defend, draw), forwarding (fighter to fighter and watchers, spectator to
  spectators, late joiner excluded), AFK skip and away and coming back, king skipped, leaving
  mid-fight and during the call and the vote, result agreement / void / single report / stale /
  spectator, play timeout, the vote, quick, the CAPS gate for old clients, ten players, codec.

# Spectator mode (watching online fights)

Status: design for version 1, written before the code (2026-11-01). Relay, game and room UI of v1
are built on branch `spectator-mode`; section 9 lists what v1 cannot do yet and section 10 the
owner's decisions it needs.

First test (2026-11-02, `dj.py scenario online --matches 2 --spectators 1`, logs
`logs/dj/20261102-032021-online`): two copies played two room matches with a knockout each, a
third copy watched both from the room. The spectator took the players' delay (2) from their
`P_RTT`, loaded the first venue 6 s after the players (28.4 s against 22 s) and ran the fight
behind them from its own entry store; it compared 60 checksums with the players' (30 checkpoints
x 2 players, steps 60..900+ of both matches), all equal, no desync; the winners agreed; it went
through both summaries by itself and was back in the room for the second match. Relay unit tests:
`server/relay/spectate_test.go`.

## 1. Why a spectator needs no game state

An online fight is deterministic lockstep (`docs/netplay.md`, `src/hooks/netplay.c`): every PC of a
room runs the same simulation from the room's `START` (seed, mode, rules, venue, the players' room
slots and fighters, the created-fighter records it names) and, per simulation step, every player's
32-byte pad entry (`P_STEPS`, sent `delay` steps ahead, the last 8 steps repeated in every packet).
Every 60 steps the PCs exchange checksums (`P_HASH`). A third PC that gets the same `START` and every
player's entries runs the same fight, bit for bit, and can check itself against the players' own
checksums. It needs no snapshot, no video and nothing from the players but what they already send.

King of the Hill already has the relay half of this: the waiting players get `WATCH` (the `START`
body) and, during the fight, the fighters' `DATA` (`koth.go` `hillRecipients`). Its game side (phase
1) only shows the names on the hill's watching screen; running the fight there was planned as its
phase 2 (`docs/research/king-of-the-hill.md` 7 and 9.2). Spectator mode builds that game side for
ordinary rooms; King of the Hill can take it over later (section 9).

## 2. Joining a room as a spectator

- **Support is announced with CAPS bit 4** (`16`, `CapSpectate`): a client sends it in its `CAPS`; a
  relay that has spectator mode answers it in its own `CAPS`. A game shows WATCH only when the relay
  answered the bit. Older relays drop the new message type; older clients never see a spectator (the
  relay sends them nothing new, see "Compatibility").
- **New message `SPECTATE` (0x2B, client -> server):** `token` 8, `room` 6. The client must be in
  the lobby (in no room). It is put into the room as a spectator, answered with `ROOM` (`you` =
  **0xFD**, "spectator") and, for created fighters, every player's `RECORD`. Requests are
  idempotent like the others: `SPECTATE` of the room the client already watches answers `ROOM` again.
- **Errors:** 1 no such room; 2 the room's spectator places are taken; 3 another build; 8 not allowed
  (the client did not announce bit 4, the room is a King of the Hill, ranked or legacy code room, or
  it has no players); 9 the client is in a room already.
- **Which rooms:** every room but ranked, legacy (`HELLO` mode 0/1) and King of the Hill rooms. Public
  rooms are watched from the room list; a room that is not listed only by its code (the code is the
  invitation, as for joining). Ranked and King of the Hill are owner decisions (section 10).
- **How many:** `Config.MaxSpectators`, default **4** per room, `relay -spectators N` (0 turns
  spectating off: the relay then also leaves bit 4 out of its `CAPS` answer).
- **Phase:** a spectator may enter in any phase. In v1 it watches from the next `START` on; a room
  that is already playing shows "a match is on" until then (catching up is v2, section 8).
- A spectator never counts as a player: not in `ROOM.players`, not in `START`, not in the ready
  check, the vote, `OVER`, the room's player count in `ROOMS`, the quick-match search or the ranked
  pairing. A room whose last player leaves is closed and its spectators are sent `ERROR 1` (the room
  is gone) and are back in the lobby.

## 3. What the relay sends a spectator

- `ROOM` whenever the room changes, like to a player, with `you` = 0xFD. To clients that announced
  bit 4 every `ROOM` carries a **trailer** after the players: `count` u8, then per spectator its
  `name` string, in the order they came (a game shows "WATCHING: ..."). Clients without bit 4 get
  the `ROOM` as before (their parsers stop after the players anyway; nothing relies on that).
- At the vote's end, with `START` to the players, **`WATCH` (0x23, the `START` body, created-fighter
  trailer included)** to every spectator present. Those spectators are the match's **watchers**. A
  `VOTE` from a watcher while the room plays re-sends its `WATCH` (lost-`WATCH` recovery, as players
  recover `START`).
- **During the match:** every `DATA` from a player whose payload is `P_STEPS` (4), `P_HASH` (5) or
  `P_RTT` (6) goes to the watchers too, verbatim, with the player's slot. Nothing else (live pads,
  room pings, rematch flags) goes to spectators.
- **Voice (owner's decision, 2026-11-02):** a player's voice `DATA` (`P_VOICE`, 7) goes to every
  spectator of the room in any phase, lobby and match, watching or not, like to the other players
  (same per-source rate limit). Spectators hear, they do not talk: the game sends no voice while it
  spectates, and the relay forwards nothing a spectator sends. The host cannot turn spectators away.
- **Spectators never reach the players:** a spectator's `DATA` is dropped by the relay (not forwarded,
  not recorded, not counted for the silence watch), its `READY`, `SETTINGS` and lobby-phase `VOTE`
  get `ERROR 8`, its `OVER` changes nothing. So a spectator cannot inject inputs, checksums or
  pings into a match, and cannot hold up the vote or the room's return to the lobby.
- **No waiting on spectators:** the relay keeps no per-spectator buffer and waits for no
  acknowledgement; it copies datagrams as they come, like it does between players. A spectator that
  falls behind (a slower load, a slower PC) only falls behind on its own PC (section 5); one that
  stops answering times out after 15 s like any client.

### Compatibility

| Relay \ client | old client | new client |
|---|---|---|
| old relay | as today | no `CAPS` bit 4 in the answer: WATCH hidden; `P_RTT` gains a byte old games ignore |
| new relay | never a spectator in its rooms' `ROOM`s (no trailer), no `WATCH` unless in a hill; `DATA` as before | spectators |

The players' games need no change to be watched, except one byte: `P_RTT` gains byte 13, the
**agreed input delay** (0 until the sync point fixes it). A spectator takes the match's delay from
it instead of re-deriving it from the pings (a player whose wait for a ping timed out derives
another delay than one that got it). Players of older builds do not send it; the spectator then
derives the delay from the pings as the players do.

## 4. Anti-cheat and privacy

- A spectator learns nothing a player does not: the inputs, the seed and the result are shown on
  every player's screen anyway. Fighting games have no hidden information. It does not get the
  players' live pads or rematch choices (it hears their voice chat, as everyone in the room does).
- It cannot change a match (section 3: everything it sends towards the players is dropped), it
  never reports a match to the account service, never takes part in ranked pairing, never counts
  in anti-cheat reports (silence, health), and its game never sends `RESULT`, `OVER` or `P_HASH`.
- A spectator's game checks every player checksum it gets against its own. A mismatch only means
  its own picture is wrong; it is logged like a player's desync and the spectator is told.
- Ranked rooms are not watchable in v1 (section 10): a friend of one player watching could still
  only see what both players see, but ranked play has rules of its own.

## 5. The spectator's game

- **Joining:** `netplay_lobby_spectate(code)`. The room screen of a spectator shows the players'
  cards, a SPECTATING banner and "B LEAVE"; ready, fighter and settings are not offered.
- **`WATCH` = the spectator's `START`.** The game sets up the same automatic match the players set
  up (`automatch_request`, the same `AutoMatch` from the same body: the players on their ports, CPU
  fighters from the same seed), with **no local position** (`N.pos` = -1, `N.spectating` = 1):
  - every position is remote; this PC's own pad drives no fighter (the toolkit's netplay mapping
    puts it on a port no player uses; in a match of four players it reads, outside the fight, as
    the fourth player's port: section 9);
  - nothing of the lockstep goes out: no `P_STEPS`, `P_HASH`, `P_RTT`, `P_REMATCH`, `P_LIVEPAD`,
    `OVER` or `RESULT`; no account report, crash report or anti-cheat sample;
  - the input delay is the players' (from their `P_RTT` byte 13, else from their pings);
  - the step loop takes every player's entry for the step, waiting for it as a player waits for
    the others, but never gives up the match the way a player does: a feed that stops mid-fight
    stops the watching (a notice says START then Quit goes back to the room);
  - it never rolls back and never predicts: it runs only on entries it has;
  - it plays from a small buffer: at the fight's first step, and after any stop for a late entry, it waits
    (at most 400 ms) until it holds every player's entry 6 steps ahead, then plays one step a frame, so an
    entry later by less than that does not stop it. Not after the decision: the players no longer predict
    there and the feed itself slows down. `RECOMP_SPECTATE_BUFFER=<steps>` (0: none). The match's stops
    and fills are logged at its end (`watching: N steps played, ...`).
- **Every entry of the match is kept** (not only the players' 512-step ring): a spectator whose
  venue loads more slowly than the players' starts the fight seconds later and plays it seconds
  behind, from step 1. Kept per player in 4096-step chunks, up to 30 minutes (2 x 32 bytes x 60
  steps = 3.8 KB per second of fight; at most about 3.5 MB per player).
- **Checksums:** at every 60th step the spectator's checksum is compared with each player's own
  (`P_HASH`), whichever comes first; the players' are kept 64 checkpoints back, so a spectator a
  minute behind still compares. Log lines as the players' (`in step: N checksums compared and
  equal`, `desync in match ...`), so the test tools read a spectator's log like a player's.
- **What it shows:** the players' versus screen (with SPECTATING on it), then the fight with a
  "SPECTATING: A vs B" notice; its pause is closed (as for players during a room match).
- **After the KO:** the winner's scene runs on the players' entries as long as they come; then the
  spectator's game goes through the summary and the results by itself (it taps A on player 1's port
  until the game is back in the main menu, and a Rematch on its results goes back to the room as in
  King of the Hill) and returns to the room screen.
- **The next match:** the spectator stays in the room; the room's next `START` brings the next
  `WATCH`. A match that `WATCH` sets up while the spectator is still on the results starts once it is
  back in the menus (automatch waits for the main menu, as for every match). A Rematch the players
  choose on their results has no new `START`; v1 did not show it. Since build 47 the spectator's game
  stops on the Rematch menu (it comes after the reward screen's save) and waits: the players' choices
  reach it (`P_REMATCH`, `CAPS` bit 6), and once every player chose Rematch (or the rematch's entries
  arrive) it calls `Game.Rematch()` as the menu would and watches the rematch like the match; when a
  player goes back to the room, the room is back in its lobby, START is pressed or three minutes pass,
  it chooses No and goes back to the room.
- **Coming while the match runs (CAPS bit 11, `WATCHLATE` 0x36):** a player report (2026-11-07) found
  that a spectator who entered while the players fought, or sat on their results, saw nothing until they
  went back to the room: their rematches bring no `START`. A game that announces bit 11 now gets, on
  `SPECTATE` into a playing room, `WATCHLATE` (the running match's `START` body) and each player's latest
  `P_RTT` (the agreed input delay), and is a watcher at once. It holds that match and keeps the players'
  entries and choices; it sets the fight up as a room match once the players' next fight of the `START`
  starts (all its first entries are here, or every player chose Rematch), numbered as theirs
  (`N.match_count`, so the match id agrees) and with its seedings counted on from the fights before
  (`seed_base`: the players' rematches seed every generator once, as a set-up from the menus does; the
  checksums tell if a venue does otherwise). A fight already under way is skipped for the next. The room
  back in its lobby ends the wait; its next `START` brings a `WATCH`. A `VOTE` while waiting re-sends the
  `WATCHLATE`. Older games still wait for the next `START`.
- **Leaving:** B in the room leaves it (`LEAVE`); during the fight START stops watching and, since
  build 47, takes the spectator straight back to the room: the game runs the pause menu's own Quit
  (`sub_000CD0D0` on its confirm page; the pause menu itself never opens for a spectator, whose pad sits
  on a port no fighter uses), which leaves the fight for the match-type screen and the room. A feed
  that stops does the same. The players never know.

## 6. Bandwidth

A player sends about 60 `P_STEPS` a second (about 270 bytes each), a `P_HASH` a second and a few
`P_RTT` before the fight; a spectator of a 1v1 therefore receives about 120 datagrams and 33 KB a
second (260 kbit/s). Four spectators of every 1v1 on a relay multiply its outgoing fight traffic by
three. Live pads are not copied; voice is (about 8 KB/s per talking player, only while they talk).
If it becomes a cost: thin the copies (every second `P_STEPS` still overlaps by four steps), or
let the relay forward only the newest entry per step (it would parse `P_STEPS`, as the recorder
already does).

## 7. Protocol summary

| Type | Name | Direction | Body after the header |
|---|---|---|---|
| 0x20 | `CAPS` | both | bit 4 (`16`): spectator mode |
| 0x2B | `SPECTATE` | client -> server | `token` 8, `room` 6 |
| 0x19 | `ROOM` | server -> client | `you` 0xFD for a spectator; to bit-4 clients a trailer: `count` u8, per spectator `name` string |
| 0x23 | `WATCH` | server -> client | the `START` body (also in ordinary rooms now, to their spectators) |
| 0x36 | `WATCHLATE` | server -> client | the `START` body of the match already running, to a client with `CAPS` bit 11 entering it |
| 0x04 | `DATA` | server -> spectator | a player's `P_STEPS`, `P_HASH`, `P_RTT` only, during the match |
| (payload) | `P_RTT` | game -> game | byte 13: the agreed input delay, 0 before the sync point |

## 8. Version 2: watching a fight already running

The relay records every fight (`record.go`, `FFNYREC1`: each player's new steps, deduplicated). A
spectator that enters during a match could ask for it (`SPECTATE` gives the `WATCH` and then the
recorded entries from step 1 in a burst, paced), and its game would run the fight from step 1
without drawing until it reaches the live steps (a "fast forward": the game loop runs updates
without presenting, which does not exist yet). The spectator store of v1 already keeps a whole
fight, so v2 needs the relay's replay feed and the fast-forward, not a new input path.

## 9. What v1 does not do

- Join a fight already running (v2, section 8). (Rematches the players choose on their results are
  watched since build 47; a spectator that comes during a match watches its next fight since bit 11.)
- King of the Hill: its watchers keep the phase-1 names screen; switching them to this game side is
  the hill's phase 2 (the hill's own results and calls need care).
- A match of four human players: the spectator's own pad sits on the fourth player's port outside
  the fight (the toolkit maps the local pad to a port 0..3); during the fight it has no effect.
- Ranked rooms; spectators in the room list (`ROOMS` shows players only); talking spectators.
- (Leaving mid-fight in one press: START, since build 47.)

## 10. Decisions for the owner

1. **How many spectators per room** (v1: 4, `-spectators`), and whether spectating is on at all on
   the public relays at first.
2. **Who may watch:** v1 lets anyone watch a listed room and anyone with the code watch an unlisted
   one. Alternatives: a room setting "allow spectators" (host decides), friends only, or never in
   unlisted rooms.
3. **Ranked and King of the Hill:** v1 refuses both.
4. **Visibility:** players see "WATCHING: names" in the room. Should the room list show a watcher
   count, and should players be able to turn spectators away?
5. **Voice:** decided 2026-11-02: spectators hear the room's voice chat and never talk.

# Netplay relay protocol (version 1)

Online play pairs two copies of the port through a small relay server (for example on a
Hetzner VPS). The server only matches players into rooms and forwards their packets; it
never runs the game and never sees game data. Everything game-specific (inputs per
simulation step, checksums, seeds) is an opaque payload to it.

```
 player A (host, slot 0) --UDP--> relay --UDP--> player B (guest, slot 1)
                         <--UDP--       <--UDP--
```

Why a relay and not direct connections: no port forwarding, no NAT traversal, and both
players see the same path. The cost is one extra hop (a few milliseconds to a nearby
data centre).

## Transport

- UDP, default port **27777**. One datagram is one message; at most **1200 bytes**.
- All integers little-endian. Strings are UTF-8, length-prefixed (u8), never NUL-terminated.
- Every message starts with the same 6-byte header:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `DJNP` (0x44 0x4A 0x4E 0x50) |
| 4 | 1 | protocol version, `1` |
| 5 | 1 | message type |

Messages with a wrong magic, an unknown version or type, or a bad length are dropped
silently (no reply), so the server cannot be used to reflect traffic.

## Messages

### Client to server

| Type | Name | Body after the header |
|---|---|---|
| 0x01 | `HELLO` | `mode` u8 (0 = create room, 1 = join room), `room` 6 bytes (ASCII `A-Z2-9`; zeros when creating), `build` 8 bytes (build fingerprint, must match the other player's), `name` string (1..24 bytes) |
| 0x04 | `DATA` | `token` 8 bytes, `payload` (the rest, 1..1150 bytes) |
| 0x05 | `PING` | `token` 8 bytes, `nonce` u32 |
| 0x07 | `BYE` | `token` 8 bytes |

### Server to client

| Type | Name | Body after the header |
|---|---|---|
| 0x02 | `WELCOME` | `token` 8 bytes (random; required in every later message), `slot` u8 (0 = host, 1 = guest), `room` 6 bytes |
| 0x03 | `PEER` | `slot` u8 of the peer, `name` string. Sent to both players when the room is full, and again to the remaining player (with name length 0) when the peer left or timed out |
| 0x04 | `DATA` | `from` u8 (sender's slot), `payload` (verbatim) |
| 0x06 | `PONG` | `nonce` u32 (echoed) |
| 0x08 | `ERROR` | `code` u8, `text` string |

Error codes: 1 room not found, 2 room full, 3 build mismatch, 4 bad name, 5 server full,
6 rate limited, 7 unknown token (the client should start over with `HELLO`).

## Rooms

- `HELLO` with mode 0 creates a room with a fresh random 6-character code from the
  alphabet `ABCDEFGHJKLMNPQRSTUVWXYZ23456789` (no 0/O, 1/I) and makes the sender slot 0.
- `HELLO` with mode 1 joins the room as slot 1 if the build fingerprints match.
- A room holds two players. When both are present each receives `PEER` about the other.
- A repeated `HELLO` from the same address and port that already has a token is answered
  with the same `WELCOME` (lost replies are retried by the client every 500 ms).
- A client is known by token and source address. A known token from an address no other client
  holds moves that client there (a router that changed its port, a laptop that changed
  networks): the session, its room and a running match go on. Any other token mismatch is
  answered with `ERROR 7` at most once per second, then ignored.
- Idle timeout: a client that sends nothing for **15 s** is removed (clients send `PING`
  every second). A room with no players is deleted at once; a room that never got a second
  player is deleted after **10 minutes**.

## Limits (server side)

- At most 512 rooms and 1024 clients; beyond that `ERROR 5`.
- Per source IP: at most 10 `HELLO` a minute (`ERROR 6`), and at most 400 messages a second
  in total (excess dropped silently). 60 Hz lockstep with redundancy needs about 60-120.
- `DATA` is forwarded only between the two members of one room, never anywhere else.

## Lobby extension (rooms, quick match, voting)

Same header (version byte 1); new message types from 0x10. A server without the extension drops
them silently, so a client that gets no answer to `LIST` within 2 s treats the server as
rooms-by-code only. Everything above stays as it is: a two-player room made by `HELLO` mode 0 and
joined by code still works, and `PEER` is still sent in rooms of two.

### Being in the lobby

`HELLO` with **mode 2** ("lobby") puts the client in no room: `WELCOME` has slot **0xFF** and a
room of six zero bytes. The token, `PING`, `BYE`, the idle timeout and the limits are as before.
From the lobby the client lists, creates, joins or quick-matches; inside a room `LEAVE` returns it
to the lobby with the same token.

### Room settings (the `Settings` block)

| Field | Size | Meaning |
|---|---|---|
| `name` | string | 1..24 bytes, shown in the list |
| `public` | u8 | 1: listed; 0: join by code only. Quick match joins only rooms QUICK made ("Quick: <player>"), never a player's own room |
| `mode` | u8 | front-end match type: 0 One on One, 1 Team (2 vs 2), 2 3-Way, 3 Free for All, 6..11 the special matches; 0x40/0x41 King of the Hill |
| `max` | u8 | players 2..4 (a Team room: 4; a special match: 2) |
| `rounds` | u8 | 0 = the game's default; otherwise the rules index the game takes |
| `pool` | u8 count (1..32) + count x u8 | the venues to vote on (`venue.xml` feID) |

Since FFNY Online build 39 the relay holds every room to its mode, in `CREATE` and `SETTINGS` alike:

- **Team** (mode 1): `max` is 4, and the room starts (the vote) only with four players present and ready, room
  slots 0 and 1 against 2 and 3 (`docs/research/team-and-special-match-types.md`).
- **Special matches** (modes 6..11: Demolition, Inferno, Subway, Window, Cage, Ringout): `max` is 2 and `pool` is
  the mode's own venue (feID 17, 24, 11, 26, 10, 8). With both ready there is no vote: `START` follows at once,
  on that venue. `QUICK` of such a mode opens or joins a room of it.
- **More than two fighters** (Team, 3-Way, Free for All): the pool loses Gun Hill Garage, Red Hook Tire Co. and
  Crow's Office (feIDs 17, 24, 26), which have one-on-one start tables only; a pool of nothing else becomes the
  mode's quick pool. One on One and King of the Hill keep them.

A room can therefore come back in `ROOM` with other `max` and `pool` than were asked for.

### Client to server

| Type | Name | Body after the header |
|---|---|---|
| 0x10 | `LIST` | `token` 8, `mode` u8 (0xFF = any) |
| 0x12 | `CREATE` | `token` 8, `Settings` |
| 0x13 | `JOIN` | `token` 8, `room` 6 |
| 0x14 | `QUICK` | `token` 8, `mode` u8, `max` u8 |
| 0x15 | `SETTINGS` | `token` 8, `Settings` (the host only; refused while voting or playing) |
| 0x16 | `READY` | `token` 8, `ready` u8, `fighter` u8 (`fighter.xml` ID the player wants) |
| 0x17 | `VOTE` | `token` 8, `choice` u8 (0..2: index into the candidates) |
| 0x18 | `LEAVE` | `token` 8 |
| 0x1B | `OVER` | `token` 8 (this client's match has ended) |
| 0x1C | `WHO` | `token` 8 (who is online; not a pending request, the hub asks every few seconds) |
| 0x1E | `RANKED` | `token` 8, `ticket` 16 (one-time, from the account service), `fighter` u8; optionally `modes` u8, `flags` u8, `pings` 3 × u16, `code` 6 (join the ranked queue; see "Ranked queue") |

### Server to client

| Type | Name | Body after the header |
|---|---|---|
| 0x11 | `ROOMS` | `count` u8, then per room: `room` 6, `name` string, `mode` u8, `players` u8, `max` u8, `phase` u8 |
| 0x19 | `ROOM` | the whole room: `room` 6, `you` u8 (your slot), `host` u8 (host's slot), `phase` u8, `Settings`, `candidates` u8 count + count x u8, `votes` count x u8 (votes per candidate), `ms_left` u16 (voting countdown), `players` u8 count, then per player: `slot` u8, `name` string, `ready` u8, `fighter` u8, `voted` u8 (0xFF = not yet) |
| 0x1A | `START` | `seed` u32, `mode` u8, `rounds` u8, `venue` u8, `players` u8 count, per player: `slot` u8, `fighter` u8 |
| 0x1D | `PLAYERS` | `online` u16 (clients connected), `rooms` u16, `count` u8 (at most 40), per player: `name` string, `state` u8 (0 in the hub, 1 in a room, 2 playing). Lobby players of the asker's build, the hub first, then by name; as many as fit one datagram. Relays before FFNY Online build 13 ignore `WHO`. |
| 0x1F | `QUEUE` | `state` u8 (0 checking the ticket, 1 searching, 2 refused, 3 out of the queue, 4 found: the ranked room's `ROOM` follows, 5 accept, 6 move), `waited` u16 (seconds searching), `searching` u16 (players in the queue); the long form (see "Ranked queue") follows for long-form games |

`ROOMS` lists public rooms only, newest first, at most 30 (as many as fit in 1200 bytes).
`ROOM` is sent to every member whenever anything in the room changes, and once a second while
voting (for the countdown).

### Room life

- **phase 0, lobby.** Players join (the lowest free slot), set `READY` with their fighter, the host
  can change the settings (which clears everyone's ready). When at least two players are present
  and all are ready, the server draws up to **3** different venues from the pool at random and
  enters phase 1.
- **phase 1, voting.** Each player `VOTE`s once (a second vote replaces the first). After **15 s**,
  or as soon as everyone has voted, the candidate with the most votes wins (ties: random among the
  tied). The server sends `START` to every member with a fresh random `seed` and enters phase 2.
  A player leaving during the vote returns the room to phase 0 with ready cleared.
- **phase 2, playing.** `DATA` is forwarded from the sender to **every other member** of the room
  (in a room of two: to the other player, as before). When every remaining member has sent `OVER`,
  or after 30 minutes, the room returns to phase 0 with ready cleared.
- The host is the lowest slot present; when the host leaves, the next lowest becomes host. An empty
  room is deleted at once.
- `QUICK` joins the public room in phase 0 with that mode (any `max`), a free slot and the most
  players (then the oldest); if there is none it creates a public room of that `max` named
  "Quick: <player>" (cut to 24 bytes) with `pool` = 1, 6, 19 and makes the client its host.
  When that player leaves or times out, the room is renamed "Quick: <new host>" (since build 36).
  (Before FFNY Online build 15 the `max` had to match too and the room was named after the player.)
- Errors as before: 1 room not found, 2 room full; new: **8** not allowed (not host, wrong phase),
  **9** not in a room / already in one.
- Requests are idempotent, since clients retry over UDP: `CREATE`, `QUICK` or `JOIN` of its own room from a client already in a room changes nothing and is answered with a fresh `ROOM` (only `JOIN` of a different room gets `ERROR 9`); `READY`, `VOTE`, `SETTINGS` or `OVER` that change nothing still get a `ROOM` to the sender; `LEAVE` has no reply and is ignored in the lobby; `LIST` always gets `ROOMS`, even with count 0; a repeated `HELLO` mode 2 gets the same `WELCOME`; a `VOTE` after the vote has ended gets the `START` again.

### Ranked queue

Ranked play is best-of-three sets between signed-in players in three modes, each with its own
rating and ladder: **Open** (every fighter, created and Story ones too), **Classic** (stock fighters
only) and **Draft** (pick and ban). It is off in production until the account service's switch is on
(`server/docs/ranked-design.md`). One central matchmaker on the account service pairs the queues of every
relay (EU, NA, West).

- The game asks the account service for a **ticket** (`POST /api/ranked/ticket` with the launcher's
  session; 32 hex digits = 16 bytes, valid for one minute, usable once) and sends `RANKED` with it.
  The launcher's session itself never goes over UDP.
- `RANKED` has two forms. The **25-byte** one (token, ticket, fighter) is the original: Classic only,
  no `ACCEPT`, never moved to another relay. The **long** one adds 14 bytes: `modes` u8 (bit 0 Open,
  bit 1 Classic, bit 2 Draft), `flags` u8 (bit 0 rollback on; bits 4-5 the primary mode 0 Open,
  1 Classic, 2 Draft), `pings` 3 × u16 (ms to the EU, NA and West relays; 0 unknown) and `code` 6 (a
  reservation code from `QUEUE` 6, else zeros).
- The relay answers `QUEUE` 0 and redeems the ticket on the account service's internal listener
  (`POST /internal/ranked/redeem`). The service refuses (`QUEUE` 2) an unknown, used or expired
  ticket, a banned account, the queue gate (a Discord account younger than 30 days, fewer than 10
  casual matches, another ranked account on the same PC within 30 days, a dodge lock) and everything
  while its switch is off; so does the relay for an account already searching from another game or
  without an account service. A client in a room gets `ERROR 9`.
- Every second the relay posts its queue to the matchmaker (`POST /internal/ranked/queue`) and
  applies the answer for each player:
  - searching: `QUEUE` 1 (the long form adds the searching count per mode);
  - found (long form only): `QUEUE` 5 with the mode, the seconds left to accept (15) and the
    opponent's rating; the game answers `ACCEPT` 1 or 0. Declining or letting it run out is a dodge
    (queue locked 2, 10, then 30 minutes; the third within a day counts as a set loss); the other
    player keeps searching. A 25-byte game is accepted for at once;
  - host: the set's private room "Ranked" (One on One, `max` 2, pool 1, 6, 11) opens here; the player
    gets `QUEUE` 4 and the room, ready with their fighter (a created fighter only in Open);
  - move (long form only): `QUEUE` 6 with the reservation `code` and the host relay's `addr`; the game
    registers there and sends the long `RANKED` with a fresh ticket and the code, and is seated in the
    set's room once its account checks out. A reservation waits 30 s, then the room closes;
  - refused: `QUEUE` 2.
- The long `QUEUE` (to long-form games only) adds after `searching`: `counts` 3 × u16 (Open, Classic,
  Draft), `mode` u8 (0xFF none), `accept` u8 (seconds), `opp` u16 (rating), `code` 6, `addr` (u8
  length, then text).
- Pairing (the matchmaker): the longest waiter first, the closest rating both windows accept
  (±(75 + RD/2), +25 every 5 s, at least ±400 after 60 s and ±600 after 120 s; ±300 from the start for
  a provisional player), the host relay with the lower worse ping (≤ 120 ms, 180 after 60 s, 250 after
  120 s), the primary mode only for 45 s and then any mode both accept; never the same PC or network,
  never the last opponent again unless both waited over a minute; a mode may require rollback.
- **The set in the room** (relay `ranked_set.go`, since ranked phase 2a): game 1's venue is drawn
  from the starters (1, 5, 6, 11, 19) and shown as the room's one candidate for 8 s (the versus
  screen; two `VOTE`s for it start it sooner). After each game both fighters send `RESULT` (0x22,
  as in King of the Hill: `seed` of the game's `START`, `winner` slot or 0xFF a draw), repeated until
  `RANKINFO` shows the game counted. Two that agree count for the set's score; two that disagree
  count for nothing here (the account service, which rates the set from the games' reports, pauses
  such a set). After a won game the loser picks the next venue from three drawn from the ranked
  pool (1, 2, 5, 6, 9, 11, 13, 19; never 17, 24, 26): only the loser's `VOTE` counts (the winner's
  is answered with `ROOM` and ignored), the vote ends when the loser votes or after 15 s (then at
  random); after a draw both vote. Both `READY` again between games (a fighter change is allowed;
  Classic and Draft stay stock only). At two wins, or after five games, the set is over: no game
  starts any more, and leaving the room is no longer a forfeit (no `setover`). `RANKINFO` follows
  every `ROOM` of the room.
- **A Draft set** (relay `draft.go`, since ranked phase 2b) replaces the venue vote with a pick and
  ban the relay runs. Seat "A" is the lower-rated player in Draft (a tie: slot 0); `DRAFT` names it.
  Bans: three each, A B B A A B, 20 s a turn, visible and kept for the whole set (a ban that runs out
  is skipped and listed as 0xFF). Game 1: A strikes 1 of the five starters, B 2, A 1 (15 s a turn; a
  strike that runs out is random), then both pick blind within 25 s: the relay never sends the
  other's blind pick or cursor, only that it is locked. Games 2 and on: the last game's winner bans a
  venue of the ranked pool, the loser picks one (15 s each, random when they run out), the winner
  picks a fighter (visible), the loser counterpicks (25 s each); nobody may pick the fighter they won
  a game of this set with. A pick that runs out takes the player's cursor (`DRAFTACT` 5) when it is
  legal, else a random legal fighter. Two turns in a row run out without any `DRAFTACT` from that
  player make them AFK: before game 1 a dodge (`setover` with `dodge`: the set is void and the
  dodger's queue locks as after a missed `ACCEPT`), after it a forfeit; `RANKINFO` then shows the
  other as the set's winner. Leaving the room during game 1's draft is a dodge too. Only stock
  fighters (never 99). The room stays in phase 0 during the draft (`READY` changes nothing); when
  the picks are complete the room shows the drafted venue as its one candidate for 6 s (`VOTE`s are
  ignored) and then sends `START` with the drafted fighters and venue. The next game's draft starts
  once both are back and `READY`; a game whose `RESULT`s disagree is played again with the same
  picks. `DRAFT` goes to both players on every change and every second; a `DRAFTACT` out of turn, in
  another step or not legal is answered with `ERROR 8` (a repeat of one already taken with `DRAFT`,
  a refused cursor move with nothing).
- The host cannot change a ranked room's settings (`ERROR 8`). Every `START` in a ranked room that
  still holds the two paired accounts is posted to the account service (`POST
  /internal/ranked/pairing`: key `<room>-<SEED in hex>`, the set, the mode, the relay and the START's
  lineup). Leaving the room between games ends the set (`POST /internal/ranked/setover`; a forfeit).
- `LEAVE` while searching leaves the queue (`QUEUE` 3); `CREATE`, `JOIN` or `QUICK` leave it
  silently; so does disconnecting. A repeated `RANKED` while queued only changes the fighter.

| Type | Name | Direction | Body |
|---|---|---|---|
| 0x2C | `ACCEPT` | client -> server | `token` 8, `answer` u8 (1 accept, 0 decline) |
| 0x2D | `DRAFT` | server -> client | `step` (1 bans, 2 strikes, 3 blind picks, 4 venue ban, 5 venue pick, 6 winner's pick, 7 loser's pick, 8 playing or revealing, 9 over), `turn` (a slot), `secs` (left), `game`, `venue` (0 none), `first` (A's slot) u8; `bans` (u8 count + ids by turn, 0xFF a skipped one), `struck`, `venue_bans` (each u8 count + ids); `picks` 2 × u8 (0xFF none or hidden); `locked` u8 bits; `cursors` 2 × u8 (0xFF none or hidden); `options` (count + the venues of this game: the starters in game 1, then the pool); `locks` 2 × (count + the fighters that slot may not pick: won with); `afk` u8 (the AFK slot, 0xFF none). Relay `draft.go` |
| 0x2E | `DRAFTACT` | client -> server | `token` 8, `step` u8 (as `DRAFT`'s, 1..7), `action` u8 (1 ban, 2 strike, 3 pick, 4 venue, 5 cursor), `id` u8 (a fighter or venue). No answer but the next `DRAFT`; `ERROR 8` when refused |
| 0x2F | `RANKINFO` | server -> client | `mode` u8, count u8, then per player `slot` u8, `division` u8 (0..21 as the account service's divisions; 0xFF placement), `placements` left u8, `rating` u16, `set_wins` u8, `flags` u8 (bit 0 provisional); in a set's room then `game` u8 (the running or next game from 1, the last once over), `picker` u8 (the slot that picks the next venue, 0xFF both), `winner` u8 (0xFF running, the set winner's slot, 0xFE level after five games). After every `ROOM` of a ranked room (since phase 2a) |

## King of the Hill extension

A room of up to 10 where two fight One on One and the others wait in a line and watch; the
winner stays, the loser goes to the back. Full design, rules and layouts:
`docs/research/king-of-the-hill.md`. Same header and version; types from 0x20, dropped silently
by older relays.

| Type | Name | Direction | Body after the header |
|---|---|---|---|
| 0x20 | `CAPS` | client -> server | `token` 8, `caps` u32 (bit 0: King of the Hill) |
| 0x20 | `CAPS` | server -> client | `caps` u32 (what the relay supports: bit 0 King of the Hill, bit 1 rollback allowed, bit 2 `YIELD`, since build 36) |
| 0x21 | `HILL` | server -> client | `room` 6, `phase` u8 (0 waiting, 1 calling, 2 voting, 3 fighting), `king` u8 (slot, 0xFF none), `streak` u16, `ms_left` u16, `seed` u32, `fights` u16, `last_winner` u8, `last_loser` u8, `last_how` u8 (0 none, 1 won, 2 draw, 3 loser left, 4 void), `count` u8, per player in line order: `slot` u8, `state` u8 (0 waiting, 1 called, 2 ready, 3 fighting, 4 away), `wins` u16 |
| 0x22 | `RESULT` | client -> server | `token` 8, `seed` u32, `winner` u8 (slot, 0xFF draw); from the two fighters, repeated until `HILL.fights` changes; in a ranked set's room until `RANKINFO` counts the game (see "Ranked queue") |
| 0x23 | `WATCH` | server -> client | the body of `START`; to every member who is not fighting when the fight starts |
| 0x26 | `YIELD` | client -> server | `token` 8; "give up my spot": the sender goes to the back of the line (a king gives up the crown, a called player is replaced by the next). `ERROR 8` while the sender votes or fights, or outside a King of the Hill room. Answered with ROOM and HILL. Relays since build 36 (`CAPS` bit 2) |

- `Settings.mode` **0x40** (venue voted by the two fighters) and **0x41** (venue at random) are
  King of the Hill rooms; their `max` is 2..10. Every fight is One on One (`START.mode` 0).
- Only a client that sent `CAPS` with bit 0 can create, quick-match, join or list such a room
  (otherwise `ERROR 8`, and LIST leaves them out); every other room behaves as before.
- Such a room takes joiners in every phase. Each ROOM is followed by a HILL. The two called
  players must `READY` within 20 s or are moved to the back (two skips in a row: away until
  their next `READY 1`). Only they get `START`; everyone else present gets `WATCH`.
- During the fight a fighter's `DATA` goes to the other fighter and the `WATCH` receivers; a
  spectator's `DATA` goes to the other spectators only. Between fights it goes to everyone.
- Two agreeing `RESULT`s decide the fight, two disagreeing ones void it (both to the back), a
  single one decides after 10 s. A fighter who leaves during the fight loses it. `OVER` changes
  nothing; `VOTE` during the fight re-sends the sender's `START` or `WATCH`.
- Any member who is not voting or fighting may change its fighter with `READY` (its ready flag as
  it is); the change counts from its next fight. Leaving is `LEAVE` at any time.

## Spectator extension (watching a room's matches)

A client may watch an ordinary room's matches without playing in them; its game runs each match
from the `START` body and the players' entries (design and limits: `docs/research/spectator-mode.md`).
Same header and version; a relay without it drops `SPECTATE` silently.

| Type | Name | Direction | Body after the header |
|---|---|---|---|
| 0x20 | `CAPS` | both | bit 4 (`16`): from a client, "I can watch"; in the relay's answer (only to a client that sent it), "rooms here can be watched" |
| 0x2B | `SPECTATE` | client -> server | `token` 8, `room` 6 |
| 0x19 | `ROOM` | server -> client | `you` = **0xFD** for a spectator; to clients that sent bit 4, after the players: `count` u8, per spectator `name` string |
| 0x23 | `WATCH` | server -> client | the `START` body (with its created-fighter trailer), to every spectator present when the match starts |

- `SPECTATE` puts a lobby client into the room as a spectator, answered with `ROOM` (to everyone in the
  room) and the players' `RECORD`s. Errors: 1 no such room, 2 the room's spectator places are taken
  (`relay -spectators N`, default 4; 0 turns spectating off), 3 another build, 8 not allowed (no bit 4,
  a King of the Hill, ranked or legacy room, a room without players), 9 already in a room. A repeated
  `SPECTATE` of the same room answers `ROOM` again.
- A spectator is never a player: not in `ROOM.players`, `START`, the ready check, the vote, `OVER` or
  the player count of `ROOMS`. Its `READY`, `SETTINGS` and lobby-phase `VOTE` get `ERROR 8`; its `OVER`
  changes nothing; its `DATA` is dropped. `LEAVE` takes it out; when the room's last player leaves, the
  room closes and its spectators get `ERROR 1`.
- At the vote's end the spectators get `WATCH` and are the match's watchers: during the match every
  player `DATA` whose payload type is `P_STEPS` (4), `P_HASH` (5) or `P_RTT` (6) goes to them too, with
  the player's slot. A watcher's `VOTE` while the room plays re-sends its `WATCH`. A spectator who
  enters during a match watches from the next one.
- `P_RTT` (game payload 6) gains byte 13 since build 40: the input delay the sender's game agreed at
  its sync point (0 before), which a spectator plays with. Byte 14 since build 45: bit 0, the sender's
  game rolls back a match with a created fighter too. A match with a created fighter rolls back only
  when every player has rollback on (byte 7) and this bit; otherwise it plays in lockstep.
- `P_PINGINFO` (game payload 9, in a room every 2 s, builds from 30: `u16` the sender's ping to the
  relay) gains byte 3 since build 46: bit 0, the sender's game rolls back in the next match (its
  setting is on for this session and the relay allows rollback, as `P_RTT` byte 7 will say); bit 1, its
  setting is on but its session runs without rollback (a restart would use it). The room page shows
  each player's flag and why a match will play in lockstep. Older builds read the first two bytes.

## Relay status (choosing a relay)

Since build 30 there is more than one relay (Europe `play.defjam.online`, US East
`na.defjam.online`, and since build 38 US West `west.defjam.online`, all port 27777). A client asks each one with `STATUS` before it registers
anywhere, and picks by round trip. Same header and version; a relay without it drops `STATUS`
silently (an unknown type), so no answer within about 2 s means "older relay or unreachable".

| Type | Name | Direction | Body after the header |
|---|---|---|---|
| 0x24 | `STATUS` | client -> server | `nonce` u32, `build` 8 (the asker's fingerprint, zeros: any), 8 zero bytes (reserved) |
| 0x24 | `STATUS` | server -> client | `nonce` u32 (the request's), `version` u8 (the protocol byte it speaks), `flags` u8 (bit 0 ranked queue, bit 1 King of the Hill, bit 2 rollback allowed), `online` u16 (registered clients, any build), `rooms` u16, `waiting` u16 (players of that build in public lobby-phase rooms with a free place: who a quick match meets), `idle` u16 (players of that build in the lobby, not in a room), `published` u32 (newest published build, 0 unknown), 2 zero bytes |

- No token, no registration, no effect on any client (not even its idle timer). The request is
  exactly 26 bytes (reserved bytes zero, anything else is dropped) and the answer is 26 bytes, never
  larger, so it cannot amplify traffic.
- Each source IP gets at most 4 answers a second; further requests are dropped silently. The
  per-IP message limit of the base protocol applies as well.
- The summary log line counts the answers (`status=N` at its end).

## Public room list without a session (all servers)

Since build 31 a player can choose **Both servers** (build 38: **All servers**, three relays): the
game stays in the lobby of one relay and also lists the rooms of the others, then moves over to join
one. It asks the other relay with
`ROOMLIST`, which, like `STATUS`, needs no token and registers nothing. Same header and version; a
relay before build 31 drops it silently (an unknown type), and the game then shows no rooms of
that relay.

| Type | Name | Direction | Body after the header |
|---|---|---|---|
| 0x25 | `ROOMLIST` | client -> server | `nonce` u32, `build` 8 (the asker's fingerprint, zeros: any), `mode` u8 (0xFF any), `caps` u32 (bit 0: list King of the Hill rooms), then zero bytes (padding, see below) |
| 0x25 | `ROOMLIST` | server -> client | `nonce` u32 (the request's), `total` u16 (rooms that match), `count` u8, then per room: `room` 6, `name` string, `mode` u8, `players` u8, `max` u8, `phase` u8 (as in `ROOMS`), `age` u16 (seconds since the room was opened, 65535 at most) |

- The rooms are those `LIST` gives a registered client of that build: public rooms, newest first,
  King of the Hill rooms only with caps bit 0, at most 30.
- **The answer is never larger than the request.** A client pads its request with zero bytes to
  the size of the answer it wants (the game sends a full 1200-byte datagram); the relay lists as
  many rooms as fit in that size and says in `total` how many there are. Padding that is not zero
  is malformed (dropped).
- Each source IP gets at most 4 answers a second (separate from `STATUS`); further requests are
  dropped silently. The per-IP message limit of the base protocol applies as well.
- The summary log line counts the answers (`roomlist=N` at its end).
- The game asks at most every 3 s per relay, while its room list shows or while it waits alone in a
  quick room once its quick match searches other relays (build 38: any choice but a custom address; the
  `age` lets two such players decide who moves; see `docs/netplay.md`).

## Created fighters (Story-mode fighters online)

Since build 37 a player may fight with the fighter they made in Story mode
(`docs/research/created-fighter-online.md`). The fighter is a fixed-size **record** the client
uploads; the relay keeps it with the player, hands it to the other members of the player's room,
and names it in `START`, so every PC writes the same bytes into the game. Same header and version;
types from 0x27, dropped silently by older relays.

| Type | Name | Direction | Body after the header |
|---|---|---|---|
| 0x27 | `FIGHTER` | client -> server | `token` 8, `len` u16 (0 or 300), `record` (len bytes; empty: clear) |
| 0x28 | `RECORD` | server -> client | `slot` u8 (a room member's slot, or 0xFE: the relay's copy of the sender's own), `len` u16 (0 or 300), `record` |
| 0x29 | `RECGET` | client -> server | `token` 8, `slot` u8 (send the record of that member again) |

- The **record** (300 bytes): `version` u8 (1), a 0x127-byte payload (name 0x20, the profile's
  bytes +0xFC..+0x143: base stats, bonus stats, move keys, voice, styles, archetype; the created
  flag 1; recipe 0x98; options 0x26), `crc` u32 (CRC-32/IEEE of version and payload).
- `CAPS` bit 3 (`8`): from a client, "I can play created fighters"; in the relay's answer, "the
  switch is on". The switch is the file `created-fighters-allowed` in the relay's updates folder
  (read at every request, so it turns on and off without a restart; `-created` forces it on for
  tests). `STATUS` does not report it.
- `FIGHTER` is answered with `RECORD` slot 0xFE: the record the relay keeps (empty when the switch is
  off, the client did not announce bit 3, or the record is malformed: wrong size or version, bad
  CRC, a name that is not 1..16 printable ASCII characters (8 before build 39: the user ID; since
  build 39 the player's online name for the fighter), an archetype outside 86..141, no first
  style, no created flag). A kept record goes to the other members of the client's room as
  `RECORD`; a client joining a room gets every member's record and the members get its own.
  Clearing a record while ready with it in a lobby-phase room makes the fighter Doc (55).
- `READY` with fighter **99** plays the record when the relay keeps one and the switch is on;
  otherwise 99 is replaced like any fighter without a database record (Doc). King of the Hill
  rooms never play records (99 becomes Doc there).
- `START` gains a trailer when a player plays a record: after the players, `count` u8, then per
  such player `slot` u8, `crc` u32 (the record the relay sent). Without created fighters `START`
  is unchanged. The relay sends the named records (`RECORD`) to every other member right before
  `START`; a client that lacks one asks with `RECGET` and sets the match up only once it has it
  (after 10 s it leaves the match).
- The relay checks only the record's shape. Every client checks and clamps the record the same way
  before the game sees it (`cp_record_sanitize`, `src/hooks/created_fighter.c`): name cut to 16
  printable ASCII characters, voice 1..6, styles 0..4, base stats at most 50, bonus stats 0..38, move keys and
  recipe parts that the game's database has (others become the defaults). The sender plays the
  checked copy too.

## Server operation

The relay is one static binary (`server/relay`, Go). `relay -listen :27777` serves; it logs
one line per room event and a summary every minute, and keeps no state on disk. A systemd
unit and Hetzner setup notes live beside it in `server/relay/README.md`.

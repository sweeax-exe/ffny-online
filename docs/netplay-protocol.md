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
| `public` | u8 | 1: listed and found by quick match; 0: join by code only |
| `mode` | u8 | front-end match type: 0 One on One, others as the game numbers them |
| `max` | u8 | players 2..4 |
| `rounds` | u8 | 0 = the game's default; otherwise the rules index the game takes |
| `pool` | u8 count (1..32) + count x u8 | the venues to vote on (`venue.xml` feID) |

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
| 0x1E | `RANKED` | `token` 8, `ticket` 16 (one-time, from the account service), `fighter` u8 (join the ranked queue; see "Ranked queue") |

### Server to client

| Type | Name | Body after the header |
|---|---|---|
| 0x11 | `ROOMS` | `count` u8, then per room: `room` 6, `name` string, `mode` u8, `players` u8, `max` u8, `phase` u8 |
| 0x19 | `ROOM` | the whole room: `room` 6, `you` u8 (your slot), `host` u8 (host's slot), `phase` u8, `Settings`, `candidates` u8 count + count x u8, `votes` count x u8 (votes per candidate), `ms_left` u16 (voting countdown), `players` u8 count, then per player: `slot` u8, `name` string, `ready` u8, `fighter` u8, `voted` u8 (0xFF = not yet) |
| 0x1A | `START` | `seed` u32, `mode` u8, `rounds` u8, `venue` u8, `players` u8 count, per player: `slot` u8, `fighter` u8 |
| 0x1D | `PLAYERS` | `online` u16 (clients connected), `rooms` u16, `count` u8 (at most 40), per player: `name` string, `state` u8 (0 in the hub, 1 in a room, 2 playing). Lobby players of the asker's build, the hub first, then by name; as many as fit one datagram. Relays before FFNY Online build 13 ignore `WHO`. |
| 0x1F | `QUEUE` | `state` u8 (0 checking the ticket, 1 searching, 2 refused, 3 out of the queue, 4 found: the ranked room's `ROOM` follows), `waited` u16 (seconds searching), `searching` u16 (players in the queue) |

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
  (Before FFNY Online build 15 the `max` had to match too and the room was named after the player.)
- Errors as before: 1 room not found, 2 room full; new: **8** not allowed (not host, wrong phase),
  **9** not in a room / already in one.
- Requests are idempotent, since clients retry over UDP: `CREATE`, `QUICK` or `JOIN` of its own room from a client already in a room changes nothing and is answered with a fresh `ROOM` (only `JOIN` of a different room gets `ERROR 9`); `READY`, `VOTE`, `SETTINGS` or `OVER` that change nothing still get a `ROOM` to the sender; `LEAVE` has no reply and is ignored in the lobby; `LIST` always gets `ROOMS`, even with count 0; a repeated `HELLO` mode 2 gets the same `WELCOME`; a `VOTE` after the vote has ended gets the `START` again.

### Ranked queue

Ranked play is One on One between signed-in players of a similar rating (`docs/ranked-design.md`).

- The game asks the account service for a **ticket** (`POST /api/ranked/ticket` with the launcher's
  session; 32 hex digits = 16 bytes, valid for one minute, usable once) and sends `RANKED` with it.
  The launcher's session itself never goes over UDP.
- The relay answers `QUEUE` 0 and redeems the ticket on the account service's local listener
  (`POST /internal/ranked/redeem`), which names the account and its rating. An unknown, used or
  expired ticket, an account already searching from another game, or a relay without an account
  service (`-accounts-internal` not set) gives `QUEUE` 2. A client in a room gets `ERROR 9`.
- While searching the client gets `QUEUE` 1 once a second. Every second the relay pairs the queue:
  the longest-waiting player first, with the closest rating that both players' windows accept. A
  window is ±100 rating points at first and grows by 50 every 10 s of searching, up to ±1000.
- A pair gets `QUEUE` 4 and a private room named "Ranked" (One on One, `max` 2, pool 1, 6, 19)
  with both players ready with their fighters, so the vote starts at once; then `START` as usual.
  The host cannot change a ranked room's settings (`ERROR 8`).
- Every `START` in a ranked room that still holds the two paired accounts is posted to the
  account service (`POST /internal/ranked/pairing`, key `<room>-<SEED in hex>`, the key the games
  report the match under). Only matches with such a pairing, reported by both paired accounts
  from their own slots with the same result, change ratings (Glicko-2), at most 5 per pair per day.
- `LEAVE` while searching leaves the queue (`QUEUE` 3); `CREATE`, `JOIN` or `QUICK` leave it
  silently; so does disconnecting. A repeated `RANKED` while queued only changes the fighter.

## King of the Hill extension

A room of up to 10 where two fight One on One and the others wait in a line and watch; the
winner stays, the loser goes to the back. Full design, rules and layouts:
`docs/research/king-of-the-hill.md`. Same header and version; types from 0x20, dropped silently
by older relays.

| Type | Name | Direction | Body after the header |
|---|---|---|---|
| 0x20 | `CAPS` | client -> server | `token` 8, `caps` u32 (bit 0: King of the Hill) |
| 0x20 | `CAPS` | server -> client | `caps` u32 (what the relay supports) |
| 0x21 | `HILL` | server -> client | `room` 6, `phase` u8 (0 waiting, 1 calling, 2 voting, 3 fighting), `king` u8 (slot, 0xFF none), `streak` u16, `ms_left` u16, `seed` u32, `fights` u16, `last_winner` u8, `last_loser` u8, `last_how` u8 (0 none, 1 won, 2 draw, 3 loser left, 4 void), `count` u8, per player in line order: `slot` u8, `state` u8 (0 waiting, 1 called, 2 ready, 3 fighting, 4 away), `wins` u16 |
| 0x22 | `RESULT` | client -> server | `token` 8, `seed` u32, `winner` u8 (slot, 0xFF draw); from the two fighters, repeated until `HILL.fights` changes |
| 0x23 | `WATCH` | server -> client | the body of `START`; to every member who is not fighting when the fight starts |

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

## Server operation

The relay is one static binary (`netplay/relay`, Go). `relay -listen :27777` serves; it logs
one line per room event and a summary every minute, and keeps no state on disk. A systemd
unit and Hetzner setup notes live beside it in `netplay/relay/README.md`.

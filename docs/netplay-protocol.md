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
- A client is known by token **and** source address; a message whose token does not match
  its source is answered with `ERROR 7` at most once per second, then ignored.
- Idle timeout: a client that sends nothing for **15 s** is removed (clients send `PING`
  every second). A room with no players is deleted at once; a room that never got a second
  player is deleted after **10 minutes**.

## Limits (server side)

- At most 512 rooms and 1024 clients; beyond that `ERROR 5`.
- Per source IP: at most 10 `HELLO` a minute (`ERROR 6`), and at most 400 messages a second
  in total (excess dropped silently). 60 Hz lockstep with redundancy needs about 60-120.
- `DATA` is forwarded only between the two members of one room, never anywhere else.

## Server operation

The relay is one static binary (`netplay/relay`, Go). `relay -listen :27777` serves; it logs
one line per room event and a summary every minute, and keeps no state on disk. A systemd
unit and Hetzner setup notes live beside it in `netplay/relay/README.md`.

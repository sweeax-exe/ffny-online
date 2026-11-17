# Direct connection (P2P), opt-in

Owner's wish (2026-11-17): P2P as an optional setting in the online menu. The relay stays the default and
the fallback. Rules: opt-in only, used only when **both** players allowed it; a clear notice when turning
it on (the other player sees your IP address, DDoS risk); off by default; if the direct path fails or
degrades the match goes on through the relay with no more than a short line; 1v1 first.

## 1. How fight packets travel today

- One UDP socket per game (`N.sock`, `src/hooks/netplay.c` `start()`), non-blocking, `sendto` the relay
  only (`send_raw`). The receive loop (`net_thread`) used `recv`, so any source reached `on_packet`.
- Client to relay: `DATA` = header 6 + token 8 + payload. Relay to client: header 6 + the sender's room slot
  + payload. The relay forwards a DATA to every other member of the room (`relay.go` `handle`, `recipients`,
  hill rooms `hillRecipients`) and to the match's watchers (spectators), and records it (`record.go`).
- The payloads (opaque to the relay): `P_STEPS` (every step: match, last step, 8 redundant 32-byte entries),
  `P_LIVEPAD` (pads before the first step), `P_RTT` (each PC's relay ping and flags, which every PC uses to
  derive the same input delay), `P_HASH` (checksums), `P_REMATCH`, `P_GONE`, `P_PINGINFO`, voice.
- `store_steps` is idempotent: an entry goes into a ring by step number (`in_tag = step + 1`), so the same
  entry arriving twice changes nothing; `P_LIVEPAD` is taken only with a newer sequence.
- The delay is fixed at the sync point from the pings in the START's `P_RTT`s (relay path: half of each of
  the two highest round trips, `netdelay_path_ms`); every PC computes it from the same numbers.

## 2. Design

**A second copy, not a second path.** With a direct path up, a game sends its `P_STEPS` and `P_LIVEPAD`
twice: to the relay exactly as before, and directly to the other player. The receiver keeps whichever
copy comes first (both are the same bytes; `store_steps` and the live pad's sequence make the second one
a no-op). Everything else (`P_RTT`, `P_HASH`, `P_REMATCH`, `P_GONE`, voice, chat) stays relay-only.
Consequences:

- The fallback needs no switch-over and cannot desync: when the direct path dies, the relay's copies are
  already arriving. The players see one short line ("Direct connection lost: the fight goes on through the
  server.").
- Spectators, recordings, the anti-cheat silence rule and the relay's view of the match are unchanged.
- The input delay is unchanged in this build: it is still agreed from the relay pings. The direct path
  makes the inputs arrive earlier, which in lockstep means fewer stalls on jitter and in rollback fewer and
  shorter rollbacks, but not a shorter delay (see section 5).
- Cost: twice the upload of `P_STEPS` (about 9 + 8 x 32 bytes, 60 a second: ~17 KB/s more) for opted-in
  players only.

**Rendezvous at the relay** (server `relay/p2p.go`). CAPS bit 27 (`CapP2P` / `CAPS_DIRECT`): every build
that has it announces it; the relay answers it while its switch file `p2p-allowed` exists in the updates
folder (no file: off; `-p2p` for tests). Consent is per match, not per session: after a 1v1 START the game
sends `P2P` (0x3A: token, START seed, 16 zero bytes so the answer is never larger) **only while
`[online] p2p_direct` is on**. The relay answers `WAIT` until the other player of that START asked for the
same seed too, then `OK` with the other one's address as the relay sees it (IPv4, port), its room slot and
an 8-byte random key for the match. So a player with the setting off never has its address given out, and
a player that has it on learns nothing about one that has it off. `NO`: the switch is off, not a 1v1 START,
a spectator, another seed, the other game lacks the bit, or an address that is not IPv4. The relay logs one
line per match ("both players opted in, endpoints exchanged", anonymised ids) and counts `P2PPairs`.

**Punching and keepalive** (game `src/hooks/netplay_p2p.[ch]` for the wire forms, `netplay.c` for the
state). Direct packets are `T_DIRECT` (0x3B): header, the key, a kind (PROBE, ACK, DATA). Both games send
PROBEs every 100 ms (which opens each NAT for the other), answer every PROBE with an ACK, and call the path
**up** at the first ACK (the round trip is measured from the echoed clock). Up: keepalive every 250 ms;
nothing direct for 1 s: **down** (log line and the short toast), probes go on and the path comes back by
itself. No ACK within 6 s: no direct path (symmetric NATs, firewalls), the match goes on through the relay
as before (log line only). Only packets with the match's key are taken; the receive loop hands `T_DIRECT`
to the P2P code before anything else and never to `on_packet` or the test lag queue. The socket gets
`SIO_UDP_CONNRESET` off, so an ICMP "port unreachable" for a probe cannot turn the next `recv` into an error.
The other player's address is never written to the log (logs go into F9 reports).

**The setting.** `[online] p2p_direct` (bool, default 0, env `RECOMP_P2P_DIRECT` for tests), read at each
START. Launcher (Online page and the basics page) "Direct connection (P2P) in 1v1 fights": turning it on
opens a modal with the notice and "Turn on" / "Cancel". In the game's online menu: VOICE CHAT page, row
"DIRECT CONNECTION (P2P), 1V1"; turning it on shows the notice on the door panel (A turns on, B cancels).
Notice text: the other player's game gets your IP address and you get theirs; someone could use it to
attack your connection (DDoS); used only in 1v1 fights and only when both turn it on; if it fails the fight
goes on through the server. The privacy page (accounts `server.go`) has a "Direct connection (P2P)"
section and the terms' fair-play rule forbids attacking other players' connections.

## 3. Switches and deploy

- Relay: `p2p-allowed` in the updates folder of each relay (EU, NA, SG); no restart. Remove it to turn the
  feature off at once for every new request (a running direct path lives until its match ends; it never
  carried anything the relay did not carry too).
- Official-client check: builds newer than the `OfficialCaps` table may add bits, so build 90 passes as is;
  the build-90 release commit adds one combined row (with the ping-sum branch's bit 26); this branch adds none.
- Order: relay first (it understands `P2P`, answers `NO` while the switch file is missing), then the game
  build, then the privacy/terms text on the account service, then the switch file.

## 4. Tests

`tests/unit/test_netplay_p2p.py` (setting off by default, notice before turning on, CAPS bit, wire forms
compiled natively against the relay's layout); server `relay/p2p_test.go` (both must opt in, WAIT/OK/NO,
same key both ways, only 1v1, switch off by default, answer not larger than the request). Two local copies
through a local relay (`FFNY_SERVER_DIR=<server worktree> DJ_RELAY_EXTRA=-p2p`, `dj.py scenario online`):
both on (direct path up, inputs first on the direct path, checksums equal) in lockstep and rollback; one
side off (relay only); `RECOMP_TEST_P2P_CUT_MS` on one copy cuts the direct path both ways mid-fight
(falls back, no desync; the cut counts from the first direct `P_STEPS`).

Results 2026-11-17 (build 89 base, local relay; `RECOMP_NETPLAY_LAG_MS=60` holds the relay's packets, never
the direct ones, so the direct path is the shorter one as on the internet):

| Run | Result |
|---|---|
| lockstep, both on, 2 matches | path up in < 0.5 s, the other player's inputs first on the direct path 975 / 978 times vs 4 / 1 through the relay; 30 checksums equal |
| rollback d4, both on, lag 60, 2 matches | first direct 1085 times, through the relay 0; 30 checksums equal |
| lockstep, only copy 0 on | copy 0: WAIT for 10 s, then "the other player has it off"; copy 1: "off in this game's settings"; the relay exchanged nothing; 10 checksums equal |
| rollback d4, lag 60, cut 8 s into the fight on copy 0, 2 x 30 s fights | ~490 entries first direct, then lost after 1016 ms on both copies, the toast, the rest through the relay; 70 checksums equal, KO in both |
| lockstep d7, lag 60, cut on copy 1 | the same; "fight waits: 0 of 100 ms or more mid-fight"; 70 checksums equal |
| gallery (180 pages) | the VOICE CHAT row and the notice page (`voice-p2p`) render; no faults |

## 5. Not in this build

- **A shorter delay from the direct path.** The delay is agreed from the START's P_RTT values, which are
  fixed before the direct path exists; changing them mid-way would let two PCs agree different delays.
  A later build could carry each PC's direct round trip in P_RTT at the START of a rematch (the path is up
  by then) and use it only when both report one, in rollback matches only (where a fallback to the longer
  relay path means deeper rollbacks, not stalls).
- Players behind the same NAT (LAN parties): the relay gives the public address; that works only with
  hairpinning routers. A local-address candidate would fix it.
- IPv6, TURN-like relaying (the relay already is that), more than two players.
- The Android port: it does not announce bit 27, so the relay answers it `NO`.

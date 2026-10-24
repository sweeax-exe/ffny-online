# Crashes on the online thread: a jump to 0x1E / 0x111 (2026-10-24)

Four player crash reports show the same thing: an access violation with RIP = 0x1E or 0x111 (a jump to a
tiny address) on a thread that is not the game's. Builds 24 name it: "online play" (`net_thread`).

| Report | Build | Match | Voice |
|---|---|---|---|
| 7699f2 | 22 | 2 players | voice chat on |
| fec288 | 22 | 2 players | microphone open |
| 979508 | 24 | 2 players, 25+ matches in one room | 30750 voice packets received |
| 7887b5 | 24 | 3 players (FFA), the owner's group | 512 voice packets |

The owner's own game vanished in the same 3-player group's first match without any crash output or Windows
error event (log `...\source\logs\run-20261024-230908.log.err` of the installed copy, ends at step ~3285).

## Where

The build is deterministic across PCs (both build-24 dumps have the same stack words), so the installed
build's PDB names them (`scripts/symbolize-host.py --exe <installed defjam_recomp.exe>`):
`net_thread+0x410` -> `send_token_only+0x41` (the return from `sendto`) -> the jump. So the online thread was
inside Winsock's `sendto` (the once-a-second T_PING) when the call went to 0x1E/0x111; build 22's dump had
`mswsock.dll` frames and no exe words.

Ruled out so far (audit of the receive path, see the session notes): no packet-driven overflow, every copy
and index is bounded; `/GS` would catch a stack smash in our functions; the runtime does not forward guest
APCs; no `WSACleanup` anywhere; `voice_on_packet` and the ADPCM decoder are bounded.

Common to all four: voice chat active (since build 22, when these crashes started). Not explained yet: how
the voice code (waveIn/waveOut with `CALLBACK_NULL`, its own thread, `sendto` of voice frames on the same
socket) could corrupt what Winsock calls. Candidates: concurrent `sendto` from the voice thread and the
online thread while the socket is replaced (`netplay_stop` closes it after a 2 s wait that can time out),
or a network filter/LSP on players' PCs (Discord, VPNs) that misbehaves with two threads.

Build 25's crash report prints the raw words around rsp and the stack depth; the next report should show
whether a `ret` popped 0x1E. Workaround to suggest meanwhile: voice chat off for players who crash.

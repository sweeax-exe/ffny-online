# The "RIP=0x1E" crash on the online play thread

Status: study only (2026-10-26), no code changed. Evidence: six bug reports from builds 22 to 28.
Confidence in the cause: high on the mechanism (a late write of an I/O status block into the
thread's stack), moderate on which Windows component leaves the I/O pending (it needs one
confirming experiment, below).

## Symptom

`[CRASH] Access violation at RIP=0x1E, fault addr=0x1E (write)`, all guest registers 0 (a host thread,
not the game thread). The "(write)" is only the printer's label for any non-zero
`ExceptionInformation[0]`; the code is 8, an instruction fetch at 0x1E. The crashing thread is
"online play" (`net_thread`, `src/hooks/netplay.c`). Lockstep hub matches, mid-fight, checksums equal
right before, no rollback.

| Report | Build | Value | Notes |
|---|---|---|---|
| 20261024-022316.591-7699f2 | 22 | RIP=0x1E | game at 4 fps, a "Waited ... (connection)" notice every second |
| 20261024-190129.726-979508 | 24 | RIP=0x1E | thread name "online play" (the build 24 report has it) |
| 20261024-212354.823-7887b5 | 24 | RIP=0x111 | same stack shape, a different small number (not in the first list of five) |
| 20261025-005751.228-d9236f | 24 | RIP=0x1E | after 1130 equal checksums, a signed-in player |
| 20261025-060344.452-49021f | 24 | RIP=0x1E | |
| 20261025-234700.424-b70fb8 | 28 | RIP=0x1E | the only one with the raw stack words, below |

In the first five the crash handler then crashed itself in `veh_handler` (a read at the thread's
stack base, `0x94260000` and similar): its stack scan runs past the top of a host thread's stack.
Build 28 has the guard; that is a separate, harmless bug.

## What the stack says (report b70fb8, build 28)

`rsp = 0x845FF6E0`, stack base `0x84600000` (2336 bytes used). The report prints raw words starting at
rsp-16:

```
rsp-16   0x0000000000000000
rsp-8    0x000000000000001E      <- the value the CPU "returned" to
rsp+0    0x00000000845FF778      (a stack address: a local of the caller)
rsp+8    0
rsp+16   0x0000000141A49ED0      (exe data)
rsp+24   0
rsp+32   0x8E0
rsp+40   WS2_32.dll+0xF8AB       (a return address inside WS2_32)
rsp+48   0x8E0
rsp+56   0x8497FEA0  ...
rsp+64   0x140000000 ...
rsp+88   0x140019931             (a return address into our exe)
rsp+200  0x14001250A             (a return address into our exe)
```

1. The instruction pointer is 0x1E and the word just below `rsp` is 0x1E, while `[rsp]` is not a code
   address. A call through a null-ish pointer would have left a return address there. So this is a
   `ret` that popped 0x1E: a function's return-address slot holds 0x1E.
2. The two words at rsp-16 and rsp-8 are `{0, 0x1E}`, 16-byte aligned. That is the exact layout of an
   `IO_STATUS_BLOCK`: `Status` (0 = STATUS_SUCCESS) then `Information` (the number of bytes
   transferred). The return slot of a function was overwritten by the `Information` field of a
   completed I/O request, 30 bytes.
3. 30 bytes is a real packet size on this thread: a `P_LIVEPAD` DATA packet is 6 (header) + 1 (slot) +
   1 (type) + 4 (sequence) + 18 (`XBOX_GAMEPAD`) = 30. The other report's 0x111 = 273 is a `P_STEPS`
   packet with `REDUNDANT` = 8 entries: 6 + 1 + 1 + 4 + 4 + 1 + 8 x 32 = 273. Those two are the most
   common packets of a lockstep match. A coincidence twice, with the right sizes, is not credible.
4. The frame directly above the corrupted slot belongs to WS2_32 (`WS2_32.dll+0xF8AB` at rsp+40). The
   function that returned was called by WS2_32: it is `recv`'s own call chain (WS2_32 calling the
   mswsock provider). All six reports show the same shape (data pointer, image base, return address,
   second return address at the same distances, in builds 22, 24 and 28), i.e. the same call path
   every time. The thread spends nearly all its time in `recv` (a 20 ms receive timeout, about 50 calls
   a second), so this is where a stale write lands.

## The mechanism

`start()` sets `SO_RCVTIMEO` = 20 ms on a blocking UDP socket and `net_thread` calls `recv()` in a
loop. A blocking `recv` with a timeout is implemented inside mswsock/AFD as an asynchronous request
that completes into an `IO_STATUS_BLOCK` on the calling thread's stack, plus a timed wait. When the
wait times out the provider cancels the request. If a datagram arrives at the same moment, the
request can complete successfully (`Status` 0, `Information` = bytes received) *after* the receive
call has already returned `WSAETIMEDOUT` and its frame is gone. The completion then writes its
16 bytes into the stale stack address. By then that address is part of another live frame, here the
return-address slot of a function WS2_32 has called on a later `recv`. Microsoft's own documentation says
that after a `SO_RCVTIMEO` timeout "the socket state is indeterminate and should not be used". This
loop uses the socket after thousands of timeouts, once per 20 ms.

It is rare (one packet landing in a window of microseconds in 20 ms), which fits "a handful of
reports from over a hundred players in three days", and it needs a busy packet stream, which is why
it shows in lockstep matches (60+ packets a second) and not in menus.

What is not shown: exactly which module leaves the request pending (WS2_32 `recv` -> mswsock
`WSPRecv`, or the cancel path in ntdll). The stale `ntdll.dll+0x1D38F0` / `mswsock.dll+0x23A60`
words in the stack are consistent with that path but are a scan, not a walk. The fix below does not
depend on it.

## Candidates, ranked by evidence

| # | Candidate | Verdict |
|---|---|---|
| 1 | A timed-out blocking `recv` leaving its I/O status block to be written later | Fits every observation: `{0, packet length}` at rsp-16, an 8-byte value in a return slot, packet sizes 30 and 273, WS2_32 frame above, same shape in six reports, no message type in common, no pattern in step number (step 3780 to 9960). |
| 2 | Stack buffer overflow in a packet parser | Rejected. Every handler was read: `Reader` (`rd8`, `rdbytes`, `rdstr`) bounds every read against the packet length; `on_rooms` (32), `on_players` (40), `on_room` (candidates 3, players 10, pool 32), `on_hill` (line 10, snprintf with `n < size-8`), `on_start` (count <= 4, `pos_of_slot` guarded), `T_PEER` (`len >= 8 + n`, copies at most 24), `T_WELCOME`, `T_ERROR` (`why[]` index 1..10 of 11 entries), `store_steps` (`len >= 9 + count*32`, index `% RING`), `P_HASH` (index 9 < 10), `voice_on_packet` (exactly 326 bytes, slot checked, index clamped). A smash would also write attacker-chosen bytes, not `{0, length}`. |
| 3 | A call through a NULL function pointer or an empty vtable (+0x1E) | Rejected. Nothing on this thread is called through a pointer table: packet handlers are direct calls (inlined into `net_thread` by the optimiser), `voice_on_packet` is direct, `automatch_request` only fills a struct. And `[rsp]` is not a return address, which a `call` would leave. |
| 4 | A C++ object or UI state used after free from another thread | Rejected. The thread touches `N` under `g_cs`, `voice` under an SRW lock; there is no object lifetime, no ImGui or overlay call on it (`toast` only formats text). |
| 5 | A stray write by the game's own guest code into the host stack | Unlikely. Guest addresses map to host `0x10000000 + VA`, so a wild guest write could in principle land on a host thread stack (stacks sit at `0x84...` to `0x95...`), but it would not write `{0, a packet length}` at the same depth in five different runs. |

## Fix proposal

File `src/hooks/netplay.c`, functions `start()` (socket set-up), `netplay_stop()` (tear-down) and
`net_thread()` (the receive step). Do not use a blocking `recv` with `SO_RCVTIMEO` at all: have no
request of ours pending on a stack variable.

1. In `start()`, replace the `setsockopt(... SO_RCVTIMEO ...)` with a receive event:
   `N.rx_event = WSACreateEvent(); WSAEventSelect(N.sock, N.rx_event, FD_READ);` (this also makes the
   socket non-blocking). Add `WSAEVENT rx_event;` to the `N` struct, create it in `init_once` if
   preferred.
2. In `net_thread()`, replace the single `got = recv(N.sock, ...)` by a wait and a drain:

   ```c
   WSAWaitForMultipleEvents(1, &N.rx_event, FALSE, 20, FALSE);   /* the 20 ms the timeout gave */
   WSAResetEvent(N.rx_event);
   for (;;) {
       int got = recv(N.sock, (char *)b, sizeof(b), 0);
       if (got <= 0) break;                    /* WSAEWOULDBLOCK: nothing more */
       ... existing lag_ms / on_packet handling for this packet ...
   }
   ```

   The existing "release what the lag queue holds" loop stays after it. Handling every waiting datagram
   per pass also removes a small 20 ms-per-packet queueing delay.
3. In `netplay_stop()`, call `WSAEventSelect(N.sock, NULL, 0)` and `WSACloseEvent(N.rx_event)` after the
   thread has stopped and before/with `closesocket`; set `N.rx_event` to `WSA_INVALID_EVENT`. Add a
   `SetEvent`-style wake-up (`WSASetEvent(N.rx_event)`) before the `WaitForSingleObject(N.thread, 2000)` so
   stopping does not wait 20 ms.
4. `sendto` on a now non-blocking UDP socket may return `WSAEWOULDBLOCK` when the send buffer is full:
   `send_raw` already ignores the result, which is correct for UDP (the next step repeats the inputs).

The simpler alternative, if a smaller change is wanted: `ioctlsocket(FIONBIO)` plus `select()` with a
20 ms `timeval` before each non-blocking `recv`. `select` waits in AFD with the timeout inside the
request and has no cancel path either.

## How to confirm before shipping

- A standalone reproduction (no game): one thread sends UDP datagrams of 30 and 273 bytes to a socket
  on the loopback at jittered intervals around 20 ms; the receiver is the current loop with
  `SO_RCVTIMEO` = 20, and after every `recv` it calls a function with a canary array placed in the
  stack shape of `net_thread` and checks the canary and its own return address. Expect `{0, n}` to appear
  after a few hundred thousand timeouts if the mechanism is right. Run the same with the event/`select`
  version: it must never fire.
- After the fix, the first crash report with `RIP` below 0x10000 on the "online play" thread means a
  second cause exists; the stack words of that report would then say which.

## Side notes

- `veh_handler` should clamp its stack scan at the thread's stack base (`NT_TIB.StackBase`) so a crash
  report never ends in a second crash (the older builds' reports). Build 28 prints the thread name
  and the words; keep both.
- The 0x111 report (7887b5) was in build 24 and is not in the first list; it has the same stack
  shape, which settles that the corrupted word is data (a packet length), not a pointer.

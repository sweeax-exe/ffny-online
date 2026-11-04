# Rollback netcode: where it stands and a plan to finish it (2026-11-01)

A read-only study of `remaster` at e105db6. Nothing was built or run. Sources: `src/hooks/rollback.c`, `src/hooks/netplay.c`,
`server/relay/cmd/relay/main.go`, `docs/research/rollback-*.md`, `rollback-sound-crashes.md`, `music-director-crash.md`,
`stream-table-crash.md`, `setstreamsource-crash.md`, `stuck-screen-fade.md`, `generator-timescale-desync.md`,
`created-fighter-online.md`, `crash-001B2480.md`, git history from 2026-10-22, and the player reports in `logs/bug-reports/`.
`PROGRESS.md` on this branch has no rollback entries (only D53, which is unrelated), and `docs/worklog/2026-10.md` has none either. The history is in the
research notes and the commit messages.

Tags: **[V]** seen in code, a log or a commit. **[I]** inferred, with the reason given. **[G]** a guess that still has to be tested.

---

## 1. How rollback works in the code today

### 1.1 When it is on
* **Local setting.** `rollback_enabled()` (`rollback.c:188`) reads `RECOMP_ROLLBACK=on|off`, or else the `[online] rollback`
  setting. It is read once at start-up, because the audio arena is set up together with the sound engine. [V]
* **Relay switch.** The relay sets `CAPS_ROLLBACK` (bit 1) only while `<updates>/rollback-allowed` exists, or always with
  `-rollback` for tests (`main.go:87,143-160`). [V]
* **The PCs agree.** Each PC sends `peer_rb` in `P_RTT` byte 7: its setting AND the relay cap (`netplay.c:1581`).
  `every_player_rolls_back()` (`netplay.c:936`) returns 1 only when every player has it on. It returns 0 for **King of the Hill**
  (369e8d9, build 28) and for **created fighters** (`match_has_created`). With rollback, `agreed_delay()` is `RB_LESS` = 4 steps
  shorter, with a minimum of `DELAY_MIN` = 2. A PC predicts at most `RB_AHEAD` = 8 steps ahead. `N.rb_match` is decided at the
  sync point (`netplay.c:2647`). [V]
* **The arena runs even when the relay switch is off.** `rollback_audio_init_begin` and `arena_start` (`rollback.c:207,1522`)
  look only at `rollback_enabled()`. So a player with the beta setting on gets the audio arena and its routing in every game,
  including lockstep matches. [V code] Player reports from 10-26 to 10-31 reach 59.0 to 63.9 MB of the 64 MB contiguous window
  with the arena (5.5 to 10 MB). Without it they reach 58.0 MB. [V `[MEM]` lines]

### 1.2 Save point, saves and loads
* **The hook.** `sub_0002C530_enter`, the first call of each game update, calls `netplay_on_update()` (`netplay.c:~3915`). [V]
* **Step 1.** `rollback_prepare()` (`rollback.c:884`):
  1. Frees the kept arena blocks that nothing points to (`release_kept_arena`, a conservative scan).
  2. Holds the game thread for 150 ms (`HOLD_MS`). Every `.data` page and heap page that changes during the hold belongs to
     another thread and is left out.
  3. Builds the `.data` segments.

  On the next update `rollback_reference()` copies the whole 42 MB heap `0x80003000..0x82A0D000` as the reference. It then
  makes every kept heap page read-only. The first write to a page faults into `track_fault`, which marks the page "changes from
  now on" (game thread) or "foreign" (any other thread). [V]
* **Save, every update.** `snap_save()` → `rollback_save(step % 16)` copies about 920 KB of `.data` in 13 pieces and every heap
  page known to change: 770 to 835 pages in player logs, about 4.0 to 4.3 MB per update. The snapshot also keeps the netplay
  side: `N.seeds`, the private crowd generator `g_crowd_gen`, and the test step. [V `rollback.c:992`, `netplay.c:3824`, log 6dbf17]
* **Load.** `rollback_load()` (`rollback.c:1046`):
  1. Drops the frees held since that save (`forget_frees_since`).
  2. Restores `.data`.
  3. Restores the saved pages around the words the mixer writes (`load_page`, `s_mixmask`).
  4. Restores pages that first changed after the save from the reference.

  There are 16 slots (`ROLLBACK_SLOTS`). [V]
* **Detection and re-run.** `netplay_on_update` checks steps from `final_step+1` to `step`. The first step that ran on a
  prediction (`used_pred`) that differs from the entry that has since arrived is the target. `rollback_to(f)` (`netplay.c:3840`):
  1. Loads the save.
  2. Rewinds `N.step`, `N.updates`, the seeds and the crowd generator.
  3. Sets `N.resim = 1`.
  4. Runs each missed update the way the main loop does: `sub_000DD930` (input), `sub_00077040` (screen manager, which holds the
     step), then, if that returns "go on", `sub_000BD860`, `sub_000BDF40` (event manager) and `sub_0001A240` (render tick).
     There is a save before each update except the first. Nothing is drawn.
  5. Restores the registers and the x87 state.

  Fighter checksums go out only for final steps (`send_hashes`), so a peer compares the same values as in lockstep. [V]
* **The fight's end.** When the match phase word is 2 or more and every step is final, `rollback_reset()` → `track_stop()` gives
  back everything held during the fight: contiguous frees, arena frees, XAPI heap frees and late frees. [V `rollback.c:411`]

### 1.3 What is left out of the saves, or kept live on purpose (all [V] in `rollback.c` unless noted)
| What | How |
|---|---|
| **Sound engine memory** | Allocations from the audio code (AEMS `0x1C000..0x1E000`, the audio subsystem `0x30000..0x3E200`, the engine `0x11C000..0x142000`) come from an **audio arena in contiguous memory**. They are routed by `rollback_route_alloc` (`:1542`) through the hand-written `sub_00029500`. The arena starts at 3 MiB and grows in 1 MiB pieces, to at most 12 MiB, 16 pieces and 65536 blocks. Also routed: allocations on any thread other than the game thread during a fight, allocations inside a sound command, and the file layer's stream buffers (`0x1E3F20`, decided by the call chain). |
| Sound in re-runs | Silent. `netplay_sound_command` and 10 bus handlers return 0 while `N.resim` is set (`netplay.c:3763-3786`). A sound command that names an engine object already given back is also silenced (`rollback_sound_command_dead`). A sound for a hit that was predicted wrongly has already played. |
| Audio `.data` | Gaps in `k_data`: the AEMS globals, the game's sound globals, the 32 bus objects `0x3454A0..0x345968`. `k_data_skip`: the engine pages `0x2E6000..0x2E8000` and `0x3A9000..0x3B1000`, and the producer and loader words `0x3C8000..0x3CA000`. `k_data_hole`: the music overlay handle `0x2DBC40..` (`music-director-crash.md`) and EAGL's copies of the D3D bindings. |
| Mixer writes into game heap objects | The mixer's words are kept live (`s_mixmask`, set by `mixer_end`). Heap frees from other threads wait until the fight ends (`late_free`). Blocks allocated in the fight and freed late are dropped (a small leak). |
| D3D / GPU | Not rolled back. `rollback_unbind_textures()` runs at every fight update, so no texture or palette reference count is carried across a save or load (`stuck-screen-fade.md` §9). XAPI heap frees made on the game thread are held (`rollback_hold_xapi_free`, `sub_001FBE1E` in `recomp_manual.c`). |
| Contiguous frees | Held until the fight ends (`hold_contig_free`, 4096 at most; beyond that they are freed at once, which is unsafe). |
| Heap pages | The frame-record queue, the file layer's request block `[0x3C8F30]`, the audio pages, and the pages another thread wrote during the hold. |
| **Crowd** | The crowd is restored like the rest of the heap. At step 1 `crowd_reset` restarts the spectators field by field. The crowd gets the fixed 1/60 frame argument and its own generator copy (saved per snapshot). A guard stops the background crowd's frame accumulator from running off its model (e6c56a5, `setstreamsource-crash.md`). |
| Shared generator `0x39C0E0` | Re-seeded at the start of every step from (session, match, step) (`shared_gen_reseed`, f82aa19). A re-run gets the same seed, and draws made outside the step are wiped. |
| Not wrapped [I] | The other bus subscribers (HUD, rumble, stats) do run in re-runs. Only the sound handlers are silenced (`rollback-step-structure.md` §2.1). |

### 1.4 Test tools that exist
* `dj.py scenario online --rollback on [--fast-ko 0 --bot 60] [--mode koth --players 3] [--rematch] [--venues N]`. The relay
  gets `-rollback` and each copy gets one guest CPU of its own.
* `--env RECOMP_NETPLAY_LAG_MS=70,10` adds simulated lag. All the 10-24/25 work used 60 to 70 ms each way. Whether `--env`
  accepts it is not checked [U].
* `RECOMP_ROLLBACK_SOAK=<depth>` with `RECOMP_ROLLBACK=on`, offline on one copy: goes back 2..depth steps every 5 updates and
  compares the step hashes. This tests whether a re-run on the same PC is exact.
* Diagnostics: `RECOMP_ROLLBACK_FULL=1` (every page in every save), `_ALLOCLOG=1`, `_WRITERS=1`, `_CHECK=1`,
  `_STREAMS_IN_HEAP=1`.
* Never use `RECOMP_GUEST_CORES=all`: `mixer_end` relies on one CPU per copy (`rollback-sound-crashes.md`, "one CPU per copy").

---

## 2. Known blockers

| # | Blocker | Evidence | Status |
|---|---|---|---|
| B1 | **Sound engine crashes** (the reason for build 21) | Builds 19 and 20: junk vtables in `sub_0012BE20` and `sub_00126150`. Root causes found and fixed on 10-24 and 10-25 (commits 0db202f … 0bc7a6b, 3cfd77b):<br>• `arena_free_now` merged free blocks across the gaps between arena pieces<br>• the toolkit APU's unchecked voice handles<br>• audio `.data` restored under the running mixer<br>• the mixer's heap words<br>• late frees<br>• held XAPI frees<br>• kept arena blocks<br>• stream buffers<br>• the music overlay handle.<br>Result: the first match runs clean in nearly every run (4-7 min, 1000-2500 rollbacks). [V `rollback-sound-crashes.md` "State at the end of 2026-10-25"] | **Mostly fixed. Open:** (a) a dynamic load early in a fight, `sub_0010AC20` from `sub_0010B3E0`, of a buffer the audio async loader `0x1AA99` had just put in the arena (run-20261025-154502; `rollback-async-loads.md`); (b) "occasional second-match faults not yet analysed". The 10-minute multi-match run without a crash, set as the bar in that note, was never recorded. |
| B2 | **Contiguous memory / audio arena** | Build 20: a fixed 12 MiB arena left the GPU too little room (froze at 62.9 of 64 MiB). Fixed by an arena that grows. [V comment `rollback.c:150`]<br>Now: reports with the arena reach 59-63.9 MB, without it 58.0 MB. In 6dbf17 the arena grew to 7.9 MB over two hill fights. Each fight leaves blocks still named (142 after fight 1 in 6dbf17), and fight-allocated late frees are dropped (a leak by design). If more than 4096 contiguous frees are held, the rest are freed at once (`s_held_over`), which is unsafe. [V] | **Open, never measured over many matches.** Whether the white-block / out-of-memory symptom comes back after 5-10 rollback matches is [U]. |
| B3 | **King of the Hill freeze** (the reason for build 28) | Build 27: with rollback, games froze or crashed early in the second consecutive hill fight. The relay logged the fighter `reason=timeout` (`how=3`). Not reproduced locally. No log from a frozen game exists. [V memory notes, 369e8d9]<br>The only build-27 hill log (6dbf17, F9 picture, the winner's PC):<br>• the next START arrived before rollback switched off for fight 1 (lines 2440-2446)<br>• fight 2 prepared and tracked normally<br>• the cumulative `rb_count` stayed at 514 through update 1800 of fight 2: **no rollback in that fight at all**, against 333 by the same point in fight 1 at the same ping<br>• the game was still running at step 2820.<br>Also: "the next match never starts" happens **without** rollback as well. On 10-28, KOTH in lockstep: the king's next fight never starts and the third copy gives up at step 3 (`created-fighter-online.md`). In the rollback rematch run both copies sat on `battle/cmtype` with the automatch armed (`crash-001B2480.md`, run-20261028-015026). [V] | **Cause unknown.** Candidates: [G1] a general room-flow stall between fights (it exists in lockstep too); [G2] rollback state from fight 1 carried into a START that comes straight after it; [G3] `netplay_on_crash` (CreateThread and WinHTTP inside the exception handler) hanging a crashing game instead of closing it; [G4] prediction never re-armed in fight 2 (the zero-rollback observation). |
| B4 | **Desyncs under rollback** | Fixed: mixed rollback/lockstep rooms (all-on rule, e42fa46). The Terror Dome shared-generator drift is fixed by the per-step re-seed (f82aa19; run rb1: 680 equal checksums, 5839 and 7554 rollbacks, `generator-timescale-desync.md` §9). Created fighters crashed at step 4 under rollback, so they were excluded; since the fifth round (build 45) they roll back (§5). [V]<br>**Open:** the lockstep position desync (`lockstep-desync-positions` memory: bot vs bot, venue 9, step 6120, a few ulps; a real pad vs bot, venue 6, step 60, large). It is the same simulation, so rollback inherits it. | Waits for the position-desync fix (§4). |
| B5 | **Crashes from inconsistent restored state** (not sound) | Build 26: `sub_000EDF90`, a model node tree walk into a reused child array, at step 4039 (`generator-timescale-desync.md` §7). Build 28, 1v1 with rollback on for every player, after all the 10-25 fixes:<br>• 9fd9bd: `sub_000E61E0+0x67`, **during a re-run**, step 145, 47 rollbacks; `edi` 0x80129778 is inside the system pool 80120D80+256 KB<br>• 22ecc8: `sub_000EE000+0x215`, **during a re-run**, step 813, 4441 rollbacks<br>• 7b829d: native `RIP` in `sub_001EE3C0+0x3E9`, a normal update, step 806, in the third rollback match.<br>A rollback stock match at venue 26 crashed once on 10-28 (`RIP 0x1408203D2`). [V] | **Not analysed.** Best guesses: [G] the small-block pool (`0x3A4000..0x3A7000` together with its heap block) and model nodes allocated or freed inside a re-run. The `sub_000E61E0` and `sub_000EE000` names come from the lifted headers only. |
| B6 | **Performance** | Saves of 4.0-4.3 MB every update, about 250 MB/s of `memcpy` at 60 Hz. Rollbacks average 1.0 ms in local tests and 5.3-6.4 ms in a player's log; the longest was 20.9 ms and the deepest 9 steps. Rollbacks over 30 ms are logged with their model loads (`ROLLBACK slow`). Step 1 holds the game for 150 ms and copies the 42 MB reference. [V] | Not measured on a weak PC. Not a known crash cause. |
| B7 | **Gaps in observability** | `rb_count` and `rb_steps` are never reset per match (B3 needed arithmetic to see this). `rollback_reference` fails silently when `!s_ref || !s_ndseg`. No hang dump comes from players. [V] | Easy to fix. |
| B8 | **Code drift since the last rollback test** | The last rollback runs were 10-26 to 10-28. Since then: upstream v0.12.x merges with re-lifts, KOTH changes, versus hold, created fighters, build 36/37 changes. `rollback.c` hard-codes guest addresses and return addresses (`0x29696`, `0x1E3F20`, `0x1284B`, ...). The guest image is the same, so they should hold [I], but the hooks (`recomp_manual.c` `sub_00029500`, `sub_001FBE1E`) have not been run since. | Step 0 done (§5): the game's heap had moved to `0x81000000`, every rollback match crashed at step 80; fixed in 70f9219. |

---

## 3. Plan: smallest risky step first

Each step is one branch or commit, tested before the next. Runs follow the machine lock and one game at a time. "Long run" here
means `dj.py scenario online --rollback on --fast-ko 0 --bot 60 --matches 5 --keep-going --env RECOMP_NETPLAY_LAG_MS=70,10`
(if `--env` rejects it, add the variable to the allowed list first).

**Step 0. Baseline, no code change.** On the current `remaster` build (re-lift first: gen code went stale after the 0.12.2 merge):
* two long runs at 70+10 ms;
* one run with `--rematch`;
* one offline soak: `RECOMP_ROLLBACK=on RECOMP_ROLLBACK_SOAK=9`, 10 min, bot fight.

Record: crashes (`dj.py digest`), desyncs, rollback counts, `[MEM] contiguous` per match, `ROLLBACK slow` lines. Result: the
real state of B1, B2, B5 and B8 today.

**Step 1. Diagnostics only (low risk).**
* Per-match `rb_count`, `rb_steps` and `rb_deepest` in the "rollback on/off for this match" lines.
* A log line when `rollback_reference` fails.
* `rollback_crash_state` gains `this` / vtable / `+0xC` / `+0x10` with `rollback_page_state()`, and whether the faulting block was
  allocated inside a re-run (suggested in `generator-timescale-desync.md` §7).
* Log `N.predicting` the first time it turns on in each match, for B3/G4.

Test: Step 0's runs again. The output must not change apart from the new lines.

**Step 2. Make the arena depend on the relay as well (B2 exposure for everyone with the setting on).** The arena has to start
with the sound engine at boot, before the relay is reached. Options: the launcher passes `RECOMP_ROLLBACK=off` unless the latest
manifest or relay status says rollback is allowed (`STATUS` flag bit 2 exists). Or the setting is ignored unless the client saw
`CAPS_ROLLBACK` in its last session. [G, decision for the owner]

Test: `--rollback off` boots show no `[ROLLBACK]` arena lines, and the contiguous peak is about 58 MB.

**Step 3. B5 crashes (restored-state class).**
1. Reproduce with the offline soak (depth 9, also with weapons and venue hazards) and with long runs.
2. Use `RECOMP_ROLLBACK_ALLOCLOG=1` and Step 1's crash lines to name the block behind `sub_000E61E0` and `sub_000EE000`.
3. Use `RECOMP_ROLLBACK_FULL=1` as the control: if FULL is clean, the write tracking misses a writer; if not, something outside
   the saves is involved (pool, host, contiguous).

Fix in `rollback.c` (the page sets or the frees held across a load).

Test: the soak gives 0 "came out otherwise", and the long runs give no crash.

**Step 4. B1 remainder (async loads, second-match faults).**
* Route or hold the audio async loader's buffers (`0x1AA99`) the same way as the stream buffers, or make the dynamic-load path
  skip blocks in the arena (`rollback-async-loads.md` §3.2 items 1-2: the file layer's request block is already skipped).
* Analyse every second-match fault from Step 0.

Test: the bar from `rollback-sound-crashes.md`, **10+ minutes of several matches with no crash, hang or desync**, run three
times. Add `--rematch` and `--venues 6` (Terror Dome) and `--venues 26`.

**Step 5. B2 contiguous memory over a session.**
1. Run `--matches 10` (fast KO 600) and record `[MEM]` at every match start and end, the arena total, the held-free overflow,
   and the "still named" counts.
2. Fix leaks found. Shrink the arena pieces given back between fights. Raise or chain `s_held_free` past 4096, or stop the fight
   from freeing more than that.

Target: the peak stays under about 61 MB after 10 matches (a figure to agree on, [G]).

Test: the same run and a 30-minute bot session.

**Step 6. B3 King of the Hill.**
1. Add a test-only switch that lifts the KOTH exclusion in `every_player_rolls_back` (both PCs must agree, as with `-rollback`).
2. Check the room-flow stall alone first: `dj.py scenario online --mode koth --players 3 --matches 6` in **lockstep** has to pass
   (on 10-28 it did not). Without that, a rollback result means nothing.
3. Then the same with `--rollback on`, `--fast-ko 600` and `--fast-ko 0 --bot`.
4. Check each of G2 and G4 in the logs: rollback-off logged after the next START; prediction on in fight 2.
5. For G3, make `netplay_on_crash` do nothing that blocks inside the exception handler (queue the report to a thread that
   already exists).

Test: 6 hill fights in a row on 3 copies, the king changing, no timeout on the relay.

**Step 7. B6 performance.**
1. Measure the cost of a save and a load per update, and the CPU time per frame, with one guest CPU at a lowered clock or with
   a CPU-burn neighbour.
2. Cheap wins to try:
   * Save only while a step is not final. When nothing is predicted, the slot of the newest final step is enough.
   * Use write tracking for `.data` too, which is 920 KB copied every update.

Test: rollbacks average under 5 ms and no frame over 16.7 ms on the reference weak PC [G thresholds].

**Step 8. Beta re-open (after §4 is met).**
1. Put `rollback-allowed` on a test relay only.
2. Real two-PC matches over the internet (EU to NA) for 5 matches.
3. Check mixed settings (lockstep fallback).
4. Run the release checklist: 5-match two-copy test, KOTH on 3 copies, HD on and off, fresh install.
5. Ask the owner before touching the live relays (server rule: announce in #server-alerts first).

**Step 9. Created fighters under rollback (last).** Their runtime-built looks are outside the saves (crash at step 4). This needs
the created-fighter desync fix first.

---

## 4. What waits for the lockstep position-desync fix

Rollback needs the same determinism as lockstep, plus one more thing: a re-run on the same PC must come out exactly as the
first run did. A lockstep drift (positions apart by ulps at step 6120, or by a lot at step 60 with a real pad) shows up the same
way in rollback matches, through the checksums of final steps. While it is open, a rollback desync cannot be told apart from
the base desync. [I]

* **Can go ahead now:** steps 0 to 3, step 5 and step 7. Crashes, hangs, memory and timing are judged by crashes and `[MEM]`
  lines, not by checksums. The offline soak checks re-runs on one PC and does not depend on the drift between PCs.
* **Must wait:**
  * any "no desync" verdict for rollback;
  * the desync part of the step 4 and step 6 acceptance criteria;
  * step 8 (beta re-open);
  * step 9 (created fighters);
  * any further determinism work inside rollback (for example deciding whether HUD or rumble handlers must also be silenced in
    re-runs because they feed state back).
* **Could help the other agent [G]:** if the drift comes from host-timed state read inside the step, the offline soak at depth
  9 would show it as "came out otherwise" on one PC. That is cheaper than a two-copy run. Running the soak with a real pad as
  well (the step-60 case) might separate a pad-input cause from a timing cause.
* **Re-check after the fix lands:** the re-seed of the shared generator, `crowd_reset` and the fixed crowd frame argument all
  live in `netplay_on_step`. If the position fix changes that order or adds per-step pins, `rollback_to` must run them in
  re-runs too (they run there now because a re-run goes through `netplay_on_step` with `N.step` rewound; check `N.resim` guards
  in any new code).

---

## 5. Results (branch work from 2026-11-02, on `remaster` 60b3134)

All runs: `dj.py`, release preset, two copies on the local relay with `-rollback`, logs under `logs/dj/`.

### Step 0, baseline: every rollback match crashed at step 80 (B8)
* `20261102-003545-online` (5 matches, `--fast-ko 0 --bot 60`, 70+10 ms lag): both copies crashed at step 79/80 of match 1,
  4-5 rollbacks in, in the sound engine on the mixer thread (`sub_0011E530+0x282`, a wild indirect call just before).
  [V]
* Cause [V]: since the toolkit's v0.12 merge the contiguous allocator keeps a low reserve, so the game's 42 MB heap moved from
  `0x80003000..0x82A0D000` to `0x81000000..0x83A09000` (`[CONTIG] alloc 0x81000000 43044 KB`). `rollback.c` still saved and
  restored the old range: the audio arena (`0x807A7000`, `0x80B0D000`, ...) and GPU blocks below 16 MB were rewound by every
  load, and the heap's top 16 MB were never saved. The prepare line showed it too: "heap 43 pages left alone (40 written by
  other threads)" against 6 / 1 in October. XAPI's heap also grew to `0x00F80000..0x04000000`, past the `0x01600000` the
  kept-arena scan stopped at.
* Fix 70f9219: the heap is found at run time (the largest block of the contiguous window), the scan covers XAPI's heap to
  `0x04000000`.

### Step 1, diagnostics (e2b8853)
Per-match line `[ROLLBACK] match N decided|went on offline|ended with rollback still on: ...` (rollbacks, steps run again,
deepest, average/longest ms, slow count, first prediction step), `[ROLLBACK] first prediction in match N at step S`, a line when
the reference copy fails, the crash state with the registers' objects (vtable, +0xC, +0x10, page state, system pool / arena,
allocated in a re-run), the crash callback armed when rollback starts. Test tools: `RECOMP_ROLLBACK_SOAK_DIFF=1` (memory
before a soak goes back against the re-run), `RECOMP_ROLLBACK_SOAK_TWICE=1` (a second re-run of the same steps compared with
the first), and the soak now replays each step's pad words in re-runs.

### After the heap fix
* `20261102-004911-online` (same settings): match 1 (venue 2, 8522 steps, 1323 / 448 rollbacks, deepest 9) clean; match 2
  (venue 9, a bot fight without a knockout, 41 800 steps = 11.6 min, 3210 rollbacks on one copy) ran without a crash but
  **desynced at step 37 440** (fighter positions 0x44/0x4C apart by 0.02; the last equal checksum at step 37 380). The copy that
  went on offline afterwards never ended the fight, so the run was stopped by hand.
* Contiguous memory: 56.5 MB at the first match start, 58.2-60.2 MB at its end, 59.2 / 61.2 MB reached at the second start,
  62.3 MB reached at the second end (arena 7.2-7.4 MB live, 6339-7058 blocks freed before a load and kept). B2 is real.
* `ROLLBACK slow`: two lines, 654 ms and 1414 ms with no model loads (machine shared with other agents' builds) [I].

### Step 3, the offline soak
* `20261102-013956-offline` (depth 9, bot): 1500 soaks, **6% of steps came out otherwise**; the memory diff shows hundreds of
  saved words differing after every re-run (render counters, EAGL matrices, per-object frame stamps).
* `20261102-021149-offline` (`SOAK_TWICE`): two re-runs of the same steps agreed on **all 9900 steps**, while the first pass and
  the re-run differed on 6%. So loads restore consistently; the difference came from the first pass. Cause [V in code]: the test
  bot decides a re-run's buttons again with memory that is not rewound (`test_bot.c`), so the soak's re-runs had other inputs.
  Online, re-runs replay the entries, so this was a test artefact; the soak now replays the pad words.
* `20261102-022059-offline` (pad words replayed): **0 of 10 442 steps otherwise** at depth 9 (1900 soaks). The plan's step 3
  test is met for re-runs on the same inputs.
* The case the soak never covered is the online one: a re-run after a pass that ran on a **wrong prediction**.
  `RECOMP_ROLLBACK_SOAK_WRONG=1` runs, between two correct re-runs, one on the buttons of 23 steps earlier:
  * `20261102-022953-offline`: 37 of 9300 steps differ between the two correct re-runs (all in one stretch, steps 1717-1723:
    the fighter's input-history words `+0x9F8`, `+0xA3C`, `+0xA4C`, `+0xA50`, written by `sub_001BA800`, differ by one
    button bit), then a **crash at step 8676 in a re-run**: a weapon teardown (`sub_000BA920 -> sub_000BA810 -> sub_00025B40
    -> sub_0010B490 -> sub_00109F00`, the model loader's clean-up list) walked a list node holding junk (`esi 0D0D0D0B`); the
    list's owner (a 16-byte block at `81012060`) was allocated in a re-run.
  * `20261102-033004-offline` (same, with `RECOMP_TEST_RNG_SEED=12345`, `ALLOCLOG`, `CHECK`): 39 of 9900 steps differ, then a
    **crash at step 8728 on the mixer thread** in `sub_0011D480+0xF3` (from `sub_0011E530`, registers in the audio arena): the
    sound-engine class of B1.
  * So both open crash classes (B5: models / weapons; B1: the sound engine) come back within about 9000 steps of a soak that
    makes wrong predictions (5300 rollbacks, three passes each), on one PC, in about 3 minutes. That is the test bench for them;
    the two-copy runs need 10+ minutes per crash. Not yet analysed further.

### Step 2 (0b99275) and step 5 (ed3512b)
* Step 2, option B chosen and implemented behind `[online] rollback_follows_server` (default 1): the game keeps the relay's
  `CAPS_ROLLBACK` from its last session in `[online] rollback_server` and uses the beta setting at the next start only if it was
  set. Reason: no launcher or relay change, it also holds for a player who starts the exe directly, and the relay switch stays
  the one source. A switch change takes effect at the next start in both options. Test: boots with a settings file
  (`rollback = 1`): `rollback_server = 0` gives "the rollback setting is on, but the server did not allow rollback ... no audio
  arena" and no arena line (`20261102-033423-boot`); `rollback_server = 1` starts the arena (`20261102-033455-boot`). An online
  copy writes the flag at the relay's CAPS answer ("the server allows rollback: the rollback setting applies from the next
  start").
* Step 5, first part: the held contiguous, arena and XAPI free lists grow instead of giving frees back at once past 4096 (none
  overflowed in the runs above; it was a latent use-twice).
* `20261102-033916-online` (`--matches 10 --fast-ko 600`, 70+10 ms, build ed3512b): **ok, 10 of 10 matches**, no crash or
  desync, 1-151 rollbacks a match. Contiguous memory flat: 56.6 MB in use at the first start, 56.8 / 57.8 MB at the last
  (copy 0's arena grew once more, by 1 MB, before match 9), 58.8 / 59.7 MB reached; the arena 4.0-5.0 MB live at each fight's
  end, kept blocks 0-650. So short fights do not leak; the growth seen above (62.3 MB reached) came with the 11-minute fight
  (6000-7000 kept blocks, arena 7.4 MB). The arena never gives a piece back.

### Second round (on `remaster` c36c0fb)
* **Lockstep control** (`20261102-034926-online`, `--rollback off --fast-ko 0 --bot 60 --venues 9`, 70+10 ms): two bot fights
  at venue 9, 40 200 steps (11 min) and 67 200+ steps (18.7 min, stopped by hand), every checksum equal, no desync. So the
  step-37 440 desync of the rollback run is rollback's, not the base game's.
* **Mixer crash (B1)**: `RECOMP_ROLLBACK_ARENA_WATCH=1` (the arena read-only during re-runs) caught the game thread writing
  the sound engine's objects in re-runs from 27 call chains under three roots: the audio subsystem's per-frame update
  `sub_0003D960` (every screen update calls it directly; through `sub_0003D560`, `sub_0003C320`, `sub_00036FB0`,
  `sub_00030AC0`, `sub_0001D030` into `sub_0013A9E0`, `sub_0013AAF0`, `sub_0011E260`), the AEMS stream calls of
  `sub_0003A340` (from the audio message handlers), and the engine stream call `sub_0011F5F0` that game objects make directly
  (`20261102-042533-offline`). ec6bf2c leaves the three out on the game thread while steps run again (re-lift needed).
  The seeded soak that crashed at step 8728 before (`20261102-033004-offline`) ran to its knockout at step 11 536 with it
  (`20261102-045005-offline`).
* No crash in five later wrong-prediction soaks (seeded, to their knockouts: 197 s, then 4 x 101 s of fight, about 10 minutes
  in all, 5000+ rollbacks of three passes each); a single 10-minute fight was not reached (the CPU knocks the bot out). The
  weapon / loader crash of `20261102-022953-offline` did not come back, but it was seen only once.
* **What carries a button across a load** (item 2): still open. The game's sound globals were one carrier (9d759ca puts them
  back after re-runs); with that, the seeded defensive-bot soak still has 16 of 7684 steps that differ between the two correct
  re-runs, at one place (steps 3483-3485). The new carrier report (memory at the two load points) shows **saved** `.data` words
  differing there (`0x2DBF38`, `0x33AB34`, the global generator `0x33DB5C`, CLogic `0x3494F0..`, `0x340D78..`), which a
  load cannot produce unless the save it read changed in between; 9d759ca stops a re-run from saving its own target again, and
  the report now names the save each load read, but that run (soak 14) was stopped by the build-41 test priority. Next: run it,
  then either the save changed (who wrote it) or the load is not exact.
* Also seen: `[ROLLBACK] no save state for step 3843 (now 3850)` five times in one soak: a load is refused when more than
  `4 * ROLLBACK_SLOTS` updates passed since the save, which updates without fight steps can reach in seven steps. Online that
  would leave a mispredicted step uncorrected ("may go out of sync"). Not seen in the online runs; worth a look with the
  desync.

### Third round (on `remaster` 75802ff)
* **The save-identity check** (`20261102-060154-offline`): at the step-3483 divergence the two loads read different saves (11
  and 0). Two soak artefacts, both fixed in d9430e0: a pass on wrong buttons can end at another step than the pass it replaces
  (moves that stop the fight steps for a while), and the soak computed the next pass's target from the step it ended at; and the
  wrong pass's own saves could overwrite the save the next pass needed ("no save state for step 3481 (now 3501)").
* **A real hole found on the way** (d9430e0): the save slots are per fight step, but held arena and XAPI frees were given back
  18 saves after they happened, while loads up to 64 updates back were allowed; with several updates to a step a load could bring
  back pointers to blocks already handed out again (a candidate for the weapon / loader crash). Frees are now held for
  `ROLLBACK_HOLD_SAVES` = 240 saves, older saves are not loaded, and `rollback_to` allows 240 updates back (was 64, the
  "load refusal"). `rollback_to` also used to re-run on the current state when `rollback_load` failed; a failed load is now
  treated as a missing save.
* After it: the seeded soak has **0** differing steps (`20261102-061819-offline`), and a **14-minute continuous**
  wrong-prediction soak with bots on both slots (`20261102-062408-offline`: 10 200 soaks of three passes, 56 100 steps compared)
  had **0 differing steps and no crash**. The plain soak (first pass against re-run, with the re-run audio left out) also had 0
  of 19 242 steps otherwise (`20261102-072212-offline`), so leaving the audio update out of re-runs changes nothing the fight
  reads.
* **Two-copy long runs** (venue 9, `--fast-ko 0 --bot 60`, 70+10 ms):
  * `20261102-070621-online` (d9430e0): **desync at step 10 200** of match 1 (fighter 1 position 0x44/0x4C apart by 0.001,
    fighter 2 0x54); the copy that went on offline then never ended the fight.
  * c4ea3af: a step can read its pads more than once (updates without a fight step); a prediction read and then the real entry
    in one step was recorded as "real", so such a step was never run again. The record now keeps the prediction. Whether this
    was the desync's cause is **not proven**: the case was not logged in the next run.
  * `20261102-073400-online` (c4ea3af): **ok**, two fights of 13 200 and 31 000 steps (12.4 minutes), 740 checksums equal,
    1324-2947 rollbacks a match, no crash, no desync. One clean run against two desyncs before (37 440 and 10 200): more runs
    are needed before calling the desync fixed.
* `RECOMP_NETPLAY_FINALLOG=1` (a0afab1) logs every rollback step's checksum fields as it becomes final, with how often it ran
  and read its pads; diffing the two copies' logs finds the first step they part at. Used in the target runs below: no
  difference in 51 286 + 41 024 final steps.
* **Target runs** (step 4 bar; `--fast-ko 0 --bot 60`, 70+10 ms, `--keep-going`):
  * `20261102-082054-online`, Terror Dome (venue 6), 3 matches with `--rematch`: **ok**, 850 checksums equal, 776-1421
    rollbacks a match, about 14 minutes of fight, contiguous memory 56.5 -> 57.6 MB (59.7 MB reached).
  * `20261102-084123-online`, venue 26, 3 matches with `--rematch`: matches 1 and 2 clean, then **the third never started**.
    Match 2 ended with code 0x201, an ending that stops the fight steps at once and skips the phase words 2 and 3; one copy had
    predicted step 14 668 past the other's last (14 667), so that step never became final and the copy never reported the
    result nor switched rollback off. f9cb31b keeps the step at which the match is seen decided and ends the match once that
    step is final.
  * `20261102-091559-online` (f9cb31b), venue 26, 3 matches with `--rematch`: **ok**, 680 checksums equal, about 11 minutes.
  * With `20261102-073400-online` (venue 9, 12.4 minutes) that is three runs of 10+ minutes of several matches with no crash,
    hang or desync, including Terror Dome, venue 26 and rematches: the plan's step-4 bar, met once each on the current build.

### Fourth round (on `remaster` 6c706b4)
* **Desync check** (`RECOMP_NETPLAY_FINALLOG=1`, `--fast-ko 0 --bot 60`, 70+10 ms): `20261102-094114-online` (venue 9, two
  fights, 16 249 final steps) and `20261102-094658-online` (venue 18, two fights, 53 472 final steps): ok, the two copies'
  final steps identical. Since c4ea3af five long two-copy runs (about 57 minutes of fight) had no desync; the 10 200 / 37 440
  kind did not come back, so its cause stays unnamed (FINALLOG is ready for the next one).
* **Step 6, King of the Hill** (3 copies, `--matches 6`, fast KO at step 600):
  * Lockstep, king always winning (`--ko-winner 0`, `20261102-100531-online`): **ok**, 6 fights.
  * Rollback (test switch `RECOMP_ROLLBACK_KOTH=1` on every copy lifts the hill's exclusion), `--ko-winner alt`
    (`20261102-101207-online`): desync in fight 2 at step 780, one copy knocked out fighter 1, the other fighter 2. A test
    artefact: "alt" picks the winner from each copy's own match count, which differs on the hill between a copy that fought
    and one that watched.
  * Rollback, `--ko-winner 1` (`20261102-103402-online`): fights 1-4 and 6 ok (no desync, 155-1102 rollbacks a copy); fight 5
    started on one copy only: the king (on a streak, "called again straight after the fight") got the START but never left
    the match-type screen, and its opponent gave up at step 4.
  * Lockstep, the same `--ko-winner 1` (`20261102-105039-online`): **the same stall at fight 5** (started on one copy only).
    So the hill's stall is the room flow's (B3 G1), not rollback's; in the fights that did run, rollback on the hill had no
    crash, hang or desync. G2 (rollback still on at the next START) was not seen: "off for the rest" came before each START.

### Fifth round (on `remaster` 2d09e81, build 44 base): step 9, created fighters, and the rematch flow
* **Created fighters under rollback.** The offline wrong-prediction soak with a created fighter
  (`RECOMP_ROLLBACK_SOAK=9 RECOMP_ROLLBACK_SOAK_WRONG=1 RECOMP_TEST_CP=slot=1,from=0,file=aw.bin`, bots on both slots) ran
  clean until step 1698 and then crashed there in every run (deterministic: `20261102-141226-offline`, `-143242`,
  `-143531`, `-144810`), in a normal update right after a load: `sub_001ED010` (memcpy) from `sub_001189B0+0x116`, EAGL's
  dynamic-vertex copy, writing through junk. The stock control on the same build: 13 minutes, 48 400 steps compared, 0
  differing, no crash (`20261102-141907-offline`). [V]
* **Cause** [V, traced with the crash dump of the draw command, `RECOMP_WATCH_WRITE` on its buffer object and a trace of
  EAGL's allocation callback]: a render-method draw command (game heap) that copies its vertices each frame keeps a
  buffer object at +0x10 (0x30 bytes from EAGL's allocation callback `[0x2E3988]` = `sub_00029800`, the game allocator),
  whose three D3D buffers live in the XAPI heap and contiguous memory. Drawing happens only in normal updates, never in
  re-runs. At step 1695 the beaten created fighter's low-health HUD drew for the first time; every soak load went back to
  before that, gave the commands "no buffer" again, and the next frame made another object (in a heap block the load had
  freed, same address each time) plus three more D3D buffers. Within a few loads the small-block free list held junk:
  the allocator returned `80F92F00`, `00FA1270`, `01080001`, and a frame copied vertices through it. Not specific to
  created fighters: any HUD element or model that first draws inside the rollback window; the weak record (AW5J3555)
  only made its low-health HUD appear inside a soak. [I for the general claim]
* **Fix** (bd14862): in a rollback fight the draw's buffer objects come from the arena (`rollback_live_alloc`; the two
  call sites in `sub_001189B0`, return addresses 0x118A29 / 0x118A52, through the EAGL callback hooks in
  `netplay_sound_handler`; frees of arena blocks go back to the arena), and after every load each such command keeps the
  words it had before (+0x10/+0x14/+0x18), when both are its own ("no buffer" or an object made here; a first version
  that did not check the live side wrote a freed command's `0D0D0D0D` back four times). Also: `cp_pose_in_step` runs in
  re-runs and in the soak; `created_fighter_on_step` unlinks by memory; P_RTT byte 14 says a build rolls back created
  fighters, so a room with an older build stays in lockstep (`docs/netplay-protocol.md`). Result:
  `20261102-152908-offline`, 13 minutes, 46 742 steps compared, 0 differing, no crash (first version);
  `20261102-165354-offline` (as committed), 11 minutes, 40 142 steps, 0 differing, no crash, the commands kept their
  arena objects over the loaded "no buffer" (-1) four times.
* **Two-copy runs with created fighters** (70+10 ms, `--bot 60`, rollback on every copy, the relay with `-created`):
  created vs created, no knockout, 2 matches (`20261102-164030-online`): **ok**, 450 checksums equal, 1614 and 652
  rollbacks, no crash or desync; rollbacks average 10.6-12.9 ms (stock about 1 ms: the pose pass and the created model
  cost; worth a look in step 7). Created vs stock (Doc), KO at step 2400, 3 matches with two rematches
  (`20261102-164937-online`): **ok**, 130 checksums equal.
* **Rematch hang** (live F9 40f4d1, test runs `20261102-101951-online`, `-104408-online`): three real causes and one test
  bug. (1) A held Rematch needed a second press; the last to choose started at once, so a first chooser who then took No
  left the other alone in a frozen fight for START_LIMIT_MS (90 s). Now the held choice starts by itself when the others'
  arrives (`netplay_rematch_tick`, 06d16bd), the results' No is sent at once, and a fight waiting for a player who went
  back to the room stops waiting. (2) In lockstep the other players' live pads stayed on their ports through the summary
  and the results menu, so every press went to every PC's menu: each copy's one cursor press undid the other's (0edb28e:
  their ports read at rest from `Game.GetMatchSummary`). (3) The test route never reached Rematch: the menu reads left and
  right, and 0.3 s A presses on an unpaced copy passed through two or three screens (d9380ba). Results:
  rollback, 70+10 ms, bots, 3 matches with two rematches: **ok** (`20261102-162513-online`); lockstep with copy 1
  unpaced: **ok**, 3 of 3 (`20261102-162906-online`); rollback without bots (`20261102-161012-online`): all three
  matches played (the run's end check needed the last summary, fixed in d9380ba).
* **Own connection dropped** (ca3ca13): a give-up names no player when this PC heard nobody (no relay packet, or no STEPS
  from any of two or more players, for 4 s): "Your connection is unstable", nobody reported as having left.
* **Open:** a soak with wrong predictions across a match end into the next match (the offline soak covers one match);
  hoodiebatman's crash (F9 3ee301, a rollback rematch, step 1230, mixer thread then `sub_001EE3C0` writing to 0x84000000)
  and goloom's black venue-24 scene (F9 09d35d, the palette count wrap of `stuck-screen-fade.md` §9) are not fixed here.

### Sixth round (on `remaster` eedecef): the three live rollback-beta reports
* **Sound-engine crash after loads** (hoodiebatman F9 3ee301; reproduced): two-copy rollback at venue 24 (70+10 ms,
  bots, `--rematch`) crashed in match 1 in both runs (`20261102-172650-online` step 1469, `-172928-online` step 1674),
  normal updates, in the sound engine walking lists of nodes the game thread had made in the fight (0x24 from
  `sub_00034FD0`, 0x10 via `sub_000379E0`), with the engine pool from audio start-up (arena block 80886480, 0x4B000)
  involved as in 3ee301. Cause [V for the chains, I for the mechanism]: those two bus handlers were not in the list
  silenced in re-runs (it came from the subscription call sites), so they rebuilt engine objects on payloads a load had
  brought back. Fix ff5aa6b; after it `-173220-online` (3 matches) and `-174907-online` (4 matches, 107-270 rollbacks a
  match): no crash, no desync. A bus audit (logged once per process) names 20 more audio handlers that still run in
  re-runs; silencing all of them made both PCs leave each other after the knockout (`-173704-online`), so they stay.
  The rematch runs are the cross-match test: rollback goes on through two and three rematches.
* **Black venue-24 scene** (goloom F9 09d35d): the palette count wrap of `stuck-screen-fade.md` §9 by its signature.
  The unbind ran only in phase 1, while saves and loads also happen in phase 0 (the lead-in) and in a rematch (phase 3
  carried over); it now runs at every rollback update (7ee9b63), with probes for a save that sees device bindings and
  for a bound object already one reference short. Neither probe fired in five venue-24 runs, and the black scene was not
  reproduced: the cause is closed as a possible route, not proven. No toolkit change.
* **Story crash** (goloom F9 64c4c2; also 875ad7, build 28): a NULL string view parsed by the C runtime; not the arena.
  `sub_000BD440` now returns 0 for an empty view (0bbf8d1, re-lift).

### Seventh round (on `remaster` cebaceb, build 45 reports)
* **Texture/palette count wrap with created fighters** (the owner's run-20261102-211740, F9 4ffb15, fee4d0): the probe of
  7ee9b63 fired only in matches with a created fighter: "a save with 4 device binding(s) ... running steps again". Since
  build 45 the created model's pose pass runs in re-runs and binds its textures and palettes, and no unbind ran between
  re-run updates. Fix 7cd9c19: unbind before every save and every load. Two-copy created-vs-created runs at 110 ms
  (`20261102-222901-online`, `-232122-online`): no "a save with" or "without its reference" line.
* **Crashes right after a rollback past the decision** (3b0da3, 291b96; 3ee301 was the sound-engine class of the sixth
  round): 7a11f13 stops predicting once the match is seen decided, so no step after the decision is predicted or run
  again. Loads after or across the decision are logged; the across case still happens when the deciding step itself ran
  on a prediction (`-230552-online`, `-232122-online`: one each, no crash). Not reproduced as a crash here; the
  mechanism is inferred from the three live logs.
* **`sub_0005F440` after a created fighter's match** (03ccd0, ce20af, fee4d0): not the count wrap, a summary thumb control
  missing for the created slot (its pad unlinked before `Battle.StartUserIDThumbCtrl`), set by `Battle.SetThumbCtrlID`.
  Reproduced on both copies at step 2582 (`-230552-online`, created vs created, 110 ms); guarded in 2d2cde3 (re-lift),
  then `-232122-online` (3 matches, rematches) and lockstep venue 26 (`-233330-online`, 3 matches) ok.
* **`sub_000BE3D0` in the reward pass** (59f513, 82d179, e108c2, 07bc78/51d3d3): the reward pass ran before any step saw
  phase 3, with the created pads still linked. 14068e0 unlinks on entry to `sub_00064ED0` (re-lift). Not reproduced in
  three forced-knockout matches at venue 26 (the step unlink came first each time), so the fix is not proven on a crash.

### Eighth round (on `remaster` 0bdb2c4, build 46 reports): created fighters' rollback cost
* **Measured** (two-copy, created vs created, 110+10 ms, `RECOMP_NETPLAY_CP_DRAW=on` = build 46,
  `logs/dj/20261103-023354-online`): rollbacks 9.71 / 11.92 ms average, 21-22 ms longest; of that, the created
  fighters' pose passes in the re-runs 8.6 / 10.6 ms (2.9-3.1 ms per step for the two fighters), loads 0.17 ms, re-run
  saves 0.26-0.38 ms. The same pass in every normal step cost 6.5-6.8 ms, and every pass left the device holding
  textures/palettes (1906 / 2029 passes). New per-match line `[ROLLBACK] match N time: ...` gives the split.
* **Cause** [V for the cost split, I for "nothing the step reads"]: the pass (`sub_00097F90`) ends in the controller's
  model update `sub_00027B80`, which ends in EAGL's model draw `sub_00114C80(ecx = [ctrl+0x11C])`: for each of the
  model's 19 parts the material set-up and the part's command list (`sub_00114FE0`, D3D calls among them). Drawing,
  not posing; the render traversal's own pass draws the model in the frame.
* **Fix**: the step's pass runs with the model's part count (+4) at 0 for its duration (every matrix the same, no part
  drawn), in normal steps and re-runs alike (`RECOMP_NETPLAY_CP_DRAW=on` for the old way). After
  (`20261103-023701-online`): 0.49 / 1.52 ms average, 0.63 / 2.15 ms longest; pass 0.08 ms per step; no pass leaves
  a binding; checksums equal. Stock control `20261103-025039-online`: 0.41 / 1.12 ms. Offline wrong-prediction soak
  with a created fighter (`20261103-024017-offline`, 10 min): 6600 soaks, 36 300 steps the same, 0 otherwise, two
  re-runs equal, no crash. Lockstep created vs created with bots (`20261103-025342-online`): ok, 30 checks.

### Ninth round (on `remaster` 265aa48, build 47; F9 926777 from build 46)
The report: 17 rollback matches at 172/94 ms, a rematch chain; out of sync in rematches 6BA8AA6C (step 1800) and
690AC92C (step 4980), then "gave up at step 8748" (a 10 s frozen picture); contiguous memory 56.6 -> 61.8 MB and
"[CONTIG] refused" before the 17th match.
* **Desync** [V for where it starts, U for the exact input]: reproduced in the first two-copy run (170+20 ms, created vs
  stock, venue 9, bots, `logs/dj/20261103-051343-online`, step 23280: fighter 2 in state 0x69 walking to a floor weapon
  that lay at -23.10/30.56 on one copy and -23.25/30.18 on the other). With the weapons in the checksum
  (`RECOMP_NETPLAY_HASH_WEAPONS=1`, af2fa0f) every run parted on the weapons while all fighter fields were equal
  (`-053624-online` step 1543, `-060426-online` step 510), each time about 20 steps after a **spectator's weapon** appears
  (slot spawned at the spectator's place, identical except its heap pointers) when it is tossed. **Lockstep does the
  same** (`-060736-online`, rollback off: weapons part at step 1357 the same way), so it is not a save/load gap; the
  re-runs and the update count per step (1 everywhere) are ruled out. A rematch keeps the weapons a match leaves, so
  it starts apart (match BDAEA2B8 differed at step 1): the "later rematches" pattern. Long lockstep runs looked clean
  only because the checksum never covered weapons; the fighters part only when one picks such a weapon up. Static
  read (sub-agent, not proven): the held weapon's matrix is Rot(slot+0x58) * Trans(slot+0x4C) * hand joint
  (`sub_000445C0`, the member's pose sampled in the crowd update); the toss `sub_000BBF30` copies it; the camera-dependent
  visibility bit (member A+0x42, `sub_000849C0` in the draw) gates the weapon model's own update `sub_00026D30`
  (+0x138 bit 0x800). Next: log member+0x50, the joint-0xA output and slot+0x4C/+0x58 at each `sub_000BAF90` call
  in the held mode on both copies to name the differing input; then reset the weapon pool at a rematch's start.
* **Contiguous memory** [V]: the growth is the audio arena: sound banks (0.25-1.3 MB, loaded outside the fight)
  left holes the next venue's bank did not fit. 0deb11b gives such blocks pieces of their own and gives empty pieces
  back. Six matches over venues 9,5,24,7,16,27: before 57.5 -> 59.9 MB in use, 61.8 reached (`-055920-online`, with
  `RECOMP_ROLLBACK_ARENA_TRIM=off`); after 55.7-57.0 at the starts, 54.7 at every end, 60.1 reached (`-055450-online`);
  rollback off 50.0-50.2, 58.0 reached (`-061003-online`).
* **The freeze after a desync** [V]: f32d773: once out of sync, a fight whose other player is past it (Rematch or No
  arrived) or silent for 3 s leaves for the room through the pause menu's Quit; seen in `-053624-online` match 2.
* Also seen once (`-052352-online`, venue 11, step ~1711, with the arena change): `[ICALL] target 0x00000000` from
  0x0013A4A8 (a venue hazard's sound via `sub_001A94B0` -> `sub_0008F990`), after which that copy's fight ended and it
  went to the main menu. The same null call is in build 46's F9 291b96 (step 1718), so it predates this round; not
  analysed.
* Checks: `regress.py --quick` 5 of 5 (`logs/regress-20261103-062031.txt`); rollback rematch chain, 5 matches,
  created vs stock, 170+20 ms, KO at 2400 (`-062043-online`): ok, 220 checksums equal, 55.0-55.3 MB in use.

### Tenth round (on `remaster` 9f95b2d, build 48 candidate; F9 cff81c, b379eb, 9aafd3 from build 47)
* **The mixer mask holds the game's own words** [V for the pattern, U for the writer]. `mixer_end` keeps live through
  every load the heap words that changed during a mixer tick in which the game thread's cycle count did not move.
  No local rollback run since 10-24 attributes a single word. Player logs with rollback are either 0 or the print cap
  (64 lines), and the second group holds every unanalysed B5 crash (9fd9bd, 22ecc8, 7b829d) plus 3ee301, 3b0da3,
  09d35d, b379eb and cff81c. The attributions come as bursts: 64 pages / 2333 words at once (cff81c), 55 / 2869
  (b379eb), 53 / 1320 (7b829d), spread over the whole heap. A burst like that is a game update or a load, not
  a mixer tick. All three build-47 reports run guest threads on CPU mask 0x4, so which thread wrote the bursts
  unseen is not known.
* **cff81c** (crash in a re-run, `sub_001156E0+0x2D0`, from `sub_000DF5C0`, a particle scheduler's time wheel)
  [V]: the crash dump marks edi 81605780 (node array) and edx 810E9580 (u16 index array) as "a mixer word (kept
  live)". The free node taken from `[ecx+0x1C]` was 0, and the write went to `edx + ((0 - edi) >> 3) * 2` =
  A0B67FA0, the fault VA. The object had been half restored by loads.
* **b379eb** (`sub_000E5F40`, the small-block allocator, a normal update) [V for the read, I for the link]: the
  0x240 class's free-list head was a float (BC2740C0). Two pool pages (81126000, 81136000) were in the burst 56
  lines before the desync at step 1740. The opponent's log of the same match (9aafd3) has no attribution then,
  so the desync is more likely this PC's half-restored state than the crowd-weapon divergence [I].
* **Fix** 44b81c6: a tick is left out when the game thread saved or loaded during it, when the mixer ran on
  another CPU than the game thread last did (logged), or when it changed more than 8 pages. The mixer's own
  words change at every tick, so the next clean tick still takes them. The cycle count is read before the copy.
* **After a desync** (b379eb: out of sync at step 1740, still predicting, crashed at step 1772; 0b7e16: **one**
  rollback in the whole fight, back to step 2861, the desync at step 2880, the first check after it, then two
  crashes at step 3444, with mixer bursts of 11 pages / 1164 words before that load): the desync and the later
  crash come from the same half-restored memory, not from re-runs after the desync [I, strong for 0b7e16].
  a08559 (the same player) shows it twice. Match 28182DF6 went back to step 8753 and was out of sync at step
  8760. Rematch BC5FC078 went back to step 2041 and was out of sync at step 2100. In both cases the load just
  before is the only one in the stretch, and the bursts came before it. That rematch then crashed in a re-run
  at step 4801 (`sub_00117410`, ecx = BF800000, a float -1.0 used as a pointer). The rematches there did roll
  back (#383-#387). The digest's "lockstep d2" for a rematch only means it had no START line of its own.
  f32d773 does not cover this case, because it leaves only when the other PC stops sending. 6656f4e stops
  prediction at a desync. 9949788 makes a rollback match that went out of sync go on offline and leave for the
  room at once, through the same pause-menu Quit. Lockstep matches still play on.
* **9aafd3** (step 0 of a lockstep match, `sub_001E6EC0` reading FFFFFFF8) [V]: not rollback. `sub_000A22A0`
  calls `sub_000BD440` (guarded in 0bbf8d1, and its "[TEXT]" line fired) and then `sub_000BD400` on the same
  empty view. 4ccbb9f guards that call too. The "[ROLLBACK] at the crash" lines were the last rollback match's
  counts (B7). The crash line now names the match's mode. Two lockstep matches ran in between, one of them a
  full fight with 650 checksums equal, and the masks and saves are cleared at each `rollback_reset`. Nothing
  points to a leak. Its desync at step 120 (74A080F3, both fighters, rollback d5) is not explained; the only
  burst in that log comes after it.
* Checks:
  * `logs/dj/20261103-072354-online` (build before 9949788 and 9fdef35; rollback, 3 matches with two rematches,
    70+10 ms, KO at 2400): ok. 52-262 rollbacks per match and copy, 130 checksums equal, no mixer attribution, and
    neither new line fired. The guards cost a clean run nothing.
  * `-080323-online` and `-082624-online` (rebased build; weapons in the checksum, `RECOMP_NETPLAY_HASH_WEAPONS=1`,
    to force the crowd-weapon desync): both copies saw the desync at the same step (1440, 1980), left within 1-3
    steps, switched rollback off and were back in the room. That happened five times in all, with no crash. Each
    next room match at the same venue was out of sync at step 60, because the weapons are kept (that test switch,
    ninth round), so the runs looped until the timeout. With `--rematch` the route lost its way (no results
    menu after a leave): a test artefact.
  * `regress.py --quick` (`logs/regress-20261103-082623.txt`): m2, m3, m4a and the fight ok. The unit row failed
    only because the worktree's toolkit folder was empty (`test_hd_setup` reads `recomp_settings.h`); with the
    toolkit there, `pytest -m "not slow"` gives 1068 passed.
  * Not verified: the bursts themselves (never seen locally), the empty-text path of 4ccbb9f, and a real
    (non-forced) desync.

### Eleventh round (on `remaster` a080b38, build 52): rollback everywhere
The owner's decision: rollback is mandatory in every mode (1v1, Team, FFA, King of the Hill, crew battles, ranked),
with no player switch, behind a server switch, keeping the automatic per-match fallback and a server-side kill
switch (global and per mode).
* **Mechanism** (`docs/netplay-protocol.md`, "Rollback everywhere"): `CAPS` bit 9 both ways; the relay's answer to a
  client that announces it carries a `u32` mode mask (bits 1v1, Team, FFA, KOTH, crew, ranked) and is sent again
  before every `START`. The relay reads it from `<updates>/rollback-everywhere` (empty: all modes; else the names it
  lists); `rollback-allowed` stays the master switch. The game sorts its room into one class (`room_rb_class`: a ranked
  set, a crew battle, a hill fight, Team, 3/4-way, else 1v1); in a listed class it rolls back whatever the setting
  says, in an unlisted one it plays lockstep (the per-mode kill switch); with a 0 mask or an older relay the setting
  decides as before. The hill's old lockstep rule is lifted only when every fighter sends `P_RTT` byte 14 bit 1, so
  both PCs agree on it and on the delay. The game keeps the mask (`[online] rollback_modes`); the next start then
  sets up the audio arena whatever the setting says. A session that started before that plays lockstep with a
  reason and asks for one restart. The room shows a `ROLLBACK` tag instead of each player's choice (players whose
  game must restart still show), the settings page replaces the toggle by a line while the mask is kept.
* **Fallback** (one `[NETPLAY] rollback off for this match: <reason>` line per reason, a notice "This match runs
  without rollback: <reason>" to the players when their PC meant to roll back): a player who cannot (restart, older
  build, setting off where the server leaves it to the players), a created fighter with a pre-45 build, a path ping
  over 300 ms (`RB_PATH_MAX_MS`, from the `P_RTT` pings, so both PCs agree), and during a fight a PC that spends 50%
  or more of two 5 s windows in a row on rollback (`rb_budget_tick`, `RECOMP_ROLLBACK_BUDGET` for tests): it stops
  predicting and, once every step is final, turns rollback off until the next `START` (rematches included). Only
  that PC; the others keep rolling back on the agreed delay, so the match stays in sync. Unchanged: the
  desync-leave rule, the decided-step rule, prepare/reference failures. Note: the old "off for the rest" check
  (`N.final_step >= N.step` before the final-step loop) never fires mid-fight with prediction stopped (the newest
  step is made final later in the same update); the slow-PC fallback checks after the loop. The desync clause has
  the same blind spot, but the desync-leave rule covers it.
* **Runs** (`dj.py scenario online --rollback mandatory`: relay `-rollback -rollback-modes all`, copies
  `RECOMP_ROLLBACK=server`, i.e. the arena but no opt-in; `--bot 60`, `RECOMP_NETPLAY_LAG_MS`):
  | Mode | Run | Lag | Matches | Rollbacks per match and copy | Result |
  |---|---|---|---|---|---|
  | 1v1, rematch chain | `-174806`, `-195812`, `-175333` | 70+10 / 70+10 / 110+10 | 4 (no bot) + 5 + 4 | 35-375 (idle rematches 0) / 2-332 / 115-311, deepest 9 | ok, no desync |
  | Team 4p | `-175855`, `-195015` | 70+10 / 110+10 | 3 + 3 | 201-726 / 414-877 | ok, 130-140 checks a match |
  | FFA 3p | `-195401`, `-180252` | 70+10 / 110+10 | 3 + 3 | 225-632 / 399-603 | ok |
  | KOTH 3p | `-180650`, `-190145` | 70+10 / 110+10 | 6 + 14 fights | 0-469 | every fight a KO (relay how=1), no timeout; the king played 13 rollback fights in a row; peak 61.1 MB, every fight ends at 54.8 MB |
  | Crew battle | `-191725-crewbattle` | 70+10 | 2 fights | 34-323 | ok, class crew |
  | Ranked classic set | `-191958` | 70+10 | 3 games | 0-343 | ok, class ranked |
  | Created vs stock, rematches | `-192434` | 110+10 | 3 | 130-412 | ok |
  The October KOTH-with-rollback freeze did not come back. Two KOTH runs stopped on test artefacts, fixed in the
  harness: `--ko-winner alt` counts each copy's own matches (the two fighters disagreed on the winner: "desync" at the
  KO step), and the king's pad script ran out of results presses (`_koth_script`, more fights within the 4095-character
  script limit). The crowd-weapon desync did not show (no weapon checksum in these runs).
* **Fallback checks**: one copy started without rollback (`RECOMP_ROLLBACK@1=off`, `-192843`): lockstep, both reasons and
  notices; forced slow PC (`RECOMP_ROLLBACK_BUDGET@0=1`, `-193344`): fallback at step 766, off at 767, its rematch
  lockstep on that PC, the other kept rolling back, in sync; ping 330+10 ms (`-193652`): lockstep d10, "the ping is too
  high (351 ms)"; KOTH left out of the mask (`-193808`): lockstep, no notice. Regressions: `--rollback on` with a relay
  without the switch (`-194625`) and `--rollback off` (`-194821`) as before.

### Twelfth round (on `remaster` e3d4835; F9 8e97cd, 70d296 from build 51, 0b7e16 from build 47)
* **The two crashes are one** [V]. All three reports crash twice: first the mixer thread (sub_001242D0, created
  as guest thread #7, stack top 0x01404FF0) in `sub_001E55D0` (`recomp_0013.c:24730`, `[slot+0x10]+4`), reached by
  `sub_00126300` -> `sub_001E8340` -> `sub_001E8080` (the music stream's read, tail-calling `sub_001E55D0`), then the
  main thread writing at 84000000. The object in `ecx` (83A07660) is the file layer's slot table `[0x3C8F30]`, which no
  save or load touches ("heap pages left alone ... 83A06000 83A07000 83A08000"). Its words are floats in all three
  reports (43E3D6E8 ...), the request handle `edi` too (43D791A1, C2750F89), and in 8e97cd they equal the first words
  of the main thread's source `ecx` 816B1D00. EAGL's draw command and loaded objects above are junk too (FEB4B1B0,
  2ACECDB9, 32E05500, FEFEFBF8: packed vertex colours). The second crash's offsets, mapped with this MSVC on the main
  checkout's PDB (players compile the same lifted C with the same toolset), are `recomp_0008.c:27764` and `:27788`:
  `sub_00115CB0` writing a particle billboard vertex (position at `[ebx+0x4C8]`, colour, uv). So the main thread's
  particle writer ran from the 0x81B... heap up through the contiguous window to 84000000, and the mixer read the
  overwritten slot table on its next tick: the mixer's crash is the fallout, not the cause.
* **The particle data was kept live** [V for the pages, I for the link]. In all three dumps the emitter (`ebx`,
  `[ebx]` = 000DF190) and the particle (`esi`) lie on "saved (page with mixer words)"; in 70d296 `esi` 816ADD78 is
  itself "a mixer word (kept live)". Before the crash 70d296 attributes 8 whole pages to the mixer in one tick
  (816BA860..816D0040, 2128 words, 1024 on one page: under the 8-page limit of 44b81c6), 8e97cd a 246-page tick (left out)
  and then dozens of small ones in the crashing match. The cff81c crash of the tenth round was the same family (the particle scheduler's free list).
* **Why the mixer was blamed** [I]. A tick is attributed when the game thread's cycle count stood still. When the
  mixer blocks inside its tick (the file layer's slot lock in `sub_001E8080`, the engine lock), any other guest
  thread on the shared CPU runs and writes, and only the game thread's own running was checked. Measured: with the
  mixer sleeping 2 ms in every tick (`RECOMP_ROLLBACK_TEST_MIX_YIELD=2`, old rule, `logs/dj/20261103-210324-online`),
  2873 of 5893 and 877 of 5915 ticks passed the game-thread check although the mixer was off its CPU. Which thread
  wrote the particle pages on the players' PCs is not known (no such writer showed locally).
* **Fix** (506b39f, `rollback.c` only): a tick counts only when the mixer's own cycles (`QueryThreadCycleTime` on
  itself) cover the tick's TSC time, within an eighth plus 40000 cycles; a tick with more than 128 new words is left
  out too. Each fight ends with `[ROLLBACK] mixer ticks this fight: N watched, C clean (W words kept live) ...`.
  `RECOMP_ROLLBACK_MIX_OFFCPU=off` gives the old rule.
* **Checks**: venue 15, 70+10 ms, no knockout, a rematch (`-204513-online`): ok, 350 + 250 checksums equal, 2165 and 957
  rollbacks in match 1, 35258 ticks watched, 33886 clean, 61 / 17 left out as off the CPU (0.2 %), the mixer had its
  CPU for 100.1 % of the time (the two counters agree), **0 words** kept live; no desync, no crash. Venue 8, created
  fighters, 170+20 ms, three matches with two rematches (`-205857-online`): ok, 40/40/50 checksums equal, 183-478
  rollbacks a match, 0 words kept live. So locally the mixer writes nothing in the game's heap in a clean tick; the
  mask only ever held what other threads wrote.
* Not seen here: the bursts themselves and the particle overrun (no local run has attributed a word since 10-24).
  Related, not fixed: F9 b1dde3 (build 51, 172 ms) crashed in a re-run in `sub_00109F00` from the crowd update,
  reading a block allocated in a re-run and freed since (0D0D0D0D); its word +4 (81028DE4) had been attributed to
  the mixer 2300 log lines earlier, so this fix may cover it [I]. The same player-PC pattern in two build-51 re-run
  crashes of one player (F9 0d0d85: `sub_001156E0+0x2D0`, the particle scheduler of cff81c, step 573 of a rematch
  after a desync at step 2820, 3286 heap blocks allocated in re-runs; 99b1d9: `sub_00117410` with ecx BF800000, as
  in a08559): both logs hit the 64-line cap of "the mixer writes" lines in the first match (99b1d9 also two
  27-30 page bursts), and the crash registers lie on "page with mixer words", in 99b1d9 `[edi+0x10]` is "a mixer
  word (kept live)". So 44b81c6 narrowed the leak but did not close it [V]; that this fix closes it is [I].

### Thirteenth round (on `remaster` 8401feb, build 54 reports 2df2cd, e7d57d, ba0793): build 55
* **Reading the reports.** "[ROLLBACK] off for the rest of this match (step N, M rollbacks)" without a reason is the
  normal end of every rollback fight, and M is the session's running total: no fallback fired in any of the three
  (no "busy", "fallback" or "out of sync" line). Per match Miguel's PCs ran 0.4-1.1 rollbacks and 1.3-1.8 re-run
  steps per step (ba0793: 8028 in 6102, 8961 in 5299, 10297 in 5834; e7d57d 11501 in 7106), not 2-3 rollbacks.
* **The crashes sit on mixer-kept words** [V for the addresses, I for the cause]. e7d57d: `sub_00117410+0x127A`
  in a re-run, edi = 8172D208, the first of the 7 words "the mixer writes 8172D000 (bytes 8172D208..)" kept live
  that fight, ecx = BF800000 (as a08559, 99b1d9). 2df2cd (54 ms, first match, 1150 rollbacks): the mixer thread in
  `sub_0011E530`, then `sub_00115CB0` writing to 84000000; EAGL's draw command and loaded object on "saved (page with
  mixer words)" are full of the billboard colour 65DCDCC2 (the "NOT A HEAP ADDRESS" line is that fill, i.e. the
  overrun's fallout). ba0793: the mixer in `sub_001EE3C0` on the file layer's slots `[0x3C8F30]`, then the same
  `sub_00115CB0` overrun; 67 attribution lines. Fix e6d7a0e: loads restore those words (`RECOMP_ROLLBACK_MIXMASK=keep`
  for the old rule); every local run since 506b39f attributes 0 words, so a clean run is unchanged.
* **Arena: kept blocks and a leak.** (1) bba6d15: kept blocks were scanned only at the next fight's start, after
  the next venue had loaded around them (e7d57d: 817 kept as the next venue loaded, "160 KB free in 516 blocks, the
  largest 21 KB"); now also at the fight's end and once more before the arena grows outside a fight, and the scan
  skips the arena's free blocks (stale handles there kept blocks "named" for ever). (2) 544ce75: a block the game
  thread's audio code got after a save leaked at every load (the restored state never had it, the re-run asked
  again); `forget_frees_since` now hands those to the same scan (`freed = 2`).
  | Run (8 matches, rematch chain, no KO, 100+10 ms, bot 60) | fight blocks still live per fight end | arena |
  |---|---|---|
  | `logs/dj/20261104-095902-online` (bba6d15 + e6d7a0e) | copy 0: 187, 547, 550, 735, 766, ... 3281 (606 KB); copy 1: 1234, 1625, 2996, ... 7253 (1340 KB) | copy 1 grew to 6984 KB in 13 pieces, 876 KB free in 1164 blocks |
  | `logs/dj/20261104-104755-online` (+ 544ce75) | 89, 78, 94, 117, 118, 120, 117, 129 / 89, 71, 94, 101, 115, 117, 115, 127 (13-22 KB) | 4952 KB, one 1 MB growth at fight 4-5, then flat at 5976 KB in 12 pieces; kept blocks still named plateau at 290 |
  Both runs: 8 of 8 matches, no crash, no desync, 160-750 checks equal a match, 600-2900 rollbacks a match and copy.
* **Rematch rule** (88ebadd): a PC that fell back sends a flag with its rematch choice (P_REMATCH byte 9); once all
  choices are in, every PC turns rollback off for the rest of the START's chain if any flag is set and every
  choice carried the byte (older builds: the old local rule). The delay stays the START's.
* **Heavy rollback** (eaf6712): two 5 s windows in a row at 2.0 or more re-run steps per normal update take the
  slow-PC fallback and set the flag. The local 100+10 chains logged single windows of 1.54-1.72, never two in a
  row. Forced with `RECOMP_ROLLBACK_HEAVY@0=0.05` (`-113913-online`): copy 0 fell back at step 606 of match 1,
  both copies played both rematches in lockstep (delay 5), in sync.
* **Other checks** (all fixes, `--rollback mandatory`, 70+10 ms, bot 60, 2 matches each): Team 4p `-115224-online`,
  FFA 3p `-114842-online`, KOTH 3p `-115025-online`: ok, 40-214 rollbacks a match and copy, no crash, no desync. An
  8-match KO-at-600 chain at 100+10 (`-095016-online`) ok. `regress.py --quick`: 5 of 5 (`logs/regress-20261104-120018.txt`).
* **Not verified**: the mixer-word crash itself (never reproduced locally; that restoring the words prevents it is
  [I]); a real (not forced) heavy fallback; a mixed 54/55 room (the 9-byte path); chains on players' PCs, where the
  zombie-scan hitch at a fight's end (15-50 ms locally, 141 ms in e7d57d) also lands on the results screen.

### Next (after the third round)
1. More two-copy long runs (the desyncs at steps 10 200 and 37 440 came once in two runs each; three later runs were clean),
   always with `RECOMP_NETPLAY_FINALLOG=1` so a desync can be traced to its first step.
2. Step 6 (King of the Hill), step 7 (performance), then step 8 (beta re-open on a test relay).

### Next (as of the first round)
1. B1 / B5 with the wrong-prediction soak: name the block behind the loader list node and the mixer's voice (the
   `ALLOCLOG` lines next to the crash), fix, then the soak must run 10 minutes clean.
2. The input-history difference after a wrong pass (steps 1717-1723 above): which state outside the saves carries a button
   into the next pass.
3. The two-copy desync at step 37 440 of a 11.6-minute fight: compare with a lockstep run of the same length (`--rollback off
   --fast-ko 0 --bot 60`, venue 9) before blaming rollback.
4. Step 5: long fights. Measure `[MEM]` over several 5-10 minute fights (`--fast-ko 0 --bot 60`); candidates are the blocks a
   load keeps (thousands per long fight) and arena pieces that could be given back between fights.
5. Then the target runs (step 4 bar), King of the Hill (step 6).

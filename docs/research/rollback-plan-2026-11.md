# Rollback netcode: where it stands and a plan to finish it (2026-11-01)

A read-only study of `remaster` at e105db6. Nothing was built or run. Sources: `src/hooks/rollback.c`, `src/hooks/netplay.c`,
`netplay/relay/cmd/relay/main.go`, `docs/research/rollback-*.md`, `rollback-sound-crashes.md`, `music-director-crash.md`,
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
| B4 | **Desyncs under rollback** | Fixed: mixed rollback/lockstep rooms (all-on rule, e42fa46). The Terror Dome shared-generator drift is fixed by the per-step re-seed (f82aa19; run rb1: 680 equal checksums, 5839 and 7554 rollbacks, `generator-timescale-desync.md` §9). Created fighters crashed at step 4 under rollback, so they are excluded. [V]<br>**Open:** the lockstep position desync (`lockstep-desync-positions` memory: bot vs bot, venue 9, step 6120, a few ulps; a real pad vs bot, venue 6, step 60, large). It is the same simulation, so rollback inherits it. | Waits for the position-desync fix (§4). |
| B5 | **Crashes from inconsistent restored state** (not sound) | Build 26: `sub_000EDF90`, a model node tree walk into a reused child array, at step 4039 (`generator-timescale-desync.md` §7). Build 28, 1v1 with rollback on for every player, after all the 10-25 fixes:<br>• 9fd9bd: `sub_000E61E0+0x67`, **during a re-run**, step 145, 47 rollbacks; `edi` 0x80129778 is inside the system pool 80120D80+256 KB<br>• 22ecc8: `sub_000EE000+0x215`, **during a re-run**, step 813, 4441 rollbacks<br>• 7b829d: native `RIP` in `sub_001EE3C0+0x3E9`, a normal update, step 806, in the third rollback match.<br>A rollback stock match at venue 26 crashed once on 10-28 (`RIP 0x1408203D2`). [V] | **Not analysed.** Best guesses: [G] the small-block pool (`0x3A4000..0x3A7000` together with its heap block) and model nodes allocated or freed inside a re-run. The `sub_000E61E0` and `sub_000EE000` names come from the lifted headers only. |
| B6 | **Performance** | Saves of 4.0-4.3 MB every update, about 250 MB/s of `memcpy` at 60 Hz. Rollbacks average 1.0 ms in local tests and 5.3-6.4 ms in a player's log; the longest was 20.9 ms and the deepest 9 steps. Rollbacks over 30 ms are logged with their model loads (`ROLLBACK slow`). Step 1 holds the game for 150 ms and copies the 42 MB reference. [V] | Not measured on a weak PC. Not a known crash cause. |
| B7 | **Gaps in observability** | `rb_count` and `rb_steps` are never reset per match (B3 needed arithmetic to see this). `rollback_reference` fails silently when `!s_ref || !s_ndseg`. No hang dump comes from players. [V] | Easy to fix. |
| B8 | **Code drift since the last rollback test** | The last rollback runs were 10-26 to 10-28. Since then: upstream v0.12.x merges with re-lifts, KOTH changes, versus hold, created fighters, build 36/37 changes. `rollback.c` hard-codes guest addresses and return addresses (`0x29696`, `0x1E3F20`, `0x1284B`, ...). The guest image is the same, so they should hold [I], but the hooks (`recomp_manual.c` `sub_00029500`, `sub_001FBE1E`) have not been run since. | Needs a fresh baseline (step 0). |

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

# Rollback netcode: how a fight step is built, what it touches, and what that means for save / restore / re-simulate (static read)

Read-only study of the lifted C in `src/recomp/gen`, the XBE image (vtables, function-pointer words, floats),
`src/recomp_manual.c`, `src/hooks/netplay.c` and the toolkit sources it cites. Nothing was built or run.
Addresses are guest addresses. Lifted-C references are `file:line` (`recomp_000N.c` under `src/recomp/gen`).
Marks: **[V]** verified in code or in the XBE image, **[I]** inferred from shapes or names, **[U]** unknown / not settled.

Method note. The call-graph claims ("reachable from the step") come from a **direct-call walk** of the lifted C
(`RECOMP_ABI_CALL`, tail jumps, fall-throughs) started at `sub_00087700` plus the 111 fighter state handlers that
`sub_001B54D0` writes on its stack (`recomp_0012.c:55249..55430`, called at `:55450`). That set has **1112
functions**. It does **not** follow vtable calls (about 64 indirect sites in that set: object methods), the
allocator's function pointers (`0x2F756C/0x2F7570`), or the kernel imports (39 sites). A "not reachable" below
means "not reachable by direct calls", never more. Rule 1 of CLAUDE.md (a miss is not evidence) applies.
The strings used to name subsystems are the allocation tags the game passes to its own `new`.

## 0. Answers in one page

1. **Who calls `sub_00087700`:** only `sub_0008BE60` (the arena / CLogic frame function, object `0x349760`), in three
   switch arms of `CLogic+0x2D820`: states 4 (after `sub_00089650`, falls into 5), 5 and 8 [V]
   (`recomp_0004.c:18926-18935`, `:18975-18982`). `sub_0008BE60` itself is called once per **step** by the screen's
   update method (`sub_00079B60` at `recomp_0003.c:77856`, `sub_0007A8F0` at `recomp_0003.c:80124`, and `sub_0002DFF0`
   at `recomp_0001.c:24363`).
2. **Steps per presented frame:** there is **no time accumulator**. The main loop runs one *complete game update*
   per "frame record" taken from a ring of 10 that a producer thread fills once per tick of a 60 Hz NV2A PTIMER
   alarm chain. 0 to 4 updates per outer loop iteration (cap), one render per iteration [V]. `dt` is the constant 1/60.
3. **A step is bigger than `sub_00087700`.** Per record the main thread runs: Input update, then the screen update
   (pad table fill, `sub_0008BE60` = AI-pad merge + state machine + `sub_00087700` + **crowd update**, then HUD, particle
   subsystem, sparkle / texture-anim / screen FX, task subsystem, fade), then the **Event Manager** (`sub_000BD860`, edge
   detects the new state and posts bus events: audio, rumble, HUD) and its snapshot copy. Rendering is a separate
   call at the loop tail [V].
4. **Side effects that leave guest RAM or are not idempotent** all hang off a **synchronous message bus** (Post
   `sub_000BD720`, handlers run on the posting thread) and one sound-command function pointer (`[0x3ADB7C]`).
   There is a cheap global off switch for the bus (`MEM32(0x39E844) = 0`) and for sound (`MEM32(0x3ADB7C) = stub`) [V mechanism].
5. **A step does allocate and free** game-heap blocks (particle emitters, animation mirrors, weapon slots, sound
   commands), and can load files and release D3D resources (weapon slot teardown). So a RAM-range snapshot is not enough.
6. **`sub_00087700` alone cannot be the re-simulated unit**: the AI-to-pad merge (`sub_000C89D0`), the crowd update, the
   state machine arms around it, and `CLogic+8` bookkeeping are in `sub_0008BE60`. The smallest self-consistent unit is
   `sub_0008BE60`.
7. **Threads:** the sim runs on the main thread only. Guest threads during a fight: frame-record producer
   (`sub_000D7700`), audio mixer (`sub_001242D0`, 100 Hz, TIME_CRITICAL), save/storage service (`sub_0019AB60`),
   async-file workers (`sub_001E4970`), plus host threads (NV2A timer / interrupt thread that runs the 60 Hz DPC, GPU
   executor, APU, USB, netplay). None of them writes fighter / CLogic state [V for the guest ones by reachability, I
   for the host ones]. The one real race surface is the **sound engine heap + engine mutex** shared by the step
   (`sub_000388B0`) and the mixer thread, and the **game heap** shared by everyone.

---

## 1. The frame / step structure

### 1.1 Call tree (main thread)

```
sub_001F62E3 (CRT start)  -> main  sub_00011500 -> sub_00011CD0   "main loop"          recomp_0000.c:4986
 |  setup (once): sub_000D7560(frameObj = registry[0x27], 60)  -> frameObj vtable+0x10 = sub_000D7DA0(60): arms the 60 Hz tick
 |  registry 0x2FB148 slot ids used here: 4 Render Subsystem, 0x17 RunModuleMan, 0x19 Input, 0x1E Event Manager, 0x27 frame obj
 |
 +- loop (loc_00011D40, recomp_0000.c:5056):
 |    sub_001ECE20(0)            host-timer callback list (0x3C9508..), elapsed-time based        :5058
 |    while ( sub_000D75E0(frameObj) )  {                  "is a step owed?"  max 4 per iteration  :5065 / :5110
 |        sub_0002C530(0x33DB90)  -> returns const dt = 1/60 (0x28C798), stores measured elapsed time (unused by sim)   :5075
 |        sub_000DD930(Input, dt)                          Input subsystem: edge/hold from the record just consumed     :5082
 |        sub_00077040(machine = registry[0], dt)          screen manager (4-state machine at +0x24)                     :5087
 |            lock 0x2FB000 (sub_001ECF00)                                                   recomp_0003.c:69933
 |              current screen vtable+0xC  = update  (RunModuleGame: sub_00079B60, vtable 0x29D5E4)       :69945
 |            unlock (sub_001ECF10)
 |        if (screen manager says continue) {
 |            sub_000BD860(EventManager)   edge-detect state -> Post()   (audio / rumble / HUD / popups)   :5097
 |            sub_000BDF40(EventManager)   copy 0x3BDE80 (0x27C B), 0x340D58 (0x550 B), 0x3B8598 (36 B) into its "previous" buffers   :5101
 |            sub_0001A240(ecx = registry[4] [I]) -> sub_0001A710 (render-side per-step tick)             :5106
 |        } else exit-flag
 |    }
 |    ... time bookkeeping (sub_001E9550, sub_0001A880, max frame time 0x2FB030)                          :5125-5153
 |    if (!exit) {  sub_000D02A0 / sub_000D0C20   frame-time statistics only                              :5163-5167
 |                  sub_00077740(machine)  = RENDER:  lock 0x2FB000, screen vtable+0x10 (sub_000796A0), sub_00077170 (fps)   recomp_0003.c:70960
 |    } -> loop
```

Screen update, `sub_00079B60` (`recomp_0003.c:77646`), in order, per step [V]:

```
 sub_0003D960(dt)                 global timers / counter 0x340538 (+1 per call)               :77824
 sub_00079280                     fill pad table 0x3B8F48 from the Input subsystem (4 pads)    :77851
 sub_0008BE60(0x349760, dt)       CLogic frame function (below)                                 :77856
 sub_0006AF30(FrontEnd, dt)       (only when the screen is in its running state)                :77892
 if running (screen+0x24 == 0):
   sub_000B5710(GAME_HUD)         HUD update                                                    :77914
   sub_000B9F40(Particle subsys)  -> sub_000B9E80                                               :77918
   sub_000C52B0(FXSparkleMan)     sub_000C59D0(FXTexAnim)                                       :77923-77928
   sub_0001B100(Task subsystem)                                                                 :77933
   sub_000CA750(FXScreen)                                                                       :77938
   sub_000D8D90 -> sub_000D90F0   fade / screen-overlay state machine at 0x3A0188 (draws 0x39C0E0 RNG)   :77942-77946
```

(Registry slot to member mapping is read from `sub_000793A0`, `recomp_0003.c:76443-76514`, whose stores lag one call behind the
pushes; names are the allocation tags listed in `sub_00011E40`, `recomp_0000.c:5264..7214`: 1 Task, 3 Audio, 4 Render, 5 Sprite,
6 Scene, 7 Font, 8 Animation Event System, 0xA CParticleSubsystem, 0xB Light Man, 0xD FrontEnd, 0x12 Entity Manager, 0x15 Cinema,
0x17 RunModuleMan, 0x18 BackEnd, 0x19 Input, 0x1D Event Message System, 0x1E Event Manager, 0x21 FXSparkleMan, 0x26 FXScreen,
0x27 frame object, 0x28 Popup.)

`sub_0008BE60` (`recomp_0004.c:18724`), in order, per step [V]:

```
 pre:  per pad slot: zero the pad entry of CPU slots with no device (sub_000DC4C0, :18793-18806)
       sub_000C89D0(ecx = CLogic+0x2DC44, arg = [0x3BDE84]) -> sub_001AD380: AI output words copied into the pad table   :18827
       CLogic+8 = CLogic+4 (previous time scale)                                                               :18831
       sub_000935B0 (-> sub_000A4860), sub_00089710 (-> A3E80, 87E20, 87AF0)                                    :18837-18841
 switch (CLogic+0x2D820):  0 88630 | 1 8B120 | 2 888C0 | 3 898E0 | 4 89650 -> 5 | 5 87700 + 8AB00 (+counter 0x2D86C, 88FF0 at 60) |
                           6 89B30 | 7 89BC0 | 8 87700 + 89DB0 | 9 89E00 | 10 89F20 | 11 8A300 | 12 8A5B0 | 13 190B0 | 14 -  | 15 86F40/89310 + 8BB00
 post: crowd manager update: vtable+8 of CLogic+0x2B9F0 (indirect), state 7 uses sub_0007E740 instead         :19064-19078
       zero 0x340DBC, 0x340E84, 0x340F4C, 0x341014 (per-fighter event words)                                   :19091-19094
       if 0x3B8598 flag: sub_00033380(1 / 2 / 3 / 0x3B859C+10)   count-in sound cues                           :19104-19176
```

`sub_00087700` (`recomp_0004.c:7731`) [V]: `MEM8(CLogic+0x2FFB0)=0`; `sub_001A2F50(0x3BDE80)` (stores that pointer at `0x3B85D0`); `sub_001A4900(CLogic+4)`
(match clock, time scale `0x3B8F2C`, `0x3B9054` flags, slow-motion ramp, RNG-using helpers); `sub_001A4A30` (the per-fighter pass: pad reader
`sub_001BAA00`, AI entry `sub_001B0290`, handler table via `sub_001B54D0`); `sub_001A93B0`; `sub_001A8E00`; `sub_000B03D0` (posts `0xF100000B`
per fighter); `sub_001A2F60(0x3BDE80)` and `sub_001A3D50(0x340D58)` (fill the **public snapshot buffers**); `CLogic+8 = [0x3BDE80]`;
`sub_000AC590(CLogic+0x2DB44, dt)` (**camera director**, see 2.7); then `CLogic+0x2D820 = 0xE / 0x10` if `CLogic+0x2B9EC` has bit 0x400 / 0x800.

### 1.2 How many steps per frame, and from which clock

* **The record ring** [V]. Frame object (registry 0x27; base-class vtable `0x2B3AC4` stored by the constructor `recomp_0006.c:32172`, final vtable `0x2B3AE0` read from the XBE: `+4 sub_000D77D0` start, `+0xC sub_000D7BF0` read pad, `+0x10 sub_000D7DA0` set rate).
  Ring of 10 records of 0x260 bytes: head `+0x17C4`, tail `+0x17C8`, count `+0x17CC`, lock `+0x1A34`.
  `sub_000D77D0` (`recomp_0006.c:32379`) creates the **producer thread** (`PUSH32(esp, 0xD77C0)` at `:32435`, stack 0x2800, abstract priority 2, wrapper `sub_001ED4B0` -> `sub_001F68D2` CreateThread).
* **Producer** `sub_000D7700` (`:32215`): waits on the event `frameObj+0x1A60` (`sub_001EDD70`), then `sub_000D7AE0` (`:32992`) reads the four pads with a virtual call
  (`vtable+0xC`, once per pad slot, `:33055`) into a 0x260-byte record; **if the ring already holds 10 (`cmp eax, 0xA`) the oldest is dropped** (`:33080`), then the record is pushed.
* **Clock** [V code, I rate]. The event is signalled by `sub_000D7EA0` (`:33595`), which re-arms itself with `sub_000D7DE0` (`:33475`, drift-free: next time = now + period - lateness, through
  `sub_00216410`) and `SetEvent`s. `sub_00216410` is Direct3D's `SetTimerCallback` body and is **hand-written in `src/recomp_manual.c:348-401`** (a time already past is armed at once). The period comes from
  `sub_000D7DA0(60)` (`:33439`: a 64-bit divide, helper `sub_00201470`, of the counter frequency in `frameObj+0x1A98` by 60) [V that it divides, I that the frequency is the PTIMER's]. The alarm is the **NV2A PTIMER alarm**, raised as a DPC on the toolkit's timer / interrupt thread
  (`tools/xboxrecomp/src/kernel/kernel_bridge.c:2404-2436`: PCRTC, PTIMER and PGRAPH interrupts "all run on the timer thread"); its time base is the toolkit's `xbox_ReadTimeStampCounter` (host QPC rescaled to 733 MHz).
  So the sim clock is **wall time through the PTIMER, quantised into 60 Hz records**, not vblank, not the swap.
* **Consumption** `sub_000D75E0` (`:32030`) [V]: count 0 -> false. Otherwise it takes the head record under the lock (`sub_000D7960` copies it into the frame object, which `sub_000DD930` then reads), `+0x1A54 == 0`
  (the only write to `+0x1A54` is the constructor's zero, `:32184`) -> `counter(+0x1A5C) += 1; return counter < 5` (`cmp eax, 5` at `:32135`). So per outer iteration **4 calls return true and the 5th consumes a record and returns false**
  (that record is thrown away) and resets the counter; an empty ring resets it too.
* **dt** [V]: `sub_0002C530` returns the float at `0x28C798` = 0.016666668 and stores `0x3C888889` (1/60) at `+4` (`recomp_0001.c:20040-20042`); `sub_00087700` ignores its own argument except to hand it to the camera director
  (`AC590` multiplies it by 60.0, `0x296DDC`), so the fight is independent of host frame time. `netplay_on_step` already overwrites `g_edi` (the crowd's dt) with 1/60 when it looks like a measured value.
* **What throttles the loop** [I]: nothing waits in `sub_00011CD0` itself. With an empty ring the iteration only renders again (an extra present of an unchanged state); the pace comes from the D3D swap throttle plus the executor's 60 Hz flip timer
  (PROGRESS D41). One render per iteration is certain [V]; several iterations per record time are possible [I].
* **Existing lockstep** blocks inside `netplay_on_step` (entry hook of `sub_00087700`, `src/recomp_manual.c:61`; `src/hooks/netplay.c:1810`, wait loop `:1885-1924`). While it waits, the producer keeps queueing; beyond 10 the oldest are dropped, so owed steps are lost, not caught up.

### 1.3 What a "step" is, at four levels (choose one for rollback)

| Level | Entry | Contains | Does not contain |
|---|---|---|---|
| record | `sub_000D75E0` true | the pad poll result | everything else |
| game update | body of the loop at `recomp_0000.c:5073-5106` | Input, screen update, Event Manager, render tick | render (loop tail) |
| screen update | `sub_00079B60` (`:77646`) | pad table fill, **8BE60**, HUD / particles / FX / tasks / fade | Event Manager, Input |
| **CLogic frame** | `sub_0008BE60` (`recomp_0004.c:18724`) | AI-to-pad merge, state machine, `87700`, crowd update, count-in cues | pad table fill, presentation systems |
| fight step | `sub_00087700` | fighters, match clock, camera director | AI merge, crowd update, `8AB00` / `89DB0` state tests |

Which screen class hosts an online match is **[U]**. Screen 2 of `sub_000773A0` (`recomp_0003.c:70431`; vtable `0x29D5E4`: `+4 sub_000793A0` init, `+8 sub_000798A0` second init, `+0xC sub_00079B60` update, `+0x10 sub_000796A0` draw)
allocates the tags "RunModuleGame.log", "PauseMenu", "FXTexAnim", "GAME_HUD", so it is the match screen [I]. Screen 3 (vtable `0x29D604`, update `sub_0007A8F0`, init reads "FightingStyle / AttributeBonus") also calls `sub_0008BE60`
and is a different mode [I]; a third caller, `sub_00077A40 -> sub_0002DFF0`, has no vtable word I could find [U]. All three pass through `sub_0008BE60`.

### 1.4 Loading and the loader thread (so hooks do not meet it in a fight)

Screen init (`sub_000793A0` -> `sub_000790D0` -> `sub_0008BB00` match setup -> `sub_0008B730` / `sub_000891A0`) does not run on the main thread: `sub_00077230` (`recomp_0003.c:70135`) starts a **loader thread**
with start routine `sub_000771E0` (`:70206`, `:70068`; waits on an event, calls the new screen's vtable+4, exits through `sub_001ED410`) while the main thread polls with `Sleep(0)` in machine states 1 and 2 (`sub_00077040`, `:69875-69880`).
Rematches re-enter `sub_0008BB00` from `sub_0008BE60` state 15 on the main thread (`recomp_0004.c:19032-19044`).

---

## 2. Side effects inside a step

Everything here is reachable from `sub_00087700` (+ handlers) unless a row says it belongs to `sub_0008BE60`, the screen update or the Event Manager.
"Repeat" = what happens if the step is simulated again.

### 2.1 The message bus (the main fan-out) [V]

* Table: `MEM32(0x39E844)` = entry count, `MEM32(0x39E84C)` = entry array, `MEM32(0x39E850)` = entry stride, `MEM32(0x39E854)` = handler slots per entry. Entry: `+0` valid byte, `+4` message id, `+8` handler count, `+0xC...` handler pointers.
  `sub_000BD720(id, payload*)` (**Post**, `recomp_0005.c:58184`) walks the entries, finds `id`, and **calls every handler synchronously** (`ICALL`, args `(id, payload)`, `:58275`). `sub_000BD610` subscribes (`:57912`), `sub_000BD6A0` unsubscribes (`:58066`).
* **Off switch** [V mechanism, I that it is safe]: with `MEM32(0x39E844) <= 0` the first compare jumps to the exit (`:58207`), so Post returns 0 at once. It also stops subscriptions (`BD610` has the same guard), so only flip it for the duration of a re-simulation.
* Posters inside the step: `sub_000B0060 .. sub_000B02B0` (`recomp_0005.c:22646-23050`) post `0xF1000000 .. 0xF100000A` (fighter events) and are called from the handlers (`001D87B0`, `001D3100 / 001D1990`, `001D11F0` x2, `001A5190 / 001A5910 / 001A5DD0 / 001A8AB0` chain,
  `001AAFD0`, `001D9250`, `001C6130`, `001AB2A0`, `001AB410`); `sub_000B03D0` (`:23199`) posts `0xF100000B` per fighter from `87700`; camera: `sub_000ABB70` -> `0xF000002A`, `sub_000AC1F0` -> `0xF000002B` (`:10958`, `:11706`); `sub_00089650` -> `0xF000002D` (`recomp_0004.c:12995`).
  The Event Manager (outside `87700`) posts `0xF0000000/01/08/0E/0F/10/13/14/15/21/23/24/25/28/29/2E/33/34/35/36` from `sub_000BD860` (`:58454`, ids at `:58531..59439`).
* Subscribers (from the `sub_000BD610` call sites; handler addresses in parentheses):

| Module (owner function) | Ids | Effect |
|---|---|---|
| **Audio Subsystem** (`sub_0003C6E0`, strings `AudioManagedRAM`, `AUDIO/SFX0..3.VIV`, `audio/animtags.dat`) | `F0000000/01/08/0A-0F/21/24/25/27-2C/2E`, `F1000000-08/0A/0B/0C` | handlers `34A60, 38440, 35250, 35670, 35890, 35970, 35A20, 35AE0, 38620, 38760` reach the cue function `sub_00032050` / `sub_00031F90` / `sub_00031FF0` -> `ICALL [0x3ADB7C]` (2.2) |
| GAME_HUD (`sub_000B5140`) | `F1000000/01/04, F0000013-15, F000000C/0E, F0000010, F000002C/2D` | HUD state (object of 0x620 bytes, `sub_000798A0` allocates it); `B4E40` / `B4E90` also play cues |
| Controller manager (`sub_000DC5B0`) | `F0000000/01/10/26` | `DC350 / DC390 / DC3D0 / DC2E0` -> vibration (`DD440 / DD400 / DD660`), string `VibLvl%d` |
| Fade / FXScreen state machine (`sub_000D9010`, handler `D8DD0`) | `F000000C/13/2A/2B, F1000000/01` | writes the overlay object at `0x3A0188` |
| Tutorial HUD (`sub_000B76A0`) | `F0000011/13/34/35/36` | |
| Popups (`sub_000D4980`) | `F0000031/32/0E` | |
| `sub_00091FC0` | `F100000D` | |
| `sub_0005B770`, `sub_0005E9D0` | `F0000037/38` | shop / loader screens |
| `sub_000793A0` | `F0000030` | |

* **Repeat:** the HUD, the audio subsystem's counters, the fade machine and the controller vibration are all *stateful and edge-fired*; a second Post for the same hit double-counts. Handlers run in whatever thread posts (main).
* The Event Manager keeps its own "previous" copies of the three public buffers (`0x3BDE80` 0x27C B, `0x340D58` 0x550 B, `0x3B8598` 36 B; `sub_000BDF40`, `recomp_0005.c:59649-59697`, destination pointers at `EventMgr+8/+0xC/+0x10`) and `sub_000BD860` posts when the live buffer differs
  from them [V shapes, I edge semantics]. If simulation state is restored but these are not, every event of the rolled-back range is posted again, or lost.

### 2.2 Sound / voice triggers [V]

* **No DirectSound (0x236F60-0x256A00) function is reachable** from the step; the game does not call DirectSound from fight logic. Sound goes through the game's own audio engine:
  `ICALL [MEM32(0x3ADB7C)]` where `MEM32(0x3ADB7C) = 0x137010` (`recomp_0009.c:33913`) = `sub_00137010` (`:36496`), a cdecl "count, args..." command call: allocates a command on the sound heap (`sub_001361E0`, an indirect allocator), runs
  `sub_00136ED0` (dispatch, bumps the 16-bit sequence counter at **`0x3AD950`**, builds a voice handle), frees it. 116 functions use the pointer (cue wrappers `0x31EC0 .. 0x32500`, `sub_00033380`, `sub_00032050`, ...).
  The call returns a handle; whether any step state stores it is **[U]**.
* Three trigger routes seen from the step: (a) bus handlers of the Audio Subsystem (2.1); (b) `sub_00033380(n)` called from `sub_0008BE60` when the word at `0x3B8598` is set (`recomp_0004.c:19104-19176`; `ICALL [0x3ADB7C]` with ids `0xC078 / 0xC079`, `recomp_0001.c:40485`);
  (c) `sub_001A94B0 -> sub_0008F990 -> sub_000388B0(float timeScale)` (`recomp_0004.c:28716-28730`, `recomp_0001.c:54753`): when the fight's time scale changes (cached in `0x340344`, handle in `0x3407DC`) it allocates a 12-byte effect record with the game allocator,
  takes the **engine mutex** (`sub_00139A20` -> `WaitForSingleObject([0x3AE10C])`, the same mutex the mixer thread holds each tick) and calls the engine's virtual `+4`. Animation tags (`audio/animtags.dat`) are loaded by the Audio Subsystem; how they fire is **[U]**.
* The mixer is a separate guest thread (4.1); a step never touches the APU or DSP directly. The APU / DSP mailbox is in `src/recomp_manual.c` and in the toolkit.
* **Repeat:** every re-run restarts the cue (and advances `0x3AD950` and the sound-heap state). **Redirect:** write a no-op stub (cdecl, returns 0) into `0x3ADB7C` for the duration of a re-simulation, plus keep the bus off (2.1). Stub also `sub_000388B0` or its time-scale argument.

### 2.3 Rumble [V path, I wiring]

No rumble function is directly reachable. It is triggered from the bus (`DC350 / DC390 / DC3D0 / DC2E0`, 2.1) and reaches the pad through the guest USB gamepad output
(`tools/xboxrecomp/src/usb/usb_gamepad.c`, `src/input/input_host.c`; `RECOMP_RUMBLE_LOG=1` logs it, CLAUDE.md section 4b). The bus switch covers it.

### 2.4 Particles, effects, decals [V partly]

* Particles are spawned in the step: fighter handlers `sub_001B6560 / sub_001B6670` -> `sub_0008F170` (strings `fx_bomb1..3`) -> `sub_00091860` -> `sub_00091630` (`fx_%s.%s`; looks up registry 0xA) -> `sub_000B8F90` ("ParticleEmitter") -> `sub_000B8940`
  ("PARTICLESYSTEM::PARTICLEEMITTER"), which allocates through `sub_00029680` (`recomp_0004.c:27411, 34219`; `recomp_0005.c:44145, 44910`). `sub_00091900` -> `sub_000B81B0` is the second spawn path (called from the weapon code `000BB600 / BBCC0 / BCC50`).
  The emitters are then aged and removed by the particle subsystem's update, `sub_000B9F40 -> sub_000B9E80`, in the **screen update**, not in `87700`.
* Sparkle / texture anim / screen FX / fade are screen-update objects (`C52B0`, `C59D0`, `CA750`, `D90F0`); the fade draws the shared RNG `0x39C0E0` once per update (`sub_000AA340(0x39C0E0, 0x64)`, `recomp_0006.c:36511`).
* Decals: nothing found [U].
* **Repeat:** each re-run of a hit spawns another emitter (heap allocation, list insertion). **Redirect:** the particle subsystem object (registry 0xA, `sub_000B9E80` state, emitter list) would have to be snapshotted with its heap objects, or particle spawning suppressed (`sub_00091630` is reached only through `sub_00091860`) during re-simulation.

### 2.5 Camera [V]

`sub_000AC590` (`recomp_0005.c:11802`), the **last call of every step**, is the camera / cinematic director (`this = CLogic+0x2DB44`). It reads fighter-derived inputs at `0x39C16C..0x39C18C`, runs `sub_000AC1F0` (special-camera moves: `motion/camera.xml`,
strings `slow`, `finish`, `after`, `back`, `shake`, loaded by `sub_000AA590`, `recomp_0005.c:6874-6914`), draws the RNG at `0x39C0E0`, writes the **time scale `CLogic+4`** through `sub_00044C10` (`:11718`), posts `0xF000002B`, and writes the view into
Render-subsystem objects (`sub_000443D0 -> sub_00018F90`, `sub_00044400 -> sub_000190C0`, and the block at `0x2DBF28` through `sub_0002ACA0 / 2ACC0`). All of that is guest memory, and the writes are plain "set from state" assignments, so they are idempotent given the director's state.
**The director's own state (and `CLogic+4`) is simulation state** because slow motion after a KO changes how fast the fighters step. Camera shake itself is therefore replayable; its audio/bus echoes are not (2.1).

### 2.6 HUD events [V]

`GAME_HUD` (alloc tag at `recomp_0003.c:77219`, ctor `sub_000B5140`, update `sub_000B5710`) is fed by bus handlers (2.1), so its score, combo and health displays advance **at Post time**, inside the step. It reads fighter state again in its update.

### 2.7 Memory allocation and free [V]

* **Allocator:** `sub_00029680(size, tagString, flags)` and `sub_000296B0` (array) call `sub_00029500`, which uses the **game heap object at `[0x33DB54]`** (vtable `+4` alloc, `+8` free; `recomp_0001.c:9604`, `:10052`) and otherwise a pool allocator
  (`sub_001E9E90` / `sub_001E9F90`, critical section `sub_001ECF00`, `recomp_0013.c`); frees are `sub_00029740 / 000294D0 / 00029760` (`recomp_0001.c:10029, 9490, 10069`), mostly reached through destructors (vtable), so they do not show in a direct walk.
  The CRT heap below it grows with `NtAllocateVirtualMemory / NtFreeVirtualMemory` (reachable through `sub_00200C41 -> 00203FFB` and `sub_00215F60`).
* **Allocation sites reached by the step** (direct walk): particle emitters (2.4); `sub_000388B0` (12-byte effect record); `sub_00021F40` ("CAnimation::InitMirror.m_pFnMirrorRotation": first use of a mirrored animation, via
  `001A8480 -> 001A76A0 -> 000446C0 -> 0009EF00 -> 0009ECB0 -> 0009E050 -> 0009FB30 -> 000243B0 -> 00021F40`); weapon slots (`sub_000296B0` in `sub_00025100`, via `001AAFD0 -> 001A8AB0 -> 001B47F0 -> 001B3B40 -> 001AA330 -> 000BBB10 -> 000BA5D0`);
  the sound command (2.2). **Frees:** weapon slot teardown `sub_000BBAA0 -> sub_000BA920` (callers: `7C2F0`, `7ED10`, `7F030` (crowd), `1AA140`, `1AA330`, `1AB410`, `1C2F50`), which frees the model, two blocks (`sub_000294D0`) and the slot.
* The crowd restart used by netplay was chosen because it does not allocate (`crowd-lockstep-reset.md`); the *update* the arena calls every step (`sub_00081170`) did not show an allocation in that study.
* **Repeat:** a re-run of an allocating step allocates again; restoring RAM without the heap leaks the first block and leaves restored pointers pointing at freed or reused memory.

### 2.8 File loads and resource loads [V reachability, U trigger]

`sub_001A5DD0` (the hit / damage routine, test hook `defjam_test_hit`) and `sub_001AAFD0` reach `sub_00044960 -> sub_0009EE20 -> sub_000979B0 -> sub_000A44F0` (format `%s%02d`, `V2IP_4` clip names) with the CRT file layer
(`sub_00200C41`, `NtQueryInformationFile / NtSetInformationFile / NtWriteFile` in `sub_001F8D61 / 001F8EBB`), `sub_00098660` (strings `NON_GEOM`, `NORMAL`) and a contiguous allocation (`sub_00026290 -> sub_00102A90 -> ... -> sub_001F4F8B` = `MmAllocateContiguousMemoryEx`).
It looks like load-on-first-use of a motion or model; **when it fires is [U]** (a hitch in the first use of a move; PROGRESS has a "later-match load hang" open item). Re-running a step that already loaded it does not reload.

### 2.9 D3D / NV2A [V]

No push-buffer building is reachable from the step except one place: **weapon slot teardown** (`sub_000BA920`, `recomp_0005.c:49290`) calls `sub_00019320` (`recomp_0000.c:25521`) -> `sub_0010E0C0` (`recomp_0007.c:108311`), which releases cached D3D resources
(tables at `0x3A6E20`, `0x3A5030`, `0x3A5050`) through `0x2142F0 -> 0x21EB90` (D3D's kick / fence, hooked in `src/recomp_manual.c:942` to wait for the executor, up to 100 ms) and `0x215F60 / 0x216140 / 0x21BB10 / 0x21C070 / 0x21CC10`. That is a GPU-visible, blocking operation that cannot be undone.
Drawing is the loop tail (`sub_00077740 -> sub_000796A0`); it writes the push buffer once per iteration and reads simulation state only. `sub_00087040` (called from the draw) reaches only a stub (`sub_001A3300` returns) and `sub_000A1E60` (CLogic+0x2D830 array), nothing that writes fighters [I].

### 2.10 Kernel calls reachable from the step (direct walk) [V]

`RtlEnter/LeaveCriticalSection` (allocator, CRT, `sub_00044C70 -> 00093CE0 -> 000A6130 -> 00200B63`), `NtWaitForSingleObjectEx / NtReleaseMutant` (`sub_000388B0 -> 0013A9E0 -> 001F6057 / 001F5DBA`, the engine mutex),
`NtAllocate/FreeVirtualMemory`, `MmAllocateContiguousMemoryEx`, `NtWriteFile / NtQueryInformationFile / NtSetInformationFile` (CRT file layer in the load path), `RtlNtStatusToDosError`, and error paths `KeBugCheck`, `RtlRaiseException`.
**No wall clock:** none of `sub_001EDB70` (tick), `xbox_ReadTimeStampCounter`, `KeQuerySystemTime` / `KeQueryInterruptTime` is reachable from the step (confirms `fight-determinism.md`). `sub_00011520`, the debug print every handler calls, is an empty `ret` (`recomp_0000.c:2960`).

### 2.11 Non-sim inputs that leak into a step (determinism hooks)

* **Shared object-form RNG `0x39C0E0`** (state `0x39C160`, term `0x39C164`) is the camera director's generator; the step draws it (`sub_000AACA0`, `sub_000ABB70`) and so does the per-step fade (`sub_000D90F0`) and about 25 non-step callers (`fight-determinism.md` Q3). It is not read by fighters (not shown), but it must be in the snapshot if the camera shake should replay.
* **Global generator `0x33DB5C`** (about 60 callers, seeded from the clock at boot). Netplay already gives the crowd a private copy of it (`netplay_crowd_update`, `src/hooks/netplay.c:1707`).
* Seeds already taken over by netplay: `sub_001BC690`, `sub_001AD220`, `sub_000AA0D0` (`src/recomp_manual.c:63-66`).

---

## 3. Calling the step N times from one place

### 3.1 Mechanics

* Yes, it can be done from the main thread. The pattern is in `src/hooks/netplay.c:1691`: `guest_call(fn, ecx, nargs, args)` moves `g_esp` below the hook frame, pushes the arguments and a dummy return address, sets `g_ecx` and calls the lifted function.
  The registers are thread-local globals (`g_eax .. g_esp`, `g_fp_stack`, `g_fp_top`, `g_fp_control_word`); save and restore all of them and the x87 stack around the block (as `crowd_reset` does, `:1716-1799`), because the hook sits in the prologue of a function whose caller keeps `esi / edi` live.
* **Reentrancy:** `sub_00087700_enter` fires on **every** entry (generated code calls it first, from every caller). The existing hook (`defjam_test_step(); netplay_on_step();`) would run the lockstep wait, step counter and hash logic for each recursive call. A depth flag is required.
  A hook on `sub_0008BE60` does not exist yet: a new `_enter` needs a re-lift (CLAUDE.md section 3, "define `void sub_XXXXXXXX_enter(void)`... re-lift after adding one").
* Rendering happens once per outer iteration by construction; nothing in `87700` or `8BE60` draws [V]. A fast-forward inside one `8BE60` therefore renders once (the final state) as long as the render at the loop tail comes after.

### 3.2 What a step reads (preconditions)

| Input | Where | Notes |
|---|---|---|
| time scale `CLogic+4` (`0x349764`) | `87700` passes it to `1A4900` (`recomp_0004.c:7751`) | 1.0, 0 when paused (`sub_0007C2F0`), written by the camera director and `sub_0008D5C0`; simulation state |
| `0x3B8F2C` | written by `1A4900` each step = scale x (0.1 + 0.6 min(1, `0x3B9064` / 60)) while bit 0x04 of `0x3B9054` is set | `simulation-tick.md` Q2 / Q3 |
| `0x3B9054` flags | `1A4900` keeps only mask `0xFFFE017F`; bits 0x04 (end slow-motion) and 0x08 (decided) persist | simulation state |
| pad table `0x3B8F48` (8 entries x 0x20) | `sub_001BAA00` per fighter; filled by `sub_00079280` before `8BE60`; CPU slots by `sub_001AD380` inside `sub_000C89D0` | netplay overwrites the entry inside `1BAA00` from `N.step` (`netplay_on_pad_read`), so a re-run needs `N.step` set per sub-step; CPU slots (`N.fighters > N.players`) need the merge, i.e. `8BE60` |
| `CLogic+0x2D820` state | `8BE60` switch | calling `87700` directly bypasses `8AB00` / `89DB0` and the KO / end transitions and the forced state 0xE / 0x10 at the tail of `87700` |
| `CLogic+8`, `CLogic+0x2D86C` | previous scale, state-4/5 counter (60) | |
| crowd manager `CLogic+0x2B9F0` and records `[0x3494E4]` | read by fighters, updated at the end of `8BE60` | must advance in the same order as the fighters |
| RNG states `0x3C1400-0x3C1488`, `0x3BFC70` (4 x 0x88), `0x39C160` | | already seeded deterministically by netplay |
| `dt` argument | constant 1/60 | only the camera director and the crowd use it |
| nothing else | no clock, no host input (checked for wall-clock reads) | |

### 3.3 What would break

1. Calling only `sub_00087700` N times: AI-controlled fighters would act on stale pad words, the crowd would not advance (its records feed catches / holds / weapons), `CLogic+0x2D86C` and the end-of-fight tests would not run.
2. Posting events N times (2.1): HUD, audio counters, rumble and the fade machine fire per sub-step unless the bus is closed; the Event Manager's previous-state copies (`sub_000BDF40`) are updated once per game update, so after a multi-step fast-forward it sees one large jump and posts one event set for the last step only (events for intermediate steps are lost).
3. The screen-update extras (particle ages, fade timers, HUD, FX, task subsystem) tick once per record; a fast-forward that skips them runs the sim ahead of the presentation systems (harmless for a one-frame correction, visible in a long catch-up).
4. Allocation / free / D3D teardown (2.7, 2.9) repeat or get skipped.
5. The step must not be entered while `sub_0008BE60` state is not 4 / 5 / 8; the live-fight states end with state 9; a replay past the KO continues the match.
6. The producer thread and the timer thread preempt the main thread mid-snapshot (they share one host CPU and the timer thread is TIME_CRITICAL, `dsound-voice-list-crash.md` section 4). They do not write simulation memory [V by reachability], so a memcpy of the sim ranges is safe against them; it is **not** safe against the game heap (everyone allocates).

---

## 4. Other threads

### 4.1 Guest threads present in a fight

| Thread | Start routine | Created at | Does | Touches sim state? |
|---|---|---|---|---|
| **main** | `sub_00011CD0` via `main` | CRT start | everything above | yes, only writer |
| frame-record producer | `sub_000D77C0 -> sub_000D7700` | `sub_000D77D0`, `recomp_0006.c:32435` | wakes on the 60 Hz event, polls 4 pads, pushes records (lock `frameObj+0x1A34`) | no (reads pads; writes the ring) |
| audio mixer | `sub_001242D0` (TIME_CRITICAL, 100 Hz) | `sub_001243A0 / sub_00124610`, `recomp_0008.c:64774, 65289` | takes the engine mutex `[0x3AE10C]` for its whole tick, `sub_0011FAF0` voice update, `sub_001241A0` ring refill (`music-ring-mixer.md`), `sub_00126300` position events | owns the sound-engine heap; shares the mutex with `sub_000388B0` in the step |
| save / storage service | `sub_0019AB60` (via `MyThread` `sub_000D1340`, `Sleep(5)` poll) | `sub_0019B0C0` (`recomp_0011.c:106686`) | memory-unit and `saveimage.xbx / savemeta.xbx` work (strings under its reach) | no |
| async file workers | `sub_001E4970` (array of 0xB8 slots at `0x3C8F30`) | `sub_001E5880` (`recomp_0013.c:625`, called by `1E5990 / 1E6060`) | [I] overlapped reads for streams and assets (the Audio Subsystem init uses the same API, `sub_001E3E40`) | [I] writes only IO buffers |
| loader | `sub_000771E0` | `sub_00077230` | screen init (not during a fight) | only before the fight |

Everything runs on **one host CPU** (`xbox_PinToGuestCore`, PROGRESS D28, `kernel_bridge.c:499`): no true parallelism, but preemption at any instruction.
The registry 0x2FB148 is a plain object table (`sub_00012CF0(slot)`), not a worker pool; `sub_00011CD0` is the main loop, not a worker.

### 4.2 Host threads

NV2A timer / interrupt thread (PCRTC vblank, PTIMER alarm = the 60 Hz tick, PGRAPH software methods; runs ISRs and DPCs on the guest stack; `kernel_bridge.c:2404`), GPU executor, APU frame thread (not pinned),
OHCI and gamepad sampler (`usb/ohci.c:1579`, `usb/usb_gamepad.c:723`), input reader (`input/input_host.c:756`), framebuffer and video pumps, and the netplay thread (`src/hooks/netplay.c:1001`; touches only `N` under `g_cs`, never guest memory).
The timer thread preempts a title thread mid-instruction while an ISR / DPC runs (`dsound-voice-list-crash.md` section 4) [V].

### 4.3 Data races a snapshot / restore must handle

1. **Game heap and sound heap:** the main thread allocates in the step; the mixer, async workers and the loader allocate too. A snapshot of heap contents while another thread allocates is torn; restoring a heap range under a running thread corrupts it.
2. **Engine mutex `[0x3AE10C]`:** the mixer holds it for a whole tick; `sub_000388B0` (time-scale change) and every cue wait on it, so a step can block behind the mixer.
3. **D3D device state / push buffer** is written by the main thread and read by the GPU executor; `sub_00019320 -> 10E0C0` (weapon teardown) waits on it.
4. **Record ring** is written by the producer and read by the main thread under `frameObj+0x1A34`; not part of the sim, but a restore must not touch it.
5. **Pad table `0x3B8F48`** is written only by the main thread (`sub_00079280`, `sub_000C89D0`, netplay); the USB sampler fills the pad buffers that the producer copies, not this table.
6. No guest thread other than main writes `CLogic` (`0x349760-0x3797B0`), the fighter records (`0x3B92E0`, stride 0x12E8), or the globals block `0x3B8F28-0x3B9074` [V for the guest threads listed in 4.1 by the direct-call walks; U for vtable-reached code].

---

## 5. State a step touches beyond the fighter records (pointers for the snapshot task)

| Area | Address | Evidence |
|---|---|---|
| CLogic / arena | `0x349760 .. ~0x3797B0` (state `+0x2D820`, crowd mgr `+0x2B9F0`, camera director `+0x2DB44`, match info `+0x2FF5C`, flag `+0x2FFB0`, `+0x30044` float) | `recomp_0004.c:18724`, `recomp_0003.c:76925` (`0x3797A4`, read by the draw) |
| Fighter records | `0x3B92E0 + slot * 0x12E8` | `fighter-record-fields.md` |
| Fight globals | `0x3B8F28 .. 0x3B9074`, `0x3B9088-0x3B90C8`, `0x3B9128`, `0x3B9634`, `0x3BE0EC-0x3BE0F4` | `simulation-tick.md` |
| Public snapshot buffers (outputs) | `0x3BDE80` (0x27C), `0x340D58` (0x550), `0x3B8598` (36) | `sub_001A2F60`, `sub_001A3D50`; copied by `sub_000BDF40` |
| Match RNG / AI | `0x3C1400-0x3C1488`; brains `0x3BF990 + slot * 0xB8`; AI RNGs `0x3BFC70 + slot * 0x88` | `fight-determinism.md` |
| Camera director + RNG | `0x39C0E0 .. 0x39C1D8` (RNG `0x39C160`), camera view block `0x2DBF28 .. 0x2DBF78` | `recomp_0005.c:11231-11780` |
| Crowd | manager `0x375150`, generator `0x375F68`, members (heap, 0x17C each), record table `[0x3494E4]` x `[0x3494E0]` x 0x34 | `crowd-lockstep-reset.md` |
| Model / animation world | `0x386350`, `0x37B0D0` (selected by scene type, `sub_000441C0`), driven through `sub_00044660 / 445A0 / 446C0 / 44960` | `recomp_0002.c:9690..` |
| Weapons | 16 slots x 0xD0 at the weapon manager (`sub_00044AC0`), type table `[0x39DB10]` | `crowd-lockstep-reset.md` section 7 |
| Particles | registry 0xA object (heap) + emitter list | 2.4 |
| HUD, Event Manager "previous" buffers, fade overlay `0x3A0180-0x3A079C`, stats module `0x340538`, audio-time-scale cache `0x340344 / 0x3407DC`, sound sequence counter `0x3AD950`, bus table `0x39E844..` | | 2.1, 2.2 |

---

## 6. Open points and cheap probes (nothing run)

* [U] screen class of an online match (`79B60` vs `7A8F0` vs `77A40`): log which of the three calls `sub_0008BE60` in a room match.
* [U] how many Post calls, allocations (`sub_00029680` entries) and `sub_00019320` calls a typical step makes: `RECOMP_WATCH_EXEC` on `0xBD720, 0x29680, 0x19320, 0x137010` gives totals per second (CLAUDE.md rule 2: read the totals).
* [U] whether anything in the step stores a sound handle or an emitter pointer in a fighter record: hash the four records before and after a hit with the bus and `0x3ADB7C` stubbed.
* [U] the 64+ vtable-reached call sites in the step set; one `icall-window.py` capture in a fight lists the targets.
* [U] whether the Task subsystem (`sub_0001B100`) or the HUD feed anything back into CLogic (tests of `0x3A0xxx`, `MEM32(0x39C0C0) & 0x40`).
* [U] what the extra present iterations (empty ring) do to timing under a rollback loop.
* [I] a quick consistency test for the "snapshot = CLogic + records + globals + RNGs" claim: hash those ranges plus the crowd every step on two runs with the same `--step-input` and `--rng-seed`; compare with `tests/golden/fight-stream.json`.

## 7. The three biggest risks for rollback

1. **Simulation state is not contiguous and not self-contained: it points into a heap that the step itself allocates from and frees to.** Fighters, crowd members and records, particle emitters, animation mirrors, weapon slots and the sound engine all hold or create heap blocks inside the step, through a shared allocator that other threads also use. Restoring `0x349760-0x3797B0` and the fighter records alone leaks blocks and leaves restored pointers dangling; the heap cannot be snapshotted safely under live threads. Needs a plan before anything else (pre-allocation, a whole-arena snapshot taken with the other threads quiesced, or confining/suppressing the allocating features during re-simulation).
2. **Stateful, edge-fired side effects are fired synchronously from inside the step.** The message bus drives HUD, audio, rumble, stats, fade and popups; the sound command pointer and the weapon-teardown D3D release are not idempotent or reversible. The off switches exist (`0x39E844`, `0x3ADB7C`), but then the subscribers' state (HUD, Event Manager previous buffers, stats, `0x3AD950`) diverges from the simulated state, and a mis-predicted hit has already been shown and heard.
3. **The unit of simulation is not `sub_00087700`.** The consistent unit is `sub_0008BE60` (AI merge, crowd, state arms, count-in cues) and the presentation systems (particles, FX, fade, task subsystem, Event Manager) tick per record outside it; steps are produced by wall-clock records (cap 4 per iteration, ring of 10, dropped records) rather than by an accumulator; and several RNGs (`0x39C0E0`, `0x33DB5C`) are shared with code that runs at frame cadence. A rollback hook therefore needs a new `sub_0008BE60_enter` (re-lift), per-sub-step input injection, reentrancy guards on every existing `_enter` hook, and a decision about which of the per-step presentation updates are replayed.

# Audio quality survey: why the Windows sound is "not good yet"

Read-only study of the toolkit APU model (`tools/xboxrecomp/src/apu/`), the XAudio2 backend, the AC'97 hook and the
game's own audio settings. Nothing was built or run. "Confirmed" means read in source; "inferred" means reasoned
from source and the existing docs; "unverified" means it needs a listening or capture test (commands at the end).
All paths are under `D:\DefJam\DefJamFFNY-recomp\` and `T = tools/xboxrecomp/src`.

The stale `T/apu/README.md` still says the output is `waveOut` and that GP "bypass" is the whole story. Trust the source,
not that file.

## 1. The path, guest voice to speakers

```
Game (statically linked DSOUND + EA sound library; EA mixes most sound in software and plays it
      through looping hardware voices, see docs/04-improvement-backlog.md 6c, PROGRESS D27)
  |  MMIO pokes at 0xFE800000 (trap handler -> T/apu/apu_mmio_hook.c) ; voice records in guest RAM
  v
fe_method()  T/apu/apu_vp.c:184   voice on/off/pitch/volume/bin/ssl registers written into the voice record
  |
  |   APU frame thread  T/apu/apu_core.c:594  (host-time paced, 8 x 32-sample frames per 5.333 ms period)
  v
mcpx_apu_vp_frame  apu_vp.c:1325  walks 2D/3D/MP voice lists, per voice voice_process  apu_vp.c:1086
  1. envelopes (filter env + amp env) stepped ONCE per 32-sample frame           apu_vp.c:1106, 1122
  2. fetch+decode: PCM8 (unsigned) / PCM16 / S24 / S32 / Xbox ADPCM (36-byte
     blocks, 64 samples used); stream voices via SSL segments                    apu_vp.c:745-955
  3. resample: LINEAR interpolation, per voice, rate = 2^(-pitch/4096)            apu_vp.c:964-1080
  4. optional per-voice SVF low-pass (FMODE)                                     apu_vp.c:1236-1262
  5. HRTF for voices < 64: compiled but OFF (g_config.audio.hrtf = false)        apu_shim.h:373, apu_vp.c:1265
  6. per-bin gain = amp env * 10^(-vol/1280) / 2^headroom, accumulate into
     32 float mix bins                                                          apu_vp.c:1274-1299
  v
mcpx_apu_dsp_frame  T/apu/apu_dsp.c:201   *** GP and EP DSP56300 are NOT emulated ***
  |  one hard-coded downmix of bins 0..5 -> stereo (src/main.c:644 forces RECOMP_APU_MIXDOWN=six)
  |  clamp +-1, int16 truncation, written into monitor.frame_buf slot (ep_frame_div % 8)
  v
mcpx_apu_monitor_frame  apu_core.c:344   every 8th frame: 256 samples into accumulator acc[1024]
  v
xa2_submit_samples  T/apu/apu_xaudio2.c:133   (Windows)  XAudio2, 48 kHz, 2 ch, int16
  |  3 x 1024-sample buffers (21.3 ms each), submit refused if 3 already queued
  v
XAudio2 mastering voice (48 kHz stereo) -> Windows audio engine (resamples to the device format)
```

Not on the path: the AC'97 hook `src/hooks/ac97_bm.c` only answers the bus-master reset handshake (`ac97_write`, lines
86-110: it clears the RR bit so DirectSound stops spinning). There is no AC'97 DMA, no codec volume, no PCM transfer. The
"software mixer" (`apu_core.c:985-1171`) and `T/audio/dsound_device.c` are an HLE bridge the game does not use (nothing in
`src/` calls `apu_mixer_*`); the mixer only runs as an empty loop each period (`apu_core.c:391`).

## 2. Answers to the six questions

**Q1 voice pipeline.** Decoding is faithful to xemu (PCM8 unsigned, PCM16, S24, S32, Xbox ADPCM `apu_state.h:292`; the
`ADPCM_SAMPLES_PER_BLOCK 64` "FIXME should be 65?" at `apu_regs.h:336` is inherited from xemu; 64 matches the Xbox format,
so I do not suspect it). Resampling is a small hand-written linear interpolator (`apu_vp.c:1032-1080`) because libsamplerate
is stubbed (`apu_shim.h:188-212`, `src_reset` is a no-op). It has no band-limiting for step > 1 and a sinc-less response for
step < 1. For a voice at exactly 48 kHz (pitch 0) `step == 1.0` and `frac` stays 0, so it is a bit-exact copy (no loss). Envelopes and volumes
change once per 32-sample frame with no per-sample ramp. SVF is the xemu filter (`apu_state.h:165-196`). HRTF code exists
but is disabled. Submix bins: all 32 are accumulated in float; headroom division is applied (`apu_vp.c:1278-1283`) but nothing
downstream re-applies the gain the GP program would.

**Q2 GP/EP.** Stubbed, not emulated. `mcpx_apu_dsp_init` allocates empty `DSPState`s and prints "STUBBED - passthrough mode"
(`apu_dsp.c:177-192`). The only GP behaviour is the doorbell acknowledgement (`apu_dsp.c:136-175`), a handshake that says every
command succeeded. Effect sends are not dropped on the floor and not processed: in the Def Jam configuration (`six`) only
bins 0-5 are read (`apu_dsp.c:250-253`) and every other bin (6-31, which includes FX sends, I3DL2, crosstalk and the multipass bin 31) is discarded. In the
upstream-default `all` mode (`apu_dsp.c:254-260`) they are summed raw into L/R (odd bins right, even bins left), which
would double the dry signal with unprocessed send copies. Mitigating fact: `dsstdfx.bin` (the standard effects image) is
not in the extracted dump (`find game -maxdepth 3 -iname "*dsstdfx*"` is empty), so the title most likely loads no reverb/EQ/compressor DSP image,
and "no reverb" may not be a real loss for this game. Unverified: the routing report below would show whether any voice
ever targets bins >= 6. The final output mix that the EP would produce is replaced by the shortcut at `apu_dsp.c:249-275`.

**Q3 output.** Host rate 48 kHz, 2 channels, int16 (`apu_xaudio2.c:25-28`), XAudio2 on Windows (SDL on macOS/Linux,
`apu_xaudio2.c:172+`). 5.1 to stereo: `apu_dsp.c:250-253`, `c = 0.7071*C + 0.5*LFE; L = 0.7*(FL + c + 0.7071*BL)`, `R` likewise.
Clip: hard clamp to +-1 (`apu_dsp.c:266-269`), conversion `(int16_t)(x * 32767.0f)` truncates (no rounding, no dither,
`apu_dsp.c:273-274`). No master limiter, no `volume_limit` (the field exists, `apu_shim.h:364,372`, and is never read).
Scaling is correct (no 24 vs 16 bit confusion found; `int24_to_float` is `<<8` then /2^31, `fpconv.h:46`). No DC offset
source found (U8 is re-centred, `fpconv.h:26`). Measured level of a 120 s Windows fight: peak 24,001/23,811 of 32,767,
RMS -20.6 dBFS, no full-scale samples (`docs/research/toolkit-rebase-acceptance.md`, "Captures, audio and saves").

**Q4 timing.** The frame clock is host time, not the consumer. `throttle` (`apu_core.c:511-546`) paces 256-sample periods
off QueryPerformanceCounter with `EP_FRAME_US = 5333` (`apu_regs.h:363`), then `SleepConditionVariableCS` in whole ms. It
catches up to four periods of lateness and clamps the deadline to four periods ahead. Nothing watches the XAudio2 queue
to adapt. Samples are dropped (whole 1024 buffer, `apu_xaudio2.c:146-149`) when 3 buffers are queued, and a device
underrun is only counted (`:144-145`), not concealed. Drift is real: see problem 2.

**Q5 TODO/approximation markers that matter.** `apu_dsp.c:1-14, 43-47, 246-248` ("not what a real EP does; cheapest
mixdown"); `apu_vp.c:957-962` ("simplified, no libsamplerate, nearest-neighbour" header, actual code is linear);
`apu_regs.h:336` FIXME; backlog 1b (`docs/04-improvement-backlog.md:194-205`: the trap handling is wrong, known, unscheduled);
`docs/known-issues.md:24-26` (one dropped buffer after every knockout, "not investigated"), `:76-80` (Deck distortion, #11).

**Q6 env vars.** `RECOMP_APU_MIXDOWN=six|...` (default `six` set in `src/main.c:644`), `RECOMP_APU_MIXDOWN_ALL=0|1`
(`apu_dsp.c:87-107`), `RECOMP_APU_LEVEL=1` (per-second peak, drop/dry counters, per-voice routing table;
`apu_core.c:373-377, 423-449`), `RECOMP_APU_PCM=<file>` (raw 48 kHz stereo s16 of what is submitted, `apu_core.c:413`),
`RECOMP_APU_VOICE_PCM=<prefix>` (voices 0xF8-0xFA before volume, `apu_vp.c:1168-1190`), `RECOMP_APU_TRACE[=n]`
(status/voice dumps, `apu_core.c:640`), `RECOMP_APU_DSP_ACK`, `RECOMP_AC97_READY`, `RECOMP_HEADLESS` (dummy SDL audio driver, SDL path only).
There is no interpolation choice, no master volume, no latency/buffer setting, no HRTF toggle; the game's settings
reference has no audio section. The EEPROM audio setting reports Stereo PCM (`T/kernel/kernel_xbox.c:156-163`).

## 3. Ranked suspected problems (most likely audible first)

### 1. Output queue has no cushion and no recovery (Windows) -> clicks, pops, short dropouts
- Evidence: XAudio2 starts immediately with no preroll (`apu_xaudio2.c:83`; the SDL path has a 2-buffer preroll,
  `:187-192`). The producer submits a 1024-sample buffer every 21.33 ms, exactly the rate the device drains it, so in
  steady state each buffer is queued just as the previous one ends: margin equals the scheduling jitter. The APU thread is a
  plain `CreateThread` at normal priority (`apu_shim.h:81-86`, no `SetThreadPriority`, no MMCSS) on a machine where the
  guest threads, GPU executor and timer thread (TIME_CRITICAL, `kernel_bridge.c:3140`) compete. Any late wake-up is an
  audible gap with a hard edge (nothing fades, `apu_xaudio2.c:143-145` only counts it). Each underrun re-times the device
  later and so builds some cushion (self-healing but only after glitches). Recorded: ten dry-queue events in the first
  seconds of a run, before the game starts (`toolkit-rebase-acceptance.md`, "ten dry-queue events across reports 1, 2, 4 and 6");
  2-7 per fight on the Deck.
- Also: the throttle sleeps in whole milliseconds (`apu_core.c:528-531`) on a Windows timer whose resolution is 15.6 ms
  unless something called `timeBeginPeriod`. The only call is in the input reader thread (`T/input/input_host.c:593`), so it
  depends on host input being active; with `RECOMP_SETTINGS=none` the pacing is coarse and bursty.
- Sounds like: occasional crackle/tick, brief mute, worse during loading and heavy scenes.
- Fix: (a) raise the APU thread priority (`THREAD_PRIORITY_TIME_CRITICAL` or AvSetMmThreadCharacteristics "Pro Audio") and
  call `timeBeginPeriod(1)` from `mcpx_apu_init_standalone`; (b) preroll 2 buffers before `Start` like the SDL path, and use
  more, smaller buffers (e.g. 8 x 512, keep ~40-60 ms queued); (c) fade the first/last 32 samples around a detected gap.
  Difficulty: Low (a, b), Medium (c).

### 2. Producer clock is 62 ppm fast and never corrected -> a dropped 21 ms buffer about every 5-6 minutes
- Evidence: `EP_FRAME_US 5333` (`apu_regs.h:363`) is an integer truncation of 5333.33 us, so the mixer runs at 48,003 Hz of
  host time, plus any difference between QPC and the sound card's crystal. `throttle` is open loop (`apu_core.c:511-546`);
  when 3 buffers are queued the new 1024-sample buffer is refused and discarded (`apu_xaudio2.c:146-149`,
  `apu_core.c:420-421` clears the accumulator regardless). Matches the harness allowance of "one dropped buffer per five
  minutes" (`docs/09-testing-harness.md:154-157`) and the "buffer dropped after a knockout" (slow motion lets the
  four-period catch-up overfill the queue) in `docs/known-issues.md:24-26`. Dropping 1024 samples mid-music is a
  discontinuity, not a fade.
- Sounds like: a click/stutter every few minutes, and one at a knockout.
- Fix: derive the period from the sample count (`next += 256 * 1e6 / 48000` in floating point or in a 1e-9 base), and close the loop on
  the real queue depth (XAudio2 `BuffersQueued`, or a ring): sleep a bit longer when the queue is above target, shorter below;
  when forced to drop, drop 32-sample frames with a crossfade instead of a whole buffer. Difficulty: Low (exact period),
  Medium (queue-locked pacing).

### 3. Final mix is a shortcut: no EP/GP, crude 5.1 -> stereo, bins >= 6 discarded, no headroom compensation, no limiter -> thin or unbalanced, quiet or occasionally harsh
- Evidence: `apu_dsp.c:201-275`. The game emits a six-channel stream (voices 0xF8-0xFA, `apu_vp.c:1168` comment) although
  the EEPROM reports stereo (`kernel_xbox.c:156-163`). The matrix is ad hoc (0.7 overall, centre at -3 dB, LFE at -6 dB into
  both channels, rears at -3 dB). The headroom each bin was given (`1 << submix_headroom`, `apu_vp.c:1281-1283`) is divided
  out in the VP and never restored, so absolute level is whatever that product gives: -20.6 dBFS RMS on a fight is on the
  quiet side for a game mix, and the Deck reported clipping (#11) with the same code, so level varies a lot by scene. The only protection against
  overload is the hard clamp at `apu_dsp.c:266-269` (flat-topped distortion). Real stereo output on a console takes
  front L/R (bins 0/1) from the EP unchanged and sends the other channels to the Dolby Digital encoder or the multi-channel DACs; folding C/LFE/rears
  into stereo is our invention, so dialog/music balance will differ from a real Xbox or xemu (xemu's non-DSP bypass reads
  bins 0/1 only, same family as the last branch of `apu_dsp.c:261-264`).
- Sounds like: voices/centre content a bit recessed or oddly phased, bass different, overall quiet, harsh when scenes
  stack up.
- Fix: first measure which bins carry energy in each scene (`RECOMP_APU_LEVEL=1`, see section 4). Then (a) replace the clamp
  with a soft limiter or a look-ahead limiter at -1 dBFS, (b) pick the downmix coefficients properly (ITU-R BS.775 style
  with normalisation, LFE low-passed or dropped), (c) add a make-up gain tied to the headroom values, (d) optionally round
  instead of truncate and add TPDF dither. Difficulty: Low (a, c, d), Medium (b, needs ears and routing data).
  Real GP/EP emulation (DSP56300 core exists in xemu, about 3000 lines) is High and only pays off if routing shows
  effects bins in use.

### 4. Looping-buffer streams depend on the guest's service thread being on time -> stutter/buzz when the guest hitches
- Evidence: Def Jam plays EA-mixed audio through looping hardware voices (`docs/04-improvement-backlog.md:299`) that the
  game refills by polling the play cursor on a 10 ms service thread (`docs/research/movie-audio-clock.md` sections 1-2).
  The APU advances the cursor on host time regardless of the guest (`apu_core.c:511-546`; cursor updates in
  `voice_get_samples`, `apu_vp.c:952`). When the guest stalls (load, shader hitch, HD pack decode, the single pinned guest
  core, D28) the voice laps the writer and replays stale ring contents; `apu_vp.c:964-970` records exactly this failure class
  from the earlier fix (patch 0068). Cursor also moves in 256-sample bursts (8 frames processed back to back, `apu_core.c:611,726`).
- Sounds like: a short repeated loop of music/speech (a "buzz" or stutter of tens of ms), mostly at transitions.
- Fix: log guest ring underruns first (count frames where the guest writer did not move between polls); then consider
  running the 10 ms service thread at higher priority / giving it a dedicated host core, and process voice frames
  one at a time with the sleep between them so the cursor is smoother. Difficulty: Medium to High (needs guest-side measurement).

### 5. Front-end trap handling drops 32-224 sample slices (known, unscheduled)
- Evidence: backlog 1b; `apu_core.c:684-722` waits at most 20 ms for the guest ISR to clear a trap and then gives up
  (`gave_up`, `expired`), after which `monitor_frame` plays the zeroed slices as silence (`apu_core.c:404, 727-731`). The
  trap is raised on every idle voice when DirectSound asks (`apu_vp.c:504-512`) and the walk keeps going after the trap
  (`apu_vp.c:1338-1356`). `RECOMP_APU_LEVEL` prints `front-end stops waited out ... outlasted the wait` (`apu_core.c:434-438`).
- Sounds like: tiny holes in music during effect-heavy moments; "distortion on every loud note" was this before the
  wait was added.
- Fix: the backlog plan: latch the first trapped method/voice, stop the voice walk at a trap, and keep producing the
  rest of the period. Difficulty: Medium.

### 6. Linear interpolation with no band-limiting -> slight dullness, aliasing and image noise on pitched voices
- Evidence: `voice_resample`, `apu_vp.c:1032-1080`; step capped at 16 (`:1042`), no low-pass before decimation; libsamplerate
  is a stub (`apu_shim.h:194-212`). xemu uses libsamplerate (I believe sinc); this is audibly worse for voices whose source
  rate is not 48 kHz (44.1 kHz or 32 kHz EA content would pass through every fractional phase, which modulates high frequencies).
  Exact-48 kHz voices are untouched. Unverified: which pitches the Def Jam voices use (`RECOMP_APU_TRACE` dump prints pitch,
  `apu_vp.c:1394`).
- Sounds like: "a bit dull" highs, faint metallic/aliasing hiss on pitch-shifted effects.
- Fix: replace the linear step with a 4-point Hermite/cubic or a windowed-sinc polyphase (8-16 taps) kernel; add a
  low-pass when step > 1. Difficulty: Low (cubic), Medium (polyphase with per-voice history).

### 7. Zipper noise from per-frame stepping of envelopes and volumes
- Evidence: `voice_step_envelope` is called once per 32-sample frame (`apu_vp.c:1106, 1122`) and the gain is a constant per frame
  (`:1276-1286`), so a fade or pan sweep is a staircase at 1.5 kHz. xemu behaves the same; unverified against hardware.
- Sounds like: faint buzzing under fades and volume ramps.
- Fix: linearly ramp the gain across the 32 samples from last frame's value. Difficulty: Low.

### 8. Smaller items
- HRTF is off (`apu_shim.h:373`); any 3D voice (index < 64) is played flat into the HRTF submix bins with duplicated mono
  (`apu_vp.c:1208-1213, 1265`). The voices seen turning on are 0x40+ (2D) and 0xF8-0xFA, so probably irrelevant. Low value.
- Per-voice SVF input scaling `sqrt(q/2+0.01)` (`apu_state.h:168`) attenuates by up to 13 dB at low Q; xemu-identical, unverified, only if the title sets FMODE.
- Voice that runs dry mid-frame leaves the rest of the frame zero rather than holding/fading (`apu_vp.c:1059-1065, 1130-1131`): a tiny click at the end of non-looping samples that do not end at zero.
- XAudio2 device loss (headset unplugged, default device change) is not handled; submissions silently fail and sound stays dead
  (`apu_xaudio2.c:159-160`). Low priority, Low difficulty (re-create on `XAUDIO2_E_DEVICE_INVALIDATED`).
- `(int16_t)(x*32767.0f)` truncation without dither (`apu_dsp.c:273-274`): quiet-passage graininess, negligible.
- Stale docs: `T/apu/README.md` claims waveOut and an older design; `apu_vp.c:957-962` header says nearest-neighbour but the code is linear.

## 4. How to confirm before changing anything (all read-only for the game, no code edit)

1. `RECOMP_APU_LEVEL=1 RECOMP_APU_PCM=<file>`: peak per second, "buffers dropped / device ran dry" per second, and the
   routing table (`voice slot -> bin gain energy`, `apu_vp.c:1020`). If voices ever route to bins >= 6 there are effects to
   miss; if bins 2-5 carry real energy the downmix matters (problem 3).
2. `RECOMP_APU_VOICE_PCM=<prefix>`: raw voices 0xF8-0xFA before volume, to compare with the final capture and separate
   decoding from mixing faults.
3. `RECOMP_APU_TRACE=n`: pitch, format, SSL state per voice (what rates the game uses; settles problem 6).
4. Listen to the capture with a spectrum view: flat-topped peaks mean clipping; a 5-6 minute periodic tick means problem 2;
   energy clustering in 21.3 ms gaps means problem 1.
5. Compare against xemu playing the same scene if the owner can (xemu needs the owner's BIOS/MCPX/EEPROM).

## 5. Suggested order of work

| Step | Change | Difficulty |
|---|---|---|
| 1 | Exact 5333.33 us period, 2-buffer preroll, more buffers, APU thread priority, `timeBeginPeriod(1)` | Low |
| 2 | Queue-locked pacing + fade on gaps/drops | Medium |
| 3 | Soft limiter, proper downmix coefficients and level calibration from the routing capture | Low-Medium |
| 4 | Cubic/sinc resampler, gain ramps inside a frame | Low |
| 5 | Backlog 1b (trap latching, no silent slices) | Medium |
| 6 | Guest-side ring underrun logging, then decide on service-thread priority | Medium-High |
| 7 | Real GP/EP emulation | High, only if routing shows effects bins in use |

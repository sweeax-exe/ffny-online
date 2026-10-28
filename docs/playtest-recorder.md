# Play-test recorder and bug bundles

For finding small, short-lived faults during ordinary play: a hitch, a picture that
is wrong for a second, a pop or a gap in the sound, something that happens only at
one venue or one Story point. Turn the recorder on, play, and press **F9** (on a pad
or a Steam Deck: hold **View** and click the **left stick**) when something looks or
sounds wrong. The seconds before the key are saved as a *bug
bundle*, which an agent (Claude, Codex) can read with
`python scripts/bug-bundle.py latest`.

Everything the recorder writes holds the game's own pictures and sound. It stays
under `logs/`, which git ignores. Do not commit it, attach it to an issue or upload
it anywhere.

## Turning it on

Any one of:

- the launcher's or the overlay's **Diagnostics** tab: *Record play-tests* (takes effect
  at the next start);
- `settings.ini`: `[debug] record = true`;
- start the game with `--record`, or with `RECOMP_RECORD=1` set, for one run.

On a Mac keyboard F9 is a media key (fast-forward): press **fn+F9**, turn on
*Use F1, F2, etc. keys as standard function keys* in the Keyboard settings, or set
`mark_key` to another key. The overlay's button works everywhere.

Every run then keeps a session folder, `logs/sessions/<date>-<time>/`, beside the
usual `logs/run-*.log.err`. `logs/sessions/LATEST` names the newest one.

| Setting (`[debug]`) | Default | Meaning |
|---|---|---|
| `record` | `false` | Keep a session for every run (env `RECOMP_RECORD`) |
| `mark_key` | `F9` | The key that writes a bundle; the overlay's Diagnostics tab also has a *Mark a bug here* button |
| `mark_pad` | `back+left_stick_click` | Pad buttons that write a bundle: hold **View/Back** and the **left stick click** together for half a second (on a Steam Deck too) |
| `record_fps` | `10` | Pictures a second kept for bundles, 0 to 30 (env `RECOMP_RECORD_FPS`) |
| `record_seconds` | `30` | How far back a bundle reaches, 5 to 120 (env `RECOMP_RECORD_SECONDS`) |
| `record_audio` | `true` | Keep the mixed sound, about 11 MB a minute |
| `record_game_state` | `true` | Log fighters, health and match events (the harness's read-only probes) |
| `auto_bundle_hitch_ms` | `0` | A frame at least this long writes a bundle by itself (five a run at most, a minute apart); 0 is off (env `RECOMP_RECORD_AUTO_HITCH_MS`) |
| `hang_seconds` | `15` | No frame for this long writes a *hang* bundle; 0 is off (env `RECOMP_RECORD_HANG_SECS`) |
| `keep_sessions` | `20` | Sessions kept whole; older ones keep their bundles and events and lose their frame rows and audio |
| `record_guest_ram` | `false` | Crash and hang bundles also hold the game's 64 MB of memory |

## What a session holds

| File | What |
|---|---|
| `frames.csv` | One row per presented frame: `frame`, `t_ms` (when it ended), `interval_ms` (its length), `present_ms` (time in Present, the display wait included), `work_ms` (time from the previous frame's end to this Present: the game and the push-buffer executor), `capture_ms` (the recorder's own picture read-back), `draws`, `vertices`, `clears`, `audio_dropped`, `audio_dry`, `step` (the fight's simulation step, -1 outside a fight), `log_bytes`, `pcm_bytes`, and the texture cache: `tex_uploads` and `tex_upload_kb` (textures uploaded this frame, new or changed in place), `tex_evictions` (evicted from a full cache this frame), `tex_cached` and `tex_cache_mb` (held now), `hd_cache_mb` (HD pack replacements resident now, 0 without a pack) |
| `events.jsonl` | One JSON object a line, each with `t_ms`, `frame` and `kind`: `fe` (every front-end call, so the screen in use; the log stops at 4,000), `hitch`, `audio_dropped`, `audio_dry`, `view` (mode 1 is a Story cutscene), `game` (match start, hits, damage, results), `overlay`, `memory` (every five seconds), `mark`, `bundle`, `crash`, `hang` |
| `audio.pcm` | The mixed output, raw 48 kHz stereo 16-bit (what the game produced, before the audio device) |
| `session.json` | Build, platform, the recorder's settings and every setting the run started with |
| `bundles/` | One folder per bundle |

**Late frames, sampled while they are late** (macOS, for agents). With `RECOMP_HITCH_PROBE=1`
the recorder looks every 4 ms for a frame that has not come for 60 ms and, while it stays late
(every 50 ms, six times at most), asks the runtime what every thread is doing: which thread holds
the guest CPU and the guest call site it is in, each thread's native stack, priority and CPU time,
and where the push-buffer executor is. The holder goes into the frame's `hitch` event, everything
into `stalls.jsonl` beside `frames.csv`; `python scripts/hitch-probes.py <session>` prints one line
per sample and `--summary` counts the causes. A sample costs 2-7 ms on a thread of its own. Off by
default.

**Times for log lines.** The log keeps its format; scripts and the harness parse it
as it is. Instead, every frame row records how many bytes of the log had been
written by then (`log_bytes`, flushed each frame while recording), so any line can
be placed on the frames' clock: it was written before the first frame whose
`log_bytes` passes the line's end. Audio samples are placed the same way through
`pcm_bytes`. `bug-bundle.py` does both.

## Bundles

A bundle is written:

- on the mark key or the overlay's button: the last `record_seconds` and three
  seconds after the press, and a full-size still from the moment of the press;
- on a crash (from the crash handler, at once; on Windows with a minidump,
  `process.dmp`, of every thread's stack);
- on a hang: no frame for `hang_seconds` (the game is still running: the README in
  the bundle gives its process id for `scripts/native-stacks.py`, on every
  host, and `scripts/sample-threads.py` on Windows);
- on a long hitch, when `auto_bundle_hitch_ms` is set.

| File | What |
|---|---|
| `bundle.json` | Why, when, the build, the last screen, the view mode, the last match start (fighters), the settings, a pacing summary, and the index of the pictures (`clip.frames`: frame number and time of each) |
| `clip.rgb` | The ring of pictures, oldest first: raw RGB, 240 rows, the picture's own width |
| `still.bmp` | The picture at the mark, 480 rows |
| `frames.csv`, `events.jsonl` | The rows and events of the same seconds |
| `log-tail.txt` | The last megabyte of the log (`log_tail_offset` in bundle.json says where in the log it starts) |
| `audio.pcm` | The audio of the same seconds (`audio_offset`) |
| `process.dmp`, `guest-ram.bin` | Crash bundles on Windows; guest memory when `record_guest_ram` is on |

## Reading a bundle

```
python scripts/bug-bundle.py latest                          the newest bundle
python scripts/bug-bundle.py logs/sessions/<s>/bundles/<b>   one bundle
python scripts/bug-bundle.py logs/sessions/<s>               a whole session: every hitch and drop-out
python scripts/bug-bundle.py latest --mp4                    also a video with sound, for a person (needs ffmpeg)
```

It needs Pillow and NumPy (`pip install pillow numpy`) and writes, beside the bundle:

| File | What |
|---|---|
| `report.md` | Start here. Where it happened (screen, cutscene or not, fighters, settings), pacing (median, p95, p99, worst, 1% low), every hitch with the side it came from and the log lines and events next to it, one-frame draw-count dips, audio gaps, clicks and clipping, flagged pictures, the events and the log of the last seconds with their times |
| `analysis.json` | The same, for tools |
| `frametime.png` | Each frame's length over the window, as a step line; hitches labelled with their side |
| `audio.png` | Level per 10 ms and a 0-12 kHz spectrum on the same time axis; gaps shaded, clicks marked |
| `contact-sheet.png` | Pictures across the whole window, labelled with seconds from the mark and the frame |
| `around-mark.png` | Every picture from three seconds before the mark to three after |
| `anomaly-N.png` | The pictures a second either side of each flagged moment |
| `log-timed.txt` | The log's end with a time and a frame on every line |

What the analysis flags, and how:

- **Hitch**: a frame at least 50 ms long and 2.5 times the median of the 120 before
  it, after taking off the recorder's read-back. *present* means most of it was
  spent in Present (display or graphics card), *game* that most was before Present
  (game code, the executor, loading), *recorder* that the read-back made it.
- **Draw-count dip**: a frame whose draw count is far from both neighbours while
  they agree: geometry missing (or extra) for one frame.
- **Flash / black picture**: a picture unlike both neighbours while they are alike;
  a picture far darker than both. A fault lasting several pictures, or a small one,
  needs a look at the sheets: the analysis does not claim to see everything, and
  vision models are known to miss game glitches, which is why the numbers come
  first.
- **Audio gap / click / clipping**: silence of 30 ms or more between sound; a step
  between samples far beyond the signal's own slope; samples at full scale. This is
  the audio the game produced; a drop-out in the device itself shows in the
  `audio_dropped` and `audio_dry` counters instead.

`python scripts/triage.py` on the run's log also names the session and its bundles
and puts `bug-bundle.py` first in its next steps.

## Cost

Recording is off by default and costs nothing then. On, it flushes the log once a
frame, appends a row and checks two file sizes. Each kept picture, ten a second by
default, is read back from the graphics card without waiting for it on Direct3D 11
and Vulkan: the copy starts in the frame that wants it and is collected a frame or
two later, filed under the frame that asked (`d3d8_readback.h` in the toolkit). The
OpenGL renderer, a multisampled target or a full set of copies in flight fall back to
a synchronous read-back, which waits for the card. Either way the cost is in every
row (`capture_ms`), and the analysis subtracts it before calling a frame a hitch.
`RECOMP_RECORD_SYNC=1` always reads synchronously, to compare the two. Lower
`record_fps` (or set it to 0) when measuring pacing itself.
The picture ring takes about 75 MB of memory at the defaults; a session about 12 MB
a minute on disk, most of it audio.

## In scripted runs

To watch or record a particular Story cutscene, see [Playing a chosen Story scene](scene-selector.md).

`scenario_suite.py run ... --record` turns the recorder on for a harness scenario and
links the session's analysis and every bundle from the scenario report.
`--record-hitch-ms N` writes a bundle for each frame of N ms or more. For harness
routes, `--env RECOMP_RECORD=1` works too.

## Not done

- The asynchronous read-back has been measured in the game on macOS only (above);
  Direct3D 11 and the Deck's Vulkan driver are still to be measured.
- Audio is captured before the device. What the device actually played, and its
  latency, is not recorded.
- Venues are not named in bundles: the game's venue index has not been mapped. The
  front-end calls around a fight (`fe` events) usually say which screen chose it.
- macOS: built and run on an M-series Mac (AppleClang, MoltenVK) on 2026-10-14: sessions,
  F9 and overlay bundles, the reports, and the asynchronous read-back (`capture_ms` about
  1.2 ms a picture against 3.4 ms synchronous). Windows (MSVC) and the Steam Deck are still
  to be run on the game.

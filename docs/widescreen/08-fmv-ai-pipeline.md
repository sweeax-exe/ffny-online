# 08 — FMV widening pipeline: blurred sides (shipping) and AI outpainting (to-do)

Status (owner, 2026-10-11): **blurred sides are the default and ship with widescreen** (WS-8 `side_fill = blur`).
AI outpainting "looks surprisingly good" but **the seam between the original picture and the generated sides is
visible and needs more blending**; it stays a to-do (WS-12). This page is the complete record of the trial so the
work can resume without redoing it. Results in [06 §5](06-cutscenes-movies-and-ai.md#5-fmv-trial-blur-vs-ai).

Everything here runs on the user's own machine from the user's own dump. Decoded frames and outputs are the game's
artwork: they live under `logs/` (ignored by git) and are never committed or published (`CLAUDE.md` §3).

## 1. What there is to widen

| File | Size | Length | Use |
|---|---|---|---|
| `movies/intro.mad` | 640x480 | 81.9 s | Opening movie |
| `movies/attract.mad` | 640x480 | 49.2 s | Attract loop on the title |
| `movies/soulplan.mad` | 640x480 | 30.1 s | Story |
| `thx.mad`, `eagames.mad`, `SoulTrai.mad`, `extra.mad` | 640x480 | 5.0, 3.2, 2.5, 2.5 s | Logos, stingers |
| `movies/movies.viv` (`blazin_NN`, `styleNN`, 87 clips) | 240x176 | 8.3 min | Shown inside menu panels: **not widened** |

About 3 minutes of full-screen video at 30.3 fps (EA "Madcow" video, `MADk` chunks). No file has baked-in bars
(ffmpeg `cropdetect` over the whole opening movie: 640x480 throughout).

## 2. Tools (installed by hand, outside the repository)

Trial layout on the owner's machine: `<data>/tools/fmv-trial/` with `ffmpeg-dl/`, `venv/`, `models/`.

| Tool | Version and source | Size | Notes |
|---|---|---|---|
| ffmpeg | 9.0.2 "essentials" Windows build, gyan.dev (`ffmpeg-9.0.2-essentials_build.zip`, SHA-256 `60f46726…a2047ba`, matches the published `.sha256`) | 115 MB zip | Has the `eamad` decoder. The .mad audio (EA ADPCM, 6 channels) is **not** supported ("Unsupported number of channels: 6"); the trial is silent, a final pack keeps the game's own audio anyway. |
| Python venv | Python 3.13; `torch==2.14.1+cu126`, torchvision (download.pytorch.org cu126 index); diffusers 0.41.0, transformers, accelerate, ftfy, sentencepiece, numpy, Pillow (PyPI) | ~3 GB | Kept separate from the system Python. |
| Model | `Wan-AI/Wan2.1-VACE-1.3B-diffusers`, Apache-2.0, Hugging Face (27 files) | 19.0 GB | Text encoder UMT5-XXL 11.4 GB, transformer 7.2 GB, VAE 0.5 GB. Check every file's size against the hub API after download (a CRLF file list once produced 27 empty files). |

Hardware used: RTX 4070 Ti SUPER 16 GB, 32 GB RAM, Windows 11. Licences of alternatives:
[generative AI report §6](../research/widescreen-genai.md).

## 3. The pipeline (`scripts/fmv-widescreen.py`)

```
# 1. Cut one shot (seconds from scene detection, then checked by eye) and decode it to PNG frames
python scripts/fmv-widescreen.py extract --ffmpeg <ffmpeg bin> --movie movies/intro.mad \
    --start 28.248 --duration 4.917 --work logs/fmv-widescreen/intro-crew
# 2. Blurred sides (the shipping look, as a video for comparison)
python scripts/fmv-widescreen.py blur --ffmpeg <ffmpeg bin> --work logs/fmv-widescreen/intro-crew
# 3. AI sides
<venv python> scripts/fmv-widescreen.py ai --model <models>/Wan2.1-VACE-1.3B-diffusers \
    --work logs/fmv-widescreen/intro-crew --prompt "<the shot and what plausibly lies beside it>"
# 4. Side-by-side video and stills; 5. side-pop report
python scripts/fmv-widescreen.py compare --ffmpeg <ffmpeg bin> --work logs/fmv-widescreen/intro-crew
<venv python> scripts/fmv-widescreen.py qa --work logs/fmv-widescreen/intro-crew
```

What each step does:

- **Shot selection.** `ffmpeg -vf "select='gt(scene,0.3)',showinfo"` lists cuts; it **missed a cut in a dark shot**
  (inside the trial clip), so check the contact sheet (`-vf "fps=1/2,scale=320:240,tile=6x7"`) by eye.
- **Extract.** `-ss` goes **after** `-i`. With `-ss` first ffmpeg starts decoding between keyframes and the first
  ~12 frames are blocky garbage, which the AI then imitates (run 1 drew a duplicate head at the seam from it).
- **Blur.** `scale=848:480, boxblur=24:3, eq=brightness=-0.12:saturation=0.8`, then the original overlaid at x = 104.
  The runtime version (WS-8) does the same from the canvas at present time.
- **AI.** Output 848x480 (multiple of 16 nearest 16:9). The original 640x480 sits at x = 104 on a mid-grey canvas;
  the mask is white (generate) on the sides, black on the picture. Windows of 81 frames (`4k+1`, the model's native
  length) overlapping by 12: in each window after the first, the 12 overlap frames are supplied complete (sides from
  the previous window, mask black), so the sides continue instead of jumping. UniPC scheduler with `flow_shift=3.0`
  (480p), 30 steps, guidance 5.0, seed 42, a negative prompt against text, borders and deformities. **VAE tiling is
  required**: without it the full-precision VAE overflowed 16 GB at 81 frames and the run stalled for 20+ minutes in
  shared memory. Model CPU offload keeps the text encoder out of VRAM. After generation the original 640 columns are
  pasted back over every frame, so the 4:3 picture stays bit-exact.
- **QA.** Mean absolute frame-to-frame change of left side, centre and right side; a side that changes more than 3x its
  average while the centre does not is reported as a "pop"; a centre jump is a cut, and the sides should change on
  the same frame.

## 4. Measurements (trial clip: `intro.mad` 0:28.25–0:33.17, 149 frames, two shots)

| Item | Value |
|---|---|
| VRAM | 9.7 GB peak with offload and VAE tiling |
| Speed | 16.7 s per step at 81 frames; 2 s per step at 17 frames; ~9.5 min per 81-frame window |
| Clip | 19 min for 4.9 s → ~3.9 GPU-minutes per second of video → **~12 GPU-hours for all 3 minutes** of FMV |
| Side stability | Mean change left 5.4, centre 9.9, right 5.3 (sides calmer than the picture) |
| Cut handling | Sides switch on the same frame as the picture (frame 60) |
| Window boundary | Not visible (no QA spike at frames 69–80) |
| Pops | 5 single-frame side jumps, all in the dark first shot (frames 19, 29 right; 41–44 left) |

## 5. What it looked like

- Good: plausible continuations lit like the scene (a brick building with windows beside the street, more of the crew
  and the street, the stone wall continued in the dark shot); people cut by the frame edge are completed (shoulders,
  arms, a whole second man in the dark shot).
- Not good enough yet (owner): **the boundary is visible**: the generated sides are softer, slightly different in
  contrast and grain, and the hard paste at x = 104 / 744 leaves a line. A known character cut by the edge (the fighter
  in the #27 jersey) gets an off-model face. The dark shot pops.

## 6. To-do: refinement plan (in order)

1. **Feathered seam band.** Let the model regenerate a band that overlaps the original by 24–32 px on each side (mask
   white over the band too), then composite with a cosine alpha ramp across the band: original fully opaque at the
   inner edge, generated fully opaque at the outer edge. This removes the hard line and lets the model's own
   transition carry over. Keep the inner 576–592 px bit-exact.
2. **Match grain, sharpness and colour at the seam.** Per frame: match mean and standard deviation (per channel) of a
   16-px strip just outside the band to the original strip just inside it; measure the original's high-frequency noise
   and add matching noise to the sides; apply a mild unsharp mask to the sides or a matching softening across the
   band. Cheap, deterministic, no extra GPU time.
3. **Fix pops.** Regenerate a window with another seed when `qa` reports pops, or replace the popped side frames by the
   average of their neighbours (single-frame pops only).
4. **Per-shot prompts and seeds.** One prompt per shot (dark shots need their own); 2–3 seeds per shot, pick by eye;
   fall back to blurred sides per shot.
5. **Bigger model if softness remains.** Wan2.1-VACE-14B (also Apache-2.0) with offloading on 16 GB: several times
   slower (estimate 40–60 GPU-hours for all FMV). Or upscale the whole frame (HD pipeline) after compositing so centre
   and sides share one look.
6. **Faces at the edge.** For shots where a licensed character is cut by the edge, either accept, regenerate with
   another seed, or use blurred sides for that shot.

## 7. How it would ship (WS-12, when the owner says the refined result is good)

- The repo ships `scripts/fmv-widescreen.py` (grown into a batch builder) and a manifest of shots (movie, start,
  length, prompt, seed): text only, no frames.
- The user runs it locally; it writes a pack under the data folder, `mods/<pack>/widescreen/movies/<movie>/`, holding
  only the **side strips** per frame (the centre always comes from the game's own decoder).
- The engine side is WS-8's `art` mode for movies: the movie hook (WS-7) reports the movie name and its decoded frame
  number, and the host draws that frame's strips instead of the blur. Frames without strips fall back to blur.
- Model downloads happen only on the user's request, and only Apache-2.0 models are defaults.

# Widescreen via generative AI: research notes

Status: research only, no code or assets touched. Date of research: 2026-10-10.
Scope: can generative AI extend fixed-4:3 2D content (FMV, menu backgrounds, loading screens, overlays) to 16:9 / 16:10 / 21:9 for the Def Jam: Fight for NY static-recompilation PC port?

Evidence labels used below:
- **[V]** verified against a primary page (model card, official repo, official docs) fetched during this research.
- **[S]** secondary source only (news article, reseller, community workflow page); treat as plausible, not proven.
- **[U]** unverified or my own inference / general knowledge; no source retrieved.

Project facts taken from the task brief and NOT independently checked: output is 640x480; FMVs are EA `.mad` files, about 147 MB total, likely 640x480 or smaller, about 30 fps; menus are Flash-style vector plus bitmap UI. **[U]** The total FMV running time is not known to me. At a typical 1 to 2 Mbps, 147 MB is roughly 10 to 20 minutes **[U]**, so verify with the real file list before budgeting GPU hours.

---

## 0. Executive summary (honest version)

1. **Still images (menu backgrounds, loading screens, static overlays):** AI outpainting is practical today, runs locally on a 12 to 24 GB consumer GPU with Apache-2.0 models, and is the only approach that yields a "real" full-bleed widescreen picture. It is good for continuable backgrounds (walls, skies, crowds, bokeh) and bad for anything with logos, text, faces or strong geometric structure at the seam. Every result needs a human eye; a fully unattended batch will produce some bad frames. Fan projects that did this for FF7 backgrounds (Cosmos Limit Break) combined AI with "extensive manual editing" **[S]**.
2. **FMV:** Local video outpainting with Wan 2.1/2.2 VACE or LTX is technically possible on a 4090-class card but is slow (order of hours of GPU time per minute of footage **[U, extrapolated]**), needs per-shot chunking, and has known seam, drift and flicker failure modes. No fan project I could find shipped AI-outpainted widescreen FMVs at scale. Realistic quality: acceptable for shots whose edges are continuable (environments, sky, crowds), poor for close-ups and shots with text, logos or characters crossing the frame edge.
3. **Do not make AI outpainting a core, default feature of the project.** Make it an optional, user-run, clearly experimental pipeline stage, like the existing HD texture pack, with a non-AI default (blurred ambient fill or pillarbox) that always works.
4. **Real-time 3D, in-engine cutscenes, HUD:** no AI needed. Render wider (Hor+), re-anchor UI. Those are engine-side tasks.
5. **Licensing:** pick Apache-2.0 models (Wan 2.1/2.2 VACE, Qwen-Image-Edit, FLUX.2 [klein] 4B) as the defaults the tool would download. Treat FLUX.1 Fill/Kontext [dev], ProPainter, SD3.5, Hunyuan, and LTX as "needs license review" or "user opt-in only". Hosted services (Runway, Luma, Adobe Firefly) are out of scope for the "generated locally from the user's own files" model.

---

## 1. Image outpainting: state of the art and local feasibility

### 1.1 Model table

| Model | Type | Params | License (for downloading and running) | Local VRAM (reported) | Notes for this project |
|---|---|---|---|---|---|
| **Qwen-Image-Edit** (also 2509 / 2511 revisions) | Instruction-based image editing, DiT | 20B | **Apache 2.0** **[V]** (model card says "Qwen-Image is licensed under Apache 2.0") | BF16 file about 41 GB; GGUF Q4_K about 11.5 GB file, Q5 about 14.3 GB, Q6_K about 16.8 GB (community repo suggests 16 to 24 GB VRAM for Q4) **[S]** | Ready-made ComfyUI outpaint workflow: pads 384 px left and right, Q5_0 GGUF + 8-step Lightning LoRA, denoise 0.95, CFG 1.0, Apache 2.0 workflow **[S]**. Best licence/quality combo for a downloadable tool. Official card does not mention outpainting itself **[V]**, so outpainting is a workflow trick over the edit model. |
| **FLUX.2 [klein] 4B** | Generation + multi-reference editing | 4B | **Apache 2.0** for 4B; 9B variant is non-commercial **[S]** (BFL help centre and blog snippets) | "as little as 13 GB" per the model card summary; FP8/NVFP4 variants reduce further **[S]** | Newer, small, fast. Editing quality for outpainting on stylised art is **not verified** by me. Candidate to benchmark. |
| **FLUX.1 Fill [dev]** | Dedicated mask inpaint/outpaint | 12B | **FLUX.1 [dev] Non-Commercial License** **[V]**; gated on Hugging Face (must accept terms) **[V]**. Outputs reportedly usable commercially, weights not **[S]** | Official ComfyUI workflow needs about 24 GB **[S]**; GGUF Q4 to Q8 community quants exist (Q5_K_M about 7.9 GB file) **[S]** | Model card itself lists "slight colour shifts in unfilled areas" and "possible lines at the edges of filled regions with complex textures" as limitations **[V]**. That is exactly the seam problem we care about. Non-commercial licence makes it unsuitable as a default. |
| **FLUX.1 Kontext [dev]** | Instruction image editing | 12B | **FLUX.1 Non-Commercial License** **[V]** (BFL blog); requires content filters/provenance compliance **[V]** | BFL gives no VRAM figure **[V]**; community GGUF quants exist **[U]** | Fixed about 1 MP with flexible aspect ratios **[S]**. No verified outpainting-specific support **[V: search found none]**. Skip. |
| **SDXL 1.0 + diffusers SDXL-inpainting-0.1** | Latent diffusion + inpaint checkpoint | about 3.5B | CreativeML Open RAIL++-M (commercial use allowed with use-based restrictions) **[S]**; confirm on the official Stability card **[U]** | Fits 8 to 12 GB **[U]** | Diffusers describes the inpaint checkpoint as better at inpainting/outpainting than base but "still experimental" **[S]**. Old, but the lightest and most controllable (ControlNet, IP-Adapter for style). |
| **SD 3.5 Large** | Text to image | 8B | Stability Community License: free under USD 1M annual revenue, above that needs an enterprise licence **[V]** | Model card gives no figure; NF4 quantisation suggested **[V]** | No outpaint-specific checkpoint documented on the card **[V]**. Not recommended. |
| **Adobe Generative Expand (Photoshop / Firefly)** | Hosted | n/a | Proprietary, cloud | n/a | Works well on stills **[S]** but is cloud-based and needs Adobe account, so it fits only the "user-made mod pack the project never distributes" path, done by hand in Photoshop. |

Sources:
- Qwen-Image-Edit-2511 card: https://huggingface.co/Qwen/Qwen-Image-Edit-2511
- Qwen outpaint workflow (Civitai): https://civitai.com/models/1934155/qwen-image-edit-fast-and-simple-outpainting
- Community Qwen GGUF sizes: https://huggingface.co/badgame/qwen-image-edit-gguf (note: this specific repo is an "abliterated" fine-tune; prefer QuantStack / unsloth conversions)
- FLUX.2 [klein]: https://bfl.ai/blog/flux2-klein-towards-interactive-visual-intelligence and https://help.bfl.ai/articles/8642316687-flux-2-klein-fast-generation-guide
- FLUX.1 Fill [dev]: https://huggingface.co/black-forest-labs/FLUX.1-Fill-dev and ComfyUI docs https://docs.comfy.org/tutorials/flux/flux-1-fill-dev
- FLUX.1 Fill GGUF listing: https://civitai.com/models/1108146/fluxfill-devgguf
- FLUX.1 Kontext [dev]: https://bfl.ai/blog/flux-1-kontext-dev
- SD 3.5 Large: https://huggingface.co/stabilityai/stable-diffusion-3.5-large
- SDXL inpainting in diffusers: https://huggingface.co/docs/diffusers/v0.39.0/api/pipelines/stable_diffusion/stable_diffusion_xl
- Adobe Generative Expand: https://helpx.adobe.com/camera-raw/using/generative-expand.html

### 1.2 Practical workflow facts

- ComfyUI's own outpaint tutorial confirms outpainting is "really an inpaint workflow" with a pad node that builds the mask, plus a feather parameter that controls how softly the new area blends into the original **[V]**. https://docs.comfy.org/tutorials/basic/outpaint
- Typical guidance: feather about 20 to 40 px, extend in stages if the extension is large, tell an edit model explicitly what must stay unchanged, and put quoted text only inside the original frame **[S]**. https://www.comfyui.org/en/beyond-the-frame-flux-model-image-outpainting (workflow guide)
- The extension needed here is modest: 640x480 to 16:9 at the same height is 853x480, so about 213 px of new content (about 25% of the output width, about 107 px per side if split evenly). 21:9 at the same height is 1120x480, so about 480 px new (about 43%). 16:10 is 768x480, 128 px new. **[arithmetic]** Smaller extensions are much more reliable than larger ones; 21:9 would mean nearly half the frame is invented.
- Qwen-based workflows scale the padded canvas to the model's working size **[S]**, so source resolution matters.

### 1.3 Upscale and outpaint order

No authoritative source settles the order for low-res game art; my search found only general guidance **[S]**: small or noisy sources give the model too little detail to match, so upscale first, outpaint second, then downscale if needed. One FLUX workflow does the reverse (outpaint then upscale) but is aimed at photographs **[S]**. https://www.runcomfy.com/comfyui-workflows/flux-outpainting-flawlessly-aligned-with-original-image

Recommended experiment (**[U]**, my reasoning, to be validated on 3 to 5 real backgrounds):
1. Upscale the 640x480 original to about 1280x960 using the project's existing local upscaler (the HD pipeline), so the outpaint model works near its native resolution and the project reuses code it already ships.
2. Pad to the target ratio, outpaint **only** the pad plus a small feathered overlap strip (say 16 to 32 px) of the original. Composite the original pixels back over everything except the overlap, to guarantee the original content is bit-identical to the HD upscale.
3. For the final native-resolution case, downscale; otherwise keep the HD result.
4. Match grain/noise and colour grade of the original on the generated strip (histogram match or a small post step). FLUX Fill documents colour shift in non-filled areas **[V]**, so the "composite original back" step is essential, not optional.

### 1.4 Quality on stylised low-res art and UI text

- **Text and logos:** AI processing damages small text. The Seamless HD Project team (RE2/RE3 upscales) explicitly says small text was a weak spot and was replaced when too distracting, and that dark corners "melt" **[S]**: https://www.pcgamesn.com/resident-evil/seamless-hd . A Nexus comment on an FF7-style opening FMV reported smeared writing **[S]**. This is upscaling rather than outpainting evidence, but the same failure class applies. Rule: never let a generator touch pixels containing text or logos; mask them out.
- **Style consistency:** Edit models (Qwen, Kontext, klein) take the original as a conditioning image and therefore inherit its style better than a plain text-to-image outpaint. Dedicated fill models are more literal about the mask. In every case, a short prompt describing the scene plus "same art style, same lighting" helps **[S]**. IP-Adapter or LoRA style locking on SDXL is a possible heavier option **[U]**.
- **Seams:** the most common visible artefact is a vertical line or brightness step at the join. Mitigations: overlap strip with feathered blend, composite-original-back, per-asset human review. The LTX docs list the same class of problem (visible seams in smooth regions such as sky, fixed by larger mask dilation) **[V]** for video.
- **Hallucination risk:** a fighting game's menu art contains characters, crowds and faces. Outpainting a character cut by the frame edge will invent limbs/faces. Those assets should be flagged for the non-AI fallback.

---

## 2. Video outpainting for the FMVs

### 2.1 Candidate tools

| Tool | Open / local? | License | Reported requirements | Notes |
|---|---|---|---|---|
| **Wan 2.1 VACE (1.3B and 14B)** | Open weights | **Apache-2.0** **[V]** (repo README + ComfyUI docs) | Repo table: 1.3B at 81 x 480 x 832 and 14B at 81 x 720 x 1280 **[V]**. 1.3B is 480p only; 14B supports 480p and 720p (better at 720p) **[V]**. Official ComfyUI page lists model files of about 7 GB (1.3B) and about 32 GB (14B) **[V]** | "Expand-Anything" is a documented task **[V]**. Official ComfyUI VACE outpaint template exists (ImagePadForOutpaint + mask repeated across frames); dimensions must be multiples of 16 and frames satisfy (length-1) mod 4 = 0 **[S]**. Timings on a 4090 from the ComfyUI page: 720x1280 x 81 frames about 40 min; 640x640 x 49 frames about 7 min **[V]**. |
| **Wan 2.2 VACE-Fun A14B** (Alibaba PAI) and Kijai GGUF/BF16 conversions | Open weights | Wan 2.2 base repo is **Apache-2.0** **[V]**; the PAI "VACE Fun" fine-tune licence was **not** verified from a primary page **[U]** (secondary sources say Apache-2.0 **[S]**) | Official PAI release reportedly needs about 60 GB VRAM; Kijai's BF16 and GGUF versions target mid-VRAM **[S]**. Wan 2.2 A14B on a single GPU officially needs 80 GB; TI2V-5B runs on 24 GB (4090) **[V]** | A ComfyUI "video outpainting with native Wan VACE 2.2" workflow exists **[S]**. https://civitai.com/models/2524167 |
| **LTX-2.x outpainting IC-LoRA** | Open weights (gated on HF) | "LTX Model License" free under about USD 10M revenue per one source, "Community License" per another, so **conflicting** **[S]**. Not Apache **[S]**. Read the actual licence text before use | Docs give no VRAM numbers **[V]**. Pipeline: 2 stages (8 steps low-res then 2 steps high-res) with Laplacian pyramid blending; 22B distilled base; needs Gemma text encoder **[V]**. Training needs 32 to 80 GB **[V]** (irrelevant for inference) | Officially documents outpaint (source video is centred on a larger canvas, padding is generated), seam and green-edge-artefact troubleshooting **[V]**. https://docs.ltx.io/open-source-model/advanced-workflows/in-outpainting . Likely the fastest open option but its 22B size and licence make it a harder default. |
| **HunyuanVideo** | Open weights | Tencent Community License **excludes EU, UK and South Korea** from the licensed territory **[S]** | Large | No official outpaint feature found **[S]**. Geographic exclusion is incompatible with a global open-source tool. Skip. |
| **ProPainter** | Open code | **NTU S-Lab License 1.0, non-commercial** **[V]** | 640x480: about 10 GB (fp32) / 6 GB (fp16) for 50 frames; 12/7 GB for 80 frames **[V]** | Designed for video inpainting; outpainting is an undocumented code option and the ComfyUI wrapper author says results "aren't very good" **[S]**. Flow-propagation methods only copy existing content, so they work for panning shots where the missing edge was visible in other frames, and fail for static shots. Useful as a pre-pass, not as the main generator. https://github.com/sczhou/ProPainter |
| **M3DDM** (Alibaba Mama, 2023-24 research) | Open code + weights | **Apache-2.0** **[V]** | Based on SD 1.5; README lists no memory figures; operates at a low internal resolution (256 mentioned for copy_original) **[V]** | Dated. Supports 16:9 target ratio **[V]**. Quality below current DiT models **[U]**. https://github.com/alimama-creative/M3DDM-Video-Outpainting |
| **Be-Your-Outpainter / MOTIA** (ECCV 2024) | Paper (arXiv 2403.13745); no repo or licence found in my search | unknown | Input-specific adaptation = per-video test-time tuning | Per-clip fine-tuning is too slow for dozens of shots. Skip. |
| **OutDreamer, "Unified long video inpainting and outpainting" (Wan 2.1 co-denoising), GlobalPaint** (2025 research) | Papers; release status not checked | unknown | n/a | Show the research frontier for chunk-seam/drift fixes (overlap-and-blend denoising, cross-clip refiner, keyframe-first generation) **[S]**. https://arxiv.org/html/2506.22298v1 , https://arxiv.org/html/2511.03272v1 |
| **Runway Expand Video** | Hosted | Proprietary | Old doc: Gen-3 Alpha Turbo, 25 credits for 5 s or less, 50 credits for longer, max 10 s, output 1280x768 **[S]** | Cloud only. |
| **Luma Ray 3.2 Reframe** | Hosted API | Proprietary | Source clip 10 s max, aspect ratios up to 21:9, resolutions 360p to 1080p, source kept frame-for-frame **[V]**. Reseller list price about USD 0.01 to 0.04 per second **[S]** | https://docs.agents.lumalabs.ai/guides/videos/reframing . Cloud only. |
| **Adobe** (Premiere Generative Extend, Photoshop video Generative Expand) | Hosted | Proprietary | Generative Extend extends in time (2 s limit), not aspect ratio **[S]**. Photoshop notes generative expand for video works best on static tripod shots **[S]** | Not a fit. |
| **Google Veo** | Hosted | Proprietary | I did not verify any Veo outpaint capability. | **[U]** Skip. |
| **CogVideoX-based outpainting** | n/a | n/a | Not investigated beyond the paper list; superseded by Wan/LTX in practice **[U]** | Skip. |

Hosted services (Runway, Luma, Adobe, Veo) require uploading the user's copyrighted game video to a third-party server. That breaks the "generated locally from the user's own files" model, and in the project-distribution sense creates a TOS question. They are only acceptable as something a user does by hand on their own, never integrated.

### 2.2 Local feasibility on RTX 4080/4090

Facts:
- Wan 2.1 VACE 14B at 720x1280, 81 frames: about 40 minutes on a 4090; 1.3B-class workloads at 640x640, 49 frames: about 7 minutes **[V]** (timings on the ComfyUI page are for VACE generally, not necessarily outpainting specifically).
- VACE officially supports 480p (1.3B) and 480p/720p (14B) **[V]**, which fits a 640x480 to 853x480 target naturally.
- A third-party workflow page claims 24 GB+ for the Wan 2.1 VACE outpaint pipeline in BF16 **[S]**; GGUF quants can drop this to 12 to 16 GB with chunking **[S]**.

My extrapolation **[U]** (please treat as a rough budget, not a measurement):
- Target 853x480, 81-frame chunks (about 2.7 s at 30 fps). Assume roughly 5 to 10 minutes per chunk on a 4090 with the 14B model at 480p with step-distill LoRA lower. A 1-minute clip at 30 fps (1800 frames) is about 22 chunks, so about 2 to 4 hours per minute of footage. For 10 to 20 minutes of FMV that is on the order of 20 to 80 GPU hours, before retries. On a 4080 (16 GB) expect longer, due to offload.
- The 1.3B model is several times faster but lower quality **[U]**.
- Frame rate: the generator works at its native frame rate (Wan 16 fps / LTX 24 fps class); processing 30 fps footage either needs frame-rate conversion (adds interpolation artefacts) or sending it at the model's fps and re-interpolating **[U]**. An alternative that preserves the original pixels: only outpaint the side strips conditioned on the original and composite the original back, which also avoids touching original frames. VACE/Luma/LTX are all described as keeping source pixels or being able to **[V for Luma/LTX, U for Wan]**.

### 2.3 Temporal-consistency and failure modes

Verified or documented:
- Chunk-based processing is a known weak point: stitching artefacts, drift and colour shift across chunks; research papers fix them with overlap-and-blend co-denoising, cross-clip refiners or keyframe-first generation **[S]** (arXiv 2506.22298, 2511.03272).
- LTX docs: visible seams in smooth regions, green artefacts leaking at mask edges, frozen-audio conditioning side effects **[V]**.
- Community reports: masked-area artefacts returning as smoke or solid colour; flicker and mismatch near reference frames **[S]** (loose match, not VACE outpainting specifically).
- ProPainter: flow propagation cannot invent content that was never visible; outpainting mode "not very good" **[S]**.
- Adobe says video generative expand is best on static tripod shots **[S]**, which implies moving-camera shots are riskier.

Expected for a fighting-game FMV (**[U]**, inference):
- Cuts: must be split per shot (shot-boundary detection) so the model never blends across a cut.
- Close-ups of characters cut by the frame edge: hallucinated body parts, mismatched skin tone/lighting; the most likely "looks bad" case.
- Text, titles, EA/publisher logos, burnt-in subtitles: must be skipped.
- Fast camera motion and motion blur at the frame edge: generated strip will not carry equivalent blur, causing visible flicker/seam.
- Compression grain (the .mad codec is lossy at 640x480): generated strips are clean, so grain mismatch is visible unless re-noised.

### 2.4 Hardware requirement summary (video)

- Minimum realistic: 16 GB VRAM + 64 GB system RAM, GGUF quant of Wan 2.x VACE, 480p, chunked **[S/U]**.
- Comfortable: 24 GB VRAM (4090), BF16/FP8 14B, 480p to 720p **[S]**.
- Disk: 14B models are about 30+ GB each plus text encoder, VAE, LoRAs; LTX 22B similar or larger **[V]**.
- NVIDIA/CUDA assumed; AMD paths exist (ROCm blog on VACE) **[S]** but are not verified.

---

## 3. Real-world fan and mod precedent

| Project | What it did | AI widescreen? | What worked / what did not | Source |
|---|---|---|---|---|
| **FF7 "Cosmos Limit Break"** (PC, 1997) | Widescreen pre-rendered backgrounds, extended using AI (reported as outpainting) plus "extensive manual editing", then upscaled. Also claims widescreen renders for nearly all cutscenes: AI expand, then manual detail | **Yes (stills; cutscene claim secondary)** | Press called results impressive but "some shots don't quite look perfect yet" and noted purists may dislike the big changes. No artefact detail given. | https://www.dexerto.com/gaming/modder-remasters-final-fantasy-7-using-ai-were-not-sure-how-to-feel-2476340/ , https://exputer.com/news/games/final-fantasy-7-mods-camera-overhaul/ **[S]** |
| **FF9 Moguri Mod v9.0** | Backgrounds re-rendered with Stable Diffusion; adds ultrawide support and 120 FPS | Backgrounds re-rendered by AI and ultrawide added; the article does not say whether extended borders were AI-generated, and FMV handling is not mentioned | Strongly positive reception (PC Gamer, RPS, GamesRadar), though some backgrounds look "painterly" | https://en.wikipedia.org/wiki/Moguri_Mod **[S]** |
| **Resident Evil "Seamless HD Project"** (GameCube RE2/RE3/RE1) | ML upscaling of pre-rendered backgrounds, textures, menus, text; runs via modified Dolphin | **No widescreen found** | Authors warn of "melting" on dark corners and distant areas; small text was a weak point and some had to be replaced; one reviewer found it softer with an "AI" look vs. a competing mod | https://www.pcgamesn.com/resident-evil/seamless-hd **[S]** |
| **Command & Conquer Remastered** (official, Petroglyph/EA) | AI-upscaled FMVs from 320x200 sources to higher resolution, 15 to 30 fps, audio cleaned | **No**; kept 4:3 | Community gripe: 4:3 FMV in 16:9 plays as a small box on black bars, which is exactly the problem here | https://www.thegamer.com/command-conquer-remastered-ai-cutscenes/ , https://steamcommunity.com/app/2229850/discussions/0/4362373720224465124/ **[S]** |
| **Aliens: Dark Descent cutscene upscale** (Nexus) | AI-upscaled cutscenes formatted for ultrawide without stretching | **No generative fill**; just letterbox handling | Side bars remain, top/bottom bars removed | https://www.nexusmods.com/aliensdarkdescent/mods/38 **[S]** |
| **Zelda 64: Recompiled (RT64)** | Native widescreen/ultrawide up to arbitrary ratios for 3D | No AI | Reports note HUD still clings to 4:3 proportions and some cutscene animations look "wonky" at the edges of ultrawide (3D geometry/animation culling at edges) | https://www.dsogaming.com/news/the-legend-of-zelda-majoras-mask-just-got-an-unofficial-pc-port-with-support-for-high-framerates-widescreen-ultrawide-gyro-aim-autosaves-and-low-input-lag/ **[S]** |
| **Myst / Myst III / Myst IV, Shenmue III** | Widescreen patches exist (engine-level). | No AI widescreen FMV found | n/a | **[S]**, search found nothing AI-specific |
| **Chrono Cross, Dark Forces, PS1/PS2 FMV widescreen** | Not found in my searches | n/a | I found no evidence of AI-outpainted FMV for these | **[U]** |

Honest takeaways:
- **No evidence found of any shipped project doing AI-outpainted widescreen for game FMVs.** The closest is Cosmos Limit Break (backgrounds and, by one secondary report, cutscene renders), and it is described as a heavy manual-curation effort. If a counterexample exists, my searches did not surface it.
- Fan consensus on AI in general: upscaling of FMV is welcomed; artefact categories reported are melting dark areas, smeared text, an overall "AI look", and softness.
- The lesson from C&C/Red Alert threads is that pillarboxed 4:3 FMV is a frequent complaint, so *some* widescreen-friendly fallback matters more than perfect AI.

---

## 4. Non-AI alternatives (all **[U]**: general techniques, no sources retrieved)

| Technique | How | Pros | Cons |
|---|---|---|---|
| **Pillarbox (black bars)** | Letterbox 4:3 inside 16:9 | Zero risk, authentic, fastest | Looks dated on 21:9 (very wide bars); the common complaint noted above |
| **Ambient blurred fill** | Scale a copy of the frame to cover the 16:9 canvas, heavy Gaussian blur, darken slightly, draw the sharp 4:3 on top. For video can be done in real time on the GPU at playback (no pre-processing and no derived files stored) or via ffmpeg `scale+gblur+overlay` | Looks intentional (like streaming "ambient mode"); trivial; deterministic; no model, no extra files; works for any FMV and any menu image; fits "generated locally" trivially because it is computed at render time | Obvious that the picture is still 4:3; blurry sidebars can flicker with fast scene brightness changes (mitigate with temporal smoothing) |
| **Mirror / edge reflect fill** | Mirror edge columns outward, optionally blurred | Preserves colour continuity | Visible symmetry artefacts, ugly with faces |
| **Edge stretch / smear** | Replicate edge pixels | Cheap | Streaky, unnatural |
| **Tiled / pattern extension** | For backgrounds that are texture-like (brick, tiles, noise) | Very good for tileable art | Only works for a few assets |
| **Cropping to 16:9 (cut top and bottom)** | Zoom 4:3 to 16:9 | Fills the screen | Loses 25% of the vertical content; heads and action cut off; poor for FMV |
| **Stretch** | Non-uniform | Fills screen | Distorts, widely disliked |
| **Artist-made side art** | Hand-painted bars/frames for menus, loading screens; ship as user-supplied mod pack | Best quality, and styled | Needs artist; assets must not be distributed in the repo unless fully original |
| **Hor+ for 3D** | Keep vertical FOV, widen horizontal FOV | Standard in recomps; no content invented, but possible culling/pop-in at edges (Zelda 64 recomp reports edge animation glitches **[S]**) | Needs engine work (culling, FOV, shadows, post effects, screen-space effects) |
| **Vector UI re-layout** | Anchor elements (left/right/centre), extend background layers | Crisp at any ratio, no AI | Requires understanding the Flash-style UI runtime; backgrounds that are bitmaps still need fill |

---

## 5. Recommendation matrix

"Local model" means generated on the user's machine from their own dump, matching the existing HD-texture pipeline. Quality/effort/risk are my judgement **[U]** unless stated.

| Content type | Recommended default | Optional / advanced | Expected quality | Effort | Risk | Fits "generated locally from user's files"? |
|---|---|---|---|---|---|---|
| **Real-time 3D scenes** | Render wider (Hor+) | n/a | High | Medium (culling, FOV, shadow/postfx checks) | Edge pop-in, per-effect bugs | Yes (runtime) |
| **In-engine cutscenes** | Hor+ render wider; test per scene; if camera framing breaks, offer letterbox/pillarbox toggle per scene | n/a | High to medium | Low to medium | Edge animation or culling issues (cf. Zelda 64 recomp **[S]**) | Yes (runtime) |
| **Pre-rendered FMV (.mad)** | **Blurred ambient fill computed at playback** (or pillarbox via a config toggle). Default = ambient fill | Opt-in "AI widescreen FMV" local batch tool: shot-split, Wan VACE (Apache-2.0) or LTX outpaint, composite source back, cache in a user-local folder; human review required | Ambient fill: fair (obviously 4:3 centre). AI: variable, good on environments, bad on close-ups/text | Ambient: low. AI: high (shot detection, chunking, review) plus 20 to 80 GPU-hours estimate **[U]** | AI: hallucinations, flicker, seams, heavy VRAM/disk, support burden | Ambient: yes. AI: yes if purely local, but strictly user-run and never distributed |
| **Static full-screen menu backgrounds (bitmaps, no text)** | Ambient blurred fill at runtime (default) | Opt-in local AI outpaint (Qwen-Image-Edit GGUF Apache-2.0 or FLUX.2 klein 4B) after HD upscale, composite original back, user reviews | AI: good for continuable backgrounds; fair elsewhere | AI: low to medium per image, but manual review needed | Seams, style drift, hallucinated figures | Yes |
| **Animated vector (Flash-style) menus** | Re-anchor/extend vector layers in the UI runtime; background layer filled via ambient or extended layer | AI-extended background plate if the background is a bitmap layer | High (vector scales cleanly) | Medium to high (needs UI runtime understanding) | Layout bugs in individual screens | Yes (runtime) |
| **Loading screens** | Ambient fill or pillarbox (usually short-lived on screen) | Opt-in AI outpaint of the plate (same as menu backgrounds) | Fair to good | Low | Text/logos in the image must be masked | Yes |
| **HUD / overlays (health bars, score)** | Keep anchored to 4:3 safe area or to screen edges per element; don't stretch | n/a | High | Low to medium | Zelda 64 recomp precedent: HUD stays 4:3 and ships that way **[S]** | Yes (runtime) |
| **Screen overlays / fixed full-screen textures (fades, vignettes, flashes)** | Scale to cover: stretch the overlay (they are low-detail by nature) or draw additional solid/blurred border | AI not needed | High | Low | Visible seams only for vignettes with hard edges | Yes |
| **Logos, text-heavy splash/title screens** | Pillarbox or centre on blurred fill. Never AI-outpaint | Hand-made border art as a user mod | n/a | Low | AI would damage text/logos **[S]** | Yes |
| **Textures (3D)** | No extension needed; HD texture pack as today | n/a | n/a | n/a | n/a | n/a |

Proposed architecture decision (for the project to confirm):
1. **Tier 0 (ships on by default):** pillarbox + runtime blurred ambient fill for every 2D full-screen element and FMV; HUD anchoring; Hor+ for 3D. No models, no network, no extra derived files. Compatible with all legal constraints because everything is computed at render time.
2. **Tier 1 (optional pack builder, same model as the HD pack):** a local script that downloads an Apache-2.0 model on demand (never bundled), reads the user's own extracted assets, writes outpainted stills (and optionally FMV) into a user-local override directory that the engine prefers over Tier 0. The repo ships only scripts and manifests; no input or output assets.
3. **Tier 2 (documented but unsupported):** hand-edited or cloud-service-generated replacements that the user drops into the same override folder. The project never hosts or links these.

Review gating: Tier 1 outputs must be reviewed by a human; a contact-sheet generator that shows each result at 1:1 around the seam and flags bad ones would be worth building before any batch run **[U]**.

---

## 6. Licensing cheat-sheet (for a public tool that only downloads models)

| Model / tool | Licence | Safe as default download in an open-source tool? |
|---|---|---|
| Wan 2.1 VACE, Wan 2.2 (base) | Apache-2.0 **[V]** | Yes. Verify the licence of whichever specific community conversion (GGUF / merged "Fun" checkpoints) the tool pulls; merges inherit base-model licences **[S]** |
| Wan 2.2 VACE-Fun A14B | Reported Apache-2.0 **[S]**, primary not verified | Verify before including |
| Qwen-Image-Edit (+2509/2511) | Apache-2.0 **[V]** | Yes (text-encoder and LoRA licences also need checking, e.g., Lightning LoRA **[U]**) |
| FLUX.2 [klein] 4B | Apache-2.0 **[S]** | Likely yes, verify on the HF card |
| FLUX.2 [klein] 9B, FLUX.1 Fill/Kontext [dev] | FLUX non-commercial licences **[V]**, gated downloads | No as default; at most user opt-in with explicit licence notice |
| SDXL 1.0 / SDXL-inpainting | CreativeML Open RAIL++-M **[S]**: commercial OK, use-based restrictions | Usually yes; confirm on Stability's card |
| SD 3.5 | Stability Community License, free under USD 1M revenue **[V]** | Conditional; hobby fine, still adds licence-tracking burden |
| LTX-2.x | "LTX Model License" / community licence, free under about USD 10M revenue **[S, conflicting]**, gated | Conditional; read the licence text |
| HunyuanVideo | Excludes EU, UK, South Korea **[S]** | No |
| ProPainter | NTU S-Lab 1.0, non-commercial **[V]** | No as a dependency; user opt-in only |
| M3DDM | Apache-2.0 **[V]** (but needs SD1.5 weights separately) | Possible but technically dated |
| Runway / Luma / Adobe / Veo | Proprietary hosted | Out of scope |

Notes: "commercial vs non-commercial" matters even for a free hobby tool if anyone could redistribute or monetise it; also gated Hugging Face models need users to log in and accept terms, which adds friction **[V for FLUX Fill]**.

---

## 7. Open questions and suggested validation (cheap, before committing)

1. Inventory the real FMVs: count, durations, resolution, which have text/logos, which have close-ups at the frame edge. Decides whether AI FMV outpaint is even worth testing. **[U]**
2. Take 5 static backgrounds (one dark, one with a crowd, one with a character, one with text, one plain) and compare: ambient fill vs. Qwen-Image-Edit Q5 GGUF vs. FLUX.2 klein 4B vs. SDXL inpaint, all after the existing upscale step, original composited back. Look at seam at 100%.
3. Take 3 FMV shots (static environment, slow pan, character close-up) and run Wan 2.1 VACE 14B (GGUF) at 480p. Measure time per chunk on the actual GPU, VRAM peak, and seam/flicker. Replaces my extrapolated numbers in section 2.2.
4. Confirm the Wan 2.2 VACE-Fun licence and any LoRA licences on primary pages.
5. Confirm what the engine's movie path (movie-display, `.mad` decode, audio clock) allows: can the player composite a blurred copy of the frame cheaply at playback, and can it load a replacement video? These are project-internal and I did not look.
6. Check how Flash-style UI scenes are structured (bitmap plates vs. vector). **[U]** Not investigated.

---

## 8. Source list (grouped)

Models and licences
- https://huggingface.co/Qwen/Qwen-Image-Edit-2511
- https://civitai.com/models/1934155/qwen-image-edit-fast-and-simple-outpainting
- https://huggingface.co/badgame/qwen-image-edit-gguf
- https://huggingface.co/black-forest-labs/FLUX.1-Fill-dev
- https://docs.comfy.org/tutorials/flux/flux-1-fill-dev
- https://civitai.com/models/1108146/fluxfill-devgguf
- https://bfl.ai/blog/flux-1-kontext-dev
- https://bfl.ai/blog/flux2-klein-towards-interactive-visual-intelligence
- https://help.bfl.ai/articles/8642316687-flux-2-klein-fast-generation-guide
- https://huggingface.co/stabilityai/stable-diffusion-3.5-large
- https://huggingface.co/docs/diffusers/v0.39.0/api/pipelines/stable_diffusion/stable_diffusion_xl
- https://github.com/ali-vilab/VACE
- https://github.com/Wan-Video/Wan2.2
- https://docs.comfy.org/tutorials/video/wan/vace
- https://www.comfyui.org/en/wan2-1-vace-video-outpainting-pipeline
- https://civitai.com/models/2524167
- https://huggingface.co/Kijai/WanVideo_comfy_GGUF
- https://docs.ltx.io/open-source-model/advanced-workflows/in-outpainting
- https://github.com/sczhou/ProPainter
- https://github.com/alimama-creative/M3DDM-Video-Outpainting
- https://eccv2024.ecva.net/virtual/2024/poster/2162 (Be-Your-Outpainter)
- https://arxiv.org/html/2506.22298v1 (OutDreamer), https://arxiv.org/html/2511.03272v1 (long-video co-denoising)
- https://huggingface.co/tencent/HunyuanVideo/commit/a4ed25a99d7eb9cf21e12f5fd8052d266f2105fc (territory clause)

Hosted services
- https://docs.agents.lumalabs.ai/guides/videos/reframing
- https://lumalabs.ai/changelog/reframe-is-here
- https://help.runwayml.com/hc/en-us/articles/34926355398675
- https://helpx.adobe.com/lv/premiere/desktop/edit-projects/edit-with-generative-ai/generative-extend-faq.html
- https://helpx.adobe.com/camera-raw/using/generative-expand.html

Workflow guidance
- https://docs.comfy.org/tutorials/basic/outpaint
- https://www.comfyui.org/en/beyond-the-frame-flux-model-image-outpainting
- https://www.runcomfy.com/comfyui-workflows/flux-outpainting-flawlessly-aligned-with-original-image

Fan and mod precedent
- https://www.dexerto.com/gaming/modder-remasters-final-fantasy-7-using-ai-were-not-sure-how-to-feel-2476340/
- https://exputer.com/news/games/final-fantasy-7-mods-camera-overhaul/
- https://en.wikipedia.org/wiki/Moguri_Mod
- https://www.pcgamesn.com/resident-evil/seamless-hd
- https://www.thegamer.com/command-conquer-remastered-ai-cutscenes/
- https://steamcommunity.com/app/2229850/discussions/0/4362373720224465124/
- https://www.nexusmods.com/aliensdarkdescent/mods/38
- https://www.dsogaming.com/news/the-legend-of-zelda-majoras-mask-just-got-an-unofficial-pc-port-with-support-for-high-framerates-widescreen-ultrawide-gyro-aim-autosaves-and-low-input-lag/

Caveats on this report: several sources were retrieved through summarising tools and search snippets, not read in full; the Nexus mod page for Cosmos Limit Break returned HTTP 403, so its claims rest on press coverage; the speed figures in section 2.2 are my extrapolation from the ComfyUI page's 4090 timings; no model was run; model landscape moves quickly, so re-check versions and licences before implementation.

# HD textures: Windows support and local workflow

The v0.6.0 candidate adds optional Windows Direct3D 11 texture packs and local
Lanczos 4x generation. HD textures on Vulkan/macOS/Linux, widescreen, vector fonts
and HD movies remain follow-ups. See `research/hd-textures-notes.md` for evidence and gaps.

## Generate the faithful pack with Setup

Run `DefJamSetup-0.6.0-windows-x64.exe` with your supported USA Xbox dump and
select **Apply HD texture upscale during install (increases install time)**.
The option starts unchecked. Setup generates the textures on your CPU after
building the game, then enables the generated packs for the first launch.
No GPU, neural model, downloaded artwork or modified game archives are needed.

The supported static corpus contains **18,971 unique images**. Its HD PNGs total
**6.21 GB (5.79 GiB)**; reusable originals/previews/metadata add about 0.65 GB.
Setup requires **15 GiB additional free space** when this option is selected,
covering work files and filesystems that cannot hard-link the generated PNGs.
Generation took about four minutes on the development PC; game compilation and
texture verification add time, and other CPUs may take longer.

In the launcher or F1 overlay, open **Textures** to disable HD packs, change the
pack list or adjust the cache. Restart after changes. Leave the generated names
in place to use both opacity and material-channel mip policies.

Close the game and rerun Setup with the same locations to update or repair.
Leaving the HD checkbox unchecked on an existing installation preserves its
current texture settings. Setup retains older caches/packs and the review data;
removing Setup preserves your data folder. For changes to generated art, create
a separate overriding pack rather than edit the immutable generated files.

One unsupported static encoding is retained as original, and the bounded census
does not prove that large audio archives contain no images. Dynamic text, movies
and unsupported render resources also retain their original path. The corpus
count is not a claim that every resource seen by the renderer is replaceable.

For manual generation, review galleries and future processing experiments, see
[the reproducible workflow](hd-texture-workflow.md).

## Install and enable a pack

Put the pack beneath the data folder, then use the launcher's or overlay's **Textures** page:

```text
<data>/mods/faithful-hd/manifest.ini
<data>/mods/faithful-hd/textures/<source-id>.png
```

Turn **Use HD texture packs** on, enter `faithful-hd` under **Pack folders**, and restart the game.
Multiple names use semicolons; the last listed pack wins. Turn packs off and restart to restore original
textures. A missing, unsupported or malformed replacement falls back to the original with a bounded log.
Explicit `RECOMP_TEXTURE_PACKS=faithful-hd` enables that pack for a diagnostic run. `RECOMP_TEXTURE_ROOT`
can override the default `<data>/mods` directory. Use packs with the game closed while rebuilding them.

```ini
[pack]
schema=1
name=Faithful HD
[textures]
<64 lowercase hexadecimal source-id>=textures/<source-id>.png
```

Names and relative paths use ASCII letters, digits, `_`, `-`, `.`, and forward slashes for paths.
Absolute paths and `..` are rejected. Paths must stay within their named pack. PNG replacements use
straight alpha and a uniform integer scale from 1x through 8x. Their visible dimensions must match the
original aspect and atlas layout. The renderer retains original guest dimensions and coordinates;
linear row padding is reproduced on the host. PNG mips use alpha-weighted colour reduction. Data,
additive and alpha-test masks need separately reviewed processing; the generic opacity mip recipe is
not a claim of correct coverage for every material.

Optional `[pack] png_mips=channels` filters RGB and alpha independently when generating PNG mips.
The default, `png_mips=opacity`, retains alpha-weighted RGB reduction. Use independent channels for
reviewed material/data alpha rather than interpreting it as transparency; the loader does not identify
alpha semantics automatically. Unknown policy names reject the pack. DDS supplied mips are unaffected.
The batch recipe's top-level `png_mips` field retains this policy in the generated manifest.

Classic 2D DDS with BC1/DXT1, BC2/DXT3 or BC3/DXT5 is also accepted, with validated supplied mip levels.
BC7, DX10 headers, arrays, cubes and volumes are unsupported. DDS replacements for guest textures with
row padding currently fall back. Use PNG to keep alpha exact during review; use DirectXTex `texconv`
only after choosing suitable compression/mips per material. No automatic sRGB format change is made.

The cache defaults to 512 MiB, accounting for retained upload data and GPU texture bytes. It protects
textures bound on all four stages when evicting; temporary pressure falls back and retries. Cube maps,
YUV/movie and depth formats and tracked render surfaces stay on their original path. Generated text
is not approved for static replacement. Restart is required to reload manifests or edited files.

## Texture identities and lossless capture

Enable **Capture source textures**, restart, visit the relevant screens/venues, then turn capture off.
The dump contains PNGs and `textures.jsonl` under `<data>/hd-work/runtime`. It preserves alpha and records
source IDs, dimensions, palette size and texture stage. `RECOMP_TEXTURE_DUMP_DIR` overrides the destination;
`RECOMP_TEXTURE_DUMP_LIMIT` limits new files per run. Capture is diagnostic work, so performance measurements
must run with capture off. Never commit the dump or derived artwork.

The identity is SHA-256 over `XRTEX01\0`, then five little-endian uint32 values: format, visible width,
height, palette entry count, row texel width; then complete base-level source blocks/texels and full BGRA
palette. Linear rows omit unused pitch padding. Addresses are excluded. Source rechecks are limited to
once per 8 ms per cached resource; this is not an immediate guest-write notification. A second decoded
identity uses format 0x12, tight BGRA pixels and no palette for comparing archive/runtime observations.
The native fixture shares a known-answer vector with Python, and checks mutations outside the old sampled
signature as well as palette changes and address independence.

## Local automated workflow

For local HD captures, `RECOMP_TRANS_SHOT_NATIVE=1` writes actual rendered pixels instead of the
standard 640x480 regression capture. At render scale 2/4 these are 1280x960/2560x1920 respectively.
The default capture path and existing goldens stay at 640x480. Capture work is excluded from performance
measurements.

Use an isolated Python environment with Pillow. This machine's environment is
`C:/Users/Vlad/code/defjam-hd-data/tools/python`; it does not change the project Python installation.
The portable official Real-ESRGAN NCNN Vulkan executable uses the local NVIDIA GPU without a CUDA/PyTorch
installation. It and both model pairs live under `<data>/tools/realesrgan`. Reproduction downloads and
hashes are in the research notes. chaiNNer is an optional GUI for auditioning and editing recipes.

```powershell
$hdData = 'C:\Users\Vlad\code\defjam-hd-data'
$hdPython = "$hdData\tools\python\Scripts\python.exe"
$hdGpu = "$hdData\tools\realesrgan\realesrgan-ncnn-vulkan.exe"

# Full metadata census; export a bounded selection for inspection.
& $hdPython scripts/hd_assets.py inventory --root "$hdData\extracted" --output "$hdData\hd-work" --select load0 --select pause --limit 50

# Build grouped, resumable jobs from observed runtime IDs.
& $hdPython scripts/hd_assets.py batch --runtime "$hdData\hd-work\runtime" --inventory "$hdData\hd-work\inventory.json" --recipes config/hd-recipes.example.json --output "$hdData\mods\faithful-hd" --tool $hdGpu
```

Recipes contain category defaults and individual overrides. The example approves only three observed UI
assets for a pilot; unapproved/unknown captures go to `review.json`. Category classification by archive
name is a suggestion, not evidence of material/alpha semantics. Approve category defaults after reviewing
representative assets; retain individual exclusions and overrides. Source archive IDs are provisional:
the game may transform pixels or palettes before upload. Runtime IDs are authoritative for replacement.

Each job splits RGB and alpha, optionally extends colour into fully transparent pixels, pads eight source
pixels by default, runs RGB inference at 4x, crops 32 output pixels, optionally reduces to 2x, then recombines
separately resized alpha. Edge padding is for clamped axes; wrapping must be confirmed per axis. `alpha=nearest`
is available for masks that require it. Inference uses one worker and 128px tiles; inspect tile boundaries.
`build.json` records source/output hashes, model and executable hashes, runner hash, recipe and job time.
Unchanged outputs resume; changed source, recipe, implementation or output invalidates the job.

| Category | Initial direction | Review |
|---|---|---|
| Logos and branded lettering | Conventional 4x reference; original vectors where available | Shapes, spelling, intentional wear |
| UI/HUD/icons | Conservative 4x RGB, opacity handled separately | Atlas gutters, halos, layout |
| Fighters/clothes/tattoos | Compare 2x/4x conservative models | Faces and original designs; no face restoration |
| Create-player customization | Separate shared/body/clothing atlas review | Preserve part/colour variants and UV metrics |
| Crowds/cinematic variants | Keep separate provenance and recipes | Consistent appearance across variants |
| Venues/props | Start 2x; selected 4x | Actual wrap axes, seams, distance shimmer |
| Particles/decals/masks | Separate material recipes or passthrough | Blend intensity, alpha-test coverage, channel meaning |
| Animated textures | Same recipe across the whole sequence | Timing and temporal consistency |
| Fonts | Original TTF outlines first; inspect FntX bitmap atlases | Metrics, kerning, counters; dynamic text remains live |
| Video | Separate extraction and short-clip pilot | Cadence, aspect, flicker, audio |

`v2ip.viv` holds named fighter atlases (for example House=`V2IP_003A`, D-Mob=`V2IP_014A/B` in
fighter.xml). `v2cp_*.viv` is create-player customization, proven by createplayer_parts.xml;
the generic `V2CP_A_A/cp_a` atlas is not established as a face texture. `icrowd/cincrowd/v2cm`
are crowd sources. `v2bg` includes fight venues and Story/shop interiors; `V_ENV` environment
maps need distinct handling. Full archive provenance takes precedence over embedded texture names.

For fonts that exist only as bitmaps, compare 8x smoothing, threshold/Potrace outlines and 4x Cairo rasterizing.
Do not trace existing TTF outlines. Preserve each glyph's original metrics and atlas placements, and convert
Cairo's premultiplied output to straight alpha. Font runtime integration remains a separate investigation.

## Representative comparison

For the 40-image visual audition, see `research/hd-representative-review.md` and
`scripts/hd_review.py`. Its local gallery compares original, Lanczos, RealESRNet
and RealESRGAN at 2x/4x and exports independent choices at each scale. This is a
review corpus; runtime correlation and in-game world acceptance remain required.

The first Lanczos in-game pilot covers Blaze, 22 opaque Foundation venue textures and three loading
assets, with authoritative runtime IDs and an explicit independent-channel mip policy. The local pack
is `<data>/mods/lanczos-test`; details and run evidence are in `research/hd-lanczos-game-pilot.md`.
It is a bounded test pack, not full-game asset coverage or release acceptance. User requested Lanczos
tests now and deferred deeper upscaler research/review.

## Movies and original fonts

```powershell
& $hdPython scripts/hd_assets.py export --root "$hdData\extracted" --output "$hdData\video-scratch\originals" --kind video --limit 3
& $hdPython scripts/hd_assets.py export --root "$hdData\extracted" --output "$hdData\hd-work\fonts" --kind fonts --limit 24
```

These export raw original files by hash with aliases, not decoded frames. FFmpeg supports EA MAD;
verify a local build with `ffprobe -show_streams -show_format -of json <clip.mad>`. Preserve timestamps,
aspect and original audio. Decode only a short segment for the pilot, for example with `ffmpeg -i <clip.mad>
-t 15 -map 0:v:0 -c:v ffv1 <pilot.mkv>`, and separately retain audio when present. Inspect field order before
deinterlacing. Do not interpolate or change the original frame rate. Video2X's Real-ESRGAN Vulkan path is
a local candidate for streamed processing; inspect motion/fades/cuts for flicker. Temporal models are a
later comparison. Process one clip/chunk at a time to bound scratch use.

HD movie playback is deferred. Renaming an upscaled MP4 to MAD will not work: integration must preserve
the game's timing, skips, loops and audio while presenting host-decoded frames. APT screen timelines are
UI assets rather than these prerecorded videos. No movie decoder or host playback change is implemented here.


## Full Lanczos corpus gallery

Owner selected Lanczos after the SwinIR audition. `scripts/hd_corpus.py build`
processes the supported static census at4x and retains all source aliases;
`serve --output <local-corpus-root> --port 8767` provides searchable categories,
original comparisons and locally saved concern flags/notes. See
`research/hd-full-lanczos-corpus.md` for full commands, counts and verification.
The generated gallery and all images/reviews stay in the data root. Runtime
correlation and remaining release gates precede installation of the whole corpus.


Owner accepts the full static Lanczos gallery (D91). For exact storage, proposed
source-only local generation and remaining runtime assembly steps, see
[release packaging](research/hd-texture-release-packaging.md). Reusable commands,
model experiments and saved-review behavior are in [workflow guide](hd-texture-workflow.md).

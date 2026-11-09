# HD textures: support and local workflow

New to making packs? Start with [Texture Modding — Getting Started](texture-modding-getting-started.md)
for a capture-to-pack walkthrough. This page is the format and processing reference.

v0.6.0 added optional Windows Direct3D 11 texture packs and local
Lanczos 4x generation; v0.6.1 draws the same packs through Vulkan on macOS
(Apple Silicon), and v0.7.2 offers them in the Linux setup (the Steam Deck included),
whose Vulkan renderer is the macOS one. Compatible menu packs add optional
wide background extensions when a compatible pack is loaded; vector fonts and HD
movies remain follow-ups. See [the HD acceptance record](research/hd-v060-release-validation.md) for dated evidence and coverage limits.

A pack is named by the contents of the original texture, not by the renderer, so the
packs Setup makes on a Mac or on Linux are the same files (18,971 images, 6,212,168,677
bytes) as on Windows, and a pack made on one can be used on the others.

## Generate the faithful pack with Setup

Run the Windows setup (`DefJamSetup-<version>-windows-x64.exe`), the macOS setup
(`DefJamSetup-<version>-macos-arm64.dmg`, v0.6.1 or newer) or the Linux setup
(`DefJamSetup-<version>-linux-x64.tar.gz`, v0.7.2 to v0.13.0; since 1.0.0 the Linux app) with your supported USA Xbox
dump and select **Apply HD texture upscale during install (increases install time)**.
The option starts unchecked. Setup generates the textures on your CPU after
building the game, then enables the generated packs for the first launch.
No GPU, neural model, downloaded artwork or modified game archives are needed.

The supported static corpus contains **18,971 unique images**. Its HD PNGs total
**6.21 GB (5.79 GiB)**; reusable originals/previews/metadata add about 0.65 GB.
Setup requires **15 GiB additional free space** when this option is selected,
covering work files and filesystems that cannot hard-link the generated PNGs.
Generation took about four minutes on the development PC; game compilation and
texture verification add time, and other CPUs may take longer. On a Steam Deck the upscale
takes about ten minutes, and the whole install with it about 15.

Replacement decoding/upload can add work when a screen first appears. The runtime
uses background preparation where supported and displays originals until replacements
are ready. Historical synchronous-load timings are not current performance guarantees;
measure the exact build, packs, cache, venue and display settings.

In the launcher or F1 overlay (Windows, macOS since v0.6.2, Linux since v0.7.0), open **Mods** to disable HD
packs, change the pack list or adjust the cache. Restart after changes. Or edit `[textures]` in
`settings.ini` in your data folder (`enabled=0` turns the packs off, `enabled=1` on; `packs` and
`cache_mb` are the pack list and memory budget; the [settings reference](settings-reference.md) lists
them). Leave the generated names in place to use both opacity and material-channel mip policies.

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

Put the pack beneath the data folder, then use the launcher's or overlay's **Mods** page:

```text
<data>/mods/faithful-hd/manifest.ini
<data>/mods/faithful-hd/textures/<source-id>.png
```

Choose **Refresh local mods**, turn **Use HD texture packs** on, enable the pack and restart the game.
The current **Load Order…** window shows highest priority at the top; use **Higher** to make a pack
override matching textures in the packs below it. For manual configuration, **Pack folders** is under
**Memory and manual pack list** and uses the stored order described below.
Multiple names use semicolons; the last listed pack wins. The list holds up to 1023 characters; a longer
one is reported in the log as a `[SETTINGS]` line and no pack loads until it is shortened. (Up to v0.10.5
the limit was 95 characters, which a setup install with one added pack already passed.) Turn packs off and restart to restore original
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

The cache defaults to 2048 MiB, accounting for retained upload data and GPU texture bytes. It protects
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

## Authoring and comparison tools

Use [DefJam Modding Tools](https://github.com/vyanhursky/DefJam-Modding-Tools)
for extraction, atlas review, original/Faithful/restored comparisons and optional
upscale experiments. Its `defjam-tools assets`, `corpus`, `audition` and `swinir`
commands replace the former authoring scripts in this repository. Keep recipes,
corpora, generated candidates and review state in the private workspace. See the
[workflow boundary](hd-texture-workflow.md) and the tools' command help.

## Movies and original fonts

```powershell
$hdPython = "python"
$hdData = "D:\DefJamData" # your existing data folder
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


## Corpus and acceptance evidence

General extraction/review galleries and optional processing live in
[Modding Tools](https://github.com/vyanhursky/DefJam-Modding-Tools).
The game keeps its frozen Setup generator and runtime aliases for compatibility;
do not replace that snapshot as part of authoring cleanup. The
[v0.6 acceptance record](research/hd-v060-release-validation.md) distinguishes
prototype controls, static gallery review and later packaged-install acceptance.
Generated images, galleries and authoring history remain private/local.

## Download independent menu mods through the launcher

The signed project catalogue is available in Mods. Refresh the catalogue,
download/install a compatible version, then enable the pack and give it higher
priority than Recomp HD in Load Order. Testing versions require an explicit opt-in.
The menu pack is separate; Faithful generation stays optional. No manual mod ZIP extraction
is needed. See [mod management](mod-manager.md) and the
[original integration acceptance record](research/integrated-menu-launcher-rc-2026-10-23.md).
That record describes the initial RC; current release behavior is documented above.


## Wide-layer authoring contract

A schema-1 pack may combine `[textures]` replacements with `[wide]` side slabs/stretch registrations. The game's foreground/layout and source identities remain live. Slabs must match their native center, sampled UV rectangle, alpha semantics and draw placement; preserve original padding and geometry. Manifest slab offsets are integer source texels: fractional source UV spans must be accounted for without emitting fractional offsets. Native acceptance covers transitions, masks, scrolling, overlays and each supported aspect. Title and Mode Select lighting require native checks independently of artwork approval. Authoring/export procedures belong in [Modding Tools](https://github.com/vyanhursky/DefJam-Modding-Tools).


## Runtime correlation and immutable generated packs

Archive identities alone do not establish runtime coverage: the guest may transform pixels, format, palette or row width. Correlation uses full decoded RGBA, including alpha, and the five-field descriptor above; legacy four-field census IDs must not be installed as verified runtime keys. One accepted PNG can serve several independently verified runtime IDs. The frozen Setup mapping adds 13 exact runtime aliases to canonical source keys, producing 18,984 manifest entries from 18,971 PNGs; this bounded mapping is not exhaustive renderer coverage. Keep that snapshot and its runtime aliases unchanged until an explicitly tested pin update.

Setup's generated opacity and material-channel packs use `faithful-hd-<recipe-id-prefix>-opacity` and `...-channels`. A changed processing source revision changes the recipe prefix. The builder verifies input/code recipes, dimensions and output hashes, then publishes owned manifests atomically. Hard links avoid a second physical PNG copy where supported; copying is the fallback. Cache repair publishes a new PNG path rather than modifying a shared hard-link inode. A modified installed PNG is preserved and assembly rejects the mismatch; author manual changes in a separate overriding pack.

Measured content bytes in the original review corpus were 6,212,168,677 HD PNGs, 525,848,032 decoded originals, 79,256,057 thumbnails, 20,331,619 recipe/hash records and 25,188,620 gallery/review metadata: 6,862,793,005 bytes total. These are file-content sizes, not allocation, compressed downloads or VRAM requirements. The initial prototype budget was 512 MiB; the current default is documented above. No full compression trial established a download-size saving. The accepted PNG baseline does not certify lossy DDS conversion or every alpha-test/additive/wrap/mip material.

The source-only local-generation policy keeps original archives intact and distributes neither derived artwork nor a compiled game. Authoring, model experiments and private review state now belong in [Modding Tools](https://github.com/vyanhursky/DefJam-Modding-Tools); [v0.6.0 validation](research/hd-v060-release-validation.md) retains the prototype controls, artwork decision and packaged acceptance limits.


## Source shape and identity checks

A wider PNG alone cannot extend a menu: uniform 1×–8× scale validation preserves source aspect, while APT triangles/UVs, crop/padding, placement and scissor remain independent. One source identity selects one replacement, without an aspect-variant selector. Extend verified logical layers through supported wide metadata rather than relaxing source-shape validation or globally stretching all large 2Ddraws. Preserve each layer's alpha and animation, with separate masks/text/logos. The 2026-10-17 audit found only 13 bounded exact-RGBA runtime aliases; archive names/hashes were not automatically runtime IDs because palettes/swizzles may transform before binding. That count was a dated pilot scope, not a completeness guarantee. Establish new mappings through runtime capture metadata, decoded-pixel matching and reviewed aliases.

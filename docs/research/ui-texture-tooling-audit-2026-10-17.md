# Menu/UI texture authoring tooling audit

2026-10-17. Read-only source audit for Vlad's menu restoration and widescreen
investigation. No artwork generation, game run, build, import, review mutation,
pack export, installation or server restart was performed. This report is the
only file written by this bounded investigation.

## Finding

The existing **standalone DefJam-Modding-Tools workshop** already supports 2D
menu/UI restoration comparisons and arbitrary generated PNG imports. Its fighter
model viewer is optional and is not needed to preview a menu atlas. Use that
workshop for the restoration pilot; do not extend the frozen authoring copies
in the recomp repository.

**Restoration and widening are separate technical tasks.** A restored texture
at the same aspect and atlas layout can replace its source through the existing
loader. An outpainted 16:9 image cannot replace a square/4:3 texture through the
current import or uniform-scale loader contract. The workshop has no aspect
variant selector, whole-menu compositor, or widescreen side-art export contract.
The widescreen design mentions future side-art packs, but its work-package
status calls AI side art a to-do. Do not mistake that design for a usable path.

## Current project and private data

Authoring source: `/Users/vlad/Code/DefJam-Modding-Tools`, established separately
from the recomp on 2026-10-16. Its `AGENTS.md`, `PROGRESS.md`, current workflow,
workshop implementation and resources were read. `local.json` resolves:

| Role | Local path |
|---|---|
| Own extracted dump | `/Users/vlad/Code/defjam-data/extracted` |
| Retained faithful corpus | `/Users/vlad/Library/Application Support/DefJamPR19-20/data/hd-work/setup-lanczos/b9aa783f1d66d41be59fb97ba25fb86e1e9f73d8ee041263b0ff5df85ceb26f0` |
| Persistent review workspace | `/Users/vlad/Code/defjam-data/hd-work/texture-workshop` |
| Browser origin | `http://127.0.0.1:8768` |

The private workspace already carries fighter candidates, reviews and saved
views. Reuse it read/write only through its existing server. Preserve the
single writer, browser origin and unsaved browser drafts. Do not instantiate
another offline writer while it is live. A separate UI investigation page may
be added later without resetting its fighter session.

## Capability matrix

| Existing tool | Menu/UI use | Boundary |
|---|---|---|
| Standalone `defjam-tools serve` / `texture_workshop.py` | Full corpus, category/provenance search, original + faithful + two candidate lanes | Initial default scope is the three-fighter pilot: choose **Full texture library**, then UI/menu category or search an archive name |
| Workshop 2D browser | Shared pan/zoom, Fit/25/50/100/200/400%, RGB/alpha, checker/light/dark, hold Original, full PNG links | Displays one source atlas at a time, not an assembled APT menu or a target aspect ratio. Candidate lanes are fixed by style, so two Rich Original Detail revisions cannot be shown together without a small UI extension |
| Workshop revision history | Any RGB/RGBA PNG can be imported as a new immutable revision; exact prompt, hashes, parent and request linkage retained | Full source dimensions at uniform integer 1–8x, maximum side 16,384; raw import maximum 24 MiB |
| Workshop technical assembly | Same-aspect size normalization, faithful alpha restoration, exact protected rectangles, parent retained outside explicit edit rectangles | Protection annotations alone do not alter pixels; actual assembly options must be used. Moved source islands cannot be fixed by resizing |
| Workshop exports | Approved sparse schema-1 packs, per-source chosen revision, bounded runtime aliases, separate opacity/channels mip policies, hash receipt | Export never installs or enables; game verification is separate. One selected revision per source ID, no ratio routing |
| Legacy `hd_assets.py` / standalone `assets` | Metadata inventory, bounded lossless original selection/export, runtime capture batch recipes | Source/archive ID is provisional until matched to actual runtime; unknown alpha semantics remain a review item |
| Legacy `hd_review.py` / standalone `audition` | Four synchronized comparisons with original and three named upscalers at 2x/4x | Models are hard-coded Lanczos/RealESRNet/RealESRGAN; not a general imagegen import UI; sheet rendering assumes Windows Arial |
| Legacy `hd_corpus.py` / standalone `corpus` | Original versus retained Lanczos corpus, aliases, channels, durable concern notes | No install manifest and no candidate authoring; build with a `selection` still builds the entire supported census |
| Recomp `build-hd-pack.py` | Immutable faithful 4x corpus assembly for Setup | Validates retained output hashes and fixed recipe; not a custom generated art importer |
| Fighter 3D model viewer | No dependency for menus | Limited to three static fighter meshes; cannot compose menu APT layers |

The current workshop has some fighter-specific wording: automatic briefs mention
faces, tattoos and UV islands, its approval gate includes `face_reviewed`, and
the exported manifest name is `Restored fighter pilot`. This does not block
2D source assets. A small later authoring change could make labels/briefs
appropriate to UI. Record “no face present” explicitly when reviewing an
ordinary menu background; do not use the checkbox to imply a face review that
was not performed.

## Imagegen and technical fidelity

The workshop queues briefs for Codex; it never calls an image API itself.
New art should use the existing **Rich original detail** direction. Historical
stylized drafts must remain available, but that lane is not a reason to create
new stylized UI redesigns without owner direction.

For a background pilot, supply the actual decoded original atlas and faithful
enlargement, with a game capture only as composition/reference context. The
capture may combine text, vectors, masks and several textures; restoring a
whole screenshot and importing it as one atlas would conflate those resources.
Keep menu lettering/logos exact. A baked text/logo region can be protected from
faithful HD rather than asking imagegen to regenerate its spelling or geometry.
Do not assume every transparent area is visible opacity; RGB beneath zero
alpha and material/data channels can matter.

Use the existing assembly path to restore the complete approved alpha and
protected text/logo boxes. Region annotations are prompts only. For exact pixel
protection, use `--assemble-scale`, `--protect-region` and, for a child draft,
`--parent` with `--edit-region`. The built-in imagegen path does not expose a
pixel-lock mask API, so no prompt is a technical guarantee.

Treat gutters, tile seams, repeated edges, atlas registration, mip behavior and
animated related frames as review criteria. Menu textures are not necessarily
full-screen 4:3 images: an original square atlas can contain several pieces or
be drawn with a nontrivial APT transform. Preserve its source layout for the
first restoration pass and confirm the composed result in game.

## Runtime identities and pack priority

The source identity is not a path or texture name. It hashes the XRTEX01 tag,
format, visible width/height, palette count, row texel width and complete source
pixels/palette. Decoded RGBA/BGRA identity allows exact archive/runtime matches
when original encodings differ. Addresses are excluded.

The game can transform archive pixels or palettes before upload. Its captured
runtime key is authoritative. The checked alias map contains 13 exact bounded
correlations, not all menu resources. The workshop export includes aliases
automatically where its packaged map knows them. Verify each pilot's runtime
source ID or exact decoded full-RGBA match before claiming it will replace.
Replacing one content ID affects every use with that content: retain all source
aliases and check shared pieces across screens.

The loader contract described in `docs/hd-textures.md` accepts straight-alpha PNG
at uniform integer 1–8x and keeps original guest coordinates/dimensions. It also
supports limited classic BC1/2/3 DDS, but PNG is the suitable review format.
Changing PNG dimensions nonuniformly is not a widening mechanism.

Pack list order is significant: **last listed wins**. List faithful HD first
and the sparse UI test pack last. Missing keys inherit earlier packs. A malformed
winning replacement falls back to the console original, not necessarily the
earlier faithful image. Restart after edited files or manifest/order changes.
Setup repair/regeneration can change pack priority, so recheck the list.

## Exact commands for a later authorized pilot

These commands are documented/source-verified, not executed in this audit.
Run from `/Users/vlad/Code/DefJam-Modding-Tools`; `local.json` supplies the
retained paths. Check current server ownership and unsaved notes before any
restart. If the server is live, use it without starting a second one.

```sh
# Only when the existing server is absent and its review state is saved:
.venv/bin/defjam-tools serve

# Import a generated same-aspect UI draft through the live writer.
# Choose SOURCE_ID and protection boxes after inspecting the exact source.
.venv/bin/defjam-tools workshop import \
  --server http://127.0.0.1:8768 \
  --asset SOURCE_ID --image /private/path/raw-menu-draft.png \
  --label "Menu background restoration 1" --style faithful \
  --prompt-file /private/path/exact-prompt.txt --assemble-scale 4

# For a targeted child, also supply --parent REVISION_ID and one or more
# --edit-region x,y,width,height rectangles. Add --protect-region rectangles
# for exact faithful copies of lettering/logos or other protected pieces.

# After owner atlas review, channel/mip review and explicit selection:
.venv/bin/defjam-tools workshop export --server http://127.0.0.1:8768
```

Normalized rectangles are `x,y,width,height` in [0,1]. `--assemble-scale 4`
preserves baseline alpha automatically. It accepts a different generation
resolution only when the source aspect is identical. A 16:9 raw draft for a
square/4:3 source is deliberately rejected.

The export lands under the private `workspace/exports/restored-...` directory.
Copy only the resulting component pack folders into the test installation's
`mods/`. Use a disposable settings/save root for game captures; keep the owner's
normal settings and saves intact. Enable components last via Textures or a
diagnostic `RECOMP_TEXTURE_ROOT` plus semicolon-separated `RECOMP_TEXTURE_PACKS`.
Do not mark `Verified in game` until the actual run has evidence.

## Suggested sequence and missing work

1. Inventory and visually map the first title/Press Start and main-mode-select
   screens to actual source IDs, APT bitmap layers and shared aliases. Distinguish
   backgrounds from dynamic text/vector elements, transparent masks, thumbnails
   and animated families. Confirm loader keys with a bounded runtime capture.
2. Start with 2–3 background source images, original geometry intact. Compare
   source, accepted faithful HD and one Rich Original Detail proposal at full
   resolution and actual screen size. Preserve lettering, silhouettes and alpha.
3. Import through the existing workshop, retain every draft/raw output/prompt,
   and review before exporting a sparse local restoration pack. Test original
   4:3 first, then 16:9 and 16:10 with current widescreen behavior.
4. Design side extension as a separate feature: source/screen identity,
   aspect/crop policy, fixed central 4:3 region, outpaint provenance and a
   composition preview. Extend the authoring workshop and the recomp runtime
   deliberately. Do not relax uniform atlas scale to smuggle a wide image into
   an incompatible replacement slot.
5. After first-screen acceptance, inventory screen families by shared art and
   animation rather than counting every UI atlas as a distinct background.

Open owner choices for the coordinating agent: strict original palette/grit or
a specified restoration latitude; whether new side regions may invent scenery;
which widest aspect the authored master should cover; exact treatment of baked
lettering/logos; and whether a single wide master with centered aspect crops or
separate art variants is preferred. The renderer's layer mapping must be
established before promising either export shape.

## Source anchors

- Standalone `docs/texture-workshop.md`, `docs/WORKFLOW.md`, `AGENTS.md`,
  `PROGRESS.md`, and historical `docs/research/imagegen-texture-restoration-plan.md`.
- Standalone `src/defjam_tools/texture_workshop.py`: `brief`,
  `assemble_generated`, `Workshop.catalog`, `import_candidate`, `export_pack`.
- Standalone `src/defjam_tools/resources/texture_workshop.js` and `.html`:
  four 2D lanes, persistent view state, candidate selectors and review controls.
- Standalone `src/defjam_tools/cli.py`: local profile/default argument handling.
- Recomp `docs/hd-textures.md`, `docs/hd-texture-workflow.md`,
  `scripts/hd_assets.py`, `scripts/hd_review.py`, `scripts/hd_corpus.py` and
  `.html`, `scripts/build-hd-pack.py`, `config/hd-runtime-aliases.json`.
- Recomp `docs/widescreen/02-design.md` and `04-work-packages.md`: planned
  side-art contract, distinct from the currently implemented source replacement.

# Menu/UI modpack implementation handover

Current checkpoint — 2026-10-20 (D182–D184/UI): a **separate menu-only RC2** is built
and being tested on this Mac. Faithful HD is optional and earlier-loaded; its library
is not bundled. Download421.6MB / installed423.4MB,157nativePNGs/164sideslabs.
Read [the current release-candidate report](research/menu-ui-separate-release-candidate-2026-10-20.md)
and PROGRESS.md §7 for exact artifact locations, hashes, commands and test coverage.

The actual pre-fight loader uses four `match/0_bg/paper_*` resources (eight IDs),
registered separately from the older approved `battle/matchLoa` preview. All670
approved/conditional art pins remain intact; no new generation was needed.
The transition has an inherited black first-tile rectangle, also reproduced with
Faithful alone; it remains a release issue. Appropriate Story saves, native visual
signoff and final checks remain. Other panel/detail/shop candidates stay held.
[Hosting discussion](research/menu-modpack-hosting-pr17-2026-10-20.md) follows PR17's
small catalogue / separate external artwork download architecture.

The October19 pause and earlier engineering notes below are historical, superseded
by the current report and D161/UI's restoration resume. Artwork stays local.

---

Owner: Vlad. Handover: 2026-10-19. **Texture restoration is PAUSED at Vlad's
request until he has more image-generation credits. Do not generate, repaint or
advance the remaining-art queue.** The next task is engineering: take the
already reviewed artwork, produce a local modpack, and let Vlad playtest it.
This session has not installed a menu-art modpack or validated these images in game.
**Update 2026-10-19 (D158/UI–D160/UI):** source-shaped pack, logo/emblem registration and true
widescreen backgrounds are built and checked on the Mac; see [the report](research/menu-ui-playtest-pack-2026-10-19.md).

## Start here

1. Read the repository's `AGENTS.md` and current `PROGRESS.md`. Work from current
   game `origin/main`, not this session's original v0.9.3 base. At handover the
   fetched main is `c4922e9` (v0.10.3); its picture settings/performance changes
   must survive. The October17 runtime audit used toolkit `4a6c88e` and is
   historical evidence; recheck today's toolkit and loader before implementation.
2. Locate the complete private snapshot below. Run the read-only verifier:

   ```sh
   python3 scripts/verify-menu-ui-artwork.py --art-root \
     /Users/vlad/Code/defjam-data/scratch/menu-ui-restoration-paused-20261019/session/pilot-v1
   ```

   Expected: **226 unique file pins, zero failures**. These include approved
   artwork and the separately conditional Mode Select scope. The catalogue
   is `config/menu-ui-approved-artwork.json`; it contains hashes, relative paths,
   approval scopes and71source IDs, never artwork bytes or a runtime-pack manifest.
3. Make an isolated local implementation/export workspace. Preserve the live
   fighter review at8768, its17candidates and Trejo. Do not rerun historical
   approval, import or archive scripts against the original workspace.
4. Deliver an ordinary source-shaped restoration pack first. Implement and
   validate true wide scenery separately; wider PNGs alone cannot provide it.

The menu session's decisions D123–D157 are qualified **/UI** when sharing a
number with decisions on other branches. Keep original historical identifiers;
D137/UI is emblem approval, whereas main's D137 is a release decision.

## Where everything lives

| Item | Exact location |
|---|---|
| Complete persistent private snapshot | `/Users/vlad/Code/defjam-data/scratch/menu-ui-restoration-paused-20261019/` |
| Authoritative art root within it | `session/pilot-v1/` |
| Original working art root | `/Users/vlad/.codex/worktrees/01c5/DefJamFFNY-recomp/logs/ui-restoration-20261017/pilot-v1/` |
| Data/dump root | `/Users/vlad/Code/defjam-data/` (`extracted/`, `mods/`, `save/`) |
| Authoring repository | `/Users/vlad/Code/DefJam-Modding-Tools/` |
| Existing faithful corpus | `/Users/vlad/Library/Application Support/DefJamPR19-20/data/hd-work/setup-lanczos/b9aa783f1d66d41be59fb97ba25fb86e1e9f73d8ee041263b0ff5df85ceb26f0/` |
| Authoring Python, Pillow/NumPy | `/Users/vlad/Code/DefJam-Modding-Tools/.venv/bin/python` |
| Live fighter workshop | `http://127.0.0.1:8768`, workspace `/Users/vlad/Code/defjam-data/hd-work/texture-workshop` |
| Separate read-only menu review | `http://127.0.0.1:51738/review.html` |

The full snapshot is self-contained session evidence, unlike the older incremental
`<data>/scratch/menu-ui-*` overlays. It retains every input/raw/final/history,
alpha registration field, prompt, recipe and review page. `manifest.json` at its
root fingerprints the snapshot. Keep it unchanged; work on a copy or new export.
To resume the review if51738is absent, serve its art root with a read-only static
server bound to127.0.0.1; use another port if51738already serves another session.
Do not start a second writer on the8768workspace. Never depend on a Codex worktree
or `$CODEX_HOME/generated_images` as the only artwork copy.

Public source counterparts:

- This handover, `docs/research/menu-*.md`, tooling/runtime audits and the hash
  catalogue belong to `vyanhursky/DefJamFFNY-recomp`.
- `vyanhursky/DefJam-Modding-Tools`, branch `codex/menu-ui-art-source`, contains
  the production guides, reusable prompt templates and
  `research/menu-ui-pilot-20261017/`: historical Python/HTML/Markdown/prompt source,
  extracted prompt specifications and a source-file hash index. It contains no
  textures, screenshots, registration arrays, workspace review JSON or game data.
  The source index retains original/published fingerprints and declares one whitespace-only publication edit; hard-coded paths and
  write side effects make them evidence, not a safe batch command to run blindly.

## Exact accepted revisions

All paths in this table are relative to the private art root. Consult the
catalogue and named approval record instead of selecting the newest PNG.

| Artwork | Accepted artifact / record | Implementation caveat |
|---|---|---|
| Title sky | `seam-v3/title-skyline-wide-master-v3.png`; `skyline-approval.json` | v3full-height cloud joins; earlier/right-upper-only repair is superseded. Approved art-space master/crops, not approval of independent workshop trials. |
| Title city | `batch2/title-middle-restrained.png`, `title-middle-wide-draft.png`; `branding-v1/city-approval.json` | Active640×310; settled stage(0,102); full640×512texture contains opaque padding. |
| Title tower/roof | `batch2/title-tower-restrained.png`, `title-tower-wide-draft.png`; `approvals-v1/approval.json#water_tower` | Active640×446; stage(0,16); do not composite its512-high padding over lower layers. |
| Mode Select | `batch2/mode-environment[-lit]-wide-draft.png`; `approvals-v1/approval.json#mode_select` | **Conditional**: right4:3border darkness must be reviewed with live spotlight/door/masks/transitions. These are scene studies, not flattened replacement textures. |
| Title logo +8door emblems | `transparency-v2/*-repaired.png`; `branding-approval-20261018.json` | v2clear holes/partial alpha approved. Earlier `branding-v1/*-restrained.png` is a different, unapproved alternate. Approved logo is2172×724, not the original512×128atlas; see registration section. |
| Options | `options-softness-v4/options-wide-v4.png`; `options-approval-20261018.json` | v4reference-width door/panel and slight right defocus. Paper and door are separate scrolling layers; preview is top scroll state. |
| Jukebox | `menus-seams-v2/jukebox-wide-v2.png`; `jukebox-approval-20261018.json` | v2aligned brick joins; live strip/mask/lighting layers remain separate. |
| Battle setup | `battle-left-light-v5/cmtype-wide-v5.png`; `battle-setup-approval-20261019.json` | v5LEFTupper lighting accepted; v4RIGHTlighting was explicitly reverted. Keep improved v3brick and pre-v4right. |
| Fighter menus2/3/4 | `battle-v1/chooseF{2,3,4}-wide-draft.png`; `battle-fighter-approval-20261019.json` | Three different frame layouts/source pairs, not one interchangeable background. |
| Rewards/Unlock Fighters/Unlock Venues | `rewards-v1/rewards-shared-world-v1.png`; `rewards-approval-20261019.json` | Same two source IDs,3screen labels/2placements. Wide vignette exception below. |
| High Scores | `hiscores-v1/hiscores-wide-v1.png`; `hiscores-approval-20261019.json` | Independent wall/board, not a Rewards reskin. Entire score board/rules protected. |
| Rematch/results1–4/reward earned | `results-v1/{rematch,matchSm1,matchSm2,matchSm3,matchSm4,rewardun}-restrained.png`; `results-approval-20261019.json` | All6approved;18original-shaped tile exports. Keep paper size/placement at wider ratios. Stage drafts are transparent placement proofs. |

Artwork coverage: **71unique original source IDs** (63unconditionally approved,
8conditional Mode Select); **101built-in imagegen calls** historically. No further
calls are authorized while paused. A composed emblem is several original tiles;
226pinned review outputs are not226independent texture replacements.

## Source-shaped pack: first playable delivery

Use the existing schema1PNG loader and sparse overrides, not modified disc files.
Game docs: [HD textures](hd-textures.md), [settings](settings-reference.md).
Authoring implementation: `src/defjam_tools/texture_workshop.py`,
`docs/texture-workshop.md`. The workshop supports immutable import/review/export;
its current live fighter state is not an empty menu workspace.

- Start with the18already source-shaped panel tiles and8Battle background tiles;
  then the Options3,Jukebox2,Rewards2,HighScores2 source-shaped files. Relevant
  `references.json` and approval file lists identify every name/source-ID pair.
  All must retain the original stored canvas, UVpadding, alpha and uniform scale.
- Title city/tower accepted4×mosaics can be split into their original512+128tile
  layout with original512-row padding. Preserve approved pixels and source alpha.
  Skyline and Mode Select need the approved composition/source-layer mapping;
  do not silently substitute the earlier unapproved four workshop trials.
- **Logo/emblem blocker:** owner approval is of the nine repaired artwork canvases,
  not new game-native exports. Their raw-canvas RGB was retained and registered
  source alpha repaired; current shapes/sizes are not uniformly scaled original
  atlases. `transparency-v2/register-alpha.py`, registration receipts/`.npz`,
  `branding-v1/assembled-references.json`, logo/emblem audits and source matrices
  are available. Register accepted art back to the native source geometry, preserve
  clear holes, partial alpha, ink/silhouette/lettering and stored padding, then
  split21emblemtiles and the logo atlas. A blind resize to fit the loader is not
  fidelity validation. Show any visibly changed derivative to Vlad; leave originals
  as an explicit first-pack fallback if registration cannot yet be verified.
- Archive names/numeric image names/SHPX indices are not interchangeable. Original
  archive source IDs are provisional runtime keys: collect actual runtime IDs with
  `RECOMP_TEXTURE_DUMP_DIR`, read `textures.jsonl` and prove decoded pixel matches.
  Current `config/hd-runtime-aliases.json` is a bounded earlier map, not71menu IDs.
  Retain alias evidence; never guess hashes or assign an entire movie one key.
- Export a new local pack with hash receipt: `<data>/mods/menu-ui-restored-test-v1/`
  (or split opacity/channels components if evidence requires it). Texture entries
  are `source-id=textures/source-id.png`; schema1under`[pack]`, straightalpha,
  identical atlas aspect, uniform integer1–8×. Prefer4×source exports where ready.
  Do not include scene studies, join crops, comparison images or AIraws as textures.
- List existing faithful packs first, menu overrides last. Example only:
  `[textures] enabled=1`, `packs=<existing-faithful-components>;menu-ui-restored-test-v1`.
  Keep the installation's actual component names/mip policies. Export does not
  install. Back up settings/pack order, install locally with game closed, restart,
  and confirm actual replacement hits. A bad winning key may fall back to console
  art rather than an earlier faithful pack. Keep an easy disable/rollback path.

## True widescreen scenery: separate engineering stage

The historical loader only accepts same-shape integer-scale replacements. Even
if that check changes, existing APT triangles, UVs, centered placement, scissor,
masks and animated transforms still need explicit support. Do not globally
stretch menus or relax all atlas validation. Preserve live controls/text, icons,
portraits, effects and original4:3behavior. Recheck current toolkit implementation;
`side_fill=art` in old design docs is a proposal, not proof of an implemented path.

Keep game-specific screen/layer mappings in the parent repo and generic renderer/
loader support in the toolkit fork. Follow current repository instructions for
fork publication, pinning and regressions. Upstream PRs remain separately approved.
Bind side artwork by proven source identity plus shape/draw context. Extend the
right background layer with matching motion, opacity, sampling and masks; keep
foreground UI centered. A static post-present side plate is a labelled prototype,
not faithful layered integration. Implement an explicit fallback on unsupported
screens/aspects and isolate opt-in wide-art behavior from ordinary HD replacement.

Art-space ratio sizes at1440high:4:3=1920,16:10=2304,16:9=2560,21:9=3360,
32:9=5120. A5120master's1920-wide center beginsx1600; crop offsets1600/1408/
1280/880/0respectively. **These are art-master coordinates, not texture dimensions
or universal layer origins.** City/tower need their source-to-stage offsets.
Rewardsworld is5256wide with699nativewidth and screen offsets29vs50.4: use
`rewards-v1/references.json` and receipts instead of generic5120crop arithmetic.
Mode's independently moving layers likewise require their actual material bounds.

Rewards exception: source-shaped/native4:3alpha, padding and edge vignette stay
exact. Approved wide views intentionally replace the peripheral baked/alpha fade
and move it to the outside edge; only native x40..659trusted interior stays exact.
Do not enforce full wide4:3RGBAcenter equality on that approved exception or undo
its reviewed treatment. Papers never receive scenery extension or widened geometry.
MatchSm4bottom-right tile startsy255,not256; preserve its one-row overlap. Exact
APT transforms, including subpixel placement, take precedence over rounded previews.

## Art constraints and acceptance

Original palette, no added yellow/warm cast; restrained detail; centered scenery;
allfive aspects including21:9/32:9. Keep source print/lettering, rules, silhouettes,
alpha holes and translucent material. Both full-height joins must match cloud/
brick structure, native texture density, focus/grain and lighting. Do not hide
joins with broad blur or use enforced edge equality as evidence of a good join.
Options' slight approved right defocus is deliberate. Keep all approved inputs
immutable and record new technical export hashes/parents separately.

Use guarded disposable saves and current harness routes. Examples from the audit
(use observed anchors/current help, not invented CLI options):

```sh
python scripts/harness.py run boot --png --env RECOMP_WS_ASPECT=1.7777778 --env RECOMP_TRANS_SHOT_NATIVE=1
python scripts/harness.py run boot --png --env RECOMP_WS_ASPECT=1.6 --env RECOMP_TRANS_SHOT_WIDE=1
```

Native capture preserves actual detail; default640×480captures can squeeze wide
output and cannot establish correct proportions. Texture dumps/diagnostics stay
out of performance measurements. Test4:3,16:10,16:9,21:9,32:9: settled and entrance/
exit, title parallax/skyline, both full-height joins, Mode spotlight/door/selection,
Options scrolling, Jukebox strip, Battle2/3/4frames, shared Rewards placements,
HighScores live text and all6paper layouts. Compare the same animation and light
state, retain live input, and confirm letterboxing/scissor/fades/mips/cache behavior.
Use default picture brightness100and record vignette settings so global options
are not confused with artwork color drift. Validate Mac Vulkan first, then Windows
D3D11and Deck Vulkan separately. Runtime/renderer changes require focused synthetic
fixtures and full current project regression; the art-review checks do not replace it.

Deliver: immutable local componentpack(s), installation/disable instructions,
source-to-runtime mapping and hashes, settings/pack order, per-screen/aspect
captures/checklist with passes/known gaps, native regression evidence, and a list
of temporarily retained originals. Do not claim “allmenuswidescreen” from one
flattened still or mark Mode's conditional border accepted without Vlad's playtest.
No art, captures, game binaries, generated guest C or dump payloads go to GitHub.

## Paused queue and resume prompt

23confirmed future priorities remain (Extras4,phone2,venueframes3,title8,support6),
plus unclassified shared/Story/popup pieces. Registeredflow textures without
custom restoration:1,252of1,323; they already have FaithfulHD. Those are not1,252
background jobs. See [remaining inventory](research/menu-ui-remaining-inventory-2026-10-19.md).
Do not process this queue until Vlad explicitly resumes restoration.

Copyable next-agent request:

> Read `docs/menu-ui-modpack-handover.md`, current `AGENTS.md` and `PROGRESS.md`.
> Restoration is paused: use the approved local artwork without new imagegen calls.
> Verify the226file pins. Build a reversible local source-shaped Menu/UI test
> modpack first, resolve original/runtime identities and missing native exports,
> then implement opt-in widescreen background support while retaining live UI and
> original4:3behavior. Preserve the fighter workshop and all accepted art. Test
> the five aspect ratios, document gaps and provide a local pack Vlad can playtest.

# High Scores approval and result-paper restoration — 2026-10-19

Vlad approves High Scores v1: “High Scores is approved lets move on”. D154 pins
13 current artwork files in the private `hiscores-approval-20261019.json`.
Approved Battle, Rewards and High Scores manifests now cover92unchanged files;
other earlier approvals retain their existing manifests. Native game acceptance
remains separate. D155 continues with six paper-panel layouts,18source tiles.
Review: http://127.0.0.1:51738/review.html,first section Results & Rematch.
Private working folder: `logs/ui-restoration-20261017/pilot-v1/results-v1/`.

## Mapped source layouts

| Movie | Active native panel | Stored atlas | Tile names | Native stage position |
|---|---|---|---|---|
| rematch |281×193|512×256|1|184,233|
| matchSm1 |272×372|272×384|11,10,9,8|194,66|
| matchSm2 |539×374|544×384|18,17,16,19|50,60|
| matchSm3 |555×376|576×384|21,19,18,20|43,65|
| matchSm4 |552×373|576×384|15,14,13,16|44,66|
| rewardun |455×328|512×512|1|110.5,68|

These are layered UI papers, not full-screen scenery. All have real partial
alpha. Preserve their authored sizes and placements, including narrow right
strips and unused tile padding. MatchSm4 bottom-right tile starts at y255,
not256; keep its one-row overlap. APT/material/root matrices, source IDs/hashes,
UV extents, ancestor placements and frame counts are retained in the private
source audit and `references.json`. Live text, scores and effects are separate.
The preview rounds a .05px material offset inSm2 and fractional reward placement
at3× to the nearest pixel (reward -1/6nativepixel); exact source matrices remain
available for runtime integration. No runtime layout has been modified.

## Restrained restoration and protected assembly

Six built-in imagegen edits,101pilot total. Exact prompts/input/raw hashes are
saved in `jobs.json`/`generations.json`. Raw outputs remain available, including
any framing/alpha/graphic drift. They are reference studies, not direct game
candidates. Prompts repeat original color accuracy, no warming/yellowing, no
added text/geometry and genuine transparency. Final candidates use source alpha.

Each tile is faithfully enlarged4× with Lanczos. The reviewed active mosaic is
assembled from those same source-shaped tile exports. Fine generated paper
brightness only: Gaussian4 highpass, strength.14, clamp±1.25L before weighted
mean removal; source Lab a/b retained before8bit RGB rounding. Safe masks exclude
translucent edges, source gradients/rules and entire printed bands. Headers,
score rows and rematch ink strips are protected; the reward-card interior,
including every star and red/black graphic, is retained exactly. Tile edge and
UV boundaries have4nativepixel guards; unused padding is exact.

Actual RGB changes are at most4/255,3forreward; changed pixels cover0.22%of
reward and6.06–9.47%of other active mosaics. This intentionally subtle pass
preserves the source folds and broad lighting. Safe-mask mean chroma deviation
is below.0021Lab from8bit rounding; no global color correction is applied.
Recipes/masks/per-panel metrics/hashes: `assemble.py`,`assembly-receipt.json`.

Five aspect studies per panel retain a centered640×480 viewport, original paper
size and placement. Wider margins are transparent; no paper is stretched or
outpainted.30original/draft pairs at1440high cover4:3,16:10,16:9,21:9,32:9.
They omit the game backdrop, live scores and animation and do not prove native
widescreen integration.

## Review, checks and retention

The first review section offers all6layouts beside native originals or Faithful
HD,RGBA/alpha,checker/dark/light,wipe anddraft-only,raw studies and five stage
aspects. High Scores below is marked approved. Allprior sections/history remain.
1161priorPNGs unchanged;92approvalpins exact;18tile alpha/UVpadding/edgeguards,
6mosaic alpha/protected pixels and30stage centers pass exact checks.
Browser:30cases×2stage images and24reference/channelcases×5texture/raw images
load;wipe50%,draft-only/side,bounds75%/12.5%at16:9,transparent imagebackground,
nooverflow/consoleerrors. `browser-verification.json`,`review-proof.png`.
No game/runtime tests were needed for these private artwork studies.

Immutable overlay `<data>/scratch/menu-ui-results-v1-hiscores-approved-20261019`
contains this batch,13approvedHighScoresfiles/manifests,the approval scripts,
source audits,review/history and bothprojects' workflow records. Apply it to a
COPY of the earlier pilot; earlier archives stay unchanged. Source checks and
instruction-file twins pass. All six new drafts await owner artwork feedback.
Authoring writer8768,17workshop candidates andTrejo remain untouched. Noimport,
install,game run,build,runtime/core edit,commit orpush.

After owner feedback: Extras note papers,Story message-center phone110/111,
venue-selection/popup frames and remaining title text/shield mapping. Preserve
intentionally soft light masks. Options scroll and conditional Mode Select
right-border lighting still need native game validation at all five ratios.

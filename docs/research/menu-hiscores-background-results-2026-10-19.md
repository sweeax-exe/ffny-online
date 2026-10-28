# Rewards approval, High Scores draft and remaining artwork — 2026-10-19

Vlad approves the shared Rewards/Unlock wall and authorizes further useful menu
texture work. D152 pins27current files in `rewards-approval-20261019.json`:
sharedmaster,15screen/ratio drafts,6joins,2extensions,mosaic and2source tiles.
The reviewed wide-vignette policy is included; raws/rejected attempts and native
game acceptance are excluded. All27Rewards plus52Battle files remain frozen.

D153 continues with the independent High Scores wall and board. The first review
panel now contains five aspect pairs, original/wipe/center/draft-only/bounds,
full-height joins and source-shaped original/Faithful HD comparisons.
Private root: `logs/ui-restoration-20261017/pilot-v1/hiscores-v1/`.
Final `hiscores-wide-v1.png`, five `hiscores-<ratio>-draft.png`, source-shaped
`hiscores-restrained.png`, `part-0-restrained.png`, `part-1-restrained.png`,
two join proofs and separate RGBA extensions. New artwork is unapproved.

## Source and protected assembly

APT/object audit confirms `screens/hiScores/hiScores.big`, shape3, native640×480,
root(0,0), names2/1:512×512+128×512, opaque alpha. Source IDs start `bc0317c0ade0`
and `553692a37819`. Stored lower32rows are padding, not extra scenery.
The wall is visually similar to Rewards but has different source IDs/placement.
A limited top-wall RGB offset comparison found no pixel registration (best mean
absolute channel error~32/255 across59offsets); this does not prove every material
is unrelated, but it rules out blindly substituting the approved Rewardspanorama.
High Scores is restored/outpainted independently. Alpha glow name150 is retained
unchanged; generated lighting is not a substitute for that live layer.

Entire source board, including its thin border/header/left rows/right paper,
is protected at native[48,86,588,440]. In4xoutput this is[192,344,2352,1760].
AIraw board interpretation is discarded. Source alpha, allpadding below1920,
and tile seamguard2032..2064 remain byte-identical to Faithful4x. Only tiny
fine LabL texture from the restoration is adopted outside protected areas:
Gaussian4 highpass,clamp±1.5L,strength0.35,source chroma retained.

Three built-in imagegen calls (95pilot total), no CLI fallback:1restoration and
2native-edge-only side outpaints. The1744×1440side target contains1600blank and
144px(48native) originalbrick context. No board is present in these targets;
prompts forbid creating/extending board/text/objects and require original courses,
material/focus/light continuity, subdued original brown/olive/gray and no warming.
Only generated regions are adopted. Normalize full target uniformly, sample
newmaterial at native density534×480 then1600×1440, transfer adjacent native144px
Labrow means/contrasts with Gaussian64,gain0.6..1.4,condition16pxadded edge.
No sourcepixels are blurred/graded/warped. Master5120×1440 protects center
[1600,0,1920,1440] exactly. All five crops retain that same center. Exact prompts,
input/raw hashes/sizes and adoption notes are in `generations.json`; recipes in
`restore-and-targets.py`, `assemble.py`, restoration/wide receipts.

## Verification and retention

1126prior PNGs unchanged,79Battle/Rewardsapprovalpins exact. Entire board,
sourcealpha/padding/tile seam and five nativecenters pass exact checks. Browser:
5ratios×6comparison/join images load,2source reference modes×2images load;
wipe50%,center1920/aspectlock,bounds75%at16:9,draft-only/side pass. Approved
Rewards/Battlelabels retained; no horizontaloverflow or console warnings/errors.
Screenshot `review-proof.jpg`. Source checkers/instruction twins pass. Artwork
preservation is not visual acceptance; no game/runtime tests this revision.

Private overlay `<data>/scratch/menu-ui-hiscores-v1-rewards-approved-20261019/`
retains newbatch,27approvedRewardsfiles/pins,otherapprovalmanifests,auditmetadata,
remaining shortlist,review/history and source workflow records. Apply to COPY
of priorpilot,earlierarchives immutable. Authoring state/workflow synchronized.
Writer8768/17candidates/Trejo untouched; noimport/install/game/build/runtime/core
edit/commit/push.

## Remaining useful work

A fresh visual shortlist inspected21source references from registered menu flows;
it is a targeted sample, not a claim of21remaining backgrounds. Source-only
metadata/contact sheets are in private `remaining-menus-v1/`.

- Match summary/rematch/reward-earned paper panels: `battle/rematch` name1,
  `matchSm1` multipart paper; `matchSm2..4` split result sheets; `rewardun` name1.
  Restore source-shaped paper/ink/alpha and rules. Map each atlas/shape before
  composition. Preserve menu labels separately. They are panels, not scenery
  to stretch across the screen.
- Extras note papers (37,42,48,52) and associated fragments: preserve handwritten
  arrow/redline/creases/silhouettes and alpha. Extras name69 and High Scores150
  are deliberately soft lighting masks, not backgrounds needing invented detail.
- Story message-center phone names110/111 form a split shell. Map the halves and
  protect all screen/UI/keycap lettering/alpha; never invent keyboard text.
- Venue-selection frame/mask pieces and shared popup/paper backing assets remain
  worthwhile after shape/alpha mapping. Localized Press Start/support text and
  title shield/effect remain separate mapping/fidelity work.

Next owner visual review of High Scores and then the result/rematch paper family.
Native integration/UV/scissor/live text/lighting/transitions and all5ratio captures
remain later, including Options scrolling and conditional Mode Select darkness.
No full-menu completion or runtime acceptance is claimed.

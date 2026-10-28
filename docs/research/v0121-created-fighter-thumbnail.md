# v0.12.1: created-fighter snapshot thumbnails

Issue #113 is independently reproduced with the original VY3 save: the small Battle selection thumbnail is empty while the normally loaded large portrait renders. Loading-card clipping is separately fixed by #110. Eye preview refresh and right-side meters are separate changes in #115 and #114.

## Cause

The original game renders its created-player snapshots into small swizzled NV2A surfaces, then reads their pixels on the CPU, quantizes them and stores indexed images in the profile. The host translator renders the surfaces on the GPU but leaves their guest-memory pixels empty. Consequently, the game's normal snapshot writer receives an empty source and saves an empty image.

Native Wardrobe-exit control `thumbnail-wardrobe-before2` confirms a fully empty 32x32 source allocation and empty profile thumbnail. Static source plus native hardware entry watches establish the normal path: preview update `sub_00017F10`, snapshot conversion `sub_000178B0`, source resource getter `sub_00100FB0`, and profile image copies `sub_00018600` / `sub_00018640`. The selection control `sub_0005F1F0` later copies those profile bytes correctly; its draw is present. Source resource payload offsets must be honored: the 0x1000 flag selects an external payload by relative offset, so bytes following the header are not necessarily pixels.

## Correction

Published runtime commit `a4d1d87` keeps the host draw and also runs the existing CPU rasterizer for complete, tightly packed ARGB capture surfaces of at most 64 KiB. The gate requires zero clip origin, valid guest span, supported linear/swizzled type, raster dimension bounds, and no stencil. A swizzled target must cover the allocation dimensions encoded by its format. It does not turn on software rendering for normal framebuffers or arbitrary small windows in a larger allocation.

This reuses the existing guest projection/AA, texture/combiners, blending, depth and Morton addressing. The existing guard against executing vertex-program constant writes twice remains. It is a bounded fallback for these small targets, not a complete GPU-to-guest readback implementation or a claim that all render-to-texture cases are supported.

## Independent validation

- The new hardware-free `nv2a_small_capture` fixture exercises real executor clears and geometry under `RECOMP_PB_D3D11=1`: linear, square and rectangular swizzled layouts, exact 64 KiB boundary, sentinel bounds, oversized/clipped/unsupported/stencil targets, and invalid/protected destinations. The pre-fix runtime fails its expected-pixel assertions; corrected Release fixture passes.
- Existing `nv2a_raster_batch` and `nv2a_ffp_backend` Release fixtures pass.
- Native `thumbnail-wardrobe-capture-fixed` generates nonempty 128x128 and 32x32 capture pixels. The original game converts and copies them into the profile. Decoding the resulting indexed thumbnail shows VY3's face and bandana.
- `thumbnail-recovery-save-exit` starts from the original empty VY3 fixture, enters Wardrobe, leaves normally, then uses Crib Exit -> Yes. The native log records `MemCard.Save(story/crib.swf)` and `Game.StartMainMenu(3)`. The saved file contains the exact 2,048-byte image/palette block captured from the game record, at serialized offset 0x20A. No save-format edit or manual image injection is used.
- Fresh-process Battle reload: the normally saved VY3 copy loads without injection, shows the correct face in the first grid cell and retains its large portrait. The 3.1-second Choose Fighter capture was visually inspected. No faults; saves restored and owner saves unchanged..
- Broader game regression and CI: recorded in the PR as they complete. Full release-candidate platform checks and owner acceptance remain due.

Every run uses a disposable save root, restores it identically, and verifies the owner's save tree unchanged. Images, saves, memory captures, generated guest code and game assets remain local and ignored.

## Existing profiles

The renderer cannot recover an image that was already saved empty without taking a new snapshot. With this fix, load the affected Story profile, enter and leave Wardrobe, then use Crib Exit and accept the save. The normal game regenerates and persists the thumbnail. The native reload test above validates this recovery route; no automatic owner-save migration is added.

## Local evidence

All paths below are relative to the game worktree and remain ignored:

- `logs/v0121-ui/thumbnail-wardrobe-before2.json`, `run-20261025-180750.log.err`.
- `logs/v0121-ui/thumbnail-wardrobe-capture-fixed.json`, `run-20261025-181056.log.err`.
- `logs/v0121-ui/thumbnail-recovery-save-exit.json`, `run-20261025-182108.log.err`.
- `logs/v0121-ui/thumbnail-recovered-battle-reload.json`, `run-20261025-182435.log.err`.
- Source/resource/record metadata in each case's `*-memory/` folder under `logs/v0121-ui`.
- `small-capture-negative-result.txt`, `small-capture-build.txt`, `build-thumbnail-final.log` under the same folder.

The earlier Battle-only `TakeSnapShot` injection did not reach the writer and was inconclusive. The first Crib navigation attempt entered Messages and was stopped with save restoration; the calibrated route selects Wardrobe two positions to the right of Map. These probes are not used as positive reproduction or fix evidence.

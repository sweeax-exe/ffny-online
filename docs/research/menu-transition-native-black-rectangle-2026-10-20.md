# Transition pan: black rectangle in native captures (2026-10-20)

## Finding

The agent observed the black rectangle in the Faithful-only 16:9 control and in the RC1 4:3 capture. The 16:9 run explicitly set `RECOMP_TEXTURE_PACKS=faithful-hd-a512d2429f11-opacity;faithful-hd-a512d2429f11-channels;` and did not inherit the process environment (`logs/menu-ui-v1-rc/menu-rc2-pan-16x9-baseline-receipt.json`). This means the new scenic pack, its registered `[wide]` slabs, and its new `stretch` registrations are not required to produce the defect. The evidence points first to the retained source/faithful texture or to guest-authored draw/clip geometry; it does not yet distinguish them.

## Evidence

- In the RC1 16:9 transition trace, the leading left tile is texture `90c3ba5f36d98cb7f5f1d80d9f6f819fb9e32891aa0656f70fe296a96c37f253`, 512x512, positioned at x=-0..512. Its adjacent tile `51ebe2dcf268b05f3ee49ced46d270f0607a766fa776d83e853d07e171f920de` is x=512..1024. The capture run reports them at log lines 6846–6854, with the 0.65s frame at line 6983 (`logs/run-20261020-214604.log.err`).
- The agent-observed Faithful-only RC2 capture is recorded at 0.65s and completed cleanly (`logs/menu-ui-v1-rc/menu-rc2-pan-16x9-baseline-receipt.json`, `logs/run-20261020-215923.log.err`). The RC2 log shows later pan tiles, but the capture path does not log per-pixel ownership, so it cannot by itself identify the draw that paints black.
- The approved menu-wide configuration makes `90c3ba…` the `transition-10` left anchor; `51ebe2…` is deliberately not a wide anchor. Bitmap9 remains Faithful-only. The configured transition and vignette overlays therefore do not explain a defect that survives a Faithful-only run.
- At 4:3, `s_ws_k >= 1` makes `ws_place_batch` return before placement, and `ws_wide_sides` also returns before querying or replaying slabs. The reported 4:3 reproduction independently excludes the widescreen side-replay path as a necessary cause.
- Toolkit `[wide]` replay changes placement/scissor and restores stage-0 texture in `pgraph_d3d11_wide_end`; it does not restore the surface scissor there. A stale scissor is therefore a code-level risk when replay is active. It cannot be the common cause here, because the Faithful-only run has no menu scenic `[wide]` manifest and 4:3 bypasses replay altogether.
- A `stretch` overlay is unlikely as the common cause: RC2's texture-pack list is the two Faithful packs only, and it reproduces there; `menu-ui-wide-layers.json`'s new vignette registrations are excluded. The RC1 trace does show many stretch-class batches in the general menu run, but does not correlate a particular overlay draw with the transition tile. This is not enough to rule out an older/base overlay or another guest draw.

## What is and is not established

Established: the defect is not dependent on the new scenic artwork/side slabs or their `stretch` additions, and a wide-side scissor/replay leak is not a necessary explanation. The Faithful-only control narrows it to behavior already present with the content-faithful replacement packs and normal guest rendering.

Not established: whether the black region is already in the replacement bitmap/alpha/mips, whether a following guest draw or clear paints it, or whether an ordinary guest clip/scissor/alpha state causes it. The wide-batch summaries provide bounds and texture identity, not draw ordering or pixel ownership. The agent observed the capture symptom but did not inspect game-art pixels as part of this static audit.

## Existing diagnostic coverage and recommended capture

`RECOMP_PB_BATCH_DUMP=shot` is the existing draw-sequence diagnostic. The capture hook calls `nv2a_pb_request_dump_next`; it requests the next flip, so the dump is the first presented frame after the timed capture (typically one frame later), not a retroactive dump of pixels already captured. For every batch it prints frame, primitive/count, guest texture address and dimensions/pitch/format/valid bit, position and UV bounds, first vertex color, depth range, combiner registers, render-state fields (alpha test, blend, color mask, depth/stencil/cull), plus any enabled secondary stages. Surface-clip method changes are printed as ordered `surface clip WxH+X+Y` lines, and clear/color-surface events are also logged. This is enough to identify candidate overlapping draws and compare clip/blend/combiner state without new runtime diagnostics.

Limitations: the batch record does not print a source content hash for each draw, stage-0 sampler/mip registers, or the final transformed raster coverage. The guest texture address must be correlated with the already available `[WS-WIDEBATCH]` texture ID/extent line or the texture-pack upload/hash records. Clip lines are change events, so associate each with the subsequent batch until another clip event. Stage-0 raw format is present, but mip selection and filter are not. `RECOMP_PB_PROBE=x,y` can add per-triangle coverage/depth for up to eight chosen points and logs whether alpha blend/stencil are enabled; choose a point inside the black rectangle and one immediately outside it, in the native 640x480 coordinate system. It does not prove final pixel color or report UV at the point.

Recommended single run, after the current serial loading matrix finishes: repeat the Faithful-only 16:9 route and retain just the 0.65s shot, with these environment settings (copy the existing RC2 pad script from the receipt unchanged):

```text
RECOMP_TEXTURE_PACKS=faithful-hd-a512d2429f11-opacity;faithful-hd-a512d2429f11-channels;
RECOMP_WS_ASPECT=1.777777778
RECOMP_TRANS_SHOT_NATIVE=1
RECOMP_TRANS_SHOT_SECS=@getscreeninfo(intmain/mainmenu,0.65
RECOMP_PB_BATCH_DUMP=shot
RECOMP_WS_WIDE_TRACE=1
RECOMP_TEXTURE_TRACE=1
RECOMP_TEXTURE_SYNC=1
```

Keep the existing route's pad script, anchor, and capture setup from the RC2 receipt; narrow its shot list to 0.65s as shown. Inspect the batch frame named by `[BATCH] dumping flip N`, then read `[BATCH]` records in order. Find the batch whose guest texture address/shape matches the `90c3ba…` `[WS-WIDEBATCH]` identity. Check its UV/position bounds, last applicable clip event, combiner and alpha/blend state, then all later batches whose bounds intersect the black tile; note any clear/color-surface event. If screen coordinates can be read from the capture, optionally add `RECOMP_PB_PROBE=x,y;x2,y2` for one point inside and one just outside the rectangle. Its `[PROBE]` triangle hits narrow which draws cover those points, but do not prove final pixel color or report UV at the point. Texture trace should confirm replacement load timing and failures; `RECOMP_TEXTURE_SYNC=1` changes asynchronous PNG replacement upload to synchronous, avoiding an in-flight `loaded == -2` fallback frame. It is a useful one-run control for the decode-timing hypothesis, but it does not expose or repair mip content. Do not add `RECOMP_TEXTURE_DUMP_DIR`: that writes source game texture pixels to disk and is unnecessary for this draw-order check.

## Additional serial controls after resuming

The no-replacement (`RECOMP_TEXTURE_PACKS=`) 16:9 control also captures the first-left-tile black rectangle; see `logs/menu-ui-v1-rc/menu-rc2-pan-16x9-original-receipt.json`. The synchronous Faithful control (`RECOMP_TEXTURE_SYNC=1`) captures a complete staircase at0.65s; see `menu-rc2-pan-sync-16x9-baseline-receipt.json`. That is useful evidence, but not yet causal proof of asynchronous loading: the timed shots are wall-clock-relative to the screen call, while synchronous decoding can shift which animation phase is actually presented. Compare source tile identity/position at the black region before claiming a timing fix. Both controls completed with restored save guards and no runtime fault signatures. Neither requires changing the approved artwork.

The app shutdown interrupted the Options tour (`run-20261020-220927.log.err`) before its guard completed. The retained pre-run payload was restored byte-identically in the isolated data root after preserving the interrupted state separately. Receipt: `logs/menu-ui-v1-rc/interruption-recovery.json`. This is not a game hang or a passed tour. The ordinary user save root was unchanged.

## Windows follow-up — 2026-10-23

The [Windows RC verification](windows-integrated-menu-mods-rc-2026-10-23.md)
reproduces the rectangle with no packs, normal managed packs, synchronous loads,
depth testing disabled and forced alpha-blend diagnostic state. Fixed consecutive
flip dumps resolve the earlier capture/dump mismatch: in f1358, the left tile
starts at y216.1, exactly where the corresponding capture's black region ends.
After the color clear, no color-writing draw covers x0..512,y0..216. Full-screen
passes there have color writes disabled, and the later clear is depth-only.
Thus source alpha of the leading tile and later color overdraw do not explain
this frame: it has a scene-coverage gap above that tile.

The bitmap9 placement noted in the mapping report remains a useful next lead.
This does not establish the intended console geometry or which guest/runtime
operation caused the gap. No renderer bypass, hardcoded tile move or artwork
cover is retained; the defect remains open. Exact Windows log lines and local
receipts are listed in the verification report.

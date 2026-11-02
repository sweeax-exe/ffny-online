# CreateTexture2D failed (fmt=15 256x2): flood in a Story cinematic

Study only, nothing run or edited. Marks: [V] verified in code/log, [I] inferred, [G] guess. TK = `tools/xboxrecomp/src`.
Log: `logs/bug-reports/20261102-004740.975-2f07f1/log.txt` (build 39, Ryzing). Not to be published.

## 1. Which format and why E_INVALIDARG

* [V] fmt=15 is `D3DFMT_DXT5` (0x0F, `TK/d3d/d3d8_xbox.h:124`; the NV2A texture format is passed through unchanged as the D3DFORMAT).
  `d3d8_to_dxgi_format` (`TK/d3d/d3d8_resources.c:102`) maps it to `DXGI_FORMAT_BC3_UNORM`.
* [V] Path: `pgraph_d3d11_set_texture_ready` (`TK/nv2a/nv2a_pgraph_d3d11.c:962`, called from `TK/kernel/nv2a_pb_exec.c:4151`)
  -> `dev->CreateTexture(w,h,1,0,fmt,0)` (:1071) -> `dev_CreateTexture` (`d3d8_device.c:1430`) -> `d3d8_CreateTextureImpl`
  (`d3d8_resources.c:1386`). It fills `td` with Width=256, Height=2, MipLevels=1, BC3, DEFAULT, SHADER_RESOURCE and no clamping or
  padding, then `ID3D11Device_CreateTexture2D` (:1433) returns 0x80070057 and prints the line (:1435).
* [I] Cause: D3D11 requires a block-compressed (BC1-BC5) texture's top-level width and height to be multiples of 4. Height 2 is not.
  Backed by the same log: a 256x4 DXT5 (`0x839D8000`, 1024 bytes) uploads fine at line 1053 of the log, 256x2 fails. No mip-count,
  bind-flag or format-support problem: levels=1, plain SRV, BC3 is mandatory on FL10+.
* [V] The guest data is not the issue: `compressed_size` (:866) rounds to 4x4 blocks, so 256x2 is 1024 bytes, one block row, the
  same bytes as 256x4. Only the host texture creation is refused.
* [I] The code path has no check for this anywhere: `d3d8_CreateTextureImpl` neither pads nor falls back, for DXT1/3/5 alike.

## 2. Who asks, and why it repeats

* [V] Only the NV2A "ready texture" bind asks (draw-time bind of whatever texture the guest has set). Not the video player, not texture packs.
* [G] What the texture is: a 256-wide, 2-high DXT5 strip, i.e. a gradient/ramp or fade/letterbox band used by the cinematic (the head
  of the log has a 256x4 sibling, same address style). No log line names the address, so this is not identified.
* [V] The failure is never cached. On a miss the code evicts the LRU slot (:1034-1063, with the "texture cache full; evicted" line,
  counter in `evicted`), calls CreateTexture, fails and returns before storing an entry (:1071-1075). The next bind of the same
  texture misses again. In the tail of the log the eviction counter rises by exactly one per failure line (#2395000 at line 8810 ->
  #2439000 at 52764, vs 44,000 failures between). The head had only #1000 evictions in all of the Story play, so the cache churn is
  caused by this failure, not by real cache pressure.
* [I] Real count is about 2.44 million, not 44,432: the log is cut ("139970831 bytes left out here; 1953460 noise lines left out",
  line 8507), and 44,432 is only the kept tail (lines 8508-53032). Eviction counter 2.44M / ~460k frames (vblank 36600 -> 498000)
  = ~5 per frame on average, ~45 per frame in the kept tail (about 530 draws per frame in all).
* [V] Drawn instead: `SetTexture(stage, NULL)` (:1073). `dev_SetTexture` (`d3d8_device.c:947`) binds a NULL SRV and sets COLOROP to
  DISABLE. [I] In the combiner path a NULL SRV samples (0,0,0,0) (see `docs/research/hd-white-textures.md` B1): the strip is black or
  transparent, never white. Whether anyone saw it is not known.

## 3. Cost, and the stuck cinematic

* [V] Frame rate stayed at 59.5-60 fps through the flood (the `[D3D] 2.0s` lines), so CPU cost is small: per failure one 2-calloc/free,
  one failed driver call, one unbuffered `fprintf(stderr)`. No leak: both allocations are freed on the failure path (:1436-1437).
* [I] Real cost is disk: the elided 140 MB is almost exactly 2.4M lines x ~57 bytes, so the player's log file grew by ~140 MB in 2 hours
  (and the report buffer lost everything else: 2.2 h of context compressed to the head, the flood and the tail). Plus, per failure,
  one live cache entry may be lost (the eviction) until the dead slot becomes the LRU.
* [I] Not the cause of the stuck cinematic: the game logic does not wait on the texture, the frame loop and the vblank gaps are
  normal, and the flood stops at line 53032 while the FE reload starts (line ~53936). Failure only removes one strip from the picture.
  Why `sc14` ran two hours is open ([G]: waiting for a skip/confirm the player never gave, or a scene-end event not firing). The
  flood does not need to be fixed to look at that; the next run should log the stage/script state instead.
* [V] Unrelated flood to not confuse: owner logs `logs/run-20261020-133253.log.err` and `-133258.log.err` hold ~189k failures each,
  but with 0x887A0005 (DEVICE_REMOVED) over many formats/sizes (fmt 15/18/6, 8x64 .. 1024x32). That is a lost GPU device, not this.

## 4. How common

* [V] `logs/bug-reports` (165 reports): the 256x2 / 0x80070057 line is in 2 only. `20261028-153802.380-b185a7` (build 35, HenryGood):
  2 lines in a Story/Shops stretch, no flood. `20261102-004740.975-2f07f1` (build 39): the flood above. No other report has
  any "CreateTexture2D failed" line. [I] The 256x2 strip exists in more scenes than the cinematic (b185a7), but only a long-lived bind of it
  (the sc14 scene) floods.

## 5. Fix proposal

Fix in the toolkit (fork commit, `tools/xboxrecomp`, per AGENTS.md section 4), both in `nv2a_pgraph_d3d11.c` so the D3D8 layer stays unchanged:

1. **Show the strip (main fix).** In `pgraph_d3d11_set_texture_ready`, define
   `soft_dxt = compressed_size(fmt,w,h) && ((w & 3) || (h & 3))`. When true: create the texture as `D3DFMT_LIN_A8R8G8B8` (0x12)
   at the logical size (:1071) instead of `nv2a_format`, and in `ready_tex_fill` (:918) decode each texel with
   `d3d8_dxt_decode_texel(data, fmt, x, y, w, &argb)` (`TK/d3d/d3d8_swizzle.h:342`, DXT1/3/5, already used by `d3d8_vk.c:1432`)
   into the locked rows (`lr.Pitch`) instead of the `memcpy` at :938. Keeps 256x2 exactly, no UV change; 2x2 and 1x1 get the same
   treatment. The cache key, the signature (:1004) and the "bytes" accounting stay as they are (guest bytes, 1024).
   Rejected: padding the height to 4 in `d3d8_CreateTextureImpl`: v in [0,1] would then span rows 2-3 that the game never defined
   and would shift the gradient. Rejected: a general fix in `d3d8_CreateTextureImpl` for all callers: the per-mip case (a 4x4+ top
   level with 2x2 and 1x1 tails) is valid in D3D11 and must not take the decode path, so the condition has to be on level 0 only.
2. **Never retry a failure (safety net).** Add a negative entry: a small `s_ready_fail[16]` keyed addr/format/w/h, checked at the
   top of the lookup (before :985); on a CreateTexture or fill failure (:1071, :1076) store it, `SetTexture(NULL)` and return 0 on
   later hits, without the eviction. Print the failure once per key (`[PGRAPH-D3D11] texture 0x%08X %ux%u fmt 0x%02X cannot be created`)
   and drop the unbounded `fprintf` in `d3d8_CreateTextureImpl` to the first 8 per (fmt,w,h). Invalidate the negative entry when the
   guest rewrites the address (the existing signature check) so it can recover.
3. Test: add a case to `tests/d3d8_smoke` or the ready-texture fixture (`ready_texture_sig.h` already has a hardware-free fixture)
   that binds a 256x2 DXT5 block row and asserts it is created and samples the expected two rows; log `[TEXUSE]`/address once
   (`RECOMP_TEX_FORMATS=1`) to name the texture the next time.
4. Log hygiene: also cap the same line in the bug-report noise filter (repeated line collapse) so one failure cannot eat the report.

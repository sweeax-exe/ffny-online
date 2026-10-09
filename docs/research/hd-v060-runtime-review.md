# HD v0.6.0 runtime source review

Read-only review of the current HD lane on 2026-10-08. The reviewer inspected
the pack loader/header, push-buffer and D3D11 integration, parent settings/UI,
capture change, focused fixture and relevant D3D8 backend implementation. No
build, test execution or game run was performed. Line references describe the
reviewed working files and may move with subsequent fixes.

No unresolved release blocker was identified in this bounded source review.
The two initial P2 findings below are resolved in the current source, and the
reviewer found no new defect in those fixes. The parent reports that the updated
focused CTest passes 1/1; the reviewer inspected the test source but did not
execute it independently.

## Resolved P2: odd PNG mip dimensions discarded the trailing edge

The original reduction floor-halved dimensions and sampled only four texels
starting at `x*2,y*2`. For an odd dimension greater than one, its final
column/row contributed to no destination pixel. A 3 × 1 image with opaque black
in columns 0–1 and opaque blue in column 2 produced an entirely black 1 × 1 mip.
Both opacity and independent-channel policies were affected.

The fix at `tools/xboxrecomp/src/nv2a/texture_pack.c:335–346` partitions the
entire source extent into nonempty integer sample regions. Its last regions
include the final row/column; even reductions retain the original 2 × 2 box.
Both channel and opacity reduction use the actual sample count. Width/height
bounds keep coordinate products and colour/alpha sums within uint32 capacity,
and one-dimensional reductions have nonzero sample counts. Cleanup and resident
accounting are unchanged.

The new synthetic 3 × 1 case at `tests/texture_pack/test_main.c:133–141`
expects a blue contribution of 85 and full alpha, so it detects the reported
edge loss. Remaining test limits: no separate odd-height case, odd varying-alpha
case or odd independent-channel case. The source uses the same extents for both
axes and policies; this is a coverage limit, not a newly identified defect.
The fix is a simple integer-region box approximation, not a claim of exact
continuous area filtering, gamma-correct mip generation or atlas isolation.

## Resolved P2: temporary allocation failures became permanent rejection

The original helpers returned permanent rejection for some WIC/heap and DDS
device failures, preventing a valid replacement from recovering later in the
process. The current code propagates WIC/heap `E_OUTOFMEMORY` through a retry
flag (`texture_pack.c:244–261,307–310`), returns `-1` for PNG padding, mip and
device/upload failures (`316,326,353`), and distinguishes malformed DDS payload
from allocation/device failure (`378–394`). Invalid dimensions, unsupported
formats and truncated DDS remain permanent failures. Failed partial textures
and temporary buffers are released before returning, and resident accounting
only increments on complete success.

`texture_pack_bind():467–468` routes the negative result to a one-second retry
instead of setting `failed`. New mock CreateTexture failure followed by success
checks cover PNG and DDS (`test_main.c:136–137,169–170`). The fixture still
calls helpers directly, so the bind-level timer/failed state and actual WIC or
heap exhaustion are not injected. These are remaining coverage limits; the
source now provides the intended retry path for the reviewed failure cases.

## Checks that look sound in the reviewed source

* **Identity:** `texture_pack.c:185–219` hashes a versioned LE descriptor plus
  complete tight source rows/blocks and full palette. Address is omitted;
  dimensions, format and physical row-texel width remain part of the identity.
  Bounds/readability are checked before refresh. The fixture has a shared known
  answer and pixel/palette mutations, stride padding and inaccessible-page
  checks (`test_main.c:70–88`).
* **Source cache:** lines 402–438 mix page-aligned addresses and palette pointers,
  probe four slots and rehash no more than once per 8 ms per cached shape.
  This retains the documented bounded interval in which an in-place mutation
  can use a previous identity. The disabled bind path returns before hashing,
  WIC activity or file access after one-time initialization; the executor still
  makes a small active check and format-size lookup.
* **Memory accounting:** PNG sums every level at eight bytes per texel
  (`texture_pack.c:324`), covering retained BGRA CPU storage and BGRA GPU payload.
  DDS uses twice the validated block payload (lines 374/392), covering retained
  blocks and BC GPU payload. This matches full CPU-chain allocation and native
  DXGI format/mip creation in `src/d3d/d3d8_resources.c:44,94–105,1416–1451`.
  The budget counts resident texture payload, not driver allocation overhead,
  decoder internals, manifest metadata or short-lived load buffers.
* **Eviction and lifetime:** `make_room():278–297` excludes replacements bound
  on any of four stages. `release_entry():264–276` unbinds all matching stages
  before dropping the cache's ownership reference. This matters because the
  actual backend's `dev_SetTexture()` retains a bare pointer
  (`src/d3d/d3d8_device.c:729–765`). The fixture covers all-four-stage protection
  and eventual release; its mock additionally owns stage references.
* **Validation and fallback:** manifests restrict names/relative paths,
  extensions, schema, PNG mip policy and entry count (`texture_pack.c:62–133`).
  PNG validates a uniform 1–8 scale and restores physical row padding. DDS
  validates supported BC formats, 2D layout, level bounds, exact payload length
  and matching scale (`299–394`). A rejected or missing replacement returns
  to the original upload path (`src/kernel/nv2a_pb_exec.c:2766`).
* **Guest state:** cube textures, tracked render surfaces, YUV and depth formats
  remain excluded (`nv2a_pb_exec.c:2727–2730`). Replacement binding preserves
  guest dimensions/coordinates; the normal address/filter calls still follow
  the bind. `nv2a_pgraph_d3d11.c:1129–1132` enables guest mip filtering only for
  replacements and resets it for original fallback while packs are active.
* **Settings/captures:** parent setting bounds match loader limits: cache
  32–4096 MiB, dump limit 0–65536, restart required. Environment setup follows
  launcher edits (`src/hooks/pc_settings.c:448–468`). Native captures are opt-in;
  default regression dimensions remain unchanged
  (`src/hooks/d3d11_translator.c:505–512`).

## Validation boundary

The focused C fixture covers real WIC/SHA with synthetic images and mocked
D3D8 resources, not a real GPU draw. It covers palette/stride identity,
alpha-weighted and channel mips, row padding, DDS truncation, later-pack
precedence, invalid policy, all-stage eviction protection and balanced resource
release. The revised source also checks an odd-width mip and helper-level
recovery after injected PNG/DDS CreateTexture exhaustion. Assertions remain
enabled in Release via its CMake configuration. It does not currently exercise
bind-level source-cache/retry timing, actual WIC/heap exhaustion, separate
odd-height/odd-alpha/channel cases, real sampler selection or real GPU failure
behavior.

Parent builds/regressions and owner playtest remain the runtime acceptance
evidence. This report alone does not close the milestone or claim parity for
every texture, generated font page, animated asset or non-Windows renderer.

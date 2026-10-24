# Press Start fire-escape transition mapping — 2026-10-20

Mapping phase: read-only metadata and source-image inspection for the reported Press Start → Main Menu downward pan. No game run, build, extraction, asset export, or artwork edit was performed. Source bytes remain in the user's own dump; this report records metadata and conclusions only.

## Finding

The cited runtime source IDs belong to the retail transition movie `screens/screens.viv::screens/trans/t1.big::t1.xsh`, not the similarly named test movie `screens/test/trans1.big`. `apt-dump.py` identifies `t1` as a 640×2000 movie with a single visible Shape 11, placed at `(0,0)` on frame 0. The local object audit ties each bitmap below to a distinct material part of that same Shape. Visual inspection of the existing local corpus originals confirms fire-escape/stairwell scenery.

`docs/research/m4a-fe-transitions.md` identifies `clip2` as the pan effect clip hosting `trans/t1`; for `panDown`, the loader moves `clip2` from y=480 toward y=`-g_iSize`. It also states that `clip2`'s inherited placement is set by its host, outside `feloader.apt`. This connects the reported transition to `t1`, while leaving the exact visible crop and runtime geometry unsettled.

## Exact requested source inventory

All five entries are type 98, format 15, unswizzled SHPX images. Names are the four-character SHPX bitmap names (trimmed here), not directory indices. Indices, full IDs, dimensions and names come from `fresh-screens-metadata.json` and `all-menu-texture-aliases.csv`.

| Runtime prefix | Full source ID | SHPX index / name | Original canvas | Shape 11 material part | Inferred Shape-local rectangle (pixels) |
|---|---|---|---:|---:|---|
| `90c3ba5f36d9` | `90c3ba5f36d98cb7f5f1d80d9f6f819fb9e32891aa0656f70fe296a96c37f253` | 1 / `10` | 512×512 | 0 | x 0–512, y 512–1024 |
| `0d9299ca3f85` | `0d9299ca3f85f11cc13a2d97119e20e113bb2a08572e039db217db6ca962cdd0` | 6 / `6` | 512×512 | 4 | x 0–512, y 1024–1536 |
| `8327612c6a9f` | `8327612c6a9f057fdea610860b7fb745b180543cd65c5978b157c83a439905cd` | 4 / `4` | 512×512 | 6 | x 0–512, y 1536–2048 |
| `8736c571e654` | `8736c571e654cccc4f533a7677af8e59431b9b9529a86c2677def42f1b392d40` | 2 / `2` | 512×512 | 8 | x 0–512, y 2048–2533 (Shape bbox clips at 2533) |
| `dd4feccde8c4` | `dd4feccde8c4fa9dee63636252e21bcfbb1cc2efd74d03ac6cc40f5d25c39c1d` | 8 / `8` | 256×512 | 2 | x 512–641, y 0–512 |

The rectangles are a **static decode inference**, not measured screen coordinates. They follow Shape 11's decoded compressed quad vertices and material matrices, using the inferred vertex-scale formula documented in `menu-pilot-uv-layout-2026-10-17.md`. The parsed Shape bbox is 641×2533; the movie stage is 640×2000. The t1 movie's placement confirms all parts are in one authored group, but not which portion is on screen in each pan frame.

For context, the same Shape also references bitmap names `1`, `3`, `5`, `7`, and `9`; they are siblings in the tall composition. Their source prefixes are `2f7823f2ca85`, `a4a02a3f4424`, `0a57126a9840`, `b870dce8d1fd`, and `51ebe2dcf268`, respectively. Their original canvases are 256×512, 256×512, 256×512, 256×512, and 512×512. They should be inspected and treated as part of the same art family before splitting any restored scene; the reported runtime IDs alone do not prove those siblings are unused.

## Restoration group recommendation

Treat all ten Shape 11 bitmaps as one continuous, vertically assembled fire-escape backdrop. Restore/outpaint from a common tall master and retain each source tile's original canvas, order, overlap, and registered seams when splitting back into texture replacements. Prioritize the five runtime-reported sources, while including the five siblings in the master review so tile boundaries or lighting do not diverge. The 256×512 name-8 strip is the narrow x=512–641 edge of the same Shape, so include it in edge planning rather than treating it as an unrelated background.

For widescreen side art, extend this shared master horizontally while preserving the original 4:3 composition at the center. How much of the master is exposed during the downward pan, which frame is the settled alignment, and whether these tiles are sampled by all relevant runtime frames remain open. Measure `per` and each anchor's settled `x0` with the existing `RECOMP_WS_WIDE_TRACE=1` path before authoring exporter layer geometry; the decoded Shape-local rectangles are useful bounds, not a substitute for that measurement.

## Existing exporter registration

`scripts/export-menu-ui-wide.py` reads layers from `config/menu-ui-wide-layers.json`. For each source anchor it requires: a source ID present in the approved artwork catalogue's `reviewed_sources` (for dimensions); an approved, hash-pinned master path; a replacement for the anchor source ID in one of the configured `anchor_packs`; measured `per` and `x0`; and the side policy (`sides`, plus any needed `match_u`/`u_range`). It matches the replacement against the master, cuts slabs using that anchor's geometry, and writes the `[wide]` registration as `source_id=dt:path;...` into the new component's `manifest.ini`.

Therefore, supply five (preferably all ten, for the shared scene) layer entries against the same approved tall widescreen master, and supply those entries' source-shaped replacement textures through a configured anchor pack. The master must be pinned in `config/menu-ui-approved-artwork.json`, and every chosen anchor must be present in its reviewed-source size table. The existing exporter does not infer transition-pan placement, add the source IDs to its configuration automatically, or register arbitrary scene groups without those layer entries. No transition anchors are currently listed in `config/menu-ui-wide-layers.json`; its existing menu slabs and packet-list fix are separate and already working.

## Evidence read

- `docs/research/menu-ui-playtest-pack-2026-10-19.md`, gap 2: the transition source prefixes are unresolved widescreen scenery and currently show black sides during the pan.
- `docs/research/m4a-fe-transitions.md`: `clip2`/`trans/t1` relationship and panDown motion; inherited clip transform is host-defined.
- `scripts/apt-dump.py screens/screens.viv screens/trans/t1.big --frames`: 640×2000 movie, two frames, Shape 11 at origin.
- `logs/ui-restoration-20261017/fresh-screens-metadata.json` and `all-menu-texture-aliases.csv`: SHPX names, indices, dimensions, source IDs.
- `logs/ui-restoration-20261017/uv-audit/audit_pilot_uv.py` and `logs/ui-restoration-20261017/next-scope/audit-result-panels.py`: existing archive, ELF, APT, relocation, vertex and UV audit conventions; the latter path is under `logs/ui-restoration-20261017/next-scope/`.
- `scripts/export-menu-ui-wide.py`, `config/menu-ui-wide-layers.json`, and `config/menu-ui-approved-artwork.json`: current wide-slab matching, sizing, approval-pin, and pack-registration requirements.

## Ten-line handoff

1. All five prefixes are `screens/trans/t1.xsh` in retail `screens/trans/t1.big`, not `test/trans1`.
2. `t1` is a 640×2000 movie; frame 0 places only Shape 11 at `(0,0)`.
3. `90c3…` = index 1/name 10, 512×512, material 0, inferred Shape-local x0–512/y512–1024.
4. `0d929…` = index 6/name 6, 512×512, material 4, inferred x0–512/y1024–1536.
5. `832761…` = index 4/name 4, 512×512, material 6, inferred x0–512/y1536–2048.
6. `8736c5…` = index 2/name 2, 512×512, material 8, inferred x0–512/y2048–2533.
7. `dd4fec…` = index 8/name 8, 256×512, material 2, inferred x512–641/y0–512.
8. Static rectangles use an inferred packed-vertex formula; runtime screen `per`, `x0`, pan crop and sampled frames remain unmeasured.
9. Recommended art group: all ten t1 Shape 11 tiles, with one shared tall fire-escape master and the reported five prioritized.
10. Export needs pinned master art, approved catalogue dimensions, anchor replacements in a configured pack, and measured layer rows in `menu-ui-wide-layers.json`.

## Subsequent main-agent runtime check

The existing combined PR62/73 Mac build was run once with all six menu packs at
32:9 under the complete-save guard (`logs/run-20261020-100730.log.err`). Thirteen
native captures show the title, autosave warning and settled main menu; the
shot schedule missed the brief pan. Zero crash/watchdog/ICALL failures and exact
save restoration do not establish transition coverage. A second schedule anchored
to main-menu load is prepared in local `probe-transition-pan.py`, but refused
to launch while another checkout's game was running. That process was left
untouched pending the owner's answer. No runtime geometry values were measured,
no transition art generated, and no wide registration added in this pass.

## Later authoring correction and continuation

Vlad answered: leave the launcher-redesign game running and continue textures.
A full ten-tile audit found bitmap9/part1's decoded quad atx512..1024,y512..1024
despite texture translation0,0. The existing baseline trace confirms that
placement below the viewport; its cause/visibility is not settled. Thus the
continuous top-left image reconstruction is a **texel-space art study**, not
a proven runtime render. Bitmap9 stays Faithful-only; no geometry patch was
made. Restoration/side artwork now proceeds separately; see
[the artwork report](menu-fire-escape-artwork-2026-10-20.md). This supersedes
the earlier note that no transition artwork has been generated.

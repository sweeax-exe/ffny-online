# HD audition gallery audit

Read-only source audit of `scripts/hd_review.py` and the RGB bleed, padding and upscale helpers in `scripts/hd_assets.py`. No code changes, inference, builds, game runs or tests were performed. The gallery is a review artifact, not an accepted release pack.

## Initial findings and follow-up

1. **Stale candidates can remain under a new asset's labels (P2).** `hd_review.py:146` changes the selected asset's title, metadata and choice controls before awaiting image loads, but does not clear `images` or the canvases. While loading, `draw()` at line145 can redraw the previous asset using the new asset's dimensions. If loading fails, those previous images remain indefinitely; a reviewer can select a model for the newly labelled asset while seeing old candidates. Clear/disable the canvases and review controls at the start of each load; restore them only for the matching load token. Token-guard the failure branch too so an older request cannot overwrite newer status.

   **Resolved by the main agent; confirmed on source re-read.** Line146 now clears images/canvases/links, disables choice/notes while loading, restores them only on the matching success token, and token-guards failures.

2. **Displayed scale and exported review scale can disagree (P2).** `hd_review.py:147` stores `scale` only when the choice or notes change. Switching the scale calls `load()` at line146, which restores the same choice but does not restore or display its saved scale. Example: choose RealESRNet at4x, switch to2x, then export without changing the choice/notes: the gallery visibly shows2x and the RealESRNet choice, but the export still says4x. Keep the selected candidate's scale explicit in the choice UI/export, or store separate decisions for each scale. Changing view scale should have a defined effect on the recorded decision.

   **Resolved by the main agent; confirmed on source re-read.** Lines144,146–147 now key decisions by sample plus scale, so2x and4x choices are independent. Storage changed to version2.

3. **Open: saved decisions are not tied to this asset selection (P2 for repeated gallery builds).** `hd_review.py:142` loads one global `defjam-hd-review-v2` localStorage object. Lines144,146–147 key decisions by the ordinal `sample` label plus scale without checking the stored `source_id`. Regenerating a gallery with a different asset at the same sample label can display the previous asset's choice and notes; export also includes old entries outside the current selection. Namespace storage by a selection fingerprint and validate every restored source identity. Export only decisions belonging to the current selection, including its fingerprint/model provenance. This does not block the first fresh40-asset gallery, but matters when this workflow is repeated.

## Checks that look correct by inspection

- Model columns match the generated directory names and MODELS ordering (`hd_review.py:15`,39–47,71–89,108–114,142).
- The2x candidates contain exactly twice each original dimension; alpha is resampled directly from the original instead of reducing neural alpha (`hd_review.py:83–87`). The advertised2x colour derivation from4x is accurate.
- RGB-only channel files preserve RGB beneath zero alpha and avoid browser canvas losing that data (`hd_review.py:95–103`).
- `bleed_rgb` fills zero-alpha texels and retains their original alpha; padding clamps/wraps independently on U and V (`hd_assets.py:282–318`).
- Neural processing feeds RGB, validates padded4x dimensions, crops padding, and recombines separately filtered original alpha (`hd_assets.py:347–377`).
- Canvas positioning uses common target dimensions and shared pan/zoom; it aligns candidates by source location at a common output scale (`hd_review.py:145`,147).

These findings concern source behavior. Generated gallery inspection and browser interaction remain the main agent's verification steps after fixes.

Main-agent follow-up: finding 3 is now resolved. Storage v3 is namespaced by a
SHA fingerprint of ordered source IDs/original hashes, all model recipes and the
review runner. Export filters entries to current source IDs and scales and carries
the fingerprint. JavaScript syntax passes; browser verification follows rendering.

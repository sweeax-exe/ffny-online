# Menu/UI remaining census — 2026-10-20

This is a source-ID census, not an estimate of how many replacement paintings or image-generation jobs remain. It reconciles the retained 13,170-row alias CSV, the 53 registered-screen rows, the 136 reviewed source IDs now recorded in `config/menu-ui-approved-artwork.json`, and the 22 unique sources in `controls-v1-20261020/sources.json`. The controls manifest is now fully represented in the config (22/22; delta 0). The 16-item/19-source `menu-details-v1-20261020` batch remains pending and is excluded from reviewed coverage.

## Current counts

The approval config now contains 136 distinct reviewed IDs, including the 22 approved Gym/control sources. None of the 19 pending menu-details IDs overlap approved IDs.

| Census boundary | Unique IDs | Approved / reviewed | Pending batch | Other prior shortlist | Untouched after pending and shortlist | Residual ≥128×128 | Residual ≥256×128 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Registered flows | 1,323 | 115 | 13 | 0 | 1,195 | 331 | 138 |
| Core menu families and support | 1,834 | 132 | 19 | 0 | 1,683 | 603 | 385 |
| Broad UI/menu alias category | 11,261 | 136 | 19 | 0 | 11,106 | 4,918 | 2,509 |

“Untouched” means outside both current reviewed IDs and the pending/shortlist sets. The 23-item confirmed shortlist from the 2026-10-19 inventory is historical: six are now approved and 17 are in the pending batch, leaving no additional shortlist IDs after those are removed. The size-filter columns count residual source IDs meeting only the stated dimensions, not candidate paintings; each ID has one consistent dimension pair in the retained aliases. The scopes overlap. The broad category includes portraits/catalog material and other UI assets. Previous “71 covered / 1,252 registered remaining” figures describe the 2026-10-19 snapshot and are stale for this question.

Registered-flow residual without custom-art review is 1,208; subtracting the 13 IDs in the pending batch leaves **1,195 outside both reviewed and pending sets**. Core residual is 1,702, with 19 pending and 1,683 outside reviewed/pending/shortlist. Broad residual is 11,125, with 19 pending and 11,106 outside those sets.

## Bounded screen-family view

Of the 53 registered screen rows, 48 match source IDs under the retained archive-path rule; five have no matching source IDs in that census. Among the 48 with IDs, 21 have no currently reviewed ID and 47 have at least one residual unreviewed ID (the latter includes the 21 zero-coverage families). `story/gym` has 10 approved IDs and 71 residual IDs. `battle/chsVenue` has 82 residual IDs; its three venue-frame studies are part of the pending 19-source batch, so they are not counted as reviewed. `pageHead` (10 IDs) and `xHead` (6 IDs) also have zero reviewed IDs. A family with any reviewed ID can still contain many unreviewed sources; partial coverage should not be read as completion.

The complete per-family counts and classifications are in the machine-readable output. This grouping is limited to those 53 registered screen names; it does not imply that all game-loadable screens are represented.

## What the census cannot classify

The files used here identify texture IDs, archive paths, dimensions, alpha ranges and review status. They do not provide sufficient visual or draw-call evidence to divide every remaining ID into paper/background art versus fonts, glyphs, photographs, icons, masks, lighting/effects, widgets or support resources. Tiny dimensions or a filename are not enough to make that classification. The bounded screen list does support saying which registered families currently have zero or partial source-ID coverage, but not how many of their residual IDs are artwork candidates.

## Reproduction

Run `python3 logs/ui-restoration-20261020/menu-ui-remaining-census.py` from the repository root. It reads metadata only and writes `logs/ui-restoration-20261020/menu-ui-remaining-census.json`; it does not read or alter game data, artwork bytes, runtime configuration, the game process, or approval state. The JSON preserves all 53 family rows and set-overlap checks.

### Ten-line summary

1. The broad retained alias inventory has 13,170 rows and 11,261 distinct texture IDs.
2. The registered-screen file lists 53 names; 48 match source IDs under the census path rule.
3. The current approval config records all 136 reviewed source IDs.
4. All 22 controls manifest IDs are in the config; the delta is zero.
5. The pending menu-details batch has 19 distinct IDs and no overlap with approvals.
6. Registered-flow scope: 115 approved, 13 pending, 1,195 untouched after pending.
7. Core-family scope: 132 approved, 19 pending, 1,683 untouched after pending.
8. Broad UI/menu scope: 136 approved, 19 pending, 11,106 untouched after pending.
9. Residuals meeting ≥128×128 are 331 / 603 / 4,918 across those scopes.
10. Size-filter residuals mix asset types; they do not estimate actionable paintings.

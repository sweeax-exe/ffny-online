# Menu pack runtime contract — 2026-10-19

The original production playtest records are preserved in the private authoring
archive. This note retains the runtime contract used by the first-pack guide.

## Widescreen backgrounds (D160/UI; toolkit defjam/menu-wide)

A schema-1 pack can provide `[textures]` native replacements and `[wide]` side
slabs or stretch registrations. The original foreground/menu layout and its
texture identities continue to be supplied by the game. A pack's side slabs must
match its native center, sampled UV rectangle, alpha semantics and draw placement.
Assemble art outside the game repo and preserve original geometry/padding.

The renderer accepts integer slab offsets; authoring must account for fractional
source UV spans without emitting a fractional manifest offset. Native acceptance
must cover transitions, masks, scrolling, overlays and each supported aspect.
Native title and Mode Select lighting are separate from artwork approval.

For field syntax and limits, see [texture packs](../hd-textures.md). General
restoration/export procedures live in [Modding Tools](https://github.com/vyanhursky/DefJam-Modding-Tools).

# Shared map border, heading and venue pins (#162 / PR #190) — 2026-11-05

[Issue #162](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/162)
comes from two consumers of the same map artwork. Story uses
`battle/chsVenue.swf` and `screens/battle/map.big`. HD Widescreen Menus has
zero-offset side copies of map bitmaps 9 and 5 with the Shop District inset
erased for Battle. Story actually uses that inset. When its shop pan carries
the inset across the centred 4:3 boundary, the cleaned side copy paints ordinary
geography over part of its black heading backing and border. The cutoff is
persistent, separate from cache eviction (#141/#182).

## Mode-specific correction

`src/patches/story_map.c` chooses the central draw's current texture for the
zero-offset side replay of these two source identities **during Story only**:

| Bitmap | Source identity |
|---|---|
| 5 | `7b4754dfd82c09030171a6198634968b504062529df77cbc943be5235ca19927` |
| 9 | `a1f9480b6262480bbc67ea60b4461949304fc8c474fd99ca5df7880319dfcc8a` |

The other offsets still use the pack's exterior extensions. Battle retains the
cleaned copies. This side-replay correction leaves no-pack and 4:3 rendering
unchanged. All
published pack images, manifests and catalogue records remain unchanged; a game
update fixes existing installations without a new texture-pack download.

`cheats_in_story()` exposes the existing front-end context tracker, independent
of any cheat being enabled or a profile being valid. It enters Story on
`Game.StartStoryMode` and leaves on `Game.StartMainMenu` or an `intMain/` screen.
A shared `battle/` screen does not change that state. This avoids guessing the
mode from the map's screen name and avoids another guest hook or re-lift.

The toolkit adds an optional application replay filter, keyed by full source
identity and source-texel offset. The pgraph side-replay code already retains
the central texture binding. It passes that binding to the loader's new replay
entry point; an accepted filter selects it directly. This also reuses the
original GPU texture while asynchronous replacement loading is incomplete.
There is no second decode, upload or image to keep resident. A missing central
binding skips that replay, and the normal replay-end path restores/releases
the retained binding. With no application filter, existing pack behavior is
unchanged. Game-specific identities and mode policy stay in the game source.

## Why the manifest-only candidate was rejected

A local diagnostic candidate redirected these two `0:` references from
`components/menu-ui-venue-wide-draft-v6/slabs/<id-prefix>_0.png` to the existing
`anchors/<full-id>.png`. It fixed Story, but a 16:9 Battle pan toward 125 Street
Station exposed a blank corner of the unused inset at the far-left edge.
The owner chose to preserve both screens. No candidate pack or catalogue update
was published; its local archive and receipts are investigation evidence only.

The exact released 1.4.0 archive has SHA-256
`f3d79f86a024b5786919ef2d6f38b1f599cb49a151d031b4a85023a5f480848d`.
Initial 16:9 reproduction used locally installed 1.1.0. The affected two anchors
and two cleaned side images in 1.4.0 have identical decoded RGBA to 1.1.0,
with different PNG compression. A separate 16:10 control reproduced the defect
using the exact 1.4.0 manifest and images; final qualification uses that
unchanged 1.4.0 content.

## Reproduction and scope

`harness.py run story-map-shops --png` selects the existing third Story profile,
opens the map, enters Shop District, then highlights clothing and gym. Like the
existing `gym` route, it needs that save-profile layout. Its completion anchor
requires `Crib.EnterMap`. Native captures at 13/19/26 seconds after the Crib
screen show district overview, clothing and gym. Set `RECOMP_ASPECT` and the
texture-pack environment variables explicitly; the harness otherwise uses
stock 4:3. The complete-save-root guard verifies byte-identical restoration.

## Foreground heading correction

Stock 4:3 also crops the left part of the **foreground** Shop District heading
when highlighting the clothing/gym pins. The owner flagged the remaining
`P DISTRICT` crop in the border-only qualification captures. The background
texture correction alone did not fix it.

Offline APT inspection confirms that `battle/map.big` places a live text field
named `shopText` at `(60, 432.9)` and assigns the complete `SHOP DISTRICT` string
(or `MAGASINS` for French). The heading is not baked into the repaired map
bitmaps. In `chsVenue`, entering Shops calls `moveToVenue` with a shop pin;
that function pans the shared map according to the pin position and adds a
small random offset. The renderer deliberately clips ordinary centred menu
batches to the transformed guest viewport, while the pack's map side replays
can extend beyond it. This explains why the background box can continue into
the wide margin while its foreground heading stops at the 4:3 edge.

The guest still submits the missing glyphs. The correction tags this field's
placement matrix in memory, through the existing APT load hooks, during Story
in widescreen only. The tag adds 16384 to its y coordinate. The renderer removes
that offset before drawing, retains centred horizontal placement and widens
only this tagged batch's full-width horizontal scissor to the canvas. Its
vertical scissor and any narrower guest mask remain intact. The independently
tagged song notification still anchors left, and untagged menu batches keep
their normal clipping. No new guest hook or re-lift is needed.

The patch checks the APT magic, authored matrix, field name and original y
before writing. It is idempotent and leaves unknown layouts unchanged. It
changes neither the string nor its x position, preserving localization and
alignment with the backing. In the 16:9 clothing comparison the only changed
pixels are the recovered letters, in the 82-by-36 rectangle immediately left
of the old clip boundary. The native captures establish that glyph generation
was intact; no guest text renderer replacement is necessary.

This fixes the horizontal cutoff. The original vertical map pans remain: gym
can put the district heading behind the separate `MAP` header, and barber can
pan it off the top. The 4:3 control retains that framing and its original left
crop. Captures cover those existing overlaps rather than claiming the heading
is fully visible during every authored pan.

Focused synthetic fixtures cover Story/Battle transitions on the same source,
unrelated identities and offsets, original-texture fallback, rebinding after
another side slab, invalid inputs and callback shutdown. Local native logs,
captures, source/toolkit commits and exact qualification limits are recorded
in the feature PR handoff. Captures, saves and private diagnostic artifacts
remain local.

## Battle venue pin clipping

Normal Battle uses the same `chsVenue` movie. With an eastern venue selected,
the map pan carries neighbouring pins past the centred 4:3 boundary. Their
paper and icon stop at that boundary while the map artwork continues into the
widescreen margin. This is the same foreground scissor limitation as the Story
heading, independently of the mode-specific background correction.

The correction tags the four static visual children of the shared pin widget
(sprite 132): `blackPaper`, `greyPin`, `paper` and `pin`. Their local placement
matrices carry the existing 16384 y tag; the renderer removes it and extends
the full-width horizontal clip. Dynamic venue artwork attached inside these
containers follows the tag. The parent `pin<FEID>` coordinates are untouched:
ActionScript's `findPins` and `moveToVenue` still see authored navigation and
pan positions. Tagging those parent coordinates would change map behaviour.

The APT load hook applies this to `chsVenue` in both Story and Battle, only in
widescreen. It validates all four placement headers, names, matrices and y
values before writing any; unknown layouts remain unchanged. Repeated loads
are idempotent. Narrower guest masks, vertical clipping, ordinary menu batches
and 4:3 retain their existing behaviour. No additional toolkit change, pack
update or re-lift is required.

`harness.py run battle-map-edges --png` uses Free For All and traverses
Foundation → Scrapyard → TerrorDome → Heights → Limit → Chop Shop, then returns
west and north. Native captures after each pan cover neighbouring pins at the
left edge. Set `RECOMP_ASPECT=16:9` or `16:10` and the released Menus pack
explicitly; the route otherwise uses stock 4:3. It uses the same saved Battle
profile/venue availability as the existing `ffa` route and restores the entire
isolated save root byte-identically.

The exact-route comparison against the original PR build (`a473281`) also
records a pre-existing thin black background strip near the right edge of
the Chop Shop pan with released Menus 1.4.0.

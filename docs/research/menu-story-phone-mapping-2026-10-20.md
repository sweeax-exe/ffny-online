# Story message-center phone mapping — 2026-10-20

The `msgCentr` APT places shape112 at the native origin, depth3, in a 640×480
movie. Its APT bounds are exactly `[0,0,640,480]`. The source images are names
110 (512×512, ID `54b10930f972c0d5ae001c813724eca77efa75a8aed86439b646a9b2723c70f8`)
and 111 (128×512, ID `e54d2ed820d536230ff2f1d0eab2229755a6e99bad7d8e1ab8577d2582602274`).
They form the expected contiguous 640×480 phone-shell layer: image110 part0
covers x=0..512.015625 with UV x=0..512.015625; image111 part1 covers
x=512.015625..640.125 with UV x=0.015625..128.125 and material translation
(-512,0). Both cover y=0..480 with UV y=0..480. This is shell artwork in a
full-screen shape, not evidence that the bitmap pair depicts a full scene.

There is a third part in the same shape: part2 reuses image110 at inferred
geometry x=640.125..1152 and UV x=640.125..1152, y=0..480. It lies wholly
outside the shape's 640-pixel APT bounds and the movie viewport. It must not be
silently folded into the visible shell or “corrected” based on this metadata;
it is a native geometry/UV exception pending runtime interpretation. The
three-part arrangement means image110 is not just the left half in the object
data. Root frame0 places shape112 at (0,0), depth3; frame1 has no placement
changes, and frame2 contains an Action. The root background is opaque RGB
153/153/153.

The two visible pieces meet at their inferred geometry edges, but UV endpoints
are fractional: part0 ends at 512.015625; part1 starts at 0.015625 and ends at
128.125. Thus there are 1/64-pixel start/end offsets and a 1/8-pixel excess at
the narrow image's far edge. Do not round these values or assume integer
source crops; fractional sampling can touch adjacent texels at joins. Part2
has the same 1/8-pixel start offset beyond the 640 screen edge. The compressed
vertex-to-screen coordinate formula is inferred from the supplied UV audit
helper and quantized vertices, not an independently established hardware
transform. Treat these decoded coordinates as a strong static mapping, with
that inference and the out-of-bounds part called out.

Both originals have alpha range 136..255 and zero fully transparent pixels.
Image110 has 32,475 partially transparent and 229,669 fully opaque pixels;
image111 has 14,531 partially transparent and 51,005 fully opaque pixels.
Alpha therefore carries real edge/detail opacity, but no pixel is fully clear.
Preserve and composite the source alpha as straight coverage (or retain the
source representation expected by the exporter); black RGB cannot be used as
a transparency key. Even the minimum alpha is about 53.3% opacity, so black
pixels remain visibly dark when composited over the movie's gray background.

The root also places character128 at depth4, instance name `headrAdjust`, at
(115,109.5), above the shell layer. Keep that foreground child and its depth
live. The available metadata does not resolve its glyph/string contents or
enumerate any dynamic phone text, numbers, logos, keycap labels, or lighting
sub-elements. Protect all such screen/UI/keycap lettering and authored
foreground marks during any shell restoration; do not paint guessed text or
icons into names110/111. This is a static placement audit, not a live overlay
inventory or runtime validation.

Evidence: `logs/ui-restoration-20261020/phone-metadata.json` and
`phone-audit.txt` (APT SHA-256 `b5e06297fe110e483ebd59cc689282cf5c75988f71b29ade544b36d6f02d3d34`,
object SHA-256 `fa365d0671f4388f32927a7fa3769e38e2e6f6f7366ae8d569dbcee19e1e8475`).

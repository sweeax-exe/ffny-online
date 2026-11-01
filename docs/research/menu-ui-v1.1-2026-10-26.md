# Fire-escape transition geometry repair — 2026-10-26

## Fire-escape rectangle: source geometry repair

The supported dump's `screens/trans/t1.big::t1.o` already contains the misplaced
quad. This is independent of PNG decoding, asynchronous replacement uploads,
depth state, the widescreen shader and side-fill choice. The previous Windows
draw audit established an uncovered region above the first left scenery tile.
This session independently read the original object and reproduced the gap on
the current release-base Mac build, with no texture packs.

Shape 11/part 1, bitmap 9 (`51ebe2dcf268…`), has an identity texture matrix,
but its four vertices decode to x512..1024/y512.042..1024.084. The other nine
parts leave the first x0..512/y0..512.042 cell empty. The object's overall
bounding box ends at x641, so that quad also exceeds its parent bounds.
The source image is the upper-left stairwell artwork; its lower/right
neighbors belong to the same continuous composition.

`src/patches/menu_geometry.c` subtracts 4096 and 1656 from that quad's encoded
x/y coordinates. It joins the next row's encoded y exactly, without a float
round-trip. The identity UV matrix then samples the intended first tile.
The entry hook is `sub_00109F80_enter`: DynamicLoader's ELF parser, whose
input descriptor at ECX holds the raw header at +0x10. The repair runs before
section fixup, relocation and vertex-buffer upload. It changes guest memory;
the dump and replacement images are unchanged.

The hook first checks the known ELF/section layout, then the whole object's
13,089-byte FNV-1a fingerprint (`208bd986`, repaired `abe1d4d6`). Original
object SHA-256 is
`ec5dd8d50a085e36a2758a728b7a588f37d4f0d5ffe8d5fbe02c34d9059db8ff`.
Other objects and repeated loads are harmless. `RECOMP_MENU_TRANSITION_REPAIR=0`
restores the original geometry for comparison; `RECOMP_MENU_GEOMETRY_TRACE=1`
prints at most 32 parser entries. Normal play logs one repair when it loads.

This is a source-informed correction, not a claim of a newly captured retail
console comparison. Native no-pack captures demonstrate that it closes the
reported hole, and trace positions independently confirm the corrected quad.


The original art-review records and export recipes are retained in the private
authoring archive. Source-only restoration procedures live in Modding Tools.

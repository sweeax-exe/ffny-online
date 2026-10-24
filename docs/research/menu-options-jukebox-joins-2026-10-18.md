# Options door and Jukebox masonry joins — 2026-10-18

Vlad accepts the Options left side and likes the restored texture art, but asks
to complete its right door/brickwork using the Mode Select lit envelope as
reference, and regenerate both Jukebox brick joins (D139). This does not approve
the new widened revisions. No other art changes were requested.

Current review: http://127.0.0.1:51738/review.html, **Repaired joins and complete
door · v2**. Previous scenery v1 remains selectable. Original comparisons,
side/wipe/draft modes,4:3center and all five aspects are retained. New full-height
before/after join controls and complete-door/reference links expose the changes.

## Three edits and five built-in calls

Private record: `logs/ui-restoration-20261017/pilot-v1/menus-seams-v2/`.
`generations.json` contains five exact prompts, local raw images, tool-origin
paths, reference hashes and dimensions;54 pilot calls total. Built-in imagegen
was used, with opaque full-height targets and same-aspect normalization.
The actual outputs are1591×988 or1591×989 versus2320×1440 targets. Raw/source
landmark registration and final architectural/mortar alignment were inspected.

Options edits only master x[3520,5120),y[0,1440), immediately beyond original
4:3. All x[0,3520), including the accepted entire left extension and original
center, are exact. The reference is a crop of the existing Mode Select lit wall,
showing the complete shutter/right jamb/threshold/brickwork. Its pixels and
conditional approval remain unchanged. The new Options door ends with a right
jamb and continues into aged masonry. No reference lamp, poster, emblem or
lighting treatment was transplanted. Extra stored85wall columns remain in the
original source-safe tile but are no longer forced into the final widened
continuation: that faulty beyond4:3 study is expressly replaced per feedback.

Jukebox edits master x[0,1600) and x[3520,5120),y[0,1440). Original center
x[1600,3520) stays exact. First two filled-target generations retained too many
short brick rows and were rejected after actual-source reassembly. Reusing a
faulty texture as edit context can preserve its wrong geometry despite a good
prompt. Second attempts remove that field from the target, retain real original
context, and supply seven approximate full-height mortar trajectories. They
regenerate larger courses that follow the original course heights on both sides.
Guide lines are constraints only, never image pixels. The approximate anchors
are documented in course-landmarks.json, not presented as a measured proof of
exact original mortar coordinates. No mirrored/clamped patches or repeated tile
were substituted for regeneration.

Final assembly restores original pixels exactly, then matches residual colors
in generated-side384px bands with one-sided harmonic correction. Grain softness
is blended toward source only within those bands (radius3 Options,1.5 Jukebox),
without filtering the original or changing its palette. Both edges of each band,
full-height joins,complete door and all ten aspect crops were visually inspected.
Mortar course alignment is substantially improved over v1; material detail and
new architecture still require owner review. Numerical edge equality cannot
establish authenticity or invisible joins by itself.

## Checks and reproduction

`verification.json`:319 previous PNG hashes unchanged,2exact original centers,
Options entire left+center exact,10exact aspect crops/centers. Previous source
restoration/alpha/padding,Mode references and approved branding remain unchanged.
`browser-verification.json`:20menu×revision×aspect combinations×4images and4join
pairs×2images load. Wipe25% and center lock work; no console warnings/errors.
39prior static links plus new provenance/close-up links resolve locally.

Order:prepare.py pins previous files and edit contexts (one-shot snapshot),
prepare-masonry.py creates the revised blank targets/guides,then retain actual
built-in outputs from generations.json,normalize full canvases,assemble.py,
finish.py,verify.py,update-review.py. The rejected first Jukebox outputs remain
history; final assembly selects *-courses-normalized.png. The harmonic helper
is the existing seam-v2/assemble-bridges.py, included in the private archive.
Exact processors,prompts,input/reference/raw/output hashes and rejected attempts
are preserved. No new reusable core processor or automatic adoption was added.

Private separate overlay: `<data>/scratch/menu-ui-options-jukebox-seams-v2-20261018/`.
Apply it only to a COPY of the previous menu pilot tree; older review sections
need their previous files. Prior archives remain immutable. Authoring workflow,
prompt scaffold and both repository handoffs are updated in the same turn.

Next: owner review of the three changed extensions. Native source-shaped mapping,
independent Options scroll extrema,transitions/lighting and all5ratio game
validation remain later work. Battle remains next authorized background group.
Workshop8768/17candidates and Trejo review untouched. No new import,selection,
installation,game/build/runtime change,commit or push.

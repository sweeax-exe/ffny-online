# Lanczos in-game pilot — 2026-10-08

Owner now requests Lanczos tests and an in-game view before further model research.
This authorizes a bounded local test pack, not every asset or the HD release.

## Corpus, correlation and pack

Own data: C:/Users/Vlad/code/defjam-hd-data. Scratch: hd-work/lanczos-game.
Default ONE_ON_ONE_SETUP with the verified VY2 fixture selects Blaze (fighter56,
V2IP_113A) vs Doc (55), at Foundation (frontend1, V2BG_02A). It does not select
the wood/concrete samples from V2BG_07A/V2BG_01A; navigation there is uncalibrated.

The pack contains 26 runtime IDs: Blaze, 22 opaque Foundation entries, three
previously proven loading assets. The lossless capture observed320 unique IDs.
Exact source/decoded identity correlation is recorded in correlation.json; five
nonopaque venue candidates remain excluded. Unknown captures remain original.
Blaze RGB matches the reviewed original byte-for-byte; its alpha spans1..255.
Neither alpha range nor existing metadata proves its shader interpretation.

All replacements are Lanczos4x, with RGB/alpha resized separately, eight pixels
of edge padding, no world RGB bleed, UI RGB bleed, unchanged aspect/atlas layout.
This audition uses clamped padding; wrap axes still need material review. Source,
recipe, runner and output hashes are retained in jobs/build.json. Local pack:
mods/lanczos-test/manifest.ini. Recipe: hd-work/lanczos-game/recipes.json.

## Runtime changes needed for the pilot

- Optional pack png_mips=channels independently reduces RGB and alpha. Default
  opacity keeps the previous alpha-weighted RGB rule; unknown policies reject
  the pack. Metadata applies regardless of manifest section order, and stays
  attached to entries through pack precedence. DDS supplied mips are unchanged.
- Source-slot address mixing avoids concentrating page-aligned allocations in
  four cache slots. The refresh interval remains8ms; this is not write tracking.
- Permission validation moved from every eligible bind to actual identity
  refresh and capture. Cached replacement binds do not read source bytes.
  Source and palette readability remain checked before hashing/capture.
- Replacement load logging has its own bounded counter. The previous g_hits
  limit could hide world uploads after UI binds; absence of old load lines was
  not evidence that those assets were unused.
- RECOMP_TRANS_SHOT_NATIVE=1 captures actual rendered pixels. Default regression
  captures stay640x480; 2x/4x native captures are1280x960/2560x1920. Gamma conversion
  is retained. This diagnostic does not change the rendering path or goldens.

Native fixture passes known SHA/palette/stride/resource cases, opacity vs independent
mips with zero and nonzero alpha, invalid policy, per-pack policy/precedence,
aligned-address distribution, unreadable source/palette and eviction. Evidence:
logs/hd-native-channels-fixture.txt. Focused Python tests12pass; full project unit
suite185pass with MSVC, logs/hd-lanczos-unit.txt. Initial compiler-unconfigured
run154pass/31skip was superseded. No source commit/publication in this turn.

## Capture evidence and limits

Run summaries and complete executable/input identities are in scratch *-run.json.
Lossless dump run195117, initial pack-on195729, native pack-on200332, native
pack-off200920 all reached the fight without harness faults and restored the
entire disposable save root byte-identically. Native on200332 positively logs
20 replacement loads, including Blaze and venue entries; its screenshots show
the actual jersey mesh/denim and Foundation surfaces.

Native4x runs showed low frame pacing even with packs off. The first pack-on diagnostic was slower
in fights. The final normal2x test below removes that extra collapse, but broader
performance certification remains required. Do not attribute all slowdown to the pack,
the GPU, the installer or cache collision without measurements. GPU spot check
was5% utilization/11.1W; setup process was idle by cumulative CPU observation.
Captures and a brief thread profiler ran during diagnostic runs, so none is a
clean performance benchmark. No60fps or release acceptance is claimed here.

Local viewer: http://127.0.0.1:8766/lanczos-in-game/index.html. It contains original
and Lanczos2560x1920 renders. Same route/seed, but wall-clock input and frame pacing
produce different poses/cameras: these are appearance review, not synchronized
pixel-difference tests. The original40-image gallery and stored reviews are intact.

## Playable preview and remaining work

Scratch launcher: hd-work/lanczos-game/play-lanczos.py; --original disables the
pack. It uses normal2x rendering, real keyboard/controller input, no script/capture/
dump and a separate persistent playtest-data/save root seeded from six hash-checked
fixture files. It never replaces an existing playtest save root. Close the game
before switching versions. Select Battle/One on One, Blaze and Foundation.

Final normal2x on-play run201339 reached the fight without harness faults, restored
saves byte-identically, and positively loaded the world/fighter pack. It produced
1280x960 captures; presents stayed roughly43 per2s (~21fps). Moving permission
queries out of cached binds removed the observed extra fight-rate collapse in
this run, but render scale and host conditions also changed: do not claim a
controlled speedup. A clean frame-pacing diagnosis remains required, even though
the actual textures render correctly. A pure4x pack-off run also ran~21fps.

Remaining: owner visual review, mask/atlas/
wrap/mip behavior, cache/memory/performance and source-change tests, public native
port reconciliation, Release/Debug/full gates including the three earlier failures.
Current lane remains a v0.4.1-based Windows prototype, not the accepted public v0.5.0
port. Fonts, HD movies, other venues and full-corpus coverage remain follow-ups.


## Final verification and interactive launch

Required quick regression completed 4/5: all 185 unit tests and three reference
screens pass; the pack-off fight failed its 110-presents-per-2s floor with a
median of 43 (~21.5fps). Evidence: logs/regress-20261008-202746.txt and
logs/hd-lanczos-quick.txt. No thresholds or golden frames were changed. This is
an unresolved gate, not a certified release; the older full 11/14 run remains
superseded for these source changes. Full regression is required before a commit.

HTTP verification read all eight captures and checked their SHA-256 values and
2560x1920 dimensions. Isolated Edge verified four time selections and native/fit
controls, with no browser errors (scratch check-viewer.cjs). The viewer describes
pose/camera differences and does not claim synchronized comparisons.

Interactive Lanczos preview launched 2026-10-08 20:28, PID12956, after the
regression exited and no other game process remained. Log:
hd-work/lanczos-game/playtest-data/play-20261008-202813.log.err. Startup positively
confirms 26 pack entries, loading-asset replacements and real keyboard player1;
no controller was attached. Select Battle / One on One, Blaze, Foundation.
WASD navigates; K/Space confirms; Enter is Start; F11 switches full screen.
The preview is left running for the owner. Never rebuild or run a second game
until the owner closes it. No source commit, push, publication or release acceptance.

Timing hypothesis for the next investigation: input_host.c reader_thread calls
timeBeginPeriod(1), while RECOMP_SETTINGS=none scripted runs keep this host input
layer disabled. Check this difference with a bounded controlled test; it is not
proven to cause the low frame rate. Do not change game timing or bless a gate
without measuring. The launched real-input preview enables that reader thread.

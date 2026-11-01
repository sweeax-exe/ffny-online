# SC04 zero-time guard: scope, implementation and validation

Follow-up to [the RCA](sc04-av-sync-rca.md), [issue #161](https://github.com/vyanhursky/DefJamFFNY-recomp/issues/161)
and [PR #164](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/164).

## Which scenes are affected?

A metadata scan of all **38 cinema archives** in the user's own dump found
**17 matching shot names, all in SC04**. None of the other 37 scenes match
the retail fallback's case-sensitive, four-byte `sc04` prefix predicate.
The inspected selector/transition paths do not have an analogous substitution
of positive time for zero. The startup MAD movie clock is a separate path.
This bounds this particular mechanism; it does not certify every scene against
every possible synchronization defect. A custom shot with the same prefix
would also match.

## Is it a game or toolkit defect?

The SC04-specific 1/30-second substitution is in the **original game XBE**.
The lifted comparison matches the original instructions. The recomp/runtime's
separate simulation and presentation clocks can produce zero-step render
frames, exposing the game's assumption that rendering consumes positive
simulation time. This is a game-code compatibility issue exposed by host
scheduling, with no evidence here of a mistranslated instruction or faulty APU
sample rate. It is not a claim that the symptom was observed on an original
Xbox. The fix belongs in the game's override layer; the toolkit is unchanged.

## Source fix and impact

`sub_000766B0` now wraps the retained generated cinema renderer. Only an active,
unpaused SC04 shot with **exactly zero** pending delta (either sign) qualifies.
For that invocation, a scoped threshold adjustment lets the original zero-time
guard operate. The original body still runs its resource cleanup, selector
dispatch and rendering preparation. The threshold is restored before returning.

Unlike the RCA's experimental permanent threshold-zero intervention, the source
fix preserves **every nonzero delta**, including the retail positive-small-delta
fallback. Other scene names, paused/inactive players, invalid shot indices and
unexpected image constants are not corrected. No audio speed, device buffering,
simulation tick rate or presentation rate is changed.

Cleanup can invoke indirect callbacks before the zero guard. A recursive lock
serializes access to the one shared constant, and each invocation saves/restores
its own value. A nested positive-delta call sees the retail threshold while its
enclosing zero-delta call resumes with the guarded value. Native tests exercise
both that reentry and simultaneous calls from two host threads. Locking adds
small per-cinema-render synchronization overhead; runtime pacing is checked
below rather than assuming it has zero cost.

`RECOMP_SC04_ZERO_DT_FIX=0` restores the unmodified path for A/B diagnostics;
`RECOMP_SC04_TIMING_TRACE=1` prints bounded running suppression totals. Direct
and lookup callers resolve to the wrapper. The patch does not install a
permanent guest-data change or skip the whole renderer on zero-step frames.

## Validation record

The source-fix lane retains the PR base's toolkit pin `be83eb9`; it is not a
toolkit migration. The original RCA used RC toolkit `d569ca1`. Source validation
compares guard enabled/disabled using the same final newly built executable,
and keeps those results separate from the older RC intervention trials.

Native fixture: default enabled, explicitly enabled and disabled variants pass.
They cover a 0/1/0/2-step sequence; signed zero; ordinary and positive-small
deltas; selectors 0/1/2; non-SC04 and differently cased names; pause/inactive and
invalid-index exclusions; original housekeeping; guest register/stack behavior;
nested positive-delta callbacks; and concurrent zero/positive renders.

All three source-fix SC04 trials used the same final executable, SHA-256
`d95aeb69604594375d6b53a8b0d218c610a30e053445f7ed140ed7476c9051c1`.
They used the reported four HD packs, 16:9, render scale 2, a 2048 MiB texture
cache and a 144 Hz Windows display. The disabled frame-clock steering is an
amplifier, not the normal player setting. The patch itself is default-on.

| Guard | Frame-clock steering | Scene duration | Last-shot animation minus simulation | Animation minus copied audio |
|---|---|---:|---:|---:|
| Off | Off, diagnostic | 67.762 s | +2.548 s | +2.689 s |
| On | Off, diagnostic | 72.594 s | -0.202 s | -0.082 s |
| On | On, normal | 72.574 s | -0.235 s | -0.043 s |

All 17 shots were sampled, and all three runs completed the final blackout and
returned to the menu. The fix removed the accumulating positive video lead;
the residual offsets include shot-boundary accounting and audio buffering.
They do not establish absolute speaker-to-mouth sync. Raw samples, captures
and audio stay local; the [derived numbers](sc04-fix-validation-numbers.json)
record source hashes, session IDs and metric definitions.

All three reported zero host starvation events, refused submissions and
underruns. `audio-clicks.py` found zero clicks and zero exact-zero dropouts in
each device-input WAV. None reached a crash, watchdog, unresolved indirect
call or untranslated-instruction fault. Inherited `[UNIMPL-KNOWN]` CPU omissions
and existing optional-file/stub warnings are not new faults.

Four non-SC04 controls played to completion with **zero suppression messages**
and zero fault signatures: SC03 (56.2 s), SC05 (69.9 s), SC00 (87.2 s) and
SC21 (39.5 s), measured between scene events. Their median frame intervals
were 16.66-16.67 ms; p99 was 16.88-19.10 ms. No scene's audio ran dry or
contained detected clicks. SC00's silence detector reports 1580 ms beginning
at +87.19 s, at its 87.2-second end boundary; almost all of that interval is
after scene completion. It is retained in the dataset rather than treating
every detected silence interval as an in-scene dropout. These are scope and
pacing controls, not a new per-scene lip-sync survey or a matched performance
benchmark against the previous release.

The Windows release-preset build and generated/build freshness verification
pass. `check-source-tree.py` passes with all new files staged. The quick
suite's unit row passes: **656 passed, 27 skipped, 1 slow test deselected**,
including all three compiled cinema-guard cases. The original-body portion of
that fixture is modeled; the scene runs above exercise the real generated body.

`python scripts/regress.py --quick --preset win-x64-release`: **5/5 pass**
in nine minutes. The loading, title and main-menu signatures match; the normal
legal/EA/THX/intro-movie sequence reaches Press Start. The scripted fight runs
for 122 seconds, median 120 presents per two seconds (minimum 107), meeting
the suite's pacing gate. Save-root restoration is enforced by the harness.
Local report: `logs/regress-20261031-214102.txt`; full command output:
`logs/sc04-quick-final.log`.

Dataset consistency checks confirm three complete 17-shot trials on one final
executable, four complete control scenes and matching source fingerprints.
Whitespace validation passes. Full regression is deferred at the owner's
request; no full-regression command was run.

## Release limits

This is Windows implementation validation. macOS/Linux scene playback, full
release regression and the owner's Story-mode late-dialogue listening check
remain release-level work. Clock counters and a device-input WAV are not an
absolute speaker-to-mouth measurement. Preserve the RCA's residual-offset
qualification when reporting the fix.

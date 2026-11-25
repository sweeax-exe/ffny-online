# Illegal-tech detection (build 112, stage 1)

The community's tournament rules (the mods morrowbound, hoodiebatman and Froukle, 2026-11-07; the clips are in
`#illegal-tech`) apply to ranked and tournament games; casual stays free. The game detects them in the fight from
the fighter records, the same on both PCs of a match, and reports them with the match report. Stage 1 (this build)
is silent: no penalty; the account service keeps every detection and posts the facts to the team's `#cheat-flags`.
Stage 2 (enforcement: the first offence in a set voids the game, a second offence or leaving after one loses the
set) comes behind its own switch once the mods have checked the false-positive rate.

| # | Rule (id in the reports) | Detected when |
|---|---|---|
| 1 | infinite (`infinite`) | a second block cancel in one combo. A block cancel: the attacker blocks (state 2) while its combo on the victim is open and the victim still reels standing (0x0E, 0x10..0x12), then its next hit lands within 40 steps. Ice-T's cancels on a victim's back (bearing bits 0xC0) do not count (his back submission loop is allowed). |
| 2 | wall redaze (`wall_redaze`) | the victim came off a wall in this combo in the long dizzy (the stun bit), and a hit leaves it standing in the dizzy (not down, not back on the wall) |
| 3 | grab off the wall (`wall_grab`) | the victim came off a wall in this combo (not down, not on the wall again; within 150 steps, or longer while it is dizzy) and the attacker's grab catches it (0x20, 0x35, 0x22, 0x33, or a wall grab 0x68 / 0x62 entered from off the wall). A grab of a victim still pinned (0x37 > 0x68) is legal. |
| 4 | chain stun reuse (`chain_stun`) | the stun bit rises a second time in one combo with the same cause: the attacker's last grapple or heavy-hit move (`+0xF50` 0x27xx; the strike strings 0x36..0x6C are no chain stuns). Not while coming off a wall (rule 2's ground). Blaze (56, 81, 82) and Comp (46) are exempt. |
| 5 | two resticks (`restick`) | the victim is pinned against a wall (0x37, 0x14, 0x1B, 0x60) for the third time in one combo (the first stick and one restick are fine). Strikes on a pinned victim keep it pinned and are no restick; a grab on the wall (0x68, 0x62) is still on it. |

A combo (ours; the game keeps none) opens on a hit that is not blocked, or when the victim is caught by a grab,
and closes when the victim is back in a free stance (1), blocks (2, 3), strikes or grabs on its own, is knocked
out (0x18), or another fighter takes it over. Hits on a victim on the ground stay in the combo. Each rule is
listed at most once per combo.

## Code

| File | What |
|---|---|
| `src/hooks/illegal_tech_rules.c/.h` | the rules, pure C (no guest memory): `it_reset`, `it_hit`, `it_step`, `it_hash`, `it_json` |
| `src/hooks/illegal_tech.c/.h` | reads the fighter records for them, the rollback save/load, the checksum word, the report |
| `src/hooks/combo_log.c/.h` | the three hooks (`sub_001C6CD0_enter` hit, `sub_00087700_enter` step, `sub_001A4D90_enter` match set-up) call the detector first, then the combo log if it is on; `combo_log_on()` is now always 1 so `src/recomp_manual.c` (an input of the lift) did not change |
| `src/hooks/netplay.c` | the state in every rollback save (`N.snap[].it`), the checksum (`illegal_tech_hash`, every online fight), `illegal_tech_set_reported` at a START (ranked and tournament set games), `"it"` in the result and left reports |
| `tests/unit/test_illegal_tech_native.py` | each rule's legal and illegal sequence, the exceptions, a load in the middle of a combo, the JSON |
| server `relay/accounts/illegaltech.go` | the `"it"` list: kept, merged between the two games, `<store>.illegaltech.jsonl`, a flag of kind `illegal_tech` |

## Determinism

The detector runs in every fight (offline too), in every run of a step, also the steps rollback runs again. It
reads only the records of fighters in the match (`+0x28` names the slot) and, for those slots only, the set-up's
fighter byte (game data `+0x1AAA + 5 * slot`; an unused slot's byte is whatever that PC's last set-up left, 157 in
the tests), at the step's start and at each hit. Its whole state (`ItState`, 32-bit words only) goes
into every rollback save and comes back with every load, and its FNV hash is folded into the lockstep checksum
of every online fight, so a verdict one PC reached and the other did not would show as a desync at the next
checksum. Whether a match's detections are reported does not change what the detector does.

## Switches (tests)

- `RECOMP_ILLEGAL_TECH=test`: report the detections in every online match, casual too.
- `RECOMP_ILLEGAL_TECH=sensitive` (every PC of the match: it changes the detector's state): as `test`, and the
  first block cancel, restick and chain stun already count, so bot fights produce detections to compare.
- `RECOMP_ILLEGAL_TECH=off`: report nothing.
- `RECOMP_ILLEGAL_TECH_TRACE=1`: `[ITRACE]` lines, every hit and every change of a fighter's state, phase or stun
  flag, with the fields the rules read. `[ILLEGAL-TECH]` lines name each detection (first runs of a step).

## Live data (2026-11-25)

Three offline bot fights with `RECOMP_ILLEGAL_TECH_TRACE=1` and the combo log (`dj.py scenario offline --human --bot
--fast-ko 0`, the pad pressing A every 3 s so the Blazin' tutorial card does not stop the fight):
`logs/dj/20261125-175117-offline` (Club Murder cage, Doc grapple vs Magic aggressive, 10481 steps),
`20261125-175518-offline` (The Foundation, Blaze grapple vs Doc aggressive, 5994 steps),
`20261125-175808-offline` (The Pit, Ice-T aggressive vs Bless grapple, 10814 steps). No detection in any of them.

Fighter `+0x2C` is the database character index (Doc 9, Magic 10), not the fighter ID; the detector takes the
set-up's fighter byte (game data `+0x1AAA + 5 * slot`: 55, 22).

| What | States seen (fighter `+0x30`) |
|---|---|
| grab | grabber 0x1E > 0x1F (caught) > 0x21 (throw) or 0x34 (hold); victim 0x20 (caught) > 0x22 (thrown) > 0x15, or 0x35 (held) > 0x36 (released) > 0x01 / 0x37 |
| throw into the wall | victim 0x35 > 0x36 > 0x37 (the wall throw) |
| grab on the wall | grabber 0x1E > 0x67, pinned victim 0x37 > 0x68 > 0x15; at a 0x60 wall 0x1E > 0x61, 0x60 > 0x62 |
| pick-up | grabber 0x1E > 0x32, downed victim 0x15 > 0x33 (then 0x33 > 0x16 > 0x01 at once, stun bit clear) |
| reversal | striker 0x45 > 0x30, grabber 0x1E > 0x31 > 0x17 / 0x5F |
| wall | 0x11 / 0x12 > 0x37 (cage, The Pit), 0x11 / 0x12 > 0x60 (The Foundation); 0x37 runs phases 1..4 and ends 0x37 > 0x01 about 200 steps later when nobody touches it, or > 0x15 |
| strikes on a pinned victim | 0x37 > 0x37, `+0x384` 1, 2; the third filled Bless's stun meter (117, 137, 139 against `+0x378`) and popped her off the wall into a big stagger with the stun bit set: 0x37 > 0x10 (bit 2 of `+0x927`), 0x10 > 0x15, the bit cleared on the ground (0x15 phase 11) |
| the long dizzy | the stun bit; state 0x16 is its standing form (its handler sub_001B50D0 clears the bit and the meter) |
| bearing `+0x918` | 0x13 / 0x15 (side), 0x83 (from behind, a victim on the ground) |

Not seen in bot fights (the bots do not play these on purpose): a block cancel, a second wall stick in one combo,
a grab of a victim coming off the wall, a stun by a grapple, state 0x16 with the stun bit set.

Scripted session (`logs/dj/20261125-191609-offline`: Doc against an idle Magic in the cage, pad bits injected
per step through the control port, `RECOMP_CONTROL_PORT`, `inject 0 <rel> <steps> <bits>`):

- Doc's string: punch (0x80) hits land 7 steps after the press, moves 0x36, 0x39, 0x3B; the third hit sends the
  victim into the big stagger (0x10). One hit's flinch (0x0E) lasts 24 steps.
- Block cancel: block (0x2000) pressed 7 steps after the string's second hit takes the attacker 0x45 > 0x02 (the
  attack's recovery is cut), 0x02 > 0x01 seven steps later, then the next punch (0x45). In one try the next hit
  landed in the step the victim recovered (its `vst` at the hit was 0x01, while every step start still saw 0x0E):
  the detector counted it as a block cancel (`bc=1`), as the combo log's `bcancel` did. A second cancel in one
  combo needs tighter timing than the scripted 60-steps-a-second presses reached; it stays covered by the unit
  test, not by a live run.
- The combo log's `bcancel` also fired for a block while the victim lay on the ground (then a hit on it): the
  detector does not count that one (the victim must still reel standing when the attacker blocks).
- A knocked-down human slot without input stays down (0x15) until a face button is pressed.

## Stage 2 (enforcement, not built yet)

Behind a switch file on the relay and the account service (`illegal-tech-enforce`, default off), ranked and
tournament sets only, once the mods have checked stage 1's false-positive rate:

1. Relay: the switch's state goes to both games with the set game's START (one value for both PCs; never
   from a later message), and a RESULT "void" (no winner) neither counts nor advances the set's in-room score:
   the same game is played again.
2. Game: with enforcement on for the match, the first detection ends the fight at the step it is found (both
   PCs find it at the same step), with no winner, and the result report says why (`"it_void"`).
3. Account service (`illegaltech.go`): a detection both games listed (or the offender's own game listed)
   counts as an offence of the attacker's account in that set: the first voids the game (no stats, no set
   game), a second offence in the set, or a leave after an offence, forfeits the set (`forfeitSet`). A
   detection only the victim's game listed pauses the set for the team (a patched game could hide its own).

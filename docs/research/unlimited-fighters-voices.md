# Own voices of community fighters beyond the places (phase 3B)

Date 2026-11-18. Branch `feat/unlimited-voices` (from build 91, `0ce00aec`). Follows
`unlimited-fighters-phase1.md` (banks 124..127, numbered fighters 1000 and up) and `new-fighter-voice.md` (a place
fighter's own voice). Tags: **[V]** measured in a run or verified in code/data, **[I]** inferred, **[U]** open.

Until now a numbered fighter spoke with its template's voice: its bank row's coreID is the template's, so the
speech row got the template's voice number, and the pack's own lines (Studio already puts them in every pack) were
not composed at all.

## 1. Measurements

* **No number per fighter.** A voice number names eight entries of `audio/speech2.viv` (`new-fighter-voice.md`).
  The speech set-up (`sub_0003DB00`) skips a voice whose flag `0x33EA78[voice < 500 ? voice : voice - 400]` is set;
  the array is 200 bytes, so at most 200 numbers exist (000..099, 500..599), and voice 1000 would index byte 600
  (other data) [V, lifted code]. Every caller of `sub_0003DB00` clears the 200 flags first and unloads the speech
  banks (`sub_0001CA80(-1)`): nothing is cached by number from one set-up to the next [V].
* **The directory is in memory as the file has it.** During a fight the game's memory holds exactly one copy of
  `speech2.viv`'s BIG directory, byte for byte the file's (31,973 of 31,973 bytes, stock dump; at `0x81455200`, in
  the contiguous window's game heap; 39,769 bytes composed) [V, `scripts/mem-find.py`-style read of a running
  copy]. The bank queue (`sub_0001C960(name, "AUDIO/SPEECH2.VIV|")`) and the stream loaders name entries; the
  entries' offsets and sizes come from that copy [I: the redirect below works, section 3].

## 2. Design

* **One voice per bank.** Bank `b` (feID 124 + b, player slot b) speaks as voice **064 + b** (the own voice of
  place 124 + b; no place fighter can hold a bank place while numbered fighters exist, `unlimited.add_banks`).
* **Composer** (`setup/ffnycontent/unlimited.py`, COMPOSER 11): a numbered fighter's own voice
  (`newfighter.apply_op(logical=True)` -> `unlimited.logical_voice_files`) is built exactly as a place fighter's
  (`fightervoice.voice_files` at the first bank's number: the template's entry tables, the modder's clips, the
  template's clips from the player's dump for empty groups) and added under its number:
  `speech\<Character|Taunts|Enviro>\Def_Jam_Characters\L<n>.dat`, `speech\streams\<intro|extro>\L<n>.asf`, and the
  `.hdr` four times, `L<n>_064.hdr` .. `L<n>_067.hdr` (a header carries its voice number at +2). When any numbered
  fighter has one, `finish_logical` adds the four bank voices' eight entries each (silence in voice 025's layout;
  never played as they are). Word 21 stays the template's. Same packs + same dump = same bytes (content print).
* **Game** (`src/hooks/new_fighters.c`, `fighter_voice.c`): at the speech set-up (`sub_0003DB00_enter`, the same
  hook as place voices), for a slot whose fighter byte is a bank bound to a numbered fighter (whose coreID the
  speech row holds), `fighter_voice_numbered_plan` finds in the host file's directory the bank voice's eight
  records and the fighter's eight files; the guest's copy of the directory (found once by a search of the
  contiguous window's live blocks, checked again at every use against the file's bytes, the bank records aside)
  gets the eight records' offset and size pointed at the fighter's files, and the slot gets voice 064 + b.
  Otherwise (no own voice, an older compose, no directory found) the slot keeps the template's voice. Place
  fighters are unchanged.
* **Lockstep / rollback.** Only audio state changes: the directory and the speech row, written at match load before
  the first fight step, from the lineup every PC shares (START); both PCs write the same bytes. A rebind (rematch,
  KOTH watchers) is applied at the next speech set-up, which every match runs.
* **Studio**: no change; packs already carry `voice/*.sch|asf` and the `voice` block. **Server**: no change.

## 3. Runs

(see the PR description for the run table)

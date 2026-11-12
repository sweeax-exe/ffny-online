# New fighters with their own voice

Date 2026-11-12. Branch `feat/fighter-voice` (from `feat/fighter-gameplay`). Follows the static study
`community/new-fighter-gameplay-study.md` (section 4, voice) and `new-fighter-gameplay.md` (the pack's gameplay
block, which reserved `voice`). Tags: **[V]** measured in a run or verified in code/data, **[I]** inferred,
**[U]** open.

## 1. Measurements

### What a fighter says, and from where [V]

A voice number N (stock: the XBE table `0x2DC5D0[coreID]`, 12-byte rows; a created fighter: `0x28E48C[voice]`)
names five things in `audio/speech2.viv`:

| File | What | Entries (stock) | Longest stock line |
|---|---|---|---|
| `speech\Character\Def_Jam_Characters\NNN.hdr/.dat` | in-fight lines; entry field 0 = the event | 1..103 | see below |
| `speech\Taunts\...\NNN.hdr/.dat` | taunt lines (fields `4 0 0` / `4 0 1`) | 3..7 | 4.12 s |
| `speech\Enviro\...\NNN.hdr/.dat` | event `Char_Enviro_CRD` (crowd/environment) | 4..20 | 5.65 s |
| `speech\streams\intro\NNN.asf` | entrance line, loaded by `sub_00038440` into a 0x25800-byte buffer per slot | 1 | 7.78 s, 152,312 bytes |
| `speech\streams\extro\NNN.asf` | victory line, loaded at 0x3C52B into a **0x7D000-byte** buffer | 1 | 8.04 s, 157,496 bytes |

Character field 0 takes four values; the bank's `Events.evt` lists four events in this order: `Char_Blazing`
(value 0: exactly one entry per voice, 6.13 s long in almost every voice: the Blazin shout), `Char_Weapons` (1),
`Char_Environment` (2), `Char_Post_Special` (3) [V names and counts; I the value-to-event order]. Longest stock
lines: 6.38, 4.63, 5.51, 6.97 s.

**There are no per-fighter hit/effort grunts or lose lines in the speech banks.** Grunts are nine shared body-type
banks in `sfx0.viv` (`Grunts_AsianGuy`, `_BBGuy`, `_BGirl`, `_Girl1..3`, `_LatinoGuy`, `_SBGuy`, `_WGuy`),
picked by the fighter's class, not its voice [V names; I selection]. So the modder's groups are what the game has
per voice: Intro, Win, Taunts, Blazin shout, weapon lines, environment-attack lines, after-a-special lines, crowd
lines.

Stock voices: 000..039 and 500..538 (no 501, 511, 523); **040..099 are free in the dump** [V listing, unit test].
The bank-loaded flags `0x33EA78[voice < 500 ? voice : voice - 400]` (200 bytes) take 040..067.

Row word 21 (speechID) equals the table's voice for every stock fighter except Masa (row 523, plays 506) and the
voiceless Suspect, Jervis, Starks (999) [V, XBE read]. The installer uses the table's value for the template.

### Format [V]

* `.hdr` (`sub_001393F0`): `u16 family, u16 voice, u8 fields, u8 count, u8 count, u8 unit`, `u16 total` at +8,
  then `count` entries of `fields + 2` bytes: a big-endian `u16` offset in `(unit + 1) * 0x100` bytes and the
  field bytes; an entry's clip ends where the next starts (the last at `total`). Then per-field `u32`s and one
  `0xFF` byte per entry (runtime state). Families: Character 0, Taunts 16625, Enviro 40962 (+1 for the IP group).
* Clips (`.dat` entries and `.asf`): EA **SCHl**, PT header (platform 7 = Xbox, tag `0x80` = 3, `0xA0` = 0x0A
  EA-XA, `0x8C` = 0x24, mono), `SCCl`, `SCDl` blocks (stock: 1624 samples in banks, 2408 in streams), `SCEl`.
  Frames: EA-XA **R2** (a 15-byte ADPCM frame, or `0xEE` + h1 + h2 + 28 big-endian PCM samples). The decoder's
  history runs on across blocks (carrying it makes the stock block joins continuous; resetting does not).
* **Rates:** streams carry `0x84` = 36000. Bank clips have no rate tag; the PT parser's default is **24000 Hz**
  (`mov word [edi], 0x5DC0` at 0x126589; `0x84` overwrites it at 0x1267F5).
* Loudness of the stock lines: the louder part at about -12 dBFS RMS (banks) / -13 to -14 (streams), peaks about
  -0.5 dBFS.

### Codec [V]

`fightervoice.py` (stdlib only, the same text in Studio and the installer) decodes and encodes R2. The encoder
tries the four filters at the shift the open-loop residual asks for and its neighbours, closed loop, and starts
each clip with a PCM frame as the stock clips do.

| Test | Result |
|---|---|
| decode stock with our decoder = the reference scratch decoder | identical on 4 clips |
| stock -> encode -> decode (intro 025, taunts 025, Character 010, extro 536e1) | SNR 73..97 dB, sizes within 0.4 % of stock |
| fresh audio (stock intro resampled to 24 kHz + noise) | SNR 29.4 dB, 11x real time in pure Python |
| our own output re-encoded | bit-identical |

## 2. Design

* **Voice number** N = 40 + (feID - 100) (040..067). No claim: the account service's place decides it.
* **Pack:** the new-fighter op's gameplay block takes `"voice": {"mode": "own", "lines": {group: [paths]}}`
  (groups `intro win taunt blazin weapon environment special crowd`, at most 1/1/7/1/8/8/8/8 clips, 7.8/8.0/4.0/
  6.0/4.5/5.5/6.0/5.5 s); the clips are in the pack as `voice/<name>.asf` (36 kHz streams) and `voice/<name>.sch`
  (24 kHz bank lines), the modder's own recordings only. A block may hold only `version` and `voice` (an own voice
  on the template's gameplay).
* **Installer** (`newfighter.apply_op` -> `fightervoice.voice_files`): every bank keeps the template's entry table
  (lines, events, order, trailer); a group's own clips fill its entries in turn; a group left empty keeps the
  template's own clips (read from the player's dump, so no game audio is in a pack); a template without that bank
  takes voice 025's layout with silence. Intro/extro: the own clip or the template's stream. The eight files are
  added to `audio/speech2.viv` **unwrapped** (the sound code reads its archives' entries in place; every other
  archive keeps literal RefPack), and word 21 of the row = N. Same pack + same dump = same bytes on every PC
  (unit test); the pack is a content pack, so it is in the content print.
* **Game hook** (`sub_0003DB00_enter`, a re-lift; `new_fighters_voice`): after `sub_0003E170` filled the per-slot
  speech rows (`0x3403A4 + 0x64*slot`: +0 coreID, +0x10 voice) and before the banks are queued, a slot whose fighter
  byte is a new fighter whose word 21 is its own N, whose row holds the fighter's coreID, gets voice N **when all
  eight entries are in the speech archive the game opens** (`fighter_voice_entries_present` on the archive's
  directory, once per voice); otherwise it keeps the template's. The intro and extro loaders read the same field.
  Nothing the simulation reads changes. `sub_001393F0_enter` logs bank line lookups (`RECOMP_SPEECH_LOG=1`; always
  for voices 40..99).
* **Speech tags of the new model** (a phase-1 gap this work found): the intro and victory speech start from
  sound tags of the fighter's camera banks in `audio/animtags.dat` ("TAGS", count, offsets; per entry an 8-byte key,
  the bank name reversed and zero-filled, sorted as a little-endian u64; the CI tags fire the speech code's event
  0xF000000A FighterIntroStart, the CE tags 0xF000000C), registered at 0x3CBD1. A new model's `CE_7xxA`/`CI_7xxA`
  banks had none, so a new fighter walked in and won silent, with any voice. `newfighter.add_anim_tags` copies the
  template's two entries under the new keys (the op adds `audio/animtags.dat` as a loose file).
* **Viv names:** `speech2.viv` names use backslashes; `viv.Archive` and `compose_archive` now fold `\` and `/`
  (before, a lookup of a backslash name never matched).
* **Launcher:** `.sch`/`.asf` are allowed pack files (`setup/mods.py`). An older launcher refuses such a pack; the
  account service gives the pack the capability `voice-v1`.

## 3. Runs

Test voice: lines spoken by the PC's own text-to-speech (no recording of anyone), made into a pack by Studio's
modules (`voiceaudio.make_clip`, `contentinstall.write_new_fighter_pack`) and composed by the launcher's
`content_compose`; the fight uses the composed files (`dj.py --override`). "Heard" = the game's mixed output
(`RECOMP_APU_PCM`, 48 kHz) cross-correlated in 100 ms chunks with the clip: a played line gives chunk peaks of about
0.9 that agree on one start time; a line not played gives no agreement.

| Run (logs/dj/...) | What | Result |
|---|---|---|
| `20261112-182041-offline` | "Voice Test" (feID 100, on Doc, voice-only block) vs CPU Xzibit | `[VOICE] voice 040: files all present in audio\speech2.viv (pack copy)`, `slot 0 feID 100: own voice 040 (the template's was 025)`; the game reads `intro .asf` and, after the KO, `extro .asf`; the CPU's taunts come from `Taunts voice 002` (fields 4 0 1 / 4 0 0) |
| `20261112-184643-offline` | our encoded intro put in place of Xzibit's `002.asf` | **heard**: 14 of 15 chunks at one start, correlation 0.96 (the codec plays in the game) |
| `20261112-183659-offline`, `-184826-offline` | voiced fighter in slot 0 / slot 1 | `intro .asf` read whole (88,308 bytes), **not heard**; Xzibit's stock intro heard (0.95); the own **crowd line** from the 040 Enviro bank (`[SPEECH] line: Enviro voice 040 entry 7 of 9`) **heard** (11/11 chunks, 0.95) |
| | cause | the new model's camera banks had no speech tags in `audio/animtags.dat` (section 2); fixed by `add_anim_tags` |
| `20261112-191729-offline` | stock Doc vs voiced fighter, with the tags | Doc's intro heard at 38.5 s (31/32, 0.95), **the own intro heard** at 42.1 s (20/28, 0.92); HUD `VOICE TEST` |
| `20261112-192533-offline` | voiced fighter wins by KO | **own intro heard** (21/28, 0.94), **own win line heard** at 63.0 s (18/35, 0.85); Xzibit's extro not played (0 agree) |
| `20261112-192125-online` | two copies, lockstep, 3 matches, voiced fighter (slot 0) vs Xzibit, same composed files | both copies `own voice 040`; **50 checksums compared, no desync**, three KOs |
| `20261112-192353-online` | two copies, **rollback**, 2 matches, Xzibit vs voiced fighter (slot 1) | both copies `own voice 040`; **30 checksums, no desync**, two KOs |

## 4. Open

* The taunt input of the scripted pad was not found (right-stick flicks and clicks during the attack mash did not
  taunt; the CPU taunted on its own); the own taunt bank was checked by composition, the same bank path as the
  crowd line that was heard, not by a taunt in a run.
* The value-to-event order of Character field 0 is inferred from `Events.evt`; a run where each event fires
  would confirm the labels (weapon / environment / after a special).
* Grunts stay the body type's shared set; an own grunt set would need a tenth bank and a per-slot choice.
* An older launcher refuses a pack with `.sch`/`.asf` files: release the launcher first, or gate on `voice-v1`.

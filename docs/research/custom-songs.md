# Custom and cut songs: how the game's music works (research, 2026-11-03)

Goal: let a player put their own music files in `<data>/songs/` (files or folders) and have the game treat
them like soundtrack songs: in the jukebox, in fights, on loading screens. Also: bring back songs that were
cut. No song bytes are hosted by the project; every player supplies their own files.

Evidence came from the user's own dump (`audio/music0-4.viv`, `assets/djv2.sod`, `screens/jukebox/beatbox`),
the lifted code of the v1.0.0 candidate, and two scripted fights with audio captured at the device
(`RECOMP_APU_PCM`). Function claims marked *inferred* were read from code, not seen live.

## 1. What a song is on the disc

Each song is a folder of four files inside one of `AUDIO/MUSIC0..4.VIV` (EA BIG archives):

| File | What it is |
|---|---|
| `tune.mpf` | EA PathFinder v4 map, little-endian (`xDFP`, version 4). Four tracks, five sections, 27 events, a node graph |
| `main.mus` | Track 0: the song cut into 4.485 s segments, each its own EA `SCHl` stream (stereo EA-XA ADPCM, split channels, ~150 kbit/s per channel) |
| `loading0.mus` | Track 1: a small RAM bank (`BNKl`), 2-6 sounds, played on loading screens |
| `overlay0.mus` | Track 2: a small RAM bank, 2-8 sounds, played when a fight starts |

Track 3 is a single 1 s item. Segment and bank layout parse with the map format used by vgmstream
(`ea_schl_map_mpf_mus.c`, version 4 branch); the decoder in the scratch tools reproduces clean audio.

- All songs are 36 kHz except the cut song `holler`, which is 44.1 kHz. The game plays it at the right speed.
- `main.mus` holds about 2-3x the song's length (Walk With Me: 216 segments, 750 s). Licensed songs have
  300-600 nodes, most of them 4-way branches, so the music can move between authored variants at any
  segment boundary. The short "Exclusive" score tracks (Dragon House, Koto, Blindside, Afterhours) and,
  unusually, Man Up are plain linear chains that loop.
- The two music effects in `music4.viv`: `cribfilt1c.fx` is applied to front-end music (the muffled
  "through the wall" sound); `djlopass1.fx` is loaded at start-up but nothing uses it (*inferred*).

## 2. The song list

`assets/djv2.sod` has a `Song` table, 32 rows: row name, album, artist, entity (the folder id), a small
integer whose meaning is unknown (values 0-31, with repeats), and title. The jukebox shows these rows.

The disc has **33** song folders. The extra one is **`holler`**: complete audio, a complete map, no
`Song` row, so the game never offers it. It is the only cut song with audio on the disc.

Other traces of cut or renamed music:
- `rockandroll` appears only in a developer start-up playlist (`dirty, nuffrespect, rockandroll, manup`,
  `sub_0002CF80`, next to the log line "JUMP STARTING THE DAMN GAME!!!"). No audio for it exists.
- `nwa` is the folder of Baxter's "Blindside" (row name "Chopshop"): renamed at least twice.
- A hard-coded 12-song list in the XBE (`arewecuttin getintoit getitnow ricache mamasaid nuffrespect pump
  poppalarge seizetheday hitem nwa minitrack2`), stored twice, is read by `sub_0001E9C0` and
  `sub_0001FCF0`. Its purpose is not resolved yet; it may be the songs a new profile starts with.

## 3. How music is chosen

- **Jukebox** (main menu → Beatbox, `screens/jukebox/beatbox`). It is a shop screen
  (`Shops.ChooseShopList(6)`) with Play, Stop, Shuffle, Promote, Demote, Enable and Disable. Each song has
  one byte in the profile: two bits of state (0 never arranged, 1 or 3 on, 2 off; see 5a) and six
  bits of order position (64 positions). The screen can show a rewards lock (`~RWRD_LOCK`); no
  code that locks or unlocks songs was found, and every song is on in the profiles tested. The profile also holds
  "ordered/shuffled" (+0x14DC) and "jukebox on" (+0x14DD).
- **Playlist player** (`sub_0003CFF0` builds it, `sub_0003D060` runs it every frame). It plays its list
  strictly in order and wraps. Shuffle is a one-time reorder when the list is built, not a random pick per
  song. The list lives in a fixed array of **36 entries x 16 bytes** at 0x33E798; there is no bound check
  (a 37th entry overwrites the count), and ids longer than 15 characters overflow.
- **Front end**: the jukebox list, filtered by `cribfilt1c.fx`, when the jukebox is on.
- **Fights** (`sub_0001F2D0`, `sub_0001F7E0`): the playlist is [the match's primary song, an optional
  second song, then the jukebox list]. The primary song comes from the match setup and defaults to
  `dirty` (Redman, "Let's Get Dirty"); a fight with nothing chosen opens with that song. The current song's
  `overlay0.mus` plays when the fight starts (`sub_00034A60`, "Fight Started").
- **Loading screens**: the song's `loading0.mus` (*inferred* from the loader modes).
- **Credits**: "Walk With Me" (Joe Budden), loaded without the playlist when the credits start
  (*inferred*).
- **No tiers or categories** in the audio code: every song can play anywhere. The only gates are the
  per-profile lock/enable state and the match's primary song.

## 4. Live checks (Windows, v1.0.0 candidate `bf636f8`)

A throw-away data root whose `music1.viv` points the `dirty` entries at `holler`'s data (entry offsets
swapped, names and sort order untouched), with the worktree's `game` junction pointed at it, then
`harness.py run fight` with `RECOMP_APU_PCM`:

| Run | Captured fight audio vs. `holler` | vs. `dirty` |
|---|---|---|
| original files (`run-20261103-142940`) | r <= 0.22 | r up to **0.83** |
| `holler` behind `dirty` (`run-20261103-144424`) | r up to **0.66** | r <= 0.19 |

(Normalised cross-correlation of 4 s windows at 8 kHz against the decoded songs; crowd and effects are
mixed in, so a match is about 0.4-0.8.) So the cut song plays in a fight from a rebuilt archive, at
44.1 kHz, with no crash, watchdog or log error, and the save was restored byte-identical.

Gotcha: the guest `D:` drive is the repo's `game` junction, not `<data>/extracted`, so `DEFJAM_DATA` alone
does not change game files. Real song mods should use the toolkit's file redirect, as language packs do.

## 5. What custom songs need

1. **Convert** each player file to the game's format: decode (MP3, FLAC, Ogg Vorbis, WAV), resample to
   36 or 44.1 kHz, cut into 4.485 s segments, encode EA-XA (a prototype encoder gives ~29 dB SNR on fresh
   audio, normal for 4-bit ADPCM), and write `main.mus`, a generated linear `tune.mpf`, and small
   `loading0`/`overlay0` banks cut from the song. Cache the result like language packs
   (`<data>/cache/songs/`), keyed by the source file's hash.
2. **Serve** the converted files: a cached sixth archive (or rebuilt copies) through
   `g_xbox_game_file_redirect`. That hook has one owner today (language packs), so it needs chaining.
3. **Register** each song: a `Song` row in `djv2.sod` (title/artist/album from tags, else the file
   name), a jukebox item key, and a profile state byte. The fighter-mod work also rewrites `djv2.sod`, so
   both need one composition step.
4. **Lift the limits**: the 36-entry playlist array (33 songs on the disc leaves 3 slots), 64 jukebox
   order positions, 128 entries in the item index (0x3A07B0), 15-character ids. Past ~3 custom songs this
   needs hand-written replacements for the playlist build/run functions with a larger host-side array.

## 5a. Slice 1: "Holler If Ya Hear Me" restored (what it took)

**Song list.** `src/hooks/pc_songs.c` appends a Song row (title, artist, album, entity
`holler`; the unnamed integer copied from the template row) to a copy of `djv2.sod` in
`<data>/cache/songs` and serves it through the game-file redirect, chained to the language
pack's (and built on the pack's copy of the database when it serves one).

**Record handles are ranks.** A record handle is `(table rank << 24) | rank in the table's
name index`, and that index is sorted by the hash of the row name. Profiles save handles
(below), so a row inserted mid-index would shift every later retail song. The writer gives an
added row a row name whose hash sorts after all others (`Holler If Ya Hear Me~N` when the
plain title does not): retail songs keep ranks 0-31, Holler is 32. The row name is internal;
the jukebox and the banner show the Title field. `tests/unit/test_songs.py` checks this on a
synthetic database and on the user's own.

**The profile's song store.** The jukebox and the playlist read a per-profile item store
(`ItemMgr`, vtable 0x286204) at profile manager B + 0x30: sorted keys (handles), a count, and
one state byte per key (bits 0-1: 0 never arranged, 1 or 3 on, 2 off; bits 2-7: jukebox
order). Its buffers are fixed in B (states B + 0x1420, keys B + 0x1444, 36 of each; an order
counter at B + 0x14D4). `sub_00013340` builds one key per Song row; loading a profile
(`sub_00014540`) copies the saved block over keys, states and counter but leaves the count, so
a song added after the save has no key: the playlist skips it, and the jukebox resolves it to
`states[-1]` (outside the array) and showed "Make It Hurt" in its place.

The fix wraps `sub_00014540` (`src/patches/song_store.c`): after the load, every built handle
the save lacks goes into a slot past the saved ones, enabled, at the position the order
counter gives (then advanced). Taking the order from the counter matters: right after a load
the game numbers its never-arranged songs from that counter, and an order taken from the
highest one in use collided with theirs (two songs at one position: one dropped, one doubled).
The saved block has room for 36 and the retail game reads only its own count, so the extra
entry a save carries is invisible to it.

**Checked live** (Windows, disposable data root, `RECOMP_SONGS_TRACE`): Beatbox lists, plays
and shows "Holler If Ya Hear Me / 2Pac / Strictly 4 My N.I.G.G.A.Z." in Now Playing; the
playlist after the profile load and after leaving the Beatbox has 33 songs, Holler once, no
doubles; with `RECOMP_TEST_SONGS_ONLY=32` the main menu's song banner announces it at start-up
with all three lines, and a One on One's playlist is Holler (captured audio r 0.96-1.00
against the decoded song).

**Found on the way.** `release/v1.0.0` never started language packs: the cheat-menu merge
(`6bccd71`) dropped `pc_language_start` from `pc_settings.c` (fixed in its own commit, with a
source check). After leaving the Beatbox the main menu keeps the title "BEATBOX"; it does so
with songs off too, so it is not from this work.

**Before the profile loads** the game plays a 16-song list it builds without the profile's
choices; Holler is not in it (a profile's jukebox decides from then on).

## 6. Owner decisions (2026-11-03, D-new for the PR)

- **Lift the song limit now**: replace the playlist build/run functions with hand-written ones over a
  larger host-side list, and widen jukebox ordering, so 100+ custom songs work.
- **Per-song mode, chosen in a launcher Songs page**: each custom song plays *straight* (linear loop, the
  default) or in an *experimental interactive* mode with generated variants/branches.
- **Custom songs' jukebox state lives in a PC-side file**, not in the profile, so saves stay clean and
  importable.
- **`holler` is restored as a bonus song, on by default**: "Holler If Ya Hear Me" by 2Pac (album
  *Strictly 4 My N.I.G.G.A.Z.*, 1993; identified by the owner from the audio). It also shows in the
  launcher's Songs page like a custom song, using its own authored interactive map.

Unknowns: the 12-song list, the `Song` integer field, how songs unlock, the bank sounds' exact roles, the
branch rules in licensed maps (which variable picks the 4-way route), and save-file offsets of the
jukebox bytes.

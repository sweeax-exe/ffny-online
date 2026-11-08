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

## 5b. Slice 2: how many songs the game can hold (2026-11-05)

The song count is bounded in more places than the playlist:

| Where | Bound | What overflows |
|---|---|---|
| Song store buffers in the profile manager (B + 0x1420 / + 0x1444) | 36 | the order counter and the manager fields after them |
| Playlist slots (`0x33E798`, `sub_0003CFF0`) | 36 names | the count at `0x33E9DC` |
| `sub_0003DA40`'s list (the front end's playlist) | 35 names + end marker | its return address |
| Shuffled playlist builder `sub_00020D70` | a 36-entry local array (esp + 0x54 .. 0xE4) | its own locals (seen: a corrupt `free` under `Audio.PlayMusic` with 49 songs on) |
| Jukebox list builder `sub_0004FD80` | local arrays of 36 (unarranged, esp + 0xF8), 36 (by order, + 0x188), 32 (never arranged, + 0x218) | its frame |
| The jukebox object (0xE4 bytes, `ChooseShopList(6)`) | 37 enabled-song indices at + 0x4C, then the flag at + 0xE0 | the flag and the heap block after it |
| Jukebox order | 6 bits | positions past 63 |

So the stock game holds **36 Song rows**. The section below takes that to 60.

What slice 2 does within that bound (`src/patches/song_store.c`):

- The store gets buffers of its own; the fixed buffers in B become the save's copy only, written
  in the **retail shape** (the 32 retail songs, orders renumbered 0.., the other slots empty)
  after the build, after a load, before every `MemCard.*` command and whenever the playlist is
  rebuilt. A save stays one the original game reads as its own.
- The added songs' jukebox places live in **`<data>/song-state/<profile>.txt`** (the profile name
  is at B + 0x2B8): one line per song, by folder (`folder#k` for a later row playing the same
  folder), plus a checksum of the save's song block. A load uses the file for every song when
  the save is the one it was written with, else the save for the retail songs and the file for
  the added ones.
- The playlist builders hand back at most 32 songs (the fights put one or two of their own in
  front) and `SetMusicPlaylist` takes at most 36, as windows that move on at every rebuild when
  more songs are on. (With 36 rows the shuffled builder's 36-entry array is just large enough;
  more rows would need it replaced too.)
- `RECOMP_TEST_SONG_CLONES=N` adds N rows that play the disc's own folders ("Test Song N"),
  to fill the store in tests.

Checked: 36 rows (retail, Holler, three clones) through boot, the Beatbox (every row), back to
the menu, and a reload with the state file; no crash, playlists 32/36, saves restored
identically. With 64 rows (before the cap) a boot crashed in `free` under `Audio.PlayMusic`:
the shuffled builder's array, which is how the table above was found.

### 60 songs: the Beatbox list rebuilt (2026-11-05)

The bounds above are of two kinds: arrays of **every** song (the store, the Beatbox list
builder's stack arrays) and arrays of the songs **on** (the playlist, the jukebox object's map
of it at J + 0x4C, PlaySong's list at esp + 0x60 in `sub_0004EEC0`, the shuffled builder's
array). The first kind is taken over here; the second stays the game's, so the number of songs
on is limited instead:

- `sub_0004FD80` (the Beatbox list) is replaced by `song_jukebox_build` (song_store.c), the same
  algorithm with arrays sized for 64: arranged songs by order, then unplaced ones (given their
  list position and turned on), then never-arranged ones; the counts at J + 4 / + 0x3C, the map
  at J + 0x4C (37 slots), and `PlaySong(index, 1)` for the song now playing.
- `sub_00020D70` (shuffled playlist) runs the ordered builder and shuffles on the host.
- At most **35 songs on** (ticking stops there: `sub_00050E00` wrapper) and **36 while a song
  that was off plays** (PlaySong turns it on and lists it with the others: `sub_0004EEC0`
  wrapper refuses a further one).
- After a load: songs the profile knows keep their order and come first (the never-arranged
  ones are arranged as the game would, on, after the others), new songs after them; past 35
  on, the later ones are off. A new song never turns a disc song off.
- `PC_SONGS_MAX_ROWS` is **60** (the order field has 62 usable positions; 63 marks "unplaced").

Checked with 60 rows (`RECOMP_TEST_SONG_CLONES=27`): the Beatbox lists all 60 and scrolls;
ticking a 36th song is refused; playing an unticked song plays it (36 on, playlist 36); playing
a further unticked one is refused; leaving the Beatbox; a One on One; no crash, saves restored
identically. Before the PlaySong limit, playing an unticked song with 36 on crashed in the
front end's script interpreter (`sub_001588F0`, calls to 0 and garbage), which is how its
36-entry list was found.

## 5c. Slices 3 and 4: the player's own songs (2026-11-06)

**Serving.** The game opens a song's files as loose files first (`D:\music\<id>\tune.mpf`, then
the archives), and that relative path reaches the game-file redirect. Converted songs are
served from `<data>/cache/songs/<id>/` that way: no archive is rebuilt.

**Straight** (`song_convert.c`): minitrack2's map, kept node for node. Its longest first-route
walk from a section start is the full play, 48 samples (intro 49-52, body 13-44, outro 53-60,
ending 45-48); the fights loop the body. The player's song is cut into 48 pieces along that walk
(other samples reuse pieces); each piece is an SCHl stream (the template's header with sample
count and rate patched, 2408-sample SCDl blocks, split channels, each with its two history words,
EA-XA frames, SCEl) aligned to 0x80 in main.mus, and the map's sample table gets its offset/0x80
and length in ms. Rate 44.1 kHz (holler shows the game plays it). A prototype of three minutes
put under `music/testsong1` played straight through in order (capture r 0.95-1.00, position
moving 5 s per 5 s); a converted MP3 the same (r 0.98-1.00).

**Interactive** (experimental): holler's map. Its fight sections branch four ways at every
segment: routes `00/1d`, `1d/4f`, `4f/78`, `78/7f` on a value of the game's land on the same
place in four lanes (the song; a 4-segment loop; two other mixes). The fight sections are
rotations of one body cycle (13-60, entered at 13, 29 or 45). Mapping: every section's lane-0
chain, longest first; a sample takes the place it first has (so rotations agree), each chain
spreads the whole song over its samples, and the other routes of a branching node inherit their
first route's place as lanes 1-3 (210 of holler's 216 samples placed; the rest reuse pieces).
Lane 0 is the song, lanes 1-3 the same song filtered (low-pass 1.2 kHz; +6 dB below 150 Hz;
+4 dB above 3.5 kHz) with a soft limit. Which game value picks the lane is not known. Checked:
the menu plays it in order (r 0.93) and a fight keeps its place (matched position advancing with
time, r 0.25-0.5 under the crowd).

### Fights play the song in order (2026-11-06)

Play-test (owner): a Straight song in a fight played 3-15 s, faded, and went on from somewhere
else; Interactive played well for a while, then the same. Cause: the fights do not walk the
menu's play. They jump between sections on events (the fight starting, Blazin, knock-downs) and
loop parts of the body, and every sample outside the menu's play held a piece taken in turn (or,
interactive, its own chain's spread of the whole song), so a jump landed on an unrelated piece.

The engine, from the lifted code (`recomp_0009.c`): `MEM32(0x3AD928)` is the active map
context (+0x34 the map, +0x38 its node offset table, +0x50 the sample table, +0x58 + 4t the
track states). `sub_001350E0(track)` plays the node at `s16 track+0x36`: it reads the node's
sample number and that sample's table entry (offset/0x80 into ebp, length into track+0x40/+0x44)
and starts the stream. Every advance (`sub_00135860`, after the router `sub_00134E70` picks a
route by `lo <= value <= hi`) and every event jump (`sub_00133E60`, via `sub_001355E0`) ends in
it, once per segment. `sub_00131080` sets up a context.

So the order is now kept at play time (`src/patches/song_cursor.c`), for custom songs only:
`sub_001350E0_enter` rewrites the table entry of the sample about to play with the next piece of
the song, in the lane the game chose. The converter writes the song as K equal pieces (the menu
play's length; the last padded with silence) once per lane in order, and `pieces.txt` lists them
and each sample's lane (`SONG_CONVERT_VERSION` 7). A cursor per song goes on through jumps and
through a map set up again in the same match (around Blazin), starts over when the song's map is
set up outside that match, and wraps at the song's end. In the menus the walk plays the pieces in
turn and ends with the song. `RECOMP_SONGS_SEGMENTS=1` turns the remap off for comparison.
Straight is now "the song, no filter" and Interactive "the song, with the game switching
filters"; both play end to end. Holler's map has 4-way routes at 334 nodes, but routes 2 and 3
(`1d/4f`, `4f/78`) mostly land on the same samples, so in practice the game picks between three
versions.

Checked (Free For All at the Terrordome, four minutes, `RECOMP_TEST_SONGS_ONLY` with the
interactive test song): the menu played pieces 0-26 on samples 85-111; the fight's map was set up
again and started at piece 0, and the fight played pieces 0-121 in order while the engine looped
sample 13, then 63-66, 213-216, 144-151 and 64 on its own, switching between lanes 0 and 1. The
straight song: pieces 0-5 in order on samples 49, 13, 14, 61, 62, 14.

**A node played again within half a piece gets the same piece.** The owner's play-test
(DMX, interactive, One on One) heard the song cut about 15 s two seconds into the fight: the log
shows the fight's map set up, pieces 0 and 1, then node 6 (sample 13) played 13 times in half a
second (`playtest-songs-20261107-095832.log.err`, frames 2486-2515), each call taking the next
piece: the song went from piece 1 to piece 14. A call that comes before the piece handed out last
has played half its length now gets that piece again (from its start), so a burst or a jump right
after a piece began repeats at most half a piece instead of skipping. Checked in a One on One: a
section jump 0.3 s into piece 27 replayed piece 27 in the new lane.

**Next song / from the start (shortcuts, 2026-11-07).** The playlist machine `sub_0003D060` runs
menus (audio mode `0x340374` 1) and fights (2). In state 2 (`0x340398`) it adds the frame time to
the timer `0x33E790` and, once that passes the song's length `0x33EC40`, moves the index
`0x33EC94` on (unless the hold byte `0x33EC90` is set), wraps it at the count `0x33E9DC` and loads
that song with its banner. A shortcut (`[songs] skip_pad`, `restart_pad`, `skip_key`,
`restart_key`; `src/hooks/pc_ui.cpp` reads them) asks `song_cursor_request`; once a frame on the
game's thread (`sub_00077040_enter`), while a song is playing (state 1, no song description
loading: `0x3407B4` not 1 or 2), it sets the timer past the length and state 2, with the index a
step back first for "from the start". A custom song loaded so starts at its first piece. Pads
cannot keep a button from the game in this runtime (only the overlay blanks them all), so the
D-pad press of the default chords reaches the game too; R3 does nothing in the game.

**The read buffer is sized from the sample table.** `sub_001317C0(flags, 3000)` (from
`sub_0001CBE0`, at boot and at a match's start) walks the active map's sample table and keeps the
largest `(next.offset - this.offset) * 0x80 / this.ms`; the music stream's ring is 3000 ms times
that. The game's own tables step through main.mus in order (about 47 bytes/ms, a ring of about
142 KB; their fight segments are all one size, holler's 103040 bytes). A first cut of this layout
named the pieces out of file order: the formula came to 29 MB, and the reads overran into a sound
bank (`0x813C6670`) and a stream object (crashes in `sub_00139F00` and `sub_0011EEF0`, found by
matching the overwritten words to main.mus). The first converter's tables were out of order too
(5-6 MB rings) and got away with it. Now sample i names piece i in file order, and samples past
the pieces get a second of silence each after them (a straight table whose last 16 entries named
one piece played nothing at all: the map loaded and main.mus was never opened), so every entry
steps forward and the formula sees the stream's own rate (unit test). When a song's map is set up
again over a table the remap changed, the hook puts the converted entries back first.

**Clip banks** (loading0/overlay0: `BNKl` v5, PT headers, EA-XA split mono channels at the
offsets in tags 0x88/0x89): kept from the template for now. Their rate is not in the header, and
correlating a clip against its song at 22.05-48 kHz found no clear match (r <= 0.29), so they
are not resampled from the player's song yet.

**Tags**: ID3v2.2-2.4 text frames, ID3v1, FLAC and Ogg Vorbis comments, WAV `LIST/INFO`, else
"Artist - Title" from the file name and the folder as album; text reduced to ASCII (accents
dropped) for the game's menu font. Read from a file's first 4 MB and last 128 bytes.

### Story fights name their songs (2026-11-05)

A Story fight (VY2 fixture, crib -> Map -> Club 357 -> Ice-T, `RECOMP_TEST_SONGS_ONLY=32` so
only Holler was on in the jukebox) built its playlist as **`og nuffrespect holler`**: "O.G.
Original Gangster" (Ice-T's own song) and "Nuff Respect" first, then the jukebox list. The
match setup (`sub_0001F2D0` / `sub_0001F7E0`) reads a primary and a second song from the match
as **Song record handles** (properties `0x92BF3967`, `0x60D0AE77`, resolved with
`sub_000BE760` and the Entity field) and falls back to `dirty` without one. Where the Story
keeps those handles is not found yet (no data table or front-end command names a song; the
fighter records do not hold them as plain fields). Battle fights carry neither: their playlist
is the jukebox list. The hard-coded 12-song list (`sub_0001E9C0`, `sub_0001FCF0`) has no caller
and no pointer to it in the image: dead code.

So the disc songs cannot be taken out of the game's song list for a session without changing
Story fight music; their handles must also keep their ranks (as the writer already ensures).
Owner, 2026-11-05: in that case, hold more songs at once instead (the jukebox rewrite).

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

2026-11-05:

- **Decoders**: vendor dr_mp3, dr_flac, dr_wav and stb_vorbis (thirdparty/audio-decoders).
- **More songs at once rather than a chosen 36 per launch**: Story fights name disc songs, so
  none can be taken out; the Beatbox list is rebuilt for 60 songs, 35 on at once.

Unknowns: the 12-song list, the `Song` integer field, how songs unlock, the bank sounds' exact roles, the
branch rules in licensed maps (which variable picks the 4-way route), and save-file offsets of the
jukebox bytes.
